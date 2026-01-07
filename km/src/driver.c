#include "../includes/globals.h"

// this function is not in official docs, but it exists
// it is advised to not use it since Microsoft might remove it\
// but for education purposes its good
NTKERNELAPI UCHAR* PsGetProcessImageFileName(PEPROCESS process);
PVOID reg_handle = NULL;

// we create a bridge between km and um, we tell windows if he sees DosDevices/Aegis switch it to /Device/Aegis
// normally /Device/Aegis is only visible to kernel in ring 0
// usermode CreateFile expects something like this "\\\\.\\Aegis" that means to it /DosDevices/Aegis and that will
// link to our /Device/Aegis
// DosDevices is kernels internal name
UNICODE_STRING device_name = RTL_CONSTANT_STRING(L"\\Device\\Aegis");
UNICODE_STRING symlink_name = RTL_CONSTANT_STRING(L"\\DosDevices\\Aegis");

void unload_driver(PDRIVER_OBJECT driver_object)
{
	if (reg_handle)
	{
		// unregister our callbacks and reset the handle
		ObUnRegisterCallbacks(reg_handle);
		reg_handle = NULL;
	}

	// unload and destroy
	protect_manager_destroy();

	// delete link and device for cleanup
	IoDeleteSymbolicLink(&symlink_name);
	IoDeleteDevice(driver_object->DeviceObject);

	DbgPrint("Aegis: Driver unloaded\n");
}

// we need to just return sucess on creation and closing of driver
// otherwise windows just fails cause the driver doesnt know how to open
// that will result in an invalid handle for our other control functions
NTSTATUS create_close(PDEVICE_OBJECT device_object, PIRP irp)
{
	UNREFERENCED_PARAMETER(device_object);

	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

// windows automatically calls this function before system processes any operation on an object
// in this case we are only looking for a specific operation and that is hadle creation
// if you want to read/write to memory of any other app on your pc, you need a HANDLE (ticket as we say)
// in this function we hook the process of Windows giving this ticket to the app who requested it
// and we nicely remove the permissions for read/write if its not a core windows app
OB_PREOP_CALLBACK_STATUS pre_open_process(PVOID reg_context, POB_PRE_OPERATION_INFORMATION info)
{
	UNREFERENCED_PARAMETER(reg_context);

	if (info->ObjectType == *PsProcessType) // check if its an actual process
	{
		PEPROCESS proc = (PEPROCESS)info->Object; // cast the object as an pointer to a process since we already checked if it is
		char* process_name = (char*)PsGetProcessImageFileName(proc); // here we use the undocumented function we declared above
		// it return us the .exe filename of the process that it wants the handle to (target handle)


		// as always since we are looping through our list, and we dont want BSOD, we are locking
		// our list and setting the KIRQL to level 2
		KIRQL irql;
		PLIST_ENTRY entry;
		KeAcquireSpinLock(&g_protect_manager->list_lock, &irql);
			for (entry = g_protect_manager->process_list.Flink; entry != &g_protect_manager->process_list; entry = entry->Flink)
			{
				pprocess proces = CONTAINING_RECORD(entry, process, entry); // check if our list has the requested process name in it
				if (strcmp(proces->path, process_name) == 0) // if not no point in protecting
				{
					if (info->Operation == OB_OPERATION_HANDLE_CREATE) // if the operation is not for creating a HANDLE (ticket) to our app then also no point
					{

						// this is not particularly needed but I found that if you dont check if for ex. notepad.exe is opening handle to notepad.exe, when you
						// open notepad, it sometimes bugs and the app will not open since the permission will be taken away
						HANDLE target_pid = PsGetProcessId(proces);
						HANDLE source_pid = PsGetCurrentProcessId();

						if (target_pid == source_pid)
						{
							KeReleaseSpinLock(&g_protect_manager->list_lock, irql); // we need to release our spinlock if we are exiting the callback since we might BSOD
							return OB_PREOP_SUCCESS;
						}


						// okay so we know that the operation is to open a new handle to another app
						// we also know that it is not the same app as the target app
						// so now we actually get the source process and its name through our undocumented function
						// and check if its also not a windows core process, if we block these processes, the user
						// is not able to open the app in the first place and we dont want that
						PEPROCESS source_process = IoGetCurrentProcess();
						char* source_name = (char*)PsGetProcessImageFileName(source_process);
						if (strcmp(source_name, "explorer.exe") == 0 || strcmp(source_name, "csrss.exe") == 0 || strcmp(source_name, "svchost.exe") == 0 || strcmp(source_name, "MsMpEng.exe") == 0 || strcmp(source_name, "RuntimeBroker.exe") == 0)
						{
							KeReleaseSpinLock(&g_protect_manager->list_lock, irql); // release as always
							return OB_PREOP_SUCCESS;
						}

						DbgPrint("Aegis: Blocked xx\n");

						// now we know that the app is actually not a core process and not the same
						// on a secure windows machine, there should not be any reason for another app to request
						// this handle, so we kindly revoke the access

						info->Parameters->CreateHandleInformation.DesiredAccess &= ~(0x1 | 0x10 | 0x20); // we dont want to set it to 0 because it can cause bugs
						// so we do a bit operation to just remove read/write from the current permissions
						// and release lock
						KeReleaseSpinLock(&g_protect_manager->list_lock, irql);

						return OB_PREOP_SUCCESS;
					}
				}
			}
			KeReleaseSpinLock(&g_protect_manager->list_lock, irql); // also dont forget to release here
	}
	return OB_PREOP_SUCCESS;
}


// manage requests from um, requests are made with codes defined in ioctl.h
NTSTATUS io_control(PDEVICE_OBJECT device_object, PIRP irp)
{
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	ULONG code = stack->Parameters.DeviceIoControl.IoControlCode; // we get the code sent from our user mode app
	NTSTATUS status = STATUS_SUCCESS;

	switch (code) // act based on code parsed from user mode
	{
		case IOCTL_PING:
		{
			DbgPrint("Aegis: Pong!\n"); // basic health check
			status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_PROTECT_PID:
		{
			ULONG len = stack->Parameters.DeviceIoControl.InputBufferLength; // we parse the length of our input buffer
			if (len < sizeof(ULONG)) { status = STATUS_INVALID_DEVICE_REQUEST; break; } // if something went wrong the input will be smaller than the size of ULONG

			char* exe_path = (char*)irp->AssociatedIrp.SystemBuffer; // we get the input buffer, in this case we know its an exe path
			if (len > 0) {
				exe_path[len - 1] = '\0';
				DbgPrint("Aegis: Prijaty text: %s\n", exe_path);
			}

			// create a new process (object) and add it to our protection list
			pprocess newproc = process_create(exe_path);
			if (newproc) protect_manager_protect_process(g_protect_manager, newproc);

			status = STATUS_SUCCESS;
			break;
		}

		default:
		{
			status = STATUS_INVALID_DEVICE_REQUEST;
		}
	}

	// if everything went good just pass on the status and finish the request
	irp->IoStatus.Status = status;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING reg_path)
{
	UNREFERENCED_PARAMETER(reg_path); // we dont need reg_path for now so state it as unreferenced

	NTSTATUS status;
	PDEVICE_OBJECT device_object; // device_object is like an api endpoint through which we communicate

	driver_object->DriverUnload = unload_driver; // define our unload function


	// try to create our endpoint device and save it to our object
	status = IoCreateDevice(
		driver_object,
		0,
		&device_name,
		FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN, // we use SECURE_OPEN so windows handles security of our driver (easiest way)
		FALSE,
		&device_object
	);

	if (!NT_SUCCESS(status)) return status; // if it fails we cant continue bcs we cant communicate we UM


	// now we create our link to use in user mode
	status = IoCreateSymbolicLink(&symlink_name, &device_name);

	if (!NT_SUCCESS(status))
	{
		IoDeleteDevice(device_object); // also if fails delete the device and we cant continue since no communication
		return status;
	}

	// initialize protect manager
	protect_manager_init(g_protect_manager); // DISCALIMER: this needs to be above all the major functions and callbacks, since it will BSOD, because for ex. our callback uses g_protectmanager
	// and before this function it is NULL
	// also forgot this and debugged this BSOD shit for 5 hours because of one line

	// set the our callbacks for create/close
	driver_object->MajorFunction[IRP_MJ_CREATE] = create_close;
	driver_object->MajorFunction[IRP_MJ_CLOSE] = create_close;

	// our io_control will handle all the device_control codes
	driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = io_control;


	// this is a basic structure to register a callback in windows
	// we zero the memory because of garbage and pass the data we need
	OB_CALLBACK_REGISTRATION ob_registration = { 0 };
	OB_OPERATION_REGISTRATION op_registration = { 0 };
	RtlSecureZeroMemory(&ob_registration, sizeof(ob_registration));
	RtlSecureZeroMemory(&op_registration, sizeof(op_registration));

	if (!PsProcessType) return STATUS_UNSUCCESSFUL;
	op_registration.ObjectType = PsProcessType;
	op_registration.Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE; // we only care about handle creation and handle duplication
	// we care about duplication because smart hackers or malware, often do not create handles but duplicate current ones to not arrouse suspiscion
	op_registration.PreOperation = pre_open_process; // we set our callback function
	op_registration.PostOperation = NULL;

	ob_registration.Version = OB_FLT_REGISTRATION_VERSION;
	ob_registration.OperationRegistrationCount = 1;
	ob_registration.RegistrationContext = NULL;
	ob_registration.OperationRegistration = &op_registration;

	RtlInitUnicodeString(&ob_registration.Altitude, L"321123"); // 321000 is standard for 3rd party drivers, we set it a bit higher to not have collision

	status = ObRegisterCallbacks(&ob_registration, &reg_handle);

	if (!NT_SUCCESS(status)) {
		DbgPrint("Aegis: OB callbacks FAILED 0x%X - continuing without\n", status);
		reg_handle = NULL;  // Continue aj bez OB!
	}
	else {
		DbgPrint("Aegis: OB callbacks OK\n");
	}

	DbgPrint("Aegis: Driver loaded and ready\n");

	return STATUS_SUCCESS;
}
#include "protect_manager.h"

// we initialize our global variable for manager
pprotect_manager g_protect_manager = NULL;

pprocess process_create(char* path)
{
	pprocess proc = ExAllocatePool(PagedPool, sizeof(process)); 
	// PagePool - if not used he can sleep on the disk to save resources
	// if we used NonPagePool - we tell it to be always in RAM, means quicker access at the cost of RAM usage
	// its also limited until the RAM runs out of memory

	if (!proc) return NULL;

	RtlZeroMemory(proc, sizeof(process)); // ExAllocatePool sets the memory to garbage we dont want
	// this sets the memory to 0x00 n times, for ex. if process is 24 bytes, it will set 24x 0x00

	// initialzie the pid and set it o active
	RtlCopyMemory(proc->path, path, strlen(path) + 1);
	proc->active = TRUE;

	return proc;
}

// our employee function that will sleep until we wake him up
// we wake him up by calling KeSetEvent for worker_event, he wakes up and sees if refresh is needed
VOID worker_thread(PVOID context)
{
	// since we know the context passed will be pointer to our protectmanager we can just cast it
	pprotect_manager self = (pprotect_manager)context;
	

	while (self->running)
	{
		KeWaitForSingleObject(&self->worker_event, Executive, KernelMode, FALSE, NULL);
		// we tell the employee to sleep until our bell rings, the bell is set to worker_event
		// we set the reason as Executive since we sleep because of work in kernel
		// set mode of sleep to KernelMode, so it sleeps as kernel thread and not user thread
		// FALSE means not alertable so just sleep do not disturb
		// and we set the timeout to NULL so just go into a straight up coma until we ring our bell

		if (self->refresh_needed)
		{
			// if we add pid we set refresh to true
			// here will lie all the logic that refreshes our process list to block
			DbgPrint("Aegis: Worker refreshing on new PID\n");
			self->refresh_needed = FALSE;
		}
	}

	// if we set running to false just terminate
	PsTerminateSystemThread(STATUS_SUCCESS);

	// fun fact: if you try to put anything below this its actually a dead code
	//           the function automatically calls return, and makes the thread dead so any code below this will not work
}

NTSTATUS protect_manager_init(pprotect_manager self)
{

	// DISCLAIMER: ExAllocatePool is deprecated, its advised to use ExAllocatePool2 since it automatically zeroes memory
	//			   since this is for education purposes, I decided to stick with this
	if (!self) {
		// check if we did not already initialize
		g_protect_manager = ExAllocatePool(NonPagedPool, sizeof(protect_manager)); // here we see a use case for NonPagedPool
		// since protect_manager is called far more often and needs to be easily accessed, we store it directly in RAM
		// we could use a PagedPool, but since we want speed, and also to showcase the use we stick with NonPagedPool

		if (!g_protect_manager) return STATUS_NO_MEMORY; // and here is the check if we even have enough memory in RAM
		// since NonPagedPool is limited we need to do this check for safety, or we might BSOD

		RtlZeroMemory(g_protect_manager, sizeof(protect_manager));
		// as always zero the memory since it had garbage
		// and set our passed pointer to actually be initialized

		self = g_protect_manager;
	}

	OBJECT_ATTRIBUTES obj_attr; // this is basically not needed, but better safe than sorry in kernel
	// we will just add OBJ_KERNEL_HANDLE so it knows its a kernel thread
	HANDLE thread_handle; // this will be our ticket to the thread, with it we can close, get thread object etc.

	// also a quick lesson stack vs heap
	// this variable thread handle is on the stack, its local, and will be destroyed after the function ends
	// since we explicitly allocated memory for our g_protect_manager above
	// that means that it is on the heap, and all its variables are on the heap too
	// variables and data on the heap, will survive until wi explicitly free the memory

	g_protect_manager = self; // we set our global var to be initialized aswell

	KeInitializeSpinLock(&self->list_lock); // spinlock or list_lock in this case, will basically lock our list while we edit the data
	// for example when we get request to add PID we need to lock our list so it does not change while we edit

	KeInitializeEvent(&self->worker_event, NotificationEvent, FALSE);
	// here we basically setup our worker event, NotificationEvent, means it can listen for ring bell from multiple threads
	// and we set the state to FALSE, since we are not signaling just sleep for now
	// we wake it up with KeSetEvent


	// before inicialization our list has garbage inside it
	// since this is a windows double-linked list, after initialization, it will point to itself
	InitializeListHead(&self->process_list);

	// here we set the kernel obj attribute as I said
	InitializeObjectAttributes(&obj_attr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

	// try to create the employee thread, we save our ticket to our handle, give it all access, our attributes
	// set the startup function to our worker function, and remember when we casted the context to pointer of protect_manager
	// we we pass it here at the end
	NTSTATUS status = PsCreateSystemThread(&thread_handle, THREAD_ALL_ACCESS, &obj_attr, NULL, NULL, worker_thread, self);

	if (NT_SUCCESS(status))
	{
		// if everyting goes well the thread is running
		// and we need to save our ticket to the heap, since thread_handle is on the stack and will disappear
		self->running = TRUE;
		self->refresh_needed = FALSE;
		self->worker_thread = thread_handle;
	}

	return status;
}

NTSTATUS protect_manager_protect_process(pprotect_manager self, pprocess process)
{
	// so KIRQL (Kernel Interrupt Request Level) is basically a number between 0-31 that tells the CPU
	// to do not disturb it
	/*
		0 = PASSIVE_LEVEL     (normálny beh)
		1 = APC_LEVEL
		2 = DISPATCH_LEVEL  
		3-15 = DIRQL (interrupty)
		31 = HIGH_LEVEL
	*/
	KIRQL irql;

	if (!process) return STATUS_INVALID_PARAMETER; // check if we have process passed as arg

	// acquirespinlock, save the current irql code to our variable and raises the KIRQL to level 2, DISPATCH_LEVEL
	// it locks the spinlock/list_lock so our list is not changing while we edit, and the cpu know to not disturb us
	KeAcquireSpinLock(&self->list_lock, &irql);
	// we insert the process entry into our list
	InsertTailList(&self->process_list, &process->entry);
	// and we release the lock and set the KIRQL back to its original state
	KeReleaseSpinLock(&self->list_lock, irql);

	// since we edited we need to refresh
	self->refresh_needed = TRUE;
	// and we need to finally wake up our thread to initialize our refresh
	// we pass it our worker event, our worker does not need priority so we set increment to 0
	// also KeSetEvent also raises the IRQL, if we set it to TRUE, it raises it and keeps it there so we need
	// again to reset it, since we are not doing any optimalizations after this we dont need that
	// if we set FALSE, it will auto revert it back to original state
	KeSetEvent(&self->worker_event, 0, FALSE);

	return STATUS_SUCCESS;
}

void protect_manager_destroy()
{
	// when closing the app its important to kill the process
	// if we dont do this when you click close you get BSOD
	// and yeah I forgot to add this when doing this, so I was greeted with a nice BSOD
	if (g_protect_manager && g_protect_manager->running)
	{
		g_protect_manager->running = FALSE;
		KeSetEvent(&g_protect_manager->worker_event, 0, FALSE); // wakeup for destroying

		ZwClose(g_protect_manager->worker_thread); // this is the important part
		// remember the ticket/handle we saved to the heap, here we return it to windows
		// if we dont we have a handle leak and a BSOD on unload

		g_protect_manager->worker_thread = NULL;
	}
}
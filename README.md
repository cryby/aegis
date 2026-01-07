# Aegis

Kernel-mode process protection driver using `ObRegisterCallbacks` to strip memory access permissions from handles. Basic implementation that hooks handle creation and removes `PROCESS_VM_READ/WRITE/OPERATION` from unauthorized access attempts.

<img src="https://i.imgur.com/oIJGLZi.png" width="600">

<img src="https://i.imgur.com/nq1Ofei.gif" width="600">

## Overview

Driver registers OB callbacks at altitude `321123` and intercepts `OB_OPERATION_HANDLE_CREATE` and `OB_OPERATION_HANDLE_DUPLICATE` operations. When a process tries to open a handle to a protected target, we check the source process name against a whitelist (explorer.exe, csrss.exe, svchost.exe, MsMpEng.exe, RuntimeBroker.exe) and strip access rights if it's not allowed.

Current implementation is name-based matching using `PsGetProcessImageFileName` (undocumented API). Protection list is managed via a linked list with spinlock synchronization. Worker thread exists but refresh logic is not implemented yet.

User-mode component provides ImGui GUI for adding processes to the protection list. Communication happens through IOCTL codes over `\\Device\\Aegis` symlink using `METHOD_BUFFERED` transfer. The driver creates a symlink from `\\DosDevices\\Aegis` to `\\Device\\Aegis` so user-mode can access it via `\\\\.\\Aegis`.

## Architecture

**Kernel (km/):**
- `driver.c` - DriverEntry, OB registration, IOCTL dispatcher, device/symlink creation
- `protect_manager/` - Process list management, worker thread (refresh logic pending)

**User-mode (um/):**
- `client.cpp` - Main loop, ImGui rendering, window management
- `gui/` - DirectX 11 + ImGui setup, custom fonts (Gilroy)
- `service_manager/` - SCM integration for driver loading/unloading
- `ioctl_manager/` - DeviceIoControl wrapper for driver communication

## Technical Details

### Kernel-Mode Implementation

- **OB Callbacks**: Registered with altitude `321123` (3rd party standard is 321000+, set higher to avoid collisions)
- **Process Identification**: Uses `PsGetProcessImageFileName` - undocumented API that returns just the .exe filename (max 15 chars)
- **Thread Safety**: `KSPIN_LOCK` operations at DISPATCH_LEVEL (IRQL 2) to prevent race conditions
- **Memory Management**: 
  - `NonPagedPool` for protect_manager (always in RAM, faster access, limited pool)
  - `PagedPool` for individual process structures (can be paged out)
- **Worker Thread**: System thread with `KEVENT` for async processing (currently stub)
- **IOCTL**: `METHOD_BUFFERED` for safe buffer handling, device type `0x8000` (3rd party range)

### User-Mode Implementation

- **GUI**: ImGui with DirectX 11 swapchain, custom styling
- **Driver Loading**: Service Control Manager API, creates service "AegisDriver" with `SERVICE_DEMAND_START`
- **Communication**: `DeviceIoControl` with IOCTL codes defined in shared `ioctl.h`
- **Error Handling**: Basic error display via MessageBox

### Code Examples

**OB Callback Registration:**
```c
// Register OB callbacks for handle creation/duplication
OB_CALLBACK_REGISTRATION ob_registration = { 0 };
OB_OPERATION_REGISTRATION op_registration = { 0 };
RtlSecureZeroMemory(&ob_registration, sizeof(ob_registration));
RtlSecureZeroMemory(&op_registration, sizeof(op_registration));

op_registration.ObjectType = PsProcessType;
op_registration.Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
// we care about duplication because smart hackers or malware, often do not create handles 
// but duplicate current ones to not arouse suspicion
op_registration.PreOperation = pre_open_process;

RtlInitUnicodeString(&ob_registration.Altitude, L"321123"); 
// 321000 is standard for 3rd party drivers, we set it a bit higher to not have collision

status = ObRegisterCallbacks(&ob_registration, &reg_handle);
```

**Handle Access Stripping:**
```c
OB_PREOP_CALLBACK_STATUS pre_open_process(PVOID reg_context, POB_PRE_OPERATION_INFORMATION info)
{
    if (info->ObjectType == *PsProcessType) {
        PEPROCESS proc = (PEPROCESS)info->Object;
        char* process_name = (char*)PsGetProcessImageFileName(proc);
        
        // Lock list at DISPATCH_LEVEL to prevent BSOD
        KIRQL irql;
        KeAcquireSpinLock(&g_protect_manager->list_lock, &irql);
        
        // Check if process is in protection list
        for (entry = g_protect_manager->process_list.Flink; 
             entry != &g_protect_manager->process_list; 
             entry = entry->Flink) {
            pprocess proces = CONTAINING_RECORD(entry, process, entry);
            if (strcmp(proces->path, process_name) == 0) {
                // Check source process - allow system processes
                PEPROCESS source_process = IoGetCurrentProcess();
                char* source_name = (char*)PsGetProcessImageFileName(source_process);
                if (strcmp(source_name, "explorer.exe") == 0 || 
                    strcmp(source_name, "csrss.exe") == 0 || ...) {
                    KeReleaseSpinLock(&g_protect_manager->list_lock, irql);
                    return OB_PREOP_SUCCESS;
                }
                
                // Strip VM_READ, VM_WRITE, VM_OPERATION (0x1, 0x10, 0x20)
                // We don't set to 0 because it can cause bugs
                info->Parameters->CreateHandleInformation.DesiredAccess &= ~(0x1 | 0x10 | 0x20);
                KeReleaseSpinLock(&g_protect_manager->list_lock, irql);
                return OB_PREOP_SUCCESS;
            }
        }
        KeReleaseSpinLock(&g_protect_manager->list_lock, irql);
    }
    return OB_PREOP_SUCCESS;
}
```

**Process List Management:**
```c
NTSTATUS protect_manager_protect_process(pprotect_manager self, pprocess process)
{
    // KIRQL (Kernel Interrupt Request Level) 0-31 tells CPU to not disturb
    // 0 = PASSIVE_LEVEL, 2 = DISPATCH_LEVEL, 31 = HIGH_LEVEL
    KIRQL irql;
    
    // Acquire spinlock, raises IRQL to DISPATCH_LEVEL (2)
    // Locks the list so it doesn't change while we edit
    KeAcquireSpinLock(&self->list_lock, &irql);
    InsertTailList(&self->process_list, &process->entry);
    KeReleaseSpinLock(&self->list_lock, irql);
    
    // Signal worker thread to refresh
    self->refresh_needed = TRUE;
    KeSetEvent(&self->worker_event, 0, FALSE); // FALSE = auto revert IRQL
    return STATUS_SUCCESS;
}
```

**User-Mode IOCTL Communication:**
```cpp
bool ioctl_manager::check(std::string name)
{
    // Open driver device via symlink \\\\.\\Aegis -> \\Device\\Aegis
    this->driver_object = CreateFile(this->path, GENERIC_READ | GENERIC_WRITE, 
                                      0, NULL, OPEN_EXISTING, 0, NULL);
    
    // Send process name to driver via IOCTL
    result = DeviceIoControl(this->driver_object, IOCTL_PROTECT_PID, 
                            (LPVOID)data.c_str(), data.size() + 1, 
                            NULL, 0, &bytes_returned, NULL);
    
    CloseHandle(this->driver_object); // don't forget to close handles
    return result;
}
```

All code is thoroughly commented with explanations of kernel concepts, IRQL levels, memory management, and potential pitfalls. Feel free to check out the source - it's educational and well-documented.

## Build Requirements

- Visual Studio with C++ workload
- Windows Driver Kit (WDK)
- Test signing enabled (`bcdedit /set testsigning on`)

Build outputs: `km.sys` (driver) and `um.exe` (user-mode app).

## Known Issues / Limitations

- **No kernel-mode protection** - Any KM driver can bypass this by directly accessing EPROCESS
- **Name-based matching only** - Process name spoofing is possible, no PID validation
- **Hardcoded whitelist** - System process list is incomplete and static
- **Handle duplication** - Only handles `OB_OPERATION_HANDLE_CREATE`, duplication attacks possible
- **Worker thread incomplete** - Refresh logic in `protect_manager.c` is empty
- **Deprecated APIs** - Uses `ExAllocatePool` instead of `ExAllocatePool2`

## TODO

- **DKOM implementation** - Process hiding via unlinking from `PsActiveProcessHead`, driver hiding from `PsLoadedModuleList`
- **Thread protection** - Extend OB callbacks to protect thread handles
- **PID-based protection** - Add process ID validation instead of relying solely on names
- **Unprotect functionality** - Add IOCTL to remove processes from protection list
- **Complete worker thread** - Implement actual refresh logic for process list updates

## Disclaimer

Educational purposes only. Uses undocumented APIs that may break with Windows updates. Requires test signing or proper code signing to run.

#pragma once


// device type (3rd party drivers go for 0x8000 or above
#define FILE_DEVICE_AEGIS 0x8000

// ioctl code for ping
// buffered method for safety
#define IOCTL_PING CTL_CODE(FILE_DEVICE_AEGIS, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

// ioctl code for sending pids to protect
#define IOCTL_PROTECT_PID CTL_CODE(FILE_DEVICE_AEGIS, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)
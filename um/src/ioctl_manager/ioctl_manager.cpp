#include "ioctl_manager.h"
#include <stdio.h>

ioctl_manager::ioctl_manager(LPCWSTR path)
{
	this->path = path;
}

bool ioctl_manager::check(std::string name)
{
	// in our driver we created a symlink to a path defined in client.cpp
	// here we open the connection by opening a file on this symlink path
	this->driver_object = CreateFile(this->path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (this->driver_object == INVALID_HANDLE_VALUE) // check if anything went wrong
	{
		auto err = GetLastError();
		char szerr[128];
		sprintf_s(szerr, "%d", err);
		MessageBoxA(NULL, szerr, "Invalid Handle", MB_OK | MB_ICONERROR);

	}

	std::string data = name;

	BOOL result = FALSE;
	DWORD bytes_returned;
	result = DeviceIoControl(this->driver_object, IOCTL_PROTECT_PID, (LPVOID)data.c_str(), data.size() + 1, NULL, 0, &bytes_returned, NULL); // send our IOCTL code for protecting, with the data parsed to us from the arguments
	
	if (!result) {
		auto err = GetLastError();
		char szerr[128];
		sprintf_s(szerr, "%d", err);
		MessageBoxA(NULL, szerr, "Error", MB_OK | MB_ICONERROR);
	}
	
	CloseHandle(this->driver_object); // dont forget to close handles to avoid memory leaks
	return (result);
}
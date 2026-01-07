#include "service_manager.h"
#include <string>

// usermode manager to create and start up our kernel driver
service_manager::service_manager(LPCWSTR path)
{
	// we create a handler for service manager to be aple to start drivers, we only require create service rights
	this->manager_handle = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);

	if (!this->manager_handle)
	{
		MessageBoxW(NULL, L"Manager not found!", L"Success", MB_OK | MB_ICONERROR);
		exit(1);
	}

	// create service with full access, give basic desc and path to our driver from arguments of constructor
	this->service_handle = CreateService(
		this->manager_handle,
		L"AegisDriver",
		L"Aegis Security Driver",
		SERVICE_ALL_ACCESS,
		SERVICE_KERNEL_DRIVER,
		SERVICE_DEMAND_START,
		SERVICE_ERROR_IGNORE,
		path,
		nullptr, nullptr, nullptr, nullptr, nullptr
	);

	// if exists only open and not create
	if (!this->service_handle && GetLastError() == ERROR_SERVICE_EXISTS)
	{
		this->service_handle = OpenService(
			this->manager_handle,
			L"AegisDriver",
			SERVICE_ALL_ACCESS
		);
	}

	if (!this->service_handle) 
	{
		auto err = GetLastError();
		char szerr[128];
		sprintf_s(szerr, "%d", err);
		MessageBoxA(NULL, szerr, "Error", MB_OK | MB_ICONERROR);
		exit(1);
	}

	// if everything succeeds start service

	if (!StartService(service_handle, 0, nullptr))
	{
		DWORD err = GetLastError();

		if (err != ERROR_SERVICE_ALREADY_RUNNING)
		{
			std::wstring msg = L"Start Error: " + std::to_wstring(err);
			MessageBoxW(NULL, msg.c_str(), L"Error", MB_OK | MB_ICONERROR);
			exit(1);
		}
	}
}


service_manager::~service_manager()
{
	// cleanup and close handles for services
	if (this->service_handle)
	{
		SERVICE_STATUS status;
		ControlService(this->service_handle, SERVICE_CONTROL_STOP, &status);
		DeleteService(this->service_handle);
		CloseServiceHandle(this->service_handle);
	}

	if (this->manager_handle) CloseServiceHandle(this->manager_handle);
}
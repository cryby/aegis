#pragma once
#include "../../includes/globals.h"
#include <string>

class ioctl_manager
{
	private:
		HANDLE driver_object;
		LPCWSTR path;

	public:
		ioctl_manager(LPCWSTR path);
		bool check(std::string name);
};
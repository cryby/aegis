#pragma once
#include "../../includes/globals.h"

class service_manager
{
	private:
		SC_HANDLE manager_handle;
		SC_HANDLE service_handle;

	public:
		service_manager(LPCWSTR path);
		~service_manager();
};
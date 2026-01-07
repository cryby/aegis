#ifndef PROTECT_MANAGER_H
#define PROTECT_MANAGER_H

#include "../../includes/globals.h"


// data for our process (object)
typedef struct _process
{
	LIST_ENTRY entry;
	char path[260];
	BOOLEAN active;
	ULONG pid;
} process, *pprocess;


typedef struct _protect_manager
{
	LIST_ENTRY process_list; // list of all the processes currently under our control 
	BOOLEAN refresh_needed;
	HANDLE worker_thread; // this is our employee basically, he will wait until we wake him up and then manage our request
	PVOID ob_callback_handle; // for registering our callback
	BOOLEAN running;
	KSPIN_LOCK list_lock; // thread safety, secures when protect/unprotect
	KEVENT worker_event; // signaling to wake up worker thread
} protect_manager, *pprotect_manager;


pprocess process_create(char* path);
void process_destroy(pprocess process);


NTSTATUS protect_manager_init(pprotect_manager self);
NTSTATUS protect_manager_protect_process(pprotect_manager self, pprocess process);
void protect_manager_destroy();

#endif
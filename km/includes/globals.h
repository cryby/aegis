#pragma once
#include <ntifs.h>
#include "../includes/ioctl.h"
#include "../src/protect_manager/protect_manager.h"

typedef struct _protect_manager protect_manager, * pprotect_manager;
extern pprotect_manager g_protect_manager;
#ifndef __MC_WATCHDOG_H_
#define __MC_WATCHDOG_H_
#include "mc_base.h"

int mc_watchdog_init(int timeout);
int mc_watchdog_feed();
void mc_watchdog_exit();
int mc_feedwd(struct ipc_timing *timing);
#endif


#ifndef __MC_GUARD_H__
#define __MC_GUARD_H__
#include "mc_base.h"
#define MC_GUARD_STATE_NEW 		0
#define MC_GUARD_STATE_RUN 		1
#define MC_GUARD_STATE_DETACH 	2
struct mc_guard * mc_guard_search(const char *name);
void mcd_guard_init(struct mc_struct *mc);
int mcd_guard_run(struct mc_struct *mc);
int mcd_guard_start(struct mc_struct *mc);
int mcd_guard_delay(struct ipc_timing *timing);
#endif

#ifndef __MC_GUARD_H__
#define __MC_GUARD_H__
#include "ipc_server.h"
struct mc_guard
{
	int 		id; /* Defined in enum MC_STATIC_GUARD */
	pid_t 		pid;
	const char *name;
	const char *cmdline; 
	struct list_head list;
};
pid_t mc_process_find(const char *name);
void mc_guard_scanning(struct list_head *guard_list);
struct mc_guard * mc_guard_search(const char *name);
int mc_guard_find(struct mc_guard *guard);
int mc_guard_inspect(struct mc_guard *guard);
int mc_guard_dynamic_append(const char *guard_conf, struct list_head *guard_list);
void mc_guard_init(struct list_head *guard_list);
int mc_guarding(struct ipc_timing *timing);
int mc_guard_delay(struct ipc_timing *timing);
#endif

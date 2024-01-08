#ifndef __MC_CLIENT_H__
#define __MC_CLIENT_H__
#include <unistd.h>
#include <sys/syscall.h>
#include "mc_define.h"
#define mc_gettid()					syscall(__NR_gettid)

int mc_client_register(unsigned long mask, struct mc_reginfo *info, 
	int (*fn)(int, void *, int, void *), 
	void *arg);
/*
 * @state: 1:detach 0:attach
 */
int mc_client_detach(int identity, int state);
int mc_client_heartbeat();
int mc_client_ready();
int mc_client_ready_test(int id, struct mc_ready *rdy);
int mc_client_exeception(int eid, void *einfo, unsigned int elen);
int mc_client_sleep_syn(int tmo);
int mc_client_sleep_ack(int identity);
int mc_client_resume_syn(int source, int tmo);
int mc_client_resume_ack(int identity);
int mc_client_reboot_syn(int tmo);
int mc_client_reboot_ack(int identity);
int mc_client_network_syn(int status);
int mc_client_apply_sleep(int tmo);
int mc_client_apply_reboot(int tmo);
int mc_client_vote_sleep(int identity, int approved);
int mc_client_vote_reboot(int identity, int approved);
int mc_client_reboot(int poweroff, int block, int wait_tmo, const char *reason);
#endif

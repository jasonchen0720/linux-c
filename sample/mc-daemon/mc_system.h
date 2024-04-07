#ifndef __MC_SYSTEM_H__
#define __MC_SYSTEM_H__
#include "mc_base.h"
void mc_reboot_system(struct mc_struct *mc, int force);
void mc_pwroff_system(struct mc_struct *mc, int force);
int mcd_client_syn_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie);
int mcd_client_ack_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie);
int mcd_client_apply_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie);
int mcd_client_vote_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie);
int mcd_client_reboot_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie);
#endif
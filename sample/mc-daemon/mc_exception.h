#ifndef __MC_EXCEPTION_H__
#define __MC_EXCEPTION_H__
#include "mc_base.h"


int mc_exception_test(int e, struct mc_einfo *info);
int mc_exception_test_only(int e, struct mc_einfo *info);
int mcd_client_exception_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie);

#endif

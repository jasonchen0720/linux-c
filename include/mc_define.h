#ifndef __MC_DEFINE_H__
#define __MC_DEFINE_H__
#include "generic_bit.h"
#include "mc_identity.h"
#define MC_SERVER		"IPC_MC"

#define MC_CLIENT_NAME_LEN		31
#define MC_CLIENT_CMDLINE_LEN	127

#define MC_GUARD_MASK 		1
#define MC_SYNC_MASK 		2
#define MC_SYSTEM_MASK 		4
#define MC_EXCEPTION_MASK	8
enum MC_MSG_ID {
	MC_MSG_PROBE,
	MC_MSG_READY,
	MC_MSG_EXIT,
	MC_MSG_HEARTBEAT,
	MC_MSG_DETACH,
	MC_MSG_EXCEPTION,
	
	MC_MSG_SYN,
	MC_MSG_ACK,
	
	MC_MSG_APPLY,
	MC_MSG_VOTE,
	MC_MSG_REBOOT,
	MC_MSG_RESTART,
};

#define MC_STRING_TRUE 		"true"
#define MC_STRING_FALSE 	"false"
struct mc_val
{
	int value;
};
struct mc_evtval
{
	int value;
};
enum MC_RESUME_SOURCE
{
	MC_RESUME_SMS,
	MC_RESUME_RTC,
	MC_RESUME_KEY,
	MC_RESUME_SENSOR,
	MC_RESUME_OTHER,
};
enum MC_SYN_ID
{
	MC_SYN_SLEEP,
	MC_SYN_RESUME,
	MC_SYN_REBOOT,
};
struct mc_syn
{
	int		synid;
	int 	timeout;
	char 	sponsor[MC_CLIENT_NAME_LEN + 1];
	int		data_len;
	char	data[0];
};
#define mc_syn_len(syn) (sizeof(struct mc_syn) + (syn)->data_len)
enum MC_SYN_CODE
{
	MC_SYN_DONE,
	MC_SYN_ERR,
	MC_SYN_TMO,
};
struct mc_synrsp
{
	int code; /* Defined in enum enum MC_SYN_CODE */
};
struct mc_ack
{
	int		synid;
	int 	identity;
	char	sponsor[MC_CLIENT_NAME_LEN + 1];
};
enum MC_APPLY_ID
{
	MC_APPLY_SLEEP,
	MC_APPLY_REBOOT,
};
struct mc_apply 
{
	int		applyid;
	int 	timeout;
	char 	sponsor[MC_CLIENT_NAME_LEN + 1];
};
enum MC_APPLY_RET
{
	MC_APPLY_R_UNAPPROVED,
	MC_APPLY_R_APPROVED,
	MC_APPLY_R_ERR,
	MC_APPLY_R_TMO,
};
struct mc_applyrsp 
{
	/* Valid values defined in enum MC_APPLY_RET */
	int approved;
};
struct mc_vote
{
	int		applyid;
	int 	approved;
	int		identity;
	char 	sponsor[MC_CLIENT_NAME_LEN + 1];
};
struct mc_detach
{
	int state;
	int exit;
	int guard_id;
	char name[MC_CLIENT_NAME_LEN + 1];
};
enum MC_IND_ID {
	/* Below indications covered by MC_GUARD_MASK */
	MC_IND_PROBE,
	MC_IND_EXIT,

	/* Below indications covered by MC_SYNC_MASK */
	MC_IND_SYNC_READY,
	MC_IND_SYNC_DEAD,
	MC_IND_SYNC_RESTART,

	/* Below indications covered by MC_SYSTEM_MASK */
	MC_IND_SYS_REBOOT,
	MC_IND_SYS_RSTMODEM,
	MC_IND_SYS_SLEEP,
	MC_IND_SYS_WAKEUP,
	MC_IND_SYS_NETWORK,
	MC_IND_SYS_VOTE_SLEEP, 	/* vote */
	MC_IND_SYS_VOTE_REBOOT, /* vote */
	MC_IND_SYS_SHUTDOWN
};
struct mc_tskinfo
{
	/* Thread PID */
	int tid;
	int strategy;
	int interval;
};
enum MC_STRATEGY
{
	MC_STRATEGY_RESTART		= 16,
	MC_STRATEGY_RSTMODEM,
	MC_STRATEGY_REBOOT,
};
struct mc_reginfo
{
	/* Process ID, On linux pid_t */
	int		pid;
	/* Process booting time */
	int latch_time;
	/* Task count */
	int		count;
	/* Always put Process' name */
	char	name[MC_CLIENT_NAME_LEN + 1];
	/* Process' starting command */
	char 	cmdline[MC_CLIENT_CMDLINE_LEN + 1];
	struct mc_tskinfo tasks[];
};
struct mc_heartbeat
{
	/* Process ID */
	int pid;
	/* Thread ID */
	unsigned long tid;
};
struct mc_probe 
{
	long time;
};
struct mc_ready
{
	unsigned long bitmap[BITS_TO_LONGS(MC_IDENTITY_MAX)];
	char name[MC_CLIENT_NAME_LEN + 1];
};
enum MC_EXCEPTIONS 
{
	MC_EXCEPTION_MIN		= -1,
	MC_EXCEPTION_CPU_USAGE	= 0,	/* Excessive usage of CPU */
	MC_EXCEPTION_MEM_USAGE,			/* Excessive usage of Memory */
	MC_EXCEPTION_CPU_TEMP,			/* Temperature of cpu too high */
	MC_EXCEPTION_DISK_USAGE,  		/* Excessive usage of disk */
	MC_EXCEPTION_NONE_CONN,			/* None connection for long time */
	MC_EXCEPTION_MAX,
};
#define MC_EXCEPTION_R_SIZE	1024 /* struct mc_exception recommended max size */
struct mc_exception 
{
	int 			eid; /* Exeception defined in enum MC_EXECEPTIONS */
	unsigned int 	len;
	unsigned char	info[];
};
#endif

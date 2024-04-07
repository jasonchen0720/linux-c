#ifndef __MC_BASE_H__
#define __MC_BASE_H__
#include <pthread.h>

#include "ipc_server.h"

#include "mc_log.h"
#include "mc_utils.h"
#include "mc_define.h"
#include "mc_config.h"

#include "generic_util.h"

#define MC_BITMAP_SIZE	2 /* 32-Bit-System: 64 clients */
#define MC_CLIENT_MAX 	(MC_BITMAP_SIZE * BITS_PER_LONG)

/*
 * MC exception info.
 */
struct mc_einfo
{
	int			  count[MC_EXCEPTION_MAX];
	unsigned long exception[BITS_TO_LONGS(MC_EXCEPTION_MAX)];
};
struct mc_timers
{
	/* Watchdog feeding */
	struct ipc_timing feedwd_timing;
	/* Heartbeat checking interval */
	struct ipc_timing patrol_timing;
	/* Probe interval */
	struct ipc_timing probe_timing;
	/* Static guard checking */
	struct ipc_timing guard_timing;
	/* Used for Modem reset */
	struct ipc_timing online_timing;
	/* Auto reboot */
	struct ipc_timing auto_reboot_timing;
};
struct mc_task
{
	unsigned long tid;
	int 	interval;
	int 	strategy;
	int 	flags;
	long	expire;
 	struct mc_client	*client;
	struct list_head 	list;
};
struct mc_synchor
{
	long flags;
	unsigned long 	ack_bitmap[MC_BITMAP_SIZE];
	pthread_cond_t	syn_cond;
};
struct mc_referee
{
	long flags;
	unsigned long 	vote_bitmap[MC_BITMAP_SIZE];
	unsigned long 	vote_approved[MC_BITMAP_SIZE];
	pthread_cond_t	vote_cond;
};
struct mc_struct
{
	int  flags;
	int  subscribers;
	long probe_time;

	/* Bitmap used to mark available index allocated to struct mc_subscriber->index */		
	unsigned long 	index_stock[MC_BITMAP_SIZE];


	unsigned long   full_bitmap[MC_BITMAP_SIZE];
	
	
	struct mc_synchor	*sync_sleep;
	struct mc_synchor	*sync_resume;
	struct mc_synchor	*sync_reboot;
	
	struct mc_referee	*vote_sleep;
	struct mc_referee	*vote_reboot;
		
	struct mc_einfo		*einfo;
	
	struct mc_ready		*ready;
	struct mc_config 	*config;
	struct mc_timers	*timers;
	
	/* Dead Client list */
	struct list_head client_deadq;
	struct list_head client_lossq;
	/* Working Client list */
	struct list_head client_runningq;
	struct list_head client_detachdq;
	/* All registered active threads list */
	struct list_head task_head;

	struct list_head guard_head;
	pthread_mutex_t 	mutex;	
	pthread_condattr_t  cattr;
};
struct mc_guard
{
	int 		id; /* Defined in enum MC_STATIC_GUARD */
	int 		state;
	pid_t 		pid;
	const char *name;
	const char *cmdline; 
	const char *pidfile;
	struct list_head list;
};
struct mc_client
{		
	int 	state;
	int		flags;
	long 	probe_expire;
	long	birth_time;
	/* Register info: process'id */
	pid_t pid;
	/* Register info: task count */
	int count;
	/* Register info: process booting time */
	int latch_time;
	/* Register info: process'name */
	char name[MC_CLIENT_NAME_LEN + 1];
	/* Register info: Cmdline for starting */
	char cmdline[MC_CLIENT_CMDLINE_LEN + 1];
	
	struct mc_guard *guard;
	
	struct ipc_timing restart_timing; /* Delay timer for restarting */
	struct mc_task 	   *tasks;
	struct mc_subscriber *subscriber;
	struct list_head list;
};

struct mc_subscriber 
{
	int cookie_type;		/* IPC cookie type */
	int index;
	
	const struct ipc_server *sevr;
	
	union {
		/* Registrant's name */
		const char *name;
		/* Client needing to be guarded, mask of MC_GUARD_MASK should be included */
		struct mc_client *client;
	};
	
	struct mc_struct *mc;
};

enum MC_IPC_COOKIE_TYPES {
	MC_IPC_COOKIE_REGONLY 	= 0,
	MC_IPC_COOKIE_CLIENT 	= 1,
};
enum MC_F_BITS 
{
	MC_F_MODEM_RST = 0,		/* Flag: Indicate modem resetting is in progress. */
	MC_F_POWER_OFF,
	MC_F_REBOOT,
	MC_F_SHUTDOWN,
	MC_F_GUARD_RUN,
	MC_F_MAX = 31,
};
enum {
	AF_BUSY,		/* Flag: Indicate action is in progress. */
	AF_WAIT,		/* Flag: Indicate Action is waiting to be done. */
	AF_DONE,		/* Flag: Indicate Action is done. */
};
enum MC_EVTS {
	MC_EVENT_REBOOT		= 100,
	MC_EVENT_SHUTDOWN,
};
#define list_move(node, head) do {\
		list_del(node);\
		list_add_tail(node, head);\
	} while (0)
#define get_client_from_cookie(cookie)	(((struct mc_subscriber *)cookie)->client)
#define is_bitmap_full(bitmap, mc)		(!memcmp(bitmap, (mc)->full_bitmap, sizeof((mc)->full_bitmap)))
void mc_event(int event);
#endif

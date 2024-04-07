#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include "mc_guard.h"
#include "mc_identity.h"
#include "mc_guard_table.h"
#include "generic_proc.h"
#include "generic_file.h"
#define LOG_TAG "guard"
static const int __mc_guard_count = ARRAY_SIZE(__mc_guard);
struct mc_guard * mc_guard_search(const char *name)
{
    int l,r,m;
	int cmp;
	int n = __mc_guard_count;
	for (l = 0, r = n -1; l <= r;) {
		m = (l + r) >> 1;				
		cmp = strcmp(name, __mc_guard[m].name);
		if (cmp < 0)
			r = m - 1;
		else if(cmp > 0)
			l = m + 1;
		else
			return &__mc_guard[m];

	}
	return NULL;
}
static int guard_scan(const char *name, pid_t pid, void *arg)
{
	struct mc_guard *guard = mc_guard_search(name);
	if (guard) {
		LOGI("Scanned static process:%s pid:%d", name, pid);
		goto found;	
	} 
	struct list_head *guard_list = arg;
	list_for_each_entry_reverse(guard, guard_list, list) {
		if (guard->id != MC_IDENTITY_DYNAMIC)
			break;
		if (!strcmp(name, guard->name)) {
			LOGI("Scanned dynamic process:%s pid:%d", name, pid);	
			goto found;
		}
	}
	return 0;
found:
	if (guard->pid == 0) {
		guard->pid = pid;
		guard->state = MC_GUARD_STATE_RUN;
	} else LOGW("Scanned duplicated process:%s pid:%d / %d", name, guard->pid, pid);
	return 0;
}
static void mc_guard_scan(struct list_head *guard_list)
{
	for_each_process(guard_scan, guard_list);
}

static int mc_guard_find(struct mc_guard *guard)
{
	pid_t pid;
	if (guard->pidfile) {
		if ((pid = sys_file_read_integer(guard->pidfile, 0)) > 0) {
			if (mc_process_validate(guard->name, pid)) {
				LOGI("Process: %s, found pid: %d from pidfile: %s", guard->name, pid, guard->pidfile);
				guard->pid = pid;
				return 1;
			}
		}
	#if 0
		return 0;
	#endif
	}  
	pid = process_find(guard->name);
	
	if (pid > 0) {
		LOGI("Found Process:%s, pid:%d", guard->name, pid);
		guard->pid = pid;
		return 1;
	} else if (pid == 0) {
		LOGE("Not Found Process:%s", guard->name);
		return 0;
	} else {
		LOGE("Find Process error");
		return -1;
	}
}

#define mc_guard_skip_spaces(string) do { while (*string == ' ' || *string == '\t') string++; } while (0)
static int mc_guard_conf_append(const char *guard_conf, struct list_head *guard_list)
{
	char buf[(MC_CLIENT_CMDLINE_LEN + 1) * 2 + 256] = {0};
	char *name, *cmdline, *pidfile, *p;
	FILE *fp = NULL;
	fp = fopen(guard_conf, "r");
	if (fp == NULL) {
		LOGE("dynamic guard list open:%d.", errno);
		return -1;
	}
	struct mc_guard *guard = NULL;
	
	while (fgets(buf, sizeof(buf), fp)) {
		name = buf;
		mc_guard_skip_spaces(name);
		if ((cmdline = strchr(name, ' ')) == NULL && 
			(cmdline = strchr(name, '\t')) == NULL)
			continue;
		
		*cmdline++ = '\0';
		mc_guard_skip_spaces(cmdline);
		for (pidfile = NULL, p = cmdline; *p; p++) {
			if (*p == '\n' || *p == '\r') {
				*p = '\0';
				break;
			}
			if (*p == '\t') {
				*p = '\0';
				pidfile = p + 1;
			}
		}

		if (*cmdline == '\0')
			continue;
			
		guard = (struct mc_guard *)calloc(1, sizeof(struct mc_guard));
		if (!guard) 
			goto err;

		guard->name = strdup(name);
		if (!guard->name)
			goto err;
		
		guard->cmdline= strdup(cmdline);
		if (!guard->cmdline)
			goto err;

		guard->pidfile = NULL;
		if (pidfile) {
			mc_guard_skip_spaces(pidfile);
			if (*pidfile) {
				guard->pidfile = strdup(pidfile);
				if (!guard->pidfile)
					goto err;
			}
		}
		guard->state = MC_GUARD_STATE_NEW;	
		guard->id 	= MC_IDENTITY_DYNAMIC;
		guard->pid	= 0;
		LOGI("dynamic guard:%s, cmdline:%s, pidfile:%s", guard->name, guard->cmdline, 
			guard->pidfile ? guard->pidfile : "null");
		list_add_tail(&guard->list, guard_list);
	}
	fclose(fp);
	return 0;
err:
	if (guard) {
		if (guard->name)
			free((void *)guard->name);
		if (guard->cmdline)
			free((void *)guard->cmdline);
		if (guard->pidfile)
			free((void *)guard->pidfile);
		free(guard);
	}
	fclose(fp);
	return -1;
}

void mcd_guard_init(struct mc_struct *mc)
{
	int i;
	for (i = 0; i < __mc_guard_count; i++) {
		__mc_guard[i].pid 	= 0;
		__mc_guard[i].state = MC_GUARD_STATE_NEW;
		list_add_tail(&__mc_guard[i].list, &mc->guard_head);
	}
#if 0
	if (mc->config->guard_delay > 0) {
		ipc_timing_init(&mc->timers->guard_timing, 0, 
						 mc->config->guard_delay, 0, 
						 mc, mc_guard_delay);
		assert(ipc_timing_register(&mc->timers->guard_timing) == 0);
	}
#else
	/*
	 * In case of reentrant calling of ipc_timing_register/unregister.
	 */
	INIT_LIST_HEAD(&mc->timers->guard_timing.list);
#endif
}

static int mcd_guard(struct ipc_timing *timing)
{
	struct mc_struct *mc = timing->arg;
	if (mc->flags & (BIT(MC_F_REBOOT) | BIT(MC_F_SHUTDOWN))) {
		LOGW("Reboot in progress.");
		return 0;
	}
	struct mc_guard *guard;
	list_for_each_entry(guard, &mc->guard_head, list) {
		switch (guard->state) {
		case MC_GUARD_STATE_NEW:
			if (guard->pid > 0 && mc_process_validate(guard->name, guard->pid)) {
				guard->state = MC_GUARD_STATE_RUN;
				continue;
			}
			LOGW("process %s[%d], daemon / attach / initial", guard->name, guard->pid);
			break;
		case MC_GUARD_STATE_RUN:
			if (mc_process_validate(guard->name, guard->pid))
				continue;
			LOGE("process %s[%d], validate failure", guard->name, guard->pid);
	#if 1
			guard->pid = mc_process_restart(guard->name, guard->cmdline);
			guard->state = MC_GUARD_STATE_NEW;
			continue;
	#else
			break;
	#endif
		case MC_GUARD_STATE_DETACH:
			continue;
		default:
			assert(0);
		}
		/*
		 * For pid = 0, initial state.
		 * For pid > 0, MC_GUARD_STATE_NEW
		 * Try to find the pid of the process first
		 */
		if (mc_guard_find(guard) == 0) {
			LOGI("Restarting guard:%s.", guard->name);
			guard->pid = mc_process_latch(guard->cmdline);
			guard->state = MC_GUARD_STATE_NEW;
		}
		
	}
	return 0;
}
int mcd_guard_run(struct mc_struct *mc)
{
	mc_guard_conf_append(mc->config->guard_conf, &mc->guard_head);
	mc_guard_scan(&mc->guard_head);

	if (mc->config->guard_interval > 0) {
		ipc_timing_init(&mc->timers->guard_timing, 1, 
						 mc->config->guard_interval, 0, 
						 mc, mcd_guard);
		ipc_timing_register(&mc->timers->guard_timing);
		mc->flags |= BIT(MC_F_GUARD_RUN);
		LOGI("mc guard started.");
	} else
		LOGI("mc guard disabled.");
	
	return 0;
}
int mcd_guard_start(struct mc_struct *mc)
{
	if (mc->flags & BIT(MC_F_GUARD_RUN)) {
		return 0;
	}
	/*
	 * Delete delay starting timer first, if have.
	 */
	ipc_timing_unregister(&mc->timers->guard_timing);
	return mcd_guard_run(mc);
}
int mcd_guard_delay(struct ipc_timing *timing)
{
	return mcd_guard_run(timing->arg);
}


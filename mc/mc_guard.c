#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include "generic_file.h"
#include "mc_base.h"
#include "mc_guard.h"
#include "mc_identity.h"
#include "mc_guard_table.h"
#include "generic_proc.h"
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
static int guard_scanning(const char *comm, pid_t pid, void *arg)
{
	struct mc_guard *guard = mc_guard_search(comm);
	if (guard) {
		LOGI("Scanned static process:%s pid:%d", comm, pid);
		goto found;	
	} 
	struct list_head *guard_list = arg;
	list_for_each_entry_reverse(guard, guard_list, list) {
		if (guard->id != MC_IDENTITY_DYNAMIC)
			break;
		if (!strcmp(comm, guard->name)) {
			LOGI("Scanned dynamic process:%s pid:%d", comm, pid);	
			goto found;
		}
	}
	return 0;
found:
	if (guard->pid == 0)
		guard->pid = pid;
	else LOGW("Scanned duplicated process:%s pid:%d / %d", comm, guard->pid, pid);
	return 0;
}
void mc_guard_scanning(struct list_head *guard_list)
{
	for_each_process(guard_scanning, guard_list);
}
int mc_guard_find(struct mc_guard *guard)
{

	int pid = process_find(guard->name);
	
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

int mc_guard_inspect(struct mc_guard *guard)
{
	char path[32];
	char name[64];
	sprintf(path,"/proc/%d/comm", guard->pid);
	if (access(path, F_OK) < 0) {
		LOGE("Process:%s pid:%d Not found.", guard->name, guard->pid);
		goto err;
	}
	if (utils_file_read_string(path, name, sizeof(name)) < 0)
		goto err;

	if (strcmp(name, guard->name)) {
		LOGE("Process:%s pid:%d, but name:%s.", guard->name, guard->pid, name);
		goto err;
	}
	return 0;
err:
	guard->pid = 0;
	return -1;
}
int mc_guard_dynamic_append(const char *guard_conf, struct list_head *guard_list)
{
	char buf[MC_CLIENT_CMDLINE_LEN + MC_CLIENT_NAME_LEN + 2] = {0};
	char *name, *comm, *p;
	FILE *fp = NULL;
	fp = fopen(guard_conf, "r");
	if (fp == NULL) {
		LOGE("dynamic guard list open:%d.", errno);
		return -1;
	}
	struct mc_guard *guard = NULL;
	
	while (fgets(buf, sizeof(buf), fp)) {
		name = buf;

		while (*name == ' ')
			name++;
		
		if ((comm = strchr(name, ' ')) == NULL)
			continue;
		
		*comm++ = '\0';
		
		while (*comm == ' ')
			comm++;

		for (p = comm; *p; p++) {
			if (*p == '\n' || *p == '\r') {
				*p = '\0';
				break;
			}
		}

		if (p == comm)
			continue;
			
		guard = (struct mc_guard *)malloc(sizeof(struct mc_guard));
		if (!guard) 
			goto err;

		guard->name = strdup(name);
		if (!guard->name)
			goto err;
		
		guard->cmdline= strdup(comm);

		if (!guard->cmdline)
			goto err;

		guard->id 	= MC_IDENTITY_DYNAMIC;
		guard->pid	= 0;
		LOGI("dynamic guard:%s, cmdline:%s", guard->name, guard->cmdline);
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
		free(guard);
	}
	fclose(fp);
	return -1;
}

void mc_guard_init(struct list_head *guard_list)
{
	int i;
	for (i = 0; i < __mc_guard_count; i++)
		list_add_tail(&__mc_guard[i].list, guard_list);
}

int mc_guarding(struct ipc_timing *timing)
{
	struct mc_struct *mc = timing->arg;
	if (mc->flags & (BIT(MC_F_REBOOT) | BIT(MC_F_SHUTDOWN))) {
		LOGW("Reboot in progress.");
		return 0;
	}
	struct mc_guard *guard;
	list_for_each_entry(guard, &mc->guard_head, list) {
		if (guard->pid) {
			if (mc_guard_inspect(guard) < 0) {
				LOGI("Restarting guard:%s.", guard->name);
				mc_process_restart(guard->name, guard->cmdline);
			}
		} else {
			if (mc_guard_find(guard) == 0) {
				LOGI("Restarting guard:%s.", guard->name);
				mc_process_latch(guard->cmdline);
			}
		}
	}
	return 0;
}

int mc_guard_delay(struct ipc_timing *timing)
{
	struct mc_struct *mc = timing->arg; 

	mc_guard_dynamic_append(mc->config->guard_conf, &mc->guard_head);

	mc_guard_scanning(&mc->guard_head);
	
	ipc_timing_init(&mc->timers->guard_timing, 1, 
						mc->config->guard_interval, 0, mc, mc_guarding);
	ipc_timing_register(&mc->timers->guard_timing);
	return 0;
}


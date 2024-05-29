#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include "scl.h"
#include "mc_log.h"
#include "mc_config.h"
#define __LOG_TAG "config"
struct mc_config __mc_config = {0};
static void set_int(const char *value, void *addr)
{
	int *p = addr;
	*p = atoi(value);
}
static void set_str(const char *value, void *addr)
{
	char **p = addr;
	*p = strdup(value);
	assert(*p);
}
#define CONF_ITEM_I(name, def)	{#name,	set_int, &__mc_config.name, (void *)#def}
#define CONF_ITEM_S(name, def)	{#name,	set_str, &__mc_config.name, (void *)#def}
/* Keep Asc order via name */
static struct {
	const char 	*name;
	void (*set)(const char *, void *);
	void 		*addr;
	void 		*def;
} __mc_conf_items[] = { 
	CONF_ITEM_I(auto_reboot_timeout, 604800), /* 7 days : 7 * 24 * 3600 = 604800s*/
	CONF_ITEM_I(delay_online, 3),
	CONF_ITEM_I(delay_reboot, 30),
	CONF_ITEM_I(delay_restart, 2),
	CONF_ITEM_S(guard_conf, /var/log/patrol.list),
	CONF_ITEM_I(guard_delay, 10),
	CONF_ITEM_I(guard_interval, 5),
	CONF_ITEM_I(loss_max, 3),
	CONF_ITEM_I(patrol_interval, 2),
	CONF_ITEM_I(probe_interval, 15),
	CONF_ITEM_I(watchdog_interval, 12),
	CONF_ITEM_I(watchdog_timeout, 60),
};
static void mc_config_fn(char *key, char *value,void *arg)
{
	printf("Key:%s Value:%s\n", key, value);

	int l,r,m;
	int n = sizeof(__mc_conf_items) / sizeof(__mc_conf_items[0]);
	int cmp;
	for (l = 0, r = n -1; l <= r;) {
		m = (l + r) >> 1;
		cmp = strcmp(key, __mc_conf_items[m].name);
		if (cmp < 0)
			r = m - 1;
		else if(cmp > 0)
			l = m + 1;
		else {
			__mc_conf_items[m].def = NULL;
			__mc_conf_items[m].set(value, __mc_conf_items[m].addr);
			break;
		}

	}
}
void mc_config_default(void)
{
	int n = sizeof(__mc_conf_items) / sizeof(__mc_conf_items[0]);

	int i;
	for (i = 0; i < n; i++) {
		if (__mc_conf_items[i].def)
			__mc_conf_items[i].set(__mc_conf_items[i].def, __mc_conf_items[i].addr);
	}
}
void mc_config_init(const char *file)
{
	struct mc_config *conf = &__mc_config;
	
	int ret = config_parser('#', '=', '"', file, mc_config_fn, NULL);

	LOGI("mc config init: %d.", ret);

	/* In case some config items missing */
	mc_config_default();


	LOGI("mc config delay_online: %d.", conf->delay_online);
	LOGI("mc config delay_reboot: %d.", conf->delay_reboot);
	LOGI("mc config delay_restart: %d.",conf->delay_restart);
	LOGI("mc config guard_conf: %s.", conf->guard_conf);
	LOGI("mc config guard_delay: %d.", conf->guard_delay);
	LOGI("mc config guard_interval: %d.", conf->guard_interval);
	LOGI("mc config loss_max: %d.", conf->loss_max);
	LOGI("mc config patrol_interval: %d.", conf->patrol_interval);
	LOGI("mc config probe_interval: %d.", conf->probe_interval);
	LOGI("mc config watchdog_interval: %d.", conf->watchdog_interval);
	LOGI("mc config watchdog_timeout: %d.", conf->watchdog_timeout);
	LOGI("mc config auto_reboot_timeout: %d.", conf->auto_reboot_timeout);
}


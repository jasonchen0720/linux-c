#ifndef __MC_CONFIG_H_
#define __MC_CONFIG_H_
struct mc_config 
{
	int delay_online;
	int delay_reboot;
	int delay_restart;
	const char *guard_conf;
	int guard_delay;
	int guard_interval;
	int	loss_max;
	int patrol_interval;
	int probe_interval;
	int watchdog_interval;
	int watchdog_timeout;
	int auto_reboot_timeout;
};

void mc_config_init(const char *file);
#endif

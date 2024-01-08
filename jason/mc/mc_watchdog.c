#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include "mc_watchdog.h"
#define WDON
#define	WDIOC_SETTIMEOUT       _IOWR('W', 6, int)
#define WDDEV   				"/dev/watchdog0"
#define LOG_TAG 				"wdog"

static int watchdog_fd = -1;


int mc_watchdog_init(int timeout)
{
#ifdef WDON
	int fd = open(WDDEV, O_RDWR);

	if (fd < 0) {
		LOGE("watchdog init failure.");
		return -1;
	}

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
		LOGE("watchdog set FD_CLOEXEC failure.");
		close(fd);
		return -1;
	}

	int ret = ioctl(fd, WDIOC_SETTIMEOUT, &timeout);

	if (ret < 0) {
		LOGE("watchdog set timeout failure.");
		close(fd);
		return -1;
	}

	watchdog_fd = fd;
#endif
	LOGI("mc watchdog init done.");
	return 0;
}
int mc_watchdog_feed()
{
#ifdef WDON
	assert(watchdog_fd > 0);
	
	ssize_t len = write(watchdog_fd, "1", 1);
	if(len < 0) {
		LOGE("Feed watchdog failure.");
		return -1;
	}
#endif
	return 0;
}

void mc_watchdog_exit()
{
	if (watchdog_fd > 0) {
		write(watchdog_fd, "V",	1);
		close(watchdog_fd);	
		watchdog_fd = -1;
	}
	LOGI("mc watchdog exited.");
}
int mc_feedwd(struct ipc_timing *timing)
{
	return mc_watchdog_feed();
}

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "timer.h"
#include "generic_log.h"

#define __LOG_FILE			"./timer-sample.log"
#define __LOG_SIZE			1024 * 1024
#define __LOG_TAG			"timer-sample"

#define TMRS_MAX	8
static struct timer_struct timers[TMRS_MAX] = {
	TIMER_INITIALIZER,
};
#define FUNC(secs) \
static void func##secs(void *arg) {\
	LOGI("Timer index:%lu expired", (unsigned long)arg);\
	usleep(500 * 1000);\
}
FUNC(01)
FUNC(02)
FUNC(03)
FUNC(04)
FUNC(05)
FUNC(06)
FUNC(07)
FUNC(08)
FUNC(09)
FUNC(10)
FUNC(11)
FUNC(12)
FUNC(13)
FUNC(14)
FUNC(15)
FUNC(16)
FUNC(17)
FUNC(18)
FUNC(19)
FUNC(20)
FUNC(21)
FUNC(22)
FUNC(23)
FUNC(24)
FUNC(25)
FUNC(26)
FUNC(27)
FUNC(28)
FUNC(29)
FUNC(30)
FUNC(31)

static void (*funcs[])(void *) = {
	func01, func02, func03, func04, func05, 
	func06, func07, func08, func09, func10,
	func11, func12, func13, func14, func15, 
	func16, func17, func18, func19, func20, 
	func21, func22, func23, func24, func25,
	func26, func27, func28, func29, func30, 
	func31};


static void test_func(void *arg)
{
	printf("Test Timer func enter.\n");
	timer_del(arg);
	printf("Test Timer func leave.\n");
}
static struct timer_struct test_timer = TIMER_INITIALIZER;
int main(int argc, char **argv)
{
	struct timer_option opt = {0, 2, 5, 0};
	timer_setup(&opt);


	timer_add(&test_timer, TMR_OPT_CYCLE, 5000, test_func, &test_timer);

	timer_add(&test_timer, TMR_OPT_CYCLE, 5000, test_func, &test_timer);
	
	usleep(1000 * 1000);
	
	unsigned long i;
	
	for (i = 0; i < TMRS_MAX; i++) {
		int n = rand() % 31;
		LOGI("Adding Timer index:%d, interval: %d Seconds.", i, n + 1);
		timer_add(timers + i, TMR_OPT_THREAD | TMR_OPT_CYCLE, (n + 1) * 1000, funcs[n], (void *)i);
		usleep(1000 * 1000);
	}
	for (i = 1; i < atoi(argv[0]); i++) {
		sleep(1);
		#if 0
		if ((i % 4) == 0)
			timer_del(&t0_timer);
		if ((i % 5) == 0)
			timer_add(&t0_timer, TMR_OPT_THREAD | TMR_OPT_CYCLE, 2000, func2, "2-seconds-Timer0");
		if ((i % 7) == 0)
			timer_del(&t3_timer);
		if ((i % 8) == 0)
			timer_add(&t3_timer, TMR_OPT_THREAD | TMR_OPT_CYCLE, 6000, func6, "6-seconds-Timer0");
		#endif
	}
	timer_destroy();

	return 0;
}


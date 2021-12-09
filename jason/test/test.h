#ifndef __TEST_H__
#define __TEST_H__
#include <stdio.h>
#include <string.h>
#include "logger.h"

#define LOG_FILE			"./test.log"
#define LOG_SIZE			1024 * 1024
#define LOG_TAG				"test"
int test_entry_for_ipc(int argc, char **argv);
int test_entry_for_tree(int argc,char **argv);
int test_entry_for_timer(int argc, char **argv);
#endif

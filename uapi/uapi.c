#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <json-c/json_object.h>
#include <json-c/json_tokener.h>
#include "uapi_client.h"
#include "uapi_common.h"
#include "ipc_client.h"
#define UAPI_BUF_SIZE	(32 * 1024)
static char uapi_buf[UAPI_BUF_SIZE];
static char uapi_dbg = 0;
static char * uapi_path_scan(char *buf, size_t size)
{
	DIR *dir = opendir(UAPI_SOCK_DIR);
	int offs = sprintf(buf, "%s", "Path list:\n");
	if (!dir) {
		printf("Open /proc failure:%d.", errno);
		return buf;
	}
	char file[288];
	struct dirent *dirent;
	struct stat fstat;
	for (dirent = readdir(dir); dirent != NULL; dirent = readdir(dir)) {
		//printf("dirent->d_name:%s", dirent->d_name);
		if (!strncmp(dirent->d_name, UAPI_PATH_PREFIX, sizeof(UAPI_PATH_PREFIX) - 1)) {
			snprintf(file, sizeof(file), UAPI_SOCK_DIR"/%s", dirent->d_name);
		   	if (stat(file, &fstat) < 0)
				continue;
			if (!S_ISSOCK(fstat.st_mode))
				continue;
			int ssize = snprintf(buf + offs, size - offs,"\t%s\n", dirent->d_name);
			if (offs + ssize < size)
				offs += ssize;
		}
	}
	closedir(dir);
	return buf;
}
static void uapi_usage()
{
	printf("uapi usage:\n"	\
		"\t uapi: enter uapi debug modde\n" \
		"\t uapi help: uapi usage\n" \
		"\t uapi list: List all the paths\n" \
		"\t uapi list @path: List all the methods of @path\n" \
		"\t uapi call @path @method @JSON: Calling @path->@method with parameter of @JSON\n" \
		"\t uapi event @name @JSON: Sending event of @name with data of @JSON\n" \
		"\n");
}
#define FAIL 	"{\"result\": \"failed\"}"
#define DONE	"{\"result\": \"done\"}"
struct uapi_command {
	const char *name;
	char * (*func)(int argc, char **argv);
};
static char * uapi_command_proc_help(int argc, char **argv)
{
	uapi_usage();
	return NULL;
}

static char * uapi_command_proc_call(int argc, char **argv)
{
	const char *path	= argc > 0 ? argv[0] : NULL;
	const char *method 	= argc > 1 ? argv[1] : NULL;
	const char *json 	= argc > 2 ? argv[2] : "";

	if (path == NULL || method == NULL) {
		return uapi_command_proc_help(0, NULL);
	}
	return uapi_method_invoke(path, method, json, uapi_buf, UAPI_BUF_SIZE, 30, uapi_dbg ? UAPI_F_MULTIPLEX : UAPI_F_ONESHOT);
}
static char * uapi_command_proc_event(int argc, char **argv)
{
	//printf("argc: %d, argv: %p\n", argc, argv);
	const char *event	= argc > 0 ? argv[0] : NULL;
	const char *json 	= argc > 1 ? argv[1] : NULL;
	if (event == NULL)
		return uapi_command_proc_help(0, NULL);

	int dest = argc > 2 ? atoi(argv[2]) : UAPI_BROADCAST;
	return uapi_event_push(event, json, dest) < 0 ?  FAIL : DONE;
}
static char * uapi_command_proc_list(int argc, char **argv)
{
	const char *path = argc > 0 ? argv[0] : NULL;
	return path ? uapi_method_list(path, uapi_buf, UAPI_BUF_SIZE, 5) : uapi_path_scan(uapi_buf, UAPI_BUF_SIZE);
}

static char * uapi_command_proc_debug(int argc, char **argv)
{
	int ret = -1;
	struct ipc_client client;
	if (ipc_client_init(UAPI_BROKER, &client) == 0) {
		ret = ipc_client_publish(&client, IPC_TO_BROADCAST, 
			UAPI_EVENT_MASK, 
			UAPI_EVENT_DEBUG, NULL, 0, 0);
		ipc_client_close(&client);
	}
	return ret != IPC_SUCCESS ? FAIL : DONE;
}
static const struct uapi_command uapi_command_list[] = {
	/*
	 * Warning: Please keep order of ascending.
	 */
	{"call", 	uapi_command_proc_call},
	{"debug", 	uapi_command_proc_debug},
	{"event", 	uapi_command_proc_event},
	{"help", 	uapi_command_proc_help},
	{"list", 	uapi_command_proc_list},
	/*
	 * Warning: Please keep order of ascending via |name|.
	 */
};

static const struct uapi_command * uapi_command_find(const char *command)
{
	int l,r,m;
	int cmp;
	int n = sizeof(uapi_command_list) / sizeof(uapi_command_list[0]);
	for (l = 0, r = n -1; l <= r;) {
		m = (l + r) >> 1;
		cmp = strcmp(command, uapi_command_list[m].name);
		//printf("uapi_command_list[m].name: %s\n", uapi_command_list[m].name);
		if (cmp < 0)
			r = m - 1;
		else if(cmp > 0)
			l = m + 1;
		else
			return &uapi_command_list[m];

	}
	return NULL;
}
static int uapi_proc(int argc, char **argv)
{
	//printf("argc: %d, argv: %p\n", argc, argv);
	if (argc < 1) {
		goto err;
	}
	const char *cmd = argv[0];

	//printf("Lookup cmd: %s\n", cmd);
	const struct uapi_command * command = uapi_command_find(cmd);
	if (command == NULL) {
		goto err;
	}

	//printf("Found command: %s\n", command->name);
	
	char *json = command->func(--argc, ++argv);

	//printf("argc: %d, argv: %p\n", argc, argv);
	
	if (json) {
		struct json_object *obj = json_tokener_parse(json);
		if (obj) {
			printf("%s\n", json_object_to_json_string(obj));
			json_object_put(obj);
		} else
			printf("%s\n", json);
	}
	return 0;
err:
	uapi_usage();
	return -1;
}

static void uapi_func(const char *event, void *data, size_t size, void *arg)
{
	const char *json = (const char *)data;
	printf("event '%s' received, data: %s, strlen: %lu, size: %lu\n", event, json, strlen(json), size);
}

struct uapic_event events[] = {
	{"test0", NULL, NULL},
	{"test1", NULL, NULL},
	{"test2", NULL, NULL},
};
static int uapi_debug()
{
	int i;
	char in[2048];
	char *p;
	char *outer;
	char *r[4];
	uapi_dbg = 1;
	uapi_event_register(events, sizeof(events) / sizeof(events[0]), uapi_func, NULL);
	for (p = in; p; p = in) {
		printf("\nPlease input @cmd @path @method @json: \n");
		memset(r, 0, sizeof(r));
		fgets(in, sizeof(in), stdin);
		//printf("in:%s\n", in);
		for (i = 0, p = strtok_r(in, " \n", &outer); p != NULL; p = strtok_r(NULL, " \n", &outer)) {
			r[i++] = p;
			if (i >= 4)
				break;
		}

		//printf("cmd:%s path:%s method:%s json:%s\n", r[0], r[1], r[2], r[3]);
		if (r[0]) {
			if (!strcmp(r[0], "exit"))
				break;
			uapi_proc(i, r);
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	return argc == 1 ? uapi_debug() : uapi_proc(--argc, ++argv);
}

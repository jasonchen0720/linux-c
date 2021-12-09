#include "test.h"

int main(int argc, char **argv) {

	if (argc < 2) {
		printf("Invalid input.\n");
		return -1;
	}
#ifdef CONFIG_IPC
	if (!strcmp(argv[1], "ipc"))
		return test_entry_for_ipc(argc - 2, argv + 2);
#endif

#ifdef CONFIG_TREE
	if (!strcmp(argv[1], "tree"))
		return test_entry_for_tree(argc - 2, argv + 2);
#endif

#ifdef CONFIG_TMR
	if (!strcmp(argv[1], "timer"))
		return test_entry_for_timer(argc - 2, argv + 2);
#endif
	printf("Unknown test command:%s\n", argv[1]);
	return -1;
}

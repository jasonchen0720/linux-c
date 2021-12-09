#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ccl.h"
#include "rb_tree.h"
struct rb_data
{
	int data;
	struct rb_node node;
};
int comparator(const struct rb_node *n1, const struct rb_node *n2)
{
	struct rb_data *p1 = rb_entry(n1, struct rb_data, node);
	struct rb_data *p2 = rb_entry(n2, struct rb_data, node);

	return p1->data - p2->data;
}
int searcher(const void *data, const struct rb_node *n)
{
	const struct rb_data *p = rb_entry(n, struct rb_data, node);
	const int *i = (const int *)data;
	return *i - p->data;
}
static void printer(const struct rb_node *n)
{
	if (!n)
		printf("\033[0mnil");
	else {
		struct rb_data *__pr = rb_entry(n, struct rb_data, node);
		if (rb_is_red(n)) {
			printf("\033[31m%d", __pr->data);
		} else {
			printf("\033[0m%d", __pr->data);
		}
	}
	printf("\033[0m\n");
}
void rbt_test(int argc, char **argv)
{
	int i;
	struct rb_tree rbtree;
	struct rb_tree *tree = &rbtree;
	tree->comparator = comparator;
	tree->searcher = searcher;
	tree->printer = printer;
	tree->rb_count = 0;
	tree->root = NULL;
	struct rb_node *n;
	struct rb_data *p;
	for (i = 0; i < atoi(argv[0]); i++) {
		p = malloc(sizeof(struct rb_data));
		if (!p)
			break;
		p->data = rand() % 100;
		printf("Insert : %d.\n", p->data);
		if (&p->node != rb_insert(&p->node, tree)) {
			printf("Duplicate : %d\n", p->data);
			free(p);
		}
	}
	int red = 0;
	int black = 0;
	for (n = rb_first(tree); n ; n = rb_next(n)) {
		rb_is_red(n) ? red++ : black++;
	}
	rb_print(tree);
	printf("rb_count : %lu, red: %d  black: %d.\n", tree->rb_count, red, black);
	for (i = 0; i < 5;) {
		int r = rand() % 100;
		n = rb_search(&r, tree);
		if (n) {
			i++;
			struct rb_data *p = rb_entry(n, struct rb_data, node);
			printf("Remove : %d\n", p->data);
			rb_remove(n, tree);
			free(p);
		}
	}
	rb_print(tree);
	printf("rb_count : %lu.\n", tree->rb_count);
	
}
int bst_test(int argc,char **argv)
{
	struct ccl_t			config;
	const struct ccl_pair 	*iter;

	config.comment_char = '#';
	config.sep_char = '=';
	config.str_char = '"';

	ccl_parse(&config, argv[0]);

	while((iter = ccl_iterator(&config)) != 0) {
		printf("(%s,%s)\n", iter->key, iter->value);
	}
	ccl_release(&config);

	return 0;

}

int test_entry_for_tree(int argc,char **argv)
{
	if (argc < 2)
		return -1;
	printf("argv[0]:%s", argv[0]);
	if (!strcmp(argv[0], "rbt"))
		rbt_test(argc - 1, argv + 1);
	else if (!strcmp(argv[0], "bst"))
		bst_test(argc - 1, argv + 1);


	return 0;
}

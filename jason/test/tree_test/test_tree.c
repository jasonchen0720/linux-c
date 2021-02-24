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
		if (n->color == RB_RED) {
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
	
	for (i = 0; i < atoi(argv[0]); i++) {
		struct rb_data *p 	= malloc(sizeof(struct rb_data));
		p->data = rand() % 1000;
		printf("Insert : %d.\n", p->data);
		if (!rb_insert(&p->node, tree))
			free(p);
	}
	int red = 0;
	int black = 0;
	struct rb_node *n;
	for (n = rb_first(tree); n ; n = rb_next(n)) {
		struct rb_data *p = rb_entry(n, struct rb_data, node);
		if (n->color == RB_RED) {
			red++;
		} else {
			black++;
		}
	}
	
	printf("rb_count : %lu, red: %d  black: %d.\n", tree->rb_count, red, black);
	for (i = 0; i < 5; i++) {
		int r = rand() % 1000;
		struct rb_node *n = rb_search(&r, tree);
		if (n) {
			struct rb_data *p = rb_entry(n, struct rb_data, node);
			printf("Remove : %d", p->data);
			rb_remove(n, tree);
			free(p);
		}
	}
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

int main(int argc,char **argv)
{
	if (argc < 2)
		return -1;
	if (!strcmp(argv[1], "rbt"))
		rbt_test(argc - 2, argv + 2);
	else if (!strcmp(argv[1], "bst"))
		bst_test(argc - 2, argv + 2);


	return 0;
}

#ifndef _LIST_H
#define _LIST_H
#define LIST_POISON1  (NULL)
#define LIST_POISON2  (NULL)

struct list_head
{
	struct list_head *next;
	struct list_head *prev;
};

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define offsetof(type, member) ((char *) &((type *)0)->member)
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})


#define list_entry(ptr, type, member)   \
((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))

#define list_head_initializer(name) { &(name), &(name) }

#define init_list_head(ptr)   \
do {(ptr)->next = (ptr); (ptr)->prev = (ptr);} while(0)

#define list_for_each(pos, head)   \
for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head)   \
for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

#define list_for_each_entry(pos, head, member)   \
for (pos=list_entry((head)->next, typeof(*pos), member); &pos->member!=(head); pos=list_entry(pos->member.next, typeof(*pos), member))

#define list_for_each_entry_safe(pos, n, head, member)	\
for (pos = list_entry((head)->next, typeof(*pos), member),n = list_entry(pos->member.next, typeof(*pos), member);&pos->member != (head); pos = n, n = list_entry(n->member.next, typeof(*n), member))

#define list_delete(ptr)  \
do {(ptr)->prev->next = (ptr)->next; (ptr)->next->prev = (ptr)->prev;} while(0)

/**
 * list_first_entry - get the first element from a list
 * @ptr:        the list head to take the element from.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the list_struct within the struct.
 *
 * Note, that list is expected to be not empty.
 */
#define list_first_entry(ptr, type, member) \
        list_entry((ptr)->next, type, member)

/**
 * list_for_each_prev	-	iterate over a list backwards
 * @pos:	the &struct list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define list_for_each_prev(pos, head) \
	for (pos = (head)->prev; pos != (head); \
        	pos = pos->prev)

/**
 * list_for_each_prev_safe - iterate over a list backwards safe against removal of list entry
 * @pos:        the &struct list_head to use as a loop cursor.
 * @n:          another &struct list_head to use as temporary storage
 * @head:       the head for your list.
 */
#define list_for_each_prev_safe(pos, n, head) \
                for (pos = (head)->prev, n = pos->prev; \
                     pos != (head); \
                     pos = n, n = pos->prev)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list->prev = list;
}

static inline void __list_add(struct list_head *node,struct list_head *prev, struct list_head *next)
{
	next->prev = node;
	node->next = next;
	node->prev = prev;
	prev->next = node;
}

static inline void list_add(struct list_head *node, struct list_head *head)
{
	__list_add(node, head, head->next);
}


static inline void list_add_tail(struct list_head *node, struct list_head *head)
{
	__list_add(node, head->prev, head);
}

static inline void __list_del(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = LIST_POISON1;
	entry->prev = LIST_POISON2;
}

static inline int list_empty(struct list_head *head)
{
	return head->next == head;
}

static inline void list_del_init(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

#endif

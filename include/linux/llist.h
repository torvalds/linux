#ifndef LLIST_H
#define LLIST_H
/*
 * Lock-less NULL terminated single linked list
 *
 * If there are multiple producers and multiple consumers, llist_add
 * can be used in producers and llist_del_all can be used in
 * consumers.  They can work simultaneously without lock.  But
 * llist_del_first can not be used here.  Because llist_del_first
 * depends on list->first->next does not changed if list->first is not
 * changed during its operation, but llist_del_first, llist_add,
 * llist_add (or llist_del_all, llist_add, llist_add) sequence in
 * another consumer may violate that.
 *
 * If there are multiple producers and one consumer, llist_add can be
 * used in producers and llist_del_all or llist_del_first can be used
 * in the consumer.
 *
 * This can be summarized as follow:
 *
 *           |   add    | del_first |  del_all
 * add       |    -     |     -     |     -
 * del_first |          |     L     |     L
 * del_all   |          |           |     -
 *
 * Where "-" stands for no lock is needed, while "L" stands for lock
 * is needed.
 *
 * The list entries deleted via llist_del_all can be traversed with
 * traversing function such as llist_for_each etc.  But the list
 * entries can not be traversed safely before deleted from the list.
 * The order of deleted entries is from the newest to the oldest added
 * one.  If you want to traverse from the oldest to the newest, you
 * must reverse the order by yourself before traversing.
 *
 * The basic atomic operation of this list is cmpxchg on long.  On
 * architectures that don't have NMI-safe cmpxchg implementation, the
 * list can NOT be used in NMI handler.  So code uses the list in NMI
 * handler should depend on CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG.
 */

struct llist_head {
	struct llist_node *first;
};

struct llist_node {
	struct llist_node *next;
};

#define LLIST_HEAD_INIT(name)	{ NULL }
#define LLIST_HEAD(name)	struct llist_head name = LLIST_HEAD_INIT(name)

/**
 * init_llist_head - initialize lock-less list head
 * @head:	the head for your lock-less list
 */
static inline void init_llist_head(struct llist_head *list)
{
	list->first = NULL;
}

/**
 * llist_entry - get the struct of this entry
 * @ptr:	the &struct llist_node pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the llist_node within the struct.
 */
#define llist_entry(ptr, type, member)		\
	container_of(ptr, type, member)

/**
 * llist_for_each - iterate over some deleted entries of a lock-less list
 * @pos:	the &struct llist_node to use as a loop cursor
 * @node:	the first entry of deleted list entries
 *
 * In general, some entries of the lock-less list can be traversed
 * safely only after being deleted from list, so start with an entry
 * instead of list head.
 *
 * If being used on entries deleted from lock-less list directly, the
 * traverse order is from the newest to the oldest added entry.  If
 * you want to traverse from the oldest to the newest, you must
 * reverse the order by yourself before traversing.
 */
#define llist_for_each(pos, node)			\
	for ((pos) = (node); pos; (pos) = (pos)->next)

/**
 * llist_for_each_entry - iterate over some deleted entries of lock-less list of given type
 * @pos:	the type * to use as a loop cursor.
 * @node:	the fist entry of deleted list entries.
 * @member:	the name of the llist_node with the struct.
 *
 * In general, some entries of the lock-less list can be traversed
 * safely only after being removed from list, so start with an entry
 * instead of list head.
 *
 * If being used on entries deleted from lock-less list directly, the
 * traverse order is from the newest to the oldest added entry.  If
 * you want to traverse from the oldest to the newest, you must
 * reverse the order by yourself before traversing.
 */
#define llist_for_each_entry(pos, node, member)				\
	for ((pos) = llist_entry((node), typeof(*(pos)), member);	\
	     &(pos)->member != NULL;					\
	     (pos) = llist_entry((pos)->member.next, typeof(*(pos)), member))

/**
 * llist_empty - tests whether a lock-less list is empty
 * @head:	the list to test
 *
 * Not guaranteed to be accurate or up to date.  Just a quick way to
 * test whether the list is empty without deleting something from the
 * list.
 */
static inline int llist_empty(const struct llist_head *head)
{
	return ACCESS_ONCE(head->first) == NULL;
}

void llist_add(struct llist_node *new, struct llist_head *head);
void llist_add_batch(struct llist_node *new_first, struct llist_node *new_last,
		     struct llist_head *head);
struct llist_node *llist_del_first(struct llist_head *head);
struct llist_node *llist_del_all(struct llist_head *head);
#endif /* LLIST_H */

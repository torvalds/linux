struct list {
	struct list *next, *prev;
};

static inline void
list_init(struct list *list)
{
	list->next = list;
	list->prev = list;
}

static inline int
list_empty(struct list *list)
{
	return list->next == list;
}

static inline void
list_insert(struct list *link, struct list *new_link)
{
	new_link->prev		= link->prev;
	new_link->next		= link;
	new_link->prev->next	= new_link;
	new_link->next->prev	= new_link;
}

static inline void
list_append(struct list *list, struct list *new_link)
{
	list_insert((struct list *)list, new_link);
}

static inline void
list_prepend(struct list *list, struct list *new_link)
{
	list_insert(list->next, new_link);
}

static inline void
list_remove(struct list *link)
{
	link->prev->next = link->next;
	link->next->prev = link->prev;
}

#define list_entry(link, type, member) \
	((type *)((char *)(link)-(unsigned long)(&((type *)0)->member)))

#define list_head(list, type, member)		\
	list_entry((list)->next, type, member)

#define list_tail(list, type, member)		\
	list_entry((list)->prev, type, member)

#define list_next(elm, member)					\
	list_entry((elm)->member.next, typeof(*elm), member)

#define list_for_each_entry(pos, list, member)			\
	for (pos = list_head(list, typeof(*pos), member);	\
	     &pos->member != (list);				\
	     pos = list_next(pos, member))


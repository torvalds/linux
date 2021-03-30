#ifndef INTERNAL_IO_WQ_H
#define INTERNAL_IO_WQ_H

#include <linux/io_uring.h>

struct io_wq;

enum {
	IO_WQ_WORK_CANCEL	= 1,
	IO_WQ_WORK_HASHED	= 2,
	IO_WQ_WORK_UNBOUND	= 4,
	IO_WQ_WORK_CONCURRENT	= 16,

	IO_WQ_WORK_FILES	= 32,
	IO_WQ_WORK_FS		= 64,
	IO_WQ_WORK_MM		= 128,
	IO_WQ_WORK_CREDS	= 256,
	IO_WQ_WORK_BLKCG	= 512,
	IO_WQ_WORK_FSIZE	= 1024,

	IO_WQ_HASH_SHIFT	= 24,	/* upper 8 bits are used for hash key */
};

enum io_wq_cancel {
	IO_WQ_CANCEL_OK,	/* cancelled before started */
	IO_WQ_CANCEL_RUNNING,	/* found, running, and attempted cancelled */
	IO_WQ_CANCEL_NOTFOUND,	/* work not found */
};

static inline void wq_list_add_after(struct io_wq_work_node *node,
				     struct io_wq_work_node *pos,
				     struct io_wq_work_list *list)
{
	struct io_wq_work_node *next = pos->next;

	pos->next = node;
	node->next = next;
	if (!next)
		list->last = node;
}

static inline void wq_list_add_tail(struct io_wq_work_node *node,
				    struct io_wq_work_list *list)
{
	if (!list->first) {
		list->last = node;
		WRITE_ONCE(list->first, node);
	} else {
		list->last->next = node;
		list->last = node;
	}
	node->next = NULL;
}

static inline void wq_list_cut(struct io_wq_work_list *list,
			       struct io_wq_work_node *last,
			       struct io_wq_work_node *prev)
{
	/* first in the list, if prev==NULL */
	if (!prev)
		WRITE_ONCE(list->first, last->next);
	else
		prev->next = last->next;

	if (last == list->last)
		list->last = prev;
	last->next = NULL;
}

static inline void wq_list_del(struct io_wq_work_list *list,
			       struct io_wq_work_node *node,
			       struct io_wq_work_node *prev)
{
	wq_list_cut(list, node, prev);
}

#define wq_list_for_each(pos, prv, head)			\
	for (pos = (head)->first, prv = NULL; pos; prv = pos, pos = (pos)->next)

#define wq_list_empty(list)	(READ_ONCE((list)->first) == NULL)
#define INIT_WQ_LIST(list)	do {				\
	(list)->first = NULL;					\
	(list)->last = NULL;					\
} while (0)

struct io_wq_work {
	struct io_wq_work_node list;
	struct io_identity *identity;
	unsigned flags;
};

static inline struct io_wq_work *wq_next_work(struct io_wq_work *work)
{
	if (!work->list.next)
		return NULL;

	return container_of(work->list.next, struct io_wq_work, list);
}

typedef struct io_wq_work *(free_work_fn)(struct io_wq_work *);
typedef void (io_wq_work_fn)(struct io_wq_work *);

struct io_wq_data {
	struct user_struct *user;

	io_wq_work_fn *do_work;
	free_work_fn *free_work;
};

struct io_wq *io_wq_create(unsigned bounded, struct io_wq_data *data);
bool io_wq_get(struct io_wq *wq, struct io_wq_data *data);
void io_wq_destroy(struct io_wq *wq);

void io_wq_enqueue(struct io_wq *wq, struct io_wq_work *work);
void io_wq_hash_work(struct io_wq_work *work, void *val);

static inline bool io_wq_is_hashed(struct io_wq_work *work)
{
	return work->flags & IO_WQ_WORK_HASHED;
}

typedef bool (work_cancel_fn)(struct io_wq_work *, void *);

enum io_wq_cancel io_wq_cancel_cb(struct io_wq *wq, work_cancel_fn *cancel,
					void *data, bool cancel_all);

struct task_struct *io_wq_get_task(struct io_wq *wq);

#if defined(CONFIG_IO_WQ)
extern void io_wq_worker_sleeping(struct task_struct *);
extern void io_wq_worker_running(struct task_struct *);
#else
static inline void io_wq_worker_sleeping(struct task_struct *tsk)
{
}
static inline void io_wq_worker_running(struct task_struct *tsk)
{
}
#endif

static inline bool io_wq_current_is_worker(void)
{
	return in_task() && (current->flags & PF_IO_WORKER);
}
#endif

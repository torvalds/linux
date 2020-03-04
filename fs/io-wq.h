#ifndef INTERNAL_IO_WQ_H
#define INTERNAL_IO_WQ_H

struct io_wq;

enum {
	IO_WQ_WORK_CANCEL	= 1,
	IO_WQ_WORK_HASHED	= 4,
	IO_WQ_WORK_UNBOUND	= 32,
	IO_WQ_WORK_NO_CANCEL	= 256,
	IO_WQ_WORK_CONCURRENT	= 512,

	IO_WQ_HASH_SHIFT	= 24,	/* upper 8 bits are used for hash key */
};

enum io_wq_cancel {
	IO_WQ_CANCEL_OK,	/* cancelled before started */
	IO_WQ_CANCEL_RUNNING,	/* found, running, and attempted cancelled */
	IO_WQ_CANCEL_NOTFOUND,	/* work not found */
};

struct io_wq_work_node {
	struct io_wq_work_node *next;
};

struct io_wq_work_list {
	struct io_wq_work_node *first;
	struct io_wq_work_node *last;
};

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
}

static inline void wq_node_del(struct io_wq_work_list *list,
			       struct io_wq_work_node *node,
			       struct io_wq_work_node *prev)
{
	if (node == list->first)
		WRITE_ONCE(list->first, node->next);
	if (node == list->last)
		list->last = prev;
	if (prev)
		prev->next = node->next;
	node->next = NULL;
}

#define wq_list_for_each(pos, prv, head)			\
	for (pos = (head)->first, prv = NULL; pos; prv = pos, pos = (pos)->next)

#define wq_list_empty(list)	(READ_ONCE((list)->first) == NULL)
#define INIT_WQ_LIST(list)	do {				\
	(list)->first = NULL;					\
	(list)->last = NULL;					\
} while (0)

struct io_wq_work {
	union {
		struct io_wq_work_node list;
		void *data;
	};
	void (*func)(struct io_wq_work **);
	struct files_struct *files;
	struct mm_struct *mm;
	const struct cred *creds;
	struct fs_struct *fs;
	unsigned flags;
	pid_t task_pid;
};

#define INIT_IO_WORK(work, _func)				\
	do {							\
		*(work) = (struct io_wq_work){ .func = _func };	\
	} while (0)						\

typedef void (free_work_fn)(struct io_wq_work *);

struct io_wq_data {
	struct user_struct *user;

	free_work_fn *free_work;
};

struct io_wq *io_wq_create(unsigned bounded, struct io_wq_data *data);
bool io_wq_get(struct io_wq *wq, struct io_wq_data *data);
void io_wq_destroy(struct io_wq *wq);

void io_wq_enqueue(struct io_wq *wq, struct io_wq_work *work);
void io_wq_enqueue_hashed(struct io_wq *wq, struct io_wq_work *work, void *val);

void io_wq_cancel_all(struct io_wq *wq);
enum io_wq_cancel io_wq_cancel_work(struct io_wq *wq, struct io_wq_work *cwork);
enum io_wq_cancel io_wq_cancel_pid(struct io_wq *wq, pid_t pid);

typedef bool (work_cancel_fn)(struct io_wq_work *, void *);

enum io_wq_cancel io_wq_cancel_cb(struct io_wq *wq, work_cancel_fn *cancel,
					void *data);

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

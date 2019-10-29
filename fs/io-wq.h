#ifndef INTERNAL_IO_WQ_H
#define INTERNAL_IO_WQ_H

struct io_wq;

enum {
	IO_WQ_WORK_CANCEL	= 1,
	IO_WQ_WORK_HAS_MM	= 2,
	IO_WQ_WORK_HASHED	= 4,
	IO_WQ_WORK_NEEDS_USER	= 8,
	IO_WQ_WORK_NEEDS_FILES	= 16,

	IO_WQ_HASH_SHIFT	= 24,	/* upper 8 bits are used for hash key */
};

enum io_wq_cancel {
	IO_WQ_CANCEL_OK,	/* cancelled before started */
	IO_WQ_CANCEL_RUNNING,	/* found, running, and attempted cancelled */
	IO_WQ_CANCEL_NOTFOUND,	/* work not found */
};

struct io_wq_work {
	struct list_head list;
	void (*func)(struct io_wq_work **);
	unsigned flags;
	struct files_struct *files;
};

#define INIT_IO_WORK(work, _func)			\
	do {						\
		(work)->func = _func;			\
		(work)->flags = 0;			\
		(work)->files = NULL;			\
	} while (0)					\

struct io_wq *io_wq_create(unsigned concurrency, struct mm_struct *mm);
void io_wq_destroy(struct io_wq *wq);

void io_wq_enqueue(struct io_wq *wq, struct io_wq_work *work);
void io_wq_enqueue_hashed(struct io_wq *wq, struct io_wq_work *work, void *val);
void io_wq_flush(struct io_wq *wq);

void io_wq_cancel_all(struct io_wq *wq);
enum io_wq_cancel io_wq_cancel_work(struct io_wq *wq, struct io_wq_work *cwork);

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

#endif

#ifndef __LINUX__AIO_H
#define __LINUX__AIO_H

#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/aio_abi.h>
#include <linux/uio.h>
#include <linux/rcupdate.h>

#include <linux/atomic.h>

struct kioctx;
struct kiocb;

#define KIOCB_KEY		0

typedef int (kiocb_cancel_fn)(struct kiocb *);

#define IOCB_EVENTFD		(1 << 0)

struct kiocb {
	struct file		*ki_filp;
	loff_t			ki_pos;
	void (*ki_complete)(struct kiocb *iocb, long ret, long ret2);
	void			*private;
	int			ki_flags;
};

static inline bool is_sync_kiocb(struct kiocb *kiocb)
{
	return kiocb->ki_complete == NULL;
}

static inline void init_sync_kiocb(struct kiocb *kiocb, struct file *filp)
{
	*kiocb = (struct kiocb) {
			.ki_filp = filp,
		};
}

/* prototypes */
#ifdef CONFIG_AIO
struct mm_struct;
extern void exit_aio(struct mm_struct *mm);
extern long do_io_submit(aio_context_t ctx_id, long nr,
			 struct iocb __user *__user *iocbpp, bool compat);
void kiocb_set_cancel_fn(struct kiocb *req, kiocb_cancel_fn *cancel);
#else
struct mm_struct;
static inline void exit_aio(struct mm_struct *mm) { }
static inline long do_io_submit(aio_context_t ctx_id, long nr,
				struct iocb __user * __user *iocbpp,
				bool compat) { return 0; }
static inline void kiocb_set_cancel_fn(struct kiocb *req,
				       kiocb_cancel_fn *cancel) { }
#endif /* CONFIG_AIO */

/* for sysctl: */
extern unsigned long aio_nr;
extern unsigned long aio_max_nr;

#endif /* __LINUX__AIO_H */

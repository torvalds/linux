/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TTY_LDISC_H
#define _LINUX_TTY_LDISC_H

/*
 * This structure defines the interface between the tty line discipline
 * implementation and the tty routines.  The following routines can be
 * defined; unless noted otherwise, they are optional, and can be
 * filled in with a null pointer.
 *
 * int	(*open)(struct tty_struct *);
 *
 *	This function is called when the line discipline is associated
 *	with the tty.  The line discipline can use this as an
 *	opportunity to initialize any state needed by the ldisc routines.
 *
 * void	(*close)(struct tty_struct *);
 *
 *	This function is called when the line discipline is being
 *	shutdown, either because the tty is being closed or because
 *	the tty is being changed to use a new line discipline
 *
 * void	(*flush_buffer)(struct tty_struct *tty);
 *
 *	This function instructs the line discipline to clear its
 *	buffers of any input characters it may have queued to be
 *	delivered to the user mode process.
 *
 * ssize_t (*read)(struct tty_struct * tty, struct file * file,
 *		   unsigned char * buf, size_t nr);
 *
 *	This function is called when the user requests to read from
 *	the tty.  The line discipline will return whatever characters
 *	it has buffered up for the user.  If this function is not
 *	defined, the user will receive an EIO error.
 *
 * ssize_t (*write)(struct tty_struct * tty, struct file * file,
 *		    const unsigned char * buf, size_t nr);
 *
 *	This function is called when the user requests to write to the
 *	tty.  The line discipline will deliver the characters to the
 *	low-level tty device for transmission, optionally performing
 *	some processing on the characters first.  If this function is
 *	not defined, the user will receive an EIO error.
 *
 * int	(*ioctl)(struct tty_struct * tty, struct file * file,
 *		 unsigned int cmd, unsigned long arg);
 *
 *	This function is called when the user requests an ioctl which
 *	is not handled by the tty layer or the low-level tty driver.
 *	It is intended for ioctls which affect line discpline
 *	operation.  Note that the search order for ioctls is (1) tty
 *	layer, (2) tty low-level driver, (3) line discpline.  So a
 *	low-level driver can "grab" an ioctl request before the line
 *	discpline has a chance to see it.
 *
 * int	(*compat_ioctl)(struct tty_struct * tty, struct file * file,
 *		        unsigned int cmd, unsigned long arg);
 *
 *	Process ioctl calls from 32-bit process on 64-bit system
 *
 *	NOTE: only ioctls that are neither "pointer to compatible
 *	structure" nor tty-generic.  Something private that takes
 *	an integer or a pointer to wordsize-sensitive structure
 *	belongs here, but most of ldiscs will happily leave
 *	it NULL.
 *
 * void	(*set_termios)(struct tty_struct *tty, struct ktermios * old);
 *
 *	This function notifies the line discpline that a change has
 *	been made to the termios structure.
 *
 * int	(*poll)(struct tty_struct * tty, struct file * file,
 *		  poll_table *wait);
 *
 *	This function is called when a user attempts to select/poll on a
 *	tty device.  It is solely the responsibility of the line
 *	discipline to handle poll requests.
 *
 * void	(*receive_buf)(struct tty_struct *, const unsigned char *cp,
 *		       char *fp, int count);
 *
 *	This function is called by the low-level tty driver to send
 *	characters received by the hardware to the line discpline for
 *	processing.  <cp> is a pointer to the buffer of input
 *	character received by the device.  <fp> is a pointer to a
 *	pointer of flag bytes which indicate whether a character was
 *	received with a parity error, etc. <fp> may be NULL to indicate
 *	all data received is TTY_NORMAL.
 *
 * void	(*write_wakeup)(struct tty_struct *);
 *
 *	This function is called by the low-level tty driver to signal
 *	that line discpline should try to send more characters to the
 *	low-level driver for transmission.  If the line discpline does
 *	not have any more data to send, it can just return. If the line
 *	discipline does have some data to send, please arise a tasklet
 *	or workqueue to do the real data transfer. Do not send data in
 *	this hook, it may leads to a deadlock.
 *
 * int (*hangup)(struct tty_struct *)
 *
 *	Called on a hangup. Tells the discipline that it should
 *	cease I/O to the tty driver. Can sleep. The driver should
 *	seek to perform this action quickly but should wait until
 *	any pending driver I/O is completed.
 *
 * void (*dcd_change)(struct tty_struct *tty, unsigned int status)
 *
 *	Tells the discipline that the DCD pin has changed its status.
 *	Used exclusively by the N_PPS (Pulse-Per-Second) line discipline.
 *
 * int	(*receive_buf2)(struct tty_struct *, const unsigned char *cp,
 *			char *fp, int count);
 *
 *	This function is called by the low-level tty driver to send
 *	characters received by the hardware to the line discpline for
 *	processing.  <cp> is a pointer to the buffer of input
 *	character received by the device.  <fp> is a pointer to a
 *	pointer of flag bytes which indicate whether a character was
 *	received with a parity error, etc. <fp> may be NULL to indicate
 *	all data received is TTY_NORMAL.
 *	If assigned, prefer this function for automatic flow control.
 */

#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/atomic.h>

/*
 * the semaphore definition
 */
struct ld_semaphore {
	atomic_long_t		count;
	raw_spinlock_t		wait_lock;
	unsigned int		wait_readers;
	struct list_head	read_wait;
	struct list_head	write_wait;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

extern void __init_ldsem(struct ld_semaphore *sem, const char *name,
			 struct lock_class_key *key);

#define init_ldsem(sem)						\
do {								\
	static struct lock_class_key __key;			\
								\
	__init_ldsem((sem), #sem, &__key);			\
} while (0)


extern int ldsem_down_read(struct ld_semaphore *sem, long timeout);
extern int ldsem_down_read_trylock(struct ld_semaphore *sem);
extern int ldsem_down_write(struct ld_semaphore *sem, long timeout);
extern int ldsem_down_write_trylock(struct ld_semaphore *sem);
extern void ldsem_up_read(struct ld_semaphore *sem);
extern void ldsem_up_write(struct ld_semaphore *sem);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
extern int ldsem_down_read_nested(struct ld_semaphore *sem, int subclass,
				  long timeout);
extern int ldsem_down_write_nested(struct ld_semaphore *sem, int subclass,
				   long timeout);
#else
# define ldsem_down_read_nested(sem, subclass, timeout)		\
		ldsem_down_read(sem, timeout)
# define ldsem_down_write_nested(sem, subclass, timeout)	\
		ldsem_down_write(sem, timeout)
#endif


struct tty_ldisc_ops {
	int	magic;
	char	*name;
	int	num;
	int	flags;

	/*
	 * The following routines are called from above.
	 */
	int	(*open)(struct tty_struct *);
	void	(*close)(struct tty_struct *);
	void	(*flush_buffer)(struct tty_struct *tty);
	ssize_t	(*read)(struct tty_struct *tty, struct file *file,
			unsigned char *buf, size_t nr,
			void **cookie, unsigned long offset);
	ssize_t	(*write)(struct tty_struct *tty, struct file *file,
			 const unsigned char *buf, size_t nr);
	int	(*ioctl)(struct tty_struct *tty, struct file *file,
			 unsigned int cmd, unsigned long arg);
	int	(*compat_ioctl)(struct tty_struct *tty, struct file *file,
				unsigned int cmd, unsigned long arg);
	void	(*set_termios)(struct tty_struct *tty, struct ktermios *old);
	__poll_t (*poll)(struct tty_struct *, struct file *,
			     struct poll_table_struct *);
	int	(*hangup)(struct tty_struct *tty);

	/*
	 * The following routines are called from below.
	 */
	void	(*receive_buf)(struct tty_struct *, const unsigned char *cp,
			       char *fp, int count);
	void	(*write_wakeup)(struct tty_struct *);
	void	(*dcd_change)(struct tty_struct *, unsigned int);
	int	(*receive_buf2)(struct tty_struct *, const unsigned char *cp,
				char *fp, int count);

	struct  module *owner;

	int refcount;
};

struct tty_ldisc {
	struct tty_ldisc_ops *ops;
	struct tty_struct *tty;
};

#define TTY_LDISC_MAGIC	0x5403

#define LDISC_FLAG_DEFINED	0x00000001

#define MODULE_ALIAS_LDISC(ldisc) \
	MODULE_ALIAS("tty-ldisc-" __stringify(ldisc))

#endif /* _LINUX_TTY_LDISC_H */

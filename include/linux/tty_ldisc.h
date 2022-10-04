/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TTY_LDISC_H
#define _LINUX_TTY_LDISC_H

struct tty_struct;

#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/seq_file.h>

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

void __init_ldsem(struct ld_semaphore *sem, const char *name,
			 struct lock_class_key *key);

#define init_ldsem(sem)						\
do {								\
	static struct lock_class_key __key;			\
								\
	__init_ldsem((sem), #sem, &__key);			\
} while (0)


int ldsem_down_read(struct ld_semaphore *sem, long timeout);
int ldsem_down_read_trylock(struct ld_semaphore *sem);
int ldsem_down_write(struct ld_semaphore *sem, long timeout);
int ldsem_down_write_trylock(struct ld_semaphore *sem);
void ldsem_up_read(struct ld_semaphore *sem);
void ldsem_up_write(struct ld_semaphore *sem);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
int ldsem_down_read_nested(struct ld_semaphore *sem, int subclass,
		long timeout);
int ldsem_down_write_nested(struct ld_semaphore *sem, int subclass,
		long timeout);
#else
# define ldsem_down_read_nested(sem, subclass, timeout)		\
		ldsem_down_read(sem, timeout)
# define ldsem_down_write_nested(sem, subclass, timeout)	\
		ldsem_down_write(sem, timeout)
#endif

/**
 * struct tty_ldisc_ops - ldisc operations
 *
 * @name: name of this ldisc rendered in /proc/tty/ldiscs
 * @num: ``N_*`` number (%N_TTY, %N_HDLC, ...) reserved to this ldisc
 *
 * @open: [TTY] ``int ()(struct tty_struct *tty)``
 *
 *	This function is called when the line discipline is associated with the
 *	@tty. No other call into the line discipline for this tty will occur
 *	until it completes successfully. It should initialize any state needed
 *	by the ldisc, and set @tty->receive_room to the maximum amount of data
 *	the line discipline is willing to accept from the driver with a single
 *	call to @receive_buf(). Returning an error will prevent the ldisc from
 *	being attached.
 *
 *	Can sleep.
 *
 * @close: [TTY] ``void ()(struct tty_struct *tty)``
 *
 *	This function is called when the line discipline is being shutdown,
 *	either because the @tty is being closed or because the @tty is being
 *	changed to use a new line discipline. At the point of execution no
 *	further users will enter the ldisc code for this tty.
 *
 *	Can sleep.
 *
 * @flush_buffer: [TTY] ``void ()(struct tty_struct *tty)``
 *
 *	This function instructs the line discipline to clear its buffers of any
 *	input characters it may have queued to be delivered to the user mode
 *	process. It may be called at any point between open and close.
 *
 * @read: [TTY] ``ssize_t ()(struct tty_struct *tty, struct file *file,
 *		unsigned char *buf, size_t nr)``
 *
 *	This function is called when the user requests to read from the @tty.
 *	The line discipline will return whatever characters it has buffered up
 *	for the user. If this function is not defined, the user will receive
 *	an %EIO error. Multiple read calls may occur in parallel and the ldisc
 *	must deal with serialization issues.
 *
 *	Can sleep.
 *
 * @write: [TTY] ``ssize_t ()(struct tty_struct *tty, struct file *file,
 *		const unsigned char *buf, size_t nr)``
 *
 *	This function is called when the user requests to write to the @tty.
 *	The line discipline will deliver the characters to the low-level tty
 *	device for transmission, optionally performing some processing on the
 *	characters first. If this function is not defined, the user will
 *	receive an %EIO error.
 *
 *	Can sleep.
 *
 * @ioctl: [TTY] ``int ()(struct tty_struct *tty, unsigned int cmd,
 *		unsigned long arg)``
 *
 *	This function is called when the user requests an ioctl which is not
 *	handled by the tty layer or the low-level tty driver. It is intended
 *	for ioctls which affect line discpline operation.  Note that the search
 *	order for ioctls is (1) tty layer, (2) tty low-level driver, (3) line
 *	discpline. So a low-level driver can "grab" an ioctl request before
 *	the line discpline has a chance to see it.
 *
 * @compat_ioctl: [TTY] ``int ()(struct tty_struct *tty, unsigned int cmd,
 *		unsigned long arg)``
 *
 *	Process ioctl calls from 32-bit process on 64-bit system.
 *
 *	Note that only ioctls that are neither "pointer to compatible
 *	structure" nor tty-generic.  Something private that takes an integer or
 *	a pointer to wordsize-sensitive structure belongs here, but most of
 *	ldiscs will happily leave it %NULL.
 *
 * @set_termios: [TTY] ``void ()(struct tty_struct *tty, struct ktermios *old)``
 *
 *	This function notifies the line discpline that a change has been made
 *	to the termios structure.
 *
 * @poll: [TTY] ``int ()(struct tty_struct *tty, struct file *file,
 *		  struct poll_table_struct *wait)``
 *
 *	This function is called when a user attempts to select/poll on a @tty
 *	device. It is solely the responsibility of the line discipline to
 *	handle poll requests.
 *
 * @hangup: [TTY] ``void ()(struct tty_struct *tty)``
 *
 *	Called on a hangup. Tells the discipline that it should cease I/O to
 *	the tty driver. The driver should seek to perform this action quickly
 *	but should wait until any pending driver I/O is completed. No further
 *	calls into the ldisc code will occur.
 *
 *	Can sleep.
 *
 * @receive_buf: [DRV] ``void ()(struct tty_struct *tty,
 *		       const unsigned char *cp, const char *fp, int count)``
 *
 *	This function is called by the low-level tty driver to send characters
 *	received by the hardware to the line discpline for processing. @cp is
 *	a pointer to the buffer of input character received by the device. @fp
 *	is a pointer to an array of flag bytes which indicate whether a
 *	character was received with a parity error, etc. @fp may be %NULL to
 *	indicate all data received is %TTY_NORMAL.
 *
 * @write_wakeup: [DRV] ``void ()(struct tty_struct *tty)``
 *
 *	This function is called by the low-level tty driver to signal that line
 *	discpline should try to send more characters to the low-level driver
 *	for transmission. If the line discpline does not have any more data to
 *	send, it can just return. If the line discipline does have some data to
 *	send, please arise a tasklet or workqueue to do the real data transfer.
 *	Do not send data in this hook, it may lead to a deadlock.
 *
 * @dcd_change: [DRV] ``void ()(struct tty_struct *tty, unsigned int status)``
 *
 *	Tells the discipline that the DCD pin has changed its status. Used
 *	exclusively by the %N_PPS (Pulse-Per-Second) line discipline.
 *
 * @receive_buf2: [DRV] ``int ()(struct tty_struct *tty,
 *			const unsigned char *cp, const char *fp, int count)``
 *
 *	This function is called by the low-level tty driver to send characters
 *	received by the hardware to the line discpline for processing. @cp is a
 *	pointer to the buffer of input character received by the device.  @fp
 *	is a pointer to an array of flag bytes which indicate whether a
 *	character was received with a parity error, etc. @fp may be %NULL to
 *	indicate all data received is %TTY_NORMAL. If assigned, prefer this
 *	function for automatic flow control.
 *
 * @lookahead_buf: [DRV] ``void ()(struct tty_struct *tty,
 *			const unsigned char *cp, const char *fp, int count)``
 *
 *	This function is called by the low-level tty driver for characters
 *	not eaten by ->receive_buf() or ->receive_buf2(). It is useful for
 *	processing high-priority characters such as software flow-control
 *	characters that could otherwise get stuck into the intermediate
 *	buffer until tty has room to receive them. Ldisc must be able to
 *	handle later a ->receive_buf() or ->receive_buf2() call for the
 *	same characters (e.g. by skipping the actions for high-priority
 *	characters already handled by ->lookahead_buf()).
 *
 * @owner: module containting this ldisc (for reference counting)
 *
 * This structure defines the interface between the tty line discipline
 * implementation and the tty routines. The above routines can be defined.
 * Unless noted otherwise, they are optional, and can be filled in with a %NULL
 * pointer.
 *
 * Hooks marked [TTY] are invoked from the TTY core, the [DRV] ones from the
 * tty_driver side.
 */
struct tty_ldisc_ops {
	char	*name;
	int	num;

	/*
	 * The following routines are called from above.
	 */
	int	(*open)(struct tty_struct *tty);
	void	(*close)(struct tty_struct *tty);
	void	(*flush_buffer)(struct tty_struct *tty);
	ssize_t	(*read)(struct tty_struct *tty, struct file *file,
			unsigned char *buf, size_t nr,
			void **cookie, unsigned long offset);
	ssize_t	(*write)(struct tty_struct *tty, struct file *file,
			 const unsigned char *buf, size_t nr);
	int	(*ioctl)(struct tty_struct *tty, unsigned int cmd,
			unsigned long arg);
	int	(*compat_ioctl)(struct tty_struct *tty, unsigned int cmd,
			unsigned long arg);
	void	(*set_termios)(struct tty_struct *tty, struct ktermios *old);
	__poll_t (*poll)(struct tty_struct *tty, struct file *file,
			     struct poll_table_struct *wait);
	void	(*hangup)(struct tty_struct *tty);

	/*
	 * The following routines are called from below.
	 */
	void	(*receive_buf)(struct tty_struct *tty, const unsigned char *cp,
			       const char *fp, int count);
	void	(*write_wakeup)(struct tty_struct *tty);
	void	(*dcd_change)(struct tty_struct *tty, unsigned int status);
	int	(*receive_buf2)(struct tty_struct *tty, const unsigned char *cp,
				const char *fp, int count);
	void	(*lookahead_buf)(struct tty_struct *tty, const unsigned char *cp,
				 const unsigned char *fp, unsigned int count);

	struct  module *owner;
};

struct tty_ldisc {
	struct tty_ldisc_ops *ops;
	struct tty_struct *tty;
};

#define MODULE_ALIAS_LDISC(ldisc) \
	MODULE_ALIAS("tty-ldisc-" __stringify(ldisc))

extern const struct seq_operations tty_ldiscs_seq_ops;

struct tty_ldisc *tty_ldisc_ref(struct tty_struct *);
void tty_ldisc_deref(struct tty_ldisc *);
struct tty_ldisc *tty_ldisc_ref_wait(struct tty_struct *);

void tty_ldisc_flush(struct tty_struct *tty);

int tty_register_ldisc(struct tty_ldisc_ops *new_ldisc);
void tty_unregister_ldisc(struct tty_ldisc_ops *ldisc);
int tty_set_ldisc(struct tty_struct *tty, int disc);

#endif /* _LINUX_TTY_LDISC_H */

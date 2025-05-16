/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TTY_H
#define _LINUX_TTY_H

#include <linux/fs.h>
#include <linux/major.h>
#include <linux/termios.h>
#include <linux/workqueue.h>
#include <linux/tty_driver.h>
#include <linux/tty_ldisc.h>
#include <linux/tty_port.h>
#include <linux/mutex.h>
#include <linux/tty_flags.h>
#include <uapi/linux/tty.h>
#include <linux/rwsem.h>
#include <linux/llist.h>


/*
 * (Note: the *_driver.minor_start values 1, 64, 128, 192 are
 * hardcoded at present.)
 */
#define NR_UNIX98_PTY_DEFAULT	4096      /* Default maximum for Unix98 ptys */
#define NR_UNIX98_PTY_RESERVE	1024	  /* Default reserve for main devpts */
#define NR_UNIX98_PTY_MAX	(1 << MINORBITS) /* Absolute limit */

/*
 * This character is the same as _POSIX_VDISABLE: it cannot be used as
 * a c_cc[] character, but indicates that a particular special character
 * isn't in use (eg VINTR has no character etc)
 */
#define __DISABLED_CHAR '\0'

#define INTR_CHAR(tty) ((tty)->termios.c_cc[VINTR])
#define QUIT_CHAR(tty) ((tty)->termios.c_cc[VQUIT])
#define ERASE_CHAR(tty) ((tty)->termios.c_cc[VERASE])
#define KILL_CHAR(tty) ((tty)->termios.c_cc[VKILL])
#define EOF_CHAR(tty) ((tty)->termios.c_cc[VEOF])
#define TIME_CHAR(tty) ((tty)->termios.c_cc[VTIME])
#define MIN_CHAR(tty) ((tty)->termios.c_cc[VMIN])
#define SWTC_CHAR(tty) ((tty)->termios.c_cc[VSWTC])
#define START_CHAR(tty) ((tty)->termios.c_cc[VSTART])
#define STOP_CHAR(tty) ((tty)->termios.c_cc[VSTOP])
#define SUSP_CHAR(tty) ((tty)->termios.c_cc[VSUSP])
#define EOL_CHAR(tty) ((tty)->termios.c_cc[VEOL])
#define REPRINT_CHAR(tty) ((tty)->termios.c_cc[VREPRINT])
#define DISCARD_CHAR(tty) ((tty)->termios.c_cc[VDISCARD])
#define WERASE_CHAR(tty) ((tty)->termios.c_cc[VWERASE])
#define LNEXT_CHAR(tty)	((tty)->termios.c_cc[VLNEXT])
#define EOL2_CHAR(tty) ((tty)->termios.c_cc[VEOL2])

#define _I_FLAG(tty, f)	((tty)->termios.c_iflag & (f))
#define _O_FLAG(tty, f)	((tty)->termios.c_oflag & (f))
#define _C_FLAG(tty, f)	((tty)->termios.c_cflag & (f))
#define _L_FLAG(tty, f)	((tty)->termios.c_lflag & (f))

#define I_IGNBRK(tty)	_I_FLAG((tty), IGNBRK)
#define I_BRKINT(tty)	_I_FLAG((tty), BRKINT)
#define I_IGNPAR(tty)	_I_FLAG((tty), IGNPAR)
#define I_PARMRK(tty)	_I_FLAG((tty), PARMRK)
#define I_INPCK(tty)	_I_FLAG((tty), INPCK)
#define I_ISTRIP(tty)	_I_FLAG((tty), ISTRIP)
#define I_INLCR(tty)	_I_FLAG((tty), INLCR)
#define I_IGNCR(tty)	_I_FLAG((tty), IGNCR)
#define I_ICRNL(tty)	_I_FLAG((tty), ICRNL)
#define I_IUCLC(tty)	_I_FLAG((tty), IUCLC)
#define I_IXON(tty)	_I_FLAG((tty), IXON)
#define I_IXANY(tty)	_I_FLAG((tty), IXANY)
#define I_IXOFF(tty)	_I_FLAG((tty), IXOFF)
#define I_IMAXBEL(tty)	_I_FLAG((tty), IMAXBEL)
#define I_IUTF8(tty)	_I_FLAG((tty), IUTF8)

#define O_OPOST(tty)	_O_FLAG((tty), OPOST)
#define O_OLCUC(tty)	_O_FLAG((tty), OLCUC)
#define O_ONLCR(tty)	_O_FLAG((tty), ONLCR)
#define O_OCRNL(tty)	_O_FLAG((tty), OCRNL)
#define O_ONOCR(tty)	_O_FLAG((tty), ONOCR)
#define O_ONLRET(tty)	_O_FLAG((tty), ONLRET)
#define O_OFILL(tty)	_O_FLAG((tty), OFILL)
#define O_OFDEL(tty)	_O_FLAG((tty), OFDEL)
#define O_NLDLY(tty)	_O_FLAG((tty), NLDLY)
#define O_CRDLY(tty)	_O_FLAG((tty), CRDLY)
#define O_TABDLY(tty)	_O_FLAG((tty), TABDLY)
#define O_BSDLY(tty)	_O_FLAG((tty), BSDLY)
#define O_VTDLY(tty)	_O_FLAG((tty), VTDLY)
#define O_FFDLY(tty)	_O_FLAG((tty), FFDLY)

#define C_BAUD(tty)	_C_FLAG((tty), CBAUD)
#define C_CSIZE(tty)	_C_FLAG((tty), CSIZE)
#define C_CSTOPB(tty)	_C_FLAG((tty), CSTOPB)
#define C_CREAD(tty)	_C_FLAG((tty), CREAD)
#define C_PARENB(tty)	_C_FLAG((tty), PARENB)
#define C_PARODD(tty)	_C_FLAG((tty), PARODD)
#define C_HUPCL(tty)	_C_FLAG((tty), HUPCL)
#define C_CLOCAL(tty)	_C_FLAG((tty), CLOCAL)
#define C_CIBAUD(tty)	_C_FLAG((tty), CIBAUD)
#define C_CRTSCTS(tty)	_C_FLAG((tty), CRTSCTS)
#define C_CMSPAR(tty)	_C_FLAG((tty), CMSPAR)

#define L_ISIG(tty)	_L_FLAG((tty), ISIG)
#define L_ICANON(tty)	_L_FLAG((tty), ICANON)
#define L_XCASE(tty)	_L_FLAG((tty), XCASE)
#define L_ECHO(tty)	_L_FLAG((tty), ECHO)
#define L_ECHOE(tty)	_L_FLAG((tty), ECHOE)
#define L_ECHOK(tty)	_L_FLAG((tty), ECHOK)
#define L_ECHONL(tty)	_L_FLAG((tty), ECHONL)
#define L_NOFLSH(tty)	_L_FLAG((tty), NOFLSH)
#define L_TOSTOP(tty)	_L_FLAG((tty), TOSTOP)
#define L_ECHOCTL(tty)	_L_FLAG((tty), ECHOCTL)
#define L_ECHOPRT(tty)	_L_FLAG((tty), ECHOPRT)
#define L_ECHOKE(tty)	_L_FLAG((tty), ECHOKE)
#define L_FLUSHO(tty)	_L_FLAG((tty), FLUSHO)
#define L_PENDIN(tty)	_L_FLAG((tty), PENDIN)
#define L_IEXTEN(tty)	_L_FLAG((tty), IEXTEN)
#define L_EXTPROC(tty)	_L_FLAG((tty), EXTPROC)

struct device;
struct signal_struct;
struct tty_operations;

/**
 * struct tty_struct - state associated with a tty while open
 *
 * @kref: reference counting by tty_kref_get() and tty_kref_put(), reaching zero
 *	  frees the structure
 * @dev: class device or %NULL (e.g. ptys, serdev)
 * @driver: &struct tty_driver operating this tty
 * @ops: &struct tty_operations of @driver for this tty (open, close, etc.)
 * @index: index of this tty (e.g. to construct @name like tty12)
 * @ldisc_sem: protects line discipline changes (@ldisc) -- lock tty not pty
 * @ldisc: the current line discipline for this tty (n_tty by default)
 * @atomic_write_lock: protects against concurrent writers, i.e. locks
 *		       @write_cnt, @write_buf and similar
 * @legacy_mutex: leftover from history (BKL -> BTM -> @legacy_mutex),
 *		  protecting several operations on this tty
 * @throttle_mutex: protects against concurrent tty_throttle_safe() and
 *		    tty_unthrottle_safe() (but not tty_unthrottle())
 * @termios_rwsem: protects @termios and @termios_locked
 * @winsize_mutex: protects @winsize
 * @termios: termios for the current tty, copied from/to @driver.termios
 * @termios_locked: locked termios (by %TIOCGLCKTRMIOS and %TIOCSLCKTRMIOS
 *		    ioctls)
 * @name: name of the tty constructed by tty_line_name() (e.g. ttyS3)
 * @flags: bitwise OR of %TTY_THROTTLED, %TTY_IO_ERROR, ...
 * @count: count of open processes, reaching zero cancels all the work for
 *	   this tty and drops a @kref too (but does not free this tty)
 * @winsize: size of the terminal "window" (cf. @winsize_mutex)
 * @flow: flow settings grouped together
 * @flow.lock: lock for @flow members
 * @flow.stopped: tty stopped/started by stop_tty()/start_tty()
 * @flow.tco_stopped: tty stopped/started by %TCOOFF/%TCOON ioctls (it has
 *		      precedence over @flow.stopped)
 * @ctrl: control settings grouped together
 * @ctrl.lock: lock for @ctrl members
 * @ctrl.pgrp: process group of this tty (setpgrp(2))
 * @ctrl.session: session of this tty (setsid(2)). Writes are protected by both
 *		  @ctrl.lock and @legacy_mutex, readers must use at least one of
 *		  them.
 * @ctrl.pktstatus: packet mode status (bitwise OR of %TIOCPKT_ constants)
 * @ctrl.packet: packet mode enabled
 * @hw_stopped: not controlled by the tty layer, under @driver's control for CTS
 *		handling
 * @receive_room: bytes permitted to feed to @ldisc without any being lost
 * @flow_change: controls behavior of throttling, see tty_throttle_safe() and
 *		 tty_unthrottle_safe()
 * @link: link to another pty (master -> slave and vice versa)
 * @fasync: state for %O_ASYNC (for %SIGIO); managed by fasync_helper()
 * @write_wait: concurrent writers are waiting in this queue until they are
 *		allowed to write
 * @read_wait: readers wait for data in this queue
 * @hangup_work: normally a work to perform a hangup (do_tty_hangup()); while
 *		 freeing the tty, (re)used to release_one_tty()
 * @disc_data: pointer to @ldisc's private data (e.g. to &struct n_tty_data)
 * @driver_data: pointer to @driver's private data (e.g. &struct uart_state)
 * @files_lock:	protects @tty_files list
 * @tty_files: list of (re)openers of this tty (i.e. linked &struct
 *	       tty_file_private)
 * @closing: when set during close, n_tty processes only START & STOP chars
 * @write_buf: temporary buffer used during tty_write() to copy user data to
 * @write_cnt: count of bytes written in tty_write() to @write_buf
 * @SAK_work: if the tty has a pending do_SAK, it is queued here
 * @port: persistent storage for this device (i.e. &struct tty_port)
 *
 * All of the state associated with a tty while the tty is open. Persistent
 * storage for tty devices is referenced here as @port and is documented in
 * &struct tty_port.
 */
struct tty_struct {
	struct kref kref;
	int index;
	struct device *dev;
	struct tty_driver *driver;
	struct tty_port *port;
	const struct tty_operations *ops;

	struct tty_ldisc *ldisc;
	struct ld_semaphore ldisc_sem;

	struct mutex atomic_write_lock;
	struct mutex legacy_mutex;
	struct mutex throttle_mutex;
	struct rw_semaphore termios_rwsem;
	struct mutex winsize_mutex;
	struct ktermios termios, termios_locked;
	char name[64];
	unsigned long flags;
	int count;
	unsigned int receive_room;
	struct winsize winsize;

	struct {
		spinlock_t lock;
		bool stopped;
		bool tco_stopped;
	} flow;

	struct {
		struct pid *pgrp;
		struct pid *session;
		spinlock_t lock;
		unsigned char pktstatus;
		bool packet;
	} ctrl;

	bool hw_stopped;
	bool closing;
	int flow_change;

	struct tty_struct *link;
	struct fasync_struct *fasync;
	wait_queue_head_t write_wait;
	wait_queue_head_t read_wait;
	struct work_struct hangup_work;
	void *disc_data;
	void *driver_data;
	spinlock_t files_lock;
	int write_cnt;
	u8 *write_buf;

	struct list_head tty_files;

	struct work_struct SAK_work;
} __randomize_layout;

/* Each of a tty's open files has private_data pointing to tty_file_private */
struct tty_file_private {
	struct tty_struct *tty;
	struct file *file;
	struct list_head list;
};

/**
 * enum tty_struct_flags - TTY Struct Flags
 *
 * These bits are used in the :c:member:`tty_struct.flags` field.
 *
 * So that interrupts won't be able to mess up the queues,
 * copy_to_cooked must be atomic with respect to itself, as must
 * tty->write.  Thus, you must use the inline functions set_bit() and
 * clear_bit() to make things atomic.
 *
 * @TTY_THROTTLED:
 *	Driver input is throttled. The ldisc should call
 *	:c:member:`tty_driver.unthrottle()` in order to resume reception when
 *	it is ready to process more data (at threshold min).
 *
 * @TTY_IO_ERROR:
 *	If set, causes all subsequent userspace read/write calls on the tty to
 *	fail, returning -%EIO. (May be no ldisc too.)
 *
 * @TTY_OTHER_CLOSED:
 *	Device is a pty and the other side has closed.
 *
 * @TTY_EXCLUSIVE:
 *	Exclusive open mode (a single opener).
 *
 * @TTY_DO_WRITE_WAKEUP:
 *	If set, causes the driver to call the
 *	:c:member:`tty_ldisc_ops.write_wakeup()` method in order to resume
 *	transmission when it can accept more data to transmit.
 *
 * @TTY_LDISC_OPEN:
 *	Indicates that a line discipline is open. For debugging purposes only.
 *
 * @TTY_PTY_LOCK:
 *	A flag private to pty code to implement %TIOCSPTLCK/%TIOCGPTLCK logic.
 *
 * @TTY_NO_WRITE_SPLIT:
 *	Prevent driver from splitting up writes into smaller chunks (preserve
 *	write boundaries to driver).
 *
 * @TTY_HUPPED:
 *	The TTY was hung up. This is set post :c:member:`tty_driver.hangup()`.
 *
 * @TTY_HUPPING:
 *	The TTY is in the process of hanging up to abort potential readers.
 *
 * @TTY_LDISC_CHANGING:
 *	Line discipline for this TTY is being changed. I/O should not block
 *	when this is set. Use tty_io_nonblock() to check.
 *
 * @TTY_LDISC_HALTED:
 *	Line discipline for this TTY was stopped. No work should be queued to
 *	this ldisc.
 */
enum tty_struct_flags {
	TTY_THROTTLED,
	TTY_IO_ERROR,
	TTY_OTHER_CLOSED,
	TTY_EXCLUSIVE,
	TTY_DO_WRITE_WAKEUP,
	TTY_LDISC_OPEN,
	TTY_PTY_LOCK,
	TTY_NO_WRITE_SPLIT,
	TTY_HUPPED,
	TTY_HUPPING,
	TTY_LDISC_CHANGING,
	TTY_LDISC_HALTED,
};

static inline bool tty_io_nonblock(struct tty_struct *tty, struct file *file)
{
	return file->f_flags & O_NONBLOCK ||
		test_bit(TTY_LDISC_CHANGING, &tty->flags);
}

static inline bool tty_io_error(struct tty_struct *tty)
{
	return test_bit(TTY_IO_ERROR, &tty->flags);
}

static inline bool tty_throttled(struct tty_struct *tty)
{
	return test_bit(TTY_THROTTLED, &tty->flags);
}

#ifdef CONFIG_TTY
void tty_kref_put(struct tty_struct *tty);
struct pid *tty_get_pgrp(struct tty_struct *tty);
void tty_vhangup_self(void);
void disassociate_ctty(int priv);
dev_t tty_devnum(struct tty_struct *tty);
void proc_clear_tty(struct task_struct *p);
struct tty_struct *get_current_tty(void);
/* tty_io.c */
int __init tty_init(void);
const char *tty_name(const struct tty_struct *tty);
struct tty_struct *tty_kopen_exclusive(dev_t device);
struct tty_struct *tty_kopen_shared(dev_t device);
void tty_kclose(struct tty_struct *tty);
int tty_dev_name_to_number(const char *name, dev_t *number);
#else
static inline void tty_kref_put(struct tty_struct *tty)
{ }
static inline struct pid *tty_get_pgrp(struct tty_struct *tty)
{ return NULL; }
static inline void tty_vhangup_self(void)
{ }
static inline void disassociate_ctty(int priv)
{ }
static inline dev_t tty_devnum(struct tty_struct *tty)
{ return 0; }
static inline void proc_clear_tty(struct task_struct *p)
{ }
static inline struct tty_struct *get_current_tty(void)
{ return NULL; }
/* tty_io.c */
static inline int __init tty_init(void)
{ return 0; }
static inline const char *tty_name(const struct tty_struct *tty)
{ return "(none)"; }
static inline struct tty_struct *tty_kopen_exclusive(dev_t device)
{ return ERR_PTR(-ENODEV); }
static inline void tty_kclose(struct tty_struct *tty)
{ }
static inline int tty_dev_name_to_number(const char *name, dev_t *number)
{ return -ENOTSUPP; }
#endif

extern struct ktermios tty_std_termios;

int vcs_init(void);

extern const struct class tty_class;

/**
 * tty_kref_get - get a tty reference
 * @tty: tty device
 *
 * Returns: a new reference to a tty object
 *
 * Locking: The caller must hold sufficient locks/counts to ensure that their
 * existing reference cannot go away.
 */
static inline struct tty_struct *tty_kref_get(struct tty_struct *tty)
{
	if (tty)
		kref_get(&tty->kref);
	return tty;
}

const char *tty_driver_name(const struct tty_struct *tty);
void tty_wait_until_sent(struct tty_struct *tty, long timeout);
void stop_tty(struct tty_struct *tty);
void start_tty(struct tty_struct *tty);
void tty_write_message(struct tty_struct *tty, char *msg);
int tty_send_xchar(struct tty_struct *tty, u8 ch);
int tty_put_char(struct tty_struct *tty, u8 c);
unsigned int tty_chars_in_buffer(struct tty_struct *tty);
unsigned int tty_write_room(struct tty_struct *tty);
void tty_driver_flush_buffer(struct tty_struct *tty);
void tty_unthrottle(struct tty_struct *tty);
bool tty_throttle_safe(struct tty_struct *tty);
bool tty_unthrottle_safe(struct tty_struct *tty);
int tty_do_resize(struct tty_struct *tty, struct winsize *ws);
int tty_get_icount(struct tty_struct *tty,
		struct serial_icounter_struct *icount);
int tty_get_tiocm(struct tty_struct *tty);
int is_current_pgrp_orphaned(void);
void tty_hangup(struct tty_struct *tty);
void tty_vhangup(struct tty_struct *tty);
int tty_hung_up_p(struct file *filp);
void do_SAK(struct tty_struct *tty);
void __do_SAK(struct tty_struct *tty);
void no_tty(void);
speed_t tty_termios_baud_rate(const struct ktermios *termios);
void tty_termios_encode_baud_rate(struct ktermios *termios, speed_t ibaud,
		speed_t obaud);
void tty_encode_baud_rate(struct tty_struct *tty, speed_t ibaud,
		speed_t obaud);

/**
 * tty_get_baud_rate - get tty bit rates
 * @tty: tty to query
 *
 * Returns: the baud rate as an integer for this terminal
 *
 * Locking: The termios lock must be held by the caller.
 */
static inline speed_t tty_get_baud_rate(const struct tty_struct *tty)
{
	return tty_termios_baud_rate(&tty->termios);
}

unsigned char tty_get_char_size(unsigned int cflag);
unsigned char tty_get_frame_size(unsigned int cflag);

void tty_termios_copy_hw(struct ktermios *new, const struct ktermios *old);
bool tty_termios_hw_change(const struct ktermios *a, const struct ktermios *b);
int tty_set_termios(struct tty_struct *tty, struct ktermios *kt);

void tty_wakeup(struct tty_struct *tty);

int tty_mode_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg);
int tty_perform_flush(struct tty_struct *tty, unsigned long arg);
struct tty_struct *tty_init_dev(struct tty_driver *driver, int idx);
void tty_release_struct(struct tty_struct *tty, int idx);
void tty_init_termios(struct tty_struct *tty);
void tty_save_termios(struct tty_struct *tty);
int tty_standard_install(struct tty_driver *driver,
		struct tty_struct *tty);

extern struct mutex tty_mutex;

/* n_tty.c */
void n_tty_inherit_ops(struct tty_ldisc_ops *ops);
#ifdef CONFIG_TTY
void __init n_tty_init(void);
#else
static inline void n_tty_init(void) { }
#endif

/* tty_audit.c */
#ifdef CONFIG_AUDIT
void tty_audit_exit(void);
void tty_audit_fork(struct signal_struct *sig);
int tty_audit_push(void);
#else
static inline void tty_audit_exit(void)
{
}
static inline void tty_audit_fork(struct signal_struct *sig)
{
}
static inline int tty_audit_push(void)
{
	return 0;
}
#endif

/* tty_ioctl.c */
int n_tty_ioctl_helper(struct tty_struct *tty, unsigned int cmd,
		unsigned long arg);

/* vt.c */

int vt_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg);

long vt_compat_ioctl(struct tty_struct *tty, unsigned int cmd,
		unsigned long arg);

/* tty_mutex.c */
/* functions for preparation of BKL removal */
void tty_lock(struct tty_struct *tty);
int  tty_lock_interruptible(struct tty_struct *tty);
void tty_unlock(struct tty_struct *tty);
void tty_lock_slave(struct tty_struct *tty);
void tty_unlock_slave(struct tty_struct *tty);
void tty_set_lock_subclass(struct tty_struct *tty);

#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TTY_H
#define _LINUX_TTY_H

#include <linux/fs.h>
#include <linux/major.h>
#include <linux/termios.h>
#include <linux/workqueue.h>
#include <linux/tty_buffer.h>
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
 * @flow.lock: lock for flow members
 * @flow.stopped: tty stopped/started by tty_stop/tty_start
 * @flow.tco_stopped: tty stopped/started by TCOOFF/TCOON ioctls (it has
 *		      precedense over @flow.stopped)
 * @flow.unused: alignment for Alpha, so that no members other than @flow.* are
 *		 modified by the same 64b word store. The @flow's __aligned is
 *		 there for the very same reason.
 * @ctrl.lock: lock for ctrl members
 * @ctrl.pgrp: process group of this tty (setpgrp(2))
 * @ctrl.session: session of this tty (setsid(2)). Writes are protected by both
 *		  @ctrl.lock and legacy mutex, readers must use at least one of
 *		  them.
 * @ctrl.pktstatus: packet mode status (bitwise OR of TIOCPKT_* constants)
 * @ctrl.packet: packet mode enabled
 *
 * All of the state associated with a tty while the tty is open. Persistent
 * storage for tty devices is referenced here as @port in struct tty_port.
 */
struct tty_struct {
	int	magic;
	struct kref kref;
	struct device *dev;	/* class device or NULL (e.g. ptys, serdev) */
	struct tty_driver *driver;
	const struct tty_operations *ops;
	int index;

	/* Protects ldisc changes: Lock tty not pty */
	struct ld_semaphore ldisc_sem;
	struct tty_ldisc *ldisc;

	struct mutex atomic_write_lock;
	struct mutex legacy_mutex;
	struct mutex throttle_mutex;
	struct rw_semaphore termios_rwsem;
	struct mutex winsize_mutex;
	/* Termios values are protected by the termios rwsem */
	struct ktermios termios, termios_locked;
	char name[64];
	unsigned long flags;
	int count;
	struct winsize winsize;		/* winsize_mutex */

	struct {
		spinlock_t lock;
		bool stopped;
		bool tco_stopped;
		unsigned long unused[0];
	} __aligned(sizeof(unsigned long)) flow;

	struct {
		spinlock_t lock;
		struct pid *pgrp;
		struct pid *session;
		unsigned char pktstatus;
		bool packet;
		unsigned long unused[0];
	} __aligned(sizeof(unsigned long)) ctrl;

	int hw_stopped;
	unsigned int receive_room;	/* Bytes free for queue */
	int flow_change;

	struct tty_struct *link;
	struct fasync_struct *fasync;
	wait_queue_head_t write_wait;
	wait_queue_head_t read_wait;
	struct work_struct hangup_work;
	void *disc_data;
	void *driver_data;
	spinlock_t files_lock;		/* protects tty_files list */
	struct list_head tty_files;

#define N_TTY_BUF_SIZE 4096

	int closing;
	unsigned char *write_buf;
	int write_cnt;
	/* If the tty has a pending do_SAK, queue it here - akpm */
	struct work_struct SAK_work;
	struct tty_port *port;
} __randomize_layout;

/* Each of a tty's open files has private_data pointing to tty_file_private */
struct tty_file_private {
	struct tty_struct *tty;
	struct file *file;
	struct list_head list;
};

/* tty magic number */
#define TTY_MAGIC		0x5401

/*
 * These bits are used in the flags field of the tty structure.
 *
 * So that interrupts won't be able to mess up the queues,
 * copy_to_cooked must be atomic with respect to itself, as must
 * tty->write.  Thus, you must use the inline functions set_bit() and
 * clear_bit() to make things atomic.
 */
#define TTY_THROTTLED 		0	/* Call unthrottle() at threshold min */
#define TTY_IO_ERROR 		1	/* Cause an I/O error (may be no ldisc too) */
#define TTY_OTHER_CLOSED 	2	/* Other side (if any) has closed */
#define TTY_EXCLUSIVE 		3	/* Exclusive open mode */
#define TTY_DO_WRITE_WAKEUP 	5	/* Call write_wakeup after queuing new */
#define TTY_LDISC_OPEN	 	11	/* Line discipline is open */
#define TTY_PTY_LOCK 		16	/* pty private */
#define TTY_NO_WRITE_SPLIT 	17	/* Preserve write boundaries to driver */
#define TTY_HUPPED 		18	/* Post driver->hangup() */
#define TTY_HUPPING		19	/* Hangup in progress */
#define TTY_LDISC_CHANGING	20	/* Change pending - non-block IO */
#define TTY_LDISC_HALTED	22	/* Line discipline is halted */

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

extern struct class *tty_class;

/**
 *	tty_kref_get		-	get a tty reference
 *	@tty: tty device
 *
 *	Return a new reference to a tty object. The caller must hold
 *	sufficient locks/counts to ensure that their existing reference cannot
 *	go away
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
int tty_send_xchar(struct tty_struct *tty, char ch);
int tty_put_char(struct tty_struct *tty, unsigned char c);
unsigned int tty_chars_in_buffer(struct tty_struct *tty);
unsigned int tty_write_room(struct tty_struct *tty);
void tty_driver_flush_buffer(struct tty_struct *tty);
void tty_unthrottle(struct tty_struct *tty);
int tty_throttle_safe(struct tty_struct *tty);
int tty_unthrottle_safe(struct tty_struct *tty);
int tty_do_resize(struct tty_struct *tty, struct winsize *ws);
int tty_get_icount(struct tty_struct *tty,
		struct serial_icounter_struct *icount);
int is_current_pgrp_orphaned(void);
void tty_hangup(struct tty_struct *tty);
void tty_vhangup(struct tty_struct *tty);
int tty_hung_up_p(struct file *filp);
void do_SAK(struct tty_struct *tty);
void __do_SAK(struct tty_struct *tty);
void no_tty(void);
speed_t tty_termios_baud_rate(struct ktermios *termios);
void tty_termios_encode_baud_rate(struct ktermios *termios, speed_t ibaud,
		speed_t obaud);
void tty_encode_baud_rate(struct tty_struct *tty, speed_t ibaud,
		speed_t obaud);

/**
 *	tty_get_baud_rate	-	get tty bit rates
 *	@tty: tty to query
 *
 *	Returns the baud rate as an integer for this terminal. The
 *	termios lock must be held by the caller and the terminal bit
 *	flags may be updated.
 *
 *	Locking: none
 */
static inline speed_t tty_get_baud_rate(struct tty_struct *tty)
{
	return tty_termios_baud_rate(&tty->termios);
}

unsigned char tty_get_char_size(unsigned int cflag);
unsigned char tty_get_frame_size(unsigned int cflag);

void tty_termios_copy_hw(struct ktermios *new, struct ktermios *old);
int tty_termios_hw_change(const struct ktermios *a, const struct ktermios *b);
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

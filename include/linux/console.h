/*
 *  linux/include/linux/console.h
 *
 *  Copyright (C) 1993        Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Changed:
 * 10-Mar-94: Arno Griffioen: Conversion for vt100 emulator port from PC LINUX
 */

#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/irq_work.h>
#include <linux/rculist.h>
#include <linux/rcuwait.h>
#include <linux/types.h>
#include <linux/vesa.h>

struct vc_data;
struct console_font_op;
struct console_font;
struct module;
struct tty_struct;
struct notifier_block;

enum con_scroll {
	SM_UP,
	SM_DOWN,
};

enum vc_intensity;

/**
 * struct consw - callbacks for consoles
 *
 * @owner:      the module to get references of when this console is used
 * @con_startup: set up the console and return its name (like VGA, EGA, ...)
 * @con_init:   initialize the console on @vc. @init is true for the very first
 *		call on this @vc.
 * @con_deinit: deinitialize the console from @vc.
 * @con_clear:  erase @count characters at [@x, @y] on @vc. @count >= 1.
 * @con_putc:   emit one character with attributes @ca to [@x, @y] on @vc.
 *		(optional -- @con_putcs would be called instead)
 * @con_putcs:  emit @count characters with attributes @s to [@x, @y] on @vc.
 * @con_cursor: enable/disable cursor depending on @enable
 * @con_scroll: move lines from @top to @bottom in direction @dir by @lines.
 *		Return true if no generic handling should be done.
 *		Invoked by csi_M and printing to the console.
 * @con_switch: notifier about the console switch; it is supposed to return
 *		true if a redraw is needed.
 * @con_blank:  blank/unblank the console. The target mode is passed in @blank.
 *		@mode_switch is set if changing from/to text/graphics. The hook
 *		is supposed to return true if a redraw is needed.
 * @con_font_set: set console @vc font to @font with height @vpitch. @flags can
 *		be %KD_FONT_FLAG_DONT_RECALC. (optional)
 * @con_font_get: fetch the current font on @vc of height @vpitch into @font.
 *		(optional)
 * @con_font_default: set default font on @vc. @name can be %NULL or font name
 *		to search for. @font can be filled back. (optional)
 * @con_resize:	resize the @vc console to @width x @height. @from_user is true
 *		when this change comes from the user space.
 * @con_set_palette: sets the palette of the console @vc to @table (optional)
 * @con_scrolldelta: the contents of the console should be scrolled by @lines.
 *		     Invoked by user. (optional)
 * @con_set_origin: set origin (see &vc_data::vc_origin) of the @vc. If not
 *		provided or returns false, the origin is set to
 *		@vc->vc_screenbuf. (optional)
 * @con_save_screen: save screen content into @vc->vc_screenbuf. Called e.g.
 *		upon entering graphics. (optional)
 * @con_build_attr: build attributes based on @color, @intensity and other
 *		parameters. The result is used for both normal and erase
 *		characters. (optional)
 * @con_invert_region: invert a region of length @count on @vc starting at @p.
 *		(optional)
 * @con_debug_enter: prepare the console for the debugger. This includes, but
 *		is not limited to, unblanking the console, loading an
 *		appropriate palette, and allowing debugger generated output.
 *		(optional)
 * @con_debug_leave: restore the console to its pre-debug state as closely as
 *		possible. (optional)
 */
struct consw {
	struct module *owner;
	const char *(*con_startup)(void);
	void	(*con_init)(struct vc_data *vc, bool init);
	void	(*con_deinit)(struct vc_data *vc);
	void	(*con_clear)(struct vc_data *vc, unsigned int y,
			     unsigned int x, unsigned int count);
	void	(*con_putc)(struct vc_data *vc, u16 ca, unsigned int y,
			    unsigned int x);
	void	(*con_putcs)(struct vc_data *vc, const u16 *s,
			     unsigned int count, unsigned int ypos,
			     unsigned int xpos);
	void	(*con_cursor)(struct vc_data *vc, bool enable);
	bool	(*con_scroll)(struct vc_data *vc, unsigned int top,
			unsigned int bottom, enum con_scroll dir,
			unsigned int lines);
	bool	(*con_switch)(struct vc_data *vc);
	bool	(*con_blank)(struct vc_data *vc, enum vesa_blank_mode blank,
			     bool mode_switch);
	int	(*con_font_set)(struct vc_data *vc,
				const struct console_font *font,
				unsigned int vpitch, unsigned int flags);
	int	(*con_font_get)(struct vc_data *vc, struct console_font *font,
			unsigned int vpitch);
	int	(*con_font_default)(struct vc_data *vc,
			struct console_font *font, const char *name);
	int     (*con_resize)(struct vc_data *vc, unsigned int width,
			      unsigned int height, bool from_user);
	void	(*con_set_palette)(struct vc_data *vc,
			const unsigned char *table);
	void	(*con_scrolldelta)(struct vc_data *vc, int lines);
	bool	(*con_set_origin)(struct vc_data *vc);
	void	(*con_save_screen)(struct vc_data *vc);
	u8	(*con_build_attr)(struct vc_data *vc, u8 color,
			enum vc_intensity intensity,
			bool blink, bool underline, bool reverse, bool italic);
	void	(*con_invert_region)(struct vc_data *vc, u16 *p, int count);
	void	(*con_debug_enter)(struct vc_data *vc);
	void	(*con_debug_leave)(struct vc_data *vc);
};

extern const struct consw *conswitchp;

extern const struct consw dummy_con;	/* dummy console buffer */
extern const struct consw vga_con;	/* VGA text console */
extern const struct consw newport_con;	/* SGI Newport console  */

struct screen_info;
#ifdef CONFIG_VGA_CONSOLE
void vgacon_register_screen(struct screen_info *si);
#else
static inline void vgacon_register_screen(struct screen_info *si) { }
#endif

int con_is_bound(const struct consw *csw);
int do_unregister_con_driver(const struct consw *csw);
int do_take_over_console(const struct consw *sw, int first, int last, int deflt);
void give_up_console(const struct consw *sw);
#ifdef CONFIG_VT
void con_debug_enter(struct vc_data *vc);
void con_debug_leave(void);
#else
static inline void con_debug_enter(struct vc_data *vc) { }
static inline void con_debug_leave(void) { }
#endif

/*
 * The interface for a console, or any other device that wants to capture
 * console messages (printer driver?)
 */

/**
 * enum cons_flags - General console flags
 * @CON_PRINTBUFFER:	Used by newly registered consoles to avoid duplicate
 *			output of messages that were already shown by boot
 *			consoles or read by userspace via syslog() syscall.
 * @CON_CONSDEV:	Indicates that the console driver is backing
 *			/dev/console.
 * @CON_ENABLED:	Indicates if a console is allowed to print records. If
 *			false, the console also will not advance to later
 *			records.
 * @CON_BOOT:		Marks the console driver as early console driver which
 *			is used during boot before the real driver becomes
 *			available. It will be automatically unregistered
 *			when the real console driver is registered unless
 *			"keep_bootcon" parameter is used.
 * @CON_ANYTIME:	A misnomed historical flag which tells the core code
 *			that the legacy @console::write callback can be invoked
 *			on a CPU which is marked OFFLINE. That is misleading as
 *			it suggests that there is no contextual limit for
 *			invoking the callback. The original motivation was
 *			readiness of the per-CPU areas.
 * @CON_BRL:		Indicates a braille device which is exempt from
 *			receiving the printk spam for obvious reasons.
 * @CON_EXTENDED:	The console supports the extended output format of
 *			/dev/kmesg which requires a larger output buffer.
 * @CON_SUSPENDED:	Indicates if a console is suspended. If true, the
 *			printing callbacks must not be called.
 * @CON_NBCON:		Console can operate outside of the legacy style console_lock
 *			constraints.
 */
enum cons_flags {
	CON_PRINTBUFFER		= BIT(0),
	CON_CONSDEV		= BIT(1),
	CON_ENABLED		= BIT(2),
	CON_BOOT		= BIT(3),
	CON_ANYTIME		= BIT(4),
	CON_BRL			= BIT(5),
	CON_EXTENDED		= BIT(6),
	CON_SUSPENDED		= BIT(7),
	CON_NBCON		= BIT(8),
};

/**
 * struct nbcon_state - console state for nbcon consoles
 * @atom:	Compound of the state fields for atomic operations
 *
 * @req_prio:		The priority of a handover request
 * @prio:		The priority of the current owner
 * @unsafe:		Console is busy in a non takeover region
 * @unsafe_takeover:	A hostile takeover in an unsafe state happened in the
 *			past. The console cannot be safe until re-initialized.
 * @cpu:		The CPU on which the owner runs
 *
 * To be used for reading and preparing of the value stored in the nbcon
 * state variable @console::nbcon_state.
 *
 * The @prio and @req_prio fields are particularly important to allow
 * spin-waiting to timeout and give up without the risk of a waiter being
 * assigned the lock after giving up.
 */
struct nbcon_state {
	union {
		unsigned int	atom;
		struct {
			unsigned int prio		:  2;
			unsigned int req_prio		:  2;
			unsigned int unsafe		:  1;
			unsigned int unsafe_takeover	:  1;
			unsigned int cpu		: 24;
		};
	};
};

/*
 * The nbcon_state struct is used to easily create and interpret values that
 * are stored in the @console::nbcon_state variable. Ensure this struct stays
 * within the size boundaries of the atomic variable's underlying type in
 * order to avoid any accidental truncation.
 */
static_assert(sizeof(struct nbcon_state) <= sizeof(int));

/**
 * enum nbcon_prio - console owner priority for nbcon consoles
 * @NBCON_PRIO_NONE:		Unused
 * @NBCON_PRIO_NORMAL:		Normal (non-emergency) usage
 * @NBCON_PRIO_EMERGENCY:	Emergency output (WARN/OOPS...)
 * @NBCON_PRIO_PANIC:		Panic output
 * @NBCON_PRIO_MAX:		The number of priority levels
 *
 * A higher priority context can takeover the console when it is
 * in the safe state. The final attempt to flush consoles in panic()
 * can be allowed to do so even in an unsafe state (Hope and pray).
 */
enum nbcon_prio {
	NBCON_PRIO_NONE = 0,
	NBCON_PRIO_NORMAL,
	NBCON_PRIO_EMERGENCY,
	NBCON_PRIO_PANIC,
	NBCON_PRIO_MAX,
};

struct console;
struct printk_buffers;

/**
 * struct nbcon_context - Context for console acquire/release
 * @console:			The associated console
 * @spinwait_max_us:		Limit for spin-wait acquire
 * @prio:			Priority of the context
 * @allow_unsafe_takeover:	Allow performing takeover even if unsafe. Can
 *				be used only with NBCON_PRIO_PANIC @prio. It
 *				might cause a system freeze when the console
 *				is used later.
 * @backlog:			Ringbuffer has pending records
 * @pbufs:			Pointer to the text buffer for this context
 * @seq:			The sequence number to print for this context
 */
struct nbcon_context {
	/* members set by caller */
	struct console		*console;
	unsigned int		spinwait_max_us;
	enum nbcon_prio		prio;
	unsigned int		allow_unsafe_takeover	: 1;

	/* members set by emit */
	unsigned int		backlog			: 1;

	/* members set by acquire */
	struct printk_buffers	*pbufs;
	u64			seq;
};

/**
 * struct nbcon_write_context - Context handed to the nbcon write callbacks
 * @ctxt:		The core console context
 * @outbuf:		Pointer to the text buffer for output
 * @len:		Length to write
 * @unsafe_takeover:	If a hostile takeover in an unsafe state has occurred
 */
struct nbcon_write_context {
	struct nbcon_context	__private ctxt;
	char			*outbuf;
	unsigned int		len;
	bool			unsafe_takeover;
};

/**
 * struct console - The console descriptor structure
 * @name:		The name of the console driver
 * @write:		Legacy write callback to output messages (Optional)
 * @read:		Read callback for console input (Optional)
 * @device:		The underlying TTY device driver (Optional)
 * @unblank:		Callback to unblank the console (Optional)
 * @setup:		Callback for initializing the console (Optional)
 * @exit:		Callback for teardown of the console (Optional)
 * @match:		Callback for matching a console (Optional)
 * @flags:		Console flags. See enum cons_flags
 * @index:		Console index, e.g. port number
 * @cflag:		TTY control mode flags
 * @ispeed:		TTY input speed
 * @ospeed:		TTY output speed
 * @seq:		Sequence number of the next ringbuffer record to print
 * @dropped:		Number of unreported dropped ringbuffer records
 * @data:		Driver private data
 * @node:		hlist node for the console list
 *
 * @nbcon_state:	State for nbcon consoles
 * @nbcon_seq:		Sequence number of the next record for nbcon to print
 * @nbcon_device_ctxt:	Context available for non-printing operations
 * @nbcon_prev_seq:	Seq num the previous nbcon owner was assigned to print
 * @pbufs:		Pointer to nbcon private buffer
 * @kthread:		Printer kthread for this console
 * @rcuwait:		RCU-safe wait object for @kthread waking
 * @irq_work:		Defer @kthread waking to IRQ work context
 */
struct console {
	char			name[16];
	void			(*write)(struct console *co, const char *s, unsigned int count);
	int			(*read)(struct console *co, char *s, unsigned int count);
	struct tty_driver	*(*device)(struct console *co, int *index);
	void			(*unblank)(void);
	int			(*setup)(struct console *co, char *options);
	int			(*exit)(struct console *co);
	int			(*match)(struct console *co, char *name, int idx, char *options);
	short			flags;
	short			index;
	int			cflag;
	uint			ispeed;
	uint			ospeed;
	u64			seq;
	unsigned long		dropped;
	void			*data;
	struct hlist_node	node;

	/* nbcon console specific members */

	/**
	 * @write_atomic:
	 *
	 * NBCON callback to write out text in any context. (Optional)
	 *
	 * This callback is called with the console already acquired. However,
	 * a higher priority context is allowed to take it over by default.
	 *
	 * The callback must call nbcon_enter_unsafe() and nbcon_exit_unsafe()
	 * around any code where the takeover is not safe, for example, when
	 * manipulating the serial port registers.
	 *
	 * nbcon_enter_unsafe() will fail if the context has lost the console
	 * ownership in the meantime. In this case, the callback is no longer
	 * allowed to go forward. It must back out immediately and carefully.
	 * The buffer content is also no longer trusted since it no longer
	 * belongs to the context.
	 *
	 * The callback should allow the takeover whenever it is safe. It
	 * increases the chance to see messages when the system is in trouble.
	 * If the driver must reacquire ownership in order to finalize or
	 * revert hardware changes, nbcon_reacquire_nobuf() can be used.
	 * However, on reacquire the buffer content is no longer available. A
	 * reacquire cannot be used to resume printing.
	 *
	 * The callback can be called from any context (including NMI).
	 * Therefore it must avoid usage of any locking and instead rely
	 * on the console ownership for synchronization.
	 */
	void (*write_atomic)(struct console *con, struct nbcon_write_context *wctxt);

	/**
	 * @write_thread:
	 *
	 * NBCON callback to write out text in task context.
	 *
	 * This callback must be called only in task context with both
	 * device_lock() and the nbcon console acquired with
	 * NBCON_PRIO_NORMAL.
	 *
	 * The same rules for console ownership verification and unsafe
	 * sections handling applies as with write_atomic().
	 *
	 * The console ownership handling is necessary for synchronization
	 * against write_atomic() which is synchronized only via the context.
	 *
	 * The device_lock() provides the primary serialization for operations
	 * on the device. It might be as relaxed (mutex)[*] or as tight
	 * (disabled preemption and interrupts) as needed. It allows
	 * the kthread to operate in the least restrictive mode[**].
	 *
	 * [*] Standalone nbcon_context_try_acquire() is not safe with
	 *     the preemption enabled, see nbcon_owner_matches(). But it
	 *     can be safe when always called in the preemptive context
	 *     under the device_lock().
	 *
	 * [**] The device_lock() makes sure that nbcon_context_try_acquire()
	 *      would never need to spin which is important especially with
	 *      PREEMPT_RT.
	 */
	void (*write_thread)(struct console *con, struct nbcon_write_context *wctxt);

	/**
	 * @device_lock:
	 *
	 * NBCON callback to begin synchronization with driver code.
	 *
	 * Console drivers typically must deal with access to the hardware
	 * via user input/output (such as an interactive login shell) and
	 * output of kernel messages via printk() calls. This callback is
	 * called by the printk-subsystem whenever it needs to synchronize
	 * with hardware access by the driver. It should be implemented to
	 * use whatever synchronization mechanism the driver is using for
	 * itself (for example, the port lock for uart serial consoles).
	 *
	 * The callback is always called from task context. It may use any
	 * synchronization method required by the driver.
	 *
	 * IMPORTANT: The callback MUST disable migration. The console driver
	 *	may be using a synchronization mechanism that already takes
	 *	care of this (such as spinlocks). Otherwise this function must
	 *	explicitly call migrate_disable().
	 *
	 * The flags argument is provided as a convenience to the driver. It
	 * will be passed again to device_unlock(). It can be ignored if the
	 * driver does not need it.
	 */
	void (*device_lock)(struct console *con, unsigned long *flags);

	/**
	 * @device_unlock:
	 *
	 * NBCON callback to finish synchronization with driver code.
	 *
	 * It is the counterpart to device_lock().
	 *
	 * This callback is always called from task context. It must
	 * appropriately re-enable migration (depending on how device_lock()
	 * disabled migration).
	 *
	 * The flags argument is the value of the same variable that was
	 * passed to device_lock().
	 */
	void (*device_unlock)(struct console *con, unsigned long flags);

	atomic_t		__private nbcon_state;
	atomic_long_t		__private nbcon_seq;
	struct nbcon_context	__private nbcon_device_ctxt;
	atomic_long_t           __private nbcon_prev_seq;

	struct printk_buffers	*pbufs;
	struct task_struct	*kthread;
	struct rcuwait		rcuwait;
	struct irq_work		irq_work;
};

#ifdef CONFIG_LOCKDEP
extern void lockdep_assert_console_list_lock_held(void);
#else
static inline void lockdep_assert_console_list_lock_held(void)
{
}
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
extern bool console_srcu_read_lock_is_held(void);
#else
static inline bool console_srcu_read_lock_is_held(void)
{
	return 1;
}
#endif

extern int console_srcu_read_lock(void);
extern void console_srcu_read_unlock(int cookie);

extern void console_list_lock(void) __acquires(console_mutex);
extern void console_list_unlock(void) __releases(console_mutex);

extern struct hlist_head console_list;

/**
 * console_srcu_read_flags - Locklessly read flags of a possibly registered
 *				console
 * @con:	struct console pointer of console to read flags from
 *
 * Locklessly reading @con->flags provides a consistent read value because
 * there is at most one CPU modifying @con->flags and that CPU is using only
 * read-modify-write operations to do so.
 *
 * Requires console_srcu_read_lock to be held, which implies that @con might
 * be a registered console. The purpose of holding console_srcu_read_lock is
 * to guarantee that the console state is valid (CON_SUSPENDED/CON_ENABLED)
 * and that no exit/cleanup routines will run if the console is currently
 * undergoing unregistration.
 *
 * If the caller is holding the console_list_lock or it is _certain_ that
 * @con is not and will not become registered, the caller may read
 * @con->flags directly instead.
 *
 * Context: Any context.
 * Return: The current value of the @con->flags field.
 */
static inline short console_srcu_read_flags(const struct console *con)
{
	WARN_ON_ONCE(!console_srcu_read_lock_is_held());

	/*
	 * The READ_ONCE() matches the WRITE_ONCE() when @flags are modified
	 * for registered consoles with console_srcu_write_flags().
	 */
	return data_race(READ_ONCE(con->flags));
}

/**
 * console_srcu_write_flags - Write flags for a registered console
 * @con:	struct console pointer of console to write flags to
 * @flags:	new flags value to write
 *
 * Only use this function to write flags for registered consoles. It
 * requires holding the console_list_lock.
 *
 * Context: Any context.
 */
static inline void console_srcu_write_flags(struct console *con, short flags)
{
	lockdep_assert_console_list_lock_held();

	/* This matches the READ_ONCE() in console_srcu_read_flags(). */
	WRITE_ONCE(con->flags, flags);
}

/* Variant of console_is_registered() when the console_list_lock is held. */
static inline bool console_is_registered_locked(const struct console *con)
{
	lockdep_assert_console_list_lock_held();
	return !hlist_unhashed(&con->node);
}

/*
 * console_is_registered - Check if the console is registered
 * @con:	struct console pointer of console to check
 *
 * Context: Process context. May sleep while acquiring console list lock.
 * Return: true if the console is in the console list, otherwise false.
 *
 * If false is returned for a console that was previously registered, it
 * can be assumed that the console's unregistration is fully completed,
 * including the exit() callback after console list removal.
 */
static inline bool console_is_registered(const struct console *con)
{
	bool ret;

	console_list_lock();
	ret = console_is_registered_locked(con);
	console_list_unlock();
	return ret;
}

/**
 * for_each_console_srcu() - Iterator over registered consoles
 * @con:	struct console pointer used as loop cursor
 *
 * Although SRCU guarantees the console list will be consistent, the
 * struct console fields may be updated by other CPUs while iterating.
 *
 * Requires console_srcu_read_lock to be held. Can be invoked from
 * any context.
 */
#define for_each_console_srcu(con)					\
	hlist_for_each_entry_srcu(con, &console_list, node,		\
				  console_srcu_read_lock_is_held())

/**
 * for_each_console() - Iterator over registered consoles
 * @con:	struct console pointer used as loop cursor
 *
 * The console list and the &console.flags are immutable while iterating.
 *
 * Requires console_list_lock to be held.
 */
#define for_each_console(con)						\
	lockdep_assert_console_list_lock_held();			\
	hlist_for_each_entry(con, &console_list, node)

#ifdef CONFIG_PRINTK
extern void nbcon_cpu_emergency_enter(void);
extern void nbcon_cpu_emergency_exit(void);
extern bool nbcon_can_proceed(struct nbcon_write_context *wctxt);
extern bool nbcon_enter_unsafe(struct nbcon_write_context *wctxt);
extern bool nbcon_exit_unsafe(struct nbcon_write_context *wctxt);
extern void nbcon_reacquire_nobuf(struct nbcon_write_context *wctxt);
#else
static inline void nbcon_cpu_emergency_enter(void) { }
static inline void nbcon_cpu_emergency_exit(void) { }
static inline bool nbcon_can_proceed(struct nbcon_write_context *wctxt) { return false; }
static inline bool nbcon_enter_unsafe(struct nbcon_write_context *wctxt) { return false; }
static inline bool nbcon_exit_unsafe(struct nbcon_write_context *wctxt) { return false; }
static inline void nbcon_reacquire_nobuf(struct nbcon_write_context *wctxt) { }
#endif

extern int console_set_on_cmdline;
extern struct console *early_console;

enum con_flush_mode {
	CONSOLE_FLUSH_PENDING,
	CONSOLE_REPLAY_ALL,
};

extern int add_preferred_console(const char *name, const short idx, char *options);
extern void console_force_preferred_locked(struct console *con);
extern void register_console(struct console *);
extern int unregister_console(struct console *);
extern void console_lock(void);
extern int console_trylock(void);
extern void console_unlock(void);
extern void console_conditional_schedule(void);
extern void console_unblank(void);
extern void console_flush_on_panic(enum con_flush_mode mode);
extern struct tty_driver *console_device(int *);
extern void console_suspend(struct console *);
extern void console_resume(struct console *);
extern int is_console_locked(void);
extern int braille_register_console(struct console *, int index,
		char *console_options, char *braille_options);
extern int braille_unregister_console(struct console *);
#ifdef CONFIG_TTY
extern void console_sysfs_notify(void);
#else
static inline void console_sysfs_notify(void)
{ }
#endif
extern bool console_suspend_enabled;

/* Suspend and resume console messages over PM events */
extern void console_suspend_all(void);
extern void console_resume_all(void);

int mda_console_init(void);

void vcs_make_sysfs(int index);
void vcs_remove_sysfs(int index);

/* Some debug stub to catch some of the obvious races in the VT code */
#define WARN_CONSOLE_UNLOCKED()						\
	WARN_ON(!atomic_read(&ignore_console_lock_warning) &&		\
		!is_console_locked() && !oops_in_progress)
/*
 * Increment ignore_console_lock_warning if you need to quiet
 * WARN_CONSOLE_UNLOCKED() for debugging purposes.
 */
extern atomic_t ignore_console_lock_warning;

extern void console_init(void);

/* For deferred console takeover */
void dummycon_register_output_notifier(struct notifier_block *nb);
void dummycon_unregister_output_notifier(struct notifier_block *nb);

#endif /* _LINUX_CONSOLE_H */

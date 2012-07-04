#ifndef _LINUX_IRQ_H
#define _LINUX_IRQ_H

/*
 * Please do not include this file in generic code.  There is currently
 * no requirement for any architecture to implement anything held
 * within this file.
 *
 * Thanks. --rmk
 */

#include <linux/smp.h>

#ifndef CONFIG_S390

#include <linux/linkage.h>
#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <linux/gfp.h>
#include <linux/irqreturn.h>
#include <linux/irqnr.h>
#include <linux/errno.h>
#include <linux/topology.h>
#include <linux/wait.h>

#include <asm/irq.h>
#include <asm/ptrace.h>
#include <asm/irq_regs.h>

struct seq_file;
struct module;
struct irq_desc;
struct irq_data;
typedef	void (*irq_flow_handler_t)(unsigned int irq,
					    struct irq_desc *desc);
typedef	void (*irq_preflow_handler_t)(struct irq_data *data);

/*
 * IRQ line status.
 *
 * Bits 0-7 are the same as the IRQF_* bits in linux/interrupt.h
 *
 * IRQ_TYPE_NONE		- default, unspecified type
 * IRQ_TYPE_EDGE_RISING		- rising edge triggered
 * IRQ_TYPE_EDGE_FALLING	- falling edge triggered
 * IRQ_TYPE_EDGE_BOTH		- rising and falling edge triggered
 * IRQ_TYPE_LEVEL_HIGH		- high level triggered
 * IRQ_TYPE_LEVEL_LOW		- low level triggered
 * IRQ_TYPE_LEVEL_MASK		- Mask to filter out the level bits
 * IRQ_TYPE_SENSE_MASK		- Mask for all the above bits
 * IRQ_TYPE_DEFAULT		- For use by some PICs to ask irq_set_type
 *				  to setup the HW to a sane default (used
 *                                by irqdomain map() callbacks to synchronize
 *                                the HW state and SW flags for a newly
 *                                allocated descriptor).
 *
 * IRQ_TYPE_PROBE		- Special flag for probing in progress
 *
 * Bits which can be modified via irq_set/clear/modify_status_flags()
 * IRQ_LEVEL			- Interrupt is level type. Will be also
 *				  updated in the code when the above trigger
 *				  bits are modified via irq_set_irq_type()
 * IRQ_PER_CPU			- Mark an interrupt PER_CPU. Will protect
 *				  it from affinity setting
 * IRQ_NOPROBE			- Interrupt cannot be probed by autoprobing
 * IRQ_NOREQUEST		- Interrupt cannot be requested via
 *				  request_irq()
 * IRQ_NOTHREAD			- Interrupt cannot be threaded
 * IRQ_NOAUTOEN			- Interrupt is not automatically enabled in
 *				  request/setup_irq()
 * IRQ_NO_BALANCING		- Interrupt cannot be balanced (affinity set)
 * IRQ_MOVE_PCNTXT		- Interrupt can be migrated from process context
 * IRQ_NESTED_TRHEAD		- Interrupt nests into another thread
 * IRQ_PER_CPU_DEVID		- Dev_id is a per-cpu variable
 */
enum {
	IRQ_TYPE_NONE		= 0x00000000,
	IRQ_TYPE_EDGE_RISING	= 0x00000001,
	IRQ_TYPE_EDGE_FALLING	= 0x00000002,
	IRQ_TYPE_EDGE_BOTH	= (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING),
	IRQ_TYPE_LEVEL_HIGH	= 0x00000004,
	IRQ_TYPE_LEVEL_LOW	= 0x00000008,
	IRQ_TYPE_LEVEL_MASK	= (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH),
	IRQ_TYPE_SENSE_MASK	= 0x0000000f,
	IRQ_TYPE_DEFAULT	= IRQ_TYPE_SENSE_MASK,

	IRQ_TYPE_PROBE		= 0x00000010,

	IRQ_LEVEL		= (1 <<  8),
	IRQ_PER_CPU		= (1 <<  9),
	IRQ_NOPROBE		= (1 << 10),
	IRQ_NOREQUEST		= (1 << 11),
	IRQ_NOAUTOEN		= (1 << 12),
	IRQ_NO_BALANCING	= (1 << 13),
	IRQ_MOVE_PCNTXT		= (1 << 14),
	IRQ_NESTED_THREAD	= (1 << 15),
	IRQ_NOTHREAD		= (1 << 16),
	IRQ_PER_CPU_DEVID	= (1 << 17),
};

#define IRQF_MODIFY_MASK	\
	(IRQ_TYPE_SENSE_MASK | IRQ_NOPROBE | IRQ_NOREQUEST | \
	 IRQ_NOAUTOEN | IRQ_MOVE_PCNTXT | IRQ_LEVEL | IRQ_NO_BALANCING | \
	 IRQ_PER_CPU | IRQ_NESTED_THREAD | IRQ_NOTHREAD | IRQ_PER_CPU_DEVID)

#define IRQ_NO_BALANCING_MASK	(IRQ_PER_CPU | IRQ_NO_BALANCING)

/*
 * Return value for chip->irq_set_affinity()
 *
 * IRQ_SET_MASK_OK	- OK, core updates irq_data.affinity
 * IRQ_SET_MASK_NOCPY	- OK, chip did update irq_data.affinity
 */
enum {
	IRQ_SET_MASK_OK = 0,
	IRQ_SET_MASK_OK_NOCOPY,
};

struct msi_desc;
struct irq_domain;

/**
 * struct irq_data - per irq and irq chip data passed down to chip functions
 * @irq:		interrupt number
 * @hwirq:		hardware interrupt number, local to the interrupt domain
 * @node:		node index useful for balancing
 * @state_use_accessors: status information for irq chip functions.
 *			Use accessor functions to deal with it
 * @chip:		low level interrupt hardware access
 * @domain:		Interrupt translation domain; responsible for mapping
 *			between hwirq number and linux irq number.
 * @handler_data:	per-IRQ data for the irq_chip methods
 * @chip_data:		platform-specific per-chip private data for the chip
 *			methods, to allow shared chip implementations
 * @msi_desc:		MSI descriptor
 * @affinity:		IRQ affinity on SMP
 *
 * The fields here need to overlay the ones in irq_desc until we
 * cleaned up the direct references and switched everything over to
 * irq_data.
 */
struct irq_data {
	unsigned int		irq;
	unsigned long		hwirq;
	unsigned int		node;
	unsigned int		state_use_accessors;
	struct irq_chip		*chip;
	struct irq_domain	*domain;
	void			*handler_data;
	void			*chip_data;
	struct msi_desc		*msi_desc;
#ifdef CONFIG_SMP
	cpumask_var_t		affinity;
#endif
};

/*
 * Bit masks for irq_data.state
 *
 * IRQD_TRIGGER_MASK		- Mask for the trigger type bits
 * IRQD_SETAFFINITY_PENDING	- Affinity setting is pending
 * IRQD_NO_BALANCING		- Balancing disabled for this IRQ
 * IRQD_PER_CPU			- Interrupt is per cpu
 * IRQD_AFFINITY_SET		- Interrupt affinity was set
 * IRQD_LEVEL			- Interrupt is level triggered
 * IRQD_WAKEUP_STATE		- Interrupt is configured for wakeup
 *				  from suspend
 * IRDQ_MOVE_PCNTXT		- Interrupt can be moved in process
 *				  context
 * IRQD_IRQ_DISABLED		- Disabled state of the interrupt
 * IRQD_IRQ_MASKED		- Masked state of the interrupt
 * IRQD_IRQ_INPROGRESS		- In progress state of the interrupt
 */
enum {
	IRQD_TRIGGER_MASK		= 0xf,
	IRQD_SETAFFINITY_PENDING	= (1 <<  8),
	IRQD_NO_BALANCING		= (1 << 10),
	IRQD_PER_CPU			= (1 << 11),
	IRQD_AFFINITY_SET		= (1 << 12),
	IRQD_LEVEL			= (1 << 13),
	IRQD_WAKEUP_STATE		= (1 << 14),
	IRQD_MOVE_PCNTXT		= (1 << 15),
	IRQD_IRQ_DISABLED		= (1 << 16),
	IRQD_IRQ_MASKED			= (1 << 17),
	IRQD_IRQ_INPROGRESS		= (1 << 18),
};

static inline bool irqd_is_setaffinity_pending(struct irq_data *d)
{
	return d->state_use_accessors & IRQD_SETAFFINITY_PENDING;
}

static inline bool irqd_is_per_cpu(struct irq_data *d)
{
	return d->state_use_accessors & IRQD_PER_CPU;
}

static inline bool irqd_can_balance(struct irq_data *d)
{
	return !(d->state_use_accessors & (IRQD_PER_CPU | IRQD_NO_BALANCING));
}

static inline bool irqd_affinity_was_set(struct irq_data *d)
{
	return d->state_use_accessors & IRQD_AFFINITY_SET;
}

static inline void irqd_mark_affinity_was_set(struct irq_data *d)
{
	d->state_use_accessors |= IRQD_AFFINITY_SET;
}

static inline u32 irqd_get_trigger_type(struct irq_data *d)
{
	return d->state_use_accessors & IRQD_TRIGGER_MASK;
}

/*
 * Must only be called inside irq_chip.irq_set_type() functions.
 */
static inline void irqd_set_trigger_type(struct irq_data *d, u32 type)
{
	d->state_use_accessors &= ~IRQD_TRIGGER_MASK;
	d->state_use_accessors |= type & IRQD_TRIGGER_MASK;
}

static inline bool irqd_is_level_type(struct irq_data *d)
{
	return d->state_use_accessors & IRQD_LEVEL;
}

static inline bool irqd_is_wakeup_set(struct irq_data *d)
{
	return d->state_use_accessors & IRQD_WAKEUP_STATE;
}

static inline bool irqd_can_move_in_process_context(struct irq_data *d)
{
	return d->state_use_accessors & IRQD_MOVE_PCNTXT;
}

static inline bool irqd_irq_disabled(struct irq_data *d)
{
	return d->state_use_accessors & IRQD_IRQ_DISABLED;
}

static inline bool irqd_irq_masked(struct irq_data *d)
{
	return d->state_use_accessors & IRQD_IRQ_MASKED;
}

static inline bool irqd_irq_inprogress(struct irq_data *d)
{
	return d->state_use_accessors & IRQD_IRQ_INPROGRESS;
}

/*
 * Functions for chained handlers which can be enabled/disabled by the
 * standard disable_irq/enable_irq calls. Must be called with
 * irq_desc->lock held.
 */
static inline void irqd_set_chained_irq_inprogress(struct irq_data *d)
{
	d->state_use_accessors |= IRQD_IRQ_INPROGRESS;
}

static inline void irqd_clr_chained_irq_inprogress(struct irq_data *d)
{
	d->state_use_accessors &= ~IRQD_IRQ_INPROGRESS;
}

static inline irq_hw_number_t irqd_to_hwirq(struct irq_data *d)
{
	return d->hwirq;
}

/**
 * struct irq_chip - hardware interrupt chip descriptor
 *
 * @name:		name for /proc/interrupts
 * @irq_startup:	start up the interrupt (defaults to ->enable if NULL)
 * @irq_shutdown:	shut down the interrupt (defaults to ->disable if NULL)
 * @irq_enable:		enable the interrupt (defaults to chip->unmask if NULL)
 * @irq_disable:	disable the interrupt
 * @irq_ack:		start of a new interrupt
 * @irq_mask:		mask an interrupt source
 * @irq_mask_ack:	ack and mask an interrupt source
 * @irq_unmask:		unmask an interrupt source
 * @irq_eoi:		end of interrupt
 * @irq_set_affinity:	set the CPU affinity on SMP machines
 * @irq_retrigger:	resend an IRQ to the CPU
 * @irq_set_type:	set the flow type (IRQ_TYPE_LEVEL/etc.) of an IRQ
 * @irq_set_wake:	enable/disable power-management wake-on of an IRQ
 * @irq_bus_lock:	function to lock access to slow bus (i2c) chips
 * @irq_bus_sync_unlock:function to sync and unlock slow bus (i2c) chips
 * @irq_cpu_online:	configure an interrupt source for a secondary CPU
 * @irq_cpu_offline:	un-configure an interrupt source for a secondary CPU
 * @irq_suspend:	function called from core code on suspend once per chip
 * @irq_resume:		function called from core code on resume once per chip
 * @irq_pm_shutdown:	function called from core code on shutdown once per chip
 * @irq_print_chip:	optional to print special chip info in show_interrupts
 * @flags:		chip specific flags
 */
struct irq_chip {
	const char	*name;
	unsigned int	(*irq_startup)(struct irq_data *data);
	void		(*irq_shutdown)(struct irq_data *data);
	void		(*irq_enable)(struct irq_data *data);
	void		(*irq_disable)(struct irq_data *data);

	void		(*irq_ack)(struct irq_data *data);
	void		(*irq_mask)(struct irq_data *data);
	void		(*irq_mask_ack)(struct irq_data *data);
	void		(*irq_unmask)(struct irq_data *data);
	void		(*irq_eoi)(struct irq_data *data);

	int		(*irq_set_affinity)(struct irq_data *data, const struct cpumask *dest, bool force);
	int		(*irq_retrigger)(struct irq_data *data);
	int		(*irq_set_type)(struct irq_data *data, unsigned int flow_type);
	int		(*irq_set_wake)(struct irq_data *data, unsigned int on);

	void		(*irq_bus_lock)(struct irq_data *data);
	void		(*irq_bus_sync_unlock)(struct irq_data *data);

	void		(*irq_cpu_online)(struct irq_data *data);
	void		(*irq_cpu_offline)(struct irq_data *data);

	void		(*irq_suspend)(struct irq_data *data);
	void		(*irq_resume)(struct irq_data *data);
	void		(*irq_pm_shutdown)(struct irq_data *data);

	void		(*irq_print_chip)(struct irq_data *data, struct seq_file *p);

	unsigned long	flags;
};

/*
 * irq_chip specific flags
 *
 * IRQCHIP_SET_TYPE_MASKED:	Mask before calling chip.irq_set_type()
 * IRQCHIP_EOI_IF_HANDLED:	Only issue irq_eoi() when irq was handled
 * IRQCHIP_MASK_ON_SUSPEND:	Mask non wake irqs in the suspend path
 * IRQCHIP_ONOFFLINE_ENABLED:	Only call irq_on/off_line callbacks
 *				when irq enabled
 * IRQCHIP_SKIP_SET_WAKE:	Skip chip.irq_set_wake(), for this irq chip
 */
enum {
	IRQCHIP_SET_TYPE_MASKED		= (1 <<  0),
	IRQCHIP_EOI_IF_HANDLED		= (1 <<  1),
	IRQCHIP_MASK_ON_SUSPEND		= (1 <<  2),
	IRQCHIP_ONOFFLINE_ENABLED	= (1 <<  3),
	IRQCHIP_SKIP_SET_WAKE		= (1 <<  4),
};

/* This include will go away once we isolated irq_desc usage to core code */
#include <linux/irqdesc.h>

/*
 * Pick up the arch-dependent methods:
 */
#include <asm/hw_irq.h>

#ifndef NR_IRQS_LEGACY
# define NR_IRQS_LEGACY 0
#endif

#ifndef ARCH_IRQ_INIT_FLAGS
# define ARCH_IRQ_INIT_FLAGS	0
#endif

#define IRQ_DEFAULT_INIT_FLAGS	ARCH_IRQ_INIT_FLAGS

struct irqaction;
extern int setup_irq(unsigned int irq, struct irqaction *new);
extern void remove_irq(unsigned int irq, struct irqaction *act);
extern int setup_percpu_irq(unsigned int irq, struct irqaction *new);
extern void remove_percpu_irq(unsigned int irq, struct irqaction *act);

extern void irq_cpu_online(void);
extern void irq_cpu_offline(void);
extern int __irq_set_affinity_locked(struct irq_data *data,  const struct cpumask *cpumask);

#ifdef CONFIG_GENERIC_HARDIRQS

#if defined(CONFIG_SMP) && defined(CONFIG_GENERIC_PENDING_IRQ)
void irq_move_irq(struct irq_data *data);
void irq_move_masked_irq(struct irq_data *data);
#else
static inline void irq_move_irq(struct irq_data *data) { }
static inline void irq_move_masked_irq(struct irq_data *data) { }
#endif

extern int no_irq_affinity;

/*
 * Built-in IRQ handlers for various IRQ types,
 * callable via desc->handle_irq()
 */
extern void handle_level_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_fasteoi_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_edge_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_edge_eoi_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_simple_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_percpu_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_percpu_devid_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_bad_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_nested_irq(unsigned int irq);

/* Handling of unhandled and spurious interrupts: */
extern void note_interrupt(unsigned int irq, struct irq_desc *desc,
			   irqreturn_t action_ret);


/* Enable/disable irq debugging output: */
extern int noirqdebug_setup(char *str);

/* Checks whether the interrupt can be requested by request_irq(): */
extern int can_request_irq(unsigned int irq, unsigned long irqflags);

/* Dummy irq-chip implementations: */
extern struct irq_chip no_irq_chip;
extern struct irq_chip dummy_irq_chip;

extern void
irq_set_chip_and_handler_name(unsigned int irq, struct irq_chip *chip,
			      irq_flow_handler_t handle, const char *name);

static inline void irq_set_chip_and_handler(unsigned int irq, struct irq_chip *chip,
					    irq_flow_handler_t handle)
{
	irq_set_chip_and_handler_name(irq, chip, handle, NULL);
}

extern int irq_set_percpu_devid(unsigned int irq);

extern void
__irq_set_handler(unsigned int irq, irq_flow_handler_t handle, int is_chained,
		  const char *name);

static inline void
irq_set_handler(unsigned int irq, irq_flow_handler_t handle)
{
	__irq_set_handler(irq, handle, 0, NULL);
}

/*
 * Set a highlevel chained flow handler for a given IRQ.
 * (a chained handler is automatically enabled and set to
 *  IRQ_NOREQUEST, IRQ_NOPROBE, and IRQ_NOTHREAD)
 */
static inline void
irq_set_chained_handler(unsigned int irq, irq_flow_handler_t handle)
{
	__irq_set_handler(irq, handle, 1, NULL);
}

void irq_modify_status(unsigned int irq, unsigned long clr, unsigned long set);

static inline void irq_set_status_flags(unsigned int irq, unsigned long set)
{
	irq_modify_status(irq, 0, set);
}

static inline void irq_clear_status_flags(unsigned int irq, unsigned long clr)
{
	irq_modify_status(irq, clr, 0);
}

static inline void irq_set_noprobe(unsigned int irq)
{
	irq_modify_status(irq, 0, IRQ_NOPROBE);
}

static inline void irq_set_probe(unsigned int irq)
{
	irq_modify_status(irq, IRQ_NOPROBE, 0);
}

static inline void irq_set_nothread(unsigned int irq)
{
	irq_modify_status(irq, 0, IRQ_NOTHREAD);
}

static inline void irq_set_thread(unsigned int irq)
{
	irq_modify_status(irq, IRQ_NOTHREAD, 0);
}

static inline void irq_set_nested_thread(unsigned int irq, bool nest)
{
	if (nest)
		irq_set_status_flags(irq, IRQ_NESTED_THREAD);
	else
		irq_clear_status_flags(irq, IRQ_NESTED_THREAD);
}

static inline void irq_set_percpu_devid_flags(unsigned int irq)
{
	irq_set_status_flags(irq,
			     IRQ_NOAUTOEN | IRQ_PER_CPU | IRQ_NOTHREAD |
			     IRQ_NOPROBE | IRQ_PER_CPU_DEVID);
}

/* Handle dynamic irq creation and destruction */
extern unsigned int create_irq_nr(unsigned int irq_want, int node);
extern int create_irq(void);
extern void destroy_irq(unsigned int irq);

/*
 * Dynamic irq helper functions. Obsolete. Use irq_alloc_desc* and
 * irq_free_desc instead.
 */
extern void dynamic_irq_cleanup(unsigned int irq);
static inline void dynamic_irq_init(unsigned int irq)
{
	dynamic_irq_cleanup(irq);
}

/* Set/get chip/data for an IRQ: */
extern int irq_set_chip(unsigned int irq, struct irq_chip *chip);
extern int irq_set_handler_data(unsigned int irq, void *data);
extern int irq_set_chip_data(unsigned int irq, void *data);
extern int irq_set_irq_type(unsigned int irq, unsigned int type);
extern int irq_set_msi_desc(unsigned int irq, struct msi_desc *entry);
extern struct irq_data *irq_get_irq_data(unsigned int irq);

static inline struct irq_chip *irq_get_chip(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);
	return d ? d->chip : NULL;
}

static inline struct irq_chip *irq_data_get_irq_chip(struct irq_data *d)
{
	return d->chip;
}

static inline void *irq_get_chip_data(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);
	return d ? d->chip_data : NULL;
}

static inline void *irq_data_get_irq_chip_data(struct irq_data *d)
{
	return d->chip_data;
}

static inline void *irq_get_handler_data(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);
	return d ? d->handler_data : NULL;
}

static inline void *irq_data_get_irq_handler_data(struct irq_data *d)
{
	return d->handler_data;
}

static inline struct msi_desc *irq_get_msi_desc(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);
	return d ? d->msi_desc : NULL;
}

static inline struct msi_desc *irq_data_get_msi(struct irq_data *d)
{
	return d->msi_desc;
}

int __irq_alloc_descs(int irq, unsigned int from, unsigned int cnt, int node,
		struct module *owner);

/* use macros to avoid needing export.h for THIS_MODULE */
#define irq_alloc_descs(irq, from, cnt, node)	\
	__irq_alloc_descs(irq, from, cnt, node, THIS_MODULE)

#define irq_alloc_desc(node)			\
	irq_alloc_descs(-1, 0, 1, node)

#define irq_alloc_desc_at(at, node)		\
	irq_alloc_descs(at, at, 1, node)

#define irq_alloc_desc_from(from, node)		\
	irq_alloc_descs(-1, from, 1, node)

void irq_free_descs(unsigned int irq, unsigned int cnt);
int irq_reserve_irqs(unsigned int from, unsigned int cnt);

static inline void irq_free_desc(unsigned int irq)
{
	irq_free_descs(irq, 1);
}

static inline int irq_reserve_irq(unsigned int irq)
{
	return irq_reserve_irqs(irq, 1);
}

#ifndef irq_reg_writel
# define irq_reg_writel(val, addr)	writel(val, addr)
#endif
#ifndef irq_reg_readl
# define irq_reg_readl(addr)		readl(addr)
#endif

/**
 * struct irq_chip_regs - register offsets for struct irq_gci
 * @enable:	Enable register offset to reg_base
 * @disable:	Disable register offset to reg_base
 * @mask:	Mask register offset to reg_base
 * @ack:	Ack register offset to reg_base
 * @eoi:	Eoi register offset to reg_base
 * @type:	Type configuration register offset to reg_base
 * @polarity:	Polarity configuration register offset to reg_base
 */
struct irq_chip_regs {
	unsigned long		enable;
	unsigned long		disable;
	unsigned long		mask;
	unsigned long		ack;
	unsigned long		eoi;
	unsigned long		type;
	unsigned long		polarity;
};

/**
 * struct irq_chip_type - Generic interrupt chip instance for a flow type
 * @chip:		The real interrupt chip which provides the callbacks
 * @regs:		Register offsets for this chip
 * @handler:		Flow handler associated with this chip
 * @type:		Chip can handle these flow types
 *
 * A irq_generic_chip can have several instances of irq_chip_type when
 * it requires different functions and register offsets for different
 * flow types.
 */
struct irq_chip_type {
	struct irq_chip		chip;
	struct irq_chip_regs	regs;
	irq_flow_handler_t	handler;
	u32			type;
};

/**
 * struct irq_chip_generic - Generic irq chip data structure
 * @lock:		Lock to protect register and cache data access
 * @reg_base:		Register base address (virtual)
 * @irq_base:		Interrupt base nr for this chip
 * @irq_cnt:		Number of interrupts handled by this chip
 * @mask_cache:		Cached mask register
 * @type_cache:		Cached type register
 * @polarity_cache:	Cached polarity register
 * @wake_enabled:	Interrupt can wakeup from suspend
 * @wake_active:	Interrupt is marked as an wakeup from suspend source
 * @num_ct:		Number of available irq_chip_type instances (usually 1)
 * @private:		Private data for non generic chip callbacks
 * @list:		List head for keeping track of instances
 * @chip_types:		Array of interrupt irq_chip_types
 *
 * Note, that irq_chip_generic can have multiple irq_chip_type
 * implementations which can be associated to a particular irq line of
 * an irq_chip_generic instance. That allows to share and protect
 * state in an irq_chip_generic instance when we need to implement
 * different flow mechanisms (level/edge) for it.
 */
struct irq_chip_generic {
	raw_spinlock_t		lock;
	void __iomem		*reg_base;
	unsigned int		irq_base;
	unsigned int		irq_cnt;
	u32			mask_cache;
	u32			type_cache;
	u32			polarity_cache;
	u32			wake_enabled;
	u32			wake_active;
	unsigned int		num_ct;
	void			*private;
	struct list_head	list;
	struct irq_chip_type	chip_types[0];
};

/**
 * enum irq_gc_flags - Initialization flags for generic irq chips
 * @IRQ_GC_INIT_MASK_CACHE:	Initialize the mask_cache by reading mask reg
 * @IRQ_GC_INIT_NESTED_LOCK:	Set the lock class of the irqs to nested for
 *				irq chips which need to call irq_set_wake() on
 *				the parent irq. Usually GPIO implementations
 */
enum irq_gc_flags {
	IRQ_GC_INIT_MASK_CACHE		= 1 << 0,
	IRQ_GC_INIT_NESTED_LOCK		= 1 << 1,
};

/* Generic chip callback functions */
void irq_gc_noop(struct irq_data *d);
void irq_gc_mask_disable_reg(struct irq_data *d);
void irq_gc_mask_set_bit(struct irq_data *d);
void irq_gc_mask_clr_bit(struct irq_data *d);
void irq_gc_unmask_enable_reg(struct irq_data *d);
void irq_gc_ack_set_bit(struct irq_data *d);
void irq_gc_ack_clr_bit(struct irq_data *d);
void irq_gc_mask_disable_reg_and_ack(struct irq_data *d);
void irq_gc_eoi(struct irq_data *d);
int irq_gc_set_wake(struct irq_data *d, unsigned int on);

/* Setup functions for irq_chip_generic */
struct irq_chip_generic *
irq_alloc_generic_chip(const char *name, int nr_ct, unsigned int irq_base,
		       void __iomem *reg_base, irq_flow_handler_t handler);
void irq_setup_generic_chip(struct irq_chip_generic *gc, u32 msk,
			    enum irq_gc_flags flags, unsigned int clr,
			    unsigned int set);
int irq_setup_alt_chip(struct irq_data *d, unsigned int type);
void irq_remove_generic_chip(struct irq_chip_generic *gc, u32 msk,
			     unsigned int clr, unsigned int set);

static inline struct irq_chip_type *irq_data_get_chip_type(struct irq_data *d)
{
	return container_of(d->chip, struct irq_chip_type, chip);
}

#define IRQ_MSK(n) (u32)((n) < 32 ? ((1 << (n)) - 1) : UINT_MAX)

#ifdef CONFIG_SMP
static inline void irq_gc_lock(struct irq_chip_generic *gc)
{
	raw_spin_lock(&gc->lock);
}

static inline void irq_gc_unlock(struct irq_chip_generic *gc)
{
	raw_spin_unlock(&gc->lock);
}
#else
static inline void irq_gc_lock(struct irq_chip_generic *gc) { }
static inline void irq_gc_unlock(struct irq_chip_generic *gc) { }
#endif

#endif /* CONFIG_GENERIC_HARDIRQS */

#endif /* !CONFIG_S390 */

#endif /* _LINUX_IRQ_H */

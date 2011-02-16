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

struct irq_desc;
typedef	void (*irq_flow_handler_t)(unsigned int irq,
					    struct irq_desc *desc);


/*
 * IRQ line status.
 *
 * Bits 0-7 are reserved for the IRQF_* bits in linux/interrupt.h
 *
 * IRQ types
 */
#define IRQ_TYPE_NONE		0x00000000	/* Default, unspecified type */
#define IRQ_TYPE_EDGE_RISING	0x00000001	/* Edge rising type */
#define IRQ_TYPE_EDGE_FALLING	0x00000002	/* Edge falling type */
#define IRQ_TYPE_EDGE_BOTH (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING)
#define IRQ_TYPE_LEVEL_HIGH	0x00000004	/* Level high type */
#define IRQ_TYPE_LEVEL_LOW	0x00000008	/* Level low type */
#define IRQ_TYPE_SENSE_MASK	0x0000000f	/* Mask of the above */
#define IRQ_TYPE_PROBE		0x00000010	/* Probing in progress */

/* Internal flags */
#define IRQ_INPROGRESS		0x00000100	/* IRQ handler active - do not enter! */
#define IRQ_DISABLED		0x00000200	/* IRQ disabled - do not enter! */
#define IRQ_PENDING		0x00000400	/* IRQ pending - replay on enable */
#define IRQ_REPLAY		0x00000800	/* IRQ has been replayed but not acked yet */
#define IRQ_AUTODETECT		0x00001000	/* IRQ is being autodetected */
#define IRQ_WAITING		0x00002000	/* IRQ not yet seen - for autodetection */
#define IRQ_LEVEL		0x00004000	/* IRQ level triggered */
#define IRQ_MASKED		0x00008000	/* IRQ masked - shouldn't be seen again */
#define IRQ_PER_CPU		0x00010000	/* IRQ is per CPU */
#define IRQ_NOPROBE		0x00020000	/* IRQ is not valid for probing */
#define IRQ_NOREQUEST		0x00040000	/* IRQ cannot be requested */
#define IRQ_NOAUTOEN		0x00080000	/* IRQ will not be enabled on request irq */
#define IRQ_WAKEUP		0x00100000	/* IRQ triggers system wakeup */
#define IRQ_MOVE_PENDING	0x00200000	/* need to re-target IRQ destination */
#define IRQ_NO_BALANCING	0x00400000	/* IRQ is excluded from balancing */
#define IRQ_SPURIOUS_DISABLED	0x00800000	/* IRQ was disabled by the spurious trap */
#define IRQ_MOVE_PCNTXT		0x01000000	/* IRQ migration from process context */
#define IRQ_AFFINITY_SET	0x02000000	/* IRQ affinity was set from userspace*/
#define IRQ_SUSPENDED		0x04000000	/* IRQ has gone through suspend sequence */
#define IRQ_ONESHOT		0x08000000	/* IRQ is not unmasked after hardirq */
#define IRQ_NESTED_THREAD	0x10000000	/* IRQ is nested into another, no own handler thread */

#define IRQF_MODIFY_MASK	\
	(IRQ_TYPE_SENSE_MASK | IRQ_NOPROBE | IRQ_NOREQUEST | \
	 IRQ_NOAUTOEN | IRQ_MOVE_PCNTXT | IRQ_LEVEL | IRQ_NO_BALANCING | \
	 IRQ_PER_CPU)

#ifdef CONFIG_IRQ_PER_CPU
# define CHECK_IRQ_PER_CPU(var) ((var) & IRQ_PER_CPU)
# define IRQ_NO_BALANCING_MASK	(IRQ_PER_CPU | IRQ_NO_BALANCING)
#else
# define CHECK_IRQ_PER_CPU(var) 0
# define IRQ_NO_BALANCING_MASK	IRQ_NO_BALANCING
#endif

struct msi_desc;

/**
 * struct irq_data - per irq and irq chip data passed down to chip functions
 * @irq:		interrupt number
 * @node:		node index useful for balancing
 * @chip:		low level interrupt hardware access
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
	unsigned int		node;
	struct irq_chip		*chip;
	void			*handler_data;
	void			*chip_data;
	struct msi_desc		*msi_desc;
#ifdef CONFIG_SMP
	cpumask_var_t		affinity;
#endif
};

/**
 * struct irq_chip - hardware interrupt chip descriptor
 *
 * @name:		name for /proc/interrupts
 * @startup:		deprecated, replaced by irq_startup
 * @shutdown:		deprecated, replaced by irq_shutdown
 * @enable:		deprecated, replaced by irq_enable
 * @disable:		deprecated, replaced by irq_disable
 * @ack:		deprecated, replaced by irq_ack
 * @mask:		deprecated, replaced by irq_mask
 * @mask_ack:		deprecated, replaced by irq_mask_ack
 * @unmask:		deprecated, replaced by irq_unmask
 * @eoi:		deprecated, replaced by irq_eoi
 * @end:		deprecated, will go away with __do_IRQ()
 * @set_affinity:	deprecated, replaced by irq_set_affinity
 * @retrigger:		deprecated, replaced by irq_retrigger
 * @set_type:		deprecated, replaced by irq_set_type
 * @set_wake:		deprecated, replaced by irq_wake
 * @bus_lock:		deprecated, replaced by irq_bus_lock
 * @bus_sync_unlock:	deprecated, replaced by irq_bus_sync_unlock
 *
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
 *
 * @release:		release function solely used by UML
 */
struct irq_chip {
	const char	*name;
#ifndef CONFIG_GENERIC_HARDIRQS_NO_DEPRECATED
	unsigned int	(*startup)(unsigned int irq);
	void		(*shutdown)(unsigned int irq);
	void		(*enable)(unsigned int irq);
	void		(*disable)(unsigned int irq);

	void		(*ack)(unsigned int irq);
	void		(*mask)(unsigned int irq);
	void		(*mask_ack)(unsigned int irq);
	void		(*unmask)(unsigned int irq);
	void		(*eoi)(unsigned int irq);

	void		(*end)(unsigned int irq);
	int		(*set_affinity)(unsigned int irq,
					const struct cpumask *dest);
	int		(*retrigger)(unsigned int irq);
	int		(*set_type)(unsigned int irq, unsigned int flow_type);
	int		(*set_wake)(unsigned int irq, unsigned int on);

	void		(*bus_lock)(unsigned int irq);
	void		(*bus_sync_unlock)(unsigned int irq);
#endif
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

	/* Currently used only by UML, might disappear one day.*/
#ifdef CONFIG_IRQ_RELEASE_METHOD
	void		(*release)(unsigned int irq, void *dev_id);
#endif
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

#define IRQ_DEFAULT_INIT_FLAGS	(IRQ_DISABLED | ARCH_IRQ_INIT_FLAGS)

struct irqaction;
extern int setup_irq(unsigned int irq, struct irqaction *new);
extern void remove_irq(unsigned int irq, struct irqaction *act);

#ifdef CONFIG_GENERIC_HARDIRQS

#if defined(CONFIG_SMP) && defined(CONFIG_GENERIC_PENDING_IRQ)
void move_native_irq(int irq);
void move_masked_irq(int irq);
#else
static inline void move_native_irq(int irq) { }
static inline void move_masked_irq(int irq) { }
#endif

extern int no_irq_affinity;

/* Handle irq action chains: */
extern irqreturn_t handle_IRQ_event(unsigned int irq, struct irqaction *action);

/*
 * Built-in IRQ handlers for various IRQ types,
 * callable via desc->handle_irq()
 */
extern void handle_level_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_fasteoi_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_edge_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_simple_irq(unsigned int irq, struct irq_desc *desc);
extern void handle_percpu_irq(unsigned int irq, struct irq_desc *desc);
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
set_irq_chip_and_handler(unsigned int irq, struct irq_chip *chip,
			 irq_flow_handler_t handle);
extern void
set_irq_chip_and_handler_name(unsigned int irq, struct irq_chip *chip,
			      irq_flow_handler_t handle, const char *name);

extern void
__set_irq_handler(unsigned int irq, irq_flow_handler_t handle, int is_chained,
		  const char *name);

/*
 * Set a highlevel flow handler for a given IRQ:
 */
static inline void
set_irq_handler(unsigned int irq, irq_flow_handler_t handle)
{
	__set_irq_handler(irq, handle, 0, NULL);
}

/*
 * Set a highlevel chained flow handler for a given IRQ.
 * (a chained handler is automatically enabled and set to
 *  IRQ_NOREQUEST and IRQ_NOPROBE)
 */
static inline void
set_irq_chained_handler(unsigned int irq,
			irq_flow_handler_t handle)
{
	__set_irq_handler(irq, handle, 1, NULL);
}

extern void set_irq_nested_thread(unsigned int irq, int nest);

void irq_modify_status(unsigned int irq, unsigned long clr, unsigned long set);

static inline void irq_set_status_flags(unsigned int irq, unsigned long set)
{
	irq_modify_status(irq, 0, set);
}

static inline void irq_clear_status_flags(unsigned int irq, unsigned long clr)
{
	irq_modify_status(irq, clr, 0);
}

static inline void set_irq_noprobe(unsigned int irq)
{
	irq_modify_status(irq, 0, IRQ_NOPROBE);
}

static inline void set_irq_probe(unsigned int irq)
{
	irq_modify_status(irq, IRQ_NOPROBE, 0);
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
extern int set_irq_chip(unsigned int irq, struct irq_chip *chip);
extern int set_irq_data(unsigned int irq, void *data);
extern int set_irq_chip_data(unsigned int irq, void *data);
extern int set_irq_type(unsigned int irq, unsigned int type);
extern int set_irq_msi(unsigned int irq, struct msi_desc *entry);
extern struct irq_data *irq_get_irq_data(unsigned int irq);

static inline struct irq_chip *get_irq_chip(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);
	return d ? d->chip : NULL;
}

static inline struct irq_chip *irq_data_get_irq_chip(struct irq_data *d)
{
	return d->chip;
}

static inline void *get_irq_chip_data(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);
	return d ? d->chip_data : NULL;
}

static inline void *irq_data_get_irq_chip_data(struct irq_data *d)
{
	return d->chip_data;
}

static inline void *get_irq_data(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);
	return d ? d->handler_data : NULL;
}

static inline void *irq_data_get_irq_data(struct irq_data *d)
{
	return d->handler_data;
}

static inline struct msi_desc *get_irq_msi(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);
	return d ? d->msi_desc : NULL;
}

static inline struct msi_desc *irq_data_get_msi(struct irq_data *d)
{
	return d->msi_desc;
}

int irq_alloc_descs(int irq, unsigned int from, unsigned int cnt, int node);
void irq_free_descs(unsigned int irq, unsigned int cnt);
int irq_reserve_irqs(unsigned int from, unsigned int cnt);

static inline int irq_alloc_desc(int node)
{
	return irq_alloc_descs(-1, 0, 1, node);
}

static inline int irq_alloc_desc_at(unsigned int at, int node)
{
	return irq_alloc_descs(at, at, 1, node);
}

static inline int irq_alloc_desc_from(unsigned int from, int node)
{
	return irq_alloc_descs(-1, from, 1, node);
}

static inline void irq_free_desc(unsigned int irq)
{
	irq_free_descs(irq, 1);
}

static inline int irq_reserve_irq(unsigned int irq)
{
	return irq_reserve_irqs(irq, 1);
}

#endif /* CONFIG_GENERIC_HARDIRQS */

#endif /* !CONFIG_S390 */

#endif /* _LINUX_IRQ_H */

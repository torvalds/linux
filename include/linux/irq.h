/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IRQ_H
#define _LINUX_IRQ_H

/*
 * Please do not include this file in generic code.  There is currently
 * no requirement for any architecture to implement anything held
 * within this file.
 *
 * Thanks. --rmk
 */

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <linux/irqhandler.h>
#include <linux/irqreturn.h>
#include <linux/irqnr.h>
#include <linux/topology.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <asm/irq.h>
#include <asm/ptrace.h>
#include <asm/irq_regs.h>

struct seq_file;
struct module;
struct msi_msg;
struct irq_affinity_desc;
enum irqchip_irq_state;

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
 * IRQ_NESTED_THREAD		- Interrupt nests into another thread
 * IRQ_PER_CPU_DEVID		- Dev_id is a per-cpu variable
 * IRQ_IS_POLLED		- Always polled by another interrupt. Exclude
 *				  it from the spurious interrupt detection
 *				  mechanism and from core side polling.
 * IRQ_DISABLE_UNLAZY		- Disable lazy irq disable
 * IRQ_HIDDEN			- Don't show up in /proc/interrupts
 * IRQ_NO_DEBUG			- Exclude from note_interrupt() debugging
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
	IRQ_IS_POLLED		= (1 << 18),
	IRQ_DISABLE_UNLAZY	= (1 << 19),
	IRQ_HIDDEN		= (1 << 20),
	IRQ_NO_DEBUG		= (1 << 21),
};

#define IRQF_MODIFY_MASK	\
	(IRQ_TYPE_SENSE_MASK | IRQ_NOPROBE | IRQ_NOREQUEST | \
	 IRQ_NOAUTOEN | IRQ_MOVE_PCNTXT | IRQ_LEVEL | IRQ_NO_BALANCING | \
	 IRQ_PER_CPU | IRQ_NESTED_THREAD | IRQ_NOTHREAD | IRQ_PER_CPU_DEVID | \
	 IRQ_IS_POLLED | IRQ_DISABLE_UNLAZY | IRQ_HIDDEN)

#define IRQ_NO_BALANCING_MASK	(IRQ_PER_CPU | IRQ_NO_BALANCING)

/*
 * Return value for chip->irq_set_affinity()
 *
 * IRQ_SET_MASK_OK	- OK, core updates irq_common_data.affinity
 * IRQ_SET_MASK_NOCPY	- OK, chip did update irq_common_data.affinity
 * IRQ_SET_MASK_OK_DONE	- Same as IRQ_SET_MASK_OK for core. Special code to
 *			  support stacked irqchips, which indicates skipping
 *			  all descendant irqchips.
 */
enum {
	IRQ_SET_MASK_OK = 0,
	IRQ_SET_MASK_OK_NOCOPY,
	IRQ_SET_MASK_OK_DONE,
};

struct msi_desc;
struct irq_domain;

/**
 * struct irq_common_data - per irq data shared by all irqchips
 * @state_use_accessors: status information for irq chip functions.
 *			Use accessor functions to deal with it
 * @node:		node index useful for balancing
 * @handler_data:	per-IRQ data for the irq_chip methods
 * @affinity:		IRQ affinity on SMP. If this is an IPI
 *			related irq, then this is the mask of the
 *			CPUs to which an IPI can be sent.
 * @effective_affinity:	The effective IRQ affinity on SMP as some irq
 *			chips do not allow multi CPU destinations.
 *			A subset of @affinity.
 * @msi_desc:		MSI descriptor
 * @ipi_offset:		Offset of first IPI target cpu in @affinity. Optional.
 */
struct irq_common_data {
	unsigned int		__private state_use_accessors;
#ifdef CONFIG_NUMA
	unsigned int		node;
#endif
	void			*handler_data;
	struct msi_desc		*msi_desc;
	cpumask_var_t		affinity;
#ifdef CONFIG_GENERIC_IRQ_EFFECTIVE_AFF_MASK
	cpumask_var_t		effective_affinity;
#endif
#ifdef CONFIG_GENERIC_IRQ_IPI
	unsigned int		ipi_offset;
#endif
};

/**
 * struct irq_data - per irq chip data passed down to chip functions
 * @mask:		precomputed bitmask for accessing the chip registers
 * @irq:		interrupt number
 * @hwirq:		hardware interrupt number, local to the interrupt domain
 * @common:		point to data shared by all irqchips
 * @chip:		low level interrupt hardware access
 * @domain:		Interrupt translation domain; responsible for mapping
 *			between hwirq number and linux irq number.
 * @parent_data:	pointer to parent struct irq_data to support hierarchy
 *			irq_domain
 * @chip_data:		platform-specific per-chip private data for the chip
 *			methods, to allow shared chip implementations
 */
struct irq_data {
	u32			mask;
	unsigned int		irq;
	unsigned long		hwirq;
	struct irq_common_data	*common;
	struct irq_chip		*chip;
	struct irq_domain	*domain;
#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
	struct irq_data		*parent_data;
#endif
	void			*chip_data;
};

/*
 * Bit masks for irq_common_data.state_use_accessors
 *
 * IRQD_TRIGGER_MASK		- Mask for the trigger type bits
 * IRQD_SETAFFINITY_PENDING	- Affinity setting is pending
 * IRQD_ACTIVATED		- Interrupt has already been activated
 * IRQD_NO_BALANCING		- Balancing disabled for this IRQ
 * IRQD_PER_CPU			- Interrupt is per cpu
 * IRQD_AFFINITY_SET		- Interrupt affinity was set
 * IRQD_LEVEL			- Interrupt is level triggered
 * IRQD_WAKEUP_STATE		- Interrupt is configured for wakeup
 *				  from suspend
 * IRQD_MOVE_PCNTXT		- Interrupt can be moved in process
 *				  context
 * IRQD_IRQ_DISABLED		- Disabled state of the interrupt
 * IRQD_IRQ_MASKED		- Masked state of the interrupt
 * IRQD_IRQ_INPROGRESS		- In progress state of the interrupt
 * IRQD_WAKEUP_ARMED		- Wakeup mode armed
 * IRQD_FORWARDED_TO_VCPU	- The interrupt is forwarded to a VCPU
 * IRQD_AFFINITY_MANAGED	- Affinity is auto-managed by the kernel
 * IRQD_IRQ_STARTED		- Startup state of the interrupt
 * IRQD_MANAGED_SHUTDOWN	- Interrupt was shutdown due to empty affinity
 *				  mask. Applies only to affinity managed irqs.
 * IRQD_SINGLE_TARGET		- IRQ allows only a single affinity target
 * IRQD_DEFAULT_TRIGGER_SET	- Expected trigger already been set
 * IRQD_CAN_RESERVE		- Can use reservation mode
 * IRQD_MSI_NOMASK_QUIRK	- Non-maskable MSI quirk for affinity change
 *				  required
 * IRQD_HANDLE_ENFORCE_IRQCTX	- Enforce that handle_irq_*() is only invoked
 *				  from actual interrupt context.
 * IRQD_AFFINITY_ON_ACTIVATE	- Affinity is set on activation. Don't call
 *				  irq_chip::irq_set_affinity() when deactivated.
 * IRQD_IRQ_ENABLED_ON_SUSPEND	- Interrupt is enabled on suspend by irq pm if
 *				  irqchip have flag IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND set.
 */
enum {
	IRQD_TRIGGER_MASK		= 0xf,
	IRQD_SETAFFINITY_PENDING	= (1 <<  8),
	IRQD_ACTIVATED			= (1 <<  9),
	IRQD_NO_BALANCING		= (1 << 10),
	IRQD_PER_CPU			= (1 << 11),
	IRQD_AFFINITY_SET		= (1 << 12),
	IRQD_LEVEL			= (1 << 13),
	IRQD_WAKEUP_STATE		= (1 << 14),
	IRQD_MOVE_PCNTXT		= (1 << 15),
	IRQD_IRQ_DISABLED		= (1 << 16),
	IRQD_IRQ_MASKED			= (1 << 17),
	IRQD_IRQ_INPROGRESS		= (1 << 18),
	IRQD_WAKEUP_ARMED		= (1 << 19),
	IRQD_FORWARDED_TO_VCPU		= (1 << 20),
	IRQD_AFFINITY_MANAGED		= (1 << 21),
	IRQD_IRQ_STARTED		= (1 << 22),
	IRQD_MANAGED_SHUTDOWN		= (1 << 23),
	IRQD_SINGLE_TARGET		= (1 << 24),
	IRQD_DEFAULT_TRIGGER_SET	= (1 << 25),
	IRQD_CAN_RESERVE		= (1 << 26),
	IRQD_MSI_NOMASK_QUIRK		= (1 << 27),
	IRQD_HANDLE_ENFORCE_IRQCTX	= (1 << 28),
	IRQD_AFFINITY_ON_ACTIVATE	= (1 << 29),
	IRQD_IRQ_ENABLED_ON_SUSPEND	= (1 << 30),
};

#define __irqd_to_state(d) ACCESS_PRIVATE((d)->common, state_use_accessors)

static inline bool irqd_is_setaffinity_pending(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_SETAFFINITY_PENDING;
}

static inline bool irqd_is_per_cpu(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_PER_CPU;
}

static inline bool irqd_can_balance(struct irq_data *d)
{
	return !(__irqd_to_state(d) & (IRQD_PER_CPU | IRQD_NO_BALANCING));
}

static inline bool irqd_affinity_was_set(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_AFFINITY_SET;
}

static inline void irqd_mark_affinity_was_set(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_AFFINITY_SET;
}

static inline bool irqd_trigger_type_was_set(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_DEFAULT_TRIGGER_SET;
}

static inline u32 irqd_get_trigger_type(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_TRIGGER_MASK;
}

/*
 * Must only be called inside irq_chip.irq_set_type() functions or
 * from the DT/ACPI setup code.
 */
static inline void irqd_set_trigger_type(struct irq_data *d, u32 type)
{
	__irqd_to_state(d) &= ~IRQD_TRIGGER_MASK;
	__irqd_to_state(d) |= type & IRQD_TRIGGER_MASK;
	__irqd_to_state(d) |= IRQD_DEFAULT_TRIGGER_SET;
}

static inline bool irqd_is_level_type(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_LEVEL;
}

/*
 * Must only be called of irqchip.irq_set_affinity() or low level
 * hierarchy domain allocation functions.
 */
static inline void irqd_set_single_target(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_SINGLE_TARGET;
}

static inline bool irqd_is_single_target(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_SINGLE_TARGET;
}

static inline void irqd_set_handle_enforce_irqctx(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_HANDLE_ENFORCE_IRQCTX;
}

static inline bool irqd_is_handle_enforce_irqctx(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_HANDLE_ENFORCE_IRQCTX;
}

static inline bool irqd_is_enabled_on_suspend(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_ENABLED_ON_SUSPEND;
}

static inline bool irqd_is_wakeup_set(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_WAKEUP_STATE;
}

static inline bool irqd_can_move_in_process_context(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_MOVE_PCNTXT;
}

static inline bool irqd_irq_disabled(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_DISABLED;
}

static inline bool irqd_irq_masked(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_MASKED;
}

static inline bool irqd_irq_inprogress(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_INPROGRESS;
}

static inline bool irqd_is_wakeup_armed(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_WAKEUP_ARMED;
}

static inline bool irqd_is_forwarded_to_vcpu(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_FORWARDED_TO_VCPU;
}

static inline void irqd_set_forwarded_to_vcpu(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_FORWARDED_TO_VCPU;
}

static inline void irqd_clr_forwarded_to_vcpu(struct irq_data *d)
{
	__irqd_to_state(d) &= ~IRQD_FORWARDED_TO_VCPU;
}

static inline bool irqd_affinity_is_managed(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_AFFINITY_MANAGED;
}

static inline bool irqd_is_activated(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_ACTIVATED;
}

static inline void irqd_set_activated(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_ACTIVATED;
}

static inline void irqd_clr_activated(struct irq_data *d)
{
	__irqd_to_state(d) &= ~IRQD_ACTIVATED;
}

static inline bool irqd_is_started(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_IRQ_STARTED;
}

static inline bool irqd_is_managed_and_shutdown(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_MANAGED_SHUTDOWN;
}

static inline void irqd_set_can_reserve(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_CAN_RESERVE;
}

static inline void irqd_clr_can_reserve(struct irq_data *d)
{
	__irqd_to_state(d) &= ~IRQD_CAN_RESERVE;
}

static inline bool irqd_can_reserve(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_CAN_RESERVE;
}

static inline void irqd_set_msi_nomask_quirk(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_MSI_NOMASK_QUIRK;
}

static inline void irqd_clr_msi_nomask_quirk(struct irq_data *d)
{
	__irqd_to_state(d) &= ~IRQD_MSI_NOMASK_QUIRK;
}

static inline bool irqd_msi_nomask_quirk(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_MSI_NOMASK_QUIRK;
}

static inline void irqd_set_affinity_on_activate(struct irq_data *d)
{
	__irqd_to_state(d) |= IRQD_AFFINITY_ON_ACTIVATE;
}

static inline bool irqd_affinity_on_activate(struct irq_data *d)
{
	return __irqd_to_state(d) & IRQD_AFFINITY_ON_ACTIVATE;
}

#undef __irqd_to_state

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
 * @irq_set_affinity:	Set the CPU affinity on SMP machines. If the force
 *			argument is true, it tells the driver to
 *			unconditionally apply the affinity setting. Sanity
 *			checks against the supplied affinity mask are not
 *			required. This is used for CPU hotplug where the
 *			target CPU is not yet set in the cpu_online_mask.
 * @irq_retrigger:	resend an IRQ to the CPU
 * @irq_set_type:	set the flow type (IRQ_TYPE_LEVEL/etc.) of an IRQ
 * @irq_set_wake:	enable/disable power-management wake-on of an IRQ
 * @irq_bus_lock:	function to lock access to slow bus (i2c) chips
 * @irq_bus_sync_unlock:function to sync and unlock slow bus (i2c) chips
 * @irq_cpu_online:	configure an interrupt source for a secondary CPU
 * @irq_cpu_offline:	un-configure an interrupt source for a secondary CPU
 * @irq_suspend:	function called from core code on suspend once per
 *			chip, when one or more interrupts are installed
 * @irq_resume:		function called from core code on resume once per chip,
 *			when one ore more interrupts are installed
 * @irq_pm_shutdown:	function called from core code on shutdown once per chip
 * @irq_calc_mask:	Optional function to set irq_data.mask for special cases
 * @irq_print_chip:	optional to print special chip info in show_interrupts
 * @irq_request_resources:	optional to request resources before calling
 *				any other callback related to this irq
 * @irq_release_resources:	optional to release resources acquired with
 *				irq_request_resources
 * @irq_compose_msi_msg:	optional to compose message content for MSI
 * @irq_write_msi_msg:	optional to write message content for MSI
 * @irq_get_irqchip_state:	return the internal state of an interrupt
 * @irq_set_irqchip_state:	set the internal state of a interrupt
 * @irq_set_vcpu_affinity:	optional to target a vCPU in a virtual machine
 * @ipi_send_single:	send a single IPI to destination cpus
 * @ipi_send_mask:	send an IPI to destination cpus in cpumask
 * @irq_nmi_setup:	function called from core code before enabling an NMI
 * @irq_nmi_teardown:	function called from core code after disabling an NMI
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

#ifdef CONFIG_DEPRECATED_IRQ_CPU_ONOFFLINE
	void		(*irq_cpu_online)(struct irq_data *data);
	void		(*irq_cpu_offline)(struct irq_data *data);
#endif
	void		(*irq_suspend)(struct irq_data *data);
	void		(*irq_resume)(struct irq_data *data);
	void		(*irq_pm_shutdown)(struct irq_data *data);

	void		(*irq_calc_mask)(struct irq_data *data);

	void		(*irq_print_chip)(struct irq_data *data, struct seq_file *p);
	int		(*irq_request_resources)(struct irq_data *data);
	void		(*irq_release_resources)(struct irq_data *data);

	void		(*irq_compose_msi_msg)(struct irq_data *data, struct msi_msg *msg);
	void		(*irq_write_msi_msg)(struct irq_data *data, struct msi_msg *msg);

	int		(*irq_get_irqchip_state)(struct irq_data *data, enum irqchip_irq_state which, bool *state);
	int		(*irq_set_irqchip_state)(struct irq_data *data, enum irqchip_irq_state which, bool state);

	int		(*irq_set_vcpu_affinity)(struct irq_data *data, void *vcpu_info);

	void		(*ipi_send_single)(struct irq_data *data, unsigned int cpu);
	void		(*ipi_send_mask)(struct irq_data *data, const struct cpumask *dest);

	int		(*irq_nmi_setup)(struct irq_data *data);
	void		(*irq_nmi_teardown)(struct irq_data *data);

	unsigned long	flags;
};

/*
 * irq_chip specific flags
 *
 * IRQCHIP_SET_TYPE_MASKED:           Mask before calling chip.irq_set_type()
 * IRQCHIP_EOI_IF_HANDLED:            Only issue irq_eoi() when irq was handled
 * IRQCHIP_MASK_ON_SUSPEND:           Mask non wake irqs in the suspend path
 * IRQCHIP_ONOFFLINE_ENABLED:         Only call irq_on/off_line callbacks
 *                                    when irq enabled
 * IRQCHIP_SKIP_SET_WAKE:             Skip chip.irq_set_wake(), for this irq chip
 * IRQCHIP_ONESHOT_SAFE:              One shot does not require mask/unmask
 * IRQCHIP_EOI_THREADED:              Chip requires eoi() on unmask in threaded mode
 * IRQCHIP_SUPPORTS_LEVEL_MSI:        Chip can provide two doorbells for Level MSIs
 * IRQCHIP_SUPPORTS_NMI:              Chip can deliver NMIs, only for root irqchips
 * IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND:  Invokes __enable_irq()/__disable_irq() for wake irqs
 *                                    in the suspend path if they are in disabled state
 * IRQCHIP_AFFINITY_PRE_STARTUP:      Default affinity update before startup
 */
enum {
	IRQCHIP_SET_TYPE_MASKED			= (1 <<  0),
	IRQCHIP_EOI_IF_HANDLED			= (1 <<  1),
	IRQCHIP_MASK_ON_SUSPEND			= (1 <<  2),
	IRQCHIP_ONOFFLINE_ENABLED		= (1 <<  3),
	IRQCHIP_SKIP_SET_WAKE			= (1 <<  4),
	IRQCHIP_ONESHOT_SAFE			= (1 <<  5),
	IRQCHIP_EOI_THREADED			= (1 <<  6),
	IRQCHIP_SUPPORTS_LEVEL_MSI		= (1 <<  7),
	IRQCHIP_SUPPORTS_NMI			= (1 <<  8),
	IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND	= (1 <<  9),
	IRQCHIP_AFFINITY_PRE_STARTUP		= (1 << 10),
};

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
extern int setup_percpu_irq(unsigned int irq, struct irqaction *new);
extern void remove_percpu_irq(unsigned int irq, struct irqaction *act);

#ifdef CONFIG_DEPRECATED_IRQ_CPU_ONOFFLINE
extern void irq_cpu_online(void);
extern void irq_cpu_offline(void);
#endif
extern int irq_set_affinity_locked(struct irq_data *data,
				   const struct cpumask *cpumask, bool force);
extern int irq_set_vcpu_affinity(unsigned int irq, void *vcpu_info);

#if defined(CONFIG_SMP) && defined(CONFIG_GENERIC_IRQ_MIGRATION)
extern void irq_migrate_all_off_this_cpu(void);
extern int irq_affinity_online_cpu(unsigned int cpu);
#else
# define irq_affinity_online_cpu	NULL
#endif

#if defined(CONFIG_SMP) && defined(CONFIG_GENERIC_PENDING_IRQ)
void __irq_move_irq(struct irq_data *data);
static inline void irq_move_irq(struct irq_data *data)
{
	if (unlikely(irqd_is_setaffinity_pending(data)))
		__irq_move_irq(data);
}
void irq_move_masked_irq(struct irq_data *data);
void irq_force_complete_move(struct irq_desc *desc);
#else
static inline void irq_move_irq(struct irq_data *data) { }
static inline void irq_move_masked_irq(struct irq_data *data) { }
static inline void irq_force_complete_move(struct irq_desc *desc) { }
#endif

extern int no_irq_affinity;

#ifdef CONFIG_HARDIRQS_SW_RESEND
int irq_set_parent(int irq, int parent_irq);
#else
static inline int irq_set_parent(int irq, int parent_irq)
{
	return 0;
}
#endif

/*
 * Built-in IRQ handlers for various IRQ types,
 * callable via desc->handle_irq()
 */
extern void handle_level_irq(struct irq_desc *desc);
extern void handle_fasteoi_irq(struct irq_desc *desc);
extern void handle_edge_irq(struct irq_desc *desc);
extern void handle_edge_eoi_irq(struct irq_desc *desc);
extern void handle_simple_irq(struct irq_desc *desc);
extern void handle_untracked_irq(struct irq_desc *desc);
extern void handle_percpu_irq(struct irq_desc *desc);
extern void handle_percpu_devid_irq(struct irq_desc *desc);
extern void handle_bad_irq(struct irq_desc *desc);
extern void handle_nested_irq(unsigned int irq);

extern void handle_fasteoi_nmi(struct irq_desc *desc);
extern void handle_percpu_devid_fasteoi_nmi(struct irq_desc *desc);

extern int irq_chip_compose_msi_msg(struct irq_data *data, struct msi_msg *msg);
extern int irq_chip_pm_get(struct irq_data *data);
extern int irq_chip_pm_put(struct irq_data *data);
#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
extern void handle_fasteoi_ack_irq(struct irq_desc *desc);
extern void handle_fasteoi_mask_irq(struct irq_desc *desc);
extern int irq_chip_set_parent_state(struct irq_data *data,
				     enum irqchip_irq_state which,
				     bool val);
extern int irq_chip_get_parent_state(struct irq_data *data,
				     enum irqchip_irq_state which,
				     bool *state);
extern void irq_chip_enable_parent(struct irq_data *data);
extern void irq_chip_disable_parent(struct irq_data *data);
extern void irq_chip_ack_parent(struct irq_data *data);
extern int irq_chip_retrigger_hierarchy(struct irq_data *data);
extern void irq_chip_mask_parent(struct irq_data *data);
extern void irq_chip_mask_ack_parent(struct irq_data *data);
extern void irq_chip_unmask_parent(struct irq_data *data);
extern void irq_chip_eoi_parent(struct irq_data *data);
extern int irq_chip_set_affinity_parent(struct irq_data *data,
					const struct cpumask *dest,
					bool force);
extern int irq_chip_set_wake_parent(struct irq_data *data, unsigned int on);
extern int irq_chip_set_vcpu_affinity_parent(struct irq_data *data,
					     void *vcpu_info);
extern int irq_chip_set_type_parent(struct irq_data *data, unsigned int type);
extern int irq_chip_request_resources_parent(struct irq_data *data);
extern void irq_chip_release_resources_parent(struct irq_data *data);
#endif

/* Handling of unhandled and spurious interrupts: */
extern void note_interrupt(struct irq_desc *desc, irqreturn_t action_ret);


/* Enable/disable irq debugging output: */
extern int noirqdebug_setup(char *str);

/* Checks whether the interrupt can be requested by request_irq(): */
extern int can_request_irq(unsigned int irq, unsigned long irqflags);

/* Dummy irq-chip implementations: */
extern struct irq_chip no_irq_chip;
extern struct irq_chip dummy_irq_chip;

extern void
irq_set_chip_and_handler_name(unsigned int irq, const struct irq_chip *chip,
			      irq_flow_handler_t handle, const char *name);

static inline void irq_set_chip_and_handler(unsigned int irq,
					    const struct irq_chip *chip,
					    irq_flow_handler_t handle)
{
	irq_set_chip_and_handler_name(irq, chip, handle, NULL);
}

extern int irq_set_percpu_devid(unsigned int irq);
extern int irq_set_percpu_devid_partition(unsigned int irq,
					  const struct cpumask *affinity);
extern int irq_get_percpu_devid_partition(unsigned int irq,
					  struct cpumask *affinity);

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

/*
 * Set a highlevel chained flow handler and its data for a given IRQ.
 * (a chained handler is automatically enabled and set to
 *  IRQ_NOREQUEST, IRQ_NOPROBE, and IRQ_NOTHREAD)
 */
void
irq_set_chained_handler_and_data(unsigned int irq, irq_flow_handler_t handle,
				 void *data);

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

/* Set/get chip/data for an IRQ: */
extern int irq_set_chip(unsigned int irq, const struct irq_chip *chip);
extern int irq_set_handler_data(unsigned int irq, void *data);
extern int irq_set_chip_data(unsigned int irq, void *data);
extern int irq_set_irq_type(unsigned int irq, unsigned int type);
extern int irq_set_msi_desc(unsigned int irq, struct msi_desc *entry);
extern int irq_set_msi_desc_off(unsigned int irq_base, unsigned int irq_offset,
				struct msi_desc *entry);
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
	return d ? d->common->handler_data : NULL;
}

static inline void *irq_data_get_irq_handler_data(struct irq_data *d)
{
	return d->common->handler_data;
}

static inline struct msi_desc *irq_get_msi_desc(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);
	return d ? d->common->msi_desc : NULL;
}

static inline struct msi_desc *irq_data_get_msi_desc(struct irq_data *d)
{
	return d->common->msi_desc;
}

static inline u32 irq_get_trigger_type(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);
	return d ? irqd_get_trigger_type(d) : 0;
}

static inline int irq_common_data_get_node(struct irq_common_data *d)
{
#ifdef CONFIG_NUMA
	return d->node;
#else
	return 0;
#endif
}

static inline int irq_data_get_node(struct irq_data *d)
{
	return irq_common_data_get_node(d->common);
}

static inline struct cpumask *irq_get_affinity_mask(int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);

	return d ? d->common->affinity : NULL;
}

static inline struct cpumask *irq_data_get_affinity_mask(struct irq_data *d)
{
	return d->common->affinity;
}

#ifdef CONFIG_GENERIC_IRQ_EFFECTIVE_AFF_MASK
static inline
struct cpumask *irq_data_get_effective_affinity_mask(struct irq_data *d)
{
	return d->common->effective_affinity;
}
static inline void irq_data_update_effective_affinity(struct irq_data *d,
						      const struct cpumask *m)
{
	cpumask_copy(d->common->effective_affinity, m);
}
#else
static inline void irq_data_update_effective_affinity(struct irq_data *d,
						      const struct cpumask *m)
{
}
static inline
struct cpumask *irq_data_get_effective_affinity_mask(struct irq_data *d)
{
	return d->common->affinity;
}
#endif

static inline struct cpumask *irq_get_effective_affinity_mask(unsigned int irq)
{
	struct irq_data *d = irq_get_irq_data(irq);

	return d ? irq_data_get_effective_affinity_mask(d) : NULL;
}

unsigned int arch_dynirq_lower_bound(unsigned int from);

int __irq_alloc_descs(int irq, unsigned int from, unsigned int cnt, int node,
		      struct module *owner,
		      const struct irq_affinity_desc *affinity);

int __devm_irq_alloc_descs(struct device *dev, int irq, unsigned int from,
			   unsigned int cnt, int node, struct module *owner,
			   const struct irq_affinity_desc *affinity);

/* use macros to avoid needing export.h for THIS_MODULE */
#define irq_alloc_descs(irq, from, cnt, node)	\
	__irq_alloc_descs(irq, from, cnt, node, THIS_MODULE, NULL)

#define irq_alloc_desc(node)			\
	irq_alloc_descs(-1, 1, 1, node)

#define irq_alloc_desc_at(at, node)		\
	irq_alloc_descs(at, at, 1, node)

#define irq_alloc_desc_from(from, node)		\
	irq_alloc_descs(-1, from, 1, node)

#define irq_alloc_descs_from(from, cnt, node)	\
	irq_alloc_descs(-1, from, cnt, node)

#define devm_irq_alloc_descs(dev, irq, from, cnt, node)		\
	__devm_irq_alloc_descs(dev, irq, from, cnt, node, THIS_MODULE, NULL)

#define devm_irq_alloc_desc(dev, node)				\
	devm_irq_alloc_descs(dev, -1, 1, 1, node)

#define devm_irq_alloc_desc_at(dev, at, node)			\
	devm_irq_alloc_descs(dev, at, at, 1, node)

#define devm_irq_alloc_desc_from(dev, from, node)		\
	devm_irq_alloc_descs(dev, -1, from, 1, node)

#define devm_irq_alloc_descs_from(dev, from, cnt, node)		\
	devm_irq_alloc_descs(dev, -1, from, cnt, node)

void irq_free_descs(unsigned int irq, unsigned int cnt);
static inline void irq_free_desc(unsigned int irq)
{
	irq_free_descs(irq, 1);
}

#ifdef CONFIG_GENERIC_IRQ_LEGACY
void irq_init_desc(unsigned int irq);
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
 * @mask_cache_priv:	Cached mask register private to the chip type
 * @mask_cache:		Pointer to cached mask register
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
	u32			mask_cache_priv;
	u32			*mask_cache;
};

/**
 * struct irq_chip_generic - Generic irq chip data structure
 * @lock:		Lock to protect register and cache data access
 * @reg_base:		Register base address (virtual)
 * @reg_readl:		Alternate I/O accessor (defaults to readl if NULL)
 * @reg_writel:		Alternate I/O accessor (defaults to writel if NULL)
 * @suspend:		Function called from core code on suspend once per
 *			chip; can be useful instead of irq_chip::suspend to
 *			handle chip details even when no interrupts are in use
 * @resume:		Function called from core code on resume once per chip;
 *			can be useful instead of irq_chip::suspend to handle
 *			chip details even when no interrupts are in use
 * @irq_base:		Interrupt base nr for this chip
 * @irq_cnt:		Number of interrupts handled by this chip
 * @mask_cache:		Cached mask register shared between all chip types
 * @type_cache:		Cached type register
 * @polarity_cache:	Cached polarity register
 * @wake_enabled:	Interrupt can wakeup from suspend
 * @wake_active:	Interrupt is marked as an wakeup from suspend source
 * @num_ct:		Number of available irq_chip_type instances (usually 1)
 * @private:		Private data for non generic chip callbacks
 * @installed:		bitfield to denote installed interrupts
 * @unused:		bitfield to denote unused interrupts
 * @domain:		irq domain pointer
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
	u32			(*reg_readl)(void __iomem *addr);
	void			(*reg_writel)(u32 val, void __iomem *addr);
	void			(*suspend)(struct irq_chip_generic *gc);
	void			(*resume)(struct irq_chip_generic *gc);
	unsigned int		irq_base;
	unsigned int		irq_cnt;
	u32			mask_cache;
	u32			type_cache;
	u32			polarity_cache;
	u32			wake_enabled;
	u32			wake_active;
	unsigned int		num_ct;
	void			*private;
	unsigned long		installed;
	unsigned long		unused;
	struct irq_domain	*domain;
	struct list_head	list;
	struct irq_chip_type	chip_types[];
};

/**
 * enum irq_gc_flags - Initialization flags for generic irq chips
 * @IRQ_GC_INIT_MASK_CACHE:	Initialize the mask_cache by reading mask reg
 * @IRQ_GC_INIT_NESTED_LOCK:	Set the lock class of the irqs to nested for
 *				irq chips which need to call irq_set_wake() on
 *				the parent irq. Usually GPIO implementations
 * @IRQ_GC_MASK_CACHE_PER_TYPE:	Mask cache is chip type private
 * @IRQ_GC_NO_MASK:		Do not calculate irq_data->mask
 * @IRQ_GC_BE_IO:		Use big-endian register accesses (default: LE)
 */
enum irq_gc_flags {
	IRQ_GC_INIT_MASK_CACHE		= 1 << 0,
	IRQ_GC_INIT_NESTED_LOCK		= 1 << 1,
	IRQ_GC_MASK_CACHE_PER_TYPE	= 1 << 2,
	IRQ_GC_NO_MASK			= 1 << 3,
	IRQ_GC_BE_IO			= 1 << 4,
};

/*
 * struct irq_domain_chip_generic - Generic irq chip data structure for irq domains
 * @irqs_per_chip:	Number of interrupts per chip
 * @num_chips:		Number of chips
 * @irq_flags_to_set:	IRQ* flags to set on irq setup
 * @irq_flags_to_clear:	IRQ* flags to clear on irq setup
 * @gc_flags:		Generic chip specific setup flags
 * @gc:			Array of pointers to generic interrupt chips
 */
struct irq_domain_chip_generic {
	unsigned int		irqs_per_chip;
	unsigned int		num_chips;
	unsigned int		irq_flags_to_clear;
	unsigned int		irq_flags_to_set;
	enum irq_gc_flags	gc_flags;
	struct irq_chip_generic	*gc[];
};

/* Generic chip callback functions */
void irq_gc_noop(struct irq_data *d);
void irq_gc_mask_disable_reg(struct irq_data *d);
void irq_gc_mask_set_bit(struct irq_data *d);
void irq_gc_mask_clr_bit(struct irq_data *d);
void irq_gc_unmask_enable_reg(struct irq_data *d);
void irq_gc_ack_set_bit(struct irq_data *d);
void irq_gc_ack_clr_bit(struct irq_data *d);
void irq_gc_mask_disable_and_ack_set(struct irq_data *d);
void irq_gc_eoi(struct irq_data *d);
int irq_gc_set_wake(struct irq_data *d, unsigned int on);

/* Setup functions for irq_chip_generic */
int irq_map_generic_chip(struct irq_domain *d, unsigned int virq,
			 irq_hw_number_t hw_irq);
struct irq_chip_generic *
irq_alloc_generic_chip(const char *name, int nr_ct, unsigned int irq_base,
		       void __iomem *reg_base, irq_flow_handler_t handler);
void irq_setup_generic_chip(struct irq_chip_generic *gc, u32 msk,
			    enum irq_gc_flags flags, unsigned int clr,
			    unsigned int set);
int irq_setup_alt_chip(struct irq_data *d, unsigned int type);
void irq_remove_generic_chip(struct irq_chip_generic *gc, u32 msk,
			     unsigned int clr, unsigned int set);

struct irq_chip_generic *
devm_irq_alloc_generic_chip(struct device *dev, const char *name, int num_ct,
			    unsigned int irq_base, void __iomem *reg_base,
			    irq_flow_handler_t handler);
int devm_irq_setup_generic_chip(struct device *dev, struct irq_chip_generic *gc,
				u32 msk, enum irq_gc_flags flags,
				unsigned int clr, unsigned int set);

struct irq_chip_generic *irq_get_domain_generic_chip(struct irq_domain *d, unsigned int hw_irq);

int __irq_alloc_domain_generic_chips(struct irq_domain *d, int irqs_per_chip,
				     int num_ct, const char *name,
				     irq_flow_handler_t handler,
				     unsigned int clr, unsigned int set,
				     enum irq_gc_flags flags);

#define irq_alloc_domain_generic_chips(d, irqs_per_chip, num_ct, name,	\
				       handler,	clr, set, flags)	\
({									\
	MAYBE_BUILD_BUG_ON(irqs_per_chip > 32);				\
	__irq_alloc_domain_generic_chips(d, irqs_per_chip, num_ct, name,\
					 handler, clr, set, flags);	\
})

static inline void irq_free_generic_chip(struct irq_chip_generic *gc)
{
	kfree(gc);
}

static inline void irq_destroy_generic_chip(struct irq_chip_generic *gc,
					    u32 msk, unsigned int clr,
					    unsigned int set)
{
	irq_remove_generic_chip(gc, msk, clr, set);
	irq_free_generic_chip(gc);
}

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

/*
 * The irqsave variants are for usage in non interrupt code. Do not use
 * them in irq_chip callbacks. Use irq_gc_lock() instead.
 */
#define irq_gc_lock_irqsave(gc, flags)	\
	raw_spin_lock_irqsave(&(gc)->lock, flags)

#define irq_gc_unlock_irqrestore(gc, flags)	\
	raw_spin_unlock_irqrestore(&(gc)->lock, flags)

static inline void irq_reg_writel(struct irq_chip_generic *gc,
				  u32 val, int reg_offset)
{
	if (gc->reg_writel)
		gc->reg_writel(val, gc->reg_base + reg_offset);
	else
		writel(val, gc->reg_base + reg_offset);
}

static inline u32 irq_reg_readl(struct irq_chip_generic *gc,
				int reg_offset)
{
	if (gc->reg_readl)
		return gc->reg_readl(gc->reg_base + reg_offset);
	else
		return readl(gc->reg_base + reg_offset);
}

struct irq_matrix;
struct irq_matrix *irq_alloc_matrix(unsigned int matrix_bits,
				    unsigned int alloc_start,
				    unsigned int alloc_end);
void irq_matrix_online(struct irq_matrix *m);
void irq_matrix_offline(struct irq_matrix *m);
void irq_matrix_assign_system(struct irq_matrix *m, unsigned int bit, bool replace);
int irq_matrix_reserve_managed(struct irq_matrix *m, const struct cpumask *msk);
void irq_matrix_remove_managed(struct irq_matrix *m, const struct cpumask *msk);
int irq_matrix_alloc_managed(struct irq_matrix *m, const struct cpumask *msk,
				unsigned int *mapped_cpu);
void irq_matrix_reserve(struct irq_matrix *m);
void irq_matrix_remove_reserved(struct irq_matrix *m);
int irq_matrix_alloc(struct irq_matrix *m, const struct cpumask *msk,
		     bool reserved, unsigned int *mapped_cpu);
void irq_matrix_free(struct irq_matrix *m, unsigned int cpu,
		     unsigned int bit, bool managed);
void irq_matrix_assign(struct irq_matrix *m, unsigned int bit);
unsigned int irq_matrix_available(struct irq_matrix *m, bool cpudown);
unsigned int irq_matrix_allocated(struct irq_matrix *m);
unsigned int irq_matrix_reserved(struct irq_matrix *m);
void irq_matrix_debug_show(struct seq_file *sf, struct irq_matrix *m, int ind);

/* Contrary to Linux irqs, for hardware irqs the irq number 0 is valid */
#define INVALID_HWIRQ	(~0UL)
irq_hw_number_t ipi_get_hwirq(unsigned int irq, unsigned int cpu);
int __ipi_send_single(struct irq_desc *desc, unsigned int cpu);
int __ipi_send_mask(struct irq_desc *desc, const struct cpumask *dest);
int ipi_send_single(unsigned int virq, unsigned int cpu);
int ipi_send_mask(unsigned int virq, const struct cpumask *dest);

#ifdef CONFIG_GENERIC_IRQ_MULTI_HANDLER
/*
 * Registers a generic IRQ handling function as the top-level IRQ handler in
 * the system, which is generally the first C code called from an assembly
 * architecture-specific interrupt handler.
 *
 * Returns 0 on success, or -EBUSY if an IRQ handler has already been
 * registered.
 */
int __init set_handle_irq(void (*handle_irq)(struct pt_regs *));

/*
 * Allows interrupt handlers to find the irqchip that's been registered as the
 * top-level IRQ handler.
 */
extern void (*handle_arch_irq)(struct pt_regs *) __ro_after_init;
asmlinkage void generic_handle_arch_irq(struct pt_regs *regs);
#else
#ifndef set_handle_irq
#define set_handle_irq(handle_irq)		\
	do {					\
		(void)handle_irq;		\
		WARN_ON(1);			\
	} while (0)
#endif
#endif

#endif /* _LINUX_IRQ_H */

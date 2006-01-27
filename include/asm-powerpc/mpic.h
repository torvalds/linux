#ifndef _ASM_POWERPC_MPIC_H
#define _ASM_POWERPC_MPIC_H
#ifdef __KERNEL__

#include <linux/irq.h>

/*
 * Global registers
 */

#define MPIC_GREG_BASE			0x01000

#define MPIC_GREG_FEATURE_0		0x00000
#define		MPIC_GREG_FEATURE_LAST_SRC_MASK		0x07ff0000
#define		MPIC_GREG_FEATURE_LAST_SRC_SHIFT	16
#define		MPIC_GREG_FEATURE_LAST_CPU_MASK		0x00001f00
#define		MPIC_GREG_FEATURE_LAST_CPU_SHIFT	8
#define		MPIC_GREG_FEATURE_VERSION_MASK		0xff
#define MPIC_GREG_FEATURE_1		0x00010
#define MPIC_GREG_GLOBAL_CONF_0		0x00020
#define		MPIC_GREG_GCONF_RESET			0x80000000
#define		MPIC_GREG_GCONF_8259_PTHROU_DIS		0x20000000
#define		MPIC_GREG_GCONF_BASE_MASK		0x000fffff
#define MPIC_GREG_GLOBAL_CONF_1		0x00030
#define MPIC_GREG_VENDOR_0		0x00040
#define MPIC_GREG_VENDOR_1		0x00050
#define MPIC_GREG_VENDOR_2		0x00060
#define MPIC_GREG_VENDOR_3		0x00070
#define MPIC_GREG_VENDOR_ID		0x00080
#define 	MPIC_GREG_VENDOR_ID_STEPPING_MASK	0x00ff0000
#define 	MPIC_GREG_VENDOR_ID_STEPPING_SHIFT	16
#define 	MPIC_GREG_VENDOR_ID_DEVICE_ID_MASK	0x0000ff00
#define 	MPIC_GREG_VENDOR_ID_DEVICE_ID_SHIFT	8
#define 	MPIC_GREG_VENDOR_ID_VENDOR_ID_MASK	0x000000ff
#define MPIC_GREG_PROCESSOR_INIT	0x00090
#define MPIC_GREG_IPI_VECTOR_PRI_0	0x000a0
#define MPIC_GREG_IPI_VECTOR_PRI_1	0x000b0
#define MPIC_GREG_IPI_VECTOR_PRI_2	0x000c0
#define MPIC_GREG_IPI_VECTOR_PRI_3	0x000d0
#define MPIC_GREG_SPURIOUS		0x000e0
#define MPIC_GREG_TIMER_FREQ		0x000f0

/*
 *
 * Timer registers
 */
#define MPIC_TIMER_BASE			0x01100
#define MPIC_TIMER_STRIDE		0x40

#define MPIC_TIMER_CURRENT_CNT		0x00000
#define MPIC_TIMER_BASE_CNT		0x00010
#define MPIC_TIMER_VECTOR_PRI		0x00020
#define MPIC_TIMER_DESTINATION		0x00030

/*
 * Per-Processor registers
 */

#define MPIC_CPU_THISBASE		0x00000
#define MPIC_CPU_BASE			0x20000
#define MPIC_CPU_STRIDE			0x01000

#define MPIC_CPU_IPI_DISPATCH_0		0x00040
#define MPIC_CPU_IPI_DISPATCH_1		0x00050
#define MPIC_CPU_IPI_DISPATCH_2		0x00060
#define MPIC_CPU_IPI_DISPATCH_3		0x00070
#define MPIC_CPU_CURRENT_TASK_PRI	0x00080
#define 	MPIC_CPU_TASKPRI_MASK			0x0000000f
#define MPIC_CPU_WHOAMI			0x00090
#define 	MPIC_CPU_WHOAMI_MASK			0x0000001f
#define MPIC_CPU_INTACK			0x000a0
#define MPIC_CPU_EOI			0x000b0

/*
 * Per-source registers
 */

#define MPIC_IRQ_BASE			0x10000
#define MPIC_IRQ_STRIDE			0x00020
#define MPIC_IRQ_VECTOR_PRI		0x00000
#define 	MPIC_VECPRI_MASK			0x80000000
#define 	MPIC_VECPRI_ACTIVITY			0x40000000	/* Read Only */
#define 	MPIC_VECPRI_PRIORITY_MASK		0x000f0000
#define 	MPIC_VECPRI_PRIORITY_SHIFT		16
#define 	MPIC_VECPRI_VECTOR_MASK			0x000007ff
#define 	MPIC_VECPRI_POLARITY_POSITIVE		0x00800000
#define 	MPIC_VECPRI_POLARITY_NEGATIVE		0x00000000
#define 	MPIC_VECPRI_POLARITY_MASK		0x00800000
#define 	MPIC_VECPRI_SENSE_LEVEL			0x00400000
#define 	MPIC_VECPRI_SENSE_EDGE			0x00000000
#define 	MPIC_VECPRI_SENSE_MASK			0x00400000
#define MPIC_IRQ_DESTINATION		0x00010

#define MPIC_MAX_IRQ_SOURCES	2048
#define MPIC_MAX_CPUS		32
#define MPIC_MAX_ISU		32

/*
 * Special vector numbers (internal use only)
 */
#define MPIC_VEC_SPURRIOUS	255
#define MPIC_VEC_IPI_3		254
#define MPIC_VEC_IPI_2		253
#define MPIC_VEC_IPI_1		252
#define MPIC_VEC_IPI_0		251

/* unused */
#define MPIC_VEC_TIMER_3	250
#define MPIC_VEC_TIMER_2	249
#define MPIC_VEC_TIMER_1	248
#define MPIC_VEC_TIMER_0	247

/* Type definition of the cascade handler */
typedef int (*mpic_cascade_t)(struct pt_regs *regs, void *data);

#ifdef CONFIG_MPIC_BROKEN_U3
/* Fixup table entry */
struct mpic_irq_fixup
{
	u8 __iomem	*base;
	u8 __iomem	*applebase;
	u32		data;
	unsigned int	index;
};
#endif /* CONFIG_MPIC_BROKEN_U3 */


/* The instance data of a given MPIC */
struct mpic
{
	/* The "linux" controller struct */
	hw_irq_controller	hc_irq;
#ifdef CONFIG_SMP
	hw_irq_controller	hc_ipi;
#endif
	const char		*name;
	/* Flags */
	unsigned int		flags;
	/* How many irq sources in a given ISU */
	unsigned int		isu_size;
	unsigned int		isu_shift;
	unsigned int		isu_mask;
	/* Offset of irq vector numbers */
	unsigned int		irq_offset;	
	unsigned int		irq_count;
	/* Offset of ipi vector numbers */
	unsigned int		ipi_offset;
	/* Number of sources */
	unsigned int		num_sources;
	/* Number of CPUs */
	unsigned int		num_cpus;
	/* cascade handler */
	mpic_cascade_t		cascade;
	void			*cascade_data;
	unsigned int		cascade_vec;
	/* senses array */
	unsigned char		*senses;
	unsigned int		senses_count;

#ifdef CONFIG_MPIC_BROKEN_U3
	/* The fixup table */
	struct mpic_irq_fixup	*fixups;
	spinlock_t		fixup_lock;
#endif

	/* The various ioremap'ed bases */
	volatile u32 __iomem	*gregs;
	volatile u32 __iomem	*tmregs;
	volatile u32 __iomem	*cpuregs[MPIC_MAX_CPUS];
	volatile u32 __iomem	*isus[MPIC_MAX_ISU];

	/* link */
	struct mpic		*next;
};

/* This is the primary controller, only that one has IPIs and
 * has afinity control. A non-primary MPIC always uses CPU0
 * registers only
 */
#define MPIC_PRIMARY			0x00000001
/* Set this for a big-endian MPIC */
#define MPIC_BIG_ENDIAN			0x00000002
/* Broken U3 MPIC */
#define MPIC_BROKEN_U3			0x00000004
/* Broken IPI registers (autodetected) */
#define MPIC_BROKEN_IPI			0x00000008
/* MPIC wants a reset */
#define MPIC_WANTS_RESET		0x00000010

/* Allocate the controller structure and setup the linux irq descs
 * for the range if interrupts passed in. No HW initialization is
 * actually performed.
 * 
 * @phys_addr:	physial base address of the MPIC
 * @flags:	flags, see constants above
 * @isu_size:	number of interrupts in an ISU. Use 0 to use a
 *              standard ISU-less setup (aka powermac)
 * @irq_offset: first irq number to assign to this mpic
 * @irq_count:  number of irqs to use with this mpic IRQ sources. Pass 0
 *	        to match the number of sources
 * @ipi_offset: first irq number to assign to this mpic IPI sources,
 *		used only on primary mpic
 * @senses:	array of sense values
 * @senses_num: number of entries in the array
 *
 * Note about the sense array. If none is passed, all interrupts are
 * setup to be level negative unless MPIC_BROKEN_U3 is set in which
 * case they are edge positive (and the array is ignored anyway).
 * The values in the array start at the first source of the MPIC,
 * that is senses[0] correspond to linux irq "irq_offset".
 */
extern struct mpic *mpic_alloc(unsigned long phys_addr,
			       unsigned int flags,
			       unsigned int isu_size,
			       unsigned int irq_offset,
			       unsigned int irq_count,
			       unsigned int ipi_offset,
			       unsigned char *senses,
			       unsigned int senses_num,
			       const char *name);

/* Assign ISUs, to call before mpic_init()
 *
 * @mpic:	controller structure as returned by mpic_alloc()
 * @isu_num:	ISU number
 * @phys_addr:	physical address of the ISU
 */
extern void mpic_assign_isu(struct mpic *mpic, unsigned int isu_num,
			    unsigned long phys_addr);

/* Initialize the controller. After this has been called, none of the above
 * should be called again for this mpic
 */
extern void mpic_init(struct mpic *mpic);

/* Setup a cascade. Currently, only one cascade is supported this
 * way, though you can always do a normal request_irq() and add
 * other cascades this way. You should call this _after_ having
 * added all the ISUs
 *
 * @irq_no:	"linux" irq number of the cascade (that is offset'ed vector)
 * @handler:	cascade handler function
 */
extern void mpic_setup_cascade(unsigned int irq_no, mpic_cascade_t hanlder,
			       void *data);

/*
 * All of the following functions must only be used after the
 * ISUs have been assigned and the controller fully initialized
 * with mpic_init()
 */


/* Change/Read the priority of an interrupt. Default is 8 for irqs and
 * 10 for IPIs. You can call this on both IPIs and IRQ numbers, but the
 * IPI number is then the offset'ed (linux irq number mapped to the IPI)
 */
extern void mpic_irq_set_priority(unsigned int irq, unsigned int pri);
extern unsigned int mpic_irq_get_priority(unsigned int irq);

/* Setup a non-boot CPU */
extern void mpic_setup_this_cpu(void);

/* Clean up for kexec (or cpu offline or ...) */
extern void mpic_teardown_this_cpu(int secondary);

/* Get the current cpu priority for this cpu (0..15) */
extern int mpic_cpu_get_priority(void);

/* Set the current cpu priority for this cpu */
extern void mpic_cpu_set_priority(int prio);

/* Request IPIs on primary mpic */
extern void mpic_request_ipis(void);

/* Send an IPI (non offseted number 0..3) */
extern void mpic_send_ipi(unsigned int ipi_no, unsigned int cpu_mask);

/* Send a message (IPI) to a given target (cpu number or MSG_*) */
void smp_mpic_message_pass(int target, int msg);

/* Fetch interrupt from a given mpic */
extern int mpic_get_one_irq(struct mpic *mpic, struct pt_regs *regs);
/* This one gets to the primary mpic */
extern int mpic_get_irq(struct pt_regs *regs);

/* global mpic for pSeries */
extern struct mpic *pSeries_mpic;

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_MPIC_H */

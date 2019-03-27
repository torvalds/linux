/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _X86_APICVAR_H_
#define _X86_APICVAR_H_

/*
 * Local && I/O APIC variable definitions.
 */

/*
 * Layout of local APIC interrupt vectors:
 *
 *	0xff (255)  +-------------+
 *                  |             | 15 (Spurious / IPIs / Local Interrupts)
 *	0xf0 (240)  +-------------+
 *                  |             | 14 (I/O Interrupts / Timer)
 *	0xe0 (224)  +-------------+
 *                  |             | 13 (I/O Interrupts)
 *	0xd0 (208)  +-------------+
 *                  |             | 12 (I/O Interrupts)
 *	0xc0 (192)  +-------------+
 *                  |             | 11 (I/O Interrupts)
 *	0xb0 (176)  +-------------+
 *                  |             | 10 (I/O Interrupts)
 *	0xa0 (160)  +-------------+
 *                  |             | 9 (I/O Interrupts)
 *	0x90 (144)  +-------------+
 *                  |             | 8 (I/O Interrupts / System Calls)
 *	0x80 (128)  +-------------+
 *                  |             | 7 (I/O Interrupts)
 *	0x70 (112)  +-------------+
 *                  |             | 6 (I/O Interrupts)
 *	0x60 (96)   +-------------+
 *                  |             | 5 (I/O Interrupts)
 *	0x50 (80)   +-------------+
 *                  |             | 4 (I/O Interrupts)
 *	0x40 (64)   +-------------+
 *                  |             | 3 (I/O Interrupts)
 *	0x30 (48)   +-------------+
 *                  |             | 2 (ATPIC Interrupts)
 *	0x20 (32)   +-------------+
 *                  |             | 1 (Exceptions, traps, faults, etc.)
 *	0x10 (16)   +-------------+
 *                  |             | 0 (Exceptions, traps, faults, etc.)
 *	0x00 (0)    +-------------+
 *
 * Note: 0x80 needs to be handled specially and not allocated to an
 * I/O device!
 */

#define	xAPIC_MAX_APIC_ID	0xfe
#define	xAPIC_ID_ALL		0xff
#define	MAX_APIC_ID		0x200
#define	APIC_ID_ALL		0xffffffff

#define	IOAPIC_MAX_ID		xAPIC_MAX_APIC_ID

/* I/O Interrupts are used for external devices such as ISA, PCI, etc. */
#define	APIC_IO_INTS	(IDT_IO_INTS + 16)
#define	APIC_NUM_IOINTS	191

/* The timer interrupt is used for clock handling and drives hardclock, etc. */
#define	APIC_TIMER_INT	(APIC_IO_INTS + APIC_NUM_IOINTS)

/*  
 ********************* !!! WARNING !!! ******************************
 * Each local apic has an interrupt receive fifo that is two entries deep
 * for each interrupt priority class (higher 4 bits of interrupt vector).
 * Once the fifo is full the APIC can no longer receive interrupts for this
 * class and sending IPIs from other CPUs will be blocked.
 * To avoid deadlocks there should be no more than two IPI interrupts
 * pending at the same time.
 * Currently this is guaranteed by dividing the IPIs in two groups that have 
 * each at most one IPI interrupt pending. The first group is protected by the
 * smp_ipi_mtx and waits for the completion of the IPI (Only one IPI user 
 * at a time) The second group uses a single interrupt and a bitmap to avoid
 * redundant IPI interrupts.
 */ 

/* Interrupts for local APIC LVT entries other than the timer. */
#define	APIC_LOCAL_INTS	240
#define	APIC_ERROR_INT	APIC_LOCAL_INTS
#define	APIC_THERMAL_INT (APIC_LOCAL_INTS + 1)
#define	APIC_CMC_INT	(APIC_LOCAL_INTS + 2)
#define	APIC_IPI_INTS	(APIC_LOCAL_INTS + 3)

#define	IPI_RENDEZVOUS	(APIC_IPI_INTS)		/* Inter-CPU rendezvous. */
#define	IPI_INVLTLB	(APIC_IPI_INTS + 1)	/* TLB Shootdown IPIs */
#define	IPI_INVLPG	(APIC_IPI_INTS + 2)
#define	IPI_INVLRNG	(APIC_IPI_INTS + 3)
#define	IPI_INVLCACHE	(APIC_IPI_INTS + 4)
/* Vector to handle bitmap based IPIs */
#define	IPI_BITMAP_VECTOR	(APIC_IPI_INTS + 5) 

/* IPIs handled by IPI_BITMAP_VECTOR */
#define	IPI_AST		0 	/* Generate software trap. */
#define IPI_PREEMPT     1
#define IPI_HARDCLOCK   2
#define IPI_BITMAP_LAST IPI_HARDCLOCK
#define IPI_IS_BITMAPED(x) ((x) <= IPI_BITMAP_LAST)

#define	IPI_STOP	(APIC_IPI_INTS + 6)	/* Stop CPU until restarted. */
#define	IPI_SUSPEND	(APIC_IPI_INTS + 7)	/* Suspend CPU until restarted. */
#define	IPI_DYN_FIRST	(APIC_IPI_INTS + 8)
#define	IPI_DYN_LAST	(253)			/* IPIs allocated at runtime */

/*
 * IPI_STOP_HARD does not need to occupy a slot in the IPI vector space since
 * it is delivered using an NMI anyways.
 */
#define	IPI_NMI_FIRST	254
#define	IPI_TRACE	254			/* Interrupt for tracing. */
#define	IPI_STOP_HARD	255			/* Stop CPU with a NMI. */

/*
 * The spurious interrupt can share the priority class with the IPIs since
 * it is not a normal interrupt. (Does not use the APIC's interrupt fifo)
 */
#define	APIC_SPURIOUS_INT 255

#ifndef LOCORE

#define	APIC_IPI_DEST_SELF	-1
#define	APIC_IPI_DEST_ALL	-2
#define	APIC_IPI_DEST_OTHERS	-3

#define	APIC_BUS_UNKNOWN	-1
#define	APIC_BUS_ISA		0
#define	APIC_BUS_EISA		1
#define	APIC_BUS_PCI		2
#define	APIC_BUS_MAX		APIC_BUS_PCI

#define	IRQ_EXTINT		-1
#define	IRQ_NMI			-2
#define	IRQ_SMI			-3
#define	IRQ_DISABLED		-4

/*
 * An APIC enumerator is a pseudo bus driver that enumerates APIC's including
 * CPU's and I/O APIC's.
 */
struct apic_enumerator {
	const char *apic_name;
	int (*apic_probe)(void);
	int (*apic_probe_cpus)(void);
	int (*apic_setup_local)(void);
	int (*apic_setup_io)(void);
	SLIST_ENTRY(apic_enumerator) apic_next;
};

inthand_t
	IDTVEC(apic_isr1), IDTVEC(apic_isr2), IDTVEC(apic_isr3),
	IDTVEC(apic_isr4), IDTVEC(apic_isr5), IDTVEC(apic_isr6),
	IDTVEC(apic_isr7), IDTVEC(cmcint), IDTVEC(errorint),
	IDTVEC(spuriousint), IDTVEC(timerint),
	IDTVEC(apic_isr1_pti), IDTVEC(apic_isr2_pti), IDTVEC(apic_isr3_pti),
	IDTVEC(apic_isr4_pti), IDTVEC(apic_isr5_pti), IDTVEC(apic_isr6_pti),
	IDTVEC(apic_isr7_pti), IDTVEC(cmcint_pti), IDTVEC(errorint_pti),
	IDTVEC(spuriousint_pti), IDTVEC(timerint_pti);

extern vm_paddr_t lapic_paddr;
extern int *apic_cpuids;

void	apic_register_enumerator(struct apic_enumerator *enumerator);
void	*ioapic_create(vm_paddr_t addr, int32_t apic_id, int intbase);
int	ioapic_disable_pin(void *cookie, u_int pin);
int	ioapic_get_vector(void *cookie, u_int pin);
void	ioapic_register(void *cookie);
int	ioapic_remap_vector(void *cookie, u_int pin, int vector);
int	ioapic_set_bus(void *cookie, u_int pin, int bus_type);
int	ioapic_set_extint(void *cookie, u_int pin);
int	ioapic_set_nmi(void *cookie, u_int pin);
int	ioapic_set_polarity(void *cookie, u_int pin, enum intr_polarity pol);
int	ioapic_set_triggermode(void *cookie, u_int pin,
	    enum intr_trigger trigger);
int	ioapic_set_smi(void *cookie, u_int pin);

/*
 * Struct containing pointers to APIC functions whose
 * implementation is run time selectable.
 */
struct apic_ops {
	void	(*create)(u_int, int);
	void	(*init)(vm_paddr_t);
	void	(*xapic_mode)(void);
	bool	(*is_x2apic)(void);
	void	(*setup)(int);
	void	(*dump)(const char *);
	void	(*disable)(void);
	void	(*eoi)(void);
	int	(*id)(void);
	int	(*intr_pending)(u_int);
	void	(*set_logical_id)(u_int, u_int, u_int);
	u_int	(*cpuid)(u_int);

	/* Vectors */
	u_int	(*alloc_vector)(u_int, u_int);
	u_int	(*alloc_vectors)(u_int, u_int *, u_int, u_int);
	void	(*enable_vector)(u_int, u_int);
	void	(*disable_vector)(u_int, u_int);
	void	(*free_vector)(u_int, u_int, u_int);


	/* PMC */
	int	(*enable_pmc)(void);
	void	(*disable_pmc)(void);
	void	(*reenable_pmc)(void);

	/* CMC */
	void	(*enable_cmc)(void);

	/* AMD ELVT */
	int	(*enable_mca_elvt)(void);

	/* IPI */
	void	(*ipi_raw)(register_t, u_int);
	void	(*ipi_vectored)(u_int, int);
	int	(*ipi_wait)(int);
	int	(*ipi_alloc)(inthand_t *ipifunc);
	void	(*ipi_free)(int vector);

	/* LVT */
	int	(*set_lvt_mask)(u_int, u_int, u_char);
	int	(*set_lvt_mode)(u_int, u_int, u_int32_t);
	int	(*set_lvt_polarity)(u_int, u_int, enum intr_polarity);
	int	(*set_lvt_triggermode)(u_int, u_int, enum intr_trigger);
};

extern struct apic_ops apic_ops;

static inline void
lapic_create(u_int apic_id, int boot_cpu)
{

	apic_ops.create(apic_id, boot_cpu);
}

static inline void
lapic_init(vm_paddr_t addr)
{

	apic_ops.init(addr);
}

static inline void
lapic_xapic_mode(void)
{

	apic_ops.xapic_mode();
}

static inline bool
lapic_is_x2apic(void)
{

	return (apic_ops.is_x2apic());
}

static inline void
lapic_setup(int boot)
{

	apic_ops.setup(boot);
}

static inline void
lapic_dump(const char *str)
{

	apic_ops.dump(str);
}

static inline void
lapic_disable(void)
{

	apic_ops.disable();
}

static inline void
lapic_eoi(void)
{

	apic_ops.eoi();
}

static inline int
lapic_id(void)
{

	return (apic_ops.id());
}

static inline int
lapic_intr_pending(u_int vector)
{

	return (apic_ops.intr_pending(vector));
}

/* XXX: UNUSED */
static inline void
lapic_set_logical_id(u_int apic_id, u_int cluster, u_int cluster_id)
{

	apic_ops.set_logical_id(apic_id, cluster, cluster_id);
}

static inline u_int
apic_cpuid(u_int apic_id)
{

	return (apic_ops.cpuid(apic_id));
}

static inline u_int
apic_alloc_vector(u_int apic_id, u_int irq)
{

	return (apic_ops.alloc_vector(apic_id, irq));
}

static inline u_int
apic_alloc_vectors(u_int apic_id, u_int *irqs, u_int count, u_int align)
{

	return (apic_ops.alloc_vectors(apic_id, irqs, count, align));
}

static inline void
apic_enable_vector(u_int apic_id, u_int vector)
{

	apic_ops.enable_vector(apic_id, vector);
}

static inline void
apic_disable_vector(u_int apic_id, u_int vector)
{

	apic_ops.disable_vector(apic_id, vector);
}

static inline void
apic_free_vector(u_int apic_id, u_int vector, u_int irq)
{

	apic_ops.free_vector(apic_id, vector, irq);
}

static inline int
lapic_enable_pmc(void)
{

	return (apic_ops.enable_pmc());
}

static inline void
lapic_disable_pmc(void)
{

	apic_ops.disable_pmc();
}

static inline void
lapic_reenable_pmc(void)
{

	apic_ops.reenable_pmc();
}

static inline void
lapic_enable_cmc(void)
{

	apic_ops.enable_cmc();
}

static inline int
lapic_enable_mca_elvt(void)
{

	return (apic_ops.enable_mca_elvt());
}

static inline void
lapic_ipi_raw(register_t icrlo, u_int dest)
{

	apic_ops.ipi_raw(icrlo, dest);
}

static inline void
lapic_ipi_vectored(u_int vector, int dest)
{

	apic_ops.ipi_vectored(vector, dest);
}

static inline int
lapic_ipi_wait(int delay)
{

	return (apic_ops.ipi_wait(delay));
}

static inline int
lapic_ipi_alloc(inthand_t *ipifunc)
{

	return (apic_ops.ipi_alloc(ipifunc));
}

static inline void
lapic_ipi_free(int vector)
{

	return (apic_ops.ipi_free(vector));
}

static inline int
lapic_set_lvt_mask(u_int apic_id, u_int lvt, u_char masked)
{

	return (apic_ops.set_lvt_mask(apic_id, lvt, masked));
}

static inline int
lapic_set_lvt_mode(u_int apic_id, u_int lvt, u_int32_t mode)
{

	return (apic_ops.set_lvt_mode(apic_id, lvt, mode));
}

static inline int
lapic_set_lvt_polarity(u_int apic_id, u_int lvt, enum intr_polarity pol)
{

	return (apic_ops.set_lvt_polarity(apic_id, lvt, pol));
}

static inline int
lapic_set_lvt_triggermode(u_int apic_id, u_int lvt, enum intr_trigger trigger)
{

	return (apic_ops.set_lvt_triggermode(apic_id, lvt, trigger));
}

void	lapic_handle_cmc(void);
void	lapic_handle_error(void);
void	lapic_handle_intr(int vector, struct trapframe *frame);
void	lapic_handle_timer(struct trapframe *frame);

int	ioapic_get_rid(u_int apic_id, uint16_t *ridp);

extern int x2apic_mode;
extern int lapic_eoi_suppression;

#ifdef _SYS_SYSCTL_H_
SYSCTL_DECL(_hw_apic);
#endif

#endif /* !LOCORE */
#endif /* _X86_APICVAR_H_ */

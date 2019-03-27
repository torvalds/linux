/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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

#ifndef	_VMM_DEV_H_
#define	_VMM_DEV_H_

#ifdef _KERNEL
void	vmmdev_init(void);
int	vmmdev_cleanup(void);
#endif

struct vm_memmap {
	vm_paddr_t	gpa;
	int		segid;		/* memory segment */
	vm_ooffset_t	segoff;		/* offset into memory segment */
	size_t		len;		/* mmap length */
	int		prot;		/* RWX */
	int		flags;
};
#define	VM_MEMMAP_F_WIRED	0x01
#define	VM_MEMMAP_F_IOMMU	0x02

#define	VM_MEMSEG_NAME(m)	((m)->name[0] != '\0' ? (m)->name : NULL)
struct vm_memseg {
	int		segid;
	size_t		len;
	char		name[SPECNAMELEN + 1];
};

struct vm_register {
	int		cpuid;
	int		regnum;		/* enum vm_reg_name */
	uint64_t	regval;
};

struct vm_seg_desc {			/* data or code segment */
	int		cpuid;
	int		regnum;		/* enum vm_reg_name */
	struct seg_desc desc;
};

struct vm_register_set {
	int		cpuid;
	unsigned int	count;
	const int	*regnums;	/* enum vm_reg_name */
	uint64_t	*regvals;
};

struct vm_run {
	int		cpuid;
	struct vm_exit	vm_exit;
};

struct vm_exception {
	int		cpuid;
	int		vector;
	uint32_t	error_code;
	int		error_code_valid;
	int		restart_instruction;
};

struct vm_lapic_msi {
	uint64_t	msg;
	uint64_t	addr;
};

struct vm_lapic_irq {
	int		cpuid;
	int		vector;
};

struct vm_ioapic_irq {
	int		irq;
};

struct vm_isa_irq {
	int		atpic_irq;
	int		ioapic_irq;
};

struct vm_isa_irq_trigger {
	int		atpic_irq;
	enum vm_intr_trigger trigger;
};

struct vm_capability {
	int		cpuid;
	enum vm_cap_type captype;
	int		capval;
	int		allcpus;
};

struct vm_pptdev {
	int		bus;
	int		slot;
	int		func;
};

struct vm_pptdev_mmio {
	int		bus;
	int		slot;
	int		func;
	vm_paddr_t	gpa;
	vm_paddr_t	hpa;
	size_t		len;
};

struct vm_pptdev_msi {
	int		vcpu;
	int		bus;
	int		slot;
	int		func;
	int		numvec;		/* 0 means disabled */
	uint64_t	msg;
	uint64_t	addr;
};

struct vm_pptdev_msix {
	int		vcpu;
	int		bus;
	int		slot;
	int		func;
	int		idx;
	uint64_t	msg;
	uint32_t	vector_control;
	uint64_t	addr;
};

struct vm_nmi {
	int		cpuid;
};

#define	MAX_VM_STATS	64
struct vm_stats {
	int		cpuid;				/* in */
	int		num_entries;			/* out */
	struct timeval	tv;
	uint64_t	statbuf[MAX_VM_STATS];
};

struct vm_stat_desc {
	int		index;				/* in */
	char		desc[128];			/* out */
};

struct vm_x2apic {
	int			cpuid;
	enum x2apic_state	state;
};

struct vm_gpa_pte {
	uint64_t	gpa;				/* in */
	uint64_t	pte[4];				/* out */
	int		ptenum;
};

struct vm_hpet_cap {
	uint32_t	capabilities;	/* lower 32 bits of HPET capabilities */
};

struct vm_suspend {
	enum vm_suspend_how how;
};

struct vm_gla2gpa {
	int		vcpuid;		/* inputs */
	int 		prot;		/* PROT_READ or PROT_WRITE */
	uint64_t	gla;
	struct vm_guest_paging paging;
	int		fault;		/* outputs */
	uint64_t	gpa;
};

struct vm_activate_cpu {
	int		vcpuid;
};

struct vm_cpuset {
	int		which;
	int		cpusetsize;
	cpuset_t	*cpus;
};
#define	VM_ACTIVE_CPUS		0
#define	VM_SUSPENDED_CPUS	1
#define	VM_DEBUG_CPUS		2

struct vm_intinfo {
	int		vcpuid;
	uint64_t	info1;
	uint64_t	info2;
};

struct vm_rtc_time {
	time_t		secs;
};

struct vm_rtc_data {
	int		offset;
	uint8_t		value;
};

struct vm_cpu_topology {
	uint16_t	sockets;
	uint16_t	cores;
	uint16_t	threads;
	uint16_t	maxcpus;
};

enum {
	/* general routines */
	IOCNUM_ABIVERS = 0,
	IOCNUM_RUN = 1,
	IOCNUM_SET_CAPABILITY = 2,
	IOCNUM_GET_CAPABILITY = 3,
	IOCNUM_SUSPEND = 4,
	IOCNUM_REINIT = 5,

	/* memory apis */
	IOCNUM_MAP_MEMORY = 10,			/* deprecated */
	IOCNUM_GET_MEMORY_SEG = 11,		/* deprecated */
	IOCNUM_GET_GPA_PMAP = 12,
	IOCNUM_GLA2GPA = 13,
	IOCNUM_ALLOC_MEMSEG = 14,
	IOCNUM_GET_MEMSEG = 15,
	IOCNUM_MMAP_MEMSEG = 16,
	IOCNUM_MMAP_GETNEXT = 17,
	IOCNUM_GLA2GPA_NOFAULT = 18,

	/* register/state accessors */
	IOCNUM_SET_REGISTER = 20,
	IOCNUM_GET_REGISTER = 21,
	IOCNUM_SET_SEGMENT_DESCRIPTOR = 22,
	IOCNUM_GET_SEGMENT_DESCRIPTOR = 23,
	IOCNUM_SET_REGISTER_SET = 24,
	IOCNUM_GET_REGISTER_SET = 25,

	/* interrupt injection */
	IOCNUM_GET_INTINFO = 28,
	IOCNUM_SET_INTINFO = 29,
	IOCNUM_INJECT_EXCEPTION = 30,
	IOCNUM_LAPIC_IRQ = 31,
	IOCNUM_INJECT_NMI = 32,
	IOCNUM_IOAPIC_ASSERT_IRQ = 33,
	IOCNUM_IOAPIC_DEASSERT_IRQ = 34,
	IOCNUM_IOAPIC_PULSE_IRQ = 35,
	IOCNUM_LAPIC_MSI = 36,
	IOCNUM_LAPIC_LOCAL_IRQ = 37,
	IOCNUM_IOAPIC_PINCOUNT = 38,
	IOCNUM_RESTART_INSTRUCTION = 39,

	/* PCI pass-thru */
	IOCNUM_BIND_PPTDEV = 40,
	IOCNUM_UNBIND_PPTDEV = 41,
	IOCNUM_MAP_PPTDEV_MMIO = 42,
	IOCNUM_PPTDEV_MSI = 43,
	IOCNUM_PPTDEV_MSIX = 44,

	/* statistics */
	IOCNUM_VM_STATS = 50, 
	IOCNUM_VM_STAT_DESC = 51,

	/* kernel device state */
	IOCNUM_SET_X2APIC_STATE = 60,
	IOCNUM_GET_X2APIC_STATE = 61,
	IOCNUM_GET_HPET_CAPABILITIES = 62,

	/* CPU Topology */
	IOCNUM_SET_TOPOLOGY = 63,
	IOCNUM_GET_TOPOLOGY = 64,

	/* legacy interrupt injection */
	IOCNUM_ISA_ASSERT_IRQ = 80,
	IOCNUM_ISA_DEASSERT_IRQ = 81,
	IOCNUM_ISA_PULSE_IRQ = 82,
	IOCNUM_ISA_SET_IRQ_TRIGGER = 83,

	/* vm_cpuset */
	IOCNUM_ACTIVATE_CPU = 90,
	IOCNUM_GET_CPUSET = 91,
	IOCNUM_SUSPEND_CPU = 92,
	IOCNUM_RESUME_CPU = 93,

	/* RTC */
	IOCNUM_RTC_READ = 100,
	IOCNUM_RTC_WRITE = 101,
	IOCNUM_RTC_SETTIME = 102,
	IOCNUM_RTC_GETTIME = 103,
};

#define	VM_RUN		\
	_IOWR('v', IOCNUM_RUN, struct vm_run)
#define	VM_SUSPEND	\
	_IOW('v', IOCNUM_SUSPEND, struct vm_suspend)
#define	VM_REINIT	\
	_IO('v', IOCNUM_REINIT)
#define	VM_ALLOC_MEMSEG	\
	_IOW('v', IOCNUM_ALLOC_MEMSEG, struct vm_memseg)
#define	VM_GET_MEMSEG	\
	_IOWR('v', IOCNUM_GET_MEMSEG, struct vm_memseg)
#define	VM_MMAP_MEMSEG	\
	_IOW('v', IOCNUM_MMAP_MEMSEG, struct vm_memmap)
#define	VM_MMAP_GETNEXT	\
	_IOWR('v', IOCNUM_MMAP_GETNEXT, struct vm_memmap)
#define	VM_SET_REGISTER \
	_IOW('v', IOCNUM_SET_REGISTER, struct vm_register)
#define	VM_GET_REGISTER \
	_IOWR('v', IOCNUM_GET_REGISTER, struct vm_register)
#define	VM_SET_SEGMENT_DESCRIPTOR \
	_IOW('v', IOCNUM_SET_SEGMENT_DESCRIPTOR, struct vm_seg_desc)
#define	VM_GET_SEGMENT_DESCRIPTOR \
	_IOWR('v', IOCNUM_GET_SEGMENT_DESCRIPTOR, struct vm_seg_desc)
#define	VM_SET_REGISTER_SET \
	_IOW('v', IOCNUM_SET_REGISTER_SET, struct vm_register_set)
#define	VM_GET_REGISTER_SET \
	_IOWR('v', IOCNUM_GET_REGISTER_SET, struct vm_register_set)
#define	VM_INJECT_EXCEPTION	\
	_IOW('v', IOCNUM_INJECT_EXCEPTION, struct vm_exception)
#define	VM_LAPIC_IRQ 		\
	_IOW('v', IOCNUM_LAPIC_IRQ, struct vm_lapic_irq)
#define	VM_LAPIC_LOCAL_IRQ 	\
	_IOW('v', IOCNUM_LAPIC_LOCAL_IRQ, struct vm_lapic_irq)
#define	VM_LAPIC_MSI		\
	_IOW('v', IOCNUM_LAPIC_MSI, struct vm_lapic_msi)
#define	VM_IOAPIC_ASSERT_IRQ	\
	_IOW('v', IOCNUM_IOAPIC_ASSERT_IRQ, struct vm_ioapic_irq)
#define	VM_IOAPIC_DEASSERT_IRQ	\
	_IOW('v', IOCNUM_IOAPIC_DEASSERT_IRQ, struct vm_ioapic_irq)
#define	VM_IOAPIC_PULSE_IRQ	\
	_IOW('v', IOCNUM_IOAPIC_PULSE_IRQ, struct vm_ioapic_irq)
#define	VM_IOAPIC_PINCOUNT	\
	_IOR('v', IOCNUM_IOAPIC_PINCOUNT, int)
#define	VM_ISA_ASSERT_IRQ	\
	_IOW('v', IOCNUM_ISA_ASSERT_IRQ, struct vm_isa_irq)
#define	VM_ISA_DEASSERT_IRQ	\
	_IOW('v', IOCNUM_ISA_DEASSERT_IRQ, struct vm_isa_irq)
#define	VM_ISA_PULSE_IRQ	\
	_IOW('v', IOCNUM_ISA_PULSE_IRQ, struct vm_isa_irq)
#define	VM_ISA_SET_IRQ_TRIGGER	\
	_IOW('v', IOCNUM_ISA_SET_IRQ_TRIGGER, struct vm_isa_irq_trigger)
#define	VM_SET_CAPABILITY \
	_IOW('v', IOCNUM_SET_CAPABILITY, struct vm_capability)
#define	VM_GET_CAPABILITY \
	_IOWR('v', IOCNUM_GET_CAPABILITY, struct vm_capability)
#define	VM_BIND_PPTDEV \
	_IOW('v', IOCNUM_BIND_PPTDEV, struct vm_pptdev)
#define	VM_UNBIND_PPTDEV \
	_IOW('v', IOCNUM_UNBIND_PPTDEV, struct vm_pptdev)
#define	VM_MAP_PPTDEV_MMIO \
	_IOW('v', IOCNUM_MAP_PPTDEV_MMIO, struct vm_pptdev_mmio)
#define	VM_PPTDEV_MSI \
	_IOW('v', IOCNUM_PPTDEV_MSI, struct vm_pptdev_msi)
#define	VM_PPTDEV_MSIX \
	_IOW('v', IOCNUM_PPTDEV_MSIX, struct vm_pptdev_msix)
#define VM_INJECT_NMI \
	_IOW('v', IOCNUM_INJECT_NMI, struct vm_nmi)
#define	VM_STATS \
	_IOWR('v', IOCNUM_VM_STATS, struct vm_stats)
#define	VM_STAT_DESC \
	_IOWR('v', IOCNUM_VM_STAT_DESC, struct vm_stat_desc)
#define	VM_SET_X2APIC_STATE \
	_IOW('v', IOCNUM_SET_X2APIC_STATE, struct vm_x2apic)
#define	VM_GET_X2APIC_STATE \
	_IOWR('v', IOCNUM_GET_X2APIC_STATE, struct vm_x2apic)
#define	VM_GET_HPET_CAPABILITIES \
	_IOR('v', IOCNUM_GET_HPET_CAPABILITIES, struct vm_hpet_cap)
#define VM_SET_TOPOLOGY \
	_IOW('v', IOCNUM_SET_TOPOLOGY, struct vm_cpu_topology)
#define VM_GET_TOPOLOGY \
	_IOR('v', IOCNUM_GET_TOPOLOGY, struct vm_cpu_topology)
#define	VM_GET_GPA_PMAP \
	_IOWR('v', IOCNUM_GET_GPA_PMAP, struct vm_gpa_pte)
#define	VM_GLA2GPA	\
	_IOWR('v', IOCNUM_GLA2GPA, struct vm_gla2gpa)
#define	VM_GLA2GPA_NOFAULT \
	_IOWR('v', IOCNUM_GLA2GPA_NOFAULT, struct vm_gla2gpa)
#define	VM_ACTIVATE_CPU	\
	_IOW('v', IOCNUM_ACTIVATE_CPU, struct vm_activate_cpu)
#define	VM_GET_CPUS	\
	_IOW('v', IOCNUM_GET_CPUSET, struct vm_cpuset)
#define	VM_SUSPEND_CPU \
	_IOW('v', IOCNUM_SUSPEND_CPU, struct vm_activate_cpu)
#define	VM_RESUME_CPU \
	_IOW('v', IOCNUM_RESUME_CPU, struct vm_activate_cpu)
#define	VM_SET_INTINFO	\
	_IOW('v', IOCNUM_SET_INTINFO, struct vm_intinfo)
#define	VM_GET_INTINFO	\
	_IOWR('v', IOCNUM_GET_INTINFO, struct vm_intinfo)
#define VM_RTC_WRITE \
	_IOW('v', IOCNUM_RTC_WRITE, struct vm_rtc_data)
#define VM_RTC_READ \
	_IOWR('v', IOCNUM_RTC_READ, struct vm_rtc_data)
#define VM_RTC_SETTIME	\
	_IOW('v', IOCNUM_RTC_SETTIME, struct vm_rtc_time)
#define VM_RTC_GETTIME	\
	_IOR('v', IOCNUM_RTC_GETTIME, struct vm_rtc_time)
#define	VM_RESTART_INSTRUCTION \
	_IOW('v', IOCNUM_RESTART_INSTRUCTION, int)
#endif

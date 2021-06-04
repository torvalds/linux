/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/x86_64/apic.h
 *
 * Copyright (C) 2021, Google LLC.
 */

#ifndef SELFTEST_KVM_APIC_H
#define SELFTEST_KVM_APIC_H

#define APIC_DEFAULT_GPA		0xfee00000ULL

/* APIC base address MSR and fields */
#define MSR_IA32_APICBASE		0x0000001b
#define MSR_IA32_APICBASE_BSP		(1<<8)
#define MSR_IA32_APICBASE_EXTD		(1<<10)
#define MSR_IA32_APICBASE_ENABLE	(1<<11)
#define MSR_IA32_APICBASE_BASE		(0xfffff<<12)
#define		GET_APIC_BASE(x)	(((x) >> 12) << 12)

#define APIC_BASE_MSR	0x800
#define X2APIC_ENABLE	(1UL << 10)
#define	APIC_ID		0x20
#define	APIC_LVR	0x30
#define		GET_APIC_ID_FIELD(x)	(((x) >> 24) & 0xFF)
#define	APIC_TASKPRI	0x80
#define	APIC_PROCPRI	0xA0
#define	APIC_EOI	0xB0
#define	APIC_SPIV	0xF0
#define		APIC_SPIV_FOCUS_DISABLED	(1 << 9)
#define		APIC_SPIV_APIC_ENABLED		(1 << 8)
#define	APIC_ICR	0x300
#define		APIC_DEST_SELF		0x40000
#define		APIC_DEST_ALLINC	0x80000
#define		APIC_DEST_ALLBUT	0xC0000
#define		APIC_ICR_RR_MASK	0x30000
#define		APIC_ICR_RR_INVALID	0x00000
#define		APIC_ICR_RR_INPROG	0x10000
#define		APIC_ICR_RR_VALID	0x20000
#define		APIC_INT_LEVELTRIG	0x08000
#define		APIC_INT_ASSERT		0x04000
#define		APIC_ICR_BUSY		0x01000
#define		APIC_DEST_LOGICAL	0x00800
#define		APIC_DEST_PHYSICAL	0x00000
#define		APIC_DM_FIXED		0x00000
#define		APIC_DM_FIXED_MASK	0x00700
#define		APIC_DM_LOWEST		0x00100
#define		APIC_DM_SMI		0x00200
#define		APIC_DM_REMRD		0x00300
#define		APIC_DM_NMI		0x00400
#define		APIC_DM_INIT		0x00500
#define		APIC_DM_STARTUP		0x00600
#define		APIC_DM_EXTINT		0x00700
#define		APIC_VECTOR_MASK	0x000FF
#define	APIC_ICR2	0x310
#define		SET_APIC_DEST_FIELD(x)	((x) << 24)

#endif /* SELFTEST_KVM_APIC_H */

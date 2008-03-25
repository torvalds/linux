/*
 * asm-s390/kvm_host.h - definition for kernel virtual machines on s390
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 */


#ifndef ASM_KVM_HOST_H
#define ASM_KVM_HOST_H
#include <linux/kvm_host.h>
#include <asm/debug.h>

#define KVM_MAX_VCPUS 64
#define KVM_MEMORY_SLOTS 32
/* memory slots that does not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS 4

struct kvm_guest_debug {
};

struct sca_entry {
	atomic_t scn;
	__u64	reserved;
	__u64	sda;
	__u64	reserved2[2];
} __attribute__((packed));


struct sca_block {
	__u64	ipte_control;
	__u64	reserved[5];
	__u64	mcn;
	__u64	reserved2;
	struct sca_entry cpu[64];
} __attribute__((packed));

#define KVM_PAGES_PER_HPAGE 256

#define CPUSTAT_HOST       0x80000000
#define CPUSTAT_WAIT       0x10000000
#define CPUSTAT_ECALL_PEND 0x08000000
#define CPUSTAT_STOP_INT   0x04000000
#define CPUSTAT_IO_INT     0x02000000
#define CPUSTAT_EXT_INT    0x01000000
#define CPUSTAT_RUNNING    0x00800000
#define CPUSTAT_RETAINED   0x00400000
#define CPUSTAT_TIMING_SUB 0x00020000
#define CPUSTAT_SIE_SUB    0x00010000
#define CPUSTAT_RRF        0x00008000
#define CPUSTAT_SLSV       0x00004000
#define CPUSTAT_SLSR       0x00002000
#define CPUSTAT_ZARCH      0x00000800
#define CPUSTAT_MCDS       0x00000100
#define CPUSTAT_SM         0x00000080
#define CPUSTAT_G          0x00000008
#define CPUSTAT_J          0x00000002
#define CPUSTAT_P          0x00000001

struct sie_block {
	atomic_t cpuflags;		/* 0x0000 */
	__u32	prefix;			/* 0x0004 */
	__u8	reserved8[32];		/* 0x0008 */
	__u64	cputm;			/* 0x0028 */
	__u64	ckc;			/* 0x0030 */
	__u64	epoch;			/* 0x0038 */
	__u8	reserved40[4];		/* 0x0040 */
	__u16   lctl;			/* 0x0044 */
	__s16	icpua;			/* 0x0046 */
	__u32	ictl;			/* 0x0048 */
	__u32	eca;			/* 0x004c */
	__u8	icptcode;		/* 0x0050 */
	__u8	reserved51;		/* 0x0051 */
	__u16	ihcpu;			/* 0x0052 */
	__u8	reserved54[2];		/* 0x0054 */
	__u16	ipa;			/* 0x0056 */
	__u32	ipb;			/* 0x0058 */
	__u32	scaoh;			/* 0x005c */
	__u8	reserved60;		/* 0x0060 */
	__u8	ecb;			/* 0x0061 */
	__u8	reserved62[2];		/* 0x0062 */
	__u32	scaol;			/* 0x0064 */
	__u8	reserved68[4];		/* 0x0068 */
	__u32	todpr;			/* 0x006c */
	__u8	reserved70[16];		/* 0x0070 */
	__u64	gmsor;			/* 0x0080 */
	__u64	gmslm;			/* 0x0088 */
	psw_t	gpsw;			/* 0x0090 */
	__u64	gg14;			/* 0x00a0 */
	__u64	gg15;			/* 0x00a8 */
	__u8	reservedb0[80];		/* 0x00b0 */
	__u64	gcr[16];		/* 0x0100 */
	__u64	gbea;			/* 0x0180 */
	__u8	reserved188[120];	/* 0x0188 */
} __attribute__((packed));

struct kvm_vcpu_stat {
	u32 exit_userspace;
};

struct kvm_vcpu_arch {
	struct sie_block *sie_block;
	unsigned long	  guest_gprs[16];
	s390_fp_regs      host_fpregs;
	unsigned int      host_acrs[NUM_ACRS];
	s390_fp_regs      guest_fpregs;
	unsigned int      guest_acrs[NUM_ACRS];
};

struct kvm_vm_stat {
	u32 remote_tlb_flush;
};

struct kvm_arch{
	unsigned long guest_origin;
	unsigned long guest_memsize;
	struct sca_block *sca;
	debug_info_t *dbf;
};

extern int sie64a(struct sie_block *, __u64 *);
#endif

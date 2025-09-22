/*	$OpenBSD: mpbiosreg.h,v 1.6 2023/04/10 04:21:20 jsg Exp $	*/
/* $NetBSD: mpbiosreg.h,v 1.1.2.3 2000/02/29 13:17:51 sommerfeld Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_MPBIOSREG_H_
#define _MACHINE_MPBIOSREG_H_

#define BIOS_BASE		(0xf0000)
#define BIOS_SIZE		(0x10000)
#define BIOS_COUNT		(BIOS_SIZE)

/*
 * Multiprocessor config table entry types.
 */

#define MPS_MCT_CPU	0
#define MPS_MCT_BUS	1
#define MPS_MCT_IOAPIC	2
#define MPS_MCT_IOINT	3
#define MPS_MCT_LINT	4

#define MPS_MCT_NTYPES	5

/*
 * Interrupt types
 */

#define MPS_INTTYPE_INT		0
#define MPS_INTTYPE_NMI		1
#define MPS_INTTYPE_SMI		2
#define MPS_INTTYPE_ExtINT	3

#define MPS_INTPO_DEF		0
#define MPS_INTPO_ACTHI		1
#define MPS_INTPO_ACTLO		3
#define MPS_INTPO_SHIFT		0
#define MPS_INTPO_MASK		3

#define MPS_INTTR_DEF		0
#define MPS_INTTR_EDGE		1
#define MPS_INTTR_LEVEL		3
#define MPS_INTTR_SHIFT		2
#define MPS_INTTR_MASK		3

#define MPS_INT(p,t) \
    ((((p) & MPS_INTPO_MASK) << MPS_INTPO_SHIFT) | \
     (((t) & MPS_INTTR_MASK) << MPS_INTTR_SHIFT))

/* MP Floating Pointer Structure */
struct mpbios_fps {
	u_int32_t	signature;
/* string defined by the Intel MP Spec as identifying the MP table */
#define MP_FP_SIG		0x5f504d5f	/* _MP_ */
	
	u_int32_t 	pap;
	u_int8_t  	length;
	u_int8_t  	spec_rev;
	u_int8_t  	checksum;
	u_int8_t  	mpfb1;	/* system configuration */
	u_int8_t  	mpfb2;	/* flags */
#define MPFPS_FLAG_IMCR		0x80	/* IMCR present */
	u_int8_t  	mpfb3;	/* unused */
	u_int8_t  	mpfb4;	/* unused */
	u_int8_t  	mpfb5;	/* unused */
};

/* MP Configuration Table Header */
struct mpbios_cth {
	u_int32_t	signature;
#define MP_CT_SIG		0x504d4350 	/* PCMP */
	
	u_int16_t 	base_len;
	u_int8_t  	spec_rev;
	u_int8_t  	checksum;
	u_int8_t  	oem_id[8];
	u_int8_t  	product_id[12];
	u_int32_t	oem_table_pointer;
	u_int16_t 	oem_table_size;
	u_int16_t 	entry_count;
	u_int32_t	apic_address;
	u_int16_t	ext_len;
	u_int8_t  	ext_cksum;
	u_int8_t  	reserved;
};

struct mpbios_proc {
	u_int8_t  type;
	u_int8_t  apic_id;
	u_int8_t  apic_version;
	u_int8_t  cpu_flags;
#define PROCENTRY_FLAG_EN	0x01
#define PROCENTRY_FLAG_BP	0x02
	u_long  cpu_signature;
	u_long  feature_flags;
	u_long  reserved1;
	u_long  reserved2;
};

struct mpbios_bus {
	u_int8_t  type;
	u_int8_t  bus_id;
	char    bus_type[6];
};

struct mpbios_ioapic {
	u_int8_t  type;
	u_int8_t  apic_id;
	u_int8_t  apic_version;
	u_int8_t  apic_flags;
#define IOAPICENTRY_FLAG_EN	0x01
	void   *apic_address;
};

struct mpbios_int {
	u_int8_t  type;
	u_int8_t  int_type;
	u_int16_t int_flags;
	u_int8_t  src_bus_id;
	u_int8_t  src_bus_irq;
	u_int8_t  dst_apic_id;
#define MPS_ALL_APICS	0xff
	u_int8_t  dst_apic_int;
};

#endif /* !_MACHINE_MPBIOSREG_H_ */

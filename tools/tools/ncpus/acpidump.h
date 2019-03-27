/*-
 * Copyright (c) 1999 Doug Rabson
 * Copyright (c) 2000 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
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
 *	$FreeBSD$
 */

#ifndef _ACPIDUMP_H_
#define _ACPIDUMP_H_

/* Root System Description Pointer */
struct ACPIrsdp {
	u_char		signature[8];
	u_char		sum;
	u_char		oem[6];
	u_char		revision;
	u_int32_t	rsdt_addr;
	u_int32_t	length;
	u_int64_t	xsdt_addr;
	u_char		xsum;
	u_char		_reserved_[3];
} __packed;

/* System Description Table */
struct ACPIsdt {
	u_char		signature[4];
	u_int32_t	len;
	u_char		rev;
	u_char		check;
	u_char		oemid[6];
	u_char		oemtblid[8];
	u_int32_t	oemrev;
	u_char		creator[4];
	u_int32_t	crerev;
#define SIZEOF_SDT_HDR 36	/* struct size except body */
	u_int32_t	body[1];/* This member should be casted */
} __packed;

struct MADT_local_apic {
	u_char		cpu_id;
	u_char		apic_id;
	u_int32_t	flags;
#define	ACPI_MADT_APIC_LOCAL_FLAG_ENABLED	1
} __packed;

struct MADT_io_apic {
	u_char		apic_id;
	u_char		reserved;
	u_int32_t	apic_addr;
	u_int32_t	int_base;
} __packed;

struct MADT_int_override {
	u_char		bus;
	u_char		source;
	u_int32_t	intr;
	u_int16_t	mps_flags;
#define	MPS_INT_FLAG_POLARITY_MASK	0x3
#define	MPS_INT_FLAG_POLARITY_CONFORM	0x0
#define	MPS_INT_FLAG_POLARITY_HIGH	0x1
#define	MPS_INT_FLAG_POLARITY_LOW	0x3
#define	MPS_INT_FLAG_TRIGGER_MASK	0xc
#define	MPS_INT_FLAG_TRIGGER_CONFORM	0x0
#define	MPS_INT_FLAG_TRIGGER_EDGE	0x4
#define	MPS_INT_FLAG_TRIGGER_LEVEL	0xc
} __packed;

struct MADT_nmi {
	u_int16_t	mps_flags;
	u_int32_t	intr;
} __packed;

struct MADT_local_nmi {
	u_char		cpu_id;
	u_int16_t	mps_flags;
	u_char		lintpin;
} __packed;

struct MADT_local_apic_override {
	u_char		reserved[2];
	u_int64_t	apic_addr;
} __packed;

struct MADT_io_sapic {
	u_char		apic_id;
	u_char		reserved;
	u_int32_t	int_base;
	u_int64_t	apic_addr;
} __packed;

struct MADT_local_sapic {
	u_char		cpu_id;
	u_char		apic_id;
	u_char		apic_eid;
	u_char		reserved[3];
	u_int32_t	flags;
} __packed;

struct MADT_int_src {
	u_int16_t	mps_flags;
	u_char		type;
#define	ACPI_MADT_APIC_INT_SOURCE_PMI	1
#define	ACPI_MADT_APIC_INT_SOURCE_INIT	2
#define	ACPI_MADT_APIC_INT_SOURCE_CPEI	3	/* Corrected Platform Error */
	u_char		cpu_id;
	u_char		cpu_eid;
	u_char		sapic_vector;
	u_int32_t	intr;
	u_char		reserved[4];
} __packed;

struct MADT_APIC {
	u_char		type;
#define	ACPI_MADT_APIC_TYPE_LOCAL_APIC	0
#define	ACPI_MADT_APIC_TYPE_IO_APIC	1
#define	ACPI_MADT_APIC_TYPE_INT_OVERRIDE 2
#define	ACPI_MADT_APIC_TYPE_NMI		3
#define	ACPI_MADT_APIC_TYPE_LOCAL_NMI	4
#define	ACPI_MADT_APIC_TYPE_LOCAL_OVERRIDE 5
#define	ACPI_MADT_APIC_TYPE_IO_SAPIC	6
#define	ACPI_MADT_APIC_TYPE_LOCAL_SAPIC	7
#define	ACPI_MADT_APIC_TYPE_INT_SRC	8
	u_char		len;
	union {
		struct MADT_local_apic local_apic;
		struct MADT_io_apic io_apic;
		struct MADT_int_override int_override;
		struct MADT_nmi nmi;
		struct MADT_local_nmi local_nmi;
		struct MADT_local_apic_override local_apic_override;
		struct MADT_io_sapic io_sapic;
		struct MADT_local_sapic local_sapic;
		struct MADT_int_src int_src;
	} body;
} __packed;

struct MADTbody {
	u_int32_t	lapic_addr;
	u_int32_t	flags;
#define	ACPI_APIC_FLAG_PCAT_COMPAT 1	/* System has dual-8259 setup. */
	u_char		body[1];
} __packed;

/*
 * Addresses to scan on ia32 for the RSD PTR.  According to section 5.2.2
 * of the ACPI spec, we only consider two regions for the base address:
 * 1. EBDA (1 KB area addressed to by 16 bit pointer at 0x40E)
 * 2. High memory (0xE0000 - 0xFFFFF)
 */
#define RSDP_EBDA_PTR	0x40E
#define RSDP_EBDA_SIZE	0x400
#define RSDP_HI_START	0xE0000
#define RSDP_HI_SIZE	0x20000

#endif	/* !_ACPIDUMP_H_ */

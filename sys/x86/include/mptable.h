/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#ifndef __MACHINE_MPTABLE_H__
#define	__MACHINE_MPTABLE_H__

enum busTypes {
    NOBUS = 0,
    CBUS = 1,
    CBUSII = 2,
    EISA = 3,
    ISA = 6,
    MCA = 9,
    PCI = 13,
    XPRESS = 18,
    MAX_BUSTYPE = 18,
    UNKNOWN_BUSTYPE = 0xff
};

/* MP Floating Pointer Structure */
typedef struct MPFPS {
	uint8_t	signature[4];
	uint32_t pap;
	uint8_t	length;
	uint8_t	spec_rev;
	uint8_t	checksum;
	uint8_t	config_type;
	uint8_t	mpfb2;
	uint8_t	mpfb3;
	uint8_t	mpfb4;
	uint8_t	mpfb5;
} __packed *mpfps_t;

#define	MPFB2_IMCR_PRESENT	0x80
#define	MPFB2_MUL_CLK_SRCS	0x40

/* MP Configuration Table Header */
typedef struct MPCTH {
	uint8_t	signature[4];
	uint16_t base_table_length;
	uint8_t	spec_rev;
	uint8_t	checksum;
	uint8_t	oem_id[8];
	uint8_t	product_id[12];
	uint32_t oem_table_pointer;
	uint16_t oem_table_size;
	uint16_t entry_count;
	uint32_t apic_address;
	uint16_t extended_table_length;
	uint8_t	extended_table_checksum;
	uint8_t	reserved;
} __packed *mpcth_t;

/* Base table entries */

#define	MPCT_ENTRY_PROCESSOR	0
#define	MPCT_ENTRY_BUS		1
#define	MPCT_ENTRY_IOAPIC	2
#define	MPCT_ENTRY_INT		3
#define	MPCT_ENTRY_LOCAL_INT	4

typedef struct PROCENTRY {
	uint8_t	type;
	uint8_t	apic_id;
	uint8_t	apic_version;
	uint8_t	cpu_flags;
	uint32_t cpu_signature;
	uint32_t feature_flags;
	uint32_t reserved1;
	uint32_t reserved2;
} __packed *proc_entry_ptr;

#define PROCENTRY_FLAG_EN	0x01
#define PROCENTRY_FLAG_BP	0x02

typedef struct BUSENTRY {
	uint8_t	type;
	uint8_t	bus_id;
	uint8_t	bus_type[6];
} __packed *bus_entry_ptr;

typedef struct IOAPICENTRY {
	uint8_t	type;
	uint8_t	apic_id;
	uint8_t	apic_version;
	uint8_t	apic_flags;
	uint32_t apic_address;
} __packed *io_apic_entry_ptr;

#define IOAPICENTRY_FLAG_EN	0x01

typedef struct INTENTRY {
	uint8_t	type;
	uint8_t	int_type;
	uint16_t int_flags;
	uint8_t	src_bus_id;
	uint8_t	src_bus_irq;
	uint8_t	dst_apic_id;
	uint8_t	dst_apic_int;
} __packed *int_entry_ptr;

#define	INTENTRY_TYPE_INT  	0
#define	INTENTRY_TYPE_NMI	1
#define	INTENTRY_TYPE_SMI	2
#define	INTENTRY_TYPE_EXTINT	3

#define	INTENTRY_FLAGS_POLARITY			0x3
#define	INTENTRY_FLAGS_POLARITY_CONFORM		0x0
#define	INTENTRY_FLAGS_POLARITY_ACTIVEHI	0x1
#define	INTENTRY_FLAGS_POLARITY_ACTIVELO	0x3
#define	INTENTRY_FLAGS_TRIGGER			0xc
#define	INTENTRY_FLAGS_TRIGGER_CONFORM		0x0
#define	INTENTRY_FLAGS_TRIGGER_EDGE		0x4
#define	INTENTRY_FLAGS_TRIGGER_LEVEL		0xc

/* Extended table entries */

typedef	struct EXTENTRY {
	uint8_t	type;
	uint8_t	length;
} __packed *ext_entry_ptr;

#define	MPCT_EXTENTRY_SAS	0x80
#define	MPCT_EXTENTRY_BHD	0x81
#define	MPCT_EXTENTRY_CBASM	0x82

typedef struct SASENTRY {
	uint8_t	type;
	uint8_t	length;
	uint8_t	bus_id;
	uint8_t	address_type;
	uint64_t address_base;
	uint64_t address_length;
} __packed *sas_entry_ptr;

#define	SASENTRY_TYPE_IO	0
#define	SASENTRY_TYPE_MEMORY	1
#define	SASENTRY_TYPE_PREFETCH	2

typedef struct BHDENTRY {
	uint8_t	type;
	uint8_t	length;
	uint8_t	bus_id;
	uint8_t	bus_info;
	uint8_t	parent_bus;
	uint8_t	reserved[3];
} __packed *bhd_entry_ptr;

#define	BHDENTRY_INFO_SUBTRACTIVE_DECODE	0x1

typedef struct CBASMENTRY {
	uint8_t	type;
	uint8_t	length;
	uint8_t	bus_id;
	uint8_t	address_mod;
	uint32_t predefined_range;
} __packed *cbasm_entry_ptr;

#define	CBASMENTRY_ADDRESS_MOD_ADD		0x0
#define	CBASMENTRY_ADDRESS_MOD_SUBTRACT		0x1

#define	CBASMENTRY_RANGE_ISA_IO		0
#define	CBASMENTRY_RANGE_VGA_IO		1

#ifdef _KERNEL
struct mptable_hostb_softc {
#ifdef NEW_PCIB
	struct pcib_host_resources sc_host_res;
	int		sc_decodes_vga_io;
	int		sc_decodes_isa_io;
#endif
};

#ifdef NEW_PCIB
void	mptable_pci_host_res_init(device_t pcib);
#endif
int	mptable_pci_probe_table(int bus);
int	mptable_pci_route_interrupt(device_t pcib, device_t dev, int pin);
#endif
#endif /* !__MACHINE_MPTABLE_H__ */

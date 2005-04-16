
/******************************************************************************
 *
 * Module Name: amlresrc.h - AML resource descriptors
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2005, R. Byron Moore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */


#ifndef __AMLRESRC_H
#define __AMLRESRC_H


#define ASL_RESNAME_ADDRESS                     "_ADR"
#define ASL_RESNAME_ALIGNMENT                   "_ALN"
#define ASL_RESNAME_ADDRESSSPACE                "_ASI"
#define ASL_RESNAME_ACCESSSIZE                  "_ASZ"
#define ASL_RESNAME_TYPESPECIFICATTRIBUTES      "_ATT"
#define ASL_RESNAME_BASEADDRESS                 "_BAS"
#define ASL_RESNAME_BUSMASTER                   "_BM_"  /* Master(1), Slave(0) */
#define ASL_RESNAME_DECODE                      "_DEC"
#define ASL_RESNAME_DMA                         "_DMA"
#define ASL_RESNAME_DMATYPE                     "_TYP"  /* Compatible(0), A(1), B(2), F(3) */
#define ASL_RESNAME_GRANULARITY                 "_GRA"
#define ASL_RESNAME_INTERRUPT                   "_INT"
#define ASL_RESNAME_INTERRUPTLEVEL              "_LL_"  /* active_lo(1), active_hi(0) */
#define ASL_RESNAME_INTERRUPTSHARE              "_SHR"  /* Shareable(1), no_share(0) */
#define ASL_RESNAME_INTERRUPTTYPE               "_HE_"  /* Edge(1), Level(0) */
#define ASL_RESNAME_LENGTH                      "_LEN"
#define ASL_RESNAME_MEMATTRIBUTES               "_MTP"  /* Memory(0), Reserved(1), ACPI(2), NVS(3) */
#define ASL_RESNAME_MEMTYPE                     "_MEM"  /* non_cache(0), Cacheable(1) Cache+combine(2), Cache+prefetch(3) */
#define ASL_RESNAME_MAXADDR                     "_MAX"
#define ASL_RESNAME_MINADDR                     "_MIN"
#define ASL_RESNAME_MAXTYPE                     "_MAF"
#define ASL_RESNAME_MINTYPE                     "_MIF"
#define ASL_RESNAME_REGISTERBITOFFSET           "_RBO"
#define ASL_RESNAME_REGISTERBITWIDTH            "_RBW"
#define ASL_RESNAME_RANGETYPE                   "_RNG"
#define ASL_RESNAME_READWRITETYPE               "_RW_"  /* read_only(0), Writeable (1) */
#define ASL_RESNAME_TRANSLATION                 "_TRA"
#define ASL_RESNAME_TRANSTYPE                   "_TRS"  /* Sparse(1), Dense(0) */
#define ASL_RESNAME_TYPE                        "_TTP"  /* Translation(1), Static (0) */
#define ASL_RESNAME_XFERTYPE                    "_SIz"  /* 8(0), 8_and16(1), 16(2) */


/* Default sizes for "small" resource descriptors */

#define ASL_RDESC_IRQ_SIZE                      0x02
#define ASL_RDESC_DMA_SIZE                      0x02
#define ASL_RDESC_ST_DEPEND_SIZE                0x00
#define ASL_RDESC_END_DEPEND_SIZE               0x00
#define ASL_RDESC_IO_SIZE                       0x07
#define ASL_RDESC_FIXED_IO_SIZE                 0x03
#define ASL_RDESC_END_TAG_SIZE                  0x01


struct asl_resource_node
{
	u32                                 buffer_length;
	void                                *buffer;
	struct asl_resource_node            *next;
};


/*
 * Resource descriptors defined in the ACPI specification.
 *
 * Packing/alignment must be BYTE because these descriptors
 * are used to overlay the AML byte stream.
 */
#pragma pack(1)

struct asl_irq_format_desc
{
	u8                                  descriptor_type;
	u16                                 irq_mask;
	u8                                  flags;
};


struct asl_irq_noflags_desc
{
	u8                                  descriptor_type;
	u16                                 irq_mask;
};


struct asl_dma_format_desc
{
	u8                                  descriptor_type;
	u8                                  dma_channel_mask;
	u8                                  flags;
};


struct asl_start_dependent_desc
{
	u8                                  descriptor_type;
	u8                                  flags;
};


struct asl_start_dependent_noprio_desc
{
	u8                                  descriptor_type;
};


struct asl_end_dependent_desc
{
	u8                                  descriptor_type;
};


struct asl_io_port_desc
{
	u8                                  descriptor_type;
	u8                                  information;
	u16                                 address_min;
	u16                                 address_max;
	u8                                  alignment;
	u8                                  length;
};


struct asl_fixed_io_port_desc
{
	u8                                  descriptor_type;
	u16                                 base_address;
	u8                                  length;
};


struct asl_small_vendor_desc
{
	u8                                  descriptor_type;
	u8                                  vendor_defined[7];
};


struct asl_end_tag_desc
{
	u8                                  descriptor_type;
	u8                                  checksum;
};


/* LARGE descriptors */

struct asl_memory_24_desc
{
	u8                                  descriptor_type;
	u16                                 length;
	u8                                  information;
	u16                                 address_min;
	u16                                 address_max;
	u16                                 alignment;
	u16                                 range_length;
};


struct asl_large_vendor_desc
{
	u8                                  descriptor_type;
	u16                                 length;
	u8                                  vendor_defined[1];
};


struct asl_memory_32_desc
{
	u8                                  descriptor_type;
	u16                                 length;
	u8                                  information;
	u32                                 address_min;
	u32                                 address_max;
	u32                                 alignment;
	u32                                 range_length;
};


struct asl_fixed_memory_32_desc
{
	u8                                  descriptor_type;
	u16                                 length;
	u8                                  information;
	u32                                 base_address;
	u32                                 range_length;
};


struct asl_extended_address_desc
{
	u8                                  descriptor_type;
	u16                                 length;
	u8                                  resource_type;
	u8                                  flags;
	u8                                  specific_flags;
	u8                                  revision_iD;
	u8                                  reserved;
	u64                                 granularity;
	u64                                 address_min;
	u64                                 address_max;
	u64                                 translation_offset;
	u64                                 address_length;
	u64                                 type_specific_attributes;
	u8                                  optional_fields[2]; /* Used for length calculation only */
};

#define ASL_EXTENDED_ADDRESS_DESC_REVISION          1       /* ACPI 3.0 */


struct asl_qword_address_desc
{
	u8                                  descriptor_type;
	u16                                 length;
	u8                                  resource_type;
	u8                                  flags;
	u8                                  specific_flags;
	u64                                 granularity;
	u64                                 address_min;
	u64                                 address_max;
	u64                                 translation_offset;
	u64                                 address_length;
	u8                                  optional_fields[2];
};


struct asl_dword_address_desc
{
	u8                                  descriptor_type;
	u16                                 length;
	u8                                  resource_type;
	u8                                  flags;
	u8                                  specific_flags;
	u32                                 granularity;
	u32                                 address_min;
	u32                                 address_max;
	u32                                 translation_offset;
	u32                                 address_length;
	u8                                  optional_fields[2];
};


struct asl_word_address_desc
{
	u8                                  descriptor_type;
	u16                                 length;
	u8                                  resource_type;
	u8                                  flags;
	u8                                  specific_flags;
	u16                                 granularity;
	u16                                 address_min;
	u16                                 address_max;
	u16                                 translation_offset;
	u16                                 address_length;
	u8                                  optional_fields[2];
};


struct asl_extended_xrupt_desc
{
	u8                                  descriptor_type;
	u16                                 length;
	u8                                  flags;
	u8                                  table_length;
	u32                                 interrupt_number[1];
	/* res_source_index, res_source optional fields follow */
};


struct asl_general_register_desc
{
	u8                                  descriptor_type;
	u16                                 length;
	u8                                  address_space_id;
	u8                                  bit_width;
	u8                                  bit_offset;
	u8                                  access_size; /* ACPI 3.0, was Reserved */
	u64                                 address;
};

/* restore default alignment */

#pragma pack()

/* Union of all resource descriptors, so we can allocate the worst case */

union asl_resource_desc
{
	struct asl_irq_format_desc          irq;
	struct asl_dma_format_desc          dma;
	struct asl_start_dependent_desc     std;
	struct asl_end_dependent_desc       end;
	struct asl_io_port_desc             iop;
	struct asl_fixed_io_port_desc       fio;
	struct asl_small_vendor_desc        smv;
	struct asl_end_tag_desc             et;

	struct asl_memory_24_desc           M24;
	struct asl_large_vendor_desc        lgv;
	struct asl_memory_32_desc           M32;
	struct asl_fixed_memory_32_desc     F32;
	struct asl_qword_address_desc       qas;
	struct asl_dword_address_desc       das;
	struct asl_word_address_desc        was;
	struct asl_extended_address_desc    eas;
	struct asl_extended_xrupt_desc      exx;
	struct asl_general_register_desc    grg;
	u32                                 u32_item;
	u16                                 u16_item;
	u8                                  U8item;
};


#endif


/******************************************************************************
 *
 * Name: acdisasm.h - AML disassembler
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
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

#ifndef __ACDISASM_H__
#define __ACDISASM_H__

#include "amlresrc.h"

#define BLOCK_NONE              0
#define BLOCK_PAREN             1
#define BLOCK_BRACE             2
#define BLOCK_COMMA_LIST        4
#define ACPI_DEFAULT_RESNAME    *(u32 *) "__RD"

struct acpi_external_list {
	char *path;
	struct acpi_external_list *next;
};

extern struct acpi_external_list *acpi_gbl_external_list;

typedef const struct acpi_dmtable_info {
	u8 opcode;
	u8 offset;
	char *name;

} acpi_dmtable_info;

/*
 * Values for Opcode above.
 * Note: 0-7 must not change, used as a flag shift value
 */
#define ACPI_DMT_FLAG0                  0
#define ACPI_DMT_FLAG1                  1
#define ACPI_DMT_FLAG2                  2
#define ACPI_DMT_FLAG3                  3
#define ACPI_DMT_FLAG4                  4
#define ACPI_DMT_FLAG5                  5
#define ACPI_DMT_FLAG6                  6
#define ACPI_DMT_FLAG7                  7
#define ACPI_DMT_FLAGS0                 8
#define ACPI_DMT_FLAGS2                 9
#define ACPI_DMT_UINT8                  10
#define ACPI_DMT_UINT16                 11
#define ACPI_DMT_UINT24                 12
#define ACPI_DMT_UINT32                 13
#define ACPI_DMT_UINT56                 14
#define ACPI_DMT_UINT64                 15
#define ACPI_DMT_STRING                 16
#define ACPI_DMT_NAME4                  17
#define ACPI_DMT_NAME6                  18
#define ACPI_DMT_NAME8                  19
#define ACPI_DMT_CHKSUM                 20
#define ACPI_DMT_SPACEID                21
#define ACPI_DMT_GAS                    22
#define ACPI_DMT_MADT                   23
#define ACPI_DMT_SRAT                   24
#define ACPI_DMT_EXIT                   25

typedef
void (*ACPI_TABLE_HANDLER) (struct acpi_table_header * table);

struct acpi_dmtable_data {
	char *signature;
	struct acpi_dmtable_info *table_info;
	ACPI_TABLE_HANDLER table_handler;
};

struct acpi_op_walk_info {
	u32 level;
	u32 bit_offset;
	u32 flags;
	struct acpi_walk_state *walk_state;
};

typedef
acpi_status(*asl_walk_callback) (union acpi_parse_object * op,
				 u32 level, void *context);

struct acpi_resource_tag {
	u32 bit_index;
	char *tag;
};

/* Strings used for decoding flags to ASL keywords */

extern const char *acpi_gbl_word_decode[];
extern const char *acpi_gbl_irq_decode[];
extern const char *acpi_gbl_lock_rule[];
extern const char *acpi_gbl_access_types[];
extern const char *acpi_gbl_update_rules[];
extern const char *acpi_gbl_match_ops[];

extern struct acpi_dmtable_info acpi_dm_table_info_asf0[];
extern struct acpi_dmtable_info acpi_dm_table_info_asf1[];
extern struct acpi_dmtable_info acpi_dm_table_info_asf2[];
extern struct acpi_dmtable_info acpi_dm_table_info_asf3[];
extern struct acpi_dmtable_info acpi_dm_table_info_asf4[];
extern struct acpi_dmtable_info acpi_dm_table_info_asf_hdr[];
extern struct acpi_dmtable_info acpi_dm_table_info_boot[];
extern struct acpi_dmtable_info acpi_dm_table_info_cpep[];
extern struct acpi_dmtable_info acpi_dm_table_info_cpep0[];
extern struct acpi_dmtable_info acpi_dm_table_info_dbgp[];
extern struct acpi_dmtable_info acpi_dm_table_info_ecdt[];
extern struct acpi_dmtable_info acpi_dm_table_info_facs[];
extern struct acpi_dmtable_info acpi_dm_table_info_fadt1[];
extern struct acpi_dmtable_info acpi_dm_table_info_fadt2[];
extern struct acpi_dmtable_info acpi_dm_table_info_gas[];
extern struct acpi_dmtable_info acpi_dm_table_info_header[];
extern struct acpi_dmtable_info acpi_dm_table_info_hpet[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt0[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt1[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt2[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt3[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt4[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt5[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt6[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt7[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt8[];
extern struct acpi_dmtable_info acpi_dm_table_info_madt_hdr[];
extern struct acpi_dmtable_info acpi_dm_table_info_mcfg[];
extern struct acpi_dmtable_info acpi_dm_table_info_mcfg0[];
extern struct acpi_dmtable_info acpi_dm_table_info_rsdp1[];
extern struct acpi_dmtable_info acpi_dm_table_info_rsdp2[];
extern struct acpi_dmtable_info acpi_dm_table_info_sbst[];
extern struct acpi_dmtable_info acpi_dm_table_info_slit[];
extern struct acpi_dmtable_info acpi_dm_table_info_spcr[];
extern struct acpi_dmtable_info acpi_dm_table_info_spmi[];
extern struct acpi_dmtable_info acpi_dm_table_info_srat[];
extern struct acpi_dmtable_info acpi_dm_table_info_srat0[];
extern struct acpi_dmtable_info acpi_dm_table_info_srat1[];
extern struct acpi_dmtable_info acpi_dm_table_info_tcpa[];
extern struct acpi_dmtable_info acpi_dm_table_info_wdrt[];

/*
 * dmtable
 */
void acpi_dm_dump_data_table(struct acpi_table_header *table);

void
acpi_dm_dump_table(u32 table_length,
		   u32 table_offset,
		   void *table,
		   u32 sub_table_length, struct acpi_dmtable_info *info);

void acpi_dm_line_header(u32 offset, u32 byte_length, char *name);

void acpi_dm_line_header2(u32 offset, u32 byte_length, char *name, u32 value);

/*
 * dmtbdump
 */
void acpi_dm_dump_asf(struct acpi_table_header *table);

void acpi_dm_dump_cpep(struct acpi_table_header *table);

void acpi_dm_dump_fadt(struct acpi_table_header *table);

void acpi_dm_dump_srat(struct acpi_table_header *table);

void acpi_dm_dump_mcfg(struct acpi_table_header *table);

void acpi_dm_dump_madt(struct acpi_table_header *table);

u32 acpi_dm_dump_rsdp(struct acpi_table_header *table);

void acpi_dm_dump_rsdt(struct acpi_table_header *table);

void acpi_dm_dump_slit(struct acpi_table_header *table);

void acpi_dm_dump_xsdt(struct acpi_table_header *table);

/*
 * dmwalk
 */
void
acpi_dm_disassemble(struct acpi_walk_state *walk_state,
		    union acpi_parse_object *origin, u32 num_opcodes);

void
acpi_dm_walk_parse_tree(union acpi_parse_object *op,
			asl_walk_callback descending_callback,
			asl_walk_callback ascending_callback, void *context);

/*
 * dmopcode
 */
void
acpi_dm_disassemble_one_op(struct acpi_walk_state *walk_state,
			   struct acpi_op_walk_info *info,
			   union acpi_parse_object *op);

void acpi_dm_decode_internal_object(union acpi_operand_object *obj_desc);

u32 acpi_dm_list_type(union acpi_parse_object *op);

void acpi_dm_method_flags(union acpi_parse_object *op);

void acpi_dm_field_flags(union acpi_parse_object *op);

void acpi_dm_address_space(u8 space_id);

void acpi_dm_region_flags(union acpi_parse_object *op);

void acpi_dm_match_op(union acpi_parse_object *op);

u8 acpi_dm_comma_if_list_member(union acpi_parse_object *op);

void acpi_dm_comma_if_field_member(union acpi_parse_object *op);

/*
 * dmnames
 */
u32 acpi_dm_dump_name(char *name);

acpi_status
acpi_ps_display_object_pathname(struct acpi_walk_state *walk_state,
				union acpi_parse_object *op);

void acpi_dm_namestring(char *name);

/*
 * dmobject
 */
void
acpi_dm_display_internal_object(union acpi_operand_object *obj_desc,
				struct acpi_walk_state *walk_state);

void acpi_dm_display_arguments(struct acpi_walk_state *walk_state);

void acpi_dm_display_locals(struct acpi_walk_state *walk_state);

void
acpi_dm_dump_method_info(acpi_status status,
			 struct acpi_walk_state *walk_state,
			 union acpi_parse_object *op);

/*
 * dmbuffer
 */
void acpi_dm_disasm_byte_list(u32 level, u8 * byte_data, u32 byte_count);

void
acpi_dm_byte_list(struct acpi_op_walk_info *info, union acpi_parse_object *op);

void acpi_dm_is_eisa_id(union acpi_parse_object *op);

void acpi_dm_eisa_id(u32 encoded_id);

u8 acpi_dm_is_unicode_buffer(union acpi_parse_object *op);

u8 acpi_dm_is_string_buffer(union acpi_parse_object *op);

/*
 * dmresrc
 */
void acpi_dm_dump_integer8(u8 value, char *name);

void acpi_dm_dump_integer16(u16 value, char *name);

void acpi_dm_dump_integer32(u32 value, char *name);

void acpi_dm_dump_integer64(u64 value, char *name);

void
acpi_dm_resource_template(struct acpi_op_walk_info *info,
			  union acpi_parse_object *op,
			  u8 * byte_data, u32 byte_count);

u8 acpi_dm_is_resource_template(union acpi_parse_object *op);

void acpi_dm_indent(u32 level);

void acpi_dm_bit_list(u16 mask);

void acpi_dm_decode_attribute(u8 attribute);

void acpi_dm_descriptor_name(void);

/*
 * dmresrcl
 */
void
acpi_dm_word_descriptor(union aml_resource *resource, u32 length, u32 level);

void
acpi_dm_dword_descriptor(union aml_resource *resource, u32 length, u32 level);

void
acpi_dm_extended_descriptor(union aml_resource *resource,
			    u32 length, u32 level);

void
acpi_dm_qword_descriptor(union aml_resource *resource, u32 length, u32 level);

void
acpi_dm_memory24_descriptor(union aml_resource *resource,
			    u32 length, u32 level);

void
acpi_dm_memory32_descriptor(union aml_resource *resource,
			    u32 length, u32 level);

void
acpi_dm_fixed_memory32_descriptor(union aml_resource *resource,
				  u32 length, u32 level);

void
acpi_dm_generic_register_descriptor(union aml_resource *resource,
				    u32 length, u32 level);

void
acpi_dm_interrupt_descriptor(union aml_resource *resource,
			     u32 length, u32 level);

void
acpi_dm_vendor_large_descriptor(union aml_resource *resource,
				u32 length, u32 level);

void acpi_dm_vendor_common(char *name, u8 * byte_data, u32 length, u32 level);

/*
 * dmresrcs
 */
void
acpi_dm_irq_descriptor(union aml_resource *resource, u32 length, u32 level);

void
acpi_dm_dma_descriptor(union aml_resource *resource, u32 length, u32 level);

void acpi_dm_io_descriptor(union aml_resource *resource, u32 length, u32 level);

void
acpi_dm_fixed_io_descriptor(union aml_resource *resource,
			    u32 length, u32 level);

void
acpi_dm_start_dependent_descriptor(union aml_resource *resource,
				   u32 length, u32 level);

void
acpi_dm_end_dependent_descriptor(union aml_resource *resource,
				 u32 length, u32 level);

void
acpi_dm_vendor_small_descriptor(union aml_resource *resource,
				u32 length, u32 level);

/*
 * dmutils
 */
void acpi_dm_add_to_external_list(char *path);

/*
 * dmrestag
 */
void acpi_dm_find_resources(union acpi_parse_object *root);

void
acpi_dm_check_resource_reference(union acpi_parse_object *op,
				 struct acpi_walk_state *walk_state);

#endif				/* __ACDISASM_H__ */

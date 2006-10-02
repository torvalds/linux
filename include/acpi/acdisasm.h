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

struct acpi_external_list {
	char *path;
	struct acpi_external_list *next;
};

extern struct acpi_external_list *acpi_gbl_external_list;

/* Strings used for decoding flags to ASL keywords */

extern const char *acpi_gbl_word_decode[4];
extern const char *acpi_gbl_irq_decode[2];
extern const char *acpi_gbl_lock_rule[ACPI_NUM_LOCK_RULES];
extern const char *acpi_gbl_access_types[ACPI_NUM_ACCESS_TYPES];
extern const char *acpi_gbl_update_rules[ACPI_NUM_UPDATE_RULES];
extern const char *acpi_gbl_match_ops[ACPI_NUM_MATCH_OPS];

struct acpi_op_walk_info {
	u32 level;
	u32 bit_offset;
	u32 flags;
	struct acpi_walk_state *walk_state;
};

typedef
acpi_status(*asl_walk_callback) (union acpi_parse_object * op,
				 u32 level, void *context);

/*
 * dmwalk
 */
void
acpi_dm_disassemble(struct acpi_walk_state *walk_state,
		    union acpi_parse_object *origin, u32 num_opcodes);

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
			  u8 * byte_data, u32 byte_count);

u8 acpi_dm_is_resource_template(union acpi_parse_object *op);

void acpi_dm_indent(u32 level);

void acpi_dm_bit_list(u16 mask);

void acpi_dm_decode_attribute(u8 attribute);

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

#endif				/* __ACDISASM_H__ */

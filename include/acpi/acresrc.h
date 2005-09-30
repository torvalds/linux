/******************************************************************************
 *
 * Name: acresrc.h - Resource Manager function prototypes
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

#ifndef __ACRESRC_H__
#define __ACRESRC_H__

/* Need the AML resource descriptor structs */

#include "amlresrc.h"

/*
 * Resource dispatch and info tables
 */
struct acpi_resource_info {
	u8 length_type;
	u8 minimum_aml_resource_length;
	u8 minimum_internal_struct_length;
};

/* Types for length_type above */

#define ACPI_FIXED_LENGTH           0
#define ACPI_VARIABLE_LENGTH        1
#define ACPI_SMALL_VARIABLE_LENGTH  2

/* Handlers */

typedef acpi_status(*ACPI_SET_RESOURCE_HANDLER) (struct acpi_resource *
						 resource,
						 union aml_resource * aml);

typedef acpi_status(*ACPI_GET_RESOURCE_HANDLER) (union aml_resource * aml,
						 u16 aml_resource_length,
						 struct acpi_resource *
						 resource);

typedef void (*ACPI_DUMP_RESOURCE_HANDLER) (union acpi_resource_data * data);

/* Tables indexed by internal resource type */

extern u8 acpi_gbl_aml_resource_sizes[];
extern ACPI_SET_RESOURCE_HANDLER acpi_gbl_set_resource_dispatch[];
extern ACPI_DUMP_RESOURCE_HANDLER acpi_gbl_dump_resource_dispatch[];

/* Tables indexed by raw AML resource descriptor type */

extern struct acpi_resource_info acpi_gbl_sm_resource_info[];
extern struct acpi_resource_info acpi_gbl_lg_resource_info[];
extern ACPI_GET_RESOURCE_HANDLER acpi_gbl_sm_get_resource_dispatch[];
extern ACPI_GET_RESOURCE_HANDLER acpi_gbl_lg_get_resource_dispatch[];

/*
 *  Function prototypes called from Acpi* APIs
 */
acpi_status
acpi_rs_get_prt_method_data(acpi_handle handle, struct acpi_buffer *ret_buffer);

acpi_status
acpi_rs_get_crs_method_data(acpi_handle handle, struct acpi_buffer *ret_buffer);

#ifdef	ACPI_FUTURE_USAGE
acpi_status
acpi_rs_get_prs_method_data(acpi_handle handle, struct acpi_buffer *ret_buffer);
#endif				/* ACPI_FUTURE_USAGE */

acpi_status
acpi_rs_get_method_data(acpi_handle handle,
			char *path, struct acpi_buffer *ret_buffer);

acpi_status
acpi_rs_set_srs_method_data(acpi_handle handle, struct acpi_buffer *ret_buffer);

acpi_status
acpi_rs_create_resource_list(union acpi_operand_object *aml_buffer,
			     struct acpi_buffer *output_buffer);

acpi_status
acpi_rs_create_aml_resources(struct acpi_resource *linked_list_buffer,
			     struct acpi_buffer *output_buffer);

acpi_status
acpi_rs_create_pci_routing_table(union acpi_operand_object *package_object,
				 struct acpi_buffer *output_buffer);

/*
 * rsdump
 */
#ifdef	ACPI_FUTURE_USAGE
void acpi_rs_dump_resource_list(struct acpi_resource *resource);

void acpi_rs_dump_irq_list(u8 * route_table);
#endif				/* ACPI_FUTURE_USAGE */

/*
 * rscalc
 */
acpi_status
acpi_rs_get_list_length(u8 * aml_buffer,
			u32 aml_buffer_length, acpi_size * size_needed);

acpi_status
acpi_rs_get_aml_length(struct acpi_resource *linked_list_buffer,
		       acpi_size * size_needed);

acpi_status
acpi_rs_get_pci_routing_table_length(union acpi_operand_object *package_object,
				     acpi_size * buffer_size_needed);

acpi_status
acpi_rs_convert_aml_to_resources(u8 * aml_buffer,
				 u32 aml_buffer_length, u8 * output_buffer);

acpi_status
acpi_rs_convert_resources_to_aml(struct acpi_resource *resource,
				 acpi_size aml_size_needed, u8 * output_buffer);

/*
 * rsio
 */
acpi_status
acpi_rs_get_io(union aml_resource *aml,
	       u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_io(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_fixed_io(union aml_resource *aml,
		     u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_fixed_io(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_dma(union aml_resource *aml,
		u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_dma(struct acpi_resource *resource, union aml_resource *aml);

/*
 * rsirq
 */
acpi_status
acpi_rs_get_irq(union aml_resource *aml,
		u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_irq(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_ext_irq(union aml_resource *aml,
		    u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_ext_irq(struct acpi_resource *resource, union aml_resource *aml);

/*
 * rsaddr
 */
acpi_status
acpi_rs_get_address16(union aml_resource *aml,
		      u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_address16(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_address32(union aml_resource *aml,
		      u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_address32(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_address64(union aml_resource *aml,
		      u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_address64(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_ext_address64(union aml_resource *aml,
			  u16 aml_resource_length,
			  struct acpi_resource *resource);

acpi_status
acpi_rs_set_ext_address64(struct acpi_resource *resource,
			  union aml_resource *aml);

/*
 * rsmemory
 */
acpi_status
acpi_rs_get_memory24(union aml_resource *aml,
		     u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_memory24(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_memory32(union aml_resource *aml,
		     u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_memory32(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_fixed_memory32(union aml_resource *aml,
			   u16 aml_resource_length,
			   struct acpi_resource *resource);

acpi_status
acpi_rs_set_fixed_memory32(struct acpi_resource *resource,
			   union aml_resource *aml);

/*
 * rsmisc
 */
acpi_status
acpi_rs_get_generic_reg(union aml_resource *aml,
			u16 aml_resource_length,
			struct acpi_resource *resource);

acpi_status
acpi_rs_set_generic_reg(struct acpi_resource *resource,
			union aml_resource *aml);

acpi_status
acpi_rs_get_vendor(union aml_resource *aml,
		   u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_vendor(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_start_dpf(union aml_resource *aml,
		      u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_start_dpf(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_end_dpf(union aml_resource *aml,
		    u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_end_dpf(struct acpi_resource *resource, union aml_resource *aml);

acpi_status
acpi_rs_get_end_tag(union aml_resource *aml,
		    u16 aml_resource_length, struct acpi_resource *resource);

acpi_status
acpi_rs_set_end_tag(struct acpi_resource *resource, union aml_resource *aml);

/*
 * rsutils
 */
void
acpi_rs_move_data(void *destination,
		  void *source, u16 item_count, u8 move_type);

/* Types used in move_type above */

#define ACPI_MOVE_TYPE_16_TO_32        0
#define ACPI_MOVE_TYPE_32_TO_16        1
#define ACPI_MOVE_TYPE_32_TO_32        2
#define ACPI_MOVE_TYPE_64_TO_64        3

u16
acpi_rs_get_resource_source(u16 resource_length,
			    acpi_size minimum_length,
			    struct acpi_resource_source *resource_source,
			    union aml_resource *aml, char *string_ptr);

acpi_size
acpi_rs_set_resource_source(union aml_resource *aml,
			    acpi_size minimum_length,
			    struct acpi_resource_source *resource_source);

u8 acpi_rs_get_resource_type(u8 resource_start_byte);

u32 acpi_rs_get_descriptor_length(union aml_resource *aml);

u16 acpi_rs_get_resource_length(union aml_resource *aml);

void
acpi_rs_set_resource_header(u8 descriptor_type,
			    acpi_size total_length, union aml_resource *aml);

struct acpi_resource_info *acpi_rs_get_resource_info(u8 resource_type);

#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)
/*
 * rsdump
 */
void acpi_rs_dump_irq(union acpi_resource_data *resource);

void acpi_rs_dump_address16(union acpi_resource_data *resource);

void acpi_rs_dump_address32(union acpi_resource_data *resource);

void acpi_rs_dump_address64(union acpi_resource_data *resource);

void acpi_rs_dump_ext_address64(union acpi_resource_data *resource);

void acpi_rs_dump_dma(union acpi_resource_data *resource);

void acpi_rs_dump_io(union acpi_resource_data *resource);

void acpi_rs_dump_ext_irq(union acpi_resource_data *resource);

void acpi_rs_dump_fixed_io(union acpi_resource_data *resource);

void acpi_rs_dump_fixed_memory32(union acpi_resource_data *resource);

void acpi_rs_dump_memory24(union acpi_resource_data *resource);

void acpi_rs_dump_memory32(union acpi_resource_data *resource);

void acpi_rs_dump_start_dpf(union acpi_resource_data *resource);

void acpi_rs_dump_vendor(union acpi_resource_data *resource);

void acpi_rs_dump_generic_reg(union acpi_resource_data *resource);

void acpi_rs_dump_end_dpf(union acpi_resource_data *resource);

void acpi_rs_dump_end_tag(union acpi_resource_data *resource);

#endif

#endif				/* __ACRESRC_H__ */

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
acpi_rs_create_resource_list(union acpi_operand_object *byte_stream_buffer,
			     struct acpi_buffer *output_buffer);

acpi_status
acpi_rs_create_byte_stream(struct acpi_resource *linked_list_buffer,
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
acpi_rs_get_byte_stream_start(u8 * byte_stream_buffer,
			      u8 ** byte_stream_start, u32 * size);

acpi_status
acpi_rs_get_list_length(u8 * byte_stream_buffer,
			u32 byte_stream_buffer_length, acpi_size * size_needed);

acpi_status
acpi_rs_get_byte_stream_length(struct acpi_resource *linked_list_buffer,
			       acpi_size * size_needed);

acpi_status
acpi_rs_get_pci_routing_table_length(union acpi_operand_object *package_object,
				     acpi_size * buffer_size_needed);

acpi_status
acpi_rs_byte_stream_to_list(u8 * byte_stream_buffer,
			    u32 byte_stream_buffer_length, u8 * output_buffer);

acpi_status
acpi_rs_list_to_byte_stream(struct acpi_resource *linked_list,
			    acpi_size byte_stream_size_needed,
			    u8 * output_buffer);

acpi_status
acpi_rs_io_resource(u8 * byte_stream_buffer,
		    acpi_size * bytes_consumed,
		    u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_fixed_io_resource(u8 * byte_stream_buffer,
			  acpi_size * bytes_consumed,
			  u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_io_stream(struct acpi_resource *linked_list,
		  u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_fixed_io_stream(struct acpi_resource *linked_list,
			u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_irq_resource(u8 * byte_stream_buffer,
		     acpi_size * bytes_consumed,
		     u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_irq_stream(struct acpi_resource *linked_list,
		   u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_dma_resource(u8 * byte_stream_buffer,
		     acpi_size * bytes_consumed,
		     u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_dma_stream(struct acpi_resource *linked_list,
		   u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_address16_resource(u8 * byte_stream_buffer,
			   acpi_size * bytes_consumed,
			   u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_address16_stream(struct acpi_resource *linked_list,
			 u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_address32_resource(u8 * byte_stream_buffer,
			   acpi_size * bytes_consumed,
			   u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_address32_stream(struct acpi_resource *linked_list,
			 u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_address64_resource(u8 * byte_stream_buffer,
			   acpi_size * bytes_consumed,
			   u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_address64_stream(struct acpi_resource *linked_list,
			 u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_start_depend_fns_resource(u8 * byte_stream_buffer,
				  acpi_size * bytes_consumed,
				  u8 ** output_buffer,
				  acpi_size * structure_size);

acpi_status
acpi_rs_end_depend_fns_resource(u8 * byte_stream_buffer,
				acpi_size * bytes_consumed,
				u8 ** output_buffer,
				acpi_size * structure_size);

acpi_status
acpi_rs_start_depend_fns_stream(struct acpi_resource *linked_list,
				u8 ** output_buffer,
				acpi_size * bytes_consumed);

acpi_status
acpi_rs_end_depend_fns_stream(struct acpi_resource *linked_list,
			      u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_memory24_resource(u8 * byte_stream_buffer,
			  acpi_size * bytes_consumed,
			  u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_memory24_stream(struct acpi_resource *linked_list,
			u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_memory32_range_resource(u8 * byte_stream_buffer,
				acpi_size * bytes_consumed,
				u8 ** output_buffer,
				acpi_size * structure_size);

acpi_status
acpi_rs_fixed_memory32_resource(u8 * byte_stream_buffer,
				acpi_size * bytes_consumed,
				u8 ** output_buffer,
				acpi_size * structure_size);

acpi_status
acpi_rs_memory32_range_stream(struct acpi_resource *linked_list,
			      u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_fixed_memory32_stream(struct acpi_resource *linked_list,
			      u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_extended_irq_resource(u8 * byte_stream_buffer,
			      acpi_size * bytes_consumed,
			      u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_extended_irq_stream(struct acpi_resource *linked_list,
			    u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_end_tag_resource(u8 * byte_stream_buffer,
			 acpi_size * bytes_consumed,
			 u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_end_tag_stream(struct acpi_resource *linked_list,
		       u8 ** output_buffer, acpi_size * bytes_consumed);

acpi_status
acpi_rs_vendor_resource(u8 * byte_stream_buffer,
			acpi_size * bytes_consumed,
			u8 ** output_buffer, acpi_size * structure_size);

acpi_status
acpi_rs_vendor_stream(struct acpi_resource *linked_list,
		      u8 ** output_buffer, acpi_size * bytes_consumed);

u8 acpi_rs_get_resource_type(u8 resource_start_byte);

#endif				/* __ACRESRC_H__ */

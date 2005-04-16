/******************************************************************************
 *
 * Name: actables.h - ACPI table management
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

#ifndef __ACTABLES_H__
#define __ACTABLES_H__


/* Used in acpi_tb_map_acpi_table for size parameter if table header is to be used */

#define SIZE_IN_HEADER          0


#ifdef ACPI_FUTURE_USAGE
acpi_status
acpi_tb_handle_to_object (
	u16                             table_id,
	struct acpi_table_desc          **table_desc);
#endif

/*
 * tbconvrt - Table conversion routines
 */

acpi_status
acpi_tb_convert_to_xsdt (
	struct acpi_table_desc          *table_info);

acpi_status
acpi_tb_convert_table_fadt (
	void);

acpi_status
acpi_tb_build_common_facs (
	struct acpi_table_desc          *table_info);

u32
acpi_tb_get_table_count (
	struct rsdp_descriptor          *RSDP,
	struct acpi_table_header        *RSDT);

/*
 * tbget - Table "get" routines
 */

acpi_status
acpi_tb_get_table (
	struct acpi_pointer             *address,
	struct acpi_table_desc          *table_info);

acpi_status
acpi_tb_get_table_header (
	struct acpi_pointer             *address,
	struct acpi_table_header        *return_header);

acpi_status
acpi_tb_get_table_body (
	struct acpi_pointer             *address,
	struct acpi_table_header        *header,
	struct acpi_table_desc          *table_info);

acpi_status
acpi_tb_get_this_table (
	struct acpi_pointer             *address,
	struct acpi_table_header        *header,
	struct acpi_table_desc          *table_info);

acpi_status
acpi_tb_table_override (
	struct acpi_table_header        *header,
	struct acpi_table_desc          *table_info);

acpi_status
acpi_tb_get_table_ptr (
	acpi_table_type                 table_type,
	u32                             instance,
	struct acpi_table_header        **table_ptr_loc);

acpi_status
acpi_tb_verify_rsdp (
	struct acpi_pointer             *address);

void
acpi_tb_get_rsdt_address (
	struct acpi_pointer             *out_address);

acpi_status
acpi_tb_validate_rsdt (
	struct acpi_table_header        *table_ptr);

acpi_status
acpi_tb_get_required_tables (
	void);

acpi_status
acpi_tb_get_primary_table (
	struct acpi_pointer             *address,
	struct acpi_table_desc          *table_info);

acpi_status
acpi_tb_get_secondary_table (
	struct acpi_pointer             *address,
	acpi_string                     signature,
	struct acpi_table_desc          *table_info);

/*
 * tbinstall - Table installation
 */

acpi_status
acpi_tb_install_table (
	struct acpi_table_desc          *table_info);

acpi_status
acpi_tb_match_signature (
	char                            *signature,
	struct acpi_table_desc          *table_info,
	u8                              search_type);

acpi_status
acpi_tb_recognize_table (
	struct acpi_table_desc          *table_info,
	u8                              search_type);

acpi_status
acpi_tb_init_table_descriptor (
	acpi_table_type                 table_type,
	struct acpi_table_desc          *table_info);


/*
 * tbremove - Table removal and deletion
 */

void
acpi_tb_delete_all_tables (
	void);

void
acpi_tb_delete_tables_by_type (
	acpi_table_type                 type);

void
acpi_tb_delete_single_table (
	struct acpi_table_desc          *table_desc);

struct acpi_table_desc *
acpi_tb_uninstall_table (
	struct acpi_table_desc          *table_desc);


/*
 * tbrsd - RSDP, RSDT utilities
 */

acpi_status
acpi_tb_get_table_rsdt (
	void);

u8 *
acpi_tb_scan_memory_for_rsdp (
	u8                              *start_address,
	u32                             length);

acpi_status
acpi_tb_find_rsdp (
	struct acpi_table_desc          *table_info,
	u32                             flags);


/*
 * tbutils - common table utilities
 */

acpi_status
acpi_tb_find_table (
	char                            *signature,
	char                            *oem_id,
	char                            *oem_table_id,
	struct acpi_table_header        **table_ptr);

acpi_status
acpi_tb_verify_table_checksum (
	struct acpi_table_header        *table_header);

u8
acpi_tb_checksum (
	void                            *buffer,
	u32                             length);

acpi_status
acpi_tb_validate_table_header (
	struct acpi_table_header        *table_header);


#endif /* __ACTABLES_H__ */

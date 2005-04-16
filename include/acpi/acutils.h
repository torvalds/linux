/******************************************************************************
 *
 * Name: acutils.h -- prototypes for the common (subsystem-wide) procedures
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

#ifndef _ACUTILS_H
#define _ACUTILS_H


typedef
acpi_status (*acpi_pkg_callback) (
	u8                              object_type,
	union acpi_operand_object       *source_object,
	union acpi_generic_state        *state,
	void                            *context);

acpi_status
acpi_ut_walk_package_tree (
	union acpi_operand_object       *source_object,
	void                            *target_object,
	acpi_pkg_callback               walk_callback,
	void                            *context);

struct acpi_pkg_info
{
	u8                              *free_space;
	acpi_size                       length;
	u32                             object_space;
	u32                             num_packages;
};

#define REF_INCREMENT       (u16) 0
#define REF_DECREMENT       (u16) 1
#define REF_FORCE_DELETE    (u16) 2

/* acpi_ut_dump_buffer */

#define DB_BYTE_DISPLAY     1
#define DB_WORD_DISPLAY     2
#define DB_DWORD_DISPLAY    4
#define DB_QWORD_DISPLAY    8


/* Global initialization interfaces */

void
acpi_ut_init_globals (
	void);

void
acpi_ut_terminate (
	void);


/*
 * ut_init - miscellaneous initialization and shutdown
 */

acpi_status
acpi_ut_hardware_initialize (
	void);

void
acpi_ut_subsystem_shutdown (
	void);

acpi_status
acpi_ut_validate_fadt (
	void);

/*
 * ut_global - Global data structures and procedures
 */

#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)

char *
acpi_ut_get_mutex_name (
	u32                             mutex_id);

#endif

char *
acpi_ut_get_type_name (
	acpi_object_type                type);

char *
acpi_ut_get_node_name (
	void                            *object);

char *
acpi_ut_get_descriptor_name (
	void                            *object);

char *
acpi_ut_get_object_type_name (
	union acpi_operand_object       *obj_desc);

char *
acpi_ut_get_region_name (
	u8                              space_id);

char *
acpi_ut_get_event_name (
	u32                             event_id);

char
acpi_ut_hex_to_ascii_char (
	acpi_integer                    integer,
	u32                             position);

u8
acpi_ut_valid_object_type (
	acpi_object_type                type);

acpi_owner_id
acpi_ut_allocate_owner_id (
	u32                             id_type);


/*
 * ut_clib - Local implementations of C library functions
 */

#ifndef ACPI_USE_SYSTEM_CLIBRARY

acpi_size
acpi_ut_strlen (
	const char                      *string);

char *
acpi_ut_strcpy (
	char                            *dst_string,
	const char                      *src_string);

char *
acpi_ut_strncpy (
	char                            *dst_string,
	const char                      *src_string,
	acpi_size                       count);

int
acpi_ut_memcmp (
	const char                      *buffer1,
	const char                      *buffer2,
	acpi_size                       count);

int
acpi_ut_strncmp (
	const char                      *string1,
	const char                      *string2,
	acpi_size                       count);

int
acpi_ut_strcmp (
	const char                      *string1,
	const char                      *string2);

char *
acpi_ut_strcat (
	char                            *dst_string,
	const char                      *src_string);

char *
acpi_ut_strncat (
	char                            *dst_string,
	const char                      *src_string,
	acpi_size                       count);

u32
acpi_ut_strtoul (
	const char                      *string,
	char                            **terminator,
	u32                             base);

char *
acpi_ut_strstr (
	char                            *string1,
	char                            *string2);

void *
acpi_ut_memcpy (
	void                            *dest,
	const void                      *src,
	acpi_size                       count);

void *
acpi_ut_memset (
	void                            *dest,
	acpi_native_uint                value,
	acpi_size                       count);

int
acpi_ut_to_upper (
	int                             c);

int
acpi_ut_to_lower (
	int                             c);

extern const u8 _acpi_ctype[];

#define _ACPI_XA     0x00    /* extra alphabetic - not supported */
#define _ACPI_XS     0x40    /* extra space */
#define _ACPI_BB     0x00    /* BEL, BS, etc. - not supported */
#define _ACPI_CN     0x20    /* CR, FF, HT, NL, VT */
#define _ACPI_DI     0x04    /* '0'-'9' */
#define _ACPI_LO     0x02    /* 'a'-'z' */
#define _ACPI_PU     0x10    /* punctuation */
#define _ACPI_SP     0x08    /* space */
#define _ACPI_UP     0x01    /* 'A'-'Z' */
#define _ACPI_XD     0x80    /* '0'-'9', 'A'-'F', 'a'-'f' */

#define ACPI_IS_DIGIT(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_DI))
#define ACPI_IS_SPACE(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_SP))
#define ACPI_IS_XDIGIT(c) (_acpi_ctype[(unsigned char)(c)] & (_ACPI_XD))
#define ACPI_IS_UPPER(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_UP))
#define ACPI_IS_LOWER(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_LO))
#define ACPI_IS_PRINT(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_LO | _ACPI_UP | _ACPI_DI | _ACPI_SP | _ACPI_PU))
#define ACPI_IS_ALPHA(c)  (_acpi_ctype[(unsigned char)(c)] & (_ACPI_LO | _ACPI_UP))
#define ACPI_IS_ASCII(c)  ((c) < 0x80)

#endif /* ACPI_USE_SYSTEM_CLIBRARY */

/*
 * ut_copy - Object construction and conversion interfaces
 */

acpi_status
acpi_ut_build_simple_object(
	union acpi_operand_object       *obj,
	union acpi_object               *user_obj,
	u8                              *data_space,
	u32                             *buffer_space_used);

acpi_status
acpi_ut_build_package_object (
	union acpi_operand_object       *obj,
	u8                              *buffer,
	u32                             *space_used);

acpi_status
acpi_ut_copy_ielement_to_eelement (
	u8                              object_type,
	union acpi_operand_object       *source_object,
	union acpi_generic_state        *state,
	void                            *context);

acpi_status
acpi_ut_copy_ielement_to_ielement (
	u8                              object_type,
	union acpi_operand_object       *source_object,
	union acpi_generic_state        *state,
	void                            *context);

acpi_status
acpi_ut_copy_iobject_to_eobject (
	union acpi_operand_object       *obj,
	struct acpi_buffer              *ret_buffer);

acpi_status
acpi_ut_copy_esimple_to_isimple(
	union acpi_object               *user_obj,
	union acpi_operand_object       **return_obj);

acpi_status
acpi_ut_copy_eobject_to_iobject (
	union acpi_object               *obj,
	union acpi_operand_object       **internal_obj);

acpi_status
acpi_ut_copy_isimple_to_isimple (
	union acpi_operand_object       *source_obj,
	union acpi_operand_object       *dest_obj);

acpi_status
acpi_ut_copy_ipackage_to_ipackage (
	union acpi_operand_object       *source_obj,
	union acpi_operand_object       *dest_obj,
	struct acpi_walk_state          *walk_state);

acpi_status
acpi_ut_copy_simple_object (
	union acpi_operand_object       *source_desc,
	union acpi_operand_object       *dest_desc);

acpi_status
acpi_ut_copy_iobject_to_iobject (
	union acpi_operand_object       *source_desc,
	union acpi_operand_object       **dest_desc,
	struct acpi_walk_state          *walk_state);


/*
 * ut_create - Object creation
 */

acpi_status
acpi_ut_update_object_reference (
	union acpi_operand_object       *object,
	u16                             action);


/*
 * ut_debug - Debug interfaces
 */

void
acpi_ut_init_stack_ptr_trace (
	void);

void
acpi_ut_track_stack_ptr (
	void);

void
acpi_ut_trace (
	u32                             line_number,
	struct acpi_debug_print_info    *dbg_info);

void
acpi_ut_trace_ptr (
	u32                             line_number,
	struct acpi_debug_print_info    *dbg_info,
	void                            *pointer);

void
acpi_ut_trace_u32 (
	u32                             line_number,
	struct acpi_debug_print_info    *dbg_info,
	u32                             integer);

void
acpi_ut_trace_str (
	u32                             line_number,
	struct acpi_debug_print_info    *dbg_info,
	char                            *string);

void
acpi_ut_exit (
	u32                             line_number,
	struct acpi_debug_print_info    *dbg_info);

void
acpi_ut_status_exit (
	u32                             line_number,
	struct acpi_debug_print_info    *dbg_info,
	acpi_status                     status);

void
acpi_ut_value_exit (
	u32                             line_number,
	struct acpi_debug_print_info    *dbg_info,
	acpi_integer                    value);

void
acpi_ut_ptr_exit (
	u32                             line_number,
	struct acpi_debug_print_info    *dbg_info,
	u8                              *ptr);

void
acpi_ut_report_info (
	char                            *module_name,
	u32                             line_number,
	u32                             component_id);

void
acpi_ut_report_error (
	char                            *module_name,
	u32                             line_number,
	u32                             component_id);

void
acpi_ut_report_warning (
	char                            *module_name,
	u32                             line_number,
	u32                             component_id);

void
acpi_ut_dump_buffer (
	u8                              *buffer,
	u32                             count,
	u32                             display,
	u32                             component_id);

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_debug_print (
	u32                             requested_debug_level,
	u32                             line_number,
	struct acpi_debug_print_info    *dbg_info,
	char                            *format,
	...) ACPI_PRINTF_LIKE_FUNC;

void ACPI_INTERNAL_VAR_XFACE
acpi_ut_debug_print_raw (
	u32                             requested_debug_level,
	u32                             line_number,
	struct acpi_debug_print_info    *dbg_info,
	char                            *format,
	...) ACPI_PRINTF_LIKE_FUNC;


/*
 * ut_delete - Object deletion
 */

void
acpi_ut_delete_internal_obj (
	union acpi_operand_object       *object);

void
acpi_ut_delete_internal_package_object (
	union acpi_operand_object       *object);

void
acpi_ut_delete_internal_simple_object (
	union acpi_operand_object       *object);

void
acpi_ut_delete_internal_object_list (
	union acpi_operand_object       **obj_list);


/*
 * ut_eval - object evaluation
 */

/* Method name strings */

#define METHOD_NAME__HID        "_HID"
#define METHOD_NAME__CID        "_CID"
#define METHOD_NAME__UID        "_UID"
#define METHOD_NAME__ADR        "_ADR"
#define METHOD_NAME__STA        "_STA"
#define METHOD_NAME__REG        "_REG"
#define METHOD_NAME__SEG        "_SEG"
#define METHOD_NAME__BBN        "_BBN"
#define METHOD_NAME__PRT        "_PRT"
#define METHOD_NAME__CRS        "_CRS"
#define METHOD_NAME__PRS        "_PRS"
#define METHOD_NAME__PRW        "_PRW"


acpi_status
acpi_ut_osi_implementation (
	struct acpi_walk_state          *walk_state);

acpi_status
acpi_ut_evaluate_object (
	struct acpi_namespace_node      *prefix_node,
	char                            *path,
	u32                             expected_return_btypes,
	union acpi_operand_object       **return_desc);

acpi_status
acpi_ut_evaluate_numeric_object (
	char                            *object_name,
	struct acpi_namespace_node      *device_node,
	acpi_integer                    *address);

acpi_status
acpi_ut_execute_HID (
	struct acpi_namespace_node      *device_node,
	struct acpi_device_id           *hid);

acpi_status
acpi_ut_execute_CID (
	struct acpi_namespace_node      *device_node,
	struct acpi_compatible_id_list **return_cid_list);

acpi_status
acpi_ut_execute_STA (
	struct acpi_namespace_node      *device_node,
	u32                             *status_flags);

acpi_status
acpi_ut_execute_UID (
	struct acpi_namespace_node      *device_node,
	struct acpi_device_id           *uid);

acpi_status
acpi_ut_execute_sxds (
	struct acpi_namespace_node      *device_node,
	u8                              *highest);

/*
 * ut_mutex - mutual exclusion interfaces
 */

acpi_status
acpi_ut_mutex_initialize (
	void);

void
acpi_ut_mutex_terminate (
	void);

acpi_status
acpi_ut_create_mutex (
	acpi_mutex_handle               mutex_id);

acpi_status
acpi_ut_delete_mutex (
	acpi_mutex_handle               mutex_id);

acpi_status
acpi_ut_acquire_mutex (
	acpi_mutex_handle               mutex_id);

acpi_status
acpi_ut_release_mutex (
	acpi_mutex_handle               mutex_id);


/*
 * ut_object - internal object create/delete/cache routines
 */

union acpi_operand_object    *
acpi_ut_create_internal_object_dbg (
	char                            *module_name,
	u32                             line_number,
	u32                             component_id,
	acpi_object_type                type);

void *
acpi_ut_allocate_object_desc_dbg (
	char                            *module_name,
	u32                             line_number,
	u32                             component_id);

#define acpi_ut_create_internal_object(t) acpi_ut_create_internal_object_dbg (_THIS_MODULE,__LINE__,_COMPONENT,t)
#define acpi_ut_allocate_object_desc()  acpi_ut_allocate_object_desc_dbg (_THIS_MODULE,__LINE__,_COMPONENT)

void
acpi_ut_delete_object_desc (
	union acpi_operand_object       *object);

u8
acpi_ut_valid_internal_object (
	void                            *object);

union acpi_operand_object *
acpi_ut_create_buffer_object (
	acpi_size                       buffer_size);

union acpi_operand_object *
acpi_ut_create_string_object (
	acpi_size                       string_size);


/*
 * ut_ref_cnt - Object reference count management
 */

void
acpi_ut_add_reference (
	union acpi_operand_object       *object);

void
acpi_ut_remove_reference (
	union acpi_operand_object       *object);

/*
 * ut_size - Object size routines
 */

acpi_status
acpi_ut_get_simple_object_size (
	union acpi_operand_object       *obj,
	acpi_size                       *obj_length);

acpi_status
acpi_ut_get_package_object_size (
	union acpi_operand_object       *obj,
	acpi_size                       *obj_length);

acpi_status
acpi_ut_get_object_size(
	union acpi_operand_object       *obj,
	acpi_size                       *obj_length);

acpi_status
acpi_ut_get_element_length (
	u8                              object_type,
	union acpi_operand_object       *source_object,
	union acpi_generic_state        *state,
	void                            *context);


/*
 * ut_state - Generic state creation/cache routines
 */

void
acpi_ut_push_generic_state (
	union acpi_generic_state        **list_head,
	union acpi_generic_state        *state);

union acpi_generic_state *
acpi_ut_pop_generic_state (
	union acpi_generic_state        **list_head);


union acpi_generic_state *
acpi_ut_create_generic_state (
	void);

struct acpi_thread_state *
acpi_ut_create_thread_state (
	void);

union acpi_generic_state *
acpi_ut_create_update_state (
	union acpi_operand_object       *object,
	u16                             action);

union acpi_generic_state *
acpi_ut_create_pkg_state (
	void                            *internal_object,
	void                            *external_object,
	u16                             index);

acpi_status
acpi_ut_create_update_state_and_push (
	union acpi_operand_object       *object,
	u16                             action,
	union acpi_generic_state        **state_list);

#ifdef ACPI_FUTURE_USAGE
acpi_status
acpi_ut_create_pkg_state_and_push (
	void                            *internal_object,
	void                            *external_object,
	u16                             index,
	union acpi_generic_state        **state_list);
#endif

union acpi_generic_state *
acpi_ut_create_control_state (
	void);

void
acpi_ut_delete_generic_state (
	union acpi_generic_state        *state);

#ifdef ACPI_ENABLE_OBJECT_CACHE
void
acpi_ut_delete_generic_state_cache (
	void);

void
acpi_ut_delete_object_cache (
	void);
#endif

/*
 * utmisc
 */

void
acpi_ut_print_string (
	char                            *string,
	u8                              max_length);

acpi_status
acpi_ut_divide (
	acpi_integer                    in_dividend,
	acpi_integer                    in_divisor,
	acpi_integer                    *out_quotient,
	acpi_integer                    *out_remainder);

acpi_status
acpi_ut_short_divide (
	acpi_integer                    in_dividend,
	u32                             divisor,
	acpi_integer                    *out_quotient,
	u32                             *out_remainder);

u8
acpi_ut_valid_acpi_name (
	u32                             name);

u8
acpi_ut_valid_acpi_character (
	char                            character);

acpi_status
acpi_ut_strtoul64 (
	char                            *string,
	u32                             base,
	acpi_integer                    *ret_integer);

/* Values for Base above (16=Hex, 10=Decimal) */

#define ACPI_ANY_BASE        0

#ifdef ACPI_FUTURE_USAGE
char *
acpi_ut_strupr (
	char                            *src_string);
#endif

u8 *
acpi_ut_get_resource_end_tag (
	union acpi_operand_object       *obj_desc);

u8
acpi_ut_generate_checksum (
	u8                              *buffer,
	u32                             length);

u32
acpi_ut_dword_byte_swap (
	u32                             value);

void
acpi_ut_set_integer_width (
	u8                              revision);

#ifdef ACPI_DEBUG_OUTPUT
void
acpi_ut_display_init_pathname (
	u8                              type,
	struct acpi_namespace_node      *obj_handle,
	char                            *path);

#endif


/*
 * Utalloc - memory allocation and object caching
 */

void *
acpi_ut_acquire_from_cache (
	u32                             list_id);

void
acpi_ut_release_to_cache (
	u32                             list_id,
	void                            *object);

#ifdef ACPI_ENABLE_OBJECT_CACHE
void
acpi_ut_delete_generic_cache (
	u32                             list_id);
#endif

acpi_status
acpi_ut_validate_buffer (
	struct acpi_buffer              *buffer);

acpi_status
acpi_ut_initialize_buffer (
	struct acpi_buffer              *buffer,
	acpi_size                       required_length);


/* Memory allocation functions */

void *
acpi_ut_allocate (
	acpi_size                       size,
	u32                             component,
	char                            *module,
	u32                             line);

void *
acpi_ut_callocate (
	acpi_size                       size,
	u32                             component,
	char                            *module,
	u32                             line);


#ifdef ACPI_DBG_TRACK_ALLOCATIONS

void *
acpi_ut_allocate_and_track (
	acpi_size                       size,
	u32                             component,
	char                            *module,
	u32                             line);

void *
acpi_ut_callocate_and_track (
	acpi_size                       size,
	u32                             component,
	char                            *module,
	u32                             line);

void
acpi_ut_free_and_track (
	void                            *address,
	u32                             component,
	char                            *module,
	u32                             line);

struct acpi_debug_mem_block *
acpi_ut_find_allocation (
	u32                             list_id,
	void                            *allocation);

acpi_status
acpi_ut_track_allocation (
	u32                             list_id,
	struct acpi_debug_mem_block     *address,
	acpi_size                       size,
	u8                              alloc_type,
	u32                             component,
	char                            *module,
	u32                             line);

acpi_status
acpi_ut_remove_allocation (
	u32                             list_id,
	struct acpi_debug_mem_block     *address,
	u32                             component,
	char                            *module,
	u32                             line);

#ifdef ACPI_FUTURE_USAGE
void
acpi_ut_dump_allocation_info (
	void);
#endif

void
acpi_ut_dump_allocations (
	u32                             component,
	char                            *module);
#endif


#endif /* _ACUTILS_H */

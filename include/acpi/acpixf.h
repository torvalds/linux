
/******************************************************************************
 *
 * Name: acpixf.h - External interfaces to the ACPI subsystem
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2010, Intel Corp.
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

#ifndef __ACXFACE_H__
#define __ACXFACE_H__

/* Current ACPICA subsystem version in YYYYMMDD format */

#define ACPI_CA_VERSION                 0x20100428

#include "actypes.h"
#include "actbl.h"

extern u8 acpi_gbl_permanent_mmap;

/*
 * Globals that are publically available, allowing for
 * run time configuration
 */
extern u32 acpi_dbg_level;
extern u32 acpi_dbg_layer;
extern u8 acpi_gbl_enable_interpreter_slack;
extern u8 acpi_gbl_all_methods_serialized;
extern u8 acpi_gbl_create_osi_method;
extern u8 acpi_gbl_leave_wake_gpes_disabled;
extern u8 acpi_gbl_use_default_register_widths;
extern acpi_name acpi_gbl_trace_method_name;
extern u32 acpi_gbl_trace_flags;
extern u8 acpi_gbl_enable_aml_debug_object;
extern u8 acpi_gbl_copy_dsdt_locally;
extern u8 acpi_gbl_truncate_io_addresses;

extern u32 acpi_current_gpe_count;
extern struct acpi_table_fadt acpi_gbl_FADT;

extern u32 acpi_rsdt_forced;
/*
 * Global interfaces
 */
acpi_status
acpi_initialize_tables(struct acpi_table_desc *initial_storage,
		       u32 initial_table_count, u8 allow_resize);

acpi_status __init acpi_initialize_subsystem(void);

acpi_status acpi_enable_subsystem(u32 flags);

acpi_status acpi_initialize_objects(u32 flags);

acpi_status acpi_terminate(void);

#ifdef ACPI_FUTURE_USAGE
acpi_status acpi_subsystem_status(void);
#endif

acpi_status acpi_enable(void);

acpi_status acpi_disable(void);

#ifdef ACPI_FUTURE_USAGE
acpi_status acpi_get_system_info(struct acpi_buffer *ret_buffer);
#endif

const char *acpi_format_exception(acpi_status exception);

acpi_status acpi_purge_cached_objects(void);

/*
 * ACPI Memory management
 */
void *acpi_allocate(u32 size);

void *acpi_callocate(u32 size);

void acpi_free(void *address);

/*
 * ACPI table manipulation interfaces
 */
acpi_status acpi_reallocate_root_table(void);

acpi_status acpi_find_root_pointer(acpi_size *rsdp_address);

acpi_status acpi_load_tables(void);

acpi_status acpi_load_table(struct acpi_table_header *table_ptr);

acpi_status acpi_unload_table_id(acpi_owner_id id);

acpi_status
acpi_get_table_header(acpi_string signature,
		      u32 instance,
		      struct acpi_table_header *out_table_header);

acpi_status
acpi_get_table_with_size(acpi_string signature,
	       u32 instance, struct acpi_table_header **out_table,
	       acpi_size *tbl_size);
acpi_status
acpi_get_table(acpi_string signature,
	       u32 instance, struct acpi_table_header **out_table);

acpi_status
acpi_get_table_by_index(u32 table_index,
			struct acpi_table_header **out_table);

acpi_status
acpi_install_table_handler(acpi_tbl_handler handler, void *context);

acpi_status acpi_remove_table_handler(acpi_tbl_handler handler);

/*
 * Namespace and name interfaces
 */
acpi_status
acpi_walk_namespace(acpi_object_type type,
		    acpi_handle start_object,
		    u32 max_depth,
		    acpi_walk_callback pre_order_visit,
		    acpi_walk_callback post_order_visit,
		    void *context, void **return_value);

acpi_status
acpi_get_devices(const char *HID,
		 acpi_walk_callback user_function,
		 void *context, void **return_value);

acpi_status
acpi_get_name(acpi_handle object,
	      u32 name_type, struct acpi_buffer *ret_path_ptr);

acpi_status
acpi_get_handle(acpi_handle parent,
		acpi_string pathname, acpi_handle * ret_handle);

acpi_status
acpi_attach_data(acpi_handle object, acpi_object_handler handler, void *data);

acpi_status acpi_detach_data(acpi_handle object, acpi_object_handler handler);

acpi_status
acpi_get_data(acpi_handle object, acpi_object_handler handler, void **data);

acpi_status
acpi_debug_trace(char *name, u32 debug_level, u32 debug_layer, u32 flags);

/*
 * Object manipulation and enumeration
 */
acpi_status
acpi_evaluate_object(acpi_handle object,
		     acpi_string pathname,
		     struct acpi_object_list *parameter_objects,
		     struct acpi_buffer *return_object_buffer);

acpi_status
acpi_evaluate_object_typed(acpi_handle object,
			   acpi_string pathname,
			   struct acpi_object_list *external_params,
			   struct acpi_buffer *return_buffer,
			   acpi_object_type return_type);

acpi_status
acpi_get_object_info(acpi_handle object,
		     struct acpi_device_info **return_buffer);

acpi_status acpi_install_method(u8 *buffer);

acpi_status
acpi_get_next_object(acpi_object_type type,
		     acpi_handle parent,
		     acpi_handle child, acpi_handle * out_handle);

acpi_status acpi_get_type(acpi_handle object, acpi_object_type * out_type);

acpi_status acpi_get_id(acpi_handle object, acpi_owner_id * out_type);

acpi_status acpi_get_parent(acpi_handle object, acpi_handle * out_handle);

/*
 * Handler interfaces
 */
acpi_status
acpi_install_initialization_handler(acpi_init_handler handler, u32 function);

acpi_status
acpi_install_fixed_event_handler(u32 acpi_event,
				 acpi_event_handler handler, void *context);

acpi_status
acpi_remove_fixed_event_handler(u32 acpi_event, acpi_event_handler handler);

acpi_status
acpi_install_notify_handler(acpi_handle device,
			    u32 handler_type,
			    acpi_notify_handler handler, void *context);

acpi_status
acpi_remove_notify_handler(acpi_handle device,
			   u32 handler_type, acpi_notify_handler handler);

acpi_status
acpi_install_address_space_handler(acpi_handle device,
				   acpi_adr_space_type space_id,
				   acpi_adr_space_handler handler,
				   acpi_adr_space_setup setup, void *context);

acpi_status
acpi_remove_address_space_handler(acpi_handle device,
				  acpi_adr_space_type space_id,
				  acpi_adr_space_handler handler);

acpi_status
acpi_install_gpe_handler(acpi_handle gpe_device,
			 u32 gpe_number,
			 u32 type, acpi_event_handler address, void *context);

acpi_status
acpi_remove_gpe_handler(acpi_handle gpe_device,
			u32 gpe_number, acpi_event_handler address);

#ifdef ACPI_FUTURE_USAGE
acpi_status acpi_install_exception_handler(acpi_exception_handler handler);
#endif

/*
 * Event interfaces
 */
acpi_status acpi_acquire_global_lock(u16 timeout, u32 * handle);

acpi_status acpi_release_global_lock(u32 handle);

acpi_status acpi_enable_event(u32 event, u32 flags);

acpi_status acpi_disable_event(u32 event, u32 flags);

acpi_status acpi_clear_event(u32 event);

acpi_status acpi_get_event_status(u32 event, acpi_event_status * event_status);

/*
 * GPE Interfaces
 */
acpi_status acpi_set_gpe(acpi_handle gpe_device, u32 gpe_number, u8 action);

acpi_status
acpi_enable_gpe(acpi_handle gpe_device, u32 gpe_number, u8 gpe_type);

acpi_status
acpi_disable_gpe(acpi_handle gpe_device, u32 gpe_number, u8 gpe_type);

acpi_status acpi_clear_gpe(acpi_handle gpe_device, u32 gpe_number);

acpi_status
acpi_get_gpe_status(acpi_handle gpe_device,
		    u32 gpe_number, acpi_event_status *event_status);

acpi_status acpi_disable_all_gpes(void);

acpi_status acpi_enable_all_runtime_gpes(void);

acpi_status acpi_get_gpe_device(u32 gpe_index, acpi_handle *gpe_device);

acpi_status
acpi_install_gpe_block(acpi_handle gpe_device,
		       struct acpi_generic_address *gpe_block_address,
		       u32 register_count, u32 interrupt_number);

acpi_status acpi_remove_gpe_block(acpi_handle gpe_device);

/*
 * Resource interfaces
 */
typedef
acpi_status(*acpi_walk_resource_callback) (struct acpi_resource * resource,
					   void *context);

acpi_status
acpi_get_vendor_resource(acpi_handle device,
			 char *name,
			 struct acpi_vendor_uuid *uuid,
			 struct acpi_buffer *ret_buffer);

acpi_status
acpi_get_current_resources(acpi_handle device, struct acpi_buffer *ret_buffer);

#ifdef ACPI_FUTURE_USAGE
acpi_status
acpi_get_possible_resources(acpi_handle device, struct acpi_buffer *ret_buffer);
#endif

acpi_status
acpi_walk_resources(acpi_handle device,
		    char *name,
		    acpi_walk_resource_callback user_function, void *context);

acpi_status
acpi_set_current_resources(acpi_handle device, struct acpi_buffer *in_buffer);

acpi_status
acpi_get_irq_routing_table(acpi_handle device, struct acpi_buffer *ret_buffer);

acpi_status
acpi_resource_to_address64(struct acpi_resource *resource,
			   struct acpi_resource_address64 *out);

/*
 * Hardware (ACPI device) interfaces
 */
acpi_status acpi_reset(void);

acpi_status acpi_read_bit_register(u32 register_id, u32 *return_value);

acpi_status acpi_write_bit_register(u32 register_id, u32 value);

acpi_status acpi_set_firmware_waking_vector(u32 physical_address);

#if ACPI_MACHINE_WIDTH == 64
acpi_status acpi_set_firmware_waking_vector64(u64 physical_address);
#endif

acpi_status acpi_read(u64 *value, struct acpi_generic_address *reg);

acpi_status acpi_write(u64 value, struct acpi_generic_address *reg);

acpi_status
acpi_get_sleep_type_data(u8 sleep_state, u8 * slp_typ_a, u8 * slp_typ_b);

acpi_status acpi_enter_sleep_state_prep(u8 sleep_state);

acpi_status asmlinkage acpi_enter_sleep_state(u8 sleep_state);

acpi_status asmlinkage acpi_enter_sleep_state_s4bios(void);

acpi_status acpi_leave_sleep_state_prep(u8 sleep_state);

acpi_status acpi_leave_sleep_state(u8 sleep_state);

/*
 * Error/Warning output
 */
void ACPI_INTERNAL_VAR_XFACE
acpi_error(const char *module_name,
	   u32 line_number, const char *format, ...) ACPI_PRINTF_LIKE(3);

void ACPI_INTERNAL_VAR_XFACE
acpi_exception(const char *module_name,
	       u32 line_number,
	       acpi_status status, const char *format, ...) ACPI_PRINTF_LIKE(4);

void ACPI_INTERNAL_VAR_XFACE
acpi_warning(const char *module_name,
	     u32 line_number, const char *format, ...) ACPI_PRINTF_LIKE(3);

void ACPI_INTERNAL_VAR_XFACE
acpi_info(const char *module_name,
	  u32 line_number, const char *format, ...) ACPI_PRINTF_LIKE(3);

/*
 * Debug output
 */
#ifdef ACPI_DEBUG_OUTPUT

void ACPI_INTERNAL_VAR_XFACE
acpi_debug_print(u32 requested_debug_level,
		 u32 line_number,
		 const char *function_name,
		 const char *module_name,
		 u32 component_id, const char *format, ...) ACPI_PRINTF_LIKE(6);

void ACPI_INTERNAL_VAR_XFACE
acpi_debug_print_raw(u32 requested_debug_level,
		     u32 line_number,
		     const char *function_name,
		     const char *module_name,
		     u32 component_id,
		     const char *format, ...) ACPI_PRINTF_LIKE(6);
#endif

#endif				/* __ACXFACE_H__ */

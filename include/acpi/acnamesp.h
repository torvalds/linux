/******************************************************************************
 *
 * Name: acnamesp.h - Namespace subcomponent prototypes and defines
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

#ifndef __ACNAMESP_H__
#define __ACNAMESP_H__

/* To search the entire name space, pass this as search_base */

#define ACPI_NS_ALL                 ((acpi_handle)0)

/*
 * Elements of acpi_ns_properties are bit significant
 * and should be one-to-one with values of acpi_object_type
 */
#define ACPI_NS_NORMAL              0
#define ACPI_NS_NEWSCOPE            1	/* a definition of this type opens a name scope */
#define ACPI_NS_LOCAL               2	/* suppress search of enclosing scopes */

/* Flags for acpi_ns_lookup, acpi_ns_search_and_enter */

#define ACPI_NS_NO_UPSEARCH         0
#define ACPI_NS_SEARCH_PARENT       0x01
#define ACPI_NS_DONT_OPEN_SCOPE     0x02
#define ACPI_NS_NO_PEER_SEARCH      0x04
#define ACPI_NS_ERROR_IF_FOUND      0x08

#define ACPI_NS_WALK_UNLOCK         TRUE
#define ACPI_NS_WALK_NO_UNLOCK      FALSE

/*
 * nsinit - Namespace initialization
 */
acpi_status acpi_ns_initialize_objects(void);

acpi_status acpi_ns_initialize_devices(void);

/*
 * nsload -  Namespace loading
 */
acpi_status acpi_ns_load_namespace(void);

acpi_status
acpi_ns_load_table(struct acpi_table_desc *table_desc,
		   struct acpi_namespace_node *node);

/*
 * nswalk - walk the namespace
 */
acpi_status
acpi_ns_walk_namespace(acpi_object_type type,
		       acpi_handle start_object,
		       u32 max_depth,
		       u8 unlock_before_callback,
		       acpi_walk_callback user_function,
		       void *context, void **return_value);

struct acpi_namespace_node *acpi_ns_get_next_node(acpi_object_type type,
						  struct acpi_namespace_node
						  *parent,
						  struct acpi_namespace_node
						  *child);

/*
 * nsparse - table parsing
 */
acpi_status
acpi_ns_parse_table(struct acpi_table_desc *table_desc,
		    struct acpi_namespace_node *scope);

acpi_status
acpi_ns_one_complete_parse(u8 pass_number, struct acpi_table_desc *table_desc);

/*
 * nsaccess - Top-level namespace access
 */
acpi_status acpi_ns_root_initialize(void);

acpi_status
acpi_ns_lookup(union acpi_generic_state *scope_info,
	       char *name,
	       acpi_object_type type,
	       acpi_interpreter_mode interpreter_mode,
	       u32 flags,
	       struct acpi_walk_state *walk_state,
	       struct acpi_namespace_node **ret_node);

/*
 * nsalloc - Named object allocation/deallocation
 */
struct acpi_namespace_node *acpi_ns_create_node(u32 name);

void acpi_ns_delete_node(struct acpi_namespace_node *node);

void
acpi_ns_delete_namespace_subtree(struct acpi_namespace_node *parent_handle);

void acpi_ns_delete_namespace_by_owner(acpi_owner_id owner_id);

void acpi_ns_detach_object(struct acpi_namespace_node *node);

void acpi_ns_delete_children(struct acpi_namespace_node *parent);

int acpi_ns_compare_names(char *name1, char *name2);

/*
 * nsdump - Namespace dump/print utilities
 */
#ifdef	ACPI_FUTURE_USAGE
void acpi_ns_dump_tables(acpi_handle search_base, u32 max_depth);
#endif				/* ACPI_FUTURE_USAGE */

void acpi_ns_dump_entry(acpi_handle handle, u32 debug_level);

void
acpi_ns_dump_pathname(acpi_handle handle, char *msg, u32 level, u32 component);

void acpi_ns_print_pathname(u32 num_segments, char *pathname);

acpi_status
acpi_ns_dump_one_object(acpi_handle obj_handle,
			u32 level, void *context, void **return_value);

#ifdef	ACPI_FUTURE_USAGE
void
acpi_ns_dump_objects(acpi_object_type type,
		     u8 display_type,
		     u32 max_depth,
		     acpi_owner_id owner_id, acpi_handle start_handle);
#endif				/* ACPI_FUTURE_USAGE */

/*
 * nseval - Namespace evaluation functions
 */
acpi_status acpi_ns_evaluate_by_handle(struct acpi_parameter_info *info);

acpi_status
acpi_ns_evaluate_by_name(char *pathname, struct acpi_parameter_info *info);

acpi_status
acpi_ns_evaluate_relative(char *pathname, struct acpi_parameter_info *info);

/*
 * nsnames - Name and Scope manipulation
 */
u32 acpi_ns_opens_scope(acpi_object_type type);

char *acpi_ns_get_external_pathname(struct acpi_namespace_node *node);

char *acpi_ns_name_of_current_scope(struct acpi_walk_state *walk_state);

acpi_status
acpi_ns_handle_to_pathname(acpi_handle target_handle,
			   struct acpi_buffer *buffer);

u8
acpi_ns_pattern_match(struct acpi_namespace_node *obj_node, char *search_for);

acpi_status
acpi_ns_get_node_by_path(char *external_pathname,
			 struct acpi_namespace_node *in_prefix_node,
			 u32 flags, struct acpi_namespace_node **out_node);

acpi_size acpi_ns_get_pathname_length(struct acpi_namespace_node *node);

/*
 * nsobject - Object management for namespace nodes
 */
acpi_status
acpi_ns_attach_object(struct acpi_namespace_node *node,
		      union acpi_operand_object *object, acpi_object_type type);

union acpi_operand_object *acpi_ns_get_attached_object(struct
						       acpi_namespace_node
						       *node);

union acpi_operand_object *acpi_ns_get_secondary_object(union
							acpi_operand_object
							*obj_desc);

acpi_status
acpi_ns_attach_data(struct acpi_namespace_node *node,
		    acpi_object_handler handler, void *data);

acpi_status
acpi_ns_detach_data(struct acpi_namespace_node *node,
		    acpi_object_handler handler);

acpi_status
acpi_ns_get_attached_data(struct acpi_namespace_node *node,
			  acpi_object_handler handler, void **data);

/*
 * nssearch - Namespace searching and entry
 */
acpi_status
acpi_ns_search_and_enter(u32 entry_name,
			 struct acpi_walk_state *walk_state,
			 struct acpi_namespace_node *node,
			 acpi_interpreter_mode interpreter_mode,
			 acpi_object_type type,
			 u32 flags, struct acpi_namespace_node **ret_node);

acpi_status
acpi_ns_search_node(u32 entry_name,
		    struct acpi_namespace_node *node,
		    acpi_object_type type,
		    struct acpi_namespace_node **ret_node);

void
acpi_ns_install_node(struct acpi_walk_state *walk_state,
		     struct acpi_namespace_node *parent_node,
		     struct acpi_namespace_node *node, acpi_object_type type);

/*
 * nsutils - Utility functions
 */
u8 acpi_ns_valid_root_prefix(char prefix);

acpi_object_type acpi_ns_get_type(struct acpi_namespace_node *node);

u32 acpi_ns_local(acpi_object_type type);

void
acpi_ns_report_error(char *module_name,
		     u32 line_number,
		     u32 component_id,
		     char *internal_name, acpi_status lookup_status);

void
acpi_ns_report_method_error(char *module_name,
			    u32 line_number,
			    u32 component_id,
			    char *message,
			    struct acpi_namespace_node *node,
			    char *path, acpi_status lookup_status);

void acpi_ns_print_node_pathname(struct acpi_namespace_node *node, char *msg);

acpi_status acpi_ns_build_internal_name(struct acpi_namestring_info *info);

void acpi_ns_get_internal_name_length(struct acpi_namestring_info *info);

acpi_status acpi_ns_internalize_name(char *dotted_name, char **converted_name);

acpi_status
acpi_ns_externalize_name(u32 internal_name_length,
			 char *internal_name,
			 u32 * converted_name_length, char **converted_name);

struct acpi_namespace_node *acpi_ns_map_handle_to_node(acpi_handle handle);

acpi_handle acpi_ns_convert_entry_to_handle(struct acpi_namespace_node *node);

void acpi_ns_terminate(void);

struct acpi_namespace_node *acpi_ns_get_parent_node(struct acpi_namespace_node
						    *node);

struct acpi_namespace_node *acpi_ns_get_next_valid_node(struct
							acpi_namespace_node
							*node);

#endif				/* __ACNAMESP_H__ */

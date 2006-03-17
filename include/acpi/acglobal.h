/******************************************************************************
 *
 * Name: acglobal.h - Declarations for global variables
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

#ifndef __ACGLOBAL_H__
#define __ACGLOBAL_H__

/*
 * Ensure that the globals are actually defined and initialized only once.
 *
 * The use of these macros allows a single list of globals (here) in order
 * to simplify maintenance of the code.
 */
#ifdef DEFINE_ACPI_GLOBALS
#define ACPI_EXTERN
#define ACPI_INIT_GLOBAL(a,b) a=b
#else
#define ACPI_EXTERN extern
#define ACPI_INIT_GLOBAL(a,b) a
#endif

/*
 * Keep local copies of these FADT-based registers.  NOTE: These globals
 * are first in this file for alignment reasons on 64-bit systems.
 */
ACPI_EXTERN struct acpi_generic_address acpi_gbl_xpm1a_enable;
ACPI_EXTERN struct acpi_generic_address acpi_gbl_xpm1b_enable;

/*****************************************************************************
 *
 * Debug support
 *
 ****************************************************************************/

/* Runtime configuration of debug print levels */

extern u32 acpi_dbg_level;
extern u32 acpi_dbg_layer;

/* Procedure nesting level for debug output */

extern u32 acpi_gbl_nesting_level;

/* Support for dynamic control method tracing mechanism */

ACPI_EXTERN u32 acpi_gbl_original_dbg_level;
ACPI_EXTERN u32 acpi_gbl_original_dbg_layer;
ACPI_EXTERN acpi_name acpi_gbl_trace_method_name;
ACPI_EXTERN u32 acpi_gbl_trace_dbg_level;
ACPI_EXTERN u32 acpi_gbl_trace_dbg_layer;
ACPI_EXTERN u32 acpi_gbl_trace_flags;

/*****************************************************************************
 *
 * Runtime configuration (static defaults that can be overriden at runtime)
 *
 ****************************************************************************/

/*
 * Enable "slack" in the AML interpreter?  Default is FALSE, and the
 * interpreter strictly follows the ACPI specification.  Setting to TRUE
 * allows the interpreter to ignore certain errors and/or bad AML constructs.
 *
 * Currently, these features are enabled by this flag:
 *
 * 1) Allow "implicit return" of last value in a control method
 * 2) Allow access beyond the end of an operation region
 * 3) Allow access to uninitialized locals/args (auto-init to integer 0)
 * 4) Allow ANY object type to be a source operand for the Store() operator
 * 5) Allow unresolved references (invalid target name) in package objects
 */
ACPI_EXTERN u8 ACPI_INIT_GLOBAL(acpi_gbl_enable_interpreter_slack, FALSE);

/*
 * Automatically serialize ALL control methods? Default is FALSE, meaning
 * to use the Serialized/not_serialized method flags on a per method basis.
 * Only change this if the ASL code is poorly written and cannot handle
 * reentrancy even though methods are marked "not_serialized".
 */
ACPI_EXTERN u8 ACPI_INIT_GLOBAL(acpi_gbl_all_methods_serialized, FALSE);

/*
 * Create the predefined _OSI method in the namespace? Default is TRUE
 * because ACPI CA is fully compatible with other ACPI implementations.
 * Changing this will revert ACPI CA (and machine ASL) to pre-OSI behavior.
 */
ACPI_EXTERN u8 ACPI_INIT_GLOBAL(acpi_gbl_create_osi_method, TRUE);

/*
 * Disable wakeup GPEs during runtime? Default is TRUE because WAKE and
 * RUNTIME GPEs should never be shared, and WAKE GPEs should typically only
 * be enabled just before going to sleep.
 */
ACPI_EXTERN u8 ACPI_INIT_GLOBAL(acpi_gbl_leave_wake_gpes_disabled, TRUE);

/*****************************************************************************
 *
 * ACPI Table globals
 *
 ****************************************************************************/

/*
 * Table pointers.
 * Although these pointers are somewhat redundant with the global acpi_table,
 * they are convenient because they are typed pointers.
 *
 * These tables are single-table only; meaning that there can be at most one
 * of each in the system.  Each global points to the actual table.
 */
ACPI_EXTERN u32 acpi_gbl_table_flags;
ACPI_EXTERN u32 acpi_gbl_rsdt_table_count;
ACPI_EXTERN struct rsdp_descriptor *acpi_gbl_RSDP;
ACPI_EXTERN XSDT_DESCRIPTOR *acpi_gbl_XSDT;
ACPI_EXTERN FADT_DESCRIPTOR *acpi_gbl_FADT;
ACPI_EXTERN struct acpi_table_header *acpi_gbl_DSDT;
ACPI_EXTERN FACS_DESCRIPTOR *acpi_gbl_FACS;
ACPI_EXTERN struct acpi_common_facs acpi_gbl_common_fACS;
/*
 * Since there may be multiple SSDTs and PSDTs, a single pointer is not
 * sufficient; Therefore, there isn't one!
 */

/* The root table can be either an RSDT or an XSDT */

ACPI_EXTERN u8 acpi_gbl_root_table_type;
#define     ACPI_TABLE_TYPE_RSDT        'R'
#define     ACPI_TABLE_TYPE_XSDT        'X'

/*
 * Handle both ACPI 1.0 and ACPI 2.0 Integer widths:
 * If we are executing a method that exists in a 32-bit ACPI table,
 * use only the lower 32 bits of the (internal) 64-bit Integer.
 */
ACPI_EXTERN u8 acpi_gbl_integer_bit_width;
ACPI_EXTERN u8 acpi_gbl_integer_byte_width;
ACPI_EXTERN u8 acpi_gbl_integer_nybble_width;

/*
 * ACPI Table info arrays
 */
extern struct acpi_table_list acpi_gbl_table_lists[NUM_ACPI_TABLE_TYPES];
extern struct acpi_table_support acpi_gbl_table_data[NUM_ACPI_TABLE_TYPES];

/*
 * Predefined mutex objects.  This array contains the
 * actual OS mutex handles, indexed by the local ACPI_MUTEX_HANDLEs.
 * (The table maps local handles to the real OS handles)
 */
ACPI_EXTERN struct acpi_mutex_info acpi_gbl_mutex_info[NUM_MUTEX];

/*****************************************************************************
 *
 * Miscellaneous globals
 *
 ****************************************************************************/

#ifdef ACPI_DBG_TRACK_ALLOCATIONS

/* Lists for tracking memory allocations */

ACPI_EXTERN struct acpi_memory_list *acpi_gbl_global_list;
ACPI_EXTERN struct acpi_memory_list *acpi_gbl_ns_node_list;
#endif

/* Object caches */

ACPI_EXTERN acpi_cache_t *acpi_gbl_namespace_cache;
ACPI_EXTERN acpi_cache_t *acpi_gbl_state_cache;
ACPI_EXTERN acpi_cache_t *acpi_gbl_ps_node_cache;
ACPI_EXTERN acpi_cache_t *acpi_gbl_ps_node_ext_cache;
ACPI_EXTERN acpi_cache_t *acpi_gbl_operand_cache;

/* Global handlers */

ACPI_EXTERN struct acpi_object_notify_handler acpi_gbl_device_notify;
ACPI_EXTERN struct acpi_object_notify_handler acpi_gbl_system_notify;
ACPI_EXTERN acpi_exception_handler acpi_gbl_exception_handler;
ACPI_EXTERN acpi_init_handler acpi_gbl_init_handler;
ACPI_EXTERN struct acpi_walk_state *acpi_gbl_breakpoint_walk;
ACPI_EXTERN acpi_handle acpi_gbl_global_lock_semaphore;

/* Misc */

ACPI_EXTERN u32 acpi_gbl_global_lock_thread_count;
ACPI_EXTERN u32 acpi_gbl_original_mode;
ACPI_EXTERN u32 acpi_gbl_rsdp_original_location;
ACPI_EXTERN u32 acpi_gbl_ns_lookup_count;
ACPI_EXTERN u32 acpi_gbl_ps_find_count;
ACPI_EXTERN u32 acpi_gbl_owner_id_mask[ACPI_NUM_OWNERID_MASKS];
ACPI_EXTERN u16 acpi_gbl_pm1_enable_register_save;
ACPI_EXTERN u16 acpi_gbl_global_lock_handle;
ACPI_EXTERN u8 acpi_gbl_last_owner_id_index;
ACPI_EXTERN u8 acpi_gbl_next_owner_id_offset;
ACPI_EXTERN u8 acpi_gbl_debugger_configuration;
ACPI_EXTERN u8 acpi_gbl_global_lock_acquired;
ACPI_EXTERN u8 acpi_gbl_step_to_next_call;
ACPI_EXTERN u8 acpi_gbl_acpi_hardware_present;
ACPI_EXTERN u8 acpi_gbl_global_lock_present;
ACPI_EXTERN u8 acpi_gbl_events_initialized;
ACPI_EXTERN u8 acpi_gbl_system_awake_and_running;

extern u8 acpi_gbl_shutdown;
extern u32 acpi_gbl_startup_flags;
extern const u8 acpi_gbl_decode_to8bit[8];
extern const char *acpi_gbl_sleep_state_names[ACPI_S_STATE_COUNT];
extern const char *acpi_gbl_highest_dstate_names[4];
extern const struct acpi_opcode_info acpi_gbl_aml_op_info[AML_NUM_OPCODES];
extern const char *acpi_gbl_region_types[ACPI_NUM_PREDEFINED_REGIONS];
extern const char *acpi_gbl_valid_osi_strings[ACPI_NUM_OSI_STRINGS];

/*****************************************************************************
 *
 * Namespace globals
 *
 ****************************************************************************/

#define NUM_NS_TYPES                    ACPI_TYPE_INVALID+1

#if !defined (ACPI_NO_METHOD_EXECUTION) || defined (ACPI_CONSTANT_EVAL_ONLY)
#define NUM_PREDEFINED_NAMES            10
#else
#define NUM_PREDEFINED_NAMES            9
#endif

ACPI_EXTERN struct acpi_namespace_node acpi_gbl_root_node_struct;
ACPI_EXTERN struct acpi_namespace_node *acpi_gbl_root_node;
ACPI_EXTERN struct acpi_namespace_node *acpi_gbl_fadt_gpe_device;

extern const u8 acpi_gbl_ns_properties[NUM_NS_TYPES];
extern const struct acpi_predefined_names
    acpi_gbl_pre_defined_names[NUM_PREDEFINED_NAMES];

#ifdef ACPI_DEBUG_OUTPUT
ACPI_EXTERN u32 acpi_gbl_current_node_count;
ACPI_EXTERN u32 acpi_gbl_current_node_size;
ACPI_EXTERN u32 acpi_gbl_max_concurrent_node_count;
ACPI_EXTERN acpi_size acpi_gbl_entry_stack_pointer;
ACPI_EXTERN acpi_size acpi_gbl_lowest_stack_pointer;
ACPI_EXTERN u32 acpi_gbl_deepest_nesting;
#endif

/*****************************************************************************
 *
 * Interpreter globals
 *
 ****************************************************************************/

ACPI_EXTERN struct acpi_thread_state *acpi_gbl_current_walk_list;

/* Control method single step flag */

ACPI_EXTERN u8 acpi_gbl_cm_single_step;

/*****************************************************************************
 *
 * Parser globals
 *
 ****************************************************************************/

ACPI_EXTERN union acpi_parse_object *acpi_gbl_parsed_namespace_root;

/*****************************************************************************
 *
 * Hardware globals
 *
 ****************************************************************************/

extern struct acpi_bit_register_info
    acpi_gbl_bit_register_info[ACPI_NUM_BITREG];
ACPI_EXTERN u8 acpi_gbl_sleep_type_a;
ACPI_EXTERN u8 acpi_gbl_sleep_type_b;

/*****************************************************************************
 *
 * Event and GPE globals
 *
 ****************************************************************************/

extern struct acpi_fixed_event_info
    acpi_gbl_fixed_event_info[ACPI_NUM_FIXED_EVENTS];
ACPI_EXTERN struct acpi_fixed_event_handler
    acpi_gbl_fixed_event_handlers[ACPI_NUM_FIXED_EVENTS];
ACPI_EXTERN struct acpi_gpe_xrupt_info *acpi_gbl_gpe_xrupt_list_head;
ACPI_EXTERN struct acpi_gpe_block_info
    *acpi_gbl_gpe_fadt_blocks[ACPI_MAX_GPE_BLOCKS];
ACPI_EXTERN acpi_handle acpi_gbl_gpe_lock;

/*****************************************************************************
 *
 * Debugger globals
 *
 ****************************************************************************/

ACPI_EXTERN u8 acpi_gbl_db_output_flags;

#ifdef ACPI_DISASSEMBLER

ACPI_EXTERN u8 acpi_gbl_db_opt_disasm;
ACPI_EXTERN u8 acpi_gbl_db_opt_verbose;
#endif

#ifdef ACPI_DEBUGGER

extern u8 acpi_gbl_method_executing;
extern u8 acpi_gbl_abort_method;
extern u8 acpi_gbl_db_terminate_threads;

ACPI_EXTERN int optind;
ACPI_EXTERN char *optarg;

ACPI_EXTERN u8 acpi_gbl_db_opt_tables;
ACPI_EXTERN u8 acpi_gbl_db_opt_stats;
ACPI_EXTERN u8 acpi_gbl_db_opt_ini_methods;

ACPI_EXTERN char *acpi_gbl_db_args[ACPI_DEBUGGER_MAX_ARGS];
ACPI_EXTERN char acpi_gbl_db_line_buf[80];
ACPI_EXTERN char acpi_gbl_db_parsed_buf[80];
ACPI_EXTERN char acpi_gbl_db_scope_buf[40];
ACPI_EXTERN char acpi_gbl_db_debug_filename[40];
ACPI_EXTERN u8 acpi_gbl_db_output_to_file;
ACPI_EXTERN char *acpi_gbl_db_buffer;
ACPI_EXTERN char *acpi_gbl_db_filename;
ACPI_EXTERN u32 acpi_gbl_db_debug_level;
ACPI_EXTERN u32 acpi_gbl_db_console_debug_level;
ACPI_EXTERN struct acpi_table_header *acpi_gbl_db_table_ptr;
ACPI_EXTERN struct acpi_namespace_node *acpi_gbl_db_scope_node;

/*
 * Statistic globals
 */
ACPI_EXTERN u16 acpi_gbl_obj_type_count[ACPI_TYPE_NS_NODE_MAX + 1];
ACPI_EXTERN u16 acpi_gbl_node_type_count[ACPI_TYPE_NS_NODE_MAX + 1];
ACPI_EXTERN u16 acpi_gbl_obj_type_count_misc;
ACPI_EXTERN u16 acpi_gbl_node_type_count_misc;
ACPI_EXTERN u32 acpi_gbl_num_nodes;
ACPI_EXTERN u32 acpi_gbl_num_objects;

ACPI_EXTERN u32 acpi_gbl_size_of_parse_tree;
ACPI_EXTERN u32 acpi_gbl_size_of_method_trees;
ACPI_EXTERN u32 acpi_gbl_size_of_node_entries;
ACPI_EXTERN u32 acpi_gbl_size_of_acpi_objects;

#endif				/* ACPI_DEBUGGER */

#endif				/* __ACGLOBAL_H__ */

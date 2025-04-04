/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acconfig.h - Global configuration constants
 *
 * Copyright (C) 2000 - 2025, Intel Corp.
 *
 *****************************************************************************/

#ifndef _ACCONFIG_H
#define _ACCONFIG_H

/******************************************************************************
 *
 * Configuration options
 *
 *****************************************************************************/

/*
 * ACPI_DEBUG_OUTPUT    - This switch enables all the debug facilities of the
 *                        ACPI subsystem.  This includes the DEBUG_PRINT output
 *                        statements.  When disabled, all DEBUG_PRINT
 *                        statements are compiled out.
 *
 * ACPI_APPLICATION     - Use this switch if the subsystem is going to be run
 *                        at the application level.
 *
 */

/*
 * OS name, used for the _OS object.  The _OS object is essentially obsolete,
 * but there is a large base of ASL/AML code in existing machines that check
 * for the string below.  The use of this string usually guarantees that
 * the ASL will execute down the most tested code path.  Also, there is some
 * code that will not execute the _OSI method unless _OS matches the string
 * below.  Therefore, change this string at your own risk.
 */
#define ACPI_OS_NAME                    "Microsoft Windows NT"

/* Maximum objects in the various object caches */

#define ACPI_MAX_STATE_CACHE_DEPTH      96	/* State objects */
#define ACPI_MAX_PARSE_CACHE_DEPTH      96	/* Parse tree objects */
#define ACPI_MAX_EXTPARSE_CACHE_DEPTH   96	/* Parse tree objects */
#define ACPI_MAX_OBJECT_CACHE_DEPTH     96	/* Interpreter operand objects */
#define ACPI_MAX_NAMESPACE_CACHE_DEPTH  96	/* Namespace objects */
#define ACPI_MAX_COMMENT_CACHE_DEPTH    96	/* Comments for the -ca option */

/*
 * Should the subsystem abort the loading of an ACPI table if the
 * table checksum is incorrect?
 */
#ifndef ACPI_CHECKSUM_ABORT
#define ACPI_CHECKSUM_ABORT             FALSE
#endif

/*
 * Generate a version of ACPICA that only supports "reduced hardware"
 * platforms (as defined in ACPI 5.0). Set to TRUE to generate a specialized
 * version of ACPICA that ONLY supports the ACPI 5.0 "reduced hardware"
 * model. In other words, no ACPI hardware is supported.
 *
 * If TRUE, this means no support for the following:
 *      PM Event and Control registers
 *      SCI interrupt (and handler)
 *      Fixed Events
 *      General Purpose Events (GPEs)
 *      Global Lock
 *      ACPI PM timer
 */
#ifndef ACPI_REDUCED_HARDWARE
#define ACPI_REDUCED_HARDWARE           FALSE
#endif

/******************************************************************************
 *
 * Subsystem Constants
 *
 *****************************************************************************/

/* Version of ACPI supported */

#define ACPI_CA_SUPPORT_LEVEL           5

/* Maximum count for a semaphore object */

#define ACPI_MAX_SEMAPHORE_COUNT        256

/* Maximum object reference count (detects object deletion issues) */

#define ACPI_MAX_REFERENCE_COUNT        0x4000

/* Default page size for use in mapping memory for operation regions */

#define ACPI_DEFAULT_PAGE_SIZE          4096	/* Must be power of 2 */

/* owner_id tracking. 128 entries allows for 4095 owner_ids */

#define ACPI_NUM_OWNERID_MASKS          128

/* Size of the root table array is increased by this increment */

#define ACPI_ROOT_TABLE_SIZE_INCREMENT  4

/* Maximum sleep allowed via Sleep() operator */

#define ACPI_MAX_SLEEP                  2000	/* 2000 millisec == two seconds */

/* Address Range lists are per-space_id (Memory and I/O only) */

#define ACPI_ADDRESS_RANGE_MAX          2

/* Maximum time (default 30s) of While() loops before abort */

#define ACPI_MAX_LOOP_TIMEOUT           30

/******************************************************************************
 *
 * ACPI Specification constants (Do not change unless the specification changes)
 *
 *****************************************************************************/

/* Method info (in WALK_STATE), containing local variables and arguments */

#define ACPI_METHOD_NUM_LOCALS          8
#define ACPI_METHOD_MAX_LOCAL           7

#define ACPI_METHOD_NUM_ARGS            7
#define ACPI_METHOD_MAX_ARG             6

/*
 * Operand Stack (in WALK_STATE), Must be large enough to contain METHOD_MAX_ARG
 */
#define ACPI_OBJ_NUM_OPERANDS           8
#define ACPI_OBJ_MAX_OPERAND            7

/* Number of elements in the Result Stack frame, can be an arbitrary value */

#define ACPI_RESULTS_FRAME_OBJ_NUM      8

/*
 * Maximal number of elements the Result Stack can contain,
 * it may be an arbitrary value not exceeding the types of
 * result_size and result_count (now u8).
 */
#define ACPI_RESULTS_OBJ_NUM_MAX        255

/* Constants used in searching for the RSDP in low memory */

#define ACPI_EBDA_PTR_LOCATION          0x0000040E	/* Physical Address */
#define ACPI_EBDA_PTR_LENGTH            2
#define ACPI_EBDA_WINDOW_SIZE           1024
#define ACPI_HI_RSDP_WINDOW_BASE        0x000E0000	/* Physical Address */
#define ACPI_HI_RSDP_WINDOW_SIZE        0x00020000
#define ACPI_RSDP_SCAN_STEP             16

/* Operation regions */

#define ACPI_USER_REGION_BEGIN          0x80

/* Maximum space_ids for Operation Regions */

#define ACPI_MAX_ADDRESS_SPACE          255
#define ACPI_NUM_DEFAULT_SPACES         4

/* Array sizes.  Used for range checking also */

#define ACPI_MAX_MATCH_OPCODE           5

/* RSDP checksums */

#define ACPI_RSDP_CHECKSUM_LENGTH       20
#define ACPI_RSDP_XCHECKSUM_LENGTH      36

/*
 * SMBus, GSBus and IPMI buffer sizes. All have a 2-byte header,
 * containing both Status and Length.
 */
#define ACPI_SERIAL_HEADER_SIZE         2	/* Common for below. Status and Length fields */

#define ACPI_SMBUS_DATA_SIZE            32
#define ACPI_SMBUS_BUFFER_SIZE          ACPI_SERIAL_HEADER_SIZE + ACPI_SMBUS_DATA_SIZE

#define ACPI_IPMI_DATA_SIZE             64
#define ACPI_IPMI_BUFFER_SIZE           ACPI_SERIAL_HEADER_SIZE + ACPI_IPMI_DATA_SIZE

#define ACPI_MAX_GSBUS_DATA_SIZE        255
#define ACPI_MAX_GSBUS_BUFFER_SIZE      ACPI_SERIAL_HEADER_SIZE + ACPI_MAX_GSBUS_DATA_SIZE

#define ACPI_PRM_INPUT_BUFFER_SIZE      26

#define ACPI_FFH_INPUT_BUFFER_SIZE      256

/* _sx_d and _sx_w control methods */

#define ACPI_NUM_sx_d_METHODS           4
#define ACPI_NUM_sx_w_METHODS           5

/******************************************************************************
 *
 * Miscellaneous constants
 *
 *****************************************************************************/

/* UUID constants */

#define UUID_BUFFER_LENGTH          16	/* Length of UUID in memory */
#define UUID_STRING_LENGTH          36	/* Total length of a UUID string */

/* Positions for required hyphens (dashes) in UUID strings */

#define UUID_HYPHEN1_OFFSET         8
#define UUID_HYPHEN2_OFFSET         13
#define UUID_HYPHEN3_OFFSET         18
#define UUID_HYPHEN4_OFFSET         23

/******************************************************************************
 *
 * ACPI AML Debugger
 *
 *****************************************************************************/

#define ACPI_DEBUGGER_MAX_ARGS          ACPI_METHOD_NUM_ARGS + 4	/* Max command line arguments */
#define ACPI_DB_LINE_BUFFER_SIZE        512

#define ACPI_DEBUGGER_COMMAND_PROMPT    '-'
#define ACPI_DEBUGGER_EXECUTE_PROMPT    '%'

#endif				/* _ACCONFIG_H */

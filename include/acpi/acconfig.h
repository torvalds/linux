/******************************************************************************
 *
 * Name: acconfig.h - Global configuration constants
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
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

/*
 * Should the subsystem abort the loading of an ACPI table if the
 * table checksum is incorrect?
 */
#define ACPI_CHECKSUM_ABORT             FALSE

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
 *      FACS table (Waking vectors and Global Lock)
 */
#define ACPI_REDUCED_HARDWARE           FALSE

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

#define ACPI_MAX_REFERENCE_COUNT        0x1000

/* Default page size for use in mapping memory for operation regions */

#define ACPI_DEFAULT_PAGE_SIZE          4096	/* Must be power of 2 */

/* owner_id tracking. 8 entries allows for 255 owner_ids */

#define ACPI_NUM_OWNERID_MASKS          8

/* Size of the root table array is increased by this increment */

#define ACPI_ROOT_TABLE_SIZE_INCREMENT  4

/* Maximum number of While() loop iterations before forced abort */

#define ACPI_MAX_LOOP_ITERATIONS        0xFFFF

/* Maximum sleep allowed via Sleep() operator */

#define ACPI_MAX_SLEEP                  2000	/* Two seconds */

/* Address Range lists are per-space_id (Memory and I/O only) */

#define ACPI_ADDRESS_RANGE_MAX          2

/******************************************************************************
 *
 * ACPI Specification constants (Do not change unless the specification changes)
 *
 *****************************************************************************/

/* Number of distinct GPE register blocks and register width */

#define ACPI_MAX_GPE_BLOCKS             2
#define ACPI_GPE_REGISTER_WIDTH         8

/* Method info (in WALK_STATE), containing local variables and argumetns */

#define ACPI_METHOD_NUM_LOCALS          8
#define ACPI_METHOD_MAX_LOCAL           7

#define ACPI_METHOD_NUM_ARGS            7
#define ACPI_METHOD_MAX_ARG             6

/* Length of _HID, _UID, _CID, and UUID values */

#define ACPI_DEVICE_ID_LENGTH           0x09
#define ACPI_MAX_CID_LENGTH             48
#define ACPI_UUID_LENGTH                16

/*
 * Operand Stack (in WALK_STATE), Must be large enough to contain METHOD_MAX_ARG
 */
#define ACPI_OBJ_NUM_OPERANDS           8
#define ACPI_OBJ_MAX_OPERAND            7

/* Number of elements in the Result Stack frame, can be an arbitrary value */

#define ACPI_RESULTS_FRAME_OBJ_NUM      8

/*
 * Maximal number of elements the Result Stack can contain,
 * it may be an arbitray value not exceeding the types of
 * result_size and result_count (now u8).
 */
#define ACPI_RESULTS_OBJ_NUM_MAX        255

/* Names within the namespace are 4 bytes long */

#define ACPI_NAME_SIZE                  4
#define ACPI_PATH_SEGMENT_LENGTH        5	/* 4 chars for name + 1 char for separator */
#define ACPI_PATH_SEPARATOR             '.'

/* Sizes for ACPI table headers */

#define ACPI_OEM_ID_SIZE                6
#define ACPI_OEM_TABLE_ID_SIZE          8

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

/* Array sizes.  Used for range checking also */

#define ACPI_MAX_MATCH_OPCODE           5

/* RSDP checksums */

#define ACPI_RSDP_CHECKSUM_LENGTH       20
#define ACPI_RSDP_XCHECKSUM_LENGTH      36

/* SMBus, GSBus and IPMI bidirectional buffer size */

#define ACPI_SMBUS_BUFFER_SIZE          34
#define ACPI_GSBUS_BUFFER_SIZE          34
#define ACPI_IPMI_BUFFER_SIZE           66

/* _sx_d and _sx_w control methods */

#define ACPI_NUM_sx_d_METHODS           4
#define ACPI_NUM_sx_w_METHODS           5

/******************************************************************************
 *
 * ACPI AML Debugger
 *
 *****************************************************************************/

#define ACPI_DEBUGGER_MAX_ARGS          8	/* Must be max method args + 1 */

#define ACPI_DEBUGGER_COMMAND_PROMPT    '-'
#define ACPI_DEBUGGER_EXECUTE_PROMPT    '%'

#endif				/* _ACCONFIG_H */

/******************************************************************************
 *
 * Name: actypes.h - Common data types for the entire ACPI subsystem
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

#ifndef __ACTYPES_H__
#define __ACTYPES_H__

/*! [Begin] no source code translation (keep the typedefs) */

/*
 * Data type ranges
 * Note: These macros are designed to be compiler independent as well as
 * working around problems that some 32-bit compilers have with 64-bit
 * constants.
 */
#define ACPI_UINT8_MAX                  (UINT8) (~((UINT8)  0))	/* 0xFF               */
#define ACPI_UINT16_MAX                 (UINT16)(~((UINT16) 0))	/* 0xFFFF             */
#define ACPI_UINT32_MAX                 (UINT32)(~((UINT32) 0))	/* 0xFFFFFFFF         */
#define ACPI_UINT64_MAX                 (UINT64)(~((UINT64) 0))	/* 0xFFFFFFFFFFFFFFFF */
#define ACPI_ASCII_MAX                  0x7F

#ifdef DEFINE_ALTERNATE_TYPES
/*
 * Types used only in translated source, defined here to enable
 * cross-platform compilation only.
 */
typedef int s32;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef COMPILER_DEPENDENT_UINT64 u64;

#endif

/*
 * Data types - Fixed across all compilation models (16/32/64)
 *
 * BOOLEAN          Logical Boolean.
 * INT8             8-bit  (1 byte) signed value
 * UINT8            8-bit  (1 byte) unsigned value
 * INT16            16-bit (2 byte) signed value
 * UINT16           16-bit (2 byte) unsigned value
 * INT32            32-bit (4 byte) signed value
 * UINT32           32-bit (4 byte) unsigned value
 * INT64            64-bit (8 byte) signed value
 * UINT64           64-bit (8 byte) unsigned value
 * ACPI_NATIVE_INT  32-bit on IA-32, 64-bit on IA-64 signed value
 * ACPI_NATIVE_UINT 32-bit on IA-32, 64-bit on IA-64 unsigned value
 */

#ifndef ACPI_MACHINE_WIDTH
#error ACPI_MACHINE_WIDTH not defined
#endif

#if ACPI_MACHINE_WIDTH == 64

/*! [Begin] no source code translation (keep the typedefs) */

/*
 * 64-bit type definitions
 */
typedef unsigned char UINT8;
typedef unsigned char BOOLEAN;
typedef unsigned short UINT16;
typedef int INT32;
typedef unsigned int UINT32;
typedef COMPILER_DEPENDENT_INT64 INT64;
typedef COMPILER_DEPENDENT_UINT64 UINT64;

/*! [End] no source code translation !*/

typedef s64 acpi_native_int;
typedef u64 acpi_native_uint;

typedef u64 acpi_table_ptr;
typedef u64 acpi_io_address;
typedef u64 acpi_physical_address;
typedef u64 acpi_size;

#define ALIGNED_ADDRESS_BOUNDARY        0x00000008	/* No hardware alignment support in IA64 */
#define ACPI_USE_NATIVE_DIVIDE	/* Native 64-bit integer support */
#define ACPI_MAX_PTR                    ACPI_UINT64_MAX
#define ACPI_SIZE_MAX                   ACPI_UINT64_MAX

#elif ACPI_MACHINE_WIDTH == 16

/*! [Begin] no source code translation (keep the typedefs) */

/*
 * 16-bit type definitions
 */
typedef unsigned char UINT8;
typedef unsigned char BOOLEAN;
typedef unsigned int UINT16;
typedef long INT32;
typedef int INT16;
typedef unsigned long UINT32;

struct {
	UINT32 Lo;
	UINT32 Hi;
};

/*! [End] no source code translation !*/

typedef u16 acpi_native_uint;
typedef s16 acpi_native_int;

typedef u32 acpi_table_ptr;
typedef u32 acpi_io_address;
typedef char *acpi_physical_address;
typedef u16 acpi_size;

#define ALIGNED_ADDRESS_BOUNDARY        0x00000002
#define ACPI_MISALIGNED_TRANSFERS
#define ACPI_USE_NATIVE_DIVIDE	/* No 64-bit integers, ok to use native divide */
#define ACPI_MAX_PTR                    ACPI_UINT16_MAX
#define ACPI_SIZE_MAX                   ACPI_UINT16_MAX

/*
 * (16-bit only) internal integers must be 32-bits, so
 * 64-bit integers cannot be supported
 */
#define ACPI_NO_INTEGER64_SUPPORT

#elif ACPI_MACHINE_WIDTH == 32

/*! [Begin] no source code translation (keep the typedefs) */

/*
 * 32-bit type definitions (default)
 */
typedef unsigned char UINT8;
typedef unsigned char BOOLEAN;
typedef unsigned short UINT16;
typedef int INT32;
typedef unsigned int UINT32;
typedef COMPILER_DEPENDENT_INT64 INT64;
typedef COMPILER_DEPENDENT_UINT64 UINT64;

/*! [End] no source code translation !*/

typedef s32 acpi_native_int;
typedef u32 acpi_native_uint;

typedef u64 acpi_table_ptr;
typedef u32 acpi_io_address;
typedef u64 acpi_physical_address;
typedef u32 acpi_size;

#define ALIGNED_ADDRESS_BOUNDARY        0x00000004
#define ACPI_MISALIGNED_TRANSFERS
#define ACPI_MAX_PTR                    ACPI_UINT32_MAX
#define ACPI_SIZE_MAX                   ACPI_UINT32_MAX

#else
#error unknown ACPI_MACHINE_WIDTH
#endif

/*
 * This type is used for bitfields in ACPI tables. The only type that is
 * even remotely portable is u8. Anything else is not portable, so
 * do not add any more bitfield types.
 */
typedef u8 UINT8_BIT;
typedef acpi_native_uint ACPI_PTRDIFF;

/*
 * Pointer overlays to avoid lots of typecasting for
 * code that accepts both physical and logical pointers.
 */
union acpi_pointers {
	acpi_physical_address physical;
	void *logical;
	acpi_table_ptr value;
};

struct acpi_pointer {
	u32 pointer_type;
	union acpi_pointers pointer;
};

/* pointer_types for above */

#define ACPI_PHYSICAL_POINTER           0x01
#define ACPI_LOGICAL_POINTER            0x02

/* Processor mode */

#define ACPI_PHYSICAL_ADDRESSING        0x04
#define ACPI_LOGICAL_ADDRESSING         0x08
#define ACPI_MEMORY_MODE                0x0C

#define ACPI_PHYSMODE_PHYSPTR           ACPI_PHYSICAL_ADDRESSING | ACPI_PHYSICAL_POINTER
#define ACPI_LOGMODE_PHYSPTR            ACPI_LOGICAL_ADDRESSING  | ACPI_PHYSICAL_POINTER
#define ACPI_LOGMODE_LOGPTR             ACPI_LOGICAL_ADDRESSING  | ACPI_LOGICAL_POINTER

/*
 * If acpi_cache_t was not defined in the OS-dependent header,
 * define it now. This is typically the case where the local cache
 * manager implementation is to be used (ACPI_USE_LOCAL_CACHE)
 */
#ifndef acpi_cache_t
#define acpi_cache_t                            struct acpi_memory_list
#endif

/*
 * Useful defines
 */
#ifdef FALSE
#undef FALSE
#endif
#define FALSE                           (1 == 0)

#ifdef TRUE
#undef TRUE
#endif
#define TRUE                            (1 == 1)

#ifndef NULL
#define NULL                            (void *) 0
#endif

/*
 * Local datatypes
 */
typedef u32 acpi_status;	/* All ACPI Exceptions */
typedef u32 acpi_name;		/* 4-byte ACPI name */
typedef char *acpi_string;	/* Null terminated ASCII string */
typedef void *acpi_handle;	/* Actually a ptr to an Node */

struct uint64_struct {
	u32 lo;
	u32 hi;
};

union uint64_overlay {
	u64 full;
	struct uint64_struct part;
};

struct uint32_struct {
	u32 lo;
	u32 hi;
};

/*
 * Acpi integer width. In ACPI version 1, integers are
 * 32 bits.  In ACPI version 2, integers are 64 bits.
 * Note that this pertains to the ACPI integer type only, not
 * other integers used in the implementation of the ACPI CA
 * subsystem.
 */
#ifdef ACPI_NO_INTEGER64_SUPPORT

/* 32-bit integers only, no 64-bit support */

typedef u32 acpi_integer;
#define ACPI_INTEGER_MAX                ACPI_UINT32_MAX
#define ACPI_INTEGER_BIT_SIZE           32
#define ACPI_MAX_DECIMAL_DIGITS         10	/* 2^32 = 4,294,967,296 */

#define ACPI_USE_NATIVE_DIVIDE	/* Use compiler native 32-bit divide */

#else

/* 64-bit integers */

typedef u64 acpi_integer;
#define ACPI_INTEGER_MAX                ACPI_UINT64_MAX
#define ACPI_INTEGER_BIT_SIZE           64
#define ACPI_MAX_DECIMAL_DIGITS         20	/* 2^64 = 18,446,744,073,709,551,616 */

#if ACPI_MACHINE_WIDTH == 64
#define ACPI_USE_NATIVE_DIVIDE	/* Use compiler native 64-bit divide */
#endif
#endif

#define ACPI_MAX64_DECIMAL_DIGITS       20
#define ACPI_MAX32_DECIMAL_DIGITS       10
#define ACPI_MAX16_DECIMAL_DIGITS        5
#define ACPI_MAX8_DECIMAL_DIGITS         3

/*
 * Constants with special meanings
 */
#define ACPI_ROOT_OBJECT                (acpi_handle) ACPI_PTR_ADD (char, NULL, ACPI_MAX_PTR)

/*
 * Initialization sequence
 */
#define ACPI_FULL_INITIALIZATION        0x00
#define ACPI_NO_ADDRESS_SPACE_INIT      0x01
#define ACPI_NO_HARDWARE_INIT           0x02
#define ACPI_NO_EVENT_INIT              0x04
#define ACPI_NO_HANDLER_INIT            0x08
#define ACPI_NO_ACPI_ENABLE             0x10
#define ACPI_NO_DEVICE_INIT             0x20
#define ACPI_NO_OBJECT_INIT             0x40

/*
 * Initialization state
 */
#define ACPI_INITIALIZED_OK             0x01

/*
 * Power state values
 */
#define ACPI_STATE_UNKNOWN              (u8) 0xFF

#define ACPI_STATE_S0                   (u8) 0
#define ACPI_STATE_S1                   (u8) 1
#define ACPI_STATE_S2                   (u8) 2
#define ACPI_STATE_S3                   (u8) 3
#define ACPI_STATE_S4                   (u8) 4
#define ACPI_STATE_S5                   (u8) 5
#define ACPI_S_STATES_MAX               ACPI_STATE_S5
#define ACPI_S_STATE_COUNT              6

#define ACPI_STATE_D0                   (u8) 0
#define ACPI_STATE_D1                   (u8) 1
#define ACPI_STATE_D2                   (u8) 2
#define ACPI_STATE_D3                   (u8) 3
#define ACPI_D_STATES_MAX               ACPI_STATE_D3
#define ACPI_D_STATE_COUNT              4

#define ACPI_STATE_C0                   (u8) 0
#define ACPI_STATE_C1                   (u8) 1
#define ACPI_STATE_C2                   (u8) 2
#define ACPI_STATE_C3                   (u8) 3
#define ACPI_C_STATES_MAX               ACPI_STATE_C3
#define ACPI_C_STATE_COUNT              4

/*
 * Sleep type invalid value
 */
#define ACPI_SLEEP_TYPE_MAX             0x7
#define ACPI_SLEEP_TYPE_INVALID         0xFF

/*
 * Standard notify values
 */
#define ACPI_NOTIFY_BUS_CHECK           (u8) 0
#define ACPI_NOTIFY_DEVICE_CHECK        (u8) 1
#define ACPI_NOTIFY_DEVICE_WAKE         (u8) 2
#define ACPI_NOTIFY_EJECT_REQUEST       (u8) 3
#define ACPI_NOTIFY_DEVICE_CHECK_LIGHT  (u8) 4
#define ACPI_NOTIFY_FREQUENCY_MISMATCH  (u8) 5
#define ACPI_NOTIFY_BUS_MODE_MISMATCH   (u8) 6
#define ACPI_NOTIFY_POWER_FAULT         (u8) 7

/*
 *  Table types.  These values are passed to the table related APIs
 */
typedef u32 acpi_table_type;

#define ACPI_TABLE_RSDP                 (acpi_table_type) 0
#define ACPI_TABLE_DSDT                 (acpi_table_type) 1
#define ACPI_TABLE_FADT                 (acpi_table_type) 2
#define ACPI_TABLE_FACS                 (acpi_table_type) 3
#define ACPI_TABLE_PSDT                 (acpi_table_type) 4
#define ACPI_TABLE_SSDT                 (acpi_table_type) 5
#define ACPI_TABLE_XSDT                 (acpi_table_type) 6
#define ACPI_TABLE_MAX                  6
#define NUM_ACPI_TABLE_TYPES            (ACPI_TABLE_MAX+1)

/*
 * Types associated with ACPI names and objects.  The first group of
 * values (up to ACPI_TYPE_EXTERNAL_MAX) correspond to the definition
 * of the ACPI object_type() operator (See the ACPI Spec). Therefore,
 * only add to the first group if the spec changes.
 *
 * NOTE: Types must be kept in sync with the global acpi_ns_properties
 * and acpi_ns_type_names arrays.
 */
typedef u32 acpi_object_type;

#define ACPI_TYPE_ANY                   0x00
#define ACPI_TYPE_INTEGER               0x01	/* Byte/Word/Dword/Zero/One/Ones */
#define ACPI_TYPE_STRING                0x02
#define ACPI_TYPE_BUFFER                0x03
#define ACPI_TYPE_PACKAGE               0x04	/* byte_const, multiple data_term/Constant/super_name */
#define ACPI_TYPE_FIELD_UNIT            0x05
#define ACPI_TYPE_DEVICE                0x06	/* Name, multiple Node */
#define ACPI_TYPE_EVENT                 0x07
#define ACPI_TYPE_METHOD                0x08	/* Name, byte_const, multiple Code */
#define ACPI_TYPE_MUTEX                 0x09
#define ACPI_TYPE_REGION                0x0A
#define ACPI_TYPE_POWER                 0x0B	/* Name,byte_const,word_const,multi Node */
#define ACPI_TYPE_PROCESSOR             0x0C	/* Name,byte_const,Dword_const,byte_const,multi nm_o */
#define ACPI_TYPE_THERMAL               0x0D	/* Name, multiple Node */
#define ACPI_TYPE_BUFFER_FIELD          0x0E
#define ACPI_TYPE_DDB_HANDLE            0x0F
#define ACPI_TYPE_DEBUG_OBJECT          0x10

#define ACPI_TYPE_EXTERNAL_MAX          0x10

/*
 * These are object types that do not map directly to the ACPI
 * object_type() operator. They are used for various internal purposes only.
 * If new predefined ACPI_TYPEs are added (via the ACPI specification), these
 * internal types must move upwards. (There is code that depends on these
 * values being contiguous with the external types above.)
 */
#define ACPI_TYPE_LOCAL_REGION_FIELD    0x11
#define ACPI_TYPE_LOCAL_BANK_FIELD      0x12
#define ACPI_TYPE_LOCAL_INDEX_FIELD     0x13
#define ACPI_TYPE_LOCAL_REFERENCE       0x14	/* Arg#, Local#, Name, Debug, ref_of, Index */
#define ACPI_TYPE_LOCAL_ALIAS           0x15
#define ACPI_TYPE_LOCAL_METHOD_ALIAS    0x16
#define ACPI_TYPE_LOCAL_NOTIFY          0x17
#define ACPI_TYPE_LOCAL_ADDRESS_HANDLER 0x18
#define ACPI_TYPE_LOCAL_RESOURCE        0x19
#define ACPI_TYPE_LOCAL_RESOURCE_FIELD  0x1A
#define ACPI_TYPE_LOCAL_SCOPE           0x1B	/* 1 Name, multiple object_list Nodes */

#define ACPI_TYPE_NS_NODE_MAX           0x1B	/* Last typecode used within a NS Node */

/*
 * These are special object types that never appear in
 * a Namespace node, only in an union acpi_operand_object
 */
#define ACPI_TYPE_LOCAL_EXTRA           0x1C
#define ACPI_TYPE_LOCAL_DATA            0x1D

#define ACPI_TYPE_LOCAL_MAX             0x1D

/* All types above here are invalid */

#define ACPI_TYPE_INVALID               0x1E
#define ACPI_TYPE_NOT_FOUND             0xFF

/*
 * Bitmapped ACPI types.  Used internally only
 */
#define ACPI_BTYPE_ANY                  0x00000000
#define ACPI_BTYPE_INTEGER              0x00000001
#define ACPI_BTYPE_STRING               0x00000002
#define ACPI_BTYPE_BUFFER               0x00000004
#define ACPI_BTYPE_PACKAGE              0x00000008
#define ACPI_BTYPE_FIELD_UNIT           0x00000010
#define ACPI_BTYPE_DEVICE               0x00000020
#define ACPI_BTYPE_EVENT                0x00000040
#define ACPI_BTYPE_METHOD               0x00000080
#define ACPI_BTYPE_MUTEX                0x00000100
#define ACPI_BTYPE_REGION               0x00000200
#define ACPI_BTYPE_POWER                0x00000400
#define ACPI_BTYPE_PROCESSOR            0x00000800
#define ACPI_BTYPE_THERMAL              0x00001000
#define ACPI_BTYPE_BUFFER_FIELD         0x00002000
#define ACPI_BTYPE_DDB_HANDLE           0x00004000
#define ACPI_BTYPE_DEBUG_OBJECT         0x00008000
#define ACPI_BTYPE_REFERENCE            0x00010000
#define ACPI_BTYPE_RESOURCE             0x00020000

#define ACPI_BTYPE_COMPUTE_DATA         (ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING | ACPI_BTYPE_BUFFER)

#define ACPI_BTYPE_DATA                 (ACPI_BTYPE_COMPUTE_DATA  | ACPI_BTYPE_PACKAGE)
#define ACPI_BTYPE_DATA_REFERENCE       (ACPI_BTYPE_DATA | ACPI_BTYPE_REFERENCE | ACPI_BTYPE_DDB_HANDLE)
#define ACPI_BTYPE_DEVICE_OBJECTS       (ACPI_BTYPE_DEVICE | ACPI_BTYPE_THERMAL | ACPI_BTYPE_PROCESSOR)
#define ACPI_BTYPE_OBJECTS_AND_REFS     0x0001FFFF	/* ARG or LOCAL */
#define ACPI_BTYPE_ALL_OBJECTS          0x0000FFFF

/*
 * All I/O
 */
#define ACPI_READ                       0
#define ACPI_WRITE                      1
#define ACPI_IO_MASK                    1

/*
 * Event Types: Fixed & General Purpose
 */
typedef u32 acpi_event_type;

/*
 * Fixed events
 */
#define ACPI_EVENT_PMTIMER              0
#define ACPI_EVENT_GLOBAL               1
#define ACPI_EVENT_POWER_BUTTON         2
#define ACPI_EVENT_SLEEP_BUTTON         3
#define ACPI_EVENT_RTC                  4
#define ACPI_EVENT_MAX                  4
#define ACPI_NUM_FIXED_EVENTS           ACPI_EVENT_MAX + 1

/*
 * Event Status - Per event
 * -------------
 * The encoding of acpi_event_status is illustrated below.
 * Note that a set bit (1) indicates the property is TRUE
 * (e.g. if bit 0 is set then the event is enabled).
 * +-------------+-+-+-+
 * |   Bits 31:3 |2|1|0|
 * +-------------+-+-+-+
 *          |     | | |
 *          |     | | +- Enabled?
 *          |     | +--- Enabled for wake?
 *          |     +----- Set?
 *          +----------- <Reserved>
 */
typedef u32 acpi_event_status;

#define ACPI_EVENT_FLAG_DISABLED        (acpi_event_status) 0x00
#define ACPI_EVENT_FLAG_ENABLED         (acpi_event_status) 0x01
#define ACPI_EVENT_FLAG_WAKE_ENABLED    (acpi_event_status) 0x02
#define ACPI_EVENT_FLAG_SET             (acpi_event_status) 0x04

/*
 * General Purpose Events (GPE)
 */
#define ACPI_GPE_INVALID                0xFF
#define ACPI_GPE_MAX                    0xFF
#define ACPI_NUM_GPE                    256

#define ACPI_GPE_ENABLE                 0
#define ACPI_GPE_DISABLE                1

/*
 * GPE info flags - Per GPE
 * +-+-+-+---+---+-+
 * |7|6|5|4:3|2:1|0|
 * +-+-+-+---+---+-+
 *  | | |  |   |  |
 *  | | |  |   |  +--- Interrupt type: Edge or Level Triggered
 *  | | |  |   +--- Type: Wake-only, Runtime-only, or wake/runtime
 *  | | |  +--- Type of dispatch -- to method, handler, or none
 *  | | +--- Enabled for runtime?
 *  | +--- Enabled for wake?
 *  +--- System state when GPE ocurred (running/waking)
 */
#define ACPI_GPE_XRUPT_TYPE_MASK        (u8) 0x01
#define ACPI_GPE_LEVEL_TRIGGERED        (u8) 0x01
#define ACPI_GPE_EDGE_TRIGGERED         (u8) 0x00

#define ACPI_GPE_TYPE_MASK              (u8) 0x06
#define ACPI_GPE_TYPE_WAKE_RUN          (u8) 0x06
#define ACPI_GPE_TYPE_WAKE              (u8) 0x02
#define ACPI_GPE_TYPE_RUNTIME           (u8) 0x04	/* Default */

#define ACPI_GPE_DISPATCH_MASK          (u8) 0x18
#define ACPI_GPE_DISPATCH_HANDLER       (u8) 0x08
#define ACPI_GPE_DISPATCH_METHOD        (u8) 0x10
#define ACPI_GPE_DISPATCH_NOT_USED      (u8) 0x00	/* Default */

#define ACPI_GPE_RUN_ENABLE_MASK        (u8) 0x20
#define ACPI_GPE_RUN_ENABLED            (u8) 0x20
#define ACPI_GPE_RUN_DISABLED           (u8) 0x00	/* Default */

#define ACPI_GPE_WAKE_ENABLE_MASK       (u8) 0x40
#define ACPI_GPE_WAKE_ENABLED           (u8) 0x40
#define ACPI_GPE_WAKE_DISABLED          (u8) 0x00	/* Default */

#define ACPI_GPE_ENABLE_MASK            (u8) 0x60	/* Both run/wake */

#define ACPI_GPE_SYSTEM_MASK            (u8) 0x80
#define ACPI_GPE_SYSTEM_RUNNING         (u8) 0x80
#define ACPI_GPE_SYSTEM_WAKING          (u8) 0x00

/*
 * Flags for GPE and Lock interfaces
 */
#define ACPI_EVENT_WAKE_ENABLE          0x2	/* acpi_gpe_enable */
#define ACPI_EVENT_WAKE_DISABLE         0x2	/* acpi_gpe_disable */

#define ACPI_NOT_ISR                    0x1
#define ACPI_ISR                        0x0

/* Notify types */

#define ACPI_SYSTEM_NOTIFY              0x1
#define ACPI_DEVICE_NOTIFY              0x2
#define ACPI_ALL_NOTIFY                 0x3
#define ACPI_MAX_NOTIFY_HANDLER_TYPE    0x3

#define ACPI_MAX_SYS_NOTIFY             0x7f

/* Address Space (Operation Region) Types */

typedef u8 acpi_adr_space_type;

#define ACPI_ADR_SPACE_SYSTEM_MEMORY    (acpi_adr_space_type) 0
#define ACPI_ADR_SPACE_SYSTEM_IO        (acpi_adr_space_type) 1
#define ACPI_ADR_SPACE_PCI_CONFIG       (acpi_adr_space_type) 2
#define ACPI_ADR_SPACE_EC               (acpi_adr_space_type) 3
#define ACPI_ADR_SPACE_SMBUS            (acpi_adr_space_type) 4
#define ACPI_ADR_SPACE_CMOS             (acpi_adr_space_type) 5
#define ACPI_ADR_SPACE_PCI_BAR_TARGET   (acpi_adr_space_type) 6
#define ACPI_ADR_SPACE_DATA_TABLE       (acpi_adr_space_type) 7
#define ACPI_ADR_SPACE_FIXED_HARDWARE   (acpi_adr_space_type) 127

/*
 * bit_register IDs
 * These are bitfields defined within the full ACPI registers
 */
#define ACPI_BITREG_TIMER_STATUS                0x00
#define ACPI_BITREG_BUS_MASTER_STATUS           0x01
#define ACPI_BITREG_GLOBAL_LOCK_STATUS          0x02
#define ACPI_BITREG_POWER_BUTTON_STATUS         0x03
#define ACPI_BITREG_SLEEP_BUTTON_STATUS         0x04
#define ACPI_BITREG_RT_CLOCK_STATUS             0x05
#define ACPI_BITREG_WAKE_STATUS                 0x06
#define ACPI_BITREG_PCIEXP_WAKE_STATUS          0x07

#define ACPI_BITREG_TIMER_ENABLE                0x08
#define ACPI_BITREG_GLOBAL_LOCK_ENABLE          0x09
#define ACPI_BITREG_POWER_BUTTON_ENABLE         0x0A
#define ACPI_BITREG_SLEEP_BUTTON_ENABLE         0x0B
#define ACPI_BITREG_RT_CLOCK_ENABLE             0x0C
#define ACPI_BITREG_WAKE_ENABLE                 0x0D
#define ACPI_BITREG_PCIEXP_WAKE_DISABLE         0x0E

#define ACPI_BITREG_SCI_ENABLE                  0x0F
#define ACPI_BITREG_BUS_MASTER_RLD              0x10
#define ACPI_BITREG_GLOBAL_LOCK_RELEASE         0x11
#define ACPI_BITREG_SLEEP_TYPE_A                0x12
#define ACPI_BITREG_SLEEP_TYPE_B                0x13
#define ACPI_BITREG_SLEEP_ENABLE                0x14

#define ACPI_BITREG_ARB_DISABLE                 0x15

#define ACPI_BITREG_MAX                         0x15
#define ACPI_NUM_BITREG                         ACPI_BITREG_MAX + 1

/*
 * External ACPI object definition
 */
union acpi_object {
	acpi_object_type type;	/* See definition of acpi_ns_type for values */
	struct {
		acpi_object_type type;
		acpi_integer value;	/* The actual number */
	} integer;

	struct {
		acpi_object_type type;
		u32 length;	/* # of bytes in string, excluding trailing null */
		char *pointer;	/* points to the string value */
	} string;

	struct {
		acpi_object_type type;
		u32 length;	/* # of bytes in buffer */
		u8 *pointer;	/* points to the buffer */
	} buffer;

	struct {
		acpi_object_type type;
		u32 fill1;
		acpi_handle handle;	/* object reference */
	} reference;

	struct {
		acpi_object_type type;
		u32 count;	/* # of elements in package */
		union acpi_object *elements;	/* Pointer to an array of ACPI_OBJECTs */
	} package;

	struct {
		acpi_object_type type;
		u32 proc_id;
		acpi_io_address pblk_address;
		u32 pblk_length;
	} processor;

	struct {
		acpi_object_type type;
		u32 system_level;
		u32 resource_order;
	} power_resource;
};

/*
 * List of objects, used as a parameter list for control method evaluation
 */
struct acpi_object_list {
	u32 count;
	union acpi_object *pointer;
};

/*
 * Miscellaneous common Data Structures used by the interfaces
 */
#define ACPI_NO_BUFFER              0
#define ACPI_ALLOCATE_BUFFER        (acpi_size) (-1)
#define ACPI_ALLOCATE_LOCAL_BUFFER  (acpi_size) (-2)

struct acpi_buffer {
	acpi_size length;	/* Length in bytes of the buffer */
	void *pointer;		/* pointer to buffer */
};

/*
 * name_type for acpi_get_name
 */
#define ACPI_FULL_PATHNAME              0
#define ACPI_SINGLE_NAME                1
#define ACPI_NAME_TYPE_MAX              1

/*
 * Structure and flags for acpi_get_system_info
 */
#define ACPI_SYS_MODE_UNKNOWN           0x0000
#define ACPI_SYS_MODE_ACPI              0x0001
#define ACPI_SYS_MODE_LEGACY            0x0002
#define ACPI_SYS_MODES_MASK             0x0003

/*
 * ACPI Table Info.  One per ACPI table _type_
 */
struct acpi_table_info {
	u32 count;
};

/*
 * System info returned by acpi_get_system_info()
 */
struct acpi_system_info {
	u32 acpi_ca_version;
	u32 flags;
	u32 timer_resolution;
	u32 reserved1;
	u32 reserved2;
	u32 debug_level;
	u32 debug_layer;
	u32 num_table_types;
	struct acpi_table_info table_info[NUM_ACPI_TABLE_TYPES];
};

/*
 * Types specific to the OS service interfaces
 */
typedef u32(ACPI_SYSTEM_XFACE * acpi_osd_handler) (void *context);

typedef void
 (ACPI_SYSTEM_XFACE * acpi_osd_exec_callback) (void *context);

/*
 * Various handlers and callback procedures
 */
typedef u32(*acpi_event_handler) (void *context);

typedef
void (*acpi_notify_handler) (acpi_handle device, u32 value, void *context);

typedef
void (*acpi_object_handler) (acpi_handle object, u32 function, void *data);

typedef acpi_status(*acpi_init_handler) (acpi_handle object, u32 function);

#define ACPI_INIT_DEVICE_INI        1

typedef
acpi_status(*acpi_exception_handler) (acpi_status aml_status,
				      acpi_name name,
				      u16 opcode,
				      u32 aml_offset, void *context);

/* Address Spaces (For Operation Regions) */

typedef
acpi_status(*acpi_adr_space_handler) (u32 function,
				      acpi_physical_address address,
				      u32 bit_width,
				      acpi_integer * value,
				      void *handler_context,
				      void *region_context);

#define ACPI_DEFAULT_HANDLER        NULL

typedef
acpi_status(*acpi_adr_space_setup) (acpi_handle region_handle,
				    u32 function,
				    void *handler_context,
				    void **region_context);

#define ACPI_REGION_ACTIVATE    0
#define ACPI_REGION_DEACTIVATE  1

typedef
acpi_status(*acpi_walk_callback) (acpi_handle obj_handle,
				  u32 nesting_level,
				  void *context, void **return_value);

/* Interrupt handler return values */

#define ACPI_INTERRUPT_NOT_HANDLED      0x00
#define ACPI_INTERRUPT_HANDLED          0x01

/* Common string version of device HIDs and UIDs */

struct acpi_device_id {
	char value[ACPI_DEVICE_ID_LENGTH];
};

/* Common string version of device CIDs */

struct acpi_compatible_id {
	char value[ACPI_MAX_CID_LENGTH];
};

struct acpi_compatible_id_list {
	u32 count;
	u32 size;
	struct acpi_compatible_id id[1];
};

/* Structure and flags for acpi_get_object_info */

#define ACPI_VALID_STA                  0x0001
#define ACPI_VALID_ADR                  0x0002
#define ACPI_VALID_HID                  0x0004
#define ACPI_VALID_UID                  0x0008
#define ACPI_VALID_CID                  0x0010
#define ACPI_VALID_SXDS                 0x0020

#define ACPI_COMMON_OBJ_INFO \
	acpi_object_type                    type;           /* ACPI object type */ \
	acpi_name                           name	/* ACPI object Name */

struct acpi_obj_info_header {
	ACPI_COMMON_OBJ_INFO;
};

/* Structure returned from Get Object Info */

struct acpi_device_info {
	ACPI_COMMON_OBJ_INFO;

	u32 valid;		/* Indicates which fields below are valid */
	u32 current_status;	/* _STA value */
	acpi_integer address;	/* _ADR value if any */
	struct acpi_device_id hardware_id;	/* _HID value if any */
	struct acpi_device_id unique_id;	/* _UID value if any */
	u8 highest_dstates[4];	/* _sx_d values: 0xFF indicates not valid */
	struct acpi_compatible_id_list compatibility_id;	/* List of _CIDs if any */
};

/* Context structs for address space handlers */

struct acpi_pci_id {
	u16 segment;
	u16 bus;
	u16 device;
	u16 function;
};

struct acpi_mem_space_context {
	u32 length;
	acpi_physical_address address;
	acpi_physical_address mapped_physical_address;
	u8 *mapped_logical_address;
	acpi_size mapped_length;
};

/*
 * Definitions for Resource Attributes
 */

/*
 *  Memory Attributes
 */
#define ACPI_READ_ONLY_MEMORY           (u8) 0x00
#define ACPI_READ_WRITE_MEMORY          (u8) 0x01

#define ACPI_NON_CACHEABLE_MEMORY       (u8) 0x00
#define ACPI_CACHABLE_MEMORY            (u8) 0x01
#define ACPI_WRITE_COMBINING_MEMORY     (u8) 0x02
#define ACPI_PREFETCHABLE_MEMORY        (u8) 0x03

/*
 *  IO Attributes
 *  The ISA Io ranges are:     n000-n0_ffh, n400-n4_ffh, n800-n8_ffh, n_c00-n_cFFh.
 *  The non-ISA Io ranges are: n100-n3_ffh, n500-n7_ffh, n900-n_bFfh, n_cd0-n_fFFh.
 */
#define ACPI_NON_ISA_ONLY_RANGES        (u8) 0x01
#define ACPI_ISA_ONLY_RANGES            (u8) 0x02
#define ACPI_ENTIRE_RANGE               (ACPI_NON_ISA_ONLY_RANGES | ACPI_ISA_ONLY_RANGES)

#define ACPI_SPARSE_TRANSLATION         (u8) 0x03

/*
 *  IO Port Descriptor Decode
 */
#define ACPI_DECODE_10                  (u8) 0x00	/* 10-bit IO address decode */
#define ACPI_DECODE_16                  (u8) 0x01	/* 16-bit IO address decode */

/*
 *  IRQ Attributes
 */
#define ACPI_EDGE_SENSITIVE             (u8) 0x00
#define ACPI_LEVEL_SENSITIVE            (u8) 0x01

#define ACPI_ACTIVE_HIGH                (u8) 0x00
#define ACPI_ACTIVE_LOW                 (u8) 0x01

#define ACPI_EXCLUSIVE                  (u8) 0x00
#define ACPI_SHARED                     (u8) 0x01

/*
 *  DMA Attributes
 */
#define ACPI_COMPATIBILITY              (u8) 0x00
#define ACPI_TYPE_A                     (u8) 0x01
#define ACPI_TYPE_B                     (u8) 0x02
#define ACPI_TYPE_F                     (u8) 0x03

#define ACPI_NOT_BUS_MASTER             (u8) 0x00
#define ACPI_BUS_MASTER                 (u8) 0x01

#define ACPI_TRANSFER_8                 (u8) 0x00
#define ACPI_TRANSFER_8_16              (u8) 0x01
#define ACPI_TRANSFER_16                (u8) 0x02

/*
 * Start Dependent Functions Priority definitions
 */
#define ACPI_GOOD_CONFIGURATION         (u8) 0x00
#define ACPI_ACCEPTABLE_CONFIGURATION   (u8) 0x01
#define ACPI_SUB_OPTIMAL_CONFIGURATION  (u8) 0x02

/*
 *  16, 32 and 64-bit Address Descriptor resource types
 */
#define ACPI_MEMORY_RANGE               (u8) 0x00
#define ACPI_IO_RANGE                   (u8) 0x01
#define ACPI_BUS_NUMBER_RANGE           (u8) 0x02

#define ACPI_ADDRESS_NOT_FIXED          (u8) 0x00
#define ACPI_ADDRESS_FIXED              (u8) 0x01

#define ACPI_POS_DECODE                 (u8) 0x00
#define ACPI_SUB_DECODE                 (u8) 0x01

#define ACPI_PRODUCER                   (u8) 0x00
#define ACPI_CONSUMER                   (u8) 0x01

/*
 *  Structures used to describe device resources
 */
struct acpi_resource_irq {
	u32 edge_level;
	u32 active_high_low;
	u32 shared_exclusive;
	u32 number_of_interrupts;
	u32 interrupts[1];
};

struct acpi_resource_dma {
	u32 type;
	u32 bus_master;
	u32 transfer;
	u32 number_of_channels;
	u32 channels[1];
};

struct acpi_resource_start_dpf {
	u32 compatibility_priority;
	u32 performance_robustness;
};

/*
 * END_DEPENDENT_FUNCTIONS_RESOURCE struct is not
 * needed because it has no fields
 */

struct acpi_resource_io {
	u32 io_decode;
	u32 min_base_address;
	u32 max_base_address;
	u32 alignment;
	u32 range_length;
};

struct acpi_resource_fixed_io {
	u32 base_address;
	u32 range_length;
};

struct acpi_resource_vendor {
	u32 length;
	u8 reserved[1];
};

struct acpi_resource_end_tag {
	u8 checksum;
};

struct acpi_resource_mem24 {
	u32 read_write_attribute;
	u32 min_base_address;
	u32 max_base_address;
	u32 alignment;
	u32 range_length;
};

struct acpi_resource_mem32 {
	u32 read_write_attribute;
	u32 min_base_address;
	u32 max_base_address;
	u32 alignment;
	u32 range_length;
};

struct acpi_resource_fixed_mem32 {
	u32 read_write_attribute;
	u32 range_base_address;
	u32 range_length;
};

struct acpi_memory_attribute {
	u16 cache_attribute;
	u16 read_write_attribute;
};

struct acpi_io_attribute {
	u16 range_attribute;
	u16 translation_attribute;
};

struct acpi_bus_attribute {
	u16 reserved1;
	u16 reserved2;
};

union acpi_resource_attribute {
	struct acpi_memory_attribute memory;
	struct acpi_io_attribute io;
	struct acpi_bus_attribute bus;
};

struct acpi_resource_source {
	u32 index;
	u32 string_length;
	char *string_ptr;
};

/* Fields common to all address descriptors, 16/32/64 bit */

#define ACPI_RESOURCE_ADDRESS_COMMON \
	u32                                 resource_type; \
	u32                                 producer_consumer; \
	u32                                 decode; \
	u32                                 min_address_fixed; \
	u32                                 max_address_fixed; \
	union acpi_resource_attribute       attribute;

struct acpi_resource_address {
ACPI_RESOURCE_ADDRESS_COMMON};

struct acpi_resource_address16 {
	ACPI_RESOURCE_ADDRESS_COMMON u32 granularity;
	u32 min_address_range;
	u32 max_address_range;
	u32 address_translation_offset;
	u32 address_length;
	struct acpi_resource_source resource_source;
};

struct acpi_resource_address32 {
	ACPI_RESOURCE_ADDRESS_COMMON u32 granularity;
	u32 min_address_range;
	u32 max_address_range;
	u32 address_translation_offset;
	u32 address_length;
	struct acpi_resource_source resource_source;
};

struct acpi_resource_address64 {
	ACPI_RESOURCE_ADDRESS_COMMON u64 granularity;
	u64 min_address_range;
	u64 max_address_range;
	u64 address_translation_offset;
	u64 address_length;
	u64 type_specific_attributes;
	struct acpi_resource_source resource_source;
};

struct acpi_resource_ext_irq {
	u32 producer_consumer;
	u32 edge_level;
	u32 active_high_low;
	u32 shared_exclusive;
	u32 number_of_interrupts;
	struct acpi_resource_source resource_source;
	u32 interrupts[1];
};

/* ACPI_RESOURCE_TYPEs */

#define ACPI_RSTYPE_IRQ                 0
#define ACPI_RSTYPE_DMA                 1
#define ACPI_RSTYPE_START_DPF           2
#define ACPI_RSTYPE_END_DPF             3
#define ACPI_RSTYPE_IO                  4
#define ACPI_RSTYPE_FIXED_IO            5
#define ACPI_RSTYPE_VENDOR              6
#define ACPI_RSTYPE_END_TAG             7
#define ACPI_RSTYPE_MEM24               8
#define ACPI_RSTYPE_MEM32               9
#define ACPI_RSTYPE_FIXED_MEM32         10
#define ACPI_RSTYPE_ADDRESS16           11
#define ACPI_RSTYPE_ADDRESS32           12
#define ACPI_RSTYPE_ADDRESS64           13
#define ACPI_RSTYPE_EXT_IRQ             14

typedef u32 acpi_resource_type;

union acpi_resource_data {
	struct acpi_resource_irq irq;
	struct acpi_resource_dma dma;
	struct acpi_resource_start_dpf start_dpf;
	struct acpi_resource_io io;
	struct acpi_resource_fixed_io fixed_io;
	struct acpi_resource_vendor vendor_specific;
	struct acpi_resource_end_tag end_tag;
	struct acpi_resource_mem24 memory24;
	struct acpi_resource_mem32 memory32;
	struct acpi_resource_fixed_mem32 fixed_memory32;
	struct acpi_resource_address address;	/* Common 16/32/64 address fields */
	struct acpi_resource_address16 address16;
	struct acpi_resource_address32 address32;
	struct acpi_resource_address64 address64;
	struct acpi_resource_ext_irq extended_irq;
};

struct acpi_resource {
	acpi_resource_type id;
	u32 length;
	union acpi_resource_data data;
};

#define ACPI_RESOURCE_LENGTH                12
#define ACPI_RESOURCE_LENGTH_NO_DATA        8	/* Id + Length fields */

#define ACPI_SIZEOF_RESOURCE(type)          (ACPI_RESOURCE_LENGTH_NO_DATA + sizeof (type))

#define ACPI_NEXT_RESOURCE(res)             (struct acpi_resource *)((u8 *) res + res->length)

#ifdef ACPI_MISALIGNED_TRANSFERS
#define ACPI_ALIGN_RESOURCE_SIZE(length)    (length)
#else
#define ACPI_ALIGN_RESOURCE_SIZE(length)    ACPI_ROUND_UP_TO_NATIVE_WORD(length)
#endif

/*
 * END: of definitions for Resource Attributes
 */

struct acpi_pci_routing_table {
	u32 length;
	u32 pin;
	acpi_integer address;	/* here for 64-bit alignment */
	u32 source_index;
	char source[4];		/* pad to 64 bits so sizeof() works in all cases */
};

/*
 * END: of definitions for PCI Routing tables
 */

#endif				/* __ACTYPES_H__ */

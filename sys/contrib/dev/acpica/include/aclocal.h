/******************************************************************************
 *
 * Name: aclocal.h - Internal data types used across the ACPI subsystem
 *
 *****************************************************************************/

/******************************************************************************
 *
 * 1. Copyright Notice
 *
 * Some or all of this work - Copyright (c) 1999 - 2019, Intel Corp.
 * All rights reserved.
 *
 * 2. License
 *
 * 2.1. This is your license from Intel Corp. under its intellectual property
 * rights. You may have additional license terms from the party that provided
 * you this software, covering your right to use that party's intellectual
 * property rights.
 *
 * 2.2. Intel grants, free of charge, to any person ("Licensee") obtaining a
 * copy of the source code appearing in this file ("Covered Code") an
 * irrevocable, perpetual, worldwide license under Intel's copyrights in the
 * base code distributed originally by Intel ("Original Intel Code") to copy,
 * make derivatives, distribute, use and display any portion of the Covered
 * Code in any form, with the right to sublicense such rights; and
 *
 * 2.3. Intel grants Licensee a non-exclusive and non-transferable patent
 * license (with the right to sublicense), under only those claims of Intel
 * patents that are infringed by the Original Intel Code, to make, use, sell,
 * offer to sell, and import the Covered Code and derivative works thereof
 * solely to the minimum extent necessary to exercise the above copyright
 * license, and in no event shall the patent license extend to any additions
 * to or modifications of the Original Intel Code. No other license or right
 * is granted directly or by implication, estoppel or otherwise;
 *
 * The above copyright and patent license is granted only if the following
 * conditions are met:
 *
 * 3. Conditions
 *
 * 3.1. Redistribution of Source with Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification with rights to further distribute source must include
 * the above Copyright Notice, the above License, this list of Conditions,
 * and the following Disclaimer and Export Compliance provision. In addition,
 * Licensee must cause all Covered Code to which Licensee contributes to
 * contain a file documenting the changes Licensee made to create that Covered
 * Code and the date of any change. Licensee must include in that file the
 * documentation of any changes made by any predecessor Licensee. Licensee
 * must include a prominent statement that the modification is derived,
 * directly or indirectly, from Original Intel Code.
 *
 * 3.2. Redistribution of Source with no Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification without rights to further distribute source must
 * include the following Disclaimer and Export Compliance provision in the
 * documentation and/or other materials provided with distribution. In
 * addition, Licensee may not authorize further sublicense of source of any
 * portion of the Covered Code, and must include terms to the effect that the
 * license from Licensee to its licensee is limited to the intellectual
 * property embodied in the software Licensee provides to its licensee, and
 * not to intellectual property embodied in modifications its licensee may
 * make.
 *
 * 3.3. Redistribution of Executable. Redistribution in executable form of any
 * substantial portion of the Covered Code or modification must reproduce the
 * above Copyright Notice, and the following Disclaimer and Export Compliance
 * provision in the documentation and/or other materials provided with the
 * distribution.
 *
 * 3.4. Intel retains all right, title, and interest in and to the Original
 * Intel Code.
 *
 * 3.5. Neither the name Intel nor any other trademark owned or controlled by
 * Intel shall be used in advertising or otherwise to promote the sale, use or
 * other dealings in products derived from or relating to the Covered Code
 * without prior written authorization from Intel.
 *
 * 4. Disclaimer and Export Compliance
 *
 * 4.1. INTEL MAKES NO WARRANTY OF ANY KIND REGARDING ANY SOFTWARE PROVIDED
 * HERE. ANY SOFTWARE ORIGINATING FROM INTEL OR DERIVED FROM INTEL SOFTWARE
 * IS PROVIDED "AS IS," AND INTEL WILL NOT PROVIDE ANY SUPPORT, ASSISTANCE,
 * INSTALLATION, TRAINING OR OTHER SERVICES. INTEL WILL NOT PROVIDE ANY
 * UPDATES, ENHANCEMENTS OR EXTENSIONS. INTEL SPECIFICALLY DISCLAIMS ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT AND FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * 4.2. IN NO EVENT SHALL INTEL HAVE ANY LIABILITY TO LICENSEE, ITS LICENSEES
 * OR ANY OTHER THIRD PARTY, FOR ANY LOST PROFITS, LOST DATA, LOSS OF USE OR
 * COSTS OF PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, OR FOR ANY INDIRECT,
 * SPECIAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THIS AGREEMENT, UNDER ANY
 * CAUSE OF ACTION OR THEORY OF LIABILITY, AND IRRESPECTIVE OF WHETHER INTEL
 * HAS ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES. THESE LIMITATIONS
 * SHALL APPLY NOTWITHSTANDING THE FAILURE OF THE ESSENTIAL PURPOSE OF ANY
 * LIMITED REMEDY.
 *
 * 4.3. Licensee shall not export, either directly or indirectly, any of this
 * software or system incorporating such software without first obtaining any
 * required license or other approval from the U. S. Department of Commerce or
 * any other agency or department of the United States Government. In the
 * event Licensee exports any such software from the United States or
 * re-exports any such software from a foreign destination, Licensee shall
 * ensure that the distribution and export/re-export of the software is in
 * compliance with all laws, regulations, orders, or other restrictions of the
 * U.S. Export Administration Regulations. Licensee agrees that neither it nor
 * any of its subsidiaries will export/re-export any technical data, process,
 * software, or service, directly or indirectly, to any country for which the
 * United States government or any agency thereof requires an export license,
 * other governmental approval, or letter of assurance, without first obtaining
 * such license, approval or letter.
 *
 *****************************************************************************
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * following license:
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 *****************************************************************************/

#ifndef __ACLOCAL_H__
#define __ACLOCAL_H__


/* acpisrc:StructDefs -- for acpisrc conversion */

#define ACPI_SERIALIZED                 0xFF

typedef UINT32                          ACPI_MUTEX_HANDLE;
#define ACPI_GLOBAL_LOCK                (ACPI_SEMAPHORE) (-1)

/* Total number of aml opcodes defined */

#define AML_NUM_OPCODES                 0x83


/* Forward declarations */

struct acpi_walk_state;
struct acpi_obj_mutex;
union acpi_parse_object;


/*****************************************************************************
 *
 * Mutex typedefs and structs
 *
 ****************************************************************************/


/*
 * Predefined handles for the mutex objects used within the subsystem
 * All mutex objects are automatically created by AcpiUtMutexInitialize.
 *
 * The acquire/release ordering protocol is implied via this list. Mutexes
 * with a lower value must be acquired before mutexes with a higher value.
 *
 * NOTE: any changes here must be reflected in the AcpiGbl_MutexNames
 * table below also!
 */
#define ACPI_MTX_INTERPRETER            0   /* AML Interpreter, main lock */
#define ACPI_MTX_NAMESPACE              1   /* ACPI Namespace */
#define ACPI_MTX_TABLES                 2   /* Data for ACPI tables */
#define ACPI_MTX_EVENTS                 3   /* Data for ACPI events */
#define ACPI_MTX_CACHES                 4   /* Internal caches, general purposes */
#define ACPI_MTX_MEMORY                 5   /* Debug memory tracking lists */

#define ACPI_MAX_MUTEX                  5
#define ACPI_NUM_MUTEX                  (ACPI_MAX_MUTEX+1)


/* Lock structure for reader/writer interfaces */

typedef struct acpi_rw_lock
{
    ACPI_MUTEX              WriterMutex;
    ACPI_MUTEX              ReaderMutex;
    UINT32                  NumReaders;

} ACPI_RW_LOCK;


/*
 * Predefined handles for spinlocks used within the subsystem.
 * These spinlocks are created by AcpiUtMutexInitialize
 */
#define ACPI_LOCK_GPES                  0
#define ACPI_LOCK_HARDWARE              1

#define ACPI_MAX_LOCK                   1
#define ACPI_NUM_LOCK                   (ACPI_MAX_LOCK+1)


/* This Thread ID means that the mutex is not in use (unlocked) */

#define ACPI_MUTEX_NOT_ACQUIRED         ((ACPI_THREAD_ID) -1)

/* This Thread ID means an invalid thread ID */

#ifdef ACPI_OS_INVALID_THREAD_ID
#define ACPI_INVALID_THREAD_ID          ACPI_OS_INVALID_THREAD_ID
#else
#define ACPI_INVALID_THREAD_ID          ((ACPI_THREAD_ID) 0xFFFFFFFF)
#endif

/* Table for the global mutexes */

typedef struct acpi_mutex_info
{
    ACPI_MUTEX                      Mutex;
    UINT32                          UseCount;
    ACPI_THREAD_ID                  ThreadId;

} ACPI_MUTEX_INFO;


/* Lock flag parameter for various interfaces */

#define ACPI_MTX_DO_NOT_LOCK            0
#define ACPI_MTX_LOCK                   1


/* Field access granularities */

#define ACPI_FIELD_BYTE_GRANULARITY     1
#define ACPI_FIELD_WORD_GRANULARITY     2
#define ACPI_FIELD_DWORD_GRANULARITY    4
#define ACPI_FIELD_QWORD_GRANULARITY    8


#define ACPI_ENTRY_NOT_FOUND            NULL


/*****************************************************************************
 *
 * Namespace typedefs and structs
 *
 ****************************************************************************/

/* Operational modes of the AML interpreter/scanner */

typedef enum
{
    ACPI_IMODE_LOAD_PASS1           = 0x01,
    ACPI_IMODE_LOAD_PASS2           = 0x02,
    ACPI_IMODE_EXECUTE              = 0x03

} ACPI_INTERPRETER_MODE;


/*
 * The Namespace Node describes a named object that appears in the AML.
 * DescriptorType is used to differentiate between internal descriptors.
 *
 * The node is optimized for both 32-bit and 64-bit platforms:
 * 20 bytes for the 32-bit case, 32 bytes for the 64-bit case.
 *
 * Note: The DescriptorType and Type fields must appear in the identical
 * position in both the ACPI_NAMESPACE_NODE and ACPI_OPERAND_OBJECT
 * structures.
 */
typedef struct acpi_namespace_node
{
    union acpi_operand_object       *Object;        /* Interpreter object */
    UINT8                           DescriptorType; /* Differentiate object descriptor types */
    UINT8                           Type;           /* ACPI Type associated with this name */
    UINT8                           Flags;          /* Miscellaneous flags */
    ACPI_OWNER_ID                   OwnerId;        /* Node creator */
    ACPI_NAME_UNION                 Name;           /* ACPI Name, always 4 chars per ACPI spec */
    struct acpi_namespace_node      *Parent;        /* Parent node */
    struct acpi_namespace_node      *Child;         /* First child */
    struct acpi_namespace_node      *Peer;          /* First peer */

    /*
     * The following fields are used by the ASL compiler and disassembler only
     */
#ifdef ACPI_LARGE_NAMESPACE_NODE
    union acpi_parse_object         *Op;
    void                            *MethodLocals;
    void                            *MethodArgs;
    UINT32                          Value;
    UINT32                          Length;
    UINT8                           ArgCount;

#endif

} ACPI_NAMESPACE_NODE;


/* Namespace Node flags */

#define ANOBJ_RESERVED                  0x01    /* Available for use */
#define ANOBJ_TEMPORARY                 0x02    /* Node is create by a method and is temporary */
#define ANOBJ_METHOD_ARG                0x04    /* Node is a method argument */
#define ANOBJ_METHOD_LOCAL              0x08    /* Node is a method local */
#define ANOBJ_SUBTREE_HAS_INI           0x10    /* Used to optimize device initialization */
#define ANOBJ_EVALUATED                 0x20    /* Set on first evaluation of node */
#define ANOBJ_ALLOCATED_BUFFER          0x40    /* Method AML buffer is dynamic (InstallMethod) */
#define ANOBJ_NODE_EARLY_INIT           0x80    /* AcpiExec only: Node was create via init file (-fi) */

#define ANOBJ_IS_EXTERNAL               0x08    /* iASL only: This object created via External() */
#define ANOBJ_METHOD_NO_RETVAL          0x10    /* iASL only: Method has no return value */
#define ANOBJ_METHOD_SOME_NO_RETVAL     0x20    /* iASL only: Method has at least one return value */
#define ANOBJ_IS_REFERENCED             0x80    /* iASL only: Object was referenced */


/* Internal ACPI table management - master table list */

typedef struct acpi_table_list
{
    ACPI_TABLE_DESC                 *Tables;            /* Table descriptor array */
    UINT32                          CurrentTableCount;  /* Tables currently in the array */
    UINT32                          MaxTableCount;      /* Max tables array will hold */
    UINT8                           Flags;

} ACPI_TABLE_LIST;

/* Flags for above */

#define ACPI_ROOT_ORIGIN_UNKNOWN        (0)     /* ~ORIGIN_ALLOCATED */
#define ACPI_ROOT_ORIGIN_ALLOCATED      (1)
#define ACPI_ROOT_ALLOW_RESIZE          (2)


/* List to manage incoming ACPI tables */

typedef struct acpi_new_table_desc
{
    ACPI_TABLE_HEADER               *Table;
    struct acpi_new_table_desc      *Next;

} ACPI_NEW_TABLE_DESC;


/* Predefined table indexes */

#define ACPI_INVALID_TABLE_INDEX        (0xFFFFFFFF)


typedef struct acpi_find_context
{
    char                            *SearchFor;
    ACPI_HANDLE                     *List;
    UINT32                          *Count;

} ACPI_FIND_CONTEXT;


typedef struct acpi_ns_search_data
{
    ACPI_NAMESPACE_NODE             *Node;

} ACPI_NS_SEARCH_DATA;


/* Object types used during package copies */

#define ACPI_COPY_TYPE_SIMPLE           0
#define ACPI_COPY_TYPE_PACKAGE          1


/* Info structure used to convert external<->internal namestrings */

typedef struct acpi_namestring_info
{
    const char                      *ExternalName;
    const char                      *NextExternalChar;
    char                            *InternalName;
    UINT32                          Length;
    UINT32                          NumSegments;
    UINT32                          NumCarats;
    BOOLEAN                         FullyQualified;

} ACPI_NAMESTRING_INFO;


/* Field creation info */

typedef struct acpi_create_field_info
{
    ACPI_NAMESPACE_NODE             *RegionNode;
    ACPI_NAMESPACE_NODE             *FieldNode;
    ACPI_NAMESPACE_NODE             *RegisterNode;
    ACPI_NAMESPACE_NODE             *DataRegisterNode;
    ACPI_NAMESPACE_NODE             *ConnectionNode;
    UINT8                           *ResourceBuffer;
    UINT32                          BankValue;
    UINT32                          FieldBitPosition;
    UINT32                          FieldBitLength;
    UINT16                          ResourceLength;
    UINT16                          PinNumberIndex;
    UINT8                           FieldFlags;
    UINT8                           Attribute;
    UINT8                           FieldType;
    UINT8                           AccessLength;

} ACPI_CREATE_FIELD_INFO;


typedef
ACPI_STATUS (*ACPI_INTERNAL_METHOD) (
    struct acpi_walk_state          *WalkState);


/*
 * Bitmapped ACPI types. Used internally only
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
#define ACPI_BTYPE_REFERENCE_OBJECT     0x00010000 /* From Index(), RefOf(), etc (Type6Opcodes) */
#define ACPI_BTYPE_RESOURCE             0x00020000
#define ACPI_BTYPE_NAMED_REFERENCE      0x00040000 /* Generic unresolved Name or Namepath */

#define ACPI_BTYPE_COMPUTE_DATA         (ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING | ACPI_BTYPE_BUFFER)

#define ACPI_BTYPE_DATA                 (ACPI_BTYPE_COMPUTE_DATA  | ACPI_BTYPE_PACKAGE)

    /* Used by Copy, DeRefOf, Store, Printf, Fprintf */

#define ACPI_BTYPE_DATA_REFERENCE       (ACPI_BTYPE_DATA | ACPI_BTYPE_REFERENCE_OBJECT | ACPI_BTYPE_DDB_HANDLE)
#define ACPI_BTYPE_DEVICE_OBJECTS       (ACPI_BTYPE_DEVICE | ACPI_BTYPE_THERMAL | ACPI_BTYPE_PROCESSOR)
#define ACPI_BTYPE_OBJECTS_AND_REFS     0x0001FFFF  /* ARG or LOCAL */
#define ACPI_BTYPE_ALL_OBJECTS          0x0000FFFF

#pragma pack(1)

/*
 * Information structure for ACPI predefined names.
 * Each entry in the table contains the following items:
 *
 * Name                 - The ACPI reserved name
 * ParamCount           - Number of arguments to the method
 * ExpectedReturnBtypes - Allowed type(s) for the return value
 */
typedef struct acpi_name_info
{
    char                        Name[ACPI_NAME_SIZE];
    UINT16                      ArgumentList;
    UINT8                       ExpectedBtypes;

} ACPI_NAME_INFO;

/*
 * Secondary information structures for ACPI predefined objects that return
 * package objects. This structure appears as the next entry in the table
 * after the NAME_INFO structure above.
 *
 * The reason for this is to minimize the size of the predefined name table.
 */

/*
 * Used for ACPI_PTYPE1_FIXED, ACPI_PTYPE1_VAR, ACPI_PTYPE2,
 * ACPI_PTYPE2_MIN, ACPI_PTYPE2_PKG_COUNT, ACPI_PTYPE2_COUNT,
 * ACPI_PTYPE2_FIX_VAR
 */
typedef struct acpi_package_info
{
    UINT8                       Type;
    UINT8                       ObjectType1;
    UINT8                       Count1;
    UINT8                       ObjectType2;
    UINT8                       Count2;
    UINT16                      Reserved;

} ACPI_PACKAGE_INFO;

/* Used for ACPI_PTYPE2_FIXED */

typedef struct acpi_package_info2
{
    UINT8                       Type;
    UINT8                       Count;
    UINT8                       ObjectType[4];
    UINT8                       Reserved;

} ACPI_PACKAGE_INFO2;

/* Used for ACPI_PTYPE1_OPTION */

typedef struct acpi_package_info3
{
    UINT8                       Type;
    UINT8                       Count;
    UINT8                       ObjectType[2];
    UINT8                       TailObjectType;
    UINT16                      Reserved;

} ACPI_PACKAGE_INFO3;

typedef struct acpi_package_info4
{
    UINT8                       Type;
    UINT8                       ObjectType1;
    UINT8                       Count1;
    UINT8                       SubObjectTypes;
    UINT8                       PkgCount;
    UINT16                      Reserved;

} ACPI_PACKAGE_INFO4;

typedef union acpi_predefined_info
{
    ACPI_NAME_INFO              Info;
    ACPI_PACKAGE_INFO           RetInfo;
    ACPI_PACKAGE_INFO2          RetInfo2;
    ACPI_PACKAGE_INFO3          RetInfo3;
    ACPI_PACKAGE_INFO4          RetInfo4;

} ACPI_PREDEFINED_INFO;

/* Reset to default packing */

#pragma pack()


/* Return object auto-repair info */

typedef ACPI_STATUS (*ACPI_OBJECT_CONVERTER) (
    struct acpi_namespace_node  *Scope,
    union acpi_operand_object   *OriginalObject,
    union acpi_operand_object   **ConvertedObject);

typedef struct acpi_simple_repair_info
{
    char                        Name[ACPI_NAME_SIZE];
    UINT32                      UnexpectedBtypes;
    UINT32                      PackageIndex;
    ACPI_OBJECT_CONVERTER       ObjectConverter;

} ACPI_SIMPLE_REPAIR_INFO;


/*
 * Bitmapped return value types
 * Note: the actual data types must be contiguous, a loop in nspredef.c
 * depends on this.
 */
#define ACPI_RTYPE_ANY                  0x00
#define ACPI_RTYPE_NONE                 0x01
#define ACPI_RTYPE_INTEGER              0x02
#define ACPI_RTYPE_STRING               0x04
#define ACPI_RTYPE_BUFFER               0x08
#define ACPI_RTYPE_PACKAGE              0x10
#define ACPI_RTYPE_REFERENCE            0x20
#define ACPI_RTYPE_ALL                  0x3F

#define ACPI_NUM_RTYPES                 5   /* Number of actual object types */


/* Info for running the _REG methods */

typedef struct acpi_reg_walk_info
{
    UINT32                  Function;
    UINT32                  RegRunCount;
    ACPI_ADR_SPACE_TYPE     SpaceId;

} ACPI_REG_WALK_INFO;


/*****************************************************************************
 *
 * Event typedefs and structs
 *
 ****************************************************************************/

/* Dispatch info for each host-installed SCI handler */

typedef struct acpi_sci_handler_info
{
    struct acpi_sci_handler_info    *Next;
    ACPI_SCI_HANDLER                Address;        /* Address of handler */
    void                            *Context;       /* Context to be passed to handler */

} ACPI_SCI_HANDLER_INFO;

/* Dispatch info for each GPE -- either a method or handler, cannot be both */

typedef struct acpi_gpe_handler_info
{
    ACPI_GPE_HANDLER                Address;        /* Address of handler, if any */
    void                            *Context;       /* Context to be passed to handler */
    ACPI_NAMESPACE_NODE             *MethodNode;    /* Method node for this GPE level (saved) */
    UINT8                           OriginalFlags;  /* Original (pre-handler) GPE info */
    BOOLEAN                         OriginallyEnabled; /* True if GPE was originally enabled */

} ACPI_GPE_HANDLER_INFO;

/* Notify info for implicit notify, multiple device objects */

typedef struct acpi_gpe_notify_info
{
    ACPI_NAMESPACE_NODE             *DeviceNode;    /* Device to be notified */
    struct acpi_gpe_notify_info     *Next;

} ACPI_GPE_NOTIFY_INFO;

/*
 * GPE dispatch info. At any time, the GPE can have at most one type
 * of dispatch - Method, Handler, or Implicit Notify.
 */
typedef union acpi_gpe_dispatch_info
{
    ACPI_NAMESPACE_NODE             *MethodNode;    /* Method node for this GPE level */
    ACPI_GPE_HANDLER_INFO           *Handler;       /* Installed GPE handler */
    ACPI_GPE_NOTIFY_INFO            *NotifyList;    /* List of _PRW devices for implicit notifies */

} ACPI_GPE_DISPATCH_INFO;

/*
 * Information about a GPE, one per each GPE in an array.
 * NOTE: Important to keep this struct as small as possible.
 */
typedef struct acpi_gpe_event_info
{
    union acpi_gpe_dispatch_info    Dispatch;           /* Either Method, Handler, or NotifyList */
    struct acpi_gpe_register_info   *RegisterInfo;      /* Backpointer to register info */
    UINT8                           Flags;              /* Misc info about this GPE */
    UINT8                           GpeNumber;          /* This GPE */
    UINT8                           RuntimeCount;       /* References to a run GPE */
    BOOLEAN                         DisableForDispatch; /* Masked during dispatching */

} ACPI_GPE_EVENT_INFO;

/* Information about a GPE register pair, one per each status/enable pair in an array */

typedef struct acpi_gpe_register_info
{
    ACPI_GENERIC_ADDRESS            StatusAddress;  /* Address of status reg */
    ACPI_GENERIC_ADDRESS            EnableAddress;  /* Address of enable reg */
    UINT16                          BaseGpeNumber;  /* Base GPE number for this register */
    UINT8                           EnableForWake;  /* GPEs to keep enabled when sleeping */
    UINT8                           EnableForRun;   /* GPEs to keep enabled when running */
    UINT8                           MaskForRun;     /* GPEs to keep masked when running */
    UINT8                           EnableMask;     /* Current mask of enabled GPEs */

} ACPI_GPE_REGISTER_INFO;

/*
 * Information about a GPE register block, one per each installed block --
 * GPE0, GPE1, and one per each installed GPE Block Device.
 */
typedef struct acpi_gpe_block_info
{
    ACPI_NAMESPACE_NODE             *Node;
    struct acpi_gpe_block_info      *Previous;
    struct acpi_gpe_block_info      *Next;
    struct acpi_gpe_xrupt_info      *XruptBlock;    /* Backpointer to interrupt block */
    ACPI_GPE_REGISTER_INFO          *RegisterInfo;  /* One per GPE register pair */
    ACPI_GPE_EVENT_INFO             *EventInfo;     /* One for each GPE */
    UINT64                          Address;        /* Base address of the block */
    UINT32                          RegisterCount;  /* Number of register pairs in block */
    UINT16                          GpeCount;       /* Number of individual GPEs in block */
    UINT16                          BlockBaseNumber;/* Base GPE number for this block */
    UINT8                           SpaceId;
    BOOLEAN                         Initialized;    /* TRUE if this block is initialized */

} ACPI_GPE_BLOCK_INFO;

/* Information about GPE interrupt handlers, one per each interrupt level used for GPEs */

typedef struct acpi_gpe_xrupt_info
{
    struct acpi_gpe_xrupt_info      *Previous;
    struct acpi_gpe_xrupt_info      *Next;
    ACPI_GPE_BLOCK_INFO             *GpeBlockListHead;  /* List of GPE blocks for this xrupt */
    UINT32                          InterruptNumber;    /* System interrupt number */

} ACPI_GPE_XRUPT_INFO;

typedef struct acpi_gpe_walk_info
{
    ACPI_NAMESPACE_NODE             *GpeDevice;
    ACPI_GPE_BLOCK_INFO             *GpeBlock;
    UINT16                          Count;
    ACPI_OWNER_ID                   OwnerId;
    BOOLEAN                         ExecuteByOwnerId;

} ACPI_GPE_WALK_INFO;

typedef struct acpi_gpe_device_info
{
    UINT32                          Index;
    UINT32                          NextBlockBaseIndex;
    ACPI_STATUS                     Status;
    ACPI_NAMESPACE_NODE             *GpeDevice;

} ACPI_GPE_DEVICE_INFO;

typedef ACPI_STATUS (*ACPI_GPE_CALLBACK) (
    ACPI_GPE_XRUPT_INFO             *GpeXruptInfo,
    ACPI_GPE_BLOCK_INFO             *GpeBlock,
    void                            *Context);


/* Information about each particular fixed event */

typedef struct acpi_fixed_event_handler
{
    ACPI_EVENT_HANDLER              Handler;        /* Address of handler. */
    void                            *Context;       /* Context to be passed to handler */

} ACPI_FIXED_EVENT_HANDLER;

typedef struct acpi_fixed_event_info
{
    UINT8                           StatusRegisterId;
    UINT8                           EnableRegisterId;
    UINT16                          StatusBitMask;
    UINT16                          EnableBitMask;

} ACPI_FIXED_EVENT_INFO;

/* Information used during field processing */

typedef struct acpi_field_info
{
    UINT8                           SkipField;
    UINT8                           FieldFlag;
    UINT32                          PkgLength;

} ACPI_FIELD_INFO;


/*****************************************************************************
 *
 * Generic "state" object for stacks
 *
 ****************************************************************************/

#define ACPI_CONTROL_NORMAL                  0xC0
#define ACPI_CONTROL_CONDITIONAL_EXECUTING   0xC1
#define ACPI_CONTROL_PREDICATE_EXECUTING     0xC2
#define ACPI_CONTROL_PREDICATE_FALSE         0xC3
#define ACPI_CONTROL_PREDICATE_TRUE          0xC4


#define ACPI_STATE_COMMON \
    void                            *Next; \
    UINT8                           DescriptorType; /* To differentiate various internal objs */\
    UINT8                           Flags; \
    UINT16                          Value; \
    UINT16                          State;

    /* There are 2 bytes available here until the next natural alignment boundary */

typedef struct acpi_common_state
{
    ACPI_STATE_COMMON
} ACPI_COMMON_STATE;


/*
 * Update state - used to traverse complex objects such as packages
 */
typedef struct acpi_update_state
{
    ACPI_STATE_COMMON
    union acpi_operand_object       *Object;

} ACPI_UPDATE_STATE;


/*
 * Pkg state - used to traverse nested package structures
 */
typedef struct acpi_pkg_state
{
    ACPI_STATE_COMMON
    UINT32                          Index;
    union acpi_operand_object       *SourceObject;
    union acpi_operand_object       *DestObject;
    struct acpi_walk_state          *WalkState;
    void                            *ThisTargetObj;
    UINT32                          NumPackages;

} ACPI_PKG_STATE;


/*
 * Control state - one per if/else and while constructs.
 * Allows nesting of these constructs
 */
typedef struct acpi_control_state
{
    ACPI_STATE_COMMON
    UINT16                          Opcode;
    union acpi_parse_object         *PredicateOp;
    UINT8                           *AmlPredicateStart;     /* Start of if/while predicate */
    UINT8                           *PackageEnd;            /* End of if/while block */
    UINT64                          LoopTimeout;            /* While() loop timeout */

} ACPI_CONTROL_STATE;


/*
 * Scope state - current scope during namespace lookups
 */
typedef struct acpi_scope_state
{
    ACPI_STATE_COMMON
    ACPI_NAMESPACE_NODE             *Node;

} ACPI_SCOPE_STATE;


typedef struct acpi_pscope_state
{
    ACPI_STATE_COMMON
    UINT32                          ArgCount;               /* Number of fixed arguments */
    union acpi_parse_object         *Op;                    /* Current op being parsed */
    UINT8                           *ArgEnd;                /* Current argument end */
    UINT8                           *PkgEnd;                /* Current package end */
    UINT32                          ArgList;                /* Next argument to parse */

} ACPI_PSCOPE_STATE;


/*
 * Thread state - one per thread across multiple walk states. Multiple walk
 * states are created when there are nested control methods executing.
 */
typedef struct acpi_thread_state
{
    ACPI_STATE_COMMON
    UINT8                           CurrentSyncLevel;       /* Mutex Sync (nested acquire) level */
    struct acpi_walk_state          *WalkStateList;         /* Head of list of WalkStates for this thread */
    union acpi_operand_object       *AcquiredMutexList;     /* List of all currently acquired mutexes */
    ACPI_THREAD_ID                  ThreadId;               /* Running thread ID */

} ACPI_THREAD_STATE;


/*
 * Result values - used to accumulate the results of nested
 * AML arguments
 */
typedef struct acpi_result_values
{
    ACPI_STATE_COMMON
    union acpi_operand_object       *ObjDesc [ACPI_RESULTS_FRAME_OBJ_NUM];

} ACPI_RESULT_VALUES;


typedef
ACPI_STATUS (*ACPI_PARSE_DOWNWARDS) (
    struct acpi_walk_state          *WalkState,
    union acpi_parse_object         **OutOp);

typedef
ACPI_STATUS (*ACPI_PARSE_UPWARDS) (
    struct acpi_walk_state          *WalkState);


/* Global handlers for AML Notifies */

typedef struct acpi_global_notify_handler
{
    ACPI_NOTIFY_HANDLER             Handler;
    void                            *Context;

} ACPI_GLOBAL_NOTIFY_HANDLER;

/*
 * Notify info - used to pass info to the deferred notify
 * handler/dispatcher.
 */
typedef struct acpi_notify_info
{
    ACPI_STATE_COMMON
    UINT8                           HandlerListId;
    ACPI_NAMESPACE_NODE             *Node;
    union acpi_operand_object       *HandlerListHead;
    ACPI_GLOBAL_NOTIFY_HANDLER      *Global;

} ACPI_NOTIFY_INFO;


/* Generic state is union of structs above */

typedef union acpi_generic_state
{
    ACPI_COMMON_STATE               Common;
    ACPI_CONTROL_STATE              Control;
    ACPI_UPDATE_STATE               Update;
    ACPI_SCOPE_STATE                Scope;
    ACPI_PSCOPE_STATE               ParseScope;
    ACPI_PKG_STATE                  Pkg;
    ACPI_THREAD_STATE               Thread;
    ACPI_RESULT_VALUES              Results;
    ACPI_NOTIFY_INFO                Notify;

} ACPI_GENERIC_STATE;


/*****************************************************************************
 *
 * Interpreter typedefs and structs
 *
 ****************************************************************************/

typedef
ACPI_STATUS (*ACPI_EXECUTE_OP) (
    struct acpi_walk_state          *WalkState);

/* Address Range info block */

typedef struct acpi_address_range
{
    struct acpi_address_range   *Next;
    ACPI_NAMESPACE_NODE         *RegionNode;
    ACPI_PHYSICAL_ADDRESS       StartAddress;
    ACPI_PHYSICAL_ADDRESS       EndAddress;

} ACPI_ADDRESS_RANGE;


/*****************************************************************************
 *
 * Parser typedefs and structs
 *
 ****************************************************************************/

/*
 * AML opcode, name, and argument layout
 */
typedef struct acpi_opcode_info
{
#if defined(ACPI_DISASSEMBLER) || defined(ACPI_DEBUG_OUTPUT)
    char                            *Name;          /* Opcode name (disassembler/debug only) */
#endif
    UINT32                          ParseArgs;      /* Grammar/Parse time arguments */
    UINT32                          RuntimeArgs;    /* Interpret time arguments */
    UINT16                          Flags;          /* Misc flags */
    UINT8                           ObjectType;     /* Corresponding internal object type */
    UINT8                           Class;          /* Opcode class */
    UINT8                           Type;           /* Opcode type */

} ACPI_OPCODE_INFO;

/* Structure for Resource Tag information */

typedef struct acpi_tag_info
{
    UINT32                          BitOffset;
    UINT32                          BitLength;

} ACPI_TAG_INFO;

/* Value associated with the parse object */

typedef union acpi_parse_value
{
    UINT64                          Integer;        /* Integer constant (Up to 64 bits) */
    UINT32                          Size;           /* bytelist or field size */
    char                            *String;        /* NULL terminated string */
    UINT8                           *Buffer;        /* buffer or string */
    char                            *Name;          /* NULL terminated string */
    union acpi_parse_object         *Arg;           /* arguments and contained ops */
    ACPI_TAG_INFO                   Tag;            /* Resource descriptor tag info  */

} ACPI_PARSE_VALUE;


#if defined(ACPI_DISASSEMBLER) || defined(ACPI_DEBUG_OUTPUT)
#define ACPI_DISASM_ONLY_MEMBERS(a)     a;
#else
#define ACPI_DISASM_ONLY_MEMBERS(a)
#endif

#if defined(ACPI_ASL_COMPILER)
#define ACPI_CONVERTER_ONLY_MEMBERS(a)  a;
#else
#define ACPI_CONVERTER_ONLY_MEMBERS(a)
#endif

#define ACPI_PARSE_COMMON \
    union acpi_parse_object         *Parent;            /* Parent op */\
    UINT8                           DescriptorType;     /* To differentiate various internal objs */\
    UINT8                           Flags;              /* Type of Op */\
    UINT16                          AmlOpcode;          /* AML opcode */\
    UINT8                           *Aml;               /* Address of declaration in AML */\
    union acpi_parse_object         *Next;              /* Next op */\
    ACPI_NAMESPACE_NODE             *Node;              /* For use by interpreter */\
    ACPI_PARSE_VALUE                Value;              /* Value or args associated with the opcode */\
    UINT8                           ArgListLength;      /* Number of elements in the arg list */\
     ACPI_DISASM_ONLY_MEMBERS (\
    UINT16                          DisasmFlags;        /* Used during AML disassembly */\
    UINT8                           DisasmOpcode;       /* Subtype used for disassembly */\
    char                            *OperatorSymbol;    /* Used for C-style operator name strings */\
    char                            AmlOpName[16])      /* Op name (debug only) */\
     ACPI_CONVERTER_ONLY_MEMBERS (\
    char                            *InlineComment;     /* Inline comment */\
    char                            *EndNodeComment;    /* End of node comment */\
    char                            *NameComment;       /* Comment associated with the first parameter of the name node */\
    char                            *CloseBraceComment; /* Comments that come after } on the same as } */\
    ACPI_COMMENT_NODE               *CommentList;       /* comments that appears before this node */\
    ACPI_COMMENT_NODE               *EndBlkComment;     /* comments that at the end of a block but before ) or } */\
    char                            *CvFilename;        /* Filename associated with this node. Used for ASL/ASL+ converter */\
    char                            *CvParentFilename)  /* Parent filename associated with this node. Used for ASL/ASL+ converter */


/* categories of comments */

typedef enum
{
    STANDARD_COMMENT = 1,
    INLINE_COMMENT,
    ENDNODE_COMMENT,
    OPENBRACE_COMMENT,
    CLOSE_BRACE_COMMENT,
    STD_DEFBLK_COMMENT,
    END_DEFBLK_COMMENT,
    FILENAME_COMMENT,
    PARENTFILENAME_COMMENT,
    ENDBLK_COMMENT,
    INCLUDE_COMMENT

} ASL_COMMENT_TYPES;


/* Internal opcodes for DisasmOpcode field above */

#define ACPI_DASM_BUFFER                0x00        /* Buffer is a simple data buffer */
#define ACPI_DASM_RESOURCE              0x01        /* Buffer is a Resource Descriptor */
#define ACPI_DASM_STRING                0x02        /* Buffer is a ASCII string */
#define ACPI_DASM_UNICODE               0x03        /* Buffer is a Unicode string */
#define ACPI_DASM_PLD_METHOD            0x04        /* Buffer is a _PLD method bit-packed buffer */
#define ACPI_DASM_UUID                  0x05        /* Buffer is a UUID/GUID */
#define ACPI_DASM_EISAID                0x06        /* Integer is an EISAID */
#define ACPI_DASM_MATCHOP               0x07        /* Parent opcode is a Match() operator */
#define ACPI_DASM_LNOT_PREFIX           0x08        /* Start of a LNotEqual (etc.) pair of opcodes */
#define ACPI_DASM_LNOT_SUFFIX           0x09        /* End  of a LNotEqual (etc.) pair of opcodes */
#define ACPI_DASM_HID_STRING            0x0A        /* String is a _HID or _CID */
#define ACPI_DASM_IGNORE_SINGLE         0x0B        /* Ignore the opcode but not it's children */
#define ACPI_DASM_SWITCH                0x0C        /* While is a Switch */
#define ACPI_DASM_SWITCH_PREDICATE      0x0D        /* Object is a predicate for a Switch or Case block */
#define ACPI_DASM_CASE                  0x0E        /* If/Else is a Case in a Switch/Case block */
#define ACPI_DASM_DEFAULT               0x0F        /* Else is a Default in a Switch/Case block */


/*
 * List struct used in the -ca option
 */
typedef struct acpi_comment_node
{
  char                      *Comment;
  struct acpi_comment_node  *Next;

} ACPI_COMMENT_NODE;


typedef struct acpi_comment_addr_node
{
  UINT8                                    *Addr;
  struct acpi_comment_addr_node            *Next;
} ACPI_COMMENT_ADDR_NODE;

/*
 * File node - used for "Include" operator file stack and
 * dependency tree for the -ca option
 */
typedef struct acpi_file_node
{
    void                    *File;
    char                    *Filename;
    char                    *FileStart;  /* Points to AML and indicates when the AML for this particular file starts. */
    char                    *FileEnd;    /* Points to AML and indicates when the AML for this particular file ends. */
    struct acpi_file_node   *Next;
    struct acpi_file_node   *Parent;
    BOOLEAN                 IncludeWritten;
    ACPI_COMMENT_NODE       *IncludeComment;

} ACPI_FILE_NODE;


/*
 * Generic operation (for example:  If, While, Store)
 */
typedef struct acpi_parse_obj_common
{
    ACPI_PARSE_COMMON
} ACPI_PARSE_OBJ_COMMON;


/*
 * Extended Op for named ops (Scope, Method, etc.), deferred ops (Methods and OpRegions),
 * and bytelists.
 */
typedef struct acpi_parse_obj_named
{
    ACPI_PARSE_COMMON
    char                            *Path;
    UINT8                           *Data;          /* AML body or bytelist data */
    UINT32                          Length;         /* AML length */
    UINT32                          Name;           /* 4-byte name or zero if no name */

} ACPI_PARSE_OBJ_NAMED;


/* This version is used by the iASL compiler only */

#define ACPI_MAX_PARSEOP_NAME       20

typedef struct acpi_parse_obj_asl
{
    ACPI_PARSE_COMMON
    union acpi_parse_object         *Child;
    union acpi_parse_object         *ParentMethod;
    char                            *Filename;
    BOOLEAN                         FileChanged;
    char                            *ParentFilename;
    char                            *ExternalName;
    char                            *Namepath;
    char                            NameSeg[4];
    UINT32                          ExtraValue;
    UINT32                          Column;
    UINT32                          LineNumber;
    UINT32                          LogicalLineNumber;
    UINT32                          LogicalByteOffset;
    UINT32                          EndLine;
    UINT32                          EndLogicalLine;
    UINT32                          AcpiBtype;
    UINT32                          AmlLength;
    UINT32                          AmlSubtreeLength;
    UINT32                          FinalAmlLength;
    UINT32                          FinalAmlOffset;
    UINT32                          CompileFlags;
    UINT16                          ParseOpcode;
    UINT8                           AmlOpcodeLength;
    UINT8                           AmlPkgLenBytes;
    UINT8                           Extra;
    char                            ParseOpName[ACPI_MAX_PARSEOP_NAME];

} ACPI_PARSE_OBJ_ASL;

typedef union acpi_parse_object
{
    ACPI_PARSE_OBJ_COMMON           Common;
    ACPI_PARSE_OBJ_NAMED            Named;
    ACPI_PARSE_OBJ_ASL              Asl;

} ACPI_PARSE_OBJECT;

typedef struct asl_comment_state
{
    UINT8                           CommentType;
    UINT32                          SpacesBefore;
    ACPI_PARSE_OBJECT               *LatestParseOp;
    ACPI_PARSE_OBJECT               *ParsingParenBraceNode;
    BOOLEAN                         CaptureComments;

} ASL_COMMENT_STATE;


/*
 * Parse state - one state per parser invocation and each control
 * method.
 */
typedef struct acpi_parse_state
{
    UINT8                           *AmlStart;      /* First AML byte */
    UINT8                           *Aml;           /* Next AML byte */
    UINT8                           *AmlEnd;        /* (last + 1) AML byte */
    UINT8                           *PkgStart;      /* Current package begin */
    UINT8                           *PkgEnd;        /* Current package end */
    union acpi_parse_object         *StartOp;       /* Root of parse tree */
    struct acpi_namespace_node      *StartNode;
    union acpi_generic_state        *Scope;         /* Current scope */
    union acpi_parse_object         *StartScope;
    UINT32                          AmlSize;

} ACPI_PARSE_STATE;


/* Parse object flags */

#define ACPI_PARSEOP_GENERIC                0x01
#define ACPI_PARSEOP_NAMED_OBJECT           0x02
#define ACPI_PARSEOP_DEFERRED               0x04
#define ACPI_PARSEOP_BYTELIST               0x08
#define ACPI_PARSEOP_IN_STACK               0x10
#define ACPI_PARSEOP_TARGET                 0x20
#define ACPI_PARSEOP_IN_CACHE               0x80

/* Parse object DisasmFlags */

#define ACPI_PARSEOP_IGNORE                 0x0001
#define ACPI_PARSEOP_PARAMETER_LIST         0x0002
#define ACPI_PARSEOP_EMPTY_TERMLIST         0x0004
#define ACPI_PARSEOP_PREDEFINED_CHECKED     0x0008
#define ACPI_PARSEOP_CLOSING_PAREN          0x0010
#define ACPI_PARSEOP_COMPOUND_ASSIGNMENT    0x0020
#define ACPI_PARSEOP_ASSIGNMENT             0x0040
#define ACPI_PARSEOP_ELSEIF                 0x0080
#define ACPI_PARSEOP_LEGACY_ASL_ONLY        0x0100


/*****************************************************************************
 *
 * Hardware (ACPI registers) and PNP
 *
 ****************************************************************************/

typedef struct acpi_bit_register_info
{
    UINT8                           ParentRegister;
    UINT8                           BitPosition;
    UINT16                          AccessBitMask;

} ACPI_BIT_REGISTER_INFO;


/*
 * Some ACPI registers have bits that must be ignored -- meaning that they
 * must be preserved.
 */
#define ACPI_PM1_STATUS_PRESERVED_BITS          0x0800  /* Bit 11 */

/* Write-only bits must be zeroed by software */

#define ACPI_PM1_CONTROL_WRITEONLY_BITS         0x2004  /* Bits 13, 2 */

/* For control registers, both ignored and reserved bits must be preserved */

/*
 * For PM1 control, the SCI enable bit (bit 0, SCI_EN) is defined by the
 * ACPI specification to be a "preserved" bit - "OSPM always preserves this
 * bit position", section 4.7.3.2.1. However, on some machines the OS must
 * write a one to this bit after resume for the machine to work properly.
 * To enable this, we no longer attempt to preserve this bit. No machines
 * are known to fail if the bit is not preserved. (May 2009)
 */
#define ACPI_PM1_CONTROL_IGNORED_BITS           0x0200  /* Bit 9 */
#define ACPI_PM1_CONTROL_RESERVED_BITS          0xC1F8  /* Bits 14-15, 3-8 */
#define ACPI_PM1_CONTROL_PRESERVED_BITS \
         (ACPI_PM1_CONTROL_IGNORED_BITS | ACPI_PM1_CONTROL_RESERVED_BITS)

#define ACPI_PM2_CONTROL_PRESERVED_BITS         0xFFFFFFFE /* All except bit 0 */

/*
 * Register IDs
 * These are the full ACPI registers
 */
#define ACPI_REGISTER_PM1_STATUS                0x01
#define ACPI_REGISTER_PM1_ENABLE                0x02
#define ACPI_REGISTER_PM1_CONTROL               0x03
#define ACPI_REGISTER_PM2_CONTROL               0x04
#define ACPI_REGISTER_PM_TIMER                  0x05
#define ACPI_REGISTER_PROCESSOR_BLOCK           0x06
#define ACPI_REGISTER_SMI_COMMAND_BLOCK         0x07


/* Masks used to access the BitRegisters */

#define ACPI_BITMASK_TIMER_STATUS               0x0001
#define ACPI_BITMASK_BUS_MASTER_STATUS          0x0010
#define ACPI_BITMASK_GLOBAL_LOCK_STATUS         0x0020
#define ACPI_BITMASK_POWER_BUTTON_STATUS        0x0100
#define ACPI_BITMASK_SLEEP_BUTTON_STATUS        0x0200
#define ACPI_BITMASK_RT_CLOCK_STATUS            0x0400
#define ACPI_BITMASK_PCIEXP_WAKE_STATUS         0x4000    /* ACPI 3.0 */
#define ACPI_BITMASK_WAKE_STATUS                0x8000

#define ACPI_BITMASK_ALL_FIXED_STATUS           (\
    ACPI_BITMASK_TIMER_STATUS          | \
    ACPI_BITMASK_BUS_MASTER_STATUS     | \
    ACPI_BITMASK_GLOBAL_LOCK_STATUS    | \
    ACPI_BITMASK_POWER_BUTTON_STATUS   | \
    ACPI_BITMASK_SLEEP_BUTTON_STATUS   | \
    ACPI_BITMASK_RT_CLOCK_STATUS       | \
    ACPI_BITMASK_PCIEXP_WAKE_STATUS    | \
    ACPI_BITMASK_WAKE_STATUS)

#define ACPI_BITMASK_TIMER_ENABLE               0x0001
#define ACPI_BITMASK_GLOBAL_LOCK_ENABLE         0x0020
#define ACPI_BITMASK_POWER_BUTTON_ENABLE        0x0100
#define ACPI_BITMASK_SLEEP_BUTTON_ENABLE        0x0200
#define ACPI_BITMASK_RT_CLOCK_ENABLE            0x0400
#define ACPI_BITMASK_PCIEXP_WAKE_DISABLE        0x4000    /* ACPI 3.0 */

#define ACPI_BITMASK_SCI_ENABLE                 0x0001
#define ACPI_BITMASK_BUS_MASTER_RLD             0x0002
#define ACPI_BITMASK_GLOBAL_LOCK_RELEASE        0x0004
#define ACPI_BITMASK_SLEEP_TYPE                 0x1C00
#define ACPI_BITMASK_SLEEP_ENABLE               0x2000

#define ACPI_BITMASK_ARB_DISABLE                0x0001


/* Raw bit position of each BitRegister */

#define ACPI_BITPOSITION_TIMER_STATUS           0x00
#define ACPI_BITPOSITION_BUS_MASTER_STATUS      0x04
#define ACPI_BITPOSITION_GLOBAL_LOCK_STATUS     0x05
#define ACPI_BITPOSITION_POWER_BUTTON_STATUS    0x08
#define ACPI_BITPOSITION_SLEEP_BUTTON_STATUS    0x09
#define ACPI_BITPOSITION_RT_CLOCK_STATUS        0x0A
#define ACPI_BITPOSITION_PCIEXP_WAKE_STATUS     0x0E    /* ACPI 3.0 */
#define ACPI_BITPOSITION_WAKE_STATUS            0x0F

#define ACPI_BITPOSITION_TIMER_ENABLE           0x00
#define ACPI_BITPOSITION_GLOBAL_LOCK_ENABLE     0x05
#define ACPI_BITPOSITION_POWER_BUTTON_ENABLE    0x08
#define ACPI_BITPOSITION_SLEEP_BUTTON_ENABLE    0x09
#define ACPI_BITPOSITION_RT_CLOCK_ENABLE        0x0A
#define ACPI_BITPOSITION_PCIEXP_WAKE_DISABLE    0x0E    /* ACPI 3.0 */

#define ACPI_BITPOSITION_SCI_ENABLE             0x00
#define ACPI_BITPOSITION_BUS_MASTER_RLD         0x01
#define ACPI_BITPOSITION_GLOBAL_LOCK_RELEASE    0x02
#define ACPI_BITPOSITION_SLEEP_TYPE             0x0A
#define ACPI_BITPOSITION_SLEEP_ENABLE           0x0D

#define ACPI_BITPOSITION_ARB_DISABLE            0x00


/* Structs and definitions for _OSI support and I/O port validation */

#define ACPI_ALWAYS_ILLEGAL             0x00

typedef struct acpi_interface_info
{
    char                        *Name;
    struct acpi_interface_info  *Next;
    UINT8                       Flags;
    UINT8                       Value;

} ACPI_INTERFACE_INFO;

#define ACPI_OSI_INVALID                0x01
#define ACPI_OSI_DYNAMIC                0x02
#define ACPI_OSI_FEATURE                0x04
#define ACPI_OSI_DEFAULT_INVALID        0x08
#define ACPI_OSI_OPTIONAL_FEATURE       (ACPI_OSI_FEATURE | ACPI_OSI_DEFAULT_INVALID | ACPI_OSI_INVALID)

typedef struct acpi_port_info
{
    char                    *Name;
    UINT16                  Start;
    UINT16                  End;
    UINT8                   OsiDependency;

} ACPI_PORT_INFO;


/*****************************************************************************
 *
 * Resource descriptors
 *
 ****************************************************************************/

/* ResourceType values */

#define ACPI_ADDRESS_TYPE_MEMORY_RANGE          0
#define ACPI_ADDRESS_TYPE_IO_RANGE              1
#define ACPI_ADDRESS_TYPE_BUS_NUMBER_RANGE      2

/* Resource descriptor types and masks */

#define ACPI_RESOURCE_NAME_LARGE                0x80
#define ACPI_RESOURCE_NAME_SMALL                0x00

#define ACPI_RESOURCE_NAME_SMALL_MASK           0x78 /* Bits 6:3 contain the type */
#define ACPI_RESOURCE_NAME_SMALL_LENGTH_MASK    0x07 /* Bits 2:0 contain the length */
#define ACPI_RESOURCE_NAME_LARGE_MASK           0x7F /* Bits 6:0 contain the type */


/*
 * Small resource descriptor "names" as defined by the ACPI specification.
 * Note: Bits 2:0 are used for the descriptor length
 */
#define ACPI_RESOURCE_NAME_IRQ                  0x20
#define ACPI_RESOURCE_NAME_DMA                  0x28
#define ACPI_RESOURCE_NAME_START_DEPENDENT      0x30
#define ACPI_RESOURCE_NAME_END_DEPENDENT        0x38
#define ACPI_RESOURCE_NAME_IO                   0x40
#define ACPI_RESOURCE_NAME_FIXED_IO             0x48
#define ACPI_RESOURCE_NAME_FIXED_DMA            0x50
#define ACPI_RESOURCE_NAME_RESERVED_S2          0x58
#define ACPI_RESOURCE_NAME_RESERVED_S3          0x60
#define ACPI_RESOURCE_NAME_RESERVED_S4          0x68
#define ACPI_RESOURCE_NAME_VENDOR_SMALL         0x70
#define ACPI_RESOURCE_NAME_END_TAG              0x78

/*
 * Large resource descriptor "names" as defined by the ACPI specification.
 * Note: includes the Large Descriptor bit in bit[7]
 */
#define ACPI_RESOURCE_NAME_MEMORY24             0x81
#define ACPI_RESOURCE_NAME_GENERIC_REGISTER     0x82
#define ACPI_RESOURCE_NAME_RESERVED_L1          0x83
#define ACPI_RESOURCE_NAME_VENDOR_LARGE         0x84
#define ACPI_RESOURCE_NAME_MEMORY32             0x85
#define ACPI_RESOURCE_NAME_FIXED_MEMORY32       0x86
#define ACPI_RESOURCE_NAME_ADDRESS32            0x87
#define ACPI_RESOURCE_NAME_ADDRESS16            0x88
#define ACPI_RESOURCE_NAME_EXTENDED_IRQ         0x89
#define ACPI_RESOURCE_NAME_ADDRESS64            0x8A
#define ACPI_RESOURCE_NAME_EXTENDED_ADDRESS64   0x8B
#define ACPI_RESOURCE_NAME_GPIO                 0x8C
#define ACPI_RESOURCE_NAME_PIN_FUNCTION         0x8D
#define ACPI_RESOURCE_NAME_SERIAL_BUS           0x8E
#define ACPI_RESOURCE_NAME_PIN_CONFIG           0x8F
#define ACPI_RESOURCE_NAME_PIN_GROUP            0x90
#define ACPI_RESOURCE_NAME_PIN_GROUP_FUNCTION   0x91
#define ACPI_RESOURCE_NAME_PIN_GROUP_CONFIG     0x92
#define ACPI_RESOURCE_NAME_LARGE_MAX            0x92


/*****************************************************************************
 *
 * Miscellaneous
 *
 ****************************************************************************/

#define ACPI_ASCII_ZERO                 0x30


/*****************************************************************************
 *
 * Disassembler
 *
 ****************************************************************************/

typedef struct acpi_external_list
{
    char                        *Path;
    char                        *InternalPath;
    struct acpi_external_list   *Next;
    UINT32                      Value;
    UINT16                      Length;
    UINT16                      Flags;
    UINT8                       Type;

} ACPI_EXTERNAL_LIST;

/* Values for Flags field above */

#define ACPI_EXT_RESOLVED_REFERENCE         0x01    /* Object was resolved during cross ref */
#define ACPI_EXT_ORIGIN_FROM_FILE           0x02    /* External came from a file */
#define ACPI_EXT_INTERNAL_PATH_ALLOCATED    0x04    /* Deallocate internal path on completion */
#define ACPI_EXT_EXTERNAL_EMITTED           0x08    /* External() statement has been emitted */
#define ACPI_EXT_ORIGIN_FROM_OPCODE         0x10    /* External came from a External() opcode */
#define ACPI_EXT_CONFLICTING_DECLARATION    0x20    /* External has a conflicting declaration within AML */


typedef struct acpi_external_file
{
    char                        *Path;
    struct acpi_external_file   *Next;

} ACPI_EXTERNAL_FILE;


typedef struct acpi_parse_object_list
{
    ACPI_PARSE_OBJECT               *Op;
    struct acpi_parse_object_list   *Next;

} ACPI_PARSE_OBJECT_LIST;

/*****************************************************************************
 *
 * Debugger
 *
 ****************************************************************************/

typedef struct acpi_db_method_info
{
    ACPI_HANDLE                     Method;
    ACPI_HANDLE                     MainThreadGate;
    ACPI_HANDLE                     ThreadCompleteGate;
    ACPI_HANDLE                     InfoGate;
    ACPI_THREAD_ID                  *Threads;
    UINT32                          NumThreads;
    UINT32                          NumCreated;
    UINT32                          NumCompleted;

    char                            *Name;
    UINT32                          Flags;
    UINT32                          NumLoops;
    char                            Pathname[ACPI_DB_LINE_BUFFER_SIZE];
    char                            **Args;
    ACPI_OBJECT_TYPE                *Types;

    /*
     * Arguments to be passed to method for the commands Threads and
     * Background. Note, ACPI specifies a maximum of 7 arguments (0 - 6).
     *
     * For the Threads command, the Number of threads, ID of current
     * thread and Index of current thread inside all them created.
     */
    char                            InitArgs;
#ifdef ACPI_DEBUGGER
    ACPI_OBJECT_TYPE                ArgTypes[ACPI_METHOD_NUM_ARGS];
#endif
    char                            *Arguments[ACPI_METHOD_NUM_ARGS];
    char                            NumThreadsStr[11];
    char                            IdOfThreadStr[11];
    char                            IndexOfThreadStr[11];

} ACPI_DB_METHOD_INFO;

typedef struct acpi_integrity_info
{
    UINT32                          Nodes;
    UINT32                          Objects;

} ACPI_INTEGRITY_INFO;


#define ACPI_DB_DISABLE_OUTPUT          0x00
#define ACPI_DB_REDIRECTABLE_OUTPUT     0x01
#define ACPI_DB_CONSOLE_OUTPUT          0x02
#define ACPI_DB_DUPLICATE_OUTPUT        0x03


typedef struct acpi_object_info
{
    UINT32                  Types[ACPI_TOTAL_TYPES];

} ACPI_OBJECT_INFO;


/*****************************************************************************
 *
 * Debug
 *
 ****************************************************************************/

/* Entry for a memory allocation (debug only) */

#define ACPI_MEM_MALLOC                 0
#define ACPI_MEM_CALLOC                 1
#define ACPI_MAX_MODULE_NAME            16

#define ACPI_COMMON_DEBUG_MEM_HEADER \
    struct acpi_debug_mem_block     *Previous; \
    struct acpi_debug_mem_block     *Next; \
    UINT32                          Size; \
    UINT32                          Component; \
    UINT32                          Line; \
    char                            Module[ACPI_MAX_MODULE_NAME]; \
    UINT8                           AllocType;

typedef struct acpi_debug_mem_header
{
    ACPI_COMMON_DEBUG_MEM_HEADER

} ACPI_DEBUG_MEM_HEADER;

typedef struct acpi_debug_mem_block
{
    ACPI_COMMON_DEBUG_MEM_HEADER
    UINT64                          UserSpace;

} ACPI_DEBUG_MEM_BLOCK;


#define ACPI_MEM_LIST_GLOBAL            0
#define ACPI_MEM_LIST_NSNODE            1
#define ACPI_MEM_LIST_MAX               1
#define ACPI_NUM_MEM_LISTS              2


/*****************************************************************************
 *
 * Info/help support
 *
 ****************************************************************************/

typedef struct ah_predefined_name
{
    char            *Name;
    char            *Description;
#ifndef ACPI_ASL_COMPILER
    char            *Action;
#endif

} AH_PREDEFINED_NAME;

typedef struct ah_device_id
{
    char            *Name;
    char            *Description;

} AH_DEVICE_ID;

typedef struct ah_uuid
{
    char            *Description;
    char            *String;

} AH_UUID;

typedef struct ah_table
{
    char                    *Signature;
    char                    *Description;

} AH_TABLE;

#endif /* __ACLOCAL_H__ */

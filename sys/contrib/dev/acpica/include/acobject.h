/******************************************************************************
 *
 * Name: acobject.h - Definition of ACPI_OPERAND_OBJECT  (Internal object only)
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

#ifndef _ACOBJECT_H
#define _ACOBJECT_H

/* acpisrc:StructDefs -- for acpisrc conversion */


/*
 * The ACPI_OPERAND_OBJECT is used to pass AML operands from the dispatcher
 * to the interpreter, and to keep track of the various handlers such as
 * address space handlers and notify handlers. The object is a constant
 * size in order to allow it to be cached and reused.
 *
 * Note: The object is optimized to be aligned and will not work if it is
 * byte-packed.
 */
#if ACPI_MACHINE_WIDTH == 64
#pragma pack(8)
#else
#pragma pack(4)
#endif

/*******************************************************************************
 *
 * Common Descriptors
 *
 ******************************************************************************/

/*
 * Common area for all objects.
 *
 * DescriptorType is used to differentiate between internal descriptors, and
 * must be in the same place across all descriptors
 *
 * Note: The DescriptorType and Type fields must appear in the identical
 * position in both the ACPI_NAMESPACE_NODE and ACPI_OPERAND_OBJECT
 * structures.
 */
#define ACPI_OBJECT_COMMON_HEADER \
    union acpi_operand_object       *NextObject;        /* Objects linked to parent NS node */\
    UINT8                           DescriptorType;     /* To differentiate various internal objs */\
    UINT8                           Type;               /* ACPI_OBJECT_TYPE */\
    UINT16                          ReferenceCount;     /* For object deletion management */\
    UINT8                           Flags;
    /*
     * Note: There are 3 bytes available here before the
     * next natural alignment boundary (for both 32/64 cases)
     */

/* Values for Flag byte above */

#define AOPOBJ_AML_CONSTANT         0x01    /* Integer is an AML constant */
#define AOPOBJ_STATIC_POINTER       0x02    /* Data is part of an ACPI table, don't delete */
#define AOPOBJ_DATA_VALID           0x04    /* Object is initialized and data is valid */
#define AOPOBJ_OBJECT_INITIALIZED   0x08    /* Region is initialized */
#define AOPOBJ_REG_CONNECTED        0x10    /* _REG was run */
#define AOPOBJ_SETUP_COMPLETE       0x20    /* Region setup is complete */
#define AOPOBJ_INVALID              0x40    /* Host OS won't allow a Region address */


/******************************************************************************
 *
 * Basic data types
 *
 *****************************************************************************/

typedef struct acpi_object_common
{
    ACPI_OBJECT_COMMON_HEADER

} ACPI_OBJECT_COMMON;


typedef struct acpi_object_integer
{
    ACPI_OBJECT_COMMON_HEADER
    UINT8                           Fill[3];            /* Prevent warning on some compilers */
    UINT64                          Value;

} ACPI_OBJECT_INTEGER;


/*
 * Note: The String and Buffer object must be identical through the
 * pointer and length elements. There is code that depends on this.
 *
 * Fields common to both Strings and Buffers
 */
#define ACPI_COMMON_BUFFER_INFO(_Type) \
    _Type                           *Pointer; \
    UINT32                          Length;


/* Null terminated, ASCII characters only */

typedef struct acpi_object_string
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_BUFFER_INFO         (char)              /* String in AML stream or allocated string */

} ACPI_OBJECT_STRING;


typedef struct acpi_object_buffer
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_BUFFER_INFO         (UINT8)             /* Buffer in AML stream or allocated buffer */
    UINT32                          AmlLength;
    UINT8                           *AmlStart;
    ACPI_NAMESPACE_NODE             *Node;              /* Link back to parent node */

} ACPI_OBJECT_BUFFER;


typedef struct acpi_object_package
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_NAMESPACE_NODE             *Node;              /* Link back to parent node */
    union acpi_operand_object       **Elements;         /* Array of pointers to AcpiObjects */
    UINT8                           *AmlStart;
    UINT32                          AmlLength;
    UINT32                          Count;              /* # of elements in package */

} ACPI_OBJECT_PACKAGE;


/******************************************************************************
 *
 * Complex data types
 *
 *****************************************************************************/

typedef struct acpi_object_event
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_SEMAPHORE                  OsSemaphore;        /* Actual OS synchronization object */

} ACPI_OBJECT_EVENT;


typedef struct acpi_object_mutex
{
    ACPI_OBJECT_COMMON_HEADER
    UINT8                           SyncLevel;          /* 0-15, specified in Mutex() call */
    UINT16                          AcquisitionDepth;   /* Allow multiple Acquires, same thread */
    ACPI_MUTEX                      OsMutex;            /* Actual OS synchronization object */
    ACPI_THREAD_ID                  ThreadId;           /* Current owner of the mutex */
    struct acpi_thread_state        *OwnerThread;       /* Current owner of the mutex */
    union acpi_operand_object       *Prev;              /* Link for list of acquired mutexes */
    union acpi_operand_object       *Next;              /* Link for list of acquired mutexes */
    ACPI_NAMESPACE_NODE             *Node;              /* Containing namespace node */
    UINT8                           OriginalSyncLevel;  /* Owner's original sync level (0-15) */

} ACPI_OBJECT_MUTEX;


typedef struct acpi_object_region
{
    ACPI_OBJECT_COMMON_HEADER
    UINT8                           SpaceId;
    ACPI_NAMESPACE_NODE             *Node;              /* Containing namespace node */
    union acpi_operand_object       *Handler;           /* Handler for region access */
    union acpi_operand_object       *Next;
    ACPI_PHYSICAL_ADDRESS           Address;
    UINT32                          Length;

} ACPI_OBJECT_REGION;


typedef struct acpi_object_method
{
    ACPI_OBJECT_COMMON_HEADER
    UINT8                           InfoFlags;
    UINT8                           ParamCount;
    UINT8                           SyncLevel;
    union acpi_operand_object       *Mutex;
    union acpi_operand_object       *Node;
    UINT8                           *AmlStart;
    union
    {
        ACPI_INTERNAL_METHOD            Implementation;
        union acpi_operand_object       *Handler;
    } Dispatch;

    UINT32                          AmlLength;
    UINT8                           ThreadCount;
    ACPI_OWNER_ID                   OwnerId;

} ACPI_OBJECT_METHOD;

/* Flags for InfoFlags field above */

#define ACPI_METHOD_MODULE_LEVEL        0x01    /* Method is actually module-level code */
#define ACPI_METHOD_INTERNAL_ONLY       0x02    /* Method is implemented internally (_OSI) */
#define ACPI_METHOD_SERIALIZED          0x04    /* Method is serialized */
#define ACPI_METHOD_SERIALIZED_PENDING  0x08    /* Method is to be marked serialized */
#define ACPI_METHOD_IGNORE_SYNC_LEVEL   0x10    /* Method was auto-serialized at table load time */
#define ACPI_METHOD_MODIFIED_NAMESPACE  0x20    /* Method modified the namespace */


/******************************************************************************
 *
 * Objects that can be notified. All share a common NotifyInfo area.
 *
 *****************************************************************************/

/*
 * Common fields for objects that support ASL notifications
 */
#define ACPI_COMMON_NOTIFY_INFO \
    union acpi_operand_object       *NotifyList[2];     /* Handlers for system/device notifies */\
    union acpi_operand_object       *Handler;           /* Handler for Address space */

/* COMMON NOTIFY for POWER, PROCESSOR, DEVICE, and THERMAL */

typedef struct acpi_object_notify_common
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_NOTIFY_INFO

} ACPI_OBJECT_NOTIFY_COMMON;


typedef struct acpi_object_device
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_NOTIFY_INFO
    ACPI_GPE_BLOCK_INFO             *GpeBlock;

} ACPI_OBJECT_DEVICE;


typedef struct acpi_object_power_resource
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_NOTIFY_INFO
    UINT32                          SystemLevel;
    UINT32                          ResourceOrder;

} ACPI_OBJECT_POWER_RESOURCE;


typedef struct acpi_object_processor
{
    ACPI_OBJECT_COMMON_HEADER

    /* The next two fields take advantage of the 3-byte space before NOTIFY_INFO */

    UINT8                           ProcId;
    UINT8                           Length;
    ACPI_COMMON_NOTIFY_INFO
    ACPI_IO_ADDRESS                 Address;

} ACPI_OBJECT_PROCESSOR;


typedef struct acpi_object_thermal_zone
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_NOTIFY_INFO

} ACPI_OBJECT_THERMAL_ZONE;


/******************************************************************************
 *
 * Fields. All share a common header/info field.
 *
 *****************************************************************************/

/*
 * Common bitfield for the field objects
 * "Field Datum"  -- a datum from the actual field object
 * "Buffer Datum" -- a datum from a user buffer, read from or to be written to the field
 */
#define ACPI_COMMON_FIELD_INFO \
    UINT8                           FieldFlags;         /* Access, update, and lock bits */\
    UINT8                           Attribute;          /* From AccessAs keyword */\
    UINT8                           AccessByteWidth;    /* Read/Write size in bytes */\
    ACPI_NAMESPACE_NODE             *Node;              /* Link back to parent node */\
    UINT32                          BitLength;          /* Length of field in bits */\
    UINT32                          BaseByteOffset;     /* Byte offset within containing object */\
    UINT32                          Value;              /* Value to store into the Bank or Index register */\
    UINT8                           StartFieldBitOffset;/* Bit offset within first field datum (0-63) */\
    UINT8                           AccessLength;       /* For serial regions/fields */

/* COMMON FIELD (for BUFFER, REGION, BANK, and INDEX fields) */

typedef struct acpi_object_field_common
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_FIELD_INFO
    union acpi_operand_object       *RegionObj;         /* Parent Operation Region object (REGION/BANK fields only) */

} ACPI_OBJECT_FIELD_COMMON;


typedef struct acpi_object_region_field
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_FIELD_INFO
    UINT16                          ResourceLength;
    union acpi_operand_object       *RegionObj;         /* Containing OpRegion object */
    UINT8                           *ResourceBuffer;    /* ResourceTemplate for serial regions/fields */
    UINT16                          PinNumberIndex;     /* Index relative to previous Connection/Template */
    UINT8                           *InternalPccBuffer; /* Internal buffer for fields associated with PCC */

} ACPI_OBJECT_REGION_FIELD;


typedef struct acpi_object_bank_field
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_FIELD_INFO
    union acpi_operand_object       *RegionObj;         /* Containing OpRegion object */
    union acpi_operand_object       *BankObj;           /* BankSelect Register object */

} ACPI_OBJECT_BANK_FIELD;


typedef struct acpi_object_index_field
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_FIELD_INFO

    /*
     * No "RegionObj" pointer needed since the Index and Data registers
     * are each field definitions unto themselves.
     */
    union acpi_operand_object       *IndexObj;          /* Index register */
    union acpi_operand_object       *DataObj;           /* Data register */

} ACPI_OBJECT_INDEX_FIELD;


/* The BufferField is different in that it is part of a Buffer, not an OpRegion */

typedef struct acpi_object_buffer_field
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_COMMON_FIELD_INFO
    union acpi_operand_object       *BufferObj;         /* Containing Buffer object */

} ACPI_OBJECT_BUFFER_FIELD;


/******************************************************************************
 *
 * Objects for handlers
 *
 *****************************************************************************/

typedef struct acpi_object_notify_handler
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_NAMESPACE_NODE             *Node;              /* Parent device */
    UINT32                          HandlerType;        /* Type: Device/System/Both */
    ACPI_NOTIFY_HANDLER             Handler;            /* Handler address */
    void                            *Context;
    union acpi_operand_object       *Next[2];           /* Device and System handler lists */

} ACPI_OBJECT_NOTIFY_HANDLER;


typedef struct acpi_object_addr_handler
{
    ACPI_OBJECT_COMMON_HEADER
    UINT8                           SpaceId;
    UINT8                           HandlerFlags;
    ACPI_ADR_SPACE_HANDLER          Handler;
    ACPI_NAMESPACE_NODE             *Node;              /* Parent device */
    void                            *Context;
    ACPI_ADR_SPACE_SETUP            Setup;
    union acpi_operand_object       *RegionList;        /* Regions using this handler */
    union acpi_operand_object       *Next;

} ACPI_OBJECT_ADDR_HANDLER;

/* Flags for address handler (HandlerFlags) */

#define ACPI_ADDR_HANDLER_DEFAULT_INSTALLED  0x01


/******************************************************************************
 *
 * Special internal objects
 *
 *****************************************************************************/

/*
 * The Reference object is used for these opcodes:
 * Arg[0-6], Local[0-7], IndexOp, NameOp, RefOfOp, LoadOp, LoadTableOp, DebugOp
 * The Reference.Class differentiates these types.
 */
typedef struct acpi_object_reference
{
    ACPI_OBJECT_COMMON_HEADER
    UINT8                           Class;              /* Reference Class */
    UINT8                           TargetType;         /* Used for Index Op */
    UINT8                           Resolved;           /* Reference has been resolved to a value */
    void                            *Object;            /* NameOp=>HANDLE to obj, IndexOp=>ACPI_OPERAND_OBJECT */
    ACPI_NAMESPACE_NODE             *Node;              /* RefOf or Namepath */
    union acpi_operand_object       **Where;            /* Target of Index */
    UINT8                           *IndexPointer;      /* Used for Buffers and Strings */
    UINT8                           *Aml;               /* Used for deferred resolution of the ref */
    UINT32                          Value;              /* Used for Local/Arg/Index/DdbHandle */

} ACPI_OBJECT_REFERENCE;

/* Values for Reference.Class above */

typedef enum
{
    ACPI_REFCLASS_LOCAL             = 0,        /* Method local */
    ACPI_REFCLASS_ARG               = 1,        /* Method argument */
    ACPI_REFCLASS_REFOF             = 2,        /* Result of RefOf() TBD: Split to Ref/Node and Ref/OperandObj? */
    ACPI_REFCLASS_INDEX             = 3,        /* Result of Index() */
    ACPI_REFCLASS_TABLE             = 4,        /* DdbHandle - Load(), LoadTable() */
    ACPI_REFCLASS_NAME              = 5,        /* Reference to a named object */
    ACPI_REFCLASS_DEBUG             = 6,        /* Debug object */

    ACPI_REFCLASS_MAX               = 6

} ACPI_REFERENCE_CLASSES;

/*
 * Extra object is used as additional storage for types that
 * have AML code in their declarations (TermArgs) that must be
 * evaluated at run time.
 *
 * Currently: Region and FieldUnit types
 */
typedef struct acpi_object_extra
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_NAMESPACE_NODE             *Method_REG;        /* _REG method for this region (if any) */
    ACPI_NAMESPACE_NODE             *ScopeNode;
    void                            *RegionContext;     /* Region-specific data */
    UINT8                           *AmlStart;
    UINT32                          AmlLength;

} ACPI_OBJECT_EXTRA;


/* Additional data that can be attached to namespace nodes */

typedef struct acpi_object_data
{
    ACPI_OBJECT_COMMON_HEADER
    ACPI_OBJECT_HANDLER             Handler;
    void                            *Pointer;

} ACPI_OBJECT_DATA;


/* Structure used when objects are cached for reuse */

typedef struct acpi_object_cache_list
{
    ACPI_OBJECT_COMMON_HEADER
    union acpi_operand_object       *Next;              /* Link for object cache and internal lists*/

} ACPI_OBJECT_CACHE_LIST;


/******************************************************************************
 *
 * ACPI_OPERAND_OBJECT Descriptor - a giant union of all of the above
 *
 *****************************************************************************/

typedef union acpi_operand_object
{
    ACPI_OBJECT_COMMON                  Common;
    ACPI_OBJECT_INTEGER                 Integer;
    ACPI_OBJECT_STRING                  String;
    ACPI_OBJECT_BUFFER                  Buffer;
    ACPI_OBJECT_PACKAGE                 Package;
    ACPI_OBJECT_EVENT                   Event;
    ACPI_OBJECT_METHOD                  Method;
    ACPI_OBJECT_MUTEX                   Mutex;
    ACPI_OBJECT_REGION                  Region;
    ACPI_OBJECT_NOTIFY_COMMON           CommonNotify;
    ACPI_OBJECT_DEVICE                  Device;
    ACPI_OBJECT_POWER_RESOURCE          PowerResource;
    ACPI_OBJECT_PROCESSOR               Processor;
    ACPI_OBJECT_THERMAL_ZONE            ThermalZone;
    ACPI_OBJECT_FIELD_COMMON            CommonField;
    ACPI_OBJECT_REGION_FIELD            Field;
    ACPI_OBJECT_BUFFER_FIELD            BufferField;
    ACPI_OBJECT_BANK_FIELD              BankField;
    ACPI_OBJECT_INDEX_FIELD             IndexField;
    ACPI_OBJECT_NOTIFY_HANDLER          Notify;
    ACPI_OBJECT_ADDR_HANDLER            AddressSpace;
    ACPI_OBJECT_REFERENCE               Reference;
    ACPI_OBJECT_EXTRA                   Extra;
    ACPI_OBJECT_DATA                    Data;
    ACPI_OBJECT_CACHE_LIST              Cache;

    /*
     * Add namespace node to union in order to simplify code that accepts both
     * ACPI_OPERAND_OBJECTs and ACPI_NAMESPACE_NODEs. The structures share
     * a common DescriptorType field in order to differentiate them.
     */
    ACPI_NAMESPACE_NODE                 Node;

} ACPI_OPERAND_OBJECT;


/******************************************************************************
 *
 * ACPI_DESCRIPTOR - objects that share a common descriptor identifier
 *
 *****************************************************************************/

/* Object descriptor types */

#define ACPI_DESC_TYPE_CACHED           0x01        /* Used only when object is cached */
#define ACPI_DESC_TYPE_STATE            0x02
#define ACPI_DESC_TYPE_STATE_UPDATE     0x03
#define ACPI_DESC_TYPE_STATE_PACKAGE    0x04
#define ACPI_DESC_TYPE_STATE_CONTROL    0x05
#define ACPI_DESC_TYPE_STATE_RPSCOPE    0x06
#define ACPI_DESC_TYPE_STATE_PSCOPE     0x07
#define ACPI_DESC_TYPE_STATE_WSCOPE     0x08
#define ACPI_DESC_TYPE_STATE_RESULT     0x09
#define ACPI_DESC_TYPE_STATE_NOTIFY     0x0A
#define ACPI_DESC_TYPE_STATE_THREAD     0x0B
#define ACPI_DESC_TYPE_WALK             0x0C
#define ACPI_DESC_TYPE_PARSER           0x0D
#define ACPI_DESC_TYPE_OPERAND          0x0E
#define ACPI_DESC_TYPE_NAMED            0x0F
#define ACPI_DESC_TYPE_MAX              0x0F


typedef struct acpi_common_descriptor
{
    void                            *CommonPointer;
    UINT8                           DescriptorType; /* To differentiate various internal objs */

} ACPI_COMMON_DESCRIPTOR;

typedef union acpi_descriptor
{
    ACPI_COMMON_DESCRIPTOR          Common;
    ACPI_OPERAND_OBJECT             Object;
    ACPI_NAMESPACE_NODE             Node;
    ACPI_PARSE_OBJECT               Op;

} ACPI_DESCRIPTOR;

#pragma pack()

#endif /* _ACOBJECT_H */

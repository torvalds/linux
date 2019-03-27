/******************************************************************************
 *
 * Name: acstruct.h - Internal structs
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

#ifndef __ACSTRUCT_H__
#define __ACSTRUCT_H__

/* acpisrc:StructDefs -- for acpisrc conversion */

/*****************************************************************************
 *
 * Tree walking typedefs and structs
 *
 ****************************************************************************/


/*
 * Walk state - current state of a parse tree walk. Used for both a leisurely
 * stroll through the tree (for whatever reason), and for control method
 * execution.
 */
#define ACPI_NEXT_OP_DOWNWARD       1
#define ACPI_NEXT_OP_UPWARD         2

/*
 * Groups of definitions for WalkType used for different implementations of
 * walkers (never simultaneously) - flags for interpreter:
 */
#define ACPI_WALK_NON_METHOD        0
#define ACPI_WALK_METHOD            0x01
#define ACPI_WALK_METHOD_RESTART    0x02


typedef struct acpi_walk_state
{
    struct acpi_walk_state          *Next;              /* Next WalkState in list */
    UINT8                           DescriptorType;     /* To differentiate various internal objs */
    UINT8                           WalkType;
    UINT16                          Opcode;             /* Current AML opcode */
    UINT8                           NextOpInfo;         /* Info about NextOp */
    UINT8                           NumOperands;        /* Stack pointer for Operands[] array */
    UINT8                           OperandIndex;       /* Index into operand stack, to be used by AcpiDsObjStackPush */
    ACPI_OWNER_ID                   OwnerId;            /* Owner of objects created during the walk */
    BOOLEAN                         LastPredicate;      /* Result of last predicate */
    UINT8                           CurrentResult;
    UINT8                           ReturnUsed;
    UINT8                           ScopeDepth;
    UINT8                           PassNumber;         /* Parse pass during table load */
    BOOLEAN                         NamespaceOverride;  /* Override existing objects */
    UINT8                           ResultSize;         /* Total elements for the result stack */
    UINT8                           ResultCount;        /* Current number of occupied elements of result stack */
    UINT8                           *Aml;
    UINT32                          ArgTypes;
    UINT32                          MethodBreakpoint;   /* For single stepping */
    UINT32                          UserBreakpoint;     /* User AML breakpoint */
    UINT32                          ParseFlags;

    ACPI_PARSE_STATE                ParserState;        /* Current state of parser */
    UINT32                          PrevArgTypes;
    UINT32                          ArgCount;           /* push for fixed or var args */
    UINT16                          MethodNestingDepth;
    UINT8                           MethodIsNested;

    struct acpi_namespace_node      Arguments[ACPI_METHOD_NUM_ARGS];        /* Control method arguments */
    struct acpi_namespace_node      LocalVariables[ACPI_METHOD_NUM_LOCALS]; /* Control method locals */
    union acpi_operand_object       *Operands[ACPI_OBJ_NUM_OPERANDS + 1];   /* Operands passed to the interpreter (+1 for NULL terminator) */
    union acpi_operand_object       **Params;

    UINT8                           *AmlLastWhile;
    union acpi_operand_object       **CallerReturnDesc;
    ACPI_GENERIC_STATE              *ControlState;      /* List of control states (nested IFs) */
    struct acpi_namespace_node      *DeferredNode;      /* Used when executing deferred opcodes */
    union acpi_operand_object       *ImplicitReturnObj;
    struct acpi_namespace_node      *MethodCallNode;    /* Called method Node*/
    ACPI_PARSE_OBJECT               *MethodCallOp;      /* MethodCall Op if running a method */
    union acpi_operand_object       *MethodDesc;        /* Method descriptor if running a method */
    struct acpi_namespace_node      *MethodNode;        /* Method node if running a method */
    char                            *MethodPathname;    /* Full pathname of running method */
    ACPI_PARSE_OBJECT               *Op;                /* Current parser op */
    const ACPI_OPCODE_INFO          *OpInfo;            /* Info on current opcode */
    ACPI_PARSE_OBJECT               *Origin;            /* Start of walk [Obsolete] */
    union acpi_operand_object       *ResultObj;
    ACPI_GENERIC_STATE              *Results;           /* Stack of accumulated results */
    union acpi_operand_object       *ReturnDesc;        /* Return object, if any */
    ACPI_GENERIC_STATE              *ScopeInfo;         /* Stack of nested scopes */
    ACPI_PARSE_OBJECT               *PrevOp;            /* Last op that was processed */
    ACPI_PARSE_OBJECT               *NextOp;            /* next op to be processed */
    ACPI_THREAD_STATE               *Thread;
    ACPI_PARSE_DOWNWARDS            DescendingCallback;
    ACPI_PARSE_UPWARDS              AscendingCallback;

} ACPI_WALK_STATE;


/* Info used by AcpiNsInitializeObjects and AcpiDsInitializeObjects */

typedef struct acpi_init_walk_info
{
    UINT32                          TableIndex;
    UINT32                          ObjectCount;
    UINT32                          MethodCount;
    UINT32                          SerialMethodCount;
    UINT32                          NonSerialMethodCount;
    UINT32                          SerializedMethodCount;
    UINT32                          DeviceCount;
    UINT32                          OpRegionCount;
    UINT32                          FieldCount;
    UINT32                          BufferCount;
    UINT32                          PackageCount;
    UINT32                          OpRegionInit;
    UINT32                          FieldInit;
    UINT32                          BufferInit;
    UINT32                          PackageInit;
    ACPI_OWNER_ID                   OwnerId;

} ACPI_INIT_WALK_INFO;


typedef struct acpi_get_devices_info
{
    ACPI_WALK_CALLBACK              UserFunction;
    void                            *Context;
    char                            *Hid;

} ACPI_GET_DEVICES_INFO;


typedef union acpi_aml_operands
{
    ACPI_OPERAND_OBJECT             *Operands[7];

    struct
    {
        ACPI_OBJECT_INTEGER             *Type;
        ACPI_OBJECT_INTEGER             *Code;
        ACPI_OBJECT_INTEGER             *Argument;

    } Fatal;

    struct
    {
        ACPI_OPERAND_OBJECT             *Source;
        ACPI_OBJECT_INTEGER             *Index;
        ACPI_OPERAND_OBJECT             *Target;

    } Index;

    struct
    {
        ACPI_OPERAND_OBJECT             *Source;
        ACPI_OBJECT_INTEGER             *Index;
        ACPI_OBJECT_INTEGER             *Length;
        ACPI_OPERAND_OBJECT             *Target;

    } Mid;

} ACPI_AML_OPERANDS;


/*
 * Structure used to pass object evaluation information and parameters.
 * Purpose is to reduce CPU stack use.
 */
typedef struct acpi_evaluate_info
{
    /* The first 3 elements are passed by the caller to AcpiNsEvaluate */

    ACPI_NAMESPACE_NODE             *PrefixNode;        /* Input: starting node */
    const char                      *RelativePathname;  /* Input: path relative to PrefixNode */
    ACPI_OPERAND_OBJECT             **Parameters;       /* Input: argument list */

    ACPI_NAMESPACE_NODE             *Node;              /* Resolved node (PrefixNode:RelativePathname) */
    ACPI_OPERAND_OBJECT             *ObjDesc;           /* Object attached to the resolved node */
    char                            *FullPathname;      /* Full pathname of the resolved node */

    const ACPI_PREDEFINED_INFO      *Predefined;        /* Used if Node is a predefined name */
    ACPI_OPERAND_OBJECT             *ReturnObject;      /* Object returned from the evaluation */
    union acpi_operand_object       *ParentPackage;     /* Used if return object is a Package */

    UINT32                          ReturnFlags;        /* Used for return value analysis */
    UINT32                          ReturnBtype;        /* Bitmapped type of the returned object */
    UINT16                          ParamCount;         /* Count of the input argument list */
    UINT8                           PassNumber;         /* Parser pass number */
    UINT8                           ReturnObjectType;   /* Object type of the returned object */
    UINT8                           NodeFlags;          /* Same as Node->Flags */
    UINT8                           Flags;              /* General flags */

} ACPI_EVALUATE_INFO;

/* Values for Flags above */

#define ACPI_IGNORE_RETURN_VALUE    1

/* Defines for ReturnFlags field above */

#define ACPI_OBJECT_REPAIRED        1
#define ACPI_OBJECT_WRAPPED         2


/* Info used by AcpiNsInitializeDevices */

typedef struct acpi_device_walk_info
{
    ACPI_TABLE_DESC                 *TableDesc;
    ACPI_EVALUATE_INFO              *EvaluateInfo;
    UINT32                          DeviceCount;
    UINT32                          Num_STA;
    UINT32                          Num_INI;

} ACPI_DEVICE_WALK_INFO;


/* TBD: [Restructure] Merge with struct above */

typedef struct acpi_walk_info
{
    UINT32                          DebugLevel;
    UINT32                          Count;
    ACPI_OWNER_ID                   OwnerId;
    UINT8                           DisplayType;

} ACPI_WALK_INFO;

/* Display Types */

#define ACPI_DISPLAY_SUMMARY        (UINT8) 0
#define ACPI_DISPLAY_OBJECTS        (UINT8) 1
#define ACPI_DISPLAY_MASK           (UINT8) 1

#define ACPI_DISPLAY_SHORT          (UINT8) 2


#endif

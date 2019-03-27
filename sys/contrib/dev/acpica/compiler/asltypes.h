/******************************************************************************
 *
 * Module Name: asltypes.h - compiler data types and struct definitions
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

#ifndef __ASLTYPES_H
#define __ASLTYPES_H


/*******************************************************************************
 *
 * Structure definitions
 *
 ******************************************************************************/


/* Op flags for the ACPI_PARSE_OBJECT */

#define OP_VISITED                  0x00000001
#define OP_AML_PACKAGE              0x00000002
#define OP_IS_TARGET                0x00000004
#define OP_IS_RESOURCE_DESC         0x00000008
#define OP_IS_RESOURCE_FIELD        0x00000010
#define OP_HAS_NO_EXIT              0x00000020
#define OP_IF_HAS_NO_EXIT           0x00000040
#define OP_NAME_INTERNALIZED        0x00000080
#define OP_METHOD_NO_RETVAL         0x00000100
#define OP_METHOD_SOME_NO_RETVAL    0x00000200
#define OP_RESULT_NOT_USED          0x00000400
#define OP_METHOD_TYPED             0x00000800
#define OP_COULD_NOT_REDUCE         0x00001000
#define OP_COMPILE_TIME_CONST       0x00002000
#define OP_IS_TERM_ARG              0x00004000
#define OP_WAS_ONES_OP              0x00008000
#define OP_IS_NAME_DECLARATION      0x00010000
#define OP_COMPILER_EMITTED         0x00020000
#define OP_IS_DUPLICATE             0x00040000
#define OP_IS_RESOURCE_DATA         0x00080000
#define OP_IS_NULL_RETURN           0x00100000
#define OP_NOT_FOUND_DURING_LOAD    0x00200000

#define ACPI_NUM_OP_FLAGS           0x22

/* Keeps information about individual control methods */

typedef struct asl_method_info
{
    ACPI_PARSE_OBJECT       *Op;
    ACPI_PARSE_OBJECT       *CurrentOp;
    struct asl_method_info  *Next;
    UINT32                  ValidArgTypes[ACPI_METHOD_NUM_ARGS];
    UINT32                  ValidReturnTypes;
    UINT32                  NumReturnNoValue;
    UINT32                  NumReturnWithValue;
    UINT8                   NumArguments;
    UINT8                   LocalInitialized[ACPI_METHOD_NUM_LOCALS];
    UINT8                   ArgInitialized[ACPI_METHOD_NUM_ARGS];
    UINT8                   HasBeenTyped;
    UINT8                   ShouldBeSerialized;
    UINT8                   CreatesNamedObjects;

} ASL_METHOD_INFO;


/* Parse tree walk info for control method analysis */

typedef struct asl_analysis_walk_info
{
    ASL_METHOD_INFO         *MethodStack;

} ASL_ANALYSIS_WALK_INFO;


/* An entry in the ParseOpcode to AmlOpcode mapping table */

typedef struct asl_mapping_entry
{
    UINT32                      Value;
    UINT32                      AcpiBtype;   /* Object type or return type */
    UINT16                      AmlOpcode;
    UINT8                       Flags;

} ASL_MAPPING_ENTRY;


/* Parse tree walk info structure */

typedef struct asl_walk_info
{
    ACPI_PARSE_OBJECT           **NodePtr;
    UINT32                      *LevelPtr;

} ASL_WALK_INFO;


/* File info */

typedef struct asl_file_info
{
    FILE                        *Handle;
    char                        *Filename;
    const char                  *ShortDescription;
    const char                  *Description;

} ASL_FILE_INFO;

typedef struct asl_file_status
{
    UINT32                  Line;
    UINT32                  Offset;

} ASL_FILE_STATUS;


/*
 * File types. Note: Any changes to this table must also be reflected
 * in the Gbl_Files array.
 *
 * Corresponding filename suffixes are in comments
 *
 * NOTE: Don't move the first 4 file types
 *
 * .xxx file extension: this is used as a temporary .aml file for
 * the ASL/ASL+ converter and is deleted after conversion. This file
 * should never be used in the interpreter.
 */
typedef enum
{
    ASL_FILE_STDOUT             = 0,
    ASL_FILE_STDERR,
    ASL_FILE_INPUT,             /* .asl */
    ASL_FILE_AML_OUTPUT,        /* .aml */
    ASL_FILE_SOURCE_OUTPUT,     /* .src */
    ASL_FILE_PREPROCESSOR,      /* .pre */
    ASL_FILE_PREPROCESSOR_USER, /* .i   */
    ASL_FILE_LISTING_OUTPUT,    /* .lst */
    ASL_FILE_HEX_OUTPUT,        /* .hex */
    ASL_FILE_NAMESPACE_OUTPUT,  /* .nsp */
    ASL_FILE_DEBUG_OUTPUT,      /* .txt */
    ASL_FILE_ASM_SOURCE_OUTPUT, /* .asm */
    ASL_FILE_C_SOURCE_OUTPUT,   /* .c   */
    ASL_FILE_ASM_INCLUDE_OUTPUT,/* .inc */
    ASL_FILE_C_INCLUDE_OUTPUT,  /* .h   */
    ASL_FILE_C_OFFSET_OUTPUT,   /* .offset.h */
    ASL_FILE_MAP_OUTPUT,        /* .map */
    ASL_FILE_XREF_OUTPUT,       /* .xrf */
    ASL_FILE_CONV_DEBUG_OUTPUT, /* .cdb */
    ASL_FILE_CONV_OUTPUT        /* .xxx */

} ASL_FILE_TYPES;


#define ASL_MAX_FILE_TYPE       18
#define ASL_NUM_FILES           (ASL_MAX_FILE_TYPE + 1)

/* Name suffixes used to create filenames for output files */

#define FILE_SUFFIX_ASL_CODE        "asl"
#define FILE_SUFFIX_AML_CODE        "aml"
#define FILE_SUFFIX_SOURCE          "src"
#define FILE_SUFFIX_PREPROCESSOR    "pre"
#define FILE_SUFFIX_PREPROC_USER    "i"
#define FILE_SUFFIX_LISTING         "lst"
#define FILE_SUFFIX_HEX_DUMP        "hex"
#define FILE_SUFFIX_NAMESPACE       "nsp"
#define FILE_SUFFIX_DEBUG           "txt"
#define FILE_SUFFIX_ASM_SOURCE      "asm"
#define FILE_SUFFIX_C_SOURCE        "c"
#define FILE_SUFFIX_ASM_INCLUDE     "inc"
#define FILE_SUFFIX_C_INCLUDE       "h"
#define FILE_SUFFIX_C_OFFSET        "offset.h"
#define FILE_SUFFIX_MAP             "map"
#define FILE_SUFFIX_XREF            "xrf"
#define FILE_SUFFIX_CONVERT_AML     "xxx"
#define FILE_SUFFIX_CONVERT_DEBUG   "cdb"


/* Cache block structure for ParseOps and Strings */

typedef struct asl_cache_info
{
    void                            *Next;
    char                            Buffer[1];

} ASL_CACHE_INFO;


typedef struct asl_include_dir
{
    char                        *Dir;
    struct asl_include_dir      *Next;

} ASL_INCLUDE_DIR;


/*
 * An entry in the exception list, one for each error/warning
 * Note: SubError nodes would be treated with the same messageId and Level
 * as the parent error node.
 */
typedef struct asl_error_msg
{
    UINT32                      LineNumber;
    UINT32                      LogicalLineNumber;
    UINT32                      LogicalByteOffset;
    UINT32                      Column;
    char                        *Message;
    struct asl_error_msg        *Next;
    struct asl_error_msg        *SubError;
    char                        *Filename;
    char                        *SourceLine;
    UINT32                      FilenameLength;
    UINT16                      MessageId;
    UINT8                       Level;

} ASL_ERROR_MSG;

/* An entry in the expected messages array */
typedef struct asl_expected_message
{
    UINT32                       MessageId;
    char                         *MessageIdStr;
    BOOLEAN                      MessageReceived;

} ASL_EXPECTED_MESSAGE;


/* An entry in the listing file stack (for include files) */

typedef struct asl_listing_node
{
    char                        *Filename;
    UINT32                      LineNumber;
    struct asl_listing_node     *Next;

} ASL_LISTING_NODE;


/* Callback interface for a parse tree walk */

/*
 * TBD - another copy of this is in adisasm.h, fix
 */
#ifndef ASL_WALK_CALLBACK_DEFINED
typedef
ACPI_STATUS (*ASL_WALK_CALLBACK) (
    ACPI_PARSE_OBJECT           *Op,
    UINT32                      Level,
    void                        *Context);
#define ASL_WALK_CALLBACK_DEFINED
#endif


typedef struct asl_event_info
{
    UINT64                      StartTime;
    UINT64                      EndTime;
    char                        *EventName;
    BOOLEAN                     Valid;

} ASL_EVENT_INFO;


/* Hardware mapping file structures */

typedef struct acpi_gpio_info
{
    struct acpi_gpio_info   *Next;
    ACPI_PARSE_OBJECT       *Op;
    char                    *DeviceName;
    ACPI_NAMESPACE_NODE     *TargetNode;
    UINT32                  References;
    UINT32                  PinCount;
    UINT32                  PinIndex;
    UINT16                  PinNumber;
    UINT8                   Type;
    UINT8                   Direction;
    UINT8                   Polarity;

} ACPI_GPIO_INFO;

typedef struct acpi_serial_info
{
    struct acpi_serial_info *Next;
    ACPI_PARSE_OBJECT       *Op;
    char                    *DeviceName;
    ACPI_NAMESPACE_NODE     *TargetNode;
    AML_RESOURCE            *Resource;
    UINT32                  Speed;
    UINT16                  Address;

} ACPI_SERIAL_INFO;

typedef struct asl_method_local
{
    ACPI_PARSE_OBJECT       *Op;
    UINT8                   Flags;

} ASL_METHOD_LOCAL;

/* Values for Flags field above */

#define ASL_LOCAL_INITIALIZED   (1)
#define ASL_LOCAL_REFERENCED    (1<<1)
#define ASL_ARG_IS_LOCAL        (1<<2)
#define ASL_ARG_INITIALIZED     (1<<3)
#define ASL_ARG_REFERENCED      (1<<4)

/* Info used to track method counts for cross reference output file */

typedef struct asl_xref_info
{
    UINT32                  ThisMethodInvocations;
    UINT32                  TotalPredefinedMethods;
    UINT32                  TotalUserMethods;
    UINT32                  TotalUnreferenceUserMethods;
    UINT32                  ThisObjectReferences;
    UINT32                  TotalObjects;
    UINT32                  TotalUnreferencedObjects;
    ACPI_PARSE_OBJECT       *MethodOp;
    ACPI_PARSE_OBJECT       *CurrentMethodOp;

} ASL_XREF_INFO;


typedef struct asl_file_node
{
    FILE                    *File;
    UINT32                  CurrentLineNumber;
    void                    *State;
    char                    *Filename;
    struct asl_file_node    *Next;

} ASL_FILE_NODE;

#endif  /* __ASLTYPES_H */

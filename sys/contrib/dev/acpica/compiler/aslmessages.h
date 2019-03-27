/******************************************************************************
 *
 * Module Name: aslmessages.h - Compiler error/warning messages
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

#ifndef __ASLMESSAGES_H
#define __ASLMESSAGES_H


/* These values must match error type string tables in aslmessages.c */

typedef enum
{
    ASL_OPTIMIZATION = 0,
    ASL_REMARK,
    ASL_WARNING,
    ASL_WARNING2,
    ASL_WARNING3,
    ASL_ERROR,
    ASL_NUM_REPORT_LEVELS

} ASL_MESSAGE_TYPES;


#define ASL_ERROR_LEVEL_LENGTH          8 /* Length of strings for types above */

/*
 * Exception code blocks, 0 - 999
 * Available for new exception blocks: 600 - 999
 */
#define ASL_MSG_MAIN_COMPILER           0       /* 0 - 299 */
#define ASL_MSG_MAIN_COMPILER_END       299

#define ASL_MSG_TABLE_COMPILER          300     /* 300 - 499 */
#define ASL_MSG_TABLE_COMPILER_END      499

#define ASL_MSG_PREPROCESSOR            500     /* 500 - 599 */
#define ASL_MSG_PREPROCESSOR_END        599


/*
 * Values (message IDs) for all compiler messages. There are currently
 * three distinct blocks of error messages (so that they can be expanded
 * individually):
 *      Main ASL compiler
 *      Data Table compiler
 *      Preprocessor
 *
 * NOTE1: This list must match the tables of message strings in the file
 * aslmessages.c exactly.
 *
 * NOTE2: With the introduction of the -vw option to disable specific
 * messages, new messages should only be added to the end of these
 * lists, so that values for existing messages are not disturbed.
 */
typedef enum
{
    ASL_MSG_RESERVED = ASL_MSG_MAIN_COMPILER,

    ASL_MSG_ALIGNMENT,
    ASL_MSG_ALPHANUMERIC_STRING,
    ASL_MSG_AML_NOT_IMPLEMENTED,
    ASL_MSG_ARG_COUNT_HI,
    ASL_MSG_ARG_COUNT_LO,
    ASL_MSG_ARG_INIT,
    ASL_MSG_BACKWARDS_OFFSET,
    ASL_MSG_BUFFER_LENGTH,
    ASL_MSG_CLOSE,
    ASL_MSG_COMPILER_INTERNAL,
    ASL_MSG_COMPILER_RESERVED,
    ASL_MSG_CONNECTION_MISSING,
    ASL_MSG_CONNECTION_INVALID,
    ASL_MSG_CONSTANT_EVALUATION,
    ASL_MSG_CONSTANT_FOLDED,
    ASL_MSG_CORE_EXCEPTION,
    ASL_MSG_DEBUG_FILE_OPEN,
    ASL_MSG_DEBUG_FILENAME,
    ASL_MSG_DEPENDENT_NESTING,
    ASL_MSG_DMA_CHANNEL,
    ASL_MSG_DMA_LIST,
    ASL_MSG_DUPLICATE_CASE,
    ASL_MSG_DUPLICATE_ITEM,
    ASL_MSG_EARLY_EOF,
    ASL_MSG_ENCODING_LENGTH,
    ASL_MSG_EX_INTERRUPT_LIST,
    ASL_MSG_EX_INTERRUPT_LIST_MIN,
    ASL_MSG_EX_INTERRUPT_NUMBER,
    ASL_MSG_FIELD_ACCESS_WIDTH,
    ASL_MSG_FIELD_UNIT_ACCESS_WIDTH,
    ASL_MSG_FIELD_UNIT_OFFSET,
    ASL_MSG_GPE_NAME_CONFLICT,
    ASL_MSG_HID_LENGTH,
    ASL_MSG_HID_PREFIX,
    ASL_MSG_HID_SUFFIX,
    ASL_MSG_INCLUDE_FILE_OPEN,
    ASL_MSG_INPUT_FILE_OPEN,
    ASL_MSG_INTEGER_LENGTH,
    ASL_MSG_INTEGER_OPTIMIZATION,
    ASL_MSG_INTERRUPT_LIST,
    ASL_MSG_INTERRUPT_NUMBER,
    ASL_MSG_INVALID_ACCESS_SIZE,
    ASL_MSG_INVALID_ADDR_FLAGS,
    ASL_MSG_INVALID_CONSTANT_OP,
    ASL_MSG_INVALID_EISAID,
    ASL_MSG_INVALID_ESCAPE,
    ASL_MSG_INVALID_GRAN_FIXED,
    ASL_MSG_INVALID_GRANULARITY,
    ASL_MSG_INVALID_LENGTH,
    ASL_MSG_INVALID_LENGTH_FIXED,
    ASL_MSG_INVALID_MIN_MAX,
    ASL_MSG_INVALID_OPERAND,
    ASL_MSG_INVALID_PERFORMANCE,
    ASL_MSG_INVALID_PRIORITY,
    ASL_MSG_INVALID_STRING,
    ASL_MSG_INVALID_TARGET,
    ASL_MSG_INVALID_TIME,
    ASL_MSG_INVALID_TYPE,
    ASL_MSG_INVALID_UUID,
    ASL_MSG_ISA_ADDRESS,
    ASL_MSG_LEADING_ASTERISK,
    ASL_MSG_LIST_LENGTH_LONG,
    ASL_MSG_LIST_LENGTH_SHORT,
    ASL_MSG_LISTING_FILE_OPEN,
    ASL_MSG_LISTING_FILENAME,
    ASL_MSG_LOCAL_INIT,
    ASL_MSG_LOCAL_OUTSIDE_METHOD,
    ASL_MSG_LONG_LINE,
    ASL_MSG_MEMORY_ALLOCATION,
    ASL_MSG_MISSING_ENDDEPENDENT,
    ASL_MSG_MISSING_STARTDEPENDENT,
    ASL_MSG_MULTIPLE_DEFAULT,
    ASL_MSG_MULTIPLE_TYPES,
    ASL_MSG_NAME_EXISTS,
    ASL_MSG_NAME_OPTIMIZATION,
    ASL_MSG_NAMED_OBJECT_IN_WHILE,
    ASL_MSG_NESTED_COMMENT,
    ASL_MSG_NO_CASES,
    ASL_MSG_NO_REGION,
    ASL_MSG_NO_RETVAL,
    ASL_MSG_NO_WHILE,
    ASL_MSG_NON_ASCII,
    ASL_MSG_NON_ZERO,
    ASL_MSG_NOT_EXIST,
    ASL_MSG_NOT_FOUND,
    ASL_MSG_NOT_METHOD,
    ASL_MSG_NOT_PARAMETER,
    ASL_MSG_NOT_REACHABLE,
    ASL_MSG_NOT_REFERENCED,
    ASL_MSG_NULL_DESCRIPTOR,
    ASL_MSG_NULL_STRING,
    ASL_MSG_OPEN,
    ASL_MSG_OUTPUT_FILE_OPEN,
    ASL_MSG_OUTPUT_FILENAME,
    ASL_MSG_PACKAGE_LENGTH,
    ASL_MSG_PREPROCESSOR_FILENAME,
    ASL_MSG_READ,
    ASL_MSG_RECURSION,
    ASL_MSG_REGION_BUFFER_ACCESS,
    ASL_MSG_REGION_BYTE_ACCESS,
    ASL_MSG_RESERVED_ARG_COUNT_HI,
    ASL_MSG_RESERVED_ARG_COUNT_LO,
    ASL_MSG_RESERVED_METHOD,
    ASL_MSG_RESERVED_NO_RETURN_VAL,
    ASL_MSG_RESERVED_OPERAND_TYPE,
    ASL_MSG_RESERVED_PACKAGE_LENGTH,
    ASL_MSG_RESERVED_RETURN_VALUE,
    ASL_MSG_RESERVED_USE,
    ASL_MSG_RESERVED_WORD,
    ASL_MSG_RESOURCE_FIELD,
    ASL_MSG_RESOURCE_INDEX,
    ASL_MSG_RESOURCE_LIST,
    ASL_MSG_RESOURCE_SOURCE,
    ASL_MSG_RESULT_NOT_USED,
    ASL_MSG_RETURN_TYPES,
    ASL_MSG_SCOPE_FWD_REF,
    ASL_MSG_SCOPE_TYPE,
    ASL_MSG_SEEK,
    ASL_MSG_SERIALIZED,
    ASL_MSG_SERIALIZED_REQUIRED,
    ASL_MSG_SINGLE_NAME_OPTIMIZATION,
    ASL_MSG_SOME_NO_RETVAL,
    ASL_MSG_STRING_LENGTH,
    ASL_MSG_SWITCH_TYPE,
    ASL_MSG_SYNC_LEVEL,
    ASL_MSG_SYNTAX,
    ASL_MSG_TABLE_SIGNATURE,
    ASL_MSG_TAG_LARGER,
    ASL_MSG_TAG_SMALLER,
    ASL_MSG_TIMEOUT,
    ASL_MSG_TOO_MANY_TEMPS,
    ASL_MSG_TRUNCATION,
    ASL_MSG_UNKNOWN_RESERVED_NAME,
    ASL_MSG_UNREACHABLE_CODE,
    ASL_MSG_UNSUPPORTED,
    ASL_MSG_UPPER_CASE,
    ASL_MSG_VENDOR_LIST,
    ASL_MSG_WRITE,
    ASL_MSG_RANGE,
    ASL_MSG_BUFFER_ALLOCATION,
    ASL_MSG_MISSING_DEPENDENCY,
    ASL_MSG_ILLEGAL_FORWARD_REF,
    ASL_MSG_ILLEGAL_METHOD_REF,
    ASL_MSG_LOCAL_NOT_USED,
    ASL_MSG_ARG_AS_LOCAL_NOT_USED,
    ASL_MSG_ARG_NOT_USED,
    ASL_MSG_CONSTANT_REQUIRED,
    ASL_MSG_CROSS_TABLE_SCOPE,
    ASL_MSG_EXCEPTION_NOT_RECEIVED,
    ASL_MSG_NULL_RESOURCE_TEMPLATE,
    ASL_MSG_FOUND_HERE,
    ASL_MSG_ILLEGAL_RECURSION,
    ASL_MSG_PLACE_HOLDER_00,
    ASL_MSG_PLACE_HOLDER_01,
    ASL_MSG_OEM_TABLE_ID,
    ASL_MSG_OEM_ID,
    ASL_MSG_UNLOAD,
    ASL_MSG_OFFSET,
    ASL_MSG_LONG_SLEEP,
    ASL_MSG_PREFIX_NOT_EXIST,
    ASL_MSG_NAMEPATH_NOT_EXIST,
    ASL_MSG_REGION_LENGTH,
    ASL_MSG_TEMPORARY_OBJECT,

    /* These messages are used by the Data Table compiler only */

    ASL_MSG_BUFFER_ELEMENT = ASL_MSG_TABLE_COMPILER,
    ASL_MSG_DIVIDE_BY_ZERO,
    ASL_MSG_FLAG_VALUE,
    ASL_MSG_INTEGER_SIZE,
    ASL_MSG_INVALID_EXPRESSION,
    ASL_MSG_INVALID_FIELD_NAME,
    ASL_MSG_INVALID_HEX_INTEGER,
    ASL_MSG_OEM_TABLE,
    ASL_MSG_RESERVED_VALUE,
    ASL_MSG_UNKNOWN_LABEL,
    ASL_MSG_UNKNOWN_SUBTABLE,
    ASL_MSG_UNKNOWN_TABLE,
    ASL_MSG_ZERO_VALUE,

    /* These messages are used by the Preprocessor only */

    ASL_MSG_DIRECTIVE_SYNTAX = ASL_MSG_PREPROCESSOR,
    ASL_MSG_ENDIF_MISMATCH,
    ASL_MSG_ERROR_DIRECTIVE,
    ASL_MSG_EXISTING_NAME,
    ASL_MSG_INVALID_INVOCATION,
    ASL_MSG_MACRO_SYNTAX,
    ASL_MSG_TOO_MANY_ARGUMENTS,
    ASL_MSG_UNKNOWN_DIRECTIVE,
    ASL_MSG_UNKNOWN_PRAGMA,
    ASL_MSG_WARNING_DIRECTIVE,
    ASL_MSG_INCLUDE_FILE

} ASL_MESSAGE_IDS;


#endif  /* __ASLMESSAGES_H */

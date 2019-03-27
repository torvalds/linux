/******************************************************************************
 *
 * Module Name: psopcode - Parser/Interpreter opcode information table
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

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acopcode.h>
#include <contrib/dev/acpica/include/amlcode.h>


#define _COMPONENT          ACPI_PARSER
        ACPI_MODULE_NAME    ("psopcode")


/*******************************************************************************
 *
 * NAME:        AcpiGbl_AmlOpInfo
 *
 * DESCRIPTION: Opcode table. Each entry contains <opcode, type, name, operands>
 *              The name is a simple ascii string, the operand specifier is an
 *              ascii string with one letter per operand. The letter specifies
 *              the operand type.
 *
 ******************************************************************************/

/*
 * Summary of opcode types/flags
 *

 Opcodes that have associated namespace objects (AML_NSOBJECT flag)

    AML_SCOPE_OP
    AML_DEVICE_OP
    AML_THERMAL_ZONE_OP
    AML_METHOD_OP
    AML_POWER_RESOURCE_OP
    AML_PROCESSOR_OP
    AML_FIELD_OP
    AML_INDEX_FIELD_OP
    AML_BANK_FIELD_OP
    AML_NAME_OP
    AML_ALIAS_OP
    AML_MUTEX_OP
    AML_EVENT_OP
    AML_REGION_OP
    AML_CREATE_FIELD_OP
    AML_CREATE_BIT_FIELD_OP
    AML_CREATE_BYTE_FIELD_OP
    AML_CREATE_WORD_FIELD_OP
    AML_CREATE_DWORD_FIELD_OP
    AML_CREATE_QWORD_FIELD_OP
    AML_INT_NAMEDFIELD_OP
    AML_INT_METHODCALL_OP
    AML_INT_NAMEPATH_OP

  Opcodes that are "namespace" opcodes (AML_NSOPCODE flag)

    AML_SCOPE_OP
    AML_DEVICE_OP
    AML_THERMAL_ZONE_OP
    AML_METHOD_OP
    AML_POWER_RESOURCE_OP
    AML_PROCESSOR_OP
    AML_FIELD_OP
    AML_INDEX_FIELD_OP
    AML_BANK_FIELD_OP
    AML_NAME_OP
    AML_ALIAS_OP
    AML_MUTEX_OP
    AML_EVENT_OP
    AML_REGION_OP
    AML_INT_NAMEDFIELD_OP

  Opcodes that have an associated namespace node (AML_NSNODE flag)

    AML_SCOPE_OP
    AML_DEVICE_OP
    AML_THERMAL_ZONE_OP
    AML_METHOD_OP
    AML_POWER_RESOURCE_OP
    AML_PROCESSOR_OP
    AML_NAME_OP
    AML_ALIAS_OP
    AML_MUTEX_OP
    AML_EVENT_OP
    AML_REGION_OP
    AML_CREATE_FIELD_OP
    AML_CREATE_BIT_FIELD_OP
    AML_CREATE_BYTE_FIELD_OP
    AML_CREATE_WORD_FIELD_OP
    AML_CREATE_DWORD_FIELD_OP
    AML_CREATE_QWORD_FIELD_OP
    AML_INT_NAMEDFIELD_OP
    AML_INT_METHODCALL_OP
    AML_INT_NAMEPATH_OP

  Opcodes that define named ACPI objects (AML_NAMED flag)

    AML_SCOPE_OP
    AML_DEVICE_OP
    AML_THERMAL_ZONE_OP
    AML_METHOD_OP
    AML_POWER_RESOURCE_OP
    AML_PROCESSOR_OP
    AML_NAME_OP
    AML_ALIAS_OP
    AML_MUTEX_OP
    AML_EVENT_OP
    AML_REGION_OP
    AML_INT_NAMEDFIELD_OP

  Opcodes that contain executable AML as part of the definition that
  must be deferred until needed

    AML_METHOD_OP
    AML_VARIABLE_PACKAGE_OP
    AML_CREATE_FIELD_OP
    AML_CREATE_BIT_FIELD_OP
    AML_CREATE_BYTE_FIELD_OP
    AML_CREATE_WORD_FIELD_OP
    AML_CREATE_DWORD_FIELD_OP
    AML_CREATE_QWORD_FIELD_OP
    AML_REGION_OP
    AML_BUFFER_OP

  Field opcodes

    AML_CREATE_FIELD_OP
    AML_FIELD_OP
    AML_INDEX_FIELD_OP
    AML_BANK_FIELD_OP

  Field "Create" opcodes

    AML_CREATE_FIELD_OP
    AML_CREATE_BIT_FIELD_OP
    AML_CREATE_BYTE_FIELD_OP
    AML_CREATE_WORD_FIELD_OP
    AML_CREATE_DWORD_FIELD_OP
    AML_CREATE_QWORD_FIELD_OP

 ******************************************************************************/


/*
 * Master Opcode information table. A summary of everything we know about each
 * opcode, all in one place.
 */
const ACPI_OPCODE_INFO    AcpiGbl_AmlOpInfo[AML_NUM_OPCODES] =
{
/*! [Begin] no source code translation */
/* Index           Name                 Parser Args               Interpreter Args                ObjectType                    Class                      Type                  Flags */

/* 00 */ ACPI_OP ("Zero",               ARGP_ZERO_OP,              ARGI_ZERO_OP,               ACPI_TYPE_INTEGER,           AML_CLASS_ARGUMENT,        AML_TYPE_CONSTANT,        AML_CONSTANT),
/* 01 */ ACPI_OP ("One",                ARGP_ONE_OP,               ARGI_ONE_OP,                ACPI_TYPE_INTEGER,           AML_CLASS_ARGUMENT,        AML_TYPE_CONSTANT,        AML_CONSTANT),
/* 02 */ ACPI_OP ("Alias",              ARGP_ALIAS_OP,             ARGI_ALIAS_OP,              ACPI_TYPE_LOCAL_ALIAS,       AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 03 */ ACPI_OP ("Name",               ARGP_NAME_OP,              ARGI_NAME_OP,               ACPI_TYPE_ANY,               AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_COMPLEX,   AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 04 */ ACPI_OP ("ByteConst",          ARGP_BYTE_OP,              ARGI_BYTE_OP,               ACPI_TYPE_INTEGER,           AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         AML_CONSTANT),
/* 05 */ ACPI_OP ("WordConst",          ARGP_WORD_OP,              ARGI_WORD_OP,               ACPI_TYPE_INTEGER,           AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         AML_CONSTANT),
/* 06 */ ACPI_OP ("DwordConst",         ARGP_DWORD_OP,             ARGI_DWORD_OP,              ACPI_TYPE_INTEGER,           AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         AML_CONSTANT),
/* 07 */ ACPI_OP ("String",             ARGP_STRING_OP,            ARGI_STRING_OP,             ACPI_TYPE_STRING,            AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         AML_CONSTANT),
/* 08 */ ACPI_OP ("Scope",              ARGP_SCOPE_OP,             ARGI_SCOPE_OP,              ACPI_TYPE_LOCAL_SCOPE,       AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_NO_OBJ,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 09 */ ACPI_OP ("Buffer",             ARGP_BUFFER_OP,            ARGI_BUFFER_OP,             ACPI_TYPE_BUFFER,            AML_CLASS_CREATE,          AML_TYPE_CREATE_OBJECT,   AML_HAS_ARGS | AML_DEFER | AML_CONSTANT),
/* 0A */ ACPI_OP ("Package",            ARGP_PACKAGE_OP,           ARGI_PACKAGE_OP,            ACPI_TYPE_PACKAGE,           AML_CLASS_CREATE,          AML_TYPE_CREATE_OBJECT,   AML_HAS_ARGS | AML_DEFER | AML_CONSTANT),
/* 0B */ ACPI_OP ("Method",             ARGP_METHOD_OP,            ARGI_METHOD_OP,             ACPI_TYPE_METHOD,            AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_COMPLEX,   AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED | AML_DEFER),
/* 0C */ ACPI_OP ("Local0",             ARGP_LOCAL0,               ARGI_LOCAL0,                ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 0D */ ACPI_OP ("Local1",             ARGP_LOCAL1,               ARGI_LOCAL1,                ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 0E */ ACPI_OP ("Local2",             ARGP_LOCAL2,               ARGI_LOCAL2,                ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 0F */ ACPI_OP ("Local3",             ARGP_LOCAL3,               ARGI_LOCAL3,                ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 10 */ ACPI_OP ("Local4",             ARGP_LOCAL4,               ARGI_LOCAL4,                ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 11 */ ACPI_OP ("Local5",             ARGP_LOCAL5,               ARGI_LOCAL5,                ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 12 */ ACPI_OP ("Local6",             ARGP_LOCAL6,               ARGI_LOCAL6,                ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 13 */ ACPI_OP ("Local7",             ARGP_LOCAL7,               ARGI_LOCAL7,                ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_LOCAL_VARIABLE,  0),
/* 14 */ ACPI_OP ("Arg0",               ARGP_ARG0,                 ARGI_ARG0,                  ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 15 */ ACPI_OP ("Arg1",               ARGP_ARG1,                 ARGI_ARG1,                  ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 16 */ ACPI_OP ("Arg2",               ARGP_ARG2,                 ARGI_ARG2,                  ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 17 */ ACPI_OP ("Arg3",               ARGP_ARG3,                 ARGI_ARG3,                  ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 18 */ ACPI_OP ("Arg4",               ARGP_ARG4,                 ARGI_ARG4,                  ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 19 */ ACPI_OP ("Arg5",               ARGP_ARG5,                 ARGI_ARG5,                  ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 1A */ ACPI_OP ("Arg6",               ARGP_ARG6,                 ARGI_ARG6,                  ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_METHOD_ARGUMENT, 0),
/* 1B */ ACPI_OP ("Store",              ARGP_STORE_OP,             ARGI_STORE_OP,              ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 1C */ ACPI_OP ("RefOf",              ARGP_REF_OF_OP,            ARGI_REF_OF_OP,             ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R),
/* 1D */ ACPI_OP ("Add",                ARGP_ADD_OP,               ARGI_ADD_OP,                ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH | AML_CONSTANT),
/* 1E */ ACPI_OP ("Concatenate",        ARGP_CONCAT_OP,            ARGI_CONCAT_OP,             ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_CONSTANT),
/* 1F */ ACPI_OP ("Subtract",           ARGP_SUBTRACT_OP,          ARGI_SUBTRACT_OP,           ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH | AML_CONSTANT),
/* 20 */ ACPI_OP ("Increment",          ARGP_INCREMENT_OP,         ARGI_INCREMENT_OP,          ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R | AML_CONSTANT),
/* 21 */ ACPI_OP ("Decrement",          ARGP_DECREMENT_OP,         ARGI_DECREMENT_OP,          ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R | AML_CONSTANT),
/* 22 */ ACPI_OP ("Multiply",           ARGP_MULTIPLY_OP,          ARGI_MULTIPLY_OP,           ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH | AML_CONSTANT),
/* 23 */ ACPI_OP ("Divide",             ARGP_DIVIDE_OP,            ARGI_DIVIDE_OP,             ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_2T_1R,   AML_FLAGS_EXEC_2A_2T_1R | AML_CONSTANT),
/* 24 */ ACPI_OP ("ShiftLeft",          ARGP_SHIFT_LEFT_OP,        ARGI_SHIFT_LEFT_OP,         ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH | AML_CONSTANT),
/* 25 */ ACPI_OP ("ShiftRight",         ARGP_SHIFT_RIGHT_OP,       ARGI_SHIFT_RIGHT_OP,        ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH | AML_CONSTANT),
/* 26 */ ACPI_OP ("And",                ARGP_BIT_AND_OP,           ARGI_BIT_AND_OP,            ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH | AML_CONSTANT),
/* 27 */ ACPI_OP ("NAnd",               ARGP_BIT_NAND_OP,          ARGI_BIT_NAND_OP,           ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH | AML_CONSTANT),
/* 28 */ ACPI_OP ("Or",                 ARGP_BIT_OR_OP,            ARGI_BIT_OR_OP,             ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH | AML_CONSTANT),
/* 29 */ ACPI_OP ("NOr",                ARGP_BIT_NOR_OP,           ARGI_BIT_NOR_OP,            ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH | AML_CONSTANT),
/* 2A */ ACPI_OP ("XOr",                ARGP_BIT_XOR_OP,           ARGI_BIT_XOR_OP,            ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_MATH | AML_CONSTANT),
/* 2B */ ACPI_OP ("Not",                ARGP_BIT_NOT_OP,           ARGI_BIT_NOT_OP,            ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R | AML_CONSTANT),
/* 2C */ ACPI_OP ("FindSetLeftBit",     ARGP_FIND_SET_LEFT_BIT_OP, ARGI_FIND_SET_LEFT_BIT_OP,  ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R | AML_CONSTANT),
/* 2D */ ACPI_OP ("FindSetRightBit",    ARGP_FIND_SET_RIGHT_BIT_OP,ARGI_FIND_SET_RIGHT_BIT_OP, ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R | AML_CONSTANT),
/* 2E */ ACPI_OP ("DerefOf",            ARGP_DEREF_OF_OP,          ARGI_DEREF_OF_OP,           ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R),
/* 2F */ ACPI_OP ("Notify",             ARGP_NOTIFY_OP,            ARGI_NOTIFY_OP,             ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_0R,   AML_FLAGS_EXEC_2A_0T_0R),
/* 30 */ ACPI_OP ("SizeOf",             ARGP_SIZE_OF_OP,           ARGI_SIZE_OF_OP,            ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R | AML_NO_OPERAND_RESOLVE),
/* 31 */ ACPI_OP ("Index",              ARGP_INDEX_OP,             ARGI_INDEX_OP,              ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R),
/* 32 */ ACPI_OP ("Match",              ARGP_MATCH_OP,             ARGI_MATCH_OP,              ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_6A_0T_1R,   AML_FLAGS_EXEC_6A_0T_1R | AML_CONSTANT),
/* 33 */ ACPI_OP ("CreateDWordField",   ARGP_CREATE_DWORD_FIELD_OP,ARGI_CREATE_DWORD_FIELD_OP, ACPI_TYPE_BUFFER_FIELD,      AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_CREATE),
/* 34 */ ACPI_OP ("CreateWordField",    ARGP_CREATE_WORD_FIELD_OP, ARGI_CREATE_WORD_FIELD_OP,  ACPI_TYPE_BUFFER_FIELD,      AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_CREATE),
/* 35 */ ACPI_OP ("CreateByteField",    ARGP_CREATE_BYTE_FIELD_OP, ARGI_CREATE_BYTE_FIELD_OP,  ACPI_TYPE_BUFFER_FIELD,      AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_CREATE),
/* 36 */ ACPI_OP ("CreateBitField",     ARGP_CREATE_BIT_FIELD_OP,  ARGI_CREATE_BIT_FIELD_OP,   ACPI_TYPE_BUFFER_FIELD,      AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_CREATE),
/* 37 */ ACPI_OP ("ObjectType",         ARGP_OBJECT_TYPE_OP,       ARGI_OBJECT_TYPE_OP,        ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R | AML_NO_OPERAND_RESOLVE),
/* 38 */ ACPI_OP ("LAnd",               ARGP_LAND_OP,              ARGI_LAND_OP,               ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R | AML_LOGICAL_NUMERIC | AML_CONSTANT),
/* 39 */ ACPI_OP ("LOr",                ARGP_LOR_OP,               ARGI_LOR_OP,                ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R | AML_LOGICAL_NUMERIC | AML_CONSTANT),
/* 3A */ ACPI_OP ("LNot",               ARGP_LNOT_OP,              ARGI_LNOT_OP,               ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_1R,   AML_FLAGS_EXEC_1A_0T_1R | AML_CONSTANT),
/* 3B */ ACPI_OP ("LEqual",             ARGP_LEQUAL_OP,            ARGI_LEQUAL_OP,             ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R | AML_LOGICAL | AML_CONSTANT),
/* 3C */ ACPI_OP ("LGreater",           ARGP_LGREATER_OP,          ARGI_LGREATER_OP,           ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R | AML_LOGICAL | AML_CONSTANT),
/* 3D */ ACPI_OP ("LLess",              ARGP_LLESS_OP,             ARGI_LLESS_OP,              ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R | AML_LOGICAL | AML_CONSTANT),
/* 3E */ ACPI_OP ("If",                 ARGP_IF_OP,                ARGI_IF_OP,                 ACPI_TYPE_ANY,               AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         AML_HAS_ARGS),
/* 3F */ ACPI_OP ("Else",               ARGP_ELSE_OP,              ARGI_ELSE_OP,               ACPI_TYPE_ANY,               AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         AML_HAS_ARGS),
/* 40 */ ACPI_OP ("While",              ARGP_WHILE_OP,             ARGI_WHILE_OP,              ACPI_TYPE_ANY,               AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         AML_HAS_ARGS),
/* 41 */ ACPI_OP ("Noop",               ARGP_NOOP_OP,              ARGI_NOOP_OP,               ACPI_TYPE_ANY,               AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         0),
/* 42 */ ACPI_OP ("Return",             ARGP_RETURN_OP,            ARGI_RETURN_OP,             ACPI_TYPE_ANY,               AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         AML_HAS_ARGS),
/* 43 */ ACPI_OP ("Break",              ARGP_BREAK_OP,             ARGI_BREAK_OP,              ACPI_TYPE_ANY,               AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         0),
/* 44 */ ACPI_OP ("BreakPoint",         ARGP_BREAK_POINT_OP,       ARGI_BREAK_POINT_OP,        ACPI_TYPE_ANY,               AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         0),
/* 45 */ ACPI_OP ("Ones",               ARGP_ONES_OP,              ARGI_ONES_OP,               ACPI_TYPE_INTEGER,           AML_CLASS_ARGUMENT,        AML_TYPE_CONSTANT,        AML_CONSTANT),

/* Prefixed opcodes (Two-byte opcodes with a prefix op) */

/* 46 */ ACPI_OP ("Mutex",              ARGP_MUTEX_OP,             ARGI_MUTEX_OP,              ACPI_TYPE_MUTEX,             AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 47 */ ACPI_OP ("Event",              ARGP_EVENT_OP,             ARGI_EVENT_OP,              ACPI_TYPE_EVENT,             AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED ),
/* 48 */ ACPI_OP ("CondRefOf",          ARGP_COND_REF_OF_OP,       ARGI_COND_REF_OF_OP,        ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 49 */ ACPI_OP ("CreateField",        ARGP_CREATE_FIELD_OP,      ARGI_CREATE_FIELD_OP,       ACPI_TYPE_BUFFER_FIELD,      AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_FIELD | AML_CREATE),
/* 4A */ ACPI_OP ("Load",               ARGP_LOAD_OP,              ARGI_LOAD_OP,               ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_0R,   AML_FLAGS_EXEC_1A_1T_0R),
/* 4B */ ACPI_OP ("Stall",              ARGP_STALL_OP,             ARGI_STALL_OP,              ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 4C */ ACPI_OP ("Sleep",              ARGP_SLEEP_OP,             ARGI_SLEEP_OP,              ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 4D */ ACPI_OP ("Acquire",            ARGP_ACQUIRE_OP,           ARGI_ACQUIRE_OP,            ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R),
/* 4E */ ACPI_OP ("Signal",             ARGP_SIGNAL_OP,            ARGI_SIGNAL_OP,             ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 4F */ ACPI_OP ("Wait",               ARGP_WAIT_OP,              ARGI_WAIT_OP,               ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_0T_1R,   AML_FLAGS_EXEC_2A_0T_1R),
/* 50 */ ACPI_OP ("Reset",              ARGP_RESET_OP,             ARGI_RESET_OP,              ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 51 */ ACPI_OP ("Release",            ARGP_RELEASE_OP,           ARGI_RELEASE_OP,            ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 52 */ ACPI_OP ("FromBCD",            ARGP_FROM_BCD_OP,          ARGI_FROM_BCD_OP,           ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R | AML_CONSTANT),
/* 53 */ ACPI_OP ("ToBCD",              ARGP_TO_BCD_OP,            ARGI_TO_BCD_OP,             ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R | AML_CONSTANT),
/* 54 */ ACPI_OP ("Unload",             ARGP_UNLOAD_OP,            ARGI_UNLOAD_OP,             ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_0T_0R,   AML_FLAGS_EXEC_1A_0T_0R),
/* 55 */ ACPI_OP ("Revision",           ARGP_REVISION_OP,          ARGI_REVISION_OP,           ACPI_TYPE_INTEGER,           AML_CLASS_ARGUMENT,        AML_TYPE_CONSTANT,        0),
/* 56 */ ACPI_OP ("Debug",              ARGP_DEBUG_OP,             ARGI_DEBUG_OP,              ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_CONSTANT,        0),
/* 57 */ ACPI_OP ("Fatal",              ARGP_FATAL_OP,             ARGI_FATAL_OP,              ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_3A_0T_0R,   AML_FLAGS_EXEC_3A_0T_0R),
/* 58 */ ACPI_OP ("OperationRegion",    ARGP_REGION_OP,            ARGI_REGION_OP,             ACPI_TYPE_REGION,            AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_COMPLEX,   AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED | AML_DEFER),
/* 59 */ ACPI_OP ("Field",              ARGP_FIELD_OP,             ARGI_FIELD_OP,              ACPI_TYPE_ANY,               AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_FIELD,     AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_FIELD),
/* 5A */ ACPI_OP ("Device",             ARGP_DEVICE_OP,            ARGI_DEVICE_OP,             ACPI_TYPE_DEVICE,            AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_NO_OBJ,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 5B */ ACPI_OP ("Processor",          ARGP_PROCESSOR_OP,         ARGI_PROCESSOR_OP,          ACPI_TYPE_PROCESSOR,         AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 5C */ ACPI_OP ("PowerResource",      ARGP_POWER_RES_OP,         ARGI_POWER_RES_OP,          ACPI_TYPE_POWER,             AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 5D */ ACPI_OP ("ThermalZone",        ARGP_THERMAL_ZONE_OP,      ARGI_THERMAL_ZONE_OP,       ACPI_TYPE_THERMAL,           AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_NO_OBJ,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 5E */ ACPI_OP ("IndexField",         ARGP_INDEX_FIELD_OP,       ARGI_INDEX_FIELD_OP,        ACPI_TYPE_ANY,               AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_FIELD,     AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_FIELD),
/* 5F */ ACPI_OP ("BankField",          ARGP_BANK_FIELD_OP,        ARGI_BANK_FIELD_OP,         ACPI_TYPE_LOCAL_BANK_FIELD,  AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_FIELD,     AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_FIELD | AML_DEFER),

/* Internal opcodes that map to invalid AML opcodes */

/* 60 */ ACPI_OP ("LNotEqual",          ARGP_LNOTEQUAL_OP,         ARGI_LNOTEQUAL_OP,          ACPI_TYPE_ANY,               AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           AML_HAS_ARGS | AML_CONSTANT),
/* 61 */ ACPI_OP ("LLessEqual",         ARGP_LLESSEQUAL_OP,        ARGI_LLESSEQUAL_OP,         ACPI_TYPE_ANY,               AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           AML_HAS_ARGS | AML_CONSTANT),
/* 62 */ ACPI_OP ("LGreaterEqual",      ARGP_LGREATEREQUAL_OP,     ARGI_LGREATEREQUAL_OP,      ACPI_TYPE_ANY,               AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           AML_HAS_ARGS | AML_CONSTANT),
/* 63 */ ACPI_OP ("-NamePath-",         ARGP_NAMEPATH_OP,          ARGI_NAMEPATH_OP,           ACPI_TYPE_LOCAL_REFERENCE,   AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         AML_NSOBJECT | AML_NSNODE ),
/* 64 */ ACPI_OP ("-MethodCall-",       ARGP_METHODCALL_OP,        ARGI_METHODCALL_OP,         ACPI_TYPE_METHOD,            AML_CLASS_METHOD_CALL,     AML_TYPE_METHOD_CALL,     AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE),
/* 65 */ ACPI_OP ("-ByteList-",         ARGP_BYTELIST_OP,          ARGI_BYTELIST_OP,           ACPI_TYPE_ANY,               AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         0),
/* 66 */ ACPI_OP ("-ReservedField-",    ARGP_RESERVEDFIELD_OP,     ARGI_RESERVEDFIELD_OP,      ACPI_TYPE_ANY,               AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           0),
/* 67 */ ACPI_OP ("-NamedField-",       ARGP_NAMEDFIELD_OP,        ARGI_NAMEDFIELD_OP,         ACPI_TYPE_ANY,               AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED ),
/* 68 */ ACPI_OP ("-AccessField-",      ARGP_ACCESSFIELD_OP,       ARGI_ACCESSFIELD_OP,        ACPI_TYPE_ANY,               AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           0),
/* 69 */ ACPI_OP ("-StaticString",      ARGP_STATICSTRING_OP,      ARGI_STATICSTRING_OP,       ACPI_TYPE_ANY,               AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           0),
/* 6A */ ACPI_OP ("-Return Value-",     ARG_NONE,                  ARG_NONE,                   ACPI_TYPE_ANY,               AML_CLASS_RETURN_VALUE,    AML_TYPE_RETURN,          AML_HAS_ARGS | AML_HAS_RETVAL),
/* 6B */ ACPI_OP ("-UNKNOWN_OP-",       ARG_NONE,                  ARG_NONE,                   ACPI_TYPE_INVALID,           AML_CLASS_UNKNOWN,         AML_TYPE_BOGUS,           AML_HAS_ARGS),
/* 6C */ ACPI_OP ("-ASCII_ONLY-",       ARG_NONE,                  ARG_NONE,                   ACPI_TYPE_ANY,               AML_CLASS_ASCII,           AML_TYPE_BOGUS,           AML_HAS_ARGS),
/* 6D */ ACPI_OP ("-PREFIX_ONLY-",      ARG_NONE,                  ARG_NONE,                   ACPI_TYPE_ANY,               AML_CLASS_PREFIX,          AML_TYPE_BOGUS,           AML_HAS_ARGS),

/* ACPI 2.0 opcodes */

/* 6E */ ACPI_OP ("QwordConst",         ARGP_QWORD_OP,             ARGI_QWORD_OP,              ACPI_TYPE_INTEGER,           AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         AML_CONSTANT),
/* 6F */ ACPI_OP ("Package", /* Var */  ARGP_VAR_PACKAGE_OP,       ARGI_VAR_PACKAGE_OP,        ACPI_TYPE_PACKAGE,           AML_CLASS_CREATE,          AML_TYPE_CREATE_OBJECT,   AML_HAS_ARGS | AML_DEFER),
/* 70 */ ACPI_OP ("ConcatenateResTemplate", ARGP_CONCAT_RES_OP,    ARGI_CONCAT_RES_OP,         ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_CONSTANT),
/* 71 */ ACPI_OP ("Mod",                ARGP_MOD_OP,               ARGI_MOD_OP,                ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_CONSTANT),
/* 72 */ ACPI_OP ("CreateQWordField",   ARGP_CREATE_QWORD_FIELD_OP,ARGI_CREATE_QWORD_FIELD_OP, ACPI_TYPE_BUFFER_FIELD,      AML_CLASS_CREATE,          AML_TYPE_CREATE_FIELD,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSNODE | AML_DEFER | AML_CREATE),
/* 73 */ ACPI_OP ("ToBuffer",           ARGP_TO_BUFFER_OP,         ARGI_TO_BUFFER_OP,          ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R | AML_CONSTANT),
/* 74 */ ACPI_OP ("ToDecimalString",    ARGP_TO_DEC_STR_OP,        ARGI_TO_DEC_STR_OP,         ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R | AML_CONSTANT),
/* 75 */ ACPI_OP ("ToHexString",        ARGP_TO_HEX_STR_OP,        ARGI_TO_HEX_STR_OP,         ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R | AML_CONSTANT),
/* 76 */ ACPI_OP ("ToInteger",          ARGP_TO_INTEGER_OP,        ARGI_TO_INTEGER_OP,         ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R | AML_CONSTANT),
/* 77 */ ACPI_OP ("ToString",           ARGP_TO_STRING_OP,         ARGI_TO_STRING_OP,          ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_2A_1T_1R,   AML_FLAGS_EXEC_2A_1T_1R | AML_CONSTANT),
/* 78 */ ACPI_OP ("CopyObject",         ARGP_COPY_OP,              ARGI_COPY_OP,               ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_1A_1T_1R,   AML_FLAGS_EXEC_1A_1T_1R),
/* 79 */ ACPI_OP ("Mid",                ARGP_MID_OP,               ARGI_MID_OP,                ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_3A_1T_1R,   AML_FLAGS_EXEC_3A_1T_1R | AML_CONSTANT),
/* 7A */ ACPI_OP ("Continue",           ARGP_CONTINUE_OP,          ARGI_CONTINUE_OP,           ACPI_TYPE_ANY,               AML_CLASS_CONTROL,         AML_TYPE_CONTROL,         0),
/* 7B */ ACPI_OP ("LoadTable",          ARGP_LOAD_TABLE_OP,        ARGI_LOAD_TABLE_OP,         ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_6A_0T_1R,   AML_FLAGS_EXEC_6A_0T_1R),
/* 7C */ ACPI_OP ("DataTableRegion",    ARGP_DATA_REGION_OP,       ARGI_DATA_REGION_OP,        ACPI_TYPE_REGION,            AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_COMPLEX,   AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED | AML_DEFER),
/* 7D */ ACPI_OP ("[EvalSubTree]",      ARGP_SCOPE_OP,             ARGI_SCOPE_OP,              ACPI_TYPE_ANY,               AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_NO_OBJ,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE),

/* ACPI 3.0 opcodes */

/* 7E */ ACPI_OP ("Timer",              ARGP_TIMER_OP,             ARGI_TIMER_OP,              ACPI_TYPE_ANY,               AML_CLASS_EXECUTE,         AML_TYPE_EXEC_0A_0T_1R,   AML_FLAGS_EXEC_0A_0T_1R),

/* ACPI 5.0 opcodes */

/* 7F */ ACPI_OP ("-ConnectField-",     ARGP_CONNECTFIELD_OP,      ARGI_CONNECTFIELD_OP,       ACPI_TYPE_ANY,               AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           AML_HAS_ARGS),
/* 80 */ ACPI_OP ("-ExtAccessField-",   ARGP_CONNECTFIELD_OP,      ARGI_CONNECTFIELD_OP,       ACPI_TYPE_ANY,               AML_CLASS_INTERNAL,        AML_TYPE_BOGUS,           0),

/* ACPI 6.0 opcodes */

/* 81 */ ACPI_OP ("External",           ARGP_EXTERNAL_OP,          ARGI_EXTERNAL_OP,           ACPI_TYPE_ANY,               AML_CLASS_NAMED_OBJECT,    AML_TYPE_NAMED_SIMPLE,    AML_HAS_ARGS | AML_NSOBJECT | AML_NSOPCODE | AML_NSNODE | AML_NAMED),
/* 82 */ ACPI_OP ("Comment",            ARGP_COMMENT_OP,           ARGI_COMMENT_OP,            ACPI_TYPE_STRING,            AML_CLASS_ARGUMENT,        AML_TYPE_LITERAL,         AML_CONSTANT)

/*! [End] no source code translation !*/
};

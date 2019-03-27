/******************************************************************************
 *
 * Name: acexcep.h - Exception codes returned by the ACPI subsystem
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

#ifndef __ACEXCEP_H__
#define __ACEXCEP_H__


/* This module contains all possible exception codes for ACPI_STATUS */

/*
 * Exception code classes
 */
#define AE_CODE_ENVIRONMENTAL           0x0000 /* General ACPICA environment */
#define AE_CODE_PROGRAMMER              0x1000 /* External ACPICA interface caller */
#define AE_CODE_ACPI_TABLES             0x2000 /* ACPI tables */
#define AE_CODE_AML                     0x3000 /* From executing AML code */
#define AE_CODE_CONTROL                 0x4000 /* Internal control codes */

#define AE_CODE_MAX                     0x4000
#define AE_CODE_MASK                    0xF000

/*
 * Macros to insert the exception code classes
 */
#define EXCEP_ENV(code)                 ((ACPI_STATUS) (code | AE_CODE_ENVIRONMENTAL))
#define EXCEP_PGM(code)                 ((ACPI_STATUS) (code | AE_CODE_PROGRAMMER))
#define EXCEP_TBL(code)                 ((ACPI_STATUS) (code | AE_CODE_ACPI_TABLES))
#define EXCEP_AML(code)                 ((ACPI_STATUS) (code | AE_CODE_AML))
#define EXCEP_CTL(code)                 ((ACPI_STATUS) (code | AE_CODE_CONTROL))

/*
 * Exception info table. The "Description" field is used only by the
 * ACPICA help application (acpihelp).
 */
typedef struct acpi_exception_info
{
    char                *Name;

#ifdef ACPI_HELP_APP
    char                *Description;
#endif
} ACPI_EXCEPTION_INFO;

#ifdef ACPI_HELP_APP
#define EXCEP_TXT(Name,Description)     {Name, Description}
#else
#define EXCEP_TXT(Name,Description)     {Name}
#endif


/*
 * Success is always zero, failure is non-zero
 */
#define ACPI_SUCCESS(a)                 (!(a))
#define ACPI_FAILURE(a)                 (a)

#define AE_OK                           (ACPI_STATUS) 0x0000

#define ACPI_ENV_EXCEPTION(Status)      (Status & AE_CODE_ENVIRONMENTAL)
#define ACPI_AML_EXCEPTION(Status)      (Status & AE_CODE_AML)
#define ACPI_PROG_EXCEPTION(Status)     (Status & AE_CODE_PROGRAMMER)
#define ACPI_TABLE_EXCEPTION(Status)    (Status & AE_CODE_ACPI_TABLES)
#define ACPI_CNTL_EXCEPTION(Status)     (Status & AE_CODE_CONTROL)


/*
 * Environmental exceptions
 */
#define AE_ERROR                        EXCEP_ENV (0x0001)
#define AE_NO_ACPI_TABLES               EXCEP_ENV (0x0002)
#define AE_NO_NAMESPACE                 EXCEP_ENV (0x0003)
#define AE_NO_MEMORY                    EXCEP_ENV (0x0004)
#define AE_NOT_FOUND                    EXCEP_ENV (0x0005)
#define AE_NOT_EXIST                    EXCEP_ENV (0x0006)
#define AE_ALREADY_EXISTS               EXCEP_ENV (0x0007)
#define AE_TYPE                         EXCEP_ENV (0x0008)
#define AE_NULL_OBJECT                  EXCEP_ENV (0x0009)
#define AE_NULL_ENTRY                   EXCEP_ENV (0x000A)
#define AE_BUFFER_OVERFLOW              EXCEP_ENV (0x000B)
#define AE_STACK_OVERFLOW               EXCEP_ENV (0x000C)
#define AE_STACK_UNDERFLOW              EXCEP_ENV (0x000D)
#define AE_NOT_IMPLEMENTED              EXCEP_ENV (0x000E)
#define AE_SUPPORT                      EXCEP_ENV (0x000F)
#define AE_LIMIT                        EXCEP_ENV (0x0010)
#define AE_TIME                         EXCEP_ENV (0x0011)
#define AE_ACQUIRE_DEADLOCK             EXCEP_ENV (0x0012)
#define AE_RELEASE_DEADLOCK             EXCEP_ENV (0x0013)
#define AE_NOT_ACQUIRED                 EXCEP_ENV (0x0014)
#define AE_ALREADY_ACQUIRED             EXCEP_ENV (0x0015)
#define AE_NO_HARDWARE_RESPONSE         EXCEP_ENV (0x0016)
#define AE_NO_GLOBAL_LOCK               EXCEP_ENV (0x0017)
#define AE_ABORT_METHOD                 EXCEP_ENV (0x0018)
#define AE_SAME_HANDLER                 EXCEP_ENV (0x0019)
#define AE_NO_HANDLER                   EXCEP_ENV (0x001A)
#define AE_OWNER_ID_LIMIT               EXCEP_ENV (0x001B)
#define AE_NOT_CONFIGURED               EXCEP_ENV (0x001C)
#define AE_ACCESS                       EXCEP_ENV (0x001D)
#define AE_IO_ERROR                     EXCEP_ENV (0x001E)
#define AE_NUMERIC_OVERFLOW             EXCEP_ENV (0x001F)
#define AE_HEX_OVERFLOW                 EXCEP_ENV (0x0020)
#define AE_DECIMAL_OVERFLOW             EXCEP_ENV (0x0021)
#define AE_OCTAL_OVERFLOW               EXCEP_ENV (0x0022)
#define AE_END_OF_TABLE                 EXCEP_ENV (0x0023)

#define AE_CODE_ENV_MAX                 0x0023


/*
 * Programmer exceptions
 */
#define AE_BAD_PARAMETER                EXCEP_PGM (0x0001)
#define AE_BAD_CHARACTER                EXCEP_PGM (0x0002)
#define AE_BAD_PATHNAME                 EXCEP_PGM (0x0003)
#define AE_BAD_DATA                     EXCEP_PGM (0x0004)
#define AE_BAD_HEX_CONSTANT             EXCEP_PGM (0x0005)
#define AE_BAD_OCTAL_CONSTANT           EXCEP_PGM (0x0006)
#define AE_BAD_DECIMAL_CONSTANT         EXCEP_PGM (0x0007)
#define AE_MISSING_ARGUMENTS            EXCEP_PGM (0x0008)
#define AE_BAD_ADDRESS                  EXCEP_PGM (0x0009)

#define AE_CODE_PGM_MAX                 0x0009


/*
 * Acpi table exceptions
 */
#define AE_BAD_SIGNATURE                EXCEP_TBL (0x0001)
#define AE_BAD_HEADER                   EXCEP_TBL (0x0002)
#define AE_BAD_CHECKSUM                 EXCEP_TBL (0x0003)
#define AE_BAD_VALUE                    EXCEP_TBL (0x0004)
#define AE_INVALID_TABLE_LENGTH         EXCEP_TBL (0x0005)

#define AE_CODE_TBL_MAX                 0x0005


/*
 * AML exceptions. These are caused by problems with
 * the actual AML byte stream
 */
#define AE_AML_BAD_OPCODE               EXCEP_AML (0x0001)
#define AE_AML_NO_OPERAND               EXCEP_AML (0x0002)
#define AE_AML_OPERAND_TYPE             EXCEP_AML (0x0003)
#define AE_AML_OPERAND_VALUE            EXCEP_AML (0x0004)
#define AE_AML_UNINITIALIZED_LOCAL      EXCEP_AML (0x0005)
#define AE_AML_UNINITIALIZED_ARG        EXCEP_AML (0x0006)
#define AE_AML_UNINITIALIZED_ELEMENT    EXCEP_AML (0x0007)
#define AE_AML_NUMERIC_OVERFLOW         EXCEP_AML (0x0008)
#define AE_AML_REGION_LIMIT             EXCEP_AML (0x0009)
#define AE_AML_BUFFER_LIMIT             EXCEP_AML (0x000A)
#define AE_AML_PACKAGE_LIMIT            EXCEP_AML (0x000B)
#define AE_AML_DIVIDE_BY_ZERO           EXCEP_AML (0x000C)
#define AE_AML_BAD_NAME                 EXCEP_AML (0x000D)
#define AE_AML_NAME_NOT_FOUND           EXCEP_AML (0x000E)
#define AE_AML_INTERNAL                 EXCEP_AML (0x000F)
#define AE_AML_INVALID_SPACE_ID         EXCEP_AML (0x0010)
#define AE_AML_STRING_LIMIT             EXCEP_AML (0x0011)
#define AE_AML_NO_RETURN_VALUE          EXCEP_AML (0x0012)
#define AE_AML_METHOD_LIMIT             EXCEP_AML (0x0013)
#define AE_AML_NOT_OWNER                EXCEP_AML (0x0014)
#define AE_AML_MUTEX_ORDER              EXCEP_AML (0x0015)
#define AE_AML_MUTEX_NOT_ACQUIRED       EXCEP_AML (0x0016)
#define AE_AML_INVALID_RESOURCE_TYPE    EXCEP_AML (0x0017)
#define AE_AML_INVALID_INDEX            EXCEP_AML (0x0018)
#define AE_AML_REGISTER_LIMIT           EXCEP_AML (0x0019)
#define AE_AML_NO_WHILE                 EXCEP_AML (0x001A)
#define AE_AML_ALIGNMENT                EXCEP_AML (0x001B)
#define AE_AML_NO_RESOURCE_END_TAG      EXCEP_AML (0x001C)
#define AE_AML_BAD_RESOURCE_VALUE       EXCEP_AML (0x001D)
#define AE_AML_CIRCULAR_REFERENCE       EXCEP_AML (0x001E)
#define AE_AML_BAD_RESOURCE_LENGTH      EXCEP_AML (0x001F)
#define AE_AML_ILLEGAL_ADDRESS          EXCEP_AML (0x0020)
#define AE_AML_LOOP_TIMEOUT             EXCEP_AML (0x0021)
#define AE_AML_UNINITIALIZED_NODE       EXCEP_AML (0x0022)
#define AE_AML_TARGET_TYPE              EXCEP_AML (0x0023)
#define AE_AML_PROTOCOL                 EXCEP_AML (0x0024)
#define AE_AML_BUFFER_LENGTH            EXCEP_AML (0x0025)

#define AE_CODE_AML_MAX                 0x0025


/*
 * Internal exceptions used for control
 */
#define AE_CTRL_RETURN_VALUE            EXCEP_CTL (0x0001)
#define AE_CTRL_PENDING                 EXCEP_CTL (0x0002)
#define AE_CTRL_TERMINATE               EXCEP_CTL (0x0003)
#define AE_CTRL_TRUE                    EXCEP_CTL (0x0004)
#define AE_CTRL_FALSE                   EXCEP_CTL (0x0005)
#define AE_CTRL_DEPTH                   EXCEP_CTL (0x0006)
#define AE_CTRL_END                     EXCEP_CTL (0x0007)
#define AE_CTRL_TRANSFER                EXCEP_CTL (0x0008)
#define AE_CTRL_BREAK                   EXCEP_CTL (0x0009)
#define AE_CTRL_CONTINUE                EXCEP_CTL (0x000A)
#define AE_CTRL_PARSE_CONTINUE          EXCEP_CTL (0x000B)
#define AE_CTRL_PARSE_PENDING           EXCEP_CTL (0x000C)

#define AE_CODE_CTRL_MAX                0x000C


/* Exception strings for AcpiFormatException */

#ifdef ACPI_DEFINE_EXCEPTION_TABLE

/*
 * String versions of the exception codes above
 * These strings must match the corresponding defines exactly
 */
static const ACPI_EXCEPTION_INFO    AcpiGbl_ExceptionNames_Env[] =
{
    EXCEP_TXT ("AE_OK",                         "No error"),
    EXCEP_TXT ("AE_ERROR",                      "Unspecified error"),
    EXCEP_TXT ("AE_NO_ACPI_TABLES",             "ACPI tables could not be found"),
    EXCEP_TXT ("AE_NO_NAMESPACE",               "A namespace has not been loaded"),
    EXCEP_TXT ("AE_NO_MEMORY",                  "Insufficient dynamic memory"),
    EXCEP_TXT ("AE_NOT_FOUND",                  "A requested entity is not found"),
    EXCEP_TXT ("AE_NOT_EXIST",                  "A required entity does not exist"),
    EXCEP_TXT ("AE_ALREADY_EXISTS",             "An entity already exists"),
    EXCEP_TXT ("AE_TYPE",                       "The object type is incorrect"),
    EXCEP_TXT ("AE_NULL_OBJECT",                "A required object was missing"),
    EXCEP_TXT ("AE_NULL_ENTRY",                 "The requested object does not exist"),
    EXCEP_TXT ("AE_BUFFER_OVERFLOW",            "The buffer provided is too small"),
    EXCEP_TXT ("AE_STACK_OVERFLOW",             "An internal stack overflowed"),
    EXCEP_TXT ("AE_STACK_UNDERFLOW",            "An internal stack underflowed"),
    EXCEP_TXT ("AE_NOT_IMPLEMENTED",            "The feature is not implemented"),
    EXCEP_TXT ("AE_SUPPORT",                    "The feature is not supported"),
    EXCEP_TXT ("AE_LIMIT",                      "A predefined limit was exceeded"),
    EXCEP_TXT ("AE_TIME",                       "A time limit or timeout expired"),
    EXCEP_TXT ("AE_ACQUIRE_DEADLOCK",           "Internal error, attempt was made to acquire a mutex in improper order"),
    EXCEP_TXT ("AE_RELEASE_DEADLOCK",           "Internal error, attempt was made to release a mutex in improper order"),
    EXCEP_TXT ("AE_NOT_ACQUIRED",               "An attempt to release a mutex or Global Lock without a previous acquire"),
    EXCEP_TXT ("AE_ALREADY_ACQUIRED",           "Internal error, attempt was made to acquire a mutex twice"),
    EXCEP_TXT ("AE_NO_HARDWARE_RESPONSE",       "Hardware did not respond after an I/O operation"),
    EXCEP_TXT ("AE_NO_GLOBAL_LOCK",             "There is no FACS Global Lock"),
    EXCEP_TXT ("AE_ABORT_METHOD",               "A control method was aborted"),
    EXCEP_TXT ("AE_SAME_HANDLER",               "Attempt was made to install the same handler that is already installed"),
    EXCEP_TXT ("AE_NO_HANDLER",                 "A handler for the operation is not installed"),
    EXCEP_TXT ("AE_OWNER_ID_LIMIT",             "There are no more Owner IDs available for ACPI tables or control methods"),
    EXCEP_TXT ("AE_NOT_CONFIGURED",             "The interface is not part of the current subsystem configuration"),
    EXCEP_TXT ("AE_ACCESS",                     "Permission denied for the requested operation"),
    EXCEP_TXT ("AE_IO_ERROR",                   "An I/O error occurred"),
    EXCEP_TXT ("AE_NUMERIC_OVERFLOW",           "Overflow during string-to-integer conversion"),
    EXCEP_TXT ("AE_HEX_OVERFLOW",               "Overflow during ASCII hex-to-binary conversion"),
    EXCEP_TXT ("AE_DECIMAL_OVERFLOW",           "Overflow during ASCII decimal-to-binary conversion"),
    EXCEP_TXT ("AE_OCTAL_OVERFLOW",             "Overflow during ASCII octal-to-binary conversion"),
    EXCEP_TXT ("AE_END_OF_TABLE",               "Reached the end of table")
};

static const ACPI_EXCEPTION_INFO    AcpiGbl_ExceptionNames_Pgm[] =
{
    EXCEP_TXT (NULL, NULL),
    EXCEP_TXT ("AE_BAD_PARAMETER",              "A parameter is out of range or invalid"),
    EXCEP_TXT ("AE_BAD_CHARACTER",              "An invalid character was found in a name"),
    EXCEP_TXT ("AE_BAD_PATHNAME",               "An invalid character was found in a pathname"),
    EXCEP_TXT ("AE_BAD_DATA",                   "A package or buffer contained incorrect data"),
    EXCEP_TXT ("AE_BAD_HEX_CONSTANT",           "Invalid character in a Hex constant"),
    EXCEP_TXT ("AE_BAD_OCTAL_CONSTANT",         "Invalid character in an Octal constant"),
    EXCEP_TXT ("AE_BAD_DECIMAL_CONSTANT",       "Invalid character in a Decimal constant"),
    EXCEP_TXT ("AE_MISSING_ARGUMENTS",          "Too few arguments were passed to a control method"),
    EXCEP_TXT ("AE_BAD_ADDRESS",                "An illegal null I/O address")
};

static const ACPI_EXCEPTION_INFO    AcpiGbl_ExceptionNames_Tbl[] =
{
    EXCEP_TXT (NULL, NULL),
    EXCEP_TXT ("AE_BAD_SIGNATURE",              "An ACPI table has an invalid signature"),
    EXCEP_TXT ("AE_BAD_HEADER",                 "Invalid field in an ACPI table header"),
    EXCEP_TXT ("AE_BAD_CHECKSUM",               "An ACPI table checksum is not correct"),
    EXCEP_TXT ("AE_BAD_VALUE",                  "An invalid value was found in a table"),
    EXCEP_TXT ("AE_INVALID_TABLE_LENGTH",       "The FADT or FACS has improper length")
};

static const ACPI_EXCEPTION_INFO    AcpiGbl_ExceptionNames_Aml[] =
{
    EXCEP_TXT (NULL, NULL),
    EXCEP_TXT ("AE_AML_BAD_OPCODE",             "Invalid AML opcode encountered"),
    EXCEP_TXT ("AE_AML_NO_OPERAND",             "A required operand is missing"),
    EXCEP_TXT ("AE_AML_OPERAND_TYPE",           "An operand of an incorrect type was encountered"),
    EXCEP_TXT ("AE_AML_OPERAND_VALUE",          "The operand had an inappropriate or invalid value"),
    EXCEP_TXT ("AE_AML_UNINITIALIZED_LOCAL",    "Method tried to use an uninitialized local variable"),
    EXCEP_TXT ("AE_AML_UNINITIALIZED_ARG",      "Method tried to use an uninitialized argument"),
    EXCEP_TXT ("AE_AML_UNINITIALIZED_ELEMENT",  "Method tried to use an empty package element"),
    EXCEP_TXT ("AE_AML_NUMERIC_OVERFLOW",       "Overflow during BCD conversion or other"),
    EXCEP_TXT ("AE_AML_REGION_LIMIT",           "Tried to access beyond the end of an Operation Region"),
    EXCEP_TXT ("AE_AML_BUFFER_LIMIT",           "Tried to access beyond the end of a buffer"),
    EXCEP_TXT ("AE_AML_PACKAGE_LIMIT",          "Tried to access beyond the end of a package"),
    EXCEP_TXT ("AE_AML_DIVIDE_BY_ZERO",         "During execution of AML Divide operator"),
    EXCEP_TXT ("AE_AML_BAD_NAME",               "An ACPI name contains invalid character(s)"),
    EXCEP_TXT ("AE_AML_NAME_NOT_FOUND",         "Could not resolve a named reference"),
    EXCEP_TXT ("AE_AML_INTERNAL",               "An internal error within the interpreter"),
    EXCEP_TXT ("AE_AML_INVALID_SPACE_ID",       "An Operation Region SpaceID is invalid"),
    EXCEP_TXT ("AE_AML_STRING_LIMIT",           "String is longer than 200 characters"),
    EXCEP_TXT ("AE_AML_NO_RETURN_VALUE",        "A method did not return a required value"),
    EXCEP_TXT ("AE_AML_METHOD_LIMIT",           "A control method reached the maximum reentrancy limit of 255"),
    EXCEP_TXT ("AE_AML_NOT_OWNER",              "A thread tried to release a mutex that it does not own"),
    EXCEP_TXT ("AE_AML_MUTEX_ORDER",            "Mutex SyncLevel release mismatch"),
    EXCEP_TXT ("AE_AML_MUTEX_NOT_ACQUIRED",     "Attempt to release a mutex that was not previously acquired"),
    EXCEP_TXT ("AE_AML_INVALID_RESOURCE_TYPE",  "Invalid resource type in resource list"),
    EXCEP_TXT ("AE_AML_INVALID_INDEX",          "Invalid Argx or Localx (x too large)"),
    EXCEP_TXT ("AE_AML_REGISTER_LIMIT",         "Bank value or Index value beyond range of register"),
    EXCEP_TXT ("AE_AML_NO_WHILE",               "Break or Continue without a While"),
    EXCEP_TXT ("AE_AML_ALIGNMENT",              "Non-aligned memory transfer on platform that does not support this"),
    EXCEP_TXT ("AE_AML_NO_RESOURCE_END_TAG",    "No End Tag in a resource list"),
    EXCEP_TXT ("AE_AML_BAD_RESOURCE_VALUE",     "Invalid value of a resource element"),
    EXCEP_TXT ("AE_AML_CIRCULAR_REFERENCE",     "Two references refer to each other"),
    EXCEP_TXT ("AE_AML_BAD_RESOURCE_LENGTH",    "The length of a Resource Descriptor in the AML is incorrect"),
    EXCEP_TXT ("AE_AML_ILLEGAL_ADDRESS",        "A memory, I/O, or PCI configuration address is invalid"),
    EXCEP_TXT ("AE_AML_LOOP_TIMEOUT",           "An AML While loop exceeded the maximum execution time"),
    EXCEP_TXT ("AE_AML_UNINITIALIZED_NODE",     "A namespace node is uninitialized or unresolved"),
    EXCEP_TXT ("AE_AML_TARGET_TYPE",            "A target operand of an incorrect type was encountered"),
    EXCEP_TXT ("AE_AML_PROTOCOL",               "Violation of a fixed ACPI protocol"),
    EXCEP_TXT ("AE_AML_BUFFER_LENGTH",          "The length of the buffer is invalid/incorrect")
};

static const ACPI_EXCEPTION_INFO    AcpiGbl_ExceptionNames_Ctrl[] =
{
    EXCEP_TXT (NULL, NULL),
    EXCEP_TXT ("AE_CTRL_RETURN_VALUE",          "A Method returned a value"),
    EXCEP_TXT ("AE_CTRL_PENDING",               "Method is calling another method"),
    EXCEP_TXT ("AE_CTRL_TERMINATE",             "Terminate the executing method"),
    EXCEP_TXT ("AE_CTRL_TRUE",                  "An If or While predicate result"),
    EXCEP_TXT ("AE_CTRL_FALSE",                 "An If or While predicate result"),
    EXCEP_TXT ("AE_CTRL_DEPTH",                 "Maximum search depth has been reached"),
    EXCEP_TXT ("AE_CTRL_END",                   "An If or While predicate is false"),
    EXCEP_TXT ("AE_CTRL_TRANSFER",              "Transfer control to called method"),
    EXCEP_TXT ("AE_CTRL_BREAK",                 "A Break has been executed"),
    EXCEP_TXT ("AE_CTRL_CONTINUE",              "A Continue has been executed"),
    EXCEP_TXT ("AE_CTRL_PARSE_CONTINUE",        "Used to skip over bad opcodes"),
    EXCEP_TXT ("AE_CTRL_PARSE_PENDING",         "Used to implement AML While loops")
};

#endif /* EXCEPTION_TABLE */

#endif /* __ACEXCEP_H__ */

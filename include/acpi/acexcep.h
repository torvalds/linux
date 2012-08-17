/******************************************************************************
 *
 * Name: acexcep.h - Exception codes returned by the ACPI subsystem
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

#ifndef __ACEXCEP_H__
#define __ACEXCEP_H__

/*
 * Exceptions returned by external ACPI interfaces
 */
#define AE_CODE_ENVIRONMENTAL           0x0000
#define AE_CODE_PROGRAMMER              0x1000
#define AE_CODE_ACPI_TABLES             0x2000
#define AE_CODE_AML                     0x3000
#define AE_CODE_CONTROL                 0x4000
#define AE_CODE_MAX                     0x4000
#define AE_CODE_MASK                    0xF000

#define ACPI_SUCCESS(a)                 (!(a))
#define ACPI_FAILURE(a)                 (a)

#define ACPI_SKIP(a)                    (a == AE_CTRL_SKIP)
#define AE_OK                           (acpi_status) 0x0000

/*
 * Environmental exceptions
 */
#define AE_ERROR                        (acpi_status) (0x0001 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_ACPI_TABLES               (acpi_status) (0x0002 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_NAMESPACE                 (acpi_status) (0x0003 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_MEMORY                    (acpi_status) (0x0004 | AE_CODE_ENVIRONMENTAL)
#define AE_NOT_FOUND                    (acpi_status) (0x0005 | AE_CODE_ENVIRONMENTAL)
#define AE_NOT_EXIST                    (acpi_status) (0x0006 | AE_CODE_ENVIRONMENTAL)
#define AE_ALREADY_EXISTS               (acpi_status) (0x0007 | AE_CODE_ENVIRONMENTAL)
#define AE_TYPE                         (acpi_status) (0x0008 | AE_CODE_ENVIRONMENTAL)
#define AE_NULL_OBJECT                  (acpi_status) (0x0009 | AE_CODE_ENVIRONMENTAL)
#define AE_NULL_ENTRY                   (acpi_status) (0x000A | AE_CODE_ENVIRONMENTAL)
#define AE_BUFFER_OVERFLOW              (acpi_status) (0x000B | AE_CODE_ENVIRONMENTAL)
#define AE_STACK_OVERFLOW               (acpi_status) (0x000C | AE_CODE_ENVIRONMENTAL)
#define AE_STACK_UNDERFLOW              (acpi_status) (0x000D | AE_CODE_ENVIRONMENTAL)
#define AE_NOT_IMPLEMENTED              (acpi_status) (0x000E | AE_CODE_ENVIRONMENTAL)
#define AE_SUPPORT                      (acpi_status) (0x000F | AE_CODE_ENVIRONMENTAL)
#define AE_LIMIT                        (acpi_status) (0x0010 | AE_CODE_ENVIRONMENTAL)
#define AE_TIME                         (acpi_status) (0x0011 | AE_CODE_ENVIRONMENTAL)
#define AE_ACQUIRE_DEADLOCK             (acpi_status) (0x0012 | AE_CODE_ENVIRONMENTAL)
#define AE_RELEASE_DEADLOCK             (acpi_status) (0x0013 | AE_CODE_ENVIRONMENTAL)
#define AE_NOT_ACQUIRED                 (acpi_status) (0x0014 | AE_CODE_ENVIRONMENTAL)
#define AE_ALREADY_ACQUIRED             (acpi_status) (0x0015 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_HARDWARE_RESPONSE         (acpi_status) (0x0016 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_GLOBAL_LOCK               (acpi_status) (0x0017 | AE_CODE_ENVIRONMENTAL)
#define AE_ABORT_METHOD                 (acpi_status) (0x0018 | AE_CODE_ENVIRONMENTAL)
#define AE_SAME_HANDLER                 (acpi_status) (0x0019 | AE_CODE_ENVIRONMENTAL)
#define AE_NO_HANDLER                   (acpi_status) (0x001A | AE_CODE_ENVIRONMENTAL)
#define AE_OWNER_ID_LIMIT               (acpi_status) (0x001B | AE_CODE_ENVIRONMENTAL)
#define AE_NOT_CONFIGURED               (acpi_status) (0x001C | AE_CODE_ENVIRONMENTAL)

#define AE_CODE_ENV_MAX                 0x001C

/*
 * Programmer exceptions
 */
#define AE_BAD_PARAMETER                (acpi_status) (0x0001 | AE_CODE_PROGRAMMER)
#define AE_BAD_CHARACTER                (acpi_status) (0x0002 | AE_CODE_PROGRAMMER)
#define AE_BAD_PATHNAME                 (acpi_status) (0x0003 | AE_CODE_PROGRAMMER)
#define AE_BAD_DATA                     (acpi_status) (0x0004 | AE_CODE_PROGRAMMER)
#define AE_BAD_HEX_CONSTANT             (acpi_status) (0x0005 | AE_CODE_PROGRAMMER)
#define AE_BAD_OCTAL_CONSTANT           (acpi_status) (0x0006 | AE_CODE_PROGRAMMER)
#define AE_BAD_DECIMAL_CONSTANT         (acpi_status) (0x0007 | AE_CODE_PROGRAMMER)
#define AE_MISSING_ARGUMENTS            (acpi_status) (0x0008 | AE_CODE_PROGRAMMER)
#define AE_BAD_ADDRESS                  (acpi_status) (0x0009 | AE_CODE_PROGRAMMER)

#define AE_CODE_PGM_MAX                 0x0009

/*
 * Acpi table exceptions
 */
#define AE_BAD_SIGNATURE                (acpi_status) (0x0001 | AE_CODE_ACPI_TABLES)
#define AE_BAD_HEADER                   (acpi_status) (0x0002 | AE_CODE_ACPI_TABLES)
#define AE_BAD_CHECKSUM                 (acpi_status) (0x0003 | AE_CODE_ACPI_TABLES)
#define AE_BAD_VALUE                    (acpi_status) (0x0004 | AE_CODE_ACPI_TABLES)
#define AE_INVALID_TABLE_LENGTH         (acpi_status) (0x0005 | AE_CODE_ACPI_TABLES)

#define AE_CODE_TBL_MAX                 0x0005

/*
 * AML exceptions.  These are caused by problems with
 * the actual AML byte stream
 */
#define AE_AML_BAD_OPCODE               (acpi_status) (0x0001 | AE_CODE_AML)
#define AE_AML_NO_OPERAND               (acpi_status) (0x0002 | AE_CODE_AML)
#define AE_AML_OPERAND_TYPE             (acpi_status) (0x0003 | AE_CODE_AML)
#define AE_AML_OPERAND_VALUE            (acpi_status) (0x0004 | AE_CODE_AML)
#define AE_AML_UNINITIALIZED_LOCAL      (acpi_status) (0x0005 | AE_CODE_AML)
#define AE_AML_UNINITIALIZED_ARG        (acpi_status) (0x0006 | AE_CODE_AML)
#define AE_AML_UNINITIALIZED_ELEMENT    (acpi_status) (0x0007 | AE_CODE_AML)
#define AE_AML_NUMERIC_OVERFLOW         (acpi_status) (0x0008 | AE_CODE_AML)
#define AE_AML_REGION_LIMIT             (acpi_status) (0x0009 | AE_CODE_AML)
#define AE_AML_BUFFER_LIMIT             (acpi_status) (0x000A | AE_CODE_AML)
#define AE_AML_PACKAGE_LIMIT            (acpi_status) (0x000B | AE_CODE_AML)
#define AE_AML_DIVIDE_BY_ZERO           (acpi_status) (0x000C | AE_CODE_AML)
#define AE_AML_BAD_NAME                 (acpi_status) (0x000D | AE_CODE_AML)
#define AE_AML_NAME_NOT_FOUND           (acpi_status) (0x000E | AE_CODE_AML)
#define AE_AML_INTERNAL                 (acpi_status) (0x000F | AE_CODE_AML)
#define AE_AML_INVALID_SPACE_ID         (acpi_status) (0x0010 | AE_CODE_AML)
#define AE_AML_STRING_LIMIT             (acpi_status) (0x0011 | AE_CODE_AML)
#define AE_AML_NO_RETURN_VALUE          (acpi_status) (0x0012 | AE_CODE_AML)
#define AE_AML_METHOD_LIMIT             (acpi_status) (0x0013 | AE_CODE_AML)
#define AE_AML_NOT_OWNER                (acpi_status) (0x0014 | AE_CODE_AML)
#define AE_AML_MUTEX_ORDER              (acpi_status) (0x0015 | AE_CODE_AML)
#define AE_AML_MUTEX_NOT_ACQUIRED       (acpi_status) (0x0016 | AE_CODE_AML)
#define AE_AML_INVALID_RESOURCE_TYPE    (acpi_status) (0x0017 | AE_CODE_AML)
#define AE_AML_INVALID_INDEX            (acpi_status) (0x0018 | AE_CODE_AML)
#define AE_AML_REGISTER_LIMIT           (acpi_status) (0x0019 | AE_CODE_AML)
#define AE_AML_NO_WHILE                 (acpi_status) (0x001A | AE_CODE_AML)
#define AE_AML_ALIGNMENT                (acpi_status) (0x001B | AE_CODE_AML)
#define AE_AML_NO_RESOURCE_END_TAG      (acpi_status) (0x001C | AE_CODE_AML)
#define AE_AML_BAD_RESOURCE_VALUE       (acpi_status) (0x001D | AE_CODE_AML)
#define AE_AML_CIRCULAR_REFERENCE       (acpi_status) (0x001E | AE_CODE_AML)
#define AE_AML_BAD_RESOURCE_LENGTH      (acpi_status) (0x001F | AE_CODE_AML)
#define AE_AML_ILLEGAL_ADDRESS          (acpi_status) (0x0020 | AE_CODE_AML)
#define AE_AML_INFINITE_LOOP            (acpi_status) (0x0021 | AE_CODE_AML)

#define AE_CODE_AML_MAX                 0x0021

/*
 * Internal exceptions used for control
 */
#define AE_CTRL_RETURN_VALUE            (acpi_status) (0x0001 | AE_CODE_CONTROL)
#define AE_CTRL_PENDING                 (acpi_status) (0x0002 | AE_CODE_CONTROL)
#define AE_CTRL_TERMINATE               (acpi_status) (0x0003 | AE_CODE_CONTROL)
#define AE_CTRL_TRUE                    (acpi_status) (0x0004 | AE_CODE_CONTROL)
#define AE_CTRL_FALSE                   (acpi_status) (0x0005 | AE_CODE_CONTROL)
#define AE_CTRL_DEPTH                   (acpi_status) (0x0006 | AE_CODE_CONTROL)
#define AE_CTRL_END                     (acpi_status) (0x0007 | AE_CODE_CONTROL)
#define AE_CTRL_TRANSFER                (acpi_status) (0x0008 | AE_CODE_CONTROL)
#define AE_CTRL_BREAK                   (acpi_status) (0x0009 | AE_CODE_CONTROL)
#define AE_CTRL_CONTINUE                (acpi_status) (0x000A | AE_CODE_CONTROL)
#define AE_CTRL_SKIP                    (acpi_status) (0x000B | AE_CODE_CONTROL)
#define AE_CTRL_PARSE_CONTINUE          (acpi_status) (0x000C | AE_CODE_CONTROL)
#define AE_CTRL_PARSE_PENDING           (acpi_status) (0x000D | AE_CODE_CONTROL)

#define AE_CODE_CTRL_MAX                0x000D

/* Exception strings for acpi_format_exception */

#ifdef ACPI_DEFINE_EXCEPTION_TABLE

/*
 * String versions of the exception codes above
 * These strings must match the corresponding defines exactly
 */
char const *acpi_gbl_exception_names_env[] = {
	"AE_OK",
	"AE_ERROR",
	"AE_NO_ACPI_TABLES",
	"AE_NO_NAMESPACE",
	"AE_NO_MEMORY",
	"AE_NOT_FOUND",
	"AE_NOT_EXIST",
	"AE_ALREADY_EXISTS",
	"AE_TYPE",
	"AE_NULL_OBJECT",
	"AE_NULL_ENTRY",
	"AE_BUFFER_OVERFLOW",
	"AE_STACK_OVERFLOW",
	"AE_STACK_UNDERFLOW",
	"AE_NOT_IMPLEMENTED",
	"AE_SUPPORT",
	"AE_LIMIT",
	"AE_TIME",
	"AE_ACQUIRE_DEADLOCK",
	"AE_RELEASE_DEADLOCK",
	"AE_NOT_ACQUIRED",
	"AE_ALREADY_ACQUIRED",
	"AE_NO_HARDWARE_RESPONSE",
	"AE_NO_GLOBAL_LOCK",
	"AE_ABORT_METHOD",
	"AE_SAME_HANDLER",
	"AE_NO_HANDLER",
	"AE_OWNER_ID_LIMIT",
	"AE_NOT_CONFIGURED"
};

char const *acpi_gbl_exception_names_pgm[] = {
	NULL,
	"AE_BAD_PARAMETER",
	"AE_BAD_CHARACTER",
	"AE_BAD_PATHNAME",
	"AE_BAD_DATA",
	"AE_BAD_HEX_CONSTANT",
	"AE_BAD_OCTAL_CONSTANT",
	"AE_BAD_DECIMAL_CONSTANT",
	"AE_MISSING_ARGUMENTS",
	"AE_BAD_ADDRESS"
};

char const *acpi_gbl_exception_names_tbl[] = {
	NULL,
	"AE_BAD_SIGNATURE",
	"AE_BAD_HEADER",
	"AE_BAD_CHECKSUM",
	"AE_BAD_VALUE",
	"AE_INVALID_TABLE_LENGTH"
};

char const *acpi_gbl_exception_names_aml[] = {
	NULL,
	"AE_AML_BAD_OPCODE",
	"AE_AML_NO_OPERAND",
	"AE_AML_OPERAND_TYPE",
	"AE_AML_OPERAND_VALUE",
	"AE_AML_UNINITIALIZED_LOCAL",
	"AE_AML_UNINITIALIZED_ARG",
	"AE_AML_UNINITIALIZED_ELEMENT",
	"AE_AML_NUMERIC_OVERFLOW",
	"AE_AML_REGION_LIMIT",
	"AE_AML_BUFFER_LIMIT",
	"AE_AML_PACKAGE_LIMIT",
	"AE_AML_DIVIDE_BY_ZERO",
	"AE_AML_BAD_NAME",
	"AE_AML_NAME_NOT_FOUND",
	"AE_AML_INTERNAL",
	"AE_AML_INVALID_SPACE_ID",
	"AE_AML_STRING_LIMIT",
	"AE_AML_NO_RETURN_VALUE",
	"AE_AML_METHOD_LIMIT",
	"AE_AML_NOT_OWNER",
	"AE_AML_MUTEX_ORDER",
	"AE_AML_MUTEX_NOT_ACQUIRED",
	"AE_AML_INVALID_RESOURCE_TYPE",
	"AE_AML_INVALID_INDEX",
	"AE_AML_REGISTER_LIMIT",
	"AE_AML_NO_WHILE",
	"AE_AML_ALIGNMENT",
	"AE_AML_NO_RESOURCE_END_TAG",
	"AE_AML_BAD_RESOURCE_VALUE",
	"AE_AML_CIRCULAR_REFERENCE",
	"AE_AML_BAD_RESOURCE_LENGTH",
	"AE_AML_ILLEGAL_ADDRESS",
	"AE_AML_INFINITE_LOOP"
};

char const *acpi_gbl_exception_names_ctrl[] = {
	NULL,
	"AE_CTRL_RETURN_VALUE",
	"AE_CTRL_PENDING",
	"AE_CTRL_TERMINATE",
	"AE_CTRL_TRUE",
	"AE_CTRL_FALSE",
	"AE_CTRL_DEPTH",
	"AE_CTRL_END",
	"AE_CTRL_TRANSFER",
	"AE_CTRL_BREAK",
	"AE_CTRL_CONTINUE",
	"AE_CTRL_SKIP",
	"AE_CTRL_PARSE_CONTINUE",
	"AE_CTRL_PARSE_PENDING"
};

#endif				/* EXCEPTION_TABLE */

#endif				/* __ACEXCEP_H__ */

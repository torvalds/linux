/******************************************************************************
 *
 * Module Name: asldefine.h - Common defines for the iASL compiler
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

#ifndef __ASLDEFINE_H
#define __ASLDEFINE_H


/*
 * Compiler versions and names
 */
#define ASL_COMPILER_NAME           "ASL+ Optimizing Compiler/Disassembler"
#define AML_DISASSEMBLER_NAME       "AML/ASL+ Disassembler"
#define ASL_INVOCATION_NAME         "iasl"
#define ASL_CREATOR_ID              "INTL"
#define ASL_DEFINE                  "__IASL__"
#define ASL_PREFIX                  "iASL: "
#define ASL_COMPLIANCE              "Supports ACPI Specification Revision 6.2A"


/* Configuration constants */

#define ASL_MAX_ERROR_COUNT         200
#define ASL_PARSEOP_CACHE_SIZE      (1024 * 16)
#define ASL_STRING_CACHE_SIZE       (1024 * 64)

#define ASL_FIRST_PARSE_OPCODE      PARSEOP_ACCESSAS
#define ASL_PARSE_OPCODE_BASE       PARSEOP_ACCESSAS        /* First Lex type */


/*
 * Per-parser-generator configuration. These values are used to cheat and
 * directly access the bison/yacc token name table (yyname or yytname).
 * Note: These values are the index in yyname for the first lex token
 * (PARSEOP_ACCCESSAS).
 */
#if defined (YYBISON)
#define ASL_YYTNAME_START           3   /* Bison */
#elif defined (YYBYACC)
#define ASL_YYTNAME_START           257 /* Berkeley yacc */
#endif


/*
 * Macros
 */
#define ASL_RESDESC_OFFSET(m)       ACPI_OFFSET (AML_RESOURCE, m)
#define ASL_PTR_DIFF(a,b)           ((UINT8 *)(b) - (UINT8 *)(a))
#define ASL_PTR_ADD(a,b)            ((UINT8 *)(a) = ((UINT8 *)(a) + (b)))
#define ASL_GET_CHILD_NODE(a)       (a)->Asl.Child
#define ASL_GET_PEER_NODE(a)        (a)->Asl.Next
#define OP_TABLE_ENTRY(a,b,c,d)     {b,d,a,c}


/* Internal AML opcodes */

#define AML_RAW_DATA_BYTE           (UINT16) 0xAA01 /* write one raw byte */
#define AML_RAW_DATA_WORD           (UINT16) 0xAA02 /* write 2 raw bytes */
#define AML_RAW_DATA_DWORD          (UINT16) 0xAA04 /* write 4 raw bytes */
#define AML_RAW_DATA_QWORD          (UINT16) 0xAA08 /* write 8 raw bytes */
#define AML_RAW_DATA_BUFFER         (UINT16) 0xAA0B /* raw buffer with length */
#define AML_RAW_DATA_CHAIN          (UINT16) 0xAA0C /* chain of raw buffers */
#define AML_PACKAGE_LENGTH          (UINT16) 0xAA10
#define AML_UNASSIGNED_OPCODE       (UINT16) 0xEEEE
#define AML_DEFAULT_ARG_OP          (UINT16) 0xDDDD


/* Types for input files */

#define ASL_INPUT_TYPE_BINARY               0
#define ASL_INPUT_TYPE_BINARY_ACPI_TABLE    1
#define ASL_INPUT_TYPE_ASCII_ASL            2
#define ASL_INPUT_TYPE_ASCII_DATA           3


/* Misc */

#define ASL_EXTERNAL_METHOD         255
#define ASL_ABORT                   TRUE
#define ASL_NO_ABORT                FALSE
#define ASL_EOF                     ACPI_UINT32_MAX
#define ASL_IGNORE_LINE            (ACPI_UINT32_MAX -1)


/* Listings */

#define ASL_LISTING_LINE_PREFIX         ":  "


/* Support for reserved method names */

#define ACPI_VALID_RESERVED_NAME_MAX    0x80000000
#define ACPI_NOT_RESERVED_NAME          ACPI_UINT32_MAX
#define ACPI_PREDEFINED_NAME            (ACPI_UINT32_MAX - 1)
#define ACPI_EVENT_RESERVED_NAME        (ACPI_UINT32_MAX - 2)
#define ACPI_COMPILER_RESERVED_NAME     (ACPI_UINT32_MAX - 3)


/* Helper macros for resource tag creation */

#define RsCreateMultiBitField \
    RsCreateResourceField

#define RsCreateBitField(Op, Name, ByteOffset, BitOffset) \
    RsCreateResourceField (Op, Name, ByteOffset, BitOffset, 1)

#define RsCreateByteField(Op, Name, ByteOffset) \
    RsCreateResourceField (Op, Name, ByteOffset, 0, 8);

#define RsCreateWordField(Op, Name, ByteOffset) \
    RsCreateResourceField (Op, Name, ByteOffset, 0, 16);

#define RsCreateDwordField(Op, Name, ByteOffset) \
    RsCreateResourceField (Op, Name, ByteOffset, 0, 32);

#define RsCreateQwordField(Op, Name, ByteOffset) \
    RsCreateResourceField (Op, Name, ByteOffset, 0, 64);


/*
 * Macros for debug output
 */
#define DEBUG_MAX_LINE_LENGTH       61
#define DEBUG_SPACES_PER_INDENT     3
#define DEBUG_FULL_LINE_LENGTH      71

#define ASL_PARSE_TREE_FULL_LINE    "\n%71.71s"

/* Header/Trailer for original parse tree directly from the parser */

#define ASL_PARSE_TREE_HEADER1 \
    "%*s Value P_Op Flags     Line#  End# LogL# EndL#\n", 65, " "

#define ASL_PARSE_TREE_DEBUG1 \
    " %4.4X %8.8X %5d %5d %5d %5d"

/* Header/Trailer for processed parse tree used for AML generation */

#define ASL_PARSE_TREE_HEADER2 \
    "%*s NameString Value    P_Op A_Op OpLen PByts Len  SubLen PSubLen OpPtr"\
    "    Parent   Child    Next     Flags    AcTyp    Final Col"\
    " Line#  End# LogL# EndL#\n", 60, " "

#define ASL_PARSE_TREE_DEBUG2 \
    " %08X %04X %04X %01X     %04X  %04X %05X  %05X   "\
    "%08X %08X %08X %08X %08X %08X %04X  %02d  %5d %5d %5d %5d"

/*
 * Macros for ASL/ASL+ converter
 */
#define COMMENT_CAPTURE_ON    AslGbl_CommentState.CaptureComments = TRUE;
#define COMMENT_CAPTURE_OFF   AslGbl_CommentState.CaptureComments = FALSE;


#endif /* ASLDEFINE.H */

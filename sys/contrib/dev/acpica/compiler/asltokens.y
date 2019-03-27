NoEcho('
/******************************************************************************
 *
 * Module Name: asltokens.y - Bison/Yacc token types
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

')

/******************************************************************************
 *
 * Token types: These are returned by the lexer
 *
 * NOTE: This list MUST match the AslKeywordMapping table found
 *       in aslmap.c EXACTLY!  Double check any changes!
 *
 *****************************************************************************/

/*
 * Most tokens are defined to return <i>, which is a UINT64.
 *
 * These tokens return <s>, a pointer to the associated lexed string:
 *
 *  PARSEOP_NAMESEG
 *  PARSEOP_NAMESTRING
 *  PARSEOP_STRING_LITERAL
 *  PARSEOP_STRUCTURE_NAMESTRING
 */
%token <i> PARSEOP_ACCESSAS
%token <i> PARSEOP_ACCESSATTRIB_BLOCK
%token <i> PARSEOP_ACCESSATTRIB_BLOCK_CALL
%token <i> PARSEOP_ACCESSATTRIB_BYTE
%token <i> PARSEOP_ACCESSATTRIB_BYTES
%token <i> PARSEOP_ACCESSATTRIB_QUICK
%token <i> PARSEOP_ACCESSATTRIB_RAW_BYTES
%token <i> PARSEOP_ACCESSATTRIB_RAW_PROCESS
%token <i> PARSEOP_ACCESSATTRIB_SND_RCV
%token <i> PARSEOP_ACCESSATTRIB_WORD
%token <i> PARSEOP_ACCESSATTRIB_WORD_CALL
%token <i> PARSEOP_ACCESSTYPE_ANY
%token <i> PARSEOP_ACCESSTYPE_BUF
%token <i> PARSEOP_ACCESSTYPE_BYTE
%token <i> PARSEOP_ACCESSTYPE_DWORD
%token <i> PARSEOP_ACCESSTYPE_QWORD
%token <i> PARSEOP_ACCESSTYPE_WORD
%token <i> PARSEOP_ACQUIRE
%token <i> PARSEOP_ADD
%token <i> PARSEOP_ADDRESSINGMODE_7BIT
%token <i> PARSEOP_ADDRESSINGMODE_10BIT
%token <i> PARSEOP_ADDRESSTYPE_ACPI
%token <i> PARSEOP_ADDRESSTYPE_MEMORY
%token <i> PARSEOP_ADDRESSTYPE_NVS
%token <i> PARSEOP_ADDRESSTYPE_RESERVED
%token <i> PARSEOP_ALIAS
%token <i> PARSEOP_AND
%token <i> PARSEOP_ARG0
%token <i> PARSEOP_ARG1
%token <i> PARSEOP_ARG2
%token <i> PARSEOP_ARG3
%token <i> PARSEOP_ARG4
%token <i> PARSEOP_ARG5
%token <i> PARSEOP_ARG6
%token <i> PARSEOP_BANKFIELD
%token <i> PARSEOP_BITSPERBYTE_EIGHT
%token <i> PARSEOP_BITSPERBYTE_FIVE
%token <i> PARSEOP_BITSPERBYTE_NINE
%token <i> PARSEOP_BITSPERBYTE_SEVEN
%token <i> PARSEOP_BITSPERBYTE_SIX
%token <i> PARSEOP_BREAK
%token <i> PARSEOP_BREAKPOINT
%token <i> PARSEOP_BUFFER
%token <i> PARSEOP_BUSMASTERTYPE_MASTER
%token <i> PARSEOP_BUSMASTERTYPE_NOTMASTER
%token <i> PARSEOP_BYTECONST
%token <i> PARSEOP_CASE
%token <i> PARSEOP_CLOCKPHASE_FIRST
%token <i> PARSEOP_CLOCKPHASE_SECOND
%token <i> PARSEOP_CLOCKPOLARITY_HIGH
%token <i> PARSEOP_CLOCKPOLARITY_LOW
%token <i> PARSEOP_CONCATENATE
%token <i> PARSEOP_CONCATENATERESTEMPLATE
%token <i> PARSEOP_CONDREFOF
%token <i> PARSEOP_CONNECTION
%token <i> PARSEOP_CONTINUE
%token <i> PARSEOP_COPYOBJECT
%token <i> PARSEOP_CREATEBITFIELD
%token <i> PARSEOP_CREATEBYTEFIELD
%token <i> PARSEOP_CREATEDWORDFIELD
%token <i> PARSEOP_CREATEFIELD
%token <i> PARSEOP_CREATEQWORDFIELD
%token <i> PARSEOP_CREATEWORDFIELD
%token <i> PARSEOP_DATABUFFER
%token <i> PARSEOP_DATATABLEREGION
%token <i> PARSEOP_DEBUG
%token <i> PARSEOP_DECODETYPE_POS
%token <i> PARSEOP_DECODETYPE_SUB
%token <i> PARSEOP_DECREMENT
%token <i> PARSEOP_DEFAULT
%token <i> PARSEOP_DEFAULT_ARG
%token <i> PARSEOP_DEFINITION_BLOCK
%token <i> PARSEOP_DEREFOF
%token <i> PARSEOP_DEVICE
%token <i> PARSEOP_DEVICEPOLARITY_HIGH
%token <i> PARSEOP_DEVICEPOLARITY_LOW
%token <i> PARSEOP_DIVIDE
%token <i> PARSEOP_DMA
%token <i> PARSEOP_DMATYPE_A
%token <i> PARSEOP_DMATYPE_COMPATIBILITY
%token <i> PARSEOP_DMATYPE_B
%token <i> PARSEOP_DMATYPE_F
%token <i> PARSEOP_DWORDCONST
%token <i> PARSEOP_DWORDIO
%token <i> PARSEOP_DWORDMEMORY
%token <i> PARSEOP_DWORDSPACE
%token <i> PARSEOP_EISAID
%token <i> PARSEOP_ELSE
%token <i> PARSEOP_ELSEIF
%token <i> PARSEOP_ENDDEPENDENTFN
%token <i> PARSEOP_ENDIAN_BIG
%token <i> PARSEOP_ENDIAN_LITTLE
%token <i> PARSEOP_ENDTAG
%token <i> PARSEOP_ERRORNODE
%token <i> PARSEOP_EVENT
%token <i> PARSEOP_EXTENDEDIO
%token <i> PARSEOP_EXTENDEDMEMORY
%token <i> PARSEOP_EXTENDEDSPACE
%token <i> PARSEOP_EXTERNAL
%token <i> PARSEOP_FATAL
%token <i> PARSEOP_FIELD
%token <i> PARSEOP_FINDSETLEFTBIT
%token <i> PARSEOP_FINDSETRIGHTBIT
%token <i> PARSEOP_FIXEDDMA
%token <i> PARSEOP_FIXEDIO
%token <i> PARSEOP_FLOWCONTROL_HW
%token <i> PARSEOP_FLOWCONTROL_NONE
%token <i> PARSEOP_FLOWCONTROL_SW
%token <i> PARSEOP_FROMBCD
%token <i> PARSEOP_FUNCTION
%token <i> PARSEOP_GPIO_INT
%token <i> PARSEOP_GPIO_IO
%token <i> PARSEOP_I2C_SERIALBUS
%token <i> PARSEOP_I2C_SERIALBUS_V2
%token <i> PARSEOP_IF
%token <i> PARSEOP_INCLUDE
%token <i> PARSEOP_INCLUDE_END
%token <i> PARSEOP_INCREMENT
%token <i> PARSEOP_INDEX
%token <i> PARSEOP_INDEXFIELD
%token <i> PARSEOP_INTEGER
%token <i> PARSEOP_INTERRUPT
%token <i> PARSEOP_INTLEVEL_ACTIVEBOTH
%token <i> PARSEOP_INTLEVEL_ACTIVEHIGH
%token <i> PARSEOP_INTLEVEL_ACTIVELOW
%token <i> PARSEOP_INTTYPE_EDGE
%token <i> PARSEOP_INTTYPE_LEVEL
%token <i> PARSEOP_IO
%token <i> PARSEOP_IODECODETYPE_10
%token <i> PARSEOP_IODECODETYPE_16
%token <i> PARSEOP_IORESTRICT_IN
%token <i> PARSEOP_IORESTRICT_NONE
%token <i> PARSEOP_IORESTRICT_OUT
%token <i> PARSEOP_IORESTRICT_PRESERVE
%token <i> PARSEOP_IRQ
%token <i> PARSEOP_IRQNOFLAGS
%token <i> PARSEOP_LAND
%token <i> PARSEOP_LEQUAL
%token <i> PARSEOP_LGREATER
%token <i> PARSEOP_LGREATEREQUAL
%token <i> PARSEOP_LLESS
%token <i> PARSEOP_LLESSEQUAL
%token <i> PARSEOP_LNOT
%token <i> PARSEOP_LNOTEQUAL
%token <i> PARSEOP_LOAD
%token <i> PARSEOP_LOADTABLE
%token <i> PARSEOP_LOCAL0
%token <i> PARSEOP_LOCAL1
%token <i> PARSEOP_LOCAL2
%token <i> PARSEOP_LOCAL3
%token <i> PARSEOP_LOCAL4
%token <i> PARSEOP_LOCAL5
%token <i> PARSEOP_LOCAL6
%token <i> PARSEOP_LOCAL7
%token <i> PARSEOP_LOCKRULE_LOCK
%token <i> PARSEOP_LOCKRULE_NOLOCK
%token <i> PARSEOP_LOR
%token <i> PARSEOP_MATCH
%token <i> PARSEOP_MATCHTYPE_MEQ
%token <i> PARSEOP_MATCHTYPE_MGE
%token <i> PARSEOP_MATCHTYPE_MGT
%token <i> PARSEOP_MATCHTYPE_MLE
%token <i> PARSEOP_MATCHTYPE_MLT
%token <i> PARSEOP_MATCHTYPE_MTR
%token <i> PARSEOP_MAXTYPE_FIXED
%token <i> PARSEOP_MAXTYPE_NOTFIXED
%token <i> PARSEOP_MEMORY24
%token <i> PARSEOP_MEMORY32
%token <i> PARSEOP_MEMORY32FIXED
%token <i> PARSEOP_MEMTYPE_CACHEABLE
%token <i> PARSEOP_MEMTYPE_NONCACHEABLE
%token <i> PARSEOP_MEMTYPE_PREFETCHABLE
%token <i> PARSEOP_MEMTYPE_WRITECOMBINING
%token <i> PARSEOP_METHOD
%token <i> PARSEOP_METHODCALL
%token <i> PARSEOP_MID
%token <i> PARSEOP_MINTYPE_FIXED
%token <i> PARSEOP_MINTYPE_NOTFIXED
%token <i> PARSEOP_MOD
%token <i> PARSEOP_MULTIPLY
%token <i> PARSEOP_MUTEX
%token <i> PARSEOP_NAME
%token <s> PARSEOP_NAMESEG
%token <s> PARSEOP_NAMESTRING
%token <i> PARSEOP_NAND
%token <i> PARSEOP_NOOP
%token <i> PARSEOP_NOR
%token <i> PARSEOP_NOT
%token <i> PARSEOP_NOTIFY
%token <i> PARSEOP_OBJECTTYPE
%token <i> PARSEOP_OBJECTTYPE_BFF
%token <i> PARSEOP_OBJECTTYPE_BUF
%token <i> PARSEOP_OBJECTTYPE_DDB
%token <i> PARSEOP_OBJECTTYPE_DEV
%token <i> PARSEOP_OBJECTTYPE_EVT
%token <i> PARSEOP_OBJECTTYPE_FLD
%token <i> PARSEOP_OBJECTTYPE_INT
%token <i> PARSEOP_OBJECTTYPE_MTH
%token <i> PARSEOP_OBJECTTYPE_MTX
%token <i> PARSEOP_OBJECTTYPE_OPR
%token <i> PARSEOP_OBJECTTYPE_PKG
%token <i> PARSEOP_OBJECTTYPE_POW
%token <i> PARSEOP_OBJECTTYPE_PRO
%token <i> PARSEOP_OBJECTTYPE_STR
%token <i> PARSEOP_OBJECTTYPE_THZ
%token <i> PARSEOP_OBJECTTYPE_UNK
%token <i> PARSEOP_OFFSET
%token <i> PARSEOP_ONE
%token <i> PARSEOP_ONES
%token <i> PARSEOP_OPERATIONREGION
%token <i> PARSEOP_OR
%token <i> PARSEOP_PACKAGE
%token <i> PARSEOP_PACKAGE_LENGTH
%token <i> PARSEOP_PARITYTYPE_EVEN
%token <i> PARSEOP_PARITYTYPE_MARK
%token <i> PARSEOP_PARITYTYPE_NONE
%token <i> PARSEOP_PARITYTYPE_ODD
%token <i> PARSEOP_PARITYTYPE_SPACE
%token <i> PARSEOP_PINCONFIG
%token <i> PARSEOP_PINFUNCTION
%token <i> PARSEOP_PINGROUP
%token <i> PARSEOP_PINGROUPCONFIG
%token <i> PARSEOP_PINGROUPFUNCTION
%token <i> PARSEOP_PIN_NOPULL
%token <i> PARSEOP_PIN_PULLDEFAULT
%token <i> PARSEOP_PIN_PULLDOWN
%token <i> PARSEOP_PIN_PULLUP
%token <i> PARSEOP_POWERRESOURCE
%token <i> PARSEOP_PROCESSOR
%token <i> PARSEOP_QWORDCONST
%token <i> PARSEOP_QWORDIO
%token <i> PARSEOP_QWORDMEMORY
%token <i> PARSEOP_QWORDSPACE
%token <i> PARSEOP_RANGETYPE_ENTIRE
%token <i> PARSEOP_RANGETYPE_ISAONLY
%token <i> PARSEOP_RANGETYPE_NONISAONLY
%token <i> PARSEOP_RAW_DATA
%token <i> PARSEOP_READWRITETYPE_BOTH
%token <i> PARSEOP_READWRITETYPE_READONLY
%token <i> PARSEOP_REFOF
%token <i> PARSEOP_REGIONSPACE_CMOS
%token <i> PARSEOP_REGIONSPACE_EC
%token <i> PARSEOP_REGIONSPACE_FFIXEDHW
%token <i> PARSEOP_REGIONSPACE_GPIO
%token <i> PARSEOP_REGIONSPACE_GSBUS
%token <i> PARSEOP_REGIONSPACE_IO
%token <i> PARSEOP_REGIONSPACE_IPMI
%token <i> PARSEOP_REGIONSPACE_MEM
%token <i> PARSEOP_REGIONSPACE_PCC
%token <i> PARSEOP_REGIONSPACE_PCI
%token <i> PARSEOP_REGIONSPACE_PCIBAR
%token <i> PARSEOP_REGIONSPACE_SMBUS
%token <i> PARSEOP_REGISTER
%token <i> PARSEOP_RELEASE
%token <i> PARSEOP_RESERVED_BYTES
%token <i> PARSEOP_RESET
%token <i> PARSEOP_RESOURCETEMPLATE
%token <i> PARSEOP_RESOURCETYPE_CONSUMER
%token <i> PARSEOP_RESOURCETYPE_PRODUCER
%token <i> PARSEOP_RETURN
%token <i> PARSEOP_REVISION
%token <i> PARSEOP_SCOPE
%token <i> PARSEOP_SERIALIZERULE_NOTSERIAL
%token <i> PARSEOP_SERIALIZERULE_SERIAL
%token <i> PARSEOP_SHARETYPE_EXCLUSIVE
%token <i> PARSEOP_SHARETYPE_EXCLUSIVEWAKE
%token <i> PARSEOP_SHARETYPE_SHARED
%token <i> PARSEOP_SHARETYPE_SHAREDWAKE
%token <i> PARSEOP_SHIFTLEFT
%token <i> PARSEOP_SHIFTRIGHT
%token <i> PARSEOP_SIGNAL
%token <i> PARSEOP_SIZEOF
%token <i> PARSEOP_SLAVEMODE_CONTROLLERINIT
%token <i> PARSEOP_SLAVEMODE_DEVICEINIT
%token <i> PARSEOP_SLEEP
%token <i> PARSEOP_SPI_SERIALBUS
%token <i> PARSEOP_SPI_SERIALBUS_V2
%token <i> PARSEOP_STALL
%token <i> PARSEOP_STARTDEPENDENTFN
%token <i> PARSEOP_STARTDEPENDENTFN_NOPRI
%token <i> PARSEOP_STOPBITS_ONE
%token <i> PARSEOP_STOPBITS_ONEPLUSHALF
%token <i> PARSEOP_STOPBITS_TWO
%token <i> PARSEOP_STOPBITS_ZERO
%token <i> PARSEOP_STORE
%token <s> PARSEOP_STRING_LITERAL
%token <i> PARSEOP_SUBTRACT
%token <i> PARSEOP_SWITCH
%token <i> PARSEOP_THERMALZONE
%token <i> PARSEOP_TIMER
%token <i> PARSEOP_TOBCD
%token <i> PARSEOP_TOBUFFER
%token <i> PARSEOP_TODECIMALSTRING
%token <i> PARSEOP_TOHEXSTRING
%token <i> PARSEOP_TOINTEGER
%token <i> PARSEOP_TOSTRING
%token <i> PARSEOP_TOUUID
%token <i> PARSEOP_TRANSLATIONTYPE_DENSE
%token <i> PARSEOP_TRANSLATIONTYPE_SPARSE
%token <i> PARSEOP_TYPE_STATIC
%token <i> PARSEOP_TYPE_TRANSLATION
%token <i> PARSEOP_UART_SERIALBUS
%token <i> PARSEOP_UART_SERIALBUS_V2
%token <i> PARSEOP_UNICODE
%token <i> PARSEOP_UNLOAD
%token <i> PARSEOP_UPDATERULE_ONES
%token <i> PARSEOP_UPDATERULE_PRESERVE
%token <i> PARSEOP_UPDATERULE_ZEROS
%token <i> PARSEOP_VAR_PACKAGE
%token <i> PARSEOP_VENDORLONG
%token <i> PARSEOP_VENDORSHORT
%token <i> PARSEOP_WAIT
%token <i> PARSEOP_WHILE
%token <i> PARSEOP_WIREMODE_FOUR
%token <i> PARSEOP_WIREMODE_THREE
%token <i> PARSEOP_WORDBUSNUMBER
%token <i> PARSEOP_WORDCONST
%token <i> PARSEOP_WORDIO
%token <i> PARSEOP_WORDSPACE
%token <i> PARSEOP_XFERSIZE_8
%token <i> PARSEOP_XFERSIZE_16
%token <i> PARSEOP_XFERSIZE_32
%token <i> PARSEOP_XFERSIZE_64
%token <i> PARSEOP_XFERSIZE_128
%token <i> PARSEOP_XFERSIZE_256
%token <i> PARSEOP_XFERTYPE_8
%token <i> PARSEOP_XFERTYPE_8_16
%token <i> PARSEOP_XFERTYPE_16
%token <i> PARSEOP_XOR
%token <i> PARSEOP_ZERO

/* ToPld macro */

%token <i> PARSEOP_TOPLD
%token <i> PARSEOP_PLD_REVISION
%token <i> PARSEOP_PLD_IGNORECOLOR
%token <i> PARSEOP_PLD_RED
%token <i> PARSEOP_PLD_GREEN
%token <i> PARSEOP_PLD_BLUE
%token <i> PARSEOP_PLD_WIDTH
%token <i> PARSEOP_PLD_HEIGHT
%token <i> PARSEOP_PLD_USERVISIBLE
%token <i> PARSEOP_PLD_DOCK
%token <i> PARSEOP_PLD_LID
%token <i> PARSEOP_PLD_PANEL
%token <i> PARSEOP_PLD_VERTICALPOSITION
%token <i> PARSEOP_PLD_HORIZONTALPOSITION
%token <i> PARSEOP_PLD_SHAPE
%token <i> PARSEOP_PLD_GROUPORIENTATION
%token <i> PARSEOP_PLD_GROUPTOKEN
%token <i> PARSEOP_PLD_GROUPPOSITION
%token <i> PARSEOP_PLD_BAY
%token <i> PARSEOP_PLD_EJECTABLE
%token <i> PARSEOP_PLD_EJECTREQUIRED
%token <i> PARSEOP_PLD_CABINETNUMBER
%token <i> PARSEOP_PLD_CARDCAGENUMBER
%token <i> PARSEOP_PLD_REFERENCE
%token <i> PARSEOP_PLD_ROTATION
%token <i> PARSEOP_PLD_ORDER
%token <i> PARSEOP_PLD_RESERVED
%token <i> PARSEOP_PLD_VERTICALOFFSET
%token <i> PARSEOP_PLD_HORIZONTALOFFSET

/*
 * C-style expression parser. These must appear after all of the
 * standard ASL operators and keywords.
 *
 * Note: The order of these tokens implements the precedence rules
 * (low precedence to high). See aslrules.y for an exhaustive list.
 */
%right <i> PARSEOP_EXP_EQUALS
           PARSEOP_EXP_ADD_EQ
           PARSEOP_EXP_SUB_EQ
           PARSEOP_EXP_MUL_EQ
           PARSEOP_EXP_DIV_EQ
           PARSEOP_EXP_MOD_EQ
           PARSEOP_EXP_SHL_EQ
           PARSEOP_EXP_SHR_EQ
           PARSEOP_EXP_AND_EQ
           PARSEOP_EXP_XOR_EQ
           PARSEOP_EXP_OR_EQ

%left <i>  PARSEOP_EXP_LOGICAL_OR
%left <i>  PARSEOP_EXP_LOGICAL_AND
%left <i>  PARSEOP_EXP_OR
%left <i>  PARSEOP_EXP_XOR
%left <i>  PARSEOP_EXP_AND
%left <i>  PARSEOP_EXP_EQUAL
           PARSEOP_EXP_NOT_EQUAL
%left <i>  PARSEOP_EXP_GREATER
           PARSEOP_EXP_LESS
           PARSEOP_EXP_GREATER_EQUAL
           PARSEOP_EXP_LESS_EQUAL
%left <i>  PARSEOP_EXP_SHIFT_RIGHT
           PARSEOP_EXP_SHIFT_LEFT
%left <i>  PARSEOP_EXP_ADD
           PARSEOP_EXP_SUBTRACT
%left <i>  PARSEOP_EXP_MULTIPLY
           PARSEOP_EXP_DIVIDE
           PARSEOP_EXP_MODULO

%right <i> PARSEOP_EXP_NOT
           PARSEOP_EXP_LOGICAL_NOT

%left <i>  PARSEOP_EXP_INCREMENT
           PARSEOP_EXP_DECREMENT

%left <i>  PARSEOP_OPEN_PAREN
           PARSEOP_CLOSE_PAREN

/* Brackets for Index() support */

%left <i>  PARSEOP_EXP_INDEX_LEFT
%right <i> PARSEOP_EXP_INDEX_RIGHT

/* Macros */

%token <i> PARSEOP_PRINTF
%token <i> PARSEOP_FPRINTF
%token <i> PARSEOP_FOR

/* Structures */

%token <i> PARSEOP_STRUCTURE
%token <s> PARSEOP_STRUCTURE_NAMESTRING
%token <i> PARSEOP_STRUCTURE_TAG
%token <i> PARSEOP_STRUCTURE_ELEMENT
%token <i> PARSEOP_STRUCTURE_INSTANCE
%token <i> PARSEOP_STRUCTURE_REFERENCE
%token <i> PARSEOP_STRUCTURE_POINTER

/* Top level */

%token <i> PARSEOP_ASL_CODE


/*******************************************************************************
 *
 * Tokens below are not in the aslmap.c file
 *
 ******************************************************************************/


/* Tokens below this are not in the aslmap.c file */

/* Specific parentheses tokens are not used at this time */
           /* PARSEOP_EXP_PAREN_OPEN */
           /* PARSEOP_EXP_PAREN_CLOSE */

/* ASL+ variable creation */

%token <i> PARSEOP_INTEGER_TYPE
%token <i> PARSEOP_STRING_TYPE
%token <i> PARSEOP_BUFFER_TYPE
%token <i> PARSEOP_PACKAGE_TYPE
%token <i> PARSEOP_REFERENCE_TYPE


/*
 * Special functions. These should probably stay at the end of this
 * table.
 */
%token <i> PARSEOP___DATE__
%token <i> PARSEOP___FILE__
%token <i> PARSEOP___LINE__
%token <i> PARSEOP___PATH__
%token <i> PARSEOP___METHOD__

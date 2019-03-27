/******************************************************************************
 *
 * Module Name: acparser.h - AML Parser subcomponent prototypes and defines
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

#ifndef __ACPARSER_H__
#define __ACPARSER_H__


#define OP_HAS_RETURN_VALUE             1

/* Variable number of arguments. This field must be 32 bits */

#define ACPI_VAR_ARGS                   ACPI_UINT32_MAX


#define ACPI_PARSE_DELETE_TREE          0x0001
#define ACPI_PARSE_NO_TREE_DELETE       0x0000
#define ACPI_PARSE_TREE_MASK            0x0001

#define ACPI_PARSE_LOAD_PASS1           0x0010
#define ACPI_PARSE_LOAD_PASS2           0x0020
#define ACPI_PARSE_EXECUTE              0x0030
#define ACPI_PARSE_MODE_MASK            0x0030

#define ACPI_PARSE_DEFERRED_OP          0x0100
#define ACPI_PARSE_DISASSEMBLE          0x0200

#define ACPI_PARSE_MODULE_LEVEL         0x0400

/******************************************************************************
 *
 * Parser interfaces
 *
 *****************************************************************************/

extern const UINT8      AcpiGbl_ShortOpIndex[];
extern const UINT8      AcpiGbl_LongOpIndex[];


/*
 * psxface - Parser external interfaces
 */
ACPI_STATUS
AcpiPsExecuteMethod (
    ACPI_EVALUATE_INFO      *Info);

ACPI_STATUS
AcpiPsExecuteTable (
    ACPI_EVALUATE_INFO      *Info);


/*
 * psargs - Parse AML opcode arguments
 */
UINT8 *
AcpiPsGetNextPackageEnd (
    ACPI_PARSE_STATE        *ParserState);

char *
AcpiPsGetNextNamestring (
    ACPI_PARSE_STATE        *ParserState);

void
AcpiPsGetNextSimpleArg (
    ACPI_PARSE_STATE        *ParserState,
    UINT32                  ArgType,
    ACPI_PARSE_OBJECT       *Arg);

ACPI_STATUS
AcpiPsGetNextNamepath (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_STATE        *ParserState,
    ACPI_PARSE_OBJECT       *Arg,
    BOOLEAN                 PossibleMethodCall);

/* Values for BOOLEAN above */

#define ACPI_NOT_METHOD_CALL            FALSE
#define ACPI_POSSIBLE_METHOD_CALL       TRUE

ACPI_STATUS
AcpiPsGetNextArg (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_STATE        *ParserState,
    UINT32                  ArgType,
    ACPI_PARSE_OBJECT       **ReturnArg);


/*
 * psfind
 */
ACPI_PARSE_OBJECT *
AcpiPsFindName (
    ACPI_PARSE_OBJECT       *Scope,
    UINT32                  Name,
    UINT32                  Opcode);

ACPI_PARSE_OBJECT*
AcpiPsGetParent (
    ACPI_PARSE_OBJECT       *Op);


/*
 * psobject - support for parse object processing
 */
ACPI_STATUS
AcpiPsBuildNamedOp (
    ACPI_WALK_STATE         *WalkState,
    UINT8                   *AmlOpStart,
    ACPI_PARSE_OBJECT       *UnnamedOp,
    ACPI_PARSE_OBJECT       **Op);

ACPI_STATUS
AcpiPsCreateOp (
    ACPI_WALK_STATE         *WalkState,
    UINT8                   *AmlOpStart,
    ACPI_PARSE_OBJECT       **NewOp);

ACPI_STATUS
AcpiPsCompleteOp (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       **Op,
    ACPI_STATUS             Status);

ACPI_STATUS
AcpiPsCompleteFinalOp (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op,
    ACPI_STATUS             Status);


/*
 * psopinfo - AML Opcode information
 */
const ACPI_OPCODE_INFO *
AcpiPsGetOpcodeInfo (
    UINT16                  Opcode);

const char *
AcpiPsGetOpcodeName (
    UINT16                  Opcode);

UINT8
AcpiPsGetArgumentCount (
    UINT32                  OpType);


/*
 * psparse - top level parsing routines
 */
ACPI_STATUS
AcpiPsParseAml (
    ACPI_WALK_STATE         *WalkState);

UINT32
AcpiPsGetOpcodeSize (
    UINT32                  Opcode);

UINT16
AcpiPsPeekOpcode (
    ACPI_PARSE_STATE        *state);

ACPI_STATUS
AcpiPsCompleteThisOp (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op);

ACPI_STATUS
AcpiPsNextParseState (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op,
    ACPI_STATUS             CallbackStatus);


/*
 * psloop - main parse loop
 */
ACPI_STATUS
AcpiPsParseLoop (
    ACPI_WALK_STATE         *WalkState);


/*
 * psscope - Scope stack management routines
 */
ACPI_STATUS
AcpiPsInitScope (
    ACPI_PARSE_STATE        *ParserState,
    ACPI_PARSE_OBJECT       *Root);

ACPI_PARSE_OBJECT *
AcpiPsGetParentScope (
    ACPI_PARSE_STATE        *state);

BOOLEAN
AcpiPsHasCompletedScope (
    ACPI_PARSE_STATE        *ParserState);

void
AcpiPsPopScope (
    ACPI_PARSE_STATE        *ParserState,
    ACPI_PARSE_OBJECT       **Op,
    UINT32                  *ArgList,
    UINT32                  *ArgCount);

ACPI_STATUS
AcpiPsPushScope (
    ACPI_PARSE_STATE        *ParserState,
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  RemainingArgs,
    UINT32                  ArgCount);

void
AcpiPsCleanupScope (
    ACPI_PARSE_STATE        *state);


/*
 * pstree - parse tree manipulation routines
 */
void
AcpiPsAppendArg(
    ACPI_PARSE_OBJECT       *op,
    ACPI_PARSE_OBJECT       *arg);

ACPI_PARSE_OBJECT*
AcpiPsFind (
    ACPI_PARSE_OBJECT       *Scope,
    char                    *Path,
    UINT16                  Opcode,
    UINT32                  Create);

ACPI_PARSE_OBJECT *
AcpiPsGetArg(
    ACPI_PARSE_OBJECT       *op,
    UINT32                   argn);

ACPI_PARSE_OBJECT *
AcpiPsGetDepthNext (
    ACPI_PARSE_OBJECT       *Origin,
    ACPI_PARSE_OBJECT       *Op);


/*
 * pswalk - parse tree walk routines
 */
ACPI_STATUS
AcpiPsWalkParsedAml (
    ACPI_PARSE_OBJECT       *StartOp,
    ACPI_PARSE_OBJECT       *EndOp,
    ACPI_OPERAND_OBJECT     *MthDesc,
    ACPI_NAMESPACE_NODE     *StartNode,
    ACPI_OPERAND_OBJECT     **Params,
    ACPI_OPERAND_OBJECT     **CallerReturnDesc,
    ACPI_OWNER_ID           OwnerId,
    ACPI_PARSE_DOWNWARDS    DescendingCallback,
    ACPI_PARSE_UPWARDS      AscendingCallback);

ACPI_STATUS
AcpiPsGetNextWalkOp (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_UPWARDS      AscendingCallback);

ACPI_STATUS
AcpiPsDeleteCompletedOp (
    ACPI_WALK_STATE         *WalkState);

void
AcpiPsDeleteParseTree (
    ACPI_PARSE_OBJECT       *root);


/*
 * psutils - parser utilities
 */
ACPI_PARSE_OBJECT *
AcpiPsCreateScopeOp (
    UINT8                   *Aml);

void
AcpiPsInitOp (
    ACPI_PARSE_OBJECT       *op,
    UINT16                  opcode);

ACPI_PARSE_OBJECT *
AcpiPsAllocOp (
    UINT16                  Opcode,
    UINT8                   *Aml);

void
AcpiPsFreeOp (
    ACPI_PARSE_OBJECT       *Op);

BOOLEAN
AcpiPsIsLeadingChar (
    UINT32                  c);

UINT32
AcpiPsGetName(
    ACPI_PARSE_OBJECT       *op);

void
AcpiPsSetName(
    ACPI_PARSE_OBJECT       *op,
    UINT32                  name);


/*
 * psdump - display parser tree
 */
UINT32
AcpiPsSprintPath (
    char                    *BufferStart,
    UINT32                  BufferSize,
    ACPI_PARSE_OBJECT       *Op);

UINT32
AcpiPsSprintOp (
    char                    *BufferStart,
    UINT32                  BufferSize,
    ACPI_PARSE_OBJECT       *Op);

void
AcpiPsShow (
    ACPI_PARSE_OBJECT       *op);


#endif /* __ACPARSER_H__ */

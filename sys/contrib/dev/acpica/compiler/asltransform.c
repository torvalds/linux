/******************************************************************************
 *
 * Module Name: asltransform - Parse tree transforms
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

#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include "aslcompiler.y.h"

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("asltransform")

/* Local prototypes */

static void
TrTransformSubtree (
    ACPI_PARSE_OBJECT       *Op);

static char *
TrAmlGetNextTempName (
    ACPI_PARSE_OBJECT       *Op,
    UINT8                   *TempCount);

static void
TrAmlInitLineNumbers (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *Neighbor);

static void
TrAmlInitNode (
    ACPI_PARSE_OBJECT       *Op,
    UINT16                  ParseOpcode);

static void
TrAmlSetSubtreeParent (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *Parent);

static void
TrAmlInsertPeer (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *NewPeer);

static void
TrDoDefinitionBlock (
    ACPI_PARSE_OBJECT       *Op);

static void
TrDoSwitch (
    ACPI_PARSE_OBJECT       *StartNode);


/*******************************************************************************
 *
 * FUNCTION:    TrAmlGetNextTempName
 *
 * PARAMETERS:  Op              - Current parse op
 *              TempCount       - Current temporary counter. Was originally
 *                                per-module; Currently per method, could be
 *                                expanded to per-scope.
 *
 * RETURN:      A pointer to name (allocated here).
 *
 * DESCRIPTION: Generate an ACPI name of the form _T_x. These names are
 *              reserved for use by the ASL compiler. (_T_0 through _T_Z)
 *
 ******************************************************************************/

static char *
TrAmlGetNextTempName (
    ACPI_PARSE_OBJECT       *Op,
    UINT8                   *TempCount)
{
    char                    *TempName;


    if (*TempCount >= (10 + 26))  /* 0-35 valid: 0-9 and A-Z for TempName[3] */
    {
        /* Too many temps */

        AslError (ASL_ERROR, ASL_MSG_TOO_MANY_TEMPS, Op, NULL);
        return (NULL);
    }

    TempName = UtLocalCalloc (5);

    if (*TempCount < 10)    /* 0-9 */
    {
        TempName[3] = (char) (*TempCount + '0');
    }
    else                    /* 10-35: A-Z */
    {
        TempName[3] = (char) (*TempCount + ('A' - 10));
    }

    (*TempCount)++;

    /* First three characters are always "_T_" */

    TempName[0] = '_';
    TempName[1] = 'T';
    TempName[2] = '_';

    return (TempName);
}


/*******************************************************************************
 *
 * FUNCTION:    TrAmlInitLineNumbers
 *
 * PARAMETERS:  Op              - Op to be initialized
 *              Neighbor        - Op used for initialization values
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialized the various line numbers for a parse node.
 *
 ******************************************************************************/

static void
TrAmlInitLineNumbers (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *Neighbor)
{

    Op->Asl.EndLine           = Neighbor->Asl.EndLine;
    Op->Asl.EndLogicalLine    = Neighbor->Asl.EndLogicalLine;
    Op->Asl.LineNumber        = Neighbor->Asl.LineNumber;
    Op->Asl.LogicalByteOffset = Neighbor->Asl.LogicalByteOffset;
    Op->Asl.LogicalLineNumber = Neighbor->Asl.LogicalLineNumber;
}


/*******************************************************************************
 *
 * FUNCTION:    TrAmlInitNode
 *
 * PARAMETERS:  Op              - Op to be initialized
 *              ParseOpcode     - Opcode for this node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a node with the parse opcode and opcode name.
 *
 ******************************************************************************/

static void
TrAmlInitNode (
    ACPI_PARSE_OBJECT       *Op,
    UINT16                  ParseOpcode)
{

    Op->Asl.ParseOpcode = ParseOpcode;
    UtSetParseOpName (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    TrAmlSetSubtreeParent
 *
 * PARAMETERS:  Op              - First node in a list of peer nodes
 *              Parent          - Parent of the subtree
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set the parent for all peer nodes in a subtree
 *
 ******************************************************************************/

static void
TrAmlSetSubtreeParent (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *Parent)
{
    ACPI_PARSE_OBJECT       *Next;


    Next = Op;
    while (Next)
    {
        Next->Asl.Parent = Parent;
        Next = Next->Asl.Next;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    TrAmlInsertPeer
 *
 * PARAMETERS:  Op              - First node in a list of peer nodes
 *              NewPeer         - Peer node to insert
 *
 * RETURN:      None
 *
 * DESCRIPTION: Insert a new peer node into a list of peers.
 *
 ******************************************************************************/

static void
TrAmlInsertPeer (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_PARSE_OBJECT       *NewPeer)
{

    NewPeer->Asl.Next = Op->Asl.Next;
    Op->Asl.Next = NewPeer;
}


/*******************************************************************************
 *
 * FUNCTION:    TrAmlTransformWalkBegin
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      None
 *
 * DESCRIPTION: Parse tree walk to generate both the AML opcodes and the AML
 *              operands.
 *
 ******************************************************************************/

ACPI_STATUS
TrAmlTransformWalkBegin (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{

    TrTransformSubtree (Op);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    TrAmlTransformWalkEnd
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      None
 *
 * DESCRIPTION: Parse tree walk to generate both the AML opcodes and the AML
 *              operands.
 *
 ******************************************************************************/

ACPI_STATUS
TrAmlTransformWalkEnd (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{

    /* Save possible Externals list in the DefintionBlock Op */

    if (Op->Asl.ParseOpcode == PARSEOP_DEFINITION_BLOCK)
    {
        Op->Asl.Value.Arg = AslGbl_ExternalsListHead;
        AslGbl_ExternalsListHead = NULL;
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    TrTransformSubtree
 *
 * PARAMETERS:  Op        - The parent parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Prepare nodes to be output as AML data and operands. The more
 *              complex AML opcodes require processing of the child nodes
 *              (arguments/operands).
 *
 ******************************************************************************/

static void
TrTransformSubtree (
    ACPI_PARSE_OBJECT           *Op)
{
    ACPI_PARSE_OBJECT           *MethodOp;


    if (Op->Asl.AmlOpcode == AML_RAW_DATA_BYTE)
    {
        return;
    }

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_DEFINITION_BLOCK:

        TrDoDefinitionBlock (Op);
        break;

    case PARSEOP_SWITCH:

        TrDoSwitch (Op);
        break;

    case PARSEOP_METHOD:
        /*
         * TBD: Zero the tempname (_T_x) count. Probably shouldn't be a global,
         * however
         */
        AslGbl_TempCount = 0;
        break;

    case PARSEOP_EXTERNAL:

        ExDoExternal (Op);
        break;

    case PARSEOP___METHOD__:

        /* Transform to a string op containing the parent method name */

        Op->Asl.ParseOpcode = PARSEOP_STRING_LITERAL;
        UtSetParseOpName (Op);

        /* Find the parent control method op */

        MethodOp = Op;
        while (MethodOp)
        {
            if (MethodOp->Asl.ParseOpcode == PARSEOP_METHOD)
            {
                /* First child contains the method name */

                MethodOp = MethodOp->Asl.Child;
                Op->Asl.Value.String = MethodOp->Asl.Value.String;
                return;
            }

            MethodOp = MethodOp->Asl.Parent;
        }

        /* At the root, invocation not within a control method */

        Op->Asl.Value.String = "\\";
        break;

    case PARSEOP_UNLOAD:

        AslError (ASL_WARNING, ASL_MSG_UNLOAD, Op, NULL);
        break;

    case PARSEOP_SLEEP:

        /* Remark for very long sleep values */

        if (Op->Asl.Child->Asl.Value.Integer > 1000)
        {
            AslError (ASL_REMARK, ASL_MSG_LONG_SLEEP, Op, NULL);
        }
        break;

    default:

        /* Nothing to do here for other opcodes */

        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    TrDoDefinitionBlock
 *
 * PARAMETERS:  Op        - Parse node
 *
 * RETURN:      None
 *
 * DESCRIPTION: Find the end of the definition block and set a global to this
 *              node. It is used by the compiler to insert compiler-generated
 *              names at the root level of the namespace.
 *
 ******************************************************************************/

static void
TrDoDefinitionBlock (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_PARSE_OBJECT       *Next;
    UINT32                  i;


    /* Reset external list when starting a definition block */

    AslGbl_ExternalsListHead = NULL;

    Next = Op->Asl.Child;
    for (i = 0; i < 5; i++)
    {
        Next = Next->Asl.Next;
        if (i == 0)
        {
            /*
             * This is the table signature. Only the DSDT can be assumed
             * to be at the root of the namespace;  Therefore, namepath
             * optimization can only be performed on the DSDT.
             */
            if (!ACPI_COMPARE_NAME (Next->Asl.Value.String, ACPI_SIG_DSDT))
            {
                AslGbl_ReferenceOptimizationFlag = FALSE;
            }
        }
    }

    AslGbl_FirstLevelInsertionNode = Next;
}


/*******************************************************************************
 *
 * FUNCTION:    TrDoSwitch
 *
 * PARAMETERS:  StartNode        - Parse node for SWITCH
 *
 * RETURN:      None
 *
 * DESCRIPTION: Translate ASL SWITCH statement to if/else pairs. There is
 *              no actual AML opcode for SWITCH -- it must be simulated.
 *
 ******************************************************************************/

static void
TrDoSwitch (
    ACPI_PARSE_OBJECT       *StartNode)
{
    ACPI_PARSE_OBJECT       *Next;
    ACPI_PARSE_OBJECT       *CaseOp = NULL;
    ACPI_PARSE_OBJECT       *CaseBlock = NULL;
    ACPI_PARSE_OBJECT       *DefaultOp = NULL;
    ACPI_PARSE_OBJECT       *CurrentParentNode;
    ACPI_PARSE_OBJECT       *Conditional = NULL;
    ACPI_PARSE_OBJECT       *Predicate;
    ACPI_PARSE_OBJECT       *Peer;
    ACPI_PARSE_OBJECT       *NewOp;
    ACPI_PARSE_OBJECT       *NewOp2;
    ACPI_PARSE_OBJECT       *MethodOp;
    ACPI_PARSE_OBJECT       *StoreOp;
    ACPI_PARSE_OBJECT       *BreakOp;
    ACPI_PARSE_OBJECT       *BufferOp;
    char                    *PredicateValueName;
    UINT16                  Index;
    UINT32                  Btype;


    /* Start node is the Switch() node */

    CurrentParentNode  = StartNode;

    /* Create a new temp name of the form _T_x */

    PredicateValueName = TrAmlGetNextTempName (StartNode, &AslGbl_TempCount);
    if (!PredicateValueName)
    {
        return;
    }

    /* First child is the Switch() predicate */

    Next = StartNode->Asl.Child;

    /*
     * Examine the return type of the Switch Value -
     * must be Integer/Buffer/String
     */
    Index = (UINT16) (Next->Asl.ParseOpcode - ASL_PARSE_OPCODE_BASE);
    Btype = AslKeywordMapping[Index].AcpiBtype;
    if ((Btype != ACPI_BTYPE_INTEGER) &&
        (Btype != ACPI_BTYPE_STRING)  &&
        (Btype != ACPI_BTYPE_BUFFER))
    {
        AslError (ASL_WARNING, ASL_MSG_SWITCH_TYPE, Next, NULL);
        Btype = ACPI_BTYPE_INTEGER;
    }

    /* CASE statements start at next child */

    Peer = Next->Asl.Next;
    while (Peer)
    {
        Next = Peer;
        Peer = Next->Asl.Next;

        if (Next->Asl.ParseOpcode == PARSEOP_CASE)
        {
            if (CaseOp)
            {
                /* Add an ELSE to complete the previous CASE */

                NewOp = TrCreateLeafOp (PARSEOP_ELSE);
                NewOp->Asl.Parent = Conditional->Asl.Parent;
                TrAmlInitLineNumbers (NewOp, NewOp->Asl.Parent);

                /* Link ELSE node as a peer to the previous IF */

                TrAmlInsertPeer (Conditional, NewOp);
                CurrentParentNode = NewOp;
            }

            CaseOp = Next;
            Conditional = CaseOp;
            CaseBlock = CaseOp->Asl.Child->Asl.Next;
            Conditional->Asl.Child->Asl.Next = NULL;
            Predicate = CaseOp->Asl.Child;

            if ((Predicate->Asl.ParseOpcode == PARSEOP_PACKAGE) ||
                (Predicate->Asl.ParseOpcode == PARSEOP_VAR_PACKAGE))
            {
                /*
                 * Convert the package declaration to this form:
                 *
                 * If (LNotEqual (Match (Package(<size>){<data>},
                 *                       MEQ, _T_x, MTR, Zero, Zero), Ones))
                 */
                NewOp2              = TrCreateLeafOp (PARSEOP_MATCHTYPE_MEQ);
                Predicate->Asl.Next = NewOp2;
                TrAmlInitLineNumbers (NewOp2, Conditional);

                NewOp               = NewOp2;
                NewOp2              = TrCreateValuedLeafOp (PARSEOP_NAMESTRING,
                                        (UINT64) ACPI_TO_INTEGER (PredicateValueName));
                NewOp->Asl.Next     = NewOp2;
                TrAmlInitLineNumbers (NewOp2, Predicate);

                NewOp               = NewOp2;
                NewOp2              = TrCreateLeafOp (PARSEOP_MATCHTYPE_MTR);
                NewOp->Asl.Next     = NewOp2;
                TrAmlInitLineNumbers (NewOp2, Predicate);

                NewOp               = NewOp2;
                NewOp2              = TrCreateLeafOp (PARSEOP_ZERO);
                NewOp->Asl.Next     = NewOp2;
                TrAmlInitLineNumbers (NewOp2, Predicate);

                NewOp               = NewOp2;
                NewOp2              = TrCreateLeafOp (PARSEOP_ZERO);
                NewOp->Asl.Next     = NewOp2;
                TrAmlInitLineNumbers (NewOp2, Predicate);

                NewOp2              = TrCreateLeafOp (PARSEOP_MATCH);
                NewOp2->Asl.Child   = Predicate;  /* PARSEOP_PACKAGE */
                TrAmlInitLineNumbers (NewOp2, Conditional);
                TrAmlSetSubtreeParent (Predicate, NewOp2);

                NewOp               = NewOp2;
                NewOp2              = TrCreateLeafOp (PARSEOP_ONES);
                NewOp->Asl.Next     = NewOp2;
                TrAmlInitLineNumbers (NewOp2, Conditional);

                NewOp2              = TrCreateLeafOp (PARSEOP_LEQUAL);
                NewOp2->Asl.Child   = NewOp;
                NewOp->Asl.Parent   = NewOp2;
                TrAmlInitLineNumbers (NewOp2, Conditional);
                TrAmlSetSubtreeParent (NewOp, NewOp2);

                NewOp               = NewOp2;
                NewOp2              = TrCreateLeafOp (PARSEOP_LNOT);
                NewOp2->Asl.Child   = NewOp;
                NewOp2->Asl.Parent  = Conditional;
                NewOp->Asl.Parent   = NewOp2;
                TrAmlInitLineNumbers (NewOp2, Conditional);

                Conditional->Asl.Child = NewOp2;
                NewOp2->Asl.Next = CaseBlock;
            }
            else
            {
                /*
                 * Integer and Buffer case.
                 *
                 * Change CaseOp() to:  If (LEqual (SwitchValue, CaseValue)) {...}
                 * Note: SwitchValue is first to allow the CaseValue to be implicitly
                 * converted to the type of SwitchValue if necessary.
                 *
                 * CaseOp->Child is the case value
                 * CaseOp->Child->Peer is the beginning of the case block
                 */
                NewOp = TrCreateValuedLeafOp (PARSEOP_NAMESTRING,
                    (UINT64) ACPI_TO_INTEGER (PredicateValueName));
                NewOp->Asl.Next = Predicate;
                TrAmlInitLineNumbers (NewOp, Predicate);

                NewOp2              = TrCreateLeafOp (PARSEOP_LEQUAL);
                NewOp2->Asl.Parent  = Conditional;
                NewOp2->Asl.Child   = NewOp;
                TrAmlInitLineNumbers (NewOp2, Conditional);

                TrAmlSetSubtreeParent (NewOp, NewOp2);

                Predicate           = NewOp2;
                Predicate->Asl.Next = CaseBlock;

                TrAmlSetSubtreeParent (Predicate, Conditional);
                Conditional->Asl.Child = Predicate;
            }

            /* Reinitialize the CASE node to an IF node */

            TrAmlInitNode (Conditional, PARSEOP_IF);

            /*
             * The first CASE(IF) is not nested under an ELSE.
             * All other CASEs are children of a parent ELSE.
             */
            if (CurrentParentNode == StartNode)
            {
                Conditional->Asl.Next = NULL;
            }
            else
            {
                /*
                 * The IF is a child of previous IF/ELSE. It
                 * is therefore without peer.
                 */
                CurrentParentNode->Asl.Child = Conditional;
                Conditional->Asl.Parent      = CurrentParentNode;
                Conditional->Asl.Next        = NULL;
            }
        }
        else if (Next->Asl.ParseOpcode == PARSEOP_DEFAULT)
        {
            if (DefaultOp)
            {
                /*
                 * More than one Default
                 * (Parser does not catch this, must check here)
                 */
                AslError (ASL_ERROR, ASL_MSG_MULTIPLE_DEFAULT, Next, NULL);
            }
            else
            {
                /* Save the DEFAULT node for later, after CASEs */

                DefaultOp = Next;
            }
        }
        else
        {
            /* Unknown peer opcode */

            AcpiOsPrintf ("Unknown parse opcode for switch statement: %s (%u)\n",
                Next->Asl.ParseOpName, Next->Asl.ParseOpcode);
        }
    }

    /* Add the default case at the end of the if/else construct */

    if (DefaultOp)
    {
        /* If no CASE statements, this is an error - see below */

        if (CaseOp)
        {
            /* Convert the DEFAULT node to an ELSE */

            TrAmlInitNode (DefaultOp, PARSEOP_ELSE);
            DefaultOp->Asl.Parent = Conditional->Asl.Parent;

            /* Link ELSE node as a peer to the previous IF */

            TrAmlInsertPeer (Conditional, DefaultOp);
        }
    }

    if (!CaseOp)
    {
        AslError (ASL_ERROR, ASL_MSG_NO_CASES, StartNode, NULL);
    }


    /*
     * Create a Name(_T_x, ...) statement. This statement must appear at the
     * method level, in case a loop surrounds the switch statement and could
     * cause the name to be created twice (error).
     */

    /* Create the Name node */

    Predicate = StartNode->Asl.Child;
    NewOp = TrCreateLeafOp (PARSEOP_NAME);
    TrAmlInitLineNumbers (NewOp, StartNode);

    /* Find the parent method */

    Next = StartNode;
    while ((Next->Asl.ParseOpcode != PARSEOP_METHOD) &&
           (Next->Asl.ParseOpcode != PARSEOP_DEFINITION_BLOCK))
    {
        Next = Next->Asl.Parent;
    }
    MethodOp = Next;

    NewOp->Asl.CompileFlags |= OP_COMPILER_EMITTED;
    NewOp->Asl.Parent = Next;

    /* Insert name after the method name and arguments */

    Next = Next->Asl.Child; /* Name */
    Next = Next->Asl.Next;  /* NumArgs */
    Next = Next->Asl.Next;  /* SerializeRule */

    /*
     * If method is not Serialized, we must make is so, because of the way
     * that Switch() must be implemented -- we cannot allow multiple threads
     * to execute this method concurrently since we need to create local
     * temporary name(s).
     */
    if (Next->Asl.ParseOpcode != PARSEOP_SERIALIZERULE_SERIAL)
    {
        AslError (ASL_REMARK, ASL_MSG_SERIALIZED, MethodOp,
            "Due to use of Switch operator");
        Next->Asl.ParseOpcode = PARSEOP_SERIALIZERULE_SERIAL;
    }

    Next = Next->Asl.Next;  /* SyncLevel */
    Next = Next->Asl.Next;  /* ReturnType */
    Next = Next->Asl.Next;  /* ParameterTypes */

    TrAmlInsertPeer (Next, NewOp);
    TrAmlInitLineNumbers (NewOp, Next);

    /* Create the NameSeg child for the Name node */

    NewOp2 = TrCreateValuedLeafOp (PARSEOP_NAMESEG,
        (UINT64) ACPI_TO_INTEGER (PredicateValueName));
    TrAmlInitLineNumbers (NewOp2, NewOp);
    NewOp2->Asl.CompileFlags |= OP_IS_NAME_DECLARATION;
    NewOp->Asl.Child  = NewOp2;

    /* Create the initial value for the Name. Btype was already validated above */

    switch (Btype)
    {
    case ACPI_BTYPE_INTEGER:

        NewOp2->Asl.Next = TrCreateValuedLeafOp (PARSEOP_ZERO,
            (UINT64) 0);
        TrAmlInitLineNumbers (NewOp2->Asl.Next, NewOp);
        break;

    case ACPI_BTYPE_STRING:

        NewOp2->Asl.Next = TrCreateValuedLeafOp (PARSEOP_STRING_LITERAL,
            (UINT64) ACPI_TO_INTEGER (""));
        TrAmlInitLineNumbers (NewOp2->Asl.Next, NewOp);
        break;

    case ACPI_BTYPE_BUFFER:

        (void) TrLinkPeerOp (NewOp2, TrCreateValuedLeafOp (PARSEOP_BUFFER,
            (UINT64) 0));
        Next = NewOp2->Asl.Next;
        TrAmlInitLineNumbers (Next, NewOp2);

        (void) TrLinkOpChildren (Next, 1, TrCreateValuedLeafOp (PARSEOP_ZERO,
            (UINT64) 1));
        TrAmlInitLineNumbers (Next->Asl.Child, Next);

        BufferOp = TrCreateValuedLeafOp (PARSEOP_DEFAULT_ARG, (UINT64) 0);
        TrAmlInitLineNumbers (BufferOp, Next->Asl.Child);
        (void) TrLinkPeerOp (Next->Asl.Child, BufferOp);

        TrAmlSetSubtreeParent (Next->Asl.Child, Next);
        break;

    default:

        break;
    }

    TrAmlSetSubtreeParent (NewOp2, NewOp);

    /*
     * Transform the Switch() into a While(One)-Break node.
     * And create a Store() node which will be used to save the
     * Switch() value. The store is of the form: Store (Value, _T_x)
     * where _T_x is the temp variable.
     */
    TrAmlInitNode (StartNode, PARSEOP_WHILE);
    NewOp = TrCreateLeafOp (PARSEOP_ONE);
    TrAmlInitLineNumbers (NewOp, StartNode);
    NewOp->Asl.Next = Predicate->Asl.Next;
    NewOp->Asl.Parent = StartNode;
    StartNode->Asl.Child = NewOp;

    /* Create a Store() node */

    StoreOp = TrCreateLeafOp (PARSEOP_STORE);
    TrAmlInitLineNumbers (StoreOp, NewOp);
    StoreOp->Asl.Parent = StartNode;
    TrAmlInsertPeer (NewOp, StoreOp);

    /* Complete the Store subtree */

    StoreOp->Asl.Child = Predicate;
    Predicate->Asl.Parent = StoreOp;

    NewOp = TrCreateValuedLeafOp (PARSEOP_NAMESEG,
        (UINT64) ACPI_TO_INTEGER (PredicateValueName));
    TrAmlInitLineNumbers (NewOp, StoreOp);
    NewOp->Asl.Parent    = StoreOp;
    Predicate->Asl.Next  = NewOp;

    /* Create a Break() node and insert it into the end of While() */

    Conditional = StartNode->Asl.Child;
    while (Conditional->Asl.Next)
    {
        Conditional = Conditional->Asl.Next;
    }

    BreakOp = TrCreateLeafOp (PARSEOP_BREAK);
    TrAmlInitLineNumbers (BreakOp, NewOp);
    BreakOp->Asl.Parent = StartNode;
    TrAmlInsertPeer (Conditional, BreakOp);
}

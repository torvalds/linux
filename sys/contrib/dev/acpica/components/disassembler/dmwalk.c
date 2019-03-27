/*******************************************************************************
 *
 * Module Name: dmwalk - AML disassembly tree walk
 *
 ******************************************************************************/

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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acdebug.h>
#include <contrib/dev/acpica/include/acconvert.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dmwalk")


/* Stub for non-compiler code */

#ifndef ACPI_ASL_COMPILER
void
AcpiDmEmitExternals (
    void)
{
    return;
}

void
AcpiDmEmitExternal (
    ACPI_PARSE_OBJECT       *NameOp,
    ACPI_PARSE_OBJECT       *TypeOp)
{
    return;
}
#endif

/* Local prototypes */

static ACPI_STATUS
AcpiDmDescendingOp (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context);

static ACPI_STATUS
AcpiDmAscendingOp (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context);


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDisassemble
 *
 * PARAMETERS:  WalkState       - Current state
 *              Origin          - Starting object
 *              NumOpcodes      - Max number of opcodes to be displayed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Disassemble parser object and its children. This is the
 *              main entry point of the disassembler.
 *
 ******************************************************************************/

void
AcpiDmDisassemble (
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Origin,
    UINT32                  NumOpcodes)
{
    ACPI_PARSE_OBJECT       *Op = Origin;
    ACPI_OP_WALK_INFO       Info;


    if (!Op)
    {
        return;
    }

    memset (&Info, 0, sizeof (ACPI_OP_WALK_INFO));
    Info.WalkState = WalkState;
    Info.StartAml = Op->Common.Aml - sizeof (ACPI_TABLE_HEADER);
    Info.AmlOffset = Op->Common.Aml - Info.StartAml;

    AcpiDmWalkParseTree (Op, AcpiDmDescendingOp, AcpiDmAscendingOp, &Info);
    return;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmWalkParseTree
 *
 * PARAMETERS:  Op                      - Root Op object
 *              DescendingCallback      - Called during tree descent
 *              AscendingCallback       - Called during tree ascent
 *              Context                 - To be passed to the callbacks
 *
 * RETURN:      Status from callback(s)
 *
 * DESCRIPTION: Walk the entire parse tree.
 *
 ******************************************************************************/

void
AcpiDmWalkParseTree (
    ACPI_PARSE_OBJECT       *Op,
    ASL_WALK_CALLBACK       DescendingCallback,
    ASL_WALK_CALLBACK       AscendingCallback,
    void                    *Context)
{
    BOOLEAN                 NodePreviouslyVisited;
    ACPI_PARSE_OBJECT       *StartOp = Op;
    ACPI_STATUS             Status;
    ACPI_PARSE_OBJECT       *Next;
    ACPI_OP_WALK_INFO       *Info = Context;


    Info->Level = 0;
    NodePreviouslyVisited = FALSE;

    while (Op)
    {
        if (NodePreviouslyVisited)
        {
            if (AscendingCallback)
            {
                Status = AscendingCallback (Op, Info->Level, Context);
                if (ACPI_FAILURE (Status))
                {
                    return;
                }
            }
        }
        else
        {
            /* Let the callback process the node */

            Status = DescendingCallback (Op, Info->Level, Context);
            if (ACPI_SUCCESS (Status))
            {
                /* Visit children first, once */

                Next = AcpiPsGetArg (Op, 0);
                if (Next)
                {
                    Info->Level++;
                    Op = Next;
                    continue;
                }
            }
            else if (Status != AE_CTRL_DEPTH)
            {
                /* Exit immediately on any error */

                return;
            }
        }

        /* Terminate walk at start op */

        if (Op == StartOp)
        {
            break;
        }

        /* No more children, re-visit this node */

        if (!NodePreviouslyVisited)
        {
            NodePreviouslyVisited = TRUE;
            continue;
        }

        /* No more children, visit peers */

        if (Op->Common.Next)
        {
            Op = Op->Common.Next;
            NodePreviouslyVisited = FALSE;
        }
        else
        {
            /* No peers, re-visit parent */

            if (Info->Level != 0 )
            {
                Info->Level--;
            }

            Op = Op->Common.Parent;
            NodePreviouslyVisited = TRUE;
        }
    }

    /* If we get here, the walk completed with no errors */

    return;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmBlockType
 *
 * PARAMETERS:  Op              - Object to be examined
 *
 * RETURN:      BlockType - not a block, parens, braces, or even both.
 *
 * DESCRIPTION: Type of block for this op (parens or braces)
 *
 ******************************************************************************/

UINT32
AcpiDmBlockType (
    ACPI_PARSE_OBJECT       *Op)
{
    const ACPI_OPCODE_INFO  *OpInfo;


    if (!Op)
    {
        return (BLOCK_NONE);
    }

    switch (Op->Common.AmlOpcode)
    {
    case AML_ELSE_OP:

        return (BLOCK_BRACE);

    case AML_METHOD_OP:
    case AML_DEVICE_OP:
    case AML_SCOPE_OP:
    case AML_PROCESSOR_OP:
    case AML_POWER_RESOURCE_OP:
    case AML_THERMAL_ZONE_OP:
    case AML_IF_OP:
    case AML_WHILE_OP:
    case AML_FIELD_OP:
    case AML_INDEX_FIELD_OP:
    case AML_BANK_FIELD_OP:

        return (BLOCK_PAREN | BLOCK_BRACE);

    case AML_BUFFER_OP:

        if ((Op->Common.DisasmOpcode == ACPI_DASM_UNICODE) ||
            (Op->Common.DisasmOpcode == ACPI_DASM_UUID) ||
            (Op->Common.DisasmOpcode == ACPI_DASM_PLD_METHOD))
        {
            return (BLOCK_NONE);
        }

        /*lint -fallthrough */

    case AML_PACKAGE_OP:
    case AML_VARIABLE_PACKAGE_OP:

        return (BLOCK_PAREN | BLOCK_BRACE);

    case AML_EVENT_OP:

        return (BLOCK_PAREN);

    case AML_INT_METHODCALL_OP:

        if (Op->Common.Parent &&
            ((Op->Common.Parent->Common.AmlOpcode == AML_PACKAGE_OP) ||
             (Op->Common.Parent->Common.AmlOpcode == AML_VARIABLE_PACKAGE_OP)))
        {
            /* This is a reference to a method, not an invocation */

            return (BLOCK_NONE);
        }

        /*lint -fallthrough */

    default:

        OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);
        if (OpInfo->Flags & AML_HAS_ARGS)
        {
            return (BLOCK_PAREN);
        }

        return (BLOCK_NONE);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmListType
 *
 * PARAMETERS:  Op              - Object to be examined
 *
 * RETURN:      ListType - has commas or not.
 *
 * DESCRIPTION: Type of block for this op (parens or braces)
 *
 ******************************************************************************/

UINT32
AcpiDmListType (
    ACPI_PARSE_OBJECT       *Op)
{
    const ACPI_OPCODE_INFO  *OpInfo;


    if (!Op)
    {
        return (BLOCK_NONE);
    }

    switch (Op->Common.AmlOpcode)
    {

    case AML_ELSE_OP:
    case AML_METHOD_OP:
    case AML_DEVICE_OP:
    case AML_SCOPE_OP:
    case AML_POWER_RESOURCE_OP:
    case AML_PROCESSOR_OP:
    case AML_THERMAL_ZONE_OP:
    case AML_IF_OP:
    case AML_WHILE_OP:
    case AML_FIELD_OP:
    case AML_INDEX_FIELD_OP:
    case AML_BANK_FIELD_OP:

        return (BLOCK_NONE);

    case AML_BUFFER_OP:
    case AML_PACKAGE_OP:
    case AML_VARIABLE_PACKAGE_OP:

        return (BLOCK_COMMA_LIST);

    default:

        OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);
        if (OpInfo->Flags & AML_HAS_ARGS)
        {
            return (BLOCK_COMMA_LIST);
        }

        return (BLOCK_NONE);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDescendingOp
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: First visitation of a parse object during tree descent.
 *              Decode opcode name and begin parameter list(s), if any.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDmDescendingOp (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ACPI_OP_WALK_INFO       *Info = Context;
    const ACPI_OPCODE_INFO  *OpInfo;
    UINT32                  Name;
    ACPI_PARSE_OBJECT       *NextOp;
    ACPI_PARSE_OBJECT       *NextOp2;
    UINT32                  AmlOffset;


    /* Determine which file this parse node is contained in. */

    if (AcpiGbl_CaptureComments)
    {
        ASL_CV_LABEL_FILENODE (Op);

        if (Level != 0 && ASL_CV_FILE_HAS_SWITCHED (Op))
        {
            ASL_CV_SWITCH_FILES (Level, Op);
        }

        /* If this parse node has regular comments, print them here. */

        ASL_CV_PRINT_ONE_COMMENT (Op, AML_COMMENT_STANDARD, NULL, Level);
    }

    OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);

    /* Listing support to dump the AML code after the ASL statement */

    if (AcpiGbl_DmOpt_Listing)
    {
        /* We only care about these classes of objects */

        if ((OpInfo->Class == AML_CLASS_NAMED_OBJECT) ||
            (OpInfo->Class == AML_CLASS_CONTROL) ||
            (OpInfo->Class == AML_CLASS_CREATE) ||
            ((OpInfo->Class == AML_CLASS_EXECUTE) && (!Op->Common.Next)))
        {
            if (AcpiGbl_DmOpt_Listing && Info->PreviousAml)
            {
                /* Dump the AML byte code for the previous Op */

                if (Op->Common.Aml > Info->PreviousAml)
                {
                    AcpiOsPrintf ("\n");
                    AcpiUtDumpBuffer (
                        (Info->StartAml + Info->AmlOffset),
                        (Op->Common.Aml - Info->PreviousAml),
                        DB_BYTE_DISPLAY, Info->AmlOffset);
                    AcpiOsPrintf ("\n");
                }

                Info->AmlOffset = (Op->Common.Aml - Info->StartAml);
            }

            Info->PreviousAml = Op->Common.Aml;
        }
    }

    if (Op->Common.DisasmFlags & ACPI_PARSEOP_IGNORE)
    {
        /* Ignore this op -- it was handled elsewhere */

        return (AE_CTRL_DEPTH);
    }

    if (Op->Common.DisasmOpcode == ACPI_DASM_IGNORE_SINGLE)
    {
        /* Ignore this op, but not it's children */

        return (AE_OK);
    }

    if (Op->Common.AmlOpcode == AML_IF_OP)
    {
        NextOp = AcpiPsGetDepthNext (NULL, Op);
        if (NextOp)
        {
            NextOp->Common.DisasmFlags |= ACPI_PARSEOP_PARAMETER_LIST;

            /* Don't emit the actual embedded externals unless asked */

            if (!AcpiGbl_DmEmitExternalOpcodes)
            {
                /*
                 * A Zero predicate indicates the possibility of one or more
                 * External() opcodes within the If() block.
                 */
                if (NextOp->Common.AmlOpcode == AML_ZERO_OP)
                {
                    NextOp2 = NextOp->Common.Next;

                    if (NextOp2 &&
                        (NextOp2->Common.AmlOpcode == AML_EXTERNAL_OP))
                    {
                        /* Ignore the If 0 block and all children */

                        Op->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                        return (AE_CTRL_DEPTH);
                    }
                }
            }
        }
    }

    /* Level 0 is at the Definition Block level */

    if (Level == 0)
    {
        /* In verbose mode, print the AML offset, opcode and depth count */

        if (Info->WalkState)
        {
            AmlOffset = (UINT32) ACPI_PTR_DIFF (Op->Common.Aml,
                Info->WalkState->ParserState.AmlStart);
            if (AcpiGbl_DmOpt_Verbose)
            {
                if (AcpiGbl_CmSingleStep)
                {
                    AcpiOsPrintf ("%5.5X/%4.4X: ",
                        AmlOffset, (UINT32) Op->Common.AmlOpcode);
                }
                else
                {
                    AcpiOsPrintf ("AML Offset %5.5X, Opcode %4.4X: ",
                        AmlOffset, (UINT32) Op->Common.AmlOpcode);
                }
            }
        }

        if (Op->Common.AmlOpcode == AML_SCOPE_OP)
        {
            /* This is the beginning of the Definition Block */

            AcpiOsPrintf ("{\n");

            /* Emit all External() declarations here */

            if (!AcpiGbl_DmEmitExternalOpcodes)
            {
                AcpiDmEmitExternals ();
            }

            return (AE_OK);
        }
    }
    else if ((AcpiDmBlockType (Op->Common.Parent) & BLOCK_BRACE) &&
         (!(Op->Common.DisasmFlags & ACPI_PARSEOP_PARAMETER_LIST)) &&
         (!(Op->Common.DisasmFlags & ACPI_PARSEOP_ELSEIF)) &&
         (Op->Common.AmlOpcode != AML_INT_BYTELIST_OP))
    {
        /*
         * This is a first-level element of a term list,
         * indent a new line
         */
        switch (Op->Common.AmlOpcode)
        {
        case AML_NOOP_OP:
            /*
             * Optionally just ignore this opcode. Some tables use
             * NoOp opcodes for "padding" out packages that the BIOS
             * changes dynamically. This can leave hundreds or
             * thousands of NoOp opcodes that if disassembled,
             * cannot be compiled because they are syntactically
             * incorrect.
             */
            if (AcpiGbl_IgnoreNoopOperator)
            {
                Op->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                return (AE_OK);
            }

            /* Fallthrough */

        default:

            AcpiDmIndent (Level);
            break;
        }

        Info->LastLevel = Level;
        Info->Count = 0;
    }

    /*
     * This is an inexpensive mechanism to try and keep lines from getting
     * too long. When the limit is hit, start a new line at the previous
     * indent plus one. A better but more expensive mechanism would be to
     * keep track of the current column.
     */
    Info->Count++;
    if (Info->Count /* +Info->LastLevel */ > 12)
    {
        Info->Count = 0;
        AcpiOsPrintf ("\n");
        AcpiDmIndent (Info->LastLevel + 1);
    }

    /* If ASL+ is enabled, check for a C-style operator */

    if (AcpiDmCheckForSymbolicOpcode (Op, Info))
    {
        return (AE_OK);
    }

    /* Print the opcode name */

    AcpiDmDisassembleOneOp (NULL, Info, Op);

    if ((Op->Common.DisasmOpcode == ACPI_DASM_LNOT_PREFIX) ||
        (Op->Common.AmlOpcode == AML_INT_CONNECTION_OP))
    {
        return (AE_OK);
    }

    if ((Op->Common.AmlOpcode == AML_NAME_OP) ||
        (Op->Common.AmlOpcode == AML_RETURN_OP))
    {
        Info->Level--;
    }

    if (Op->Common.AmlOpcode == AML_EXTERNAL_OP)
    {
        Op->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
        return (AE_CTRL_DEPTH);
    }

    /* Start the opcode argument list if necessary */

    if ((OpInfo->Flags & AML_HAS_ARGS) ||
        (Op->Common.AmlOpcode == AML_EVENT_OP))
    {
        /* This opcode has an argument list */

        if (AcpiDmBlockType (Op) & BLOCK_PAREN)
        {
            AcpiOsPrintf (" (");
            if (!(AcpiDmBlockType (Op) & BLOCK_BRACE))
            {
                ASL_CV_PRINT_ONE_COMMENT (Op, AMLCOMMENT_INLINE, " ", 0);
            }
        }

        /* If this is a named opcode, print the associated name value */

        if (OpInfo->Flags & AML_NAMED)
        {
            switch (Op->Common.AmlOpcode)
            {
            case AML_ALIAS_OP:

                NextOp = AcpiPsGetDepthNext (NULL, Op);
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                AcpiDmNamestring (NextOp->Common.Value.Name);
                AcpiOsPrintf (", ");

                /*lint -fallthrough */

            default:

                Name = AcpiPsGetName (Op);
                if (Op->Named.Path)
                {
                    AcpiDmNamestring (Op->Named.Path);
                }
                else
                {
                    AcpiDmDumpName (Name);
                }

                if (Op->Common.AmlOpcode != AML_INT_NAMEDFIELD_OP)
                {
                    if (AcpiGbl_DmOpt_Verbose)
                    {
                        (void) AcpiPsDisplayObjectPathname (NULL, Op);
                    }
                }
                break;
            }

            switch (Op->Common.AmlOpcode)
            {
            case AML_METHOD_OP:

                AcpiDmMethodFlags (Op);
                ASL_CV_CLOSE_PAREN (Op, Level);

                /* Emit description comment for Method() with a predefined ACPI name */

                AcpiDmPredefinedDescription (Op);
                break;

            case AML_NAME_OP:

                /* Check for _HID and related EISAID() */

                AcpiDmCheckForHardwareId (Op);
                AcpiOsPrintf (", ");
                ASL_CV_PRINT_ONE_COMMENT (Op, AML_NAMECOMMENT, NULL, 0);
                break;

            case AML_REGION_OP:

                AcpiDmRegionFlags (Op);
                break;

            case AML_POWER_RESOURCE_OP:

                /* Mark the next two Ops as part of the parameter list */

                AcpiOsPrintf (", ");
                NextOp = AcpiPsGetDepthNext (NULL, Op);
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_PARAMETER_LIST;

                NextOp = NextOp->Common.Next;
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_PARAMETER_LIST;
                return (AE_OK);

            case AML_PROCESSOR_OP:

                /* Mark the next three Ops as part of the parameter list */

                AcpiOsPrintf (", ");
                NextOp = AcpiPsGetDepthNext (NULL, Op);
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_PARAMETER_LIST;

                NextOp = NextOp->Common.Next;
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_PARAMETER_LIST;

                NextOp = NextOp->Common.Next;
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_PARAMETER_LIST;
                return (AE_OK);

            case AML_MUTEX_OP:
            case AML_DATA_REGION_OP:

                AcpiOsPrintf (", ");
                return (AE_OK);

            case AML_EVENT_OP:
            case AML_ALIAS_OP:

                return (AE_OK);

            case AML_SCOPE_OP:
            case AML_DEVICE_OP:
            case AML_THERMAL_ZONE_OP:

                ASL_CV_CLOSE_PAREN (Op, Level);
                break;

            default:

                AcpiOsPrintf ("*** Unhandled named opcode %X\n",
                    Op->Common.AmlOpcode);
                break;
            }
        }

        else switch (Op->Common.AmlOpcode)
        {
        case AML_FIELD_OP:
        case AML_BANK_FIELD_OP:
        case AML_INDEX_FIELD_OP:

            Info->BitOffset = 0;

            /* Name of the parent OperationRegion */

            NextOp = AcpiPsGetDepthNext (NULL, Op);
            AcpiDmNamestring (NextOp->Common.Value.Name);
            AcpiOsPrintf (", ");
            NextOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;

            switch (Op->Common.AmlOpcode)
            {
            case AML_BANK_FIELD_OP:

                /* Namestring - Bank Name */

                NextOp = AcpiPsGetDepthNext (NULL, NextOp);
                AcpiDmNamestring (NextOp->Common.Value.Name);
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                AcpiOsPrintf (", ");

                /*
                 * Bank Value. This is a TermArg in the middle of the parameter
                 * list, must handle it here.
                 *
                 * Disassemble the TermArg parse tree. ACPI_PARSEOP_PARAMETER_LIST
                 * eliminates newline in the output.
                 */
                NextOp = NextOp->Common.Next;

                Info->Flags = ACPI_PARSEOP_PARAMETER_LIST;
                AcpiDmWalkParseTree (NextOp, AcpiDmDescendingOp,
                    AcpiDmAscendingOp, Info);
                Info->Flags = 0;
                Info->Level = Level;

                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                AcpiOsPrintf (", ");
                break;

            case AML_INDEX_FIELD_OP:

                /* Namestring - Data Name */

                NextOp = AcpiPsGetDepthNext (NULL, NextOp);
                AcpiDmNamestring (NextOp->Common.Value.Name);
                AcpiOsPrintf (", ");
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                break;

            default:

                break;
            }

            AcpiDmFieldFlags (NextOp);
            break;

        case AML_BUFFER_OP:

            /* The next op is the size parameter */

            NextOp = AcpiPsGetDepthNext (NULL, Op);
            if (!NextOp)
            {
                /* Single-step support */

                return (AE_OK);
            }

            if (Op->Common.DisasmOpcode == ACPI_DASM_RESOURCE)
            {
                /*
                 * We have a resource list. Don't need to output
                 * the buffer size Op. Open up a new block
                 */
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                NextOp = NextOp->Common.Next;
                ASL_CV_CLOSE_PAREN (Op, Level);

                /* Emit description comment for Name() with a predefined ACPI name */

                AcpiDmPredefinedDescription (Op->Asl.Parent);

                AcpiOsPrintf ("\n");
                AcpiDmIndent (Info->Level);
                AcpiOsPrintf ("{\n");
                return (AE_OK);
            }

            /* Normal Buffer, mark size as in the parameter list */

            NextOp->Common.DisasmFlags |= ACPI_PARSEOP_PARAMETER_LIST;
            return (AE_OK);

        case AML_IF_OP:
        case AML_VARIABLE_PACKAGE_OP:
        case AML_WHILE_OP:

            /* The next op is the size or predicate parameter */

            NextOp = AcpiPsGetDepthNext (NULL, Op);
            if (NextOp)
            {
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_PARAMETER_LIST;
            }
            return (AE_OK);

        case AML_PACKAGE_OP:

            /* The next op is the size parameter */

            NextOp = AcpiPsGetDepthNext (NULL, Op);
            if (NextOp)
            {
                NextOp->Common.DisasmFlags |= ACPI_PARSEOP_PARAMETER_LIST;
            }
            return (AE_OK);

        case AML_MATCH_OP:

            AcpiDmMatchOp (Op);
            break;

        default:

            break;
        }

        if (AcpiDmBlockType (Op) & BLOCK_BRACE)
        {
            AcpiOsPrintf ("\n");
            AcpiDmIndent (Level);
            AcpiOsPrintf ("{\n");
        }
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmAscendingOp
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Second visitation of a parse object, during ascent of parse
 *              tree. Close out any parameter lists and complete the opcode.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDmAscendingOp (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ACPI_OP_WALK_INFO       *Info = Context;
    ACPI_PARSE_OBJECT       *ParentOp;


    /* Point the Op's filename pointer to the proper file */

    if (AcpiGbl_CaptureComments)
    {
        ASL_CV_LABEL_FILENODE (Op);

        /* Switch the output of these files if necessary */

        if (ASL_CV_FILE_HAS_SWITCHED (Op))
        {
            ASL_CV_SWITCH_FILES (Level, Op);
        }
    }

    if (Op->Common.DisasmFlags & ACPI_PARSEOP_IGNORE ||
        Op->Common.DisasmOpcode == ACPI_DASM_IGNORE_SINGLE)
    {
        /* Ignore this op -- it was handled elsewhere */

        return (AE_OK);
    }

    if ((Level == 0) && (Op->Common.AmlOpcode == AML_SCOPE_OP))
    {
        /* Indicates the end of the current descriptor block (table) */

        ASL_CV_CLOSE_BRACE (Op, Level);

        /* Print any comments that are at the end of the file here */

        if (AcpiGbl_CaptureComments && AcpiGbl_LastListHead)
        {
            AcpiOsPrintf ("\n");
            ASL_CV_PRINT_ONE_COMMENT_LIST (AcpiGbl_LastListHead, 0);
        }
        AcpiOsPrintf ("\n\n");

        return (AE_OK);
    }

    switch (AcpiDmBlockType (Op))
    {
    case BLOCK_PAREN:

        /* Completed an op that has arguments, add closing paren if needed */

        AcpiDmCloseOperator (Op);

        if (Op->Common.AmlOpcode == AML_NAME_OP)
        {
            /* Emit description comment for Name() with a predefined ACPI name */

            AcpiDmPredefinedDescription (Op);
        }
        else
        {
            /* For Create* operators, attempt to emit resource tag description */

            AcpiDmFieldPredefinedDescription (Op);
        }

        /* Decode Notify() values */

        if (Op->Common.AmlOpcode == AML_NOTIFY_OP)
        {
            AcpiDmNotifyDescription (Op);
        }

        AcpiDmDisplayTargetPathname (Op);

        /* Could be a nested operator, check if comma required */

        if (!AcpiDmCommaIfListMember (Op))
        {
            if ((AcpiDmBlockType (Op->Common.Parent) & BLOCK_BRACE) &&
                 (!(Op->Common.DisasmFlags & ACPI_PARSEOP_PARAMETER_LIST)) &&
                 (Op->Common.AmlOpcode != AML_INT_BYTELIST_OP))
            {
                /*
                 * This is a first-level element of a term list
                 * start a new line
                 */
                if (!(Info->Flags & ACPI_PARSEOP_PARAMETER_LIST))
                {
                    AcpiOsPrintf ("\n");
                }
            }
        }
        break;

    case BLOCK_BRACE:
    case (BLOCK_BRACE | BLOCK_PAREN):

        /* Completed an op that has a term list, add closing brace */

        if (Op->Common.DisasmFlags & ACPI_PARSEOP_EMPTY_TERMLIST)
        {
            ASL_CV_CLOSE_BRACE (Op, Level);
        }
        else
        {
            AcpiDmIndent (Level);
            ASL_CV_CLOSE_BRACE (Op, Level);
        }

        AcpiDmCommaIfListMember (Op);

        if (AcpiDmBlockType (Op->Common.Parent) != BLOCK_PAREN)
        {
            AcpiOsPrintf ("\n");
            if (!(Op->Common.DisasmFlags & ACPI_PARSEOP_EMPTY_TERMLIST))
            {
                if ((Op->Common.AmlOpcode == AML_IF_OP)  &&
                    (Op->Common.Next) &&
                    (Op->Common.Next->Common.AmlOpcode == AML_ELSE_OP))
                {
                    break;
                }

                if ((AcpiDmBlockType (Op->Common.Parent) & BLOCK_BRACE) &&
                    (!Op->Common.Next))
                {
                    break;
                }
                AcpiOsPrintf ("\n");
            }
        }
        break;

    case BLOCK_NONE:
    default:

        /* Could be a nested operator, check if comma required */

        if (!AcpiDmCommaIfListMember (Op))
        {
            if ((AcpiDmBlockType (Op->Common.Parent) & BLOCK_BRACE) &&
                 (!(Op->Common.DisasmFlags & ACPI_PARSEOP_PARAMETER_LIST)) &&
                 (Op->Common.AmlOpcode != AML_INT_BYTELIST_OP))
            {
                /*
                 * This is a first-level element of a term list
                 * start a new line
                 */
                AcpiOsPrintf ("\n");
            }
        }
        else if (Op->Common.Parent)
        {
            switch (Op->Common.Parent->Common.AmlOpcode)
            {
            case AML_PACKAGE_OP:
            case AML_VARIABLE_PACKAGE_OP:

                if (!(Op->Common.DisasmFlags & ACPI_PARSEOP_PARAMETER_LIST))
                {
                    AcpiOsPrintf ("\n");
                }
                break;

            default:

                break;
            }
        }
        break;
    }

    if (Op->Common.DisasmFlags & ACPI_PARSEOP_PARAMETER_LIST)
    {
        if ((Op->Common.Next) &&
            (Op->Common.Next->Common.DisasmFlags & ACPI_PARSEOP_PARAMETER_LIST))
        {
            return (AE_OK);
        }

        /*
         * The parent Op is guaranteed to be valid because of the flag
         * ACPI_PARSEOP_PARAMETER_LIST -- which means that this op is part of
         * a parameter list and thus has a valid parent.
         */
        ParentOp = Op->Common.Parent;

        /*
         * Just completed a parameter node for something like "Buffer (param)".
         * Close the paren and open up the term list block with a brace.
         *
         * Switch predicates don't have a Next node but require a closing paren
         * and opening brace.
         */
        if (Op->Common.Next || Op->Common.DisasmOpcode == ACPI_DASM_SWITCH_PREDICATE)
        {
            ASL_CV_CLOSE_PAREN (Op, Level);

            /*
             * Emit a description comment for a Name() operator that is a
             * predefined ACPI name. Must check the grandparent.
             */
            ParentOp = ParentOp->Common.Parent;
            if (ParentOp &&
                (ParentOp->Asl.AmlOpcode == AML_NAME_OP))
            {
                AcpiDmPredefinedDescription (ParentOp);
            }

            /* Correct the indentation level for Switch and Case predicates */

            if (Op->Common.DisasmOpcode == ACPI_DASM_SWITCH_PREDICATE)
            {
                --Level;
            }

            AcpiOsPrintf ("\n");
            AcpiDmIndent (Level - 1);
            AcpiOsPrintf ("{\n");
        }
        else
        {
            ParentOp->Common.DisasmFlags |= ACPI_PARSEOP_EMPTY_TERMLIST;
            ASL_CV_CLOSE_PAREN (Op, Level);
            AcpiOsPrintf ("{");
        }
    }

    if ((Op->Common.AmlOpcode == AML_NAME_OP) ||
        (Op->Common.AmlOpcode == AML_RETURN_OP))
    {
        Info->Level++;
    }

    /*
     * For ASL+, check for and emit a C-style symbol. If valid, the
     * symbol string has been deferred until after the first operand
     */
    if (AcpiGbl_CstyleDisassembly)
    {
        if (Op->Asl.OperatorSymbol)
        {
            AcpiOsPrintf ("%s", Op->Asl.OperatorSymbol);
            Op->Asl.OperatorSymbol = NULL;
        }
    }

    return (AE_OK);
}

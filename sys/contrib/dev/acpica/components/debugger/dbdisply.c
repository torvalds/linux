/*******************************************************************************
 *
 * Module Name: dbdisply - debug display commands
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
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acevents.h>
#include <contrib/dev/acpica/include/acdebug.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dbdisply")

/* Local prototypes */

static void
AcpiDbDumpParserDescriptor (
    ACPI_PARSE_OBJECT       *Op);

static void *
AcpiDbGetPointer (
    void                    *Target);

static ACPI_STATUS
AcpiDbDisplayNonRootHandlers (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

/*
 * System handler information.
 * Used for Handlers command, in AcpiDbDisplayHandlers.
 */
#define ACPI_PREDEFINED_PREFIX          "%25s (%.2X) : "
#define ACPI_HANDLER_NAME_STRING               "%30s : "
#define ACPI_HANDLER_PRESENT_STRING                    "%-9s (%p)\n"
#define ACPI_HANDLER_PRESENT_STRING2                   "%-9s (%p)"
#define ACPI_HANDLER_NOT_PRESENT_STRING                "%-9s\n"

/* All predefined Address Space IDs */

static ACPI_ADR_SPACE_TYPE  AcpiGbl_SpaceIdList[] =
{
    ACPI_ADR_SPACE_SYSTEM_MEMORY,
    ACPI_ADR_SPACE_SYSTEM_IO,
    ACPI_ADR_SPACE_PCI_CONFIG,
    ACPI_ADR_SPACE_EC,
    ACPI_ADR_SPACE_SMBUS,
    ACPI_ADR_SPACE_CMOS,
    ACPI_ADR_SPACE_PCI_BAR_TARGET,
    ACPI_ADR_SPACE_IPMI,
    ACPI_ADR_SPACE_GPIO,
    ACPI_ADR_SPACE_GSBUS,
    ACPI_ADR_SPACE_DATA_TABLE,
    ACPI_ADR_SPACE_FIXED_HARDWARE
};

/* Global handler information */

typedef struct acpi_handler_info
{
    void                    *Handler;
    char                    *Name;

} ACPI_HANDLER_INFO;

static ACPI_HANDLER_INFO    AcpiGbl_HandlerList[] =
{
    {&AcpiGbl_GlobalNotify[0].Handler,  "System Notifications"},
    {&AcpiGbl_GlobalNotify[1].Handler,  "Device Notifications"},
    {&AcpiGbl_TableHandler,             "ACPI Table Events"},
    {&AcpiGbl_ExceptionHandler,         "Control Method Exceptions"},
    {&AcpiGbl_InterfaceHandler,         "OSI Invocations"}
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbGetPointer
 *
 * PARAMETERS:  Target          - Pointer to string to be converted
 *
 * RETURN:      Converted pointer
 *
 * DESCRIPTION: Convert an ascii pointer value to a real value
 *
 ******************************************************************************/

static void *
AcpiDbGetPointer (
    void                    *Target)
{
    void                    *ObjPtr;
    ACPI_SIZE               Address;


    Address = strtoul (Target, NULL, 16);
    ObjPtr = ACPI_TO_POINTER (Address);
    return (ObjPtr);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDumpParserDescriptor
 *
 * PARAMETERS:  Op              - A parser Op descriptor
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display a formatted parser object
 *
 ******************************************************************************/

static void
AcpiDbDumpParserDescriptor (
    ACPI_PARSE_OBJECT       *Op)
{
    const ACPI_OPCODE_INFO  *Info;


    Info = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);

    AcpiOsPrintf ("Parser Op Descriptor:\n");
    AcpiOsPrintf ("%20.20s : %4.4X\n", "Opcode", Op->Common.AmlOpcode);

    ACPI_DEBUG_ONLY_MEMBERS (AcpiOsPrintf ("%20.20s : %s\n", "Opcode Name",
        Info->Name));

    AcpiOsPrintf ("%20.20s : %p\n", "Value/ArgList", Op->Common.Value.Arg);
    AcpiOsPrintf ("%20.20s : %p\n", "Parent", Op->Common.Parent);
    AcpiOsPrintf ("%20.20s : %p\n", "NextOp", Op->Common.Next);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDecodeAndDisplayObject
 *
 * PARAMETERS:  Target          - String with object to be displayed. Names
 *                                and hex pointers are supported.
 *              OutputType      - Byte, Word, Dword, or Qword (B|W|D|Q)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display a formatted ACPI object
 *
 ******************************************************************************/

void
AcpiDbDecodeAndDisplayObject (
    char                    *Target,
    char                    *OutputType)
{
    void                    *ObjPtr;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    UINT32                  Display = DB_BYTE_DISPLAY;
    char                    Buffer[80];
    ACPI_BUFFER             RetBuf;
    ACPI_STATUS             Status;
    UINT32                  Size;


    if (!Target)
    {
        return;
    }

    /* Decode the output type */

    if (OutputType)
    {
        AcpiUtStrupr (OutputType);
        if (OutputType[0] == 'W')
        {
            Display = DB_WORD_DISPLAY;
        }
        else if (OutputType[0] == 'D')
        {
            Display = DB_DWORD_DISPLAY;
        }
        else if (OutputType[0] == 'Q')
        {
            Display = DB_QWORD_DISPLAY;
        }
    }

    RetBuf.Length = sizeof (Buffer);
    RetBuf.Pointer = Buffer;

    /* Differentiate between a number and a name */

    if ((Target[0] >= 0x30) && (Target[0] <= 0x39))
    {
        ObjPtr = AcpiDbGetPointer (Target);
        if (!AcpiOsReadable (ObjPtr, 16))
        {
            AcpiOsPrintf (
                "Address %p is invalid in this address space\n",
                ObjPtr);
            return;
        }

        /* Decode the object type */

        switch (ACPI_GET_DESCRIPTOR_TYPE (ObjPtr))
        {
        case ACPI_DESC_TYPE_NAMED:

            /* This is a namespace Node */

            if (!AcpiOsReadable (ObjPtr, sizeof (ACPI_NAMESPACE_NODE)))
            {
                AcpiOsPrintf (
                    "Cannot read entire Named object at address %p\n",
                    ObjPtr);
                return;
            }

            Node = ObjPtr;
            goto DumpNode;

        case ACPI_DESC_TYPE_OPERAND:

            /* This is a ACPI OPERAND OBJECT */

            if (!AcpiOsReadable (ObjPtr, sizeof (ACPI_OPERAND_OBJECT)))
            {
                AcpiOsPrintf (
                    "Cannot read entire ACPI object at address %p\n",
                    ObjPtr);
                return;
            }

            AcpiUtDebugDumpBuffer (ObjPtr, sizeof (ACPI_OPERAND_OBJECT),
                Display, ACPI_UINT32_MAX);
            AcpiExDumpObjectDescriptor (ObjPtr, 1);
            break;

        case ACPI_DESC_TYPE_PARSER:

            /* This is a Parser Op object */

            if (!AcpiOsReadable (ObjPtr, sizeof (ACPI_PARSE_OBJECT)))
            {
                AcpiOsPrintf (
                    "Cannot read entire Parser object at address %p\n",
                    ObjPtr);
                return;
            }

            AcpiUtDebugDumpBuffer (ObjPtr, sizeof (ACPI_PARSE_OBJECT),
                Display, ACPI_UINT32_MAX);
            AcpiDbDumpParserDescriptor ((ACPI_PARSE_OBJECT *) ObjPtr);
            break;

        default:

            /* Is not a recognizable object */

            AcpiOsPrintf (
                "Not a known ACPI internal object, descriptor type %2.2X\n",
                ACPI_GET_DESCRIPTOR_TYPE (ObjPtr));

            Size = 16;
            if (AcpiOsReadable (ObjPtr, 64))
            {
                Size = 64;
            }

            /* Just dump some memory */

            AcpiUtDebugDumpBuffer (ObjPtr, Size, Display, ACPI_UINT32_MAX);
            break;
        }

        return;
    }

    /* The parameter is a name string that must be resolved to a Named obj */

    Node = AcpiDbLocalNsLookup (Target);
    if (!Node)
    {
        return;
    }


DumpNode:
    /* Now dump the NS node */

    Status = AcpiGetName (Node, ACPI_FULL_PATHNAME_NO_TRAILING, &RetBuf);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could not convert name to pathname\n");
    }

    else
    {
        AcpiOsPrintf ("Object %p: Namespace Node - Pathname: %s\n",
            Node, (char *) RetBuf.Pointer);
    }

    if (!AcpiOsReadable (Node, sizeof (ACPI_NAMESPACE_NODE)))
    {
        AcpiOsPrintf ("Invalid Named object at address %p\n", Node);
        return;
    }

    AcpiUtDebugDumpBuffer ((void *) Node, sizeof (ACPI_NAMESPACE_NODE),
        Display, ACPI_UINT32_MAX);
    AcpiExDumpNamespaceNode (Node, 1);

    ObjDesc = AcpiNsGetAttachedObject (Node);
    if (ObjDesc)
    {
        AcpiOsPrintf ("\nAttached Object %p:", ObjDesc);
        if (!AcpiOsReadable (ObjDesc, sizeof (ACPI_OPERAND_OBJECT)))
        {
            AcpiOsPrintf ("Invalid internal ACPI Object at address %p\n",
                ObjDesc);
            return;
        }

        if (ACPI_GET_DESCRIPTOR_TYPE (
            ((ACPI_NAMESPACE_NODE *) ObjDesc)) == ACPI_DESC_TYPE_NAMED)
        {
            AcpiOsPrintf (" Namespace Node - ");
            Status = AcpiGetName ((ACPI_NAMESPACE_NODE *) ObjDesc,
                ACPI_FULL_PATHNAME_NO_TRAILING, &RetBuf);
            if (ACPI_FAILURE (Status))
            {
                AcpiOsPrintf ("Could not convert name to pathname\n");
            }
            else
            {
                AcpiOsPrintf ("Pathname: %s",
                    (char *) RetBuf.Pointer);
            }

            AcpiOsPrintf ("\n");
            AcpiUtDebugDumpBuffer ((void *) ObjDesc,
                sizeof (ACPI_NAMESPACE_NODE), Display, ACPI_UINT32_MAX);
        }
        else
        {
            AcpiOsPrintf ("\n");
            AcpiUtDebugDumpBuffer ((void *) ObjDesc,
                sizeof (ACPI_OPERAND_OBJECT), Display, ACPI_UINT32_MAX);
        }

        AcpiExDumpObjectDescriptor (ObjDesc, 1);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayMethodInfo
 *
 * PARAMETERS:  StartOp         - Root of the control method parse tree
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display information about the current method
 *
 ******************************************************************************/

void
AcpiDbDisplayMethodInfo (
    ACPI_PARSE_OBJECT       *StartOp)
{
    ACPI_WALK_STATE         *WalkState;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_PARSE_OBJECT       *RootOp;
    ACPI_PARSE_OBJECT       *Op;
    const ACPI_OPCODE_INFO  *OpInfo;
    UINT32                  NumOps = 0;
    UINT32                  NumOperands = 0;
    UINT32                  NumOperators = 0;
    UINT32                  NumRemainingOps = 0;
    UINT32                  NumRemainingOperands = 0;
    UINT32                  NumRemainingOperators = 0;
    BOOLEAN                 CountRemaining = FALSE;


    WalkState = AcpiDsGetCurrentWalkState (AcpiGbl_CurrentWalkList);
    if (!WalkState)
    {
        AcpiOsPrintf ("There is no method currently executing\n");
        return;
    }

    ObjDesc = WalkState->MethodDesc;
    Node = WalkState->MethodNode;

    AcpiOsPrintf ("Currently executing control method is [%4.4s]\n",
        AcpiUtGetNodeName (Node));
    AcpiOsPrintf ("%X Arguments, SyncLevel = %X\n",
        (UINT32) ObjDesc->Method.ParamCount,
        (UINT32) ObjDesc->Method.SyncLevel);

    RootOp = StartOp;
    while (RootOp->Common.Parent)
    {
        RootOp = RootOp->Common.Parent;
    }

    Op = RootOp;

    while (Op)
    {
        if (Op == StartOp)
        {
            CountRemaining = TRUE;
        }

        NumOps++;
        if (CountRemaining)
        {
            NumRemainingOps++;
        }

        /* Decode the opcode */

        OpInfo = AcpiPsGetOpcodeInfo (Op->Common.AmlOpcode);
        switch (OpInfo->Class)
        {
        case AML_CLASS_ARGUMENT:

            if (CountRemaining)
            {
                NumRemainingOperands++;
            }

            NumOperands++;
            break;

        case AML_CLASS_UNKNOWN:

            /* Bad opcode or ASCII character */

            continue;

        default:

            if (CountRemaining)
            {
                NumRemainingOperators++;
            }

            NumOperators++;
            break;
        }

        Op = AcpiPsGetDepthNext (StartOp, Op);
    }

    AcpiOsPrintf (
        "Method contains:       %X AML Opcodes - %X Operators, %X Operands\n",
        NumOps, NumOperators, NumOperands);

    AcpiOsPrintf (
        "Remaining to execute:  %X AML Opcodes - %X Operators, %X Operands\n",
        NumRemainingOps, NumRemainingOperators, NumRemainingOperands);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayLocals
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display all locals for the currently running control method
 *
 ******************************************************************************/

void
AcpiDbDisplayLocals (
    void)
{
    ACPI_WALK_STATE         *WalkState;


    WalkState = AcpiDsGetCurrentWalkState (AcpiGbl_CurrentWalkList);
    if (!WalkState)
    {
        AcpiOsPrintf ("There is no method currently executing\n");
        return;
    }

    AcpiDbDecodeLocals (WalkState);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayArguments
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display all arguments for the currently running control method
 *
 ******************************************************************************/

void
AcpiDbDisplayArguments (
    void)
{
    ACPI_WALK_STATE         *WalkState;


    WalkState = AcpiDsGetCurrentWalkState (AcpiGbl_CurrentWalkList);
    if (!WalkState)
    {
        AcpiOsPrintf ("There is no method currently executing\n");
        return;
    }

    AcpiDbDecodeArguments (WalkState);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayResults
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display current contents of a method result stack
 *
 ******************************************************************************/

void
AcpiDbDisplayResults (
    void)
{
    UINT32                  i;
    ACPI_WALK_STATE         *WalkState;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    UINT32                  ResultCount = 0;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_GENERIC_STATE      *Frame;
    UINT32                  Index; /* Index onto current frame */


    WalkState = AcpiDsGetCurrentWalkState (AcpiGbl_CurrentWalkList);
    if (!WalkState)
    {
        AcpiOsPrintf ("There is no method currently executing\n");
        return;
    }

    ObjDesc = WalkState->MethodDesc;
    Node  = WalkState->MethodNode;

    if (WalkState->Results)
    {
        ResultCount = WalkState->ResultCount;
    }

    AcpiOsPrintf ("Method [%4.4s] has %X stacked result objects\n",
        AcpiUtGetNodeName (Node), ResultCount);

    /* From the top element of result stack */

    Frame = WalkState->Results;
    Index = (ResultCount - 1) % ACPI_RESULTS_FRAME_OBJ_NUM;

    for (i = 0; i < ResultCount; i++)
    {
        ObjDesc = Frame->Results.ObjDesc[Index];
        AcpiOsPrintf ("Result%u: ", i);
        AcpiDbDisplayInternalObject (ObjDesc, WalkState);

        if (Index == 0)
        {
            Frame = Frame->Results.Next;
            Index = ACPI_RESULTS_FRAME_OBJ_NUM;
        }

        Index--;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayCallingTree
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display current calling tree of nested control methods
 *
 ******************************************************************************/

void
AcpiDbDisplayCallingTree (
    void)
{
    ACPI_WALK_STATE         *WalkState;
    ACPI_NAMESPACE_NODE     *Node;


    WalkState = AcpiDsGetCurrentWalkState (AcpiGbl_CurrentWalkList);
    if (!WalkState)
    {
        AcpiOsPrintf ("There is no method currently executing\n");
        return;
    }

    Node = WalkState->MethodNode;
    AcpiOsPrintf ("Current Control Method Call Tree\n");

    while (WalkState)
    {
        Node = WalkState->MethodNode;
        AcpiOsPrintf ("    [%4.4s]\n", AcpiUtGetNodeName (Node));

        WalkState = WalkState->Next;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayObjectType
 *
 * PARAMETERS:  ObjectArg       - User entered NS node handle
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display type of an arbitrary NS node
 *
 ******************************************************************************/

void
AcpiDbDisplayObjectType (
    char                    *ObjectArg)
{
    ACPI_SIZE               Arg;
    ACPI_HANDLE             Handle;
    ACPI_DEVICE_INFO        *Info;
    ACPI_STATUS             Status;
    UINT32                  i;


    Arg = strtoul (ObjectArg, NULL, 16);
    Handle = ACPI_TO_POINTER (Arg);

    Status = AcpiGetObjectInfo (Handle, &Info);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could not get object info, %s\n",
            AcpiFormatException (Status));
        return;
    }

    AcpiOsPrintf ("ADR: %8.8X%8.8X, Flags: %X\n",
        ACPI_FORMAT_UINT64 (Info->Address), Info->Flags);

    AcpiOsPrintf ("S1D-%2.2X S2D-%2.2X S3D-%2.2X S4D-%2.2X\n",
        Info->HighestDstates[0], Info->HighestDstates[1],
        Info->HighestDstates[2], Info->HighestDstates[3]);

    AcpiOsPrintf ("S0W-%2.2X S1W-%2.2X S2W-%2.2X S3W-%2.2X S4W-%2.2X\n",
        Info->LowestDstates[0], Info->LowestDstates[1],
        Info->LowestDstates[2], Info->LowestDstates[3],
        Info->LowestDstates[4]);

    if (Info->Valid & ACPI_VALID_HID)
    {
        AcpiOsPrintf ("HID: %s\n", Info->HardwareId.String);
    }

    if (Info->Valid & ACPI_VALID_UID)
    {
        AcpiOsPrintf ("UID: %s\n", Info->UniqueId.String);
    }

    if (Info->Valid & ACPI_VALID_CID)
    {
        for (i = 0; i < Info->CompatibleIdList.Count; i++)
        {
            AcpiOsPrintf ("CID %u: %s\n", i,
                Info->CompatibleIdList.Ids[i].String);
        }
    }

    ACPI_FREE (Info);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayResultObject
 *
 * PARAMETERS:  ObjDesc         - Object to be displayed
 *              WalkState       - Current walk state
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display the result of an AML opcode
 *
 * Note: Currently only displays the result object if we are single stepping.
 * However, this output may be useful in other contexts and could be enabled
 * to do so if needed.
 *
 ******************************************************************************/

void
AcpiDbDisplayResultObject (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_WALK_STATE         *WalkState)
{

#ifndef ACPI_APPLICATION
    if (AcpiGbl_DbThreadId != AcpiOsGetThreadId())
    {
        return;
    }
#endif

    /* Only display if single stepping */

    if (!AcpiGbl_CmSingleStep)
    {
        return;
    }

    AcpiOsPrintf ("ResultObj: ");
    AcpiDbDisplayInternalObject (ObjDesc, WalkState);
    AcpiOsPrintf ("\n");
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayArgumentObject
 *
 * PARAMETERS:  ObjDesc         - Object to be displayed
 *              WalkState       - Current walk state
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display the result of an AML opcode
 *
 ******************************************************************************/

void
AcpiDbDisplayArgumentObject (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_WALK_STATE         *WalkState)
{

#ifndef ACPI_APPLICATION
    if (AcpiGbl_DbThreadId != AcpiOsGetThreadId())
    {
        return;
    }
#endif

    if (!AcpiGbl_CmSingleStep)
    {
        return;
    }

    AcpiOsPrintf ("ArgObj:    ");
    AcpiDbDisplayInternalObject (ObjDesc, WalkState);
}


#if (!ACPI_REDUCED_HARDWARE)
/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayGpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display the current GPE structures
 *
 ******************************************************************************/

void
AcpiDbDisplayGpes (
    void)
{
    ACPI_GPE_BLOCK_INFO     *GpeBlock;
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo;
    ACPI_GPE_EVENT_INFO     *GpeEventInfo;
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    char                    *GpeType;
    ACPI_GPE_NOTIFY_INFO    *Notify;
    UINT32                  GpeIndex;
    UINT32                  Block = 0;
    UINT32                  i;
    UINT32                  j;
    UINT32                  Count;
    char                    Buffer[80];
    ACPI_BUFFER             RetBuf;
    ACPI_STATUS             Status;


    RetBuf.Length = sizeof (Buffer);
    RetBuf.Pointer = Buffer;

    Block = 0;

    /* Walk the GPE lists */

    GpeXruptInfo = AcpiGbl_GpeXruptListHead;
    while (GpeXruptInfo)
    {
        GpeBlock = GpeXruptInfo->GpeBlockListHead;
        while (GpeBlock)
        {
            Status = AcpiGetName (GpeBlock->Node,
                ACPI_FULL_PATHNAME_NO_TRAILING, &RetBuf);
            if (ACPI_FAILURE (Status))
            {
                AcpiOsPrintf ("Could not convert name to pathname\n");
            }

            if (GpeBlock->Node == AcpiGbl_FadtGpeDevice)
            {
                GpeType = "FADT-defined GPE block";
            }
            else
            {
                GpeType = "GPE Block Device";
            }

            AcpiOsPrintf (
                "\nBlock %u - Info %p  DeviceNode %p [%s] - %s\n",
                Block, GpeBlock, GpeBlock->Node, Buffer, GpeType);

            AcpiOsPrintf (
                "    Registers:    %u (%u GPEs)\n",
                GpeBlock->RegisterCount, GpeBlock->GpeCount);

            AcpiOsPrintf (
                "    GPE range:    0x%X to 0x%X on interrupt %u\n",
                GpeBlock->BlockBaseNumber,
                GpeBlock->BlockBaseNumber + (GpeBlock->GpeCount - 1),
                GpeXruptInfo->InterruptNumber);

            AcpiOsPrintf (
                "    RegisterInfo: %p  Status %8.8X%8.8X Enable %8.8X%8.8X\n",
                GpeBlock->RegisterInfo,
                ACPI_FORMAT_UINT64 (
                    GpeBlock->RegisterInfo->StatusAddress.Address),
                ACPI_FORMAT_UINT64 (
                    GpeBlock->RegisterInfo->EnableAddress.Address));

            AcpiOsPrintf ("    EventInfo:    %p\n", GpeBlock->EventInfo);

            /* Examine each GPE Register within the block */

            for (i = 0; i < GpeBlock->RegisterCount; i++)
            {
                GpeRegisterInfo = &GpeBlock->RegisterInfo[i];

                AcpiOsPrintf (
                    "    Reg %u: (GPE %.2X-%.2X)  "
                    "RunEnable %2.2X WakeEnable %2.2X"
                    " Status %8.8X%8.8X Enable %8.8X%8.8X\n",
                    i, GpeRegisterInfo->BaseGpeNumber,
                    GpeRegisterInfo->BaseGpeNumber +
                        (ACPI_GPE_REGISTER_WIDTH - 1),
                    GpeRegisterInfo->EnableForRun,
                    GpeRegisterInfo->EnableForWake,
                    ACPI_FORMAT_UINT64 (
                        GpeRegisterInfo->StatusAddress.Address),
                    ACPI_FORMAT_UINT64 (
                        GpeRegisterInfo->EnableAddress.Address));

                /* Now look at the individual GPEs in this byte register */

                for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++)
                {
                    GpeIndex = (i * ACPI_GPE_REGISTER_WIDTH) + j;
                    GpeEventInfo = &GpeBlock->EventInfo[GpeIndex];

                    if (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags) ==
                        ACPI_GPE_DISPATCH_NONE)
                    {
                        /* This GPE is not used (no method or handler), ignore it */

                        continue;
                    }

                    AcpiOsPrintf (
                        "        GPE %.2X: %p  RunRefs %2.2X Flags %2.2X (",
                        GpeBlock->BlockBaseNumber + GpeIndex, GpeEventInfo,
                        GpeEventInfo->RuntimeCount, GpeEventInfo->Flags);

                    /* Decode the flags byte */

                    if (GpeEventInfo->Flags & ACPI_GPE_LEVEL_TRIGGERED)
                    {
                        AcpiOsPrintf ("Level, ");
                    }
                    else
                    {
                        AcpiOsPrintf ("Edge,  ");
                    }

                    if (GpeEventInfo->Flags & ACPI_GPE_CAN_WAKE)
                    {
                        AcpiOsPrintf ("CanWake, ");
                    }
                    else
                    {
                        AcpiOsPrintf ("RunOnly, ");
                    }

                    switch (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags))
                    {
                    case ACPI_GPE_DISPATCH_NONE:

                        AcpiOsPrintf ("NotUsed");
                        break;

                    case ACPI_GPE_DISPATCH_METHOD:

                        AcpiOsPrintf ("Method");
                        break;

                    case ACPI_GPE_DISPATCH_HANDLER:

                        AcpiOsPrintf ("Handler");
                        break;

                    case ACPI_GPE_DISPATCH_NOTIFY:

                        Count = 0;
                        Notify = GpeEventInfo->Dispatch.NotifyList;
                        while (Notify)
                        {
                            Count++;
                            Notify = Notify->Next;
                        }

                        AcpiOsPrintf ("Implicit Notify on %u devices",
                            Count);
                        break;

                    case ACPI_GPE_DISPATCH_RAW_HANDLER:

                        AcpiOsPrintf ("RawHandler");
                        break;

                    default:

                        AcpiOsPrintf ("UNKNOWN: %X",
                            ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags));
                        break;
                    }

                    AcpiOsPrintf (")\n");
                }
            }

            Block++;
            GpeBlock = GpeBlock->Next;
        }

        GpeXruptInfo = GpeXruptInfo->Next;
    }
}
#endif /* !ACPI_REDUCED_HARDWARE */


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayHandlers
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display the currently installed global handlers
 *
 ******************************************************************************/

void
AcpiDbDisplayHandlers (
    void)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OPERAND_OBJECT     *HandlerObj;
    ACPI_ADR_SPACE_TYPE     SpaceId;
    UINT32                  i;


    /* Operation region handlers */

    AcpiOsPrintf ("\nOperation Region Handlers at the namespace root:\n");

    ObjDesc = AcpiNsGetAttachedObject (AcpiGbl_RootNode);
    if (ObjDesc)
    {
        for (i = 0; i < ACPI_ARRAY_LENGTH (AcpiGbl_SpaceIdList); i++)
        {
            SpaceId = AcpiGbl_SpaceIdList[i];

            AcpiOsPrintf (ACPI_PREDEFINED_PREFIX,
                AcpiUtGetRegionName ((UINT8) SpaceId), SpaceId);

            HandlerObj = AcpiEvFindRegionHandler (
                SpaceId, ObjDesc->CommonNotify.Handler);
            if (HandlerObj)
            {
                AcpiOsPrintf (ACPI_HANDLER_PRESENT_STRING,
                    (HandlerObj->AddressSpace.HandlerFlags &
                        ACPI_ADDR_HANDLER_DEFAULT_INSTALLED) ?
                        "Default" : "User",
                    HandlerObj->AddressSpace.Handler);

                goto FoundHandler;
            }

            /* There is no handler for this SpaceId */

            AcpiOsPrintf ("None\n");

        FoundHandler:;
        }

        /* Find all handlers for user-defined SpaceIDs */

        HandlerObj = ObjDesc->CommonNotify.Handler;
        while (HandlerObj)
        {
            if (HandlerObj->AddressSpace.SpaceId >= ACPI_USER_REGION_BEGIN)
            {
                AcpiOsPrintf (ACPI_PREDEFINED_PREFIX,
                    "User-defined ID", HandlerObj->AddressSpace.SpaceId);
                AcpiOsPrintf (ACPI_HANDLER_PRESENT_STRING,
                    (HandlerObj->AddressSpace.HandlerFlags &
                        ACPI_ADDR_HANDLER_DEFAULT_INSTALLED) ?
                        "Default" : "User",
                    HandlerObj->AddressSpace.Handler);
            }

            HandlerObj = HandlerObj->AddressSpace.Next;
        }
    }

#if (!ACPI_REDUCED_HARDWARE)

    /* Fixed event handlers */

    AcpiOsPrintf ("\nFixed Event Handlers:\n");

    for (i = 0; i < ACPI_NUM_FIXED_EVENTS; i++)
    {
        AcpiOsPrintf (ACPI_PREDEFINED_PREFIX, AcpiUtGetEventName (i), i);
        if (AcpiGbl_FixedEventHandlers[i].Handler)
        {
            AcpiOsPrintf (ACPI_HANDLER_PRESENT_STRING, "User",
                AcpiGbl_FixedEventHandlers[i].Handler);
        }
        else
        {
            AcpiOsPrintf (ACPI_HANDLER_NOT_PRESENT_STRING, "None");
        }
    }

#endif /* !ACPI_REDUCED_HARDWARE */

    /* Miscellaneous global handlers */

    AcpiOsPrintf ("\nMiscellaneous Global Handlers:\n");

    for (i = 0; i < ACPI_ARRAY_LENGTH (AcpiGbl_HandlerList); i++)
    {
        AcpiOsPrintf (ACPI_HANDLER_NAME_STRING,
            AcpiGbl_HandlerList[i].Name);

        if (AcpiGbl_HandlerList[i].Handler)
        {
            AcpiOsPrintf (ACPI_HANDLER_PRESENT_STRING, "User",
                AcpiGbl_HandlerList[i].Handler);
        }
        else
        {
            AcpiOsPrintf (ACPI_HANDLER_NOT_PRESENT_STRING, "None");
        }
    }


    /* Other handlers that are installed throughout the namespace */

    AcpiOsPrintf ("\nOperation Region Handlers for specific devices:\n");

    (void) AcpiWalkNamespace (ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, AcpiDbDisplayNonRootHandlers,
        NULL, NULL, NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayNonRootHandlers
 *
 * PARAMETERS:  ACPI_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display information about all handlers installed for a
 *              device object.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbDisplayNonRootHandlers (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_NAMESPACE_NODE     *Node = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, ObjHandle);
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OPERAND_OBJECT     *HandlerObj;
    char                    *Pathname;


    ObjDesc = AcpiNsGetAttachedObject (Node);
    if (!ObjDesc)
    {
        return (AE_OK);
    }

    Pathname = AcpiNsGetNormalizedPathname (Node, TRUE);
    if (!Pathname)
    {
        return (AE_OK);
    }

    /* Display all handlers associated with this device */

    HandlerObj = ObjDesc->CommonNotify.Handler;
    while (HandlerObj)
    {
        AcpiOsPrintf (ACPI_PREDEFINED_PREFIX,
            AcpiUtGetRegionName ((UINT8) HandlerObj->AddressSpace.SpaceId),
            HandlerObj->AddressSpace.SpaceId);

        AcpiOsPrintf (ACPI_HANDLER_PRESENT_STRING2,
            (HandlerObj->AddressSpace.HandlerFlags &
                ACPI_ADDR_HANDLER_DEFAULT_INSTALLED) ? "Default" : "User",
            HandlerObj->AddressSpace.Handler);

        AcpiOsPrintf (" Device Name: %s (%p)\n", Pathname, Node);

        HandlerObj = HandlerObj->AddressSpace.Next;
    }

    ACPI_FREE (Pathname);
    return (AE_OK);
}

/******************************************************************************
 *
 * Module Name: aslnamesp - Namespace output file generation
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
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslnamesp")

/* Local prototypes */

static ACPI_STATUS
NsDoOneNamespaceObject (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
NsDoOnePathname (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue);


/*******************************************************************************
 *
 * FUNCTION:    NsSetupNamespaceListing
 *
 * PARAMETERS:  Handle          - local file handle
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set the namespace output file to the input handle
 *
 ******************************************************************************/

void
NsSetupNamespaceListing (
    void                    *Handle)
{

    AslGbl_NsOutputFlag = TRUE;
    AslGbl_Files[ASL_FILE_NAMESPACE_OUTPUT].Handle = Handle;
}


/*******************************************************************************
 *
 * FUNCTION:    NsDisplayNamespace
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the namespace an display information about each node
 *              in the tree. Information is written to the optional
 *              namespace output file.
 *
 ******************************************************************************/

ACPI_STATUS
NsDisplayNamespace (
    void)
{
    ACPI_STATUS             Status;


    if (!AslGbl_NsOutputFlag)
    {
        return (AE_OK);
    }

    AslGbl_NumNamespaceObjects = 0;

    /* File header */

    FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "Contents of ACPI Namespace\n\n");
    FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "Count  Depth    Name - Type\n\n");

    /* Walk entire namespace from the root */

    Status = AcpiNsWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, FALSE, NsDoOneNamespaceObject, NULL,
        NULL, NULL);

    /* Print the full pathname for each namespace node */

    FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "\nNamespace pathnames\n\n");

    Status = AcpiNsWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, FALSE, NsDoOnePathname, NULL,
        NULL, NULL);

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    NsDoOneNamespaceObject
 *
 * PARAMETERS:  ACPI_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dump a namespace object to the namespace output file.
 *              Called during the walk of the namespace to dump all objects.
 *
 ******************************************************************************/

static ACPI_STATUS
NsDoOneNamespaceObject (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_PARSE_OBJECT       *Op;


    AslGbl_NumNamespaceObjects++;

    FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "%5u  [%u]  %*s %4.4s - %s",
        AslGbl_NumNamespaceObjects, Level, (Level * 3), " ",
        &Node->Name, AcpiUtGetTypeName (Node->Type));

    Op = Node->Op;
    ObjDesc = ACPI_CAST_PTR (ACPI_OPERAND_OBJECT, Node->Object);

    if (!Op)
    {
        FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "\n");
        return (AE_OK);
    }


    if ((ObjDesc) &&
        (ACPI_GET_DESCRIPTOR_TYPE (ObjDesc) == ACPI_DESC_TYPE_OPERAND))
    {
        switch (Node->Type)
        {
        case ACPI_TYPE_INTEGER:

            FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                "       [Initial Value   0x%8.8X%8.8X]",
                ACPI_FORMAT_UINT64 (ObjDesc->Integer.Value));
            break;

        case ACPI_TYPE_STRING:

            FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                "        [Initial Value   \"%s\"]",
                ObjDesc->String.Pointer);
            break;

        default:

            /* Nothing to do for other types */

            break;
        }

    }
    else
    {
        switch (Node->Type)
        {
        case ACPI_TYPE_INTEGER:

            if (Op->Asl.ParseOpcode == PARSEOP_NAME)
            {
                Op = Op->Asl.Child;
            }

            if ((Op->Asl.ParseOpcode == PARSEOP_NAMESEG)  ||
                (Op->Asl.ParseOpcode == PARSEOP_NAMESTRING))
            {
                Op = Op->Asl.Next;
            }

            FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                "       [Initial Value   0x%8.8X%8.8X]",
                ACPI_FORMAT_UINT64 (Op->Asl.Value.Integer));
            break;

        case ACPI_TYPE_STRING:

            if (Op->Asl.ParseOpcode == PARSEOP_NAME)
            {
                Op = Op->Asl.Child;
            }

            if ((Op->Asl.ParseOpcode == PARSEOP_NAMESEG)  ||
                (Op->Asl.ParseOpcode == PARSEOP_NAMESTRING))
            {
                Op = Op->Asl.Next;
            }

            FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                "        [Initial Value   \"%s\"]",
                Op->Asl.Value.String);
            break;

        case ACPI_TYPE_LOCAL_REGION_FIELD:

            if ((Op->Asl.ParseOpcode == PARSEOP_NAMESEG)  ||
                (Op->Asl.ParseOpcode == PARSEOP_NAMESTRING))
            {
                Op = Op->Asl.Child;
            }

            FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                "   [Offset 0x%04X   Length 0x%04X bits]",
                Op->Asl.Parent->Asl.ExtraValue, (UINT32) Op->Asl.Value.Integer);
            break;

        case ACPI_TYPE_BUFFER_FIELD:

            switch (Op->Asl.ParseOpcode)
            {
            case PARSEOP_CREATEBYTEFIELD:

                FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "   [BYTE  ( 8 bit)]");
                break;

            case PARSEOP_CREATEDWORDFIELD:

                FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "   [DWORD (32 bit)]");
                break;

            case PARSEOP_CREATEQWORDFIELD:

                FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "   [QWORD (64 bit)]");
                break;

            case PARSEOP_CREATEWORDFIELD:

                FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "   [WORD  (16 bit)]");
                break;

            case PARSEOP_CREATEBITFIELD:

                FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "   [BIT   ( 1 bit)]");
                break;

            case PARSEOP_CREATEFIELD:

                FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "   [Arbitrary Bit Field]");
                break;

            default:

                break;

            }
            break;

        case ACPI_TYPE_PACKAGE:

            if (Op->Asl.ParseOpcode == PARSEOP_NAME)
            {
                Op = Op->Asl.Child;
            }

            if ((Op->Asl.ParseOpcode == PARSEOP_NAMESEG)  ||
                (Op->Asl.ParseOpcode == PARSEOP_NAMESTRING))
            {
                Op = Op->Asl.Next;
            }

            Op = Op->Asl.Child;

            if ((Op->Asl.ParseOpcode == PARSEOP_BYTECONST) ||
                (Op->Asl.ParseOpcode == PARSEOP_RAW_DATA))
            {
                FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                    "       [Initial Length  0x%.2X elements]",
                    Op->Asl.Value.Integer);
            }
            break;

        case ACPI_TYPE_BUFFER:

            if (Op->Asl.ParseOpcode == PARSEOP_NAME)
            {
                Op = Op->Asl.Child;
            }

            if ((Op->Asl.ParseOpcode == PARSEOP_NAMESEG)  ||
                (Op->Asl.ParseOpcode == PARSEOP_NAMESTRING))
            {
                Op = Op->Asl.Next;
            }

            Op = Op->Asl.Child;

            if (Op && (Op->Asl.ParseOpcode == PARSEOP_INTEGER))
            {
                FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                    "        [Initial Length  0x%.2X bytes]",
                    Op->Asl.Value.Integer);
            }
            break;

        case ACPI_TYPE_METHOD:

            FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                "        [Code Length     0x%.4X bytes]",
                Op->Asl.AmlSubtreeLength);
            break;

        case ACPI_TYPE_LOCAL_RESOURCE:

            FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                "  [Desc Offset     0x%.4X Bytes]", Node->Value);
            break;

        case ACPI_TYPE_LOCAL_RESOURCE_FIELD:

            FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                "   [Field Offset    0x%.4X Bits 0x%.4X Bytes] ",
                Node->Value, Node->Value / 8);

            if (Node->Flags & ANOBJ_IS_REFERENCED)
            {
                FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                    "Referenced");
            }
            else
            {
                FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT,
                    "Name not referenced");
            }
            break;

        default:

            /* Nothing to do for other types */

            break;
        }
    }

    FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "\n");
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    NsDoOnePathname
 *
 * PARAMETERS:  ACPI_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Print the full pathname for a namespace node.
 *
 ******************************************************************************/

static ACPI_STATUS
NsDoOnePathname (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;
    ACPI_STATUS             Status;
    ACPI_BUFFER             TargetPath;


    TargetPath.Length = ACPI_ALLOCATE_LOCAL_BUFFER;
    Status = AcpiNsHandleToPathname (Node, &TargetPath, FALSE);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    FlPrintFile (ASL_FILE_NAMESPACE_OUTPUT, "%s\n", TargetPath.Pointer);
    ACPI_FREE (TargetPath.Pointer);
    return (AE_OK);
}

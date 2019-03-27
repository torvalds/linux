/******************************************************************************
 *
 * Module Name: exdebug - Support for stores to the AML Debug Object
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
#include <contrib/dev/acpica/include/acinterp.h>


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("exdebug")


#ifndef ACPI_NO_ERROR_MESSAGES
/*******************************************************************************
 *
 * FUNCTION:    AcpiExDoDebugObject
 *
 * PARAMETERS:  SourceDesc          - Object to be output to "Debug Object"
 *              Level               - Indentation level (used for packages)
 *              Index               - Current package element, zero if not pkg
 *
 * RETURN:      None
 *
 * DESCRIPTION: Handles stores to the AML Debug Object. For example:
 *              Store(INT1, Debug)
 *
 * This function is not compiled if ACPI_NO_ERROR_MESSAGES is set.
 *
 * This function is only enabled if AcpiGbl_EnableAmlDebugObject is set, or
 * if ACPI_LV_DEBUG_OBJECT is set in the AcpiDbgLevel. Thus, in the normal
 * operational case, stores to the debug object are ignored but can be easily
 * enabled if necessary.
 *
 ******************************************************************************/

void
AcpiExDoDebugObject (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    UINT32                  Level,
    UINT32                  Index)
{
    UINT32                  i;
    UINT32                  Timer;
    ACPI_OPERAND_OBJECT     *ObjectDesc;
    UINT32                  Value;


    ACPI_FUNCTION_TRACE_PTR (ExDoDebugObject, SourceDesc);


    /* Output must be enabled via the DebugObject global or the DbgLevel */

    if (!AcpiGbl_EnableAmlDebugObject &&
        !(AcpiDbgLevel & ACPI_LV_DEBUG_OBJECT))
    {
        return_VOID;
    }

    /* Newline -- don't emit the line header */

    if (SourceDesc &&
        (ACPI_GET_DESCRIPTOR_TYPE (SourceDesc) == ACPI_DESC_TYPE_OPERAND) &&
        (SourceDesc->Common.Type == ACPI_TYPE_STRING))
    {
        if ((SourceDesc->String.Length == 1) &&
            (*SourceDesc->String.Pointer == '\n'))
        {
            AcpiOsPrintf ("\n");
            return_VOID;
        }
    }

    /*
     * Print line header as long as we are not in the middle of an
     * object display
     */
    if (!((Level > 0) && Index == 0))
    {
        if (AcpiGbl_DisplayDebugTimer)
        {
            /*
             * We will emit the current timer value (in microseconds) with each
             * debug output. Only need the lower 26 bits. This allows for 67
             * million microseconds or 67 seconds before rollover.
             *
             * Convert 100 nanosecond units to microseconds
             */
            Timer = ((UINT32) AcpiOsGetTimer () / 10);
            Timer &= 0x03FFFFFF;

            AcpiOsPrintf ("ACPI Debug: T=0x%8.8X %*s", Timer, Level, " ");
        }
        else
        {
            AcpiOsPrintf ("ACPI Debug: %*s", Level, " ");
        }
    }

    /* Display the index for package output only */

    if (Index > 0)
    {
       AcpiOsPrintf ("(%.2u) ", Index - 1);
    }

    if (!SourceDesc)
    {
        AcpiOsPrintf ("[Null Object]\n");
        return_VOID;
    }

    if (ACPI_GET_DESCRIPTOR_TYPE (SourceDesc) == ACPI_DESC_TYPE_OPERAND)
    {
        /* No object type prefix needed for integers and strings */

        if ((SourceDesc->Common.Type != ACPI_TYPE_INTEGER) &&
            (SourceDesc->Common.Type != ACPI_TYPE_STRING))
        {
            AcpiOsPrintf ("%s  ", AcpiUtGetObjectTypeName (SourceDesc));
        }

        if (!AcpiUtValidInternalObject (SourceDesc))
        {
           AcpiOsPrintf ("%p, Invalid Internal Object!\n", SourceDesc);
           return_VOID;
        }
    }
    else if (ACPI_GET_DESCRIPTOR_TYPE (SourceDesc) == ACPI_DESC_TYPE_NAMED)
    {
        AcpiOsPrintf ("%s  (Node %p)\n",
            AcpiUtGetTypeName (((ACPI_NAMESPACE_NODE *) SourceDesc)->Type),
                SourceDesc);
        return_VOID;
    }
    else
    {
        return_VOID;
    }

    /* SourceDesc is of type ACPI_DESC_TYPE_OPERAND */

    switch (SourceDesc->Common.Type)
    {
    case ACPI_TYPE_INTEGER:

        /* Output correct integer width */

        if (AcpiGbl_IntegerByteWidth == 4)
        {
            AcpiOsPrintf ("0x%8.8X\n",
                (UINT32) SourceDesc->Integer.Value);
        }
        else
        {
            AcpiOsPrintf ("0x%8.8X%8.8X\n",
                ACPI_FORMAT_UINT64 (SourceDesc->Integer.Value));
        }
        break;

    case ACPI_TYPE_BUFFER:

        AcpiOsPrintf ("[0x%.2X]\n", (UINT32) SourceDesc->Buffer.Length);
        AcpiUtDumpBuffer (SourceDesc->Buffer.Pointer,
            (SourceDesc->Buffer.Length < 256) ?
                SourceDesc->Buffer.Length : 256, DB_BYTE_DISPLAY, 0);
        break;

    case ACPI_TYPE_STRING:

        AcpiOsPrintf ("\"%s\"\n", SourceDesc->String.Pointer);
        break;

    case ACPI_TYPE_PACKAGE:

        AcpiOsPrintf ("(Contains 0x%.2X Elements):\n",
            SourceDesc->Package.Count);

        /* Output the entire contents of the package */

        for (i = 0; i < SourceDesc->Package.Count; i++)
        {
            AcpiExDoDebugObject (SourceDesc->Package.Elements[i],
                Level + 4, i + 1);
        }
        break;

    case ACPI_TYPE_LOCAL_REFERENCE:

        AcpiOsPrintf ("[%s] ", AcpiUtGetReferenceName (SourceDesc));

        /* Decode the reference */

        switch (SourceDesc->Reference.Class)
        {
        case ACPI_REFCLASS_INDEX:

            AcpiOsPrintf ("0x%X\n", SourceDesc->Reference.Value);
            break;

        case ACPI_REFCLASS_TABLE:

            /* Case for DdbHandle */

            AcpiOsPrintf ("Table Index 0x%X\n", SourceDesc->Reference.Value);
            return_VOID;

        default:

            break;
        }

        AcpiOsPrintf ("  ");

        /* Check for valid node first, then valid object */

        if (SourceDesc->Reference.Node)
        {
            if (ACPI_GET_DESCRIPTOR_TYPE (SourceDesc->Reference.Node) !=
                ACPI_DESC_TYPE_NAMED)
            {
                AcpiOsPrintf (" %p - Not a valid namespace node\n",
                    SourceDesc->Reference.Node);
            }
            else
            {
                AcpiOsPrintf ("Node %p [%4.4s] ", SourceDesc->Reference.Node,
                    (SourceDesc->Reference.Node)->Name.Ascii);

                switch ((SourceDesc->Reference.Node)->Type)
                {
                /* These types have no attached object */

                case ACPI_TYPE_DEVICE:
                    AcpiOsPrintf ("Device\n");
                    break;

                case ACPI_TYPE_THERMAL:
                    AcpiOsPrintf ("Thermal Zone\n");
                    break;

                default:

                    AcpiExDoDebugObject ((SourceDesc->Reference.Node)->Object,
                        Level + 4, 0);
                    break;
                }
            }
        }
        else if (SourceDesc->Reference.Object)
        {
            if (ACPI_GET_DESCRIPTOR_TYPE (SourceDesc->Reference.Object) ==
                ACPI_DESC_TYPE_NAMED)
            {
                /* Reference object is a namespace node */

                AcpiExDoDebugObject (ACPI_CAST_PTR (ACPI_OPERAND_OBJECT,
                    SourceDesc->Reference.Object),
                    Level + 4, 0);
            }
            else
            {
                ObjectDesc = SourceDesc->Reference.Object;
                Value = SourceDesc->Reference.Value;

                switch (ObjectDesc->Common.Type)
                {
                case ACPI_TYPE_BUFFER:

                    AcpiOsPrintf ("Buffer[%u] = 0x%2.2X\n",
                        Value, *SourceDesc->Reference.IndexPointer);
                    break;

                case ACPI_TYPE_STRING:

                    AcpiOsPrintf ("String[%u] = \"%c\" (0x%2.2X)\n",
                        Value, *SourceDesc->Reference.IndexPointer,
                        *SourceDesc->Reference.IndexPointer);
                    break;

                case ACPI_TYPE_PACKAGE:

                    AcpiOsPrintf ("Package[%u] = ", Value);
                    if (!(*SourceDesc->Reference.Where))
                    {
                        AcpiOsPrintf ("[Uninitialized Package Element]\n");
                    }
                    else
                    {
                        AcpiExDoDebugObject (*SourceDesc->Reference.Where,
                            Level+4, 0);
                    }
                    break;

                default:

                    AcpiOsPrintf ("Unknown Reference object type %X\n",
                        ObjectDesc->Common.Type);
                    break;
                }
            }
        }
        break;

    default:

        AcpiOsPrintf ("(Descriptor %p)\n", SourceDesc);
        break;
    }

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC, "\n"));
    return_VOID;
}
#endif

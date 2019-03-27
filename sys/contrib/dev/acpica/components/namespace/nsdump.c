/******************************************************************************
 *
 * Module Name: nsdump - table dumping routines for debug
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
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acoutput.h>


#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nsdump")

/* Local prototypes */

#ifdef ACPI_OBSOLETE_FUNCTIONS
void
AcpiNsDumpRootDevices (
    void);

static ACPI_STATUS
AcpiNsDumpOneDevice (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue);
#endif


#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)

static ACPI_STATUS
AcpiNsDumpOneObjectPath (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
AcpiNsGetMaxDepth (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue);


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsPrintPathname
 *
 * PARAMETERS:  NumSegments         - Number of ACPI name segments
 *              Pathname            - The compressed (internal) path
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print an object's full namespace pathname
 *
 ******************************************************************************/

void
AcpiNsPrintPathname (
    UINT32                  NumSegments,
    const char              *Pathname)
{
    UINT32                  i;


    ACPI_FUNCTION_NAME (NsPrintPathname);


    /* Check if debug output enabled */

    if (!ACPI_IS_DEBUG_ENABLED (ACPI_LV_NAMES, ACPI_NAMESPACE))
    {
        return;
    }

    /* Print the entire name */

    ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "["));

    while (NumSegments)
    {
        for (i = 0; i < 4; i++)
        {
            isprint ((int) Pathname[i]) ?
                AcpiOsPrintf ("%c", Pathname[i]) :
                AcpiOsPrintf ("?");
        }

        Pathname += ACPI_NAME_SIZE;
        NumSegments--;
        if (NumSegments)
        {
            AcpiOsPrintf (".");
        }
    }

    AcpiOsPrintf ("]\n");
}


#ifdef ACPI_OBSOLETE_FUNCTIONS
/* Not used at this time, perhaps later */

/*******************************************************************************
 *
 * FUNCTION:    AcpiNsDumpPathname
 *
 * PARAMETERS:  Handle              - Object
 *              Msg                 - Prefix message
 *              Level               - Desired debug level
 *              Component           - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print an object's full namespace pathname
 *              Manages allocation/freeing of a pathname buffer
 *
 ******************************************************************************/

void
AcpiNsDumpPathname (
    ACPI_HANDLE             Handle,
    const char              *Msg,
    UINT32                  Level,
    UINT32                  Component)
{

    ACPI_FUNCTION_TRACE (NsDumpPathname);


    /* Do this only if the requested debug level and component are enabled */

    if (!ACPI_IS_DEBUG_ENABLED (Level, Component))
    {
        return_VOID;
    }

    /* Convert handle to a full pathname and print it (with supplied message) */

    AcpiNsPrintNodePathname (Handle, Msg);
    AcpiOsPrintf ("\n");
    return_VOID;
}
#endif


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsDumpOneObject
 *
 * PARAMETERS:  ObjHandle           - Node to be dumped
 *              Level               - Nesting level of the handle
 *              Context             - Passed into WalkNamespace
 *              ReturnValue         - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dump a single Node
 *              This procedure is a UserFunction called by AcpiNsWalkNamespace.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiNsDumpOneObject (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_WALK_INFO          *Info = (ACPI_WALK_INFO *) Context;
    ACPI_NAMESPACE_NODE     *ThisNode;
    ACPI_OPERAND_OBJECT     *ObjDesc = NULL;
    ACPI_OBJECT_TYPE        ObjType;
    ACPI_OBJECT_TYPE        Type;
    UINT32                  BytesToDump;
    UINT32                  DbgLevel;
    UINT32                  i;


    ACPI_FUNCTION_NAME (NsDumpOneObject);


    /* Is output enabled? */

    if (!(AcpiDbgLevel & Info->DebugLevel))
    {
        return (AE_OK);
    }

    if (!ObjHandle)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Null object handle\n"));
        return (AE_OK);
    }

    ThisNode = AcpiNsValidateHandle (ObjHandle);
    if (!ThisNode)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Invalid object handle %p\n",
            ObjHandle));
        return (AE_OK);
    }

    Type = ThisNode->Type;
    Info->Count++;

    /* Check if the owner matches */

    if ((Info->OwnerId != ACPI_OWNER_ID_MAX) &&
        (Info->OwnerId != ThisNode->OwnerId))
    {
        return (AE_OK);
    }

    if (!(Info->DisplayType & ACPI_DISPLAY_SHORT))
    {
        /* Indent the object according to the level */

        AcpiOsPrintf ("%2d%*s", (UINT32) Level - 1, (int) Level * 2, " ");

        /* Check the node type and name */

        if (Type > ACPI_TYPE_LOCAL_MAX)
        {
            ACPI_WARNING ((AE_INFO,
                "Invalid ACPI Object Type 0x%08X", Type));
        }

        AcpiOsPrintf ("%4.4s", AcpiUtGetNodeName (ThisNode));
    }

    /* Now we can print out the pertinent information */

    AcpiOsPrintf (" %-12s %p %2.2X ",
        AcpiUtGetTypeName (Type), ThisNode, ThisNode->OwnerId);

    DbgLevel = AcpiDbgLevel;
    AcpiDbgLevel = 0;
    ObjDesc = AcpiNsGetAttachedObject (ThisNode);
    AcpiDbgLevel = DbgLevel;

    /* Temp nodes are those nodes created by a control method */

    if (ThisNode->Flags & ANOBJ_TEMPORARY)
    {
        AcpiOsPrintf ("(T) ");
    }

    switch (Info->DisplayType & ACPI_DISPLAY_MASK)
    {
    case ACPI_DISPLAY_SUMMARY:

        if (!ObjDesc)
        {
            /* No attached object. Some types should always have an object */

            switch (Type)
            {
            case ACPI_TYPE_INTEGER:
            case ACPI_TYPE_PACKAGE:
            case ACPI_TYPE_BUFFER:
            case ACPI_TYPE_STRING:
            case ACPI_TYPE_METHOD:

                AcpiOsPrintf ("<No attached object>");
                break;

            default:

                break;
            }

            AcpiOsPrintf ("\n");
            return (AE_OK);
        }

        switch (Type)
        {
        case ACPI_TYPE_PROCESSOR:

            AcpiOsPrintf ("ID %02X Len %02X Addr %8.8X%8.8X\n",
                ObjDesc->Processor.ProcId, ObjDesc->Processor.Length,
                ACPI_FORMAT_UINT64 (ObjDesc->Processor.Address));
            break;

        case ACPI_TYPE_DEVICE:

            AcpiOsPrintf ("Notify Object: %p\n", ObjDesc);
            break;

        case ACPI_TYPE_METHOD:

            AcpiOsPrintf ("Args %X Len %.4X Aml %p\n",
                (UINT32) ObjDesc->Method.ParamCount,
                ObjDesc->Method.AmlLength, ObjDesc->Method.AmlStart);
            break;

        case ACPI_TYPE_INTEGER:

            AcpiOsPrintf ("= %8.8X%8.8X\n",
                ACPI_FORMAT_UINT64 (ObjDesc->Integer.Value));
            break;

        case ACPI_TYPE_PACKAGE:

            if (ObjDesc->Common.Flags & AOPOBJ_DATA_VALID)
            {
                AcpiOsPrintf ("Elements %.2X\n",
                    ObjDesc->Package.Count);
            }
            else
            {
                AcpiOsPrintf ("[Length not yet evaluated]\n");
            }
            break;

        case ACPI_TYPE_BUFFER:

            if (ObjDesc->Common.Flags & AOPOBJ_DATA_VALID)
            {
                AcpiOsPrintf ("Len %.2X",
                    ObjDesc->Buffer.Length);

                /* Dump some of the buffer */

                if (ObjDesc->Buffer.Length > 0)
                {
                    AcpiOsPrintf (" =");
                    for (i = 0; (i < ObjDesc->Buffer.Length && i < 12); i++)
                    {
                        AcpiOsPrintf (" %.2hX", ObjDesc->Buffer.Pointer[i]);
                    }
                }
                AcpiOsPrintf ("\n");
            }
            else
            {
                AcpiOsPrintf ("[Length not yet evaluated]\n");
            }
            break;

        case ACPI_TYPE_STRING:

            AcpiOsPrintf ("Len %.2X ", ObjDesc->String.Length);
            AcpiUtPrintString (ObjDesc->String.Pointer, 80);
            AcpiOsPrintf ("\n");
            break;

        case ACPI_TYPE_REGION:

            AcpiOsPrintf ("[%s]",
                AcpiUtGetRegionName (ObjDesc->Region.SpaceId));
            if (ObjDesc->Region.Flags & AOPOBJ_DATA_VALID)
            {
                AcpiOsPrintf (" Addr %8.8X%8.8X Len %.4X\n",
                    ACPI_FORMAT_UINT64 (ObjDesc->Region.Address),
                    ObjDesc->Region.Length);
            }
            else
            {
                AcpiOsPrintf (" [Address/Length not yet evaluated]\n");
            }
            break;

        case ACPI_TYPE_LOCAL_REFERENCE:

            AcpiOsPrintf ("[%s]\n", AcpiUtGetReferenceName (ObjDesc));
            break;

        case ACPI_TYPE_BUFFER_FIELD:

            if (ObjDesc->BufferField.BufferObj &&
                ObjDesc->BufferField.BufferObj->Buffer.Node)
            {
                AcpiOsPrintf ("Buf [%4.4s]",
                    AcpiUtGetNodeName (
                        ObjDesc->BufferField.BufferObj->Buffer.Node));
            }
            break;

        case ACPI_TYPE_LOCAL_REGION_FIELD:

            AcpiOsPrintf ("Rgn [%4.4s]",
                AcpiUtGetNodeName (
                    ObjDesc->CommonField.RegionObj->Region.Node));
            break;

        case ACPI_TYPE_LOCAL_BANK_FIELD:

            AcpiOsPrintf ("Rgn [%4.4s] Bnk [%4.4s]",
                AcpiUtGetNodeName (
                    ObjDesc->CommonField.RegionObj->Region.Node),
                AcpiUtGetNodeName (
                    ObjDesc->BankField.BankObj->CommonField.Node));
            break;

        case ACPI_TYPE_LOCAL_INDEX_FIELD:

            AcpiOsPrintf ("Idx [%4.4s] Dat [%4.4s]",
                AcpiUtGetNodeName (
                    ObjDesc->IndexField.IndexObj->CommonField.Node),
                AcpiUtGetNodeName (
                    ObjDesc->IndexField.DataObj->CommonField.Node));
            break;

        case ACPI_TYPE_LOCAL_ALIAS:
        case ACPI_TYPE_LOCAL_METHOD_ALIAS:

            AcpiOsPrintf ("Target %4.4s (%p)\n",
                AcpiUtGetNodeName (ObjDesc), ObjDesc);
            break;

        default:

            AcpiOsPrintf ("Object %p\n", ObjDesc);
            break;
        }

        /* Common field handling */

        switch (Type)
        {
        case ACPI_TYPE_BUFFER_FIELD:
        case ACPI_TYPE_LOCAL_REGION_FIELD:
        case ACPI_TYPE_LOCAL_BANK_FIELD:
        case ACPI_TYPE_LOCAL_INDEX_FIELD:

            AcpiOsPrintf (" Off %.3X Len %.2X Acc %.2hd\n",
                (ObjDesc->CommonField.BaseByteOffset * 8)
                    + ObjDesc->CommonField.StartFieldBitOffset,
                ObjDesc->CommonField.BitLength,
                ObjDesc->CommonField.AccessByteWidth);
            break;

        default:

            break;
        }
        break;

    case ACPI_DISPLAY_OBJECTS:

        AcpiOsPrintf ("O:%p", ObjDesc);
        if (!ObjDesc)
        {
            /* No attached object, we are done */

            AcpiOsPrintf ("\n");
            return (AE_OK);
        }

        AcpiOsPrintf ("(R%u)", ObjDesc->Common.ReferenceCount);

        switch (Type)
        {
        case ACPI_TYPE_METHOD:

            /* Name is a Method and its AML offset/length are set */

            AcpiOsPrintf (" M:%p-%X\n", ObjDesc->Method.AmlStart,
                ObjDesc->Method.AmlLength);
            break;

        case ACPI_TYPE_INTEGER:

            AcpiOsPrintf (" I:%8.8X8.8%X\n",
                ACPI_FORMAT_UINT64 (ObjDesc->Integer.Value));
            break;

        case ACPI_TYPE_STRING:

            AcpiOsPrintf (" S:%p-%X\n", ObjDesc->String.Pointer,
                ObjDesc->String.Length);
            break;

        case ACPI_TYPE_BUFFER:

            AcpiOsPrintf (" B:%p-%X\n", ObjDesc->Buffer.Pointer,
                ObjDesc->Buffer.Length);
            break;

        default:

            AcpiOsPrintf ("\n");
            break;
        }
        break;

    default:
        AcpiOsPrintf ("\n");
        break;
    }

    /* If debug turned off, done */

    if (!(AcpiDbgLevel & ACPI_LV_VALUES))
    {
        return (AE_OK);
    }

    /* If there is an attached object, display it */

    DbgLevel = AcpiDbgLevel;
    AcpiDbgLevel = 0;
    ObjDesc = AcpiNsGetAttachedObject (ThisNode);
    AcpiDbgLevel = DbgLevel;

    /* Dump attached objects */

    while (ObjDesc)
    {
        ObjType = ACPI_TYPE_INVALID;
        AcpiOsPrintf ("Attached Object %p: ", ObjDesc);

        /* Decode the type of attached object and dump the contents */

        switch (ACPI_GET_DESCRIPTOR_TYPE (ObjDesc))
        {
        case ACPI_DESC_TYPE_NAMED:

            AcpiOsPrintf ("(Ptr to Node)\n");
            BytesToDump = sizeof (ACPI_NAMESPACE_NODE);
            ACPI_DUMP_BUFFER (ObjDesc, BytesToDump);
            break;

        case ACPI_DESC_TYPE_OPERAND:

            ObjType = ObjDesc->Common.Type;

            if (ObjType > ACPI_TYPE_LOCAL_MAX)
            {
                AcpiOsPrintf (
                    "(Pointer to ACPI Object type %.2X [UNKNOWN])\n",
                    ObjType);

                BytesToDump = 32;
            }
            else
            {
                AcpiOsPrintf (
                    "(Pointer to ACPI Object type %.2X [%s])\n",
                    ObjType, AcpiUtGetTypeName (ObjType));

                BytesToDump = sizeof (ACPI_OPERAND_OBJECT);
            }

            ACPI_DUMP_BUFFER (ObjDesc, BytesToDump);
            break;

        default:

            break;
        }

        /* If value is NOT an internal object, we are done */

        if (ACPI_GET_DESCRIPTOR_TYPE (ObjDesc) != ACPI_DESC_TYPE_OPERAND)
        {
            goto Cleanup;
        }

        /* Valid object, get the pointer to next level, if any */

        switch (ObjType)
        {
        case ACPI_TYPE_BUFFER:
        case ACPI_TYPE_STRING:
            /*
             * NOTE: takes advantage of common fields between string/buffer
             */
            BytesToDump = ObjDesc->String.Length;
            ObjDesc = (void *) ObjDesc->String.Pointer;

            AcpiOsPrintf ("(Buffer/String pointer %p length %X)\n",
                ObjDesc, BytesToDump);
            ACPI_DUMP_BUFFER (ObjDesc, BytesToDump);
            goto Cleanup;

        case ACPI_TYPE_BUFFER_FIELD:

            ObjDesc = (ACPI_OPERAND_OBJECT *) ObjDesc->BufferField.BufferObj;
            break;

        case ACPI_TYPE_PACKAGE:

            ObjDesc = (void *) ObjDesc->Package.Elements;
            break;

        case ACPI_TYPE_METHOD:

            ObjDesc = (void *) ObjDesc->Method.AmlStart;
            break;

        case ACPI_TYPE_LOCAL_REGION_FIELD:

            ObjDesc = (void *) ObjDesc->Field.RegionObj;
            break;

        case ACPI_TYPE_LOCAL_BANK_FIELD:

            ObjDesc = (void *) ObjDesc->BankField.RegionObj;
            break;

        case ACPI_TYPE_LOCAL_INDEX_FIELD:

            ObjDesc = (void *) ObjDesc->IndexField.IndexObj;
            break;

        default:

            goto Cleanup;
        }

        ObjType = ACPI_TYPE_INVALID;   /* Terminate loop after next pass */
    }

Cleanup:
    AcpiOsPrintf ("\n");
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsDumpObjects
 *
 * PARAMETERS:  Type                - Object type to be dumped
 *              DisplayType         - 0 or ACPI_DISPLAY_SUMMARY
 *              MaxDepth            - Maximum depth of dump. Use ACPI_UINT32_MAX
 *                                    for an effectively unlimited depth.
 *              OwnerId             - Dump only objects owned by this ID. Use
 *                                    ACPI_UINT32_MAX to match all owners.
 *              StartHandle         - Where in namespace to start/end search
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump typed objects within the loaded namespace. Uses
 *              AcpiNsWalkNamespace in conjunction with AcpiNsDumpOneObject.
 *
 ******************************************************************************/

void
AcpiNsDumpObjects (
    ACPI_OBJECT_TYPE        Type,
    UINT8                   DisplayType,
    UINT32                  MaxDepth,
    ACPI_OWNER_ID           OwnerId,
    ACPI_HANDLE             StartHandle)
{
    ACPI_WALK_INFO          Info;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_ENTRY ();


    /*
     * Just lock the entire namespace for the duration of the dump.
     * We don't want any changes to the namespace during this time,
     * especially the temporary nodes since we are going to display
     * them also.
     */
    Status = AcpiUtAcquireMutex (ACPI_MTX_NAMESPACE);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could not acquire namespace mutex\n");
        return;
    }

    Info.Count = 0;
    Info.DebugLevel = ACPI_LV_TABLES;
    Info.OwnerId = OwnerId;
    Info.DisplayType = DisplayType;

    (void) AcpiNsWalkNamespace (Type, StartHandle, MaxDepth,
        ACPI_NS_WALK_NO_UNLOCK | ACPI_NS_WALK_TEMP_NODES,
        AcpiNsDumpOneObject, NULL, (void *) &Info, NULL);

    AcpiOsPrintf ("\nNamespace node count: %u\n\n", Info.Count);
    (void) AcpiUtReleaseMutex (ACPI_MTX_NAMESPACE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsDumpOneObjectPath, AcpiNsGetMaxDepth
 *
 * PARAMETERS:  ObjHandle           - Node to be dumped
 *              Level               - Nesting level of the handle
 *              Context             - Passed into WalkNamespace
 *              ReturnValue         - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dump the full pathname to a namespace object. AcpNsGetMaxDepth
 *              computes the maximum nesting depth in the namespace tree, in
 *              order to simplify formatting in AcpiNsDumpOneObjectPath.
 *              These procedures are UserFunctions called by AcpiNsWalkNamespace.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiNsDumpOneObjectPath (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue)
{
    UINT32                  MaxLevel = *((UINT32 *) Context);
    char                    *Pathname;
    ACPI_NAMESPACE_NODE     *Node;
    int                     PathIndent;


    if (!ObjHandle)
    {
        return (AE_OK);
    }

    Node = AcpiNsValidateHandle (ObjHandle);
    if (!Node)
    {
        /* Ignore bad node during namespace walk */

        return (AE_OK);
    }

    Pathname = AcpiNsGetNormalizedPathname (Node, TRUE);

    PathIndent = 1;
    if (Level <= MaxLevel)
    {
        PathIndent = MaxLevel - Level + 1;
    }

    AcpiOsPrintf ("%2d%*s%-12s%*s",
        Level, Level, " ", AcpiUtGetTypeName (Node->Type),
        PathIndent, " ");

    AcpiOsPrintf ("%s\n", &Pathname[1]);
    ACPI_FREE (Pathname);
    return (AE_OK);
}


static ACPI_STATUS
AcpiNsGetMaxDepth (
    ACPI_HANDLE             ObjHandle,
    UINT32                  Level,
    void                    *Context,
    void                    **ReturnValue)
{
    UINT32                  *MaxLevel = (UINT32 *) Context;


    if (Level > *MaxLevel)
    {
        *MaxLevel = Level;
    }
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsDumpObjectPaths
 *
 * PARAMETERS:  Type                - Object type to be dumped
 *              DisplayType         - 0 or ACPI_DISPLAY_SUMMARY
 *              MaxDepth            - Maximum depth of dump. Use ACPI_UINT32_MAX
 *                                    for an effectively unlimited depth.
 *              OwnerId             - Dump only objects owned by this ID. Use
 *                                    ACPI_UINT32_MAX to match all owners.
 *              StartHandle         - Where in namespace to start/end search
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump full object pathnames within the loaded namespace. Uses
 *              AcpiNsWalkNamespace in conjunction with AcpiNsDumpOneObjectPath.
 *
 ******************************************************************************/

void
AcpiNsDumpObjectPaths (
    ACPI_OBJECT_TYPE        Type,
    UINT8                   DisplayType,
    UINT32                  MaxDepth,
    ACPI_OWNER_ID           OwnerId,
    ACPI_HANDLE             StartHandle)
{
    ACPI_STATUS             Status;
    UINT32                  MaxLevel = 0;


    ACPI_FUNCTION_ENTRY ();


    /*
     * Just lock the entire namespace for the duration of the dump.
     * We don't want any changes to the namespace during this time,
     * especially the temporary nodes since we are going to display
     * them also.
     */
    Status = AcpiUtAcquireMutex (ACPI_MTX_NAMESPACE);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could not acquire namespace mutex\n");
        return;
    }

    /* Get the max depth of the namespace tree, for formatting later */

    (void) AcpiNsWalkNamespace (Type, StartHandle, MaxDepth,
        ACPI_NS_WALK_NO_UNLOCK | ACPI_NS_WALK_TEMP_NODES,
        AcpiNsGetMaxDepth, NULL, (void *) &MaxLevel, NULL);

    /* Now dump the entire namespace */

    (void) AcpiNsWalkNamespace (Type, StartHandle, MaxDepth,
        ACPI_NS_WALK_NO_UNLOCK | ACPI_NS_WALK_TEMP_NODES,
        AcpiNsDumpOneObjectPath, NULL, (void *) &MaxLevel, NULL);

    (void) AcpiUtReleaseMutex (ACPI_MTX_NAMESPACE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiNsDumpEntry
 *
 * PARAMETERS:  Handle              - Node to be dumped
 *              DebugLevel          - Output level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump a single Node
 *
 ******************************************************************************/

void
AcpiNsDumpEntry (
    ACPI_HANDLE             Handle,
    UINT32                  DebugLevel)
{
    ACPI_WALK_INFO          Info;


    ACPI_FUNCTION_ENTRY ();


    Info.DebugLevel = DebugLevel;
    Info.OwnerId = ACPI_OWNER_ID_MAX;
    Info.DisplayType = ACPI_DISPLAY_SUMMARY;

    (void) AcpiNsDumpOneObject (Handle, 1, &Info, NULL);
}


#ifdef ACPI_ASL_COMPILER
/*******************************************************************************
 *
 * FUNCTION:    AcpiNsDumpTables
 *
 * PARAMETERS:  SearchBase          - Root of subtree to be dumped, or
 *                                    NS_ALL to dump the entire namespace
 *              MaxDepth            - Maximum depth of dump. Use INT_MAX
 *                                    for an effectively unlimited depth.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the name space, or a portion of it.
 *
 ******************************************************************************/

void
AcpiNsDumpTables (
    ACPI_HANDLE             SearchBase,
    UINT32                  MaxDepth)
{
    ACPI_HANDLE             SearchHandle = SearchBase;


    ACPI_FUNCTION_TRACE (NsDumpTables);


    if (!AcpiGbl_RootNode)
    {
        /*
         * If the name space has not been initialized,
         * there is nothing to dump.
         */
        ACPI_DEBUG_PRINT ((ACPI_DB_TABLES,
            "namespace not initialized!\n"));
        return_VOID;
    }

    if (ACPI_NS_ALL == SearchBase)
    {
        /* Entire namespace */

        SearchHandle = AcpiGbl_RootNode;
        ACPI_DEBUG_PRINT ((ACPI_DB_TABLES, "\\\n"));
    }

    AcpiNsDumpObjects (ACPI_TYPE_ANY, ACPI_DISPLAY_OBJECTS, MaxDepth,
        ACPI_OWNER_ID_MAX, SearchHandle);
    return_VOID;
}
#endif
#endif

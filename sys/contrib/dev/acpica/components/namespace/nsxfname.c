/******************************************************************************
 *
 * Module Name: nsxfname - Public interfaces to the ACPI subsystem
 *                         ACPI Namespace oriented interfaces
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

#define EXPORT_ACPI_INTERFACES

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/amlcode.h>


#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("nsxfname")

/* Local prototypes */

static char *
AcpiNsCopyDeviceId (
    ACPI_PNP_DEVICE_ID      *Dest,
    ACPI_PNP_DEVICE_ID      *Source,
    char                    *StringArea);


/******************************************************************************
 *
 * FUNCTION:    AcpiGetHandle
 *
 * PARAMETERS:  Parent          - Object to search under (search scope).
 *              Pathname        - Pointer to an asciiz string containing the
 *                                name
 *              RetHandle       - Where the return handle is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This routine will search for a caller specified name in the
 *              name space. The caller can restrict the search region by
 *              specifying a non NULL parent. The parent value is itself a
 *              namespace handle.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiGetHandle (
    ACPI_HANDLE             Parent,
    ACPI_STRING             Pathname,
    ACPI_HANDLE             *RetHandle)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node = NULL;
    ACPI_NAMESPACE_NODE     *PrefixNode = NULL;


    ACPI_FUNCTION_ENTRY ();


    /* Parameter Validation */

    if (!RetHandle || !Pathname)
    {
        return (AE_BAD_PARAMETER);
    }

    /* Convert a parent handle to a prefix node */

    if (Parent)
    {
        PrefixNode = AcpiNsValidateHandle (Parent);
        if (!PrefixNode)
        {
            return (AE_BAD_PARAMETER);
        }
    }

    /*
     * Valid cases are:
     * 1) Fully qualified pathname
     * 2) Parent + Relative pathname
     *
     * Error for <null Parent + relative path>
     */
    if (ACPI_IS_ROOT_PREFIX (Pathname[0]))
    {
        /* Pathname is fully qualified (starts with '\') */

        /* Special case for root-only, since we can't search for it */

        if (!strcmp (Pathname, ACPI_NS_ROOT_PATH))
        {
            *RetHandle = ACPI_CAST_PTR (ACPI_HANDLE, AcpiGbl_RootNode);
            return (AE_OK);
        }
    }
    else if (!PrefixNode)
    {
        /* Relative path with null prefix is disallowed */

        return (AE_BAD_PARAMETER);
    }

    /* Find the Node and convert to a handle */

    Status = AcpiNsGetNode (PrefixNode, Pathname, ACPI_NS_NO_UPSEARCH, &Node);
    if (ACPI_SUCCESS (Status))
    {
        *RetHandle = ACPI_CAST_PTR (ACPI_HANDLE, Node);
    }

    return (Status);
}

ACPI_EXPORT_SYMBOL (AcpiGetHandle)


/******************************************************************************
 *
 * FUNCTION:    AcpiGetName
 *
 * PARAMETERS:  Handle          - Handle to be converted to a pathname
 *              NameType        - Full pathname or single segment
 *              Buffer          - Buffer for returned path
 *
 * RETURN:      Pointer to a string containing the fully qualified Name.
 *
 * DESCRIPTION: This routine returns the fully qualified name associated with
 *              the Handle parameter. This and the AcpiPathnameToHandle are
 *              complementary functions.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiGetName (
    ACPI_HANDLE             Handle,
    UINT32                  NameType,
    ACPI_BUFFER             *Buffer)
{
    ACPI_STATUS             Status;


    /* Parameter validation */

    if (NameType > ACPI_NAME_TYPE_MAX)
    {
        return (AE_BAD_PARAMETER);
    }

    Status = AcpiUtValidateBuffer (Buffer);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /*
     * Wants the single segment ACPI name.
     * Validate handle and convert to a namespace Node
     */
    Status = AcpiUtAcquireMutex (ACPI_MTX_NAMESPACE);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    if (NameType == ACPI_FULL_PATHNAME ||
        NameType == ACPI_FULL_PATHNAME_NO_TRAILING)
    {
        /* Get the full pathname (From the namespace root) */

        Status = AcpiNsHandleToPathname (Handle, Buffer,
            NameType == ACPI_FULL_PATHNAME ? FALSE : TRUE);
    }
    else
    {
        /* Get the single name */

        Status = AcpiNsHandleToName (Handle, Buffer);
    }

    (void) AcpiUtReleaseMutex (ACPI_MTX_NAMESPACE);
    return (Status);
}

ACPI_EXPORT_SYMBOL (AcpiGetName)


/******************************************************************************
 *
 * FUNCTION:    AcpiNsCopyDeviceId
 *
 * PARAMETERS:  Dest                - Pointer to the destination PNP_DEVICE_ID
 *              Source              - Pointer to the source PNP_DEVICE_ID
 *              StringArea          - Pointer to where to copy the dest string
 *
 * RETURN:      Pointer to the next string area
 *
 * DESCRIPTION: Copy a single PNP_DEVICE_ID, including the string data.
 *
 ******************************************************************************/

static char *
AcpiNsCopyDeviceId (
    ACPI_PNP_DEVICE_ID      *Dest,
    ACPI_PNP_DEVICE_ID      *Source,
    char                    *StringArea)
{
    /* Create the destination PNP_DEVICE_ID */

    Dest->String = StringArea;
    Dest->Length = Source->Length;

    /* Copy actual string and return a pointer to the next string area */

    memcpy (StringArea, Source->String, Source->Length);
    return (StringArea + Source->Length);
}


/******************************************************************************
 *
 * FUNCTION:    AcpiGetObjectInfo
 *
 * PARAMETERS:  Handle              - Object Handle
 *              ReturnBuffer        - Where the info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Returns information about an object as gleaned from the
 *              namespace node and possibly by running several standard
 *              control methods (Such as in the case of a device.)
 *
 * For Device and Processor objects, run the Device _HID, _UID, _CID,
 * _CLS, _ADR, _SxW, and _SxD methods.
 *
 * Note: Allocates the return buffer, must be freed by the caller.
 *
 * Note: This interface is intended to be used during the initial device
 * discovery namespace traversal. Therefore, no complex methods can be
 * executed, especially those that access operation regions. Therefore, do
 * not add any additional methods that could cause problems in this area.
 * Because of this reason support for the following methods has been removed:
 * 1) _SUB method was removed (11/2015)
 * 2) _STA method was removed (02/2018)
 *
 ******************************************************************************/

ACPI_STATUS
AcpiGetObjectInfo (
    ACPI_HANDLE             Handle,
    ACPI_DEVICE_INFO        **ReturnBuffer)
{
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_DEVICE_INFO        *Info;
    ACPI_PNP_DEVICE_ID_LIST *CidList = NULL;
    ACPI_PNP_DEVICE_ID      *Hid = NULL;
    ACPI_PNP_DEVICE_ID      *Uid = NULL;
    ACPI_PNP_DEVICE_ID      *Cls = NULL;
    char                    *NextIdString;
    ACPI_OBJECT_TYPE        Type;
    ACPI_NAME               Name;
    UINT8                   ParamCount= 0;
    UINT16                  Valid = 0;
    UINT32                  InfoSize;
    UINT32                  i;
    ACPI_STATUS             Status;


    /* Parameter validation */

    if (!Handle || !ReturnBuffer)
    {
        return (AE_BAD_PARAMETER);
    }

    Status = AcpiUtAcquireMutex (ACPI_MTX_NAMESPACE);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    Node = AcpiNsValidateHandle (Handle);
    if (!Node)
    {
        (void) AcpiUtReleaseMutex (ACPI_MTX_NAMESPACE);
        return (AE_BAD_PARAMETER);
    }

    /* Get the namespace node data while the namespace is locked */

    InfoSize = sizeof (ACPI_DEVICE_INFO);
    Type = Node->Type;
    Name = Node->Name.Integer;

    if (Node->Type == ACPI_TYPE_METHOD)
    {
        ParamCount = Node->Object->Method.ParamCount;
    }

    Status = AcpiUtReleaseMutex (ACPI_MTX_NAMESPACE);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    if ((Type == ACPI_TYPE_DEVICE) ||
        (Type == ACPI_TYPE_PROCESSOR))
    {
        /*
         * Get extra info for ACPI Device/Processor objects only:
         * Run the Device _HID, _UID, _CLS, and _CID methods.
         *
         * Note: none of these methods are required, so they may or may
         * not be present for this device. The Info->Valid bitfield is used
         * to indicate which methods were found and run successfully.
         */

        /* Execute the Device._HID method */

        Status = AcpiUtExecute_HID (Node, &Hid);
        if (ACPI_SUCCESS (Status))
        {
            InfoSize += Hid->Length;
            Valid |= ACPI_VALID_HID;
        }

        /* Execute the Device._UID method */

        Status = AcpiUtExecute_UID (Node, &Uid);
        if (ACPI_SUCCESS (Status))
        {
            InfoSize += Uid->Length;
            Valid |= ACPI_VALID_UID;
        }

        /* Execute the Device._CID method */

        Status = AcpiUtExecute_CID (Node, &CidList);
        if (ACPI_SUCCESS (Status))
        {
            /* Add size of CID strings and CID pointer array */

            InfoSize += (CidList->ListSize - sizeof (ACPI_PNP_DEVICE_ID_LIST));
            Valid |= ACPI_VALID_CID;
        }

        /* Execute the Device._CLS method */

        Status = AcpiUtExecute_CLS (Node, &Cls);
        if (ACPI_SUCCESS (Status))
        {
            InfoSize += Cls->Length;
            Valid |= ACPI_VALID_CLS;
        }
    }

    /*
     * Now that we have the variable-length data, we can allocate the
     * return buffer
     */
    Info = ACPI_ALLOCATE_ZEROED (InfoSize);
    if (!Info)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    /* Get the fixed-length data */

    if ((Type == ACPI_TYPE_DEVICE) ||
        (Type == ACPI_TYPE_PROCESSOR))
    {
        /*
         * Get extra info for ACPI Device/Processor objects only:
         * Run the _ADR and, SxW, and _SxD methods.
         *
         * Notes: none of these methods are required, so they may or may
         * not be present for this device. The Info->Valid bitfield is used
         * to indicate which methods were found and run successfully.
         */

        /* Execute the Device._ADR method */

        Status = AcpiUtEvaluateNumericObject (METHOD_NAME__ADR, Node,
            &Info->Address);
        if (ACPI_SUCCESS (Status))
        {
            Valid |= ACPI_VALID_ADR;
        }

        /* Execute the Device._SxW methods */

        Status = AcpiUtExecutePowerMethods (Node,
            AcpiGbl_LowestDstateNames, ACPI_NUM_SxW_METHODS,
            Info->LowestDstates);
        if (ACPI_SUCCESS (Status))
        {
            Valid |= ACPI_VALID_SXWS;
        }

        /* Execute the Device._SxD methods */

        Status = AcpiUtExecutePowerMethods (Node,
            AcpiGbl_HighestDstateNames, ACPI_NUM_SxD_METHODS,
            Info->HighestDstates);
        if (ACPI_SUCCESS (Status))
        {
            Valid |= ACPI_VALID_SXDS;
        }
    }

    /*
     * Create a pointer to the string area of the return buffer.
     * Point to the end of the base ACPI_DEVICE_INFO structure.
     */
    NextIdString = ACPI_CAST_PTR (char, Info->CompatibleIdList.Ids);
    if (CidList)
    {
        /* Point past the CID PNP_DEVICE_ID array */

        NextIdString += ((ACPI_SIZE) CidList->Count * sizeof (ACPI_PNP_DEVICE_ID));
    }

    /*
     * Copy the HID, UID, and CIDs to the return buffer. The variable-length
     * strings are copied to the reserved area at the end of the buffer.
     *
     * For HID and CID, check if the ID is a PCI Root Bridge.
     */
    if (Hid)
    {
        NextIdString = AcpiNsCopyDeviceId (&Info->HardwareId,
            Hid, NextIdString);

        if (AcpiUtIsPciRootBridge (Hid->String))
        {
            Info->Flags |= ACPI_PCI_ROOT_BRIDGE;
        }
    }

    if (Uid)
    {
        NextIdString = AcpiNsCopyDeviceId (&Info->UniqueId,
            Uid, NextIdString);
    }

    if (CidList)
    {
        Info->CompatibleIdList.Count = CidList->Count;
        Info->CompatibleIdList.ListSize = CidList->ListSize;

        /* Copy each CID */

        for (i = 0; i < CidList->Count; i++)
        {
            NextIdString = AcpiNsCopyDeviceId (&Info->CompatibleIdList.Ids[i],
                &CidList->Ids[i], NextIdString);

            if (AcpiUtIsPciRootBridge (CidList->Ids[i].String))
            {
                Info->Flags |= ACPI_PCI_ROOT_BRIDGE;
            }
        }
    }

    if (Cls)
    {
        NextIdString = AcpiNsCopyDeviceId (&Info->ClassCode,
            Cls, NextIdString);
    }

    /* Copy the fixed-length data */

    Info->InfoSize = InfoSize;
    Info->Type = Type;
    Info->Name = Name;
    Info->ParamCount = ParamCount;
    Info->Valid = Valid;

    *ReturnBuffer = Info;
    Status = AE_OK;


Cleanup:
    if (Hid)
    {
        ACPI_FREE (Hid);
    }
    if (Uid)
    {
        ACPI_FREE (Uid);
    }
    if (CidList)
    {
        ACPI_FREE (CidList);
    }
    if (Cls)
    {
        ACPI_FREE (Cls);
    }
    return (Status);
}

ACPI_EXPORT_SYMBOL (AcpiGetObjectInfo)


/******************************************************************************
 *
 * FUNCTION:    AcpiInstallMethod
 *
 * PARAMETERS:  Buffer         - An ACPI table containing one control method
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a control method into the namespace. If the method
 *              name already exists in the namespace, it is overwritten. The
 *              input buffer must contain a valid DSDT or SSDT containing a
 *              single control method.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiInstallMethod (
    UINT8                   *Buffer)
{
    ACPI_TABLE_HEADER       *Table = ACPI_CAST_PTR (ACPI_TABLE_HEADER, Buffer);
    UINT8                   *AmlBuffer;
    UINT8                   *AmlStart;
    char                    *Path;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_OPERAND_OBJECT     *MethodObj;
    ACPI_PARSE_STATE        ParserState;
    UINT32                  AmlLength;
    UINT16                  Opcode;
    UINT8                   MethodFlags;
    ACPI_STATUS             Status;


    /* Parameter validation */

    if (!Buffer)
    {
        return (AE_BAD_PARAMETER);
    }

    /* Table must be a DSDT or SSDT */

    if (!ACPI_COMPARE_NAME (Table->Signature, ACPI_SIG_DSDT) &&
        !ACPI_COMPARE_NAME (Table->Signature, ACPI_SIG_SSDT))
    {
        return (AE_BAD_HEADER);
    }

    /* First AML opcode in the table must be a control method */

    ParserState.Aml = Buffer + sizeof (ACPI_TABLE_HEADER);
    Opcode = AcpiPsPeekOpcode (&ParserState);
    if (Opcode != AML_METHOD_OP)
    {
        return (AE_BAD_PARAMETER);
    }

    /* Extract method information from the raw AML */

    ParserState.Aml += AcpiPsGetOpcodeSize (Opcode);
    ParserState.PkgEnd = AcpiPsGetNextPackageEnd (&ParserState);
    Path = AcpiPsGetNextNamestring (&ParserState);

    MethodFlags = *ParserState.Aml++;
    AmlStart = ParserState.Aml;
    AmlLength = ACPI_PTR_DIFF (ParserState.PkgEnd, AmlStart);

    /*
     * Allocate resources up-front. We don't want to have to delete a new
     * node from the namespace if we cannot allocate memory.
     */
    AmlBuffer = ACPI_ALLOCATE (AmlLength);
    if (!AmlBuffer)
    {
        return (AE_NO_MEMORY);
    }

    MethodObj = AcpiUtCreateInternalObject (ACPI_TYPE_METHOD);
    if (!MethodObj)
    {
        ACPI_FREE (AmlBuffer);
        return (AE_NO_MEMORY);
    }

    /* Lock namespace for AcpiNsLookup, we may be creating a new node */

    Status = AcpiUtAcquireMutex (ACPI_MTX_NAMESPACE);
    if (ACPI_FAILURE (Status))
    {
        goto ErrorExit;
    }

    /* The lookup either returns an existing node or creates a new one */

    Status = AcpiNsLookup (NULL, Path, ACPI_TYPE_METHOD, ACPI_IMODE_LOAD_PASS1,
        ACPI_NS_DONT_OPEN_SCOPE | ACPI_NS_ERROR_IF_FOUND, NULL, &Node);

    (void) AcpiUtReleaseMutex (ACPI_MTX_NAMESPACE);

    if (ACPI_FAILURE (Status)) /* NsLookup */
    {
        if (Status != AE_ALREADY_EXISTS)
        {
            goto ErrorExit;
        }

        /* Node existed previously, make sure it is a method node */

        if (Node->Type != ACPI_TYPE_METHOD)
        {
            Status = AE_TYPE;
            goto ErrorExit;
        }
    }

    /* Copy the method AML to the local buffer */

    memcpy (AmlBuffer, AmlStart, AmlLength);

    /* Initialize the method object with the new method's information */

    MethodObj->Method.AmlStart = AmlBuffer;
    MethodObj->Method.AmlLength = AmlLength;

    MethodObj->Method.ParamCount = (UINT8)
        (MethodFlags & AML_METHOD_ARG_COUNT);

    if (MethodFlags & AML_METHOD_SERIALIZED)
    {
        MethodObj->Method.InfoFlags = ACPI_METHOD_SERIALIZED;

        MethodObj->Method.SyncLevel = (UINT8)
            ((MethodFlags & AML_METHOD_SYNC_LEVEL) >> 4);
    }

    /*
     * Now that it is complete, we can attach the new method object to
     * the method Node (detaches/deletes any existing object)
     */
    Status = AcpiNsAttachObject (Node, MethodObj, ACPI_TYPE_METHOD);

    /*
     * Flag indicates AML buffer is dynamic, must be deleted later.
     * Must be set only after attach above.
     */
    Node->Flags |= ANOBJ_ALLOCATED_BUFFER;

    /* Remove local reference to the method object */

    AcpiUtRemoveReference (MethodObj);
    return (Status);


ErrorExit:

    ACPI_FREE (AmlBuffer);
    ACPI_FREE (MethodObj);
    return (Status);
}

ACPI_EXPORT_SYMBOL (AcpiInstallMethod)

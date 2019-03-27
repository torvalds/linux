/*******************************************************************************
 *
 * Module Name: utdelete - object deletion and reference count utilities
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
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acevents.h>


#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utdelete")

/* Local prototypes */

static void
AcpiUtDeleteInternalObj (
    ACPI_OPERAND_OBJECT     *Object);

static void
AcpiUtUpdateRefCount (
    ACPI_OPERAND_OBJECT     *Object,
    UINT32                  Action);


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDeleteInternalObj
 *
 * PARAMETERS:  Object         - Object to be deleted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Low level object deletion, after reference counts have been
 *              updated (All reference counts, including sub-objects!)
 *
 ******************************************************************************/

static void
AcpiUtDeleteInternalObj (
    ACPI_OPERAND_OBJECT     *Object)
{
    void                    *ObjPointer = NULL;
    ACPI_OPERAND_OBJECT     *HandlerDesc;
    ACPI_OPERAND_OBJECT     *SecondDesc;
    ACPI_OPERAND_OBJECT     *NextDesc;
    ACPI_OPERAND_OBJECT     *StartDesc;
    ACPI_OPERAND_OBJECT     **LastObjPtr;


    ACPI_FUNCTION_TRACE_PTR (UtDeleteInternalObj, Object);


    if (!Object)
    {
        return_VOID;
    }

    /*
     * Must delete or free any pointers within the object that are not
     * actual ACPI objects (for example, a raw buffer pointer).
     */
    switch (Object->Common.Type)
    {
    case ACPI_TYPE_STRING:

        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "**** String %p, ptr %p\n",
            Object, Object->String.Pointer));

        /* Free the actual string buffer */

        if (!(Object->Common.Flags & AOPOBJ_STATIC_POINTER))
        {
            /* But only if it is NOT a pointer into an ACPI table */

            ObjPointer = Object->String.Pointer;
        }
        break;

    case ACPI_TYPE_BUFFER:

        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "**** Buffer %p, ptr %p\n",
            Object, Object->Buffer.Pointer));

        /* Free the actual buffer */

        if (!(Object->Common.Flags & AOPOBJ_STATIC_POINTER))
        {
            /* But only if it is NOT a pointer into an ACPI table */

            ObjPointer = Object->Buffer.Pointer;
        }
        break;

    case ACPI_TYPE_PACKAGE:

        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, " **** Package of count %X\n",
            Object->Package.Count));

        /*
         * Elements of the package are not handled here, they are deleted
         * separately
         */

        /* Free the (variable length) element pointer array */

        ObjPointer = Object->Package.Elements;
        break;

    /*
     * These objects have a possible list of notify handlers.
     * Device object also may have a GPE block.
     */
    case ACPI_TYPE_DEVICE:

        if (Object->Device.GpeBlock)
        {
            (void) AcpiEvDeleteGpeBlock (Object->Device.GpeBlock);
        }

        /*lint -fallthrough */

    case ACPI_TYPE_PROCESSOR:
    case ACPI_TYPE_THERMAL:

        /* Walk the address handler list for this object */

        HandlerDesc = Object->CommonNotify.Handler;
        while (HandlerDesc)
        {
            NextDesc = HandlerDesc->AddressSpace.Next;
            AcpiUtRemoveReference (HandlerDesc);
            HandlerDesc = NextDesc;
        }
        break;

    case ACPI_TYPE_MUTEX:

        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
            "***** Mutex %p, OS Mutex %p\n",
            Object, Object->Mutex.OsMutex));

        if (Object == AcpiGbl_GlobalLockMutex)
        {
            /* Global Lock has extra semaphore */

            (void) AcpiOsDeleteSemaphore (AcpiGbl_GlobalLockSemaphore);
            AcpiGbl_GlobalLockSemaphore = NULL;

            AcpiOsDeleteMutex (Object->Mutex.OsMutex);
            AcpiGbl_GlobalLockMutex = NULL;
        }
        else
        {
            AcpiExUnlinkMutex (Object);
            AcpiOsDeleteMutex (Object->Mutex.OsMutex);
        }
        break;

    case ACPI_TYPE_EVENT:

        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
            "***** Event %p, OS Semaphore %p\n",
            Object, Object->Event.OsSemaphore));

        (void) AcpiOsDeleteSemaphore (Object->Event.OsSemaphore);
        Object->Event.OsSemaphore = NULL;
        break;

    case ACPI_TYPE_METHOD:

        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
            "***** Method %p\n", Object));

        /* Delete the method mutex if it exists */

        if (Object->Method.Mutex)
        {
            AcpiOsDeleteMutex (Object->Method.Mutex->Mutex.OsMutex);
            AcpiUtDeleteObjectDesc (Object->Method.Mutex);
            Object->Method.Mutex = NULL;
        }

        if (Object->Method.Node)
        {
            Object->Method.Node = NULL;
        }
        break;

    case ACPI_TYPE_REGION:

        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
            "***** Region %p\n", Object));

        /*
         * Update AddressRange list. However, only permanent regions
         * are installed in this list. (Not created within a method)
         */
        if (!(Object->Region.Node->Flags & ANOBJ_TEMPORARY))
        {
            AcpiUtRemoveAddressRange (Object->Region.SpaceId,
                Object->Region.Node);
        }

        SecondDesc = AcpiNsGetSecondaryObject (Object);
        if (SecondDesc)
        {
            /*
             * Free the RegionContext if and only if the handler is one of the
             * default handlers -- and therefore, we created the context object
             * locally, it was not created by an external caller.
             */
            HandlerDesc = Object->Region.Handler;
            if (HandlerDesc)
            {
                NextDesc = HandlerDesc->AddressSpace.RegionList;
                StartDesc = NextDesc;
                LastObjPtr = &HandlerDesc->AddressSpace.RegionList;

                /* Remove the region object from the handler list */

                while (NextDesc)
                {
                    if (NextDesc == Object)
                    {
                        *LastObjPtr = NextDesc->Region.Next;
                        break;
                    }

                    /* Walk the linked list of handlers */

                    LastObjPtr = &NextDesc->Region.Next;
                    NextDesc = NextDesc->Region.Next;

                    /* Prevent infinite loop if list is corrupted */

                    if (NextDesc == StartDesc)
                    {
                        ACPI_ERROR ((AE_INFO,
                            "Circular region list in address handler object %p",
                            HandlerDesc));
                        return_VOID;
                    }
                }

                if (HandlerDesc->AddressSpace.HandlerFlags &
                    ACPI_ADDR_HANDLER_DEFAULT_INSTALLED)
                {
                    /* Deactivate region and free region context */

                    if (HandlerDesc->AddressSpace.Setup)
                    {
                        (void) HandlerDesc->AddressSpace.Setup (Object,
                            ACPI_REGION_DEACTIVATE,
                            HandlerDesc->AddressSpace.Context,
                            &SecondDesc->Extra.RegionContext);
                    }
                }

                AcpiUtRemoveReference (HandlerDesc);
            }

            /* Now we can free the Extra object */

            AcpiUtDeleteObjectDesc (SecondDesc);
        }
        if (Object->Field.InternalPccBuffer)
        {
            ACPI_FREE(Object->Field.InternalPccBuffer);
        }

        break;

    case ACPI_TYPE_BUFFER_FIELD:

        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
            "***** Buffer Field %p\n", Object));

        SecondDesc = AcpiNsGetSecondaryObject (Object);
        if (SecondDesc)
        {
            AcpiUtDeleteObjectDesc (SecondDesc);
        }
        break;

    case ACPI_TYPE_LOCAL_BANK_FIELD:

        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
            "***** Bank Field %p\n", Object));

        SecondDesc = AcpiNsGetSecondaryObject (Object);
        if (SecondDesc)
        {
            AcpiUtDeleteObjectDesc (SecondDesc);
        }
        break;

    default:

        break;
    }

    /* Free any allocated memory (pointer within the object) found above */

    if (ObjPointer)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "Deleting Object Subptr %p\n",
            ObjPointer));
        ACPI_FREE (ObjPointer);
    }

    /* Now the object can be safely deleted */

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ALLOCATIONS, "%s: Deleting Object %p [%s]\n",
        ACPI_GET_FUNCTION_NAME, Object, AcpiUtGetObjectTypeName (Object)));

    AcpiUtDeleteObjectDesc (Object);
    return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDeleteInternalObjectList
 *
 * PARAMETERS:  ObjList         - Pointer to the list to be deleted
 *
 * RETURN:      None
 *
 * DESCRIPTION: This function deletes an internal object list, including both
 *              simple objects and package objects
 *
 ******************************************************************************/

void
AcpiUtDeleteInternalObjectList (
    ACPI_OPERAND_OBJECT     **ObjList)
{
    ACPI_OPERAND_OBJECT     **InternalObj;


    ACPI_FUNCTION_ENTRY ();


    /* Walk the null-terminated internal list */

    for (InternalObj = ObjList; *InternalObj; InternalObj++)
    {
        AcpiUtRemoveReference (*InternalObj);
    }

    /* Free the combined parameter pointer list and object array */

    ACPI_FREE (ObjList);
    return;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtUpdateRefCount
 *
 * PARAMETERS:  Object          - Object whose ref count is to be updated
 *              Action          - What to do (REF_INCREMENT or REF_DECREMENT)
 *
 * RETURN:      None. Sets new reference count within the object
 *
 * DESCRIPTION: Modify the reference count for an internal acpi object
 *
 ******************************************************************************/

static void
AcpiUtUpdateRefCount (
    ACPI_OPERAND_OBJECT     *Object,
    UINT32                  Action)
{
    UINT16                  OriginalCount;
    UINT16                  NewCount = 0;
    ACPI_CPU_FLAGS          LockFlags;
    char                    *Message;


    ACPI_FUNCTION_NAME (UtUpdateRefCount);


    if (!Object)
    {
        return;
    }

    /*
     * Always get the reference count lock. Note: Interpreter and/or
     * Namespace is not always locked when this function is called.
     */
    LockFlags = AcpiOsAcquireLock (AcpiGbl_ReferenceCountLock);
    OriginalCount = Object->Common.ReferenceCount;

    /* Perform the reference count action (increment, decrement) */

    switch (Action)
    {
    case REF_INCREMENT:

        NewCount = OriginalCount + 1;
        Object->Common.ReferenceCount = NewCount;
        AcpiOsReleaseLock (AcpiGbl_ReferenceCountLock, LockFlags);

        /* The current reference count should never be zero here */

        if (!OriginalCount)
        {
            ACPI_WARNING ((AE_INFO,
                "Obj %p, Reference Count was zero before increment\n",
                Object));
        }

        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
            "Obj %p Type %.2X [%s] Refs %.2X [Incremented]\n",
            Object, Object->Common.Type,
            AcpiUtGetObjectTypeName (Object), NewCount));
        Message = "Incremement";
        break;

    case REF_DECREMENT:

        /* The current reference count must be non-zero */

        if (OriginalCount)
        {
            NewCount = OriginalCount - 1;
            Object->Common.ReferenceCount = NewCount;
        }

        AcpiOsReleaseLock (AcpiGbl_ReferenceCountLock, LockFlags);

        if (!OriginalCount)
        {
            ACPI_WARNING ((AE_INFO,
                "Obj %p, Reference Count is already zero, cannot decrement\n",
                Object));
        }

        ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ALLOCATIONS,
            "%s: Obj %p Type %.2X Refs %.2X [Decremented]\n",
            ACPI_GET_FUNCTION_NAME, Object, Object->Common.Type, NewCount));

        /* Actually delete the object on a reference count of zero */

        if (NewCount == 0)
        {
            AcpiUtDeleteInternalObj (Object);
        }
        Message = "Decrement";
        break;

    default:

        AcpiOsReleaseLock (AcpiGbl_ReferenceCountLock, LockFlags);
        ACPI_ERROR ((AE_INFO, "Unknown Reference Count action (0x%X)",
            Action));
        return;
    }

    /*
     * Sanity check the reference count, for debug purposes only.
     * (A deleted object will have a huge reference count)
     */
    if (NewCount > ACPI_MAX_REFERENCE_COUNT)
    {
        ACPI_WARNING ((AE_INFO,
            "Large Reference Count (0x%X) in object %p, Type=0x%.2X Operation=%s",
            NewCount, Object, Object->Common.Type, Message));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtUpdateObjectReference
 *
 * PARAMETERS:  Object              - Increment ref count for this object
 *                                    and all sub-objects
 *              Action              - Either REF_INCREMENT or REF_DECREMENT
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Increment the object reference count
 *
 * Object references are incremented when:
 * 1) An object is attached to a Node (namespace object)
 * 2) An object is copied (all subobjects must be incremented)
 *
 * Object references are decremented when:
 * 1) An object is detached from an Node
 *
 ******************************************************************************/

ACPI_STATUS
AcpiUtUpdateObjectReference (
    ACPI_OPERAND_OBJECT     *Object,
    UINT16                  Action)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_GENERIC_STATE      *StateList = NULL;
    ACPI_OPERAND_OBJECT     *NextObject = NULL;
    ACPI_OPERAND_OBJECT     *PrevObject;
    ACPI_GENERIC_STATE      *State;
    UINT32                  i;


    ACPI_FUNCTION_NAME (UtUpdateObjectReference);


    while (Object)
    {
        /* Make sure that this isn't a namespace handle */

        if (ACPI_GET_DESCRIPTOR_TYPE (Object) == ACPI_DESC_TYPE_NAMED)
        {
            ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
                "Object %p is NS handle\n", Object));
            return (AE_OK);
        }

        /*
         * All sub-objects must have their reference count incremented
         * also. Different object types have different subobjects.
         */
        switch (Object->Common.Type)
        {
        case ACPI_TYPE_DEVICE:
        case ACPI_TYPE_PROCESSOR:
        case ACPI_TYPE_POWER:
        case ACPI_TYPE_THERMAL:
            /*
             * Update the notify objects for these types (if present)
             * Two lists, system and device notify handlers.
             */
            for (i = 0; i < ACPI_NUM_NOTIFY_TYPES; i++)
            {
                PrevObject = Object->CommonNotify.NotifyList[i];
                while (PrevObject)
                {
                    NextObject = PrevObject->Notify.Next[i];
                    AcpiUtUpdateRefCount (PrevObject, Action);
                    PrevObject = NextObject;
                }
            }
            break;

        case ACPI_TYPE_PACKAGE:
            /*
             * We must update all the sub-objects of the package,
             * each of whom may have their own sub-objects.
             */
            for (i = 0; i < Object->Package.Count; i++)
            {
                /*
                 * Null package elements are legal and can be simply
                 * ignored.
                 */
                NextObject = Object->Package.Elements[i];
                if (!NextObject)
                {
                    continue;
                }

                switch (NextObject->Common.Type)
                {
                case ACPI_TYPE_INTEGER:
                case ACPI_TYPE_STRING:
                case ACPI_TYPE_BUFFER:
                    /*
                     * For these very simple sub-objects, we can just
                     * update the reference count here and continue.
                     * Greatly increases performance of this operation.
                     */
                    AcpiUtUpdateRefCount (NextObject, Action);
                    break;

                default:
                    /*
                     * For complex sub-objects, push them onto the stack
                     * for later processing (this eliminates recursion.)
                     */
                    Status = AcpiUtCreateUpdateStateAndPush (
                        NextObject, Action, &StateList);
                    if (ACPI_FAILURE (Status))
                    {
                        goto ErrorExit;
                    }
                    break;
                }
            }
            NextObject = NULL;
            break;

        case ACPI_TYPE_BUFFER_FIELD:

            NextObject = Object->BufferField.BufferObj;
            break;

        case ACPI_TYPE_LOCAL_REGION_FIELD:

            NextObject = Object->Field.RegionObj;
            break;

        case ACPI_TYPE_LOCAL_BANK_FIELD:

            NextObject = Object->BankField.BankObj;
            Status = AcpiUtCreateUpdateStateAndPush (
                Object->BankField.RegionObj, Action, &StateList);
            if (ACPI_FAILURE (Status))
            {
                goto ErrorExit;
            }
            break;

        case ACPI_TYPE_LOCAL_INDEX_FIELD:

            NextObject = Object->IndexField.IndexObj;
            Status = AcpiUtCreateUpdateStateAndPush (
                Object->IndexField.DataObj, Action, &StateList);
            if (ACPI_FAILURE (Status))
            {
                goto ErrorExit;
            }
            break;

        case ACPI_TYPE_LOCAL_REFERENCE:
            /*
             * The target of an Index (a package, string, or buffer) or a named
             * reference must track changes to the ref count of the index or
             * target object.
             */
            if ((Object->Reference.Class == ACPI_REFCLASS_INDEX) ||
                (Object->Reference.Class== ACPI_REFCLASS_NAME))
            {
                NextObject = Object->Reference.Object;
            }
            break;

        case ACPI_TYPE_REGION:
        default:

            break; /* No subobjects for all other types */
        }

        /*
         * Now we can update the count in the main object. This can only
         * happen after we update the sub-objects in case this causes the
         * main object to be deleted.
         */
        AcpiUtUpdateRefCount (Object, Action);
        Object = NULL;

        /* Move on to the next object to be updated */

        if (NextObject)
        {
            Object = NextObject;
            NextObject = NULL;
        }
        else if (StateList)
        {
            State = AcpiUtPopGenericState (&StateList);
            Object = State->Update.Object;
            AcpiUtDeleteGenericState (State);
        }
    }

    return (AE_OK);


ErrorExit:

    ACPI_EXCEPTION ((AE_INFO, Status,
        "Could not update object reference count"));

    /* Free any stacked Update State objects */

    while (StateList)
    {
        State = AcpiUtPopGenericState (&StateList);
        AcpiUtDeleteGenericState (State);
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtAddReference
 *
 * PARAMETERS:  Object          - Object whose reference count is to be
 *                                incremented
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add one reference to an ACPI object
 *
 ******************************************************************************/

void
AcpiUtAddReference (
    ACPI_OPERAND_OBJECT     *Object)
{

    ACPI_FUNCTION_NAME (UtAddReference);


    /* Ensure that we have a valid object */

    if (!AcpiUtValidInternalObject (Object))
    {
        return;
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
        "Obj %p Current Refs=%X [To Be Incremented]\n",
        Object, Object->Common.ReferenceCount));

    /* Increment the reference count */

    (void) AcpiUtUpdateObjectReference (Object, REF_INCREMENT);
    return;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtRemoveReference
 *
 * PARAMETERS:  Object         - Object whose ref count will be decremented
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decrement the reference count of an ACPI internal object
 *
 ******************************************************************************/

void
AcpiUtRemoveReference (
    ACPI_OPERAND_OBJECT     *Object)
{

    ACPI_FUNCTION_NAME (UtRemoveReference);


    /*
     * Allow a NULL pointer to be passed in, just ignore it. This saves
     * each caller from having to check. Also, ignore NS nodes.
     */
    if (!Object ||
        (ACPI_GET_DESCRIPTOR_TYPE (Object) == ACPI_DESC_TYPE_NAMED))

    {
        return;
    }

    /* Ensure that we have a valid object */

    if (!AcpiUtValidInternalObject (Object))
    {
        return;
    }

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ALLOCATIONS,
        "%s: Obj %p Current Refs=%X [To Be Decremented]\n",
        ACPI_GET_FUNCTION_NAME, Object, Object->Common.ReferenceCount));

    /*
     * Decrement the reference count, and only actually delete the object
     * if the reference count becomes 0. (Must also decrement the ref count
     * of all subobjects!)
     */
    (void) AcpiUtUpdateObjectReference (Object, REF_DECREMENT);
    return;
}

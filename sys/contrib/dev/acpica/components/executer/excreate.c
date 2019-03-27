/******************************************************************************
 *
 * Module Name: excreate - Named object creation
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
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("excreate")


/*******************************************************************************
 *
 * FUNCTION:    AcpiExCreateAlias
 *
 * PARAMETERS:  WalkState            - Current state, contains operands
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new named alias
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExCreateAlias (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_NAMESPACE_NODE     *TargetNode;
    ACPI_NAMESPACE_NODE     *AliasNode;
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE (ExCreateAlias);


    /* Get the source/alias operands (both namespace nodes) */

    AliasNode =  (ACPI_NAMESPACE_NODE *) WalkState->Operands[0];
    TargetNode = (ACPI_NAMESPACE_NODE *) WalkState->Operands[1];

    if ((TargetNode->Type == ACPI_TYPE_LOCAL_ALIAS)  ||
        (TargetNode->Type == ACPI_TYPE_LOCAL_METHOD_ALIAS))
    {
        /*
         * Dereference an existing alias so that we don't create a chain
         * of aliases. With this code, we guarantee that an alias is
         * always exactly one level of indirection away from the
         * actual aliased name.
         */
        TargetNode = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, TargetNode->Object);
    }

    /* Ensure that the target node is valid */

    if (!TargetNode)
    {
        return_ACPI_STATUS (AE_NULL_OBJECT);
    }

    /* Construct the alias object (a namespace node) */

    switch (TargetNode->Type)
    {
    case ACPI_TYPE_METHOD:
        /*
         * Control method aliases need to be differentiated with
         * a special type
         */
        AliasNode->Type = ACPI_TYPE_LOCAL_METHOD_ALIAS;
        break;

    default:
        /*
         * All other object types.
         *
         * The new alias has the type ALIAS and points to the original
         * NS node, not the object itself.
         */
        AliasNode->Type = ACPI_TYPE_LOCAL_ALIAS;
        AliasNode->Object = ACPI_CAST_PTR (ACPI_OPERAND_OBJECT, TargetNode);
        break;
    }

    /* Since both operands are Nodes, we don't need to delete them */

    AliasNode->Object = ACPI_CAST_PTR (ACPI_OPERAND_OBJECT, TargetNode);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExCreateEvent
 *
 * PARAMETERS:  WalkState           - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new event object
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExCreateEvent (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *ObjDesc;


    ACPI_FUNCTION_TRACE (ExCreateEvent);


    ObjDesc = AcpiUtCreateInternalObject (ACPI_TYPE_EVENT);
    if (!ObjDesc)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    /*
     * Create the actual OS semaphore, with zero initial units -- meaning
     * that the event is created in an unsignalled state
     */
    Status = AcpiOsCreateSemaphore (ACPI_NO_UNIT_LIMIT, 0,
        &ObjDesc->Event.OsSemaphore);
    if (ACPI_FAILURE (Status))
    {
        goto Cleanup;
    }

    /* Attach object to the Node */

    Status = AcpiNsAttachObject (
        (ACPI_NAMESPACE_NODE *) WalkState->Operands[0],
        ObjDesc, ACPI_TYPE_EVENT);

Cleanup:
    /*
     * Remove local reference to the object (on error, will cause deletion
     * of both object and semaphore if present.)
     */
    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExCreateMutex
 *
 * PARAMETERS:  WalkState           - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new mutex object
 *
 *              Mutex (Name[0], SyncLevel[1])
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExCreateMutex (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_OPERAND_OBJECT     *ObjDesc;


    ACPI_FUNCTION_TRACE_PTR (ExCreateMutex, ACPI_WALK_OPERANDS);


    /* Create the new mutex object */

    ObjDesc = AcpiUtCreateInternalObject (ACPI_TYPE_MUTEX);
    if (!ObjDesc)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    /* Create the actual OS Mutex */

    Status = AcpiOsCreateMutex (&ObjDesc->Mutex.OsMutex);
    if (ACPI_FAILURE (Status))
    {
        goto Cleanup;
    }

    /* Init object and attach to NS node */

    ObjDesc->Mutex.SyncLevel = (UINT8) WalkState->Operands[1]->Integer.Value;
    ObjDesc->Mutex.Node = (ACPI_NAMESPACE_NODE *) WalkState->Operands[0];

    Status = AcpiNsAttachObject (
        ObjDesc->Mutex.Node, ObjDesc, ACPI_TYPE_MUTEX);


Cleanup:
    /*
     * Remove local reference to the object (on error, will cause deletion
     * of both object and semaphore if present.)
     */
    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExCreateRegion
 *
 * PARAMETERS:  AmlStart            - Pointer to the region declaration AML
 *              AmlLength           - Max length of the declaration AML
 *              SpaceId             - Address space ID for the region
 *              WalkState           - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new operation region object
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExCreateRegion (
    UINT8                   *AmlStart,
    UINT32                  AmlLength,
    UINT8                   SpaceId,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_OPERAND_OBJECT     *RegionObj2;


    ACPI_FUNCTION_TRACE (ExCreateRegion);


    /* Get the Namespace Node */

    Node = WalkState->Op->Common.Node;

    /*
     * If the region object is already attached to this node,
     * just return
     */
    if (AcpiNsGetAttachedObject (Node))
    {
        return_ACPI_STATUS (AE_OK);
    }

    /*
     * Space ID must be one of the predefined IDs, or in the user-defined
     * range
     */
    if (!AcpiIsValidSpaceId (SpaceId))
    {
        /*
         * Print an error message, but continue. We don't want to abort
         * a table load for this exception. Instead, if the region is
         * actually used at runtime, abort the executing method.
         */
        ACPI_ERROR ((AE_INFO,
            "Invalid/unknown Address Space ID: 0x%2.2X", SpaceId));
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_LOAD, "Region Type - %s (0x%X)\n",
        AcpiUtGetRegionName (SpaceId), SpaceId));

    /* Create the region descriptor */

    ObjDesc = AcpiUtCreateInternalObject (ACPI_TYPE_REGION);
    if (!ObjDesc)
    {
        Status = AE_NO_MEMORY;
        goto Cleanup;
    }

    /*
     * Remember location in AML stream of address & length
     * operands since they need to be evaluated at run time.
     */
    RegionObj2 = AcpiNsGetSecondaryObject (ObjDesc);
    RegionObj2->Extra.AmlStart = AmlStart;
    RegionObj2->Extra.AmlLength = AmlLength;
    RegionObj2->Extra.Method_REG = NULL;
    if (WalkState->ScopeInfo)
    {
        RegionObj2->Extra.ScopeNode = WalkState->ScopeInfo->Scope.Node;
    }
    else
    {
        RegionObj2->Extra.ScopeNode = Node;
    }

    /* Init the region from the operands */

    ObjDesc->Region.SpaceId = SpaceId;
    ObjDesc->Region.Address = 0;
    ObjDesc->Region.Length = 0;
    ObjDesc->Region.Node = Node;
    ObjDesc->Region.Handler = NULL;
    ObjDesc->Common.Flags &=
        ~(AOPOBJ_SETUP_COMPLETE | AOPOBJ_REG_CONNECTED |
          AOPOBJ_OBJECT_INITIALIZED);

    /* Install the new region object in the parent Node */

    Status = AcpiNsAttachObject (Node, ObjDesc, ACPI_TYPE_REGION);


Cleanup:

    /* Remove local reference to the object */

    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExCreateProcessor
 *
 * PARAMETERS:  WalkState           - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new processor object and populate the fields
 *
 *              Processor (Name[0], CpuID[1], PblockAddr[2], PblockLength[3])
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExCreateProcessor (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     **Operand = &WalkState->Operands[0];
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE_PTR (ExCreateProcessor, WalkState);


    /* Create the processor object */

    ObjDesc = AcpiUtCreateInternalObject (ACPI_TYPE_PROCESSOR);
    if (!ObjDesc)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Initialize the processor object from the operands */

    ObjDesc->Processor.ProcId = (UINT8) Operand[1]->Integer.Value;
    ObjDesc->Processor.Length = (UINT8) Operand[3]->Integer.Value;
    ObjDesc->Processor.Address = (ACPI_IO_ADDRESS) Operand[2]->Integer.Value;

    /* Install the processor object in the parent Node */

    Status = AcpiNsAttachObject ((ACPI_NAMESPACE_NODE *) Operand[0],
        ObjDesc, ACPI_TYPE_PROCESSOR);

    /* Remove local reference to the object */

    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExCreatePowerResource
 *
 * PARAMETERS:  WalkState           - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new PowerResource object and populate the fields
 *
 *              PowerResource (Name[0], SystemLevel[1], ResourceOrder[2])
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExCreatePowerResource (
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     **Operand = &WalkState->Operands[0];
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *ObjDesc;


    ACPI_FUNCTION_TRACE_PTR (ExCreatePowerResource, WalkState);


    /* Create the power resource object */

    ObjDesc = AcpiUtCreateInternalObject (ACPI_TYPE_POWER);
    if (!ObjDesc)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Initialize the power object from the operands */

    ObjDesc->PowerResource.SystemLevel = (UINT8) Operand[1]->Integer.Value;
    ObjDesc->PowerResource.ResourceOrder = (UINT16) Operand[2]->Integer.Value;

    /* Install the  power resource object in the parent Node */

    Status = AcpiNsAttachObject ((ACPI_NAMESPACE_NODE *) Operand[0],
        ObjDesc, ACPI_TYPE_POWER);

    /* Remove local reference to the object */

    AcpiUtRemoveReference (ObjDesc);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExCreateMethod
 *
 * PARAMETERS:  AmlStart        - First byte of the method's AML
 *              AmlLength       - AML byte count for this method
 *              WalkState       - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new method object
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExCreateMethod (
    UINT8                   *AmlStart,
    UINT32                  AmlLength,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     **Operand = &WalkState->Operands[0];
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_STATUS             Status;
    UINT8                   MethodFlags;


    ACPI_FUNCTION_TRACE_PTR (ExCreateMethod, WalkState);


    /* Create a new method object */

    ObjDesc = AcpiUtCreateInternalObject (ACPI_TYPE_METHOD);
    if (!ObjDesc)
    {
       Status = AE_NO_MEMORY;
       goto Exit;
    }

    /* Save the method's AML pointer and length  */

    ObjDesc->Method.AmlStart = AmlStart;
    ObjDesc->Method.AmlLength = AmlLength;
    ObjDesc->Method.Node = Operand[0];

    /*
     * Disassemble the method flags. Split off the ArgCount, Serialized
     * flag, and SyncLevel for efficiency.
     */
    MethodFlags = (UINT8) Operand[1]->Integer.Value;
    ObjDesc->Method.ParamCount = (UINT8)
        (MethodFlags & AML_METHOD_ARG_COUNT);

    /*
     * Get the SyncLevel. If method is serialized, a mutex will be
     * created for this method when it is parsed.
     */
    if (MethodFlags & AML_METHOD_SERIALIZED)
    {
        ObjDesc->Method.InfoFlags = ACPI_METHOD_SERIALIZED;

        /*
         * ACPI 1.0: SyncLevel = 0
         * ACPI 2.0: SyncLevel = SyncLevel in method declaration
         */
        ObjDesc->Method.SyncLevel = (UINT8)
            ((MethodFlags & AML_METHOD_SYNC_LEVEL) >> 4);
    }

    /* Attach the new object to the method Node */

    Status = AcpiNsAttachObject ((ACPI_NAMESPACE_NODE *) Operand[0],
        ObjDesc, ACPI_TYPE_METHOD);

    /* Remove local reference to the object */

    AcpiUtRemoveReference (ObjDesc);

Exit:
    /* Remove a reference to the operand */

    AcpiUtRemoveReference (Operand[1]);
    return_ACPI_STATUS (Status);
}

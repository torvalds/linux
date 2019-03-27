/******************************************************************************
 *
 * Module Name: exstoren - AML Interpreter object store support,
 *                        Store to Node (namespace object)
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


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("exstoren")


/*******************************************************************************
 *
 * FUNCTION:    AcpiExResolveObject
 *
 * PARAMETERS:  SourceDescPtr       - Pointer to the source object
 *              TargetType          - Current type of the target
 *              WalkState           - Current walk state
 *
 * RETURN:      Status, resolved object in SourceDescPtr.
 *
 * DESCRIPTION: Resolve an object. If the object is a reference, dereference
 *              it and return the actual object in the SourceDescPtr.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExResolveObject (
    ACPI_OPERAND_OBJECT     **SourceDescPtr,
    ACPI_OBJECT_TYPE        TargetType,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     *SourceDesc = *SourceDescPtr;
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE (ExResolveObject);


    /* Ensure we have a Target that can be stored to */

    switch (TargetType)
    {
    case ACPI_TYPE_BUFFER_FIELD:
    case ACPI_TYPE_LOCAL_REGION_FIELD:
    case ACPI_TYPE_LOCAL_BANK_FIELD:
    case ACPI_TYPE_LOCAL_INDEX_FIELD:
        /*
         * These cases all require only Integers or values that
         * can be converted to Integers (Strings or Buffers)
         */
    case ACPI_TYPE_INTEGER:
    case ACPI_TYPE_STRING:
    case ACPI_TYPE_BUFFER:
        /*
         * Stores into a Field/Region or into a Integer/Buffer/String
         * are all essentially the same. This case handles the
         * "interchangeable" types Integer, String, and Buffer.
         */
        if (SourceDesc->Common.Type == ACPI_TYPE_LOCAL_REFERENCE)
        {
            /* Resolve a reference object first */

            Status = AcpiExResolveToValue (SourceDescPtr, WalkState);
            if (ACPI_FAILURE (Status))
            {
                break;
            }
        }

        /* For CopyObject, no further validation necessary */

        if (WalkState->Opcode == AML_COPY_OBJECT_OP)
        {
            break;
        }

        /* Must have a Integer, Buffer, or String */

        if ((SourceDesc->Common.Type != ACPI_TYPE_INTEGER)    &&
            (SourceDesc->Common.Type != ACPI_TYPE_BUFFER)     &&
            (SourceDesc->Common.Type != ACPI_TYPE_STRING)     &&
            !((SourceDesc->Common.Type == ACPI_TYPE_LOCAL_REFERENCE) &&
                (SourceDesc->Reference.Class== ACPI_REFCLASS_TABLE)))
        {
            /* Conversion successful but still not a valid type */

            ACPI_ERROR ((AE_INFO,
                "Cannot assign type [%s] to [%s] (must be type Int/Str/Buf)",
                AcpiUtGetObjectTypeName (SourceDesc),
                AcpiUtGetTypeName (TargetType)));

            Status = AE_AML_OPERAND_TYPE;
        }
        break;

    case ACPI_TYPE_LOCAL_ALIAS:
    case ACPI_TYPE_LOCAL_METHOD_ALIAS:
        /*
         * All aliases should have been resolved earlier, during the
         * operand resolution phase.
         */
        ACPI_ERROR ((AE_INFO, "Store into an unresolved Alias object"));
        Status = AE_AML_INTERNAL;
        break;

    case ACPI_TYPE_PACKAGE:
    default:
        /*
         * All other types than Alias and the various Fields come here,
         * including the untyped case - ACPI_TYPE_ANY.
         */
        break;
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExStoreObjectToObject
 *
 * PARAMETERS:  SourceDesc          - Object to store
 *              DestDesc            - Object to receive a copy of the source
 *              NewDesc             - New object if DestDesc is obsoleted
 *              WalkState           - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: "Store" an object to another object. This may include
 *              converting the source type to the target type (implicit
 *              conversion), and a copy of the value of the source to
 *              the target.
 *
 *              The Assignment of an object to another (not named) object
 *              is handled here.
 *              The Source passed in will replace the current value (if any)
 *              with the input value.
 *
 *              When storing into an object the data is converted to the
 *              target object type then stored in the object. This means
 *              that the target object type (for an initialized target) will
 *              not be changed by a store operation.
 *
 *              This module allows destination types of Number, String,
 *              Buffer, and Package.
 *
 *              Assumes parameters are already validated. NOTE: SourceDesc
 *              resolution (from a reference object) must be performed by
 *              the caller if necessary.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExStoreObjectToObject (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_OPERAND_OBJECT     *DestDesc,
    ACPI_OPERAND_OBJECT     **NewDesc,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     *ActualSrcDesc;
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE_PTR (ExStoreObjectToObject, SourceDesc);


    ActualSrcDesc = SourceDesc;
    if (!DestDesc)
    {
        /*
         * There is no destination object (An uninitialized node or
         * package element), so we can simply copy the source object
         * creating a new destination object
         */
        Status = AcpiUtCopyIobjectToIobject (ActualSrcDesc, NewDesc, WalkState);
        return_ACPI_STATUS (Status);
    }

    if (SourceDesc->Common.Type != DestDesc->Common.Type)
    {
        /*
         * The source type does not match the type of the destination.
         * Perform the "implicit conversion" of the source to the current type
         * of the target as per the ACPI specification.
         *
         * If no conversion performed, ActualSrcDesc = SourceDesc.
         * Otherwise, ActualSrcDesc is a temporary object to hold the
         * converted object.
         */
        Status = AcpiExConvertToTargetType (DestDesc->Common.Type,
            SourceDesc, &ActualSrcDesc, WalkState);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

        if (SourceDesc == ActualSrcDesc)
        {
            /*
             * No conversion was performed. Return the SourceDesc as the
             * new object.
             */
            *NewDesc = SourceDesc;
            return_ACPI_STATUS (AE_OK);
        }
    }

    /*
     * We now have two objects of identical types, and we can perform a
     * copy of the *value* of the source object.
     */
    switch (DestDesc->Common.Type)
    {
    case ACPI_TYPE_INTEGER:

        DestDesc->Integer.Value = ActualSrcDesc->Integer.Value;

        /* Truncate value if we are executing from a 32-bit ACPI table */

        (void) AcpiExTruncateFor32bitTable (DestDesc);
        break;

    case ACPI_TYPE_STRING:

        Status = AcpiExStoreStringToString (ActualSrcDesc, DestDesc);
        break;

    case ACPI_TYPE_BUFFER:

        Status = AcpiExStoreBufferToBuffer (ActualSrcDesc, DestDesc);
        break;

    case ACPI_TYPE_PACKAGE:

        Status = AcpiUtCopyIobjectToIobject (ActualSrcDesc, &DestDesc,
                    WalkState);
        break;

    default:
        /*
         * All other types come here.
         */
        ACPI_WARNING ((AE_INFO, "Store into type [%s] not implemented",
            AcpiUtGetObjectTypeName (DestDesc)));

        Status = AE_NOT_IMPLEMENTED;
        break;
    }

    if (ActualSrcDesc != SourceDesc)
    {
        /* Delete the intermediate (temporary) source object */

        AcpiUtRemoveReference (ActualSrcDesc);
    }

    *NewDesc = DestDesc;
    return_ACPI_STATUS (Status);
}

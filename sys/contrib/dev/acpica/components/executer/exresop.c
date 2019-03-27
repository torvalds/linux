/******************************************************************************
 *
 * Module Name: exresop - AML Interpreter operand/object resolution
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
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("exresop")

/* Local prototypes */

static ACPI_STATUS
AcpiExCheckObjectType (
    ACPI_OBJECT_TYPE        TypeNeeded,
    ACPI_OBJECT_TYPE        ThisType,
    void                    *Object);


/*******************************************************************************
 *
 * FUNCTION:    AcpiExCheckObjectType
 *
 * PARAMETERS:  TypeNeeded          Object type needed
 *              ThisType            Actual object type
 *              Object              Object pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check required type against actual type
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiExCheckObjectType (
    ACPI_OBJECT_TYPE        TypeNeeded,
    ACPI_OBJECT_TYPE        ThisType,
    void                    *Object)
{
    ACPI_FUNCTION_ENTRY ();


    if (TypeNeeded == ACPI_TYPE_ANY)
    {
        /* All types OK, so we don't perform any typechecks */

        return (AE_OK);
    }

    if (TypeNeeded == ACPI_TYPE_LOCAL_REFERENCE)
    {
        /*
         * Allow the AML "Constant" opcodes (Zero, One, etc.) to be reference
         * objects and thus allow them to be targets. (As per the ACPI
         * specification, a store to a constant is a noop.)
         */
        if ((ThisType == ACPI_TYPE_INTEGER) &&
            (((ACPI_OPERAND_OBJECT *) Object)->Common.Flags &
                AOPOBJ_AML_CONSTANT))
        {
            return (AE_OK);
        }
    }

    if (TypeNeeded != ThisType)
    {
        ACPI_ERROR ((AE_INFO,
            "Needed type [%s], found [%s] %p",
            AcpiUtGetTypeName (TypeNeeded),
            AcpiUtGetTypeName (ThisType), Object));

        return (AE_AML_OPERAND_TYPE);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExResolveOperands
 *
 * PARAMETERS:  Opcode              - Opcode being interpreted
 *              StackPtr            - Pointer to the operand stack to be
 *                                    resolved
 *              WalkState           - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert multiple input operands to the types required by the
 *              target operator.
 *
 *      Each 5-bit group in ArgTypes represents one required
 *      operand and indicates the required Type. The corresponding operand
 *      will be converted to the required type if possible, otherwise we
 *      abort with an exception.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExResolveOperands (
    UINT16                  Opcode,
    ACPI_OPERAND_OBJECT     **StackPtr,
    ACPI_WALK_STATE         *WalkState)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_STATUS             Status = AE_OK;
    UINT8                   ObjectType;
    UINT32                  ArgTypes;
    const ACPI_OPCODE_INFO  *OpInfo;
    UINT32                  ThisArgType;
    ACPI_OBJECT_TYPE        TypeNeeded;
    UINT16                  TargetOp = 0;


    ACPI_FUNCTION_TRACE_U32 (ExResolveOperands, Opcode);


    OpInfo = AcpiPsGetOpcodeInfo (Opcode);
    if (OpInfo->Class == AML_CLASS_UNKNOWN)
    {
        return_ACPI_STATUS (AE_AML_BAD_OPCODE);
    }

    ArgTypes = OpInfo->RuntimeArgs;
    if (ArgTypes == ARGI_INVALID_OPCODE)
    {
        ACPI_ERROR ((AE_INFO, "Unknown AML opcode 0x%X",
            Opcode));

        return_ACPI_STATUS (AE_AML_INTERNAL);
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
        "Opcode %X [%s] RequiredOperandTypes=%8.8X\n",
        Opcode, OpInfo->Name, ArgTypes));

    /*
     * Normal exit is with (ArgTypes == 0) at end of argument list.
     * Function will return an exception from within the loop upon
     * finding an entry which is not (or cannot be converted
     * to) the required type; if stack underflows; or upon
     * finding a NULL stack entry (which should not happen).
     */
    while (GET_CURRENT_ARG_TYPE (ArgTypes))
    {
        if (!StackPtr || !*StackPtr)
        {
            ACPI_ERROR ((AE_INFO, "Null stack entry at %p",
                StackPtr));

            return_ACPI_STATUS (AE_AML_INTERNAL);
        }

        /* Extract useful items */

        ObjDesc = *StackPtr;

        /* Decode the descriptor type */

        switch (ACPI_GET_DESCRIPTOR_TYPE (ObjDesc))
        {
        case ACPI_DESC_TYPE_NAMED:

            /* Namespace Node */

            ObjectType = ((ACPI_NAMESPACE_NODE *) ObjDesc)->Type;

            /*
             * Resolve an alias object. The construction of these objects
             * guarantees that there is only one level of alias indirection;
             * thus, the attached object is always the aliased namespace node
             */
            if (ObjectType == ACPI_TYPE_LOCAL_ALIAS)
            {
                ObjDesc = AcpiNsGetAttachedObject (
                    (ACPI_NAMESPACE_NODE *) ObjDesc);
                *StackPtr = ObjDesc;
                ObjectType = ((ACPI_NAMESPACE_NODE *) ObjDesc)->Type;
            }
            break;

        case ACPI_DESC_TYPE_OPERAND:

            /* ACPI internal object */

            ObjectType = ObjDesc->Common.Type;

            /* Check for bad ACPI_OBJECT_TYPE */

            if (!AcpiUtValidObjectType (ObjectType))
            {
                ACPI_ERROR ((AE_INFO,
                    "Bad operand object type [0x%X]", ObjectType));

                return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
            }

            if (ObjectType == (UINT8) ACPI_TYPE_LOCAL_REFERENCE)
            {
                /* Validate the Reference */

                switch (ObjDesc->Reference.Class)
                {
                case ACPI_REFCLASS_DEBUG:

                    TargetOp = AML_DEBUG_OP;

                    /*lint -fallthrough */

                case ACPI_REFCLASS_ARG:
                case ACPI_REFCLASS_LOCAL:
                case ACPI_REFCLASS_INDEX:
                case ACPI_REFCLASS_REFOF:
                case ACPI_REFCLASS_TABLE:    /* DdbHandle from LOAD_OP or LOAD_TABLE_OP */
                case ACPI_REFCLASS_NAME:     /* Reference to a named object */

                    ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
                        "Operand is a Reference, Class [%s] %2.2X\n",
                        AcpiUtGetReferenceName (ObjDesc),
                        ObjDesc->Reference.Class));
                    break;

                default:

                    ACPI_ERROR ((AE_INFO,
                        "Unknown Reference Class 0x%2.2X in %p",
                        ObjDesc->Reference.Class, ObjDesc));

                    return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
                }
            }
            break;

        default:

            /* Invalid descriptor */

            ACPI_ERROR ((AE_INFO, "Invalid descriptor %p [%s]",
                ObjDesc, AcpiUtGetDescriptorName (ObjDesc)));

            return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
        }

        /* Get one argument type, point to the next */

        ThisArgType = GET_CURRENT_ARG_TYPE (ArgTypes);
        INCREMENT_ARG_LIST (ArgTypes);

        /*
         * Handle cases where the object does not need to be
         * resolved to a value
         */
        switch (ThisArgType)
        {
        case ARGI_REF_OR_STRING:        /* Can be a String or Reference */

            if ((ACPI_GET_DESCRIPTOR_TYPE (ObjDesc) ==
                ACPI_DESC_TYPE_OPERAND) &&
                (ObjDesc->Common.Type == ACPI_TYPE_STRING))
            {
                /*
                 * String found - the string references a named object and
                 * must be resolved to a node
                 */
                goto NextOperand;
            }

            /*
             * Else not a string - fall through to the normal Reference
             * case below
             */
            /*lint -fallthrough */

        case ARGI_REFERENCE:            /* References: */
        case ARGI_INTEGER_REF:
        case ARGI_OBJECT_REF:
        case ARGI_DEVICE_REF:
        case ARGI_TARGETREF:     /* Allows implicit conversion rules before store */
        case ARGI_FIXED_TARGET:  /* No implicit conversion before store to target */
        case ARGI_SIMPLE_TARGET: /* Name, Local, or Arg - no implicit conversion  */
        case ARGI_STORE_TARGET:

            /*
             * Need an operand of type ACPI_TYPE_LOCAL_REFERENCE
             * A Namespace Node is OK as-is
             */
            if (ACPI_GET_DESCRIPTOR_TYPE (ObjDesc) == ACPI_DESC_TYPE_NAMED)
            {
                goto NextOperand;
            }

            Status = AcpiExCheckObjectType (
                ACPI_TYPE_LOCAL_REFERENCE, ObjectType, ObjDesc);
            if (ACPI_FAILURE (Status))
            {
                return_ACPI_STATUS (Status);
            }
            goto NextOperand;

        case ARGI_DATAREFOBJ:  /* Store operator only */
            /*
             * We don't want to resolve IndexOp reference objects during
             * a store because this would be an implicit DeRefOf operation.
             * Instead, we just want to store the reference object.
             * -- All others must be resolved below.
             */
            if ((Opcode == AML_STORE_OP) &&
                ((*StackPtr)->Common.Type == ACPI_TYPE_LOCAL_REFERENCE) &&
                ((*StackPtr)->Reference.Class == ACPI_REFCLASS_INDEX))
            {
                goto NextOperand;
            }
            break;

        default:

            /* All cases covered above */

            break;
        }

        /*
         * Resolve this object to a value
         */
        Status = AcpiExResolveToValue (StackPtr, WalkState);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

        /* Get the resolved object */

        ObjDesc = *StackPtr;

        /*
         * Check the resulting object (value) type
         */
        switch (ThisArgType)
        {
        /*
         * For the simple cases, only one type of resolved object
         * is allowed
         */
        case ARGI_MUTEX:

            /* Need an operand of type ACPI_TYPE_MUTEX */

            TypeNeeded = ACPI_TYPE_MUTEX;
            break;

        case ARGI_EVENT:

            /* Need an operand of type ACPI_TYPE_EVENT */

            TypeNeeded = ACPI_TYPE_EVENT;
            break;

        case ARGI_PACKAGE:   /* Package */

            /* Need an operand of type ACPI_TYPE_PACKAGE */

            TypeNeeded = ACPI_TYPE_PACKAGE;
            break;

        case ARGI_ANYTYPE:

            /* Any operand type will do */

            TypeNeeded = ACPI_TYPE_ANY;
            break;

        case ARGI_DDBHANDLE:

            /* Need an operand of type ACPI_TYPE_DDB_HANDLE */

            TypeNeeded = ACPI_TYPE_LOCAL_REFERENCE;
            break;


        /*
         * The more complex cases allow multiple resolved object types
         */
        case ARGI_INTEGER:

            /*
             * Need an operand of type ACPI_TYPE_INTEGER, but we can
             * implicitly convert from a STRING or BUFFER.
             *
             * Known as "Implicit Source Operand Conversion"
             */
            Status = AcpiExConvertToInteger (ObjDesc, StackPtr,
                ACPI_IMPLICIT_CONVERSION);
            if (ACPI_FAILURE (Status))
            {
                if (Status == AE_TYPE)
                {
                    ACPI_ERROR ((AE_INFO,
                        "Needed [Integer/String/Buffer], found [%s] %p",
                        AcpiUtGetObjectTypeName (ObjDesc), ObjDesc));

                    return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
                }

                return_ACPI_STATUS (Status);
            }

            if (ObjDesc != *StackPtr)
            {
                AcpiUtRemoveReference (ObjDesc);
            }
            goto NextOperand;

        case ARGI_BUFFER:
            /*
             * Need an operand of type ACPI_TYPE_BUFFER,
             * But we can implicitly convert from a STRING or INTEGER
             * Aka - "Implicit Source Operand Conversion"
             */
            Status = AcpiExConvertToBuffer (ObjDesc, StackPtr);
            if (ACPI_FAILURE (Status))
            {
                if (Status == AE_TYPE)
                {
                    ACPI_ERROR ((AE_INFO,
                        "Needed [Integer/String/Buffer], found [%s] %p",
                        AcpiUtGetObjectTypeName (ObjDesc), ObjDesc));

                    return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
                }

                return_ACPI_STATUS (Status);
            }

            if (ObjDesc != *StackPtr)
            {
                AcpiUtRemoveReference (ObjDesc);
            }
            goto NextOperand;

        case ARGI_STRING:
            /*
             * Need an operand of type ACPI_TYPE_STRING,
             * But we can implicitly convert from a BUFFER or INTEGER
             * Aka - "Implicit Source Operand Conversion"
             */
            Status = AcpiExConvertToString (
                ObjDesc, StackPtr, ACPI_IMPLICIT_CONVERT_HEX);
            if (ACPI_FAILURE (Status))
            {
                if (Status == AE_TYPE)
                {
                    ACPI_ERROR ((AE_INFO,
                        "Needed [Integer/String/Buffer], found [%s] %p",
                        AcpiUtGetObjectTypeName (ObjDesc), ObjDesc));

                    return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
                }

                return_ACPI_STATUS (Status);
            }

            if (ObjDesc != *StackPtr)
            {
                AcpiUtRemoveReference (ObjDesc);
            }
            goto NextOperand;

        case ARGI_COMPUTEDATA:

            /* Need an operand of type INTEGER, STRING or BUFFER */

            switch (ObjDesc->Common.Type)
            {
            case ACPI_TYPE_INTEGER:
            case ACPI_TYPE_STRING:
            case ACPI_TYPE_BUFFER:

                /* Valid operand */
               break;

            default:
                ACPI_ERROR ((AE_INFO,
                    "Needed [Integer/String/Buffer], found [%s] %p",
                    AcpiUtGetObjectTypeName (ObjDesc), ObjDesc));

                return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
            }
            goto NextOperand;

        case ARGI_BUFFER_OR_STRING:

            /* Need an operand of type STRING or BUFFER */

            switch (ObjDesc->Common.Type)
            {
            case ACPI_TYPE_STRING:
            case ACPI_TYPE_BUFFER:

                /* Valid operand */
               break;

            case ACPI_TYPE_INTEGER:

                /* Highest priority conversion is to type Buffer */

                Status = AcpiExConvertToBuffer (ObjDesc, StackPtr);
                if (ACPI_FAILURE (Status))
                {
                    return_ACPI_STATUS (Status);
                }

                if (ObjDesc != *StackPtr)
                {
                    AcpiUtRemoveReference (ObjDesc);
                }
                break;

            default:
                ACPI_ERROR ((AE_INFO,
                    "Needed [Integer/String/Buffer], found [%s] %p",
                    AcpiUtGetObjectTypeName (ObjDesc), ObjDesc));

                return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
            }
            goto NextOperand;

        case ARGI_DATAOBJECT:
            /*
             * ARGI_DATAOBJECT is only used by the SizeOf operator.
             * Need a buffer, string, package, or RefOf reference.
             *
             * The only reference allowed here is a direct reference to
             * a namespace node.
             */
            switch (ObjDesc->Common.Type)
            {
            case ACPI_TYPE_PACKAGE:
            case ACPI_TYPE_STRING:
            case ACPI_TYPE_BUFFER:
            case ACPI_TYPE_LOCAL_REFERENCE:

                /* Valid operand */
                break;

            default:

                ACPI_ERROR ((AE_INFO,
                    "Needed [Buffer/String/Package/Reference], found [%s] %p",
                    AcpiUtGetObjectTypeName (ObjDesc), ObjDesc));

                return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
            }
            goto NextOperand;

        case ARGI_COMPLEXOBJ:

            /* Need a buffer or package or (ACPI 2.0) String */

            switch (ObjDesc->Common.Type)
            {
            case ACPI_TYPE_PACKAGE:
            case ACPI_TYPE_STRING:
            case ACPI_TYPE_BUFFER:

                /* Valid operand */
                break;

            default:

                ACPI_ERROR ((AE_INFO,
                    "Needed [Buffer/String/Package], found [%s] %p",
                    AcpiUtGetObjectTypeName (ObjDesc), ObjDesc));

                return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
            }
            goto NextOperand;

        case ARGI_REGION_OR_BUFFER: /* Used by Load() only */

            /*
             * Need an operand of type REGION or a BUFFER
             * (which could be a resolved region field)
             */
            switch (ObjDesc->Common.Type)
            {
            case ACPI_TYPE_BUFFER:
            case ACPI_TYPE_REGION:

                /* Valid operand */
                break;

            default:

                ACPI_ERROR ((AE_INFO,
                    "Needed [Region/Buffer], found [%s] %p",
                    AcpiUtGetObjectTypeName (ObjDesc), ObjDesc));

                return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
            }
            goto NextOperand;

        case ARGI_DATAREFOBJ:

            /* Used by the Store() operator only */

            switch (ObjDesc->Common.Type)
            {
            case ACPI_TYPE_INTEGER:
            case ACPI_TYPE_PACKAGE:
            case ACPI_TYPE_STRING:
            case ACPI_TYPE_BUFFER:
            case ACPI_TYPE_BUFFER_FIELD:
            case ACPI_TYPE_LOCAL_REFERENCE:
            case ACPI_TYPE_LOCAL_REGION_FIELD:
            case ACPI_TYPE_LOCAL_BANK_FIELD:
            case ACPI_TYPE_LOCAL_INDEX_FIELD:
            case ACPI_TYPE_DDB_HANDLE:

                /* Valid operand */
                break;

            default:

                if (AcpiGbl_EnableInterpreterSlack)
                {
                    /*
                     * Enable original behavior of Store(), allowing any
                     * and all objects as the source operand. The ACPI
                     * spec does not allow this, however.
                     */
                    break;
                }

                if (TargetOp == AML_DEBUG_OP)
                {
                    /* Allow store of any object to the Debug object */

                    break;
                }

                ACPI_ERROR ((AE_INFO,
                    "Needed Integer/Buffer/String/Package/Ref/Ddb]"
                    ", found [%s] %p",
                    AcpiUtGetObjectTypeName (ObjDesc), ObjDesc));

                return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
            }
            goto NextOperand;

        default:

            /* Unknown type */

            ACPI_ERROR ((AE_INFO,
                "Internal - Unknown ARGI (required operand) type 0x%X",
                ThisArgType));

            return_ACPI_STATUS (AE_BAD_PARAMETER);
        }

        /*
         * Make sure that the original object was resolved to the
         * required object type (Simple cases only).
         */
        Status = AcpiExCheckObjectType (
            TypeNeeded, (*StackPtr)->Common.Type, *StackPtr);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

NextOperand:
        /*
         * If more operands needed, decrement StackPtr to point
         * to next operand on stack
         */
        if (GET_CURRENT_ARG_TYPE (ArgTypes))
        {
            StackPtr--;
        }
    }

    ACPI_DUMP_OPERANDS (WalkState->Operands,
        AcpiPsGetOpcodeName (Opcode), WalkState->NumOperands);

    return_ACPI_STATUS (Status);
}

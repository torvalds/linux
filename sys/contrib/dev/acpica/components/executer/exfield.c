/******************************************************************************
 *
 * Module Name: exfield - AML execution - FieldUnit read/write
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
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/amlcode.h>


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("exfield")


/*
 * This table maps the various Attrib protocols to the byte transfer
 * length. Used for the generic serial bus.
 */
#define ACPI_INVALID_PROTOCOL_ID        0x80
#define ACPI_MAX_PROTOCOL_ID            0x0F

const UINT8     AcpiProtocolLengths[] =
{
    ACPI_INVALID_PROTOCOL_ID,   /* 0 - reserved */
    ACPI_INVALID_PROTOCOL_ID,   /* 1 - reserved */
    0x00,                       /* 2 - ATTRIB_QUICK */
    ACPI_INVALID_PROTOCOL_ID,   /* 3 - reserved */
    0x01,                       /* 4 - ATTRIB_SEND_RECEIVE */
    ACPI_INVALID_PROTOCOL_ID,   /* 5 - reserved */
    0x01,                       /* 6 - ATTRIB_BYTE */
    ACPI_INVALID_PROTOCOL_ID,   /* 7 - reserved */
    0x02,                       /* 8 - ATTRIB_WORD */
    ACPI_INVALID_PROTOCOL_ID,   /* 9 - reserved */
    0xFF,                       /* A - ATTRIB_BLOCK  */
    0xFF,                       /* B - ATTRIB_BYTES */
    0x02,                       /* C - ATTRIB_PROCESS_CALL */
    0xFF,                       /* D - ATTRIB_BLOCK_PROCESS_CALL */
    0xFF,                       /* E - ATTRIB_RAW_BYTES */
    0xFF                        /* F - ATTRIB_RAW_PROCESS_BYTES */
};

#define PCC_MASTER_SUBSPACE     3

/*
 * The following macros determine a given offset is a COMD field.
 * According to the specification, generic subspaces (types 0-2) contains a
 * 2-byte COMD field at offset 4 and master subspaces (type 3) contains a 4-byte
 * COMD field starting at offset 12.
 */
#define GENERIC_SUBSPACE_COMMAND(a)     (4 == a || a == 5)
#define MASTER_SUBSPACE_COMMAND(a)      (12 <= a && a <= 15)


/*******************************************************************************
 *
 * FUNCTION:    AcpiExGetProtocolBufferLength
 *
 * PARAMETERS:  ProtocolId      - The type of the protocol indicated by region
 *                                field access attributes
 *              ReturnLength    - Where the protocol byte transfer length is
 *                                returned
 *
 * RETURN:      Status and decoded byte transfer length
 *
 * DESCRIPTION: This routine returns the length of the GenericSerialBus
 *              protocol bytes
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExGetProtocolBufferLength (
    UINT32                  ProtocolId,
    UINT32                  *ReturnLength)
{

    if ((ProtocolId > ACPI_MAX_PROTOCOL_ID) ||
        (AcpiProtocolLengths[ProtocolId] == ACPI_INVALID_PROTOCOL_ID))
    {
        ACPI_ERROR ((AE_INFO,
            "Invalid Field/AccessAs protocol ID: 0x%4.4X", ProtocolId));

        return (AE_AML_PROTOCOL);
    }

    *ReturnLength = AcpiProtocolLengths[ProtocolId];
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExReadDataFromField
 *
 * PARAMETERS:  WalkState           - Current execution state
 *              ObjDesc             - The named field
 *              RetBufferDesc       - Where the return data object is stored
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from a named field. Returns either an Integer or a
 *              Buffer, depending on the size of the field.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExReadDataFromField (
    ACPI_WALK_STATE         *WalkState,
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_OPERAND_OBJECT     **RetBufferDesc)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *BufferDesc;
    void                    *Buffer;
    UINT32                  BufferLength;


    ACPI_FUNCTION_TRACE_PTR (ExReadDataFromField, ObjDesc);


    /* Parameter validation */

    if (!ObjDesc)
    {
        return_ACPI_STATUS (AE_AML_NO_OPERAND);
    }
    if (!RetBufferDesc)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    if (ObjDesc->Common.Type == ACPI_TYPE_BUFFER_FIELD)
    {
        /*
         * If the BufferField arguments have not been previously evaluated,
         * evaluate them now and save the results.
         */
        if (!(ObjDesc->Common.Flags & AOPOBJ_DATA_VALID))
        {
            Status = AcpiDsGetBufferFieldArguments (ObjDesc);
            if (ACPI_FAILURE (Status))
            {
                return_ACPI_STATUS (Status);
            }
        }
    }
    else if ((ObjDesc->Common.Type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
        (ObjDesc->Field.RegionObj->Region.SpaceId == ACPI_ADR_SPACE_SMBUS ||
         ObjDesc->Field.RegionObj->Region.SpaceId == ACPI_ADR_SPACE_GSBUS ||
         ObjDesc->Field.RegionObj->Region.SpaceId == ACPI_ADR_SPACE_IPMI))
    {
        /* SMBus, GSBus, IPMI serial */

        Status = AcpiExReadSerialBus (ObjDesc, RetBufferDesc);
        return_ACPI_STATUS (Status);
    }

    /*
     * Allocate a buffer for the contents of the field.
     *
     * If the field is larger than the current integer width, create
     * a BUFFER to hold it. Otherwise, use an INTEGER. This allows
     * the use of arithmetic operators on the returned value if the
     * field size is equal or smaller than an Integer.
     *
     * Note: Field.length is in bits.
     */
    BufferLength = (ACPI_SIZE) ACPI_ROUND_BITS_UP_TO_BYTES (
        ObjDesc->Field.BitLength);

    if (BufferLength > AcpiGbl_IntegerByteWidth)
    {
        /* Field is too large for an Integer, create a Buffer instead */

        BufferDesc = AcpiUtCreateBufferObject (BufferLength);
        if (!BufferDesc)
        {
            return_ACPI_STATUS (AE_NO_MEMORY);
        }
        Buffer = BufferDesc->Buffer.Pointer;
    }
    else
    {
        /* Field will fit within an Integer (normal case) */

        BufferDesc = AcpiUtCreateIntegerObject ((UINT64) 0);
        if (!BufferDesc)
        {
            return_ACPI_STATUS (AE_NO_MEMORY);
        }

        BufferLength = AcpiGbl_IntegerByteWidth;
        Buffer = &BufferDesc->Integer.Value;
    }

    if ((ObjDesc->Common.Type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
        (ObjDesc->Field.RegionObj->Region.SpaceId == ACPI_ADR_SPACE_GPIO))
    {
        /* General Purpose I/O */

        Status = AcpiExReadGpio (ObjDesc, Buffer);
        goto Exit;
    }
    else if ((ObjDesc->Common.Type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
        (ObjDesc->Field.RegionObj->Region.SpaceId == ACPI_ADR_SPACE_PLATFORM_COMM))
    {
        /*
         * Reading from a PCC field unit does not require the handler because
         * it only requires reading from the InternalPccBuffer.
         */
        ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
            "PCC FieldRead bits %u\n", ObjDesc->Field.BitLength));

        memcpy (Buffer, ObjDesc->Field.RegionObj->Field.InternalPccBuffer +
        ObjDesc->Field.BaseByteOffset, (ACPI_SIZE) ACPI_ROUND_BITS_UP_TO_BYTES (
            ObjDesc->Field.BitLength));

        *RetBufferDesc = BufferDesc;
        return AE_OK;
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
        "FieldRead [TO]:   Obj %p, Type %X, Buf %p, ByteLen %X\n",
        ObjDesc, ObjDesc->Common.Type, Buffer, BufferLength));
    ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
        "FieldRead [FROM]: BitLen %X, BitOff %X, ByteOff %X\n",
        ObjDesc->CommonField.BitLength,
        ObjDesc->CommonField.StartFieldBitOffset,
        ObjDesc->CommonField.BaseByteOffset));

    /* Lock entire transaction if requested */

    AcpiExAcquireGlobalLock (ObjDesc->CommonField.FieldFlags);

    /* Read from the field */

    Status = AcpiExExtractFromField (ObjDesc, Buffer, BufferLength);
    AcpiExReleaseGlobalLock (ObjDesc->CommonField.FieldFlags);


Exit:
    if (ACPI_FAILURE (Status))
    {
        AcpiUtRemoveReference (BufferDesc);
    }
    else
    {
        *RetBufferDesc = BufferDesc;
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExWriteDataToField
 *
 * PARAMETERS:  SourceDesc          - Contains data to write
 *              ObjDesc             - The named field
 *              ResultDesc          - Where the return value is returned, if any
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to a named field
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExWriteDataToField (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_OPERAND_OBJECT     **ResultDesc)
{
    ACPI_STATUS             Status;
    UINT32                  BufferLength;
    UINT32                  DataLength;
    void                    *Buffer;


    ACPI_FUNCTION_TRACE_PTR (ExWriteDataToField, ObjDesc);


    /* Parameter validation */

    if (!SourceDesc || !ObjDesc)
    {
        return_ACPI_STATUS (AE_AML_NO_OPERAND);
    }

    if (ObjDesc->Common.Type == ACPI_TYPE_BUFFER_FIELD)
    {
        /*
         * If the BufferField arguments have not been previously evaluated,
         * evaluate them now and save the results.
         */
        if (!(ObjDesc->Common.Flags & AOPOBJ_DATA_VALID))
        {
            Status = AcpiDsGetBufferFieldArguments (ObjDesc);
            if (ACPI_FAILURE (Status))
            {
                return_ACPI_STATUS (Status);
            }
        }
    }
    else if ((ObjDesc->Common.Type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
        (ObjDesc->Field.RegionObj->Region.SpaceId == ACPI_ADR_SPACE_GPIO))
    {
        /* General Purpose I/O */

        Status = AcpiExWriteGpio (SourceDesc, ObjDesc, ResultDesc);
        return_ACPI_STATUS (Status);
    }
    else if ((ObjDesc->Common.Type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
        (ObjDesc->Field.RegionObj->Region.SpaceId == ACPI_ADR_SPACE_SMBUS ||
         ObjDesc->Field.RegionObj->Region.SpaceId == ACPI_ADR_SPACE_GSBUS ||
         ObjDesc->Field.RegionObj->Region.SpaceId == ACPI_ADR_SPACE_IPMI))
    {
        /* SMBus, GSBus, IPMI serial */

        Status = AcpiExWriteSerialBus (SourceDesc, ObjDesc, ResultDesc);
        return_ACPI_STATUS (Status);
    }
    else if ((ObjDesc->Common.Type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
             (ObjDesc->Field.RegionObj->Region.SpaceId == ACPI_ADR_SPACE_PLATFORM_COMM))
    {
        /*
         * According to the spec a write to the COMD field will invoke the
         * region handler. Otherwise, write to the PccInternal buffer. This
         * implementation will use the offsets specified rather than the name
         * of the field. This is considered safer because some firmware tools
         * are known to obfiscate named objects.
         */
        DataLength = (ACPI_SIZE) ACPI_ROUND_BITS_UP_TO_BYTES (
            ObjDesc->Field.BitLength);
        memcpy (ObjDesc->Field.RegionObj->Field.InternalPccBuffer +
            ObjDesc->Field.BaseByteOffset,
            SourceDesc->Buffer.Pointer, DataLength);

        if ((ObjDesc->Field.RegionObj->Region.Address == PCC_MASTER_SUBSPACE &&
           MASTER_SUBSPACE_COMMAND (ObjDesc->Field.BaseByteOffset)) ||
           GENERIC_SUBSPACE_COMMAND (ObjDesc->Field.BaseByteOffset))
        {
            /* Perform the write */

            ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
                "PCC COMD field has been written. Invoking PCC handler now.\n"));

            Status = AcpiExAccessRegion (
                ObjDesc, 0, (UINT64 *) ObjDesc->Field.RegionObj->Field.InternalPccBuffer,
                ACPI_WRITE);
            return_ACPI_STATUS (Status);
        }
        return (AE_OK);
    }


    /* Get a pointer to the data to be written */

    switch (SourceDesc->Common.Type)
    {
    case ACPI_TYPE_INTEGER:

        Buffer = &SourceDesc->Integer.Value;
        BufferLength = sizeof (SourceDesc->Integer.Value);
        break;

    case ACPI_TYPE_BUFFER:

        Buffer = SourceDesc->Buffer.Pointer;
        BufferLength = SourceDesc->Buffer.Length;
        break;

    case ACPI_TYPE_STRING:

        Buffer = SourceDesc->String.Pointer;
        BufferLength = SourceDesc->String.Length;
        break;

    default:
        return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
        "FieldWrite [FROM]: Obj %p (%s:%X), Buf %p, ByteLen %X\n",
        SourceDesc, AcpiUtGetTypeName (SourceDesc->Common.Type),
        SourceDesc->Common.Type, Buffer, BufferLength));

    ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
        "FieldWrite [TO]:   Obj %p (%s:%X), BitLen %X, BitOff %X, ByteOff %X\n",
        ObjDesc, AcpiUtGetTypeName (ObjDesc->Common.Type),
        ObjDesc->Common.Type,
        ObjDesc->CommonField.BitLength,
        ObjDesc->CommonField.StartFieldBitOffset,
        ObjDesc->CommonField.BaseByteOffset));

    /* Lock entire transaction if requested */

    AcpiExAcquireGlobalLock (ObjDesc->CommonField.FieldFlags);

    /* Write to the field */

    Status = AcpiExInsertIntoField (ObjDesc, Buffer, BufferLength);
    AcpiExReleaseGlobalLock (ObjDesc->CommonField.FieldFlags);
    return_ACPI_STATUS (Status);
}

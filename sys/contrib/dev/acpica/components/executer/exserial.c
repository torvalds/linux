/******************************************************************************
 *
 * Module Name: exserial - FieldUnit support for serial address spaces
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
        ACPI_MODULE_NAME    ("exserial")


/*******************************************************************************
 *
 * FUNCTION:    AcpiExReadGpio
 *
 * PARAMETERS:  ObjDesc             - The named field to read
 *              Buffer              - Where the return data is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from a named field that references a Generic Serial Bus
 *              field
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExReadGpio (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    void                    *Buffer)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE_PTR (ExReadGpio, ObjDesc);


    /*
     * For GPIO (GeneralPurposeIo), the Address will be the bit offset
     * from the previous Connection() operator, making it effectively a
     * pin number index. The BitLength is the length of the field, which
     * is thus the number of pins.
     */
    ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
        "GPIO FieldRead [FROM]:  Pin %u Bits %u\n",
        ObjDesc->Field.PinNumberIndex, ObjDesc->Field.BitLength));

    /* Lock entire transaction if requested */

    AcpiExAcquireGlobalLock (ObjDesc->CommonField.FieldFlags);

    /* Perform the read */

    Status = AcpiExAccessRegion (
        ObjDesc, 0, (UINT64 *) Buffer, ACPI_READ);

    AcpiExReleaseGlobalLock (ObjDesc->CommonField.FieldFlags);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExWriteGpio
 *
 * PARAMETERS:  SourceDesc          - Contains data to write. Expect to be
 *                                    an Integer object.
 *              ObjDesc             - The named field
 *              ResultDesc          - Where the return value is returned, if any
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to a named field that references a General Purpose I/O
 *              field.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExWriteGpio (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_OPERAND_OBJECT     **ReturnBuffer)
{
    ACPI_STATUS             Status;
    void                    *Buffer;


    ACPI_FUNCTION_TRACE_PTR (ExWriteGpio, ObjDesc);


    /*
     * For GPIO (GeneralPurposeIo), we will bypass the entire field
     * mechanism and handoff the bit address and bit width directly to
     * the handler. The Address will be the bit offset
     * from the previous Connection() operator, making it effectively a
     * pin number index. The BitLength is the length of the field, which
     * is thus the number of pins.
     */
    if (SourceDesc->Common.Type != ACPI_TYPE_INTEGER)
    {
        return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
        "GPIO FieldWrite [FROM]: (%s:%X), Value %.8X  [TO]: Pin %u Bits %u\n",
        AcpiUtGetTypeName (SourceDesc->Common.Type),
        SourceDesc->Common.Type, (UINT32) SourceDesc->Integer.Value,
        ObjDesc->Field.PinNumberIndex, ObjDesc->Field.BitLength));

    Buffer = &SourceDesc->Integer.Value;

    /* Lock entire transaction if requested */

    AcpiExAcquireGlobalLock (ObjDesc->CommonField.FieldFlags);

    /* Perform the write */

    Status = AcpiExAccessRegion (
        ObjDesc, 0, (UINT64 *) Buffer, ACPI_WRITE);
    AcpiExReleaseGlobalLock (ObjDesc->CommonField.FieldFlags);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExReadSerialBus
 *
 * PARAMETERS:  ObjDesc             - The named field to read
 *              ReturnBuffer        - Where the return value is returned, if any
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from a named field that references a serial bus
 *              (SMBus, IPMI, or GSBus).
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExReadSerialBus (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_OPERAND_OBJECT     **ReturnBuffer)
{
    ACPI_STATUS             Status;
    UINT32                  BufferLength;
    ACPI_OPERAND_OBJECT     *BufferDesc;
    UINT32                  Function;
    UINT16                  AccessorType;


    ACPI_FUNCTION_TRACE_PTR (ExReadSerialBus, ObjDesc);


    /*
     * This is an SMBus, GSBus or IPMI read. We must create a buffer to
     * hold the data and then directly access the region handler.
     *
     * Note: SMBus and GSBus protocol value is passed in upper 16-bits
     * of Function
     *
     * Common buffer format:
     *     Status;    (Byte 0 of the data buffer)
     *     Length;    (Byte 1 of the data buffer)
     *     Data[x-1]: (Bytes 2-x of the arbitrary length data buffer)
     */
    switch (ObjDesc->Field.RegionObj->Region.SpaceId)
    {
    case ACPI_ADR_SPACE_SMBUS:

        BufferLength = ACPI_SMBUS_BUFFER_SIZE;
        Function = ACPI_READ | (ObjDesc->Field.Attribute << 16);
        break;

    case ACPI_ADR_SPACE_IPMI:

        BufferLength = ACPI_IPMI_BUFFER_SIZE;
        Function = ACPI_READ;
        break;

    case ACPI_ADR_SPACE_GSBUS:

        AccessorType = ObjDesc->Field.Attribute;
        if (AccessorType == AML_FIELD_ATTRIB_RAW_PROCESS_BYTES)
        {
            ACPI_ERROR ((AE_INFO,
                "Invalid direct read using bidirectional write-then-read protocol"));

            return_ACPI_STATUS (AE_AML_PROTOCOL);
        }

        Status = AcpiExGetProtocolBufferLength (AccessorType, &BufferLength);
        if (ACPI_FAILURE (Status))
        {
            ACPI_ERROR ((AE_INFO,
                "Invalid protocol ID for GSBus: 0x%4.4X", AccessorType));

            return_ACPI_STATUS (Status);
        }

        /* Add header length to get the full size of the buffer */

        BufferLength += ACPI_SERIAL_HEADER_SIZE;
        Function = ACPI_READ | (AccessorType << 16);
        break;

    default:
        return_ACPI_STATUS (AE_AML_INVALID_SPACE_ID);
    }

    /* Create the local transfer buffer that is returned to the caller */

    BufferDesc = AcpiUtCreateBufferObject (BufferLength);
    if (!BufferDesc)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Lock entire transaction if requested */

    AcpiExAcquireGlobalLock (ObjDesc->CommonField.FieldFlags);

    /* Call the region handler for the write-then-read */

    Status = AcpiExAccessRegion (ObjDesc, 0,
        ACPI_CAST_PTR (UINT64, BufferDesc->Buffer.Pointer), Function);
    AcpiExReleaseGlobalLock (ObjDesc->CommonField.FieldFlags);

    *ReturnBuffer = BufferDesc;
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExWriteSerialBus
 *
 * PARAMETERS:  SourceDesc          - Contains data to write
 *              ObjDesc             - The named field
 *              ReturnBuffer        - Where the return value is returned, if any
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to a named field that references a serial bus
 *              (SMBus, IPMI, GSBus).
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExWriteSerialBus (
    ACPI_OPERAND_OBJECT     *SourceDesc,
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_OPERAND_OBJECT     **ReturnBuffer)
{
    ACPI_STATUS             Status;
    UINT32                  BufferLength;
    UINT32                  DataLength;
    void                    *Buffer;
    ACPI_OPERAND_OBJECT     *BufferDesc;
    UINT32                  Function;
    UINT16                  AccessorType;


    ACPI_FUNCTION_TRACE_PTR (ExWriteSerialBus, ObjDesc);


    /*
     * This is an SMBus, GSBus or IPMI write. We will bypass the entire
     * field mechanism and handoff the buffer directly to the handler.
     * For these address spaces, the buffer is bidirectional; on a
     * write, return data is returned in the same buffer.
     *
     * Source must be a buffer of sufficient size, these are fixed size:
     * ACPI_SMBUS_BUFFER_SIZE, or ACPI_IPMI_BUFFER_SIZE.
     *
     * Note: SMBus and GSBus protocol type is passed in upper 16-bits
     * of Function
     *
     * Common buffer format:
     *     Status;    (Byte 0 of the data buffer)
     *     Length;    (Byte 1 of the data buffer)
     *     Data[x-1]: (Bytes 2-x of the arbitrary length data buffer)
     */
    if (SourceDesc->Common.Type != ACPI_TYPE_BUFFER)
    {
        ACPI_ERROR ((AE_INFO,
            "SMBus/IPMI/GenericSerialBus write requires "
            "Buffer, found type %s",
            AcpiUtGetObjectTypeName (SourceDesc)));

        return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
    }

    switch (ObjDesc->Field.RegionObj->Region.SpaceId)
    {
    case ACPI_ADR_SPACE_SMBUS:

        BufferLength = ACPI_SMBUS_BUFFER_SIZE;
        Function = ACPI_WRITE | (ObjDesc->Field.Attribute << 16);
        break;

    case ACPI_ADR_SPACE_IPMI:

        BufferLength = ACPI_IPMI_BUFFER_SIZE;
        Function = ACPI_WRITE;
        break;

    case ACPI_ADR_SPACE_GSBUS:

        AccessorType = ObjDesc->Field.Attribute;
        Status = AcpiExGetProtocolBufferLength (AccessorType, &BufferLength);
        if (ACPI_FAILURE (Status))
        {
            ACPI_ERROR ((AE_INFO,
                "Invalid protocol ID for GSBus: 0x%4.4X", AccessorType));

            return_ACPI_STATUS (Status);
        }

        /* Add header length to get the full size of the buffer */

        BufferLength += ACPI_SERIAL_HEADER_SIZE;
        Function = ACPI_WRITE | (AccessorType << 16);
        break;

    default:
        return_ACPI_STATUS (AE_AML_INVALID_SPACE_ID);
    }

    /* Create the transfer/bidirectional/return buffer */

    BufferDesc = AcpiUtCreateBufferObject (BufferLength);
    if (!BufferDesc)
    {
        return_ACPI_STATUS (AE_NO_MEMORY);
    }

    /* Copy the input buffer data to the transfer buffer */

    Buffer = BufferDesc->Buffer.Pointer;
    DataLength = (BufferLength < SourceDesc->Buffer.Length ?
        BufferLength : SourceDesc->Buffer.Length);
    memcpy (Buffer, SourceDesc->Buffer.Pointer, DataLength);

    /* Lock entire transaction if requested */

    AcpiExAcquireGlobalLock (ObjDesc->CommonField.FieldFlags);

    /*
     * Perform the write (returns status and perhaps data in the
     * same buffer)
     */
    Status = AcpiExAccessRegion (
        ObjDesc, 0, (UINT64 *) Buffer, Function);
    AcpiExReleaseGlobalLock (ObjDesc->CommonField.FieldFlags);

    *ReturnBuffer = BufferDesc;
    return_ACPI_STATUS (Status);
}

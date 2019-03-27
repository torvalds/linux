/******************************************************************************
 *
 * Module Name: exfldio - Aml Field I/O
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
#include <contrib/dev/acpica/include/acevents.h>
#include <contrib/dev/acpica/include/acdispat.h>


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("exfldio")

/* Local prototypes */

static ACPI_STATUS
AcpiExFieldDatumIo (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    UINT32                  FieldDatumByteOffset,
    UINT64                  *Value,
    UINT32                  ReadWrite);

static BOOLEAN
AcpiExRegisterOverflow (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    UINT64                  Value);

static ACPI_STATUS
AcpiExSetupRegion (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    UINT32                  FieldDatumByteOffset);


/*******************************************************************************
 *
 * FUNCTION:    AcpiExSetupRegion
 *
 * PARAMETERS:  ObjDesc                 - Field to be read or written
 *              FieldDatumByteOffset    - Byte offset of this datum within the
 *                                        parent field
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Common processing for AcpiExExtractFromField and
 *              AcpiExInsertIntoField. Initialize the Region if necessary and
 *              validate the request.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiExSetupRegion (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    UINT32                  FieldDatumByteOffset)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_OPERAND_OBJECT     *RgnDesc;
    UINT8                   SpaceId;


    ACPI_FUNCTION_TRACE_U32 (ExSetupRegion, FieldDatumByteOffset);


    RgnDesc = ObjDesc->CommonField.RegionObj;

    /* We must have a valid region */

    if (RgnDesc->Common.Type != ACPI_TYPE_REGION)
    {
        ACPI_ERROR ((AE_INFO, "Needed Region, found type 0x%X (%s)",
            RgnDesc->Common.Type,
            AcpiUtGetObjectTypeName (RgnDesc)));

        return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
    }

    SpaceId = RgnDesc->Region.SpaceId;

    /* Validate the Space ID */

    if (!AcpiIsValidSpaceId (SpaceId))
    {
        ACPI_ERROR ((AE_INFO,
            "Invalid/unknown Address Space ID: 0x%2.2X", SpaceId));
        return_ACPI_STATUS (AE_AML_INVALID_SPACE_ID);
    }

    /*
     * If the Region Address and Length have not been previously evaluated,
     * evaluate them now and save the results.
     */
    if (!(RgnDesc->Common.Flags & AOPOBJ_DATA_VALID))
    {
        Status = AcpiDsGetRegionArguments (RgnDesc);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }
    }

    /*
     * Exit now for SMBus, GSBus or IPMI address space, it has a non-linear
     * address space and the request cannot be directly validated
     */
    if (SpaceId == ACPI_ADR_SPACE_SMBUS ||
        SpaceId == ACPI_ADR_SPACE_GSBUS ||
        SpaceId == ACPI_ADR_SPACE_IPMI)
    {
        /* SMBus or IPMI has a non-linear address space */

        return_ACPI_STATUS (AE_OK);
    }

#ifdef ACPI_UNDER_DEVELOPMENT
    /*
     * If the Field access is AnyAcc, we can now compute the optimal
     * access (because we know know the length of the parent region)
     */
    if (!(ObjDesc->Common.Flags & AOPOBJ_DATA_VALID))
    {
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }
    }
#endif

    /*
     * Validate the request. The entire request from the byte offset for a
     * length of one field datum (access width) must fit within the region.
     * (Region length is specified in bytes)
     */
    if (RgnDesc->Region.Length <
        (ObjDesc->CommonField.BaseByteOffset + FieldDatumByteOffset +
        ObjDesc->CommonField.AccessByteWidth))
    {
        if (AcpiGbl_EnableInterpreterSlack)
        {
            /*
             * Slack mode only:  We will go ahead and allow access to this
             * field if it is within the region length rounded up to the next
             * access width boundary. ACPI_SIZE cast for 64-bit compile.
             */
            if (ACPI_ROUND_UP (RgnDesc->Region.Length,
                    ObjDesc->CommonField.AccessByteWidth) >=
                ((ACPI_SIZE) ObjDesc->CommonField.BaseByteOffset +
                    ObjDesc->CommonField.AccessByteWidth +
                    FieldDatumByteOffset))
            {
                return_ACPI_STATUS (AE_OK);
            }
        }

        if (RgnDesc->Region.Length < ObjDesc->CommonField.AccessByteWidth)
        {
            /*
             * This is the case where the AccessType (AccWord, etc.) is wider
             * than the region itself. For example, a region of length one
             * byte, and a field with Dword access specified.
             */
            ACPI_ERROR ((AE_INFO,
                "Field [%4.4s] access width (%u bytes) "
                "too large for region [%4.4s] (length %u)",
                AcpiUtGetNodeName (ObjDesc->CommonField.Node),
                ObjDesc->CommonField.AccessByteWidth,
                AcpiUtGetNodeName (RgnDesc->Region.Node),
                RgnDesc->Region.Length));
        }

        /*
         * Offset rounded up to next multiple of field width
         * exceeds region length, indicate an error
         */
        ACPI_ERROR ((AE_INFO,
            "Field [%4.4s] Base+Offset+Width %u+%u+%u "
            "is beyond end of region [%4.4s] (length %u)",
            AcpiUtGetNodeName (ObjDesc->CommonField.Node),
            ObjDesc->CommonField.BaseByteOffset,
            FieldDatumByteOffset, ObjDesc->CommonField.AccessByteWidth,
            AcpiUtGetNodeName (RgnDesc->Region.Node),
            RgnDesc->Region.Length));

        return_ACPI_STATUS (AE_AML_REGION_LIMIT);
    }

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExAccessRegion
 *
 * PARAMETERS:  ObjDesc                 - Field to be read
 *              FieldDatumByteOffset    - Byte offset of this datum within the
 *                                        parent field
 *              Value                   - Where to store value (must at least
 *                                        64 bits)
 *              Function                - Read or Write flag plus other region-
 *                                        dependent flags
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or Write a single field datum to an Operation Region.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExAccessRegion (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    UINT32                  FieldDatumByteOffset,
    UINT64                  *Value,
    UINT32                  Function)
{
    ACPI_STATUS             Status;
    ACPI_OPERAND_OBJECT     *RgnDesc;
    UINT32                  RegionOffset;


    ACPI_FUNCTION_TRACE (ExAccessRegion);


    /*
     * Ensure that the region operands are fully evaluated and verify
     * the validity of the request
     */
    Status = AcpiExSetupRegion (ObjDesc, FieldDatumByteOffset);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /*
     * The physical address of this field datum is:
     *
     * 1) The base of the region, plus
     * 2) The base offset of the field, plus
     * 3) The current offset into the field
     */
    RgnDesc = ObjDesc->CommonField.RegionObj;
    RegionOffset =
        ObjDesc->CommonField.BaseByteOffset +
        FieldDatumByteOffset;

    if ((Function & ACPI_IO_MASK) == ACPI_READ)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "[READ]"));
    }
    else
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "[WRITE]"));
    }

    ACPI_DEBUG_PRINT_RAW ((ACPI_DB_BFIELD,
        " Region [%s:%X], Width %X, ByteBase %X, Offset %X at %8.8X%8.8X\n",
        AcpiUtGetRegionName (RgnDesc->Region.SpaceId),
        RgnDesc->Region.SpaceId,
        ObjDesc->CommonField.AccessByteWidth,
        ObjDesc->CommonField.BaseByteOffset,
        FieldDatumByteOffset,
        ACPI_FORMAT_UINT64 (RgnDesc->Region.Address + RegionOffset)));

    /* Invoke the appropriate AddressSpace/OpRegion handler */

    Status = AcpiEvAddressSpaceDispatch (RgnDesc, ObjDesc,
        Function, RegionOffset,
        ACPI_MUL_8 (ObjDesc->CommonField.AccessByteWidth), Value);

    if (ACPI_FAILURE (Status))
    {
        if (Status == AE_NOT_IMPLEMENTED)
        {
            ACPI_ERROR ((AE_INFO,
                "Region %s (ID=%u) not implemented",
                AcpiUtGetRegionName (RgnDesc->Region.SpaceId),
                RgnDesc->Region.SpaceId));
        }
        else if (Status == AE_NOT_EXIST)
        {
            ACPI_ERROR ((AE_INFO,
                "Region %s (ID=%u) has no handler",
                AcpiUtGetRegionName (RgnDesc->Region.SpaceId),
                RgnDesc->Region.SpaceId));
        }
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExRegisterOverflow
 *
 * PARAMETERS:  ObjDesc                 - Register(Field) to be written
 *              Value                   - Value to be stored
 *
 * RETURN:      TRUE if value overflows the field, FALSE otherwise
 *
 * DESCRIPTION: Check if a value is out of range of the field being written.
 *              Used to check if the values written to Index and Bank registers
 *              are out of range. Normally, the value is simply truncated
 *              to fit the field, but this case is most likely a serious
 *              coding error in the ASL.
 *
 ******************************************************************************/

static BOOLEAN
AcpiExRegisterOverflow (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    UINT64                  Value)
{

    if (ObjDesc->CommonField.BitLength >= ACPI_INTEGER_BIT_SIZE)
    {
        /*
         * The field is large enough to hold the maximum integer, so we can
         * never overflow it.
         */
        return (FALSE);
    }

    if (Value >= ((UINT64) 1 << ObjDesc->CommonField.BitLength))
    {
        /*
         * The Value is larger than the maximum value that can fit into
         * the register.
         */
        ACPI_ERROR ((AE_INFO,
            "Index value 0x%8.8X%8.8X overflows field width 0x%X",
            ACPI_FORMAT_UINT64 (Value),
            ObjDesc->CommonField.BitLength));

        return (TRUE);
    }

    /* The Value will fit into the field with no truncation */

    return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExFieldDatumIo
 *
 * PARAMETERS:  ObjDesc                 - Field to be read
 *              FieldDatumByteOffset    - Byte offset of this datum within the
 *                                        parent field
 *              Value                   - Where to store value (must be 64 bits)
 *              ReadWrite               - Read or Write flag
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or Write a single datum of a field. The FieldType is
 *              demultiplexed here to handle the different types of fields
 *              (BufferField, RegionField, IndexField, BankField)
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiExFieldDatumIo (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    UINT32                  FieldDatumByteOffset,
    UINT64                  *Value,
    UINT32                  ReadWrite)
{
    ACPI_STATUS             Status;
    UINT64                  LocalValue;


    ACPI_FUNCTION_TRACE_U32 (ExFieldDatumIo, FieldDatumByteOffset);


    if (ReadWrite == ACPI_READ)
    {
        if (!Value)
        {
            LocalValue = 0;

            /* To support reads without saving return value */
            Value = &LocalValue;
        }

        /* Clear the entire return buffer first, [Very Important!] */

        *Value = 0;
    }

    /*
     * The four types of fields are:
     *
     * BufferField - Read/write from/to a Buffer
     * RegionField - Read/write from/to a Operation Region.
     * BankField   - Write to a Bank Register, then read/write from/to an
     *               OperationRegion
     * IndexField  - Write to an Index Register, then read/write from/to a
     *               Data Register
     */
    switch (ObjDesc->Common.Type)
    {
    case ACPI_TYPE_BUFFER_FIELD:
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

        if (ReadWrite == ACPI_READ)
        {
            /*
             * Copy the data from the source buffer.
             * Length is the field width in bytes.
             */
            memcpy (Value,
                (ObjDesc->BufferField.BufferObj)->Buffer.Pointer +
                    ObjDesc->BufferField.BaseByteOffset +
                    FieldDatumByteOffset,
                ObjDesc->CommonField.AccessByteWidth);
        }
        else
        {
            /*
             * Copy the data to the target buffer.
             * Length is the field width in bytes.
             */
            memcpy ((ObjDesc->BufferField.BufferObj)->Buffer.Pointer +
                ObjDesc->BufferField.BaseByteOffset +
                FieldDatumByteOffset,
                Value, ObjDesc->CommonField.AccessByteWidth);
        }

        Status = AE_OK;
        break;

    case ACPI_TYPE_LOCAL_BANK_FIELD:
        /*
         * Ensure that the BankValue is not beyond the capacity of
         * the register
         */
        if (AcpiExRegisterOverflow (ObjDesc->BankField.BankObj,
                (UINT64) ObjDesc->BankField.Value))
        {
            return_ACPI_STATUS (AE_AML_REGISTER_LIMIT);
        }

        /*
         * For BankFields, we must write the BankValue to the BankRegister
         * (itself a RegionField) before we can access the data.
         */
        Status = AcpiExInsertIntoField (ObjDesc->BankField.BankObj,
                    &ObjDesc->BankField.Value,
                    sizeof (ObjDesc->BankField.Value));
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

        /*
         * Now that the Bank has been selected, fall through to the
         * RegionField case and write the datum to the Operation Region
         */

        /*lint -fallthrough */

    case ACPI_TYPE_LOCAL_REGION_FIELD:
        /*
         * For simple RegionFields, we just directly access the owning
         * Operation Region.
         */
        Status = AcpiExAccessRegion (
            ObjDesc, FieldDatumByteOffset, Value, ReadWrite);
        break;

    case ACPI_TYPE_LOCAL_INDEX_FIELD:
        /*
         * Ensure that the IndexValue is not beyond the capacity of
         * the register
         */
        if (AcpiExRegisterOverflow (ObjDesc->IndexField.IndexObj,
                (UINT64) ObjDesc->IndexField.Value))
        {
            return_ACPI_STATUS (AE_AML_REGISTER_LIMIT);
        }

        /* Write the index value to the IndexRegister (itself a RegionField) */

        FieldDatumByteOffset += ObjDesc->IndexField.Value;

        ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
            "Write to Index Register: Value %8.8X\n",
            FieldDatumByteOffset));

        Status = AcpiExInsertIntoField (ObjDesc->IndexField.IndexObj,
            &FieldDatumByteOffset, sizeof (FieldDatumByteOffset));
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

        if (ReadWrite == ACPI_READ)
        {
            /* Read the datum from the DataRegister */

            ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
                "Read from Data Register\n"));

            Status = AcpiExExtractFromField (
                ObjDesc->IndexField.DataObj, Value, sizeof (UINT64));
        }
        else
        {
            /* Write the datum to the DataRegister */

            ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
                "Write to Data Register: Value %8.8X%8.8X\n",
                ACPI_FORMAT_UINT64 (*Value)));

            Status = AcpiExInsertIntoField (
                ObjDesc->IndexField.DataObj, Value, sizeof (UINT64));
        }
        break;

    default:

        ACPI_ERROR ((AE_INFO, "Wrong object type in field I/O %u",
            ObjDesc->Common.Type));
        Status = AE_AML_INTERNAL;
        break;
    }

    if (ACPI_SUCCESS (Status))
    {
        if (ReadWrite == ACPI_READ)
        {
            ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
                "Value Read %8.8X%8.8X, Width %u\n",
                ACPI_FORMAT_UINT64 (*Value),
                ObjDesc->CommonField.AccessByteWidth));
        }
        else
        {
            ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
                "Value Written %8.8X%8.8X, Width %u\n",
                ACPI_FORMAT_UINT64 (*Value),
                ObjDesc->CommonField.AccessByteWidth));
        }
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExWriteWithUpdateRule
 *
 * PARAMETERS:  ObjDesc                 - Field to be written
 *              Mask                    - bitmask within field datum
 *              FieldValue              - Value to write
 *              FieldDatumByteOffset    - Offset of datum within field
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Apply the field update rule to a field write
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExWriteWithUpdateRule (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    UINT64                  Mask,
    UINT64                  FieldValue,
    UINT32                  FieldDatumByteOffset)
{
    ACPI_STATUS             Status = AE_OK;
    UINT64                  MergedValue;
    UINT64                  CurrentValue;


    ACPI_FUNCTION_TRACE_U32 (ExWriteWithUpdateRule, Mask);


    /* Start with the new bits  */

    MergedValue = FieldValue;

    /* If the mask is all ones, we don't need to worry about the update rule */

    if (Mask != ACPI_UINT64_MAX)
    {
        /* Decode the update rule */

        switch (ObjDesc->CommonField.FieldFlags & AML_FIELD_UPDATE_RULE_MASK)
        {
        case AML_FIELD_UPDATE_PRESERVE:
            /*
             * Check if update rule needs to be applied (not if mask is all
             * ones)  The left shift drops the bits we want to ignore.
             */
            if ((~Mask << (ACPI_MUL_8 (sizeof (Mask)) -
                   ACPI_MUL_8 (ObjDesc->CommonField.AccessByteWidth))) != 0)
            {
                /*
                 * Read the current contents of the byte/word/dword containing
                 * the field, and merge with the new field value.
                 */
                Status = AcpiExFieldDatumIo (
                    ObjDesc, FieldDatumByteOffset, &CurrentValue, ACPI_READ);
                if (ACPI_FAILURE (Status))
                {
                    return_ACPI_STATUS (Status);
                }

                MergedValue |= (CurrentValue & ~Mask);
            }
            break;

        case AML_FIELD_UPDATE_WRITE_AS_ONES:

            /* Set positions outside the field to all ones */

            MergedValue |= ~Mask;
            break;

        case AML_FIELD_UPDATE_WRITE_AS_ZEROS:

            /* Set positions outside the field to all zeros */

            MergedValue &= Mask;
            break;

        default:

            ACPI_ERROR ((AE_INFO,
                "Unknown UpdateRule value: 0x%X",
                (ObjDesc->CommonField.FieldFlags &
                    AML_FIELD_UPDATE_RULE_MASK)));
            return_ACPI_STATUS (AE_AML_OPERAND_VALUE);
        }
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
        "Mask %8.8X%8.8X, DatumOffset %X, Width %X, "
        "Value %8.8X%8.8X, MergedValue %8.8X%8.8X\n",
        ACPI_FORMAT_UINT64 (Mask),
        FieldDatumByteOffset,
        ObjDesc->CommonField.AccessByteWidth,
        ACPI_FORMAT_UINT64 (FieldValue),
        ACPI_FORMAT_UINT64 (MergedValue)));

    /* Write the merged value */

    Status = AcpiExFieldDatumIo (
        ObjDesc, FieldDatumByteOffset, &MergedValue, ACPI_WRITE);

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExExtractFromField
 *
 * PARAMETERS:  ObjDesc             - Field to be read
 *              Buffer              - Where to store the field data
 *              BufferLength        - Length of Buffer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve the current value of the given field
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExExtractFromField (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    void                    *Buffer,
    UINT32                  BufferLength)
{
    ACPI_STATUS             Status;
    UINT64                  RawDatum;
    UINT64                  MergedDatum;
    UINT32                  FieldOffset = 0;
    UINT32                  BufferOffset = 0;
    UINT32                  BufferTailBits;
    UINT32                  DatumCount;
    UINT32                  FieldDatumCount;
    UINT32                  AccessBitWidth;
    UINT32                  i;


    ACPI_FUNCTION_TRACE (ExExtractFromField);


    /* Validate target buffer and clear it */

    if (BufferLength <
        ACPI_ROUND_BITS_UP_TO_BYTES (ObjDesc->CommonField.BitLength))
    {
        ACPI_ERROR ((AE_INFO,
            "Field size %u (bits) is too large for buffer (%u)",
            ObjDesc->CommonField.BitLength, BufferLength));

        return_ACPI_STATUS (AE_BUFFER_OVERFLOW);
    }

    memset (Buffer, 0, BufferLength);
    AccessBitWidth = ACPI_MUL_8 (ObjDesc->CommonField.AccessByteWidth);

    /* Handle the simple case here */

    if ((ObjDesc->CommonField.StartFieldBitOffset == 0) &&
        (ObjDesc->CommonField.BitLength == AccessBitWidth))
    {
        if (BufferLength >= sizeof (UINT64))
        {
            Status = AcpiExFieldDatumIo (ObjDesc, 0, Buffer, ACPI_READ);
        }
        else
        {
            /* Use RawDatum (UINT64) to handle buffers < 64 bits */

            Status = AcpiExFieldDatumIo (ObjDesc, 0, &RawDatum, ACPI_READ);
            memcpy (Buffer, &RawDatum, BufferLength);
        }

        return_ACPI_STATUS (Status);
    }

/* TBD: Move to common setup code */

    /* Field algorithm is limited to sizeof(UINT64), truncate if needed */

    if (ObjDesc->CommonField.AccessByteWidth > sizeof (UINT64))
    {
        ObjDesc->CommonField.AccessByteWidth = sizeof (UINT64);
        AccessBitWidth = sizeof (UINT64) * 8;
    }

    /* Compute the number of datums (access width data items) */

    DatumCount = ACPI_ROUND_UP_TO (
        ObjDesc->CommonField.BitLength, AccessBitWidth);

    FieldDatumCount = ACPI_ROUND_UP_TO (
        ObjDesc->CommonField.BitLength +
        ObjDesc->CommonField.StartFieldBitOffset, AccessBitWidth);

    /* Priming read from the field */

    Status = AcpiExFieldDatumIo (ObjDesc, FieldOffset, &RawDatum, ACPI_READ);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }
    MergedDatum = RawDatum >> ObjDesc->CommonField.StartFieldBitOffset;

    /* Read the rest of the field */

    for (i = 1; i < FieldDatumCount; i++)
    {
        /* Get next input datum from the field */

        FieldOffset += ObjDesc->CommonField.AccessByteWidth;
        Status = AcpiExFieldDatumIo (
            ObjDesc, FieldOffset, &RawDatum, ACPI_READ);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

        /*
         * Merge with previous datum if necessary.
         *
         * Note: Before the shift, check if the shift value will be larger than
         * the integer size. If so, there is no need to perform the operation.
         * This avoids the differences in behavior between different compilers
         * concerning shift values larger than the target data width.
         */
        if (AccessBitWidth - ObjDesc->CommonField.StartFieldBitOffset <
            ACPI_INTEGER_BIT_SIZE)
        {
            MergedDatum |= RawDatum <<
                (AccessBitWidth - ObjDesc->CommonField.StartFieldBitOffset);
        }

        if (i == DatumCount)
        {
            break;
        }

        /* Write merged datum to target buffer */

        memcpy (((char *) Buffer) + BufferOffset, &MergedDatum,
            ACPI_MIN(ObjDesc->CommonField.AccessByteWidth,
                BufferLength - BufferOffset));

        BufferOffset += ObjDesc->CommonField.AccessByteWidth;
        MergedDatum = RawDatum >> ObjDesc->CommonField.StartFieldBitOffset;
    }

    /* Mask off any extra bits in the last datum */

    BufferTailBits = ObjDesc->CommonField.BitLength % AccessBitWidth;
    if (BufferTailBits)
    {
        MergedDatum &= ACPI_MASK_BITS_ABOVE (BufferTailBits);
    }

    /* Write the last datum to the buffer */

    memcpy (((char *) Buffer) + BufferOffset, &MergedDatum,
        ACPI_MIN(ObjDesc->CommonField.AccessByteWidth,
            BufferLength - BufferOffset));

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExInsertIntoField
 *
 * PARAMETERS:  ObjDesc             - Field to be written
 *              Buffer              - Data to be written
 *              BufferLength        - Length of Buffer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the Buffer contents into the given field
 *
 ******************************************************************************/

ACPI_STATUS
AcpiExInsertIntoField (
    ACPI_OPERAND_OBJECT     *ObjDesc,
    void                    *Buffer,
    UINT32                  BufferLength)
{
    void                    *NewBuffer;
    ACPI_STATUS             Status;
    UINT64                  Mask;
    UINT64                  WidthMask;
    UINT64                  MergedDatum;
    UINT64                  RawDatum = 0;
    UINT32                  FieldOffset = 0;
    UINT32                  BufferOffset = 0;
    UINT32                  BufferTailBits;
    UINT32                  DatumCount;
    UINT32                  FieldDatumCount;
    UINT32                  AccessBitWidth;
    UINT32                  RequiredLength;
    UINT32                  i;


    ACPI_FUNCTION_TRACE (ExInsertIntoField);


    /* Validate input buffer */

    NewBuffer = NULL;
    RequiredLength = ACPI_ROUND_BITS_UP_TO_BYTES (
        ObjDesc->CommonField.BitLength);

    /*
     * We must have a buffer that is at least as long as the field
     * we are writing to. This is because individual fields are
     * indivisible and partial writes are not supported -- as per
     * the ACPI specification.
     */
    if (BufferLength < RequiredLength)
    {
        /* We need to create a new buffer */

        NewBuffer = ACPI_ALLOCATE_ZEROED (RequiredLength);
        if (!NewBuffer)
        {
            return_ACPI_STATUS (AE_NO_MEMORY);
        }

        /*
         * Copy the original data to the new buffer, starting
         * at Byte zero. All unused (upper) bytes of the
         * buffer will be 0.
         */
        memcpy ((char *) NewBuffer, (char *) Buffer, BufferLength);
        Buffer = NewBuffer;
        BufferLength = RequiredLength;
    }

/* TBD: Move to common setup code */

    /* Algo is limited to sizeof(UINT64), so cut the AccessByteWidth */
    if (ObjDesc->CommonField.AccessByteWidth > sizeof (UINT64))
    {
        ObjDesc->CommonField.AccessByteWidth = sizeof (UINT64);
    }

    AccessBitWidth = ACPI_MUL_8 (ObjDesc->CommonField.AccessByteWidth);

    /* Create the bitmasks used for bit insertion */

    WidthMask = ACPI_MASK_BITS_ABOVE_64 (AccessBitWidth);
    Mask = WidthMask &
        ACPI_MASK_BITS_BELOW (ObjDesc->CommonField.StartFieldBitOffset);

    /* Compute the number of datums (access width data items) */

    DatumCount = ACPI_ROUND_UP_TO (ObjDesc->CommonField.BitLength,
        AccessBitWidth);

    FieldDatumCount = ACPI_ROUND_UP_TO (ObjDesc->CommonField.BitLength +
        ObjDesc->CommonField.StartFieldBitOffset,
        AccessBitWidth);

    /* Get initial Datum from the input buffer */

    memcpy (&RawDatum, Buffer,
        ACPI_MIN(ObjDesc->CommonField.AccessByteWidth,
            BufferLength - BufferOffset));

    MergedDatum = RawDatum << ObjDesc->CommonField.StartFieldBitOffset;

    /* Write the entire field */

    for (i = 1; i < FieldDatumCount; i++)
    {
        /* Write merged datum to the target field */

        MergedDatum &= Mask;
        Status = AcpiExWriteWithUpdateRule (
            ObjDesc, Mask, MergedDatum, FieldOffset);
        if (ACPI_FAILURE (Status))
        {
            goto Exit;
        }

        FieldOffset += ObjDesc->CommonField.AccessByteWidth;

        /*
         * Start new output datum by merging with previous input datum
         * if necessary.
         *
         * Note: Before the shift, check if the shift value will be larger than
         * the integer size. If so, there is no need to perform the operation.
         * This avoids the differences in behavior between different compilers
         * concerning shift values larger than the target data width.
         */
        if ((AccessBitWidth - ObjDesc->CommonField.StartFieldBitOffset) <
            ACPI_INTEGER_BIT_SIZE)
        {
            MergedDatum = RawDatum >>
                (AccessBitWidth - ObjDesc->CommonField.StartFieldBitOffset);
        }
        else
        {
            MergedDatum = 0;
        }

        Mask = WidthMask;

        if (i == DatumCount)
        {
            break;
        }

        /* Get the next input datum from the buffer */

        BufferOffset += ObjDesc->CommonField.AccessByteWidth;
        memcpy (&RawDatum, ((char *) Buffer) + BufferOffset,
            ACPI_MIN(ObjDesc->CommonField.AccessByteWidth,
                 BufferLength - BufferOffset));

        MergedDatum |= RawDatum << ObjDesc->CommonField.StartFieldBitOffset;
    }

    /* Mask off any extra bits in the last datum */

    BufferTailBits = (ObjDesc->CommonField.BitLength +
        ObjDesc->CommonField.StartFieldBitOffset) % AccessBitWidth;
    if (BufferTailBits)
    {
        Mask &= ACPI_MASK_BITS_ABOVE (BufferTailBits);
    }

    /* Write the last datum to the field */

    MergedDatum &= Mask;
    Status = AcpiExWriteWithUpdateRule (
        ObjDesc, Mask, MergedDatum, FieldOffset);

Exit:
    /* Free temporary buffer if we used one */

    if (NewBuffer)
    {
        ACPI_FREE (NewBuffer);
    }
    return_ACPI_STATUS (Status);
}

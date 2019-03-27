/*******************************************************************************
 *
 * Module Name: dbtest - Various debug-related tests
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
#include <contrib/dev/acpica/include/acdebug.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acpredef.h>
#include <contrib/dev/acpica/include/acinterp.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dbtest")


/* Local prototypes */

static void
AcpiDbTestAllObjects (
    void);

static ACPI_STATUS
AcpiDbTestOneObject (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
AcpiDbTestIntegerType (
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  BitLength);

static ACPI_STATUS
AcpiDbTestBufferType (
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  BitLength);

static ACPI_STATUS
AcpiDbTestStringType (
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  ByteLength);

static ACPI_STATUS
AcpiDbTestPackageType (
    ACPI_NAMESPACE_NODE     *Node);

static ACPI_STATUS
AcpiDbTestFieldUnitType (
    ACPI_OPERAND_OBJECT     *ObjDesc);

static ACPI_STATUS
AcpiDbReadFromObject (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_OBJECT_TYPE        ExpectedType,
    ACPI_OBJECT             **Value);

static ACPI_STATUS
AcpiDbWriteToObject (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_OBJECT             *Value);

static void
AcpiDbEvaluateAllPredefinedNames (
    char                    *CountArg);

static ACPI_STATUS
AcpiDbEvaluateOnePredefinedName (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

/*
 * Test subcommands
 */
static ACPI_DB_ARGUMENT_INFO    AcpiDbTestTypes [] =
{
    {"OBJECTS"},
    {"PREDEFINED"},
    {NULL}           /* Must be null terminated */
};

#define CMD_TEST_OBJECTS        0
#define CMD_TEST_PREDEFINED     1

#define BUFFER_FILL_VALUE       0xFF

/*
 * Support for the special debugger read/write control methods.
 * These methods are installed into the current namespace and are
 * used to read and write the various namespace objects. The point
 * is to force the AML interpreter do all of the work.
 */
#define ACPI_DB_READ_METHOD     "\\_T98"
#define ACPI_DB_WRITE_METHOD    "\\_T99"

static ACPI_HANDLE          ReadHandle = NULL;
static ACPI_HANDLE          WriteHandle = NULL;

/* ASL Definitions of the debugger read/write control methods. AML below. */

#if 0
DefinitionBlock ("ssdt.aml", "SSDT", 2, "Intel", "DEBUG", 0x00000001)
{
    Method (_T98, 1, NotSerialized)     /* Read */
    {
        Return (DeRefOf (Arg0))
    }
}
DefinitionBlock ("ssdt2.aml", "SSDT", 2, "Intel", "DEBUG", 0x00000001)
{
    Method (_T99, 2, NotSerialized)     /* Write */
    {
        Store (Arg1, Arg0)
    }
}
#endif

static unsigned char ReadMethodCode[] =
{
    0x53,0x53,0x44,0x54,0x2E,0x00,0x00,0x00,  /* 00000000    "SSDT...." */
    0x02,0xC9,0x49,0x6E,0x74,0x65,0x6C,0x00,  /* 00000008    "..Intel." */
    0x44,0x45,0x42,0x55,0x47,0x00,0x00,0x00,  /* 00000010    "DEBUG..." */
    0x01,0x00,0x00,0x00,0x49,0x4E,0x54,0x4C,  /* 00000018    "....INTL" */
    0x18,0x12,0x13,0x20,0x14,0x09,0x5F,0x54,  /* 00000020    "... .._T" */
    0x39,0x38,0x01,0xA4,0x83,0x68             /* 00000028    "98...h"   */
};

static unsigned char WriteMethodCode[] =
{
    0x53,0x53,0x44,0x54,0x2E,0x00,0x00,0x00,  /* 00000000    "SSDT...." */
    0x02,0x15,0x49,0x6E,0x74,0x65,0x6C,0x00,  /* 00000008    "..Intel." */
    0x44,0x45,0x42,0x55,0x47,0x00,0x00,0x00,  /* 00000010    "DEBUG..." */
    0x01,0x00,0x00,0x00,0x49,0x4E,0x54,0x4C,  /* 00000018    "....INTL" */
    0x18,0x12,0x13,0x20,0x14,0x09,0x5F,0x54,  /* 00000020    "... .._T" */
    0x39,0x39,0x02,0x70,0x69,0x68             /* 00000028    "99.pih"   */
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbExecuteTest
 *
 * PARAMETERS:  TypeArg         - Subcommand
 *
 * RETURN:      None
 *
 * DESCRIPTION: Execute various debug tests.
 *
 * Note: Code is prepared for future expansion of the TEST command.
 *
 ******************************************************************************/

void
AcpiDbExecuteTest (
    char                    *TypeArg)
{
    UINT32                  Temp;


    AcpiUtStrupr (TypeArg);
    Temp = AcpiDbMatchArgument (TypeArg, AcpiDbTestTypes);
    if (Temp == ACPI_TYPE_NOT_FOUND)
    {
        AcpiOsPrintf ("Invalid or unsupported argument\n");
        return;
    }

    switch (Temp)
    {
    case CMD_TEST_OBJECTS:

        AcpiDbTestAllObjects ();
        break;

    case CMD_TEST_PREDEFINED:

        AcpiDbEvaluateAllPredefinedNames (NULL);
        break;

    default:
        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbTestAllObjects
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: This test implements the OBJECTS subcommand. It exercises the
 *              namespace by reading/writing/comparing all data objects such
 *              as integers, strings, buffers, fields, buffer fields, etc.
 *
 ******************************************************************************/

static void
AcpiDbTestAllObjects (
    void)
{
    ACPI_STATUS             Status;


    /* Install the debugger read-object control method if necessary */

    if (!ReadHandle)
    {
        Status = AcpiInstallMethod (ReadMethodCode);
        if (ACPI_FAILURE (Status))
        {
            AcpiOsPrintf ("%s, Could not install debugger read method\n",
                AcpiFormatException (Status));
            return;
        }

        Status = AcpiGetHandle (NULL, ACPI_DB_READ_METHOD, &ReadHandle);
        if (ACPI_FAILURE (Status))
        {
            AcpiOsPrintf ("Could not obtain handle for debug method %s\n",
                ACPI_DB_READ_METHOD);
            return;
        }
    }

    /* Install the debugger write-object control method if necessary */

    if (!WriteHandle)
    {
        Status = AcpiInstallMethod (WriteMethodCode);
        if (ACPI_FAILURE (Status))
        {
            AcpiOsPrintf ("%s, Could not install debugger write method\n",
                AcpiFormatException (Status));
            return;
        }

        Status = AcpiGetHandle (NULL, ACPI_DB_WRITE_METHOD, &WriteHandle);
        if (ACPI_FAILURE (Status))
        {
            AcpiOsPrintf ("Could not obtain handle for debug method %s\n",
                ACPI_DB_WRITE_METHOD);
            return;
        }
    }

    /* Walk the entire namespace, testing each supported named data object */

    (void) AcpiWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, AcpiDbTestOneObject, NULL, NULL, NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbTestOneObject
 *
 * PARAMETERS:  ACPI_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test one namespace object. Supported types are Integer,
 *              String, Buffer, Package, BufferField, and FieldUnit.
 *              All other object types are simply ignored.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbTestOneObject (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_OBJECT_TYPE        LocalType;
    UINT32                  BitLength = 0;
    UINT32                  ByteLength = 0;
    ACPI_STATUS             Status = AE_OK;


    Node = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, ObjHandle);
    ObjDesc = Node->Object;

    /*
     * For the supported types, get the actual bit length or
     * byte length. Map the type to one of Integer/String/Buffer.
     */
    switch (Node->Type)
    {
    case ACPI_TYPE_INTEGER:

        /* Integer width is either 32 or 64 */

        LocalType = ACPI_TYPE_INTEGER;
        BitLength = AcpiGbl_IntegerBitWidth;
        break;

    case ACPI_TYPE_STRING:

        LocalType = ACPI_TYPE_STRING;
        ByteLength = ObjDesc->String.Length;
        break;

    case ACPI_TYPE_BUFFER:

        LocalType = ACPI_TYPE_BUFFER;
        ByteLength = ObjDesc->Buffer.Length;
        BitLength = ByteLength * 8;
        break;

    case ACPI_TYPE_PACKAGE:

        LocalType = ACPI_TYPE_PACKAGE;
        break;

    case ACPI_TYPE_FIELD_UNIT:
    case ACPI_TYPE_LOCAL_REGION_FIELD:
    case ACPI_TYPE_LOCAL_INDEX_FIELD:
    case ACPI_TYPE_LOCAL_BANK_FIELD:

        LocalType = ACPI_TYPE_FIELD_UNIT;
        break;

    case ACPI_TYPE_BUFFER_FIELD:
        /*
         * The returned object will be a Buffer if the field length
         * is larger than the size of an Integer (32 or 64 bits
         * depending on the DSDT version).
         */
        LocalType = ACPI_TYPE_INTEGER;
        if (ObjDesc)
        {
            BitLength = ObjDesc->CommonField.BitLength;
            ByteLength = ACPI_ROUND_BITS_UP_TO_BYTES (BitLength);
            if (BitLength > AcpiGbl_IntegerBitWidth)
            {
                LocalType = ACPI_TYPE_BUFFER;
            }
        }
        break;

default:

        /* Ignore all non-data types - Methods, Devices, Scopes, etc. */

        return (AE_OK);
    }

    /* Emit the common prefix: Type:Name */

    AcpiOsPrintf ("%14s: %4.4s",
        AcpiUtGetTypeName (Node->Type), Node->Name.Ascii);

    if (!ObjDesc)
    {
        AcpiOsPrintf (" No attached sub-object, ignoring\n");
        return (AE_OK);
    }

    /* At this point, we have resolved the object to one of the major types */

    switch (LocalType)
    {
    case ACPI_TYPE_INTEGER:

        Status = AcpiDbTestIntegerType (Node, BitLength);
        break;

    case ACPI_TYPE_STRING:

        Status = AcpiDbTestStringType (Node, ByteLength);
        break;

    case ACPI_TYPE_BUFFER:

        Status = AcpiDbTestBufferType (Node, BitLength);
        break;

    case ACPI_TYPE_PACKAGE:

        Status = AcpiDbTestPackageType (Node);
        break;

    case ACPI_TYPE_FIELD_UNIT:

        Status = AcpiDbTestFieldUnitType (ObjDesc);
        break;

    default:

        AcpiOsPrintf (" Ignoring, type not implemented (%2.2X)",
            LocalType);
        break;
    }

    /* Exit on error, but don't abort the namespace walk */

    if (ACPI_FAILURE (Status))
    {
        Status = AE_OK;
    }

    AcpiOsPrintf ("\n");
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbTestIntegerType
 *
 * PARAMETERS:  Node                - Parent NS node for the object
 *              BitLength           - Actual length of the object. Used for
 *                                    support of arbitrary length FieldUnit
 *                                    and BufferField objects.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test read/write for an Integer-valued object. Performs a
 *              write/read/compare of an arbitrary new value, then performs
 *              a write/read/compare of the original value.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbTestIntegerType (
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  BitLength)
{
    ACPI_OBJECT             *Temp1 = NULL;
    ACPI_OBJECT             *Temp2 = NULL;
    ACPI_OBJECT             *Temp3 = NULL;
    ACPI_OBJECT             WriteValue;
    UINT64                  ValueToWrite;
    ACPI_STATUS             Status;


    if (BitLength > 64)
    {
        AcpiOsPrintf (" Invalid length for an Integer: %u", BitLength);
        return (AE_OK);
    }

    /* Read the original value */

    Status = AcpiDbReadFromObject (Node, ACPI_TYPE_INTEGER, &Temp1);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    AcpiOsPrintf (ACPI_DEBUG_LENGTH_FORMAT " %8.8X%8.8X",
        BitLength, ACPI_ROUND_BITS_UP_TO_BYTES (BitLength),
        ACPI_FORMAT_UINT64 (Temp1->Integer.Value));

    ValueToWrite = ACPI_UINT64_MAX >> (64 - BitLength);
    if (Temp1->Integer.Value == ValueToWrite)
    {
        ValueToWrite = 0;
    }
    /* Write a new value */

    WriteValue.Type = ACPI_TYPE_INTEGER;
    WriteValue.Integer.Value = ValueToWrite;
    Status = AcpiDbWriteToObject (Node, &WriteValue);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    /* Ensure that we can read back the new value */

    Status = AcpiDbReadFromObject (Node, ACPI_TYPE_INTEGER, &Temp2);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    if (Temp2->Integer.Value != ValueToWrite)
    {
        AcpiOsPrintf (" MISMATCH 2: %8.8X%8.8X, expecting %8.8X%8.8X",
            ACPI_FORMAT_UINT64 (Temp2->Integer.Value),
            ACPI_FORMAT_UINT64 (ValueToWrite));
    }

    /* Write back the original value */

    WriteValue.Integer.Value = Temp1->Integer.Value;
    Status = AcpiDbWriteToObject (Node, &WriteValue);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    /* Ensure that we can read back the original value */

    Status = AcpiDbReadFromObject (Node, ACPI_TYPE_INTEGER, &Temp3);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    if (Temp3->Integer.Value != Temp1->Integer.Value)
    {
        AcpiOsPrintf (" MISMATCH 3: %8.8X%8.8X, expecting %8.8X%8.8X",
            ACPI_FORMAT_UINT64 (Temp3->Integer.Value),
            ACPI_FORMAT_UINT64 (Temp1->Integer.Value));
    }

Exit:
    if (Temp1) {AcpiOsFree (Temp1);}
    if (Temp2) {AcpiOsFree (Temp2);}
    if (Temp3) {AcpiOsFree (Temp3);}
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbTestBufferType
 *
 * PARAMETERS:  Node                - Parent NS node for the object
 *              BitLength           - Actual length of the object.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test read/write for an Buffer-valued object. Performs a
 *              write/read/compare of an arbitrary new value, then performs
 *              a write/read/compare of the original value.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbTestBufferType (
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  BitLength)
{
    ACPI_OBJECT             *Temp1 = NULL;
    ACPI_OBJECT             *Temp2 = NULL;
    ACPI_OBJECT             *Temp3 = NULL;
    UINT8                   *Buffer;
    ACPI_OBJECT             WriteValue;
    ACPI_STATUS             Status;
    UINT32                  ByteLength;
    UINT32                  i;
    UINT8                   ExtraBits;


    ByteLength = ACPI_ROUND_BITS_UP_TO_BYTES (BitLength);
    if (ByteLength == 0)
    {
        AcpiOsPrintf (" Ignoring zero length buffer");
        return (AE_OK);
    }

    /* Allocate a local buffer */

    Buffer = ACPI_ALLOCATE_ZEROED (ByteLength);
    if (!Buffer)
    {
        return (AE_NO_MEMORY);
    }

    /* Read the original value */

    Status = AcpiDbReadFromObject (Node, ACPI_TYPE_BUFFER, &Temp1);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    /* Emit a few bytes of the buffer */

    AcpiOsPrintf (ACPI_DEBUG_LENGTH_FORMAT, BitLength, Temp1->Buffer.Length);
    for (i = 0; ((i < 8) && (i < ByteLength)); i++)
    {
        AcpiOsPrintf (" %2.2X", Temp1->Buffer.Pointer[i]);
    }
    AcpiOsPrintf ("...  ");

    /*
     * Write a new value.
     *
     * Handle possible extra bits at the end of the buffer. Can
     * happen for FieldUnits larger than an integer, but the bit
     * count is not an integral number of bytes. Zero out the
     * unused bits.
     */
    memset (Buffer, BUFFER_FILL_VALUE, ByteLength);
    ExtraBits = BitLength % 8;
    if (ExtraBits)
    {
        Buffer [ByteLength - 1] = ACPI_MASK_BITS_ABOVE (ExtraBits);
    }

    WriteValue.Type = ACPI_TYPE_BUFFER;
    WriteValue.Buffer.Length = ByteLength;
    WriteValue.Buffer.Pointer = Buffer;

    Status = AcpiDbWriteToObject (Node, &WriteValue);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    /* Ensure that we can read back the new value */

    Status = AcpiDbReadFromObject (Node, ACPI_TYPE_BUFFER, &Temp2);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    if (memcmp (Temp2->Buffer.Pointer, Buffer, ByteLength))
    {
        AcpiOsPrintf (" MISMATCH 2: New buffer value");
    }

    /* Write back the original value */

    WriteValue.Buffer.Length = ByteLength;
    WriteValue.Buffer.Pointer = Temp1->Buffer.Pointer;

    Status = AcpiDbWriteToObject (Node, &WriteValue);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    /* Ensure that we can read back the original value */

    Status = AcpiDbReadFromObject (Node, ACPI_TYPE_BUFFER, &Temp3);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    if (memcmp (Temp1->Buffer.Pointer,
            Temp3->Buffer.Pointer, ByteLength))
    {
        AcpiOsPrintf (" MISMATCH 3: While restoring original buffer");
    }

Exit:
    ACPI_FREE (Buffer);
    if (Temp1) {AcpiOsFree (Temp1);}
    if (Temp2) {AcpiOsFree (Temp2);}
    if (Temp3) {AcpiOsFree (Temp3);}
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbTestStringType
 *
 * PARAMETERS:  Node                - Parent NS node for the object
 *              ByteLength          - Actual length of the object.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test read/write for an String-valued object. Performs a
 *              write/read/compare of an arbitrary new value, then performs
 *              a write/read/compare of the original value.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbTestStringType (
    ACPI_NAMESPACE_NODE     *Node,
    UINT32                  ByteLength)
{
    ACPI_OBJECT             *Temp1 = NULL;
    ACPI_OBJECT             *Temp2 = NULL;
    ACPI_OBJECT             *Temp3 = NULL;
    char                    *ValueToWrite = "Test String from AML Debugger";
    ACPI_OBJECT             WriteValue;
    ACPI_STATUS             Status;


    /* Read the original value */

    Status = AcpiDbReadFromObject (Node, ACPI_TYPE_STRING, &Temp1);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    AcpiOsPrintf (ACPI_DEBUG_LENGTH_FORMAT " \"%s\"", (Temp1->String.Length * 8),
        Temp1->String.Length, Temp1->String.Pointer);

    /* Write a new value */

    WriteValue.Type = ACPI_TYPE_STRING;
    WriteValue.String.Length = strlen (ValueToWrite);
    WriteValue.String.Pointer = ValueToWrite;

    Status = AcpiDbWriteToObject (Node, &WriteValue);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    /* Ensure that we can read back the new value */

    Status = AcpiDbReadFromObject (Node, ACPI_TYPE_STRING, &Temp2);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    if (strcmp (Temp2->String.Pointer, ValueToWrite))
    {
        AcpiOsPrintf (" MISMATCH 2: %s, expecting %s",
            Temp2->String.Pointer, ValueToWrite);
    }

    /* Write back the original value */

    WriteValue.String.Length = strlen (Temp1->String.Pointer);
    WriteValue.String.Pointer = Temp1->String.Pointer;

    Status = AcpiDbWriteToObject (Node, &WriteValue);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    /* Ensure that we can read back the original value */

    Status = AcpiDbReadFromObject (Node, ACPI_TYPE_STRING, &Temp3);
    if (ACPI_FAILURE (Status))
    {
        goto Exit;
    }

    if (strcmp (Temp1->String.Pointer, Temp3->String.Pointer))
    {
        AcpiOsPrintf (" MISMATCH 3: %s, expecting %s",
            Temp3->String.Pointer, Temp1->String.Pointer);
    }

Exit:
    if (Temp1) {AcpiOsFree (Temp1);}
    if (Temp2) {AcpiOsFree (Temp2);}
    if (Temp3) {AcpiOsFree (Temp3);}
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbTestPackageType
 *
 * PARAMETERS:  Node                - Parent NS node for the object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test read for a Package object.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbTestPackageType (
    ACPI_NAMESPACE_NODE     *Node)
{
    ACPI_OBJECT             *Temp1 = NULL;
    ACPI_STATUS             Status;


    /* Read the original value */

    Status = AcpiDbReadFromObject (Node, ACPI_TYPE_PACKAGE, &Temp1);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    AcpiOsPrintf (" %.2X Elements", Temp1->Package.Count);
    AcpiOsFree (Temp1);
    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbTestFieldUnitType
 *
 * PARAMETERS:  ObjDesc                 - A field unit object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test read/write on a named field unit.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbTestFieldUnitType (
    ACPI_OPERAND_OBJECT     *ObjDesc)
{
    ACPI_OPERAND_OBJECT     *RegionObj;
    UINT32                  BitLength = 0;
    UINT32                  ByteLength = 0;
    ACPI_STATUS             Status = AE_OK;
    ACPI_OPERAND_OBJECT     *RetBufferDesc;


    /* Supported spaces are memory/io/pci_config */

    RegionObj = ObjDesc->Field.RegionObj;
    switch (RegionObj->Region.SpaceId)
    {
    case ACPI_ADR_SPACE_SYSTEM_MEMORY:
    case ACPI_ADR_SPACE_SYSTEM_IO:
    case ACPI_ADR_SPACE_PCI_CONFIG:

        /* Need the interpreter to execute */

        AcpiUtAcquireMutex (ACPI_MTX_INTERPRETER);
        AcpiUtAcquireMutex (ACPI_MTX_NAMESPACE);

        /* Exercise read-then-write */

        Status = AcpiExReadDataFromField (NULL, ObjDesc, &RetBufferDesc);
        if (Status == AE_OK)
        {
            AcpiExWriteDataToField (RetBufferDesc, ObjDesc, NULL);
            AcpiUtRemoveReference (RetBufferDesc);
        }

        AcpiUtReleaseMutex (ACPI_MTX_NAMESPACE);
        AcpiUtReleaseMutex (ACPI_MTX_INTERPRETER);

        BitLength = ObjDesc->CommonField.BitLength;
        ByteLength = ACPI_ROUND_BITS_UP_TO_BYTES (BitLength);

        AcpiOsPrintf (ACPI_DEBUG_LENGTH_FORMAT " [%s]", BitLength,
            ByteLength, AcpiUtGetRegionName (RegionObj->Region.SpaceId));
        return (Status);

    default:

        AcpiOsPrintf (
            "      %s address space is not supported in this command [%4.4s]",
            AcpiUtGetRegionName (RegionObj->Region.SpaceId),
            RegionObj->Region.Node->Name.Ascii);
        return (AE_OK);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbReadFromObject
 *
 * PARAMETERS:  Node                - Parent NS node for the object
 *              ExpectedType        - Object type expected from the read
 *              Value               - Where the value read is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Performs a read from the specified object by invoking the
 *              special debugger control method that reads the object. Thus,
 *              the AML interpreter is doing all of the work, increasing the
 *              validity of the test.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbReadFromObject (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_OBJECT_TYPE        ExpectedType,
    ACPI_OBJECT             **Value)
{
    ACPI_OBJECT             *RetValue;
    ACPI_OBJECT_LIST        ParamObjects;
    ACPI_OBJECT             Params[2];
    ACPI_BUFFER             ReturnObj;
    ACPI_STATUS             Status;


    Params[0].Type = ACPI_TYPE_LOCAL_REFERENCE;
    Params[0].Reference.ActualType = Node->Type;
    Params[0].Reference.Handle = ACPI_CAST_PTR (ACPI_HANDLE, Node);

    ParamObjects.Count = 1;
    ParamObjects.Pointer = Params;

    ReturnObj.Length  = ACPI_ALLOCATE_BUFFER;

    AcpiGbl_MethodExecuting = TRUE;
    Status = AcpiEvaluateObject (ReadHandle, NULL,
        &ParamObjects, &ReturnObj);

    AcpiGbl_MethodExecuting = FALSE;
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could not read from object, %s",
            AcpiFormatException (Status));
        return (Status);
    }

    RetValue = (ACPI_OBJECT *) ReturnObj.Pointer;

    switch (RetValue->Type)
    {
    case ACPI_TYPE_INTEGER:
    case ACPI_TYPE_BUFFER:
    case ACPI_TYPE_STRING:
    case ACPI_TYPE_PACKAGE:
        /*
         * Did we receive the type we wanted? Most important for the
         * Integer/Buffer case (when a field is larger than an Integer,
         * it should return a Buffer).
         */
        if (RetValue->Type != ExpectedType)
        {
            AcpiOsPrintf (" Type mismatch:  Expected %s, Received %s",
                AcpiUtGetTypeName (ExpectedType),
                AcpiUtGetTypeName (RetValue->Type));

            AcpiOsFree (ReturnObj.Pointer);
            return (AE_TYPE);
        }

        *Value = RetValue;
        break;

    default:

        AcpiOsPrintf (" Unsupported return object type, %s",
            AcpiUtGetTypeName (RetValue->Type));

        AcpiOsFree (ReturnObj.Pointer);
        return (AE_TYPE);
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbWriteToObject
 *
 * PARAMETERS:  Node                - Parent NS node for the object
 *              Value               - Value to be written
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Performs a write to the specified object by invoking the
 *              special debugger control method that writes the object. Thus,
 *              the AML interpreter is doing all of the work, increasing the
 *              validity of the test.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbWriteToObject (
    ACPI_NAMESPACE_NODE     *Node,
    ACPI_OBJECT             *Value)
{
    ACPI_OBJECT_LIST        ParamObjects;
    ACPI_OBJECT             Params[2];
    ACPI_STATUS             Status;


    Params[0].Type = ACPI_TYPE_LOCAL_REFERENCE;
    Params[0].Reference.ActualType = Node->Type;
    Params[0].Reference.Handle = ACPI_CAST_PTR (ACPI_HANDLE, Node);

    /* Copy the incoming user parameter */

    memcpy (&Params[1], Value, sizeof (ACPI_OBJECT));

    ParamObjects.Count = 2;
    ParamObjects.Pointer = Params;

    AcpiGbl_MethodExecuting = TRUE;
    Status = AcpiEvaluateObject (WriteHandle, NULL, &ParamObjects, NULL);
    AcpiGbl_MethodExecuting = FALSE;

    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could not write to object, %s",
            AcpiFormatException (Status));
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbEvaluateAllPredefinedNames
 *
 * PARAMETERS:  CountArg            - Max number of methods to execute
 *
 * RETURN:      None
 *
 * DESCRIPTION: Namespace batch execution. Execute predefined names in the
 *              namespace, up to the max count, if specified.
 *
 ******************************************************************************/

static void
AcpiDbEvaluateAllPredefinedNames (
    char                    *CountArg)
{
    ACPI_DB_EXECUTE_WALK    Info;


    Info.Count = 0;
    Info.MaxCount = ACPI_UINT32_MAX;

    if (CountArg)
    {
        Info.MaxCount = strtoul (CountArg, NULL, 0);
    }

    /* Search all nodes in namespace */

    (void) AcpiWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, AcpiDbEvaluateOnePredefinedName, NULL,
        (void *) &Info, NULL);

    AcpiOsPrintf (
        "Evaluated %u predefined names in the namespace\n", Info.Count);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbEvaluateOnePredefinedName
 *
 * PARAMETERS:  Callback from WalkNamespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Batch execution module. Currently only executes predefined
 *              ACPI names.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbEvaluateOnePredefinedName (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_NAMESPACE_NODE         *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;
    ACPI_DB_EXECUTE_WALK        *Info = (ACPI_DB_EXECUTE_WALK *) Context;
    char                        *Pathname;
    const ACPI_PREDEFINED_INFO  *Predefined;
    ACPI_DEVICE_INFO            *ObjInfo;
    ACPI_OBJECT_LIST            ParamObjects;
    ACPI_OBJECT                 Params[ACPI_METHOD_NUM_ARGS];
    ACPI_OBJECT                 *ThisParam;
    ACPI_BUFFER                 ReturnObj;
    ACPI_STATUS                 Status;
    UINT16                      ArgTypeList;
    UINT8                       ArgCount;
    UINT8                       ArgType;
    UINT32                      i;


    /* The name must be a predefined ACPI name */

    Predefined = AcpiUtMatchPredefinedMethod (Node->Name.Ascii);
    if (!Predefined)
    {
        return (AE_OK);
    }

    if (Node->Type == ACPI_TYPE_LOCAL_SCOPE)
    {
        return (AE_OK);
    }

    Pathname = AcpiNsGetNormalizedPathname (Node, TRUE);
    if (!Pathname)
    {
        return (AE_OK);
    }

    /* Get the object info for number of method parameters */

    Status = AcpiGetObjectInfo (ObjHandle, &ObjInfo);
    if (ACPI_FAILURE (Status))
    {
        ACPI_FREE (Pathname);
        return (Status);
    }

    ParamObjects.Count = 0;
    ParamObjects.Pointer = NULL;

    if (ObjInfo->Type == ACPI_TYPE_METHOD)
    {
        /* Setup default parameters (with proper types) */

        ArgTypeList = Predefined->Info.ArgumentList;
        ArgCount = METHOD_GET_ARG_COUNT (ArgTypeList);

        /*
         * Setup the ACPI-required number of arguments, regardless of what
         * the actual method defines. If there is a difference, then the
         * method is wrong and a warning will be issued during execution.
         */
        ThisParam = Params;
        for (i = 0; i < ArgCount; i++)
        {
            ArgType = METHOD_GET_NEXT_TYPE (ArgTypeList);
            ThisParam->Type = ArgType;

            switch (ArgType)
            {
            case ACPI_TYPE_INTEGER:

                ThisParam->Integer.Value = 1;
                break;

            case ACPI_TYPE_STRING:

                ThisParam->String.Pointer =
                    "This is the default argument string";
                ThisParam->String.Length =
                    strlen (ThisParam->String.Pointer);
                break;

            case ACPI_TYPE_BUFFER:

                ThisParam->Buffer.Pointer = (UINT8 *) Params; /* just a garbage buffer */
                ThisParam->Buffer.Length = 48;
                break;

             case ACPI_TYPE_PACKAGE:

                ThisParam->Package.Elements = NULL;
                ThisParam->Package.Count = 0;
                break;

           default:

                AcpiOsPrintf ("%s: Unsupported argument type: %u\n",
                    Pathname, ArgType);
                break;
            }

            ThisParam++;
        }

        ParamObjects.Count = ArgCount;
        ParamObjects.Pointer = Params;
    }

    ACPI_FREE (ObjInfo);
    ReturnObj.Pointer = NULL;
    ReturnObj.Length = ACPI_ALLOCATE_BUFFER;

    /* Do the actual method execution */

    AcpiGbl_MethodExecuting = TRUE;

    Status = AcpiEvaluateObject (Node, NULL, &ParamObjects, &ReturnObj);

    AcpiOsPrintf ("%-32s returned %s\n",
        Pathname, AcpiFormatException (Status));
    AcpiGbl_MethodExecuting = FALSE;
    ACPI_FREE (Pathname);

    /* Ignore status from method execution */

    Status = AE_OK;

    /* Update count, check if we have executed enough methods */

    Info->Count++;
    if (Info->Count >= Info->MaxCount)
    {
        Status = AE_CTRL_TERMINATE;
    }

    return (Status);
}

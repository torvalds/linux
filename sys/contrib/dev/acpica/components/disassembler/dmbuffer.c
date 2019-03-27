/*******************************************************************************
 *
 * Module Name: dmbuffer - AML disassembler, buffer and string support
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
#include <contrib/dev/acpica/include/acutils.h>
#include <contrib/dev/acpica/include/acdisasm.h>
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acinterp.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dmbuffer")

/* Local prototypes */

static void
AcpiDmUuid (
    ACPI_PARSE_OBJECT       *Op);

static void
AcpiDmUnicode (
    ACPI_PARSE_OBJECT       *Op);

static void
AcpiDmGetHardwareIdType (
    ACPI_PARSE_OBJECT       *Op);

static void
AcpiDmPldBuffer (
    UINT32                  Level,
    UINT8                   *ByteData,
    UINT32                  ByteCount);

static const char *
AcpiDmFindNameByIndex (
    UINT64                  Index,
    const char              **List);


#define ACPI_BUFFER_BYTES_PER_LINE      8


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDisasmByteList
 *
 * PARAMETERS:  Level               - Current source code indentation level
 *              ByteData            - Pointer to the byte list
 *              ByteCount           - Length of the byte list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump an AML "ByteList" in Hex format. 8 bytes per line, prefixed
 *              with the hex buffer offset.
 *
 ******************************************************************************/

void
AcpiDmDisasmByteList (
    UINT32                  Level,
    UINT8                   *ByteData,
    UINT32                  ByteCount)
{
    UINT32                  i;
    UINT32                  j;
    UINT32                  CurrentIndex;
    UINT8                   BufChar;


    if (!ByteCount)
    {
        return;
    }

    for (i = 0; i < ByteCount; i += ACPI_BUFFER_BYTES_PER_LINE)
    {
        /* Line indent and offset prefix for each new line */

        AcpiDmIndent (Level);
        if (ByteCount > ACPI_BUFFER_BYTES_PER_LINE)
        {
            AcpiOsPrintf ("/* %04X */ ", i);
        }

        /* Dump the actual hex values */

        for (j = 0; j < ACPI_BUFFER_BYTES_PER_LINE; j++)
        {
            CurrentIndex = i + j;
            if (CurrentIndex >= ByteCount)
            {
                /* Dump fill spaces */

                AcpiOsPrintf ("      ");
                continue;
            }

            AcpiOsPrintf (" 0x%2.2X", ByteData[CurrentIndex]);

            /* Add comma if there are more bytes to display */

            if (CurrentIndex < (ByteCount - 1))
            {
                AcpiOsPrintf (",");
            }
            else
            {
                AcpiOsPrintf (" ");
            }
        }

        /* Dump the ASCII equivalents within a comment */

        AcpiOsPrintf ("  // ");
        for (j = 0; j < ACPI_BUFFER_BYTES_PER_LINE; j++)
        {
            CurrentIndex = i + j;
            if (CurrentIndex >= ByteCount)
            {
                break;
            }

            BufChar = ByteData[CurrentIndex];
            if (isprint (BufChar))
            {
                AcpiOsPrintf ("%c", BufChar);
            }
            else
            {
                AcpiOsPrintf (".");
            }
        }

        /* Finished with this line */

        AcpiOsPrintf ("\n");
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmByteList
 *
 * PARAMETERS:  Info            - Parse tree walk info
 *              Op              - Byte list op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump a buffer byte list, handling the various types of buffers.
 *              Buffer type must be already set in the Op DisasmOpcode.
 *
 ******************************************************************************/

void
AcpiDmByteList (
    ACPI_OP_WALK_INFO       *Info,
    ACPI_PARSE_OBJECT       *Op)
{
    UINT8                   *ByteData;
    UINT32                  ByteCount;


    ByteData = Op->Named.Data;
    ByteCount = (UINT32) Op->Common.Value.Integer;

    /*
     * The byte list belongs to a buffer, and can be produced by either
     * a ResourceTemplate, Unicode, quoted string, or a plain byte list.
     */
    switch (Op->Common.Parent->Common.DisasmOpcode)
    {
    case ACPI_DASM_RESOURCE:

        AcpiDmResourceTemplate (
            Info, Op->Common.Parent, ByteData, ByteCount);
        break;

    case ACPI_DASM_STRING:

        AcpiDmIndent (Info->Level);
        AcpiUtPrintString ((char *) ByteData, ACPI_UINT16_MAX);
        AcpiOsPrintf ("\n");
        break;

    case ACPI_DASM_UUID:

        AcpiDmUuid (Op);
        break;

    case ACPI_DASM_UNICODE:

        AcpiDmUnicode (Op);
        break;

    case ACPI_DASM_PLD_METHOD:
#if 0
        AcpiDmDisasmByteList (Info->Level, ByteData, ByteCount);
#endif
        AcpiDmPldBuffer (Info->Level, ByteData, ByteCount);
        break;

    case ACPI_DASM_BUFFER:
    default:
        /*
         * Not a resource, string, or unicode string.
         * Just dump the buffer
         */
        AcpiDmDisasmByteList (Info->Level, ByteData, ByteCount);
        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmIsUuidBuffer
 *
 * PARAMETERS:  Op              - Buffer Object to be examined
 *
 * RETURN:      TRUE if buffer contains a UUID
 *
 * DESCRIPTION: Determine if a buffer Op contains a UUID
 *
 * To help determine whether the buffer is a UUID versus a raw data buffer,
 * there a are a couple bytes we can look at:
 *
 *    xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
 *
 * The variant covered by the UUID specification is indicated by the two most
 * significant bits of N being 1 0 (i.e., the hexadecimal N will always be
 * 8, 9, A, or B).
 *
 * The variant covered by the UUID specification has five versions. For this
 * variant, the four bits of M indicates the UUID version (i.e., the
 * hexadecimal M will be either 1, 2, 3, 4, or 5).
 *
 ******************************************************************************/

BOOLEAN
AcpiDmIsUuidBuffer (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT8                   *ByteData;
    UINT32                  ByteCount;
    ACPI_PARSE_OBJECT       *SizeOp;
    ACPI_PARSE_OBJECT       *NextOp;


    /* Buffer size is the buffer argument */

    SizeOp = Op->Common.Value.Arg;

    /* Next, the initializer byte list to examine */

    NextOp = SizeOp->Common.Next;
    if (!NextOp)
    {
        return (FALSE);
    }

    /* Extract the byte list info */

    ByteData = NextOp->Named.Data;
    ByteCount = (UINT32) NextOp->Common.Value.Integer;

    /* Byte count must be exactly 16 */

    if (ByteCount != UUID_BUFFER_LENGTH)
    {
        return (FALSE);
    }

    /* Check for valid "M" and "N" values (see function header above) */

    if (((ByteData[7] & 0xF0) == 0x00) || /* M={1,2,3,4,5} */
        ((ByteData[7] & 0xF0) > 0x50)  ||
        ((ByteData[8] & 0xF0) < 0x80)  || /* N={8,9,A,B} */
        ((ByteData[8] & 0xF0) > 0xB0))
    {
        return (FALSE);
    }

    /* Ignore the Size argument in the disassembly of this buffer op */

    SizeOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmUuid
 *
 * PARAMETERS:  Op              - Byte List op containing a UUID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump a buffer containing a UUID as a standard ASCII string.
 *
 * Output Format:
 * In its canonical form, the UUID is represented by a string containing 32
 * lowercase hexadecimal digits, displayed in 5 groups separated by hyphens.
 * The complete form is 8-4-4-4-12 for a total of 36 characters (32
 * alphanumeric characters representing hex digits and 4 hyphens). In bytes,
 * 4-2-2-2-6. Example:
 *
 *    ToUUID ("107ededd-d381-4fd7-8da9-08e9a6c79644")
 *
 ******************************************************************************/

static void
AcpiDmUuid (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT8                   *Data;
    const char              *Description;


    Data = ACPI_CAST_PTR (UINT8, Op->Named.Data);

    /* Emit the 36-byte UUID string in the proper format/order */

    AcpiOsPrintf (
        "\"%2.2x%2.2x%2.2x%2.2x-"
        "%2.2x%2.2x-"
        "%2.2x%2.2x-"
        "%2.2x%2.2x-"
        "%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x\")",
        Data[3], Data[2], Data[1], Data[0],
        Data[5], Data[4],
        Data[7], Data[6],
        Data[8], Data[9],
        Data[10], Data[11], Data[12], Data[13], Data[14], Data[15]);

    /* Dump the UUID description string if available */

    Description = AcpiAhMatchUuid (Data);
    if (Description)
    {
        AcpiOsPrintf (" /* %s */", Description);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmIsUnicodeBuffer
 *
 * PARAMETERS:  Op              - Buffer Object to be examined
 *
 * RETURN:      TRUE if buffer contains a UNICODE string
 *
 * DESCRIPTION: Determine if a buffer Op contains a Unicode string
 *
 ******************************************************************************/

BOOLEAN
AcpiDmIsUnicodeBuffer (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT8                   *ByteData;
    UINT32                  ByteCount;
    UINT32                  WordCount;
    ACPI_PARSE_OBJECT       *SizeOp;
    ACPI_PARSE_OBJECT       *NextOp;
    UINT32                  i;


    /* Buffer size is the buffer argument */

    SizeOp = Op->Common.Value.Arg;

    /* Next, the initializer byte list to examine */

    NextOp = SizeOp->Common.Next;
    if (!NextOp)
    {
        return (FALSE);
    }

    /* Extract the byte list info */

    ByteData = NextOp->Named.Data;
    ByteCount = (UINT32) NextOp->Common.Value.Integer;
    WordCount = ACPI_DIV_2 (ByteCount);

    /*
     * Unicode string must have an even number of bytes and last
     * word must be zero
     */
    if ((!ByteCount)     ||
         (ByteCount < 4) ||
         (ByteCount & 1) ||
        ((UINT16 *) (void *) ByteData)[WordCount - 1] != 0)
    {
        return (FALSE);
    }

    /*
     * For each word, 1st byte must be printable ascii, and the
     * 2nd byte must be zero. This does not allow for escape
     * sequences, but it is the most secure way to detect a
     * unicode string.
     */
    for (i = 0; i < (ByteCount - 2); i += 2)
    {
        if ((ByteData[i] == 0) ||
            !(isprint (ByteData[i])) ||
            (ByteData[(ACPI_SIZE) i + 1] != 0))
        {
            return (FALSE);
        }
    }

    /* Ignore the Size argument in the disassembly of this buffer op */

    SizeOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmIsStringBuffer
 *
 * PARAMETERS:  Op              - Buffer Object to be examined
 *
 * RETURN:      TRUE if buffer contains a ASCII string, FALSE otherwise
 *
 * DESCRIPTION: Determine if a buffer Op contains a ASCII string
 *
 ******************************************************************************/

BOOLEAN
AcpiDmIsStringBuffer (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT8                   *ByteData;
    UINT32                  ByteCount;
    ACPI_PARSE_OBJECT       *SizeOp;
    ACPI_PARSE_OBJECT       *NextOp;
    UINT32                  i;


    /* Buffer size is the buffer argument */

    SizeOp = Op->Common.Value.Arg;

    /* Next, the initializer byte list to examine */

    NextOp = SizeOp->Common.Next;
    if (!NextOp)
    {
        return (FALSE);
    }

    /* Extract the byte list info */

    ByteData = NextOp->Named.Data;
    ByteCount = (UINT32) NextOp->Common.Value.Integer;

    /* Last byte must be the null terminator */

    if ((!ByteCount)     ||
         (ByteCount < 2) ||
         (ByteData[ByteCount-1] != 0))
    {
        return (FALSE);
    }

    /*
     * Check for a possible standalone resource EndTag, ignore it
     * here. However, this sequence is also the string "Y", but
     * this seems rare enough to be acceptable.
     */
    if ((ByteCount == 2) && (ByteData[0] == 0x79))
    {
        return (FALSE);
    }

    /* Check all bytes for ASCII */

    for (i = 0; i < (ByteCount - 1); i++)
    {
        /*
         * TBD: allow some escapes (non-ascii chars).
         * they will be handled in the string output routine
         */

        /* Not a string if not printable ascii */

        if (!isprint (ByteData[i]))
        {
            return (FALSE);
        }
    }

    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmIsPldBuffer
 *
 * PARAMETERS:  Op                  - Buffer Object to be examined
 *
 * RETURN:      TRUE if buffer appears to contain data produced via the
 *              ToPLD macro, FALSE otherwise
 *
 * DESCRIPTION: Determine if a buffer Op contains a _PLD structure
 *
 ******************************************************************************/

BOOLEAN
AcpiDmIsPldBuffer (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_PARSE_OBJECT       *SizeOp;
    ACPI_PARSE_OBJECT       *ByteListOp;
    ACPI_PARSE_OBJECT       *ParentOp;
    UINT64                  BufferSize;
    UINT64                  InitializerSize;


    /*
     * Get the BufferSize argument - Buffer(BufferSize)
     * If the buffer was generated by the ToPld macro, it must
     * be a BYTE constant.
     */
    SizeOp = Op->Common.Value.Arg;
    if (SizeOp->Common.AmlOpcode != AML_BYTE_OP)
    {
        return (FALSE);
    }

    /* Check the declared BufferSize, two possibilities */

    BufferSize = SizeOp->Common.Value.Integer;
    if ((BufferSize != ACPI_PLD_REV1_BUFFER_SIZE) &&
        (BufferSize != ACPI_PLD_REV2_BUFFER_SIZE))
    {
        return (FALSE);
    }

    /*
     * Check the initializer list length. This is the actual
     * number of bytes in the buffer as counted by the AML parser.
     * The declared BufferSize can be larger than the actual length.
     * However, for the ToPLD macro, the BufferSize will be the same
     * as the initializer list length.
     */
    ByteListOp = SizeOp->Common.Next;
    if (!ByteListOp)
    {
        return (FALSE); /* Zero-length buffer case */
    }

    InitializerSize = ByteListOp->Common.Value.Integer;
    if ((InitializerSize != ACPI_PLD_REV1_BUFFER_SIZE) &&
        (InitializerSize != ACPI_PLD_REV2_BUFFER_SIZE))
    {
        return (FALSE);
    }

    /* Final size check */

    if (BufferSize != InitializerSize)
    {
        return (FALSE);
    }

    /* Now examine the buffer parent */

    ParentOp = Op->Common.Parent;
    if (!ParentOp)
    {
        return (FALSE);
    }

    /* Check for form: Name(_PLD, Buffer() {}). Not legal, however */

    if (ParentOp->Common.AmlOpcode == AML_NAME_OP)
    {
        Node = ParentOp->Common.Node;

        if (ACPI_COMPARE_NAME (Node->Name.Ascii, METHOD_NAME__PLD))
        {
            /* Ignore the Size argument in the disassembly of this buffer op */

            SizeOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
            return (TRUE);
        }

        return (FALSE);
    }

    /*
     * Check for proper form: Name(_PLD, Package() {ToPLD()})
     *
     * Note: All other forms such as
     *      Return (Package() {ToPLD()})
     *      Local0 = ToPLD()
     * etc. are not converted back to the ToPLD macro, because
     * there is really no deterministic way to disassemble the buffer
     * back to the ToPLD macro, other than trying to find the "_PLD"
     * name
     */
    if (ParentOp->Common.AmlOpcode == AML_PACKAGE_OP)
    {
        ParentOp = ParentOp->Common.Parent;
        if (!ParentOp)
        {
            return (FALSE);
        }

        if (ParentOp->Common.AmlOpcode == AML_NAME_OP)
        {
            Node = ParentOp->Common.Node;

            if (ACPI_COMPARE_NAME (Node->Name.Ascii, METHOD_NAME__PLD))
            {
                /* Ignore the Size argument in the disassembly of this buffer op */

                SizeOp->Common.DisasmFlags |= ACPI_PARSEOP_IGNORE;
                return (TRUE);
            }
        }
    }

    return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmFindNameByIndex
 *
 * PARAMETERS:  Index               - Index of array to check
 *              List                - Array to reference
 *
 * RETURN:      String from List or empty string
 *
 * DESCRIPTION: Finds and returns the char string located at the given index
 *              position in List.
 *
 ******************************************************************************/

static const char *
AcpiDmFindNameByIndex (
    UINT64                  Index,
    const char              **List)
{
    const char              *NameString;
    UINT32                  i;


    /* Bounds check */

    NameString = List[0];
    i = 0;

    while (NameString)
    {
        i++;
        NameString = List[i];
    }

    if (Index >= i)
    {
        /* TBD: Add error msg */

        return ("");
    }

    return (List[Index]);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmPldBuffer
 *
 * PARAMETERS:  Level               - Current source code indentation level
 *              ByteData            - Pointer to the byte list
 *              ByteCount           - Length of the byte list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump and format the contents of a _PLD buffer object
 *
 ******************************************************************************/

#define ACPI_PLD_OUTPUT08   "%*.s%-22s = 0x%X,\n", ACPI_MUL_4 (Level), " "
#define ACPI_PLD_OUTPUT08P  "%*.s%-22s = 0x%X)\n", ACPI_MUL_4 (Level), " "
#define ACPI_PLD_OUTPUT16   "%*.s%-22s = 0x%X,\n", ACPI_MUL_4 (Level), " "
#define ACPI_PLD_OUTPUT16P  "%*.s%-22s = 0x%X)\n", ACPI_MUL_4 (Level), " "
#define ACPI_PLD_OUTPUT24   "%*.s%-22s = 0x%X,\n", ACPI_MUL_4 (Level), " "
#define ACPI_PLD_OUTPUTSTR  "%*.s%-22s = \"%s\",\n", ACPI_MUL_4 (Level), " "

static void
AcpiDmPldBuffer (
    UINT32                  Level,
    UINT8                   *ByteData,
    UINT32                  ByteCount)
{
    ACPI_PLD_INFO           *PldInfo;
    ACPI_STATUS             Status;


    /* Check for valid byte count */

    if (ByteCount < ACPI_PLD_REV1_BUFFER_SIZE)
    {
        return;
    }

    /* Convert _PLD buffer to local _PLD struct */

    Status = AcpiDecodePldBuffer (ByteData, ByteCount, &PldInfo);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    AcpiOsPrintf ("\n");

    /* First 32-bit dword */

    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_Revision", PldInfo->Revision);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_IgnoreColor", PldInfo->IgnoreColor);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_Red", PldInfo->Red);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_Green", PldInfo->Green);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_Blue", PldInfo->Blue);

    /* Second 32-bit dword */

    AcpiOsPrintf (ACPI_PLD_OUTPUT16,  "PLD_Width", PldInfo->Width);
    AcpiOsPrintf (ACPI_PLD_OUTPUT16,  "PLD_Height", PldInfo->Height);

    /* Third 32-bit dword */

    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_UserVisible", PldInfo->UserVisible);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_Dock", PldInfo->Dock);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_Lid", PldInfo->Lid);
    AcpiOsPrintf (ACPI_PLD_OUTPUTSTR, "PLD_Panel",
        AcpiDmFindNameByIndex(PldInfo->Panel, AcpiGbl_PldPanelList));

    AcpiOsPrintf (ACPI_PLD_OUTPUTSTR, "PLD_VerticalPosition",
        AcpiDmFindNameByIndex(PldInfo->VerticalPosition, AcpiGbl_PldVerticalPositionList));

    AcpiOsPrintf (ACPI_PLD_OUTPUTSTR, "PLD_HorizontalPosition",
        AcpiDmFindNameByIndex(PldInfo->HorizontalPosition, AcpiGbl_PldHorizontalPositionList));

    AcpiOsPrintf (ACPI_PLD_OUTPUTSTR, "PLD_Shape",
        AcpiDmFindNameByIndex(PldInfo->Shape, AcpiGbl_PldShapeList));
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_GroupOrientation", PldInfo->GroupOrientation);

    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_GroupToken", PldInfo->GroupToken);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_GroupPosition", PldInfo->GroupPosition);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_Bay", PldInfo->Bay);

    /* Fourth 32-bit dword */

    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_Ejectable", PldInfo->Ejectable);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_EjectRequired", PldInfo->OspmEjectRequired);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_CabinetNumber", PldInfo->CabinetNumber);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_CardCageNumber", PldInfo->CardCageNumber);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_Reference", PldInfo->Reference);
    AcpiOsPrintf (ACPI_PLD_OUTPUT08,  "PLD_Rotation", PldInfo->Rotation);

    if (ByteCount >= ACPI_PLD_REV2_BUFFER_SIZE)
    {
        AcpiOsPrintf (ACPI_PLD_OUTPUT08, "PLD_Order", PldInfo->Order);

        /* Fifth 32-bit dword */

        AcpiOsPrintf (ACPI_PLD_OUTPUT16,  "PLD_VerticalOffset", PldInfo->VerticalOffset);
        AcpiOsPrintf (ACPI_PLD_OUTPUT16P, "PLD_HorizontalOffset", PldInfo->HorizontalOffset);
    }
    else /* Rev 1 buffer */
    {
        AcpiOsPrintf (ACPI_PLD_OUTPUT08P, "PLD_Order", PldInfo->Order);
    }

    ACPI_FREE (PldInfo);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmUnicode
 *
 * PARAMETERS:  Op              - Byte List op containing Unicode string
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump Unicode string as a standard ASCII string. (Remove
 *              the extra zero bytes).
 *
 ******************************************************************************/

static void
AcpiDmUnicode (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT16                  *WordData;
    UINT32                  WordCount;
    UINT32                  i;
    int                     OutputValue;


    /* Extract the buffer info as a WORD buffer */

    WordData = ACPI_CAST_PTR (UINT16, Op->Named.Data);
    WordCount = ACPI_DIV_2 (((UINT32) Op->Common.Value.Integer));

    /* Write every other byte as an ASCII character */

    AcpiOsPrintf ("\"");
    for (i = 0; i < (WordCount - 1); i++)
    {
        OutputValue = (int) WordData[i];

        /* Handle values that must be escaped */

        if ((OutputValue == '\"') ||
            (OutputValue == '\\'))
        {
            AcpiOsPrintf ("\\%c", OutputValue);
        }
        else if (!isprint (OutputValue))
        {
            AcpiOsPrintf ("\\x%2.2X", OutputValue);
        }
        else
        {
            AcpiOsPrintf ("%c", OutputValue);
        }
    }

    AcpiOsPrintf ("\")");
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmGetHardwareIdType
 *
 * PARAMETERS:  Op              - Op to be examined
 *
 * RETURN:      None
 *
 * DESCRIPTION: Determine the type of the argument to a _HID or _CID
 *              1) Strings are allowed
 *              2) If Integer, determine if it is a valid EISAID
 *
 ******************************************************************************/

static void
AcpiDmGetHardwareIdType (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT32                  BigEndianId;
    UINT32                  Prefix[3];
    UINT32                  i;


    switch (Op->Common.AmlOpcode)
    {
    case AML_STRING_OP:

        /* Mark this string as an _HID/_CID string */

        Op->Common.DisasmOpcode = ACPI_DASM_HID_STRING;
        break;

    case AML_WORD_OP:
    case AML_DWORD_OP:

        /* Determine if a Word/Dword is a valid encoded EISAID */

        /* Swap from little-endian to big-endian to simplify conversion */

        BigEndianId = AcpiUtDwordByteSwap ((UINT32) Op->Common.Value.Integer);

        /* Create the 3 leading ASCII letters */

        Prefix[0] = ((BigEndianId >> 26) & 0x1F) + 0x40;
        Prefix[1] = ((BigEndianId >> 21) & 0x1F) + 0x40;
        Prefix[2] = ((BigEndianId >> 16) & 0x1F) + 0x40;

        /* Verify that all 3 are ascii and alpha */

        for (i = 0; i < 3; i++)
        {
            if (!ACPI_IS_ASCII (Prefix[i]) ||
                !isalpha (Prefix[i]))
            {
                return;
            }
        }

        /* Mark this node as convertible to an EISA ID string */

        Op->Common.DisasmOpcode = ACPI_DASM_EISAID;
        break;

    default:
        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmCheckForHardwareId
 *
 * PARAMETERS:  Op              - Op to be examined
 *
 * RETURN:      None
 *
 * DESCRIPTION: Determine if a Name() Op is a _HID/_CID.
 *
 ******************************************************************************/

void
AcpiDmCheckForHardwareId (
    ACPI_PARSE_OBJECT       *Op)
{
    UINT32                  Name;
    ACPI_PARSE_OBJECT       *NextOp;


    /* Get the NameSegment */

    Name = AcpiPsGetName (Op);
    if (!Name)
    {
        return;
    }

    NextOp = AcpiPsGetDepthNext (NULL, Op);
    if (!NextOp)
    {
        return;
    }

    /* Check for _HID - has one argument */

    if (ACPI_COMPARE_NAME (&Name, METHOD_NAME__HID))
    {
        AcpiDmGetHardwareIdType (NextOp);
        return;
    }

    /* Exit if not _CID */

    if (!ACPI_COMPARE_NAME (&Name, METHOD_NAME__CID))
    {
        return;
    }

    /* _CID can contain a single argument or a package */

    if (NextOp->Common.AmlOpcode != AML_PACKAGE_OP)
    {
        AcpiDmGetHardwareIdType (NextOp);
        return;
    }

    /* _CID with Package: get the package length, check all elements */

    NextOp = AcpiPsGetDepthNext (NULL, NextOp);
    if (!NextOp)
    {
        return;
    }

    /* Don't need to use the length, just walk the peer list */

    NextOp = NextOp->Common.Next;
    while (NextOp)
    {
        AcpiDmGetHardwareIdType (NextOp);
        NextOp = NextOp->Common.Next;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDmDecompressEisaId
 *
 * PARAMETERS:  EncodedId       - Raw encoded EISA ID.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Convert an encoded EISAID back to the original ASCII String
 *              and emit the correct ASL statement. If the ID is known, emit
 *              a description of the ID as a comment.
 *
 ******************************************************************************/

void
AcpiDmDecompressEisaId (
    UINT32                  EncodedId)
{
    char                    IdBuffer[ACPI_EISAID_STRING_SIZE];
    const AH_DEVICE_ID      *Info;


    /* Convert EISAID to a string an emit the statement */

    AcpiExEisaIdToString (IdBuffer, EncodedId);
    AcpiOsPrintf ("EisaId (\"%s\")", IdBuffer);

    /* If we know about the ID, emit the description */

    Info = AcpiAhMatchHardwareId (IdBuffer);
    if (Info)
    {
        AcpiOsPrintf (" /* %s */", Info->Description);
    }
}

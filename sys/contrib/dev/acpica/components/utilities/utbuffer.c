/******************************************************************************
 *
 * Module Name: utbuffer - Buffer dump routines
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

#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utbuffer")


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDumpBuffer
 *
 * PARAMETERS:  Buffer              - Buffer to dump
 *              Count               - Amount to dump, in bytes
 *              Display             - BYTE, WORD, DWORD, or QWORD display:
 *                                      DB_BYTE_DISPLAY
 *                                      DB_WORD_DISPLAY
 *                                      DB_DWORD_DISPLAY
 *                                      DB_QWORD_DISPLAY
 *              BaseOffset          - Beginning buffer offset (display only)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generic dump buffer in both hex and ascii.
 *
 ******************************************************************************/

void
AcpiUtDumpBuffer (
    UINT8                   *Buffer,
    UINT32                  Count,
    UINT32                  Display,
    UINT32                  BaseOffset)
{
    UINT32                  i = 0;
    UINT32                  j;
    UINT32                  Temp32;
    UINT8                   BufChar;


    if (!Buffer)
    {
        AcpiOsPrintf ("Null Buffer Pointer in DumpBuffer!\n");
        return;
    }

    if ((Count < 4) || (Count & 0x01))
    {
        Display = DB_BYTE_DISPLAY;
    }

    /* Nasty little dump buffer routine! */

    while (i < Count)
    {
        /* Print current offset */

        AcpiOsPrintf ("%8.4X: ", (BaseOffset + i));

        /* Print 16 hex chars */

        for (j = 0; j < 16;)
        {
            if (i + j >= Count)
            {
                /* Dump fill spaces */

                AcpiOsPrintf ("%*s", ((Display * 2) + 1), " ");
                j += Display;
                continue;
            }

            switch (Display)
            {
            case DB_BYTE_DISPLAY:
            default:    /* Default is BYTE display */

                AcpiOsPrintf ("%02X ", Buffer[(ACPI_SIZE) i + j]);
                break;

            case DB_WORD_DISPLAY:

                ACPI_MOVE_16_TO_32 (&Temp32, &Buffer[(ACPI_SIZE) i + j]);
                AcpiOsPrintf ("%04X ", Temp32);
                break;

            case DB_DWORD_DISPLAY:

                ACPI_MOVE_32_TO_32 (&Temp32, &Buffer[(ACPI_SIZE) i + j]);
                AcpiOsPrintf ("%08X ", Temp32);
                break;

            case DB_QWORD_DISPLAY:

                ACPI_MOVE_32_TO_32 (&Temp32, &Buffer[(ACPI_SIZE) i + j]);
                AcpiOsPrintf ("%08X", Temp32);

                ACPI_MOVE_32_TO_32 (&Temp32, &Buffer[(ACPI_SIZE) i + j + 4]);
                AcpiOsPrintf ("%08X ", Temp32);
                break;
            }

            j += Display;
        }

        /*
         * Print the ASCII equivalent characters but watch out for the bad
         * unprintable ones (printable chars are 0x20 through 0x7E)
         */
        AcpiOsPrintf (" ");
        for (j = 0; j < 16; j++)
        {
            if (i + j >= Count)
            {
                AcpiOsPrintf ("\n");
                return;
            }

            /*
             * Add comment characters so rest of line is ignored when
             * compiled
             */
            if (j == 0)
            {
                AcpiOsPrintf ("// ");
            }

            BufChar = Buffer[(ACPI_SIZE) i + j];
            if (isprint (BufChar))
            {
                AcpiOsPrintf ("%c", BufChar);
            }
            else
            {
                AcpiOsPrintf (".");
            }
        }

        /* Done with that line. */

        AcpiOsPrintf ("\n");
        i += 16;
    }

    return;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDebugDumpBuffer
 *
 * PARAMETERS:  Buffer              - Buffer to dump
 *              Count               - Amount to dump, in bytes
 *              Display             - BYTE, WORD, DWORD, or QWORD display:
 *                                      DB_BYTE_DISPLAY
 *                                      DB_WORD_DISPLAY
 *                                      DB_DWORD_DISPLAY
 *                                      DB_QWORD_DISPLAY
 *              ComponentID         - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generic dump buffer in both hex and ascii.
 *
 ******************************************************************************/

void
AcpiUtDebugDumpBuffer (
    UINT8                   *Buffer,
    UINT32                  Count,
    UINT32                  Display,
    UINT32                  ComponentId)
{

    /* Only dump the buffer if tracing is enabled */

    if (!((ACPI_LV_TABLES & AcpiDbgLevel) &&
        (ComponentId & AcpiDbgLayer)))
    {
        return;
    }

    AcpiUtDumpBuffer (Buffer, Count, Display, 0);
}


#ifdef ACPI_APPLICATION
/*******************************************************************************
 *
 * FUNCTION:    AcpiUtDumpBufferToFile
 *
 * PARAMETERS:  File                - File descriptor
 *              Buffer              - Buffer to dump
 *              Count               - Amount to dump, in bytes
 *              Display             - BYTE, WORD, DWORD, or QWORD display:
 *                                      DB_BYTE_DISPLAY
 *                                      DB_WORD_DISPLAY
 *                                      DB_DWORD_DISPLAY
 *                                      DB_QWORD_DISPLAY
 *              BaseOffset          - Beginning buffer offset (display only)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generic dump buffer in both hex and ascii to a file.
 *
 ******************************************************************************/

void
AcpiUtDumpBufferToFile (
    ACPI_FILE               File,
    UINT8                   *Buffer,
    UINT32                  Count,
    UINT32                  Display,
    UINT32                  BaseOffset)
{
    UINT32                  i = 0;
    UINT32                  j;
    UINT32                  Temp32;
    UINT8                   BufChar;


    if (!Buffer)
    {
        fprintf (File, "Null Buffer Pointer in DumpBuffer!\n");
        return;
    }

    if ((Count < 4) || (Count & 0x01))
    {
        Display = DB_BYTE_DISPLAY;
    }

    /* Nasty little dump buffer routine! */

    while (i < Count)
    {
        /* Print current offset */

        fprintf (File, "%8.4X: ", (BaseOffset + i));

        /* Print 16 hex chars */

        for (j = 0; j < 16;)
        {
            if (i + j >= Count)
            {
                /* Dump fill spaces */

                fprintf (File, "%*s", ((Display * 2) + 1), " ");
                j += Display;
                continue;
            }

            switch (Display)
            {
            case DB_BYTE_DISPLAY:
            default:    /* Default is BYTE display */

                fprintf (File, "%02X ", Buffer[(ACPI_SIZE) i + j]);
                break;

            case DB_WORD_DISPLAY:

                ACPI_MOVE_16_TO_32 (&Temp32, &Buffer[(ACPI_SIZE) i + j]);
                fprintf (File, "%04X ", Temp32);
                break;

            case DB_DWORD_DISPLAY:

                ACPI_MOVE_32_TO_32 (&Temp32, &Buffer[(ACPI_SIZE) i + j]);
                fprintf (File, "%08X ", Temp32);
                break;

            case DB_QWORD_DISPLAY:

                ACPI_MOVE_32_TO_32 (&Temp32, &Buffer[(ACPI_SIZE) i + j]);
                fprintf (File, "%08X", Temp32);

                ACPI_MOVE_32_TO_32 (&Temp32, &Buffer[(ACPI_SIZE) i + j + 4]);
                fprintf (File, "%08X ", Temp32);
                break;
            }

            j += Display;
        }

        /*
         * Print the ASCII equivalent characters but watch out for the bad
         * unprintable ones (printable chars are 0x20 through 0x7E)
         */
        fprintf (File, " ");
        for (j = 0; j < 16; j++)
        {
            if (i + j >= Count)
            {
                fprintf (File, "\n");
                return;
            }

            BufChar = Buffer[(ACPI_SIZE) i + j];
            if (isprint (BufChar))
            {
                fprintf (File, "%c", BufChar);
            }
            else
            {
                fprintf (File, ".");
            }
        }

        /* Done with that line. */

        fprintf (File, "\n");
        i += 16;
    }

    return;
}
#endif

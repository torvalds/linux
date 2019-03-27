/******************************************************************************
 *
 * Module Name: aslascii - ASCII detection and support routines
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

#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include <contrib/dev/acpica/include/actables.h>
#include <contrib/dev/acpica/include/acapps.h>

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslascii")


/* Local prototypes */

static void
FlConsumeAnsiComment (
    FILE                    *Handle,
    ASL_FILE_STATUS         *Status);

static void
FlConsumeNewComment (
    FILE                    *Handle,
    ASL_FILE_STATUS         *Status);


/*******************************************************************************
 *
 * FUNCTION:    FlIsFileAsciiSource
 *
 * PARAMETERS:  Filename            - Full input filename
 *              DisplayErrors       - TRUE if error messages desired
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Verify that the input file is entirely ASCII. Ignores characters
 *              within comments. Note: does not handle nested comments and does
 *              not handle comment delimiters within string literals. However,
 *              on the rare chance this happens and an invalid character is
 *              missed, the parser will catch the error by failing in some
 *              spectactular manner.
 *
 ******************************************************************************/

ACPI_STATUS
FlIsFileAsciiSource (
    char                    *Filename,
    BOOLEAN                 DisplayErrors)
{
    UINT8                   Byte;
    UINT32                  BadBytes = 0;
    BOOLEAN                 OpeningComment = FALSE;
    ASL_FILE_STATUS         Status;
    FILE                    *Handle;


    /* Open file in text mode so file offset is always accurate */

    Handle = fopen (Filename, "rb");
    if (!Handle)
    {
        perror ("Could not open input file");
        return (AE_ERROR);
    }

    Status.Line = 1;
    Status.Offset = 0;

    /* Read the entire file */

    while (fread (&Byte, 1, 1, Handle) == 1)
    {
        /* Ignore comment fields (allow non-ascii within) */

        if (OpeningComment)
        {
            /* Check for second comment open delimiter */

            if (Byte == '*')
            {
                FlConsumeAnsiComment (Handle, &Status);
            }

            if (Byte == '/')
            {
                FlConsumeNewComment (Handle, &Status);
            }

            /* Reset */

            OpeningComment = FALSE;
        }
        else if (Byte == '/')
        {
            OpeningComment = TRUE;
        }

        /* Check for an ASCII character */

        if (!ACPI_IS_ASCII (Byte))
        {
            if ((BadBytes < 10) && (DisplayErrors))
            {
                AcpiOsPrintf (
                    "Found non-ASCII character in source text: "
                    "0x%2.2X in line %u, file offset 0x%2.2X\n",
                    Byte, Status.Line, Status.Offset);
            }
            BadBytes++;
        }

        /* Ensure character is either printable or a "space" char */

        else if (!isprint (Byte) && !isspace (Byte))
        {
            if ((BadBytes < 10) && (DisplayErrors))
            {
                AcpiOsPrintf (
                    "Found invalid character in source text: "
                    "0x%2.2X in line %u, file offset 0x%2.2X\n",
                    Byte, Status.Line, Status.Offset);
            }
            BadBytes++;
        }

        /* Update line counter as necessary */

        if (Byte == 0x0A)
        {
            Status.Line++;
        }

        Status.Offset++;
    }

    fclose (Handle);

    /* Were there any non-ASCII characters in the file? */

    if (BadBytes)
    {
        fprintf (stderr,
            "File appears to be binary: found %u non-ASCII characters, disassembling\n",
            BadBytes);
        if (DisplayErrors)
        {
            AcpiOsPrintf (
                "Total %u invalid characters found in input source text, "
                "could be a binary file\n", BadBytes);
            AslError (ASL_ERROR, ASL_MSG_NON_ASCII, NULL, Filename);
        }

        return (AE_BAD_CHARACTER);
    }

    /* File is OK (100% ASCII) */

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    FlConsumeAnsiComment
 *
 * PARAMETERS:  Handle              - Open input file
 *              Status              - File current status struct
 *
 * RETURN:      Number of lines consumed
 *
 * DESCRIPTION: Step over a normal slash-star type comment
 *
 ******************************************************************************/

static void
FlConsumeAnsiComment (
    FILE                    *Handle,
    ASL_FILE_STATUS         *Status)
{
    UINT8                   Byte;
    BOOLEAN                 ClosingComment = FALSE;


    while (fread (&Byte, 1, 1, Handle) == 1)
    {
        /* Scan until comment close is found */

        if (ClosingComment)
        {
            if (Byte == '/')
            {
                Status->Offset++;
                return;
            }

            if (Byte != '*')
            {
                /* Reset */

                ClosingComment = FALSE;
            }
        }
        else if (Byte == '*')
        {
            ClosingComment = TRUE;
        }

        /* Maintain line count */

        if (Byte == 0x0A)
        {
            Status->Line++;
        }

        Status->Offset++;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    FlConsumeNewComment
 *
 * PARAMETERS:  Handle              - Open input file
 *              Status              - File current status struct
 *
 * RETURN:      Number of lines consumed
 *
 * DESCRIPTION: Step over a slash-slash type of comment
 *
 ******************************************************************************/

static void
FlConsumeNewComment (
    FILE                    *Handle,
    ASL_FILE_STATUS         *Status)
{
    UINT8                   Byte;


    while (fread (&Byte, 1, 1, Handle) == 1)
    {
        Status->Offset++;

        /* Comment ends at newline */

        if (Byte == 0x0A)
        {
            Status->Line++;
            return;
        }
    }
}

/******************************************************************************
 *
 * Module Name: acgetline - local line editing
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
#include <contrib/dev/acpica/include/acdebug.h>

/*
 * This is an os-independent implementation of line-editing services needed
 * by the AcpiExec utility. It uses getchar() and putchar() and the existing
 * history support provided by the AML debugger. It assumes that the terminal
 * is in the correct line-editing mode such as raw and noecho. The OSL
 * interface AcpiOsInitialize should do this. AcpiOsTerminate should put the
 * terminal back into the original mode.
 */
#define _COMPONENT          ACPI_OS_SERVICES
        ACPI_MODULE_NAME    ("acgetline")


/* Local prototypes */

static void
AcpiAcClearLine (
    UINT32                  EndOfLine,
    UINT32                  CursorPosition);

/* Various ASCII constants */

#define _ASCII_NUL                  0
#define _ASCII_BACKSPACE            0x08
#define _ASCII_TAB                  0x09
#define _ASCII_ESCAPE               0x1B
#define _ASCII_SPACE                0x20
#define _ASCII_LEFT_BRACKET         0x5B
#define _ASCII_DEL                  0x7F
#define _ASCII_UP_ARROW             'A'
#define _ASCII_DOWN_ARROW           'B'
#define _ASCII_RIGHT_ARROW          'C'
#define _ASCII_LEFT_ARROW           'D'
#define _ASCII_NEWLINE              '\n'

extern UINT32               AcpiGbl_NextCmdNum;

/* Erase a single character on the input command line */

#define ACPI_CLEAR_CHAR() \
    putchar (_ASCII_BACKSPACE); \
    putchar (_ASCII_SPACE); \
    putchar (_ASCII_BACKSPACE);

/* Backup cursor by Count positions */

#define ACPI_BACKUP_CURSOR(i, Count) \
    for (i = 0; i < (Count); i++) \
        {putchar (_ASCII_BACKSPACE);}


/******************************************************************************
 *
 * FUNCTION:    AcpiAcClearLine
 *
 * PARAMETERS:  EndOfLine           - Current end-of-line index
 *              CursorPosition      - Current cursor position within line
 *
 * RETURN:      None
 *
 * DESCRIPTION: Clear the entire command line the hard way, but probably the
 *              most portable.
 *
 *****************************************************************************/

static void
AcpiAcClearLine (
    UINT32                  EndOfLine,
    UINT32                  CursorPosition)
{
    UINT32                  i;


    if (CursorPosition < EndOfLine)
    {
        /* Clear line from current position to end of line */

        for (i = 0; i < (EndOfLine - CursorPosition); i++)
        {
            putchar (' ');
        }
    }

    /* Clear the entire line */

    for (; EndOfLine > 0; EndOfLine--)
    {
        ACPI_CLEAR_CHAR ();
    }
}


/******************************************************************************
 *
 * FUNCTION:    AcpiOsGetLine
 *
 * PARAMETERS:  Buffer              - Where to return the command line
 *              BufferLength        - Maximum length of Buffer
 *              BytesRead           - Where the actual byte count is returned
 *
 * RETURN:      Status and actual bytes read
 *
 * DESCRIPTION: Get the next input line from the terminal. NOTE: terminal
 *              is expected to be in a mode that supports line-editing (raw,
 *              noecho). This function is intended to be very portable. Also,
 *              it uses the history support implemented in the AML debugger.
 *
 *****************************************************************************/

ACPI_STATUS
AcpiOsGetLine (
    char                    *Buffer,
    UINT32                  BufferLength,
    UINT32                  *BytesRead)
{
    char                    *NextCommand;
    UINT32                  MaxCommandIndex = AcpiGbl_NextCmdNum - 1;
    UINT32                  CurrentCommandIndex = MaxCommandIndex;
    UINT32                  PreviousCommandIndex = MaxCommandIndex;
    int                     InputChar;
    UINT32                  CursorPosition = 0;
    UINT32                  EndOfLine = 0;
    UINT32                  i;


    /* Always clear the line buffer before we read a new line */

    memset (Buffer, 0, BufferLength);

    /*
     * This loop gets one character at a time (except for esc sequences)
     * until a newline or error is detected.
     *
     * Note: Don't attempt to write terminal control ESC sequences, even
     * though it makes certain things more difficult.
     */
    while (1)
    {
        if (EndOfLine >= (BufferLength - 1))
        {
            return (AE_BUFFER_OVERFLOW);
        }

        InputChar = getchar ();
        switch (InputChar)
        {
        default: /* This is the normal character case */

            /* Echo the character (at EOL) and copy it to the line buffer */

            if (EndOfLine == CursorPosition)
            {
                putchar (InputChar);
                Buffer[EndOfLine] = (char) InputChar;

                EndOfLine++;
                CursorPosition++;
                Buffer[EndOfLine] = 0;
                continue;
            }

            /* Insert character into the middle of the buffer */

            memmove (&Buffer[CursorPosition + 1], &Buffer[CursorPosition],
                (EndOfLine - CursorPosition + 1));

            Buffer [CursorPosition] = (char) InputChar;
            Buffer [EndOfLine + 1] = 0;

            /* Display the new part of line starting at the new character */

            fprintf (stdout, "%s", &Buffer[CursorPosition]);

            /* Restore cursor */

            ACPI_BACKUP_CURSOR (i, EndOfLine - CursorPosition);
            CursorPosition++;
            EndOfLine++;
            continue;

        case _ASCII_DEL: /* Backspace key */

            if (!EndOfLine) /* Any characters on the command line? */
            {
                continue;
            }

            if (EndOfLine == CursorPosition) /* Erase the final character */
            {
                ACPI_CLEAR_CHAR ();
                EndOfLine--;
                CursorPosition--;
                continue;
            }

            if (!CursorPosition) /* Do not backup beyond start of line */
            {
                continue;
            }

            /* Remove the character from the line */

            memmove (&Buffer[CursorPosition - 1], &Buffer[CursorPosition],
                (EndOfLine - CursorPosition + 1));

            /* Display the new part of line starting at the new character */

            putchar (_ASCII_BACKSPACE);
            fprintf (stdout, "%s ", &Buffer[CursorPosition - 1]);

            /* Restore cursor */

            ACPI_BACKUP_CURSOR (i, EndOfLine - CursorPosition + 1);
            EndOfLine--;

            if (CursorPosition > 0)
            {
                CursorPosition--;
            }
            continue;

        case _ASCII_NEWLINE: /* Normal exit case at end of command line */
        case _ASCII_NUL:

            /* Return the number of bytes in the command line string */

            if (BytesRead)
            {
                *BytesRead = EndOfLine;
            }

            /* Echo, terminate string buffer, and exit */

            putchar (InputChar);
            Buffer[EndOfLine] = 0;
            return (AE_OK);

        case _ASCII_TAB:

            /* Ignore */

            continue;

        case EOF:

            return (AE_ERROR);

        case _ASCII_ESCAPE:

            /* Check for escape sequences of the form "ESC[x" */

            InputChar = getchar ();
            if (InputChar != _ASCII_LEFT_BRACKET)
            {
                continue; /* Ignore this ESC, does not have the '[' */
            }

            /* Get the code following the ESC [ */

            InputChar = getchar (); /* Backup one character */
            switch (InputChar)
            {
            case _ASCII_LEFT_ARROW:

                if (CursorPosition > 0)
                {
                    putchar (_ASCII_BACKSPACE);
                    CursorPosition--;
                }
                continue;

            case _ASCII_RIGHT_ARROW:
                /*
                 * Move one character forward. Do this without sending
                 * ESC sequence to the terminal for max portability.
                 */
                if (CursorPosition < EndOfLine)
                {
                    /* Backup to start of line and print the entire line */

                    ACPI_BACKUP_CURSOR (i, CursorPosition);
                    fprintf (stdout, "%s", Buffer);

                    /* Backup to where the cursor should be */

                    CursorPosition++;
                    ACPI_BACKUP_CURSOR (i, EndOfLine - CursorPosition);
                }
                continue;

            case _ASCII_UP_ARROW:

                /* If no commands available or at start of history list, ignore */

                if (!CurrentCommandIndex)
                {
                    continue;
                }

                /* Manage our up/down progress */

                if (CurrentCommandIndex > PreviousCommandIndex)
                {
                    CurrentCommandIndex = PreviousCommandIndex;
                }

                /* Get the historical command from the debugger */

                NextCommand = AcpiDbGetHistoryByIndex (CurrentCommandIndex);
                if (!NextCommand)
                {
                    return (AE_ERROR);
                }

                /* Make this the active command and echo it */

                AcpiAcClearLine (EndOfLine, CursorPosition);
                strcpy (Buffer, NextCommand);
                fprintf (stdout, "%s", Buffer);
                EndOfLine = CursorPosition = strlen (Buffer);

                PreviousCommandIndex = CurrentCommandIndex;
                CurrentCommandIndex--;
                continue;

            case _ASCII_DOWN_ARROW:

                if (!MaxCommandIndex) /* Any commands available? */
                {
                    continue;
                }

                /* Manage our up/down progress */

                if (CurrentCommandIndex < PreviousCommandIndex)
                {
                    CurrentCommandIndex = PreviousCommandIndex;
                }

                /* If we are the end of the history list, output a clear new line */

                if ((CurrentCommandIndex + 1) > MaxCommandIndex)
                {
                    AcpiAcClearLine (EndOfLine, CursorPosition);
                    EndOfLine = CursorPosition = 0;
                    PreviousCommandIndex = CurrentCommandIndex;
                    continue;
                }

                PreviousCommandIndex = CurrentCommandIndex;
                CurrentCommandIndex++;

                 /* Get the historical command from the debugger */

                NextCommand = AcpiDbGetHistoryByIndex (CurrentCommandIndex);
                if (!NextCommand)
                {
                    return (AE_ERROR);
                }

                /* Make this the active command and echo it */

                AcpiAcClearLine (EndOfLine, CursorPosition);
                strcpy (Buffer, NextCommand);
                fprintf (stdout, "%s", Buffer);
                EndOfLine = CursorPosition = strlen (Buffer);
                continue;

            case 0x31:
            case 0x32:
            case 0x33:
            case 0x34:
            case 0x35:
            case 0x36:
                /*
                 * Ignore the various keys like insert/delete/home/end, etc.
                 * But we must eat the final character of the ESC sequence.
                 */
                InputChar = getchar ();
                continue;

            default:

                /* Ignore random escape sequences that we don't care about */

                continue;
            }
            continue;
        }
    }
}

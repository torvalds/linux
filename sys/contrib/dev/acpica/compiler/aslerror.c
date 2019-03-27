/******************************************************************************
 *
 * Module Name: aslerror - Error handling and statistics
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

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslerror")

/* Local prototypes */

static void
AeAddToErrorLog (
    ASL_ERROR_MSG           *Enode);

static BOOLEAN
AslIsExceptionExpected (
    UINT8                   Level,
    UINT16                  MessageId);

static BOOLEAN
AslIsExceptionDisabled (
    UINT8                   Level,
    UINT16                  MessageId);

static void AslInitEnode (
    ASL_ERROR_MSG           **Enode,
    UINT8                   Level,
    UINT16                  MessageId,
    UINT32                  LineNumber,
    UINT32                  LogicalLineNumber,
    UINT32                  LogicalByteOffset,
    UINT32                  Column,
    char                    *Filename,
    char                    *Message,
    char                    *SourceLine,
    ASL_ERROR_MSG           *SubError);

static void
AslLogNewError (
    UINT8                   Level,
    UINT16                  MessageId,
    UINT32                  LineNumber,
    UINT32                  LogicalLineNumber,
    UINT32                  LogicalByteOffset,
    UINT32                  Column,
    char                    *Filename,
    char                    *Message,
    char                    *SourceLine,
    ASL_ERROR_MSG           *SubError);

static void
AePrintSubError (
    FILE                    *OutputFile,
    ASL_ERROR_MSG           *Enode);

static UINT8
GetModifiedLevel (
    UINT8                   Level,
    UINT16                  MessageId);


/*******************************************************************************
 *
 * FUNCTION:    AslAbort
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the error log and abort the compiler. Used for serious
 *              I/O errors.
 *
 ******************************************************************************/

void
AslAbort (
    void)
{

    AePrintErrorLog (ASL_FILE_STDERR);
    if (AslGbl_DebugFlag)
    {
        /* Print error summary to stdout also */

        AePrintErrorLog (ASL_FILE_STDOUT);
    }

    exit (1);
}


/*******************************************************************************
 *
 * FUNCTION:    AeClearErrorLog
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Empty the error list
 *
 ******************************************************************************/

void
AeClearErrorLog (
    void)
{
    ASL_ERROR_MSG           *Enode = AslGbl_ErrorLog;
    ASL_ERROR_MSG           *Next;


    /* Walk the error node list */

    while (Enode)
    {
        Next = Enode->Next;
        ACPI_FREE (Enode);
        Enode = Next;
    }

   AslGbl_ErrorLog = NULL;
}


/*******************************************************************************
 *
 * FUNCTION:    AeAddToErrorLog
 *
 * PARAMETERS:  Enode       - An error node to add to the log
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add a new error node to the error log. The error log is
 *              ordered by the "logical" line number (cumulative line number
 *              including all include files.)
 *
 ******************************************************************************/

static void
AeAddToErrorLog (
    ASL_ERROR_MSG           *Enode)
{
    ASL_ERROR_MSG           *Next;
    ASL_ERROR_MSG           *Prev;


    /* If Gbl_ErrorLog is null, this is the first error node */

    if (!AslGbl_ErrorLog)
    {
        AslGbl_ErrorLog = Enode;
        return;
    }

    /*
     * Walk error list until we find a line number greater than ours.
     * List is sorted according to line number.
     */
    Prev = NULL;
    Next = AslGbl_ErrorLog;

    while ((Next) && (Next->LogicalLineNumber <= Enode->LogicalLineNumber))
    {
        Prev = Next;
        Next = Next->Next;
    }

    /* Found our place in the list */

    Enode->Next = Next;

    if (Prev)
    {
        Prev->Next = Enode;
    }
    else
    {
        AslGbl_ErrorLog = Enode;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AeDecodeErrorMessageId
 *
 * PARAMETERS:  OutputFile      - Output file
 *              Enode           - Error node to print
 *              PrematureEOF    - True = PrematureEOF has been reached
 *              Total           - Total legth of line
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print the source line of an error.
 *
 ******************************************************************************/

static void
AeDecodeErrorMessageId (
    FILE                    *OutputFile,
    ASL_ERROR_MSG           *Enode,
    BOOLEAN                 PrematureEOF,
    UINT32                  Total)
{
    UINT32                  MsgLength;
    const char              *MainMessage;
    char                    *ExtraMessage;
    UINT32                  SourceColumn;
    UINT32                  ErrorColumn;


    fprintf (OutputFile, "%s %4.4d -",
        AeDecodeExceptionLevel (Enode->Level),
        AeBuildFullExceptionCode (Enode->Level, Enode->MessageId));

    MainMessage = AeDecodeMessageId (Enode->MessageId);
    ExtraMessage = Enode->Message;

    /* If a NULL line number, just print the decoded message */

    if (!Enode->LineNumber)
    {
        fprintf (OutputFile, " %s %s\n\n", MainMessage, ExtraMessage);
        return;
    }

    MsgLength = strlen (MainMessage);
    if (MsgLength == 0)
    {
        /* Use the secondary/extra message as main message */

        MainMessage = Enode->Message;
        if (!MainMessage)
        {
            MainMessage = "";
        }

        MsgLength = strlen (MainMessage);
        ExtraMessage = NULL;
    }

    if (AslGbl_VerboseErrors && !PrematureEOF)
    {
        if (Total >= 256)
        {
            fprintf (OutputFile, "    %s",
                MainMessage);
        }
        else
        {
            SourceColumn = Enode->Column + Enode->FilenameLength + 6 + 2;
            ErrorColumn = ASL_ERROR_LEVEL_LENGTH + 5 + 2 + 1;

            if ((MsgLength + ErrorColumn) < (SourceColumn - 1))
            {
                fprintf (OutputFile, "%*s%s",
                    (int) ((SourceColumn - 1) - ErrorColumn),
                    MainMessage, " ^ ");
            }
            else
            {
                fprintf (OutputFile, "%*s %s",
                    (int) ((SourceColumn - ErrorColumn) + 1), "^",
                    MainMessage);
            }
        }
    }
    else
    {
        fprintf (OutputFile, " %s", MainMessage);
    }

    /* Print the extra info message if present */

    if (ExtraMessage)
    {
        fprintf (OutputFile, " (%s)", ExtraMessage);
    }

    if (PrematureEOF)
    {
        fprintf (OutputFile, " and premature End-Of-File");
    }

    fprintf (OutputFile, "\n");
    if (AslGbl_VerboseErrors && !Enode->SubError)
    {
        fprintf (OutputFile, "\n");
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AePrintErrorSourceLine
 *
 * PARAMETERS:  OutputFile      - Output file
 *              Enode           - Error node to print
 *              PrematureEOF    - True = PrematureEOF has been reached
 *              Total           - amount of characters printed so far
 *
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Print the source line of an error.
 *
 ******************************************************************************/

static ACPI_STATUS
AePrintErrorSourceLine (
    FILE                    *OutputFile,
    ASL_ERROR_MSG           *Enode,
    BOOLEAN                 *PrematureEOF,
    UINT32                  *Total)
{
    UINT8                   SourceByte;
    int                     Actual;
    size_t                  RActual;
    FILE                    *SourceFile = NULL;
    long                    FileSize;


    if (!Enode->SourceLine)
    {
        /*
         * Use the merged header/source file if present, otherwise
         * use input file
         */
        SourceFile = AslGbl_Files[ASL_FILE_SOURCE_OUTPUT].Handle;
        if (!SourceFile)
        {
            SourceFile = AslGbl_Files[ASL_FILE_INPUT].Handle;
        }

        if (SourceFile)
        {
            /* Determine if the error occurred at source file EOF */

            fseek (SourceFile, 0, SEEK_END);
            FileSize = ftell (SourceFile);

            if ((long) Enode->LogicalByteOffset >= FileSize)
            {
                *PrematureEOF = TRUE;
            }
        }
        else
        {
            fprintf (OutputFile,
                "[*** iASL: Source File Does not exist ***]\n");
            return AE_IO_ERROR;
        }
    }

    /* Print filename and line number if present and valid */

    if (AslGbl_VerboseErrors)
    {
        fprintf (OutputFile, "%-8s", Enode->Filename);

        if (Enode->SourceLine && Enode->LineNumber)
        {
            fprintf (OutputFile, " %6u: %s",
                Enode->LineNumber, Enode->SourceLine);
        }
        else if (Enode->LineNumber)
        {
            fprintf (OutputFile, " %6u: ", Enode->LineNumber);

            /*
             * If not at EOF, get the corresponding source code line
             * and display it. Don't attempt this if we have a
             * premature EOF condition.
             */
            if (*PrematureEOF)
            {
                fprintf (OutputFile, "\n");
                return AE_OK;
            }
            /*
             * Seek to the offset in the combined source file,
             * read the source line, and write it to the output.
             */
            Actual = fseek (SourceFile,
                (long) Enode->LogicalByteOffset, (int) SEEK_SET);
            if (Actual)
            {
                fprintf (OutputFile,
                    "[*** iASL: Seek error on source code temp file %s ***]",
                    AslGbl_Files[ASL_FILE_SOURCE_OUTPUT].Filename);

                fprintf (OutputFile, "\n");
                return AE_OK;
            }
            RActual = fread (&SourceByte, 1, 1, SourceFile);
            if (RActual != 1)
            {
                fprintf (OutputFile,
                    "[*** iASL: Read error on source code temp file %s ***]",
                    AslGbl_Files[ASL_FILE_SOURCE_OUTPUT].Filename);
                return AE_IO_ERROR;
            }
                /* Read/write the source line, up to the maximum line length */

            while (RActual && SourceByte && (SourceByte != '\n'))
            {
                if (*Total < 256)
                {
                    /* After the max line length, we will just read the line, no write */

                    if (fwrite (&SourceByte, 1, 1, OutputFile) != 1)
                    {
                        printf ("[*** iASL: Write error on output file ***]\n");
                        return AE_IO_ERROR;
                    }
                }
                else if (*Total == 256)
                {
                    fprintf (OutputFile,
                        "\n[*** iASL: Very long input line, message below refers to column %u ***]",
                        Enode->Column);
                }

                RActual = fread (&SourceByte, 1, 1, SourceFile);
                if (RActual != 1)
                {
                    fprintf (OutputFile,
                        "[*** iASL: Read error on source code temp file %s ***]",
                        AslGbl_Files[ASL_FILE_SOURCE_OUTPUT].Filename);

                    return AE_IO_ERROR;
                }
                *Total += 1;
            }

            fprintf (OutputFile, "\n");
        }
    }
    else
    {
        /*
         * Less verbose version of the error message, enabled via the
         * -vi switch. The format is compatible with MS Visual Studio.
         */
        fprintf (OutputFile, "%s", Enode->Filename);

        if (Enode->LineNumber)
        {
            fprintf (OutputFile, "(%u) : ",
                Enode->LineNumber);
        }
    }

    return AE_OK;
}

/*******************************************************************************
 *
 * FUNCTION:    AePrintException
 *
 * PARAMETERS:  FileId          - ID of output file
 *              Enode           - Error node to print
 *              Header          - Additional text before each message
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print the contents of an error node.
 *
 * NOTE:        We don't use the FlxxxFile I/O functions here because on error
 *              they abort the compiler and call this function!  Since we
 *              are reporting errors here, we ignore most output errors and
 *              just try to get out as much as we can.
 *
 ******************************************************************************/

void
AePrintException (
    UINT32                  FileId,
    ASL_ERROR_MSG           *Enode,
    char                    *Header)
{
    FILE                    *OutputFile;
    BOOLEAN                 PrematureEOF = FALSE;
    UINT32                  Total = 0;
    ACPI_STATUS             Status;
    ASL_ERROR_MSG           *Child = Enode->SubError;


    if (AslGbl_NoErrors)
    {
        return;
    }

    /*
     * Only listing files have a header, and remarks/optimizations
     * are always output
     */
    if (!Header)
    {
        /* Ignore remarks if requested */

        switch (Enode->Level)
        {
        case ASL_WARNING:
        case ASL_WARNING2:
        case ASL_WARNING3:

            if (!AslGbl_DisplayWarnings)
            {
                return;
            }
            break;

        case ASL_REMARK:

            if (!AslGbl_DisplayRemarks)
            {
                return;
            }
            break;

        case ASL_OPTIMIZATION:

            if (!AslGbl_DisplayOptimizations)
            {
                return;
            }
            break;

        default:

            break;
        }
    }

    /* Get the various required file handles */

    OutputFile = AslGbl_Files[FileId].Handle;

    if (Header)
    {
        fprintf (OutputFile, "%s", Header);
    }

    if (!Enode->Filename)
    {
        AeDecodeErrorMessageId (OutputFile, Enode, PrematureEOF, Total);
        return;
    }

    Status = AePrintErrorSourceLine (OutputFile, Enode, &PrematureEOF, &Total);
    if (ACPI_FAILURE (Status))
    {
        return;
    }

    /* If a NULL message ID, just print the raw message */

    if (Enode->MessageId == 0)
    {
        fprintf (OutputFile, "%s\n", Enode->Message);
        return;
    }

    AeDecodeErrorMessageId (OutputFile, Enode, PrematureEOF, Total);

    while (Child)
    {
        fprintf (OutputFile, "\n");
        AePrintSubError (OutputFile, Child);
        Child = Child->SubError;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AePrintSubError
 *
 * PARAMETERS:  OutputFile      - Output file
 *              Enode           - Error node to print
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print the contents of an error nodes. This function is tailored
 *              to print error nodes that are SubErrors within ASL_ERROR_MSG
 *
 ******************************************************************************/

static void
AePrintSubError (
    FILE                    *OutputFile,
    ASL_ERROR_MSG           *Enode)
{
    UINT32                  Total = 0;
    BOOLEAN                 PrematureEOF = FALSE;
    const char              *MainMessage;


    MainMessage = AeDecodeMessageId (Enode->MessageId);

    fprintf (OutputFile, "    %s%s", MainMessage, "\n    ");
    (void) AePrintErrorSourceLine (OutputFile, Enode, &PrematureEOF, &Total);
    fprintf (OutputFile, "\n");
}


/*******************************************************************************
 *
 * FUNCTION:    AePrintErrorLog
 *
 * PARAMETERS:  FileId           - Where to output the error log
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print the entire contents of the error log
 *
 ******************************************************************************/

void
AePrintErrorLog (
    UINT32                  FileId)
{
    ASL_ERROR_MSG           *Enode = AslGbl_ErrorLog;


    /* Walk the error node list */

    while (Enode)
    {
        AePrintException (FileId, Enode, NULL);
        Enode = Enode->Next;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AslInitEnode
 *
 * PARAMETERS:  InputEnode          - Input Error node to initialize
 *              Level               - Seriousness (Warning/error, etc.)
 *              MessageId           - Index into global message buffer
 *              CurrentLineNumber   - Actual file line number
 *              LogicalLineNumber   - Cumulative line number
 *              LogicalByteOffset   - Byte offset in source file
 *              Column              - Column in current line
 *              Filename            - source filename
 *              ExtraMessage        - additional error message
 *              SourceLine          - Line of error source code
 *              SubError            - SubError of this InputEnode
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize an Error node
 *
 ******************************************************************************/

static void AslInitEnode (
    ASL_ERROR_MSG           **InputEnode,
    UINT8                   Level,
    UINT16                  MessageId,
    UINT32                  LineNumber,
    UINT32                  LogicalLineNumber,
    UINT32                  LogicalByteOffset,
    UINT32                  Column,
    char                    *Filename,
    char                    *ExtraMessage,
    char                    *SourceLine,
    ASL_ERROR_MSG           *SubError)
{
    ASL_ERROR_MSG           *Enode;


    *InputEnode = UtLocalCalloc (sizeof (ASL_ERROR_MSG));
    Enode = *InputEnode;
    Enode->Level                = Level;
    Enode->MessageId            = MessageId;
    Enode->LineNumber           = LineNumber;
    Enode->LogicalLineNumber    = LogicalLineNumber;
    Enode->LogicalByteOffset    = LogicalByteOffset;
    Enode->Column               = Column;
    Enode->SubError             = SubError;
    Enode->Message              = NULL;
    Enode->SourceLine           = NULL;
    Enode->Filename             = NULL;

    if (ExtraMessage)
    {
        /* Allocate a buffer for the message and a new error node */

        Enode->Message = UtLocalCacheCalloc (strlen (ExtraMessage) + 1);

        /* Keep a copy of the extra message */

        strcpy (Enode->Message, ExtraMessage);
    }

    if (SourceLine)
    {
        Enode->SourceLine = UtLocalCalloc (strlen (SourceLine) + 1);
        strcpy (Enode->SourceLine, SourceLine);
    }


    if (Filename)
    {
        Enode->Filename = Filename;
        Enode->FilenameLength = strlen (Filename);
        if (Enode->FilenameLength < 6)
        {
            Enode->FilenameLength = 6;
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AslCommonError2
 *
 * PARAMETERS:  Level               - Seriousness (Warning/error, etc.)
 *              MessageId           - Index into global message buffer
 *              LineNumber          - Actual file line number
 *              Column              - Column in current line
 *              SourceLine          - Actual source code line
 *              Filename            - source filename
 *              ExtraMessage        - additional error message
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create a new error node and add it to the error log
 *
 ******************************************************************************/

void
AslCommonError2 (
    UINT8                   Level,
    UINT16                  MessageId,
    UINT32                  LineNumber,
    UINT32                  Column,
    char                    *SourceLine,
    char                    *Filename,
    char                    *ExtraMessage)
{
    AslLogNewError (Level, MessageId, LineNumber, LineNumber, 0, Column,
        Filename, ExtraMessage, SourceLine, NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    AslCommonError
 *
 * PARAMETERS:  Level               - Seriousness (Warning/error, etc.)
 *              MessageId           - Index into global message buffer
 *              CurrentLineNumber   - Actual file line number
 *              LogicalLineNumber   - Cumulative line number
 *              LogicalByteOffset   - Byte offset in source file
 *              Column              - Column in current line
 *              Filename            - source filename
 *              ExtraMessage        - additional error message
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create a new error node and add it to the error log
 *
 ******************************************************************************/

void
AslCommonError (
    UINT8                   Level,
    UINT16                  MessageId,
    UINT32                  CurrentLineNumber,
    UINT32                  LogicalLineNumber,
    UINT32                  LogicalByteOffset,
    UINT32                  Column,
    char                    *Filename,
    char                    *ExtraMessage)
{
    /* Check if user wants to ignore this exception */

    if (AslIsExceptionIgnored (Level, MessageId))
    {
        return;
    }

    AslLogNewError (Level, MessageId, CurrentLineNumber, LogicalLineNumber,
        LogicalByteOffset, Column, Filename, ExtraMessage,
        NULL, NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    AslLogNewError
 *
 * PARAMETERS:  Level               - Seriousness (Warning/error, etc.)
 *              MessageId           - Index into global message buffer
 *              CurrentLineNumber   - Actual file line number
 *              LogicalLineNumber   - Cumulative line number
 *              LogicalByteOffset   - Byte offset in source file
 *              Column              - Column in current line
 *              Filename            - source filename
 *              Message             - additional error message
 *              SourceLine          - Actual line of source code
 *              SubError            - Sub-error associated with this error
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create a new error node and add it to the error log
 *
 ******************************************************************************/
static void
AslLogNewError (
    UINT8                   Level,
    UINT16                  MessageId,
    UINT32                  LineNumber,
    UINT32                  LogicalLineNumber,
    UINT32                  LogicalByteOffset,
    UINT32                  Column,
    char                    *Filename,
    char                    *Message,
    char                    *SourceLine,
    ASL_ERROR_MSG           *SubError)
{
    ASL_ERROR_MSG           *Enode = NULL;
    UINT8                   ModifiedLevel = GetModifiedLevel (Level, MessageId);


    AslInitEnode (&Enode, ModifiedLevel, MessageId, LineNumber,
        LogicalLineNumber, LogicalByteOffset, Column, Filename, Message,
        SourceLine, SubError);

    /* Add the new node to the error node list */

    AeAddToErrorLog (Enode);

    if (AslGbl_DebugFlag)
    {
        /* stderr is a file, send error to it immediately */

        AePrintException (ASL_FILE_STDERR, Enode, NULL);
    }

    AslGbl_ExceptionCount[ModifiedLevel]++;
    if (AslGbl_ExceptionCount[ASL_ERROR] > ASL_MAX_ERROR_COUNT)
    {
        printf ("\nMaximum error count (%u) exceeded\n", ASL_MAX_ERROR_COUNT);

        AslGbl_SourceLine = 0;
        AslGbl_NextError = AslGbl_ErrorLog;
        CmCleanupAndExit ();
        exit(1);
    }

    return;
}


/*******************************************************************************
 *
 * FUNCTION:    GetModifiedLevel
 *
 * PARAMETERS:  Level           - Seriousness (Warning/error, etc.)
 *              MessageId       - Index into global message buffer
 *
 * RETURN:      UINT8           - modified level
 *
 * DESCRIPTION: Get the modified level of exception codes that are reported as
 *              errors from the -ww option.
 *
 ******************************************************************************/

static UINT8
GetModifiedLevel (
    UINT8                   Level,
    UINT16                  MessageId)
{
    UINT16                  i;
    UINT16                  ExceptionCode;


    ExceptionCode = AeBuildFullExceptionCode (Level, MessageId);

    for (i = 0; i < AslGbl_ElevatedMessagesIndex; i++)
    {
        if (ExceptionCode == AslGbl_ElevatedMessages[i])
        {
            return (ASL_ERROR);
        }
    }

    return (Level);
}


/*******************************************************************************
 *
 * FUNCTION:    AslIsExceptionIgnored
 *
 * PARAMETERS:  Level           - Seriousness (Warning/error, etc.)
 *              MessageId       - Index into global message buffer
 *
 * RETURN:      BOOLEAN
 *
 * DESCRIPTION: Check if a particular exception is ignored. In this case it
 *              means that the exception is (expected or disabled.
 *
 ******************************************************************************/

BOOLEAN
AslIsExceptionIgnored (
    UINT8                   Level,
    UINT16                  MessageId)
{
    BOOLEAN                 ExceptionIgnored;


    /* Note: this allows exception to be disabled and expected */

    ExceptionIgnored = AslIsExceptionDisabled (Level, MessageId);
    ExceptionIgnored |= AslIsExceptionExpected (Level, MessageId);

    return (AslGbl_AllExceptionsDisabled || ExceptionIgnored);
}


/*******************************************************************************
 *
 * FUNCTION:    AslCheckExpectException
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Check the global expected messages table and raise an error
 *              for each message that has not been received.
 *
 ******************************************************************************/

void
AslCheckExpectedExceptions (
    void)
{
    UINT8                   i;


    for (i = 0; i < AslGbl_ExpectedMessagesIndex; ++i)
    {
        if (!AslGbl_ExpectedMessages[i].MessageReceived)
        {
            AslError (ASL_ERROR, ASL_MSG_EXCEPTION_NOT_RECEIVED, NULL,
                AslGbl_ExpectedMessages[i].MessageIdStr);
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AslExpectException
 *
 * PARAMETERS:  MessageIdString     - ID of excepted exception during compile
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter a message ID into the global expected messages table
 *              If these messages are not raised during the compilation, throw
 *              an error.
 *
 ******************************************************************************/

ACPI_STATUS
AslExpectException (
    char                    *MessageIdString)
{
    UINT32                  MessageId;


    /* Convert argument to an integer and validate it */

    MessageId = (UINT32) strtoul (MessageIdString, NULL, 0);

    if (MessageId > 6999)
    {
        printf ("\"%s\" is not a valid warning/remark/erro ID\n",
            MessageIdString);
        return (AE_BAD_PARAMETER);
    }

    /* Insert value into the global expected message array */

    if (AslGbl_ExpectedMessagesIndex >= ASL_MAX_EXPECTED_MESSAGES)
    {
        printf ("Too many messages have been registered as expected (max %d)\n",
            ASL_MAX_DISABLED_MESSAGES);
        return (AE_LIMIT);
    }

    AslGbl_ExpectedMessages[AslGbl_ExpectedMessagesIndex].MessageId = MessageId;
    AslGbl_ExpectedMessages[AslGbl_ExpectedMessagesIndex].MessageIdStr = MessageIdString;
    AslGbl_ExpectedMessages[AslGbl_ExpectedMessagesIndex].MessageReceived = FALSE;
    AslGbl_ExpectedMessagesIndex++;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AslDisableException
 *
 * PARAMETERS:  MessageIdString     - ID to be disabled
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter a message ID into the global disabled messages table
 *
 ******************************************************************************/

ACPI_STATUS
AslDisableException (
    char                    *MessageIdString)
{
    UINT32                  MessageId;


    /* Convert argument to an integer and validate it */

    MessageId = (UINT32) strtoul (MessageIdString, NULL, 0);

    if ((MessageId < 2000) || (MessageId > 6999))
    {
        printf ("\"%s\" is not a valid warning/remark/error ID\n",
            MessageIdString);
        return (AE_BAD_PARAMETER);
    }

    /* Insert value into the global disabled message array */

    if (AslGbl_DisabledMessagesIndex >= ASL_MAX_DISABLED_MESSAGES)
    {
        printf ("Too many messages have been disabled (max %d)\n",
            ASL_MAX_DISABLED_MESSAGES);
        return (AE_LIMIT);
    }

    AslGbl_DisabledMessages[AslGbl_DisabledMessagesIndex] = MessageId;
    AslGbl_DisabledMessagesIndex++;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AslElevateException
 *
 * PARAMETERS:  MessageIdString     - ID of excepted exception during compile
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter a message ID into the global elevated exceptions table.
 *              These messages will be considered as compilation errors.
 *
 ******************************************************************************/

ACPI_STATUS
AslElevateException (
    char                    *MessageIdString)
{
    UINT32                  MessageId;


    /* Convert argument to an integer and validate it */

    MessageId = (UINT32) strtoul (MessageIdString, NULL, 0);

    if (MessageId > 6999)
    {
        printf ("\"%s\" is not a valid warning/remark/erro ID\n",
            MessageIdString);
        return (AE_BAD_PARAMETER);
    }

    /* Insert value into the global expected message array */

    if (AslGbl_ElevatedMessagesIndex >= ASL_MAX_ELEVATED_MESSAGES)
    {
        printf ("Too many messages have been registered as elevated (max %d)\n",
            ASL_MAX_DISABLED_MESSAGES);
        return (AE_LIMIT);
    }

    AslGbl_ElevatedMessages[AslGbl_ExpectedMessagesIndex] = MessageId;
    AslGbl_ElevatedMessagesIndex++;
    return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    AslIsExceptionDisabled
 *
 * PARAMETERS:  Level           - Seriousness (Warning/error, etc.)
 *              MessageId       - Index into global message buffer
 *
 * RETURN:      TRUE if exception/message should be ignored
 *
 * DESCRIPTION: Check if the user has specified options such that this
 *              exception should be ignored
 *
 ******************************************************************************/

static BOOLEAN
AslIsExceptionExpected (
    UINT8                   Level,
    UINT16                  MessageId)
{
    UINT32                  EncodedMessageId;
    UINT32                  i;


    /* Mark this exception as received */

    EncodedMessageId = AeBuildFullExceptionCode (Level, MessageId);
    for (i = 0; i < AslGbl_ExpectedMessagesIndex; i++)
    {
        /* Simple implementation via fixed array */

        if (EncodedMessageId == AslGbl_ExpectedMessages[i].MessageId)
        {
            return (AslGbl_ExpectedMessages[i].MessageReceived = TRUE);
        }
    }

    return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    AslIsExceptionDisabled
 *
 * PARAMETERS:  Level               - Seriousness (Warning/error, etc.)
 *              MessageId           - Index into global message buffer
 *
 * RETURN:      TRUE if exception/message should be ignored
 *
 * DESCRIPTION: Check if the user has specified options such that this
 *              exception should be ignored
 *
 ******************************************************************************/

static BOOLEAN
AslIsExceptionDisabled (
    UINT8                   Level,
    UINT16                  MessageId)
{
    UINT32                  EncodedMessageId;
    UINT32                  i;


    switch (Level)
    {
    case ASL_WARNING2:
    case ASL_WARNING3:

        /* Check for global disable via -w1/-w2/-w3 options */

        if (Level > AslGbl_WarningLevel)
        {
            return (TRUE);
        }
        /* Fall through */

    case ASL_WARNING:
    case ASL_REMARK:
    case ASL_ERROR:
        /*
         * Ignore this error/warning/remark if it has been disabled by
         * the user (-vw option)
         */
        EncodedMessageId = AeBuildFullExceptionCode (Level, MessageId);
        for (i = 0; i < AslGbl_DisabledMessagesIndex; i++)
        {
            /* Simple implementation via fixed array */

            if (EncodedMessageId == AslGbl_DisabledMessages[i])
            {
                return (TRUE);
            }
        }
        break;

    default:
        break;
    }

    return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    AslDualParseOpError
 *
 * PARAMETERS:  Level           - Seriousness (Warning/error, etc.)
 *              MainMsgId       - Index into global message buffer
 *              MainOp          - Parse node where error happened
 *              MainMsg         - Message pertaining to the MainOp
 *              SubMsgId        - Index into global message buffer
 *              SubOp           - Additional parse node for better message
 *              SubMsg          - Message pertainint to SubOp
 *
 *
 * RETURN:      None
 *
 * DESCRIPTION: Main error reporting routine for the ASL compiler for error
 *              messages that point to multiple parse objects.
 *
 ******************************************************************************/

void
AslDualParseOpError (
    UINT8                   Level,
    UINT16                  MainMsgId,
    ACPI_PARSE_OBJECT       *MainOp,
    char                    *MainMsg,
    UINT16                  SubMsgId,
    ACPI_PARSE_OBJECT       *SubOp,
    char                    *SubMsg)
{
    ASL_ERROR_MSG           *SubEnode = NULL;


    /* Check if user wants to ignore this exception */

    if (AslIsExceptionIgnored (Level, MainMsgId) || !MainOp)
    {
        return;
    }

    if (SubOp)
    {
        AslInitEnode (&SubEnode, Level, SubMsgId, SubOp->Asl.LineNumber,
            SubOp->Asl.LogicalLineNumber, SubOp->Asl.LogicalByteOffset,
            SubOp->Asl.Column, SubOp->Asl.Filename, SubMsg,
            NULL, NULL);
    }

    AslLogNewError (Level, MainMsgId, MainOp->Asl.LineNumber,
        MainOp->Asl.LogicalLineNumber, MainOp->Asl.LogicalByteOffset,
        MainOp->Asl.Column, MainOp->Asl.Filename, MainMsg,
        NULL, SubEnode);
}


/*******************************************************************************
 *
 * FUNCTION:    AslError
 *
 * PARAMETERS:  Level               - Seriousness (Warning/error, etc.)
 *              MessageId           - Index into global message buffer
 *              Op                  - Parse node where error happened
 *              ExtraMessage        - additional error message
 *
 * RETURN:      None
 *
 * DESCRIPTION: Main error reporting routine for the ASL compiler (all code
 *              except the parser.)
 *
 ******************************************************************************/

void
AslError (
    UINT8                   Level,
    UINT16                  MessageId,
    ACPI_PARSE_OBJECT       *Op,
    char                    *ExtraMessage)
{
    if (Op)
    {
        AslCommonError (Level, MessageId, Op->Asl.LineNumber,
            Op->Asl.LogicalLineNumber,
            Op->Asl.LogicalByteOffset,
            Op->Asl.Column,
            Op->Asl.Filename, ExtraMessage);
    }
    else
    {
        AslCommonError (Level, MessageId, 0,
            0, 0, 0, NULL, ExtraMessage);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AslCoreSubsystemError
 *
 * PARAMETERS:  Op                  - Parse node where error happened
 *              Status              - The ACPICA Exception
 *              ExtraMessage        - additional error message
 *              Abort               - TRUE -> Abort compilation
 *
 * RETURN:      None
 *
 * DESCRIPTION: Error reporting routine for exceptions returned by the ACPICA
 *              core subsystem.
 *
 ******************************************************************************/

void
AslCoreSubsystemError (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_STATUS             Status,
    char                    *ExtraMessage,
    BOOLEAN                 Abort)
{

    sprintf (AslGbl_MsgBuffer, "%s %s", AcpiFormatException (Status), ExtraMessage);

    if (Op)
    {
        AslCommonError (ASL_ERROR, ASL_MSG_CORE_EXCEPTION,
            Op->Asl.LineNumber,
            Op->Asl.LogicalLineNumber,
            Op->Asl.LogicalByteOffset,
            Op->Asl.Column,
            Op->Asl.Filename, AslGbl_MsgBuffer);
    }
    else
    {
        AslCommonError (ASL_ERROR, ASL_MSG_CORE_EXCEPTION,
            0, 0, 0, 0, NULL, AslGbl_MsgBuffer);
    }

    if (Abort)
    {
        AslAbort ();
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AslCompilererror
 *
 * PARAMETERS:  CompilerMessage         - Error message from the parser
 *
 * RETURN:      Status (0 for now)
 *
 * DESCRIPTION: Report an error situation discovered in a production
 *              NOTE: don't change the name of this function, it is called
 *              from the auto-generated parser.
 *
 ******************************************************************************/

int
AslCompilererror (
    const char              *CompilerMessage)
{

    AslGbl_SyntaxError++;

    AslCommonError (ASL_ERROR, ASL_MSG_SYNTAX, AslGbl_CurrentLineNumber,
        AslGbl_LogicalLineNumber, AslGbl_CurrentLineOffset,
        AslGbl_CurrentColumn, AslGbl_Files[ASL_FILE_INPUT].Filename,
        ACPI_CAST_PTR (char, CompilerMessage));

    return (0);
}

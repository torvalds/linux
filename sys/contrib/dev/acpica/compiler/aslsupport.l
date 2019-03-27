/******************************************************************************
 *
 * Module Name: aslsupport.l - Flex/lex scanner C support routines.
 *              NOTE: Included into aslcompile.l, not compiled by itself.
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

/* Configuration */

#define ASL_SPACES_PER_TAB      4

#define ASL_NORMAL_CHAR         0
#define ASL_ESCAPE_SEQUENCE     1
#define ASL_OCTAL_CONSTANT      2
#define ASL_HEX_CONSTANT        3


void
yyerror (char const *s)
{

  AcpiOsPrintf ("YYERROR: %s\n", s);
}


/*******************************************************************************
 *
 * FUNCTION:    AslParserCleanup
 *
 * Used to delete the current buffer
 *
 ******************************************************************************/

void
AslParserCleanup (
    void)
{

    yy_delete_buffer (YY_CURRENT_BUFFER);
}


/*******************************************************************************
 *
 * FUNCTION:    AslDoLineDirective
 *
 * PARAMETERS:  None. Uses input() to access current source code line
 *
 * RETURN:      Updates global line number and filename
 *
 * DESCRIPTION: Handle #line directives emitted by the preprocessor.
 *
 * The #line directive is emitted by the preprocesser, and is used to
 * pass through line numbers from the original source code file to the
 * preprocessor output file (.i). This allows any compiler-generated
 * error messages to be displayed with the correct line number.
 *
 ******************************************************************************/

static void
AslDoLineDirective (
    void)
{
    int                     c;
    char                    *Token;
    UINT32                  LineNumber;
    char                    *Filename;
    UINT32                  i;

   AslGbl_HasIncludeFiles = TRUE;

    /* Eat the entire line that contains the #line directive */

    AslGbl_LineBufPtr = AslGbl_CurrentLineBuffer;

    while ((c = input()) != '\n' && c != EOF)
    {
        *AslGbl_LineBufPtr = c;
        AslGbl_LineBufPtr++;
    }
    *AslGbl_LineBufPtr = 0;

    /* First argument is the actual line number */

    Token = strtok (AslGbl_CurrentLineBuffer, " ");
    if (!Token)
    {
        goto ResetAndExit;
    }

    /* First argument is the line number */

    LineNumber = (UINT32) UtDoConstant (Token);

    /* Emit the appropriate number of newlines */

    AslGbl_CurrentColumn = 0;
    if (LineNumber > AslGbl_CurrentLineNumber)
    {
        for (i = 0; i < (LineNumber - AslGbl_CurrentLineNumber); i++)
        {
            FlWriteFile (ASL_FILE_SOURCE_OUTPUT, "\n", 1);
            AslGbl_CurrentColumn++;
        }
    }

    FlSetLineNumber (LineNumber);

    /* Second argument is the optional filename (in double quotes) */

    Token = strtok (NULL, " \"");
    if (Token)
    {
        Filename = UtLocalCacheCalloc (strlen (Token) + 1);
        strcpy (Filename, Token);
        FlSetFilename (Filename);
    }

    /* Third argument is not supported at this time */

ResetAndExit:

    /* Reset globals for a new line */

    AslGbl_CurrentLineOffset += AslGbl_CurrentColumn;
    AslGbl_CurrentColumn = 0;
    AslGbl_LineBufPtr = AslGbl_CurrentLineBuffer;
}


/*******************************************************************************
 *
 * FUNCTION:    AslPopInputFileStack
 *
 * PARAMETERS:  None
 *
 * RETURN:      0 if a node was popped, -1 otherwise
 *
 * DESCRIPTION: Pop the top of the input file stack and point the parser to
 *              the saved parse buffer contained in the fnode. Also, set the
 *              global line counters to the saved values. This function is
 *              called when an include file reaches EOF.
 *
 ******************************************************************************/

int
AslPopInputFileStack (
    void)
{
    ASL_FILE_NODE           *Fnode;


    AslGbl_PreviousIncludeFilename = AslGbl_Files[ASL_FILE_INPUT].Filename;
    Fnode = AslGbl_IncludeFileStack;
    DbgPrint (ASL_PARSE_OUTPUT,
        "\nPop InputFile Stack, Fnode %p\n", Fnode);

    DbgPrint (ASL_PARSE_OUTPUT,
        "Include: Closing \"%s\"\n\n", AslGbl_Files[ASL_FILE_INPUT].Filename);

    if (!Fnode)
    {
        return (-1);
    }

    /* Close the current include file */

    fclose (yyin);

    /* Update the top-of-stack */

    AslGbl_IncludeFileStack = Fnode->Next;

    /* Reset global line counter and filename */

    AslGbl_Files[ASL_FILE_INPUT].Filename = Fnode->Filename;
    AslGbl_CurrentLineNumber = Fnode->CurrentLineNumber;

    /* Point the parser to the popped file */

    yy_delete_buffer (YY_CURRENT_BUFFER);
    yy_switch_to_buffer (Fnode->State);

    /* All done with this node */

    ACPI_FREE (Fnode);
    return (0);
}


/*******************************************************************************
 *
 * FUNCTION:    AslPushInputFileStack
 *
 * PARAMETERS:  InputFile           - Open file pointer
 *              Filename            - Name of the file
 *
 * RETURN:      None
 *
 * DESCRIPTION: Push the InputFile onto the file stack, and point the parser
 *              to this file. Called when an include file is successfully
 *              opened.
 *
 ******************************************************************************/

void
AslPushInputFileStack (
    FILE                    *InputFile,
    char                    *Filename)
{
    ASL_FILE_NODE           *Fnode;
    YY_BUFFER_STATE         State;


    /* Save the current state in an Fnode */

    Fnode = UtLocalCalloc (sizeof (ASL_FILE_NODE));

    Fnode->File = yyin;
    Fnode->Next = AslGbl_IncludeFileStack;
    Fnode->State = YY_CURRENT_BUFFER;
    Fnode->Filename = AslGbl_Files[ASL_FILE_INPUT].Filename;
    Fnode->CurrentLineNumber = AslGbl_CurrentLineNumber;

    /* Push it on the stack */

    AslGbl_IncludeFileStack = Fnode;

    /* Point the parser to this file */

    State = yy_create_buffer (InputFile, YY_BUF_SIZE);
    yy_switch_to_buffer (State);

    DbgPrint (ASL_PARSE_OUTPUT,
        "\nPush InputFile Stack, returning %p\n\n", InputFile);

    /* Reset the global line count and filename */

    AslGbl_Files[ASL_FILE_INPUT].Filename =
        UtLocalCacheCalloc (strlen (Filename) + 1);

    strcpy (AslGbl_Files[ASL_FILE_INPUT].Filename, Filename);

    AslGbl_CurrentLineNumber = 1;
    yyin = InputFile;

    /* converter: reset the comment state to STANDARD_COMMENT */

    AslGbl_CommentState.CommentType = STANDARD_COMMENT;
}


/*******************************************************************************
 *
 * FUNCTION:    AslResetCurrentLineBuffer
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Reset the Line Buffer to zero, increment global line numbers.
 *
 ******************************************************************************/

void
AslResetCurrentLineBuffer (
    void)
{

    if (AslGbl_Files[ASL_FILE_SOURCE_OUTPUT].Handle)
    {
        FlWriteFile (ASL_FILE_SOURCE_OUTPUT, AslGbl_CurrentLineBuffer,
            AslGbl_LineBufPtr - AslGbl_CurrentLineBuffer);
    }

    AslGbl_CurrentLineOffset += AslGbl_CurrentColumn;
    AslGbl_CurrentColumn = 0;

    AslGbl_CurrentLineNumber++;
    AslGbl_LogicalLineNumber++;
    AslGbl_LineBufPtr = AslGbl_CurrentLineBuffer;
}


/*******************************************************************************
 *
 * FUNCTION:    AslInsertLineBuffer
 *
 * PARAMETERS:  SourceChar          - One char from the input ASL source file
 *
 * RETURN:      None
 *
 * DESCRIPTION: Put one character of the source file into the temp line buffer
 *
 ******************************************************************************/

void
AslInsertLineBuffer (
    int                     SourceChar)
{
    UINT32                  i;
    UINT32                  Count = 1;


    if (SourceChar == EOF)
    {
        return;
    }

    AslGbl_InputByteCount++;

    /* Handle tabs. Convert to spaces */

    if (SourceChar == '\t')
    {
        SourceChar = ' ';
        Count = ASL_SPACES_PER_TAB -
                    (AslGbl_CurrentColumn & (ASL_SPACES_PER_TAB-1));
    }

    for (i = 0; i < Count; i++)
    {
        AslGbl_CurrentColumn++;

        /* Insert the character into the line buffer */

        *AslGbl_LineBufPtr = (UINT8) SourceChar;
        AslGbl_LineBufPtr++;

        if (AslGbl_LineBufPtr >
            (AslGbl_CurrentLineBuffer + (AslGbl_LineBufferSize - 1)))
        {
#if 0
            /*
             * Warning if we have split a long source line.
             * <Probably overkill>
             */
            sprintf (MsgBuffer, "Max %u", AslGbl_LineBufferSize);
            AslCommonError (ASL_WARNING, ASL_MSG_LONG_LINE,
                AslGbl_CurrentLineNumber, AslGbl_LogicalLineNumber,
                AslGbl_CurrentLineOffset, AslGbl_CurrentColumn,
                AslGbl_Files[ASL_FILE_INPUT].Filename, MsgBuffer);
#endif

            AslResetCurrentLineBuffer ();
        }
        else if (SourceChar == '\n')
        {
            /* End of line */

            AslResetCurrentLineBuffer ();
        }

        if (AcpiGbl_CaptureComments)
        {
            CvProcessCommentState (SourceChar);
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    count
 *
 * PARAMETERS:  yytext              - Contains the matched keyword.
 *              Type                - Keyword/Character type:
 *                                      0 = anything except a keyword
 *                                      1 = pseudo-keywords
 *                                      2 = non-executable ASL keywords
 *                                      3 = executable ASL keywords
 *
 * RETURN:      None
 *
 * DESCRIPTION: Count keywords and put them into the line buffer
 *
 ******************************************************************************/

static void
count (
    int                 Type)
{
    char                *p;


    switch (Type)
    {
    case 2:

        AslGbl_TotalKeywords++;
        AslGbl_TotalNamedObjects++;
        break;

    case 3:

        AslGbl_TotalKeywords++;
        AslGbl_TotalExecutableOpcodes++;
        break;

    default:

        break;
    }

    for (p = yytext; *p != '\0'; p++)
    {
        AslInsertLineBuffer (*p);
        *AslGbl_LineBufPtr = 0;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AslDoComment
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Process a standard comment.
 *
 ******************************************************************************/

static BOOLEAN
AslDoComment (
    void)
{
    int                     c;
    int                     c1 = 0;
    char                    *StringBuffer = AslGbl_MsgBuffer;
    char                    *EndBuffer = AslGbl_MsgBuffer + ASL_MSG_BUFFER_SIZE;
    ASL_COMMENT_STATE       CurrentState = AslGbl_CommentState; /* to reference later on */


    AslInsertLineBuffer ('/');
    AslInsertLineBuffer ('*');
    if (AcpiGbl_CaptureComments && CurrentState.CaptureComments)
    {
        *StringBuffer = '/';
        ++StringBuffer;
        *StringBuffer = '*';
        ++StringBuffer;
    }

loop:

    /* Eat chars until end-of-comment */

    while (((c = input ()) != '*') && (c != EOF))
    {
        AslInsertLineBuffer (c);
        if (AcpiGbl_CaptureComments && CurrentState.CaptureComments)
        {
            *StringBuffer = c;
            ++StringBuffer;
        }
        c1 = c;
    }

    if (c == EOF)
    {
        goto EarlyEOF;
    }

    /*
     * Check for nested comment -- can help catch cases where a previous
     * comment was accidentally left unterminated
     */
    if ((c1 == '/') && (c == '*'))
    {
        AslCommonError (ASL_WARNING, ASL_MSG_NESTED_COMMENT,
            AslGbl_CurrentLineNumber, AslGbl_LogicalLineNumber,
            AslGbl_InputByteCount, AslGbl_CurrentColumn,
            AslGbl_Files[ASL_FILE_INPUT].Filename, NULL);
    }

    /* Comment is closed only if the NEXT character is a slash */

    AslInsertLineBuffer (c);
    if (AcpiGbl_CaptureComments && CurrentState.CaptureComments)
    {
        *StringBuffer = c;
        ++StringBuffer;
    }

    if (((c1 = input ()) != '/') && (c1 != EOF))
    {
        unput (c1);
        goto loop;
    }

    if (c1 == EOF)
    {
        goto EarlyEOF;
    }
    if (StringBuffer > EndBuffer)
    {
        goto BufferOverflow;
    }

    AslInsertLineBuffer (c1);
    CvProcessComment (CurrentState, StringBuffer, c1);
    return (TRUE);


EarlyEOF:
    /*
     * Premature End-Of-File
     */
    AslCommonError (ASL_ERROR, ASL_MSG_EARLY_EOF,
        AslGbl_CurrentLineNumber, AslGbl_LogicalLineNumber,
        AslGbl_CurrentLineOffset, AslGbl_CurrentColumn,
        AslGbl_Files[ASL_FILE_INPUT].Filename, NULL);
    return (FALSE);


BufferOverflow:

    /* Comment was too long */

    AslCommonError (ASL_ERROR, ASL_MSG_STRING_LENGTH,
        AslGbl_CurrentLineNumber, AslGbl_LogicalLineNumber,
        AslGbl_CurrentLineOffset, AslGbl_CurrentColumn,
        AslGbl_Files[ASL_FILE_INPUT].Filename, "Max length 4096");
    return (FALSE);

}


/*******************************************************************************
 *
 * FUNCTION:    AslDoCommentType2
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Process a new "//" comment. Inline comments will be converted
 *              to "/ *" standard comments.
 *
 ******************************************************************************/

static BOOLEAN
AslDoCommentType2 (
    void)
{
    int                     c;
    char                    *StringBuffer = AslGbl_MsgBuffer;
    char                    *EndBuffer = AslGbl_MsgBuffer + ASL_MSG_BUFFER_SIZE;
    ASL_COMMENT_STATE       CurrentState = AslGbl_CommentState;


    AslInsertLineBuffer ('/');

    if (AcpiGbl_CaptureComments && CurrentState.CaptureComments)
    {
        AslInsertLineBuffer ('*');
        *StringBuffer = '/';
        ++StringBuffer;
        *StringBuffer = '*';
        ++StringBuffer;
    }
    else
    {
        AslInsertLineBuffer ('/');
    }

    while (((c = input ()) != '\n') && (c != EOF))
    {
        AslInsertLineBuffer (c);
        if (AcpiGbl_CaptureComments && CurrentState.CaptureComments)
        {
            *StringBuffer = c;
            ++StringBuffer;
        }
    }

    if (c == EOF)
    {
        /* End of file is OK, change to newline. Let parser detect EOF later */

        c = '\n';
    }

    if (StringBuffer > EndBuffer)
    {
        goto BufferOverflow;
    }
    AslInsertLineBuffer (c);

    CvProcessCommentType2 (CurrentState, StringBuffer);
    return (TRUE);


BufferOverflow:

    /* Comment was too long */

    AslCommonError (ASL_ERROR, ASL_MSG_STRING_LENGTH,
        AslGbl_CurrentLineNumber, AslGbl_LogicalLineNumber,
        AslGbl_CurrentLineOffset, AslGbl_CurrentColumn,
        AslGbl_Files[ASL_FILE_INPUT].Filename, "Max length 4096");
    return (FALSE);

}


/*******************************************************************************
 *
 * FUNCTION:    AslDoStringLiteral
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Process a string literal (surrounded by quotes)
 *
 ******************************************************************************/

static char
AslDoStringLiteral (
    void)
{
    char                *StringBuffer = AslGbl_MsgBuffer;
    char                *EndBuffer = AslGbl_MsgBuffer + ASL_MSG_BUFFER_SIZE;
    char                *CleanString;
    int                 StringChar;
    UINT32              State = ASL_NORMAL_CHAR;
    UINT32              i = 0;
    UINT8               Digit;
    char                ConvertBuffer[4];


    /*
     * Eat chars until end-of-literal.
     * NOTE:  Put back the original surrounding quotes into the
     * source line buffer.
     */
    AslInsertLineBuffer ('\"');
    while ((StringChar = input()) != EOF)
    {
        AslInsertLineBuffer (StringChar);

DoCharacter:
        switch (State)
        {
        case ASL_NORMAL_CHAR:

            switch (StringChar)
            {
            case '\\':
                /*
                 * Special handling for backslash-escape sequence. We will
                 * toss the backslash and translate the escape char(s).
                 */
                State = ASL_ESCAPE_SEQUENCE;
                continue;

            case '\"':

                /* String terminator */

                goto CompletedString;

            default:

                break;
            }
            break;


        case ASL_ESCAPE_SEQUENCE:

            State = ASL_NORMAL_CHAR;
            switch (StringChar)
            {
            case 'a':

                StringChar = 0x07;      /* BELL */
                break;

            case 'b':

                StringChar = 0x08;      /* BACKSPACE */
                break;

            case 'f':

                StringChar = 0x0C;      /* FORMFEED */
                break;

            case 'n':

                StringChar = 0x0A;      /* LINEFEED */
                break;

            case 'r':

                StringChar = 0x0D;      /* CARRIAGE RETURN*/
                break;

            case 't':

                StringChar = 0x09;      /* HORIZONTAL TAB */
                break;

            case 'v':

                StringChar = 0x0B;      /* VERTICAL TAB */
                break;

            case 'x':

                State = ASL_HEX_CONSTANT;
                i = 0;
                continue;

            case '\'':                  /* Single Quote */
            case '\"':                  /* Double Quote */
            case '\\':                  /* Backslash */

                break;

            default:

                /* Check for an octal digit (0-7) */

                if (ACPI_IS_OCTAL_DIGIT (StringChar))
                {
                    State = ASL_OCTAL_CONSTANT;
                    ConvertBuffer[0] = StringChar;
                    i = 1;
                    continue;
                }

                /* Unknown escape sequence issue warning, but use the character */

                AslCommonError (ASL_WARNING, ASL_MSG_INVALID_ESCAPE,
                    AslGbl_CurrentLineNumber, AslGbl_LogicalLineNumber,
                    AslGbl_CurrentLineOffset, AslGbl_CurrentColumn,
                    AslGbl_Files[ASL_FILE_INPUT].Filename, NULL);
                break;
            }
            break;


        case ASL_OCTAL_CONSTANT:

            /* Up to three octal digits allowed */

            if (!ACPI_IS_OCTAL_DIGIT (StringChar) ||
                (i > 2))
            {
                /*
                 * Reached end of the constant. Convert the assembled ASCII
                 * string and resume processing of the next character
                 */
                ConvertBuffer[i] = 0;
                Digit = (UINT8) strtoul (ConvertBuffer, NULL, 8);

                /* Check for NULL or non-ascii character (ignore if so) */

                if ((Digit == 0) || (Digit > ACPI_ASCII_MAX))
                {
                    AslCommonError (ASL_WARNING, ASL_MSG_INVALID_STRING,
                        AslGbl_CurrentLineNumber, AslGbl_LogicalLineNumber,
                        AslGbl_CurrentLineOffset, AslGbl_CurrentColumn,
                        AslGbl_Files[ASL_FILE_INPUT].Filename, NULL);
                }
                else
                {
                    *StringBuffer = (char) Digit;
                    StringBuffer++;
                    if (StringBuffer >= EndBuffer)
                    {
                        goto BufferOverflow;
                    }
                }

                State = ASL_NORMAL_CHAR;
                goto DoCharacter;
                break;
            }

            /* Append another digit of the constant */

            ConvertBuffer[i] = StringChar;
            i++;
            continue;

        case ASL_HEX_CONSTANT:

            /* Up to two hex digits allowed */

            if (!isxdigit (StringChar) ||
                (i > 1))
            {
                /*
                 * Reached end of the constant. Convert the assembled ASCII
                 * string and resume processing of the next character
                 */
                ConvertBuffer[i] = 0;
                Digit = (UINT8) strtoul (ConvertBuffer, NULL, 16);

                /* Check for NULL or non-ascii character (ignore if so) */

                if ((Digit == 0) || (Digit > ACPI_ASCII_MAX))
                {
                    AslCommonError (ASL_WARNING, ASL_MSG_INVALID_STRING,
                        AslGbl_CurrentLineNumber, AslGbl_LogicalLineNumber,
                        AslGbl_CurrentLineOffset, AslGbl_CurrentColumn,
                        AslGbl_Files[ASL_FILE_INPUT].Filename, NULL);
                }
                else
                {
                    *StringBuffer = (char) Digit;
                    StringBuffer++;
                    if (StringBuffer >= EndBuffer)
                    {
                        goto BufferOverflow;
                    }
                }

                State = ASL_NORMAL_CHAR;
                goto DoCharacter;
                break;
            }

            /* Append another digit of the constant */

            ConvertBuffer[i] = StringChar;
            i++;
            continue;

        default:

            break;
        }

        /* Save the finished character */

        *StringBuffer = StringChar;
        StringBuffer++;
        if (StringBuffer >= EndBuffer)
        {
            goto BufferOverflow;
        }
    }

    /*
     * Premature End-Of-File
     */
    AslCommonError (ASL_ERROR, ASL_MSG_EARLY_EOF,
        AslGbl_CurrentLineNumber, AslGbl_LogicalLineNumber,
        AslGbl_CurrentLineOffset, AslGbl_CurrentColumn,
        AslGbl_Files[ASL_FILE_INPUT].Filename, NULL);
    return (FALSE);


CompletedString:
    /*
     * Null terminate the input string and copy string to a new buffer
     */
    *StringBuffer = 0;

    CleanString = UtLocalCacheCalloc (strlen (AslGbl_MsgBuffer) + 1);
    strcpy (CleanString, AslGbl_MsgBuffer);
    AslCompilerlval.s = CleanString;
    return (TRUE);


BufferOverflow:

    /* Literal was too long */

    AslCommonError (ASL_ERROR, ASL_MSG_STRING_LENGTH,
        AslGbl_CurrentLineNumber, AslGbl_LogicalLineNumber,
        AslGbl_CurrentLineOffset, AslGbl_CurrentColumn,
        AslGbl_Files[ASL_FILE_INPUT].Filename, "Max length 4096");
    return (FALSE);
}

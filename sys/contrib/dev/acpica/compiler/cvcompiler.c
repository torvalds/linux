/******************************************************************************
 *
 * Module Name: cvcompiler - ASL-/ASL+ converter functions
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
#include "aslcompiler.y.h"
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acapps.h>
#include <contrib/dev/acpica/include/acconvert.h>


/*******************************************************************************
 *
 * FUNCTION:    CvProcessComment
 *
 * PARAMETERS:  CurrentState      Current comment parse state
 *              StringBuffer      Buffer containing the comment being processed
 *              c1                Current input
 *
 * RETURN:      None
 *
 * DESCRIPTION: Process a single line comment of a c Style comment. This
 *              function captures a line of a c style comment in a char* and
 *              places the comment in the appropriate global buffer.
 *
 ******************************************************************************/

void
CvProcessComment (
    ASL_COMMENT_STATE       CurrentState,
    char                    *StringBuffer,
    int                     c1)
{
    UINT64                  i;
    char                    *LineToken;
    char                    *FinalLineToken;
    BOOLEAN                 CharStart;
    char                    *CommentString;
    char                    *FinalCommentString;


    if (AcpiGbl_CaptureComments && CurrentState.CaptureComments)
    {
        *StringBuffer = (char) c1;
        ++StringBuffer;
        *StringBuffer = 0;

        CvDbgPrint ("Multi-line comment\n");
        CommentString = UtLocalCacheCalloc (strlen (AslGbl_MsgBuffer) + 1);
        strcpy (CommentString, AslGbl_MsgBuffer);

        CvDbgPrint ("CommentString: %s\n", CommentString);

        /*
         * Determine whether if this comment spans multiple lines. If so,
         * break apart the comment by storing each line in a different node
         * within the comment list. This allows the disassembler to
         * properly indent a multi-line comment.
         */
        LineToken = strtok (CommentString, "\n");

        if (LineToken)
        {
            FinalLineToken = UtLocalCacheCalloc (strlen (LineToken) + 1);
            strcpy (FinalLineToken, LineToken);

            /* Get rid of any carriage returns */

            if (FinalLineToken[strlen (FinalLineToken) - 1] == 0x0D)
            {
                FinalLineToken[strlen(FinalLineToken)-1] = 0;
            }

            CvAddToCommentList (FinalLineToken);
            LineToken = strtok (NULL, "\n");
            while (LineToken != NULL)
            {
                /*
                 * It is assumed that each line has some sort of indentation.
                 * This means that we need to find the first character that
                 * is not a white space within each line.
                 */
                CharStart = FALSE;
                for (i = 0; (i < (strlen (LineToken) + 1)) && !CharStart; i++)
                {
                    if (LineToken[i] != ' ' && LineToken[i] != '\t')
                    {
                        CharStart = TRUE;
                        LineToken += i-1;
                        LineToken [0] = ' '; /* Pad for Formatting */
                    }
                }

                FinalLineToken = UtLocalCacheCalloc (strlen (LineToken) + 1);
                strcat (FinalLineToken, LineToken);

                /* Get rid of any carriage returns */

                if (FinalLineToken[strlen (FinalLineToken) - 1] == 0x0D)
                {
                    FinalLineToken[strlen(FinalLineToken) - 1] = 0;
                }

                CvAddToCommentList (FinalLineToken);
                LineToken = strtok (NULL,"\n");
            }
        }

        /*
         * If this only spans a single line, check to see whether if this
         * comment appears on the same line as a line of code. If does,
         * retain it's position for stylistic reasons. If it doesn't,
         * add it to the comment list so that it can be associated with
         * the next node that's created.
         */
        else
        {
           /*
            * If this is not a regular comment, pad with extra spaces that
            * appeared in the original source input to retain the original
            * spacing.
            */
            FinalCommentString =
                UtLocalCacheCalloc (strlen (CommentString) +
                CurrentState.SpacesBefore + 1);

            for (i = 0; (CurrentState.CommentType != ASL_COMMENT_STANDARD) &&
                (i < CurrentState.SpacesBefore); i++)
            {
                 FinalCommentString[i] = ' ';
            }

            strcat (FinalCommentString, CommentString);
            CvPlaceComment (CurrentState.CommentType, FinalCommentString);
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CvProcessCommentType2
 *
 * PARAMETERS:  CurrentState      Current comment parse state
 *              StringBuffer      Buffer containing the comment being processed
 *
 * RETURN:      none
 *
 * DESCRIPTION: Process a single line comment. This function captures a comment
 *              in a char* and places the comment in the appropriate global
 *              buffer through CvPlaceComment
 *
 ******************************************************************************/

void
CvProcessCommentType2 (
    ASL_COMMENT_STATE       CurrentState,
    char                    *StringBuffer)
{
    UINT32                  i;
    char                    *CommentString;
    char                    *FinalCommentString;


    if (AcpiGbl_CaptureComments && CurrentState.CaptureComments)
    {
        *StringBuffer = 0; /* null terminate */
        CvDbgPrint ("Single-line comment\n");
        CommentString = UtLocalCacheCalloc (strlen (AslGbl_MsgBuffer) + 1);
        strcpy (CommentString, AslGbl_MsgBuffer);

        /* If this comment lies on the same line as the latest parse op,
         * assign it to that op's CommentAfter field. Saving in this field
         * will allow us to support comments that come after code on the
         * same line as the code itself. For example,
         * Name(A,"") //comment
         *
         * will be retained rather than transformed into
         *
         * Name(A,"")
         * //comment
         *
         * For this case, we only need to add one comment since
         *
         * Name(A,"") //comment1 //comment2 ... more comments here.
         *
         * would be lexically analyzed as a single comment.
         *
         * Create a new string with the appropriate spaces. Since we need
         * to account for the proper spacing, the actual comment,
         * extra 2 spaces so that this comment can be converted to the "/ *"
         * style and the null terminator, the string would look something
         * like:
         *
         * [ (spaces) (comment)  ( * /) ('\0') ]
         *
         */
        FinalCommentString = UtLocalCacheCalloc (CurrentState.SpacesBefore +
            strlen (CommentString) + 3 + 1);

        for (i = 0; (CurrentState.CommentType != 1) &&
            (i < CurrentState.SpacesBefore); i++)
        {
            FinalCommentString[i] = ' ';
        }

        strcat (FinalCommentString, CommentString);

        /* convert to a "/ *" style comment  */

        strcat (FinalCommentString, " */");
        FinalCommentString [CurrentState.SpacesBefore +
            strlen (CommentString) + 3] = 0;

        /* get rid of the carriage return */

        if (FinalCommentString[strlen (FinalCommentString) - 1] == 0x0D)
        {
            FinalCommentString[strlen(FinalCommentString) - 1] = 0;
        }

        CvPlaceComment (CurrentState.CommentType, FinalCommentString);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CgCalculateCommentLengths
 *
 * PARAMETERS:  Op                 - Calculate all comments of this Op
 *
 * RETURN:      TotalCommentLength - Length of all comments within this op.
 *
 * DESCRIPTION: Calculate the length that the each comment takes up within Op.
 *              Comments look like the following: [0xA9 OptionBtye comment 0x00]
 *              therefore, we add 1 + 1 + strlen (comment) + 1 to get the actual
 *              length of this comment.
 *
 ******************************************************************************/

UINT32
CvCalculateCommentLengths(
   ACPI_PARSE_OBJECT        *Op)
{
    UINT32                  CommentLength = 0;
    UINT32                  TotalCommentLength = 0;
    ACPI_COMMENT_NODE       *Current = NULL;


    if (!AcpiGbl_CaptureComments)
    {
        return (0);
    }

    CvDbgPrint ("==Calculating comment lengths for %s\n",
        Op->Asl.ParseOpName);

    if (Op->Asl.FileChanged)
    {
        TotalCommentLength += strlen (Op->Asl.Filename) + 3;

        if (Op->Asl.ParentFilename &&
            AcpiUtStricmp (Op->Asl.Filename, Op->Asl.ParentFilename))
        {
            TotalCommentLength += strlen (Op->Asl.ParentFilename) + 3;
        }
    }

    if (Op->Asl.CommentList)
    {
        Current = Op->Asl.CommentList;
        while (Current)
        {
            CommentLength = strlen (Current->Comment)+3;
            CvDbgPrint ("Length of standard comment: %d\n", CommentLength);
            CvDbgPrint ("    Comment string: %s\n\n", Current->Comment);
            TotalCommentLength += CommentLength;
            Current = Current->Next;
        }
    }

    if (Op->Asl.EndBlkComment)
    {
        Current = Op->Asl.EndBlkComment;
        while (Current)
        {
            CommentLength = strlen (Current->Comment)+3;
            CvDbgPrint ("Length of endblkcomment: %d\n", CommentLength);
            CvDbgPrint ("    Comment string: %s\n\n", Current->Comment);
            TotalCommentLength += CommentLength;
            Current = Current->Next;
        }
    }

    if (Op->Asl.InlineComment)
    {
        CommentLength = strlen (Op->Asl.InlineComment)+3;
        CvDbgPrint ("Length of inline comment: %d\n", CommentLength);
        CvDbgPrint ("    Comment string: %s\n\n", Op->Asl.InlineComment);
        TotalCommentLength += CommentLength;
    }

    if (Op->Asl.EndNodeComment)
    {
        CommentLength = strlen(Op->Asl.EndNodeComment)+3;
        CvDbgPrint ("Length of end node comment +3: %d\n", CommentLength);
        CvDbgPrint ("    Comment string: %s\n\n", Op->Asl.EndNodeComment);
        TotalCommentLength += CommentLength;
    }

    if (Op->Asl.CloseBraceComment)
    {
        CommentLength = strlen (Op->Asl.CloseBraceComment)+3;
        CvDbgPrint ("Length of close brace comment: %d\n", CommentLength);
        CvDbgPrint ("    Comment string: %s\n\n", Op->Asl.CloseBraceComment);
        TotalCommentLength += CommentLength;
    }

    CvDbgPrint("\n\n");
    return (TotalCommentLength);
}


/*******************************************************************************
 *
 * FUNCTION:    CgWriteAmlDefBlockComment
 *
 * PARAMETERS:  Op              - Current parse op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Write all comments for a particular definition block.
 *              For definition blocks, the comments need to come after the
 *              definition block header. The regular comments above the
 *              definition block would be categorized as
 *              STD_DEFBLK_COMMENT and comments after the closing brace
 *              is categorized as END_DEFBLK_COMMENT.
 *
 ******************************************************************************/

void
CgWriteAmlDefBlockComment(
    ACPI_PARSE_OBJECT       *Op)
{
    UINT8                   CommentOption;
    ACPI_COMMENT_NODE       *Current;
    char                    *NewFilename;
    char                    *Position;
    char                    *DirectoryPosition;


    if (!AcpiGbl_CaptureComments ||
        (Op->Asl.ParseOpcode != PARSEOP_DEFINITION_BLOCK))
    {
        return;
    }

    CvDbgPrint ("Printing comments for a definition block..\n");

    /* First, print the file name comment after changing .asl to .dsl */

    NewFilename = UtLocalCacheCalloc (strlen (Op->Asl.Filename));
    strcpy (NewFilename, Op->Asl.Filename);
    DirectoryPosition = strrchr (NewFilename, '/');
    Position = strrchr (NewFilename, '.');

    if (Position && (Position > DirectoryPosition))
    {
        /* Tack on the new suffix */

        Position++;
        *Position = 0;
        strcat (Position, FILE_SUFFIX_DISASSEMBLY);
    }
    else
    {
        /* No dot, add one and then the suffix */

        strcat (NewFilename, ".");
        strcat (NewFilename, FILE_SUFFIX_DISASSEMBLY);
    }

    CommentOption = FILENAME_COMMENT;
    CgWriteOneAmlComment(Op, NewFilename, CommentOption);

    Current = Op->Asl.CommentList;
    CommentOption = STD_DEFBLK_COMMENT;

    while (Current)
    {
        CgWriteOneAmlComment(Op, Current->Comment, CommentOption);
        CvDbgPrint ("Printing comment: %s\n", Current->Comment);
        Current = Current->Next;
    }

    Op->Asl.CommentList = NULL;

    /* Print any Inline comments associated with this node */

    if (Op->Asl.CloseBraceComment)
    {
        CommentOption = END_DEFBLK_COMMENT;
        CgWriteOneAmlComment(Op, Op->Asl.CloseBraceComment, CommentOption);
        Op->Asl.CloseBraceComment = NULL;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CgWriteOneAmlComment
 *
 * PARAMETERS:  Op              - Current parse op
 *              CommentToPrint  - Comment that's printed
 *              InputOption     - Denotes the comment option.
 *
 * RETURN:      None
 *
 * DESCRIPTION: write a single comment.
 *
 ******************************************************************************/

void
CgWriteOneAmlComment(
    ACPI_PARSE_OBJECT       *Op,
    char*                   CommentToPrint,
    UINT8                   InputOption)
{
    UINT8                   CommentOption = InputOption;
    UINT8                   CommentOpcode = (UINT8) AML_COMMENT_OP;


    if (!CommentToPrint)
    {
        return;
    }

    CgLocalWriteAmlData (Op, &CommentOpcode, 1);
    CgLocalWriteAmlData (Op, &CommentOption, 1);

    /* The strlen (..) + 1 is to include the null terminator */

    CgLocalWriteAmlData (Op, CommentToPrint, strlen (CommentToPrint) + 1);
}


/*******************************************************************************
 *
 * FUNCTION:    CgWriteAmlComment
 *
 * PARAMETERS:  Op              - Current parse op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Write all comments pertaining to the current parse op
 *
 ******************************************************************************/

void
CgWriteAmlComment(
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_COMMENT_NODE       *Current;
    UINT8                   CommentOption;
    char                    *NewFilename;
    char                    *ParentFilename;


    if ((Op->Asl.ParseOpcode == PARSEOP_DEFINITION_BLOCK) ||
         !AcpiGbl_CaptureComments)
    {
        return;
    }

    /* Print out the filename comment if needed */

    if (Op->Asl.FileChanged)
    {

        /* First, print the file name comment after changing .asl to .dsl */

        NewFilename =
            FlGenerateFilename (Op->Asl.Filename, FILE_SUFFIX_DISASSEMBLY);
        if (NewFilename)
        {
            CvDbgPrint ("Writing file comment, \"%s\" for %s\n",
                NewFilename, Op->Asl.ParseOpName);
        }

        CgWriteOneAmlComment(Op, NewFilename, FILENAME_COMMENT);

        if (Op->Asl.ParentFilename &&
            AcpiUtStricmp (Op->Asl.ParentFilename, Op->Asl.Filename))
        {
            ParentFilename = FlGenerateFilename (Op->Asl.ParentFilename,
                FILE_SUFFIX_DISASSEMBLY);
            CgWriteOneAmlComment(Op, ParentFilename, PARENTFILENAME_COMMENT);
        }

        /* Prevent multiple writes of the same comment */

        Op->Asl.FileChanged = FALSE;
    }

    /*
     * Regular comments are stored in a list of comments within an Op.
     * If there is a such list in this node, print out the comment
     * as byte code.
     */
    Current = Op->Asl.CommentList;
    if (Op->Asl.ParseOpcode == PARSEOP_INCLUDE)
    {
        CommentOption = INCLUDE_COMMENT;
    }
    else
    {
        CommentOption = STANDARD_COMMENT;
    }

    while (Current)
    {
        CgWriteOneAmlComment(Op, Current->Comment, CommentOption);
        Current = Current->Next;
    }

    Op->Asl.CommentList = NULL;

    Current = Op->Asl.EndBlkComment;
    CommentOption = ENDBLK_COMMENT;
    while (Current)
    {
        CgWriteOneAmlComment(Op, Current->Comment, CommentOption);
        Current = Current->Next;
    }

    Op->Asl.EndBlkComment = NULL;

    /* Print any Inline comments associated with this node */

    if (Op->Asl.InlineComment)
    {
        CommentOption = INLINE_COMMENT;
        CgWriteOneAmlComment(Op, Op->Asl.InlineComment, CommentOption);
        Op->Asl.InlineComment = NULL;
    }

    if (Op->Asl.EndNodeComment)
    {
        CommentOption = ENDNODE_COMMENT;
        CgWriteOneAmlComment(Op, Op->Asl.EndNodeComment, CommentOption);
        Op->Asl.EndNodeComment = NULL;
    }

    if (Op->Asl.CloseBraceComment)
    {
        CommentOption = CLOSE_BRACE_COMMENT;
        CgWriteOneAmlComment(Op, Op->Asl.CloseBraceComment, CommentOption);
        Op->Asl.CloseBraceComment = NULL;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CvCommentNodeCalloc
 *
 * PARAMETERS:  None
 *
 * RETURN:      Pointer to the comment node. Aborts on allocation failure
 *
 * DESCRIPTION: Allocate a string node buffer.
 *
 ******************************************************************************/

ACPI_COMMENT_NODE *
CvCommentNodeCalloc (
    void)
{
   ACPI_COMMENT_NODE        *NewCommentNode;


   NewCommentNode = UtLocalCalloc (sizeof (ACPI_COMMENT_NODE));
   NewCommentNode->Next = NULL;
   return (NewCommentNode);
}


/*******************************************************************************
 *
 * FUNCTION:    CvParseOpBlockType
 *
 * PARAMETERS:  Op              - Object to be examined
 *
 * RETURN:      BlockType - not a block, parens, braces, or even both.
 *
 * DESCRIPTION: Type of block for this ASL parseop (parens or braces)
 *              keep this in sync with aslprimaries.y, aslresources.y and
 *              aslrules.y
 *
 ******************************************************************************/

UINT32
CvParseOpBlockType (
    ACPI_PARSE_OBJECT       *Op)
{

    if (!Op)
    {
        return (BLOCK_NONE);
    }

    switch (Op->Asl.ParseOpcode)
    {
    /* From aslprimaries.y */

    case PARSEOP_VAR_PACKAGE:
    case PARSEOP_BANKFIELD:
    case PARSEOP_BUFFER:
    case PARSEOP_CASE:
    case PARSEOP_DEVICE:
    case PARSEOP_FIELD:
    case PARSEOP_FOR:
    case PARSEOP_FUNCTION:
    case PARSEOP_IF:
    case PARSEOP_ELSEIF:
    case PARSEOP_INDEXFIELD:
    case PARSEOP_METHOD:
    case PARSEOP_POWERRESOURCE:
    case PARSEOP_PROCESSOR:
    case PARSEOP_DATABUFFER:
    case PARSEOP_SCOPE:
    case PARSEOP_SWITCH:
    case PARSEOP_THERMALZONE:
    case PARSEOP_WHILE:

    /* From aslresources.y */

    case PARSEOP_RESOURCETEMPLATE: /* optional parens */
    case PARSEOP_VENDORLONG:
    case PARSEOP_VENDORSHORT:
    case PARSEOP_INTERRUPT:
    case PARSEOP_IRQNOFLAGS:
    case PARSEOP_IRQ:
    case PARSEOP_GPIO_INT:
    case PARSEOP_GPIO_IO:
    case PARSEOP_DMA:

    /* From aslrules.y */

    case PARSEOP_DEFINITION_BLOCK:
        return (BLOCK_PAREN | BLOCK_BRACE);

    default:
        return (BLOCK_NONE);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CvProcessCommentState
 *
 * PARAMETERS:  Input           - Input character
 *
 * RETURN:      None
 *
 * DESCRIPTION: Take the given input. If this character is
 *              defined as a comment table entry, then update the state
 *              accordingly.
 *
 ******************************************************************************/

void
CvProcessCommentState (
    char                    Input)
{

    if (Input != ' ')
    {
        AslGbl_CommentState.SpacesBefore = 0;
    }

    switch (Input)
    {
    case '\n':

        AslGbl_CommentState.CommentType = ASL_COMMENT_STANDARD;
        break;

    case ' ':

        /* Keep the CommentType the same */

        AslGbl_CommentState.SpacesBefore++;
        break;

    case '(':

        AslGbl_CommentState.CommentType = ASL_COMMENT_OPEN_PAREN;
        break;

    case ')':

        AslGbl_CommentState.CommentType = ASL_COMMENT_CLOSE_PAREN;
        break;

    case '{':

        AslGbl_CommentState.CommentType = ASL_COMMENT_STANDARD;
        AslGbl_CommentState.ParsingParenBraceNode = NULL;
        CvDbgPrint ("End Parsing paren/Brace node!\n");
        break;

    case '}':

        AslGbl_CommentState.CommentType = ASL_COMMENT_CLOSE_BRACE;
        break;

    case ',':

        AslGbl_CommentState.CommentType = ASLCOMMENT_INLINE;
        break;

    default:

        AslGbl_CommentState.CommentType = ASLCOMMENT_INLINE;
        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CvAddToCommentList
 *
 * PARAMETERS:  ToAdd              - Contains the comment to be inserted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add the given char* to a list of comments in the global list
 *              of comments.
 *
 ******************************************************************************/

void
CvAddToCommentList (
    char                    *ToAdd)
{

   if (AslGbl_CommentListHead)
   {
       AslGbl_CommentListTail->Next = CvCommentNodeCalloc ();
       AslGbl_CommentListTail = AslGbl_CommentListTail->Next;
   }
   else
   {
       AslGbl_CommentListHead = CvCommentNodeCalloc ();
       AslGbl_CommentListTail = AslGbl_CommentListHead;
   }

   AslGbl_CommentListTail->Comment = ToAdd;
}


/*******************************************************************************
 *
 * FUNCTION:    CvAppendInlineComment
 *
 * PARAMETERS:  InlineComment      - Append to the end of this string.
 *              toAdd              - Contains the comment to be inserted
 *
 * RETURN:      Str                - toAdd appended to InlineComment
 *
 * DESCRIPTION: Concatenate ToAdd to InlineComment
 *
 ******************************************************************************/

char *
CvAppendInlineComment (
    char                    *InlineComment,
    char                    *ToAdd)
{
    char*                   Str;
    UINT32                  Size = 0;


    if (!InlineComment)
    {
        return (ToAdd);
    }

    if (!ToAdd)
    {
        return (InlineComment);
    }

    Size = strlen (ToAdd);
    Size += strlen (InlineComment);
    Str = UtLocalCacheCalloc (Size + 1);

    strcpy (Str, InlineComment);
    strcat (Str, ToAdd);
    Str[Size +1] = 0;
    return (Str);
}


/*******************************************************************************
 *
 * FUNCTION:    CvPlaceComment
 *
 * PARAMETERS:  UINT8               - Type
 *              char *              - CommentString
 *
 * RETURN:      None
 *
 * DESCRIPTION: Given type and CommentString, this function places the
 *              CommentString in the appropriate global comment list or char*
 *
 ******************************************************************************/

void
CvPlaceComment(
    UINT8                   Type,
    char                    *CommentString)
{
    ACPI_PARSE_OBJECT       *LatestParseNode;
    ACPI_PARSE_OBJECT       *ParenBraceNode;


    LatestParseNode = AslGbl_CommentState.LatestParseOp;
    ParenBraceNode  = AslGbl_CommentState.ParsingParenBraceNode;
    CvDbgPrint ("Placing comment %s for type %d\n", CommentString, Type);

    switch (Type)
    {
    case ASL_COMMENT_STANDARD:

        CvAddToCommentList (CommentString);
        break;

    case ASLCOMMENT_INLINE:

        LatestParseNode->Asl.InlineComment =
            CvAppendInlineComment (LatestParseNode->Asl.InlineComment,
            CommentString);
        break;

    case ASL_COMMENT_OPEN_PAREN:

        AslGbl_InlineCommentBuffer =
            CvAppendInlineComment(AslGbl_InlineCommentBuffer,
            CommentString);
        break;

    case ASL_COMMENT_CLOSE_PAREN:

        if (ParenBraceNode)
        {
            ParenBraceNode->Asl.EndNodeComment =
                CvAppendInlineComment (ParenBraceNode->Asl.EndNodeComment,
                CommentString);
        }
        else
        {
            LatestParseNode->Asl.EndNodeComment =
                CvAppendInlineComment (LatestParseNode->Asl.EndNodeComment,
                CommentString);
        }
        break;

    case ASL_COMMENT_CLOSE_BRACE:

        LatestParseNode->Asl.CloseBraceComment = CommentString;
        break;

    default:

        break;
    }
}

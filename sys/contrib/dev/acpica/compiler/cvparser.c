/******************************************************************************
 *
 * Module Name: cvparser - Converter functions that are called from the AML
 *                         parser.
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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acdispat.h>
#include <contrib/dev/acpica/include/amlcode.h>
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acdisasm.h>
#include <contrib/dev/acpica/include/acconvert.h>


/* local prototypes */

static BOOLEAN
CvCommentExists (
    UINT8                   *Address);

static BOOLEAN
CvIsFilename (
    char                   *Filename);

static ACPI_FILE_NODE*
CvFileAddressLookup(
    char                    *Address,
    ACPI_FILE_NODE          *Head);

static void
CvAddToFileTree (
    char                    *Filename,
    char                    *PreviousFilename);

static void
CvSetFileParent (
    char                    *ChildFile,
    char                    *ParentFile);


/*******************************************************************************
 *
 * FUNCTION:    CvIsFilename
 *
 * PARAMETERS:  filename - input filename
 *
 * RETURN:      BOOLEAN - TRUE if all characters are between 0x20 and 0x7f
 *
 * DESCRIPTION: Take a given char * and see if it contains all printable
 *              characters. If all characters have hexvalues 20-7f and ends with
 *              .dsl, we will assume that it is a proper filename.
 *
 ******************************************************************************/

static BOOLEAN
CvIsFilename (
    char                    *Filename)
{
    UINT64                  Length = strlen(Filename);
    char                    *FileExt = Filename + Length - 4;
    UINT64                  i;


    if ((Length > 4) && AcpiUtStricmp (FileExt, ".dsl"))
    {
        return (FALSE);
    }

    for(i = 0; i<Length; ++i)
    {
        if (!isprint ((int) Filename[i]))
        {
            return (FALSE);
        }
    }

    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    CvInitFileTree
 *
 * PARAMETERS:  Table      - input table
 *              AmlStart   - Address of the starting point of the AML.
 *              AmlLength  - Length of the AML file.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize the file dependency tree by scanning the AML.
 *              This is referred as ASL_CV_INIT_FILETREE.
 *
 ******************************************************************************/

void
CvInitFileTree (
    ACPI_TABLE_HEADER       *Table,
    UINT8                   *AmlStart,
    UINT32                  AmlLength)
{
    UINT8                   *TreeAml;
    UINT8                   *FileEnd;
    char                    *Filename = NULL;
    char                    *PreviousFilename = NULL;
    char                    *ParentFilename = NULL;
    char                    *ChildFilename = NULL;


    if (!AcpiGbl_CaptureComments)
    {
        return;
    }

    CvDbgPrint ("AmlLength: %x\n", AmlLength);
    CvDbgPrint ("AmlStart:  %p\n", AmlStart);
    CvDbgPrint ("AmlEnd?:   %p\n", AmlStart+AmlLength);

    AcpiGbl_FileTreeRoot = AcpiOsAcquireObject (AcpiGbl_FileCache);

    AcpiGbl_FileTreeRoot->FileStart = (char *)(AmlStart);
    AcpiGbl_FileTreeRoot->FileEnd = (char *)(AmlStart + Table->Length);
    AcpiGbl_FileTreeRoot->Next = NULL;
    AcpiGbl_FileTreeRoot->Parent = NULL;
    AcpiGbl_FileTreeRoot->Filename = (char *)(AmlStart+2);

    /* Set the root file to the current open file */

    AcpiGbl_FileTreeRoot->File = AcpiGbl_OutputFile;

    /*
     * Set this to true because we don't need to output
     * an include statement for the topmost file
     */
    AcpiGbl_FileTreeRoot->IncludeWritten = TRUE;
    Filename = NULL;
    AcpiGbl_CurrentFilename = (char *)(AmlStart+2);
    AcpiGbl_RootFilename    = (char *)(AmlStart+2);

    TreeAml = AmlStart;
    FileEnd = AmlStart + AmlLength;

    while (TreeAml <= FileEnd)
    {
        /*
         * Make sure that this filename contains all printable characters
         * and a .dsl extension at the end. If not, then it must be some
         * raw data that doesn't outline a filename.
         */
        if ((*TreeAml == AML_COMMENT_OP) &&
            (*(TreeAml +1) == FILENAME_COMMENT) &&
            (CvIsFilename ((char *)(TreeAml +2))))
        {
            CvDbgPrint ("A9 and a 08 file\n");
            PreviousFilename = Filename;
            Filename = (char *) (TreeAml +2);

            CvAddToFileTree (Filename, PreviousFilename);
            ChildFilename = Filename;
            CvDbgPrint ("%s\n", Filename);
        }
        else if ((*TreeAml == AML_COMMENT_OP) &&
            (*(TreeAml +1) == PARENTFILENAME_COMMENT) &&
            (CvIsFilename ((char *)(TreeAml +2))))
        {
            CvDbgPrint ("A9 and a 09 file\n");
            ParentFilename = (char *)(TreeAml +2);
            CvSetFileParent (ChildFilename, ParentFilename);
            CvDbgPrint ("%s\n", ParentFilename);
        }

        ++TreeAml;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CvClearOpComments
 *
 * PARAMETERS:  Op -- clear all comments within this Op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Clear all converter-related fields of the given Op.
 *              This is referred as ASL_CV_CLEAR_OP_COMMENTS.
 *
 ******************************************************************************/

void
CvClearOpComments (
    ACPI_PARSE_OBJECT       *Op)
{

    Op->Common.InlineComment     = NULL;
    Op->Common.EndNodeComment    = NULL;
    Op->Common.NameComment       = NULL;
    Op->Common.CommentList       = NULL;
    Op->Common.EndBlkComment     = NULL;
    Op->Common.CloseBraceComment = NULL;
    Op->Common.CvFilename        = NULL;
    Op->Common.CvParentFilename  = NULL;
}


/*******************************************************************************
 *
 * FUNCTION:    CvCommentExists
 *
 * PARAMETERS:  Address - check if this address appears in the list
 *
 * RETURN:      BOOLEAN - TRUE if the address exists.
 *
 * DESCRIPTION: Look at the pointer address and check if this appears in the
 *              list of all addresses. If it exists in the list, return TRUE
 *              if it exists. Otherwise add to the list and return FALSE.
 *
 ******************************************************************************/

static BOOLEAN
CvCommentExists (
    UINT8                    *Address)
{
    ACPI_COMMENT_ADDR_NODE   *Current = AcpiGbl_CommentAddrListHead;
    UINT8                    Option;


    if (!Address)
    {
        return (FALSE);
    }

    Option = *(Address + 1);

    /*
     * FILENAME_COMMENT and PARENTFILENAME_COMMENT are not treated as
     * comments. They serve as markers for where the file starts and ends.
     */
    if ((Option == FILENAME_COMMENT) ||
        (Option == PARENTFILENAME_COMMENT))
    {
       return (FALSE);
    }

    if (!Current)
    {
        AcpiGbl_CommentAddrListHead =
            AcpiOsAcquireObject (AcpiGbl_RegCommentCache);
        AcpiGbl_CommentAddrListHead->Addr = Address;
        AcpiGbl_CommentAddrListHead->Next = NULL;
        return (FALSE);
    }
    else
    {
        while (Current)
        {
            if (Current->Addr != Address)
            {
                Current = Current->Next;
            }
            else
            {
                return (TRUE);
            }
        }

        /*
         * If the execution gets to this point, it means that this
         * address does not exists in the list. Add this address to the
         * beginning of the list.
         */
        Current = AcpiGbl_CommentAddrListHead;
        AcpiGbl_CommentAddrListHead =
            AcpiOsAcquireObject (AcpiGbl_RegCommentCache);

        AcpiGbl_CommentAddrListHead->Addr = Address;
        AcpiGbl_CommentAddrListHead->Next = Current;
        return (FALSE);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CvFilenameExists
 *
 * PARAMETERS:  Filename        - filename to search
 *
 * RETURN:      ACPI_FILE_NODE - a pointer to a file node
 *
 * DESCRIPTION: Look for the given filename in the file dependency tree.
 *              Returns the file node if it exists, returns NULL if it does not.
 *
 ******************************************************************************/

ACPI_FILE_NODE*
CvFilenameExists(
    char                    *Filename,
    ACPI_FILE_NODE          *Head)
{
    ACPI_FILE_NODE          *Current = Head;


    if (!Filename)
    {
        return (NULL);
    }

    while (Current)
    {
        if (!AcpiUtStricmp (Current->Filename, Filename))
        {
            return (Current);
        }

        Current = Current->Next;
    }
    return (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    CvFileAddressLookup
 *
 * PARAMETERS:  Address        - address to look up
 *              Head           - file dependency tree
 *
 * RETURN:      ACPI_FILE_NODE - pointer to a file node containing the address
 *
 * DESCRIPTION: Look for the given address in the file dependency tree.
 *              Returns the first file node where the given address is within
 *              the file node's starting and ending address.
 *
 ******************************************************************************/

static ACPI_FILE_NODE *
CvFileAddressLookup(
    char                    *Address,
    ACPI_FILE_NODE          *Head)
{
    ACPI_FILE_NODE          *Current = Head;


    while (Current)
    {
        if ((Address >= Current->FileStart) &&
            (Address < Current->FileEnd ||
            !Current->FileEnd))
        {
            return (Current);
        }

        Current = Current->Next;
    }

    return (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    CvLabelFileNode
 *
 * PARAMETERS:  Op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Takes a given parse op, looks up its Op->Common.Aml field
 *              within the file tree and fills in appropriate file information
 *              from a matching node within the tree.
 *              This is referred as ASL_CV_LABEL_FILENODE.
 *
 ******************************************************************************/

void
CvLabelFileNode(
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_FILE_NODE          *Node;


    if (!Op)
    {
        return;
    }

    Node = CvFileAddressLookup ((char *)
        Op->Common.Aml, AcpiGbl_FileTreeRoot);
    if (!Node)
    {
       return;
    }

    Op->Common.CvFilename = Node->Filename;
    if (Node->Parent)
    {
        Op->Common.CvParentFilename = Node->Parent->Filename;
    }
    else
    {
        Op->Common.CvParentFilename = Node->Filename;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CvAddToFileTree
 *
 * PARAMETERS:  Filename          - Address containing the name of the current
 *                                  filename
 *              PreviousFilename  - Address containing the name of the previous
 *                                  filename
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add this filename to the AcpiGbl_FileTree if it does not exist.
 *
 ******************************************************************************/

static void
CvAddToFileTree (
    char                    *Filename,
    char                    *PreviousFilename)
{
    ACPI_FILE_NODE          *Node;


    if (!AcpiUtStricmp(Filename, AcpiGbl_RootFilename) &&
        PreviousFilename)
    {
        Node = CvFilenameExists (PreviousFilename, AcpiGbl_FileTreeRoot);
        if (Node)
        {
            /*
             * Set the end point of the PreviousFilename to the address
             * of Filename.
             */
            Node->FileEnd = Filename;
        }
    }
    else if (!AcpiUtStricmp(Filename, AcpiGbl_RootFilename) &&
             !PreviousFilename)
    {
        return;
    }

    Node = CvFilenameExists (Filename, AcpiGbl_FileTreeRoot);
    if (Node && PreviousFilename)
    {
        /*
         * Update the end of the previous file and all of their parents'
         * ending addresses. This is done to ensure that parent file
         * ranges extend to the end of their childrens' files.
         */
        Node = CvFilenameExists (PreviousFilename, AcpiGbl_FileTreeRoot);
        if (Node && (Node->FileEnd < Filename))
        {
            Node->FileEnd = Filename;
            Node = Node->Parent;
            while (Node)
            {
                if (Node->FileEnd < Filename)
                {
                    Node->FileEnd = Filename;
                }

                Node = Node->Parent;
            }
        }
    }
    else
    {
        Node = AcpiGbl_FileTreeRoot;
        AcpiGbl_FileTreeRoot = AcpiOsAcquireObject (AcpiGbl_FileCache);

        AcpiGbl_FileTreeRoot->Next = Node;
        AcpiGbl_FileTreeRoot->Parent = NULL;
        AcpiGbl_FileTreeRoot->Filename = Filename;
        AcpiGbl_FileTreeRoot->FileStart = Filename;
        AcpiGbl_FileTreeRoot->IncludeWritten = FALSE;
        AcpiGbl_FileTreeRoot->File = fopen(Filename, "w+");

        /*
         * If we can't open the file, we need to abort here before we
         * accidentally write to a NULL file.
         */
        if (!AcpiGbl_FileTreeRoot->File)
        {
            /* delete the .xxx file */

            FlDeleteFile (ASL_FILE_AML_OUTPUT);
            sprintf (AslGbl_MsgBuffer, "\"%s\" - %s", Filename, strerror (errno));
            AslCommonError (ASL_ERROR, ASL_MSG_OPEN, 0, 0, 0, 0,
                NULL, AslGbl_MsgBuffer);
            AslAbort ();
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CvSetFileParent
 *
 * PARAMETERS:  ChildFile  - contains the filename of the child file
 *              ParentFile - contains the filename of the parent file.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Point the parent pointer of the Child to the node that
 *              corresponds with the parent file node.
 *
 ******************************************************************************/

static void
CvSetFileParent (
    char                    *ChildFile,
    char                    *ParentFile)
{
    ACPI_FILE_NODE          *Child;
    ACPI_FILE_NODE          *Parent;


    Child  = CvFilenameExists (ChildFile, AcpiGbl_FileTreeRoot);
    Parent = CvFilenameExists (ParentFile, AcpiGbl_FileTreeRoot);

    if (Child && Parent)
    {
        Child->Parent = Parent;

        while (Child->Parent)
        {
            if (Child->Parent->FileEnd < Child->FileStart)
            {
                Child->Parent->FileEnd = Child->FileStart;
            }

            Child = Child->Parent;
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CvCaptureCommentsOnly
 *
 * PARAMETERS:  ParserState         - A parser state object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Look at the aml that the parser state is pointing to,
 *              capture any AML_COMMENT_OP and it's arguments and increment the
 *              aml pointer past the comment. Comments are transferred to parse
 *              nodes through CvTransferComments() as well as
 *              AcpiPsBuildNamedOp().
 *              This is referred as ASL_CV_CAPTURE_COMMENTS_ONLY.
 *
 ******************************************************************************/

void
CvCaptureCommentsOnly (
    ACPI_PARSE_STATE        *ParserState)
{
    UINT8                   *Aml = ParserState->Aml;
    UINT16                  Opcode = (UINT16) ACPI_GET8 (Aml);
    UINT32                  Length = 0;
    UINT8                   CommentOption;
    BOOLEAN                 StdDefBlockFlag = FALSE;
    ACPI_COMMENT_NODE       *CommentNode;
    ACPI_FILE_NODE          *FileNode;


    if (!AcpiGbl_CaptureComments ||
        Opcode != AML_COMMENT_OP)
    {
       return;
    }

    while (Opcode == AML_COMMENT_OP)
    {
        CvDbgPrint ("comment aml address: %p\n", Aml);

        if (CvCommentExists(ParserState->Aml))
        {
            CvDbgPrint ("Avoiding capturing an existing comment.\n");
        }
        else
        {
            CommentOption = *(Aml +1);

            /*
             * Increment past the comment option and point the
             * appropriate char pointers
             */
            Aml += 2;

            /* Found a comment. Now, set pointers to these comments. */

            switch (CommentOption)
            {
                case STD_DEFBLK_COMMENT:

                    StdDefBlockFlag = TRUE;

                    /*
                     * Add to a linked list of nodes. This list will be
                     * taken by the parse node created next.
                     */
                    CommentNode = AcpiOsAcquireObject (
                        AcpiGbl_RegCommentCache);
                    CommentNode->Comment = ACPI_CAST_PTR (char, Aml);
                    CommentNode->Next = NULL;

                    if (!AcpiGbl_DefBlkCommentListHead)
                    {
                        AcpiGbl_DefBlkCommentListHead = CommentNode;
                        AcpiGbl_DefBlkCommentListTail = CommentNode;
                    }
                    else
                    {
                        AcpiGbl_DefBlkCommentListTail->Next = CommentNode;
                        AcpiGbl_DefBlkCommentListTail =
                            AcpiGbl_DefBlkCommentListTail->Next;
                    }
                    break;

                case STANDARD_COMMENT:

                    CvDbgPrint ("found regular comment.\n");

                    /*
                     * Add to a linked list of nodes. This list will be
                     * taken by the parse node created next.
                     */
                    CommentNode = AcpiOsAcquireObject (
                        AcpiGbl_RegCommentCache);
                    CommentNode->Comment = ACPI_CAST_PTR (char, Aml);
                    CommentNode->Next    = NULL;

                    if (!AcpiGbl_RegCommentListHead)
                    {
                        AcpiGbl_RegCommentListHead = CommentNode;
                        AcpiGbl_RegCommentListTail = CommentNode;
                    }
                    else
                    {
                        AcpiGbl_RegCommentListTail->Next = CommentNode;
                        AcpiGbl_RegCommentListTail =
                            AcpiGbl_RegCommentListTail->Next;
                    }
                    break;

                case ENDBLK_COMMENT:

                    CvDbgPrint ("found endblk comment.\n");

                    /* Add to a linked list of nodes. This will be
                     * taken by the next created parse node.
                     */
                    CommentNode = AcpiOsAcquireObject (
                        AcpiGbl_RegCommentCache);
                    CommentNode->Comment = ACPI_CAST_PTR (char, Aml);
                    CommentNode->Next    = NULL;

                    if (!AcpiGbl_EndBlkCommentListHead)
                    {
                        AcpiGbl_EndBlkCommentListHead = CommentNode;
                        AcpiGbl_EndBlkCommentListTail = CommentNode;
                    }
                    else
                    {
                        AcpiGbl_EndBlkCommentListTail->Next = CommentNode;
                        AcpiGbl_EndBlkCommentListTail =
                            AcpiGbl_EndBlkCommentListTail->Next;
                    }
                    break;

                case INLINE_COMMENT:

                    CvDbgPrint ("found inline comment.\n");
                    AcpiGbl_CurrentInlineComment =
                        ACPI_CAST_PTR (char, Aml);
                    break;

                case ENDNODE_COMMENT:

                    CvDbgPrint ("found EndNode comment.\n");
                    AcpiGbl_CurrentEndNodeComment =
                        ACPI_CAST_PTR (char, Aml);
                    break;

                case CLOSE_BRACE_COMMENT:

                    CvDbgPrint ("found close brace comment.\n");
                    AcpiGbl_CurrentCloseBraceComment =
                        ACPI_CAST_PTR (char, Aml);
                    break;

                case END_DEFBLK_COMMENT:

                    CvDbgPrint ("Found comment that belongs after"
                        " the } for a definition block.\n");
                    AcpiGbl_CurrentScope->Common.CloseBraceComment =
                        ACPI_CAST_PTR (char, Aml);
                    break;

                case FILENAME_COMMENT:

                    CvDbgPrint ("Found a filename: %s\n",
                        ACPI_CAST_PTR (char, Aml));
                    FileNode = CvFilenameExists (
                        ACPI_CAST_PTR (char, Aml), AcpiGbl_FileTreeRoot);

                    /*
                     * If there is an INCLUDE_COMMENT followed by a
                     * FILENAME_COMMENT, then the INCLUDE_COMMENT is a comment
                     * that is emitted before the #include for the file.
                     * We will save the IncludeComment within the FileNode
                     * associated with this FILENAME_COMMENT.
                     */
                    if (FileNode && AcpiGbl_IncCommentListHead)
                    {
                        FileNode->IncludeComment = AcpiGbl_IncCommentListHead;
                        AcpiGbl_IncCommentListHead = NULL;
                        AcpiGbl_IncCommentListTail = NULL;
                    }
                    break;

                case PARENTFILENAME_COMMENT:
                    CvDbgPrint ("    Found a parent filename.\n");
                    break;

                case INCLUDE_COMMENT:

                    /*
                     * Add to a linked list. This list will be taken by the
                     * parse node created next. See the FILENAME_COMMENT case
                     * for more details
                     */
                    CommentNode = AcpiOsAcquireObject (
                        AcpiGbl_RegCommentCache);
                    CommentNode->Comment = ACPI_CAST_PTR (char, Aml);
                    CommentNode->Next = NULL;

                    if (!AcpiGbl_IncCommentListHead)
                    {
                        AcpiGbl_IncCommentListHead = CommentNode;
                        AcpiGbl_IncCommentListTail = CommentNode;
                    }
                    else
                    {
                        AcpiGbl_IncCommentListTail->Next = CommentNode;
                        AcpiGbl_IncCommentListTail =
                            AcpiGbl_IncCommentListTail->Next;
                    }

                    CvDbgPrint ("Found a include comment: %s\n",
                        CommentNode->Comment);
                    break;

                default:

                    /* Not a valid comment option. Revert the AML */

                    Aml -= 2;
                    goto DefBlock;

            } /* End switch statement */

        } /* End else */

        /* Determine the length and move forward that amount */

        Length = 0;
        while (ParserState->Aml[Length])
        {
            Length++;
        }

        ParserState->Aml += Length + 1;

        /* Peek at the next Opcode. */

        Aml = ParserState->Aml;
        Opcode = (UINT16) ACPI_GET8 (Aml);
    }

DefBlock:
    if (StdDefBlockFlag)
    {
        /*
         * Give all of its comments to the current scope, which is known as
         * the definition block, since STD_DEFBLK_COMMENT only appears after
         * definition block headers.
         */
        AcpiGbl_CurrentScope->Common.CommentList
            = AcpiGbl_DefBlkCommentListHead;
        AcpiGbl_DefBlkCommentListHead = NULL;
        AcpiGbl_DefBlkCommentListTail = NULL;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    CvCaptureComments
 *
 * PARAMETERS:  ParserState         - A parser state object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Wrapper function for CvCaptureCommentsOnly
 *              This is referred as ASL_CV_CAPTURE_COMMENTS.
 *
 ******************************************************************************/

void
CvCaptureComments (
    ACPI_WALK_STATE         *WalkState)
{
    UINT8                   *Aml;
    UINT16                  Opcode;
    const ACPI_OPCODE_INFO  *OpInfo;


    if (!AcpiGbl_CaptureComments)
    {
        return;
    }

    /*
     * Before parsing, check to see that comments that come directly
     * after deferred opcodes aren't being processed.
     */
    Aml = WalkState->ParserState.Aml;
    Opcode = (UINT16) ACPI_GET8 (Aml);
    OpInfo = AcpiPsGetOpcodeInfo (Opcode);

    if (!(OpInfo->Flags & AML_DEFER) ||
        ((OpInfo->Flags & AML_DEFER) &&
        (WalkState->PassNumber != ACPI_IMODE_LOAD_PASS1)))
    {
        CvCaptureCommentsOnly (&WalkState->ParserState);
        WalkState->Aml = WalkState->ParserState.Aml;
    }

}


/*******************************************************************************
 *
 * FUNCTION:    CvTransferComments
 *
 * PARAMETERS:  Op                  - Transfer comments to this Op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Transfer all of the comments stored in global containers to the
 *              given Op. This will be invoked shortly after the parser creates
 *              a ParseOp.
 *              This is referred as ASL_CV_TRANSFER_COMMENTS.
 *
 ******************************************************************************/

void
CvTransferComments (
    ACPI_PARSE_OBJECT       *Op)
{

    Op->Common.InlineComment = AcpiGbl_CurrentInlineComment;
    AcpiGbl_CurrentInlineComment = NULL;

    Op->Common.EndNodeComment = AcpiGbl_CurrentEndNodeComment;
    AcpiGbl_CurrentEndNodeComment = NULL;

    Op->Common.CloseBraceComment = AcpiGbl_CurrentCloseBraceComment;
    AcpiGbl_CurrentCloseBraceComment = NULL;

    Op->Common.CommentList = AcpiGbl_RegCommentListHead;
    AcpiGbl_RegCommentListHead = NULL;
    AcpiGbl_RegCommentListTail = NULL;

    Op->Common.EndBlkComment = AcpiGbl_EndBlkCommentListHead;
    AcpiGbl_EndBlkCommentListHead = NULL;
    AcpiGbl_EndBlkCommentListTail = NULL;
}

/******************************************************************************
 *
 * Module Name: prscan - Preprocessor start-up and file scan module
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

#define _DECLARE_PR_GLOBALS

#include <contrib/dev/acpica/compiler/aslcompiler.h>

/*
 * TBDs:
 *
 * No nested macros, maybe never
 * Implement ASL "Include" as well as "#include" here?
 */
#define _COMPONENT          ASL_PREPROCESSOR
        ACPI_MODULE_NAME    ("prscan")


/* Local prototypes */

static void
PrPreprocessInputFile (
    void);

static void
PrDoDirective (
    char                    *DirectiveToken,
    char                    **Next);

static void
PrGetNextLineInit (
    void);

static UINT32
PrGetNextLine (
    FILE                    *Handle);

static int
PrMatchDirective (
    char                    *Directive);

static void
PrPushDirective (
    int                     Directive,
    char                    *Argument);

static ACPI_STATUS
PrPopDirective (
    void);

static void
PrDbgPrint (
    char                    *Action,
    char                    *DirectiveName);

static void
PrDoIncludeBuffer (
    char                    *Pathname,
    char                    *BufferName);

static void
PrDoIncludeFile (
    char                    *Pathname);


/*
 * Supported preprocessor directives
 * Each entry is of the form "Name, ArgumentCount"
 */
static const PR_DIRECTIVE_INFO      AslGbl_DirectiveInfo[] =
{
    {"define",          1},
    {"elif",            0}, /* Converted to #else..#if internally */
    {"else",            0},
    {"endif",           0},
    {"error",           1},
    {"if",              1},
    {"ifdef",           1},
    {"ifndef",          1},
    {"include",         0}, /* Argument is not standard format, so just use 0 here */
    {"includebuffer",   0}, /* Argument is not standard format, so just use 0 here */
    {"line",            1},
    {"pragma",          1},
    {"undef",           1},
    {"warning",         1},
    {NULL,              0}
};

/* This table must match ordering of above table exactly */

enum Gbl_DirectiveIndexes
{
    PR_DIRECTIVE_DEFINE = 0,
    PR_DIRECTIVE_ELIF,
    PR_DIRECTIVE_ELSE,
    PR_DIRECTIVE_ENDIF,
    PR_DIRECTIVE_ERROR,
    PR_DIRECTIVE_IF,
    PR_DIRECTIVE_IFDEF,
    PR_DIRECTIVE_IFNDEF,
    PR_DIRECTIVE_INCLUDE,
    PR_DIRECTIVE_INCLUDEBUFFER,
    PR_DIRECTIVE_LINE,
    PR_DIRECTIVE_PRAGMA,
    PR_DIRECTIVE_UNDEF,
    PR_DIRECTIVE_WARNING
};

#define ASL_DIRECTIVE_NOT_FOUND     -1


/*******************************************************************************
 *
 * FUNCTION:    PrInitializePreprocessor
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Startup initialization for the Preprocessor.
 *
 ******************************************************************************/

void
PrInitializePreprocessor (
    void)
{
    /* Init globals and the list of #defines */

    PrInitializeGlobals ();
    AslGbl_DefineList = NULL;
}


/*******************************************************************************
 *
 * FUNCTION:    PrInitializeGlobals
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize globals for the Preprocessor. Used for startuup
 *              initialization and re-initialization between compiles during
 *              a multiple source file compile.
 *
 ******************************************************************************/

void
PrInitializeGlobals (
    void)
{
    /* Init globals */

    AslGbl_InputFileList = NULL;
    AslGbl_CurrentLineNumber = 1;
    AslGbl_PreprocessorLineNumber = 1;
    AslGbl_PreprocessorError = FALSE;

    /* These are used to track #if/#else blocks (possibly nested) */

    AslGbl_IfDepth = 0;
    AslGbl_IgnoringThisCodeBlock = FALSE;
    AslGbl_DirectiveStack = NULL;
}


/*******************************************************************************
 *
 * FUNCTION:    PrTerminatePreprocessor
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Termination of the preprocessor. Delete lists. Keep any
 *              defines that were specified on the command line, in order to
 *              support multiple compiles with a single compiler invocation.
 *
 ******************************************************************************/

void
PrTerminatePreprocessor (
    void)
{
    PR_DEFINE_INFO          *DefineInfo;


    /*
     * The persistent defines (created on the command line) are always at the
     * end of the list. We save them.
     */
    while ((AslGbl_DefineList) && (!AslGbl_DefineList->Persist))
    {
        DefineInfo = AslGbl_DefineList;
        AslGbl_DefineList = DefineInfo->Next;

        ACPI_FREE (DefineInfo->Replacement);
        ACPI_FREE (DefineInfo->Identifier);
        ACPI_FREE (DefineInfo);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    PrDoPreprocess
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Main entry point for the iASL Preprocessor. Input file must
 *              be already open. Handles multiple input files via the
 *              #include directive.
 *
 ******************************************************************************/

void
PrDoPreprocess (
    void)
{
    BOOLEAN                 MoreInputFiles;


    DbgPrint (ASL_DEBUG_OUTPUT, "Starting preprocessing phase\n\n");


    FlSeekFile (ASL_FILE_INPUT, 0);
    PrDumpPredefinedNames ();

    /* Main preprocessor loop, handles include files */

    do
    {
        PrPreprocessInputFile ();
        MoreInputFiles = PrPopInputFileStack ();

    } while (MoreInputFiles);

    /* Point compiler input to the new preprocessor output file (.pre) */

    FlCloseFile (ASL_FILE_INPUT);
    AslGbl_Files[ASL_FILE_INPUT].Handle = AslGbl_Files[ASL_FILE_PREPROCESSOR].Handle;
    AslCompilerin = AslGbl_Files[ASL_FILE_INPUT].Handle;

    /* Reset globals to allow compiler to run */

    FlSeekFile (ASL_FILE_INPUT, 0);
    if (!AslGbl_PreprocessOnly)
    {
        AslGbl_CurrentLineNumber = 0;
    }

    DbgPrint (ASL_DEBUG_OUTPUT, "Preprocessing phase complete \n\n");
}


/*******************************************************************************
 *
 * FUNCTION:    PrPreprocessInputFile
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Preprocess one entire file, line-by-line.
 *
 * Input:  Raw user ASL from ASL_FILE_INPUT
 * Output: Preprocessed file written to ASL_FILE_PREPROCESSOR and
 *         (optionally) ASL_FILE_PREPROCESSOR_USER
 *
 ******************************************************************************/

static void
PrPreprocessInputFile (
    void)
{
    UINT32                  Status;
    char                    *Token;
    char                    *ReplaceString;
    PR_DEFINE_INFO          *DefineInfo;
    ACPI_SIZE               TokenOffset;
    char                    *Next;
    int                     OffsetAdjust;


    PrGetNextLineInit ();

    /* Scan source line-by-line and process directives. Then write the .i file */

    while ((Status = PrGetNextLine (AslGbl_Files[ASL_FILE_INPUT].Handle)) != ASL_EOF)
    {
        AslGbl_CurrentLineNumber++;
        AslGbl_LogicalLineNumber++;

        if (Status == ASL_IGNORE_LINE)
        {
            goto WriteEntireLine;
        }

        /* Need a copy of the input line for strok() */

        strcpy (AslGbl_MainTokenBuffer, AslGbl_CurrentLineBuffer);
        Token = PrGetNextToken (AslGbl_MainTokenBuffer, PR_TOKEN_SEPARATORS, &Next);
        OffsetAdjust = 0;

        /* All preprocessor directives must begin with '#' */

        if (Token && (*Token == '#'))
        {
            if (strlen (Token) == 1)
            {
                Token = PrGetNextToken (NULL, PR_TOKEN_SEPARATORS, &Next);
            }
            else
            {
                Token++;    /* Skip leading # */
            }

            /* Execute the directive, do not write line to output file */

            PrDoDirective (Token, &Next);
            continue;
        }

        /*
         * If we are currently within the part of an IF/ELSE block that is
         * FALSE, ignore the line and do not write it to the output file.
         * This continues until an #else or #endif is encountered.
         */
        if (AslGbl_IgnoringThisCodeBlock)
        {
            continue;
        }

        /* Match and replace all #defined names within this source line */

        while (Token)
        {
            DefineInfo = PrMatchDefine (Token);
            if (DefineInfo)
            {
                if (DefineInfo->Body)
                {
                    /* This is a macro */

                    DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID
                        "Matched Macro: %s->%s\n",
                        AslGbl_CurrentLineNumber, DefineInfo->Identifier,
                        DefineInfo->Replacement);

                    PrDoMacroInvocation (AslGbl_MainTokenBuffer, Token,
                        DefineInfo, &Next);
                }
                else
                {
                    ReplaceString = DefineInfo->Replacement;

                    /* Replace the name in the original line buffer */

                    TokenOffset = Token - AslGbl_MainTokenBuffer + OffsetAdjust;
                    PrReplaceData (
                        &AslGbl_CurrentLineBuffer[TokenOffset], strlen (Token),
                        ReplaceString, strlen (ReplaceString));

                    /* Adjust for length difference between old and new name length */

                    OffsetAdjust += strlen (ReplaceString) - strlen (Token);

                    DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID
                        "Matched #define: %s->%s\n",
                        AslGbl_CurrentLineNumber, Token,
                        *ReplaceString ? ReplaceString : "(NULL STRING)");
                }
            }

            Token = PrGetNextToken (NULL, PR_TOKEN_SEPARATORS, &Next);
        }

        AslGbl_PreprocessorLineNumber++;


WriteEntireLine:
        /*
         * Now we can write the possibly modified source line to the
         * preprocessor file(s).
         */
        FlWriteFile (ASL_FILE_PREPROCESSOR, AslGbl_CurrentLineBuffer,
            strlen (AslGbl_CurrentLineBuffer));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    PrDoDirective
 *
 * PARAMETERS:  Directive               - Pointer to directive name token
 *              Next                    - "Next" buffer from GetNextToken
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Main processing for all preprocessor directives
 *
 ******************************************************************************/

static void
PrDoDirective (
    char                    *DirectiveToken,
    char                    **Next)
{
    char                    *Token = AslGbl_MainTokenBuffer;
    char                    *Token2 = NULL;
    char                    *End;
    UINT64                  Value;
    ACPI_SIZE               TokenOffset;
    int                     Directive;
    ACPI_STATUS             Status;


    if (!DirectiveToken)
    {
        goto SyntaxError;
    }

    Directive = PrMatchDirective (DirectiveToken);
    if (Directive == ASL_DIRECTIVE_NOT_FOUND)
    {
        PrError (ASL_ERROR, ASL_MSG_UNKNOWN_DIRECTIVE,
            THIS_TOKEN_OFFSET (DirectiveToken));

        DbgPrint (ASL_PARSE_OUTPUT, PR_PREFIX_ID
            "#%s: Unknown directive\n",
            AslGbl_CurrentLineNumber, DirectiveToken);
        return;
    }

    /*
     * Emit a line directive into the preprocessor file (.pre) after
     * every matched directive. This is passed through to the compiler
     * so that error/warning messages are kept in sync with the
     * original source file.
     */
    FlPrintFile (ASL_FILE_PREPROCESSOR, "#line %u \"%s\" // #%s\n",
        AslGbl_CurrentLineNumber, AslGbl_Files[ASL_FILE_INPUT].Filename,
        AslGbl_DirectiveInfo[Directive].Name);

    /*
     * If we are currently ignoring this block and we encounter a #else or
     * #elif, we must ignore their blocks also if the parent block is also
     * being ignored.
     */
    if (AslGbl_IgnoringThisCodeBlock)
    {
        switch (Directive)
        {
        case PR_DIRECTIVE_ELSE:
        case PR_DIRECTIVE_ELIF:

            if (AslGbl_DirectiveStack &&
                AslGbl_DirectiveStack->IgnoringThisCodeBlock)
            {
                PrDbgPrint ("Ignoring", AslGbl_DirectiveInfo[Directive].Name);
                return;
            }
            break;

        default:
            break;
        }
    }

    /*
     * Need to always check for #else, #elif, #endif regardless of
     * whether we are ignoring the current code block, since these
     * are conditional code block terminators.
     */
    switch (Directive)
    {
    case PR_DIRECTIVE_ELSE:

        AslGbl_IgnoringThisCodeBlock = !(AslGbl_IgnoringThisCodeBlock);
        PrDbgPrint ("Executing", "else block");
        return;

    case PR_DIRECTIVE_ELIF:

        AslGbl_IgnoringThisCodeBlock = !(AslGbl_IgnoringThisCodeBlock);
        Directive = PR_DIRECTIVE_IF;

        if (AslGbl_IgnoringThisCodeBlock == TRUE)
        {
            /* Not executing the ELSE part -- all done here */
            PrDbgPrint ("Ignoring", "elif block");
            return;
        }

        /*
         * After this, we will execute the IF part further below.
         * First, however, pop off the original #if directive.
         */
        if (ACPI_FAILURE (PrPopDirective ()))
        {
            PrError (ASL_ERROR, ASL_MSG_COMPILER_INTERNAL,
                THIS_TOKEN_OFFSET (DirectiveToken));
        }

        PrDbgPrint ("Executing", "elif block");
        break;

    case PR_DIRECTIVE_ENDIF:

        PrDbgPrint ("Executing", "endif");

        /* Pop the owning #if/#ifdef/#ifndef */

        if (ACPI_FAILURE (PrPopDirective ()))
        {
            PrError (ASL_ERROR, ASL_MSG_ENDIF_MISMATCH,
                THIS_TOKEN_OFFSET (DirectiveToken));
        }
        return;

    default:
        break;
    }

    /* Most directives have at least one argument */

    if (AslGbl_DirectiveInfo[Directive].ArgCount >= 1)
    {
        Token = PrGetNextToken (NULL, PR_TOKEN_SEPARATORS, Next);
        if (!Token)
        {
            goto SyntaxError;
        }
    }

    if (AslGbl_DirectiveInfo[Directive].ArgCount >= 2)
    {
        Token2 = PrGetNextToken (NULL, PR_TOKEN_SEPARATORS, Next);
        if (!Token2)
        {
            goto SyntaxError;
        }
    }

    /*
     * At this point, if we are ignoring the current code block,
     * do not process any more directives (i.e., ignore them also.)
     * For "if" style directives, open/push a new block anyway. We
     * must do this to keep track of #endif directives
     */
    if (AslGbl_IgnoringThisCodeBlock)
    {
        switch (Directive)
        {
        case PR_DIRECTIVE_IF:
        case PR_DIRECTIVE_IFDEF:
        case PR_DIRECTIVE_IFNDEF:

            PrPushDirective (Directive, Token);
            PrDbgPrint ("Ignoring", AslGbl_DirectiveInfo[Directive].Name);
            break;

        default:
            break;
        }

        return;
    }

    /*
     * Execute the directive
     */
    PrDbgPrint ("Begin execution", AslGbl_DirectiveInfo[Directive].Name);

    switch (Directive)
    {
    case PR_DIRECTIVE_IF:

        TokenOffset = Token - AslGbl_MainTokenBuffer;

        /* Need to expand #define macros in the expression string first */

        Status = PrResolveIntegerExpression (
            &AslGbl_CurrentLineBuffer[TokenOffset-1], &Value);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        PrPushDirective (Directive, Token);
        if (!Value)
        {
            AslGbl_IgnoringThisCodeBlock = TRUE;
        }

        DbgPrint (ASL_PARSE_OUTPUT, PR_PREFIX_ID
            "Resolved #if: %8.8X%8.8X %s\n",
            AslGbl_CurrentLineNumber, ACPI_FORMAT_UINT64 (Value),
            AslGbl_IgnoringThisCodeBlock ? "<Skipping Block>" : "<Executing Block>");
        break;

    case PR_DIRECTIVE_IFDEF:

        PrPushDirective (Directive, Token);
        if (!PrMatchDefine (Token))
        {
            AslGbl_IgnoringThisCodeBlock = TRUE;
        }

        PrDbgPrint ("Evaluated", "ifdef");
        break;

    case PR_DIRECTIVE_IFNDEF:

        PrPushDirective (Directive, Token);
        if (PrMatchDefine (Token))
        {
            AslGbl_IgnoringThisCodeBlock = TRUE;
        }

        PrDbgPrint ("Evaluated", "ifndef");
        break;

    case PR_DIRECTIVE_DEFINE:
        /*
         * By definition, if first char after the name is a paren,
         * this is a function macro.
         */
        TokenOffset = Token - AslGbl_MainTokenBuffer + strlen (Token);
        if (*(&AslGbl_CurrentLineBuffer[TokenOffset]) == '(')
        {
#ifndef MACROS_SUPPORTED
            AcpiOsPrintf (
                "%s ERROR - line %u: #define macros are not supported yet\n",
                AslGbl_CurrentLineBuffer, AslGbl_LogicalLineNumber);
            exit(1);
#else
            PrAddMacro (Token, Next);
#endif
        }
        else
        {
            /* Use the remainder of the line for the #define */

            Token2 = *Next;
            if (Token2)
            {
                while ((*Token2 == ' ') || (*Token2 == '\t'))
                {
                    Token2++;
                }

                End = Token2;
                while (*End != '\n')
                {
                    End++;
                }

                *End = 0;
            }
            else
            {
                Token2 = "";
            }
#if 0
            Token2 = PrGetNextToken (NULL, "\n", /*PR_TOKEN_SEPARATORS,*/ Next);
            if (!Token2)
            {
                Token2 = "";
            }
#endif
            DbgPrint (ASL_PARSE_OUTPUT, PR_PREFIX_ID
                "New #define: %s->%s\n",
                AslGbl_LogicalLineNumber, Token, Token2);

            PrAddDefine (Token, Token2, FALSE);
        }
        break;

    case PR_DIRECTIVE_ERROR:

        /* Note: No macro expansion */

        PrError (ASL_ERROR, ASL_MSG_ERROR_DIRECTIVE,
            THIS_TOKEN_OFFSET (Token));

        AslGbl_SourceLine = 0;
        AslGbl_NextError = AslGbl_ErrorLog;
        CmCleanupAndExit ();
        exit(1);

    case PR_DIRECTIVE_INCLUDE:

        Token = PrGetNextToken (NULL, " \"<>", Next);
        if (!Token)
        {
            goto SyntaxError;
        }

        DbgPrint (ASL_PARSE_OUTPUT, PR_PREFIX_ID
            "Start #include file \"%s\"\n", AslGbl_CurrentLineNumber,
            Token, AslGbl_CurrentLineNumber);

        PrDoIncludeFile (Token);
        break;

    case PR_DIRECTIVE_INCLUDEBUFFER:

        Token = PrGetNextToken (NULL, " \"<>", Next);
        if (!Token)
        {
            goto SyntaxError;
        }

        Token2 = PrGetNextToken (NULL, PR_TOKEN_SEPARATORS, Next);
        if (!Token2)
        {
            goto SyntaxError;
        }

        DbgPrint (ASL_PARSE_OUTPUT, PR_PREFIX_ID
            "Start #includebuffer input from file \"%s\", buffer name %s\n",
            AslGbl_CurrentLineNumber, Token, Token2);

        PrDoIncludeBuffer (Token, Token2);
        break;

    case PR_DIRECTIVE_LINE:

        TokenOffset = Token - AslGbl_MainTokenBuffer;

        Status = PrResolveIntegerExpression (
            &AslGbl_CurrentLineBuffer[TokenOffset-1], &Value);
        if (ACPI_FAILURE (Status))
        {
            return;
        }

        DbgPrint (ASL_PARSE_OUTPUT, PR_PREFIX_ID
            "User #line invocation %s\n", AslGbl_CurrentLineNumber,
            Token);

        AslGbl_CurrentLineNumber = (UINT32) Value;

        /* Emit #line into the preprocessor file */

        FlPrintFile (ASL_FILE_PREPROCESSOR, "#line %u \"%s\"\n",
            AslGbl_CurrentLineNumber, AslGbl_Files[ASL_FILE_INPUT].Filename);
        break;

    case PR_DIRECTIVE_PRAGMA:

        if (!strcmp (Token, "disable"))
        {
            Token = PrGetNextToken (NULL, PR_TOKEN_SEPARATORS, Next);
            if (!Token)
            {
                goto SyntaxError;
            }

            TokenOffset = Token - AslGbl_MainTokenBuffer;
            AslDisableException (&AslGbl_CurrentLineBuffer[TokenOffset]);
        }
        else if (!strcmp (Token, "message"))
        {
            Token = PrGetNextToken (NULL, PR_TOKEN_SEPARATORS, Next);
            if (!Token)
            {
                goto SyntaxError;
            }

            TokenOffset = Token - AslGbl_MainTokenBuffer;
            AcpiOsPrintf ("%s\n", &AslGbl_CurrentLineBuffer[TokenOffset]);
        }
        else
        {
            PrError (ASL_ERROR, ASL_MSG_UNKNOWN_PRAGMA,
                THIS_TOKEN_OFFSET (Token));
            return;
        }

        break;

    case PR_DIRECTIVE_UNDEF:

        DbgPrint (ASL_PARSE_OUTPUT, PR_PREFIX_ID
            "#undef: %s\n", AslGbl_CurrentLineNumber, Token);

        PrRemoveDefine (Token);
        break;

    case PR_DIRECTIVE_WARNING:

        PrError (ASL_WARNING, ASL_MSG_WARNING_DIRECTIVE,
            THIS_TOKEN_OFFSET (Token));

        AslGbl_SourceLine = 0;
        AslGbl_NextError = AslGbl_ErrorLog;
        break;

    default:

        /* Should never get here */
        DbgPrint (ASL_PARSE_OUTPUT, PR_PREFIX_ID
            "Unrecognized directive: %u\n",
            AslGbl_CurrentLineNumber, Directive);
        break;
    }

    return;

SyntaxError:

    PrError (ASL_ERROR, ASL_MSG_DIRECTIVE_SYNTAX,
        THIS_TOKEN_OFFSET (DirectiveToken));
    return;
}


/*******************************************************************************
 *
 * FUNCTION:    PrGetNextLine, PrGetNextLineInit
 *
 * PARAMETERS:  Handle              - Open file handle for the source file
 *
 * RETURN:      Status of the GetLine operation:
 *              AE_OK               - Normal line, OK status
 *              ASL_IGNORE_LINE     - Line is blank or part of a multi-line
 *                                      comment
 *              ASL_EOF             - End-of-file reached
 *
 * DESCRIPTION: Get the next text line from the input file. Does not strip
 *              comments.
 *
 ******************************************************************************/

#define PR_NORMAL_TEXT          0
#define PR_MULTI_LINE_COMMENT   1
#define PR_SINGLE_LINE_COMMENT  2
#define PR_QUOTED_STRING        3

static UINT8                    AcpiGbl_LineScanState = PR_NORMAL_TEXT;

static void
PrGetNextLineInit (
    void)
{
    AcpiGbl_LineScanState = 0;
}

static UINT32
PrGetNextLine (
    FILE                    *Handle)
{
    UINT32                  i;
    int                     c = 0;
    int                     PreviousChar;


    /* Always clear the global line buffer */

    memset (AslGbl_CurrentLineBuffer, 0, AslGbl_LineBufferSize);
    for (i = 0; ;)
    {
        /*
         * If line is too long, expand the line buffers. Also increases
         * AslGbl_LineBufferSize.
         */
        if (i >= AslGbl_LineBufferSize)
        {
            UtExpandLineBuffers ();
        }

        PreviousChar = c;
        c = getc (Handle);
        if (c == EOF)
        {
            /*
             * On EOF: If there is anything in the line buffer, terminate
             * it with a newline, and catch the EOF on the next call
             * to this function.
             */
            if (i > 0)
            {
                AslGbl_CurrentLineBuffer[i] = '\n';
                return (AE_OK);
            }

            return (ASL_EOF);
        }

        /* Update state machine as necessary */

        switch (AcpiGbl_LineScanState)
        {
        case PR_NORMAL_TEXT:

            /* Check for multi-line comment start */

            if ((PreviousChar == '/') && (c == '*'))
            {
                AcpiGbl_LineScanState = PR_MULTI_LINE_COMMENT;
            }

            /* Check for single-line comment start */

            else if ((PreviousChar == '/') && (c == '/'))
            {
                AcpiGbl_LineScanState = PR_SINGLE_LINE_COMMENT;
            }

            /* Check for quoted string start */

            else if (PreviousChar == '"')
            {
                AcpiGbl_LineScanState = PR_QUOTED_STRING;
            }
            break;

        case PR_QUOTED_STRING:

            if (PreviousChar == '"')
            {
                AcpiGbl_LineScanState = PR_NORMAL_TEXT;
            }
            break;

        case PR_MULTI_LINE_COMMENT:

            /* Check for multi-line comment end */

            if ((PreviousChar == '*') && (c == '/'))
            {
                AcpiGbl_LineScanState = PR_NORMAL_TEXT;
            }
            break;

        case PR_SINGLE_LINE_COMMENT: /* Just ignore text until EOL */
        default:
            break;
        }

        /* Always copy the character into line buffer */

        AslGbl_CurrentLineBuffer[i] = (char) c;
        i++;

        /* Always exit on end-of-line */

        if (c == '\n')
        {
            /* Handle multi-line comments */

            if (AcpiGbl_LineScanState == PR_MULTI_LINE_COMMENT)
            {
                return (ASL_IGNORE_LINE);
            }

            /* End of single-line comment */

            if (AcpiGbl_LineScanState == PR_SINGLE_LINE_COMMENT)
            {
                AcpiGbl_LineScanState = PR_NORMAL_TEXT;
                return (AE_OK);
            }

            /* Blank line */

            if (i == 1)
            {
                return (ASL_IGNORE_LINE);
            }

            return (AE_OK);
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    PrMatchDirective
 *
 * PARAMETERS:  Directive           - Pointer to directive name token
 *
 * RETURN:      Index into command array, -1 if not found
 *
 * DESCRIPTION: Lookup the incoming directive in the known directives table.
 *
 ******************************************************************************/

static int
PrMatchDirective (
    char                    *Directive)
{
    int                     i;


    if (!Directive || Directive[0] == 0)
    {
        return (ASL_DIRECTIVE_NOT_FOUND);
    }

    for (i = 0; AslGbl_DirectiveInfo[i].Name; i++)
    {
        if (!strcmp (AslGbl_DirectiveInfo[i].Name, Directive))
        {
            return (i);
        }
    }

    return (ASL_DIRECTIVE_NOT_FOUND);    /* Command not recognized */
}


/*******************************************************************************
 *
 * FUNCTION:    PrPushDirective
 *
 * PARAMETERS:  Directive           - Encoded directive ID
 *              Argument            - String containing argument to the
 *                                    directive
 *
 * RETURN:      None
 *
 * DESCRIPTION: Push an item onto the directive stack. Used for processing
 *              nested #if/#else type conditional compilation directives.
 *              Specifically: Used on detection of #if/#ifdef/#ifndef to open
 *              a block.
 *
 ******************************************************************************/

static void
PrPushDirective (
    int                     Directive,
    char                    *Argument)
{
    DIRECTIVE_INFO          *Info;


    /* Allocate and populate a stack info item */

    Info = ACPI_CAST_PTR (DIRECTIVE_INFO,
        UtLocalCacheCalloc (sizeof (DIRECTIVE_INFO)));

    Info->Next = AslGbl_DirectiveStack;
    Info->Directive = Directive;
    Info->IgnoringThisCodeBlock = AslGbl_IgnoringThisCodeBlock;
    AcpiUtSafeStrncpy (Info->Argument, Argument, MAX_ARGUMENT_LENGTH);

    DbgPrint (ASL_DEBUG_OUTPUT,
        "Pr(%.4u) - [%u %s] %*s Pushed [#%s %s]: IgnoreFlag = %s\n",
        AslGbl_CurrentLineNumber, AslGbl_IfDepth,
        AslGbl_IgnoringThisCodeBlock ? "I" : "E",
        AslGbl_IfDepth * 4, " ",
        AslGbl_DirectiveInfo[Directive].Name,
        Argument, AslGbl_IgnoringThisCodeBlock ? "TRUE" : "FALSE");

    /* Push new item */

    AslGbl_DirectiveStack = Info;
    AslGbl_IfDepth++;
}


/*******************************************************************************
 *
 * FUNCTION:    PrPopDirective
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status. Error if the stack is empty.
 *
 * DESCRIPTION: Pop an item off the directive stack. Used for processing
 *              nested #if/#else type conditional compilation directives.
 *              Specifically: Used on detection of #elif and #endif to remove
 *              the original #if/#ifdef/#ifndef from the stack and close
 *              the block.
 *
 ******************************************************************************/

static ACPI_STATUS
PrPopDirective (
    void)
{
    DIRECTIVE_INFO          *Info;


    /* Check for empty stack */

    Info = AslGbl_DirectiveStack;
    if (!Info)
    {
        return (AE_ERROR);
    }

    /* Pop one item, keep globals up-to-date */

    AslGbl_IfDepth--;
    AslGbl_IgnoringThisCodeBlock = Info->IgnoringThisCodeBlock;
    AslGbl_DirectiveStack = Info->Next;

    DbgPrint (ASL_DEBUG_OUTPUT,
        "Pr(%.4u) - [%u %s] %*s Popped [#%s %s]: IgnoreFlag now = %s\n",
        AslGbl_CurrentLineNumber, AslGbl_IfDepth,
        AslGbl_IgnoringThisCodeBlock ? "I" : "E",
        AslGbl_IfDepth * 4, " ",
        AslGbl_DirectiveInfo[Info->Directive].Name,
        Info->Argument, AslGbl_IgnoringThisCodeBlock ? "TRUE" : "FALSE");

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    PrDbgPrint
 *
 * PARAMETERS:  Action              - Action being performed
 *              DirectiveName       - Directive being processed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Special debug print for directive processing.
 *
 ******************************************************************************/

static void
PrDbgPrint (
    char                    *Action,
    char                    *DirectiveName)
{

    DbgPrint (ASL_DEBUG_OUTPUT, "Pr(%.4u) - [%u %s] "
        "%*s %s #%s, IfDepth %u\n",
        AslGbl_CurrentLineNumber, AslGbl_IfDepth,
        AslGbl_IgnoringThisCodeBlock ? "I" : "E",
        AslGbl_IfDepth * 4, " ",
        Action, DirectiveName, AslGbl_IfDepth);
}


/*******************************************************************************
 *
 * FUNCTION:    PrDoIncludeFile
 *
 * PARAMETERS:  Pathname                - Name of the input file
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Open an include file, from #include.
 *
 ******************************************************************************/

static void
PrDoIncludeFile (
    char                    *Pathname)
{
    char                    *FullPathname;


    (void) PrOpenIncludeFile (Pathname, "r", &FullPathname);
}


/*******************************************************************************
 *
 * FUNCTION:    PrDoIncludeBuffer
 *
 * PARAMETERS:  Pathname                - Name of the input binary file
 *              BufferName              - ACPI namepath of the buffer
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Create an ACPI buffer object from a binary file. The contents
 *              of the file are emitted into the buffer object as ascii
 *              hex data. From #includebuffer.
 *
 ******************************************************************************/

static void
PrDoIncludeBuffer (
    char                    *Pathname,
    char                    *BufferName)
{
    char                    *FullPathname;
    FILE                    *BinaryBufferFile;
    UINT32                  i = 0;
    UINT8                   c;


    BinaryBufferFile = PrOpenIncludeFile (Pathname, "rb", &FullPathname);
    if (!BinaryBufferFile)
    {
        return;
    }

    /* Emit "Name (XXXX, Buffer() {" header */

    FlPrintFile (ASL_FILE_PREPROCESSOR, "Name (%s, Buffer()\n{", BufferName);

    /* Dump the entire file in ascii hex format */

    while (fread (&c, 1, 1, BinaryBufferFile))
    {
        if (!(i % 8))
        {
            FlPrintFile (ASL_FILE_PREPROCESSOR, "\n   ", c);
        }

        FlPrintFile (ASL_FILE_PREPROCESSOR, " 0x%2.2X,", c);
        i++;
    }

    DbgPrint (ASL_PARSE_OUTPUT, PR_PREFIX_ID
        "#includebuffer: read %u bytes from %s\n",
        AslGbl_CurrentLineNumber, i, FullPathname);

    /* Close the Name() operator */

    FlPrintFile (ASL_FILE_PREPROCESSOR, "\n})\n", BufferName);
    fclose (BinaryBufferFile);
}

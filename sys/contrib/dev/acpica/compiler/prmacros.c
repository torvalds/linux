/******************************************************************************
 *
 * Module Name: prmacros - Preprocessor #define macro support
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

#define _COMPONENT          ASL_PREPROCESSOR
        ACPI_MODULE_NAME    ("prmacros")


/*******************************************************************************
 *
 * FUNCTION:    PrDumpPredefinedNames
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the list of #defines. Used as the preprocessor starts, to
 *              display the names that were defined on the command line.
 *              Debug information only.
 *
 ******************************************************************************/

void
PrDumpPredefinedNames (
    void)
{
    PR_DEFINE_INFO          *DefineInfo;


    DefineInfo = AslGbl_DefineList;
    while (DefineInfo)
    {
        DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID
            "Predefined #define: %s->%s\n",
            0, DefineInfo->Identifier, DefineInfo->Replacement);

        DefineInfo = DefineInfo->Next;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    PrAddDefine
 *
 * PARAMETERS:  Identifier          - Name to be replaced
 *              Replacement         - Replacement for Identifier
 *              Persist             - Keep define across multiple compiles?
 *
 * RETURN:      A new define_info struct. NULL on error.
 *
 * DESCRIPTION: Add a new #define to the global list
 *
 ******************************************************************************/

PR_DEFINE_INFO *
PrAddDefine (
    char                    *Identifier,
    char                    *Replacement,
    BOOLEAN                 Persist)
{
    char                    *IdentifierString;
    char                    *ReplacementString;
    PR_DEFINE_INFO          *DefineInfo;


    if (!Replacement)
    {
        Replacement = "";
    }

    /* Check for already-defined first */

    DefineInfo = PrMatchDefine (Identifier);
    if (DefineInfo)
    {
        DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID,
            "#define: name already exists: %s\n",
            AslGbl_CurrentLineNumber, Identifier);

        /*
         * Name already exists. This is only an error if the target name
         * is different.
         */
        if (strcmp (Replacement, DefineInfo->Replacement))
        {
            PrError (ASL_ERROR, ASL_MSG_EXISTING_NAME,
                THIS_TOKEN_OFFSET (Identifier));

            return (NULL);
        }

        return (DefineInfo);
    }

    /* Copy input strings */

    IdentifierString = UtLocalCalloc (strlen (Identifier) + 1);
    strcpy (IdentifierString, Identifier);

    ReplacementString = UtLocalCalloc (strlen (Replacement) + 1);
    strcpy (ReplacementString, Replacement);

    /* Init and link new define info struct */

    DefineInfo = UtLocalCalloc (sizeof (PR_DEFINE_INFO));
    DefineInfo->Replacement = ReplacementString;
    DefineInfo->Identifier = IdentifierString;
    DefineInfo->Persist = Persist;

    if (AslGbl_DefineList)
    {
        AslGbl_DefineList->Previous = DefineInfo;
    }

    DefineInfo->Next = AslGbl_DefineList;
    AslGbl_DefineList = DefineInfo;
    return (DefineInfo);
}


/*******************************************************************************
 *
 * FUNCTION:    PrRemoveDefine
 *
 * PARAMETERS:  DefineName          - Name of define to be removed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Implements #undef. Remove a #define if found in the global
 *              list. No error if the target of the #undef does not exist,
 *              as per the C #undef definition.
 *
 ******************************************************************************/

void
PrRemoveDefine (
    char                    *DefineName)
{
    PR_DEFINE_INFO          *DefineInfo;


    /* Match name and delete the node */

    DefineInfo = AslGbl_DefineList;
    while (DefineInfo)
    {
        if (!strcmp (DefineName, DefineInfo->Identifier))
        {
            /* Remove from linked list */

            if (DefineInfo->Previous)
            {
                (DefineInfo->Previous)->Next = DefineInfo->Next;
            }
            else
            {
                AslGbl_DefineList = DefineInfo->Next;
            }

            if (DefineInfo->Next)
            {
                (DefineInfo->Next)->Previous = DefineInfo->Previous;
            }

            free (DefineInfo);
            return;
        }

        DefineInfo = DefineInfo->Next;
    }

    /*
     * Name was not found. By definition of #undef, this is not
     * an error, however.
     */
    DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID
        "#undef: could not find %s\n",
        AslGbl_CurrentLineNumber, DefineName);
}


/*******************************************************************************
 *
 * FUNCTION:    PrMatchDefine
 *
 * PARAMETERS:  MatchString         - Name associated with the #define
 *
 * RETURN:      Matched string if found. NULL otherwise.
 *
 * DESCRIPTION: Find a name in global #define list
 *
 ******************************************************************************/

PR_DEFINE_INFO *
PrMatchDefine (
    char                    *MatchString)
{
    PR_DEFINE_INFO          *DefineInfo;


    DefineInfo = AslGbl_DefineList;
    while (DefineInfo)
    {
        if (!strcmp (MatchString, DefineInfo->Identifier))
        {
            return (DefineInfo);
        }

        DefineInfo = DefineInfo->Next;
    }

    return (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    PrAddMacro
 *
 * PARAMETERS:  Name                - Start of the macro definition
 *              Next                - "Next" buffer from GetNextToken
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add a new macro to the list of #defines. Handles argument
 *              processing.
 *
 ******************************************************************************/

void
PrAddMacro (
    char                    *Name,
    char                    **Next)
{
    char                    *Token = NULL;
    ACPI_SIZE               TokenOffset;
    ACPI_SIZE               MacroBodyOffset;
    PR_DEFINE_INFO          *DefineInfo;
    PR_MACRO_ARG            *Args;
    char                    *Body;
    char                    *BodyInSource;
    UINT32                  i;
    UINT16                  UseCount = 0;
    UINT16                  ArgCount = 0;
    UINT32                  Depth = 1;
    UINT32                  EndOfArgList;
    char                    BufferChar;


    /* Find the end of the arguments list */

    TokenOffset = Name - AslGbl_MainTokenBuffer + strlen (Name) + 1;
    while (1)
    {
        BufferChar = AslGbl_CurrentLineBuffer[TokenOffset];
        if (BufferChar == '(')
        {
            Depth++;
        }
        else if (BufferChar == ')')
        {
            Depth--;
        }
        else if (BufferChar == 0)
        {
            PrError (ASL_ERROR, ASL_MSG_MACRO_SYNTAX, TokenOffset);
            return;
        }

        if (Depth == 0)
        {
            /* Found arg list end */

            EndOfArgList = TokenOffset;
            break;
        }

        TokenOffset++;
    }

    /* At this point, we know that we have a reasonable argument list */

    Args = UtLocalCalloc (sizeof (PR_MACRO_ARG) * PR_MAX_MACRO_ARGS);

    /* Get the macro argument names */

    for (i = 0; i < PR_MAX_MACRO_ARGS; i++)
    {
        Token = PrGetNextToken (NULL, PR_MACRO_SEPARATORS, Next);
        if (!Token)
        {
            /* This is the case for a NULL macro body */

            BodyInSource = "";
            goto AddMacroToList;
        }

        /* Don't go beyond the argument list */

        TokenOffset = Token - AslGbl_MainTokenBuffer + strlen (Token);
        if (TokenOffset > EndOfArgList)
        {
            break;
        }

        DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID
            "Macro arg: %s \n",
            AslGbl_CurrentLineNumber, Token);

        Args[i].Name = UtLocalCalloc (strlen (Token) + 1);
        strcpy (Args[i].Name, Token);

        Args[i].UseCount = 0;

        ArgCount++;
        if (ArgCount >= PR_MAX_MACRO_ARGS)
        {
            PrError (ASL_ERROR, ASL_MSG_TOO_MANY_ARGUMENTS, TokenOffset);
            goto ErrorExit;
        }
    }

    /* Get the macro body. Token now points to start of body */

    MacroBodyOffset = Token - AslGbl_MainTokenBuffer;

    /* Match each method arg in the macro body for later use */

    Token = PrGetNextToken (NULL, PR_MACRO_SEPARATORS, Next);
    while (Token)
    {
        /* Search the macro arg list for matching arg */

        for (i = 0; ((i < PR_MAX_MACRO_ARGS) && Args[i].Name); i++)
        {
            /*
             * Save argument offset within macro body. This is the mechanism
             * used to expand the macro upon invocation.
             *
             * Handles multiple instances of the same argument
             */
            if (!strcmp (Token, Args[i].Name))
            {
                UseCount = Args[i].UseCount;

                Args[i].Offset[UseCount] =
                    (Token - AslGbl_MainTokenBuffer) - MacroBodyOffset;

                DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID
                    "Macro Arg #%u: %s UseCount %u Offset %u \n",
                    AslGbl_CurrentLineNumber, i, Token,
                    UseCount+1, Args[i].Offset[UseCount]);

                Args[i].UseCount++;
                if (Args[i].UseCount >= PR_MAX_ARG_INSTANCES)
                {
                    PrError (ASL_ERROR, ASL_MSG_TOO_MANY_ARGUMENTS,
                        THIS_TOKEN_OFFSET (Token));

                    goto ErrorExit;
                }
                break;
            }
        }

        Token = PrGetNextToken (NULL, PR_MACRO_SEPARATORS, Next);
    }

    BodyInSource = &AslGbl_CurrentLineBuffer[MacroBodyOffset];


AddMacroToList:

    /* Check if name is already defined first */

    DefineInfo = PrMatchDefine (Name);
    if (DefineInfo)
    {
        DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID
            "#define: macro name already exists: %s\n",
            AslGbl_CurrentLineNumber, Name);

        /* Error only if not exactly the same macro */

        if (strcmp (DefineInfo->Body, BodyInSource) ||
            (DefineInfo->ArgCount != ArgCount))
        {
            PrError (ASL_ERROR, ASL_MSG_EXISTING_NAME,
                THIS_TOKEN_OFFSET (Name));
        }

        goto ErrorExit;
    }

    DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID
        "Macro body: %s \n",
        AslGbl_CurrentLineNumber, BodyInSource);

    /* Add macro to the #define list */

    DefineInfo = PrAddDefine (Name, BodyInSource, FALSE);
    if (DefineInfo)
    {
        Body = UtLocalCalloc (strlen (BodyInSource) + 1);
        strcpy (Body, BodyInSource);

        DefineInfo->Body = Body;
        DefineInfo->Args = Args;
        DefineInfo->ArgCount = ArgCount;
    }

    return;


ErrorExit:
    ACPI_FREE (Args);
    return;
}


/*******************************************************************************
 *
 * FUNCTION:    PrDoMacroInvocation
 *
 * PARAMETERS:  TokenBuffer         - Current line buffer
 *              MacroStart          - Start of the macro invocation within
 *                                    the token buffer
 *              DefineInfo          - Info for this macro
 *              Next                - "Next" buffer from GetNextToken
 *
 * RETURN:      None
 *
 * DESCRIPTION: Expand a macro invocation
 *
 ******************************************************************************/

void
PrDoMacroInvocation (
    char                    *TokenBuffer,
    char                    *MacroStart,
    PR_DEFINE_INFO          *DefineInfo,
    char                    **Next)
{
    PR_MACRO_ARG            *Args;
    char                    *Token = NULL;
    UINT32                  TokenOffset;
    UINT32                  Length;
    UINT32                  i;


    /* Take a copy of the macro body for expansion */

    strcpy (AslGbl_MacroTokenBuffer, DefineInfo->Body);

    /* Replace each argument within the prototype body */

    Args = DefineInfo->Args;
    if (!Args->Name)
    {
        /* This macro has no arguments */

        Token = PrGetNextToken (NULL, PR_MACRO_ARGUMENTS, Next);
        if (!Token)
        {
            goto BadInvocation;
        }

        TokenOffset = (MacroStart - TokenBuffer);
        Length = Token - MacroStart + strlen (Token) + 1;

        PrReplaceData (
            &AslGbl_CurrentLineBuffer[TokenOffset], Length,
            AslGbl_MacroTokenBuffer, strlen (AslGbl_MacroTokenBuffer));
        return;
    }

    while (Args->Name)
    {
        /* Get the next argument from macro invocation */

        Token = PrGetNextToken (NULL, PR_MACRO_SEPARATORS, Next);
        if (!Token)
        {
            goto BadInvocation;
        }

        /* Replace all instances of this argument */

        for (i = 0; i < Args->UseCount; i++)
        {
            /* Offset zero indicates "arg not used" */
            /* TBD: Not really needed now, with UseCount available */

            if (Args->Offset[i] == 0)
            {
                break;
            }

            PrReplaceData (
                &AslGbl_MacroTokenBuffer[Args->Offset[i]], strlen (Args->Name),
                Token, strlen (Token));

            DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID
                "ExpandArg: %s \n",
                AslGbl_CurrentLineNumber, AslGbl_MacroTokenBuffer);
        }

        Args++;
    }

    /* TBD: need to make sure macro was not invoked with too many arguments */

    if (!Token)
    {
        return;
    }

    /* Replace the entire macro invocation with the expanded macro */

    TokenOffset = (MacroStart - TokenBuffer);
    Length = Token - MacroStart + strlen (Token) + 1;

    PrReplaceData (
        &AslGbl_CurrentLineBuffer[TokenOffset], Length,
        AslGbl_MacroTokenBuffer, strlen (AslGbl_MacroTokenBuffer));

    return;


BadInvocation:
    PrError (ASL_ERROR, ASL_MSG_INVALID_INVOCATION,
        THIS_TOKEN_OFFSET (MacroStart));

    DbgPrint (ASL_DEBUG_OUTPUT, PR_PREFIX_ID
        "Bad macro invocation: %s \n",
        AslGbl_CurrentLineNumber, AslGbl_MacroTokenBuffer);
    return;
}

NoEcho('
/******************************************************************************
 *
 * Module Name: aslprimaries.y - Rules for primary ASL operators
 *                             - Keep this file synched with the
 *                               CvParseOpBlockType function in cvcompiler.c
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

')


/*******************************************************************************
 *
 * ASL Primary Terms
 *
 ******************************************************************************/

AccessAsTerm
    : PARSEOP_ACCESSAS
        PARSEOP_OPEN_PAREN
        AccessTypeKeyword
        OptionalAccessAttribTerm
        PARSEOP_CLOSE_PAREN         {$$ = TrCreateOp (PARSEOP_ACCESSAS,2,$3,$4);}
    | PARSEOP_ACCESSAS
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

AcquireTerm
    : PARSEOP_ACQUIRE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp(PARSEOP_ACQUIRE);}
        SuperName
        ',' WordConstExpr
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$6);}
    | PARSEOP_ACQUIRE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

AddTerm
    : PARSEOP_ADD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_ADD);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_ADD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

AliasTerm
    : PARSEOP_ALIAS
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_ALIAS);}
        NameString
        NameStringItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,
                                        TrSetOpFlags ($5, OP_IS_NAME_DECLARATION));}
    | PARSEOP_ALIAS
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

AndTerm
    : PARSEOP_AND
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_AND);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_AND
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ArgTerm
    : PARSEOP_ARG0                  {$$ = TrCreateLeafOp (PARSEOP_ARG0);}
    | PARSEOP_ARG1                  {$$ = TrCreateLeafOp (PARSEOP_ARG1);}
    | PARSEOP_ARG2                  {$$ = TrCreateLeafOp (PARSEOP_ARG2);}
    | PARSEOP_ARG3                  {$$ = TrCreateLeafOp (PARSEOP_ARG3);}
    | PARSEOP_ARG4                  {$$ = TrCreateLeafOp (PARSEOP_ARG4);}
    | PARSEOP_ARG5                  {$$ = TrCreateLeafOp (PARSEOP_ARG5);}
    | PARSEOP_ARG6                  {$$ = TrCreateLeafOp (PARSEOP_ARG6);}
    ;

BankFieldTerm
    : PARSEOP_BANKFIELD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_BANKFIELD);}
        NameString
        NameStringItem
        TermArgItem
        OptionalAccessTypeKeyword
        OptionalLockRuleKeyword
        OptionalUpdateRuleKeyword
        PARSEOP_CLOSE_PAREN '{'
            FieldUnitList '}'       {$$ = TrLinkOpChildren ($<n>3,7,
                                        $4,$5,$6,$7,$8,$9,$12);}
    | PARSEOP_BANKFIELD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN
        '{' error '}'               {$$ = AslDoError(); yyclearin;}
    ;

BreakTerm
    : PARSEOP_BREAK                 {$$ = TrCreateOp (PARSEOP_BREAK, 0);}
    ;

BreakPointTerm
    : PARSEOP_BREAKPOINT            {$$ = TrCreateOp (PARSEOP_BREAKPOINT, 0);}
    ;

BufferTerm
    : PARSEOP_BUFFER                {$<n>$ = TrCreateLeafOp (PARSEOP_BUFFER); COMMENT_CAPTURE_OFF; }
        OptionalDataCount
        '{' BufferTermData '}'      {$$ = TrLinkOpChildren ($<n>2,2,$3,$5); COMMENT_CAPTURE_ON;}
    ;

BufferTermData
    : ByteList                      {}
    | StringData                    {}
    ;

CaseTerm
    : PARSEOP_CASE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_CASE);}
        DataObject
        PARSEOP_CLOSE_PAREN '{'
            TermList '}'            {$$ = TrLinkOpChildren ($<n>3,2,$4,$7);}
    | PARSEOP_CASE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ConcatTerm
    : PARSEOP_CONCATENATE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_CONCATENATE);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_CONCATENATE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ConcatResTerm
    : PARSEOP_CONCATENATERESTEMPLATE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (
                                        PARSEOP_CONCATENATERESTEMPLATE);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_CONCATENATERESTEMPLATE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

CondRefOfTerm
    : PARSEOP_CONDREFOF
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_CONDREFOF);}
        CondRefOfSource
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_CONDREFOF
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ConnectionTerm
    : PARSEOP_CONNECTION
        PARSEOP_OPEN_PAREN
        NameString
        PARSEOP_CLOSE_PAREN         {$$ = TrCreateOp (PARSEOP_CONNECTION,1,$3);}
    | PARSEOP_CONNECTION
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_CONNECTION);}
        ResourceMacroTerm
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3, 1,
                                        TrLinkOpChildren (
                                            TrCreateLeafOp (PARSEOP_RESOURCETEMPLATE), 3,
                                            TrCreateLeafOp (PARSEOP_DEFAULT_ARG),
                                            TrCreateLeafOp (PARSEOP_DEFAULT_ARG),
                                            $4));}
    | PARSEOP_CONNECTION
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ContinueTerm
    : PARSEOP_CONTINUE              {$$ = TrCreateOp (PARSEOP_CONTINUE, 0);}
    ;

CopyObjectTerm
    : PARSEOP_COPYOBJECT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_COPYOBJECT);}
        TermArg
        ',' SimpleName
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,
                                        TrSetOpFlags ($6, OP_IS_TARGET));}
    | PARSEOP_COPYOBJECT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

CreateBitFieldTerm
    : PARSEOP_CREATEBITFIELD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_CREATEBITFIELD);}
        TermArg
        TermArgItem
        NameStringItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,
                                        TrSetOpFlags ($6, OP_IS_NAME_DECLARATION));}
    | PARSEOP_CREATEBITFIELD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

CreateByteFieldTerm
    : PARSEOP_CREATEBYTEFIELD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_CREATEBYTEFIELD);}
        TermArg
        TermArgItem
        NameStringItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,
                                        TrSetOpFlags ($6, OP_IS_NAME_DECLARATION));}
    | PARSEOP_CREATEBYTEFIELD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

CreateDWordFieldTerm
    : PARSEOP_CREATEDWORDFIELD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_CREATEDWORDFIELD);}
        TermArg
        TermArgItem
        NameStringItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,
                                        TrSetOpFlags ($6, OP_IS_NAME_DECLARATION));}
    | PARSEOP_CREATEDWORDFIELD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

CreateFieldTerm
    : PARSEOP_CREATEFIELD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_CREATEFIELD);}
        TermArg
        TermArgItem
        TermArgItem
        NameStringItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,4,$4,$5,$6,
                                        TrSetOpFlags ($7, OP_IS_NAME_DECLARATION));}
    | PARSEOP_CREATEFIELD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

CreateQWordFieldTerm
    : PARSEOP_CREATEQWORDFIELD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_CREATEQWORDFIELD);}
        TermArg
        TermArgItem
        NameStringItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,
                                        TrSetOpFlags ($6, OP_IS_NAME_DECLARATION));}
    | PARSEOP_CREATEQWORDFIELD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

CreateWordFieldTerm
    : PARSEOP_CREATEWORDFIELD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_CREATEWORDFIELD);}
        TermArg
        TermArgItem
        NameStringItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,
                                        TrSetOpFlags ($6, OP_IS_NAME_DECLARATION));}
    | PARSEOP_CREATEWORDFIELD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

DataRegionTerm
    : PARSEOP_DATATABLEREGION
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_DATATABLEREGION);}
        NameString
        TermArgItem
        TermArgItem
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,4,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),$5,$6,$7);}
    | PARSEOP_DATATABLEREGION
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

DebugTerm
    : PARSEOP_DEBUG                 {$$ = TrCreateLeafOp (PARSEOP_DEBUG);}
    ;

DecTerm
    : PARSEOP_DECREMENT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_DECREMENT);}
        SuperName
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_DECREMENT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

DefaultTerm
    : PARSEOP_DEFAULT '{'           {$<n>$ = TrCreateLeafOp (PARSEOP_DEFAULT);}
        TermList '}'                {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_DEFAULT '{'
        error '}'                   {$$ = AslDoError(); yyclearin;}
    ;

DerefOfTerm
    : PARSEOP_DEREFOF
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_DEREFOF);}
        DerefOfSource
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_DEREFOF
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

DeviceTerm
    : PARSEOP_DEVICE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_DEVICE);}
        NameString
        PARSEOP_CLOSE_PAREN '{'
            TermList '}'            {$$ = TrLinkOpChildren ($<n>3,2,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),$7);}
    | PARSEOP_DEVICE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

DivideTerm
    : PARSEOP_DIVIDE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_DIVIDE);}
        TermArg
        TermArgItem
        Target
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,4,$4,$5,$6,$7);}
    | PARSEOP_DIVIDE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

EISAIDTerm
    : PARSEOP_EISAID
        PARSEOP_OPEN_PAREN
        StringData
        PARSEOP_CLOSE_PAREN         {$$ = TrSetOpIntegerValue (PARSEOP_EISAID, $3);}
    | PARSEOP_EISAID
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ElseIfTerm
    : IfTerm ElseTerm               {$$ = TrLinkPeerOp ($1,$2);}
    ;

ElseTerm
    :                               {$$ = NULL;}
    | PARSEOP_ELSE '{'
        TermList           {$<n>$ = TrCreateLeafOp (PARSEOP_ELSE);}
        '}'                {$$ = TrLinkOpChildren ($<n>4,1,$3);}

    | PARSEOP_ELSE '{'
        error '}'                   {$$ = AslDoError(); yyclearin;}

    | PARSEOP_ELSE
        error                       {$$ = AslDoError(); yyclearin;}

    | PARSEOP_ELSEIF
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_ELSE);}
        TermArg                     {$<n>$ = TrCreateLeafOp (PARSEOP_IF);}
        PARSEOP_CLOSE_PAREN '{'
            TermList '}'            {TrLinkOpChildren ($<n>5,2,$4,$8);}
        ElseTerm                    {TrLinkPeerOp ($<n>5,$11);}
                                    {$$ = TrLinkOpChildren ($<n>3,1,$<n>5);}

    | PARSEOP_ELSEIF
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}

    | PARSEOP_ELSEIF
        error                       {$$ = AslDoError(); yyclearin;}
    ;

EventTerm
    : PARSEOP_EVENT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_EVENT);}
        NameString
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION));}
    | PARSEOP_EVENT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ExternalTerm
    : PARSEOP_EXTERNAL
        PARSEOP_OPEN_PAREN
        NameString
        OptionalObjectTypeKeyword
        OptionalParameterTypePackage
        OptionalParameterTypesPackage
        PARSEOP_CLOSE_PAREN         {$$ = TrCreateOp (PARSEOP_EXTERNAL,4,$3,$4,$5,$6);}
    | PARSEOP_EXTERNAL
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

FatalTerm
    : PARSEOP_FATAL
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_FATAL);}
        ByteConstExpr
        ',' DWordConstExpr
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$6,$7);}
    | PARSEOP_FATAL
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

FieldTerm
    : PARSEOP_FIELD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_FIELD);}
        NameString
        OptionalAccessTypeKeyword
        OptionalLockRuleKeyword
        OptionalUpdateRuleKeyword
        PARSEOP_CLOSE_PAREN '{'
            FieldUnitList '}'       {$$ = TrLinkOpChildren ($<n>3,5,$4,$5,$6,$7,$10);}
    | PARSEOP_FIELD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN
        '{' error '}'               {$$ = AslDoError(); yyclearin;}
    ;

FindSetLeftBitTerm
    : PARSEOP_FINDSETLEFTBIT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_FINDSETLEFTBIT);}
        TermArg
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_FINDSETLEFTBIT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

FindSetRightBitTerm
    : PARSEOP_FINDSETRIGHTBIT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_FINDSETRIGHTBIT);}
        TermArg
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_FINDSETRIGHTBIT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

    /* Convert a For() loop to a While() loop */
ForTerm
    : PARSEOP_FOR
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_WHILE);}
        OptionalTermArg ','         {}
        OptionalPredicate ','
        OptionalTermArg             {$<n>$ = TrLinkPeerOp ($4,$<n>3);
                                            TrSetOpParent ($9,$<n>3);}                /* New parent is WHILE */
        PARSEOP_CLOSE_PAREN
        '{' TermList '}'            {$<n>$ = TrLinkOpChildren ($<n>3,2,$7,$13);}
                                    {$<n>$ = TrLinkPeerOp ($13,$9);
                                        $$ = $<n>10;}
    ;

OptionalPredicate
    :                               {$$ = TrCreateValuedLeafOp (PARSEOP_INTEGER, 1);}
    | TermArg                       {$$ = $1;}
    ;

FprintfTerm
    : PARSEOP_FPRINTF
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_FPRINTF);}
        TermArg ','
        StringData
        PrintfArgList
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$6,$7);}
    | PARSEOP_FPRINTF
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

FromBCDTerm
    : PARSEOP_FROMBCD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_FROMBCD);}
        TermArg
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_FROMBCD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

FunctionTerm
    : PARSEOP_FUNCTION
        PARSEOP_OPEN_PAREN          {COMMENT_CAPTURE_OFF; $<n>$ = TrCreateLeafOp (PARSEOP_METHOD); }
        NameString
        OptionalParameterTypePackage
        OptionalParameterTypesPackage
        PARSEOP_CLOSE_PAREN '{'     {COMMENT_CAPTURE_ON; }
            TermList '}'            {$$ = TrLinkOpChildren ($<n>3,7,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),
                                        TrCreateValuedLeafOp (PARSEOP_BYTECONST, 0),
                                        TrCreateLeafOp (PARSEOP_SERIALIZERULE_NOTSERIAL),
                                        TrCreateValuedLeafOp (PARSEOP_BYTECONST, 0),$5,$6,$10);}
    | PARSEOP_FUNCTION
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

IfTerm
    : PARSEOP_IF
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_IF);}
        TermArg
        PARSEOP_CLOSE_PAREN '{'
            TermList '}'            {$$ = TrLinkOpChildren ($<n>3,2,$4,$7);}

    | PARSEOP_IF
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

IncludeTerm
    : PARSEOP_INCLUDE
        PARSEOP_OPEN_PAREN
        String
        PARSEOP_CLOSE_PAREN         {$$ = TrSetOpIntegerValue (PARSEOP_INCLUDE, $3);
                                        FlOpenIncludeFile ($3);}
    ;

IncludeEndTerm
    : PARSEOP_INCLUDE_END           {$<n>$ = TrCreateLeafOp (PARSEOP_INCLUDE_END);
                                        TrSetOpCurrentFilename ($$);}
    ;

IncTerm
    : PARSEOP_INCREMENT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_INCREMENT);}
        SuperName
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_INCREMENT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

IndexFieldTerm
    : PARSEOP_INDEXFIELD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_INDEXFIELD);}
        NameString
        NameStringItem
        OptionalAccessTypeKeyword
        OptionalLockRuleKeyword
        OptionalUpdateRuleKeyword
        PARSEOP_CLOSE_PAREN '{'
            FieldUnitList '}'       {$$ = TrLinkOpChildren ($<n>3,6,$4,$5,$6,$7,$8,$11);}
    | PARSEOP_INDEXFIELD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN
        '{' error '}'               {$$ = AslDoError(); yyclearin;}
    ;

IndexTerm
    : PARSEOP_INDEX
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_INDEX);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_INDEX
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LAndTerm
    : PARSEOP_LAND
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LAND);}
        TermArg
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_LAND
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LEqualTerm
    : PARSEOP_LEQUAL
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LEQUAL);}
        TermArg
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_LEQUAL
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LGreaterEqualTerm
    : PARSEOP_LGREATEREQUAL
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LLESS);}
        TermArg
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrCreateOp (PARSEOP_LNOT, 1,
                                        TrLinkOpChildren ($<n>3,2,$4,$5));}
    | PARSEOP_LGREATEREQUAL
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LGreaterTerm
    : PARSEOP_LGREATER
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LGREATER);}
        TermArg
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_LGREATER
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LLessEqualTerm
    : PARSEOP_LLESSEQUAL
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LGREATER);}
        TermArg
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrCreateOp (PARSEOP_LNOT, 1,
                                        TrLinkOpChildren ($<n>3,2,$4,$5));}
    | PARSEOP_LLESSEQUAL
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LLessTerm
    : PARSEOP_LLESS
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LLESS);}
        TermArg
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_LLESS
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LNotEqualTerm
    : PARSEOP_LNOTEQUAL
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LEQUAL);}
        TermArg
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrCreateOp (PARSEOP_LNOT, 1,
                                        TrLinkOpChildren ($<n>3,2,$4,$5));}
    | PARSEOP_LNOTEQUAL
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LNotTerm
    : PARSEOP_LNOT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LNOT);}
        TermArg
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_LNOT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LoadTableTerm
    : PARSEOP_LOADTABLE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LOADTABLE);}
        TermArg
        TermArgItem
        TermArgItem
        OptionalListString
        OptionalListString
        OptionalReference
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,6,$4,$5,$6,$7,$8,$9);}
    | PARSEOP_LOADTABLE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LoadTerm
    : PARSEOP_LOAD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LOAD);}
        NameString
        RequiredTarget
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_LOAD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

LocalTerm
    : PARSEOP_LOCAL0                {$$ = TrCreateLeafOp (PARSEOP_LOCAL0);}
    | PARSEOP_LOCAL1                {$$ = TrCreateLeafOp (PARSEOP_LOCAL1);}
    | PARSEOP_LOCAL2                {$$ = TrCreateLeafOp (PARSEOP_LOCAL2);}
    | PARSEOP_LOCAL3                {$$ = TrCreateLeafOp (PARSEOP_LOCAL3);}
    | PARSEOP_LOCAL4                {$$ = TrCreateLeafOp (PARSEOP_LOCAL4);}
    | PARSEOP_LOCAL5                {$$ = TrCreateLeafOp (PARSEOP_LOCAL5);}
    | PARSEOP_LOCAL6                {$$ = TrCreateLeafOp (PARSEOP_LOCAL6);}
    | PARSEOP_LOCAL7                {$$ = TrCreateLeafOp (PARSEOP_LOCAL7);}
    ;

LOrTerm
    : PARSEOP_LOR
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_LOR);}
        TermArg
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_LOR
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

MatchTerm
    : PARSEOP_MATCH
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_MATCH);}
        TermArg
        ',' MatchOpKeyword
        TermArgItem
        ',' MatchOpKeyword
        TermArgItem
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,6,$4,$6,$7,$9,$10,$11);}
    | PARSEOP_MATCH
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

MethodTerm
    : PARSEOP_METHOD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_METHOD); COMMENT_CAPTURE_OFF;}
        NameString
        OptionalByteConstExpr       {UtCheckIntegerRange ($5, 0, 7);}
        OptionalSerializeRuleKeyword
        OptionalByteConstExpr
        OptionalParameterTypePackage
        OptionalParameterTypesPackage
        PARSEOP_CLOSE_PAREN '{'     {COMMENT_CAPTURE_ON;}
            TermList '}'            {$$ = TrLinkOpChildren ($<n>3,7,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),
                                        $5,$7,$8,$9,$10,$14);}
    | PARSEOP_METHOD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

MidTerm
    : PARSEOP_MID
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_MID);}
        TermArg
        TermArgItem
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,4,$4,$5,$6,$7);}
    | PARSEOP_MID
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ModTerm
    : PARSEOP_MOD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_MOD);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_MOD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

MultiplyTerm
    : PARSEOP_MULTIPLY
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_MULTIPLY);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_MULTIPLY
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

MutexTerm
    : PARSEOP_MUTEX
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_MUTEX);}
        NameString
        OptionalSyncLevel
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),$5);}
    | PARSEOP_MUTEX
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

NameTerm
    : PARSEOP_NAME
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_NAME);}
        NameString
        ',' DataObject
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),$6);}
    | PARSEOP_NAME
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

NAndTerm
    : PARSEOP_NAND
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_NAND);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_NAND
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

NoOpTerm
    : PARSEOP_NOOP                  {$$ = TrCreateOp (PARSEOP_NOOP, 0);}
    ;

NOrTerm
    : PARSEOP_NOR
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_NOR);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_NOR
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

NotifyTerm
    : PARSEOP_NOTIFY
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_NOTIFY);}
        SuperName
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_NOTIFY
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

NotTerm
    : PARSEOP_NOT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_NOT);}
        TermArg
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_NOT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ObjectTypeTerm
    : PARSEOP_OBJECTTYPE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_OBJECTTYPE);}
        ObjectTypeSource
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_OBJECTTYPE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

OffsetTerm
    : PARSEOP_OFFSET
        PARSEOP_OPEN_PAREN
        AmlPackageLengthTerm
        PARSEOP_CLOSE_PAREN         {$$ = TrCreateOp (PARSEOP_OFFSET,1,$3);}
    | PARSEOP_OFFSET
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

OpRegionTerm
    : PARSEOP_OPERATIONREGION
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_OPERATIONREGION);}
        NameString
        ',' OpRegionSpaceIdTerm
        TermArgItem
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,4,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),
                                        $6,$7,$8);}
    | PARSEOP_OPERATIONREGION
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

OpRegionSpaceIdTerm
    : RegionSpaceKeyword            {}
    | ByteConst                     {$$ = UtCheckIntegerRange ($1, 0x80, 0xFF);}
    ;

OrTerm
    : PARSEOP_OR
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_OR);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_OR
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

PackageTerm
    : PARSEOP_PACKAGE               {$<n>$ = TrCreateLeafOp (PARSEOP_VAR_PACKAGE);}
        OptionalDataCount
        '{' PackageList '}'         {$$ = TrLinkOpChildren ($<n>2,2,$3,$5);}

PowerResTerm
    : PARSEOP_POWERRESOURCE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_POWERRESOURCE);}
        NameString
        ',' ByteConstExpr
        ',' WordConstExpr
        PARSEOP_CLOSE_PAREN '{'
            TermList '}'            {$$ = TrLinkOpChildren ($<n>3,4,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),
                                        $6,$8,$11);}
    | PARSEOP_POWERRESOURCE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

PrintfTerm
    : PARSEOP_PRINTF
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_PRINTF);}
        StringData
        PrintfArgList
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_PRINTF
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

PrintfArgList
    :                               {$$ = NULL;}
    | TermArg                       {$$ = $1;}
    | PrintfArgList ','
       TermArg                      {$$ = TrLinkPeerOp ($1, $3);}
    ;

ProcessorTerm
    : PARSEOP_PROCESSOR
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_PROCESSOR);}
        NameString
        ',' ByteConstExpr
        OptionalDWordConstExpr
        OptionalByteConstExpr
        PARSEOP_CLOSE_PAREN '{'
            TermList '}'            {$$ = TrLinkOpChildren ($<n>3,5,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),
                                        $6,$7,$8,$11);}
    | PARSEOP_PROCESSOR
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

RawDataBufferTerm
    : PARSEOP_DATABUFFER
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_DATABUFFER);}
        OptionalWordConst
        PARSEOP_CLOSE_PAREN '{'
            ByteList '}'            {$$ = TrLinkOpChildren ($<n>3,2,$4,$7);}
    | PARSEOP_DATABUFFER
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

/*
 * In RefOf, the node isn't really a target, but we can't keep track of it after
 * we've taken a pointer to it. (hard to tell if a local becomes initialized this way.)
 */
RefOfTerm
    : PARSEOP_REFOF
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_REFOF);}
        RefOfSource
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,
                                        TrSetOpFlags ($4, OP_IS_TARGET));}
    | PARSEOP_REFOF
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ReleaseTerm
    : PARSEOP_RELEASE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_RELEASE);}
        SuperName
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_RELEASE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ResetTerm
    : PARSEOP_RESET
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_RESET);}
        SuperName
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_RESET
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ReturnTerm
    : PARSEOP_RETURN
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_RETURN);}
        OptionalReturnArg
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_RETURN                {$$ = TrLinkOpChildren (
                                        TrCreateLeafOp (PARSEOP_RETURN),1,
                                        TrSetOpFlags (TrCreateLeafOp (PARSEOP_ZERO),
                                            OP_IS_NULL_RETURN));}
    | PARSEOP_RETURN
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ScopeTerm
    : PARSEOP_SCOPE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_SCOPE);}
        NameString
        PARSEOP_CLOSE_PAREN '{'
            TermList '}'            {$$ = TrLinkOpChildren ($<n>3,2,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),$7);}
    | PARSEOP_SCOPE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ShiftLeftTerm
    : PARSEOP_SHIFTLEFT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_SHIFTLEFT);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_SHIFTLEFT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ShiftRightTerm
    : PARSEOP_SHIFTRIGHT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_SHIFTRIGHT);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_SHIFTRIGHT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

SignalTerm
    : PARSEOP_SIGNAL
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_SIGNAL);}
        SuperName
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_SIGNAL
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

SizeOfTerm
    : PARSEOP_SIZEOF
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_SIZEOF);}
        SuperName
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_SIZEOF
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

SleepTerm
    : PARSEOP_SLEEP
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_SLEEP);}
        TermArg
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_SLEEP
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

StallTerm
    : PARSEOP_STALL
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_STALL);}
        TermArg
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_STALL
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

StoreTerm
    : PARSEOP_STORE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_STORE);}
        TermArg
        ',' SuperName
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,
                                            TrSetOpFlags ($6, OP_IS_TARGET));}
    | PARSEOP_STORE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

SubtractTerm
    : PARSEOP_SUBTRACT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_SUBTRACT);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_SUBTRACT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

SwitchTerm
    : PARSEOP_SWITCH
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_SWITCH);}
        TermArg
        PARSEOP_CLOSE_PAREN '{'
            CaseDefaultTermList '}' {$$ = TrLinkOpChildren ($<n>3,2,$4,$7);}
    | PARSEOP_SWITCH
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ThermalZoneTerm
    : PARSEOP_THERMALZONE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_THERMALZONE);}
        NameString
        PARSEOP_CLOSE_PAREN '{'
            TermList '}'            {$$ = TrLinkOpChildren ($<n>3,2,
                                        TrSetOpFlags ($4, OP_IS_NAME_DECLARATION),$7);}
    | PARSEOP_THERMALZONE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

TimerTerm
    : PARSEOP_TIMER
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_TIMER);}
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,0);}
    | PARSEOP_TIMER                 {$$ = TrLinkOpChildren (
                                        TrCreateLeafOp (PARSEOP_TIMER),0);}
    | PARSEOP_TIMER
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ToBCDTerm
    : PARSEOP_TOBCD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_TOBCD);}
        TermArg
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_TOBCD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ToBufferTerm
    : PARSEOP_TOBUFFER
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_TOBUFFER);}
        TermArg
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_TOBUFFER
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ToDecimalStringTerm
    : PARSEOP_TODECIMALSTRING
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_TODECIMALSTRING);}
        TermArg
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_TODECIMALSTRING
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ToHexStringTerm
    : PARSEOP_TOHEXSTRING
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_TOHEXSTRING);}
        TermArg
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_TOHEXSTRING
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ToIntegerTerm
    : PARSEOP_TOINTEGER
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_TOINTEGER);}
        TermArg
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_TOINTEGER
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ToPLDTerm
    : PARSEOP_TOPLD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_TOPLD);}
        PldKeywordList
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_TOPLD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

PldKeywordList
    :                               {$$ = NULL;}
    | PldKeyword
        PARSEOP_EXP_EQUALS Integer  {$$ = TrLinkOpChildren ($1,1,$3);}
    | PldKeyword
        PARSEOP_EXP_EQUALS String   {$$ = TrLinkOpChildren ($1,1,$3);}
    | PldKeywordList ','            /* Allows a trailing comma at list end */
    | PldKeywordList ','
        PldKeyword
        PARSEOP_EXP_EQUALS Integer  {$$ = TrLinkPeerOp ($1,TrLinkOpChildren ($3,1,$5));}
    | PldKeywordList ','
        PldKeyword
        PARSEOP_EXP_EQUALS String   {$$ = TrLinkPeerOp ($1,TrLinkOpChildren ($3,1,$5));}
    ;


ToStringTerm
    : PARSEOP_TOSTRING
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_TOSTRING);}
        TermArg
        OptionalCount
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_TOSTRING
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

ToUUIDTerm
    : PARSEOP_TOUUID
        PARSEOP_OPEN_PAREN
        StringData
        PARSEOP_CLOSE_PAREN         {$$ = TrSetOpIntegerValue (PARSEOP_TOUUID, $3);}
    | PARSEOP_TOUUID
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

UnicodeTerm
    : PARSEOP_UNICODE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_UNICODE);}
        StringData
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,0,$4);}
    | PARSEOP_UNICODE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

UnloadTerm
    : PARSEOP_UNLOAD
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_UNLOAD);}
        SuperName
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,1,$4);}
    | PARSEOP_UNLOAD
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

WaitTerm
    : PARSEOP_WAIT
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_WAIT);}
        SuperName
        TermArgItem
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,2,$4,$5);}
    | PARSEOP_WAIT
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

XOrTerm
    : PARSEOP_XOR
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_XOR);}
        TermArg
        TermArgItem
        Target
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkOpChildren ($<n>3,3,$4,$5,$6);}
    | PARSEOP_XOR
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

WhileTerm
    : PARSEOP_WHILE
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_WHILE);}
        TermArg
        PARSEOP_CLOSE_PAREN
            '{' TermList '}'        {$$ = TrLinkOpChildren ($<n>3,2,$4,$7);}
    | PARSEOP_WHILE
        PARSEOP_OPEN_PAREN
        error PARSEOP_CLOSE_PAREN   {$$ = AslDoError(); yyclearin;}
    ;

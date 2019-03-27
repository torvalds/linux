NoEcho('
/******************************************************************************
 *
 * Module Name: aslrules.y - Main Bison/Yacc production rules
 *                         - Keep this file synched with the
 *                           CvParseOpBlockType function in cvcompiler.c
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
 * ASL Root and Secondary Terms
 *
 ******************************************************************************/

/*
 * Root term. Allow multiple #line directives before the definition block
 * to handle output from preprocessors
 */
AslCode
    : DefinitionBlockList           {$<n>$ = TrLinkOpChildren (
                                        TrCreateLeafOp (PARSEOP_ASL_CODE),1, $1);}
    | error                         {YYABORT; $$ = NULL;}
    ;


/*
 * Note concerning support for "module-level code".
 *
 * ACPI 1.0 allowed Type1 and Type2 executable opcodes outside of control
 * methods (the so-called module-level code.) This support was explicitly
 * removed in ACPI 2.0, but this type of code continues to be created by
 * BIOS vendors. In order to support the disassembly and recompilation of
 * such code (and the porting of ASL code to iASL), iASL supports this
 * code in violation of the current ACPI specification.
 *
 * The grammar change to support module-level code is to revert the
 * {ObjectList} portion of the DefinitionBlockTerm in ACPI 2.0 to the
 * original use of {TermList} instead (see below.) This allows the use
 * of Type1 and Type2 opcodes at module level.
 *
 * 04/2016: The module-level code is now allowed in the following terms:
 * DeviceTerm, PowerResTerm, ProcessorTerm, ScopeTerm, ThermalZoneTerm.
 * The ObjectList term is obsolete and has been removed.
 */
DefinitionBlockTerm
    : PARSEOP_DEFINITION_BLOCK
        PARSEOP_OPEN_PAREN          {$<n>$ = TrCreateLeafOp (PARSEOP_DEFINITION_BLOCK); COMMENT_CAPTURE_OFF;}
        String ','
        String ','
        ByteConst ','
        String ','
        String ','
        DWordConst
        PARSEOP_CLOSE_PAREN         {TrSetOpIntegerWidth ($6,$8);
                                        TrSetOpEndLineNumber ($<n>3); COMMENT_CAPTURE_ON;}
            '{' TermList '}'        {$$ = TrLinkOpChildren ($<n>3,7,
                                        $4,$6,$8,$10,$12,$14,$18);}
    ;

DefinitionBlockList
    : DefinitionBlockTerm
    | DefinitionBlockTerm
        DefinitionBlockList         {$$ = TrLinkPeerOps (2, $1,$2);}
    ;


/******* Basic ASCII identifiers **************************************************/

/* Allow IO, DMA, IRQ Resource macro and FOR macro names to also be used as identifiers */

NameString
    : NameSeg                       {}
    | PARSEOP_NAMESTRING            {$$ = TrCreateValuedLeafOp (PARSEOP_NAMESTRING, (ACPI_NATIVE_INT) $1);}
    | PARSEOP_IO                    {$$ = TrCreateValuedLeafOp (PARSEOP_NAMESTRING, (ACPI_NATIVE_INT) "IO");}
    | PARSEOP_DMA                   {$$ = TrCreateValuedLeafOp (PARSEOP_NAMESTRING, (ACPI_NATIVE_INT) "DMA");}
    | PARSEOP_IRQ                   {$$ = TrCreateValuedLeafOp (PARSEOP_NAMESTRING, (ACPI_NATIVE_INT) "IRQ");}
    | PARSEOP_FOR                   {$$ = TrCreateValuedLeafOp (PARSEOP_NAMESTRING, (ACPI_NATIVE_INT) "FOR");}
    ;
/*
NameSeg
    : PARSEOP_NAMESEG               {$$ = TrCreateValuedLeafOp (PARSEOP_NAMESEG, (ACPI_NATIVE_INT)
                                        TrNormalizeNameSeg ($1));}
    ;
*/

NameSeg
    : PARSEOP_NAMESEG               {$$ = TrCreateValuedLeafOp (PARSEOP_NAMESEG,
                                        (ACPI_NATIVE_INT) AslCompilerlval.s);}
    ;


/******* Fundamental argument/statement types ***********************************/

Term
    : Object                        {}
    | Type1Opcode                   {}
    | Type2Opcode                   {}
    | Type2IntegerOpcode            {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);}
    | Type2StringOpcode             {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);}
    | Type2BufferOpcode             {}
    | Type2BufferOrStringOpcode     {}
    | error                         {$$ = AslDoError(); yyclearin;}
    ;

SuperName
    : SimpleName                    {}
    | DebugTerm                     {}
    | Type6Opcode                   {}
    ;

Target
    :                               {$$ = TrCreateNullTargetOp ();} /* Placeholder is a ZeroOp object */
    | ','                           {$$ = TrCreateNullTargetOp ();} /* Placeholder is a ZeroOp object */
    | ',' SuperName                 {$$ = TrSetOpFlags ($2, OP_IS_TARGET);}
    ;

RequiredTarget
    : ',' SuperName                 {$$ = TrSetOpFlags ($2, OP_IS_TARGET);}
    ;

TermArg
    : SimpleName                    {$$ = TrSetOpFlags ($1, OP_IS_TERM_ARG);}
    | Type2Opcode                   {$$ = TrSetOpFlags ($1, OP_IS_TERM_ARG);}
    | DataObject                    {$$ = TrSetOpFlags ($1, OP_IS_TERM_ARG);}
    | PARSEOP_OPEN_PAREN
        TermArg
        PARSEOP_CLOSE_PAREN         {$$ = TrSetOpFlags ($2, OP_IS_TERM_ARG);}
    ;

/*
 NOTE: Removed from TermArg due to reduce/reduce conflicts:
    | Type2IntegerOpcode            {$$ = TrSetOpFlags ($1, OP_IS_TERM_ARG);}
    | Type2StringOpcode             {$$ = TrSetOpFlags ($1, OP_IS_TERM_ARG);}
    | Type2BufferOpcode             {$$ = TrSetOpFlags ($1, OP_IS_TERM_ARG);}
    | Type2BufferOrStringOpcode     {$$ = TrSetOpFlags ($1, OP_IS_TERM_ARG);}

*/

MethodInvocationTerm
    : NameString
        PARSEOP_OPEN_PAREN          {TrSetOpIntegerValue (PARSEOP_METHODCALL, $1); COMMENT_CAPTURE_OFF;}
        ArgList
        PARSEOP_CLOSE_PAREN         {$$ = TrLinkChildOp ($1,$4); COMMENT_CAPTURE_ON;}
    ;

/* OptionalCount must appear before ByteList or an incorrect reduction will result */

OptionalCount
    :                               {$$ = TrCreateLeafOp (PARSEOP_ONES);}       /* Placeholder is a OnesOp object */
    | ','                           {$$ = TrCreateLeafOp (PARSEOP_ONES);}       /* Placeholder is a OnesOp object */
    | ',' TermArg                   {$$ = $2;}
    ;

/*
 * Data count for buffers and packages (byte count for buffers,
 * element count for packages).
 */
OptionalDataCount

        /* Legacy ASL */
    :                               {$$ = NULL;}
    | PARSEOP_OPEN_PAREN
        TermArg
        PARSEOP_CLOSE_PAREN         {$$ = $2;}
    | PARSEOP_OPEN_PAREN
        PARSEOP_CLOSE_PAREN         {$$ = NULL;}

        /* C-style (ASL+) -- adds equals term */

    |  PARSEOP_EXP_EQUALS           {$$ = NULL;}

    | PARSEOP_OPEN_PAREN
        TermArg
        PARSEOP_CLOSE_PAREN
        PARSEOP_EXP_EQUALS          {$$ = $2;}

    | PARSEOP_OPEN_PAREN
        PARSEOP_CLOSE_PAREN
        String
        PARSEOP_EXP_EQUALS          {$$ = NULL;}
    ;


/******* List Terms **************************************************/

    /* ACPI 3.0 -- allow semicolons between terms */

TermList
    :                               {$$ = NULL;}
    | TermList Term                 {$$ = TrLinkPeerOp (
                                        TrSetOpFlags ($1, OP_RESULT_NOT_USED),$2);}
    | TermList Term ';'             {$$ = TrLinkPeerOp (
                                        TrSetOpFlags ($1, OP_RESULT_NOT_USED),$2);}
    | TermList ';' Term             {$$ = TrLinkPeerOp (
                                        TrSetOpFlags ($1, OP_RESULT_NOT_USED),$3);}
    | TermList ';' Term ';'         {$$ = TrLinkPeerOp (
                                        TrSetOpFlags ($1, OP_RESULT_NOT_USED),$3);}
    ;

ArgList
    :                               {$$ = NULL;}
    | TermArg
    | ArgList ','                   /* Allows a trailing comma at list end */
    | ArgList ','
        TermArg                     {$$ = TrLinkPeerOp ($1,$3);}
    ;

ByteList
    :                               {$$ = NULL;}
    | ByteConstExpr
    | ByteList ','                  /* Allows a trailing comma at list end */
    | ByteList ','
        ByteConstExpr               {$$ = TrLinkPeerOp ($1,$3);}
    ;

DWordList
    :                               {$$ = NULL;}
    | DWordConstExpr
    | DWordList ','                 /* Allows a trailing comma at list end */
    | DWordList ','
        DWordConstExpr              {$$ = TrLinkPeerOp ($1,$3);}
    ;

FieldUnitList
    :                               {$$ = NULL;}
    | FieldUnit
    | FieldUnitList ','             /* Allows a trailing comma at list end */
    | FieldUnitList ','
        FieldUnit                   {$$ = TrLinkPeerOp ($1,$3);}
    ;

FieldUnit
    : FieldUnitEntry                {}
    | OffsetTerm                    {}
    | AccessAsTerm                  {}
    | ConnectionTerm                {}
    ;

FieldUnitEntry
    : ',' AmlPackageLengthTerm      {$$ = TrCreateOp (PARSEOP_RESERVED_BYTES,1,$2);}
    | NameSeg ','
        AmlPackageLengthTerm        {$$ = TrLinkChildOp ($1,$3);}
    ;

Object
    : CompilerDirective             {}
    | NamedObject                   {}
    | NameSpaceModifier             {}
/*    | StructureTerm                 {} */
    ;

PackageList
    :                               {$$ = NULL;}
    | PackageElement
    | PackageList ','               /* Allows a trailing comma at list end */
    | PackageList ','
        PackageElement              {$$ = TrLinkPeerOp ($1,$3);}
    ;

PackageElement
    : DataObject                    {}
    | NameString                    {}
    ;

    /* Rules for specifying the type of one method argument or return value */

ParameterTypePackage
    :                               {$$ = NULL;}
    | ObjectTypeKeyword             {$$ = $1;}
    | ParameterTypePackage ','
        ObjectTypeKeyword           {$$ = TrLinkPeerOps (2,$1,$3);}
    ;

ParameterTypePackageList
    :                               {$$ = NULL;}
    | ObjectTypeKeyword             {$$ = $1;}
    | '{' ParameterTypePackage '}'  {$$ = $2;}
    ;

OptionalParameterTypePackage
    :                               {$$ = TrCreateLeafOp (PARSEOP_DEFAULT_ARG);}
    | ',' ParameterTypePackageList  {$$ = TrLinkOpChildren (
                                        TrCreateLeafOp (PARSEOP_DEFAULT_ARG),1,$2);}
    ;

    /* Rules for specifying the types for method arguments */

ParameterTypesPackage
    : ParameterTypePackageList      {$$ = $1;}
    | ParameterTypesPackage ','
        ParameterTypePackageList    {$$ = TrLinkPeerOps (2,$1,$3);}
    ;

ParameterTypesPackageList
    :                               {$$ = NULL;}
    | ObjectTypeKeyword             {$$ = $1;}
    | '{' ParameterTypesPackage '}' {$$ = $2;}
    ;

OptionalParameterTypesPackage
    :                               {$$ = TrCreateLeafOp (PARSEOP_DEFAULT_ARG);}
    | ',' ParameterTypesPackageList {$$ = TrLinkOpChildren (
                                        TrCreateLeafOp (PARSEOP_DEFAULT_ARG),1,$2);}
    ;

/*
 * Case-Default list; allow only one Default term and unlimited Case terms
 */
CaseDefaultTermList
    :                               {$$ = NULL;}
    | CaseTerm                      {}
    | DefaultTerm                   {}
    | CaseDefaultTermList
        CaseTerm                    {$$ = TrLinkPeerOp ($1,$2);}
    | CaseDefaultTermList
        DefaultTerm                 {$$ = TrLinkPeerOp ($1,$2);}

/* Original - attempts to force zero or one default term within the switch */

/*
CaseDefaultTermList
    :                               {$$ = NULL;}
    | CaseTermList
        DefaultTerm
        CaseTermList                {$$ = TrLinkPeerOp ($1,TrLinkPeerOp ($2, $3));}
    | CaseTermList
        CaseTerm                    {$$ = TrLinkPeerOp ($1,$2);}
    ;

CaseTermList
    :                               {$$ = NULL;}
    | CaseTerm                      {}
    | CaseTermList
        CaseTerm                    {$$ = TrLinkPeerOp ($1,$2);}
    ;
*/


/*******************************************************************************
 *
 * ASL Data and Constant Terms
 *
 ******************************************************************************/

DataObject
    : BufferData                    {}
    | PackageData                   {}
    | IntegerData                   {}
    | StringData                    {}
    ;

BufferData
    : Type5Opcode                   {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);}
    | Type2BufferOrStringOpcode     {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);}
    | Type2BufferOpcode             {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);}
    | BufferTerm                    {}
    ;

PackageData
    : PackageTerm                   {}
    ;

IntegerData
    : Type2IntegerOpcode            {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);}
    | Type3Opcode                   {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);}
    | Integer                       {}
    | ConstTerm                     {}
    ;

StringData
    : Type2StringOpcode             {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);}
    | String                        {}
    ;

ByteConst
    : Integer                       {$$ = TrSetOpIntegerValue (PARSEOP_BYTECONST, $1);}
    ;

WordConst
    : Integer                       {$$ = TrSetOpIntegerValue (PARSEOP_WORDCONST, $1);}
    ;

DWordConst
    : Integer                       {$$ = TrSetOpIntegerValue (PARSEOP_DWORDCONST, $1);}
    ;

QWordConst
    : Integer                       {$$ = TrSetOpIntegerValue (PARSEOP_QWORDCONST, $1);}
    ;

/*
 * The OP_COMPILE_TIME_CONST flag in the following constant expressions
 * enables compile-time constant folding to reduce the Type3Opcodes/Type2IntegerOpcodes
 * to simple integers. It is an error if these types of expressions cannot be
 * reduced, since the AML grammar for ****ConstExpr requires a simple constant.
 * Note: The required byte length of the constant is passed through to the
 * constant folding code in the node AmlLength field.
 */
ByteConstExpr
    : Type3Opcode                   {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);
                                        TrSetOpAmlLength ($1, 1);}
    | Type2IntegerOpcode            {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);
                                        TrSetOpAmlLength ($1, 1);}
    | ConstExprTerm                 {$$ = TrSetOpIntegerValue (PARSEOP_BYTECONST, $1);}
    | ByteConst                     {}
    ;

WordConstExpr
    : Type3Opcode                   {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);
                                        TrSetOpAmlLength ($1, 2);}
    | Type2IntegerOpcode            {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);
                                        TrSetOpAmlLength ($1, 2);}
    | ConstExprTerm                 {$$ = TrSetOpIntegerValue (PARSEOP_WORDCONST, $1);}
    | WordConst                     {}
    ;

DWordConstExpr
    : Type3Opcode                   {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);
                                        TrSetOpAmlLength ($1, 4);}
    | Type2IntegerOpcode            {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);
                                        TrSetOpAmlLength ($1, 4);}
    | ConstExprTerm                 {$$ = TrSetOpIntegerValue (PARSEOP_DWORDCONST, $1);}
    | DWordConst                    {}
    ;

QWordConstExpr
    : Type3Opcode                   {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);
                                        TrSetOpAmlLength ($1, 8);}
    | Type2IntegerOpcode            {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);
                                        TrSetOpAmlLength ($1, 8);}
    | ConstExprTerm                 {$$ = TrSetOpIntegerValue (PARSEOP_QWORDCONST, $1);}
    | QWordConst                    {}
    ;

ConstTerm
    : ConstExprTerm                 {}
    | PARSEOP_REVISION              {$$ = TrCreateLeafOp (PARSEOP_REVISION);}
    ;

ConstExprTerm
    : PARSEOP_ZERO                  {$$ = TrCreateValuedLeafOp (PARSEOP_ZERO, 0);}
    | PARSEOP_ONE                   {$$ = TrCreateValuedLeafOp (PARSEOP_ONE, 1);}
    | PARSEOP_ONES                  {$$ = TrCreateValuedLeafOp (PARSEOP_ONES, ACPI_UINT64_MAX);}
    | PARSEOP___DATE__              {$$ = TrCreateConstantLeafOp (PARSEOP___DATE__);}
    | PARSEOP___FILE__              {$$ = TrCreateConstantLeafOp (PARSEOP___FILE__);}
    | PARSEOP___LINE__              {$$ = TrCreateConstantLeafOp (PARSEOP___LINE__);}
    | PARSEOP___PATH__              {$$ = TrCreateConstantLeafOp (PARSEOP___PATH__);}
    | PARSEOP___METHOD__            {$$ = TrCreateConstantLeafOp (PARSEOP___METHOD__);}
    ;

Integer
    : PARSEOP_INTEGER               {$$ = TrCreateValuedLeafOp (PARSEOP_INTEGER,
                                        AslCompilerlval.i);}
    ;

String
    : PARSEOP_STRING_LITERAL        {$$ = TrCreateValuedLeafOp (PARSEOP_STRING_LITERAL,
                                        (ACPI_NATIVE_INT) AslCompilerlval.s);}
    ;


/*******************************************************************************
 *
 * ASL Opcode Terms
 *
 ******************************************************************************/

CompilerDirective
    : IncludeTerm                   {}
    | IncludeEndTerm                {}
    | ExternalTerm                  {}
    ;

NamedObject
    : BankFieldTerm                 {}
    | CreateBitFieldTerm            {}
    | CreateByteFieldTerm           {}
    | CreateDWordFieldTerm          {}
    | CreateFieldTerm               {}
    | CreateQWordFieldTerm          {}
    | CreateWordFieldTerm           {}
    | DataRegionTerm                {}
    | DeviceTerm                    {}
    | EventTerm                     {}
    | FieldTerm                     {}
    | FunctionTerm                  {}
    | IndexFieldTerm                {}
    | MethodTerm                    {}
    | MutexTerm                     {}
    | OpRegionTerm                  {}
    | PowerResTerm                  {}
    | ProcessorTerm                 {}
    | ThermalZoneTerm               {}
    ;

NameSpaceModifier
    : AliasTerm                     {}
    | NameTerm                      {}
/*    | NameTermAslPlus               {} */
    | ScopeTerm                     {}
    ;

SimpleName
    : NameString                    {}
    | LocalTerm                     {}
    | ArgTerm                       {}
    ;

/* For ObjectType(), SuperName except for MethodInvocationTerm */

ObjectTypeSource
    : SimpleName                    {}
    | DebugTerm                     {}
    | RefOfTerm                     {}
    | DerefOfTerm                   {}
    | IndexTerm                     {}
    | IndexExpTerm                  {}
    ;

/* For DeRefOf(), SuperName except for DerefOf and Debug */

DerefOfSource
    : SimpleName                    {}
    | RefOfTerm                     {}
    | DerefOfTerm                   {}
    | IndexTerm                     {}
    | IndexExpTerm                  {}
    | StoreTerm                     {}
    | EqualsTerm                    {}
    | MethodInvocationTerm          {}
    ;

/* For RefOf(), SuperName except for RefOf and MethodInvocationTerm */

RefOfSource
    : SimpleName                    {}
    | DebugTerm                     {}
    | DerefOfTerm                   {}
    | IndexTerm                     {}
    | IndexExpTerm                  {}
    ;

/* For CondRefOf(), SuperName except for RefOf and MethodInvocationTerm */

CondRefOfSource
    : SimpleName                    {}
    | DebugTerm                     {}
    | DerefOfTerm                   {}
    | IndexTerm                     {}
    | IndexExpTerm                  {}
    ;

/*
 * Opcode types, as defined in the ACPI specification
 */
Type1Opcode
    : BreakTerm                     {}
    | BreakPointTerm                {}
    | ContinueTerm                  {}
    | FatalTerm                     {}
    | ForTerm                       {}
    | ElseIfTerm                    {}
    | LoadTerm                      {}
    | NoOpTerm                      {}
    | NotifyTerm                    {}
    | ReleaseTerm                   {}
    | ResetTerm                     {}
    | ReturnTerm                    {}
    | SignalTerm                    {}
    | SleepTerm                     {}
    | StallTerm                     {}
    | SwitchTerm                    {}
    | UnloadTerm                    {}
    | WhileTerm                     {}
    ;

Type2Opcode
    : AcquireTerm                   {}
    | CondRefOfTerm                 {}
    | CopyObjectTerm                {}
    | DerefOfTerm                   {}
    | ObjectTypeTerm                {}
    | RefOfTerm                     {}
    | SizeOfTerm                    {}
    | StoreTerm                     {}
    | EqualsTerm                    {}
    | TimerTerm                     {}
    | WaitTerm                      {}
    | MethodInvocationTerm          {}
    ;

/*
 * Type 3/4/5 opcodes
 */
Type2IntegerOpcode                  /* "Type3" opcodes */
    : Expression                    {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);}
    | AddTerm                       {}
    | AndTerm                       {}
    | DecTerm                       {}
    | DivideTerm                    {}
    | FindSetLeftBitTerm            {}
    | FindSetRightBitTerm           {}
    | FromBCDTerm                   {}
    | IncTerm                       {}
    | IndexTerm                     {}
/*    | StructureIndexTerm            {} */
/*    | StructurePointerTerm          {} */
    | LAndTerm                      {}
    | LEqualTerm                    {}
    | LGreaterTerm                  {}
    | LGreaterEqualTerm             {}
    | LLessTerm                     {}
    | LLessEqualTerm                {}
    | LNotTerm                      {}
    | LNotEqualTerm                 {}
    | LoadTableTerm                 {}
    | LOrTerm                       {}
    | MatchTerm                     {}
    | ModTerm                       {}
    | MultiplyTerm                  {}
    | NAndTerm                      {}
    | NOrTerm                       {}
    | NotTerm                       {}
    | OrTerm                        {}
    | ShiftLeftTerm                 {}
    | ShiftRightTerm                {}
    | SubtractTerm                  {}
    | ToBCDTerm                     {}
    | ToIntegerTerm                 {}
    | XOrTerm                       {}
    ;

Type2StringOpcode                   /* "Type4" Opcodes */
    : ToDecimalStringTerm           {}
    | ToHexStringTerm               {}
    | ToStringTerm                  {}
    ;

Type2BufferOpcode                   /* "Type5" Opcodes */
    : ToBufferTerm                  {}
    | ConcatResTerm                 {}
    ;

Type2BufferOrStringOpcode
    : ConcatTerm                    {$$ = TrSetOpFlags ($1, OP_COMPILE_TIME_CONST);}
    | PrintfTerm                    {}
    | FprintfTerm                   {}
    | MidTerm                       {}
    ;

/*
 * A type 3 opcode evaluates to an Integer and cannot have a destination operand
 */
Type3Opcode
    : EISAIDTerm                    {}
    ;

/* Obsolete
Type4Opcode
    : ConcatTerm                    {}
    | ToDecimalStringTerm           {}
    | ToHexStringTerm               {}
    | MidTerm                       {}
    | ToStringTerm                  {}
    ;
*/

/* Type 5 opcodes are a subset of Type2 opcodes, and return a constant */

Type5Opcode
    : ResourceTemplateTerm          {}
    | UnicodeTerm                   {}
    | ToPLDTerm                     {}
    | ToUUIDTerm                    {}
    ;

Type6Opcode
    : RefOfTerm                     {}
    | DerefOfTerm                   {}
    | IndexTerm                     {}
    | IndexExpTerm                  {}
/*    | StructureIndexTerm            {} */
/*    | StructurePointerTerm          {} */
    | MethodInvocationTerm          {}
    ;


/*******************************************************************************
 *
 * ASL Helper Terms
 *
 ******************************************************************************/

AmlPackageLengthTerm
    : Integer                       {$$ = TrSetOpIntegerValue (PARSEOP_PACKAGE_LENGTH,
                                        (ACPI_PARSE_OBJECT *) $1);}
    ;

NameStringItem
    : ',' NameString                {$$ = $2;}
    | ',' error                     {$$ = AslDoError (); yyclearin;}
    ;

TermArgItem
    : ',' TermArg                   {$$ = $2;}
    | ',' error                     {$$ = AslDoError (); yyclearin;}
    ;

OptionalReference
    :                               {$$ = TrCreateLeafOp (PARSEOP_ZERO);}       /* Placeholder is a ZeroOp object */
    | ','                           {$$ = TrCreateLeafOp (PARSEOP_ZERO);}       /* Placeholder is a ZeroOp object */
    | ',' TermArg                   {$$ = $2;}
    ;

OptionalReturnArg
    :                               {$$ = TrSetOpFlags (TrCreateLeafOp (PARSEOP_ZERO),
                                            OP_IS_NULL_RETURN);}       /* Placeholder is a ZeroOp object */
    | TermArg                       {$$ = $1;}
    ;

OptionalSerializeRuleKeyword
    :                               {$$ = NULL;}
    | ','                           {$$ = NULL;}
    | ',' SerializeRuleKeyword      {$$ = $2;}
    ;

OptionalTermArg
    :                               {$$ = TrCreateLeafOp (PARSEOP_DEFAULT_ARG);}
    | TermArg                       {$$ = $1;}
    ;

OptionalWordConst
    :                               {$$ = NULL;}
    | WordConst                     {$$ = $1;}
    ;

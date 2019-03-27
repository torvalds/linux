%{
/******************************************************************************
 *
 * Module Name: aslcompiler.l - Flex/lex input file
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
#include <contrib/dev/acpica/include/acconvert.h>

#include <stdlib.h>
#include <string.h>
YYSTYPE AslCompilerlval;

/*
 * Generation:  Use the following command line:
 *
 * flex.exe -PAslCompiler -i -o$(InputPath).c $(InputPath)
 *
 * -i: Scanner must be case-insensitive
 */

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslscanner")


/* Local prototypes */

static void
AslDoLineDirective (void);

static BOOLEAN
AslDoComment (void);

static BOOLEAN
AslDoCommentType2 (void);

static char
AslDoStringLiteral (void);

static void
count (int type);


/*! [Begin] no source code translation */

%}
    /* Definitions */

LeadNameChar                [A-Za-z_]
DigitChar                   [0-9]
OctalChar                   [0-7]
HexDigitChar                [A-Fa-f0-9]
RootChar                    [\\]
Nothing                     []

NameChar                    [A-Za-z_0-9]
NameSeg1                    {LeadNameChar}{NameChar}
NameSeg2                    {LeadNameChar}{NameChar}{NameChar}
NameSeg3                    {LeadNameChar}{NameChar}{NameChar}{NameChar}
NameSeg                     {LeadNameChar}|{NameSeg1}|{NameSeg2}|{NameSeg3}

NameString                  {RootChar}|{RootChar}{NamePath}|[\^]+{NamePath}|{NonEmptyNamePath}
NamePath                    {NonEmptyNamePath}?
NonEmptyNamePath            {NameSeg}{NamePathTail}*
NamePathTail                [.]{NameSeg}

%%
    /* Rules */

[ ]                         { count (0); }
[\n]                        { count (0); } /* Handle files with both LF and CR/LF */
[\r]                        { count (0); } /* termination on both Unix and Windows */
[ \t]                       { count (0); }


"/*"                        { if (!AslDoComment ()) {yyterminate ();} }
"//"                        { if (!AslDoCommentType2 ()) {yyterminate ();} }

"\""                        { if (AslDoStringLiteral ()) {return (PARSEOP_STRING_LITERAL);}
                              else {yyterminate ();} }
";"                         { count (0); return(';'); }

    /* ASL Extension: Standard C operators */

"~"                         { count (3); return (PARSEOP_EXP_NOT); }
"!"                         { count (3); return (PARSEOP_EXP_LOGICAL_NOT); }
"*"                         { count (3); return (PARSEOP_EXP_MULTIPLY); }
"/"                         { count (3); return (PARSEOP_EXP_DIVIDE); }
"%"                         { count (3); return (PARSEOP_EXP_MODULO); }
"+"                         { count (3); return (PARSEOP_EXP_ADD); }
"-"                         { count (3); return (PARSEOP_EXP_SUBTRACT); }
">>"                        { count (3); return (PARSEOP_EXP_SHIFT_RIGHT); }
"<<"                        { count (3); return (PARSEOP_EXP_SHIFT_LEFT); }
"<"                         { count (3); return (PARSEOP_EXP_LESS); }
">"                         { count (3); return (PARSEOP_EXP_GREATER); }
"&"                         { count (3); return (PARSEOP_EXP_AND); }
"<="                        { count (3); return (PARSEOP_EXP_LESS_EQUAL); }
">="                        { count (3); return (PARSEOP_EXP_GREATER_EQUAL); }
"=="                        { count (3); return (PARSEOP_EXP_EQUAL); }
"!="                        { count (3); return (PARSEOP_EXP_NOT_EQUAL); }
"|"                         { count (3); return (PARSEOP_EXP_OR); }
"&&"                        { count (3); return (PARSEOP_EXP_LOGICAL_AND); }
"||"                        { count (3); return (PARSEOP_EXP_LOGICAL_OR); }
"++"                        { count (3); return (PARSEOP_EXP_INCREMENT); }
"--"                        { count (3); return (PARSEOP_EXP_DECREMENT); }
"^ "                        { count (3); return (PARSEOP_EXP_XOR); }

    /* ASL Extension: Standard C assignment operators */

"="                         { count (3); return (PARSEOP_EXP_EQUALS); }
"+="                        { count (3); return (PARSEOP_EXP_ADD_EQ); }
"-="                        { count (3); return (PARSEOP_EXP_SUB_EQ); }
"*="                        { count (3); return (PARSEOP_EXP_MUL_EQ); }
"/="                        { count (3); return (PARSEOP_EXP_DIV_EQ); }
"%="                        { count (3); return (PARSEOP_EXP_MOD_EQ); }
"<<="                       { count (3); return (PARSEOP_EXP_SHL_EQ); }
">>="                       { count (3); return (PARSEOP_EXP_SHR_EQ); }
"&="                        { count (3); return (PARSEOP_EXP_AND_EQ); }
"^="                        { count (3); return (PARSEOP_EXP_XOR_EQ); }
"|="                        { count (3); return (PARSEOP_EXP_OR_EQ); }

"["                         { count (3); return (PARSEOP_EXP_INDEX_LEFT); }
"]"                         { count (0); return (PARSEOP_EXP_INDEX_RIGHT); }
"("                         { count (0); return (PARSEOP_OPEN_PAREN); }
")"                         { count (0); return (PARSEOP_CLOSE_PAREN); }

"{"                         { count (0); return ('{'); }
"}"                         { count (0); return ('}'); }
","                         { count (0); return (','); }


    /*
     * Begin standard ASL grammar
     */
[0-9][a-zA-Z0-9]*            { AslCompilerlval.i = UtDoConstant ((char *) AslCompilertext);
                                count (1); return (PARSEOP_INTEGER); }

"Include"                   { count (1); return (PARSEOP_INCLUDE); }
"External"                  { count (1); return (PARSEOP_EXTERNAL); }

 /*
  * The #line directive is emitted by the preprocessor and handled
  * here in the main iASL lexer - simply set the line number and
  * optionally the current filename.
  */
"#line"                     { AslDoLineDirective ();}


 /****************************************************************************
  *
  * Main ASL operators
  *
  ****************************************************************************/

"AccessAs"                  { count (1); return (PARSEOP_ACCESSAS); }
"Acquire"                   { count (3); return (PARSEOP_ACQUIRE); }
"Add"                       { count (3); return (PARSEOP_ADD); }
"Alias"                     { count (2); return (PARSEOP_ALIAS); }
"And"                       { count (3); return (PARSEOP_AND); }
"BankField"                 { count (2); return (PARSEOP_BANKFIELD); }
"Break"                     { count (3); return (PARSEOP_BREAK); }
"BreakPoint"                { count (3); return (PARSEOP_BREAKPOINT); }
"Buffer"                    { count (1); return (PARSEOP_BUFFER); }
"Case"                      { count (3); return (PARSEOP_CASE); }
"Concatenate"               { count (3); return (PARSEOP_CONCATENATE); }
"ConcatenateResTemplate"    { count (3); return (PARSEOP_CONCATENATERESTEMPLATE); }
"CondRefOf"                 { count (3); return (PARSEOP_CONDREFOF); }
"Connection"                { count (2); return (PARSEOP_CONNECTION); }
"Continue"                  { count (3); return (PARSEOP_CONTINUE); }
"CopyObject"                { count (3); return (PARSEOP_COPYOBJECT); }
"CreateBitField"            { count (2); return (PARSEOP_CREATEBITFIELD); }
"CreateByteField"           { count (2); return (PARSEOP_CREATEBYTEFIELD); }
"CreateDWordField"          { count (2); return (PARSEOP_CREATEDWORDFIELD); }
"CreateField"               { count (2); return (PARSEOP_CREATEFIELD); }
"CreateQWordField"          { count (2); return (PARSEOP_CREATEQWORDFIELD); }
"CreateWordField"           { count (2); return (PARSEOP_CREATEWORDFIELD); }
"DataTableRegion"           { count (2); return (PARSEOP_DATATABLEREGION); }
"Debug"                     { count (1); return (PARSEOP_DEBUG); }
"Decrement"                 { count (3); return (PARSEOP_DECREMENT); }
"Default"                   { count (3); return (PARSEOP_DEFAULT); }
"DefinitionBlock"           { count (1); return (PARSEOP_DEFINITION_BLOCK); }
"DeRefOf"                   { count (3); return (PARSEOP_DEREFOF); }
"Device"                    { count (2); return (PARSEOP_DEVICE); }
"Divide"                    { count (3); return (PARSEOP_DIVIDE); }
"Eisaid"                    { count (1); return (PARSEOP_EISAID); }
"Else"                      { count (3); return (PARSEOP_ELSE); }
"ElseIf"                    { count (3); return (PARSEOP_ELSEIF); }
"Event"                     { count (2); return (PARSEOP_EVENT); }
"Fatal"                     { count (3); return (PARSEOP_FATAL); }
"Field"                     { count (2); return (PARSEOP_FIELD); }
"FindSetLeftBit"            { count (3); return (PARSEOP_FINDSETLEFTBIT); }
"FindSetRightBit"           { count (3); return (PARSEOP_FINDSETRIGHTBIT); }
"FromBcd"                   { count (3); return (PARSEOP_FROMBCD); }
"Function"                  { count (2); return (PARSEOP_FUNCTION); }
"If"                        { count (3); return (PARSEOP_IF); }
"Increment"                 { count (3); return (PARSEOP_INCREMENT); }
"Index"                     { count (3); return (PARSEOP_INDEX); }
"IndexField"                { count (2); return (PARSEOP_INDEXFIELD); }
"LAnd"                      { count (3); return (PARSEOP_LAND); }
"LEqual"                    { count (3); return (PARSEOP_LEQUAL); }
"LGreater"                  { count (3); return (PARSEOP_LGREATER); }
"LGreaterEqual"             { count (3); return (PARSEOP_LGREATEREQUAL); }
"LLess"                     { count (3); return (PARSEOP_LLESS); }
"LLessEqual"                { count (3); return (PARSEOP_LLESSEQUAL); }
"LNot"                      { count (3); return (PARSEOP_LNOT); }
"LNotEqual"                 { count (3); return (PARSEOP_LNOTEQUAL); }
"Load"                      { count (3); return (PARSEOP_LOAD); }
"LoadTable"                 { count (3); return (PARSEOP_LOADTABLE); }
"LOr"                       { count (3); return (PARSEOP_LOR); }
"Match"                     { count (3); return (PARSEOP_MATCH); }
"Method"                    { count (2); return (PARSEOP_METHOD); }
"Mid"                       { count (3); return (PARSEOP_MID); }
"Mod"                       { count (3); return (PARSEOP_MOD); }
"Multiply"                  { count (3); return (PARSEOP_MULTIPLY); }
"Mutex"                     { count (2); return (PARSEOP_MUTEX); }
"Name"                      { count (2); return (PARSEOP_NAME); }
"NAnd"                      { count (3); return (PARSEOP_NAND); }
"Noop"                      { if (!AcpiGbl_IgnoreNoopOperator) {count (3); return (PARSEOP_NOOP);} }
"NOr"                       { count (3); return (PARSEOP_NOR); }
"Not"                       { count (3); return (PARSEOP_NOT); }
"Notify"                    { count (3); return (PARSEOP_NOTIFY); }
"ObjectType"                { count (3); return (PARSEOP_OBJECTTYPE); }
"Offset"                    { count (1); return (PARSEOP_OFFSET); }
"One"                       { count (1); return (PARSEOP_ONE); }
"Ones"                      { count (1); return (PARSEOP_ONES); }
"OperationRegion"           { count (2); return (PARSEOP_OPERATIONREGION); }
"Or"                        { count (3); return (PARSEOP_OR); }
"Package"                   { count (1); return (PARSEOP_PACKAGE); }
"PowerResource"             { count (2); return (PARSEOP_POWERRESOURCE); }
"Processor"                 { count (2); return (PARSEOP_PROCESSOR); }
"RefOf"                     { count (3); return (PARSEOP_REFOF); }
"Release"                   { count (3); return (PARSEOP_RELEASE); }
"Reset"                     { count (3); return (PARSEOP_RESET); }
"Return"                    { count (3); return (PARSEOP_RETURN); }
"Revision"                  { count (1); return (PARSEOP_REVISION); }
"Scope"                     { count (2); return (PARSEOP_SCOPE); }
"ShiftLeft"                 { count (3); return (PARSEOP_SHIFTLEFT); }
"ShiftRight"                { count (3); return (PARSEOP_SHIFTRIGHT); }
"Signal"                    { count (3); return (PARSEOP_SIGNAL); }
"SizeOf"                    { count (3); return (PARSEOP_SIZEOF); }
"Sleep"                     { count (3); return (PARSEOP_SLEEP); }
"Stall"                     { count (3); return (PARSEOP_STALL); }
"Store"                     { count (3); return (PARSEOP_STORE); }
"Subtract"                  { count (3); return (PARSEOP_SUBTRACT); }
"Switch"                    { count (3); return (PARSEOP_SWITCH); }
"ThermalZone"               { count (2); return (PARSEOP_THERMALZONE); }
"Timer"                     { count (3); return (PARSEOP_TIMER); }
"ToBcd"                     { count (3); return (PARSEOP_TOBCD); }
"ToBuffer"                  { count (3); return (PARSEOP_TOBUFFER); }
"ToDecimalString"           { count (3); return (PARSEOP_TODECIMALSTRING); }
"ToHexString"               { count (3); return (PARSEOP_TOHEXSTRING); }
"ToInteger"                 { count (3); return (PARSEOP_TOINTEGER); }
"ToString"                  { count (3); return (PARSEOP_TOSTRING); }
"ToUuid"                    { count (1); return (PARSEOP_TOUUID); }
"Unicode"                   { count (1); return (PARSEOP_UNICODE); }
"Unload"                    { count (3); return (PARSEOP_UNLOAD); }
"Wait"                      { count (3); return (PARSEOP_WAIT); }
"While"                     { count (3); return (PARSEOP_WHILE); }
"XOr"                       { count (3); return (PARSEOP_XOR); }
"Zero"                      { count (1); return (PARSEOP_ZERO); }

    /* Control method arguments and locals */

"Arg0"                      { count (1); return (PARSEOP_ARG0); }
"Arg1"                      { count (1); return (PARSEOP_ARG1); }
"Arg2"                      { count (1); return (PARSEOP_ARG2); }
"Arg3"                      { count (1); return (PARSEOP_ARG3); }
"Arg4"                      { count (1); return (PARSEOP_ARG4); }
"Arg5"                      { count (1); return (PARSEOP_ARG5); }
"Arg6"                      { count (1); return (PARSEOP_ARG6); }
"Local0"                    { count (1); return (PARSEOP_LOCAL0); }
"Local1"                    { count (1); return (PARSEOP_LOCAL1); }
"Local2"                    { count (1); return (PARSEOP_LOCAL2); }
"Local3"                    { count (1); return (PARSEOP_LOCAL3); }
"Local4"                    { count (1); return (PARSEOP_LOCAL4); }
"Local5"                    { count (1); return (PARSEOP_LOCAL5); }
"Local6"                    { count (1); return (PARSEOP_LOCAL6); }
"Local7"                    { count (1); return (PARSEOP_LOCAL7); }


 /****************************************************************************
  *
  * Resource Descriptor macros
  *
  ****************************************************************************/

"ResourceTemplate"          { count (1); return (PARSEOP_RESOURCETEMPLATE); }
"RawDataBuffer"             { count (1); return (PARSEOP_DATABUFFER); }

"DMA"                       { count (1); return (PARSEOP_DMA); }
"DWordIO"                   { count (1); return (PARSEOP_DWORDIO); }
"DWordMemory"               { count (1); return (PARSEOP_DWORDMEMORY); }
"DWordSpace"                { count (1); return (PARSEOP_DWORDSPACE); }
"EndDependentFn"            { count (1); return (PARSEOP_ENDDEPENDENTFN); }
"ExtendedIO"                { count (1); return (PARSEOP_EXTENDEDIO); }
"ExtendedMemory"            { count (1); return (PARSEOP_EXTENDEDMEMORY); }
"ExtendedSpace"             { count (1); return (PARSEOP_EXTENDEDSPACE); }
"FixedDma"                  { count (1); return (PARSEOP_FIXEDDMA); }
"FixedIO"                   { count (1); return (PARSEOP_FIXEDIO); }
"GpioInt"                   { count (1); return (PARSEOP_GPIO_INT); }
"GpioIo"                    { count (1); return (PARSEOP_GPIO_IO); }
"I2cSerialBus"              { count (1); return (PARSEOP_I2C_SERIALBUS); }
"I2cSerialBusV2"            { count (1); return (PARSEOP_I2C_SERIALBUS_V2); }
"Interrupt"                 { count (1); return (PARSEOP_INTERRUPT); }
"IO"                        { count (1); return (PARSEOP_IO); }
"IRQ"                       { count (1); return (PARSEOP_IRQ); }
"IRQNoFlags"                { count (1); return (PARSEOP_IRQNOFLAGS); }
"Memory24"                  { count (1); return (PARSEOP_MEMORY24); }
"Memory32"                  { count (1); return (PARSEOP_MEMORY32); }
"Memory32Fixed"             { count (1); return (PARSEOP_MEMORY32FIXED); }
"PinConfig"                 { count (1); return (PARSEOP_PINCONFIG); }
"PinFunction"               { count (1); return (PARSEOP_PINFUNCTION); }
"PinGroup"                  { count (1); return (PARSEOP_PINGROUP); }
"PinGroupConfig"            { count (1); return (PARSEOP_PINGROUPCONFIG); }
"PinGroupFunction"          { count (1); return (PARSEOP_PINGROUPFUNCTION); }
"QWordIO"                   { count (1); return (PARSEOP_QWORDIO); }
"QWordMemory"               { count (1); return (PARSEOP_QWORDMEMORY); }
"QWordSpace"                { count (1); return (PARSEOP_QWORDSPACE); }
"Register"                  { count (1); return (PARSEOP_REGISTER); }
"SpiSerialBus"              { count (1); return (PARSEOP_SPI_SERIALBUS); }
"SpiSerialBusV2"            { count (1); return (PARSEOP_SPI_SERIALBUS_V2); }
"StartDependentFn"          { count (1); return (PARSEOP_STARTDEPENDENTFN); }
"StartDependentFnNoPri"     { count (1); return (PARSEOP_STARTDEPENDENTFN_NOPRI); }
"UartSerialBus"             { count (1); return (PARSEOP_UART_SERIALBUS); }
"UartSerialBusV2"           { count (1); return (PARSEOP_UART_SERIALBUS_V2); }
"VendorLong"                { count (1); return (PARSEOP_VENDORLONG); }
"VendorShort"               { count (1); return (PARSEOP_VENDORSHORT); }
"WordBusNumber"             { count (1); return (PARSEOP_WORDBUSNUMBER); }
"WordIO"                    { count (1); return (PARSEOP_WORDIO); }
"WordSpace"                 { count (1); return (PARSEOP_WORDSPACE); }


 /****************************************************************************
  *
  * Keywords used as arguments to ASL operators and macros
  *
  ****************************************************************************/

    /*  AccessAttribKeyword: Serial Bus Attributes (ACPI 5.0) */

"AttribQuick"               { count (0); return (PARSEOP_ACCESSATTRIB_QUICK); }
"AttribSendReceive"         { count (0); return (PARSEOP_ACCESSATTRIB_SND_RCV); }
"AttribByte"                { count (0); return (PARSEOP_ACCESSATTRIB_BYTE); }
"AttribWord"                { count (0); return (PARSEOP_ACCESSATTRIB_WORD); }
"AttribBlock"               { count (0); return (PARSEOP_ACCESSATTRIB_BLOCK); }
"AttribProcessCall"         { count (0); return (PARSEOP_ACCESSATTRIB_WORD_CALL); }
"AttribBlockProcessCall"    { count (0); return (PARSEOP_ACCESSATTRIB_BLOCK_CALL); }

    /* AccessAttribKeyword: Legacy synonyms for above (pre-ACPI 5.0) */

"SMBQuick"                  { count (0); return (PARSEOP_ACCESSATTRIB_QUICK); }
"SMBSendReceive"            { count (0); return (PARSEOP_ACCESSATTRIB_SND_RCV); }
"SMBByte"                   { count (0); return (PARSEOP_ACCESSATTRIB_BYTE); }
"SMBWord"                   { count (0); return (PARSEOP_ACCESSATTRIB_WORD); }
"SMBBlock"                  { count (0); return (PARSEOP_ACCESSATTRIB_BLOCK); }
"SMBProcessCall"            { count (0); return (PARSEOP_ACCESSATTRIB_WORD_CALL); }
"SMBBlockProcessCall"       { count (0); return (PARSEOP_ACCESSATTRIB_BLOCK_CALL); }

    /* AccessTypeKeyword: Field Access Types */

"AnyAcc"                    { count (0); return (PARSEOP_ACCESSTYPE_ANY); }
"ByteAcc"                   { count (0); return (PARSEOP_ACCESSTYPE_BYTE); }
"WordAcc"                   { count (0); return (PARSEOP_ACCESSTYPE_WORD); }
"DWordAcc"                  { count (0); return (PARSEOP_ACCESSTYPE_DWORD); }
"QWordAcc"                  { count (0); return (PARSEOP_ACCESSTYPE_QWORD); }
"BufferAcc"                 { count (0); return (PARSEOP_ACCESSTYPE_BUF); }

    /* AddressingModeKeyword: Mode - Resource Descriptors (ACPI 5.0) */

"AddressingMode7Bit"        { count (0); return (PARSEOP_ADDRESSINGMODE_7BIT); }
"AddressingMode10Bit"       { count (0); return (PARSEOP_ADDRESSINGMODE_10BIT); }

    /* AddressKeyword: ACPI memory range types */

"AddressRangeMemory"        { count (0); return (PARSEOP_ADDRESSTYPE_MEMORY); }
"AddressRangeReserved"      { count (0); return (PARSEOP_ADDRESSTYPE_RESERVED); }
"AddressRangeNVS"           { count (0); return (PARSEOP_ADDRESSTYPE_NVS); }
"AddressRangeACPI"          { count (0); return (PARSEOP_ADDRESSTYPE_ACPI); }

    /* BusMasterKeyword: DMA Bus Mastering */

"BusMaster"                 { count (0); return (PARSEOP_BUSMASTERTYPE_MASTER); }
"NotBusMaster"              { count (0); return (PARSEOP_BUSMASTERTYPE_NOTMASTER); }

    /* ByteLengthKeyword: Bits per Byte - Resource Descriptors (ACPI 5.0) */

"DataBitsFive"              { count (0); return (PARSEOP_BITSPERBYTE_FIVE); }
"DataBitsSix"               { count (0); return (PARSEOP_BITSPERBYTE_SIX); }
"DataBitsSeven"             { count (0); return (PARSEOP_BITSPERBYTE_SEVEN); }
"DataBitsEight"             { count (0); return (PARSEOP_BITSPERBYTE_EIGHT); }
"DataBitsNine"              { count (0); return (PARSEOP_BITSPERBYTE_NINE); }

    /* ClockPhaseKeyword: Resource Descriptors (ACPI 5.0) */

"ClockPhaseFirst"           { count (0); return (PARSEOP_CLOCKPHASE_FIRST); }
"ClockPhaseSecond"          { count (0); return (PARSEOP_CLOCKPHASE_SECOND); }

    /* ClockPolarityKeyword: Resource Descriptors (ACPI 5.0) */

"ClockPolarityLow"          { count (0); return (PARSEOP_CLOCKPOLARITY_LOW); }
"ClockPolarityHigh"         { count (0); return (PARSEOP_CLOCKPOLARITY_HIGH); }

    /* DecodeKeyword: Type of Memory Decoding - Resource Descriptors */

"PosDecode"                 { count (0); return (PARSEOP_DECODETYPE_POS); }
"SubDecode"                 { count (0); return (PARSEOP_DECODETYPE_SUB); }

    /* DmaTypeKeyword: DMA Types - DMA Resource Descriptor */

"Compatibility"             { count (0); return (PARSEOP_DMATYPE_COMPATIBILITY); }
"TypeA"                     { count (0); return (PARSEOP_DMATYPE_A); }
"TypeB"                     { count (0); return (PARSEOP_DMATYPE_B); }
"TypeF"                     { count (0); return (PARSEOP_DMATYPE_F); }

    /* EndianKeyword: Endian type - Resource Descriptor (ACPI 5.0) */

"LittleEndian"              { count (0); return (PARSEOP_ENDIAN_LITTLE); }
"BigEndian"                 { count (0); return (PARSEOP_ENDIAN_BIG); }

    /* ExtendedAttribKeyword: Bus attributes, AccessAs operator (ACPI 5.0) */

"AttribBytes"               { count (0); return (PARSEOP_ACCESSATTRIB_BYTES); }
"AttribRawBytes"            { count (0); return (PARSEOP_ACCESSATTRIB_RAW_BYTES); }
"AttribRawProcessBytes"     { count (0); return (PARSEOP_ACCESSATTRIB_RAW_PROCESS); }

    /* FlowControlKeyword: Resource Descriptors (ACPI 5.0) */

"FlowControlHardware"       { count (0); return (PARSEOP_FLOWCONTROL_HW); }
"FlowControlNone"           { count (0); return (PARSEOP_FLOWCONTROL_NONE); }
"FlowControlXon"            { count (0); return (PARSEOP_FLOWCONTROL_SW); }

    /* InterruptLevelKeyword: Interrupt Active Types */

"ActiveBoth"                { count (0); return (PARSEOP_INTLEVEL_ACTIVEBOTH); }
"ActiveHigh"                { count (0); return (PARSEOP_INTLEVEL_ACTIVEHIGH); }
"ActiveLow"                 { count (0); return (PARSEOP_INTLEVEL_ACTIVELOW); }

    /* InterruptTypeKeyword: Interrupt Types */

"Edge"                      { count (0); return (PARSEOP_INTTYPE_EDGE); }
"Level"                     { count (0); return (PARSEOP_INTTYPE_LEVEL); }

    /* IoDecodeKeyword: Type of Memory Decoding - Resource Descriptors */

"Decode10"                  { count (0); return (PARSEOP_IODECODETYPE_10); }
"Decode16"                  { count (0); return (PARSEOP_IODECODETYPE_16); }

    /* IoRestrictionKeyword: I/O Restriction - GPIO Resource Descriptors (ACPI 5.0) */

"IoRestrictionNone"         { count (0); return (PARSEOP_IORESTRICT_NONE); }
"IoRestrictionInputOnly"    { count (0); return (PARSEOP_IORESTRICT_IN); }
"IoRestrictionOutputOnly"   { count (0); return (PARSEOP_IORESTRICT_OUT); }
"IoRestrictionNoneAndPreserve"   { count (0); return (PARSEOP_IORESTRICT_PRESERVE); }

    /* LockRuleKeyword: Global Lock use for Field Operator */

"Lock"                      { count (0); return (PARSEOP_LOCKRULE_LOCK); }
"NoLock"                    { count (0); return (PARSEOP_LOCKRULE_NOLOCK); }

    /* MatchOpKeyword: Types for Match Operator */

"MTR"                       { count (0); return (PARSEOP_MATCHTYPE_MTR); }
"MEQ"                       { count (0); return (PARSEOP_MATCHTYPE_MEQ); }
"MLE"                       { count (0); return (PARSEOP_MATCHTYPE_MLE); }
"MLT"                       { count (0); return (PARSEOP_MATCHTYPE_MLT); }
"MGE"                       { count (0); return (PARSEOP_MATCHTYPE_MGE); }
"MGT"                       { count (0); return (PARSEOP_MATCHTYPE_MGT); }

    /* MaxKeyword: Max Range Type - Resource Descriptors */

"MaxFixed"                  { count (0); return (PARSEOP_MAXTYPE_FIXED); }
"MaxNotFixed"               { count (0); return (PARSEOP_MAXTYPE_NOTFIXED); }

    /* MemTypeKeyword: Memory Types - Resource Descriptors */

"Cacheable"                 { count (0); return (PARSEOP_MEMTYPE_CACHEABLE); }
"WriteCombining"            { count (0); return (PARSEOP_MEMTYPE_WRITECOMBINING); }
"Prefetchable"              { count (0); return (PARSEOP_MEMTYPE_PREFETCHABLE); }
"NonCacheable"              { count (0); return (PARSEOP_MEMTYPE_NONCACHEABLE); }

    /* MinKeyword: Min Range Type - Resource Descriptors */

"MinFixed"                  { count (0); return (PARSEOP_MINTYPE_FIXED); }
"MinNotFixed"               { count (0); return (PARSEOP_MINTYPE_NOTFIXED); }

    /* ObjectTypeKeyword: ACPI Object Types */

"UnknownObj"                { count (0); return (PARSEOP_OBJECTTYPE_UNK); }
"IntObj"                    { count (0); return (PARSEOP_OBJECTTYPE_INT); }
"StrObj"                    { count (0); return (PARSEOP_OBJECTTYPE_STR); }
"BuffObj"                   { count (0); return (PARSEOP_OBJECTTYPE_BUF); }
"PkgObj"                    { count (0); return (PARSEOP_OBJECTTYPE_PKG); }
"FieldUnitObj"              { count (0); return (PARSEOP_OBJECTTYPE_FLD); }
"DeviceObj"                 { count (0); return (PARSEOP_OBJECTTYPE_DEV); }
"EventObj"                  { count (0); return (PARSEOP_OBJECTTYPE_EVT); }
"MethodObj"                 { count (0); return (PARSEOP_OBJECTTYPE_MTH); }
"MutexObj"                  { count (0); return (PARSEOP_OBJECTTYPE_MTX); }
"OpRegionObj"               { count (0); return (PARSEOP_OBJECTTYPE_OPR); }
"PowerResObj"               { count (0); return (PARSEOP_OBJECTTYPE_POW); }
"ProcessorObj"              { count (0); return (PARSEOP_OBJECTTYPE_PRO); }
"ThermalZoneObj"            { count (0); return (PARSEOP_OBJECTTYPE_THZ); }
"BuffFieldObj"              { count (0); return (PARSEOP_OBJECTTYPE_BFF); }
"DDBHandleObj"              { count (0); return (PARSEOP_OBJECTTYPE_DDB); }

    /* ParityKeyword: Resource Descriptors (ACPI 5.0) */

"ParityTypeSpace"           { count (0); return (PARSEOP_PARITYTYPE_SPACE); }
"ParityTypeMark"            { count (0); return (PARSEOP_PARITYTYPE_MARK); }
"ParityTypeOdd"             { count (0); return (PARSEOP_PARITYTYPE_ODD); }
"ParityTypeEven"            { count (0); return (PARSEOP_PARITYTYPE_EVEN); }
"ParityTypeNone"            { count (0); return (PARSEOP_PARITYTYPE_NONE); }

    /* PinConfigKeyword: Pin Configuration - GPIO Resource Descriptors (ACPI 5.0) */

"PullDefault"               { count (0); return (PARSEOP_PIN_PULLDEFAULT); }
"PullUp"                    { count (0); return (PARSEOP_PIN_PULLUP); }
"PullDown"                  { count (0); return (PARSEOP_PIN_PULLDOWN); }
"PullNone"                  { count (0); return (PARSEOP_PIN_NOPULL); }

    /* PolarityKeyword: Resource Descriptors (ACPI 5.0) */

"PolarityLow"               { count (0); return (PARSEOP_DEVICEPOLARITY_LOW); }
"PolarityHigh"              { count (0); return (PARSEOP_DEVICEPOLARITY_HIGH); }

    /* RangeTypeKeyword: I/O Range Types - Resource Descriptors */

"ISAOnlyRanges"             { count (0); return (PARSEOP_RANGETYPE_ISAONLY); }
"NonISAOnlyRanges"          { count (0); return (PARSEOP_RANGETYPE_NONISAONLY); }
"EntireRange"               { count (0); return (PARSEOP_RANGETYPE_ENTIRE); }

    /* ReadWriteKeyword: Memory Access Types - Resource Descriptors */

"ReadWrite"                 { count (0); return (PARSEOP_READWRITETYPE_BOTH); }
"ReadOnly"                  { count (0); return (PARSEOP_READWRITETYPE_READONLY); }

    /* RegionSpaceKeyword: Operation Region Address Space Types */

"SystemIO"                  { count (0); return (PARSEOP_REGIONSPACE_IO); }
"SystemMemory"              { count (0); return (PARSEOP_REGIONSPACE_MEM); }
"PCI_Config"                { count (0); return (PARSEOP_REGIONSPACE_PCI); }
"EmbeddedControl"           { count (0); return (PARSEOP_REGIONSPACE_EC); }
"SMBus"                     { count (0); return (PARSEOP_REGIONSPACE_SMBUS); }
"SystemCMOS"                { count (0); return (PARSEOP_REGIONSPACE_CMOS); }
"PciBarTarget"              { count (0); return (PARSEOP_REGIONSPACE_PCIBAR); }
"IPMI"                      { count (0); return (PARSEOP_REGIONSPACE_IPMI); }
"GeneralPurposeIo"          { count (0); return (PARSEOP_REGIONSPACE_GPIO); }       /* ACPI 5.0 */
"GenericSerialBus"          { count (0); return (PARSEOP_REGIONSPACE_GSBUS); }      /* ACPI 5.0 */
"PlatformCommChannel"       { count (0); return (PARSEOP_REGIONSPACE_PCC); }        /* ACPI 5.0 */
"FFixedHW"                  { count (0); return (PARSEOP_REGIONSPACE_FFIXEDHW); }

    /* ResourceTypeKeyword: Resource Usage - Resource Descriptors */

"ResourceConsumer"          { count (0); return (PARSEOP_RESOURCETYPE_CONSUMER); }
"ResourceProducer"          { count (0); return (PARSEOP_RESOURCETYPE_PRODUCER); }

    /* SerializeRuleKeyword: Control Method Serialization */

"Serialized"                { count (0); return (PARSEOP_SERIALIZERULE_SERIAL); }
"NotSerialized"             { count (0); return (PARSEOP_SERIALIZERULE_NOTSERIAL); }

    /* ShareTypeKeyword: Interrupt Sharing - Resource Descriptors */

"Shared"                    { count (0); return (PARSEOP_SHARETYPE_SHARED); }
"Exclusive"                 { count (0); return (PARSEOP_SHARETYPE_EXCLUSIVE); }
"SharedAndWake"             { count (0); return (PARSEOP_SHARETYPE_SHAREDWAKE); }       /* ACPI 5.0 */
"ExclusiveAndWake"          { count (0); return (PARSEOP_SHARETYPE_EXCLUSIVEWAKE); }    /* ACPI 5.0 */

    /* SlaveModeKeyword: Resource Descriptors (ACPI 5.0) */

"ControllerInitiated"       { count (0); return (PARSEOP_SLAVEMODE_CONTROLLERINIT); }
"DeviceInitiated"           { count (0); return (PARSEOP_SLAVEMODE_DEVICEINIT); }

    /* StopBitsKeyword: Resource Descriptors (ACPI 5.0) */

"StopBitsOne"               { count (0); return (PARSEOP_STOPBITS_ONE); }
"StopBitsOnePlusHalf"       { count (0); return (PARSEOP_STOPBITS_ONEPLUSHALF); }
"StopBitsTwo"               { count (0); return (PARSEOP_STOPBITS_TWO); }
"StopBitsZero"              { count (0); return (PARSEOP_STOPBITS_ZERO); }

    /* TransferWidthKeyword: DMA Widths - Fixed DMA Resource Descriptor (ACPI 5.0) */

"Width8bit"                 { count (0); return (PARSEOP_XFERSIZE_8); }
"Width16bit"                { count (0); return (PARSEOP_XFERSIZE_16); }
"Width32bit"                { count (0); return (PARSEOP_XFERSIZE_32); }
"Width64bit"                { count (0); return (PARSEOP_XFERSIZE_64); }
"Width128bit"               { count (0); return (PARSEOP_XFERSIZE_128); }
"Width256bit"               { count (0); return (PARSEOP_XFERSIZE_256); }

    /* TranslationKeyword: Translation Density Types - Resource Descriptors */

"SparseTranslation"         { count (0); return (PARSEOP_TRANSLATIONTYPE_SPARSE); }
"DenseTranslation"          { count (0); return (PARSEOP_TRANSLATIONTYPE_DENSE); }

    /* TypeKeyword: Translation Types - Resource Descriptors */

"TypeTranslation"           { count (0); return (PARSEOP_TYPE_TRANSLATION); }
"TypeStatic"                { count (0); return (PARSEOP_TYPE_STATIC); }

    /* UpdateRuleKeyword: Field Update Rules */

"Preserve"                  { count (0); return (PARSEOP_UPDATERULE_PRESERVE); }
"WriteAsOnes"               { count (0); return (PARSEOP_UPDATERULE_ONES); }
"WriteAsZeros"              { count (0); return (PARSEOP_UPDATERULE_ZEROS); }

    /* WireModeKeyword: SPI Wire Mode - Resource Descriptors (ACPI 5.0) */

"FourWireMode"              { count (0); return (PARSEOP_WIREMODE_FOUR); }
"ThreeWireMode"             { count (0); return (PARSEOP_WIREMODE_THREE); }

    /* XferTypeKeyword: DMA Transfer Types */

"Transfer8"                 { count (0); return (PARSEOP_XFERTYPE_8); }
"Transfer8_16"              { count (0); return (PARSEOP_XFERTYPE_8_16); }
"Transfer16"                { count (0); return (PARSEOP_XFERTYPE_16); }

    /* ToPld macro */

"ToPLD"                     { count (0); return (PARSEOP_TOPLD); }

"PLD_Revision"              { count (0); return (PARSEOP_PLD_REVISION); }
"PLD_IgnoreColor"           { count (0); return (PARSEOP_PLD_IGNORECOLOR); }
"PLD_Red"                   { count (0); return (PARSEOP_PLD_RED); }
"PLD_Green"                 { count (0); return (PARSEOP_PLD_GREEN); }
"PLD_Blue"                  { count (0); return (PARSEOP_PLD_BLUE); }
"PLD_Width"                 { count (0); return (PARSEOP_PLD_WIDTH); }
"PLD_Height"                { count (0); return (PARSEOP_PLD_HEIGHT); }
"PLD_UserVisible"           { count (0); return (PARSEOP_PLD_USERVISIBLE); }
"PLD_Dock"                  { count (0); return (PARSEOP_PLD_DOCK); }
"PLD_Lid"                   { count (0); return (PARSEOP_PLD_LID); }
"PLD_Panel"                 { count (0); return (PARSEOP_PLD_PANEL); }
"PLD_VerticalPosition"      { count (0); return (PARSEOP_PLD_VERTICALPOSITION); }
"PLD_HorizontalPosition"    { count (0); return (PARSEOP_PLD_HORIZONTALPOSITION); }
"PLD_Shape"                 { count (0); return (PARSEOP_PLD_SHAPE); }
"PLD_GroupOrientation"      { count (0); return (PARSEOP_PLD_GROUPORIENTATION); }
"PLD_GroupToken"            { count (0); return (PARSEOP_PLD_GROUPTOKEN); }
"PLD_GroupPosition"         { count (0); return (PARSEOP_PLD_GROUPPOSITION); }
"PLD_Bay"                   { count (0); return (PARSEOP_PLD_BAY); }
"PLD_Ejectable"             { count (0); return (PARSEOP_PLD_EJECTABLE); }
"PLD_EjectRequired"         { count (0); return (PARSEOP_PLD_EJECTREQUIRED); }
"PLD_CabinetNumber"         { count (0); return (PARSEOP_PLD_CABINETNUMBER); }
"PLD_CardCageNumber"        { count (0); return (PARSEOP_PLD_CARDCAGENUMBER); }
"PLD_Reference"             { count (0); return (PARSEOP_PLD_REFERENCE); }
"PLD_Rotation"              { count (0); return (PARSEOP_PLD_ROTATION); }
"PLD_Order"                 { count (0); return (PARSEOP_PLD_ORDER); }
"PLD_Reserved"              { count (0); return (PARSEOP_PLD_RESERVED); }
"PLD_VerticalOffset"        { count (0); return (PARSEOP_PLD_VERTICALOFFSET); }
"PLD_HorizontalOffset"      { count (0); return (PARSEOP_PLD_HORIZONTALOFFSET); }


    /* printf debug macros */

"printf"                    { count (0); return (PARSEOP_PRINTF); }
"fprintf"                   { count (0); return (PARSEOP_FPRINTF); }

    /* Other macros */

"For"                       { count (0); return (PARSEOP_FOR); }

    /* Predefined compiler names */

"__DATE__"                  { count (0); return (PARSEOP___DATE__); }
"__FILE__"                  { count (0); return (PARSEOP___FILE__); }
"__LINE__"                  { count (0); return (PARSEOP___LINE__); }
"__PATH__"                  { count (0); return (PARSEOP___PATH__); }
"__METHOD__"                { count (0); return (PARSEOP___METHOD__); }

{NameSeg}                   { char *s;
                                count (0);
                                s=UtLocalCacheCalloc (ACPI_NAME_SIZE + 1);
                                if (strcmp (AslCompilertext, "\\"))
                                {
                                    /*
                                     * According to the ACPI specification,
                                     * NameSegments must have length of 4. If
                                     * the NameSegment has length less than 4,
                                     * they are padded with underscores to meet
                                     * the required length.
                                     */
                                    strcpy (s, "____");
                                    AcpiUtStrupr (AslCompilertext);
                                }
                                memcpy (s, AslCompilertext, strlen (AslCompilertext));
                                AslCompilerlval.s = s;
                                DbgPrint (ASL_PARSE_OUTPUT, "NameSeg: %s\n", s);
                                return (PARSEOP_NAMESEG); }

{NameString}                { char *s;
                                count (0);
                                s=UtLocalCacheCalloc (strlen (AslCompilertext)+1);
                                AcpiUtStrupr (AslCompilertext);
                                strcpy (s, AslCompilertext);
                                AslCompilerlval.s = s;
                                DbgPrint (ASL_PARSE_OUTPUT, "NameString: %s\n", s);
                                return (PARSEOP_NAMESTRING); }

.                           { count (1);
                                if (isprint ((int) *AslCompilertext))
                                {
                                    sprintf (AslGbl_MsgBuffer,
                                        "Invalid character (%c), expecting ASL keyword or name",
                                        *AslCompilertext);
                                }
                                else
                                {
                                    sprintf (AslGbl_MsgBuffer,
                                        "Invalid character (0x%2.2X), expecting ASL keyword or name",
                                        *AslCompilertext);
                                }
                                AslCompilererror (AslGbl_MsgBuffer);}

<<EOF>>                     { if (AslPopInputFileStack ())
                                {yyterminate();}
                              else
                                {return (PARSEOP_INCLUDE_END);} };

%%

/*! [End] no source code translation !*/

/*
 * Bring in the scanner support routines
 */
#include <contrib/dev/acpica/compiler/aslsupport.l>

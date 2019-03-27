NoEcho('
/******************************************************************************
 *
 * Module Name: asltypes.y - Bison/Yacc production types/names
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

/******************************************************************************
 *
 * Production names
 *
 *****************************************************************************/

%type <n> ArgList
%type <n> AslCode
%type <n> BufferData
%type <n> BufferTermData
%type <n> CompilerDirective
%type <n> DataObject
%type <n> DefinitionBlockTerm
%type <n> DefinitionBlockList
%type <n> IntegerData
%type <n> NamedObject
%type <n> NameSpaceModifier
%type <n> Object
%type <n> PackageData
%type <n> ParameterTypePackage
%type <n> ParameterTypePackageList
%type <n> ParameterTypesPackage
%type <n> ParameterTypesPackageList
%type <n> RequiredTarget
%type <n> SimpleName
%type <n> StringData
%type <n> Target
%type <n> Term
%type <n> TermArg
%type <n> TermList
%type <n> MethodInvocationTerm

/* Type4Opcode is obsolete */

%type <n> Type1Opcode
%type <n> Type2BufferOpcode
%type <n> Type2BufferOrStringOpcode
%type <n> Type2IntegerOpcode
%type <n> Type2Opcode
%type <n> Type2StringOpcode
%type <n> Type3Opcode
%type <n> Type5Opcode
%type <n> Type6Opcode

%type <n> AccessAsTerm
%type <n> ExternalTerm
%type <n> FieldUnit
%type <n> FieldUnitEntry
%type <n> FieldUnitList
%type <n> IncludeTerm
%type <n> OffsetTerm
%type <n> OptionalAccessAttribTerm

/* Named Objects */

%type <n> BankFieldTerm
%type <n> CreateBitFieldTerm
%type <n> CreateByteFieldTerm
%type <n> CreateDWordFieldTerm
%type <n> CreateFieldTerm
%type <n> CreateQWordFieldTerm
%type <n> CreateWordFieldTerm
%type <n> DataRegionTerm
%type <n> DeviceTerm
%type <n> EventTerm
%type <n> FieldTerm
%type <n> FunctionTerm
%type <n> IndexFieldTerm
%type <n> MethodTerm
%type <n> MutexTerm
%type <n> OpRegionTerm
%type <n> OpRegionSpaceIdTerm
%type <n> PowerResTerm
%type <n> ProcessorTerm
%type <n> ThermalZoneTerm

/* Namespace modifiers */

%type <n> AliasTerm
%type <n> NameTerm
%type <n> ScopeTerm

/* Type 1 opcodes */

%type <n> BreakPointTerm
%type <n> BreakTerm
%type <n> CaseDefaultTermList
%type <n> CaseTerm
%type <n> ContinueTerm
%type <n> DefaultTerm
%type <n> ElseTerm
%type <n> FatalTerm
%type <n> ElseIfTerm
%type <n> IfTerm
%type <n> LoadTerm
%type <n> NoOpTerm
%type <n> NotifyTerm
%type <n> ReleaseTerm
%type <n> ResetTerm
%type <n> ReturnTerm
%type <n> SignalTerm
%type <n> SleepTerm
%type <n> StallTerm
%type <n> SwitchTerm
%type <n> UnloadTerm
%type <n> WhileTerm
/* %type <n> CaseTermList */

/* Type 2 opcodes */

%type <n> AcquireTerm
%type <n> AddTerm
%type <n> AndTerm
%type <n> ConcatResTerm
%type <n> ConcatTerm
%type <n> CondRefOfTerm
%type <n> CopyObjectTerm
%type <n> DecTerm
%type <n> DerefOfTerm
%type <n> DivideTerm
%type <n> FindSetLeftBitTerm
%type <n> FindSetRightBitTerm
%type <n> FromBCDTerm
%type <n> IncTerm
%type <n> IndexTerm
%type <n> LAndTerm
%type <n> LEqualTerm
%type <n> LGreaterEqualTerm
%type <n> LGreaterTerm
%type <n> LLessEqualTerm
%type <n> LLessTerm
%type <n> LNotEqualTerm
%type <n> LNotTerm
%type <n> LoadTableTerm
%type <n> LOrTerm
%type <n> MatchTerm
%type <n> MidTerm
%type <n> ModTerm
%type <n> MultiplyTerm
%type <n> NAndTerm
%type <n> NOrTerm
%type <n> NotTerm
%type <n> ObjectTypeTerm
%type <n> OrTerm
%type <n> RawDataBufferTerm
%type <n> RefOfTerm
%type <n> ShiftLeftTerm
%type <n> ShiftRightTerm
%type <n> SizeOfTerm
%type <n> StoreTerm
%type <n> SubtractTerm
%type <n> TimerTerm
%type <n> ToBCDTerm
%type <n> ToBufferTerm
%type <n> ToDecimalStringTerm
%type <n> ToHexStringTerm
%type <n> ToIntegerTerm
%type <n> ToStringTerm
%type <n> WaitTerm
%type <n> XOrTerm

/* Keywords */

%type <n> AccessAttribKeyword
%type <n> AccessTypeKeyword
%type <n> AddressingModeKeyword
%type <n> AddressKeyword
%type <n> AddressSpaceKeyword
%type <n> BitsPerByteKeyword
%type <n> ClockPhaseKeyword
%type <n> ClockPolarityKeyword
%type <n> DecodeKeyword
%type <n> DevicePolarityKeyword
%type <n> DMATypeKeyword
%type <n> EndianKeyword
%type <n> FlowControlKeyword
%type <n> InterruptLevel
%type <n> InterruptTypeKeyword
%type <n> IODecodeKeyword
%type <n> IoRestrictionKeyword
%type <n> LockRuleKeyword
%type <n> MatchOpKeyword
%type <n> MaxKeyword
%type <n> MemTypeKeyword
%type <n> MinKeyword
%type <n> ObjectTypeKeyword
%type <n> OptionalBusMasterKeyword
%type <n> OptionalReadWriteKeyword
%type <n> ParityTypeKeyword
%type <n> PinConfigByte
%type <n> PinConfigKeyword
%type <n> RangeTypeKeyword
%type <n> RegionSpaceKeyword
%type <n> ResourceTypeKeyword
%type <n> SerializeRuleKeyword
%type <n> ShareTypeKeyword
%type <n> SlaveModeKeyword
%type <n> StopBitsKeyword
%type <n> TranslationKeyword
%type <n> TypeKeyword
%type <n> UpdateRuleKeyword
%type <n> WireModeKeyword
%type <n> XferSizeKeyword
%type <n> XferTypeKeyword

/* Types */

%type <n> SuperName
%type <n> ObjectTypeSource
%type <n> DerefOfSource
%type <n> RefOfSource
%type <n> CondRefOfSource
%type <n> ArgTerm
%type <n> LocalTerm
%type <n> DebugTerm

%type <n> Integer
%type <n> ByteConst
%type <n> WordConst
%type <n> DWordConst
%type <n> QWordConst
%type <n> String

%type <n> ConstTerm
%type <n> ConstExprTerm
%type <n> ByteConstExpr
%type <n> WordConstExpr
%type <n> DWordConstExpr
%type <n> QWordConstExpr

%type <n> DWordList
%type <n> BufferTerm
%type <n> ByteList

%type <n> PackageElement
%type <n> PackageList
%type <n> PackageTerm

/* Macros */

%type <n> EISAIDTerm
%type <n> ResourceMacroList
%type <n> ResourceMacroTerm
%type <n> ResourceTemplateTerm
%type <n> PldKeyword
%type <n> PldKeywordList
%type <n> ToPLDTerm
%type <n> ToUUIDTerm
%type <n> UnicodeTerm
%type <n> PrintfArgList
%type <n> PrintfTerm
%type <n> FprintfTerm
%type <n> ForTerm

/* Resource Descriptors */

%type <n> ConnectionTerm
%type <n> DMATerm
%type <n> DWordIOTerm
%type <n> DWordMemoryTerm
%type <n> DWordSpaceTerm
%type <n> EndDependentFnTerm
%type <n> ExtendedIOTerm
%type <n> ExtendedMemoryTerm
%type <n> ExtendedSpaceTerm
%type <n> FixedDmaTerm
%type <n> FixedIOTerm
%type <n> GpioIntTerm
%type <n> GpioIoTerm
%type <n> I2cSerialBusTerm
%type <n> I2cSerialBusTermV2
%type <n> InterruptTerm
%type <n> IOTerm
%type <n> IRQNoFlagsTerm
%type <n> IRQTerm
%type <n> Memory24Term
%type <n> Memory32FixedTerm
%type <n> Memory32Term
%type <n> NameSeg
%type <n> NameString
%type <n> PinConfigTerm
%type <n> PinFunctionTerm
%type <n> PinGroupTerm
%type <n> PinGroupConfigTerm
%type <n> PinGroupFunctionTerm
%type <n> QWordIOTerm
%type <n> QWordMemoryTerm
%type <n> QWordSpaceTerm
%type <n> RegisterTerm
%type <n> SpiSerialBusTerm
%type <n> SpiSerialBusTermV2
%type <n> StartDependentFnNoPriTerm
%type <n> StartDependentFnTerm
%type <n> UartSerialBusTerm
%type <n> UartSerialBusTermV2
%type <n> VendorLongTerm
%type <n> VendorShortTerm
%type <n> WordBusNumberTerm
%type <n> WordIOTerm
%type <n> WordSpaceTerm

/* Local types that help construct the AML, not in ACPI spec */

%type <n> AmlPackageLengthTerm
%type <n> IncludeEndTerm
%type <n> NameStringItem
%type <n> TermArgItem

%type <n> OptionalAccessSize
%type <n> OptionalAccessTypeKeyword
%type <n> OptionalAddressingMode
%type <n> OptionalAddressRange
%type <n> OptionalBitsPerByte
%type <n> OptionalBuffer_Last
%type <n> OptionalByteConstExpr
%type <n> OptionalCount
%type <n> OptionalDataCount
%type <n> OptionalDecodeType
%type <n> OptionalDevicePolarity
%type <n> OptionalDWordConstExpr
%type <n> OptionalEndian
%type <n> OptionalFlowControl
%type <n> OptionalIoRestriction
%type <n> OptionalListString
%type <n> OptionalLockRuleKeyword
%type <n> OptionalMaxType
%type <n> OptionalMemType
%type <n> OptionalMinType
%type <n> OptionalNameString
%type <n> OptionalNameString_First
%type <n> OptionalNameString_Last
%type <n> OptionalObjectTypeKeyword
%type <n> OptionalParameterTypePackage
%type <n> OptionalParameterTypesPackage
%type <n> OptionalParentheses
%type <n> OptionalParityType
%type <n> OptionalPredicate
%type <n> OptionalQWordConstExpr
%type <n> OptionalRangeType
%type <n> OptionalReference
%type <n> OptionalResourceType
%type <n> OptionalResourceType_First
%type <n> OptionalProducerResourceType
%type <n> OptionalReturnArg
%type <n> OptionalSerializeRuleKeyword
%type <n> OptionalShareType
%type <n> OptionalShareType_First
%type <n> OptionalSlaveMode
%type <n> OptionalStopBits
%type <n> OptionalStringData
%type <n> OptionalSyncLevel
%type <n> OptionalTermArg
%type <n> OptionalTranslationType_Last
%type <n> OptionalType
%type <n> OptionalType_Last
%type <n> OptionalUpdateRuleKeyword
%type <n> OptionalWireMode
%type <n> OptionalWordConst
%type <n> OptionalWordConstExpr
%type <n> OptionalXferSize

/*
 * ASL+ (C-style) parser
 */

/* Expressions and symbolic operators */

%type <n> Expression
%type <n> EqualsTerm
%type <n> IndexExpTerm

/* ASL+ Named object declaration support */
/*
%type <n> NameTermAslPlus

%type <n> BufferBegin
%type <n> BufferEnd
%type <n> PackageBegin
%type <n> PackageEnd
%type <n> OptionalLength
*/
/* ASL+ Structure declarations */
/*
%type <n> StructureTerm
%type <n> StructureTermBegin
%type <n> StructureType
%type <n> StructureTag
%type <n> StructureElementList
%type <n> StructureElement
%type <n> StructureElementType
%type <n> OptionalStructureElementType
%type <n> StructureId
*/
/* Structure instantiantion */
/*
%type <n> StructureInstanceTerm
%type <n> StructureTagReference
%type <n> StructureInstanceEnd
*/
/* Pseudo-instantiantion for method Args/Locals */
/*
%type <n> MethodStructureTerm
%type <n> LocalStructureName
*/
/* Direct structure references via the Index operator */
/*
%type <n> StructureReference
%type <n> StructureIndexTerm
%type <n> StructurePointerTerm
%type <n> StructurePointerReference
%type <n> OptionalDefinePointer
*/

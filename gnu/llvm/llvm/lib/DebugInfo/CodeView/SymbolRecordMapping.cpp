//===- SymbolRecordMapping.cpp -----------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/SymbolRecordMapping.h"

using namespace llvm;
using namespace llvm::codeview;

#define error(X)                                                               \
  if (auto EC = X)                                                             \
    return EC;

namespace {
struct MapGap {
  Error operator()(CodeViewRecordIO &IO, LocalVariableAddrGap &Gap) const {
    error(IO.mapInteger(Gap.GapStartOffset));
    error(IO.mapInteger(Gap.Range));
    return Error::success();
  }
};
}

static Error mapLocalVariableAddrRange(CodeViewRecordIO &IO,
                                       LocalVariableAddrRange &Range) {
  error(IO.mapInteger(Range.OffsetStart));
  error(IO.mapInteger(Range.ISectStart));
  error(IO.mapInteger(Range.Range));
  return Error::success();
}

Error SymbolRecordMapping::visitSymbolBegin(CVSymbol &Record) {
  error(IO.beginRecord(MaxRecordLength - sizeof(RecordPrefix)));
  return Error::success();
}

Error SymbolRecordMapping::visitSymbolEnd(CVSymbol &Record) {
  error(IO.padToAlignment(alignOf(Container)));
  error(IO.endRecord());
  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR, BlockSym &Block) {

  error(IO.mapInteger(Block.Parent));
  error(IO.mapInteger(Block.End));
  error(IO.mapInteger(Block.CodeSize));
  error(IO.mapInteger(Block.CodeOffset));
  error(IO.mapInteger(Block.Segment));
  error(IO.mapStringZ(Block.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR, Thunk32Sym &Thunk) {

  error(IO.mapInteger(Thunk.Parent));
  error(IO.mapInteger(Thunk.End));
  error(IO.mapInteger(Thunk.Next));
  error(IO.mapInteger(Thunk.Offset));
  error(IO.mapInteger(Thunk.Segment));
  error(IO.mapInteger(Thunk.Length));
  error(IO.mapEnum(Thunk.Thunk));
  error(IO.mapStringZ(Thunk.Name));
  error(IO.mapByteVectorTail(Thunk.VariantData));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            TrampolineSym &Tramp) {

  error(IO.mapEnum(Tramp.Type));
  error(IO.mapInteger(Tramp.Size));
  error(IO.mapInteger(Tramp.ThunkOffset));
  error(IO.mapInteger(Tramp.TargetOffset));
  error(IO.mapInteger(Tramp.ThunkSection));
  error(IO.mapInteger(Tramp.TargetSection));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            SectionSym &Section) {
  uint8_t Padding = 0;

  error(IO.mapInteger(Section.SectionNumber));
  error(IO.mapInteger(Section.Alignment));
  error(IO.mapInteger(Padding));
  error(IO.mapInteger(Section.Rva));
  error(IO.mapInteger(Section.Length));
  error(IO.mapInteger(Section.Characteristics));
  error(IO.mapStringZ(Section.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            CoffGroupSym &CoffGroup) {

  error(IO.mapInteger(CoffGroup.Size));
  error(IO.mapInteger(CoffGroup.Characteristics));
  error(IO.mapInteger(CoffGroup.Offset));
  error(IO.mapInteger(CoffGroup.Segment));
  error(IO.mapStringZ(CoffGroup.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            BPRelativeSym &BPRel) {

  error(IO.mapInteger(BPRel.Offset));
  error(IO.mapInteger(BPRel.Type));
  error(IO.mapStringZ(BPRel.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            BuildInfoSym &BuildInfo) {

  error(IO.mapInteger(BuildInfo.BuildId));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            CallSiteInfoSym &CallSiteInfo) {
  uint16_t Padding = 0;

  error(IO.mapInteger(CallSiteInfo.CodeOffset));
  error(IO.mapInteger(CallSiteInfo.Segment));
  error(IO.mapInteger(Padding));
  error(IO.mapInteger(CallSiteInfo.Type));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            EnvBlockSym &EnvBlock) {

  uint8_t Reserved = 0;
  error(IO.mapInteger(Reserved));
  error(IO.mapStringZVectorZ(EnvBlock.Fields));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            FileStaticSym &FileStatic) {

  error(IO.mapInteger(FileStatic.Index));
  error(IO.mapInteger(FileStatic.ModFilenameOffset));
  error(IO.mapEnum(FileStatic.Flags));
  error(IO.mapStringZ(FileStatic.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR, ExportSym &Export) {

  error(IO.mapInteger(Export.Ordinal));
  error(IO.mapEnum(Export.Flags));
  error(IO.mapStringZ(Export.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            Compile2Sym &Compile2) {

  error(IO.mapEnum(Compile2.Flags));
  error(IO.mapEnum(Compile2.Machine));
  error(IO.mapInteger(Compile2.VersionFrontendMajor));
  error(IO.mapInteger(Compile2.VersionFrontendMinor));
  error(IO.mapInteger(Compile2.VersionFrontendBuild));
  error(IO.mapInteger(Compile2.VersionBackendMajor));
  error(IO.mapInteger(Compile2.VersionBackendMinor));
  error(IO.mapInteger(Compile2.VersionBackendBuild));
  error(IO.mapStringZ(Compile2.Version));
  error(IO.mapStringZVectorZ(Compile2.ExtraStrings));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            Compile3Sym &Compile3) {

  error(IO.mapEnum(Compile3.Flags));
  error(IO.mapEnum(Compile3.Machine));
  error(IO.mapInteger(Compile3.VersionFrontendMajor));
  error(IO.mapInteger(Compile3.VersionFrontendMinor));
  error(IO.mapInteger(Compile3.VersionFrontendBuild));
  error(IO.mapInteger(Compile3.VersionFrontendQFE));
  error(IO.mapInteger(Compile3.VersionBackendMajor));
  error(IO.mapInteger(Compile3.VersionBackendMinor));
  error(IO.mapInteger(Compile3.VersionBackendBuild));
  error(IO.mapInteger(Compile3.VersionBackendQFE));
  error(IO.mapStringZ(Compile3.Version));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            ConstantSym &Constant) {

  error(IO.mapInteger(Constant.Type));
  error(IO.mapEncodedInteger(Constant.Value));
  error(IO.mapStringZ(Constant.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR, DataSym &Data) {

  error(IO.mapInteger(Data.Type));
  error(IO.mapInteger(Data.DataOffset));
  error(IO.mapInteger(Data.Segment));
  error(IO.mapStringZ(Data.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(
    CVSymbol &CVR, DefRangeFramePointerRelSym &DefRangeFramePointerRel) {

  error(IO.mapObject(DefRangeFramePointerRel.Hdr.Offset));
  error(mapLocalVariableAddrRange(IO, DefRangeFramePointerRel.Range));
  error(IO.mapVectorTail(DefRangeFramePointerRel.Gaps, MapGap()));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(
    CVSymbol &CVR,
    DefRangeFramePointerRelFullScopeSym &DefRangeFramePointerRelFullScope) {

  error(IO.mapInteger(DefRangeFramePointerRelFullScope.Offset));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(
    CVSymbol &CVR, DefRangeRegisterRelSym &DefRangeRegisterRel) {

  error(IO.mapObject(DefRangeRegisterRel.Hdr.Register));
  error(IO.mapObject(DefRangeRegisterRel.Hdr.Flags));
  error(IO.mapObject(DefRangeRegisterRel.Hdr.BasePointerOffset));
  error(mapLocalVariableAddrRange(IO, DefRangeRegisterRel.Range));
  error(IO.mapVectorTail(DefRangeRegisterRel.Gaps, MapGap()));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(
    CVSymbol &CVR, DefRangeRegisterSym &DefRangeRegister) {

  error(IO.mapObject(DefRangeRegister.Hdr.Register));
  error(IO.mapObject(DefRangeRegister.Hdr.MayHaveNoName));
  error(mapLocalVariableAddrRange(IO, DefRangeRegister.Range));
  error(IO.mapVectorTail(DefRangeRegister.Gaps, MapGap()));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(
    CVSymbol &CVR, DefRangeSubfieldRegisterSym &DefRangeSubfieldRegister) {

  error(IO.mapObject(DefRangeSubfieldRegister.Hdr.Register));
  error(IO.mapObject(DefRangeSubfieldRegister.Hdr.MayHaveNoName));
  error(IO.mapObject(DefRangeSubfieldRegister.Hdr.OffsetInParent));
  error(mapLocalVariableAddrRange(IO, DefRangeSubfieldRegister.Range));
  error(IO.mapVectorTail(DefRangeSubfieldRegister.Gaps, MapGap()));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(
    CVSymbol &CVR, DefRangeSubfieldSym &DefRangeSubfield) {

  error(IO.mapInteger(DefRangeSubfield.Program));
  error(IO.mapInteger(DefRangeSubfield.OffsetInParent));
  error(mapLocalVariableAddrRange(IO, DefRangeSubfield.Range));
  error(IO.mapVectorTail(DefRangeSubfield.Gaps, MapGap()));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            DefRangeSym &DefRange) {

  error(IO.mapInteger(DefRange.Program));
  error(mapLocalVariableAddrRange(IO, DefRange.Range));
  error(IO.mapVectorTail(DefRange.Gaps, MapGap()));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            FrameCookieSym &FrameCookie) {

  error(IO.mapInteger(FrameCookie.CodeOffset));
  error(IO.mapInteger(FrameCookie.Register));
  error(IO.mapEnum(FrameCookie.CookieKind));
  error(IO.mapInteger(FrameCookie.Flags));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            FrameProcSym &FrameProc) {
  error(IO.mapInteger(FrameProc.TotalFrameBytes));
  error(IO.mapInteger(FrameProc.PaddingFrameBytes));
  error(IO.mapInteger(FrameProc.OffsetToPadding));
  error(IO.mapInteger(FrameProc.BytesOfCalleeSavedRegisters));
  error(IO.mapInteger(FrameProc.OffsetOfExceptionHandler));
  error(IO.mapInteger(FrameProc.SectionIdOfExceptionHandler));
  error(IO.mapEnum(FrameProc.Flags));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(
    CVSymbol &CVR, HeapAllocationSiteSym &HeapAllocSite) {

  error(IO.mapInteger(HeapAllocSite.CodeOffset));
  error(IO.mapInteger(HeapAllocSite.Segment));
  error(IO.mapInteger(HeapAllocSite.CallInstructionSize));
  error(IO.mapInteger(HeapAllocSite.Type));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            InlineSiteSym &InlineSite) {

  error(IO.mapInteger(InlineSite.Parent));
  error(IO.mapInteger(InlineSite.End));
  error(IO.mapInteger(InlineSite.Inlinee));
  error(IO.mapByteVectorTail(InlineSite.AnnotationData));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            RegisterSym &Register) {

  error(IO.mapInteger(Register.Index));
  error(IO.mapEnum(Register.Register));
  error(IO.mapStringZ(Register.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            PublicSym32 &Public) {

  error(IO.mapEnum(Public.Flags));
  error(IO.mapInteger(Public.Offset));
  error(IO.mapInteger(Public.Segment));
  error(IO.mapStringZ(Public.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            ProcRefSym &ProcRef) {

  error(IO.mapInteger(ProcRef.SumName));
  error(IO.mapInteger(ProcRef.SymOffset));
  error(IO.mapInteger(ProcRef.Module));
  error(IO.mapStringZ(ProcRef.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR, LabelSym &Label) {

  error(IO.mapInteger(Label.CodeOffset));
  error(IO.mapInteger(Label.Segment));
  error(IO.mapEnum(Label.Flags));
  error(IO.mapStringZ(Label.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR, LocalSym &Local) {
  error(IO.mapInteger(Local.Type));
  error(IO.mapEnum(Local.Flags));
  error(IO.mapStringZ(Local.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            ObjNameSym &ObjName) {

  error(IO.mapInteger(ObjName.Signature));
  error(IO.mapStringZ(ObjName.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR, ProcSym &Proc) {
  error(IO.mapInteger(Proc.Parent));
  error(IO.mapInteger(Proc.End));
  error(IO.mapInteger(Proc.Next));
  error(IO.mapInteger(Proc.CodeSize));
  error(IO.mapInteger(Proc.DbgStart));
  error(IO.mapInteger(Proc.DbgEnd));
  error(IO.mapInteger(Proc.FunctionType));
  error(IO.mapInteger(Proc.CodeOffset));
  error(IO.mapInteger(Proc.Segment));
  error(IO.mapEnum(Proc.Flags));
  error(IO.mapStringZ(Proc.Name));
  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            ScopeEndSym &ScopeEnd) {
  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR, CallerSym &Caller) {
  error(IO.mapVectorN<uint32_t>(
      Caller.Indices,
      [](CodeViewRecordIO &IO, TypeIndex &N) { return IO.mapInteger(N); }));
  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            RegRelativeSym &RegRel) {

  error(IO.mapInteger(RegRel.Offset));
  error(IO.mapInteger(RegRel.Type));
  error(IO.mapEnum(RegRel.Register));
  error(IO.mapStringZ(RegRel.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            ThreadLocalDataSym &Data) {

  error(IO.mapInteger(Data.Type));
  error(IO.mapInteger(Data.DataOffset));
  error(IO.mapInteger(Data.Segment));
  error(IO.mapStringZ(Data.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR, UDTSym &UDT) {

  error(IO.mapInteger(UDT.Type));
  error(IO.mapStringZ(UDT.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            UsingNamespaceSym &UN) {

  error(IO.mapStringZ(UN.Name));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            AnnotationSym &Annot) {

  error(IO.mapInteger(Annot.CodeOffset));
  error(IO.mapInteger(Annot.Segment));
  error(IO.mapVectorN<uint16_t>(
      Annot.Strings,
      [](CodeViewRecordIO &IO, StringRef &S) { return IO.mapStringZ(S); }));

  return Error::success();
}

Error SymbolRecordMapping::visitKnownRecord(CVSymbol &CVR,
                                            JumpTableSym &JumpTable) {
  error(IO.mapInteger(JumpTable.BaseOffset));
  error(IO.mapInteger(JumpTable.BaseSegment));
  error(IO.mapEnum(JumpTable.SwitchType));
  error(IO.mapInteger(JumpTable.BranchOffset));
  error(IO.mapInteger(JumpTable.TableOffset));
  error(IO.mapInteger(JumpTable.BranchSegment));
  error(IO.mapInteger(JumpTable.TableSegment));
  error(IO.mapInteger(JumpTable.EntriesCount));
  return Error::success();
}

RegisterId codeview::decodeFramePtrReg(EncodedFramePtrReg EncodedReg,
                                       CPUType CPU) {
  assert(unsigned(EncodedReg) < 4);
  switch (CPU) {
  // FIXME: Add ARM and AArch64 variants here.
  default:
    break;
  case CPUType::Intel8080:
  case CPUType::Intel8086:
  case CPUType::Intel80286:
  case CPUType::Intel80386:
  case CPUType::Intel80486:
  case CPUType::Pentium:
  case CPUType::PentiumPro:
  case CPUType::Pentium3:
    switch (EncodedReg) {
    case EncodedFramePtrReg::None:     return RegisterId::NONE;
    case EncodedFramePtrReg::StackPtr: return RegisterId::VFRAME;
    case EncodedFramePtrReg::FramePtr: return RegisterId::EBP;
    case EncodedFramePtrReg::BasePtr:  return RegisterId::EBX;
    }
    llvm_unreachable("bad encoding");
  case CPUType::X64:
    switch (EncodedReg) {
    case EncodedFramePtrReg::None:     return RegisterId::NONE;
    case EncodedFramePtrReg::StackPtr: return RegisterId::RSP;
    case EncodedFramePtrReg::FramePtr: return RegisterId::RBP;
    case EncodedFramePtrReg::BasePtr:  return RegisterId::R13;
    }
    llvm_unreachable("bad encoding");
  }
  return RegisterId::NONE;
}

EncodedFramePtrReg codeview::encodeFramePtrReg(RegisterId Reg, CPUType CPU) {
  switch (CPU) {
  // FIXME: Add ARM and AArch64 variants here.
  default:
    break;
  case CPUType::Intel8080:
  case CPUType::Intel8086:
  case CPUType::Intel80286:
  case CPUType::Intel80386:
  case CPUType::Intel80486:
  case CPUType::Pentium:
  case CPUType::PentiumPro:
  case CPUType::Pentium3:
    switch (Reg) {
    case RegisterId::VFRAME:
      return EncodedFramePtrReg::StackPtr;
    case RegisterId::EBP:
      return EncodedFramePtrReg::FramePtr;
    case RegisterId::EBX:
      return EncodedFramePtrReg::BasePtr;
    default:
      break;
    }
    break;
  case CPUType::X64:
    switch (Reg) {
    case RegisterId::RSP:
      return EncodedFramePtrReg::StackPtr;
    case RegisterId::RBP:
      return EncodedFramePtrReg::FramePtr;
    case RegisterId::R13:
      return EncodedFramePtrReg::BasePtr;
    default:
      break;
    }
    break;
  }
  return EncodedFramePtrReg::None;
}

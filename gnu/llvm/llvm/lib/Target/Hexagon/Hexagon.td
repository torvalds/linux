//===-- Hexagon.td - Describe the Hexagon Target Machine --*- tablegen -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the top level entry point for the Hexagon target.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces which we are implementing
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Hexagon Subtarget features.
//===----------------------------------------------------------------------===//

// Hexagon Architectures
include "HexagonDepArch.td"

def ProcTinyCore: SubtargetFeature<"tinycore", "HexagonProcFamily",
      "TinyCore", "Hexagon Tiny Core">;

// Hexagon ISA Extensions
def ExtensionZReg: SubtargetFeature<"zreg", "UseZRegOps", "true",
      "Hexagon ZReg extension instructions">;
def ExtensionHVXQFloat: SubtargetFeature<"hvx-qfloat", "UseHVXQFloatOps",
      "true", "Hexagon HVX QFloating point instructions">;

def ExtensionHVX: SubtargetFeature<"hvx", "HexagonHVXVersion",
      "Hexagon::ArchEnum::V60", "Hexagon HVX instructions">;
def ExtensionHVXV60: SubtargetFeature<"hvxv60", "HexagonHVXVersion",
      "Hexagon::ArchEnum::V60", "Hexagon HVX instructions",
      [ExtensionHVX]>;
def ExtensionHVXV62: SubtargetFeature<"hvxv62", "HexagonHVXVersion",
      "Hexagon::ArchEnum::V62", "Hexagon HVX instructions",
      [ExtensionHVX, ExtensionHVXV60]>;
def ExtensionHVXV65: SubtargetFeature<"hvxv65", "HexagonHVXVersion",
      "Hexagon::ArchEnum::V65", "Hexagon HVX instructions",
      [ExtensionHVX, ExtensionHVXV60, ExtensionHVXV62]>;
def ExtensionHVXV66: SubtargetFeature<"hvxv66", "HexagonHVXVersion",
      "Hexagon::ArchEnum::V66", "Hexagon HVX instructions",
      [ExtensionHVX, ExtensionHVXV60, ExtensionHVXV62, ExtensionHVXV65,
       ExtensionZReg]>;
def ExtensionHVXV67: SubtargetFeature<"hvxv67", "HexagonHVXVersion",
      "Hexagon::ArchEnum::V67", "Hexagon HVX instructions",
      [ExtensionHVXV60, ExtensionHVXV62, ExtensionHVXV65, ExtensionHVXV66]>;
def ExtensionHVXV68: SubtargetFeature<"hvxv68", "HexagonHVXVersion",
      "Hexagon::ArchEnum::V68", "Hexagon HVX instructions",
      [ExtensionHVXV60, ExtensionHVXV62, ExtensionHVXV65, ExtensionHVXV66,
       ExtensionHVXV67]>;
def ExtensionHVXV69: SubtargetFeature<"hvxv69", "HexagonHVXVersion",
      "Hexagon::ArchEnum::V69", "Hexagon HVX instructions",
      [ExtensionHVXV60, ExtensionHVXV62, ExtensionHVXV65, ExtensionHVXV66,
       ExtensionHVXV67, ExtensionHVXV68]>;
def ExtensionHVXV71: SubtargetFeature<"hvxv71", "HexagonHVXVersion",
      "Hexagon::ArchEnum::V71", "Hexagon HVX instructions",
      [ExtensionHVXV60, ExtensionHVXV62, ExtensionHVXV65, ExtensionHVXV66,
       ExtensionHVXV67, ExtensionHVXV68, ExtensionHVXV69]>;
def ExtensionHVXV73: SubtargetFeature<"hvxv73", "HexagonHVXVersion",
      "Hexagon::ArchEnum::V73", "Hexagon HVX instructions",
      [ExtensionHVXV60, ExtensionHVXV62, ExtensionHVXV65, ExtensionHVXV66,
       ExtensionHVXV67, ExtensionHVXV68, ExtensionHVXV69, ExtensionHVXV71]>;

def ExtensionHVX64B: SubtargetFeature<"hvx-length64b", "UseHVX64BOps",
      "true", "Hexagon HVX 64B instructions", [ExtensionHVX]>;
def ExtensionHVX128B: SubtargetFeature<"hvx-length128b", "UseHVX128BOps",
      "true", "Hexagon HVX 128B instructions", [ExtensionHVX]>;

def ExtensionAudio: SubtargetFeature<"audio", "UseAudioOps", "true",
      "Hexagon Audio extension instructions">;

def ExtensionHVXIEEEFP: SubtargetFeature<"hvx-ieee-fp", "UseHVXIEEEFPOps",
      "true", "Hexagon HVX IEEE floating point instructions">;

def FeatureCompound: SubtargetFeature<"compound", "UseCompound", "true",
      "Use compound instructions">;
def FeaturePackets: SubtargetFeature<"packets", "UsePackets", "true",
      "Support for instruction packets">;
def FeaturePreV65: SubtargetFeature<"prev65", "HasPreV65", "true",
      "Support features deprecated in v65">;
def FeatureLongCalls: SubtargetFeature<"long-calls", "UseLongCalls", "true",
      "Use constant-extended calls">;
def FeatureMemNoShuf: SubtargetFeature<"mem_noshuf", "HasMemNoShuf", "false",
      "Supports mem_noshuf feature">;
def FeatureMemops: SubtargetFeature<"memops", "UseMemops", "true",
      "Use memop instructions">;
def FeatureNVJ: SubtargetFeature<"nvj", "UseNewValueJumps", "true",
      "Support for new-value jumps", [FeaturePackets]>;
def FeatureNVS: SubtargetFeature<"nvs", "UseNewValueStores", "true",
      "Support for new-value stores", [FeaturePackets]>;
def FeatureSmallData: SubtargetFeature<"small-data", "UseSmallData", "true",
      "Allow GP-relative addressing of global variables">;
def FeatureDuplex: SubtargetFeature<"duplex", "EnableDuplex", "true",
      "Enable generation of duplex instruction">;
def FeatureUnsafeFP: SubtargetFeature<"unsafe-fp", "UseUnsafeMath", "true",
      "Use unsafe FP math">;
def FeatureReservedR19: SubtargetFeature<"reserved-r19", "ReservedR19",
      "true", "Reserve register R19">;
def FeatureNoreturnStackElim: SubtargetFeature<"noreturn-stack-elim",
      "NoreturnStackElim", "true",
      "Eliminate stack allocation in a noreturn function when possible">;
def FeatureCabac: SubtargetFeature<"cabac", "UseCabac", "false",
      "Emit the CABAC instruction">;

//===----------------------------------------------------------------------===//
// Hexagon Instruction Predicate Definitions.
//===----------------------------------------------------------------------===//

def UseMEMOPS          : Predicate<"HST->useMemops()">;
def UseHVX64B          : Predicate<"HST->useHVX64BOps()">,
                         AssemblerPredicate<(all_of ExtensionHVX64B)>;
def UseHVX128B         : Predicate<"HST->useHVX128BOps()">,
                         AssemblerPredicate<(all_of ExtensionHVX128B)>;
def UseHVX             : Predicate<"HST->useHVXOps()">,
                         AssemblerPredicate<(all_of ExtensionHVXV60)>;
def UseHVXV60          : Predicate<"HST->useHVXV60Ops()">,
                         AssemblerPredicate<(all_of ExtensionHVXV60)>;
def UseHVXV62          : Predicate<"HST->useHVXV62Ops()">,
                         AssemblerPredicate<(all_of ExtensionHVXV62)>;
def UseHVXV65          : Predicate<"HST->useHVXV65Ops()">,
                         AssemblerPredicate<(all_of ExtensionHVXV65)>;
def UseHVXV66          : Predicate<"HST->useHVXV66Ops()">,
                         AssemblerPredicate<(all_of ExtensionHVXV66)>;
def UseHVXV67          : Predicate<"HST->useHVXV67Ops()">,
                         AssemblerPredicate<(all_of ExtensionHVXV67)>;
def UseHVXV68          : Predicate<"HST->useHVXV68Ops()">,
                         AssemblerPredicate<(all_of ExtensionHVXV68)>;
def UseHVXV69          : Predicate<"HST->useHVXV69Ops()">,
                         AssemblerPredicate<(all_of ExtensionHVXV69)>;
def UseHVXV71          : Predicate<"HST->useHVXV71Ops()">,
                         AssemblerPredicate<(all_of ExtensionHVXV71)>;
def UseHVXV73          : Predicate<"HST->useHVXV73Ops()">,
                         AssemblerPredicate<(all_of ExtensionHVXV73)>;
def UseAudio           : Predicate<"HST->useAudioOps()">,
                         AssemblerPredicate<(all_of ExtensionAudio)>;
def UseZReg            : Predicate<"HST->useZRegOps()">,
                         AssemblerPredicate<(all_of ExtensionZReg)>;
def UseCompound        : Predicate<"HST->useCompound()">;
def HasPreV65          : Predicate<"HST->hasPreV65()">,
                         AssemblerPredicate<(all_of FeaturePreV65)>;
def UseHVXIEEEFP       : Predicate<"HST->useHVXIEEEFPOps()">,
                         AssemblerPredicate<(all_of ExtensionHVXIEEEFP)>;
def UseHVXQFloat       : Predicate<"HST->useHVXQFloatOps()">,
                         AssemblerPredicate<(all_of ExtensionHVXQFloat)>;
def UseHVXFloatingPoint: Predicate<"HST->useHVXFloatingPoint()">;
def HasMemNoShuf       : Predicate<"HST->hasMemNoShuf()">,
                         AssemblerPredicate<(all_of FeatureMemNoShuf)>;
def UseUnsafeMath      : Predicate<"HST->useUnsafeMath()">;
def NotOptTinyCore     : Predicate<"!HST->isTinyCore() ||"
                                   "MF->getFunction().hasOptSize()"> {
  let RecomputePerFunction = 1;
}
def UseSmallData       : Predicate<"HST->useSmallData()">;
def UseCabac           : Predicate<"HST->useCabac()">,
                         AssemblerPredicate<(any_of FeatureCabac)>;

def Hvx64:  HwMode<"+hvx-length64b", [UseHVX64B]>;
def Hvx128: HwMode<"+hvx-length128b", [UseHVX128B]>;

//===----------------------------------------------------------------------===//
// Classes used for relation maps.
//===----------------------------------------------------------------------===//

// The classes below should remain in hierarchical order...
class ImmRegShl;
// ImmRegRel - Filter class used to relate instructions having reg-reg form
// with their reg-imm counterparts.
class ImmRegRel;
// PredRel - Filter class used to relate non-predicated instructions with their
// predicated forms.
class PredRel;
class PredNewRel: PredRel;
// NewValueRel - Filter class used to relate regular store instructions with
// their new-value store form.
class NewValueRel: PredNewRel;
class AddrModeRel: NewValueRel;
class PostInc_BaseImm;
class IntrinsicsRel;
// ... through here.

//===----------------------------------------------------------------------===//
// Generate mapping table to relate non-predicate instructions with their
// predicated formats - true and false.
//

def getPredOpcode : InstrMapping {
  let FilterClass = "PredRel";
  // Instructions with the same BaseOpcode and isNVStore values form a row.
  let RowFields = ["BaseOpcode", "isNVStore", "PNewValue", "isBrTaken", "isNT"];
  // Instructions with the same predicate sense form a column.
  let ColFields = ["PredSense"];
  // The key column is the unpredicated instructions.
  let KeyCol = [""];
  // Value columns are PredSense=true and PredSense=false
  let ValueCols = [["true"], ["false"]];
}

//===----------------------------------------------------------------------===//
// Generate mapping table to relate predicate-true instructions with their
// predicate-false forms
//
def getFalsePredOpcode : InstrMapping {
  let FilterClass = "PredRel";
  let RowFields = ["BaseOpcode", "PNewValue", "isNVStore", "isBrTaken", "isNT"];
  let ColFields = ["PredSense"];
  let KeyCol = ["true"];
  let ValueCols = [["false"]];
}

//===----------------------------------------------------------------------===//
// Generate mapping table to relate predicate-false instructions with their
// predicate-true forms
//
def getTruePredOpcode : InstrMapping {
  let FilterClass = "PredRel";
  let RowFields = ["BaseOpcode", "PNewValue", "isNVStore", "isBrTaken", "isNT"];
  let ColFields = ["PredSense"];
  let KeyCol = ["false"];
  let ValueCols = [["true"]];
}

//===----------------------------------------------------------------------===//
// Generate mapping table to relate predicated instructions with their .new
// format.
//
def getPredNewOpcode : InstrMapping {
  let FilterClass = "PredNewRel";
  let RowFields = ["BaseOpcode", "PredSense", "isNVStore", "isBrTaken"];
  let ColFields = ["PNewValue"];
  let KeyCol = [""];
  let ValueCols = [["new"]];
}

//===----------------------------------------------------------------------===//
// Generate mapping table to relate .new predicated instructions with their old
// format.
//
def getPredOldOpcode : InstrMapping {
  let FilterClass = "PredNewRel";
  let RowFields = ["BaseOpcode", "PredSense", "isNVStore", "isBrTaken"];
  let ColFields = ["PNewValue"];
  let KeyCol = ["new"];
  let ValueCols = [[""]];
}

//===----------------------------------------------------------------------===//
// Generate mapping table to relate store instructions with their new-value
// format.
//
def getNewValueOpcode : InstrMapping {
  let FilterClass = "NewValueRel";
  let RowFields = ["BaseOpcode", "PredSense", "PNewValue", "addrMode", "isNT"];
  let ColFields = ["NValueST"];
  let KeyCol = ["false"];
  let ValueCols = [["true"]];
}

//===----------------------------------------------------------------------===//
// Generate mapping table to relate new-value store instructions with their old
// format.
//
def getNonNVStore : InstrMapping {
  let FilterClass = "NewValueRel";
  let RowFields = ["BaseOpcode", "PredSense", "PNewValue", "addrMode", "isNT"];
  let ColFields = ["NValueST"];
  let KeyCol = ["true"];
  let ValueCols = [["false"]];
}

def changeAddrMode_abs_io: InstrMapping {
  let FilterClass = "AddrModeRel";
  let RowFields = ["CextOpcode", "PredSense", "PNewValue", "isNVStore",
                   "isFloat"];
  let ColFields = ["addrMode"];
  let KeyCol = ["Absolute"];
  let ValueCols = [["BaseImmOffset"]];
}

def changeAddrMode_io_abs: InstrMapping {
  let FilterClass = "AddrModeRel";
  let RowFields = ["CextOpcode", "PredSense", "PNewValue", "isNVStore",
                   "isFloat"];
  let ColFields = ["addrMode"];
  let KeyCol = ["BaseImmOffset"];
  let ValueCols = [["Absolute"]];
}

def changeAddrMode_io_rr: InstrMapping {
  let FilterClass = "AddrModeRel";
  let RowFields = ["CextOpcode", "PredSense", "PNewValue", "isNVStore"];
  let ColFields = ["addrMode"];
  let KeyCol = ["BaseImmOffset"];
  let ValueCols = [["BaseRegOffset"]];
}

def changeAddrMode_rr_io: InstrMapping {
  let FilterClass = "AddrModeRel";
  let RowFields = ["CextOpcode", "PredSense", "PNewValue", "isNVStore"];
  let ColFields = ["addrMode"];
  let KeyCol = ["BaseRegOffset"];
  let ValueCols = [["BaseImmOffset"]];
}

def changeAddrMode_pi_io: InstrMapping {
  let FilterClass = "PostInc_BaseImm";
  let RowFields = ["CextOpcode", "PredSense", "PNewValue", "isNVStore"];
  let ColFields = ["addrMode"];
  let KeyCol = ["PostInc"];
  let ValueCols = [["BaseImmOffset"]];
}

def changeAddrMode_io_pi: InstrMapping {
  let FilterClass = "PostInc_BaseImm";
  let RowFields = ["CextOpcode", "PredSense", "PNewValue", "isNVStore"];
  let ColFields = ["addrMode"];
  let KeyCol = ["BaseImmOffset"];
  let ValueCols = [["PostInc"]];
}

def changeAddrMode_rr_ur: InstrMapping {
  let FilterClass = "ImmRegShl";
  let RowFields = ["CextOpcode", "PredSense", "PNewValue", "isNVStore"];
  let ColFields = ["addrMode"];
  let KeyCol = ["BaseRegOffset"];
  let ValueCols = [["BaseLongOffset"]];
}

def changeAddrMode_ur_rr: InstrMapping {
  let FilterClass = "ImmRegShl";
  let RowFields = ["CextOpcode", "PredSense", "PNewValue", "isNVStore"];
  let ColFields = ["addrMode"];
  let KeyCol = ["BaseLongOffset"];
  let ValueCols = [["BaseRegOffset"]];
}

def getRegForm : InstrMapping {
  let FilterClass = "ImmRegRel";
  let RowFields = ["CextOpcode", "PredSense", "PNewValue"];
  let ColFields = ["InputType"];
  let KeyCol = ["imm"];
  let ValueCols = [["reg"]];
}

def notTakenBranchPrediction : InstrMapping {
  let FilterClass = "PredRel";
  let RowFields = ["BaseOpcode", "PNewValue",  "PredSense", "isBranch", "isPredicated"];
  let ColFields = ["isBrTaken"];
  let KeyCol = ["true"];
  let ValueCols = [["false"]];
}

def takenBranchPrediction : InstrMapping {
  let FilterClass = "PredRel";
  let RowFields = ["BaseOpcode", "PNewValue",  "PredSense", "isBranch", "isPredicated"];
  let ColFields = ["isBrTaken"];
  let KeyCol = ["false"];
  let ValueCols = [["true"]];
}

def getRealHWInstr : InstrMapping {
  let FilterClass = "IntrinsicsRel";
  let RowFields = ["BaseOpcode"];
  let ColFields = ["InstrType"];
  let KeyCol = ["Pseudo"];
  let ValueCols = [["Pseudo"], ["Real"]];
}
//===----------------------------------------------------------------------===//
// Register File, Instruction Descriptions
//===----------------------------------------------------------------------===//
include "HexagonSchedule.td"
include "HexagonRegisterInfo.td"
include "HexagonOperands.td"
include "HexagonDepOperands.td"
include "HexagonDepITypes.td"
include "HexagonInstrFormats.td"
include "HexagonDepInstrFormats.td"
include "HexagonDepInstrInfo.td"
include "HexagonCallingConv.td"
include "HexagonPseudo.td"
include "HexagonPatterns.td"
include "HexagonPatternsHVX.td"
include "HexagonPatternsV65.td"
include "HexagonDepMappings.td"
include "HexagonIntrinsics.td"

def HexagonInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Hexagon processors supported.
//===----------------------------------------------------------------------===//

class Proc<string Name, SchedMachineModel Model,
           list<SubtargetFeature> Features>
 : ProcessorModel<Name, Model, Features>;

def : Proc<"generic", HexagonModelV60,
           [ArchV5, ArchV55, ArchV60,
            FeatureCompound, FeatureDuplex, FeaturePreV65, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv5",  HexagonModelV5,
           [ArchV5,
            FeatureCompound, FeatureDuplex, FeaturePreV65, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv55", HexagonModelV55,
           [ArchV5, ArchV55,
            FeatureCompound, FeatureDuplex, FeaturePreV65, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv60", HexagonModelV60,
           [ArchV5, ArchV55, ArchV60,
            FeatureCompound, FeatureDuplex, FeaturePreV65, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv62", HexagonModelV62,
           [ArchV5, ArchV55, ArchV60, ArchV62,
            FeatureCompound, FeatureDuplex, FeaturePreV65, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv65", HexagonModelV65,
           [ArchV5, ArchV55, ArchV60, ArchV62, ArchV65,
            FeatureCompound, FeatureDuplex, FeatureMemNoShuf, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv66", HexagonModelV66,
           [ArchV5, ArchV55, ArchV60, ArchV62, ArchV65, ArchV66,
            FeatureCompound, FeatureDuplex, FeatureMemNoShuf, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv67", HexagonModelV67,
           [ArchV5, ArchV55, ArchV60, ArchV62, ArchV65, ArchV66, ArchV67,
            FeatureCompound, FeatureDuplex, FeatureMemNoShuf, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv68", HexagonModelV68,
           [ArchV5, ArchV55, ArchV60, ArchV62, ArchV65, ArchV66, ArchV67,
            ArchV68,
            FeatureCompound, FeatureDuplex, FeatureMemNoShuf, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv69", HexagonModelV69,
           [ArchV5, ArchV55, ArchV60, ArchV62, ArchV65, ArchV66, ArchV67,
            ArchV68, ArchV69,
            FeatureCompound, FeatureDuplex, FeatureMemNoShuf, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv71", HexagonModelV71,
           [ArchV5, ArchV55, ArchV60, ArchV62, ArchV65, ArchV66, ArchV67,
            ArchV68, ArchV69, ArchV71,
            FeatureCompound, FeatureDuplex, FeatureMemNoShuf, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData,
            FeatureCabac]>;
def : Proc<"hexagonv73", HexagonModelV73,
           [ArchV5, ArchV55, ArchV60, ArchV62, ArchV65, ArchV66, ArchV67,
            ArchV68, ArchV69, ArchV71, ArchV73,
            FeatureCompound, FeatureDuplex, FeatureMemNoShuf, FeatureMemops,
            FeatureNVJ, FeatureNVS, FeaturePackets, FeatureSmallData]>;
// Need to update the correct features for tiny core.
// Disable NewValueJumps since the packetizer is unable to handle a packet with
// a new value jump and another SLOT0 instruction.
def : Proc<"hexagonv67t", HexagonModelV67T,
           [ArchV5, ArchV55, ArchV60, ArchV62, ArchV65, ArchV66, ArchV67,
            ProcTinyCore, ExtensionAudio,
            FeatureCompound, FeatureMemNoShuf, FeatureMemops,
            FeatureNVS, FeaturePackets, FeatureSmallData]>;

def : Proc<"hexagonv71t", HexagonModelV71T,
           [ArchV5, ArchV55, ArchV60, ArchV62, ArchV65, ArchV66, ArchV67,
            ArchV68, ArchV69, ArchV71,
            ProcTinyCore, ExtensionAudio,
            FeatureCompound, FeatureMemNoShuf, FeatureMemops,
            FeatureNVS, FeaturePackets, FeatureSmallData]>;

//===----------------------------------------------------------------------===//
// Declare the target which we are implementing
//===----------------------------------------------------------------------===//

def HexagonAsmParser : AsmParser {
  let ShouldEmitMatchRegisterAltName = 1;
  bit HasMnemonicFirst = 0;
}

def HexagonAsmParserVariant : AsmParserVariant {
  int Variant = 0;
  string TokenizingCharacters = "#()=:.<>!+*-|^&";
  string BreakCharacters = "";
}

def HexagonAsmWriter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  bit isMCAsmWriter = 1;
}

def Hexagon : Target {
  let InstructionSet = HexagonInstrInfo;
  let AssemblyParsers = [HexagonAsmParser];
  let AssemblyParserVariants = [HexagonAsmParserVariant];
  let AssemblyWriters = [HexagonAsmWriter];
  let AllowRegisterRenaming = 1;
}

//===-- llvm/CodeGen/GlobalISel/LegalizationArtifactCombiner.h -----*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file contains some helper functions which try to cleanup artifacts
// such as G_TRUNCs/G_[ZSA]EXTENDS that were created during legalization to make
// the types match. This file also contains some combines of merges that happens
// at the end of the legalization.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LEGALIZATIONARTIFACTCOMBINER_H
#define LLVM_CODEGEN_GLOBALISEL_LEGALIZATIONARTIFACTCOMBINER_H

#include "llvm/ADT/SmallBitVector.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "legalizer"

namespace llvm {
class LegalizationArtifactCombiner {
  MachineIRBuilder &Builder;
  MachineRegisterInfo &MRI;
  const LegalizerInfo &LI;
  GISelKnownBits *KB;

  static bool isArtifactCast(unsigned Opc) {
    switch (Opc) {
    case TargetOpcode::G_TRUNC:
    case TargetOpcode::G_SEXT:
    case TargetOpcode::G_ZEXT:
    case TargetOpcode::G_ANYEXT:
      return true;
    default:
      return false;
    }
  }

public:
  LegalizationArtifactCombiner(MachineIRBuilder &B, MachineRegisterInfo &MRI,
                               const LegalizerInfo &LI,
                               GISelKnownBits *KB = nullptr)
      : Builder(B), MRI(MRI), LI(LI), KB(KB) {}

  bool tryCombineAnyExt(MachineInstr &MI,
                        SmallVectorImpl<MachineInstr *> &DeadInsts,
                        SmallVectorImpl<Register> &UpdatedDefs,
                        GISelObserverWrapper &Observer) {
    using namespace llvm::MIPatternMatch;
    assert(MI.getOpcode() == TargetOpcode::G_ANYEXT);

    Builder.setInstrAndDebugLoc(MI);
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = lookThroughCopyInstrs(MI.getOperand(1).getReg());

    // aext(trunc x) - > aext/copy/trunc x
    Register TruncSrc;
    if (mi_match(SrcReg, MRI, m_GTrunc(m_Reg(TruncSrc)))) {
      LLVM_DEBUG(dbgs() << ".. Combine MI: " << MI;);
      if (MRI.getType(DstReg) == MRI.getType(TruncSrc))
        replaceRegOrBuildCopy(DstReg, TruncSrc, MRI, Builder, UpdatedDefs,
                              Observer);
      else
        Builder.buildAnyExtOrTrunc(DstReg, TruncSrc);
      UpdatedDefs.push_back(DstReg);
      markInstAndDefDead(MI, *MRI.getVRegDef(SrcReg), DeadInsts);
      return true;
    }

    // aext([asz]ext x) -> [asz]ext x
    Register ExtSrc;
    MachineInstr *ExtMI;
    if (mi_match(SrcReg, MRI,
                 m_all_of(m_MInstr(ExtMI), m_any_of(m_GAnyExt(m_Reg(ExtSrc)),
                                                    m_GSExt(m_Reg(ExtSrc)),
                                                    m_GZExt(m_Reg(ExtSrc)))))) {
      Builder.buildInstr(ExtMI->getOpcode(), {DstReg}, {ExtSrc});
      UpdatedDefs.push_back(DstReg);
      markInstAndDefDead(MI, *ExtMI, DeadInsts);
      return true;
    }

    // Try to fold aext(g_constant) when the larger constant type is legal.
    auto *SrcMI = MRI.getVRegDef(SrcReg);
    if (SrcMI->getOpcode() == TargetOpcode::G_CONSTANT) {
      const LLT DstTy = MRI.getType(DstReg);
      if (isInstLegal({TargetOpcode::G_CONSTANT, {DstTy}})) {
        auto &CstVal = SrcMI->getOperand(1);
        auto *MergedLocation = DILocation::getMergedLocation(
            MI.getDebugLoc().get(), SrcMI->getDebugLoc().get());
        // Set the debug location to the merged location of the SrcMI and the MI
        // if the aext fold is successful.
        Builder.setDebugLoc(MergedLocation);
        Builder.buildConstant(
            DstReg, CstVal.getCImm()->getValue().sext(DstTy.getSizeInBits()));
        UpdatedDefs.push_back(DstReg);
        markInstAndDefDead(MI, *SrcMI, DeadInsts);
        return true;
      }
    }
    return tryFoldImplicitDef(MI, DeadInsts, UpdatedDefs);
  }

  bool tryCombineZExt(MachineInstr &MI,
                      SmallVectorImpl<MachineInstr *> &DeadInsts,
                      SmallVectorImpl<Register> &UpdatedDefs,
                      GISelObserverWrapper &Observer) {
    using namespace llvm::MIPatternMatch;
    assert(MI.getOpcode() == TargetOpcode::G_ZEXT);

    Builder.setInstrAndDebugLoc(MI);
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = lookThroughCopyInstrs(MI.getOperand(1).getReg());

    // zext(trunc x) - > and (aext/copy/trunc x), mask
    // zext(sext x) -> and (sext x), mask
    Register TruncSrc;
    Register SextSrc;
    if (mi_match(SrcReg, MRI, m_GTrunc(m_Reg(TruncSrc))) ||
        mi_match(SrcReg, MRI, m_GSExt(m_Reg(SextSrc)))) {
      LLT DstTy = MRI.getType(DstReg);
      if (isInstUnsupported({TargetOpcode::G_AND, {DstTy}}) ||
          isConstantUnsupported(DstTy))
        return false;
      LLVM_DEBUG(dbgs() << ".. Combine MI: " << MI;);
      LLT SrcTy = MRI.getType(SrcReg);
      APInt MaskVal = APInt::getAllOnes(SrcTy.getScalarSizeInBits());
      if (SextSrc && (DstTy != MRI.getType(SextSrc)))
        SextSrc = Builder.buildSExtOrTrunc(DstTy, SextSrc).getReg(0);
      if (TruncSrc && (DstTy != MRI.getType(TruncSrc)))
        TruncSrc = Builder.buildAnyExtOrTrunc(DstTy, TruncSrc).getReg(0);
      APInt ExtMaskVal = MaskVal.zext(DstTy.getScalarSizeInBits());
      Register AndSrc = SextSrc ? SextSrc : TruncSrc;
      // Elide G_AND and mask constant if possible.
      // The G_AND would also be removed by the post-legalize redundant_and
      // combine, but in this very common case, eliding early and regardless of
      // OptLevel results in significant compile-time and O0 code-size
      // improvements. Inserting unnecessary instructions between boolean defs
      // and uses hinders a lot of folding during ISel.
      if (KB && (KB->getKnownZeroes(AndSrc) | ExtMaskVal).isAllOnes()) {
        replaceRegOrBuildCopy(DstReg, AndSrc, MRI, Builder, UpdatedDefs,
                              Observer);
      } else {
        auto Mask = Builder.buildConstant(DstTy, ExtMaskVal);
        Builder.buildAnd(DstReg, AndSrc, Mask);
      }
      markInstAndDefDead(MI, *MRI.getVRegDef(SrcReg), DeadInsts);
      return true;
    }

    // zext(zext x) -> (zext x)
    Register ZextSrc;
    if (mi_match(SrcReg, MRI, m_GZExt(m_Reg(ZextSrc)))) {
      LLVM_DEBUG(dbgs() << ".. Combine MI: " << MI);
      Observer.changingInstr(MI);
      MI.getOperand(1).setReg(ZextSrc);
      Observer.changedInstr(MI);
      UpdatedDefs.push_back(DstReg);
      markDefDead(MI, *MRI.getVRegDef(SrcReg), DeadInsts);
      return true;
    }

    // Try to fold zext(g_constant) when the larger constant type is legal.
    auto *SrcMI = MRI.getVRegDef(SrcReg);
    if (SrcMI->getOpcode() == TargetOpcode::G_CONSTANT) {
      const LLT DstTy = MRI.getType(DstReg);
      if (isInstLegal({TargetOpcode::G_CONSTANT, {DstTy}})) {
        auto &CstVal = SrcMI->getOperand(1);
        Builder.buildConstant(
            DstReg, CstVal.getCImm()->getValue().zext(DstTy.getSizeInBits()));
        UpdatedDefs.push_back(DstReg);
        markInstAndDefDead(MI, *SrcMI, DeadInsts);
        return true;
      }
    }
    return tryFoldImplicitDef(MI, DeadInsts, UpdatedDefs);
  }

  bool tryCombineSExt(MachineInstr &MI,
                      SmallVectorImpl<MachineInstr *> &DeadInsts,
                      SmallVectorImpl<Register> &UpdatedDefs,
                      GISelObserverWrapper &Observer) {
    using namespace llvm::MIPatternMatch;
    assert(MI.getOpcode() == TargetOpcode::G_SEXT);

    Builder.setInstrAndDebugLoc(MI);
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = lookThroughCopyInstrs(MI.getOperand(1).getReg());

    // sext(trunc x) - > (sext_inreg (aext/copy/trunc x), c)
    Register TruncSrc;
    if (mi_match(SrcReg, MRI, m_GTrunc(m_Reg(TruncSrc)))) {
      LLT DstTy = MRI.getType(DstReg);
      if (isInstUnsupported({TargetOpcode::G_SEXT_INREG, {DstTy}}))
        return false;
      LLVM_DEBUG(dbgs() << ".. Combine MI: " << MI;);
      LLT SrcTy = MRI.getType(SrcReg);
      uint64_t SizeInBits = SrcTy.getScalarSizeInBits();
      if (DstTy != MRI.getType(TruncSrc))
        TruncSrc = Builder.buildAnyExtOrTrunc(DstTy, TruncSrc).getReg(0);
      // Elide G_SEXT_INREG if possible. This is similar to eliding G_AND in
      // tryCombineZExt. Refer to the comment in tryCombineZExt for rationale.
      if (KB && KB->computeNumSignBits(TruncSrc) >
                    DstTy.getScalarSizeInBits() - SizeInBits)
        replaceRegOrBuildCopy(DstReg, TruncSrc, MRI, Builder, UpdatedDefs,
                              Observer);
      else
        Builder.buildSExtInReg(DstReg, TruncSrc, SizeInBits);
      markInstAndDefDead(MI, *MRI.getVRegDef(SrcReg), DeadInsts);
      return true;
    }

    // sext(zext x) -> (zext x)
    // sext(sext x) -> (sext x)
    Register ExtSrc;
    MachineInstr *ExtMI;
    if (mi_match(SrcReg, MRI,
                 m_all_of(m_MInstr(ExtMI), m_any_of(m_GZExt(m_Reg(ExtSrc)),
                                                    m_GSExt(m_Reg(ExtSrc)))))) {
      LLVM_DEBUG(dbgs() << ".. Combine MI: " << MI);
      Builder.buildInstr(ExtMI->getOpcode(), {DstReg}, {ExtSrc});
      UpdatedDefs.push_back(DstReg);
      markInstAndDefDead(MI, *MRI.getVRegDef(SrcReg), DeadInsts);
      return true;
    }

    // Try to fold sext(g_constant) when the larger constant type is legal.
    auto *SrcMI = MRI.getVRegDef(SrcReg);
    if (SrcMI->getOpcode() == TargetOpcode::G_CONSTANT) {
      const LLT DstTy = MRI.getType(DstReg);
      if (isInstLegal({TargetOpcode::G_CONSTANT, {DstTy}})) {
        auto &CstVal = SrcMI->getOperand(1);
        Builder.buildConstant(
            DstReg, CstVal.getCImm()->getValue().sext(DstTy.getSizeInBits()));
        UpdatedDefs.push_back(DstReg);
        markInstAndDefDead(MI, *SrcMI, DeadInsts);
        return true;
      }
    }

    return tryFoldImplicitDef(MI, DeadInsts, UpdatedDefs);
  }

  bool tryCombineTrunc(MachineInstr &MI,
                       SmallVectorImpl<MachineInstr *> &DeadInsts,
                       SmallVectorImpl<Register> &UpdatedDefs,
                       GISelObserverWrapper &Observer) {
    using namespace llvm::MIPatternMatch;
    assert(MI.getOpcode() == TargetOpcode::G_TRUNC);

    Builder.setInstr(MI);
    Register DstReg = MI.getOperand(0).getReg();
    const LLT DstTy = MRI.getType(DstReg);
    Register SrcReg = lookThroughCopyInstrs(MI.getOperand(1).getReg());

    // Try to fold trunc(g_constant) when the smaller constant type is legal.
    auto *SrcMI = MRI.getVRegDef(SrcReg);
    if (SrcMI->getOpcode() == TargetOpcode::G_CONSTANT) {
      if (isInstLegal({TargetOpcode::G_CONSTANT, {DstTy}})) {
        auto &CstVal = SrcMI->getOperand(1);
        Builder.buildConstant(
            DstReg, CstVal.getCImm()->getValue().trunc(DstTy.getSizeInBits()));
        UpdatedDefs.push_back(DstReg);
        markInstAndDefDead(MI, *SrcMI, DeadInsts);
        return true;
      }
    }

    // Try to fold trunc(merge) to directly use the source of the merge.
    // This gets rid of large, difficult to legalize, merges
    if (auto *SrcMerge = dyn_cast<GMerge>(SrcMI)) {
      const Register MergeSrcReg = SrcMerge->getSourceReg(0);
      const LLT MergeSrcTy = MRI.getType(MergeSrcReg);

      // We can only fold if the types are scalar
      const unsigned DstSize = DstTy.getSizeInBits();
      const unsigned MergeSrcSize = MergeSrcTy.getSizeInBits();
      if (!DstTy.isScalar() || !MergeSrcTy.isScalar())
        return false;

      if (DstSize < MergeSrcSize) {
        // When the merge source is larger than the destination, we can just
        // truncate the merge source directly
        if (isInstUnsupported({TargetOpcode::G_TRUNC, {DstTy, MergeSrcTy}}))
          return false;

        LLVM_DEBUG(dbgs() << "Combining G_TRUNC(G_MERGE_VALUES) to G_TRUNC: "
                          << MI);

        Builder.buildTrunc(DstReg, MergeSrcReg);
        UpdatedDefs.push_back(DstReg);
      } else if (DstSize == MergeSrcSize) {
        // If the sizes match we can simply try to replace the register
        LLVM_DEBUG(
            dbgs() << "Replacing G_TRUNC(G_MERGE_VALUES) with merge input: "
                   << MI);
        replaceRegOrBuildCopy(DstReg, MergeSrcReg, MRI, Builder, UpdatedDefs,
                              Observer);
      } else if (DstSize % MergeSrcSize == 0) {
        // If the trunc size is a multiple of the merge source size we can use
        // a smaller merge instead
        if (isInstUnsupported(
                {TargetOpcode::G_MERGE_VALUES, {DstTy, MergeSrcTy}}))
          return false;

        LLVM_DEBUG(
            dbgs() << "Combining G_TRUNC(G_MERGE_VALUES) to G_MERGE_VALUES: "
                   << MI);

        const unsigned NumSrcs = DstSize / MergeSrcSize;
        assert(NumSrcs < SrcMI->getNumOperands() - 1 &&
               "trunc(merge) should require less inputs than merge");
        SmallVector<Register, 8> SrcRegs(NumSrcs);
        for (unsigned i = 0; i < NumSrcs; ++i)
          SrcRegs[i] = SrcMerge->getSourceReg(i);

        Builder.buildMergeValues(DstReg, SrcRegs);
        UpdatedDefs.push_back(DstReg);
      } else {
        // Unable to combine
        return false;
      }

      markInstAndDefDead(MI, *SrcMerge, DeadInsts);
      return true;
    }

    // trunc(trunc) -> trunc
    Register TruncSrc;
    if (mi_match(SrcReg, MRI, m_GTrunc(m_Reg(TruncSrc)))) {
      // Always combine trunc(trunc) since the eventual resulting trunc must be
      // legal anyway as it must be legal for all outputs of the consumer type
      // set.
      LLVM_DEBUG(dbgs() << ".. Combine G_TRUNC(G_TRUNC): " << MI);

      Builder.buildTrunc(DstReg, TruncSrc);
      UpdatedDefs.push_back(DstReg);
      markInstAndDefDead(MI, *MRI.getVRegDef(TruncSrc), DeadInsts);
      return true;
    }

    // trunc(ext x) -> x
    ArtifactValueFinder Finder(MRI, Builder, LI);
    if (Register FoundReg =
            Finder.findValueFromDef(DstReg, 0, DstTy.getSizeInBits())) {
      LLT FoundRegTy = MRI.getType(FoundReg);
      if (DstTy == FoundRegTy) {
        LLVM_DEBUG(dbgs() << ".. Combine G_TRUNC(G_[S,Z,ANY]EXT/G_TRUNC...): "
                          << MI;);

        replaceRegOrBuildCopy(DstReg, FoundReg, MRI, Builder, UpdatedDefs,
                              Observer);
        UpdatedDefs.push_back(DstReg);
        markInstAndDefDead(MI, *MRI.getVRegDef(SrcReg), DeadInsts);
        return true;
      }
    }

    return false;
  }

  /// Try to fold G_[ASZ]EXT (G_IMPLICIT_DEF).
  bool tryFoldImplicitDef(MachineInstr &MI,
                          SmallVectorImpl<MachineInstr *> &DeadInsts,
                          SmallVectorImpl<Register> &UpdatedDefs) {
    unsigned Opcode = MI.getOpcode();
    assert(Opcode == TargetOpcode::G_ANYEXT || Opcode == TargetOpcode::G_ZEXT ||
           Opcode == TargetOpcode::G_SEXT);

    if (MachineInstr *DefMI = getOpcodeDef(TargetOpcode::G_IMPLICIT_DEF,
                                           MI.getOperand(1).getReg(), MRI)) {
      Builder.setInstr(MI);
      Register DstReg = MI.getOperand(0).getReg();
      LLT DstTy = MRI.getType(DstReg);

      if (Opcode == TargetOpcode::G_ANYEXT) {
        // G_ANYEXT (G_IMPLICIT_DEF) -> G_IMPLICIT_DEF
        if (!isInstLegal({TargetOpcode::G_IMPLICIT_DEF, {DstTy}}))
          return false;
        LLVM_DEBUG(dbgs() << ".. Combine G_ANYEXT(G_IMPLICIT_DEF): " << MI;);
        Builder.buildInstr(TargetOpcode::G_IMPLICIT_DEF, {DstReg}, {});
        UpdatedDefs.push_back(DstReg);
      } else {
        // G_[SZ]EXT (G_IMPLICIT_DEF) -> G_CONSTANT 0 because the top
        // bits will be 0 for G_ZEXT and 0/1 for the G_SEXT.
        if (isConstantUnsupported(DstTy))
          return false;
        LLVM_DEBUG(dbgs() << ".. Combine G_[SZ]EXT(G_IMPLICIT_DEF): " << MI;);
        Builder.buildConstant(DstReg, 0);
        UpdatedDefs.push_back(DstReg);
      }

      markInstAndDefDead(MI, *DefMI, DeadInsts);
      return true;
    }
    return false;
  }

  bool tryFoldUnmergeCast(MachineInstr &MI, MachineInstr &CastMI,
                          SmallVectorImpl<MachineInstr *> &DeadInsts,
                          SmallVectorImpl<Register> &UpdatedDefs) {

    assert(MI.getOpcode() == TargetOpcode::G_UNMERGE_VALUES);

    const unsigned CastOpc = CastMI.getOpcode();

    if (!isArtifactCast(CastOpc))
      return false;

    const unsigned NumDefs = MI.getNumOperands() - 1;

    const Register CastSrcReg = CastMI.getOperand(1).getReg();
    const LLT CastSrcTy = MRI.getType(CastSrcReg);
    const LLT DestTy = MRI.getType(MI.getOperand(0).getReg());
    const LLT SrcTy = MRI.getType(MI.getOperand(NumDefs).getReg());

    const unsigned CastSrcSize = CastSrcTy.getSizeInBits();
    const unsigned DestSize = DestTy.getSizeInBits();

    if (CastOpc == TargetOpcode::G_TRUNC) {
      if (SrcTy.isVector() && SrcTy.getScalarType() == DestTy.getScalarType()) {
        //  %1:_(<4 x s8>) = G_TRUNC %0(<4 x s32>)
        //  %2:_(s8), %3:_(s8), %4:_(s8), %5:_(s8) = G_UNMERGE_VALUES %1
        // =>
        //  %6:_(s32), %7:_(s32), %8:_(s32), %9:_(s32) = G_UNMERGE_VALUES %0
        //  %2:_(s8) = G_TRUNC %6
        //  %3:_(s8) = G_TRUNC %7
        //  %4:_(s8) = G_TRUNC %8
        //  %5:_(s8) = G_TRUNC %9

        unsigned UnmergeNumElts =
            DestTy.isVector() ? CastSrcTy.getNumElements() / NumDefs : 1;
        LLT UnmergeTy = CastSrcTy.changeElementCount(
            ElementCount::getFixed(UnmergeNumElts));
        LLT SrcWideTy =
            SrcTy.changeElementCount(ElementCount::getFixed(UnmergeNumElts));

        if (isInstUnsupported(
                {TargetOpcode::G_UNMERGE_VALUES, {UnmergeTy, CastSrcTy}}) ||
            LI.getAction({TargetOpcode::G_TRUNC, {SrcWideTy, UnmergeTy}})
                    .Action == LegalizeActions::MoreElements)
          return false;

        Builder.setInstr(MI);
        auto NewUnmerge = Builder.buildUnmerge(UnmergeTy, CastSrcReg);

        for (unsigned I = 0; I != NumDefs; ++I) {
          Register DefReg = MI.getOperand(I).getReg();
          UpdatedDefs.push_back(DefReg);
          Builder.buildTrunc(DefReg, NewUnmerge.getReg(I));
        }

        markInstAndDefDead(MI, CastMI, DeadInsts);
        return true;
      }

      if (CastSrcTy.isScalar() && SrcTy.isScalar() && !DestTy.isVector()) {
        //  %1:_(s16) = G_TRUNC %0(s32)
        //  %2:_(s8), %3:_(s8) = G_UNMERGE_VALUES %1
        // =>
        //  %2:_(s8), %3:_(s8), %4:_(s8), %5:_(s8) = G_UNMERGE_VALUES %0

        // Unmerge(trunc) can be combined if the trunc source size is a multiple
        // of the unmerge destination size
        if (CastSrcSize % DestSize != 0)
          return false;

        // Check if the new unmerge is supported
        if (isInstUnsupported(
                {TargetOpcode::G_UNMERGE_VALUES, {DestTy, CastSrcTy}}))
          return false;

        // Gather the original destination registers and create new ones for the
        // unused bits
        const unsigned NewNumDefs = CastSrcSize / DestSize;
        SmallVector<Register, 8> DstRegs(NewNumDefs);
        for (unsigned Idx = 0; Idx < NewNumDefs; ++Idx) {
          if (Idx < NumDefs)
            DstRegs[Idx] = MI.getOperand(Idx).getReg();
          else
            DstRegs[Idx] = MRI.createGenericVirtualRegister(DestTy);
        }

        // Build new unmerge
        Builder.setInstr(MI);
        Builder.buildUnmerge(DstRegs, CastSrcReg);
        UpdatedDefs.append(DstRegs.begin(), DstRegs.begin() + NewNumDefs);
        markInstAndDefDead(MI, CastMI, DeadInsts);
        return true;
      }
    }

    // TODO: support combines with other casts as well
    return false;
  }

  static bool canFoldMergeOpcode(unsigned MergeOp, unsigned ConvertOp,
                                 LLT OpTy, LLT DestTy) {
    // Check if we found a definition that is like G_MERGE_VALUES.
    switch (MergeOp) {
    default:
      return false;
    case TargetOpcode::G_BUILD_VECTOR:
    case TargetOpcode::G_MERGE_VALUES:
      // The convert operation that we will need to insert is
      // going to convert the input of that type of instruction (scalar)
      // to the destination type (DestTy).
      // The conversion needs to stay in the same domain (scalar to scalar
      // and vector to vector), so if we were to allow to fold the merge
      // we would need to insert some bitcasts.
      // E.g.,
      // <2 x s16> = build_vector s16, s16
      // <2 x s32> = zext <2 x s16>
      // <2 x s16>, <2 x s16> = unmerge <2 x s32>
      //
      // As is the folding would produce:
      // <2 x s16> = zext s16  <-- scalar to vector
      // <2 x s16> = zext s16  <-- scalar to vector
      // Which is invalid.
      // Instead we would want to generate:
      // s32 = zext s16
      // <2 x s16> = bitcast s32
      // s32 = zext s16
      // <2 x s16> = bitcast s32
      //
      // That is not done yet.
      if (ConvertOp == 0)
        return true;
      return !DestTy.isVector() && OpTy.isVector() &&
             DestTy == OpTy.getElementType();
    case TargetOpcode::G_CONCAT_VECTORS: {
      if (ConvertOp == 0)
        return true;
      if (!DestTy.isVector())
        return false;

      const unsigned OpEltSize = OpTy.getElementType().getSizeInBits();

      // Don't handle scalarization with a cast that isn't in the same
      // direction as the vector cast. This could be handled, but it would
      // require more intermediate unmerges.
      if (ConvertOp == TargetOpcode::G_TRUNC)
        return DestTy.getSizeInBits() <= OpEltSize;
      return DestTy.getSizeInBits() >= OpEltSize;
    }
    }
  }

  /// Try to replace DstReg with SrcReg or build a COPY instruction
  /// depending on the register constraints.
  static void replaceRegOrBuildCopy(Register DstReg, Register SrcReg,
                                    MachineRegisterInfo &MRI,
                                    MachineIRBuilder &Builder,
                                    SmallVectorImpl<Register> &UpdatedDefs,
                                    GISelChangeObserver &Observer) {
    if (!llvm::canReplaceReg(DstReg, SrcReg, MRI)) {
      Builder.buildCopy(DstReg, SrcReg);
      UpdatedDefs.push_back(DstReg);
      return;
    }
    SmallVector<MachineInstr *, 4> UseMIs;
    // Get the users and notify the observer before replacing.
    for (auto &UseMI : MRI.use_instructions(DstReg)) {
      UseMIs.push_back(&UseMI);
      Observer.changingInstr(UseMI);
    }
    // Replace the registers.
    MRI.replaceRegWith(DstReg, SrcReg);
    UpdatedDefs.push_back(SrcReg);
    // Notify the observer that we changed the instructions.
    for (auto *UseMI : UseMIs)
      Observer.changedInstr(*UseMI);
  }

  /// Return the operand index in \p MI that defines \p Def
  static unsigned getDefIndex(const MachineInstr &MI, Register SearchDef) {
    unsigned DefIdx = 0;
    for (const MachineOperand &Def : MI.defs()) {
      if (Def.getReg() == SearchDef)
        break;
      ++DefIdx;
    }

    return DefIdx;
  }

  /// This class provides utilities for finding source registers of specific
  /// bit ranges in an artifact. The routines can look through the source
  /// registers if they're other artifacts to try to find a non-artifact source
  /// of a value.
  class ArtifactValueFinder {
    MachineRegisterInfo &MRI;
    MachineIRBuilder &MIB;
    const LegalizerInfo &LI;

    // Stores the best register found in the current query so far.
    Register CurrentBest = Register();

    /// Given an concat_vector op \p Concat and a start bit and size, try to
    /// find the origin of the value defined by that start position and size.
    ///
    /// \returns a register with the requested size, or the current best
    /// register found during the current query.
    Register findValueFromConcat(GConcatVectors &Concat, unsigned StartBit,
                                 unsigned Size) {
      assert(Size > 0);

      // Find the source operand that provides the bits requested.
      Register Src1Reg = Concat.getSourceReg(0);
      unsigned SrcSize = MRI.getType(Src1Reg).getSizeInBits();

      // Operand index of the source that provides the start of the bit range.
      unsigned StartSrcIdx = (StartBit / SrcSize) + 1;
      // Offset into the source at which the bit range starts.
      unsigned InRegOffset = StartBit % SrcSize;
      // Check that the bits don't span multiple sources.
      // FIXME: we might be able return multiple sources? Or create an
      // appropriate concat to make it fit.
      if (InRegOffset + Size > SrcSize)
        return CurrentBest;

      Register SrcReg = Concat.getReg(StartSrcIdx);
      if (InRegOffset == 0 && Size == SrcSize) {
        CurrentBest = SrcReg;
        return findValueFromDefImpl(SrcReg, 0, Size);
      }

      return findValueFromDefImpl(SrcReg, InRegOffset, Size);
    }

    /// Given an build_vector op \p BV and a start bit and size, try to find
    /// the origin of the value defined by that start position and size.
    ///
    /// \returns a register with the requested size, or the current best
    /// register found during the current query.
    Register findValueFromBuildVector(GBuildVector &BV, unsigned StartBit,
                                      unsigned Size) {
      assert(Size > 0);

      // Find the source operand that provides the bits requested.
      Register Src1Reg = BV.getSourceReg(0);
      unsigned SrcSize = MRI.getType(Src1Reg).getSizeInBits();

      // Operand index of the source that provides the start of the bit range.
      unsigned StartSrcIdx = (StartBit / SrcSize) + 1;
      // Offset into the source at which the bit range starts.
      unsigned InRegOffset = StartBit % SrcSize;

      if (InRegOffset != 0)
        return CurrentBest; // Give up, bits don't start at a scalar source.
      if (Size < SrcSize)
        return CurrentBest; // Scalar source is too large for requested bits.

      // If the bits cover multiple sources evenly, then create a new
      // build_vector to synthesize the required size, if that's been requested.
      if (Size > SrcSize) {
        if (Size % SrcSize > 0)
          return CurrentBest; // Isn't covered exactly by sources.

        unsigned NumSrcsUsed = Size / SrcSize;
        // If we're requesting all of the sources, just return this def.
        if (NumSrcsUsed == BV.getNumSources())
          return BV.getReg(0);

        LLT SrcTy = MRI.getType(Src1Reg);
        LLT NewBVTy = LLT::fixed_vector(NumSrcsUsed, SrcTy);

        // Check if the resulting build vector would be legal.
        LegalizeActionStep ActionStep =
            LI.getAction({TargetOpcode::G_BUILD_VECTOR, {NewBVTy, SrcTy}});
        if (ActionStep.Action != LegalizeActions::Legal)
          return CurrentBest;

        SmallVector<Register> NewSrcs;
        for (unsigned SrcIdx = StartSrcIdx; SrcIdx < StartSrcIdx + NumSrcsUsed;
             ++SrcIdx)
          NewSrcs.push_back(BV.getReg(SrcIdx));
        MIB.setInstrAndDebugLoc(BV);
        return MIB.buildBuildVector(NewBVTy, NewSrcs).getReg(0);
      }
      // A single source is requested, just return it.
      return BV.getReg(StartSrcIdx);
    }

    /// Given an G_INSERT op \p MI and a start bit and size, try to find
    /// the origin of the value defined by that start position and size.
    ///
    /// \returns a register with the requested size, or the current best
    /// register found during the current query.
    Register findValueFromInsert(MachineInstr &MI, unsigned StartBit,
                                 unsigned Size) {
      assert(MI.getOpcode() == TargetOpcode::G_INSERT);
      assert(Size > 0);

      Register ContainerSrcReg = MI.getOperand(1).getReg();
      Register InsertedReg = MI.getOperand(2).getReg();
      LLT InsertedRegTy = MRI.getType(InsertedReg);
      unsigned InsertOffset = MI.getOperand(3).getImm();

      // There are 4 possible container/insertreg + requested bit-range layouts
      // that the instruction and query could be representing.
      // For: %_ = G_INSERT %CONTAINER, %INS, InsOff (abbrev. to 'IO')
      // and a start bit 'SB', with size S, giving an end bit 'EB', we could
      // have...
      // Scenario A:
      //   --------------------------
      //  |  INS    |  CONTAINER     |
      //   --------------------------
      //       |   |
      //       SB  EB
      //
      // Scenario B:
      //   --------------------------
      //  |  INS    |  CONTAINER     |
      //   --------------------------
      //                |    |
      //                SB   EB
      //
      // Scenario C:
      //   --------------------------
      //  |  CONTAINER    |  INS     |
      //   --------------------------
      //       |    |
      //       SB   EB
      //
      // Scenario D:
      //   --------------------------
      //  |  CONTAINER    |  INS     |
      //   --------------------------
      //                     |   |
      //                     SB  EB
      //
      // So therefore, A and D are requesting data from the INS operand, while
      // B and C are requesting from the container operand.

      unsigned InsertedEndBit = InsertOffset + InsertedRegTy.getSizeInBits();
      unsigned EndBit = StartBit + Size;
      unsigned NewStartBit;
      Register SrcRegToUse;
      if (EndBit <= InsertOffset || InsertedEndBit <= StartBit) {
        SrcRegToUse = ContainerSrcReg;
        NewStartBit = StartBit;
        return findValueFromDefImpl(SrcRegToUse, NewStartBit, Size);
      }
      if (InsertOffset <= StartBit && EndBit <= InsertedEndBit) {
        SrcRegToUse = InsertedReg;
        NewStartBit = StartBit - InsertOffset;
        if (NewStartBit == 0 &&
            Size == MRI.getType(SrcRegToUse).getSizeInBits())
          CurrentBest = SrcRegToUse;
        return findValueFromDefImpl(SrcRegToUse, NewStartBit, Size);
      }
      // The bit range spans both the inserted and container regions.
      return Register();
    }

    /// Given an G_SEXT, G_ZEXT, G_ANYEXT op \p MI and a start bit and
    /// size, try to find the origin of the value defined by that start
    /// position and size.
    ///
    /// \returns a register with the requested size, or the current best
    /// register found during the current query.
    Register findValueFromExt(MachineInstr &MI, unsigned StartBit,
                              unsigned Size) {
      assert(MI.getOpcode() == TargetOpcode::G_SEXT ||
             MI.getOpcode() == TargetOpcode::G_ZEXT ||
             MI.getOpcode() == TargetOpcode::G_ANYEXT);
      assert(Size > 0);

      Register SrcReg = MI.getOperand(1).getReg();
      LLT SrcType = MRI.getType(SrcReg);
      unsigned SrcSize = SrcType.getSizeInBits();

      // Currently we don't go into vectors.
      if (!SrcType.isScalar())
        return CurrentBest;

      if (StartBit + Size > SrcSize)
        return CurrentBest;

      if (StartBit == 0 && SrcType.getSizeInBits() == Size)
        CurrentBest = SrcReg;
      return findValueFromDefImpl(SrcReg, StartBit, Size);
    }

    /// Given an G_TRUNC op \p MI and a start bit and size, try to find
    /// the origin of the value defined by that start position and size.
    ///
    /// \returns a register with the requested size, or the current best
    /// register found during the current query.
    Register findValueFromTrunc(MachineInstr &MI, unsigned StartBit,
                                unsigned Size) {
      assert(MI.getOpcode() == TargetOpcode::G_TRUNC);
      assert(Size > 0);

      Register SrcReg = MI.getOperand(1).getReg();
      LLT SrcType = MRI.getType(SrcReg);

      // Currently we don't go into vectors.
      if (!SrcType.isScalar())
        return CurrentBest;

      return findValueFromDefImpl(SrcReg, StartBit, Size);
    }

    /// Internal implementation for findValueFromDef(). findValueFromDef()
    /// initializes some data like the CurrentBest register, which this method
    /// and its callees rely upon.
    Register findValueFromDefImpl(Register DefReg, unsigned StartBit,
                                  unsigned Size) {
      std::optional<DefinitionAndSourceRegister> DefSrcReg =
          getDefSrcRegIgnoringCopies(DefReg, MRI);
      MachineInstr *Def = DefSrcReg->MI;
      DefReg = DefSrcReg->Reg;
      // If the instruction has a single def, then simply delegate the search.
      // For unmerge however with multiple defs, we need to compute the offset
      // into the source of the unmerge.
      switch (Def->getOpcode()) {
      case TargetOpcode::G_CONCAT_VECTORS:
        return findValueFromConcat(cast<GConcatVectors>(*Def), StartBit, Size);
      case TargetOpcode::G_UNMERGE_VALUES: {
        unsigned DefStartBit = 0;
        unsigned DefSize = MRI.getType(DefReg).getSizeInBits();
        for (const auto &MO : Def->defs()) {
          if (MO.getReg() == DefReg)
            break;
          DefStartBit += DefSize;
        }
        Register SrcReg = Def->getOperand(Def->getNumOperands() - 1).getReg();
        Register SrcOriginReg =
            findValueFromDefImpl(SrcReg, StartBit + DefStartBit, Size);
        if (SrcOriginReg)
          return SrcOriginReg;
        // Failed to find a further value. If the StartBit and Size perfectly
        // covered the requested DefReg, return that since it's better than
        // nothing.
        if (StartBit == 0 && Size == DefSize)
          return DefReg;
        return CurrentBest;
      }
      case TargetOpcode::G_BUILD_VECTOR:
        return findValueFromBuildVector(cast<GBuildVector>(*Def), StartBit,
                                        Size);
      case TargetOpcode::G_INSERT:
        return findValueFromInsert(*Def, StartBit, Size);
      case TargetOpcode::G_TRUNC:
        return findValueFromTrunc(*Def, StartBit, Size);
      case TargetOpcode::G_SEXT:
      case TargetOpcode::G_ZEXT:
      case TargetOpcode::G_ANYEXT:
        return findValueFromExt(*Def, StartBit, Size);
      default:
        return CurrentBest;
      }
    }

  public:
    ArtifactValueFinder(MachineRegisterInfo &Mri, MachineIRBuilder &Builder,
                        const LegalizerInfo &Info)
        : MRI(Mri), MIB(Builder), LI(Info) {}

    /// Try to find a source of the value defined in the def \p DefReg, starting
    /// at position \p StartBit with size \p Size.
    /// \returns a register with the requested size, or an empty Register if no
    /// better value could be found.
    Register findValueFromDef(Register DefReg, unsigned StartBit,
                              unsigned Size) {
      CurrentBest = Register();
      Register FoundReg = findValueFromDefImpl(DefReg, StartBit, Size);
      return FoundReg != DefReg ? FoundReg : Register();
    }

    /// Try to combine the defs of an unmerge \p MI by attempting to find
    /// values that provides the bits for each def reg.
    /// \returns true if all the defs of the unmerge have been made dead.
    bool tryCombineUnmergeDefs(GUnmerge &MI, GISelChangeObserver &Observer,
                               SmallVectorImpl<Register> &UpdatedDefs) {
      unsigned NumDefs = MI.getNumDefs();
      LLT DestTy = MRI.getType(MI.getReg(0));

      SmallBitVector DeadDefs(NumDefs);
      for (unsigned DefIdx = 0; DefIdx < NumDefs; ++DefIdx) {
        Register DefReg = MI.getReg(DefIdx);
        if (MRI.use_nodbg_empty(DefReg)) {
          DeadDefs[DefIdx] = true;
          continue;
        }
        Register FoundVal = findValueFromDef(DefReg, 0, DestTy.getSizeInBits());
        if (!FoundVal)
          continue;
        if (MRI.getType(FoundVal) != DestTy)
          continue;

        replaceRegOrBuildCopy(DefReg, FoundVal, MRI, MIB, UpdatedDefs,
                              Observer);
        // We only want to replace the uses, not the def of the old reg.
        Observer.changingInstr(MI);
        MI.getOperand(DefIdx).setReg(DefReg);
        Observer.changedInstr(MI);
        DeadDefs[DefIdx] = true;
      }
      return DeadDefs.all();
    }

    GUnmerge *findUnmergeThatDefinesReg(Register Reg, unsigned Size,
                                        unsigned &DefOperandIdx) {
      if (Register Def = findValueFromDefImpl(Reg, 0, Size)) {
        if (auto *Unmerge = dyn_cast<GUnmerge>(MRI.getVRegDef(Def))) {
          DefOperandIdx =
              Unmerge->findRegisterDefOperandIdx(Def, /*TRI=*/nullptr);
          return Unmerge;
        }
      }
      return nullptr;
    }

    // Check if sequence of elements from merge-like instruction is defined by
    // another sequence of elements defined by unmerge. Most often this is the
    // same sequence. Search for elements using findValueFromDefImpl.
    bool isSequenceFromUnmerge(GMergeLikeInstr &MI, unsigned MergeStartIdx,
                               GUnmerge *Unmerge, unsigned UnmergeIdxStart,
                               unsigned NumElts, unsigned EltSize,
                               bool AllowUndef) {
      assert(MergeStartIdx + NumElts <= MI.getNumSources());
      for (unsigned i = MergeStartIdx; i < MergeStartIdx + NumElts; ++i) {
        unsigned EltUnmergeIdx;
        GUnmerge *EltUnmerge = findUnmergeThatDefinesReg(
            MI.getSourceReg(i), EltSize, EltUnmergeIdx);
        // Check if source i comes from the same Unmerge.
        if (EltUnmerge == Unmerge) {
          // Check that source i's def has same index in sequence in Unmerge.
          if (i - MergeStartIdx != EltUnmergeIdx - UnmergeIdxStart)
            return false;
        } else if (!AllowUndef ||
                   MRI.getVRegDef(MI.getSourceReg(i))->getOpcode() !=
                       TargetOpcode::G_IMPLICIT_DEF)
          return false;
      }
      return true;
    }

    bool tryCombineMergeLike(GMergeLikeInstr &MI,
                             SmallVectorImpl<MachineInstr *> &DeadInsts,
                             SmallVectorImpl<Register> &UpdatedDefs,
                             GISelChangeObserver &Observer) {
      Register Elt0 = MI.getSourceReg(0);
      LLT EltTy = MRI.getType(Elt0);
      unsigned EltSize = EltTy.getSizeInBits();

      unsigned Elt0UnmergeIdx;
      // Search for unmerge that will be candidate for combine.
      auto *Unmerge = findUnmergeThatDefinesReg(Elt0, EltSize, Elt0UnmergeIdx);
      if (!Unmerge)
        return false;

      unsigned NumMIElts = MI.getNumSources();
      Register Dst = MI.getReg(0);
      LLT DstTy = MRI.getType(Dst);
      Register UnmergeSrc = Unmerge->getSourceReg();
      LLT UnmergeSrcTy = MRI.getType(UnmergeSrc);

      // Recognize copy of UnmergeSrc to Dst.
      // Unmerge UnmergeSrc and reassemble it using merge-like opcode into Dst.
      //
      // %0:_(EltTy), %1, ... = G_UNMERGE_VALUES %UnmergeSrc:_(Ty)
      // %Dst:_(Ty) = G_merge_like_opcode %0:_(EltTy), %1, ...
      //
      // %Dst:_(Ty) = COPY %UnmergeSrc:_(Ty)
      if ((DstTy == UnmergeSrcTy) && (Elt0UnmergeIdx == 0)) {
        if (!isSequenceFromUnmerge(MI, 0, Unmerge, 0, NumMIElts, EltSize,
                                   /*AllowUndef=*/DstTy.isVector()))
          return false;

        replaceRegOrBuildCopy(Dst, UnmergeSrc, MRI, MIB, UpdatedDefs, Observer);
        DeadInsts.push_back(&MI);
        return true;
      }

      // Recognize UnmergeSrc that can be unmerged to DstTy directly.
      // Types have to be either both vector or both non-vector types.
      // Merge-like opcodes are combined one at the time. First one creates new
      // unmerge, following should use the same unmerge (builder performs CSE).
      //
      // %0:_(EltTy), %1, %2, %3 = G_UNMERGE_VALUES %UnmergeSrc:_(UnmergeSrcTy)
      // %Dst:_(DstTy) = G_merge_like_opcode %0:_(EltTy), %1
      // %AnotherDst:_(DstTy) = G_merge_like_opcode %2:_(EltTy), %3
      //
      // %Dst:_(DstTy), %AnotherDst = G_UNMERGE_VALUES %UnmergeSrc
      if ((DstTy.isVector() == UnmergeSrcTy.isVector()) &&
          (Elt0UnmergeIdx % NumMIElts == 0) &&
          getCoverTy(UnmergeSrcTy, DstTy) == UnmergeSrcTy) {
        if (!isSequenceFromUnmerge(MI, 0, Unmerge, Elt0UnmergeIdx, NumMIElts,
                                   EltSize, false))
          return false;
        MIB.setInstrAndDebugLoc(MI);
        auto NewUnmerge = MIB.buildUnmerge(DstTy, Unmerge->getSourceReg());
        unsigned DstIdx = (Elt0UnmergeIdx * EltSize) / DstTy.getSizeInBits();
        replaceRegOrBuildCopy(Dst, NewUnmerge.getReg(DstIdx), MRI, MIB,
                              UpdatedDefs, Observer);
        DeadInsts.push_back(&MI);
        return true;
      }

      // Recognize when multiple unmerged sources with UnmergeSrcTy type
      // can be merged into Dst with DstTy type directly.
      // Types have to be either both vector or both non-vector types.

      // %0:_(EltTy), %1 = G_UNMERGE_VALUES %UnmergeSrc:_(UnmergeSrcTy)
      // %2:_(EltTy), %3 = G_UNMERGE_VALUES %AnotherUnmergeSrc:_(UnmergeSrcTy)
      // %Dst:_(DstTy) = G_merge_like_opcode %0:_(EltTy), %1, %2, %3
      //
      // %Dst:_(DstTy) = G_merge_like_opcode %UnmergeSrc, %AnotherUnmergeSrc

      if ((DstTy.isVector() == UnmergeSrcTy.isVector()) &&
          getCoverTy(DstTy, UnmergeSrcTy) == DstTy) {
        SmallVector<Register, 4> ConcatSources;
        unsigned NumElts = Unmerge->getNumDefs();
        for (unsigned i = 0; i < MI.getNumSources(); i += NumElts) {
          unsigned EltUnmergeIdx;
          auto *UnmergeI = findUnmergeThatDefinesReg(MI.getSourceReg(i),
                                                     EltSize, EltUnmergeIdx);
          // All unmerges have to be the same size.
          if ((!UnmergeI) || (UnmergeI->getNumDefs() != NumElts) ||
              (EltUnmergeIdx != 0))
            return false;
          if (!isSequenceFromUnmerge(MI, i, UnmergeI, 0, NumElts, EltSize,
                                     false))
            return false;
          ConcatSources.push_back(UnmergeI->getSourceReg());
        }

        MIB.setInstrAndDebugLoc(MI);
        MIB.buildMergeLikeInstr(Dst, ConcatSources);
        DeadInsts.push_back(&MI);
        return true;
      }

      return false;
    }
  };

  bool tryCombineUnmergeValues(GUnmerge &MI,
                               SmallVectorImpl<MachineInstr *> &DeadInsts,
                               SmallVectorImpl<Register> &UpdatedDefs,
                               GISelChangeObserver &Observer) {
    unsigned NumDefs = MI.getNumDefs();
    Register SrcReg = MI.getSourceReg();
    MachineInstr *SrcDef = getDefIgnoringCopies(SrcReg, MRI);
    if (!SrcDef)
      return false;

    LLT OpTy = MRI.getType(SrcReg);
    LLT DestTy = MRI.getType(MI.getReg(0));
    unsigned SrcDefIdx = getDefIndex(*SrcDef, SrcReg);

    Builder.setInstrAndDebugLoc(MI);

    ArtifactValueFinder Finder(MRI, Builder, LI);
    if (Finder.tryCombineUnmergeDefs(MI, Observer, UpdatedDefs)) {
      markInstAndDefDead(MI, *SrcDef, DeadInsts, SrcDefIdx);
      return true;
    }

    if (auto *SrcUnmerge = dyn_cast<GUnmerge>(SrcDef)) {
      // %0:_(<4 x s16>) = G_FOO
      // %1:_(<2 x s16>), %2:_(<2 x s16>) = G_UNMERGE_VALUES %0
      // %3:_(s16), %4:_(s16) = G_UNMERGE_VALUES %1
      //
      // %3:_(s16), %4:_(s16), %5:_(s16), %6:_(s16) = G_UNMERGE_VALUES %0
      Register SrcUnmergeSrc = SrcUnmerge->getSourceReg();
      LLT SrcUnmergeSrcTy = MRI.getType(SrcUnmergeSrc);

      // If we need to decrease the number of vector elements in the result type
      // of an unmerge, this would involve the creation of an equivalent unmerge
      // to copy back to the original result registers.
      LegalizeActionStep ActionStep = LI.getAction(
          {TargetOpcode::G_UNMERGE_VALUES, {OpTy, SrcUnmergeSrcTy}});
      switch (ActionStep.Action) {
      case LegalizeActions::Lower:
      case LegalizeActions::Unsupported:
        break;
      case LegalizeActions::FewerElements:
      case LegalizeActions::NarrowScalar:
        if (ActionStep.TypeIdx == 1)
          return false;
        break;
      default:
        return false;
      }

      auto NewUnmerge = Builder.buildUnmerge(DestTy, SrcUnmergeSrc);

      // TODO: Should we try to process out the other defs now? If the other
      // defs of the source unmerge are also unmerged, we end up with a separate
      // unmerge for each one.
      for (unsigned I = 0; I != NumDefs; ++I) {
        Register Def = MI.getReg(I);
        replaceRegOrBuildCopy(Def, NewUnmerge.getReg(SrcDefIdx * NumDefs + I),
                              MRI, Builder, UpdatedDefs, Observer);
      }

      markInstAndDefDead(MI, *SrcUnmerge, DeadInsts, SrcDefIdx);
      return true;
    }

    MachineInstr *MergeI = SrcDef;
    unsigned ConvertOp = 0;

    // Handle intermediate conversions
    unsigned SrcOp = SrcDef->getOpcode();
    if (isArtifactCast(SrcOp)) {
      ConvertOp = SrcOp;
      MergeI = getDefIgnoringCopies(SrcDef->getOperand(1).getReg(), MRI);
    }

    if (!MergeI || !canFoldMergeOpcode(MergeI->getOpcode(),
                                       ConvertOp, OpTy, DestTy)) {
      // We might have a chance to combine later by trying to combine
      // unmerge(cast) first
      return tryFoldUnmergeCast(MI, *SrcDef, DeadInsts, UpdatedDefs);
    }

    const unsigned NumMergeRegs = MergeI->getNumOperands() - 1;

    if (NumMergeRegs < NumDefs) {
      if (NumDefs % NumMergeRegs != 0)
        return false;

      Builder.setInstr(MI);
      // Transform to UNMERGEs, for example
      //   %1 = G_MERGE_VALUES %4, %5
      //   %9, %10, %11, %12 = G_UNMERGE_VALUES %1
      // to
      //   %9, %10 = G_UNMERGE_VALUES %4
      //   %11, %12 = G_UNMERGE_VALUES %5

      const unsigned NewNumDefs = NumDefs / NumMergeRegs;
      for (unsigned Idx = 0; Idx < NumMergeRegs; ++Idx) {
        SmallVector<Register, 8> DstRegs;
        for (unsigned j = 0, DefIdx = Idx * NewNumDefs; j < NewNumDefs;
             ++j, ++DefIdx)
          DstRegs.push_back(MI.getReg(DefIdx));

        if (ConvertOp) {
          LLT MergeDstTy = MRI.getType(SrcDef->getOperand(0).getReg());

          // This is a vector that is being split and casted. Extract to the
          // element type, and do the conversion on the scalars (or smaller
          // vectors).
          LLT MergeEltTy = MergeDstTy.divide(NumMergeRegs);

          // Handle split to smaller vectors, with conversions.
          // %2(<8 x s8>) = G_CONCAT_VECTORS %0(<4 x s8>), %1(<4 x s8>)
          // %3(<8 x s16>) = G_SEXT %2
          // %4(<2 x s16>), %5(<2 x s16>), %6(<2 x s16>), %7(<2 x s16>) =
          // G_UNMERGE_VALUES %3
          //
          // =>
          //
          // %8(<4 x s16>) = G_SEXT %0
          // %9(<4 x s16>) = G_SEXT %1
          // %4(<2 x s16>), %5(<2 x s16>) = G_UNMERGE_VALUES %8
          // %7(<2 x s16>), %7(<2 x s16>) = G_UNMERGE_VALUES %9

          Register TmpReg = MRI.createGenericVirtualRegister(MergeEltTy);
          Builder.buildInstr(ConvertOp, {TmpReg},
                             {MergeI->getOperand(Idx + 1).getReg()});
          Builder.buildUnmerge(DstRegs, TmpReg);
        } else {
          Builder.buildUnmerge(DstRegs, MergeI->getOperand(Idx + 1).getReg());
        }
        UpdatedDefs.append(DstRegs.begin(), DstRegs.end());
      }

    } else if (NumMergeRegs > NumDefs) {
      if (ConvertOp != 0 || NumMergeRegs % NumDefs != 0)
        return false;

      Builder.setInstr(MI);
      // Transform to MERGEs
      //   %6 = G_MERGE_VALUES %17, %18, %19, %20
      //   %7, %8 = G_UNMERGE_VALUES %6
      // to
      //   %7 = G_MERGE_VALUES %17, %18
      //   %8 = G_MERGE_VALUES %19, %20

      const unsigned NumRegs = NumMergeRegs / NumDefs;
      for (unsigned DefIdx = 0; DefIdx < NumDefs; ++DefIdx) {
        SmallVector<Register, 8> Regs;
        for (unsigned j = 0, Idx = NumRegs * DefIdx + 1; j < NumRegs;
             ++j, ++Idx)
          Regs.push_back(MergeI->getOperand(Idx).getReg());

        Register DefReg = MI.getReg(DefIdx);
        Builder.buildMergeLikeInstr(DefReg, Regs);
        UpdatedDefs.push_back(DefReg);
      }

    } else {
      LLT MergeSrcTy = MRI.getType(MergeI->getOperand(1).getReg());

      if (!ConvertOp && DestTy != MergeSrcTy)
        ConvertOp = TargetOpcode::G_BITCAST;

      if (ConvertOp) {
        Builder.setInstr(MI);

        for (unsigned Idx = 0; Idx < NumDefs; ++Idx) {
          Register DefReg = MI.getOperand(Idx).getReg();
          Register MergeSrc = MergeI->getOperand(Idx + 1).getReg();

          if (!MRI.use_empty(DefReg)) {
            Builder.buildInstr(ConvertOp, {DefReg}, {MergeSrc});
            UpdatedDefs.push_back(DefReg);
          }
        }

        markInstAndDefDead(MI, *MergeI, DeadInsts);
        return true;
      }

      assert(DestTy == MergeSrcTy &&
             "Bitcast and the other kinds of conversions should "
             "have happened earlier");

      Builder.setInstr(MI);
      for (unsigned Idx = 0; Idx < NumDefs; ++Idx) {
        Register DstReg = MI.getOperand(Idx).getReg();
        Register SrcReg = MergeI->getOperand(Idx + 1).getReg();
        replaceRegOrBuildCopy(DstReg, SrcReg, MRI, Builder, UpdatedDefs,
                              Observer);
      }
    }

    markInstAndDefDead(MI, *MergeI, DeadInsts);
    return true;
  }

  bool tryCombineExtract(MachineInstr &MI,
                         SmallVectorImpl<MachineInstr *> &DeadInsts,
                         SmallVectorImpl<Register> &UpdatedDefs) {
    assert(MI.getOpcode() == TargetOpcode::G_EXTRACT);

    // Try to use the source registers from a G_MERGE_VALUES
    //
    // %2 = G_MERGE_VALUES %0, %1
    // %3 = G_EXTRACT %2, N
    // =>
    //
    // for N < %2.getSizeInBits() / 2
    //     %3 = G_EXTRACT %0, N
    //
    // for N >= %2.getSizeInBits() / 2
    //    %3 = G_EXTRACT %1, (N - %0.getSizeInBits()

    Register SrcReg = lookThroughCopyInstrs(MI.getOperand(1).getReg());
    MachineInstr *MergeI = MRI.getVRegDef(SrcReg);
    if (!MergeI || !isa<GMergeLikeInstr>(MergeI))
      return false;

    Register DstReg = MI.getOperand(0).getReg();
    LLT DstTy = MRI.getType(DstReg);
    LLT SrcTy = MRI.getType(SrcReg);

    // TODO: Do we need to check if the resulting extract is supported?
    unsigned ExtractDstSize = DstTy.getSizeInBits();
    unsigned Offset = MI.getOperand(2).getImm();
    unsigned NumMergeSrcs = MergeI->getNumOperands() - 1;
    unsigned MergeSrcSize = SrcTy.getSizeInBits() / NumMergeSrcs;
    unsigned MergeSrcIdx = Offset / MergeSrcSize;

    // Compute the offset of the last bit the extract needs.
    unsigned EndMergeSrcIdx = (Offset + ExtractDstSize - 1) / MergeSrcSize;

    // Can't handle the case where the extract spans multiple inputs.
    if (MergeSrcIdx != EndMergeSrcIdx)
      return false;

    // TODO: We could modify MI in place in most cases.
    Builder.setInstr(MI);
    Builder.buildExtract(DstReg, MergeI->getOperand(MergeSrcIdx + 1).getReg(),
                         Offset - MergeSrcIdx * MergeSrcSize);
    UpdatedDefs.push_back(DstReg);
    markInstAndDefDead(MI, *MergeI, DeadInsts);
    return true;
  }

  /// Try to combine away MI.
  /// Returns true if it combined away the MI.
  /// Adds instructions that are dead as a result of the combine
  /// into DeadInsts, which can include MI.
  bool tryCombineInstruction(MachineInstr &MI,
                             SmallVectorImpl<MachineInstr *> &DeadInsts,
                             GISelObserverWrapper &WrapperObserver) {
    ArtifactValueFinder Finder(MRI, Builder, LI);

    // This might be a recursive call, and we might have DeadInsts already
    // populated. To avoid bad things happening later with multiple vreg defs
    // etc, process the dead instructions now if any.
    if (!DeadInsts.empty())
      deleteMarkedDeadInsts(DeadInsts, WrapperObserver);

    // Put here every vreg that was redefined in such a way that it's at least
    // possible that one (or more) of its users (immediate or COPY-separated)
    // could become artifact combinable with the new definition (or the
    // instruction reachable from it through a chain of copies if any).
    SmallVector<Register, 4> UpdatedDefs;
    bool Changed = false;
    switch (MI.getOpcode()) {
    default:
      return false;
    case TargetOpcode::G_ANYEXT:
      Changed = tryCombineAnyExt(MI, DeadInsts, UpdatedDefs, WrapperObserver);
      break;
    case TargetOpcode::G_ZEXT:
      Changed = tryCombineZExt(MI, DeadInsts, UpdatedDefs, WrapperObserver);
      break;
    case TargetOpcode::G_SEXT:
      Changed = tryCombineSExt(MI, DeadInsts, UpdatedDefs, WrapperObserver);
      break;
    case TargetOpcode::G_UNMERGE_VALUES:
      Changed = tryCombineUnmergeValues(cast<GUnmerge>(MI), DeadInsts,
                                        UpdatedDefs, WrapperObserver);
      break;
    case TargetOpcode::G_MERGE_VALUES:
    case TargetOpcode::G_BUILD_VECTOR:
    case TargetOpcode::G_CONCAT_VECTORS:
      // If any of the users of this merge are an unmerge, then add them to the
      // artifact worklist in case there's folding that can be done looking up.
      for (MachineInstr &U : MRI.use_instructions(MI.getOperand(0).getReg())) {
        if (U.getOpcode() == TargetOpcode::G_UNMERGE_VALUES ||
            U.getOpcode() == TargetOpcode::G_TRUNC) {
          UpdatedDefs.push_back(MI.getOperand(0).getReg());
          break;
        }
      }
      Changed = Finder.tryCombineMergeLike(cast<GMergeLikeInstr>(MI), DeadInsts,
                                           UpdatedDefs, WrapperObserver);
      break;
    case TargetOpcode::G_EXTRACT:
      Changed = tryCombineExtract(MI, DeadInsts, UpdatedDefs);
      break;
    case TargetOpcode::G_TRUNC:
      Changed = tryCombineTrunc(MI, DeadInsts, UpdatedDefs, WrapperObserver);
      if (!Changed) {
        // Try to combine truncates away even if they are legal. As all artifact
        // combines at the moment look only "up" the def-use chains, we achieve
        // that by throwing truncates' users (with look through copies) into the
        // ArtifactList again.
        UpdatedDefs.push_back(MI.getOperand(0).getReg());
      }
      break;
    }
    // If the main loop through the ArtifactList found at least one combinable
    // pair of artifacts, not only combine it away (as done above), but also
    // follow the def-use chain from there to combine everything that can be
    // combined within this def-use chain of artifacts.
    while (!UpdatedDefs.empty()) {
      Register NewDef = UpdatedDefs.pop_back_val();
      assert(NewDef.isVirtual() && "Unexpected redefinition of a physreg");
      for (MachineInstr &Use : MRI.use_instructions(NewDef)) {
        switch (Use.getOpcode()) {
        // Keep this list in sync with the list of all artifact combines.
        case TargetOpcode::G_ANYEXT:
        case TargetOpcode::G_ZEXT:
        case TargetOpcode::G_SEXT:
        case TargetOpcode::G_UNMERGE_VALUES:
        case TargetOpcode::G_EXTRACT:
        case TargetOpcode::G_TRUNC:
        case TargetOpcode::G_BUILD_VECTOR:
          // Adding Use to ArtifactList.
          WrapperObserver.changedInstr(Use);
          break;
        case TargetOpcode::G_ASSERT_SEXT:
        case TargetOpcode::G_ASSERT_ZEXT:
        case TargetOpcode::G_ASSERT_ALIGN:
        case TargetOpcode::COPY: {
          Register Copy = Use.getOperand(0).getReg();
          if (Copy.isVirtual())
            UpdatedDefs.push_back(Copy);
          break;
        }
        default:
          // If we do not have an artifact combine for the opcode, there is no
          // point in adding it to the ArtifactList as nothing interesting will
          // be done to it anyway.
          break;
        }
      }
    }
    return Changed;
  }

private:
  static Register getArtifactSrcReg(const MachineInstr &MI) {
    switch (MI.getOpcode()) {
    case TargetOpcode::COPY:
    case TargetOpcode::G_TRUNC:
    case TargetOpcode::G_ZEXT:
    case TargetOpcode::G_ANYEXT:
    case TargetOpcode::G_SEXT:
    case TargetOpcode::G_EXTRACT:
    case TargetOpcode::G_ASSERT_SEXT:
    case TargetOpcode::G_ASSERT_ZEXT:
    case TargetOpcode::G_ASSERT_ALIGN:
      return MI.getOperand(1).getReg();
    case TargetOpcode::G_UNMERGE_VALUES:
      return MI.getOperand(MI.getNumOperands() - 1).getReg();
    default:
      llvm_unreachable("Not a legalization artifact happen");
    }
  }

  /// Mark a def of one of MI's original operands, DefMI, as dead if changing MI
  /// (either by killing it or changing operands) results in DefMI being dead
  /// too. In-between COPYs or artifact-casts are also collected if they are
  /// dead.
  /// MI is not marked dead.
  void markDefDead(MachineInstr &MI, MachineInstr &DefMI,
                   SmallVectorImpl<MachineInstr *> &DeadInsts,
                   unsigned DefIdx = 0) {
    // Collect all the copy instructions that are made dead, due to deleting
    // this instruction. Collect all of them until the Trunc(DefMI).
    // Eg,
    // %1(s1) = G_TRUNC %0(s32)
    // %2(s1) = COPY %1(s1)
    // %3(s1) = COPY %2(s1)
    // %4(s32) = G_ANYEXT %3(s1)
    // In this case, we would have replaced %4 with a copy of %0,
    // and as a result, %3, %2, %1 are dead.
    MachineInstr *PrevMI = &MI;
    while (PrevMI != &DefMI) {
      Register PrevRegSrc = getArtifactSrcReg(*PrevMI);

      MachineInstr *TmpDef = MRI.getVRegDef(PrevRegSrc);
      if (MRI.hasOneUse(PrevRegSrc)) {
        if (TmpDef != &DefMI) {
          assert((TmpDef->getOpcode() == TargetOpcode::COPY ||
                  isArtifactCast(TmpDef->getOpcode()) ||
                  isPreISelGenericOptimizationHint(TmpDef->getOpcode())) &&
                 "Expecting copy or artifact cast here");

          DeadInsts.push_back(TmpDef);
        }
      } else
        break;
      PrevMI = TmpDef;
    }

    if (PrevMI == &DefMI) {
      unsigned I = 0;
      bool IsDead = true;
      for (MachineOperand &Def : DefMI.defs()) {
        if (I != DefIdx) {
          if (!MRI.use_empty(Def.getReg())) {
            IsDead = false;
            break;
          }
        } else {
          if (!MRI.hasOneUse(DefMI.getOperand(DefIdx).getReg()))
            break;
        }

        ++I;
      }

      if (IsDead)
        DeadInsts.push_back(&DefMI);
    }
  }

  /// Mark MI as dead. If a def of one of MI's operands, DefMI, would also be
  /// dead due to MI being killed, then mark DefMI as dead too.
  /// Some of the combines (extends(trunc)), try to walk through redundant
  /// copies in between the extends and the truncs, and this attempts to collect
  /// the in between copies if they're dead.
  void markInstAndDefDead(MachineInstr &MI, MachineInstr &DefMI,
                          SmallVectorImpl<MachineInstr *> &DeadInsts,
                          unsigned DefIdx = 0) {
    DeadInsts.push_back(&MI);
    markDefDead(MI, DefMI, DeadInsts, DefIdx);
  }

  /// Erase the dead instructions in the list and call the observer hooks.
  /// Normally the Legalizer will deal with erasing instructions that have been
  /// marked dead. However, for the trunc(ext(x)) cases we can end up trying to
  /// process instructions which have been marked dead, but otherwise break the
  /// MIR by introducing multiple vreg defs. For those cases, allow the combines
  /// to explicitly delete the instructions before we run into trouble.
  void deleteMarkedDeadInsts(SmallVectorImpl<MachineInstr *> &DeadInsts,
                             GISelObserverWrapper &WrapperObserver) {
    for (auto *DeadMI : DeadInsts) {
      LLVM_DEBUG(dbgs() << *DeadMI << "Is dead, eagerly deleting\n");
      WrapperObserver.erasingInstr(*DeadMI);
      DeadMI->eraseFromParent();
    }
    DeadInsts.clear();
  }

  /// Checks if the target legalizer info has specified anything about the
  /// instruction, or if unsupported.
  bool isInstUnsupported(const LegalityQuery &Query) const {
    using namespace LegalizeActions;
    auto Step = LI.getAction(Query);
    return Step.Action == Unsupported || Step.Action == NotFound;
  }

  bool isInstLegal(const LegalityQuery &Query) const {
    return LI.getAction(Query).Action == LegalizeActions::Legal;
  }

  bool isConstantUnsupported(LLT Ty) const {
    if (!Ty.isVector())
      return isInstUnsupported({TargetOpcode::G_CONSTANT, {Ty}});

    LLT EltTy = Ty.getElementType();
    return isInstUnsupported({TargetOpcode::G_CONSTANT, {EltTy}}) ||
           isInstUnsupported({TargetOpcode::G_BUILD_VECTOR, {Ty, EltTy}});
  }

  /// Looks through copy instructions and returns the actual
  /// source register.
  Register lookThroughCopyInstrs(Register Reg) {
    Register TmpReg = getSrcRegIgnoringCopies(Reg, MRI);
    return TmpReg.isValid() ? TmpReg : Reg;
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_LEGALIZATIONARTIFACTCOMBINER_H

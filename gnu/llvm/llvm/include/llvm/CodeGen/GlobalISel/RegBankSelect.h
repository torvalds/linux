//=- llvm/CodeGen/GlobalISel/RegBankSelect.h - Reg Bank Selector --*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file describes the interface of the MachineFunctionPass
/// responsible for assigning the generic virtual registers to register bank.
///
/// By default, the reg bank selector relies on local decisions to
/// assign the register bank. In other words, it looks at one instruction
/// at a time to decide where the operand of that instruction should live.
///
/// At higher optimization level, we could imagine that the reg bank selector
/// would use more global analysis and do crazier thing like duplicating
/// instructions and so on. This is future work.
///
/// For now, the pass uses a greedy algorithm to decide where the operand
/// of an instruction should live. It asks the target which banks may be
/// used for each operand of the instruction and what is the cost. Then,
/// it chooses the solution which minimize the cost of the instruction plus
/// the cost of any move that may be needed to the values into the right
/// register bank.
/// In other words, the cost for an instruction on a register bank RegBank
/// is: Cost of I on RegBank plus the sum of the cost for bringing the
/// input operands from their current register bank to RegBank.
/// Thus, the following formula:
/// cost(I, RegBank) = cost(I.Opcode, RegBank) +
///    sum(for each arg in I.arguments: costCrossCopy(arg.RegBank, RegBank))
///
/// E.g., Let say we are assigning the register bank for the instruction
/// defining v2.
/// v0(A_REGBANK) = ...
/// v1(A_REGBANK) = ...
/// v2 = G_ADD i32 v0, v1 <-- MI
///
/// The target may say it can generate G_ADD i32 on register bank A and B
/// with a cost of respectively 5 and 1.
/// Then, let say the cost of a cross register bank copies from A to B is 1.
/// The reg bank selector would compare the following two costs:
/// cost(MI, A_REGBANK) = cost(G_ADD, A_REGBANK) + cost(v0.RegBank, A_REGBANK) +
///    cost(v1.RegBank, A_REGBANK)
///                     = 5 + cost(A_REGBANK, A_REGBANK) + cost(A_REGBANK,
///                                                             A_REGBANK)
///                     = 5 + 0 + 0 = 5
/// cost(MI, B_REGBANK) = cost(G_ADD, B_REGBANK) + cost(v0.RegBank, B_REGBANK) +
///    cost(v1.RegBank, B_REGBANK)
///                     = 1 + cost(A_REGBANK, B_REGBANK) + cost(A_REGBANK,
///                                                             B_REGBANK)
///                     = 1 + 1 + 1 = 3
/// Therefore, in this specific example, the reg bank selector would choose
/// bank B for MI.
/// v0(A_REGBANK) = ...
/// v1(A_REGBANK) = ...
/// tmp0(B_REGBANK) = COPY v0
/// tmp1(B_REGBANK) = COPY v1
/// v2(B_REGBANK) = G_ADD i32 tmp0, tmp1
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_REGBANKSELECT_H
#define LLVM_CODEGEN_GLOBALISEL_REGBANKSELECT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include <cassert>
#include <cstdint>
#include <memory>

namespace llvm {

class BlockFrequency;
class MachineBlockFrequencyInfo;
class MachineBranchProbabilityInfo;
class MachineOperand;
class MachineRegisterInfo;
class Pass;
class raw_ostream;
class TargetPassConfig;
class TargetRegisterInfo;

/// This pass implements the reg bank selector pass used in the GlobalISel
/// pipeline. At the end of this pass, all register operands have been assigned
class RegBankSelect : public MachineFunctionPass {
public:
  static char ID;

  /// List of the modes supported by the RegBankSelect pass.
  enum Mode {
    /// Assign the register banks as fast as possible (default).
    Fast,
    /// Greedily minimize the cost of assigning register banks.
    /// This should produce code of greater quality, but will
    /// require more compile time.
    Greedy
  };

  /// Abstract class used to represent an insertion point in a CFG.
  /// This class records an insertion point and materializes it on
  /// demand.
  /// It allows to reason about the frequency of this insertion point,
  /// without having to logically materialize it (e.g., on an edge),
  /// before we actually need to insert something.
  class InsertPoint {
  protected:
    /// Tell if the insert point has already been materialized.
    bool WasMaterialized = false;

    /// Materialize the insertion point.
    ///
    /// If isSplit() is true, this involves actually splitting
    /// the block or edge.
    ///
    /// \post getPointImpl() returns a valid iterator.
    /// \post getInsertMBBImpl() returns a valid basic block.
    /// \post isSplit() == false ; no more splitting should be required.
    virtual void materialize() = 0;

    /// Return the materialized insertion basic block.
    /// Code will be inserted into that basic block.
    ///
    /// \pre ::materialize has been called.
    virtual MachineBasicBlock &getInsertMBBImpl() = 0;

    /// Return the materialized insertion point.
    /// Code will be inserted before that point.
    ///
    /// \pre ::materialize has been called.
    virtual MachineBasicBlock::iterator getPointImpl() = 0;

  public:
    virtual ~InsertPoint() = default;

    /// The first call to this method will cause the splitting to
    /// happen if need be, then sub sequent calls just return
    /// the iterator to that point. I.e., no more splitting will
    /// occur.
    ///
    /// \return The iterator that should be used with
    /// MachineBasicBlock::insert. I.e., additional code happens
    /// before that point.
    MachineBasicBlock::iterator getPoint() {
      if (!WasMaterialized) {
        WasMaterialized = true;
        assert(canMaterialize() && "Impossible to materialize this point");
        materialize();
      }
      // When we materialized the point we should have done the splitting.
      assert(!isSplit() && "Wrong pre-condition");
      return getPointImpl();
    }

    /// The first call to this method will cause the splitting to
    /// happen if need be, then sub sequent calls just return
    /// the basic block that contains the insertion point.
    /// I.e., no more splitting will occur.
    ///
    /// \return The basic block should be used with
    /// MachineBasicBlock::insert and ::getPoint. The new code should
    /// happen before that point.
    MachineBasicBlock &getInsertMBB() {
      if (!WasMaterialized) {
        WasMaterialized = true;
        assert(canMaterialize() && "Impossible to materialize this point");
        materialize();
      }
      // When we materialized the point we should have done the splitting.
      assert(!isSplit() && "Wrong pre-condition");
      return getInsertMBBImpl();
    }

    /// Insert \p MI in the just before ::getPoint()
    MachineBasicBlock::iterator insert(MachineInstr &MI) {
      return getInsertMBB().insert(getPoint(), &MI);
    }

    /// Does this point involve splitting an edge or block?
    /// As soon as ::getPoint is called and thus, the point
    /// materialized, the point will not require splitting anymore,
    /// i.e., this will return false.
    virtual bool isSplit() const { return false; }

    /// Frequency of the insertion point.
    /// \p P is used to access the various analysis that will help to
    /// get that information, like MachineBlockFrequencyInfo.  If \p P
    /// does not contain enough to return the actual frequency,
    /// this returns 1.
    virtual uint64_t frequency(const Pass &P) const { return 1; }

    /// Check whether this insertion point can be materialized.
    /// As soon as ::getPoint is called and thus, the point materialized
    /// calling this method does not make sense.
    virtual bool canMaterialize() const { return false; }
  };

  /// Insertion point before or after an instruction.
  class InstrInsertPoint : public InsertPoint {
  private:
    /// Insertion point.
    MachineInstr &Instr;

    /// Does the insertion point is before or after Instr.
    bool Before;

    void materialize() override;

    MachineBasicBlock::iterator getPointImpl() override {
      if (Before)
        return Instr;
      return Instr.getNextNode() ? *Instr.getNextNode()
                                 : Instr.getParent()->end();
    }

    MachineBasicBlock &getInsertMBBImpl() override {
      return *Instr.getParent();
    }

  public:
    /// Create an insertion point before (\p Before=true) or after \p Instr.
    InstrInsertPoint(MachineInstr &Instr, bool Before = true);

    bool isSplit() const override;
    uint64_t frequency(const Pass &P) const override;

    // Worst case, we need to slice the basic block, but that is still doable.
    bool canMaterialize() const override { return true; }
  };

  /// Insertion point at the beginning or end of a basic block.
  class MBBInsertPoint : public InsertPoint {
  private:
    /// Insertion point.
    MachineBasicBlock &MBB;

    /// Does the insertion point is at the beginning or end of MBB.
    bool Beginning;

    void materialize() override { /*Nothing to do to materialize*/
    }

    MachineBasicBlock::iterator getPointImpl() override {
      return Beginning ? MBB.begin() : MBB.end();
    }

    MachineBasicBlock &getInsertMBBImpl() override { return MBB; }

  public:
    MBBInsertPoint(MachineBasicBlock &MBB, bool Beginning = true)
        : MBB(MBB), Beginning(Beginning) {
      // If we try to insert before phis, we should use the insertion
      // points on the incoming edges.
      assert((!Beginning || MBB.getFirstNonPHI() == MBB.begin()) &&
             "Invalid beginning point");
      // If we try to insert after the terminators, we should use the
      // points on the outcoming edges.
      assert((Beginning || MBB.getFirstTerminator() == MBB.end()) &&
             "Invalid end point");
    }

    bool isSplit() const override { return false; }
    uint64_t frequency(const Pass &P) const override;
    bool canMaterialize() const override { return true; };
  };

  /// Insertion point on an edge.
  class EdgeInsertPoint : public InsertPoint {
  private:
    /// Source of the edge.
    MachineBasicBlock &Src;

    /// Destination of the edge.
    /// After the materialization is done, this hold the basic block
    /// that resulted from the splitting.
    MachineBasicBlock *DstOrSplit;

    /// P is used to update the analysis passes as applicable.
    Pass &P;

    void materialize() override;

    MachineBasicBlock::iterator getPointImpl() override {
      // DstOrSplit should be the Split block at this point.
      // I.e., it should have one predecessor, Src, and one successor,
      // the original Dst.
      assert(DstOrSplit && DstOrSplit->isPredecessor(&Src) &&
             DstOrSplit->pred_size() == 1 && DstOrSplit->succ_size() == 1 &&
             "Did not split?!");
      return DstOrSplit->begin();
    }

    MachineBasicBlock &getInsertMBBImpl() override { return *DstOrSplit; }

  public:
    EdgeInsertPoint(MachineBasicBlock &Src, MachineBasicBlock &Dst, Pass &P)
        : Src(Src), DstOrSplit(&Dst), P(P) {}

    bool isSplit() const override {
      return Src.succ_size() > 1 && DstOrSplit->pred_size() > 1;
    }

    uint64_t frequency(const Pass &P) const override;
    bool canMaterialize() const override;
  };

  /// Struct used to represent the placement of a repairing point for
  /// a given operand.
  class RepairingPlacement {
  public:
    /// Define the kind of action this repairing needs.
    enum RepairingKind {
      /// Nothing to repair, just drop this action.
      None,
      /// Reparing code needs to happen before InsertPoints.
      Insert,
      /// (Re)assign the register bank of the operand.
      Reassign,
      /// Mark this repairing placement as impossible.
      Impossible
    };

    /// \name Convenient types for a list of insertion points.
    /// @{
    using InsertionPoints = SmallVector<std::unique_ptr<InsertPoint>, 2>;
    using insertpt_iterator = InsertionPoints::iterator;
    using const_insertpt_iterator = InsertionPoints::const_iterator;
    /// @}

  private:
    /// Kind of repairing.
    RepairingKind Kind;
    /// Index of the operand that will be repaired.
    unsigned OpIdx;
    /// Are all the insert points materializeable?
    bool CanMaterialize;
    /// Is there any of the insert points needing splitting?
    bool HasSplit = false;
    /// Insertion point for the repair code.
    /// The repairing code needs to happen just before these points.
    InsertionPoints InsertPoints;
    /// Some insertion points may need to update the liveness and such.
    Pass &P;

  public:
    /// Create a repairing placement for the \p OpIdx-th operand of
    /// \p MI. \p TRI is used to make some checks on the register aliases
    /// if the machine operand is a physical register. \p P is used to
    /// to update liveness information and such when materializing the
    /// points.
    RepairingPlacement(MachineInstr &MI, unsigned OpIdx,
                       const TargetRegisterInfo &TRI, Pass &P,
                       RepairingKind Kind = RepairingKind::Insert);

    /// \name Getters.
    /// @{
    RepairingKind getKind() const { return Kind; }
    unsigned getOpIdx() const { return OpIdx; }
    bool canMaterialize() const { return CanMaterialize; }
    bool hasSplit() { return HasSplit; }
    /// @}

    /// \name Overloaded methods to add an insertion point.
    /// @{
    /// Add a MBBInsertionPoint to the list of InsertPoints.
    void addInsertPoint(MachineBasicBlock &MBB, bool Beginning);
    /// Add a InstrInsertionPoint to the list of InsertPoints.
    void addInsertPoint(MachineInstr &MI, bool Before);
    /// Add an EdgeInsertionPoint (\p Src, \p Dst) to the list of InsertPoints.
    void addInsertPoint(MachineBasicBlock &Src, MachineBasicBlock &Dst);
    /// Add an InsertPoint to the list of insert points.
    /// This method takes the ownership of &\p Point.
    void addInsertPoint(InsertPoint &Point);
    /// @}

    /// \name Accessors related to the insertion points.
    /// @{
    insertpt_iterator begin() { return InsertPoints.begin(); }
    insertpt_iterator end() { return InsertPoints.end(); }

    const_insertpt_iterator begin() const { return InsertPoints.begin(); }
    const_insertpt_iterator end() const { return InsertPoints.end(); }

    unsigned getNumInsertPoints() const { return InsertPoints.size(); }
    /// @}

    /// Change the type of this repairing placement to \p NewKind.
    /// It is not possible to switch a repairing placement to the
    /// RepairingKind::Insert. There is no fundamental problem with
    /// that, but no uses as well, so do not support it for now.
    ///
    /// \pre NewKind != RepairingKind::Insert
    /// \post getKind() == NewKind
    void switchTo(RepairingKind NewKind) {
      assert(NewKind != Kind && "Already of the right Kind");
      Kind = NewKind;
      InsertPoints.clear();
      CanMaterialize = NewKind != RepairingKind::Impossible;
      HasSplit = false;
      assert(NewKind != RepairingKind::Insert &&
             "We would need more MI to switch to Insert");
    }
  };

protected:
  /// Helper class used to represent the cost for mapping an instruction.
  /// When mapping an instruction, we may introduce some repairing code.
  /// In most cases, the repairing code is local to the instruction,
  /// thus, we can omit the basic block frequency from the cost.
  /// However, some alternatives may produce non-local cost, e.g., when
  /// repairing a phi, and thus we then need to scale the local cost
  /// to the non-local cost. This class does this for us.
  /// \note: We could simply always scale the cost. The problem is that
  /// there are higher chances that we saturate the cost easier and end
  /// up having the same cost for actually different alternatives.
  /// Another option would be to use APInt everywhere.
  class MappingCost {
  private:
    /// Cost of the local instructions.
    /// This cost is free of basic block frequency.
    uint64_t LocalCost = 0;
    /// Cost of the non-local instructions.
    /// This cost should include the frequency of the related blocks.
    uint64_t NonLocalCost = 0;
    /// Frequency of the block where the local instructions live.
    uint64_t LocalFreq;

    MappingCost(uint64_t LocalCost, uint64_t NonLocalCost, uint64_t LocalFreq)
        : LocalCost(LocalCost), NonLocalCost(NonLocalCost),
          LocalFreq(LocalFreq) {}

    /// Check if this cost is saturated.
    bool isSaturated() const;

  public:
    /// Create a MappingCost assuming that most of the instructions
    /// will occur in a basic block with \p LocalFreq frequency.
    MappingCost(BlockFrequency LocalFreq);

    /// Add \p Cost to the local cost.
    /// \return true if this cost is saturated, false otherwise.
    bool addLocalCost(uint64_t Cost);

    /// Add \p Cost to the non-local cost.
    /// Non-local cost should reflect the frequency of their placement.
    /// \return true if this cost is saturated, false otherwise.
    bool addNonLocalCost(uint64_t Cost);

    /// Saturate the cost to the maximal representable value.
    void saturate();

    /// Return an instance of MappingCost that represents an
    /// impossible mapping.
    static MappingCost ImpossibleCost();

    /// Check if this is less than \p Cost.
    bool operator<(const MappingCost &Cost) const;
    /// Check if this is equal to \p Cost.
    bool operator==(const MappingCost &Cost) const;
    /// Check if this is not equal to \p Cost.
    bool operator!=(const MappingCost &Cost) const { return !(*this == Cost); }
    /// Check if this is greater than \p Cost.
    bool operator>(const MappingCost &Cost) const {
      return *this != Cost && Cost < *this;
    }

    /// Print this on dbgs() stream.
    void dump() const;

    /// Print this on \p OS;
    void print(raw_ostream &OS) const;

    /// Overload the stream operator for easy debug printing.
    friend raw_ostream &operator<<(raw_ostream &OS, const MappingCost &Cost) {
      Cost.print(OS);
      return OS;
    }
  };

  /// Interface to the target lowering info related
  /// to register banks.
  const RegisterBankInfo *RBI = nullptr;

  /// MRI contains all the register class/bank information that this
  /// pass uses and updates.
  MachineRegisterInfo *MRI = nullptr;

  /// Information on the register classes for the current function.
  const TargetRegisterInfo *TRI = nullptr;

  /// Get the frequency of blocks.
  /// This is required for non-fast mode.
  MachineBlockFrequencyInfo *MBFI = nullptr;

  /// Get the frequency of the edges.
  /// This is required for non-fast mode.
  MachineBranchProbabilityInfo *MBPI = nullptr;

  /// Current optimization remark emitter. Used to report failures.
  std::unique_ptr<MachineOptimizationRemarkEmitter> MORE;

  /// Helper class used for every code morphing.
  MachineIRBuilder MIRBuilder;

  /// Optimization mode of the pass.
  Mode OptMode;

  /// Current target configuration. Controls how the pass handles errors.
  const TargetPassConfig *TPC;

  /// Assign the register bank of each operand of \p MI.
  /// \return True on success, false otherwise.
  bool assignInstr(MachineInstr &MI);

  /// Initialize the field members using \p MF.
  void init(MachineFunction &MF);

  /// Check if \p Reg is already assigned what is described by \p ValMapping.
  /// \p OnlyAssign == true means that \p Reg just needs to be assigned a
  /// register bank.  I.e., no repairing is necessary to have the
  /// assignment match.
  bool assignmentMatch(Register Reg,
                       const RegisterBankInfo::ValueMapping &ValMapping,
                       bool &OnlyAssign) const;

  /// Insert repairing code for \p Reg as specified by \p ValMapping.
  /// The repairing placement is specified by \p RepairPt.
  /// \p NewVRegs contains all the registers required to remap \p Reg.
  /// In other words, the number of registers in NewVRegs must be equal
  /// to ValMapping.BreakDown.size().
  ///
  /// The transformation could be sketched as:
  /// \code
  /// ... = op Reg
  /// \endcode
  /// Becomes
  /// \code
  /// <NewRegs> = COPY or extract Reg
  /// ... = op Reg
  /// \endcode
  ///
  /// and
  /// \code
  /// Reg = op ...
  /// \endcode
  /// Becomes
  /// \code
  /// Reg = op ...
  /// Reg = COPY or build_sequence <NewRegs>
  /// \endcode
  ///
  /// \pre NewVRegs.size() == ValMapping.BreakDown.size()
  ///
  /// \note The caller is supposed to do the rewriting of op if need be.
  /// I.e., Reg = op ... => <NewRegs> = NewOp ...
  ///
  /// \return True if the repairing worked, false otherwise.
  bool repairReg(MachineOperand &MO,
                 const RegisterBankInfo::ValueMapping &ValMapping,
                 RegBankSelect::RepairingPlacement &RepairPt,
                 const iterator_range<SmallVectorImpl<Register>::const_iterator>
                     &NewVRegs);

  /// Return the cost of the instruction needed to map \p MO to \p ValMapping.
  /// The cost is free of basic block frequencies.
  /// \pre MO.isReg()
  /// \pre MO is assigned to a register bank.
  /// \pre ValMapping is a valid mapping for MO.
  uint64_t
  getRepairCost(const MachineOperand &MO,
                const RegisterBankInfo::ValueMapping &ValMapping) const;

  /// Find the best mapping for \p MI from \p PossibleMappings.
  /// \return a reference on the best mapping in \p PossibleMappings.
  const RegisterBankInfo::InstructionMapping &
  findBestMapping(MachineInstr &MI,
                  RegisterBankInfo::InstructionMappings &PossibleMappings,
                  SmallVectorImpl<RepairingPlacement> &RepairPts);

  /// Compute the cost of mapping \p MI with \p InstrMapping and
  /// compute the repairing placement for such mapping in \p
  /// RepairPts.
  /// \p BestCost is used to specify when the cost becomes too high
  /// and thus it is not worth computing the RepairPts.  Moreover if
  /// \p BestCost == nullptr, the mapping cost is actually not
  /// computed.
  MappingCost
  computeMapping(MachineInstr &MI,
                 const RegisterBankInfo::InstructionMapping &InstrMapping,
                 SmallVectorImpl<RepairingPlacement> &RepairPts,
                 const MappingCost *BestCost = nullptr);

  /// When \p RepairPt involves splitting to repair \p MO for the
  /// given \p ValMapping, try to change the way we repair such that
  /// the splitting is not required anymore.
  ///
  /// \pre \p RepairPt.hasSplit()
  /// \pre \p MO == MO.getParent()->getOperand(\p RepairPt.getOpIdx())
  /// \pre \p ValMapping is the mapping of \p MO for MO.getParent()
  ///      that implied \p RepairPt.
  void tryAvoidingSplit(RegBankSelect::RepairingPlacement &RepairPt,
                        const MachineOperand &MO,
                        const RegisterBankInfo::ValueMapping &ValMapping) const;

  /// Apply \p Mapping to \p MI. \p RepairPts represents the different
  /// mapping action that need to happen for the mapping to be
  /// applied.
  /// \return True if the mapping was applied sucessfully, false otherwise.
  bool applyMapping(MachineInstr &MI,
                    const RegisterBankInfo::InstructionMapping &InstrMapping,
                    SmallVectorImpl<RepairingPlacement> &RepairPts);

public:
  /// Create a RegBankSelect pass with the specified \p RunningMode.
  RegBankSelect(char &PassID = ID, Mode RunningMode = Fast);

  StringRef getPassName() const override { return "RegBankSelect"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties()
        .set(MachineFunctionProperties::Property::IsSSA)
        .set(MachineFunctionProperties::Property::Legalized);
  }

  MachineFunctionProperties getSetProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::RegBankSelected);
  }

  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties()
      .set(MachineFunctionProperties::Property::NoPHIs);
  }

  /// Check that our input is fully legal: we require the function to have the
  /// Legalized property, so it should be.
  ///
  /// FIXME: This should be in the MachineVerifier.
  bool checkFunctionIsLegal(MachineFunction &MF) const;

  /// Walk through \p MF and assign a register bank to every virtual register
  /// that are still mapped to nothing.
  /// The target needs to provide a RegisterBankInfo and in particular
  /// override RegisterBankInfo::getInstrMapping.
  ///
  /// Simplified algo:
  /// \code
  ///   RBI = MF.subtarget.getRegBankInfo()
  ///   MIRBuilder.setMF(MF)
  ///   for each bb in MF
  ///     for each inst in bb
  ///       MIRBuilder.setInstr(inst)
  ///       MappingCosts = RBI.getMapping(inst);
  ///       Idx = findIdxOfMinCost(MappingCosts)
  ///       CurRegBank = MappingCosts[Idx].RegBank
  ///       MRI.setRegBank(inst.getOperand(0).getReg(), CurRegBank)
  ///       for each argument in inst
  ///         if (CurRegBank != argument.RegBank)
  ///           ArgReg = argument.getReg()
  ///           Tmp = MRI.createNewVirtual(MRI.getSize(ArgReg), CurRegBank)
  ///           MIRBuilder.buildInstr(COPY, Tmp, ArgReg)
  ///           inst.getOperand(argument.getOperandNo()).setReg(Tmp)
  /// \endcode
  bool assignRegisterBanks(MachineFunction &MF);

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_REGBANKSELECT_H

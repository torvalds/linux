//===- SIMemoryLegalizer.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Memory legalizer - implements memory model. More information can be
/// found here:
///   http://llvm.org/docs/AMDGPUUsage.html#memory-model
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUMachineModuleInfo.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/MemoryModelRelaxationAnnotations.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/TargetParser/TargetParser.h"

using namespace llvm;
using namespace llvm::AMDGPU;

#define DEBUG_TYPE "si-memory-legalizer"
#define PASS_NAME "SI Memory Legalizer"

static cl::opt<bool> AmdgcnSkipCacheInvalidations(
    "amdgcn-skip-cache-invalidations", cl::init(false), cl::Hidden,
    cl::desc("Use this to skip inserting cache invalidating instructions."));

namespace {

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

/// Memory operation flags. Can be ORed together.
enum class SIMemOp {
  NONE = 0u,
  LOAD = 1u << 0,
  STORE = 1u << 1,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestFlag = */ STORE)
};

/// Position to insert a new instruction relative to an existing
/// instruction.
enum class Position {
  BEFORE,
  AFTER
};

/// The atomic synchronization scopes supported by the AMDGPU target.
enum class SIAtomicScope {
  NONE,
  SINGLETHREAD,
  WAVEFRONT,
  WORKGROUP,
  AGENT,
  SYSTEM
};

/// The distinct address spaces supported by the AMDGPU target for
/// atomic memory operation. Can be ORed together.
enum class SIAtomicAddrSpace {
  NONE = 0u,
  GLOBAL = 1u << 0,
  LDS = 1u << 1,
  SCRATCH = 1u << 2,
  GDS = 1u << 3,
  OTHER = 1u << 4,

  /// The address spaces that can be accessed by a FLAT instruction.
  FLAT = GLOBAL | LDS | SCRATCH,

  /// The address spaces that support atomic instructions.
  ATOMIC = GLOBAL | LDS | SCRATCH | GDS,

  /// All address spaces.
  ALL = GLOBAL | LDS | SCRATCH | GDS | OTHER,

  LLVM_MARK_AS_BITMASK_ENUM(/* LargestFlag = */ ALL)
};

class SIMemOpInfo final {
private:

  friend class SIMemOpAccess;

  AtomicOrdering Ordering = AtomicOrdering::NotAtomic;
  AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic;
  SIAtomicScope Scope = SIAtomicScope::SYSTEM;
  SIAtomicAddrSpace OrderingAddrSpace = SIAtomicAddrSpace::NONE;
  SIAtomicAddrSpace InstrAddrSpace = SIAtomicAddrSpace::NONE;
  bool IsCrossAddressSpaceOrdering = false;
  bool IsVolatile = false;
  bool IsNonTemporal = false;
  bool IsLastUse = false;

  SIMemOpInfo(
      AtomicOrdering Ordering = AtomicOrdering::SequentiallyConsistent,
      SIAtomicScope Scope = SIAtomicScope::SYSTEM,
      SIAtomicAddrSpace OrderingAddrSpace = SIAtomicAddrSpace::ATOMIC,
      SIAtomicAddrSpace InstrAddrSpace = SIAtomicAddrSpace::ALL,
      bool IsCrossAddressSpaceOrdering = true,
      AtomicOrdering FailureOrdering = AtomicOrdering::SequentiallyConsistent,
      bool IsVolatile = false, bool IsNonTemporal = false,
      bool IsLastUse = false)
      : Ordering(Ordering), FailureOrdering(FailureOrdering), Scope(Scope),
        OrderingAddrSpace(OrderingAddrSpace), InstrAddrSpace(InstrAddrSpace),
        IsCrossAddressSpaceOrdering(IsCrossAddressSpaceOrdering),
        IsVolatile(IsVolatile), IsNonTemporal(IsNonTemporal),
        IsLastUse(IsLastUse) {

    if (Ordering == AtomicOrdering::NotAtomic) {
      assert(Scope == SIAtomicScope::NONE &&
             OrderingAddrSpace == SIAtomicAddrSpace::NONE &&
             !IsCrossAddressSpaceOrdering &&
             FailureOrdering == AtomicOrdering::NotAtomic);
      return;
    }

    assert(Scope != SIAtomicScope::NONE &&
           (OrderingAddrSpace & SIAtomicAddrSpace::ATOMIC) !=
               SIAtomicAddrSpace::NONE &&
           (InstrAddrSpace & SIAtomicAddrSpace::ATOMIC) !=
               SIAtomicAddrSpace::NONE);

    // There is also no cross address space ordering if the ordering
    // address space is the same as the instruction address space and
    // only contains a single address space.
    if ((OrderingAddrSpace == InstrAddrSpace) &&
        isPowerOf2_32(uint32_t(InstrAddrSpace)))
      this->IsCrossAddressSpaceOrdering = false;

    // Limit the scope to the maximum supported by the instruction's address
    // spaces.
    if ((InstrAddrSpace & ~SIAtomicAddrSpace::SCRATCH) ==
        SIAtomicAddrSpace::NONE) {
      this->Scope = std::min(Scope, SIAtomicScope::SINGLETHREAD);
    } else if ((InstrAddrSpace &
                ~(SIAtomicAddrSpace::SCRATCH | SIAtomicAddrSpace::LDS)) ==
               SIAtomicAddrSpace::NONE) {
      this->Scope = std::min(Scope, SIAtomicScope::WORKGROUP);
    } else if ((InstrAddrSpace &
                ~(SIAtomicAddrSpace::SCRATCH | SIAtomicAddrSpace::LDS |
                  SIAtomicAddrSpace::GDS)) == SIAtomicAddrSpace::NONE) {
      this->Scope = std::min(Scope, SIAtomicScope::AGENT);
    }
  }

public:
  /// \returns Atomic synchronization scope of the machine instruction used to
  /// create this SIMemOpInfo.
  SIAtomicScope getScope() const {
    return Scope;
  }

  /// \returns Ordering constraint of the machine instruction used to
  /// create this SIMemOpInfo.
  AtomicOrdering getOrdering() const {
    return Ordering;
  }

  /// \returns Failure ordering constraint of the machine instruction used to
  /// create this SIMemOpInfo.
  AtomicOrdering getFailureOrdering() const {
    return FailureOrdering;
  }

  /// \returns The address spaces be accessed by the machine
  /// instruction used to create this SIMemOpInfo.
  SIAtomicAddrSpace getInstrAddrSpace() const {
    return InstrAddrSpace;
  }

  /// \returns The address spaces that must be ordered by the machine
  /// instruction used to create this SIMemOpInfo.
  SIAtomicAddrSpace getOrderingAddrSpace() const {
    return OrderingAddrSpace;
  }

  /// \returns Return true iff memory ordering of operations on
  /// different address spaces is required.
  bool getIsCrossAddressSpaceOrdering() const {
    return IsCrossAddressSpaceOrdering;
  }

  /// \returns True if memory access of the machine instruction used to
  /// create this SIMemOpInfo is volatile, false otherwise.
  bool isVolatile() const {
    return IsVolatile;
  }

  /// \returns True if memory access of the machine instruction used to
  /// create this SIMemOpInfo is nontemporal, false otherwise.
  bool isNonTemporal() const {
    return IsNonTemporal;
  }

  /// \returns True if memory access of the machine instruction used to
  /// create this SIMemOpInfo is last use, false otherwise.
  bool isLastUse() const { return IsLastUse; }

  /// \returns True if ordering constraint of the machine instruction used to
  /// create this SIMemOpInfo is unordered or higher, false otherwise.
  bool isAtomic() const {
    return Ordering != AtomicOrdering::NotAtomic;
  }

};

class SIMemOpAccess final {
private:
  const AMDGPUMachineModuleInfo *MMI = nullptr;

  /// Reports unsupported message \p Msg for \p MI to LLVM context.
  void reportUnsupported(const MachineBasicBlock::iterator &MI,
                         const char *Msg) const;

  /// Inspects the target synchronization scope \p SSID and determines
  /// the SI atomic scope it corresponds to, the address spaces it
  /// covers, and whether the memory ordering applies between address
  /// spaces.
  std::optional<std::tuple<SIAtomicScope, SIAtomicAddrSpace, bool>>
  toSIAtomicScope(SyncScope::ID SSID, SIAtomicAddrSpace InstrAddrSpace) const;

  /// \return Return a bit set of the address spaces accessed by \p AS.
  SIAtomicAddrSpace toSIAtomicAddrSpace(unsigned AS) const;

  /// \returns Info constructed from \p MI, which has at least machine memory
  /// operand.
  std::optional<SIMemOpInfo>
  constructFromMIWithMMO(const MachineBasicBlock::iterator &MI) const;

public:
  /// Construct class to support accessing the machine memory operands
  /// of instructions in the machine function \p MF.
  SIMemOpAccess(const AMDGPUMachineModuleInfo &MMI);

  /// \returns Load info if \p MI is a load operation, "std::nullopt" otherwise.
  std::optional<SIMemOpInfo>
  getLoadInfo(const MachineBasicBlock::iterator &MI) const;

  /// \returns Store info if \p MI is a store operation, "std::nullopt"
  /// otherwise.
  std::optional<SIMemOpInfo>
  getStoreInfo(const MachineBasicBlock::iterator &MI) const;

  /// \returns Atomic fence info if \p MI is an atomic fence operation,
  /// "std::nullopt" otherwise.
  std::optional<SIMemOpInfo>
  getAtomicFenceInfo(const MachineBasicBlock::iterator &MI) const;

  /// \returns Atomic cmpxchg/rmw info if \p MI is an atomic cmpxchg or
  /// rmw operation, "std::nullopt" otherwise.
  std::optional<SIMemOpInfo>
  getAtomicCmpxchgOrRmwInfo(const MachineBasicBlock::iterator &MI) const;
};

class SICacheControl {
protected:

  /// AMDGPU subtarget info.
  const GCNSubtarget &ST;

  /// Instruction info.
  const SIInstrInfo *TII = nullptr;

  IsaVersion IV;

  /// Whether to insert cache invalidating instructions.
  bool InsertCacheInv;

  SICacheControl(const GCNSubtarget &ST);

  /// Sets named bit \p BitName to "true" if present in instruction \p MI.
  /// \returns Returns true if \p MI is modified, false otherwise.
  bool enableNamedBit(const MachineBasicBlock::iterator MI,
                      AMDGPU::CPol::CPol Bit) const;

public:

  /// Create a cache control for the subtarget \p ST.
  static std::unique_ptr<SICacheControl> create(const GCNSubtarget &ST);

  /// Update \p MI memory load instruction to bypass any caches up to
  /// the \p Scope memory scope for address spaces \p
  /// AddrSpace. Return true iff the instruction was modified.
  virtual bool enableLoadCacheBypass(const MachineBasicBlock::iterator &MI,
                                     SIAtomicScope Scope,
                                     SIAtomicAddrSpace AddrSpace) const = 0;

  /// Update \p MI memory store instruction to bypass any caches up to
  /// the \p Scope memory scope for address spaces \p
  /// AddrSpace. Return true iff the instruction was modified.
  virtual bool enableStoreCacheBypass(const MachineBasicBlock::iterator &MI,
                                      SIAtomicScope Scope,
                                      SIAtomicAddrSpace AddrSpace) const = 0;

  /// Update \p MI memory read-modify-write instruction to bypass any caches up
  /// to the \p Scope memory scope for address spaces \p AddrSpace. Return true
  /// iff the instruction was modified.
  virtual bool enableRMWCacheBypass(const MachineBasicBlock::iterator &MI,
                                    SIAtomicScope Scope,
                                    SIAtomicAddrSpace AddrSpace) const = 0;

  /// Update \p MI memory instruction of kind \p Op associated with address
  /// spaces \p AddrSpace to indicate it is volatile and/or
  /// nontemporal/last-use. Return true iff the instruction was modified.
  virtual bool enableVolatileAndOrNonTemporal(MachineBasicBlock::iterator &MI,
                                              SIAtomicAddrSpace AddrSpace,
                                              SIMemOp Op, bool IsVolatile,
                                              bool IsNonTemporal,
                                              bool IsLastUse = false) const = 0;

  virtual bool expandSystemScopeStore(MachineBasicBlock::iterator &MI) const {
    return false;
  };

  /// Inserts any necessary instructions at position \p Pos relative
  /// to instruction \p MI to ensure memory instructions before \p Pos of kind
  /// \p Op associated with address spaces \p AddrSpace have completed. Used
  /// between memory instructions to enforce the order they become visible as
  /// observed by other memory instructions executing in memory scope \p Scope.
  /// \p IsCrossAddrSpaceOrdering indicates if the memory ordering is between
  /// address spaces. Returns true iff any instructions inserted.
  virtual bool insertWait(MachineBasicBlock::iterator &MI,
                          SIAtomicScope Scope,
                          SIAtomicAddrSpace AddrSpace,
                          SIMemOp Op,
                          bool IsCrossAddrSpaceOrdering,
                          Position Pos) const = 0;

  /// Inserts any necessary instructions at position \p Pos relative to
  /// instruction \p MI to ensure any subsequent memory instructions of this
  /// thread with address spaces \p AddrSpace will observe the previous memory
  /// operations by any thread for memory scopes up to memory scope \p Scope .
  /// Returns true iff any instructions inserted.
  virtual bool insertAcquire(MachineBasicBlock::iterator &MI,
                             SIAtomicScope Scope,
                             SIAtomicAddrSpace AddrSpace,
                             Position Pos) const = 0;

  /// Inserts any necessary instructions at position \p Pos relative to
  /// instruction \p MI to ensure previous memory instructions by this thread
  /// with address spaces \p AddrSpace have completed and can be observed by
  /// subsequent memory instructions by any thread executing in memory scope \p
  /// Scope. \p IsCrossAddrSpaceOrdering indicates if the memory ordering is
  /// between address spaces. Returns true iff any instructions inserted.
  virtual bool insertRelease(MachineBasicBlock::iterator &MI,
                             SIAtomicScope Scope,
                             SIAtomicAddrSpace AddrSpace,
                             bool IsCrossAddrSpaceOrdering,
                             Position Pos) const = 0;

  /// Virtual destructor to allow derivations to be deleted.
  virtual ~SICacheControl() = default;

  virtual bool tryForceStoreSC0SC1(const SIMemOpInfo &MOI,
                                   MachineBasicBlock::iterator &MI) const {
    return false;
  }
};

class SIGfx6CacheControl : public SICacheControl {
protected:

  /// Sets GLC bit to "true" if present in \p MI. Returns true if \p MI
  /// is modified, false otherwise.
  bool enableGLCBit(const MachineBasicBlock::iterator &MI) const {
    return enableNamedBit(MI, AMDGPU::CPol::GLC);
  }

  /// Sets SLC bit to "true" if present in \p MI. Returns true if \p MI
  /// is modified, false otherwise.
  bool enableSLCBit(const MachineBasicBlock::iterator &MI) const {
    return enableNamedBit(MI, AMDGPU::CPol::SLC);
  }

public:

  SIGfx6CacheControl(const GCNSubtarget &ST) : SICacheControl(ST) {}

  bool enableLoadCacheBypass(const MachineBasicBlock::iterator &MI,
                             SIAtomicScope Scope,
                             SIAtomicAddrSpace AddrSpace) const override;

  bool enableStoreCacheBypass(const MachineBasicBlock::iterator &MI,
                              SIAtomicScope Scope,
                              SIAtomicAddrSpace AddrSpace) const override;

  bool enableRMWCacheBypass(const MachineBasicBlock::iterator &MI,
                            SIAtomicScope Scope,
                            SIAtomicAddrSpace AddrSpace) const override;

  bool enableVolatileAndOrNonTemporal(MachineBasicBlock::iterator &MI,
                                      SIAtomicAddrSpace AddrSpace, SIMemOp Op,
                                      bool IsVolatile, bool IsNonTemporal,
                                      bool IsLastUse) const override;

  bool insertWait(MachineBasicBlock::iterator &MI,
                  SIAtomicScope Scope,
                  SIAtomicAddrSpace AddrSpace,
                  SIMemOp Op,
                  bool IsCrossAddrSpaceOrdering,
                  Position Pos) const override;

  bool insertAcquire(MachineBasicBlock::iterator &MI,
                     SIAtomicScope Scope,
                     SIAtomicAddrSpace AddrSpace,
                     Position Pos) const override;

  bool insertRelease(MachineBasicBlock::iterator &MI,
                     SIAtomicScope Scope,
                     SIAtomicAddrSpace AddrSpace,
                     bool IsCrossAddrSpaceOrdering,
                     Position Pos) const override;
};

class SIGfx7CacheControl : public SIGfx6CacheControl {
public:

  SIGfx7CacheControl(const GCNSubtarget &ST) : SIGfx6CacheControl(ST) {}

  bool insertAcquire(MachineBasicBlock::iterator &MI,
                     SIAtomicScope Scope,
                     SIAtomicAddrSpace AddrSpace,
                     Position Pos) const override;

};

class SIGfx90ACacheControl : public SIGfx7CacheControl {
public:

  SIGfx90ACacheControl(const GCNSubtarget &ST) : SIGfx7CacheControl(ST) {}

  bool enableLoadCacheBypass(const MachineBasicBlock::iterator &MI,
                             SIAtomicScope Scope,
                             SIAtomicAddrSpace AddrSpace) const override;

  bool enableStoreCacheBypass(const MachineBasicBlock::iterator &MI,
                              SIAtomicScope Scope,
                              SIAtomicAddrSpace AddrSpace) const override;

  bool enableRMWCacheBypass(const MachineBasicBlock::iterator &MI,
                            SIAtomicScope Scope,
                            SIAtomicAddrSpace AddrSpace) const override;

  bool enableVolatileAndOrNonTemporal(MachineBasicBlock::iterator &MI,
                                      SIAtomicAddrSpace AddrSpace, SIMemOp Op,
                                      bool IsVolatile, bool IsNonTemporal,
                                      bool IsLastUse) const override;

  bool insertWait(MachineBasicBlock::iterator &MI,
                  SIAtomicScope Scope,
                  SIAtomicAddrSpace AddrSpace,
                  SIMemOp Op,
                  bool IsCrossAddrSpaceOrdering,
                  Position Pos) const override;

  bool insertAcquire(MachineBasicBlock::iterator &MI,
                     SIAtomicScope Scope,
                     SIAtomicAddrSpace AddrSpace,
                     Position Pos) const override;

  bool insertRelease(MachineBasicBlock::iterator &MI,
                     SIAtomicScope Scope,
                     SIAtomicAddrSpace AddrSpace,
                     bool IsCrossAddrSpaceOrdering,
                     Position Pos) const override;
};

class SIGfx940CacheControl : public SIGfx90ACacheControl {
protected:

  /// Sets SC0 bit to "true" if present in \p MI. Returns true if \p MI
  /// is modified, false otherwise.
  bool enableSC0Bit(const MachineBasicBlock::iterator &MI) const {
    return enableNamedBit(MI, AMDGPU::CPol::SC0);
  }

  /// Sets SC1 bit to "true" if present in \p MI. Returns true if \p MI
  /// is modified, false otherwise.
  bool enableSC1Bit(const MachineBasicBlock::iterator &MI) const {
    return enableNamedBit(MI, AMDGPU::CPol::SC1);
  }

  /// Sets NT bit to "true" if present in \p MI. Returns true if \p MI
  /// is modified, false otherwise.
  bool enableNTBit(const MachineBasicBlock::iterator &MI) const {
    return enableNamedBit(MI, AMDGPU::CPol::NT);
  }

public:

  SIGfx940CacheControl(const GCNSubtarget &ST) : SIGfx90ACacheControl(ST) {};

  bool enableLoadCacheBypass(const MachineBasicBlock::iterator &MI,
                             SIAtomicScope Scope,
                             SIAtomicAddrSpace AddrSpace) const override;

  bool enableStoreCacheBypass(const MachineBasicBlock::iterator &MI,
                              SIAtomicScope Scope,
                              SIAtomicAddrSpace AddrSpace) const override;

  bool enableRMWCacheBypass(const MachineBasicBlock::iterator &MI,
                            SIAtomicScope Scope,
                            SIAtomicAddrSpace AddrSpace) const override;

  bool enableVolatileAndOrNonTemporal(MachineBasicBlock::iterator &MI,
                                      SIAtomicAddrSpace AddrSpace, SIMemOp Op,
                                      bool IsVolatile, bool IsNonTemporal,
                                      bool IsLastUse) const override;

  bool insertAcquire(MachineBasicBlock::iterator &MI, SIAtomicScope Scope,
                     SIAtomicAddrSpace AddrSpace, Position Pos) const override;

  bool insertRelease(MachineBasicBlock::iterator &MI, SIAtomicScope Scope,
                     SIAtomicAddrSpace AddrSpace, bool IsCrossAddrSpaceOrdering,
                     Position Pos) const override;

  bool tryForceStoreSC0SC1(const SIMemOpInfo &MOI,
                           MachineBasicBlock::iterator &MI) const override {
    bool Changed = false;
    if (ST.hasForceStoreSC0SC1() &&
        (MOI.getInstrAddrSpace() & (SIAtomicAddrSpace::SCRATCH |
                                    SIAtomicAddrSpace::GLOBAL |
                                    SIAtomicAddrSpace::OTHER)) !=
         SIAtomicAddrSpace::NONE) {
      Changed |= enableSC0Bit(MI);
      Changed |= enableSC1Bit(MI);
    }
    return Changed;
  }
};

class SIGfx10CacheControl : public SIGfx7CacheControl {
protected:

  /// Sets DLC bit to "true" if present in \p MI. Returns true if \p MI
  /// is modified, false otherwise.
  bool enableDLCBit(const MachineBasicBlock::iterator &MI) const {
    return enableNamedBit(MI, AMDGPU::CPol::DLC);
  }

public:

  SIGfx10CacheControl(const GCNSubtarget &ST) : SIGfx7CacheControl(ST) {}

  bool enableLoadCacheBypass(const MachineBasicBlock::iterator &MI,
                             SIAtomicScope Scope,
                             SIAtomicAddrSpace AddrSpace) const override;

  bool enableVolatileAndOrNonTemporal(MachineBasicBlock::iterator &MI,
                                      SIAtomicAddrSpace AddrSpace, SIMemOp Op,
                                      bool IsVolatile, bool IsNonTemporal,
                                      bool IsLastUse) const override;

  bool insertWait(MachineBasicBlock::iterator &MI,
                  SIAtomicScope Scope,
                  SIAtomicAddrSpace AddrSpace,
                  SIMemOp Op,
                  bool IsCrossAddrSpaceOrdering,
                  Position Pos) const override;

  bool insertAcquire(MachineBasicBlock::iterator &MI,
                     SIAtomicScope Scope,
                     SIAtomicAddrSpace AddrSpace,
                     Position Pos) const override;
};

class SIGfx11CacheControl : public SIGfx10CacheControl {
public:
  SIGfx11CacheControl(const GCNSubtarget &ST) : SIGfx10CacheControl(ST) {}

  bool enableLoadCacheBypass(const MachineBasicBlock::iterator &MI,
                             SIAtomicScope Scope,
                             SIAtomicAddrSpace AddrSpace) const override;

  bool enableVolatileAndOrNonTemporal(MachineBasicBlock::iterator &MI,
                                      SIAtomicAddrSpace AddrSpace, SIMemOp Op,
                                      bool IsVolatile, bool IsNonTemporal,
                                      bool IsLastUse) const override;
};

class SIGfx12CacheControl : public SIGfx11CacheControl {
protected:
  // Sets TH policy to \p Value if CPol operand is present in instruction \p MI.
  // \returns Returns true if \p MI is modified, false otherwise.
  bool setTH(const MachineBasicBlock::iterator MI,
             AMDGPU::CPol::CPol Value) const;
  // Sets Scope policy to \p Value if CPol operand is present in instruction \p
  // MI. \returns Returns true if \p MI is modified, false otherwise.
  bool setScope(const MachineBasicBlock::iterator MI,
                AMDGPU::CPol::CPol Value) const;

  // Stores with system scope (SCOPE_SYS) need to wait for:
  // - loads or atomics(returning) - wait for {LOAD|SAMPLE|BVH|KM}CNT==0
  // - non-returning-atomics       - wait for STORECNT==0
  //   TODO: SIInsertWaitcnts will not always be able to remove STORECNT waits
  //   since it does not distinguish atomics-with-return from regular stores.
  // There is no need to wait if memory is cached (mtype != UC).
  bool
  insertWaitsBeforeSystemScopeStore(const MachineBasicBlock::iterator MI) const;

  bool setAtomicScope(const MachineBasicBlock::iterator &MI,
                      SIAtomicScope Scope, SIAtomicAddrSpace AddrSpace) const;

public:
  SIGfx12CacheControl(const GCNSubtarget &ST) : SIGfx11CacheControl(ST) {}

  bool insertWait(MachineBasicBlock::iterator &MI, SIAtomicScope Scope,
                  SIAtomicAddrSpace AddrSpace, SIMemOp Op,
                  bool IsCrossAddrSpaceOrdering, Position Pos) const override;

  bool insertAcquire(MachineBasicBlock::iterator &MI, SIAtomicScope Scope,
                     SIAtomicAddrSpace AddrSpace, Position Pos) const override;

  bool enableVolatileAndOrNonTemporal(MachineBasicBlock::iterator &MI,
                                      SIAtomicAddrSpace AddrSpace, SIMemOp Op,
                                      bool IsVolatile, bool IsNonTemporal,
                                      bool IsLastUse) const override;

  bool expandSystemScopeStore(MachineBasicBlock::iterator &MI) const override;

  bool insertRelease(MachineBasicBlock::iterator &MI, SIAtomicScope Scope,
                     SIAtomicAddrSpace AddrSpace, bool IsCrossAddrSpaceOrdering,
                     Position Pos) const override;

  bool enableLoadCacheBypass(const MachineBasicBlock::iterator &MI,
                             SIAtomicScope Scope,
                             SIAtomicAddrSpace AddrSpace) const override {
    return setAtomicScope(MI, Scope, AddrSpace);
  }

  bool enableStoreCacheBypass(const MachineBasicBlock::iterator &MI,
                              SIAtomicScope Scope,
                              SIAtomicAddrSpace AddrSpace) const override {
    return setAtomicScope(MI, Scope, AddrSpace);
  }

  bool enableRMWCacheBypass(const MachineBasicBlock::iterator &MI,
                            SIAtomicScope Scope,
                            SIAtomicAddrSpace AddrSpace) const override {
    return setAtomicScope(MI, Scope, AddrSpace);
  }
};

class SIMemoryLegalizer final : public MachineFunctionPass {
private:

  /// Cache Control.
  std::unique_ptr<SICacheControl> CC = nullptr;

  /// List of atomic pseudo instructions.
  std::list<MachineBasicBlock::iterator> AtomicPseudoMIs;

  /// Return true iff instruction \p MI is a atomic instruction that
  /// returns a result.
  bool isAtomicRet(const MachineInstr &MI) const {
    return SIInstrInfo::isAtomicRet(MI);
  }

  /// Removes all processed atomic pseudo instructions from the current
  /// function. Returns true if current function is modified, false otherwise.
  bool removeAtomicPseudoMIs();

  /// Expands load operation \p MI. Returns true if instructions are
  /// added/deleted or \p MI is modified, false otherwise.
  bool expandLoad(const SIMemOpInfo &MOI,
                  MachineBasicBlock::iterator &MI);
  /// Expands store operation \p MI. Returns true if instructions are
  /// added/deleted or \p MI is modified, false otherwise.
  bool expandStore(const SIMemOpInfo &MOI,
                   MachineBasicBlock::iterator &MI);
  /// Expands atomic fence operation \p MI. Returns true if
  /// instructions are added/deleted or \p MI is modified, false otherwise.
  bool expandAtomicFence(const SIMemOpInfo &MOI,
                         MachineBasicBlock::iterator &MI);
  /// Expands atomic cmpxchg or rmw operation \p MI. Returns true if
  /// instructions are added/deleted or \p MI is modified, false otherwise.
  bool expandAtomicCmpxchgOrRmw(const SIMemOpInfo &MOI,
                                MachineBasicBlock::iterator &MI);

public:
  static char ID;

  SIMemoryLegalizer() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override {
    return PASS_NAME;
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

static const StringMap<SIAtomicAddrSpace> ASNames = {{
    {"global", SIAtomicAddrSpace::GLOBAL},
    {"local", SIAtomicAddrSpace::LDS},
}};

void diagnoseUnknownMMRAASName(const MachineInstr &MI, StringRef AS) {
  const MachineFunction *MF = MI.getMF();
  const Function &Fn = MF->getFunction();
  SmallString<128> Str;
  raw_svector_ostream OS(Str);
  OS << "unknown address space '" << AS << "'; expected one of ";
  ListSeparator LS;
  for (const auto &[Name, Val] : ASNames)
    OS << LS << '\'' << Name << '\'';
  DiagnosticInfoUnsupported BadTag(Fn, Str.str(), MI.getDebugLoc(), DS_Warning);
  Fn.getContext().diagnose(BadTag);
}

/// Reads \p MI's MMRAs to parse the "amdgpu-as" MMRA.
/// If this tag isn't present, or if it has no meaningful values, returns \p
/// Default. Otherwise returns all the address spaces concerned by the MMRA.
static SIAtomicAddrSpace getFenceAddrSpaceMMRA(const MachineInstr &MI,
                                               SIAtomicAddrSpace Default) {
  static constexpr StringLiteral FenceASPrefix = "amdgpu-as";

  auto MMRA = MMRAMetadata(MI.getMMRAMetadata());
  if (!MMRA)
    return Default;

  SIAtomicAddrSpace Result = SIAtomicAddrSpace::NONE;
  for (const auto &[Prefix, Suffix] : MMRA) {
    if (Prefix != FenceASPrefix)
      continue;

    if (auto It = ASNames.find(Suffix); It != ASNames.end())
      Result |= It->second;
    else
      diagnoseUnknownMMRAASName(MI, Suffix);
  }

  return (Result != SIAtomicAddrSpace::NONE) ? Result : Default;
}

} // end anonymous namespace

void SIMemOpAccess::reportUnsupported(const MachineBasicBlock::iterator &MI,
                                      const char *Msg) const {
  const Function &Func = MI->getParent()->getParent()->getFunction();
  DiagnosticInfoUnsupported Diag(Func, Msg, MI->getDebugLoc());
  Func.getContext().diagnose(Diag);
}

std::optional<std::tuple<SIAtomicScope, SIAtomicAddrSpace, bool>>
SIMemOpAccess::toSIAtomicScope(SyncScope::ID SSID,
                               SIAtomicAddrSpace InstrAddrSpace) const {
  if (SSID == SyncScope::System)
    return std::tuple(SIAtomicScope::SYSTEM, SIAtomicAddrSpace::ATOMIC, true);
  if (SSID == MMI->getAgentSSID())
    return std::tuple(SIAtomicScope::AGENT, SIAtomicAddrSpace::ATOMIC, true);
  if (SSID == MMI->getWorkgroupSSID())
    return std::tuple(SIAtomicScope::WORKGROUP, SIAtomicAddrSpace::ATOMIC,
                      true);
  if (SSID == MMI->getWavefrontSSID())
    return std::tuple(SIAtomicScope::WAVEFRONT, SIAtomicAddrSpace::ATOMIC,
                      true);
  if (SSID == SyncScope::SingleThread)
    return std::tuple(SIAtomicScope::SINGLETHREAD, SIAtomicAddrSpace::ATOMIC,
                      true);
  if (SSID == MMI->getSystemOneAddressSpaceSSID())
    return std::tuple(SIAtomicScope::SYSTEM,
                      SIAtomicAddrSpace::ATOMIC & InstrAddrSpace, false);
  if (SSID == MMI->getAgentOneAddressSpaceSSID())
    return std::tuple(SIAtomicScope::AGENT,
                      SIAtomicAddrSpace::ATOMIC & InstrAddrSpace, false);
  if (SSID == MMI->getWorkgroupOneAddressSpaceSSID())
    return std::tuple(SIAtomicScope::WORKGROUP,
                      SIAtomicAddrSpace::ATOMIC & InstrAddrSpace, false);
  if (SSID == MMI->getWavefrontOneAddressSpaceSSID())
    return std::tuple(SIAtomicScope::WAVEFRONT,
                      SIAtomicAddrSpace::ATOMIC & InstrAddrSpace, false);
  if (SSID == MMI->getSingleThreadOneAddressSpaceSSID())
    return std::tuple(SIAtomicScope::SINGLETHREAD,
                      SIAtomicAddrSpace::ATOMIC & InstrAddrSpace, false);
  return std::nullopt;
}

SIAtomicAddrSpace SIMemOpAccess::toSIAtomicAddrSpace(unsigned AS) const {
  if (AS == AMDGPUAS::FLAT_ADDRESS)
    return SIAtomicAddrSpace::FLAT;
  if (AS == AMDGPUAS::GLOBAL_ADDRESS)
    return SIAtomicAddrSpace::GLOBAL;
  if (AS == AMDGPUAS::LOCAL_ADDRESS)
    return SIAtomicAddrSpace::LDS;
  if (AS == AMDGPUAS::PRIVATE_ADDRESS)
    return SIAtomicAddrSpace::SCRATCH;
  if (AS == AMDGPUAS::REGION_ADDRESS)
    return SIAtomicAddrSpace::GDS;

  return SIAtomicAddrSpace::OTHER;
}

SIMemOpAccess::SIMemOpAccess(const AMDGPUMachineModuleInfo &MMI_)
    : MMI(&MMI_) {}

std::optional<SIMemOpInfo> SIMemOpAccess::constructFromMIWithMMO(
    const MachineBasicBlock::iterator &MI) const {
  assert(MI->getNumMemOperands() > 0);

  SyncScope::ID SSID = SyncScope::SingleThread;
  AtomicOrdering Ordering = AtomicOrdering::NotAtomic;
  AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic;
  SIAtomicAddrSpace InstrAddrSpace = SIAtomicAddrSpace::NONE;
  bool IsNonTemporal = true;
  bool IsVolatile = false;
  bool IsLastUse = false;

  // Validator should check whether or not MMOs cover the entire set of
  // locations accessed by the memory instruction.
  for (const auto &MMO : MI->memoperands()) {
    IsNonTemporal &= MMO->isNonTemporal();
    IsVolatile |= MMO->isVolatile();
    IsLastUse |= MMO->getFlags() & MOLastUse;
    InstrAddrSpace |=
      toSIAtomicAddrSpace(MMO->getPointerInfo().getAddrSpace());
    AtomicOrdering OpOrdering = MMO->getSuccessOrdering();
    if (OpOrdering != AtomicOrdering::NotAtomic) {
      const auto &IsSyncScopeInclusion =
          MMI->isSyncScopeInclusion(SSID, MMO->getSyncScopeID());
      if (!IsSyncScopeInclusion) {
        reportUnsupported(MI,
          "Unsupported non-inclusive atomic synchronization scope");
        return std::nullopt;
      }

      SSID = *IsSyncScopeInclusion ? SSID : MMO->getSyncScopeID();
      Ordering = getMergedAtomicOrdering(Ordering, OpOrdering);
      assert(MMO->getFailureOrdering() != AtomicOrdering::Release &&
             MMO->getFailureOrdering() != AtomicOrdering::AcquireRelease);
      FailureOrdering =
          getMergedAtomicOrdering(FailureOrdering, MMO->getFailureOrdering());
    }
  }

  SIAtomicScope Scope = SIAtomicScope::NONE;
  SIAtomicAddrSpace OrderingAddrSpace = SIAtomicAddrSpace::NONE;
  bool IsCrossAddressSpaceOrdering = false;
  if (Ordering != AtomicOrdering::NotAtomic) {
    auto ScopeOrNone = toSIAtomicScope(SSID, InstrAddrSpace);
    if (!ScopeOrNone) {
      reportUnsupported(MI, "Unsupported atomic synchronization scope");
      return std::nullopt;
    }
    std::tie(Scope, OrderingAddrSpace, IsCrossAddressSpaceOrdering) =
        *ScopeOrNone;
    if ((OrderingAddrSpace == SIAtomicAddrSpace::NONE) ||
        ((OrderingAddrSpace & SIAtomicAddrSpace::ATOMIC) != OrderingAddrSpace) ||
        ((InstrAddrSpace & SIAtomicAddrSpace::ATOMIC) == SIAtomicAddrSpace::NONE)) {
      reportUnsupported(MI, "Unsupported atomic address space");
      return std::nullopt;
    }
  }
  return SIMemOpInfo(Ordering, Scope, OrderingAddrSpace, InstrAddrSpace,
                     IsCrossAddressSpaceOrdering, FailureOrdering, IsVolatile,
                     IsNonTemporal, IsLastUse);
}

std::optional<SIMemOpInfo>
SIMemOpAccess::getLoadInfo(const MachineBasicBlock::iterator &MI) const {
  assert(MI->getDesc().TSFlags & SIInstrFlags::maybeAtomic);

  if (!(MI->mayLoad() && !MI->mayStore()))
    return std::nullopt;

  // Be conservative if there are no memory operands.
  if (MI->getNumMemOperands() == 0)
    return SIMemOpInfo();

  return constructFromMIWithMMO(MI);
}

std::optional<SIMemOpInfo>
SIMemOpAccess::getStoreInfo(const MachineBasicBlock::iterator &MI) const {
  assert(MI->getDesc().TSFlags & SIInstrFlags::maybeAtomic);

  if (!(!MI->mayLoad() && MI->mayStore()))
    return std::nullopt;

  // Be conservative if there are no memory operands.
  if (MI->getNumMemOperands() == 0)
    return SIMemOpInfo();

  return constructFromMIWithMMO(MI);
}

std::optional<SIMemOpInfo>
SIMemOpAccess::getAtomicFenceInfo(const MachineBasicBlock::iterator &MI) const {
  assert(MI->getDesc().TSFlags & SIInstrFlags::maybeAtomic);

  if (MI->getOpcode() != AMDGPU::ATOMIC_FENCE)
    return std::nullopt;

  AtomicOrdering Ordering =
    static_cast<AtomicOrdering>(MI->getOperand(0).getImm());

  SyncScope::ID SSID = static_cast<SyncScope::ID>(MI->getOperand(1).getImm());
  auto ScopeOrNone = toSIAtomicScope(SSID, SIAtomicAddrSpace::ATOMIC);
  if (!ScopeOrNone) {
    reportUnsupported(MI, "Unsupported atomic synchronization scope");
    return std::nullopt;
  }

  SIAtomicScope Scope = SIAtomicScope::NONE;
  SIAtomicAddrSpace OrderingAddrSpace = SIAtomicAddrSpace::NONE;
  bool IsCrossAddressSpaceOrdering = false;
  std::tie(Scope, OrderingAddrSpace, IsCrossAddressSpaceOrdering) =
      *ScopeOrNone;

  if ((OrderingAddrSpace == SIAtomicAddrSpace::NONE) ||
      ((OrderingAddrSpace & SIAtomicAddrSpace::ATOMIC) != OrderingAddrSpace)) {
    reportUnsupported(MI, "Unsupported atomic address space");
    return std::nullopt;
  }

  return SIMemOpInfo(Ordering, Scope, OrderingAddrSpace, SIAtomicAddrSpace::ATOMIC,
                     IsCrossAddressSpaceOrdering, AtomicOrdering::NotAtomic);
}

std::optional<SIMemOpInfo> SIMemOpAccess::getAtomicCmpxchgOrRmwInfo(
    const MachineBasicBlock::iterator &MI) const {
  assert(MI->getDesc().TSFlags & SIInstrFlags::maybeAtomic);

  if (!(MI->mayLoad() && MI->mayStore()))
    return std::nullopt;

  // Be conservative if there are no memory operands.
  if (MI->getNumMemOperands() == 0)
    return SIMemOpInfo();

  return constructFromMIWithMMO(MI);
}

SICacheControl::SICacheControl(const GCNSubtarget &ST) : ST(ST) {
  TII = ST.getInstrInfo();
  IV = getIsaVersion(ST.getCPU());
  InsertCacheInv = !AmdgcnSkipCacheInvalidations;
}

bool SICacheControl::enableNamedBit(const MachineBasicBlock::iterator MI,
                                    AMDGPU::CPol::CPol Bit) const {
  MachineOperand *CPol = TII->getNamedOperand(*MI, AMDGPU::OpName::cpol);
  if (!CPol)
    return false;

  CPol->setImm(CPol->getImm() | Bit);
  return true;
}

/* static */
std::unique_ptr<SICacheControl> SICacheControl::create(const GCNSubtarget &ST) {
  GCNSubtarget::Generation Generation = ST.getGeneration();
  if (ST.hasGFX940Insts())
    return std::make_unique<SIGfx940CacheControl>(ST);
  if (ST.hasGFX90AInsts())
    return std::make_unique<SIGfx90ACacheControl>(ST);
  if (Generation <= AMDGPUSubtarget::SOUTHERN_ISLANDS)
    return std::make_unique<SIGfx6CacheControl>(ST);
  if (Generation < AMDGPUSubtarget::GFX10)
    return std::make_unique<SIGfx7CacheControl>(ST);
  if (Generation < AMDGPUSubtarget::GFX11)
    return std::make_unique<SIGfx10CacheControl>(ST);
  if (Generation < AMDGPUSubtarget::GFX12)
    return std::make_unique<SIGfx11CacheControl>(ST);
  return std::make_unique<SIGfx12CacheControl>(ST);
}

bool SIGfx6CacheControl::enableLoadCacheBypass(
    const MachineBasicBlock::iterator &MI,
    SIAtomicScope Scope,
    SIAtomicAddrSpace AddrSpace) const {
  assert(MI->mayLoad() && !MI->mayStore());
  bool Changed = false;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      // Set L1 cache policy to MISS_EVICT.
      // Note: there is no L2 cache bypass policy at the ISA level.
      Changed |= enableGLCBit(MI);
      break;
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // No cache to bypass.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory caches
  /// to be bypassed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  return Changed;
}

bool SIGfx6CacheControl::enableStoreCacheBypass(
    const MachineBasicBlock::iterator &MI,
    SIAtomicScope Scope,
    SIAtomicAddrSpace AddrSpace) const {
  assert(!MI->mayLoad() && MI->mayStore());
  bool Changed = false;

  /// The L1 cache is write through so does not need to be bypassed. There is no
  /// bypass control for the L2 cache at the isa level.

  return Changed;
}

bool SIGfx6CacheControl::enableRMWCacheBypass(
    const MachineBasicBlock::iterator &MI,
    SIAtomicScope Scope,
    SIAtomicAddrSpace AddrSpace) const {
  assert(MI->mayLoad() && MI->mayStore());
  bool Changed = false;

  /// Do not set GLC for RMW atomic operations as L0/L1 cache is automatically
  /// bypassed, and the GLC bit is instead used to indicate if they are
  /// return or no-return.
  /// Note: there is no L2 cache coherent bypass control at the ISA level.

  return Changed;
}

bool SIGfx6CacheControl::enableVolatileAndOrNonTemporal(
    MachineBasicBlock::iterator &MI, SIAtomicAddrSpace AddrSpace, SIMemOp Op,
    bool IsVolatile, bool IsNonTemporal, bool IsLastUse = false) const {
  // Only handle load and store, not atomic read-modify-write insructions. The
  // latter use glc to indicate if the atomic returns a result and so must not
  // be used for cache control.
  assert(MI->mayLoad() ^ MI->mayStore());

  // Only update load and store, not LLVM IR atomic read-modify-write
  // instructions. The latter are always marked as volatile so cannot sensibly
  // handle it as do not want to pessimize all atomics. Also they do not support
  // the nontemporal attribute.
  assert(Op == SIMemOp::LOAD || Op == SIMemOp::STORE);

  bool Changed = false;

  if (IsVolatile) {
    // Set L1 cache policy to be MISS_EVICT for load instructions
    // and MISS_LRU for store instructions.
    // Note: there is no L2 cache bypass policy at the ISA level.
    if (Op == SIMemOp::LOAD)
      Changed |= enableGLCBit(MI);

    // Ensure operation has completed at system scope to cause all volatile
    // operations to be visible outside the program in a global order. Do not
    // request cross address space as only the global address space can be
    // observable outside the program, so no need to cause a waitcnt for LDS
    // address space operations.
    Changed |= insertWait(MI, SIAtomicScope::SYSTEM, AddrSpace, Op, false,
                          Position::AFTER);

    return Changed;
  }

  if (IsNonTemporal) {
    // Setting both GLC and SLC configures L1 cache policy to MISS_EVICT
    // for both loads and stores, and the L2 cache policy to STREAM.
    Changed |= enableGLCBit(MI);
    Changed |= enableSLCBit(MI);
    return Changed;
  }

  return Changed;
}

bool SIGfx6CacheControl::insertWait(MachineBasicBlock::iterator &MI,
                                    SIAtomicScope Scope,
                                    SIAtomicAddrSpace AddrSpace,
                                    SIMemOp Op,
                                    bool IsCrossAddrSpaceOrdering,
                                    Position Pos) const {
  bool Changed = false;

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  if (Pos == Position::AFTER)
    ++MI;

  bool VMCnt = false;
  bool LGKMCnt = false;

  if ((AddrSpace & (SIAtomicAddrSpace::GLOBAL | SIAtomicAddrSpace::SCRATCH)) !=
      SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      VMCnt |= true;
      break;
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // The L1 cache keeps all memory operations in order for
      // wavefronts in the same work-group.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  if ((AddrSpace & SIAtomicAddrSpace::LDS) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
    case SIAtomicScope::WORKGROUP:
      // If no cross address space ordering then an "S_WAITCNT lgkmcnt(0)" is
      // not needed as LDS operations for all waves are executed in a total
      // global ordering as observed by all waves. Required if also
      // synchronizing with global/GDS memory as LDS operations could be
      // reordered with respect to later global/GDS memory operations of the
      // same wave.
      LGKMCnt |= IsCrossAddrSpaceOrdering;
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // The LDS keeps all memory operations in order for
      // the same wavefront.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  if ((AddrSpace & SIAtomicAddrSpace::GDS) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      // If no cross address space ordering then an GDS "S_WAITCNT lgkmcnt(0)"
      // is not needed as GDS operations for all waves are executed in a total
      // global ordering as observed by all waves. Required if also
      // synchronizing with global/LDS memory as GDS operations could be
      // reordered with respect to later global/LDS memory operations of the
      // same wave.
      LGKMCnt |= IsCrossAddrSpaceOrdering;
      break;
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // The GDS keeps all memory operations in order for
      // the same work-group.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  if (VMCnt || LGKMCnt) {
    unsigned WaitCntImmediate =
      AMDGPU::encodeWaitcnt(IV,
                            VMCnt ? 0 : getVmcntBitMask(IV),
                            getExpcntBitMask(IV),
                            LGKMCnt ? 0 : getLgkmcntBitMask(IV));
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_WAITCNT_soft))
        .addImm(WaitCntImmediate);
    Changed = true;
  }

  if (Pos == Position::AFTER)
    --MI;

  return Changed;
}

bool SIGfx6CacheControl::insertAcquire(MachineBasicBlock::iterator &MI,
                                       SIAtomicScope Scope,
                                       SIAtomicAddrSpace AddrSpace,
                                       Position Pos) const {
  if (!InsertCacheInv)
    return false;

  bool Changed = false;

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  if (Pos == Position::AFTER)
    ++MI;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_WBINVL1));
      Changed = true;
      break;
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // No cache to invalidate.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory cache
  /// to be flushed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  if (Pos == Position::AFTER)
    --MI;

  return Changed;
}

bool SIGfx6CacheControl::insertRelease(MachineBasicBlock::iterator &MI,
                                       SIAtomicScope Scope,
                                       SIAtomicAddrSpace AddrSpace,
                                       bool IsCrossAddrSpaceOrdering,
                                       Position Pos) const {
  return insertWait(MI, Scope, AddrSpace, SIMemOp::LOAD | SIMemOp::STORE,
                    IsCrossAddrSpaceOrdering, Pos);
}

bool SIGfx7CacheControl::insertAcquire(MachineBasicBlock::iterator &MI,
                                       SIAtomicScope Scope,
                                       SIAtomicAddrSpace AddrSpace,
                                       Position Pos) const {
  if (!InsertCacheInv)
    return false;

  bool Changed = false;

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  const GCNSubtarget &STM = MBB.getParent()->getSubtarget<GCNSubtarget>();

  const unsigned InvalidateL1 = STM.isAmdPalOS() || STM.isMesa3DOS()
                                    ? AMDGPU::BUFFER_WBINVL1
                                    : AMDGPU::BUFFER_WBINVL1_VOL;

  if (Pos == Position::AFTER)
    ++MI;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      BuildMI(MBB, MI, DL, TII->get(InvalidateL1));
      Changed = true;
      break;
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // No cache to invalidate.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory cache
  /// to be flushed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  if (Pos == Position::AFTER)
    --MI;

  return Changed;
}

bool SIGfx90ACacheControl::enableLoadCacheBypass(
    const MachineBasicBlock::iterator &MI,
    SIAtomicScope Scope,
    SIAtomicAddrSpace AddrSpace) const {
  assert(MI->mayLoad() && !MI->mayStore());
  bool Changed = false;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      // Set the L1 cache policy to MISS_LRU.
      // Note: there is no L2 cache bypass policy at the ISA level.
      Changed |= enableGLCBit(MI);
      break;
    case SIAtomicScope::WORKGROUP:
      // In threadgroup split mode the waves of a work-group can be executing on
      // different CUs. Therefore need to bypass the L1 which is per CU.
      // Otherwise in non-threadgroup split mode all waves of a work-group are
      // on the same CU, and so the L1 does not need to be bypassed.
      if (ST.isTgSplitEnabled())
        Changed |= enableGLCBit(MI);
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // No cache to bypass.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory caches
  /// to be bypassed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  return Changed;
}

bool SIGfx90ACacheControl::enableStoreCacheBypass(
    const MachineBasicBlock::iterator &MI,
    SIAtomicScope Scope,
    SIAtomicAddrSpace AddrSpace) const {
  assert(!MI->mayLoad() && MI->mayStore());
  bool Changed = false;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      /// Do not set glc for store atomic operations as they implicitly write
      /// through the L1 cache.
      break;
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // No cache to bypass. Store atomics implicitly write through the L1
      // cache.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory caches
  /// to be bypassed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  return Changed;
}

bool SIGfx90ACacheControl::enableRMWCacheBypass(
    const MachineBasicBlock::iterator &MI,
    SIAtomicScope Scope,
    SIAtomicAddrSpace AddrSpace) const {
  assert(MI->mayLoad() && MI->mayStore());
  bool Changed = false;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      /// Do not set glc for RMW atomic operations as they implicitly bypass
      /// the L1 cache, and the glc bit is instead used to indicate if they are
      /// return or no-return.
      break;
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // No cache to bypass. RMW atomics implicitly bypass the L1 cache.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  return Changed;
}

bool SIGfx90ACacheControl::enableVolatileAndOrNonTemporal(
    MachineBasicBlock::iterator &MI, SIAtomicAddrSpace AddrSpace, SIMemOp Op,
    bool IsVolatile, bool IsNonTemporal, bool IsLastUse = false) const {
  // Only handle load and store, not atomic read-modify-write insructions. The
  // latter use glc to indicate if the atomic returns a result and so must not
  // be used for cache control.
  assert(MI->mayLoad() ^ MI->mayStore());

  // Only update load and store, not LLVM IR atomic read-modify-write
  // instructions. The latter are always marked as volatile so cannot sensibly
  // handle it as do not want to pessimize all atomics. Also they do not support
  // the nontemporal attribute.
  assert(Op == SIMemOp::LOAD || Op == SIMemOp::STORE);

  bool Changed = false;

  if (IsVolatile) {
    // Set L1 cache policy to be MISS_EVICT for load instructions
    // and MISS_LRU for store instructions.
    // Note: there is no L2 cache bypass policy at the ISA level.
    if (Op == SIMemOp::LOAD)
      Changed |= enableGLCBit(MI);

    // Ensure operation has completed at system scope to cause all volatile
    // operations to be visible outside the program in a global order. Do not
    // request cross address space as only the global address space can be
    // observable outside the program, so no need to cause a waitcnt for LDS
    // address space operations.
    Changed |= insertWait(MI, SIAtomicScope::SYSTEM, AddrSpace, Op, false,
                          Position::AFTER);

    return Changed;
  }

  if (IsNonTemporal) {
    // Setting both GLC and SLC configures L1 cache policy to MISS_EVICT
    // for both loads and stores, and the L2 cache policy to STREAM.
    Changed |= enableGLCBit(MI);
    Changed |= enableSLCBit(MI);
    return Changed;
  }

  return Changed;
}

bool SIGfx90ACacheControl::insertWait(MachineBasicBlock::iterator &MI,
                                      SIAtomicScope Scope,
                                      SIAtomicAddrSpace AddrSpace,
                                      SIMemOp Op,
                                      bool IsCrossAddrSpaceOrdering,
                                      Position Pos) const {
  if (ST.isTgSplitEnabled()) {
    // In threadgroup split mode the waves of a work-group can be executing on
    // different CUs. Therefore need to wait for global or GDS memory operations
    // to complete to ensure they are visible to waves in the other CUs.
    // Otherwise in non-threadgroup split mode all waves of a work-group are on
    // the same CU, so no need to wait for global memory as all waves in the
    // work-group access the same the L1, nor wait for GDS as access are ordered
    // on a CU.
    if (((AddrSpace & (SIAtomicAddrSpace::GLOBAL | SIAtomicAddrSpace::SCRATCH |
                       SIAtomicAddrSpace::GDS)) != SIAtomicAddrSpace::NONE) &&
        (Scope == SIAtomicScope::WORKGROUP)) {
      // Same as GFX7 using agent scope.
      Scope = SIAtomicScope::AGENT;
    }
    // In threadgroup split mode LDS cannot be allocated so no need to wait for
    // LDS memory operations.
    AddrSpace &= ~SIAtomicAddrSpace::LDS;
  }
  return SIGfx7CacheControl::insertWait(MI, Scope, AddrSpace, Op,
                                        IsCrossAddrSpaceOrdering, Pos);
}

bool SIGfx90ACacheControl::insertAcquire(MachineBasicBlock::iterator &MI,
                                         SIAtomicScope Scope,
                                         SIAtomicAddrSpace AddrSpace,
                                         Position Pos) const {
  if (!InsertCacheInv)
    return false;

  bool Changed = false;

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  if (Pos == Position::AFTER)
    ++MI;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
      // Ensures that following loads will not see stale remote VMEM data or
      // stale local VMEM data with MTYPE NC. Local VMEM data with MTYPE RW and
      // CC will never be stale due to the local memory probes.
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_INVL2));
      // Inserting a "S_WAITCNT vmcnt(0)" after is not required because the
      // hardware does not reorder memory operations by the same wave with
      // respect to a preceding "BUFFER_INVL2". The invalidate is guaranteed to
      // remove any cache lines of earlier writes by the same wave and ensures
      // later reads by the same wave will refetch the cache lines.
      Changed = true;
      break;
    case SIAtomicScope::AGENT:
      // Same as GFX7.
      break;
    case SIAtomicScope::WORKGROUP:
      // In threadgroup split mode the waves of a work-group can be executing on
      // different CUs. Therefore need to invalidate the L1 which is per CU.
      // Otherwise in non-threadgroup split mode all waves of a work-group are
      // on the same CU, and so the L1 does not need to be invalidated.
      if (ST.isTgSplitEnabled()) {
        // Same as GFX7 using agent scope.
        Scope = SIAtomicScope::AGENT;
      }
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // Same as GFX7.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory cache
  /// to be flushed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  if (Pos == Position::AFTER)
    --MI;

  Changed |= SIGfx7CacheControl::insertAcquire(MI, Scope, AddrSpace, Pos);

  return Changed;
}

bool SIGfx90ACacheControl::insertRelease(MachineBasicBlock::iterator &MI,
                                         SIAtomicScope Scope,
                                         SIAtomicAddrSpace AddrSpace,
                                         bool IsCrossAddrSpaceOrdering,
                                         Position Pos) const {
  bool Changed = false;

  MachineBasicBlock &MBB = *MI->getParent();
  const DebugLoc &DL = MI->getDebugLoc();

  if (Pos == Position::AFTER)
    ++MI;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
      // Inserting a "S_WAITCNT vmcnt(0)" before is not required because the
      // hardware does not reorder memory operations by the same wave with
      // respect to a following "BUFFER_WBL2". The "BUFFER_WBL2" is guaranteed
      // to initiate writeback of any dirty cache lines of earlier writes by the
      // same wave. A "S_WAITCNT vmcnt(0)" is needed after to ensure the
      // writeback has completed.
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_WBL2))
        // Set SC bits to indicate system scope.
        .addImm(AMDGPU::CPol::SC0 | AMDGPU::CPol::SC1);
      // Followed by same as GFX7, which will ensure the necessary "S_WAITCNT
      // vmcnt(0)" needed by the "BUFFER_WBL2".
      Changed = true;
      break;
    case SIAtomicScope::AGENT:
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // Same as GFX7.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  if (Pos == Position::AFTER)
    --MI;

  Changed |=
      SIGfx7CacheControl::insertRelease(MI, Scope, AddrSpace,
                                        IsCrossAddrSpaceOrdering, Pos);

  return Changed;
}

bool SIGfx940CacheControl::enableLoadCacheBypass(
    const MachineBasicBlock::iterator &MI, SIAtomicScope Scope,
    SIAtomicAddrSpace AddrSpace) const {
  assert(MI->mayLoad() && !MI->mayStore());
  bool Changed = false;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
      // Set SC bits to indicate system scope.
      Changed |= enableSC0Bit(MI);
      Changed |= enableSC1Bit(MI);
      break;
    case SIAtomicScope::AGENT:
      // Set SC bits to indicate agent scope.
      Changed |= enableSC1Bit(MI);
      break;
    case SIAtomicScope::WORKGROUP:
      // In threadgroup split mode the waves of a work-group can be executing on
      // different CUs. Therefore need to bypass the L1 which is per CU.
      // Otherwise in non-threadgroup split mode all waves of a work-group are
      // on the same CU, and so the L1 does not need to be bypassed. Setting SC
      // bits to indicate work-group scope will do this automatically.
      Changed |= enableSC0Bit(MI);
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // Leave SC bits unset to indicate wavefront scope.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory caches
  /// to be bypassed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  return Changed;
}

bool SIGfx940CacheControl::enableStoreCacheBypass(
    const MachineBasicBlock::iterator &MI,
    SIAtomicScope Scope, SIAtomicAddrSpace AddrSpace) const {
  assert(!MI->mayLoad() && MI->mayStore());
  bool Changed = false;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
      // Set SC bits to indicate system scope.
      Changed |= enableSC0Bit(MI);
      Changed |= enableSC1Bit(MI);
      break;
    case SIAtomicScope::AGENT:
      // Set SC bits to indicate agent scope.
      Changed |= enableSC1Bit(MI);
      break;
    case SIAtomicScope::WORKGROUP:
      // Set SC bits to indicate workgroup scope.
      Changed |= enableSC0Bit(MI);
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // Leave SC bits unset to indicate wavefront scope.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory caches
  /// to be bypassed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  return Changed;
}

bool SIGfx940CacheControl::enableRMWCacheBypass(
    const MachineBasicBlock::iterator &MI, SIAtomicScope Scope,
    SIAtomicAddrSpace AddrSpace) const {
  assert(MI->mayLoad() && MI->mayStore());
  bool Changed = false;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
      // Set SC1 bit to indicate system scope.
      Changed |= enableSC1Bit(MI);
      break;
    case SIAtomicScope::AGENT:
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // RMW atomic operations implicitly bypass the L1 cache and only use SC1
      // to indicate system or agent scope. The SC0 bit is used to indicate if
      // they are return or no-return. Leave SC1 bit unset to indicate agent
      // scope.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  return Changed;
}

bool SIGfx940CacheControl::enableVolatileAndOrNonTemporal(
    MachineBasicBlock::iterator &MI, SIAtomicAddrSpace AddrSpace, SIMemOp Op,
    bool IsVolatile, bool IsNonTemporal, bool IsLastUse = false) const {
  // Only handle load and store, not atomic read-modify-write insructions. The
  // latter use glc to indicate if the atomic returns a result and so must not
  // be used for cache control.
  assert(MI->mayLoad() ^ MI->mayStore());

  // Only update load and store, not LLVM IR atomic read-modify-write
  // instructions. The latter are always marked as volatile so cannot sensibly
  // handle it as do not want to pessimize all atomics. Also they do not support
  // the nontemporal attribute.
  assert(Op == SIMemOp::LOAD || Op == SIMemOp::STORE);

  bool Changed = false;

  if (IsVolatile) {
    // Set SC bits to indicate system scope.
    Changed |= enableSC0Bit(MI);
    Changed |= enableSC1Bit(MI);

    // Ensure operation has completed at system scope to cause all volatile
    // operations to be visible outside the program in a global order. Do not
    // request cross address space as only the global address space can be
    // observable outside the program, so no need to cause a waitcnt for LDS
    // address space operations.
    Changed |= insertWait(MI, SIAtomicScope::SYSTEM, AddrSpace, Op, false,
                          Position::AFTER);

    return Changed;
  }

  if (IsNonTemporal) {
    Changed |= enableNTBit(MI);
    return Changed;
  }

  return Changed;
}

bool SIGfx940CacheControl::insertAcquire(MachineBasicBlock::iterator &MI,
                                         SIAtomicScope Scope,
                                         SIAtomicAddrSpace AddrSpace,
                                         Position Pos) const {
  if (!InsertCacheInv)
    return false;

  bool Changed = false;

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  if (Pos == Position::AFTER)
    ++MI;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
      // Ensures that following loads will not see stale remote VMEM data or
      // stale local VMEM data with MTYPE NC. Local VMEM data with MTYPE RW and
      // CC will never be stale due to the local memory probes.
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_INV))
          // Set SC bits to indicate system scope.
          .addImm(AMDGPU::CPol::SC0 | AMDGPU::CPol::SC1);
      // Inserting a "S_WAITCNT vmcnt(0)" after is not required because the
      // hardware does not reorder memory operations by the same wave with
      // respect to a preceding "BUFFER_INV". The invalidate is guaranteed to
      // remove any cache lines of earlier writes by the same wave and ensures
      // later reads by the same wave will refetch the cache lines.
      Changed = true;
      break;
    case SIAtomicScope::AGENT:
      // Ensures that following loads will not see stale remote date or local
      // MTYPE NC global data. Local MTYPE RW and CC memory will never be stale
      // due to the memory probes.
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_INV))
          // Set SC bits to indicate agent scope.
          .addImm(AMDGPU::CPol::SC1);
      // Inserting "S_WAITCNT vmcnt(0)" is not required because the hardware
      // does not reorder memory operations with respect to preceeding buffer
      // invalidate. The invalidate is guaranteed to remove any cache lines of
      // earlier writes and ensures later writes will refetch the cache lines.
      Changed = true;
      break;
    case SIAtomicScope::WORKGROUP:
      // In threadgroup split mode the waves of a work-group can be executing on
      // different CUs. Therefore need to invalidate the L1 which is per CU.
      // Otherwise in non-threadgroup split mode all waves of a work-group are
      // on the same CU, and so the L1 does not need to be invalidated.
      if (ST.isTgSplitEnabled()) {
        // Ensures L1 is invalidated if in threadgroup split mode. In
        // non-threadgroup split mode it is a NOP, but no point generating it in
        // that case if know not in that mode.
        BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_INV))
            // Set SC bits to indicate work-group scope.
            .addImm(AMDGPU::CPol::SC0);
        // Inserting "S_WAITCNT vmcnt(0)" is not required because the hardware
        // does not reorder memory operations with respect to preceeding buffer
        // invalidate. The invalidate is guaranteed to remove any cache lines of
        // earlier writes and ensures later writes will refetch the cache lines.
        Changed = true;
      }
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // Could generate "BUFFER_INV" but it would do nothing as there are no
      // caches to invalidate.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory cache
  /// to be flushed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  if (Pos == Position::AFTER)
    --MI;

  return Changed;
}

bool SIGfx940CacheControl::insertRelease(MachineBasicBlock::iterator &MI,
                                         SIAtomicScope Scope,
                                         SIAtomicAddrSpace AddrSpace,
                                         bool IsCrossAddrSpaceOrdering,
                                         Position Pos) const {
  bool Changed = false;

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  if (Pos == Position::AFTER)
    ++MI;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
      // Inserting a "S_WAITCNT vmcnt(0)" before is not required because the
      // hardware does not reorder memory operations by the same wave with
      // respect to a following "BUFFER_WBL2". The "BUFFER_WBL2" is guaranteed
      // to initiate writeback of any dirty cache lines of earlier writes by the
      // same wave. A "S_WAITCNT vmcnt(0)" is needed after to ensure the
      // writeback has completed.
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_WBL2))
          // Set SC bits to indicate system scope.
          .addImm(AMDGPU::CPol::SC0 | AMDGPU::CPol::SC1);
      // Since AddrSpace contains SIAtomicAddrSpace::GLOBAL and Scope is
      // SIAtomicScope::SYSTEM, the following insertWait will generate the
      // required "S_WAITCNT vmcnt(0)" needed by the "BUFFER_WBL2".
      Changed = true;
      break;
    case SIAtomicScope::AGENT:
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_WBL2))
          // Set SC bits to indicate agent scope.
          .addImm(AMDGPU::CPol::SC1);

      // Since AddrSpace contains SIAtomicAddrSpace::GLOBAL and Scope is
      // SIAtomicScope::AGENT, the following insertWait will generate the
      // required "S_WAITCNT vmcnt(0)".
      Changed = true;
      break;
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // Do not generate "BUFFER_WBL2" as there are no caches it would
      // writeback, and would require an otherwise unnecessary
      // "S_WAITCNT vmcnt(0)".
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  if (Pos == Position::AFTER)
    --MI;

  // Ensure the necessary S_WAITCNT needed by any "BUFFER_WBL2" as well as other
  // S_WAITCNT needed.
  Changed |= insertWait(MI, Scope, AddrSpace, SIMemOp::LOAD | SIMemOp::STORE,
                        IsCrossAddrSpaceOrdering, Pos);

  return Changed;
}

bool SIGfx10CacheControl::enableLoadCacheBypass(
    const MachineBasicBlock::iterator &MI,
    SIAtomicScope Scope,
    SIAtomicAddrSpace AddrSpace) const {
  assert(MI->mayLoad() && !MI->mayStore());
  bool Changed = false;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      // Set the L0 and L1 cache policies to MISS_EVICT.
      // Note: there is no L2 cache coherent bypass control at the ISA level.
      Changed |= enableGLCBit(MI);
      Changed |= enableDLCBit(MI);
      break;
    case SIAtomicScope::WORKGROUP:
      // In WGP mode the waves of a work-group can be executing on either CU of
      // the WGP. Therefore need to bypass the L0 which is per CU. Otherwise in
      // CU mode all waves of a work-group are on the same CU, and so the L0
      // does not need to be bypassed.
      if (!ST.isCuModeEnabled())
        Changed |= enableGLCBit(MI);
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // No cache to bypass.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory caches
  /// to be bypassed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  return Changed;
}

bool SIGfx10CacheControl::enableVolatileAndOrNonTemporal(
    MachineBasicBlock::iterator &MI, SIAtomicAddrSpace AddrSpace, SIMemOp Op,
    bool IsVolatile, bool IsNonTemporal, bool IsLastUse = false) const {

  // Only handle load and store, not atomic read-modify-write insructions. The
  // latter use glc to indicate if the atomic returns a result and so must not
  // be used for cache control.
  assert(MI->mayLoad() ^ MI->mayStore());

  // Only update load and store, not LLVM IR atomic read-modify-write
  // instructions. The latter are always marked as volatile so cannot sensibly
  // handle it as do not want to pessimize all atomics. Also they do not support
  // the nontemporal attribute.
  assert(Op == SIMemOp::LOAD || Op == SIMemOp::STORE);

  bool Changed = false;

  if (IsVolatile) {
    // Set L0 and L1 cache policy to be MISS_EVICT for load instructions
    // and MISS_LRU for store instructions.
    // Note: there is no L2 cache coherent bypass control at the ISA level.
    if (Op == SIMemOp::LOAD) {
      Changed |= enableGLCBit(MI);
      Changed |= enableDLCBit(MI);
    }

    // Ensure operation has completed at system scope to cause all volatile
    // operations to be visible outside the program in a global order. Do not
    // request cross address space as only the global address space can be
    // observable outside the program, so no need to cause a waitcnt for LDS
    // address space operations.
    Changed |= insertWait(MI, SIAtomicScope::SYSTEM, AddrSpace, Op, false,
                          Position::AFTER);
    return Changed;
  }

  if (IsNonTemporal) {
    // For loads setting SLC configures L0 and L1 cache policy to HIT_EVICT
    // and L2 cache policy to STREAM.
    // For stores setting both GLC and SLC configures L0 and L1 cache policy
    // to MISS_EVICT and the L2 cache policy to STREAM.
    if (Op == SIMemOp::STORE)
      Changed |= enableGLCBit(MI);
    Changed |= enableSLCBit(MI);

    return Changed;
  }

  return Changed;
}

bool SIGfx10CacheControl::insertWait(MachineBasicBlock::iterator &MI,
                                     SIAtomicScope Scope,
                                     SIAtomicAddrSpace AddrSpace,
                                     SIMemOp Op,
                                     bool IsCrossAddrSpaceOrdering,
                                     Position Pos) const {
  bool Changed = false;

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  if (Pos == Position::AFTER)
    ++MI;

  bool VMCnt = false;
  bool VSCnt = false;
  bool LGKMCnt = false;

  if ((AddrSpace & (SIAtomicAddrSpace::GLOBAL | SIAtomicAddrSpace::SCRATCH)) !=
      SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      if ((Op & SIMemOp::LOAD) != SIMemOp::NONE)
        VMCnt |= true;
      if ((Op & SIMemOp::STORE) != SIMemOp::NONE)
        VSCnt |= true;
      break;
    case SIAtomicScope::WORKGROUP:
      // In WGP mode the waves of a work-group can be executing on either CU of
      // the WGP. Therefore need to wait for operations to complete to ensure
      // they are visible to waves in the other CU as the L0 is per CU.
      // Otherwise in CU mode and all waves of a work-group are on the same CU
      // which shares the same L0.
      if (!ST.isCuModeEnabled()) {
        if ((Op & SIMemOp::LOAD) != SIMemOp::NONE)
          VMCnt |= true;
        if ((Op & SIMemOp::STORE) != SIMemOp::NONE)
          VSCnt |= true;
      }
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // The L0 cache keeps all memory operations in order for
      // work-items in the same wavefront.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  if ((AddrSpace & SIAtomicAddrSpace::LDS) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
    case SIAtomicScope::WORKGROUP:
      // If no cross address space ordering then an "S_WAITCNT lgkmcnt(0)" is
      // not needed as LDS operations for all waves are executed in a total
      // global ordering as observed by all waves. Required if also
      // synchronizing with global/GDS memory as LDS operations could be
      // reordered with respect to later global/GDS memory operations of the
      // same wave.
      LGKMCnt |= IsCrossAddrSpaceOrdering;
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // The LDS keeps all memory operations in order for
      // the same wavefront.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  if ((AddrSpace & SIAtomicAddrSpace::GDS) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      // If no cross address space ordering then an GDS "S_WAITCNT lgkmcnt(0)"
      // is not needed as GDS operations for all waves are executed in a total
      // global ordering as observed by all waves. Required if also
      // synchronizing with global/LDS memory as GDS operations could be
      // reordered with respect to later global/LDS memory operations of the
      // same wave.
      LGKMCnt |= IsCrossAddrSpaceOrdering;
      break;
    case SIAtomicScope::WORKGROUP:
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // The GDS keeps all memory operations in order for
      // the same work-group.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  if (VMCnt || LGKMCnt) {
    unsigned WaitCntImmediate =
      AMDGPU::encodeWaitcnt(IV,
                            VMCnt ? 0 : getVmcntBitMask(IV),
                            getExpcntBitMask(IV),
                            LGKMCnt ? 0 : getLgkmcntBitMask(IV));
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_WAITCNT_soft))
        .addImm(WaitCntImmediate);
    Changed = true;
  }

  if (VSCnt) {
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_WAITCNT_VSCNT_soft))
        .addReg(AMDGPU::SGPR_NULL, RegState::Undef)
        .addImm(0);
    Changed = true;
  }

  if (Pos == Position::AFTER)
    --MI;

  return Changed;
}

bool SIGfx10CacheControl::insertAcquire(MachineBasicBlock::iterator &MI,
                                        SIAtomicScope Scope,
                                        SIAtomicAddrSpace AddrSpace,
                                        Position Pos) const {
  if (!InsertCacheInv)
    return false;

  bool Changed = false;

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  if (Pos == Position::AFTER)
    ++MI;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      // The order of invalidates matter here. We must invalidate "outer in"
      // so L1 -> L0 to avoid L0 pulling in stale data from L1 when it is
      // invalidated.
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_GL1_INV));
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_GL0_INV));
      Changed = true;
      break;
    case SIAtomicScope::WORKGROUP:
      // In WGP mode the waves of a work-group can be executing on either CU of
      // the WGP. Therefore need to invalidate the L0 which is per CU. Otherwise
      // in CU mode and all waves of a work-group are on the same CU, and so the
      // L0 does not need to be invalidated.
      if (!ST.isCuModeEnabled()) {
        BuildMI(MBB, MI, DL, TII->get(AMDGPU::BUFFER_GL0_INV));
        Changed = true;
      }
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // No cache to invalidate.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory cache
  /// to be flushed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  if (Pos == Position::AFTER)
    --MI;

  return Changed;
}

bool SIGfx11CacheControl::enableLoadCacheBypass(
    const MachineBasicBlock::iterator &MI, SIAtomicScope Scope,
    SIAtomicAddrSpace AddrSpace) const {
  assert(MI->mayLoad() && !MI->mayStore());
  bool Changed = false;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      // Set the L0 and L1 cache policies to MISS_EVICT.
      // Note: there is no L2 cache coherent bypass control at the ISA level.
      Changed |= enableGLCBit(MI);
      break;
    case SIAtomicScope::WORKGROUP:
      // In WGP mode the waves of a work-group can be executing on either CU of
      // the WGP. Therefore need to bypass the L0 which is per CU. Otherwise in
      // CU mode all waves of a work-group are on the same CU, and so the L0
      // does not need to be bypassed.
      if (!ST.isCuModeEnabled())
        Changed |= enableGLCBit(MI);
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // No cache to bypass.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  /// The scratch address space does not need the global memory caches
  /// to be bypassed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.

  return Changed;
}

bool SIGfx11CacheControl::enableVolatileAndOrNonTemporal(
    MachineBasicBlock::iterator &MI, SIAtomicAddrSpace AddrSpace, SIMemOp Op,
    bool IsVolatile, bool IsNonTemporal, bool IsLastUse = false) const {

  // Only handle load and store, not atomic read-modify-write insructions. The
  // latter use glc to indicate if the atomic returns a result and so must not
  // be used for cache control.
  assert(MI->mayLoad() ^ MI->mayStore());

  // Only update load and store, not LLVM IR atomic read-modify-write
  // instructions. The latter are always marked as volatile so cannot sensibly
  // handle it as do not want to pessimize all atomics. Also they do not support
  // the nontemporal attribute.
  assert(Op == SIMemOp::LOAD || Op == SIMemOp::STORE);

  bool Changed = false;

  if (IsVolatile) {
    // Set L0 and L1 cache policy to be MISS_EVICT for load instructions
    // and MISS_LRU for store instructions.
    // Note: there is no L2 cache coherent bypass control at the ISA level.
    if (Op == SIMemOp::LOAD)
      Changed |= enableGLCBit(MI);

    // Set MALL NOALLOC for load and store instructions.
    Changed |= enableDLCBit(MI);

    // Ensure operation has completed at system scope to cause all volatile
    // operations to be visible outside the program in a global order. Do not
    // request cross address space as only the global address space can be
    // observable outside the program, so no need to cause a waitcnt for LDS
    // address space operations.
    Changed |= insertWait(MI, SIAtomicScope::SYSTEM, AddrSpace, Op, false,
                          Position::AFTER);
    return Changed;
  }

  if (IsNonTemporal) {
    // For loads setting SLC configures L0 and L1 cache policy to HIT_EVICT
    // and L2 cache policy to STREAM.
    // For stores setting both GLC and SLC configures L0 and L1 cache policy
    // to MISS_EVICT and the L2 cache policy to STREAM.
    if (Op == SIMemOp::STORE)
      Changed |= enableGLCBit(MI);
    Changed |= enableSLCBit(MI);

    // Set MALL NOALLOC for load and store instructions.
    Changed |= enableDLCBit(MI);
    return Changed;
  }

  return Changed;
}

bool SIGfx12CacheControl::setTH(const MachineBasicBlock::iterator MI,
                                AMDGPU::CPol::CPol Value) const {
  MachineOperand *CPol = TII->getNamedOperand(*MI, OpName::cpol);
  if (!CPol)
    return false;

  uint64_t NewTH = Value & AMDGPU::CPol::TH;
  if ((CPol->getImm() & AMDGPU::CPol::TH) != NewTH) {
    CPol->setImm((CPol->getImm() & ~AMDGPU::CPol::TH) | NewTH);
    return true;
  }

  return false;
}

bool SIGfx12CacheControl::setScope(const MachineBasicBlock::iterator MI,
                                   AMDGPU::CPol::CPol Value) const {
  MachineOperand *CPol = TII->getNamedOperand(*MI, OpName::cpol);
  if (!CPol)
    return false;

  uint64_t NewScope = Value & AMDGPU::CPol::SCOPE;
  if ((CPol->getImm() & AMDGPU::CPol::SCOPE) != NewScope) {
    CPol->setImm((CPol->getImm() & ~AMDGPU::CPol::SCOPE) | NewScope);
    return true;
  }

  return false;
}

bool SIGfx12CacheControl::insertWaitsBeforeSystemScopeStore(
    const MachineBasicBlock::iterator MI) const {
  // TODO: implement flag for frontend to give us a hint not to insert waits.

  MachineBasicBlock &MBB = *MI->getParent();
  const DebugLoc &DL = MI->getDebugLoc();

  BuildMI(MBB, MI, DL, TII->get(S_WAIT_LOADCNT_soft)).addImm(0);
  BuildMI(MBB, MI, DL, TII->get(S_WAIT_SAMPLECNT_soft)).addImm(0);
  BuildMI(MBB, MI, DL, TII->get(S_WAIT_BVHCNT_soft)).addImm(0);
  BuildMI(MBB, MI, DL, TII->get(S_WAIT_KMCNT_soft)).addImm(0);
  BuildMI(MBB, MI, DL, TII->get(S_WAIT_STORECNT_soft)).addImm(0);

  return true;
}

bool SIGfx12CacheControl::insertWait(MachineBasicBlock::iterator &MI,
                                     SIAtomicScope Scope,
                                     SIAtomicAddrSpace AddrSpace, SIMemOp Op,
                                     bool IsCrossAddrSpaceOrdering,
                                     Position Pos) const {
  bool Changed = false;

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  bool LOADCnt = false;
  bool DSCnt = false;
  bool STORECnt = false;

  if (Pos == Position::AFTER)
    ++MI;

  if ((AddrSpace & (SIAtomicAddrSpace::GLOBAL | SIAtomicAddrSpace::SCRATCH)) !=
      SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
      if ((Op & SIMemOp::LOAD) != SIMemOp::NONE)
        LOADCnt |= true;
      if ((Op & SIMemOp::STORE) != SIMemOp::NONE)
        STORECnt |= true;
      break;
    case SIAtomicScope::WORKGROUP:
      // In WGP mode the waves of a work-group can be executing on either CU of
      // the WGP. Therefore need to wait for operations to complete to ensure
      // they are visible to waves in the other CU as the L0 is per CU.
      // Otherwise in CU mode and all waves of a work-group are on the same CU
      // which shares the same L0.
      if (!ST.isCuModeEnabled()) {
        if ((Op & SIMemOp::LOAD) != SIMemOp::NONE)
          LOADCnt |= true;
        if ((Op & SIMemOp::STORE) != SIMemOp::NONE)
          STORECnt |= true;
      }
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // The L0 cache keeps all memory operations in order for
      // work-items in the same wavefront.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  if ((AddrSpace & SIAtomicAddrSpace::LDS) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
    case SIAtomicScope::AGENT:
    case SIAtomicScope::WORKGROUP:
      // If no cross address space ordering then an "S_WAITCNT lgkmcnt(0)" is
      // not needed as LDS operations for all waves are executed in a total
      // global ordering as observed by all waves. Required if also
      // synchronizing with global/GDS memory as LDS operations could be
      // reordered with respect to later global/GDS memory operations of the
      // same wave.
      DSCnt |= IsCrossAddrSpaceOrdering;
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // The LDS keeps all memory operations in order for
      // the same wavefront.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  if (LOADCnt) {
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_WAIT_BVHCNT_soft)).addImm(0);
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_WAIT_SAMPLECNT_soft)).addImm(0);
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_WAIT_LOADCNT_soft)).addImm(0);
    Changed = true;
  }

  if (STORECnt) {
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_WAIT_STORECNT_soft)).addImm(0);
    Changed = true;
  }

  if (DSCnt) {
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_WAIT_DSCNT_soft)).addImm(0);
    Changed = true;
  }

  if (Pos == Position::AFTER)
    --MI;

  return Changed;
}

bool SIGfx12CacheControl::insertAcquire(MachineBasicBlock::iterator &MI,
                                        SIAtomicScope Scope,
                                        SIAtomicAddrSpace AddrSpace,
                                        Position Pos) const {
  if (!InsertCacheInv)
    return false;

  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  /// The scratch address space does not need the global memory cache
  /// to be flushed as all memory operations by the same thread are
  /// sequentially consistent, and no other thread can access scratch
  /// memory.

  /// Other address spaces do not have a cache.
  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) == SIAtomicAddrSpace::NONE)
    return false;

  AMDGPU::CPol::CPol ScopeImm = AMDGPU::CPol::SCOPE_DEV;
  switch (Scope) {
  case SIAtomicScope::SYSTEM:
    ScopeImm = AMDGPU::CPol::SCOPE_SYS;
    break;
  case SIAtomicScope::AGENT:
    ScopeImm = AMDGPU::CPol::SCOPE_DEV;
    break;
  case SIAtomicScope::WORKGROUP:
    // In WGP mode the waves of a work-group can be executing on either CU of
    // the WGP. Therefore we need to invalidate the L0 which is per CU.
    // Otherwise in CU mode all waves of a work-group are on the same CU, and so
    // the L0 does not need to be invalidated.
    if (ST.isCuModeEnabled())
      return false;

    ScopeImm = AMDGPU::CPol::SCOPE_SE;
    break;
  case SIAtomicScope::WAVEFRONT:
  case SIAtomicScope::SINGLETHREAD:
    // No cache to invalidate.
    return false;
  default:
    llvm_unreachable("Unsupported synchronization scope");
  }

  if (Pos == Position::AFTER)
    ++MI;

  BuildMI(MBB, MI, DL, TII->get(AMDGPU::GLOBAL_INV)).addImm(ScopeImm);

  if (Pos == Position::AFTER)
    --MI;

  return true;
}

bool SIGfx12CacheControl::insertRelease(MachineBasicBlock::iterator &MI,
                                        SIAtomicScope Scope,
                                        SIAtomicAddrSpace AddrSpace,
                                        bool IsCrossAddrSpaceOrdering,
                                        Position Pos) const {
  MachineBasicBlock &MBB = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  // The scratch address space does not need the global memory cache
  // writeback as all memory operations by the same thread are
  // sequentially consistent, and no other thread can access scratch
  // memory.

  // Other address spaces do not have a cache.
  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) == SIAtomicAddrSpace::NONE)
    return false;

  if (Pos == Position::AFTER)
    ++MI;

  // GLOBAL_WB is always needed, even for write-through caches, as it
  // additionally ensures all operations have reached the desired cache level.
  bool SkipWB = false;
  AMDGPU::CPol::CPol ScopeImm = AMDGPU::CPol::SCOPE_DEV;
  switch (Scope) {
  case SIAtomicScope::SYSTEM:
    ScopeImm = AMDGPU::CPol::SCOPE_SYS;
    break;
  case SIAtomicScope::AGENT:
    ScopeImm = AMDGPU::CPol::SCOPE_DEV;
    break;
  case SIAtomicScope::WORKGROUP:
    // In WGP mode the waves of a work-group can be executing on either CU of
    // the WGP. Therefore we need to ensure all operations have reached L1,
    // hence the SCOPE_SE WB.
    // For CU mode, we need operations to reach L0, so the wait is enough -
    // there are no ways for an operation to report completion without reaching
    // at least L0.
    if (ST.isCuModeEnabled())
      SkipWB = true;
    else
      ScopeImm = AMDGPU::CPol::SCOPE_SE;
    break;
  case SIAtomicScope::WAVEFRONT:
  case SIAtomicScope::SINGLETHREAD:
    // No cache to invalidate.
    return false;
  default:
    llvm_unreachable("Unsupported synchronization scope");
  }

  if (!SkipWB)
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::GLOBAL_WB)).addImm(ScopeImm);

  if (Pos == Position::AFTER)
    --MI;

  // We always have to wait for previous memory operations (load/store) to
  // complete, whether we inserted a WB or not. If we inserted a WB (storecnt),
  // we of course need to wait for that as well.
  insertWait(MI, Scope, AddrSpace, SIMemOp::LOAD | SIMemOp::STORE,
             IsCrossAddrSpaceOrdering, Pos);

  return true;
}

bool SIGfx12CacheControl::enableVolatileAndOrNonTemporal(
    MachineBasicBlock::iterator &MI, SIAtomicAddrSpace AddrSpace, SIMemOp Op,
    bool IsVolatile, bool IsNonTemporal, bool IsLastUse = false) const {

  // Only handle load and store, not atomic read-modify-write instructions.
  assert(MI->mayLoad() ^ MI->mayStore());

  // Only update load and store, not LLVM IR atomic read-modify-write
  // instructions. The latter are always marked as volatile so cannot sensibly
  // handle it as do not want to pessimize all atomics. Also they do not support
  // the nontemporal attribute.
  assert(Op == SIMemOp::LOAD || Op == SIMemOp::STORE);

  bool Changed = false;

  if (IsLastUse) {
    // Set last-use hint.
    Changed |= setTH(MI, AMDGPU::CPol::TH_LU);
  } else if (IsNonTemporal) {
    // Set non-temporal hint for all cache levels.
    Changed |= setTH(MI, AMDGPU::CPol::TH_NT);
  }

  if (IsVolatile) {
    Changed |= setScope(MI, AMDGPU::CPol::SCOPE_SYS);

    if (Op == SIMemOp::STORE)
      Changed |= insertWaitsBeforeSystemScopeStore(MI);

    // Ensure operation has completed at system scope to cause all volatile
    // operations to be visible outside the program in a global order. Do not
    // request cross address space as only the global address space can be
    // observable outside the program, so no need to cause a waitcnt for LDS
    // address space operations.
    Changed |= insertWait(MI, SIAtomicScope::SYSTEM, AddrSpace, Op, false,
                          Position::AFTER);
  }

  return Changed;
}

bool SIGfx12CacheControl::expandSystemScopeStore(
    MachineBasicBlock::iterator &MI) const {
  MachineOperand *CPol = TII->getNamedOperand(*MI, OpName::cpol);
  if (CPol && ((CPol->getImm() & CPol::SCOPE) == CPol::SCOPE_SYS))
    return insertWaitsBeforeSystemScopeStore(MI);

  return false;
}

bool SIGfx12CacheControl::setAtomicScope(const MachineBasicBlock::iterator &MI,
                                         SIAtomicScope Scope,
                                         SIAtomicAddrSpace AddrSpace) const {
  bool Changed = false;

  if ((AddrSpace & SIAtomicAddrSpace::GLOBAL) != SIAtomicAddrSpace::NONE) {
    switch (Scope) {
    case SIAtomicScope::SYSTEM:
      Changed |= setScope(MI, AMDGPU::CPol::SCOPE_SYS);
      break;
    case SIAtomicScope::AGENT:
      Changed |= setScope(MI, AMDGPU::CPol::SCOPE_DEV);
      break;
    case SIAtomicScope::WORKGROUP:
      // In workgroup mode, SCOPE_SE is needed as waves can executes on
      // different CUs that access different L0s.
      if (!ST.isCuModeEnabled())
        Changed |= setScope(MI, AMDGPU::CPol::SCOPE_SE);
      break;
    case SIAtomicScope::WAVEFRONT:
    case SIAtomicScope::SINGLETHREAD:
      // No cache to bypass.
      break;
    default:
      llvm_unreachable("Unsupported synchronization scope");
    }
  }

  // The scratch address space does not need the global memory caches
  // to be bypassed as all memory operations by the same thread are
  // sequentially consistent, and no other thread can access scratch
  // memory.

  // Other address spaces do not have a cache.

  return Changed;
}

bool SIMemoryLegalizer::removeAtomicPseudoMIs() {
  if (AtomicPseudoMIs.empty())
    return false;

  for (auto &MI : AtomicPseudoMIs)
    MI->eraseFromParent();

  AtomicPseudoMIs.clear();
  return true;
}

bool SIMemoryLegalizer::expandLoad(const SIMemOpInfo &MOI,
                                   MachineBasicBlock::iterator &MI) {
  assert(MI->mayLoad() && !MI->mayStore());

  bool Changed = false;

  if (MOI.isAtomic()) {
    if (MOI.getOrdering() == AtomicOrdering::Monotonic ||
        MOI.getOrdering() == AtomicOrdering::Acquire ||
        MOI.getOrdering() == AtomicOrdering::SequentiallyConsistent) {
      Changed |= CC->enableLoadCacheBypass(MI, MOI.getScope(),
                                           MOI.getOrderingAddrSpace());
    }

    if (MOI.getOrdering() == AtomicOrdering::SequentiallyConsistent)
      Changed |= CC->insertWait(MI, MOI.getScope(),
                                MOI.getOrderingAddrSpace(),
                                SIMemOp::LOAD | SIMemOp::STORE,
                                MOI.getIsCrossAddressSpaceOrdering(),
                                Position::BEFORE);

    if (MOI.getOrdering() == AtomicOrdering::Acquire ||
        MOI.getOrdering() == AtomicOrdering::SequentiallyConsistent) {
      Changed |= CC->insertWait(MI, MOI.getScope(),
                                MOI.getInstrAddrSpace(),
                                SIMemOp::LOAD,
                                MOI.getIsCrossAddressSpaceOrdering(),
                                Position::AFTER);
      Changed |= CC->insertAcquire(MI, MOI.getScope(),
                                   MOI.getOrderingAddrSpace(),
                                   Position::AFTER);
    }

    return Changed;
  }

  // Atomic instructions already bypass caches to the scope specified by the
  // SyncScope operand. Only non-atomic volatile and nontemporal/last-use
  // instructions need additional treatment.
  Changed |= CC->enableVolatileAndOrNonTemporal(
      MI, MOI.getInstrAddrSpace(), SIMemOp::LOAD, MOI.isVolatile(),
      MOI.isNonTemporal(), MOI.isLastUse());

  return Changed;
}

bool SIMemoryLegalizer::expandStore(const SIMemOpInfo &MOI,
                                    MachineBasicBlock::iterator &MI) {
  assert(!MI->mayLoad() && MI->mayStore());

  bool Changed = false;

  if (MOI.isAtomic()) {
    if (MOI.getOrdering() == AtomicOrdering::Monotonic ||
        MOI.getOrdering() == AtomicOrdering::Release ||
        MOI.getOrdering() == AtomicOrdering::SequentiallyConsistent) {
      Changed |= CC->enableStoreCacheBypass(MI, MOI.getScope(),
                                            MOI.getOrderingAddrSpace());
    }

    if (MOI.getOrdering() == AtomicOrdering::Release ||
        MOI.getOrdering() == AtomicOrdering::SequentiallyConsistent)
      Changed |= CC->insertRelease(MI, MOI.getScope(),
                                   MOI.getOrderingAddrSpace(),
                                   MOI.getIsCrossAddressSpaceOrdering(),
                                   Position::BEFORE);

    return Changed;
  }

  // Atomic instructions already bypass caches to the scope specified by the
  // SyncScope operand. Only non-atomic volatile and nontemporal instructions
  // need additional treatment.
  Changed |= CC->enableVolatileAndOrNonTemporal(
      MI, MOI.getInstrAddrSpace(), SIMemOp::STORE, MOI.isVolatile(),
      MOI.isNonTemporal());

  // GFX12 specific, scope(desired coherence domain in cache hierarchy) is
  // instruction field, do not confuse it with atomic scope.
  Changed |= CC->expandSystemScopeStore(MI);
  return Changed;
}

bool SIMemoryLegalizer::expandAtomicFence(const SIMemOpInfo &MOI,
                                          MachineBasicBlock::iterator &MI) {
  assert(MI->getOpcode() == AMDGPU::ATOMIC_FENCE);

  AtomicPseudoMIs.push_back(MI);
  bool Changed = false;

  // Refine fenced address space based on MMRAs.
  //
  // TODO: Should we support this MMRA on other atomic operations?
  auto OrderingAddrSpace =
      getFenceAddrSpaceMMRA(*MI, MOI.getOrderingAddrSpace());

  if (MOI.isAtomic()) {
    if (MOI.getOrdering() == AtomicOrdering::Acquire)
      Changed |= CC->insertWait(
          MI, MOI.getScope(), OrderingAddrSpace, SIMemOp::LOAD | SIMemOp::STORE,
          MOI.getIsCrossAddressSpaceOrdering(), Position::BEFORE);

    if (MOI.getOrdering() == AtomicOrdering::Release ||
        MOI.getOrdering() == AtomicOrdering::AcquireRelease ||
        MOI.getOrdering() == AtomicOrdering::SequentiallyConsistent)
      /// TODO: This relies on a barrier always generating a waitcnt
      /// for LDS to ensure it is not reordered with the completion of
      /// the proceeding LDS operations. If barrier had a memory
      /// ordering and memory scope, then library does not need to
      /// generate a fence. Could add support in this file for
      /// barrier. SIInsertWaitcnt.cpp could then stop unconditionally
      /// adding S_WAITCNT before a S_BARRIER.
      Changed |= CC->insertRelease(MI, MOI.getScope(), OrderingAddrSpace,
                                   MOI.getIsCrossAddressSpaceOrdering(),
                                   Position::BEFORE);

    // TODO: If both release and invalidate are happening they could be combined
    // to use the single "BUFFER_WBINV*" instruction. This could be done by
    // reorganizing this code or as part of optimizing SIInsertWaitcnt pass to
    // track cache invalidate and write back instructions.

    if (MOI.getOrdering() == AtomicOrdering::Acquire ||
        MOI.getOrdering() == AtomicOrdering::AcquireRelease ||
        MOI.getOrdering() == AtomicOrdering::SequentiallyConsistent)
      Changed |= CC->insertAcquire(MI, MOI.getScope(), OrderingAddrSpace,
                                   Position::BEFORE);

    return Changed;
  }

  return Changed;
}

bool SIMemoryLegalizer::expandAtomicCmpxchgOrRmw(const SIMemOpInfo &MOI,
  MachineBasicBlock::iterator &MI) {
  assert(MI->mayLoad() && MI->mayStore());

  bool Changed = false;

  if (MOI.isAtomic()) {
    if (MOI.getOrdering() == AtomicOrdering::Monotonic ||
        MOI.getOrdering() == AtomicOrdering::Acquire ||
        MOI.getOrdering() == AtomicOrdering::Release ||
        MOI.getOrdering() == AtomicOrdering::AcquireRelease ||
        MOI.getOrdering() == AtomicOrdering::SequentiallyConsistent) {
      Changed |= CC->enableRMWCacheBypass(MI, MOI.getScope(),
                                          MOI.getInstrAddrSpace());
    }

    if (MOI.getOrdering() == AtomicOrdering::Release ||
        MOI.getOrdering() == AtomicOrdering::AcquireRelease ||
        MOI.getOrdering() == AtomicOrdering::SequentiallyConsistent ||
        MOI.getFailureOrdering() == AtomicOrdering::SequentiallyConsistent)
      Changed |= CC->insertRelease(MI, MOI.getScope(),
                                   MOI.getOrderingAddrSpace(),
                                   MOI.getIsCrossAddressSpaceOrdering(),
                                   Position::BEFORE);

    if (MOI.getOrdering() == AtomicOrdering::Acquire ||
        MOI.getOrdering() == AtomicOrdering::AcquireRelease ||
        MOI.getOrdering() == AtomicOrdering::SequentiallyConsistent ||
        MOI.getFailureOrdering() == AtomicOrdering::Acquire ||
        MOI.getFailureOrdering() == AtomicOrdering::SequentiallyConsistent) {
      Changed |= CC->insertWait(MI, MOI.getScope(),
                                MOI.getInstrAddrSpace(),
                                isAtomicRet(*MI) ? SIMemOp::LOAD :
                                                   SIMemOp::STORE,
                                MOI.getIsCrossAddressSpaceOrdering(),
                                Position::AFTER);
      Changed |= CC->insertAcquire(MI, MOI.getScope(),
                                   MOI.getOrderingAddrSpace(),
                                   Position::AFTER);
    }

    return Changed;
  }

  return Changed;
}

bool SIMemoryLegalizer::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;

  const MachineModuleInfo &MMI =
      getAnalysis<MachineModuleInfoWrapperPass>().getMMI();

  SIMemOpAccess MOA(MMI.getObjFileInfo<AMDGPUMachineModuleInfo>());
  CC = SICacheControl::create(MF.getSubtarget<GCNSubtarget>());

  for (auto &MBB : MF) {
    for (auto MI = MBB.begin(); MI != MBB.end(); ++MI) {

      // Unbundle instructions after the post-RA scheduler.
      if (MI->isBundle() && MI->mayLoadOrStore()) {
        MachineBasicBlock::instr_iterator II(MI->getIterator());
        for (MachineBasicBlock::instr_iterator I = ++II, E = MBB.instr_end();
             I != E && I->isBundledWithPred(); ++I) {
          I->unbundleFromPred();
          for (MachineOperand &MO : I->operands())
            if (MO.isReg())
              MO.setIsInternalRead(false);
        }

        MI->eraseFromParent();
        MI = II->getIterator();
      }

      if (!(MI->getDesc().TSFlags & SIInstrFlags::maybeAtomic))
        continue;

      if (const auto &MOI = MOA.getLoadInfo(MI))
        Changed |= expandLoad(*MOI, MI);
      else if (const auto &MOI = MOA.getStoreInfo(MI)) {
        Changed |= expandStore(*MOI, MI);
        Changed |= CC->tryForceStoreSC0SC1(*MOI, MI);
      } else if (const auto &MOI = MOA.getAtomicFenceInfo(MI))
        Changed |= expandAtomicFence(*MOI, MI);
      else if (const auto &MOI = MOA.getAtomicCmpxchgOrRmwInfo(MI))
        Changed |= expandAtomicCmpxchgOrRmw(*MOI, MI);
    }
  }

  Changed |= removeAtomicPseudoMIs();
  return Changed;
}

INITIALIZE_PASS(SIMemoryLegalizer, DEBUG_TYPE, PASS_NAME, false, false)

char SIMemoryLegalizer::ID = 0;
char &llvm::SIMemoryLegalizerID = SIMemoryLegalizer::ID;

FunctionPass *llvm::createSIMemoryLegalizerPass() {
  return new SIMemoryLegalizer();
}

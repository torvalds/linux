//===-- HexagonVectorCombine.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// HexagonVectorCombine is a utility class implementing a variety of functions
// that assist in vector-based optimizations.
//
// AlignVectors: replace unaligned vector loads and stores with aligned ones.
// HvxIdioms: recognize various opportunities to generate HVX intrinsic code.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InstSimplifyFolder.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsHexagon.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/Local.h"

#include "HexagonSubtarget.h"
#include "HexagonTargetMachine.h"

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#define DEBUG_TYPE "hexagon-vc"

using namespace llvm;

namespace {
cl::opt<bool> DumpModule("hvc-dump-module", cl::Hidden);
cl::opt<bool> VAEnabled("hvc-va", cl::Hidden, cl::init(true)); // Align
cl::opt<bool> VIEnabled("hvc-vi", cl::Hidden, cl::init(true)); // Idioms
cl::opt<bool> VADoFullStores("hvc-va-full-stores", cl::Hidden);

cl::opt<unsigned> VAGroupCountLimit("hvc-va-group-count-limit", cl::Hidden,
                                    cl::init(~0));
cl::opt<unsigned> VAGroupSizeLimit("hvc-va-group-size-limit", cl::Hidden,
                                   cl::init(~0));

class HexagonVectorCombine {
public:
  HexagonVectorCombine(Function &F_, AliasAnalysis &AA_, AssumptionCache &AC_,
                       DominatorTree &DT_, ScalarEvolution &SE_,
                       TargetLibraryInfo &TLI_, const TargetMachine &TM_)
      : F(F_), DL(F.getDataLayout()), AA(AA_), AC(AC_), DT(DT_),
        SE(SE_), TLI(TLI_),
        HST(static_cast<const HexagonSubtarget &>(*TM_.getSubtargetImpl(F))) {}

  bool run();

  // Common integer type.
  IntegerType *getIntTy(unsigned Width = 32) const;
  // Byte type: either scalar (when Length = 0), or vector with given
  // element count.
  Type *getByteTy(int ElemCount = 0) const;
  // Boolean type: either scalar (when Length = 0), or vector with given
  // element count.
  Type *getBoolTy(int ElemCount = 0) const;
  // Create a ConstantInt of type returned by getIntTy with the value Val.
  ConstantInt *getConstInt(int Val, unsigned Width = 32) const;
  // Get the integer value of V, if it exists.
  std::optional<APInt> getIntValue(const Value *Val) const;
  // Is Val a constant 0, or a vector of 0s?
  bool isZero(const Value *Val) const;
  // Is Val an undef value?
  bool isUndef(const Value *Val) const;
  // Is Val a scalar (i1 true) or a vector of (i1 true)?
  bool isTrue(const Value *Val) const;
  // Is Val a scalar (i1 false) or a vector of (i1 false)?
  bool isFalse(const Value *Val) const;

  // Get HVX vector type with the given element type.
  VectorType *getHvxTy(Type *ElemTy, bool Pair = false) const;

  enum SizeKind {
    Store, // Store size
    Alloc, // Alloc size
  };
  int getSizeOf(const Value *Val, SizeKind Kind = Store) const;
  int getSizeOf(const Type *Ty, SizeKind Kind = Store) const;
  int getTypeAlignment(Type *Ty) const;
  size_t length(Value *Val) const;
  size_t length(Type *Ty) const;

  Constant *getNullValue(Type *Ty) const;
  Constant *getFullValue(Type *Ty) const;
  Constant *getConstSplat(Type *Ty, int Val) const;

  Value *simplify(Value *Val) const;

  Value *insertb(IRBuilderBase &Builder, Value *Dest, Value *Src, int Start,
                 int Length, int Where) const;
  Value *vlalignb(IRBuilderBase &Builder, Value *Lo, Value *Hi,
                  Value *Amt) const;
  Value *vralignb(IRBuilderBase &Builder, Value *Lo, Value *Hi,
                  Value *Amt) const;
  Value *concat(IRBuilderBase &Builder, ArrayRef<Value *> Vecs) const;
  Value *vresize(IRBuilderBase &Builder, Value *Val, int NewSize,
                 Value *Pad) const;
  Value *rescale(IRBuilderBase &Builder, Value *Mask, Type *FromTy,
                 Type *ToTy) const;
  Value *vlsb(IRBuilderBase &Builder, Value *Val) const;
  Value *vbytes(IRBuilderBase &Builder, Value *Val) const;
  Value *subvector(IRBuilderBase &Builder, Value *Val, unsigned Start,
                   unsigned Length) const;
  Value *sublo(IRBuilderBase &Builder, Value *Val) const;
  Value *subhi(IRBuilderBase &Builder, Value *Val) const;
  Value *vdeal(IRBuilderBase &Builder, Value *Val0, Value *Val1) const;
  Value *vshuff(IRBuilderBase &Builder, Value *Val0, Value *Val1) const;

  Value *createHvxIntrinsic(IRBuilderBase &Builder, Intrinsic::ID IntID,
                            Type *RetTy, ArrayRef<Value *> Args,
                            ArrayRef<Type *> ArgTys = std::nullopt,
                            ArrayRef<Value *> MDSources = std::nullopt) const;
  SmallVector<Value *> splitVectorElements(IRBuilderBase &Builder, Value *Vec,
                                           unsigned ToWidth) const;
  Value *joinVectorElements(IRBuilderBase &Builder, ArrayRef<Value *> Values,
                            VectorType *ToType) const;

  std::optional<int> calculatePointerDifference(Value *Ptr0, Value *Ptr1) const;

  unsigned getNumSignificantBits(const Value *V,
                                 const Instruction *CtxI = nullptr) const;
  KnownBits getKnownBits(const Value *V,
                         const Instruction *CtxI = nullptr) const;

  bool isSafeToClone(const Instruction &In) const;

  template <typename T = std::vector<Instruction *>>
  bool isSafeToMoveBeforeInBB(const Instruction &In,
                              BasicBlock::const_iterator To,
                              const T &IgnoreInsts = {}) const;

  // This function is only used for assertions at the moment.
  [[maybe_unused]] bool isByteVecTy(Type *Ty) const;

  Function &F;
  const DataLayout &DL;
  AliasAnalysis &AA;
  AssumptionCache &AC;
  DominatorTree &DT;
  ScalarEvolution &SE;
  TargetLibraryInfo &TLI;
  const HexagonSubtarget &HST;

private:
  Value *getElementRange(IRBuilderBase &Builder, Value *Lo, Value *Hi,
                         int Start, int Length) const;
};

class AlignVectors {
  // This code tries to replace unaligned vector loads/stores with aligned
  // ones.
  // Consider unaligned load:
  //   %v = original_load %some_addr, align <bad>
  //   %user = %v
  // It will generate
  //      = load ..., align <good>
  //      = load ..., align <good>
  //      = valign
  //      etc.
  //   %synthesize = combine/shuffle the loaded data so that it looks
  //                 exactly like what "original_load" has loaded.
  //   %user = %synthesize
  // Similarly for stores.
public:
  AlignVectors(const HexagonVectorCombine &HVC_) : HVC(HVC_) {}

  bool run();

private:
  using InstList = std::vector<Instruction *>;
  using InstMap = DenseMap<Instruction *, Instruction *>;

  struct AddrInfo {
    AddrInfo(const AddrInfo &) = default;
    AddrInfo(const HexagonVectorCombine &HVC, Instruction *I, Value *A, Type *T,
             Align H)
        : Inst(I), Addr(A), ValTy(T), HaveAlign(H),
          NeedAlign(HVC.getTypeAlignment(ValTy)) {}
    AddrInfo &operator=(const AddrInfo &) = default;

    // XXX: add Size member?
    Instruction *Inst;
    Value *Addr;
    Type *ValTy;
    Align HaveAlign;
    Align NeedAlign;
    int Offset = 0; // Offset (in bytes) from the first member of the
                    // containing AddrList.
  };
  using AddrList = std::vector<AddrInfo>;

  struct InstrLess {
    bool operator()(const Instruction *A, const Instruction *B) const {
      return A->comesBefore(B);
    }
  };
  using DepList = std::set<Instruction *, InstrLess>;

  struct MoveGroup {
    MoveGroup(const AddrInfo &AI, Instruction *B, bool Hvx, bool Load)
        : Base(B), Main{AI.Inst}, Clones{}, IsHvx(Hvx), IsLoad(Load) {}
    MoveGroup() = default;
    Instruction *Base; // Base instruction of the parent address group.
    InstList Main;     // Main group of instructions.
    InstList Deps;     // List of dependencies.
    InstMap Clones;    // Map from original Deps to cloned ones.
    bool IsHvx;        // Is this group of HVX instructions?
    bool IsLoad;       // Is this a load group?
  };
  using MoveList = std::vector<MoveGroup>;

  struct ByteSpan {
    // A representation of "interesting" bytes within a given span of memory.
    // These bytes are those that are loaded or stored, and they don't have
    // to cover the entire span of memory.
    //
    // The representation works by picking a contiguous sequence of bytes
    // from somewhere within a llvm::Value, and placing it at a given offset
    // within the span.
    //
    // The sequence of bytes from llvm:Value is represented by Segment.
    // Block is Segment, plus where it goes in the span.
    //
    // An important feature of ByteSpan is being able to make a "section",
    // i.e. creating another ByteSpan corresponding to a range of offsets
    // relative to the source span.

    struct Segment {
      // Segment of a Value: 'Len' bytes starting at byte 'Begin'.
      Segment(Value *Val, int Begin, int Len)
          : Val(Val), Start(Begin), Size(Len) {}
      Segment(const Segment &Seg) = default;
      Segment &operator=(const Segment &Seg) = default;
      Value *Val; // Value representable as a sequence of bytes.
      int Start;  // First byte of the value that belongs to the segment.
      int Size;   // Number of bytes in the segment.
    };

    struct Block {
      Block(Value *Val, int Len, int Pos) : Seg(Val, 0, Len), Pos(Pos) {}
      Block(Value *Val, int Off, int Len, int Pos)
          : Seg(Val, Off, Len), Pos(Pos) {}
      Block(const Block &Blk) = default;
      Block &operator=(const Block &Blk) = default;
      Segment Seg; // Value segment.
      int Pos;     // Position (offset) of the block in the span.
    };

    int extent() const;
    ByteSpan section(int Start, int Length) const;
    ByteSpan &shift(int Offset);
    SmallVector<Value *, 8> values() const;

    int size() const { return Blocks.size(); }
    Block &operator[](int i) { return Blocks[i]; }
    const Block &operator[](int i) const { return Blocks[i]; }

    std::vector<Block> Blocks;

    using iterator = decltype(Blocks)::iterator;
    iterator begin() { return Blocks.begin(); }
    iterator end() { return Blocks.end(); }
    using const_iterator = decltype(Blocks)::const_iterator;
    const_iterator begin() const { return Blocks.begin(); }
    const_iterator end() const { return Blocks.end(); }
  };

  Align getAlignFromValue(const Value *V) const;
  std::optional<AddrInfo> getAddrInfo(Instruction &In) const;
  bool isHvx(const AddrInfo &AI) const;
  // This function is only used for assertions at the moment.
  [[maybe_unused]] bool isSectorTy(Type *Ty) const;

  Value *getPayload(Value *Val) const;
  Value *getMask(Value *Val) const;
  Value *getPassThrough(Value *Val) const;

  Value *createAdjustedPointer(IRBuilderBase &Builder, Value *Ptr, Type *ValTy,
                               int Adjust,
                               const InstMap &CloneMap = InstMap()) const;
  Value *createAlignedPointer(IRBuilderBase &Builder, Value *Ptr, Type *ValTy,
                              int Alignment,
                              const InstMap &CloneMap = InstMap()) const;

  Value *createLoad(IRBuilderBase &Builder, Type *ValTy, Value *Ptr,
                    Value *Predicate, int Alignment, Value *Mask,
                    Value *PassThru,
                    ArrayRef<Value *> MDSources = std::nullopt) const;
  Value *createSimpleLoad(IRBuilderBase &Builder, Type *ValTy, Value *Ptr,
                          int Alignment,
                          ArrayRef<Value *> MDSources = std::nullopt) const;

  Value *createStore(IRBuilderBase &Builder, Value *Val, Value *Ptr,
                     Value *Predicate, int Alignment, Value *Mask,
                     ArrayRef<Value *> MDSources = std ::nullopt) const;
  Value *createSimpleStore(IRBuilderBase &Builder, Value *Val, Value *Ptr,
                           int Alignment,
                           ArrayRef<Value *> MDSources = std ::nullopt) const;

  Value *createPredicatedLoad(IRBuilderBase &Builder, Type *ValTy, Value *Ptr,
                              Value *Predicate, int Alignment,
                              ArrayRef<Value *> MDSources = std::nullopt) const;
  Value *
  createPredicatedStore(IRBuilderBase &Builder, Value *Val, Value *Ptr,
                        Value *Predicate, int Alignment,
                        ArrayRef<Value *> MDSources = std::nullopt) const;

  DepList getUpwardDeps(Instruction *In, Instruction *Base) const;
  bool createAddressGroups();
  MoveList createLoadGroups(const AddrList &Group) const;
  MoveList createStoreGroups(const AddrList &Group) const;
  bool moveTogether(MoveGroup &Move) const;
  template <typename T> InstMap cloneBefore(Instruction *To, T &&Insts) const;

  void realignLoadGroup(IRBuilderBase &Builder, const ByteSpan &VSpan,
                        int ScLen, Value *AlignVal, Value *AlignAddr) const;
  void realignStoreGroup(IRBuilderBase &Builder, const ByteSpan &VSpan,
                         int ScLen, Value *AlignVal, Value *AlignAddr) const;
  bool realignGroup(const MoveGroup &Move) const;

  Value *makeTestIfUnaligned(IRBuilderBase &Builder, Value *AlignVal,
                             int Alignment) const;

  friend raw_ostream &operator<<(raw_ostream &OS, const AddrInfo &AI);
  friend raw_ostream &operator<<(raw_ostream &OS, const MoveGroup &MG);
  friend raw_ostream &operator<<(raw_ostream &OS, const ByteSpan::Block &B);
  friend raw_ostream &operator<<(raw_ostream &OS, const ByteSpan &BS);

  std::map<Instruction *, AddrList> AddrGroups;
  const HexagonVectorCombine &HVC;
};

LLVM_ATTRIBUTE_UNUSED
raw_ostream &operator<<(raw_ostream &OS, const AlignVectors::AddrInfo &AI) {
  OS << "Inst: " << AI.Inst << "  " << *AI.Inst << '\n';
  OS << "Addr: " << *AI.Addr << '\n';
  OS << "Type: " << *AI.ValTy << '\n';
  OS << "HaveAlign: " << AI.HaveAlign.value() << '\n';
  OS << "NeedAlign: " << AI.NeedAlign.value() << '\n';
  OS << "Offset: " << AI.Offset;
  return OS;
}

LLVM_ATTRIBUTE_UNUSED
raw_ostream &operator<<(raw_ostream &OS, const AlignVectors::MoveGroup &MG) {
  OS << "IsLoad:" << (MG.IsLoad ? "yes" : "no");
  OS << ", IsHvx:" << (MG.IsHvx ? "yes" : "no") << '\n';
  OS << "Main\n";
  for (Instruction *I : MG.Main)
    OS << "  " << *I << '\n';
  OS << "Deps\n";
  for (Instruction *I : MG.Deps)
    OS << "  " << *I << '\n';
  OS << "Clones\n";
  for (auto [K, V] : MG.Clones) {
    OS << "    ";
    K->printAsOperand(OS, false);
    OS << "\t-> " << *V << '\n';
  }
  return OS;
}

LLVM_ATTRIBUTE_UNUSED
raw_ostream &operator<<(raw_ostream &OS,
                        const AlignVectors::ByteSpan::Block &B) {
  OS << "  @" << B.Pos << " [" << B.Seg.Start << ',' << B.Seg.Size << "] ";
  if (B.Seg.Val == reinterpret_cast<const Value *>(&B)) {
    OS << "(self:" << B.Seg.Val << ')';
  } else if (B.Seg.Val != nullptr) {
    OS << *B.Seg.Val;
  } else {
    OS << "(null)";
  }
  return OS;
}

LLVM_ATTRIBUTE_UNUSED
raw_ostream &operator<<(raw_ostream &OS, const AlignVectors::ByteSpan &BS) {
  OS << "ByteSpan[size=" << BS.size() << ", extent=" << BS.extent() << '\n';
  for (const AlignVectors::ByteSpan::Block &B : BS)
    OS << B << '\n';
  OS << ']';
  return OS;
}

class HvxIdioms {
public:
  HvxIdioms(const HexagonVectorCombine &HVC_) : HVC(HVC_) {
    auto *Int32Ty = HVC.getIntTy(32);
    HvxI32Ty = HVC.getHvxTy(Int32Ty, /*Pair=*/false);
    HvxP32Ty = HVC.getHvxTy(Int32Ty, /*Pair=*/true);
  }

  bool run();

private:
  enum Signedness { Positive, Signed, Unsigned };

  // Value + sign
  // This is to keep track of whether the value should be treated as signed
  // or unsigned, or is known to be positive.
  struct SValue {
    Value *Val;
    Signedness Sgn;
  };

  struct FxpOp {
    unsigned Opcode;
    unsigned Frac; // Number of fraction bits
    SValue X, Y;
    // If present, add 1 << RoundAt before shift:
    std::optional<unsigned> RoundAt;
    VectorType *ResTy;
  };

  auto getNumSignificantBits(Value *V, Instruction *In) const
      -> std::pair<unsigned, Signedness>;
  auto canonSgn(SValue X, SValue Y) const -> std::pair<SValue, SValue>;

  auto matchFxpMul(Instruction &In) const -> std::optional<FxpOp>;
  auto processFxpMul(Instruction &In, const FxpOp &Op) const -> Value *;

  auto processFxpMulChopped(IRBuilderBase &Builder, Instruction &In,
                            const FxpOp &Op) const -> Value *;
  auto createMulQ15(IRBuilderBase &Builder, SValue X, SValue Y,
                    bool Rounding) const -> Value *;
  auto createMulQ31(IRBuilderBase &Builder, SValue X, SValue Y,
                    bool Rounding) const -> Value *;
  // Return {Result, Carry}, where Carry is a vector predicate.
  auto createAddCarry(IRBuilderBase &Builder, Value *X, Value *Y,
                      Value *CarryIn = nullptr) const
      -> std::pair<Value *, Value *>;
  auto createMul16(IRBuilderBase &Builder, SValue X, SValue Y) const -> Value *;
  auto createMulH16(IRBuilderBase &Builder, SValue X, SValue Y) const
      -> Value *;
  auto createMul32(IRBuilderBase &Builder, SValue X, SValue Y) const
      -> std::pair<Value *, Value *>;
  auto createAddLong(IRBuilderBase &Builder, ArrayRef<Value *> WordX,
                     ArrayRef<Value *> WordY) const -> SmallVector<Value *>;
  auto createMulLong(IRBuilderBase &Builder, ArrayRef<Value *> WordX,
                     Signedness SgnX, ArrayRef<Value *> WordY,
                     Signedness SgnY) const -> SmallVector<Value *>;

  VectorType *HvxI32Ty;
  VectorType *HvxP32Ty;
  const HexagonVectorCombine &HVC;

  friend raw_ostream &operator<<(raw_ostream &, const FxpOp &);
};

[[maybe_unused]] raw_ostream &operator<<(raw_ostream &OS,
                                         const HvxIdioms::FxpOp &Op) {
  static const char *SgnNames[] = {"Positive", "Signed", "Unsigned"};
  OS << Instruction::getOpcodeName(Op.Opcode) << '.' << Op.Frac;
  if (Op.RoundAt.has_value()) {
    if (Op.Frac != 0 && *Op.RoundAt == Op.Frac - 1) {
      OS << ":rnd";
    } else {
      OS << " + 1<<" << *Op.RoundAt;
    }
  }
  OS << "\n  X:(" << SgnNames[Op.X.Sgn] << ") " << *Op.X.Val << "\n"
     << "  Y:(" << SgnNames[Op.Y.Sgn] << ") " << *Op.Y.Val;
  return OS;
}

} // namespace

namespace {

template <typename T> T *getIfUnordered(T *MaybeT) {
  return MaybeT && MaybeT->isUnordered() ? MaybeT : nullptr;
}
template <typename T> T *isCandidate(Instruction *In) {
  return dyn_cast<T>(In);
}
template <> LoadInst *isCandidate<LoadInst>(Instruction *In) {
  return getIfUnordered(dyn_cast<LoadInst>(In));
}
template <> StoreInst *isCandidate<StoreInst>(Instruction *In) {
  return getIfUnordered(dyn_cast<StoreInst>(In));
}

#if !defined(_MSC_VER) || _MSC_VER >= 1926
// VS2017 and some versions of VS2019 have trouble compiling this:
// error C2976: 'std::map': too few template arguments
// VS 2019 16.x is known to work, except for 16.4/16.5 (MSC_VER 1924/1925)
template <typename Pred, typename... Ts>
void erase_if(std::map<Ts...> &map, Pred p)
#else
template <typename Pred, typename T, typename U>
void erase_if(std::map<T, U> &map, Pred p)
#endif
{
  for (auto i = map.begin(), e = map.end(); i != e;) {
    if (p(*i))
      i = map.erase(i);
    else
      i = std::next(i);
  }
}

// Forward other erase_ifs to the LLVM implementations.
template <typename Pred, typename T> void erase_if(T &&container, Pred p) {
  llvm::erase_if(std::forward<T>(container), p);
}

} // namespace

// --- Begin AlignVectors

// For brevity, only consider loads. We identify a group of loads where we
// know the relative differences between their addresses, so we know how they
// are laid out in memory (relative to one another). These loads can overlap,
// can be shorter or longer than the desired vector length.
// Ultimately we want to generate a sequence of aligned loads that will load
// every byte that the original loads loaded, and have the program use these
// loaded values instead of the original loads.
// We consider the contiguous memory area spanned by all these loads.
//
// Let's say that a single aligned vector load can load 16 bytes at a time.
// If the program wanted to use a byte at offset 13 from the beginning of the
// original span, it will be a byte at offset 13+x in the aligned data for
// some x>=0. This may happen to be in the first aligned load, or in the load
// following it. Since we generally don't know what the that alignment value
// is at compile time, we proactively do valigns on the aligned loads, so that
// byte that was at offset 13 is still at offset 13 after the valigns.
//
// This will be the starting point for making the rest of the program use the
// data loaded by the new loads.
// For each original load, and its users:
//   %v = load ...
//   ... = %v
//   ... = %v
// we create
//   %new_v = extract/combine/shuffle data from loaded/valigned vectors so
//            it contains the same value as %v did before
// then replace all users of %v with %new_v.
//   ... = %new_v
//   ... = %new_v

auto AlignVectors::ByteSpan::extent() const -> int {
  if (size() == 0)
    return 0;
  int Min = Blocks[0].Pos;
  int Max = Blocks[0].Pos + Blocks[0].Seg.Size;
  for (int i = 1, e = size(); i != e; ++i) {
    Min = std::min(Min, Blocks[i].Pos);
    Max = std::max(Max, Blocks[i].Pos + Blocks[i].Seg.Size);
  }
  return Max - Min;
}

auto AlignVectors::ByteSpan::section(int Start, int Length) const -> ByteSpan {
  ByteSpan Section;
  for (const ByteSpan::Block &B : Blocks) {
    int L = std::max(B.Pos, Start);                       // Left end.
    int R = std::min(B.Pos + B.Seg.Size, Start + Length); // Right end+1.
    if (L < R) {
      // How much to chop off the beginning of the segment:
      int Off = L > B.Pos ? L - B.Pos : 0;
      Section.Blocks.emplace_back(B.Seg.Val, B.Seg.Start + Off, R - L, L);
    }
  }
  return Section;
}

auto AlignVectors::ByteSpan::shift(int Offset) -> ByteSpan & {
  for (Block &B : Blocks)
    B.Pos += Offset;
  return *this;
}

auto AlignVectors::ByteSpan::values() const -> SmallVector<Value *, 8> {
  SmallVector<Value *, 8> Values(Blocks.size());
  for (int i = 0, e = Blocks.size(); i != e; ++i)
    Values[i] = Blocks[i].Seg.Val;
  return Values;
}

auto AlignVectors::getAlignFromValue(const Value *V) const -> Align {
  const auto *C = dyn_cast<ConstantInt>(V);
  assert(C && "Alignment must be a compile-time constant integer");
  return C->getAlignValue();
}

auto AlignVectors::getAddrInfo(Instruction &In) const
    -> std::optional<AddrInfo> {
  if (auto *L = isCandidate<LoadInst>(&In))
    return AddrInfo(HVC, L, L->getPointerOperand(), L->getType(),
                    L->getAlign());
  if (auto *S = isCandidate<StoreInst>(&In))
    return AddrInfo(HVC, S, S->getPointerOperand(),
                    S->getValueOperand()->getType(), S->getAlign());
  if (auto *II = isCandidate<IntrinsicInst>(&In)) {
    Intrinsic::ID ID = II->getIntrinsicID();
    switch (ID) {
    case Intrinsic::masked_load:
      return AddrInfo(HVC, II, II->getArgOperand(0), II->getType(),
                      getAlignFromValue(II->getArgOperand(1)));
    case Intrinsic::masked_store:
      return AddrInfo(HVC, II, II->getArgOperand(1),
                      II->getArgOperand(0)->getType(),
                      getAlignFromValue(II->getArgOperand(2)));
    }
  }
  return std::nullopt;
}

auto AlignVectors::isHvx(const AddrInfo &AI) const -> bool {
  return HVC.HST.isTypeForHVX(AI.ValTy);
}

auto AlignVectors::getPayload(Value *Val) const -> Value * {
  if (auto *In = dyn_cast<Instruction>(Val)) {
    Intrinsic::ID ID = 0;
    if (auto *II = dyn_cast<IntrinsicInst>(In))
      ID = II->getIntrinsicID();
    if (isa<StoreInst>(In) || ID == Intrinsic::masked_store)
      return In->getOperand(0);
  }
  return Val;
}

auto AlignVectors::getMask(Value *Val) const -> Value * {
  if (auto *II = dyn_cast<IntrinsicInst>(Val)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::masked_load:
      return II->getArgOperand(2);
    case Intrinsic::masked_store:
      return II->getArgOperand(3);
    }
  }

  Type *ValTy = getPayload(Val)->getType();
  if (auto *VecTy = dyn_cast<VectorType>(ValTy))
    return HVC.getFullValue(HVC.getBoolTy(HVC.length(VecTy)));
  return HVC.getFullValue(HVC.getBoolTy());
}

auto AlignVectors::getPassThrough(Value *Val) const -> Value * {
  if (auto *II = dyn_cast<IntrinsicInst>(Val)) {
    if (II->getIntrinsicID() == Intrinsic::masked_load)
      return II->getArgOperand(3);
  }
  return UndefValue::get(getPayload(Val)->getType());
}

auto AlignVectors::createAdjustedPointer(IRBuilderBase &Builder, Value *Ptr,
                                         Type *ValTy, int Adjust,
                                         const InstMap &CloneMap) const
    -> Value * {
  if (auto *I = dyn_cast<Instruction>(Ptr))
    if (Instruction *New = CloneMap.lookup(I))
      Ptr = New;
  return Builder.CreatePtrAdd(Ptr, HVC.getConstInt(Adjust), "gep");
}

auto AlignVectors::createAlignedPointer(IRBuilderBase &Builder, Value *Ptr,
                                        Type *ValTy, int Alignment,
                                        const InstMap &CloneMap) const
    -> Value * {
  auto remap = [&](Value *V) -> Value * {
    if (auto *I = dyn_cast<Instruction>(V)) {
      for (auto [Old, New] : CloneMap)
        I->replaceUsesOfWith(Old, New);
      return I;
    }
    return V;
  };
  Value *AsInt = Builder.CreatePtrToInt(Ptr, HVC.getIntTy(), "pti");
  Value *Mask = HVC.getConstInt(-Alignment);
  Value *And = Builder.CreateAnd(remap(AsInt), Mask, "and");
  return Builder.CreateIntToPtr(
      And, PointerType::getUnqual(ValTy->getContext()), "itp");
}

auto AlignVectors::createLoad(IRBuilderBase &Builder, Type *ValTy, Value *Ptr,
                              Value *Predicate, int Alignment, Value *Mask,
                              Value *PassThru,
                              ArrayRef<Value *> MDSources) const -> Value * {
  bool HvxHasPredLoad = HVC.HST.useHVXV62Ops();
  // Predicate is nullptr if not creating predicated load
  if (Predicate) {
    assert(!Predicate->getType()->isVectorTy() &&
           "Expectning scalar predicate");
    if (HVC.isFalse(Predicate))
      return UndefValue::get(ValTy);
    if (!HVC.isTrue(Predicate) && HvxHasPredLoad) {
      Value *Load = createPredicatedLoad(Builder, ValTy, Ptr, Predicate,
                                         Alignment, MDSources);
      return Builder.CreateSelect(Mask, Load, PassThru);
    }
    // Predicate == true here.
  }
  assert(!HVC.isUndef(Mask)); // Should this be allowed?
  if (HVC.isZero(Mask))
    return PassThru;
  if (HVC.isTrue(Mask))
    return createSimpleLoad(Builder, ValTy, Ptr, Alignment, MDSources);

  Instruction *Load = Builder.CreateMaskedLoad(ValTy, Ptr, Align(Alignment),
                                               Mask, PassThru, "mld");
  propagateMetadata(Load, MDSources);
  return Load;
}

auto AlignVectors::createSimpleLoad(IRBuilderBase &Builder, Type *ValTy,
                                    Value *Ptr, int Alignment,
                                    ArrayRef<Value *> MDSources) const
    -> Value * {
  Instruction *Load =
      Builder.CreateAlignedLoad(ValTy, Ptr, Align(Alignment), "ald");
  propagateMetadata(Load, MDSources);
  return Load;
}

auto AlignVectors::createPredicatedLoad(IRBuilderBase &Builder, Type *ValTy,
                                        Value *Ptr, Value *Predicate,
                                        int Alignment,
                                        ArrayRef<Value *> MDSources) const
    -> Value * {
  assert(HVC.HST.isTypeForHVX(ValTy) &&
         "Predicates 'scalar' vector loads not yet supported");
  assert(Predicate);
  assert(!Predicate->getType()->isVectorTy() && "Expectning scalar predicate");
  assert(HVC.getSizeOf(ValTy, HVC.Alloc) % Alignment == 0);
  if (HVC.isFalse(Predicate))
    return UndefValue::get(ValTy);
  if (HVC.isTrue(Predicate))
    return createSimpleLoad(Builder, ValTy, Ptr, Alignment, MDSources);

  auto V6_vL32b_pred_ai = HVC.HST.getIntrinsicId(Hexagon::V6_vL32b_pred_ai);
  // FIXME: This may not put the offset from Ptr into the vmem offset.
  return HVC.createHvxIntrinsic(Builder, V6_vL32b_pred_ai, ValTy,
                                {Predicate, Ptr, HVC.getConstInt(0)},
                                std::nullopt, MDSources);
}

auto AlignVectors::createStore(IRBuilderBase &Builder, Value *Val, Value *Ptr,
                               Value *Predicate, int Alignment, Value *Mask,
                               ArrayRef<Value *> MDSources) const -> Value * {
  if (HVC.isZero(Mask) || HVC.isUndef(Val) || HVC.isUndef(Mask))
    return UndefValue::get(Val->getType());
  assert(!Predicate || (!Predicate->getType()->isVectorTy() &&
                        "Expectning scalar predicate"));
  if (Predicate) {
    if (HVC.isFalse(Predicate))
      return UndefValue::get(Val->getType());
    if (HVC.isTrue(Predicate))
      Predicate = nullptr;
  }
  // Here both Predicate and Mask are true or unknown.

  if (HVC.isTrue(Mask)) {
    if (Predicate) { // Predicate unknown
      return createPredicatedStore(Builder, Val, Ptr, Predicate, Alignment,
                                   MDSources);
    }
    // Predicate is true:
    return createSimpleStore(Builder, Val, Ptr, Alignment, MDSources);
  }

  // Mask is unknown
  if (!Predicate) {
    Instruction *Store =
        Builder.CreateMaskedStore(Val, Ptr, Align(Alignment), Mask);
    propagateMetadata(Store, MDSources);
    return Store;
  }

  // Both Predicate and Mask are unknown.
  // Emulate masked store with predicated-load + mux + predicated-store.
  Value *PredLoad = createPredicatedLoad(Builder, Val->getType(), Ptr,
                                         Predicate, Alignment, MDSources);
  Value *Mux = Builder.CreateSelect(Mask, Val, PredLoad);
  return createPredicatedStore(Builder, Mux, Ptr, Predicate, Alignment,
                               MDSources);
}

auto AlignVectors::createSimpleStore(IRBuilderBase &Builder, Value *Val,
                                     Value *Ptr, int Alignment,
                                     ArrayRef<Value *> MDSources) const
    -> Value * {
  Instruction *Store = Builder.CreateAlignedStore(Val, Ptr, Align(Alignment));
  propagateMetadata(Store, MDSources);
  return Store;
}

auto AlignVectors::createPredicatedStore(IRBuilderBase &Builder, Value *Val,
                                         Value *Ptr, Value *Predicate,
                                         int Alignment,
                                         ArrayRef<Value *> MDSources) const
    -> Value * {
  assert(HVC.HST.isTypeForHVX(Val->getType()) &&
         "Predicates 'scalar' vector stores not yet supported");
  assert(Predicate);
  if (HVC.isFalse(Predicate))
    return UndefValue::get(Val->getType());
  if (HVC.isTrue(Predicate))
    return createSimpleStore(Builder, Val, Ptr, Alignment, MDSources);

  assert(HVC.getSizeOf(Val, HVC.Alloc) % Alignment == 0);
  auto V6_vS32b_pred_ai = HVC.HST.getIntrinsicId(Hexagon::V6_vS32b_pred_ai);
  // FIXME: This may not put the offset from Ptr into the vmem offset.
  return HVC.createHvxIntrinsic(Builder, V6_vS32b_pred_ai, nullptr,
                                {Predicate, Ptr, HVC.getConstInt(0), Val},
                                std::nullopt, MDSources);
}

auto AlignVectors::getUpwardDeps(Instruction *In, Instruction *Base) const
    -> DepList {
  BasicBlock *Parent = Base->getParent();
  assert(In->getParent() == Parent &&
         "Base and In should be in the same block");
  assert(Base->comesBefore(In) && "Base should come before In");

  DepList Deps;
  std::deque<Instruction *> WorkQ = {In};
  while (!WorkQ.empty()) {
    Instruction *D = WorkQ.front();
    WorkQ.pop_front();
    if (D != In)
      Deps.insert(D);
    for (Value *Op : D->operands()) {
      if (auto *I = dyn_cast<Instruction>(Op)) {
        if (I->getParent() == Parent && Base->comesBefore(I))
          WorkQ.push_back(I);
      }
    }
  }
  return Deps;
}

auto AlignVectors::createAddressGroups() -> bool {
  // An address group created here may contain instructions spanning
  // multiple basic blocks.
  AddrList WorkStack;

  auto findBaseAndOffset = [&](AddrInfo &AI) -> std::pair<Instruction *, int> {
    for (AddrInfo &W : WorkStack) {
      if (auto D = HVC.calculatePointerDifference(AI.Addr, W.Addr))
        return std::make_pair(W.Inst, *D);
    }
    return std::make_pair(nullptr, 0);
  };

  auto traverseBlock = [&](DomTreeNode *DomN, auto Visit) -> void {
    BasicBlock &Block = *DomN->getBlock();
    for (Instruction &I : Block) {
      auto AI = this->getAddrInfo(I); // Use this-> for gcc6.
      if (!AI)
        continue;
      auto F = findBaseAndOffset(*AI);
      Instruction *GroupInst;
      if (Instruction *BI = F.first) {
        AI->Offset = F.second;
        GroupInst = BI;
      } else {
        WorkStack.push_back(*AI);
        GroupInst = AI->Inst;
      }
      AddrGroups[GroupInst].push_back(*AI);
    }

    for (DomTreeNode *C : DomN->children())
      Visit(C, Visit);

    while (!WorkStack.empty() && WorkStack.back().Inst->getParent() == &Block)
      WorkStack.pop_back();
  };

  traverseBlock(HVC.DT.getRootNode(), traverseBlock);
  assert(WorkStack.empty());

  // AddrGroups are formed.

  // Remove groups of size 1.
  erase_if(AddrGroups, [](auto &G) { return G.second.size() == 1; });
  // Remove groups that don't use HVX types.
  erase_if(AddrGroups, [&](auto &G) {
    return llvm::none_of(
        G.second, [&](auto &I) { return HVC.HST.isTypeForHVX(I.ValTy); });
  });

  return !AddrGroups.empty();
}

auto AlignVectors::createLoadGroups(const AddrList &Group) const -> MoveList {
  // Form load groups.
  // To avoid complications with moving code across basic blocks, only form
  // groups that are contained within a single basic block.
  unsigned SizeLimit = VAGroupSizeLimit;
  if (SizeLimit == 0)
    return {};

  auto tryAddTo = [&](const AddrInfo &Info, MoveGroup &Move) {
    assert(!Move.Main.empty() && "Move group should have non-empty Main");
    if (Move.Main.size() >= SizeLimit)
      return false;
    // Don't mix HVX and non-HVX instructions.
    if (Move.IsHvx != isHvx(Info))
      return false;
    // Leading instruction in the load group.
    Instruction *Base = Move.Main.front();
    if (Base->getParent() != Info.Inst->getParent())
      return false;
    // Check if it's safe to move the load.
    if (!HVC.isSafeToMoveBeforeInBB(*Info.Inst, Base->getIterator()))
      return false;
    // And if it's safe to clone the dependencies.
    auto isSafeToCopyAtBase = [&](const Instruction *I) {
      return HVC.isSafeToMoveBeforeInBB(*I, Base->getIterator()) &&
             HVC.isSafeToClone(*I);
    };
    DepList Deps = getUpwardDeps(Info.Inst, Base);
    if (!llvm::all_of(Deps, isSafeToCopyAtBase))
      return false;

    Move.Main.push_back(Info.Inst);
    llvm::append_range(Move.Deps, Deps);
    return true;
  };

  MoveList LoadGroups;

  for (const AddrInfo &Info : Group) {
    if (!Info.Inst->mayReadFromMemory())
      continue;
    if (LoadGroups.empty() || !tryAddTo(Info, LoadGroups.back()))
      LoadGroups.emplace_back(Info, Group.front().Inst, isHvx(Info), true);
  }

  // Erase singleton groups.
  erase_if(LoadGroups, [](const MoveGroup &G) { return G.Main.size() <= 1; });

  // Erase HVX groups on targets < HvxV62 (due to lack of predicated loads).
  if (!HVC.HST.useHVXV62Ops())
    erase_if(LoadGroups, [](const MoveGroup &G) { return G.IsHvx; });

  return LoadGroups;
}

auto AlignVectors::createStoreGroups(const AddrList &Group) const -> MoveList {
  // Form store groups.
  // To avoid complications with moving code across basic blocks, only form
  // groups that are contained within a single basic block.
  unsigned SizeLimit = VAGroupSizeLimit;
  if (SizeLimit == 0)
    return {};

  auto tryAddTo = [&](const AddrInfo &Info, MoveGroup &Move) {
    assert(!Move.Main.empty() && "Move group should have non-empty Main");
    if (Move.Main.size() >= SizeLimit)
      return false;
    // For stores with return values we'd have to collect downward depenencies.
    // There are no such stores that we handle at the moment, so omit that.
    assert(Info.Inst->getType()->isVoidTy() &&
           "Not handling stores with return values");
    // Don't mix HVX and non-HVX instructions.
    if (Move.IsHvx != isHvx(Info))
      return false;
    // For stores we need to be careful whether it's safe to move them.
    // Stores that are otherwise safe to move together may not appear safe
    // to move over one another (i.e. isSafeToMoveBefore may return false).
    Instruction *Base = Move.Main.front();
    if (Base->getParent() != Info.Inst->getParent())
      return false;
    if (!HVC.isSafeToMoveBeforeInBB(*Info.Inst, Base->getIterator(), Move.Main))
      return false;
    Move.Main.push_back(Info.Inst);
    return true;
  };

  MoveList StoreGroups;

  for (auto I = Group.rbegin(), E = Group.rend(); I != E; ++I) {
    const AddrInfo &Info = *I;
    if (!Info.Inst->mayWriteToMemory())
      continue;
    if (StoreGroups.empty() || !tryAddTo(Info, StoreGroups.back()))
      StoreGroups.emplace_back(Info, Group.front().Inst, isHvx(Info), false);
  }

  // Erase singleton groups.
  erase_if(StoreGroups, [](const MoveGroup &G) { return G.Main.size() <= 1; });

  // Erase HVX groups on targets < HvxV62 (due to lack of predicated loads).
  if (!HVC.HST.useHVXV62Ops())
    erase_if(StoreGroups, [](const MoveGroup &G) { return G.IsHvx; });

  // Erase groups where every store is a full HVX vector. The reason is that
  // aligning predicated stores generates complex code that may be less
  // efficient than a sequence of unaligned vector stores.
  if (!VADoFullStores) {
    erase_if(StoreGroups, [this](const MoveGroup &G) {
      return G.IsHvx && llvm::all_of(G.Main, [this](Instruction *S) {
               auto MaybeInfo = this->getAddrInfo(*S);
               assert(MaybeInfo.has_value());
               return HVC.HST.isHVXVectorType(
                   EVT::getEVT(MaybeInfo->ValTy, false));
             });
    });
  }

  return StoreGroups;
}

auto AlignVectors::moveTogether(MoveGroup &Move) const -> bool {
  // Move all instructions to be adjacent.
  assert(!Move.Main.empty() && "Move group should have non-empty Main");
  Instruction *Where = Move.Main.front();

  if (Move.IsLoad) {
    // Move all the loads (and dependencies) to where the first load is.
    // Clone all deps to before Where, keeping order.
    Move.Clones = cloneBefore(Where, Move.Deps);
    // Move all main instructions to after Where, keeping order.
    ArrayRef<Instruction *> Main(Move.Main);
    for (Instruction *M : Main) {
      if (M != Where)
        M->moveAfter(Where);
      for (auto [Old, New] : Move.Clones)
        M->replaceUsesOfWith(Old, New);
      Where = M;
    }
    // Replace Deps with the clones.
    for (int i = 0, e = Move.Deps.size(); i != e; ++i)
      Move.Deps[i] = Move.Clones[Move.Deps[i]];
  } else {
    // Move all the stores to where the last store is.
    // NOTE: Deps are empty for "store" groups. If they need to be
    // non-empty, decide on the order.
    assert(Move.Deps.empty());
    // Move all main instructions to before Where, inverting order.
    ArrayRef<Instruction *> Main(Move.Main);
    for (Instruction *M : Main.drop_front(1)) {
      M->moveBefore(Where);
      Where = M;
    }
  }

  return Move.Main.size() + Move.Deps.size() > 1;
}

template <typename T>
auto AlignVectors::cloneBefore(Instruction *To, T &&Insts) const -> InstMap {
  InstMap Map;

  for (Instruction *I : Insts) {
    assert(HVC.isSafeToClone(*I));
    Instruction *C = I->clone();
    C->setName(Twine("c.") + I->getName() + ".");
    C->insertBefore(To);

    for (auto [Old, New] : Map)
      C->replaceUsesOfWith(Old, New);
    Map.insert(std::make_pair(I, C));
  }
  return Map;
}

auto AlignVectors::realignLoadGroup(IRBuilderBase &Builder,
                                    const ByteSpan &VSpan, int ScLen,
                                    Value *AlignVal, Value *AlignAddr) const
    -> void {
  LLVM_DEBUG(dbgs() << __func__ << "\n");

  Type *SecTy = HVC.getByteTy(ScLen);
  int NumSectors = (VSpan.extent() + ScLen - 1) / ScLen;
  bool DoAlign = !HVC.isZero(AlignVal);
  BasicBlock::iterator BasePos = Builder.GetInsertPoint();
  BasicBlock *BaseBlock = Builder.GetInsertBlock();

  ByteSpan ASpan;
  auto *True = HVC.getFullValue(HVC.getBoolTy(ScLen));
  auto *Undef = UndefValue::get(SecTy);

  // Created load does not have to be "Instruction" (e.g. "undef").
  SmallVector<Value *> Loads(NumSectors + DoAlign, nullptr);

  // We could create all of the aligned loads, and generate the valigns
  // at the location of the first load, but for large load groups, this
  // could create highly suboptimal code (there have been groups of 140+
  // loads in real code).
  // Instead, place the loads/valigns as close to the users as possible.
  // In any case we need to have a mapping from the blocks of VSpan (the
  // span covered by the pre-existing loads) to ASpan (the span covered
  // by the aligned loads). There is a small problem, though: ASpan needs
  // to have pointers to the loads/valigns, but we don't have these loads
  // because we don't know where to put them yet. We find out by creating
  // a section of ASpan that corresponds to values (blocks) from VSpan,
  // and checking where the new load should be placed. We need to attach
  // this location information to each block in ASpan somehow, so we put
  // distincts values for Seg.Val in each ASpan.Blocks[i], and use a map
  // to store the location for each Seg.Val.
  // The distinct values happen to be Blocks[i].Seg.Val = &Blocks[i],
  // which helps with printing ByteSpans without crashing when printing
  // Segments with these temporary identifiers in place of Val.

  // Populate the blocks first, to avoid reallocations of the vector
  // interfering with generating the placeholder addresses.
  for (int Index = 0; Index != NumSectors; ++Index)
    ASpan.Blocks.emplace_back(nullptr, ScLen, Index * ScLen);
  for (int Index = 0; Index != NumSectors; ++Index) {
    ASpan.Blocks[Index].Seg.Val =
        reinterpret_cast<Value *>(&ASpan.Blocks[Index]);
  }

  // Multiple values from VSpan can map to the same value in ASpan. Since we
  // try to create loads lazily, we need to find the earliest use for each
  // value from ASpan.
  DenseMap<void *, Instruction *> EarliestUser;
  auto isEarlier = [](Instruction *A, Instruction *B) {
    if (B == nullptr)
      return true;
    if (A == nullptr)
      return false;
    assert(A->getParent() == B->getParent());
    return A->comesBefore(B);
  };
  auto earliestUser = [&](const auto &Uses) {
    Instruction *User = nullptr;
    for (const Use &U : Uses) {
      auto *I = dyn_cast<Instruction>(U.getUser());
      assert(I != nullptr && "Load used in a non-instruction?");
      // Make sure we only consider users in this block, but we need
      // to remember if there were users outside the block too. This is
      // because if no users are found, aligned loads will not be created.
      if (I->getParent() == BaseBlock) {
        if (!isa<PHINode>(I))
          User = std::min(User, I, isEarlier);
      } else {
        User = std::min(User, BaseBlock->getTerminator(), isEarlier);
      }
    }
    return User;
  };

  for (const ByteSpan::Block &B : VSpan) {
    ByteSpan ASection = ASpan.section(B.Pos, B.Seg.Size);
    for (const ByteSpan::Block &S : ASection) {
      EarliestUser[S.Seg.Val] = std::min(
          EarliestUser[S.Seg.Val], earliestUser(B.Seg.Val->uses()), isEarlier);
    }
  }

  LLVM_DEBUG({
    dbgs() << "ASpan:\n" << ASpan << '\n';
    dbgs() << "Earliest users of ASpan:\n";
    for (auto &[Val, User] : EarliestUser) {
      dbgs() << Val << "\n ->" << *User << '\n';
    }
  });

  auto createLoad = [&](IRBuilderBase &Builder, const ByteSpan &VSpan,
                        int Index, bool MakePred) {
    Value *Ptr =
        createAdjustedPointer(Builder, AlignAddr, SecTy, Index * ScLen);
    Value *Predicate =
        MakePred ? makeTestIfUnaligned(Builder, AlignVal, ScLen) : nullptr;

    // If vector shifting is potentially needed, accumulate metadata
    // from source sections of twice the load width.
    int Start = (Index - DoAlign) * ScLen;
    int Width = (1 + DoAlign) * ScLen;
    return this->createLoad(Builder, SecTy, Ptr, Predicate, ScLen, True, Undef,
                            VSpan.section(Start, Width).values());
  };

  auto moveBefore = [this](Instruction *In, Instruction *To) {
    // Move In and its upward dependencies to before To.
    assert(In->getParent() == To->getParent());
    DepList Deps = getUpwardDeps(In, To);
    In->moveBefore(To);
    // DepList is sorted with respect to positions in the basic block.
    InstMap Map = cloneBefore(In, Deps);
    for (auto [Old, New] : Map)
      In->replaceUsesOfWith(Old, New);
  };

  // Generate necessary loads at appropriate locations.
  LLVM_DEBUG(dbgs() << "Creating loads for ASpan sectors\n");
  for (int Index = 0; Index != NumSectors + 1; ++Index) {
    // In ASpan, each block will be either a single aligned load, or a
    // valign of a pair of loads. In the latter case, an aligned load j
    // will belong to the current valign, and the one in the previous
    // block (for j > 0).
    // Place the load at a location which will dominate the valign, assuming
    // the valign will be placed right before the earliest user.
    Instruction *PrevAt =
        DoAlign && Index > 0 ? EarliestUser[&ASpan[Index - 1]] : nullptr;
    Instruction *ThisAt =
        Index < NumSectors ? EarliestUser[&ASpan[Index]] : nullptr;
    if (auto *Where = std::min(PrevAt, ThisAt, isEarlier)) {
      Builder.SetInsertPoint(Where);
      Loads[Index] =
          createLoad(Builder, VSpan, Index, DoAlign && Index == NumSectors);
      // We know it's safe to put the load at BasePos, but we'd prefer to put
      // it at "Where". To see if the load is safe to be placed at Where, put
      // it there first and then check if it's safe to move it to BasePos.
      // If not, then the load needs to be placed at BasePos.
      // We can't do this check proactively because we need the load to exist
      // in order to check legality.
      if (auto *Load = dyn_cast<Instruction>(Loads[Index])) {
        if (!HVC.isSafeToMoveBeforeInBB(*Load, BasePos))
          moveBefore(Load, &*BasePos);
      }
      LLVM_DEBUG(dbgs() << "Loads[" << Index << "]:" << *Loads[Index] << '\n');
    }
  }

  // Generate valigns if needed, and fill in proper values in ASpan
  LLVM_DEBUG(dbgs() << "Creating values for ASpan sectors\n");
  for (int Index = 0; Index != NumSectors; ++Index) {
    ASpan[Index].Seg.Val = nullptr;
    if (auto *Where = EarliestUser[&ASpan[Index]]) {
      Builder.SetInsertPoint(Where);
      Value *Val = Loads[Index];
      assert(Val != nullptr);
      if (DoAlign) {
        Value *NextLoad = Loads[Index + 1];
        assert(NextLoad != nullptr);
        Val = HVC.vralignb(Builder, Val, NextLoad, AlignVal);
      }
      ASpan[Index].Seg.Val = Val;
      LLVM_DEBUG(dbgs() << "ASpan[" << Index << "]:" << *Val << '\n');
    }
  }

  for (const ByteSpan::Block &B : VSpan) {
    ByteSpan ASection = ASpan.section(B.Pos, B.Seg.Size).shift(-B.Pos);
    Value *Accum = UndefValue::get(HVC.getByteTy(B.Seg.Size));
    Builder.SetInsertPoint(cast<Instruction>(B.Seg.Val));

    // We're generating a reduction, where each instruction depends on
    // the previous one, so we need to order them according to the position
    // of their inputs in the code.
    std::vector<ByteSpan::Block *> ABlocks;
    for (ByteSpan::Block &S : ASection) {
      if (S.Seg.Val != nullptr)
        ABlocks.push_back(&S);
    }
    llvm::sort(ABlocks,
               [&](const ByteSpan::Block *A, const ByteSpan::Block *B) {
                 return isEarlier(cast<Instruction>(A->Seg.Val),
                                  cast<Instruction>(B->Seg.Val));
               });
    for (ByteSpan::Block *S : ABlocks) {
      // The processing of the data loaded by the aligned loads
      // needs to be inserted after the data is available.
      Instruction *SegI = cast<Instruction>(S->Seg.Val);
      Builder.SetInsertPoint(&*std::next(SegI->getIterator()));
      Value *Pay = HVC.vbytes(Builder, getPayload(S->Seg.Val));
      Accum =
          HVC.insertb(Builder, Accum, Pay, S->Seg.Start, S->Seg.Size, S->Pos);
    }
    // Instead of casting everything to bytes for the vselect, cast to the
    // original value type. This will avoid complications with casting masks.
    // For example, in cases when the original mask applied to i32, it could
    // be converted to a mask applicable to i8 via pred_typecast intrinsic,
    // but if the mask is not exactly of HVX length, extra handling would be
    // needed to make it work.
    Type *ValTy = getPayload(B.Seg.Val)->getType();
    Value *Cast = Builder.CreateBitCast(Accum, ValTy, "cst");
    Value *Sel = Builder.CreateSelect(getMask(B.Seg.Val), Cast,
                                      getPassThrough(B.Seg.Val), "sel");
    B.Seg.Val->replaceAllUsesWith(Sel);
  }
}

auto AlignVectors::realignStoreGroup(IRBuilderBase &Builder,
                                     const ByteSpan &VSpan, int ScLen,
                                     Value *AlignVal, Value *AlignAddr) const
    -> void {
  LLVM_DEBUG(dbgs() << __func__ << "\n");

  Type *SecTy = HVC.getByteTy(ScLen);
  int NumSectors = (VSpan.extent() + ScLen - 1) / ScLen;
  bool DoAlign = !HVC.isZero(AlignVal);

  // Stores.
  ByteSpan ASpanV, ASpanM;

  // Return a vector value corresponding to the input value Val:
  // either <1 x Val> for scalar Val, or Val itself for vector Val.
  auto MakeVec = [](IRBuilderBase &Builder, Value *Val) -> Value * {
    Type *Ty = Val->getType();
    if (Ty->isVectorTy())
      return Val;
    auto *VecTy = VectorType::get(Ty, 1, /*Scalable=*/false);
    return Builder.CreateBitCast(Val, VecTy, "cst");
  };

  // Create an extra "undef" sector at the beginning and at the end.
  // They will be used as the left/right filler in the vlalign step.
  for (int Index = (DoAlign ? -1 : 0); Index != NumSectors + DoAlign; ++Index) {
    // For stores, the size of each section is an aligned vector length.
    // Adjust the store offsets relative to the section start offset.
    ByteSpan VSection =
        VSpan.section(Index * ScLen, ScLen).shift(-Index * ScLen);
    Value *Undef = UndefValue::get(SecTy);
    Value *Zero = HVC.getNullValue(SecTy);
    Value *AccumV = Undef;
    Value *AccumM = Zero;
    for (ByteSpan::Block &S : VSection) {
      Value *Pay = getPayload(S.Seg.Val);
      Value *Mask = HVC.rescale(Builder, MakeVec(Builder, getMask(S.Seg.Val)),
                                Pay->getType(), HVC.getByteTy());
      Value *PartM = HVC.insertb(Builder, Zero, HVC.vbytes(Builder, Mask),
                                 S.Seg.Start, S.Seg.Size, S.Pos);
      AccumM = Builder.CreateOr(AccumM, PartM);

      Value *PartV = HVC.insertb(Builder, Undef, HVC.vbytes(Builder, Pay),
                                 S.Seg.Start, S.Seg.Size, S.Pos);

      AccumV = Builder.CreateSelect(
          Builder.CreateICmp(CmpInst::ICMP_NE, PartM, Zero), PartV, AccumV);
    }
    ASpanV.Blocks.emplace_back(AccumV, ScLen, Index * ScLen);
    ASpanM.Blocks.emplace_back(AccumM, ScLen, Index * ScLen);
  }

  LLVM_DEBUG({
    dbgs() << "ASpanV before vlalign:\n" << ASpanV << '\n';
    dbgs() << "ASpanM before vlalign:\n" << ASpanM << '\n';
  });

  // vlalign
  if (DoAlign) {
    for (int Index = 1; Index != NumSectors + 2; ++Index) {
      Value *PrevV = ASpanV[Index - 1].Seg.Val, *ThisV = ASpanV[Index].Seg.Val;
      Value *PrevM = ASpanM[Index - 1].Seg.Val, *ThisM = ASpanM[Index].Seg.Val;
      assert(isSectorTy(PrevV->getType()) && isSectorTy(PrevM->getType()));
      ASpanV[Index - 1].Seg.Val = HVC.vlalignb(Builder, PrevV, ThisV, AlignVal);
      ASpanM[Index - 1].Seg.Val = HVC.vlalignb(Builder, PrevM, ThisM, AlignVal);
    }
  }

  LLVM_DEBUG({
    dbgs() << "ASpanV after vlalign:\n" << ASpanV << '\n';
    dbgs() << "ASpanM after vlalign:\n" << ASpanM << '\n';
  });

  auto createStore = [&](IRBuilderBase &Builder, const ByteSpan &ASpanV,
                         const ByteSpan &ASpanM, int Index, bool MakePred) {
    Value *Val = ASpanV[Index].Seg.Val;
    Value *Mask = ASpanM[Index].Seg.Val; // bytes
    if (HVC.isUndef(Val) || HVC.isZero(Mask))
      return;
    Value *Ptr =
        createAdjustedPointer(Builder, AlignAddr, SecTy, Index * ScLen);
    Value *Predicate =
        MakePred ? makeTestIfUnaligned(Builder, AlignVal, ScLen) : nullptr;

    // If vector shifting is potentially needed, accumulate metadata
    // from source sections of twice the store width.
    int Start = (Index - DoAlign) * ScLen;
    int Width = (1 + DoAlign) * ScLen;
    this->createStore(Builder, Val, Ptr, Predicate, ScLen,
                      HVC.vlsb(Builder, Mask),
                      VSpan.section(Start, Width).values());
  };

  for (int Index = 0; Index != NumSectors + DoAlign; ++Index) {
    createStore(Builder, ASpanV, ASpanM, Index, DoAlign && Index == NumSectors);
  }
}

auto AlignVectors::realignGroup(const MoveGroup &Move) const -> bool {
  LLVM_DEBUG(dbgs() << "Realigning group:\n" << Move << '\n');

  // TODO: Needs support for masked loads/stores of "scalar" vectors.
  if (!Move.IsHvx)
    return false;

  // Return the element with the maximum alignment from Range,
  // where GetValue obtains the value to compare from an element.
  auto getMaxOf = [](auto Range, auto GetValue) {
    return *llvm::max_element(Range, [&GetValue](auto &A, auto &B) {
      return GetValue(A) < GetValue(B);
    });
  };

  const AddrList &BaseInfos = AddrGroups.at(Move.Base);

  // Conceptually, there is a vector of N bytes covering the addresses
  // starting from the minimum offset (i.e. Base.Addr+Start). This vector
  // represents a contiguous memory region that spans all accessed memory
  // locations.
  // The correspondence between loaded or stored values will be expressed
  // in terms of this vector. For example, the 0th element of the vector
  // from the Base address info will start at byte Start from the beginning
  // of this conceptual vector.
  //
  // This vector will be loaded/stored starting at the nearest down-aligned
  // address and the amount od the down-alignment will be AlignVal:
  //   valign(load_vector(align_down(Base+Start)), AlignVal)

  std::set<Instruction *> TestSet(Move.Main.begin(), Move.Main.end());
  AddrList MoveInfos;
  llvm::copy_if(
      BaseInfos, std::back_inserter(MoveInfos),
      [&TestSet](const AddrInfo &AI) { return TestSet.count(AI.Inst); });

  // Maximum alignment present in the whole address group.
  const AddrInfo &WithMaxAlign =
      getMaxOf(MoveInfos, [](const AddrInfo &AI) { return AI.HaveAlign; });
  Align MaxGiven = WithMaxAlign.HaveAlign;

  // Minimum alignment present in the move address group.
  const AddrInfo &WithMinOffset =
      getMaxOf(MoveInfos, [](const AddrInfo &AI) { return -AI.Offset; });

  const AddrInfo &WithMaxNeeded =
      getMaxOf(MoveInfos, [](const AddrInfo &AI) { return AI.NeedAlign; });
  Align MinNeeded = WithMaxNeeded.NeedAlign;

  // Set the builder's insertion point right before the load group, or
  // immediately after the store group. (Instructions in a store group are
  // listed in reverse order.)
  Instruction *InsertAt = Move.Main.front();
  if (!Move.IsLoad) {
    // There should be a terminator (which store isn't, but check anyways).
    assert(InsertAt->getIterator() != InsertAt->getParent()->end());
    InsertAt = &*std::next(InsertAt->getIterator());
  }

  IRBuilder Builder(InsertAt->getParent(), InsertAt->getIterator(),
                    InstSimplifyFolder(HVC.DL));
  Value *AlignAddr = nullptr; // Actual aligned address.
  Value *AlignVal = nullptr;  // Right-shift amount (for valign).

  if (MinNeeded <= MaxGiven) {
    int Start = WithMinOffset.Offset;
    int OffAtMax = WithMaxAlign.Offset;
    // Shift the offset of the maximally aligned instruction (OffAtMax)
    // back by just enough multiples of the required alignment to cover the
    // distance from Start to OffAtMax.
    // Calculate the address adjustment amount based on the address with the
    // maximum alignment. This is to allow a simple gep instruction instead
    // of potential bitcasts to i8*.
    int Adjust = -alignTo(OffAtMax - Start, MinNeeded.value());
    AlignAddr = createAdjustedPointer(Builder, WithMaxAlign.Addr,
                                      WithMaxAlign.ValTy, Adjust, Move.Clones);
    int Diff = Start - (OffAtMax + Adjust);
    AlignVal = HVC.getConstInt(Diff);
    assert(Diff >= 0);
    assert(static_cast<decltype(MinNeeded.value())>(Diff) < MinNeeded.value());
  } else {
    // WithMinOffset is the lowest address in the group,
    //   WithMinOffset.Addr = Base+Start.
    // Align instructions for both HVX (V6_valign) and scalar (S2_valignrb)
    // mask off unnecessary bits, so it's ok to just the original pointer as
    // the alignment amount.
    // Do an explicit down-alignment of the address to avoid creating an
    // aligned instruction with an address that is not really aligned.
    AlignAddr =
        createAlignedPointer(Builder, WithMinOffset.Addr, WithMinOffset.ValTy,
                             MinNeeded.value(), Move.Clones);
    AlignVal =
        Builder.CreatePtrToInt(WithMinOffset.Addr, HVC.getIntTy(), "pti");
    if (auto *I = dyn_cast<Instruction>(AlignVal)) {
      for (auto [Old, New] : Move.Clones)
        I->replaceUsesOfWith(Old, New);
    }
  }

  ByteSpan VSpan;
  for (const AddrInfo &AI : MoveInfos) {
    VSpan.Blocks.emplace_back(AI.Inst, HVC.getSizeOf(AI.ValTy),
                              AI.Offset - WithMinOffset.Offset);
  }

  // The aligned loads/stores will use blocks that are either scalars,
  // or HVX vectors. Let "sector" be the unified term for such a block.
  // blend(scalar, vector) -> sector...
  int ScLen = Move.IsHvx ? HVC.HST.getVectorLength()
                         : std::max<int>(MinNeeded.value(), 4);
  assert(!Move.IsHvx || ScLen == 64 || ScLen == 128);
  assert(Move.IsHvx || ScLen == 4 || ScLen == 8);

  LLVM_DEBUG({
    dbgs() << "ScLen:  " << ScLen << "\n";
    dbgs() << "AlignVal:" << *AlignVal << "\n";
    dbgs() << "AlignAddr:" << *AlignAddr << "\n";
    dbgs() << "VSpan:\n" << VSpan << '\n';
  });

  if (Move.IsLoad)
    realignLoadGroup(Builder, VSpan, ScLen, AlignVal, AlignAddr);
  else
    realignStoreGroup(Builder, VSpan, ScLen, AlignVal, AlignAddr);

  for (auto *Inst : Move.Main)
    Inst->eraseFromParent();

  return true;
}

auto AlignVectors::makeTestIfUnaligned(IRBuilderBase &Builder, Value *AlignVal,
                                       int Alignment) const -> Value * {
  auto *AlignTy = AlignVal->getType();
  Value *And = Builder.CreateAnd(
      AlignVal, ConstantInt::get(AlignTy, Alignment - 1), "and");
  Value *Zero = ConstantInt::get(AlignTy, 0);
  return Builder.CreateICmpNE(And, Zero, "isz");
}

auto AlignVectors::isSectorTy(Type *Ty) const -> bool {
  if (!HVC.isByteVecTy(Ty))
    return false;
  int Size = HVC.getSizeOf(Ty);
  if (HVC.HST.isTypeForHVX(Ty))
    return Size == static_cast<int>(HVC.HST.getVectorLength());
  return Size == 4 || Size == 8;
}

auto AlignVectors::run() -> bool {
  LLVM_DEBUG(dbgs() << "Running HVC::AlignVectors on " << HVC.F.getName()
                    << '\n');
  if (!createAddressGroups())
    return false;

  LLVM_DEBUG({
    dbgs() << "Address groups(" << AddrGroups.size() << "):\n";
    for (auto &[In, AL] : AddrGroups) {
      for (const AddrInfo &AI : AL)
        dbgs() << "---\n" << AI << '\n';
    }
  });

  bool Changed = false;
  MoveList LoadGroups, StoreGroups;

  for (auto &G : AddrGroups) {
    llvm::append_range(LoadGroups, createLoadGroups(G.second));
    llvm::append_range(StoreGroups, createStoreGroups(G.second));
  }

  LLVM_DEBUG({
    dbgs() << "\nLoad groups(" << LoadGroups.size() << "):\n";
    for (const MoveGroup &G : LoadGroups)
      dbgs() << G << "\n";
    dbgs() << "Store groups(" << StoreGroups.size() << "):\n";
    for (const MoveGroup &G : StoreGroups)
      dbgs() << G << "\n";
  });

  // Cumulative limit on the number of groups.
  unsigned CountLimit = VAGroupCountLimit;
  if (CountLimit == 0)
    return false;

  if (LoadGroups.size() > CountLimit) {
    LoadGroups.resize(CountLimit);
    StoreGroups.clear();
  } else {
    unsigned StoreLimit = CountLimit - LoadGroups.size();
    if (StoreGroups.size() > StoreLimit)
      StoreGroups.resize(StoreLimit);
  }

  for (auto &M : LoadGroups)
    Changed |= moveTogether(M);
  for (auto &M : StoreGroups)
    Changed |= moveTogether(M);

  LLVM_DEBUG(dbgs() << "After moveTogether:\n" << HVC.F);

  for (auto &M : LoadGroups)
    Changed |= realignGroup(M);
  for (auto &M : StoreGroups)
    Changed |= realignGroup(M);

  return Changed;
}

// --- End AlignVectors

// --- Begin HvxIdioms

auto HvxIdioms::getNumSignificantBits(Value *V, Instruction *In) const
    -> std::pair<unsigned, Signedness> {
  unsigned Bits = HVC.getNumSignificantBits(V, In);
  // The significant bits are calculated including the sign bit. This may
  // add an extra bit for zero-extended values, e.g. (zext i32 to i64) may
  // result in 33 significant bits. To avoid extra words, skip the extra
  // sign bit, but keep information that the value is to be treated as
  // unsigned.
  KnownBits Known = HVC.getKnownBits(V, In);
  Signedness Sign = Signed;
  unsigned NumToTest = 0; // Number of bits used in test for unsignedness.
  if (isPowerOf2_32(Bits))
    NumToTest = Bits;
  else if (Bits > 1 && isPowerOf2_32(Bits - 1))
    NumToTest = Bits - 1;

  if (NumToTest != 0 && Known.Zero.ashr(NumToTest).isAllOnes()) {
    Sign = Unsigned;
    Bits = NumToTest;
  }

  // If the top bit of the nearest power-of-2 is zero, this value is
  // positive. It could be treated as either signed or unsigned.
  if (unsigned Pow2 = PowerOf2Ceil(Bits); Pow2 != Bits) {
    if (Known.Zero.ashr(Pow2 - 1).isAllOnes())
      Sign = Positive;
  }
  return {Bits, Sign};
}

auto HvxIdioms::canonSgn(SValue X, SValue Y) const
    -> std::pair<SValue, SValue> {
  // Canonicalize the signedness of X and Y, so that the result is one of:
  //   S, S
  //   U/P, S
  //   U/P, U/P
  if (X.Sgn == Signed && Y.Sgn != Signed)
    std::swap(X, Y);
  return {X, Y};
}

// Match
//   (X * Y) [>> N], or
//   ((X * Y) + (1 << M)) >> N
auto HvxIdioms::matchFxpMul(Instruction &In) const -> std::optional<FxpOp> {
  using namespace PatternMatch;
  auto *Ty = In.getType();

  if (!Ty->isVectorTy() || !Ty->getScalarType()->isIntegerTy())
    return std::nullopt;

  unsigned Width = cast<IntegerType>(Ty->getScalarType())->getBitWidth();

  FxpOp Op;
  Value *Exp = &In;

  // Fixed-point multiplication is always shifted right (except when the
  // fraction is 0 bits).
  auto m_Shr = [](auto &&V, auto &&S) {
    return m_CombineOr(m_LShr(V, S), m_AShr(V, S));
  };

  const APInt *Qn = nullptr;
  if (Value * T; match(Exp, m_Shr(m_Value(T), m_APInt(Qn)))) {
    Op.Frac = Qn->getZExtValue();
    Exp = T;
  } else {
    Op.Frac = 0;
  }

  if (Op.Frac > Width)
    return std::nullopt;

  // Check if there is rounding added.
  const APInt *C = nullptr;
  if (Value * T; Op.Frac > 0 && match(Exp, m_Add(m_Value(T), m_APInt(C)))) {
    uint64_t CV = C->getZExtValue();
    if (CV != 0 && !isPowerOf2_64(CV))
      return std::nullopt;
    if (CV != 0)
      Op.RoundAt = Log2_64(CV);
    Exp = T;
  }

  // Check if the rest is a multiplication.
  if (match(Exp, m_Mul(m_Value(Op.X.Val), m_Value(Op.Y.Val)))) {
    Op.Opcode = Instruction::Mul;
    // FIXME: The information below is recomputed.
    Op.X.Sgn = getNumSignificantBits(Op.X.Val, &In).second;
    Op.Y.Sgn = getNumSignificantBits(Op.Y.Val, &In).second;
    Op.ResTy = cast<VectorType>(Ty);
    return Op;
  }

  return std::nullopt;
}

auto HvxIdioms::processFxpMul(Instruction &In, const FxpOp &Op) const
    -> Value * {
  assert(Op.X.Val->getType() == Op.Y.Val->getType());

  auto *VecTy = dyn_cast<VectorType>(Op.X.Val->getType());
  if (VecTy == nullptr)
    return nullptr;
  auto *ElemTy = cast<IntegerType>(VecTy->getElementType());
  unsigned ElemWidth = ElemTy->getBitWidth();

  // TODO: This can be relaxed after legalization is done pre-isel.
  if ((HVC.length(VecTy) * ElemWidth) % (8 * HVC.HST.getVectorLength()) != 0)
    return nullptr;

  // There are no special intrinsics that should be used for multiplying
  // signed 8-bit values, so just skip them. Normal codegen should handle
  // this just fine.
  if (ElemWidth <= 8)
    return nullptr;
  // Similarly, if this is just a multiplication that can be handled without
  // intervention, then leave it alone.
  if (ElemWidth <= 32 && Op.Frac == 0)
    return nullptr;

  auto [BitsX, SignX] = getNumSignificantBits(Op.X.Val, &In);
  auto [BitsY, SignY] = getNumSignificantBits(Op.Y.Val, &In);

  // TODO: Add multiplication of vectors by scalar registers (up to 4 bytes).

  Value *X = Op.X.Val, *Y = Op.Y.Val;
  IRBuilder Builder(In.getParent(), In.getIterator(),
                    InstSimplifyFolder(HVC.DL));

  auto roundUpWidth = [](unsigned Width) -> unsigned {
    if (Width <= 32 && !isPowerOf2_32(Width)) {
      // If the element width is not a power of 2, round it up
      // to the next one. Do this for widths not exceeding 32.
      return PowerOf2Ceil(Width);
    }
    if (Width > 32 && Width % 32 != 0) {
      // For wider elements, round it up to the multiple of 32.
      return alignTo(Width, 32u);
    }
    return Width;
  };

  BitsX = roundUpWidth(BitsX);
  BitsY = roundUpWidth(BitsY);

  // For elementwise multiplication vectors must have the same lengths, so
  // resize the elements of both inputs to the same width, the max of the
  // calculated significant bits.
  unsigned Width = std::max(BitsX, BitsY);

  auto *ResizeTy = VectorType::get(HVC.getIntTy(Width), VecTy);
  if (Width < ElemWidth) {
    X = Builder.CreateTrunc(X, ResizeTy, "trn");
    Y = Builder.CreateTrunc(Y, ResizeTy, "trn");
  } else if (Width > ElemWidth) {
    X = SignX == Signed ? Builder.CreateSExt(X, ResizeTy, "sxt")
                        : Builder.CreateZExt(X, ResizeTy, "zxt");
    Y = SignY == Signed ? Builder.CreateSExt(Y, ResizeTy, "sxt")
                        : Builder.CreateZExt(Y, ResizeTy, "zxt");
  };

  assert(X->getType() == Y->getType() && X->getType() == ResizeTy);

  unsigned VecLen = HVC.length(ResizeTy);
  unsigned ChopLen = (8 * HVC.HST.getVectorLength()) / std::min(Width, 32u);

  SmallVector<Value *> Results;
  FxpOp ChopOp = Op;
  ChopOp.ResTy = VectorType::get(Op.ResTy->getElementType(), ChopLen, false);

  for (unsigned V = 0; V != VecLen / ChopLen; ++V) {
    ChopOp.X.Val = HVC.subvector(Builder, X, V * ChopLen, ChopLen);
    ChopOp.Y.Val = HVC.subvector(Builder, Y, V * ChopLen, ChopLen);
    Results.push_back(processFxpMulChopped(Builder, In, ChopOp));
    if (Results.back() == nullptr)
      break;
  }

  if (Results.empty() || Results.back() == nullptr)
    return nullptr;

  Value *Cat = HVC.concat(Builder, Results);
  Value *Ext = SignX == Signed || SignY == Signed
                   ? Builder.CreateSExt(Cat, VecTy, "sxt")
                   : Builder.CreateZExt(Cat, VecTy, "zxt");
  return Ext;
}

auto HvxIdioms::processFxpMulChopped(IRBuilderBase &Builder, Instruction &In,
                                     const FxpOp &Op) const -> Value * {
  assert(Op.X.Val->getType() == Op.Y.Val->getType());
  auto *InpTy = cast<VectorType>(Op.X.Val->getType());
  unsigned Width = InpTy->getScalarSizeInBits();
  bool Rounding = Op.RoundAt.has_value();

  if (!Op.RoundAt || *Op.RoundAt == Op.Frac - 1) {
    // The fixed-point intrinsics do signed multiplication.
    if (Width == Op.Frac + 1 && Op.X.Sgn != Unsigned && Op.Y.Sgn != Unsigned) {
      Value *QMul = nullptr;
      if (Width == 16) {
        QMul = createMulQ15(Builder, Op.X, Op.Y, Rounding);
      } else if (Width == 32) {
        QMul = createMulQ31(Builder, Op.X, Op.Y, Rounding);
      }
      if (QMul != nullptr)
        return QMul;
    }
  }

  assert(Width >= 32 || isPowerOf2_32(Width)); // Width <= 32 => Width is 2^n
  assert(Width < 32 || Width % 32 == 0);       // Width > 32 => Width is 32*k

  // If Width < 32, then it should really be 16.
  if (Width < 32) {
    if (Width < 16)
      return nullptr;
    // Getting here with Op.Frac == 0 isn't wrong, but suboptimal: here we
    // generate a full precision products, which is unnecessary if there is
    // no shift.
    assert(Width == 16);
    assert(Op.Frac != 0 && "Unshifted mul should have been skipped");
    if (Op.Frac == 16) {
      // Multiply high
      if (Value *MulH = createMulH16(Builder, Op.X, Op.Y))
        return MulH;
    }
    // Do full-precision multiply and shift.
    Value *Prod32 = createMul16(Builder, Op.X, Op.Y);
    if (Rounding) {
      Value *RoundVal = HVC.getConstSplat(Prod32->getType(), 1 << *Op.RoundAt);
      Prod32 = Builder.CreateAdd(Prod32, RoundVal, "add");
    }

    Value *ShiftAmt = HVC.getConstSplat(Prod32->getType(), Op.Frac);
    Value *Shifted = Op.X.Sgn == Signed || Op.Y.Sgn == Signed
                         ? Builder.CreateAShr(Prod32, ShiftAmt, "asr")
                         : Builder.CreateLShr(Prod32, ShiftAmt, "lsr");
    return Builder.CreateTrunc(Shifted, InpTy, "trn");
  }

  // Width >= 32

  // Break up the arguments Op.X and Op.Y into vectors of smaller widths
  // in preparation of doing the multiplication by 32-bit parts.
  auto WordX = HVC.splitVectorElements(Builder, Op.X.Val, /*ToWidth=*/32);
  auto WordY = HVC.splitVectorElements(Builder, Op.Y.Val, /*ToWidth=*/32);
  auto WordP = createMulLong(Builder, WordX, Op.X.Sgn, WordY, Op.Y.Sgn);

  auto *HvxWordTy = cast<VectorType>(WordP.front()->getType());

  // Add the optional rounding to the proper word.
  if (Op.RoundAt.has_value()) {
    Value *Zero = HVC.getNullValue(WordX[0]->getType());
    SmallVector<Value *> RoundV(WordP.size(), Zero);
    RoundV[*Op.RoundAt / 32] =
        HVC.getConstSplat(HvxWordTy, 1 << (*Op.RoundAt % 32));
    WordP = createAddLong(Builder, WordP, RoundV);
  }

  // createRightShiftLong?

  // Shift all products right by Op.Frac.
  unsigned SkipWords = Op.Frac / 32;
  Constant *ShiftAmt = HVC.getConstSplat(HvxWordTy, Op.Frac % 32);

  for (int Dst = 0, End = WordP.size() - SkipWords; Dst != End; ++Dst) {
    int Src = Dst + SkipWords;
    Value *Lo = WordP[Src];
    if (Src + 1 < End) {
      Value *Hi = WordP[Src + 1];
      WordP[Dst] = Builder.CreateIntrinsic(HvxWordTy, Intrinsic::fshr,
                                           {Hi, Lo, ShiftAmt},
                                           /*FMFSource*/ nullptr, "int");
    } else {
      // The shift of the most significant word.
      WordP[Dst] = Builder.CreateAShr(Lo, ShiftAmt, "asr");
    }
  }
  if (SkipWords != 0)
    WordP.resize(WordP.size() - SkipWords);

  return HVC.joinVectorElements(Builder, WordP, Op.ResTy);
}

auto HvxIdioms::createMulQ15(IRBuilderBase &Builder, SValue X, SValue Y,
                             bool Rounding) const -> Value * {
  assert(X.Val->getType() == Y.Val->getType());
  assert(X.Val->getType()->getScalarType() == HVC.getIntTy(16));
  assert(HVC.HST.isHVXVectorType(EVT::getEVT(X.Val->getType(), false)));

  // There is no non-rounding intrinsic for i16.
  if (!Rounding || X.Sgn == Unsigned || Y.Sgn == Unsigned)
    return nullptr;

  auto V6_vmpyhvsrs = HVC.HST.getIntrinsicId(Hexagon::V6_vmpyhvsrs);
  return HVC.createHvxIntrinsic(Builder, V6_vmpyhvsrs, X.Val->getType(),
                                {X.Val, Y.Val});
}

auto HvxIdioms::createMulQ31(IRBuilderBase &Builder, SValue X, SValue Y,
                             bool Rounding) const -> Value * {
  Type *InpTy = X.Val->getType();
  assert(InpTy == Y.Val->getType());
  assert(InpTy->getScalarType() == HVC.getIntTy(32));
  assert(HVC.HST.isHVXVectorType(EVT::getEVT(InpTy, false)));

  if (X.Sgn == Unsigned || Y.Sgn == Unsigned)
    return nullptr;

  auto V6_vmpyewuh = HVC.HST.getIntrinsicId(Hexagon::V6_vmpyewuh);
  auto V6_vmpyo_acc = Rounding
                          ? HVC.HST.getIntrinsicId(Hexagon::V6_vmpyowh_rnd_sacc)
                          : HVC.HST.getIntrinsicId(Hexagon::V6_vmpyowh_sacc);
  Value *V1 =
      HVC.createHvxIntrinsic(Builder, V6_vmpyewuh, InpTy, {X.Val, Y.Val});
  return HVC.createHvxIntrinsic(Builder, V6_vmpyo_acc, InpTy,
                                {V1, X.Val, Y.Val});
}

auto HvxIdioms::createAddCarry(IRBuilderBase &Builder, Value *X, Value *Y,
                               Value *CarryIn) const
    -> std::pair<Value *, Value *> {
  assert(X->getType() == Y->getType());
  auto VecTy = cast<VectorType>(X->getType());
  if (VecTy == HvxI32Ty && HVC.HST.useHVXV62Ops()) {
    SmallVector<Value *> Args = {X, Y};
    Intrinsic::ID AddCarry;
    if (CarryIn == nullptr && HVC.HST.useHVXV66Ops()) {
      AddCarry = HVC.HST.getIntrinsicId(Hexagon::V6_vaddcarryo);
    } else {
      AddCarry = HVC.HST.getIntrinsicId(Hexagon::V6_vaddcarry);
      if (CarryIn == nullptr)
        CarryIn = HVC.getNullValue(HVC.getBoolTy(HVC.length(VecTy)));
      Args.push_back(CarryIn);
    }
    Value *Ret = HVC.createHvxIntrinsic(Builder, AddCarry,
                                        /*RetTy=*/nullptr, Args);
    Value *Result = Builder.CreateExtractValue(Ret, {0}, "ext");
    Value *CarryOut = Builder.CreateExtractValue(Ret, {1}, "ext");
    return {Result, CarryOut};
  }

  // In other cases, do a regular add, and unsigned compare-less-than.
  // The carry-out can originate in two places: adding the carry-in or adding
  // the two input values.
  Value *Result1 = X; // Result1 = X + CarryIn
  if (CarryIn != nullptr) {
    unsigned Width = VecTy->getScalarSizeInBits();
    uint32_t Mask = 1;
    if (Width < 32) {
      for (unsigned i = 0, e = 32 / Width; i != e; ++i)
        Mask = (Mask << Width) | 1;
    }
    auto V6_vandqrt = HVC.HST.getIntrinsicId(Hexagon::V6_vandqrt);
    Value *ValueIn =
        HVC.createHvxIntrinsic(Builder, V6_vandqrt, /*RetTy=*/nullptr,
                               {CarryIn, HVC.getConstInt(Mask)});
    Result1 = Builder.CreateAdd(X, ValueIn, "add");
  }

  Value *CarryOut1 = Builder.CreateCmp(CmpInst::ICMP_ULT, Result1, X, "cmp");
  Value *Result2 = Builder.CreateAdd(Result1, Y, "add");
  Value *CarryOut2 = Builder.CreateCmp(CmpInst::ICMP_ULT, Result2, Y, "cmp");
  return {Result2, Builder.CreateOr(CarryOut1, CarryOut2, "orb")};
}

auto HvxIdioms::createMul16(IRBuilderBase &Builder, SValue X, SValue Y) const
    -> Value * {
  Intrinsic::ID V6_vmpyh = 0;
  std::tie(X, Y) = canonSgn(X, Y);

  if (X.Sgn == Signed) {
    V6_vmpyh = HVC.HST.getIntrinsicId(Hexagon::V6_vmpyhv);
  } else if (Y.Sgn == Signed) {
    // In vmpyhus the second operand is unsigned
    V6_vmpyh = HVC.HST.getIntrinsicId(Hexagon::V6_vmpyhus);
  } else {
    V6_vmpyh = HVC.HST.getIntrinsicId(Hexagon::V6_vmpyuhv);
  }

  // i16*i16 -> i32 / interleaved
  Value *P =
      HVC.createHvxIntrinsic(Builder, V6_vmpyh, HvxP32Ty, {Y.Val, X.Val});
  // Deinterleave
  return HVC.vshuff(Builder, HVC.sublo(Builder, P), HVC.subhi(Builder, P));
}

auto HvxIdioms::createMulH16(IRBuilderBase &Builder, SValue X, SValue Y) const
    -> Value * {
  Type *HvxI16Ty = HVC.getHvxTy(HVC.getIntTy(16), /*Pair=*/false);

  if (HVC.HST.useHVXV69Ops()) {
    if (X.Sgn != Signed && Y.Sgn != Signed) {
      auto V6_vmpyuhvs = HVC.HST.getIntrinsicId(Hexagon::V6_vmpyuhvs);
      return HVC.createHvxIntrinsic(Builder, V6_vmpyuhvs, HvxI16Ty,
                                    {X.Val, Y.Val});
    }
  }

  Type *HvxP16Ty = HVC.getHvxTy(HVC.getIntTy(16), /*Pair=*/true);
  Value *Pair16 =
      Builder.CreateBitCast(createMul16(Builder, X, Y), HvxP16Ty, "cst");
  unsigned Len = HVC.length(HvxP16Ty) / 2;

  SmallVector<int, 128> PickOdd(Len);
  for (int i = 0; i != static_cast<int>(Len); ++i)
    PickOdd[i] = 2 * i + 1;

  return Builder.CreateShuffleVector(
      HVC.sublo(Builder, Pair16), HVC.subhi(Builder, Pair16), PickOdd, "shf");
}

auto HvxIdioms::createMul32(IRBuilderBase &Builder, SValue X, SValue Y) const
    -> std::pair<Value *, Value *> {
  assert(X.Val->getType() == Y.Val->getType());
  assert(X.Val->getType() == HvxI32Ty);

  Intrinsic::ID V6_vmpy_parts;
  std::tie(X, Y) = canonSgn(X, Y);

  if (X.Sgn == Signed) {
    V6_vmpy_parts = Intrinsic::hexagon_V6_vmpyss_parts;
  } else if (Y.Sgn == Signed) {
    V6_vmpy_parts = Intrinsic::hexagon_V6_vmpyus_parts;
  } else {
    V6_vmpy_parts = Intrinsic::hexagon_V6_vmpyuu_parts;
  }

  Value *Parts = HVC.createHvxIntrinsic(Builder, V6_vmpy_parts, nullptr,
                                        {X.Val, Y.Val}, {HvxI32Ty});
  Value *Hi = Builder.CreateExtractValue(Parts, {0}, "ext");
  Value *Lo = Builder.CreateExtractValue(Parts, {1}, "ext");
  return {Lo, Hi};
}

auto HvxIdioms::createAddLong(IRBuilderBase &Builder, ArrayRef<Value *> WordX,
                              ArrayRef<Value *> WordY) const
    -> SmallVector<Value *> {
  assert(WordX.size() == WordY.size());
  unsigned Idx = 0, Length = WordX.size();
  SmallVector<Value *> Sum(Length);

  while (Idx != Length) {
    if (HVC.isZero(WordX[Idx]))
      Sum[Idx] = WordY[Idx];
    else if (HVC.isZero(WordY[Idx]))
      Sum[Idx] = WordX[Idx];
    else
      break;
    ++Idx;
  }

  Value *Carry = nullptr;
  for (; Idx != Length; ++Idx) {
    std::tie(Sum[Idx], Carry) =
        createAddCarry(Builder, WordX[Idx], WordY[Idx], Carry);
  }

  // This drops the final carry beyond the highest word.
  return Sum;
}

auto HvxIdioms::createMulLong(IRBuilderBase &Builder, ArrayRef<Value *> WordX,
                              Signedness SgnX, ArrayRef<Value *> WordY,
                              Signedness SgnY) const -> SmallVector<Value *> {
  SmallVector<SmallVector<Value *>> Products(WordX.size() + WordY.size());

  // WordX[i] * WordY[j] produces words i+j and i+j+1 of the results,
  // that is halves 2(i+j), 2(i+j)+1, 2(i+j)+2, 2(i+j)+3.
  for (int i = 0, e = WordX.size(); i != e; ++i) {
    for (int j = 0, f = WordY.size(); j != f; ++j) {
      // Check the 4 halves that this multiplication can generate.
      Signedness SX = (i + 1 == e) ? SgnX : Unsigned;
      Signedness SY = (j + 1 == f) ? SgnY : Unsigned;
      auto [Lo, Hi] = createMul32(Builder, {WordX[i], SX}, {WordY[j], SY});
      Products[i + j + 0].push_back(Lo);
      Products[i + j + 1].push_back(Hi);
    }
  }

  Value *Zero = HVC.getNullValue(WordX[0]->getType());

  auto pop_back_or_zero = [Zero](auto &Vector) -> Value * {
    if (Vector.empty())
      return Zero;
    auto Last = Vector.back();
    Vector.pop_back();
    return Last;
  };

  for (int i = 0, e = Products.size(); i != e; ++i) {
    while (Products[i].size() > 1) {
      Value *Carry = nullptr; // no carry-in
      for (int j = i; j != e; ++j) {
        auto &ProdJ = Products[j];
        auto [Sum, CarryOut] = createAddCarry(Builder, pop_back_or_zero(ProdJ),
                                              pop_back_or_zero(ProdJ), Carry);
        ProdJ.insert(ProdJ.begin(), Sum);
        Carry = CarryOut;
      }
    }
  }

  SmallVector<Value *> WordP;
  for (auto &P : Products) {
    assert(P.size() == 1 && "Should have been added together");
    WordP.push_back(P.front());
  }

  return WordP;
}

auto HvxIdioms::run() -> bool {
  bool Changed = false;

  for (BasicBlock &B : HVC.F) {
    for (auto It = B.rbegin(); It != B.rend(); ++It) {
      if (auto Fxm = matchFxpMul(*It)) {
        Value *New = processFxpMul(*It, *Fxm);
        // Always report "changed" for now.
        Changed = true;
        if (!New)
          continue;
        bool StartOver = !isa<Instruction>(New);
        It->replaceAllUsesWith(New);
        RecursivelyDeleteTriviallyDeadInstructions(&*It, &HVC.TLI);
        It = StartOver ? B.rbegin()
                       : cast<Instruction>(New)->getReverseIterator();
        Changed = true;
      }
    }
  }

  return Changed;
}

// --- End HvxIdioms

auto HexagonVectorCombine::run() -> bool {
  if (DumpModule)
    dbgs() << "Module before HexagonVectorCombine\n" << *F.getParent();

  bool Changed = false;
  if (HST.useHVXOps()) {
    if (VAEnabled)
      Changed |= AlignVectors(*this).run();
    if (VIEnabled)
      Changed |= HvxIdioms(*this).run();
  }

  if (DumpModule) {
    dbgs() << "Module " << (Changed ? "(modified)" : "(unchanged)")
           << " after HexagonVectorCombine\n"
           << *F.getParent();
  }
  return Changed;
}

auto HexagonVectorCombine::getIntTy(unsigned Width) const -> IntegerType * {
  return IntegerType::get(F.getContext(), Width);
}

auto HexagonVectorCombine::getByteTy(int ElemCount) const -> Type * {
  assert(ElemCount >= 0);
  IntegerType *ByteTy = Type::getInt8Ty(F.getContext());
  if (ElemCount == 0)
    return ByteTy;
  return VectorType::get(ByteTy, ElemCount, /*Scalable=*/false);
}

auto HexagonVectorCombine::getBoolTy(int ElemCount) const -> Type * {
  assert(ElemCount >= 0);
  IntegerType *BoolTy = Type::getInt1Ty(F.getContext());
  if (ElemCount == 0)
    return BoolTy;
  return VectorType::get(BoolTy, ElemCount, /*Scalable=*/false);
}

auto HexagonVectorCombine::getConstInt(int Val, unsigned Width) const
    -> ConstantInt * {
  return ConstantInt::getSigned(getIntTy(Width), Val);
}

auto HexagonVectorCombine::isZero(const Value *Val) const -> bool {
  if (auto *C = dyn_cast<Constant>(Val))
    return C->isZeroValue();
  return false;
}

auto HexagonVectorCombine::getIntValue(const Value *Val) const
    -> std::optional<APInt> {
  if (auto *CI = dyn_cast<ConstantInt>(Val))
    return CI->getValue();
  return std::nullopt;
}

auto HexagonVectorCombine::isUndef(const Value *Val) const -> bool {
  return isa<UndefValue>(Val);
}

auto HexagonVectorCombine::isTrue(const Value *Val) const -> bool {
  return Val == ConstantInt::getTrue(Val->getType());
}

auto HexagonVectorCombine::isFalse(const Value *Val) const -> bool {
  return isZero(Val);
}

auto HexagonVectorCombine::getHvxTy(Type *ElemTy, bool Pair) const
    -> VectorType * {
  EVT ETy = EVT::getEVT(ElemTy, false);
  assert(ETy.isSimple() && "Invalid HVX element type");
  // Do not allow boolean types here: they don't have a fixed length.
  assert(HST.isHVXElementType(ETy.getSimpleVT(), /*IncludeBool=*/false) &&
         "Invalid HVX element type");
  unsigned HwLen = HST.getVectorLength();
  unsigned NumElems = (8 * HwLen) / ETy.getSizeInBits();
  return VectorType::get(ElemTy, Pair ? 2 * NumElems : NumElems,
                         /*Scalable=*/false);
}

auto HexagonVectorCombine::getSizeOf(const Value *Val, SizeKind Kind) const
    -> int {
  return getSizeOf(Val->getType(), Kind);
}

auto HexagonVectorCombine::getSizeOf(const Type *Ty, SizeKind Kind) const
    -> int {
  auto *NcTy = const_cast<Type *>(Ty);
  switch (Kind) {
  case Store:
    return DL.getTypeStoreSize(NcTy).getFixedValue();
  case Alloc:
    return DL.getTypeAllocSize(NcTy).getFixedValue();
  }
  llvm_unreachable("Unhandled SizeKind enum");
}

auto HexagonVectorCombine::getTypeAlignment(Type *Ty) const -> int {
  // The actual type may be shorter than the HVX vector, so determine
  // the alignment based on subtarget info.
  if (HST.isTypeForHVX(Ty))
    return HST.getVectorLength();
  return DL.getABITypeAlign(Ty).value();
}

auto HexagonVectorCombine::length(Value *Val) const -> size_t {
  return length(Val->getType());
}

auto HexagonVectorCombine::length(Type *Ty) const -> size_t {
  auto *VecTy = dyn_cast<VectorType>(Ty);
  assert(VecTy && "Must be a vector type");
  return VecTy->getElementCount().getFixedValue();
}

auto HexagonVectorCombine::getNullValue(Type *Ty) const -> Constant * {
  assert(Ty->isIntOrIntVectorTy());
  auto Zero = ConstantInt::get(Ty->getScalarType(), 0);
  if (auto *VecTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VecTy->getElementCount(), Zero);
  return Zero;
}

auto HexagonVectorCombine::getFullValue(Type *Ty) const -> Constant * {
  assert(Ty->isIntOrIntVectorTy());
  auto Minus1 = ConstantInt::get(Ty->getScalarType(), -1);
  if (auto *VecTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VecTy->getElementCount(), Minus1);
  return Minus1;
}

auto HexagonVectorCombine::getConstSplat(Type *Ty, int Val) const
    -> Constant * {
  assert(Ty->isVectorTy());
  auto VecTy = cast<VectorType>(Ty);
  Type *ElemTy = VecTy->getElementType();
  // Add support for floats if needed.
  auto *Splat = ConstantVector::getSplat(VecTy->getElementCount(),
                                         ConstantInt::get(ElemTy, Val));
  return Splat;
}

auto HexagonVectorCombine::simplify(Value *V) const -> Value * {
  if (auto *In = dyn_cast<Instruction>(V)) {
    SimplifyQuery Q(DL, &TLI, &DT, &AC, In);
    return simplifyInstruction(In, Q);
  }
  return nullptr;
}

// Insert bytes [Start..Start+Length) of Src into Dst at byte Where.
auto HexagonVectorCombine::insertb(IRBuilderBase &Builder, Value *Dst,
                                   Value *Src, int Start, int Length,
                                   int Where) const -> Value * {
  assert(isByteVecTy(Dst->getType()) && isByteVecTy(Src->getType()));
  int SrcLen = getSizeOf(Src);
  int DstLen = getSizeOf(Dst);
  assert(0 <= Start && Start + Length <= SrcLen);
  assert(0 <= Where && Where + Length <= DstLen);

  int P2Len = PowerOf2Ceil(SrcLen | DstLen);
  auto *Undef = UndefValue::get(getByteTy());
  Value *P2Src = vresize(Builder, Src, P2Len, Undef);
  Value *P2Dst = vresize(Builder, Dst, P2Len, Undef);

  SmallVector<int, 256> SMask(P2Len);
  for (int i = 0; i != P2Len; ++i) {
    // If i is in [Where, Where+Length), pick Src[Start+(i-Where)].
    // Otherwise, pick Dst[i];
    SMask[i] =
        (Where <= i && i < Where + Length) ? P2Len + Start + (i - Where) : i;
  }

  Value *P2Insert = Builder.CreateShuffleVector(P2Dst, P2Src, SMask, "shf");
  return vresize(Builder, P2Insert, DstLen, Undef);
}

auto HexagonVectorCombine::vlalignb(IRBuilderBase &Builder, Value *Lo,
                                    Value *Hi, Value *Amt) const -> Value * {
  assert(Lo->getType() == Hi->getType() && "Argument type mismatch");
  if (isZero(Amt))
    return Hi;
  int VecLen = getSizeOf(Hi);
  if (auto IntAmt = getIntValue(Amt))
    return getElementRange(Builder, Lo, Hi, VecLen - IntAmt->getSExtValue(),
                           VecLen);

  if (HST.isTypeForHVX(Hi->getType())) {
    assert(static_cast<unsigned>(VecLen) == HST.getVectorLength() &&
           "Expecting an exact HVX type");
    return createHvxIntrinsic(Builder, HST.getIntrinsicId(Hexagon::V6_vlalignb),
                              Hi->getType(), {Hi, Lo, Amt});
  }

  if (VecLen == 4) {
    Value *Pair = concat(Builder, {Lo, Hi});
    Value *Shift =
        Builder.CreateLShr(Builder.CreateShl(Pair, Amt, "shl"), 32, "lsr");
    Value *Trunc =
        Builder.CreateTrunc(Shift, Type::getInt32Ty(F.getContext()), "trn");
    return Builder.CreateBitCast(Trunc, Hi->getType(), "cst");
  }
  if (VecLen == 8) {
    Value *Sub = Builder.CreateSub(getConstInt(VecLen), Amt, "sub");
    return vralignb(Builder, Lo, Hi, Sub);
  }
  llvm_unreachable("Unexpected vector length");
}

auto HexagonVectorCombine::vralignb(IRBuilderBase &Builder, Value *Lo,
                                    Value *Hi, Value *Amt) const -> Value * {
  assert(Lo->getType() == Hi->getType() && "Argument type mismatch");
  if (isZero(Amt))
    return Lo;
  int VecLen = getSizeOf(Lo);
  if (auto IntAmt = getIntValue(Amt))
    return getElementRange(Builder, Lo, Hi, IntAmt->getSExtValue(), VecLen);

  if (HST.isTypeForHVX(Lo->getType())) {
    assert(static_cast<unsigned>(VecLen) == HST.getVectorLength() &&
           "Expecting an exact HVX type");
    return createHvxIntrinsic(Builder, HST.getIntrinsicId(Hexagon::V6_valignb),
                              Lo->getType(), {Hi, Lo, Amt});
  }

  if (VecLen == 4) {
    Value *Pair = concat(Builder, {Lo, Hi});
    Value *Shift = Builder.CreateLShr(Pair, Amt, "lsr");
    Value *Trunc =
        Builder.CreateTrunc(Shift, Type::getInt32Ty(F.getContext()), "trn");
    return Builder.CreateBitCast(Trunc, Lo->getType(), "cst");
  }
  if (VecLen == 8) {
    Type *Int64Ty = Type::getInt64Ty(F.getContext());
    Value *Lo64 = Builder.CreateBitCast(Lo, Int64Ty, "cst");
    Value *Hi64 = Builder.CreateBitCast(Hi, Int64Ty, "cst");
    Function *FI = Intrinsic::getDeclaration(F.getParent(),
                                             Intrinsic::hexagon_S2_valignrb);
    Value *Call = Builder.CreateCall(FI, {Hi64, Lo64, Amt}, "cup");
    return Builder.CreateBitCast(Call, Lo->getType(), "cst");
  }
  llvm_unreachable("Unexpected vector length");
}

// Concatenates a sequence of vectors of the same type.
auto HexagonVectorCombine::concat(IRBuilderBase &Builder,
                                  ArrayRef<Value *> Vecs) const -> Value * {
  assert(!Vecs.empty());
  SmallVector<int, 256> SMask;
  std::vector<Value *> Work[2];
  int ThisW = 0, OtherW = 1;

  Work[ThisW].assign(Vecs.begin(), Vecs.end());
  while (Work[ThisW].size() > 1) {
    auto *Ty = cast<VectorType>(Work[ThisW].front()->getType());
    SMask.resize(length(Ty) * 2);
    std::iota(SMask.begin(), SMask.end(), 0);

    Work[OtherW].clear();
    if (Work[ThisW].size() % 2 != 0)
      Work[ThisW].push_back(UndefValue::get(Ty));
    for (int i = 0, e = Work[ThisW].size(); i < e; i += 2) {
      Value *Joined = Builder.CreateShuffleVector(
          Work[ThisW][i], Work[ThisW][i + 1], SMask, "shf");
      Work[OtherW].push_back(Joined);
    }
    std::swap(ThisW, OtherW);
  }

  // Since there may have been some undefs appended to make shuffle operands
  // have the same type, perform the last shuffle to only pick the original
  // elements.
  SMask.resize(Vecs.size() * length(Vecs.front()->getType()));
  std::iota(SMask.begin(), SMask.end(), 0);
  Value *Total = Work[ThisW].front();
  return Builder.CreateShuffleVector(Total, SMask, "shf");
}

auto HexagonVectorCombine::vresize(IRBuilderBase &Builder, Value *Val,
                                   int NewSize, Value *Pad) const -> Value * {
  assert(isa<VectorType>(Val->getType()));
  auto *ValTy = cast<VectorType>(Val->getType());
  assert(ValTy->getElementType() == Pad->getType());

  int CurSize = length(ValTy);
  if (CurSize == NewSize)
    return Val;
  // Truncate?
  if (CurSize > NewSize)
    return getElementRange(Builder, Val, /*Ignored*/ Val, 0, NewSize);
  // Extend.
  SmallVector<int, 128> SMask(NewSize);
  std::iota(SMask.begin(), SMask.begin() + CurSize, 0);
  std::fill(SMask.begin() + CurSize, SMask.end(), CurSize);
  Value *PadVec = Builder.CreateVectorSplat(CurSize, Pad, "spt");
  return Builder.CreateShuffleVector(Val, PadVec, SMask, "shf");
}

auto HexagonVectorCombine::rescale(IRBuilderBase &Builder, Value *Mask,
                                   Type *FromTy, Type *ToTy) const -> Value * {
  // Mask is a vector <N x i1>, where each element corresponds to an
  // element of FromTy. Remap it so that each element will correspond
  // to an element of ToTy.
  assert(isa<VectorType>(Mask->getType()));

  Type *FromSTy = FromTy->getScalarType();
  Type *ToSTy = ToTy->getScalarType();
  if (FromSTy == ToSTy)
    return Mask;

  int FromSize = getSizeOf(FromSTy);
  int ToSize = getSizeOf(ToSTy);
  assert(FromSize % ToSize == 0 || ToSize % FromSize == 0);

  auto *MaskTy = cast<VectorType>(Mask->getType());
  int FromCount = length(MaskTy);
  int ToCount = (FromCount * FromSize) / ToSize;
  assert((FromCount * FromSize) % ToSize == 0);

  auto *FromITy = getIntTy(FromSize * 8);
  auto *ToITy = getIntTy(ToSize * 8);

  // Mask <N x i1> -> sext to <N x FromTy> -> bitcast to <M x ToTy> ->
  // -> trunc to <M x i1>.
  Value *Ext = Builder.CreateSExt(
      Mask, VectorType::get(FromITy, FromCount, /*Scalable=*/false), "sxt");
  Value *Cast = Builder.CreateBitCast(
      Ext, VectorType::get(ToITy, ToCount, /*Scalable=*/false), "cst");
  return Builder.CreateTrunc(
      Cast, VectorType::get(getBoolTy(), ToCount, /*Scalable=*/false), "trn");
}

// Bitcast to bytes, and return least significant bits.
auto HexagonVectorCombine::vlsb(IRBuilderBase &Builder, Value *Val) const
    -> Value * {
  Type *ScalarTy = Val->getType()->getScalarType();
  if (ScalarTy == getBoolTy())
    return Val;

  Value *Bytes = vbytes(Builder, Val);
  if (auto *VecTy = dyn_cast<VectorType>(Bytes->getType()))
    return Builder.CreateTrunc(Bytes, getBoolTy(getSizeOf(VecTy)), "trn");
  // If Bytes is a scalar (i.e. Val was a scalar byte), return i1, not
  // <1 x i1>.
  return Builder.CreateTrunc(Bytes, getBoolTy(), "trn");
}

// Bitcast to bytes for non-bool. For bool, convert i1 -> i8.
auto HexagonVectorCombine::vbytes(IRBuilderBase &Builder, Value *Val) const
    -> Value * {
  Type *ScalarTy = Val->getType()->getScalarType();
  if (ScalarTy == getByteTy())
    return Val;

  if (ScalarTy != getBoolTy())
    return Builder.CreateBitCast(Val, getByteTy(getSizeOf(Val)), "cst");
  // For bool, return a sext from i1 to i8.
  if (auto *VecTy = dyn_cast<VectorType>(Val->getType()))
    return Builder.CreateSExt(Val, VectorType::get(getByteTy(), VecTy), "sxt");
  return Builder.CreateSExt(Val, getByteTy(), "sxt");
}

auto HexagonVectorCombine::subvector(IRBuilderBase &Builder, Value *Val,
                                     unsigned Start, unsigned Length) const
    -> Value * {
  assert(Start + Length <= length(Val));
  return getElementRange(Builder, Val, /*Ignored*/ Val, Start, Length);
}

auto HexagonVectorCombine::sublo(IRBuilderBase &Builder, Value *Val) const
    -> Value * {
  size_t Len = length(Val);
  assert(Len % 2 == 0 && "Length should be even");
  return subvector(Builder, Val, 0, Len / 2);
}

auto HexagonVectorCombine::subhi(IRBuilderBase &Builder, Value *Val) const
    -> Value * {
  size_t Len = length(Val);
  assert(Len % 2 == 0 && "Length should be even");
  return subvector(Builder, Val, Len / 2, Len / 2);
}

auto HexagonVectorCombine::vdeal(IRBuilderBase &Builder, Value *Val0,
                                 Value *Val1) const -> Value * {
  assert(Val0->getType() == Val1->getType());
  int Len = length(Val0);
  SmallVector<int, 128> Mask(2 * Len);

  for (int i = 0; i != Len; ++i) {
    Mask[i] = 2 * i;           // Even
    Mask[i + Len] = 2 * i + 1; // Odd
  }
  return Builder.CreateShuffleVector(Val0, Val1, Mask, "shf");
}

auto HexagonVectorCombine::vshuff(IRBuilderBase &Builder, Value *Val0,
                                  Value *Val1) const -> Value * { //
  assert(Val0->getType() == Val1->getType());
  int Len = length(Val0);
  SmallVector<int, 128> Mask(2 * Len);

  for (int i = 0; i != Len; ++i) {
    Mask[2 * i + 0] = i;       // Val0
    Mask[2 * i + 1] = i + Len; // Val1
  }
  return Builder.CreateShuffleVector(Val0, Val1, Mask, "shf");
}

auto HexagonVectorCombine::createHvxIntrinsic(IRBuilderBase &Builder,
                                              Intrinsic::ID IntID, Type *RetTy,
                                              ArrayRef<Value *> Args,
                                              ArrayRef<Type *> ArgTys,
                                              ArrayRef<Value *> MDSources) const
    -> Value * {
  auto getCast = [&](IRBuilderBase &Builder, Value *Val,
                     Type *DestTy) -> Value * {
    Type *SrcTy = Val->getType();
    if (SrcTy == DestTy)
      return Val;

    // Non-HVX type. It should be a scalar, and it should already have
    // a valid type.
    assert(HST.isTypeForHVX(SrcTy, /*IncludeBool=*/true));

    Type *BoolTy = Type::getInt1Ty(F.getContext());
    if (cast<VectorType>(SrcTy)->getElementType() != BoolTy)
      return Builder.CreateBitCast(Val, DestTy, "cst");

    // Predicate HVX vector.
    unsigned HwLen = HST.getVectorLength();
    Intrinsic::ID TC = HwLen == 64 ? Intrinsic::hexagon_V6_pred_typecast
                                   : Intrinsic::hexagon_V6_pred_typecast_128B;
    Function *FI =
        Intrinsic::getDeclaration(F.getParent(), TC, {DestTy, Val->getType()});
    return Builder.CreateCall(FI, {Val}, "cup");
  };

  Function *IntrFn = Intrinsic::getDeclaration(F.getParent(), IntID, ArgTys);
  FunctionType *IntrTy = IntrFn->getFunctionType();

  SmallVector<Value *, 4> IntrArgs;
  for (int i = 0, e = Args.size(); i != e; ++i) {
    Value *A = Args[i];
    Type *T = IntrTy->getParamType(i);
    if (A->getType() != T) {
      IntrArgs.push_back(getCast(Builder, A, T));
    } else {
      IntrArgs.push_back(A);
    }
  }
  StringRef MaybeName = !IntrTy->getReturnType()->isVoidTy() ? "cup" : "";
  CallInst *Call = Builder.CreateCall(IntrFn, IntrArgs, MaybeName);

  MemoryEffects ME = Call->getAttributes().getMemoryEffects();
  if (!ME.doesNotAccessMemory() && !ME.onlyAccessesInaccessibleMem())
    propagateMetadata(Call, MDSources);

  Type *CallTy = Call->getType();
  if (RetTy == nullptr || CallTy == RetTy)
    return Call;
  // Scalar types should have RetTy matching the call return type.
  assert(HST.isTypeForHVX(CallTy, /*IncludeBool=*/true));
  return getCast(Builder, Call, RetTy);
}

auto HexagonVectorCombine::splitVectorElements(IRBuilderBase &Builder,
                                               Value *Vec,
                                               unsigned ToWidth) const
    -> SmallVector<Value *> {
  // Break a vector of wide elements into a series of vectors with narrow
  // elements:
  //   (...c0:b0:a0, ...c1:b1:a1, ...c2:b2:a2, ...)
  // -->
  //   (a0, a1, a2, ...)    // lowest "ToWidth" bits
  //   (b0, b1, b2, ...)    // the next lowest...
  //   (c0, c1, c2, ...)    // ...
  //   ...
  //
  // The number of elements in each resulting vector is the same as
  // in the original vector.

  auto *VecTy = cast<VectorType>(Vec->getType());
  assert(VecTy->getElementType()->isIntegerTy());
  unsigned FromWidth = VecTy->getScalarSizeInBits();
  assert(isPowerOf2_32(ToWidth) && isPowerOf2_32(FromWidth));
  assert(ToWidth <= FromWidth && "Breaking up into wider elements?");
  unsigned NumResults = FromWidth / ToWidth;

  SmallVector<Value *> Results(NumResults);
  Results[0] = Vec;
  unsigned Length = length(VecTy);

  // Do it by splitting in half, since those operations correspond to deal
  // instructions.
  auto splitInHalf = [&](unsigned Begin, unsigned End, auto splitFunc) -> void {
    // Take V = Results[Begin], split it in L, H.
    // Store Results[Begin] = L, Results[(Begin+End)/2] = H
    // Call itself recursively split(Begin, Half), split(Half+1, End)
    if (Begin + 1 == End)
      return;

    Value *Val = Results[Begin];
    unsigned Width = Val->getType()->getScalarSizeInBits();

    auto *VTy = VectorType::get(getIntTy(Width / 2), 2 * Length, false);
    Value *VVal = Builder.CreateBitCast(Val, VTy, "cst");

    Value *Res = vdeal(Builder, sublo(Builder, VVal), subhi(Builder, VVal));

    unsigned Half = (Begin + End) / 2;
    Results[Begin] = sublo(Builder, Res);
    Results[Half] = subhi(Builder, Res);

    splitFunc(Begin, Half, splitFunc);
    splitFunc(Half, End, splitFunc);
  };

  splitInHalf(0, NumResults, splitInHalf);
  return Results;
}

auto HexagonVectorCombine::joinVectorElements(IRBuilderBase &Builder,
                                              ArrayRef<Value *> Values,
                                              VectorType *ToType) const
    -> Value * {
  assert(ToType->getElementType()->isIntegerTy());

  // If the list of values does not have power-of-2 elements, append copies
  // of the sign bit to it, to make the size be 2^n.
  // The reason for this is that the values will be joined in pairs, because
  // otherwise the shuffles will result in convoluted code. With pairwise
  // joins, the shuffles will hopefully be folded into a perfect shuffle.
  // The output will need to be sign-extended to a type with element width
  // being a power-of-2 anyways.
  SmallVector<Value *> Inputs(Values.begin(), Values.end());

  unsigned ToWidth = ToType->getScalarSizeInBits();
  unsigned Width = Inputs.front()->getType()->getScalarSizeInBits();
  assert(Width <= ToWidth);
  assert(isPowerOf2_32(Width) && isPowerOf2_32(ToWidth));
  unsigned Length = length(Inputs.front()->getType());

  unsigned NeedInputs = ToWidth / Width;
  if (Inputs.size() != NeedInputs) {
    // Having too many inputs is ok: drop the high bits (usual wrap-around).
    // If there are too few, fill them with the sign bit.
    Value *Last = Inputs.back();
    Value *Sign = Builder.CreateAShr(
        Last, getConstSplat(Last->getType(), Width - 1), "asr");
    Inputs.resize(NeedInputs, Sign);
  }

  while (Inputs.size() > 1) {
    Width *= 2;
    auto *VTy = VectorType::get(getIntTy(Width), Length, false);
    for (int i = 0, e = Inputs.size(); i < e; i += 2) {
      Value *Res = vshuff(Builder, Inputs[i], Inputs[i + 1]);
      Inputs[i / 2] = Builder.CreateBitCast(Res, VTy, "cst");
    }
    Inputs.resize(Inputs.size() / 2);
  }

  assert(Inputs.front()->getType() == ToType);
  return Inputs.front();
}

auto HexagonVectorCombine::calculatePointerDifference(Value *Ptr0,
                                                      Value *Ptr1) const
    -> std::optional<int> {
  // Try SCEV first.
  const SCEV *Scev0 = SE.getSCEV(Ptr0);
  const SCEV *Scev1 = SE.getSCEV(Ptr1);
  const SCEV *ScevDiff = SE.getMinusSCEV(Scev0, Scev1);
  if (auto *Const = dyn_cast<SCEVConstant>(ScevDiff)) {
    APInt V = Const->getAPInt();
    if (V.isSignedIntN(8 * sizeof(int)))
      return static_cast<int>(V.getSExtValue());
  }

  struct Builder : IRBuilder<> {
    Builder(BasicBlock *B) : IRBuilder<>(B->getTerminator()) {}
    ~Builder() {
      for (Instruction *I : llvm::reverse(ToErase))
        I->eraseFromParent();
    }
    SmallVector<Instruction *, 8> ToErase;
  };

#define CallBuilder(B, F)                                                      \
  [&](auto &B_) {                                                              \
    Value *V = B_.F;                                                           \
    if (auto *I = dyn_cast<Instruction>(V))                                    \
      B_.ToErase.push_back(I);                                                 \
    return V;                                                                  \
  }(B)

  auto Simplify = [this](Value *V) {
    if (Value *S = simplify(V))
      return S;
    return V;
  };

  auto StripBitCast = [](Value *V) {
    while (auto *C = dyn_cast<BitCastInst>(V))
      V = C->getOperand(0);
    return V;
  };

  Ptr0 = StripBitCast(Ptr0);
  Ptr1 = StripBitCast(Ptr1);
  if (!isa<GetElementPtrInst>(Ptr0) || !isa<GetElementPtrInst>(Ptr1))
    return std::nullopt;

  auto *Gep0 = cast<GetElementPtrInst>(Ptr0);
  auto *Gep1 = cast<GetElementPtrInst>(Ptr1);
  if (Gep0->getPointerOperand() != Gep1->getPointerOperand())
    return std::nullopt;
  if (Gep0->getSourceElementType() != Gep1->getSourceElementType())
    return std::nullopt;

  Builder B(Gep0->getParent());
  int Scale = getSizeOf(Gep0->getSourceElementType(), Alloc);

  // FIXME: for now only check GEPs with a single index.
  if (Gep0->getNumOperands() != 2 || Gep1->getNumOperands() != 2)
    return std::nullopt;

  Value *Idx0 = Gep0->getOperand(1);
  Value *Idx1 = Gep1->getOperand(1);

  // First, try to simplify the subtraction directly.
  if (auto *Diff = dyn_cast<ConstantInt>(
          Simplify(CallBuilder(B, CreateSub(Idx0, Idx1)))))
    return Diff->getSExtValue() * Scale;

  KnownBits Known0 = getKnownBits(Idx0, Gep0);
  KnownBits Known1 = getKnownBits(Idx1, Gep1);
  APInt Unknown = ~(Known0.Zero | Known0.One) | ~(Known1.Zero | Known1.One);
  if (Unknown.isAllOnes())
    return std::nullopt;

  Value *MaskU = ConstantInt::get(Idx0->getType(), Unknown);
  Value *AndU0 = Simplify(CallBuilder(B, CreateAnd(Idx0, MaskU)));
  Value *AndU1 = Simplify(CallBuilder(B, CreateAnd(Idx1, MaskU)));
  Value *SubU = Simplify(CallBuilder(B, CreateSub(AndU0, AndU1)));
  int Diff0 = 0;
  if (auto *C = dyn_cast<ConstantInt>(SubU)) {
    Diff0 = C->getSExtValue();
  } else {
    return std::nullopt;
  }

  Value *MaskK = ConstantInt::get(MaskU->getType(), ~Unknown);
  Value *AndK0 = Simplify(CallBuilder(B, CreateAnd(Idx0, MaskK)));
  Value *AndK1 = Simplify(CallBuilder(B, CreateAnd(Idx1, MaskK)));
  Value *SubK = Simplify(CallBuilder(B, CreateSub(AndK0, AndK1)));
  int Diff1 = 0;
  if (auto *C = dyn_cast<ConstantInt>(SubK)) {
    Diff1 = C->getSExtValue();
  } else {
    return std::nullopt;
  }

  return (Diff0 + Diff1) * Scale;

#undef CallBuilder
}

auto HexagonVectorCombine::getNumSignificantBits(const Value *V,
                                                 const Instruction *CtxI) const
    -> unsigned {
  return ComputeMaxSignificantBits(V, DL, /*Depth=*/0, &AC, CtxI, &DT);
}

auto HexagonVectorCombine::getKnownBits(const Value *V,
                                        const Instruction *CtxI) const
    -> KnownBits {
  return computeKnownBits(V, DL, /*Depth=*/0, &AC, CtxI, &DT);
}

auto HexagonVectorCombine::isSafeToClone(const Instruction &In) const -> bool {
  if (In.mayHaveSideEffects() || In.isAtomic() || In.isVolatile() ||
      In.isFenceLike() || In.mayReadOrWriteMemory()) {
    return false;
  }
  if (isa<CallBase>(In) || isa<AllocaInst>(In))
    return false;
  return true;
}

template <typename T>
auto HexagonVectorCombine::isSafeToMoveBeforeInBB(const Instruction &In,
                                                  BasicBlock::const_iterator To,
                                                  const T &IgnoreInsts) const
    -> bool {
  auto getLocOrNone =
      [this](const Instruction &I) -> std::optional<MemoryLocation> {
    if (const auto *II = dyn_cast<IntrinsicInst>(&I)) {
      switch (II->getIntrinsicID()) {
      case Intrinsic::masked_load:
        return MemoryLocation::getForArgument(II, 0, TLI);
      case Intrinsic::masked_store:
        return MemoryLocation::getForArgument(II, 1, TLI);
      }
    }
    return MemoryLocation::getOrNone(&I);
  };

  // The source and the destination must be in the same basic block.
  const BasicBlock &Block = *In.getParent();
  assert(Block.begin() == To || Block.end() == To || To->getParent() == &Block);
  // No PHIs.
  if (isa<PHINode>(In) || (To != Block.end() && isa<PHINode>(*To)))
    return false;

  if (!mayHaveNonDefUseDependency(In))
    return true;
  bool MayWrite = In.mayWriteToMemory();
  auto MaybeLoc = getLocOrNone(In);

  auto From = In.getIterator();
  if (From == To)
    return true;
  bool MoveUp = (To != Block.end() && To->comesBefore(&In));
  auto Range =
      MoveUp ? std::make_pair(To, From) : std::make_pair(std::next(From), To);
  for (auto It = Range.first; It != Range.second; ++It) {
    const Instruction &I = *It;
    if (llvm::is_contained(IgnoreInsts, &I))
      continue;
    // assume intrinsic can be ignored
    if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
      if (II->getIntrinsicID() == Intrinsic::assume)
        continue;
    }
    // Parts based on isSafeToMoveBefore from CoveMoverUtils.cpp.
    if (I.mayThrow())
      return false;
    if (auto *CB = dyn_cast<CallBase>(&I)) {
      if (!CB->hasFnAttr(Attribute::WillReturn))
        return false;
      if (!CB->hasFnAttr(Attribute::NoSync))
        return false;
    }
    if (I.mayReadOrWriteMemory()) {
      auto MaybeLocI = getLocOrNone(I);
      if (MayWrite || I.mayWriteToMemory()) {
        if (!MaybeLoc || !MaybeLocI)
          return false;
        if (!AA.isNoAlias(*MaybeLoc, *MaybeLocI))
          return false;
      }
    }
  }
  return true;
}

auto HexagonVectorCombine::isByteVecTy(Type *Ty) const -> bool {
  if (auto *VecTy = dyn_cast<VectorType>(Ty))
    return VecTy->getElementType() == getByteTy();
  return false;
}

auto HexagonVectorCombine::getElementRange(IRBuilderBase &Builder, Value *Lo,
                                           Value *Hi, int Start,
                                           int Length) const -> Value * {
  assert(0 <= Start && size_t(Start + Length) < length(Lo) + length(Hi));
  SmallVector<int, 128> SMask(Length);
  std::iota(SMask.begin(), SMask.end(), Start);
  return Builder.CreateShuffleVector(Lo, Hi, SMask, "shf");
}

// Pass management.

namespace llvm {
void initializeHexagonVectorCombineLegacyPass(PassRegistry &);
FunctionPass *createHexagonVectorCombineLegacyPass();
} // namespace llvm

namespace {
class HexagonVectorCombineLegacy : public FunctionPass {
public:
  static char ID;

  HexagonVectorCombineLegacy() : FunctionPass(ID) {}

  StringRef getPassName() const override { return "Hexagon Vector Combine"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<TargetPassConfig>();
    FunctionPass::getAnalysisUsage(AU);
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;
    AliasAnalysis &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
    AssumptionCache &AC =
        getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    TargetLibraryInfo &TLI =
        getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
    auto &TM = getAnalysis<TargetPassConfig>().getTM<HexagonTargetMachine>();
    HexagonVectorCombine HVC(F, AA, AC, DT, SE, TLI, TM);
    return HVC.run();
  }
};
} // namespace

char HexagonVectorCombineLegacy::ID = 0;

INITIALIZE_PASS_BEGIN(HexagonVectorCombineLegacy, DEBUG_TYPE,
                      "Hexagon Vector Combine", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(HexagonVectorCombineLegacy, DEBUG_TYPE,
                    "Hexagon Vector Combine", false, false)

FunctionPass *llvm::createHexagonVectorCombineLegacyPass() {
  return new HexagonVectorCombineLegacy();
}

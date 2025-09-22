#include "llvm/Transforms/Utils/VNCoercion.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "vncoerce"

namespace llvm {
namespace VNCoercion {

static bool isFirstClassAggregateOrScalableType(Type *Ty) {
  return Ty->isStructTy() || Ty->isArrayTy() || isa<ScalableVectorType>(Ty);
}

/// Return true if coerceAvailableValueToLoadType will succeed.
bool canCoerceMustAliasedValueToLoad(Value *StoredVal, Type *LoadTy,
                                     const DataLayout &DL) {
  Type *StoredTy = StoredVal->getType();

  if (StoredTy == LoadTy)
    return true;

  // If the loaded/stored value is a first class array/struct, or scalable type,
  // don't try to transform them. We need to be able to bitcast to integer.
  if (isFirstClassAggregateOrScalableType(LoadTy) ||
      isFirstClassAggregateOrScalableType(StoredTy))
    return false;

  uint64_t StoreSize = DL.getTypeSizeInBits(StoredTy).getFixedValue();

  // The store size must be byte-aligned to support future type casts.
  if (llvm::alignTo(StoreSize, 8) != StoreSize)
    return false;

  // The store has to be at least as big as the load.
  if (StoreSize < DL.getTypeSizeInBits(LoadTy).getFixedValue())
    return false;

  bool StoredNI = DL.isNonIntegralPointerType(StoredTy->getScalarType());
  bool LoadNI = DL.isNonIntegralPointerType(LoadTy->getScalarType());
  // Don't coerce non-integral pointers to integers or vice versa.
  if (StoredNI != LoadNI) {
    // As a special case, allow coercion of memset used to initialize
    // an array w/null.  Despite non-integral pointers not generally having a
    // specific bit pattern, we do assume null is zero.
    if (auto *CI = dyn_cast<Constant>(StoredVal))
      return CI->isNullValue();
    return false;
  } else if (StoredNI && LoadNI &&
             StoredTy->getPointerAddressSpace() !=
                 LoadTy->getPointerAddressSpace()) {
    return false;
  }


  // The implementation below uses inttoptr for vectors of unequal size; we
  // can't allow this for non integral pointers. We could teach it to extract
  // exact subvectors if desired.
  if (StoredNI && StoreSize != DL.getTypeSizeInBits(LoadTy).getFixedValue())
    return false;

  if (StoredTy->isTargetExtTy() || LoadTy->isTargetExtTy())
    return false;

  return true;
}

/// If we saw a store of a value to memory, and
/// then a load from a must-aliased pointer of a different type, try to coerce
/// the stored value.  LoadedTy is the type of the load we want to replace.
/// IRB is IRBuilder used to insert new instructions.
///
/// If we can't do it, return null.
Value *coerceAvailableValueToLoadType(Value *StoredVal, Type *LoadedTy,
                                      IRBuilderBase &Helper,
                                      const DataLayout &DL) {
  assert(canCoerceMustAliasedValueToLoad(StoredVal, LoadedTy, DL) &&
         "precondition violation - materialization can't fail");
  if (auto *C = dyn_cast<Constant>(StoredVal))
    StoredVal = ConstantFoldConstant(C, DL);

  // If this is already the right type, just return it.
  Type *StoredValTy = StoredVal->getType();

  uint64_t StoredValSize = DL.getTypeSizeInBits(StoredValTy).getFixedValue();
  uint64_t LoadedValSize = DL.getTypeSizeInBits(LoadedTy).getFixedValue();

  // If the store and reload are the same size, we can always reuse it.
  if (StoredValSize == LoadedValSize) {
    // Pointer to Pointer -> use bitcast.
    if (StoredValTy->isPtrOrPtrVectorTy() && LoadedTy->isPtrOrPtrVectorTy()) {
      StoredVal = Helper.CreateBitCast(StoredVal, LoadedTy);
    } else {
      // Convert source pointers to integers, which can be bitcast.
      if (StoredValTy->isPtrOrPtrVectorTy()) {
        StoredValTy = DL.getIntPtrType(StoredValTy);
        StoredVal = Helper.CreatePtrToInt(StoredVal, StoredValTy);
      }

      Type *TypeToCastTo = LoadedTy;
      if (TypeToCastTo->isPtrOrPtrVectorTy())
        TypeToCastTo = DL.getIntPtrType(TypeToCastTo);

      if (StoredValTy != TypeToCastTo)
        StoredVal = Helper.CreateBitCast(StoredVal, TypeToCastTo);

      // Cast to pointer if the load needs a pointer type.
      if (LoadedTy->isPtrOrPtrVectorTy())
        StoredVal = Helper.CreateIntToPtr(StoredVal, LoadedTy);
    }

    if (auto *C = dyn_cast<ConstantExpr>(StoredVal))
      StoredVal = ConstantFoldConstant(C, DL);

    return StoredVal;
  }
  // If the loaded value is smaller than the available value, then we can
  // extract out a piece from it.  If the available value is too small, then we
  // can't do anything.
  assert(StoredValSize >= LoadedValSize &&
         "canCoerceMustAliasedValueToLoad fail");

  // Convert source pointers to integers, which can be manipulated.
  if (StoredValTy->isPtrOrPtrVectorTy()) {
    StoredValTy = DL.getIntPtrType(StoredValTy);
    StoredVal = Helper.CreatePtrToInt(StoredVal, StoredValTy);
  }

  // Convert vectors and fp to integer, which can be manipulated.
  if (!StoredValTy->isIntegerTy()) {
    StoredValTy = IntegerType::get(StoredValTy->getContext(), StoredValSize);
    StoredVal = Helper.CreateBitCast(StoredVal, StoredValTy);
  }

  // If this is a big-endian system, we need to shift the value down to the low
  // bits so that a truncate will work.
  if (DL.isBigEndian()) {
    uint64_t ShiftAmt = DL.getTypeStoreSizeInBits(StoredValTy).getFixedValue() -
                        DL.getTypeStoreSizeInBits(LoadedTy).getFixedValue();
    StoredVal = Helper.CreateLShr(
        StoredVal, ConstantInt::get(StoredVal->getType(), ShiftAmt));
  }

  // Truncate the integer to the right size now.
  Type *NewIntTy = IntegerType::get(StoredValTy->getContext(), LoadedValSize);
  StoredVal = Helper.CreateTruncOrBitCast(StoredVal, NewIntTy);

  if (LoadedTy != NewIntTy) {
    // If the result is a pointer, inttoptr.
    if (LoadedTy->isPtrOrPtrVectorTy())
      StoredVal = Helper.CreateIntToPtr(StoredVal, LoadedTy);
    else
      // Otherwise, bitcast.
      StoredVal = Helper.CreateBitCast(StoredVal, LoadedTy);
  }

  if (auto *C = dyn_cast<Constant>(StoredVal))
    StoredVal = ConstantFoldConstant(C, DL);

  return StoredVal;
}

/// This function is called when we have a memdep query of a load that ends up
/// being a clobbering memory write (store, memset, memcpy, memmove).  This
/// means that the write *may* provide bits used by the load but we can't be
/// sure because the pointers don't must-alias.
///
/// Check this case to see if there is anything more we can do before we give
/// up.  This returns -1 if we have to give up, or a byte number in the stored
/// value of the piece that feeds the load.
static int analyzeLoadFromClobberingWrite(Type *LoadTy, Value *LoadPtr,
                                          Value *WritePtr,
                                          uint64_t WriteSizeInBits,
                                          const DataLayout &DL) {
  // If the loaded/stored value is a first class array/struct, or scalable type,
  // don't try to transform them. We need to be able to bitcast to integer.
  if (isFirstClassAggregateOrScalableType(LoadTy))
    return -1;

  int64_t StoreOffset = 0, LoadOffset = 0;
  Value *StoreBase =
      GetPointerBaseWithConstantOffset(WritePtr, StoreOffset, DL);
  Value *LoadBase = GetPointerBaseWithConstantOffset(LoadPtr, LoadOffset, DL);
  if (StoreBase != LoadBase)
    return -1;

  uint64_t LoadSize = DL.getTypeSizeInBits(LoadTy).getFixedValue();

  if ((WriteSizeInBits & 7) | (LoadSize & 7))
    return -1;
  uint64_t StoreSize = WriteSizeInBits / 8; // Convert to bytes.
  LoadSize /= 8;

  // If the Load isn't completely contained within the stored bits, we don't
  // have all the bits to feed it.  We could do something crazy in the future
  // (issue a smaller load then merge the bits in) but this seems unlikely to be
  // valuable.
  if (StoreOffset > LoadOffset ||
      StoreOffset + int64_t(StoreSize) < LoadOffset + int64_t(LoadSize))
    return -1;

  // Okay, we can do this transformation.  Return the number of bytes into the
  // store that the load is.
  return LoadOffset - StoreOffset;
}

/// This function is called when we have a
/// memdep query of a load that ends up being a clobbering store.
int analyzeLoadFromClobberingStore(Type *LoadTy, Value *LoadPtr,
                                   StoreInst *DepSI, const DataLayout &DL) {
  auto *StoredVal = DepSI->getValueOperand();

  // Cannot handle reading from store of first-class aggregate or scalable type.
  if (isFirstClassAggregateOrScalableType(StoredVal->getType()))
    return -1;

  if (!canCoerceMustAliasedValueToLoad(StoredVal, LoadTy, DL))
    return -1;

  Value *StorePtr = DepSI->getPointerOperand();
  uint64_t StoreSize =
      DL.getTypeSizeInBits(DepSI->getValueOperand()->getType()).getFixedValue();
  return analyzeLoadFromClobberingWrite(LoadTy, LoadPtr, StorePtr, StoreSize,
                                        DL);
}

/// This function is called when we have a
/// memdep query of a load that ends up being clobbered by another load.  See if
/// the other load can feed into the second load.
int analyzeLoadFromClobberingLoad(Type *LoadTy, Value *LoadPtr, LoadInst *DepLI,
                                  const DataLayout &DL) {
  // Cannot handle reading from store of first-class aggregate yet.
  if (DepLI->getType()->isStructTy() || DepLI->getType()->isArrayTy())
    return -1;

  if (!canCoerceMustAliasedValueToLoad(DepLI, LoadTy, DL))
    return -1;

  Value *DepPtr = DepLI->getPointerOperand();
  uint64_t DepSize = DL.getTypeSizeInBits(DepLI->getType()).getFixedValue();
  return analyzeLoadFromClobberingWrite(LoadTy, LoadPtr, DepPtr, DepSize, DL);
}

int analyzeLoadFromClobberingMemInst(Type *LoadTy, Value *LoadPtr,
                                     MemIntrinsic *MI, const DataLayout &DL) {
  // If the mem operation is a non-constant size, we can't handle it.
  ConstantInt *SizeCst = dyn_cast<ConstantInt>(MI->getLength());
  if (!SizeCst)
    return -1;
  uint64_t MemSizeInBits = SizeCst->getZExtValue() * 8;

  // If this is memset, we just need to see if the offset is valid in the size
  // of the memset..
  if (const auto *memset_inst = dyn_cast<MemSetInst>(MI)) {
    if (DL.isNonIntegralPointerType(LoadTy->getScalarType())) {
      auto *CI = dyn_cast<ConstantInt>(memset_inst->getValue());
      if (!CI || !CI->isZero())
        return -1;
    }
    return analyzeLoadFromClobberingWrite(LoadTy, LoadPtr, MI->getDest(),
                                          MemSizeInBits, DL);
  }

  // If we have a memcpy/memmove, the only case we can handle is if this is a
  // copy from constant memory.  In that case, we can read directly from the
  // constant memory.
  MemTransferInst *MTI = cast<MemTransferInst>(MI);

  Constant *Src = dyn_cast<Constant>(MTI->getSource());
  if (!Src)
    return -1;

  GlobalVariable *GV = dyn_cast<GlobalVariable>(getUnderlyingObject(Src));
  if (!GV || !GV->isConstant() || !GV->hasDefinitiveInitializer())
    return -1;

  // See if the access is within the bounds of the transfer.
  int Offset = analyzeLoadFromClobberingWrite(LoadTy, LoadPtr, MI->getDest(),
                                              MemSizeInBits, DL);
  if (Offset == -1)
    return Offset;

  // Otherwise, see if we can constant fold a load from the constant with the
  // offset applied as appropriate.
  unsigned IndexSize = DL.getIndexTypeSizeInBits(Src->getType());
  if (ConstantFoldLoadFromConstPtr(Src, LoadTy, APInt(IndexSize, Offset), DL))
    return Offset;
  return -1;
}

static Value *getStoreValueForLoadHelper(Value *SrcVal, unsigned Offset,
                                         Type *LoadTy, IRBuilderBase &Builder,
                                         const DataLayout &DL) {
  LLVMContext &Ctx = SrcVal->getType()->getContext();

  // If two pointers are in the same address space, they have the same size,
  // so we don't need to do any truncation, etc. This avoids introducing
  // ptrtoint instructions for pointers that may be non-integral.
  if (SrcVal->getType()->isPointerTy() && LoadTy->isPointerTy() &&
      cast<PointerType>(SrcVal->getType())->getAddressSpace() ==
          cast<PointerType>(LoadTy)->getAddressSpace()) {
    return SrcVal;
  }

  uint64_t StoreSize =
      (DL.getTypeSizeInBits(SrcVal->getType()).getFixedValue() + 7) / 8;
  uint64_t LoadSize = (DL.getTypeSizeInBits(LoadTy).getFixedValue() + 7) / 8;
  // Compute which bits of the stored value are being used by the load.  Convert
  // to an integer type to start with.
  if (SrcVal->getType()->isPtrOrPtrVectorTy())
    SrcVal =
        Builder.CreatePtrToInt(SrcVal, DL.getIntPtrType(SrcVal->getType()));
  if (!SrcVal->getType()->isIntegerTy())
    SrcVal =
        Builder.CreateBitCast(SrcVal, IntegerType::get(Ctx, StoreSize * 8));

  // Shift the bits to the least significant depending on endianness.
  unsigned ShiftAmt;
  if (DL.isLittleEndian())
    ShiftAmt = Offset * 8;
  else
    ShiftAmt = (StoreSize - LoadSize - Offset) * 8;
  if (ShiftAmt)
    SrcVal = Builder.CreateLShr(SrcVal,
                                ConstantInt::get(SrcVal->getType(), ShiftAmt));

  if (LoadSize != StoreSize)
    SrcVal = Builder.CreateTruncOrBitCast(SrcVal,
                                          IntegerType::get(Ctx, LoadSize * 8));
  return SrcVal;
}

Value *getValueForLoad(Value *SrcVal, unsigned Offset, Type *LoadTy,
                       Instruction *InsertPt, const DataLayout &DL) {

#ifndef NDEBUG
  unsigned SrcValSize = DL.getTypeStoreSize(SrcVal->getType()).getFixedValue();
  unsigned LoadSize = DL.getTypeStoreSize(LoadTy).getFixedValue();
  assert(Offset + LoadSize <= SrcValSize);
#endif
  IRBuilder<> Builder(InsertPt);
  SrcVal = getStoreValueForLoadHelper(SrcVal, Offset, LoadTy, Builder, DL);
  return coerceAvailableValueToLoadType(SrcVal, LoadTy, Builder, DL);
}

Constant *getConstantValueForLoad(Constant *SrcVal, unsigned Offset,
                                  Type *LoadTy, const DataLayout &DL) {
#ifndef NDEBUG
  unsigned SrcValSize = DL.getTypeStoreSize(SrcVal->getType()).getFixedValue();
  unsigned LoadSize = DL.getTypeStoreSize(LoadTy).getFixedValue();
  assert(Offset + LoadSize <= SrcValSize);
#endif
  return ConstantFoldLoadFromConst(SrcVal, LoadTy, APInt(32, Offset), DL);
}

/// This function is called when we have a
/// memdep query of a load that ends up being a clobbering mem intrinsic.
Value *getMemInstValueForLoad(MemIntrinsic *SrcInst, unsigned Offset,
                              Type *LoadTy, Instruction *InsertPt,
                              const DataLayout &DL) {
  LLVMContext &Ctx = LoadTy->getContext();
  uint64_t LoadSize = DL.getTypeSizeInBits(LoadTy).getFixedValue() / 8;
  IRBuilder<> Builder(InsertPt);

  // We know that this method is only called when the mem transfer fully
  // provides the bits for the load.
  if (MemSetInst *MSI = dyn_cast<MemSetInst>(SrcInst)) {
    // memset(P, 'x', 1234) -> splat('x'), even if x is a variable, and
    // independently of what the offset is.
    Value *Val = MSI->getValue();
    if (LoadSize != 1)
      Val =
          Builder.CreateZExtOrBitCast(Val, IntegerType::get(Ctx, LoadSize * 8));
    Value *OneElt = Val;

    // Splat the value out to the right number of bits.
    for (unsigned NumBytesSet = 1; NumBytesSet != LoadSize;) {
      // If we can double the number of bytes set, do it.
      if (NumBytesSet * 2 <= LoadSize) {
        Value *ShVal = Builder.CreateShl(
            Val, ConstantInt::get(Val->getType(), NumBytesSet * 8));
        Val = Builder.CreateOr(Val, ShVal);
        NumBytesSet <<= 1;
        continue;
      }

      // Otherwise insert one byte at a time.
      Value *ShVal =
          Builder.CreateShl(Val, ConstantInt::get(Val->getType(), 1 * 8));
      Val = Builder.CreateOr(OneElt, ShVal);
      ++NumBytesSet;
    }

    return coerceAvailableValueToLoadType(Val, LoadTy, Builder, DL);
  }

  // Otherwise, this is a memcpy/memmove from a constant global.
  MemTransferInst *MTI = cast<MemTransferInst>(SrcInst);
  Constant *Src = cast<Constant>(MTI->getSource());
  unsigned IndexSize = DL.getIndexTypeSizeInBits(Src->getType());
  return ConstantFoldLoadFromConstPtr(Src, LoadTy, APInt(IndexSize, Offset),
                                      DL);
}

Constant *getConstantMemInstValueForLoad(MemIntrinsic *SrcInst, unsigned Offset,
                                         Type *LoadTy, const DataLayout &DL) {
  LLVMContext &Ctx = LoadTy->getContext();
  uint64_t LoadSize = DL.getTypeSizeInBits(LoadTy).getFixedValue() / 8;

  // We know that this method is only called when the mem transfer fully
  // provides the bits for the load.
  if (MemSetInst *MSI = dyn_cast<MemSetInst>(SrcInst)) {
    auto *Val = dyn_cast<ConstantInt>(MSI->getValue());
    if (!Val)
      return nullptr;

    Val = ConstantInt::get(Ctx, APInt::getSplat(LoadSize * 8, Val->getValue()));
    return ConstantFoldLoadFromConst(Val, LoadTy, DL);
  }

  // Otherwise, this is a memcpy/memmove from a constant global.
  MemTransferInst *MTI = cast<MemTransferInst>(SrcInst);
  Constant *Src = cast<Constant>(MTI->getSource());
  unsigned IndexSize = DL.getIndexTypeSizeInBits(Src->getType());
  return ConstantFoldLoadFromConstPtr(Src, LoadTy, APInt(IndexSize, Offset),
                                      DL);
}
} // namespace VNCoercion
} // namespace llvm

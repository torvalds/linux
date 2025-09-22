//===-- CSKYTargetStreamer.h - CSKY Target Streamer ----------*- C++ -*----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_CSKYTARGETSTREAMER_H
#define LLVM_LIB_TARGET_CSKY_CSKYTARGETSTREAMER_H

#include "MCTargetDesc/CSKYMCExpr.h"
#include "llvm/MC/ConstantPools.h"
#include "llvm/MC/MCStreamer.h"

namespace llvm {

class CSKYConstantPool {
  using EntryVecTy = SmallVector<ConstantPoolEntry, 4>;
  EntryVecTy Entries;
  std::map<int64_t, const MCSymbolRefExpr *> CachedEntries;

  MCSection *CurrentSection = nullptr;

public:
  // Initialize a new empty constant pool
  CSKYConstantPool() = default;

  // Add a new entry to the constant pool in the next slot.
  // \param Value is the new entry to put in the constant pool.
  // \param Size is the size in bytes of the entry
  //
  // \returns a MCExpr that references the newly inserted value
  const MCExpr *addEntry(MCStreamer &Streamer, const MCExpr *Value,
                         unsigned Size, SMLoc Loc, const MCExpr *AdjustExpr);

  void emitAll(MCStreamer &Streamer);

  // Return true if the constant pool is empty
  bool empty();

  void clearCache();
};

class CSKYTargetStreamer : public MCTargetStreamer {
public:
  typedef struct {
    const MCSymbol *sym;
    CSKYMCExpr::VariantKind kind;
  } SymbolIndex;

protected:
  std::unique_ptr<CSKYConstantPool> ConstantPool;

  DenseMap<SymbolIndex, const MCExpr *> ConstantMap;

  unsigned ConstantCounter = 0;

public:
  CSKYTargetStreamer(MCStreamer &S);

  virtual void emitTextAttribute(unsigned Attribute, StringRef String);
  virtual void emitAttribute(unsigned Attribute, unsigned Value);
  virtual void finishAttributeSection();

  virtual void emitTargetAttributes(const MCSubtargetInfo &STI);
  /// Add a new entry to the constant pool for the current section and return an
  /// MCExpr that can be used to refer to the constant pool location.
  const MCExpr *addConstantPoolEntry(const MCExpr *, SMLoc Loc,
                                     const MCExpr *AdjustExpr = nullptr);

  void emitCurrentConstantPool();

  void finish() override;
};

template <> struct DenseMapInfo<CSKYTargetStreamer::SymbolIndex> {
  static inline CSKYTargetStreamer::SymbolIndex getEmptyKey() {
    return {nullptr, CSKYMCExpr::VK_CSKY_Invalid};
  }
  static inline CSKYTargetStreamer::SymbolIndex getTombstoneKey() {
    return {nullptr, CSKYMCExpr::VK_CSKY_Invalid};
  }
  static unsigned getHashValue(const CSKYTargetStreamer::SymbolIndex &V) {
    return hash_combine(DenseMapInfo<const MCSymbol *>::getHashValue(V.sym),
                        DenseMapInfo<int>::getHashValue(V.kind));
  }
  static bool isEqual(const CSKYTargetStreamer::SymbolIndex &A,
                      const CSKYTargetStreamer::SymbolIndex &B) {
    return A.sym == B.sym && A.kind == B.kind;
  }
};

class formatted_raw_ostream;

class CSKYTargetAsmStreamer : public CSKYTargetStreamer {
  formatted_raw_ostream &OS;

  void emitAttribute(unsigned Attribute, unsigned Value) override;
  void emitTextAttribute(unsigned Attribute, StringRef String) override;
  void finishAttributeSection() override;

public:
  CSKYTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS)
      : CSKYTargetStreamer(S), OS(OS) {}
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_CSKY_CSKYTARGETSTREAMER_H

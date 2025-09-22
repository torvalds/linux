#ifndef LLVM_DWP_DWPSTRINGPOOL_H
#define LLVM_DWP_DWPSTRINGPOOL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include <cassert>

namespace llvm {
class DWPStringPool {

  struct CStrDenseMapInfo {
    static inline const char *getEmptyKey() {
      return reinterpret_cast<const char *>(~static_cast<uintptr_t>(0));
    }
    static inline const char *getTombstoneKey() {
      return reinterpret_cast<const char *>(~static_cast<uintptr_t>(1));
    }
    static unsigned getHashValue(const char *Val) {
      assert(Val != getEmptyKey() && "Cannot hash the empty key!");
      assert(Val != getTombstoneKey() && "Cannot hash the tombstone key!");
      return (unsigned)hash_value(StringRef(Val));
    }
    static bool isEqual(const char *LHS, const char *RHS) {
      if (RHS == getEmptyKey())
        return LHS == getEmptyKey();
      if (RHS == getTombstoneKey())
        return LHS == getTombstoneKey();
      return strcmp(LHS, RHS) == 0;
    }
  };

  MCStreamer &Out;
  MCSection *Sec;
  DenseMap<const char *, uint32_t, CStrDenseMapInfo> Pool;
  uint32_t Offset = 0;

public:
  DWPStringPool(MCStreamer &Out, MCSection *Sec) : Out(Out), Sec(Sec) {}

  uint32_t getOffset(const char *Str, unsigned Length) {
    assert(strlen(Str) + 1 == Length && "Ensure length hint is correct");

    auto Pair = Pool.insert(std::make_pair(Str, Offset));
    if (Pair.second) {
      Out.switchSection(Sec);
      Out.emitBytes(StringRef(Str, Length));
      Offset += Length;
    }

    return Pair.first->second;
  }
};
} // namespace llvm

#endif // LLVM_DWP_DWPSTRINGPOOL_H

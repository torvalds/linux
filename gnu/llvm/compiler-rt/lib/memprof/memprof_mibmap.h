#ifndef MEMPROF_MIBMAP_H_
#define MEMPROF_MIBMAP_H_

#include <stdint.h>

#include "profile/MemProfData.inc"
#include "sanitizer_common/sanitizer_addrhashmap.h"
#include "sanitizer_common/sanitizer_mutex.h"

namespace __memprof {

struct LockedMemInfoBlock {
  __sanitizer::StaticSpinMutex mutex;
  ::llvm::memprof::MemInfoBlock mib;
};

// The MIB map stores a mapping from stack ids to MemInfoBlocks.
typedef __sanitizer::AddrHashMap<LockedMemInfoBlock *, 200003> MIBMapTy;

// Insert a new MemInfoBlock or merge with an existing block identified by the
// stack id.
void InsertOrMerge(const uptr Id, const ::llvm::memprof::MemInfoBlock &Block,
                   MIBMapTy &Map);

} // namespace __memprof

#endif // MEMPROF_MIBMAP_H_

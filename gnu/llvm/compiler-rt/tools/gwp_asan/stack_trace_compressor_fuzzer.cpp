#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gwp_asan/stack_trace_compressor.h"

constexpr size_t kBytesForLargestVarInt = (sizeof(uintptr_t) * 8) / 7 + 1;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  size_t BufferSize = kBytesForLargestVarInt * Size / sizeof(uintptr_t);
  std::vector<uint8_t> Buffer(BufferSize);
  std::vector<uint8_t> Buffer2(BufferSize);

  // Unpack the fuzz bytes.
  gwp_asan::compression::unpack(Data, Size,
                                reinterpret_cast<uintptr_t *>(Buffer2.data()),
                                BufferSize / sizeof(uintptr_t));

  // Pack the fuzz bytes.
  size_t BytesWritten = gwp_asan::compression::pack(
      reinterpret_cast<const uintptr_t *>(Data), Size / sizeof(uintptr_t),
      Buffer.data(), BufferSize);

  // Unpack the compressed buffer.
  size_t DecodedElements = gwp_asan::compression::unpack(
      Buffer.data(), BytesWritten,
      reinterpret_cast<uintptr_t *>(Buffer2.data()),
      BufferSize / sizeof(uintptr_t));

  // Ensure that every element was encoded and decoded properly.
  if (DecodedElements != Size / sizeof(uintptr_t))
    abort();

  // Ensure that the compression and uncompression resulted in the same trace.
  const uintptr_t *FuzzPtrs = reinterpret_cast<const uintptr_t *>(Data);
  const uintptr_t *DecodedPtrs =
      reinterpret_cast<const uintptr_t *>(Buffer2.data());
  for (size_t i = 0; i < Size / sizeof(uintptr_t); ++i) {
    if (FuzzPtrs[i] != DecodedPtrs[i]) {
      fprintf(stderr, "FuzzPtrs[%zu] != DecodedPtrs[%zu] (0x%zx vs. 0x%zx)", i,
              i, FuzzPtrs[i], DecodedPtrs[i]);
      abort();
    }
  }

  return 0;
}

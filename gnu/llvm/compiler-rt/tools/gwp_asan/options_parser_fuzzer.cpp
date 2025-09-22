#include <cstddef>
#include <cstdint>

#include <fuzzer/FuzzedDataProvider.h>

#include "gwp_asan/optional/options_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  FuzzedDataProvider Fdp(Data, Size);
  gwp_asan::options::initOptions(Fdp.ConsumeRemainingBytesAsString().c_str());
  return 0;
}

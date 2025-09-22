//===- FuzzerExtFunctionsDlsym.cpp - Interface to external functions ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Implementation for operating systems that support dlsym(). We only use it on
// Apple platforms for now. We don't use this approach on Linux because it
// requires that clients of LibFuzzer pass ``--export-dynamic`` to the linker.
// That is a complication we don't wish to expose to clients right now.
//===----------------------------------------------------------------------===//
#include "FuzzerPlatform.h"
#if LIBFUZZER_APPLE

#include "FuzzerExtFunctions.h"
#include "FuzzerIO.h"
#include <dlfcn.h>

using namespace fuzzer;

template <typename T>
static T GetFnPtr(const char *FnName, bool WarnIfMissing) {
  dlerror(); // Clear any previous errors.
  void *Fn = dlsym(RTLD_DEFAULT, FnName);
  if (Fn == nullptr) {
    if (WarnIfMissing) {
      const char *ErrorMsg = dlerror();
      Printf("WARNING: Failed to find function \"%s\".", FnName);
      if (ErrorMsg)
        Printf(" Reason %s.", ErrorMsg);
      Printf("\n");
    }
  }
  return reinterpret_cast<T>(Fn);
}

namespace fuzzer {

ExternalFunctions::ExternalFunctions() {
#define EXT_FUNC(NAME, RETURN_TYPE, FUNC_SIG, WARN)                            \
  this->NAME = GetFnPtr<decltype(ExternalFunctions::NAME)>(#NAME, WARN)

#include "FuzzerExtFunctions.def"

#undef EXT_FUNC
}

} // namespace fuzzer

#endif // LIBFUZZER_APPLE

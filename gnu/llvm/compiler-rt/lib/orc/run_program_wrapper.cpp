//===- run_program_wrapper.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime support library.
//
//===----------------------------------------------------------------------===//

#include "adt.h"
#include "common.h"
#include "wrapper_function_utils.h"

#include <vector>

using namespace __orc_rt;

extern "C" int64_t __orc_rt_run_program(const char *JITDylibName,
                                        const char *EntrySymbolName, int argc,
                                        char *argv[]);

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_run_program_wrapper(const char *ArgData, size_t ArgSize) {
  return WrapperFunction<int64_t(SPSString, SPSString,
                                 SPSSequence<SPSString>)>::
      handle(ArgData, ArgSize,
             [](const std::string &JITDylibName,
                const std::string &EntrySymbolName,
                const std::vector<std::string_view> &Args) {
               std::vector<std::unique_ptr<char[]>> ArgVStorage;
               ArgVStorage.reserve(Args.size());
               for (auto &Arg : Args) {
                 ArgVStorage.push_back(
                     std::make_unique<char[]>(Arg.size() + 1));
                 memcpy(ArgVStorage.back().get(), Arg.data(), Arg.size());
                 ArgVStorage.back()[Arg.size()] = '\0';
               }
               std::vector<char *> ArgV;
               ArgV.reserve(ArgVStorage.size() + 1);
               for (auto &ArgStorage : ArgVStorage)
                 ArgV.push_back(ArgStorage.get());
               ArgV.push_back(nullptr);
               return __orc_rt_run_program(JITDylibName.c_str(),
                                           EntrySymbolName.c_str(),
                                           ArgV.size() - 1, ArgV.data());
             })
          .release();
}

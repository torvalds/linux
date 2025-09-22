/*===- DataFlow.cpp - a standalone DataFlow tracer                  -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// An experimental data-flow tracer for fuzz targets.
// It is based on DFSan and SanitizerCoverage.
// https://clang.llvm.org/docs/DataFlowSanitizer.html
// https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-data-flow
//
// It executes the fuzz target on the given input while monitoring the
// data flow for every instrumented comparison instruction.
//
// The output shows which functions depend on which bytes of the input,
// and also provides basic-block coverage for every input.
//
// Build:
//   1. Compile this file (DataFlow.cpp) with -fsanitize=dataflow and -O2.
//   2. Compile DataFlowCallbacks.cpp with -O2 -fPIC.
//   3. Build the fuzz target with -g -fsanitize=dataflow
//       -fsanitize-coverage=trace-pc-guard,pc-table,bb,trace-cmp
//   4. Link those together with -fsanitize=dataflow
//
//  -fsanitize-coverage=trace-cmp inserts callbacks around every comparison
//  instruction, DFSan modifies the calls to pass the data flow labels.
//  The callbacks update the data flow label for the current function.
//  See e.g. __dfsw___sanitizer_cov_trace_cmp1 below.
//
//  -fsanitize-coverage=trace-pc-guard,pc-table,bb instruments function
//  entries so that the comparison callback knows that current function.
//  -fsanitize-coverage=...,bb also allows to collect basic block coverage.
//
//
// Run:
//   # Collect data flow and coverage for INPUT_FILE
//   # write to OUTPUT_FILE (default: stdout)
//   export DFSAN_OPTIONS=warn_unimplemented=0
//   ./a.out INPUT_FILE [OUTPUT_FILE]
//
//   # Print all instrumented functions. llvm-symbolizer must be present in PATH
//   ./a.out
//
// Example output:
// ===============
//  F0 11111111111111
//  F1 10000000000000
//  C0 1 2 3 4 5
//  C1 8
//  ===============
// "FN xxxxxxxxxx": tells what bytes of the input does the function N depend on.
// "CN X Y Z T": tells that a function N has basic blocks X, Y, and Z covered
//    in addition to the function's entry block, out of T total instrumented
//    blocks.
//
//===----------------------------------------------------------------------===*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <execinfo.h>  // backtrace_symbols_fd

#include "DataFlow.h"

extern "C" {
extern int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size);
__attribute__((weak)) extern int LLVMFuzzerInitialize(int *argc, char ***argv);
} // extern "C"

CallbackData __dft;
static size_t InputLen;
static size_t NumIterations;
static dfsan_label **FuncLabelsPerIter;  // NumIterations x NumFuncs;

static inline bool BlockIsEntry(size_t BlockIdx) {
  return __dft.PCsBeg[BlockIdx * 2 + 1] & PCFLAG_FUNC_ENTRY;
}

const int kNumLabels = 8;

// Prints all instrumented functions.
static int PrintFunctions() {
  // We don't have the symbolizer integrated with dfsan yet.
  // So use backtrace_symbols_fd and pipe it through llvm-symbolizer.
  // TODO(kcc): this is pretty ugly and may break in lots of ways.
  //      We'll need to make a proper in-process symbolizer work with DFSan.
  FILE *Pipe = popen("sed 's/(+/ /g; s/).*//g' "
                     "| llvm-symbolizer "
                     "| grep '\\.dfsan' "
                     "| sed 's/\\.dfsan//g' "
                     "| c++filt",
                     "w");
  for (size_t I = 0; I < __dft.NumGuards; I++) {
    uintptr_t PC = __dft.PCsBeg[I * 2];
    if (!BlockIsEntry(I)) continue;
    void *const Buf[1] = {(void*)PC};
    backtrace_symbols_fd(Buf, 1, fileno(Pipe));
  }
  pclose(Pipe);
  return 0;
}

static void PrintBinary(FILE *Out, dfsan_label L, size_t Len) {
  char buf[kNumLabels + 1];
  assert(Len <= kNumLabels);
  for (int i = 0; i < kNumLabels; i++)
    buf[i] = (L & (1 << i)) ? '1' : '0';
  buf[Len] = 0;
  fprintf(Out, "%s", buf);
}

static void PrintDataFlow(FILE *Out) {
  for (size_t Func = 0; Func < __dft.NumFuncs; Func++) {
    bool HasAny = false;
    for (size_t Iter = 0; Iter < NumIterations; Iter++)
      if (FuncLabelsPerIter[Iter][Func])
        HasAny = true;
    if (!HasAny)
      continue;
    fprintf(Out, "F%zd ", Func);
    size_t LenOfLastIteration = kNumLabels;
    if (auto Tail = InputLen % kNumLabels)
        LenOfLastIteration = Tail;
    for (size_t Iter = 0; Iter < NumIterations; Iter++)
      PrintBinary(Out, FuncLabelsPerIter[Iter][Func],
                  Iter == NumIterations - 1 ? LenOfLastIteration : kNumLabels);
    fprintf(Out, "\n");
  }
}

static void PrintCoverage(FILE *Out) {
  ssize_t CurrentFuncGuard = -1;
  ssize_t CurrentFuncNum = -1;
  ssize_t NumBlocksInCurrentFunc = -1;
  for (size_t FuncBeg = 0; FuncBeg < __dft.NumGuards;) {
    CurrentFuncNum++;
    assert(BlockIsEntry(FuncBeg));
    size_t FuncEnd = FuncBeg + 1;
    for (; FuncEnd < __dft.NumGuards && !BlockIsEntry(FuncEnd); FuncEnd++)
      ;
    if (__dft.BBExecuted[FuncBeg]) {
      fprintf(Out, "C%zd", CurrentFuncNum);
      for (size_t I = FuncBeg + 1; I < FuncEnd; I++)
        if (__dft.BBExecuted[I])
          fprintf(Out, " %zd", I - FuncBeg);
      fprintf(Out, " %zd\n", FuncEnd - FuncBeg);
    }
    FuncBeg = FuncEnd;
  }
}

int main(int argc, char **argv) {
  if (LLVMFuzzerInitialize)
    LLVMFuzzerInitialize(&argc, &argv);
  if (argc == 1)
    return PrintFunctions();
  assert(argc == 2 || argc == 3);

  const char *Input = argv[1];
  fprintf(stderr, "INFO: reading '%s'\n", Input);
  FILE *In = fopen(Input, "r");
  assert(In);
  fseek(In, 0, SEEK_END);
  InputLen = ftell(In);
  fseek(In, 0, SEEK_SET);
  unsigned char *Buf = (unsigned char*)malloc(InputLen);
  size_t NumBytesRead = fread(Buf, 1, InputLen, In);
  assert(NumBytesRead == InputLen);
  fclose(In);

  NumIterations = (NumBytesRead + kNumLabels - 1) / kNumLabels;
  FuncLabelsPerIter =
      (dfsan_label **)calloc(NumIterations, sizeof(dfsan_label *));
  for (size_t Iter = 0; Iter < NumIterations; Iter++)
    FuncLabelsPerIter[Iter] =
        (dfsan_label *)calloc(__dft.NumFuncs, sizeof(dfsan_label));

  for (size_t Iter = 0; Iter < NumIterations; Iter++) {
    fprintf(stderr, "INFO: running '%s' %zd/%zd\n", Input, Iter, NumIterations);
    dfsan_flush();
    dfsan_set_label(0, Buf, InputLen);
    __dft.FuncLabels = FuncLabelsPerIter[Iter];

    size_t BaseIdx = Iter * kNumLabels;
    size_t LastIdx = BaseIdx + kNumLabels < NumBytesRead ? BaseIdx + kNumLabels
                                                         : NumBytesRead;
    assert(BaseIdx < LastIdx);
    for (size_t Idx = BaseIdx; Idx < LastIdx; Idx++)
      dfsan_set_label(1 << (Idx - BaseIdx), Buf + Idx, 1);
    LLVMFuzzerTestOneInput(Buf, InputLen);
  }
  free(Buf);

  bool OutIsStdout = argc == 2;
  fprintf(stderr, "INFO: writing dataflow to %s\n",
          OutIsStdout ? "<stdout>" : argv[2]);
  FILE *Out = OutIsStdout ? stdout : fopen(argv[2], "w");
  PrintDataFlow(Out);
  PrintCoverage(Out);
  if (!OutIsStdout) fclose(Out);
}

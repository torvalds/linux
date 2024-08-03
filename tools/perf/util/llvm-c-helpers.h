/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_LLVM_C_HELPERS
#define __PERF_LLVM_C_HELPERS 1

/*
 * Helpers to call into LLVM C++ code from C, for the parts that do not have
 * C APIs.
 */

#include <linux/compiler.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dso;

struct llvm_a2l_frame {
  char* filename;
  char* funcname;
  unsigned int line;
};

/*
 * Implement addr2line() using libLLVM. LLVM is a C++ API, and
 * many of the linux/ headers cannot be included in a C++ compile unit,
 * so we need to make a little bridge code here. llvm_addr2line() will
 * convert the inline frame information from LLVM's internal structures
 * and put them into a flat array given in inline_frames. The caller
 * is then responsible for taking that array and convert it into perf's
 * regular inline frame structures (which depend on e.g. struct list_head).
 *
 * If the address could not be resolved, or an error occurred (e.g. OOM),
 * returns 0. Otherwise, returns the number of inline frames (which means 1
 * if the address was not part of an inlined function). If unwind_inlines
 * is set and the return code is nonzero, inline_frames will be set to
 * a newly allocated array with that length. The caller is then responsible
 * for freeing both the strings and the array itself.
 */
int llvm_addr2line(const char* dso_name,
                   u64 addr,
                   char** file,
                   unsigned int* line,
                   bool unwind_inlines,
                   struct llvm_a2l_frame** inline_frames);

/*
 * Simple symbolizers for addresses; will convert something like
 * 0x12345 to "func+0x123". Will return NULL if no symbol was found.
 *
 * The returned value must be freed by the caller, with free().
 */
char *llvm_name_for_code(struct dso *dso, const char *dso_name, u64 addr);
char *llvm_name_for_data(struct dso *dso, const char *dso_name, u64 addr);

#ifdef __cplusplus
}
#endif

#endif /* __PERF_LLVM_C_HELPERS */

// SPDX-License-Identifier: GPL-2.0

/*
 * Must come before the linux/compiler.h include, which defines several
 * macros (e.g. noinline) that conflict with compiler builtins used
 * by LLVM.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"  /* Needed for LLVM <= 15 */
#include <llvm/DebugInfo/Symbolize/Symbolize.h>
#include <llvm/Support/TargetSelect.h>
#pragma GCC diagnostic pop

#include <inttypes.h>
#include <stdio.h>
#include <sys/types.h>
#include <linux/compiler.h>
extern "C" {
#include <linux/zalloc.h>
}
#include "symbol_conf.h"
#include "llvm-c-helpers.h"

extern "C"
char *dso__demangle_sym(struct dso *dso, int kmodule, const char *elf_name);

using namespace llvm;
using llvm::symbolize::LLVMSymbolizer;

/*
 * Allocate a static LLVMSymbolizer, which will live to the end of the program.
 * Unlike the bfd paths, LLVMSymbolizer has its own cache, so we do not need
 * to store anything in the dso struct.
 */
static LLVMSymbolizer *get_symbolizer()
{
	static LLVMSymbolizer *instance = nullptr;
	if (instance == nullptr) {
		LLVMSymbolizer::Options opts;
		/*
		 * LLVM sometimes demangles slightly different from the rest
		 * of the code, and this mismatch can cause new_inline_sym()
		 * to get confused and mark non-inline symbol as inlined
		 * (since the name does not properly match up with base_sym).
		 * Thus, disable the demangling and let the rest of the code
		 * handle it.
		 */
		opts.Demangle = false;
		instance = new LLVMSymbolizer(opts);
	}
	return instance;
}

/* Returns 0 on error, 1 on success. */
static int extract_file_and_line(const DILineInfo &line_info, char **file,
				 unsigned int *line)
{
	if (file) {
		if (line_info.FileName == "<invalid>") {
			/* Match the convention of libbfd. */
			*file = nullptr;
		} else {
			/* The caller expects to get something it can free(). */
			*file = strdup(line_info.FileName.c_str());
			if (*file == nullptr)
				return 0;
		}
	}
	if (line)
		*line = line_info.Line;
	return 1;
}

extern "C"
int llvm_addr2line(const char *dso_name, u64 addr,
		   char **file, unsigned int *line,
		   bool unwind_inlines,
		   llvm_a2l_frame **inline_frames)
{
	LLVMSymbolizer *symbolizer = get_symbolizer();
	object::SectionedAddress sectioned_addr = {
		addr,
		object::SectionedAddress::UndefSection
	};

	if (unwind_inlines) {
		Expected<DIInliningInfo> res_or_err =
			symbolizer->symbolizeInlinedCode(dso_name,
							 sectioned_addr);
		if (!res_or_err)
			return 0;
		unsigned num_frames = res_or_err->getNumberOfFrames();
		if (num_frames == 0)
			return 0;

		if (extract_file_and_line(res_or_err->getFrame(0),
					  file, line) == 0)
			return 0;

		*inline_frames = (llvm_a2l_frame *)calloc(
			num_frames, sizeof(**inline_frames));
		if (*inline_frames == nullptr)
			return 0;

		for (unsigned i = 0; i < num_frames; ++i) {
			const DILineInfo &src = res_or_err->getFrame(i);

			llvm_a2l_frame &dst = (*inline_frames)[i];
			if (src.FileName == "<invalid>")
				/* Match the convention of libbfd. */
				dst.filename = nullptr;
			else
				dst.filename = strdup(src.FileName.c_str());
			dst.funcname = strdup(src.FunctionName.c_str());
			dst.line = src.Line;

			if (dst.filename == nullptr ||
			    dst.funcname == nullptr) {
				for (unsigned j = 0; j <= i; ++j) {
					zfree(&(*inline_frames)[j].filename);
					zfree(&(*inline_frames)[j].funcname);
				}
				zfree(inline_frames);
				return 0;
			}
		}

		return num_frames;
	} else {
		if (inline_frames)
			*inline_frames = nullptr;

		Expected<DILineInfo> res_or_err =
			symbolizer->symbolizeCode(dso_name, sectioned_addr);
		if (!res_or_err)
			return 0;
		return extract_file_and_line(*res_or_err, file, line);
	}
}

static char *
make_symbol_relative_string(struct dso *dso, const char *sym_name,
			    u64 addr, u64 base_addr)
{
	if (!strcmp(sym_name, "<invalid>"))
		return NULL;

	char *demangled = dso__demangle_sym(dso, 0, sym_name);
	if (base_addr && base_addr != addr) {
		char buf[256];
		snprintf(buf, sizeof(buf), "%s+0x%" PRIx64,
			 demangled ? demangled : sym_name, addr - base_addr);
		free(demangled);
		return strdup(buf);
	} else {
		if (demangled)
			return demangled;
		else
			return strdup(sym_name);
	}
}

extern "C"
char *llvm_name_for_code(struct dso *dso, const char *dso_name, u64 addr)
{
	LLVMSymbolizer *symbolizer = get_symbolizer();
	object::SectionedAddress sectioned_addr = {
		addr,
		object::SectionedAddress::UndefSection
	};
	Expected<DILineInfo> res_or_err =
		symbolizer->symbolizeCode(dso_name, sectioned_addr);
	if (!res_or_err) {
		return NULL;
	}
	return make_symbol_relative_string(
		dso, res_or_err->FunctionName.c_str(),
		addr, res_or_err->StartAddress ? *res_or_err->StartAddress : 0);
}

extern "C"
char *llvm_name_for_data(struct dso *dso, const char *dso_name, u64 addr)
{
	LLVMSymbolizer *symbolizer = get_symbolizer();
	object::SectionedAddress sectioned_addr = {
		addr,
		object::SectionedAddress::UndefSection
	};
	Expected<DIGlobal> res_or_err =
		symbolizer->symbolizeData(dso_name, sectioned_addr);
	if (!res_or_err) {
		return NULL;
	}
	return make_symbol_relative_string(
		dso, res_or_err->Name.c_str(),
		addr, res_or_err->Start);
}

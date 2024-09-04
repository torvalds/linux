// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <test_progs.h>

#ifdef HAVE_LLVM_SUPPORT

#include <llvm-c/Core.h>
#include <llvm-c/Disassembler.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

/* The intent is to use get_jited_program_text() for small test
 * programs written in BPF assembly, thus assume that 32 local labels
 * would be sufficient.
 */
#define MAX_LOCAL_LABELS 32

/* Local labels are encoded as 'L42', this requires 4 bytes of storage:
 * 3 characters + zero byte
 */
#define LOCAL_LABEL_LEN 4

static bool llvm_initialized;

struct local_labels {
	bool print_phase;
	__u32 prog_len;
	__u32 cnt;
	__u32 pcs[MAX_LOCAL_LABELS];
	char names[MAX_LOCAL_LABELS][LOCAL_LABEL_LEN];
};

static const char *lookup_symbol(void *data, uint64_t ref_value, uint64_t *ref_type,
				 uint64_t ref_pc, const char **ref_name)
{
	struct local_labels *labels = data;
	uint64_t type = *ref_type;
	int i;

	*ref_type = LLVMDisassembler_ReferenceType_InOut_None;
	*ref_name = NULL;
	if (type != LLVMDisassembler_ReferenceType_In_Branch)
		return NULL;
	/* Depending on labels->print_phase either discover local labels or
	 * return a name assigned with local jump target:
	 * - if print_phase is true and ref_value is in labels->pcs,
	 *   return corresponding labels->name.
	 * - if print_phase is false, save program-local jump targets
	 *   in labels->pcs;
	 */
	if (labels->print_phase) {
		for (i = 0; i < labels->cnt; ++i)
			if (labels->pcs[i] == ref_value)
				return labels->names[i];
	} else {
		if (labels->cnt < MAX_LOCAL_LABELS && ref_value < labels->prog_len)
			labels->pcs[labels->cnt++] = ref_value;
	}
	return NULL;
}

static int disasm_insn(LLVMDisasmContextRef ctx, uint8_t *image, __u32 len, __u32 pc,
		       char *buf, __u32 buf_sz)
{
	int i, cnt;

	cnt = LLVMDisasmInstruction(ctx, image + pc, len - pc, pc,
				    buf, buf_sz);
	if (cnt > 0)
		return cnt;
	PRINT_FAIL("Can't disasm instruction at offset %d:", pc);
	for (i = 0; i < 16 && pc + i < len; ++i)
		printf(" %02x", image[pc + i]);
	printf("\n");
	return -EINVAL;
}

static int cmp_u32(const void *_a, const void *_b)
{
	__u32 a = *(__u32 *)_a;
	__u32 b = *(__u32 *)_b;

	if (a < b)
		return -1;
	if (a > b)
		return 1;
	return 0;
}

static int disasm_one_func(FILE *text_out, uint8_t *image, __u32 len)
{
	char *label, *colon, *triple = NULL;
	LLVMDisasmContextRef ctx = NULL;
	struct local_labels labels = {};
	__u32 *label_pc, pc;
	int i, cnt, err = 0;
	char buf[64];

	triple = LLVMGetDefaultTargetTriple();
	ctx = LLVMCreateDisasm(triple, &labels, 0, NULL, lookup_symbol);
	if (!ASSERT_OK_PTR(ctx, "LLVMCreateDisasm")) {
		err = -EINVAL;
		goto out;
	}

	cnt = LLVMSetDisasmOptions(ctx, LLVMDisassembler_Option_PrintImmHex);
	if (!ASSERT_EQ(cnt, 1, "LLVMSetDisasmOptions")) {
		err = -EINVAL;
		goto out;
	}

	/* discover labels */
	labels.prog_len = len;
	pc = 0;
	while (pc < len) {
		cnt = disasm_insn(ctx, image, len, pc, buf, 1);
		if (cnt < 0) {
			err = cnt;
			goto out;
		}
		pc += cnt;
	}
	qsort(labels.pcs, labels.cnt, sizeof(*labels.pcs), cmp_u32);
	for (i = 0; i < labels.cnt; ++i)
		/* gcc is unable to infer upper bound for labels.cnt and assumes
		 * it to be U32_MAX. U32_MAX takes 10 decimal digits.
		 * snprintf below prints into labels.names[*],
		 * which has space only for two digits and a letter.
		 * To avoid truncation warning use (i % MAX_LOCAL_LABELS),
		 * which informs gcc about printed value upper bound.
		 */
		snprintf(labels.names[i], sizeof(labels.names[i]), "L%d", i % MAX_LOCAL_LABELS);

	/* now print with labels */
	labels.print_phase = true;
	pc = 0;
	while (pc < len) {
		cnt = disasm_insn(ctx, image, len, pc, buf, sizeof(buf));
		if (cnt < 0) {
			err = cnt;
			goto out;
		}
		label_pc = bsearch(&pc, labels.pcs, labels.cnt, sizeof(*labels.pcs), cmp_u32);
		label = "";
		colon = "";
		if (label_pc) {
			label = labels.names[label_pc - labels.pcs];
			colon = ":";
		}
		fprintf(text_out, "%x:\t", pc);
		for (i = 0; i < cnt; ++i)
			fprintf(text_out, "%02x ", image[pc + i]);
		for (i = cnt * 3; i < 12 * 3; ++i)
			fputc(' ', text_out);
		fprintf(text_out, "%s%s%s\n", label, colon, buf);
		pc += cnt;
	}

out:
	if (triple)
		LLVMDisposeMessage(triple);
	if (ctx)
		LLVMDisasmDispose(ctx);
	return err;
}

int get_jited_program_text(int fd, char *text, size_t text_sz)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	__u32 jited_funcs, len, pc;
	__u32 *func_lens = NULL;
	FILE *text_out = NULL;
	uint8_t *image = NULL;
	int i, err = 0;

	if (!llvm_initialized) {
		LLVMInitializeAllTargetInfos();
		LLVMInitializeAllTargetMCs();
		LLVMInitializeAllDisassemblers();
		llvm_initialized = 1;
	}

	text_out = fmemopen(text, text_sz, "w");
	if (!ASSERT_OK_PTR(text_out, "open_memstream")) {
		err = -errno;
		goto out;
	}

	/* first call is to find out jited program len */
	err = bpf_prog_get_info_by_fd(fd, &info, &info_len);
	if (!ASSERT_OK(err, "bpf_prog_get_info_by_fd #1"))
		goto out;

	len = info.jited_prog_len;
	image = malloc(len);
	if (!ASSERT_OK_PTR(image, "malloc(info.jited_prog_len)")) {
		err = -ENOMEM;
		goto out;
	}

	jited_funcs = info.nr_jited_func_lens;
	func_lens = malloc(jited_funcs * sizeof(__u32));
	if (!ASSERT_OK_PTR(func_lens, "malloc(info.nr_jited_func_lens)")) {
		err = -ENOMEM;
		goto out;
	}

	memset(&info, 0, sizeof(info));
	info.jited_prog_insns = (__u64)image;
	info.jited_prog_len = len;
	info.jited_func_lens = (__u64)func_lens;
	info.nr_jited_func_lens = jited_funcs;
	err = bpf_prog_get_info_by_fd(fd, &info, &info_len);
	if (!ASSERT_OK(err, "bpf_prog_get_info_by_fd #2"))
		goto out;

	for (pc = 0, i = 0; i < jited_funcs; ++i) {
		fprintf(text_out, "func #%d:\n", i);
		disasm_one_func(text_out, image + pc, func_lens[i]);
		fprintf(text_out, "\n");
		pc += func_lens[i];
	}

out:
	if (text_out)
		fclose(text_out);
	if (image)
		free(image);
	if (func_lens)
		free(func_lens);
	return err;
}

#else /* HAVE_LLVM_SUPPORT */

int get_jited_program_text(int fd, char *text, size_t text_sz)
{
	if (env.verbosity >= VERBOSE_VERY)
		printf("compiled w/o llvm development libraries, can't dis-assembly binary code");
	return -EOPNOTSUPP;
}

#endif /* HAVE_LLVM_SUPPORT */

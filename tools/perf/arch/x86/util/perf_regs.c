#include <string.h>

#include "../../perf.h"
#include "../../util/util.h"
#include "../../util/perf_regs.h"

const struct sample_reg sample_reg_masks[] = {
	SMPL_REG(AX, PERF_REG_X86_AX),
	SMPL_REG(BX, PERF_REG_X86_BX),
	SMPL_REG(CX, PERF_REG_X86_CX),
	SMPL_REG(DX, PERF_REG_X86_DX),
	SMPL_REG(SI, PERF_REG_X86_SI),
	SMPL_REG(DI, PERF_REG_X86_DI),
	SMPL_REG(BP, PERF_REG_X86_BP),
	SMPL_REG(SP, PERF_REG_X86_SP),
	SMPL_REG(IP, PERF_REG_X86_IP),
	SMPL_REG(FLAGS, PERF_REG_X86_FLAGS),
	SMPL_REG(CS, PERF_REG_X86_CS),
	SMPL_REG(SS, PERF_REG_X86_SS),
#ifdef HAVE_ARCH_X86_64_SUPPORT
	SMPL_REG(R8, PERF_REG_X86_R8),
	SMPL_REG(R9, PERF_REG_X86_R9),
	SMPL_REG(R10, PERF_REG_X86_R10),
	SMPL_REG(R11, PERF_REG_X86_R11),
	SMPL_REG(R12, PERF_REG_X86_R12),
	SMPL_REG(R13, PERF_REG_X86_R13),
	SMPL_REG(R14, PERF_REG_X86_R14),
	SMPL_REG(R15, PERF_REG_X86_R15),
#endif
	SMPL_REG_END
};

struct sdt_name_reg {
	const char *sdt_name;
	const char *uprobe_name;
};
#define SDT_NAME_REG(n, m) {.sdt_name = "%" #n, .uprobe_name = "%" #m}
#define SDT_NAME_REG_END {.sdt_name = NULL, .uprobe_name = NULL}

static const struct sdt_name_reg sdt_reg_renamings[] = {
	SDT_NAME_REG(eax, ax),
	SDT_NAME_REG(rax, ax),
	SDT_NAME_REG(ebx, bx),
	SDT_NAME_REG(rbx, bx),
	SDT_NAME_REG(ecx, cx),
	SDT_NAME_REG(rcx, cx),
	SDT_NAME_REG(edx, dx),
	SDT_NAME_REG(rdx, dx),
	SDT_NAME_REG(esi, si),
	SDT_NAME_REG(rsi, si),
	SDT_NAME_REG(edi, di),
	SDT_NAME_REG(rdi, di),
	SDT_NAME_REG(ebp, bp),
	SDT_NAME_REG(rbp, bp),
	SDT_NAME_REG_END,
};

int sdt_rename_register(char **pdesc, char *old_name)
{
	const struct sdt_name_reg *rnames = sdt_reg_renamings;
	char *new_desc, *old_desc = *pdesc;
	size_t prefix_len, sdt_len, uprobe_len, old_desc_len, offset;
	int ret = -1;

	while (ret != 0 && rnames->sdt_name != NULL) {
		sdt_len = strlen(rnames->sdt_name);
		ret = strncmp(old_name, rnames->sdt_name, sdt_len);
		rnames += !!ret;
	}

	if (rnames->sdt_name == NULL)
		return 0;

	sdt_len = strlen(rnames->sdt_name);
	uprobe_len = strlen(rnames->uprobe_name);
	old_desc_len = strlen(old_desc) + 1;

	new_desc = zalloc(old_desc_len + uprobe_len - sdt_len);
	if (new_desc == NULL)
		return -1;

	/* Copy the chars before the register name (at least '%') */
	prefix_len = old_name - old_desc;
	memcpy(new_desc, old_desc, prefix_len);

	/* Copy the new register name */
	memcpy(new_desc + prefix_len, rnames->uprobe_name, uprobe_len);

	/* Copy the chars after the register name (if need be) */
	offset = prefix_len + sdt_len;
	if (offset < old_desc_len) {
		/*
		 * The orginal register name can be suffixed by 'b',
		 * 'w' or 'd' to indicate its size; so, we need to
		 * skip this char if we met one.
		 */
		char sfx = old_desc[offset];

		if (sfx == 'b' || sfx == 'w'  || sfx == 'd')
			offset++;
	}

	if (offset < old_desc_len)
		memcpy(new_desc + prefix_len + uprobe_len,
			old_desc + offset, old_desc_len - offset);

	free(old_desc);
	*pdesc = new_desc;

	return 0;
}

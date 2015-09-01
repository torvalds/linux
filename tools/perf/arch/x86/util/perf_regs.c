#include "../../perf.h"
#include "../../util/perf_regs.h"

#define REG(n, b) { .name = #n, .mask = 1ULL << (b) }
#define REG_END { .name = NULL }
const struct sample_reg sample_reg_masks[] = {
	REG(AX, PERF_REG_X86_AX),
	REG(BX, PERF_REG_X86_BX),
	REG(CX, PERF_REG_X86_CX),
	REG(DX, PERF_REG_X86_DX),
	REG(SI, PERF_REG_X86_SI),
	REG(DI, PERF_REG_X86_DI),
	REG(BP, PERF_REG_X86_BP),
	REG(SP, PERF_REG_X86_SP),
	REG(IP, PERF_REG_X86_IP),
	REG(FLAGS, PERF_REG_X86_FLAGS),
	REG(CS, PERF_REG_X86_CS),
	REG(SS, PERF_REG_X86_SS),
#ifdef HAVE_ARCH_X86_64_SUPPORT
	REG(R8, PERF_REG_X86_R8),
	REG(R9, PERF_REG_X86_R9),
	REG(R10, PERF_REG_X86_R10),
	REG(R11, PERF_REG_X86_R11),
	REG(R12, PERF_REG_X86_R12),
	REG(R13, PERF_REG_X86_R13),
	REG(R14, PERF_REG_X86_R14),
	REG(R15, PERF_REG_X86_R15),
#endif
	REG_END
};

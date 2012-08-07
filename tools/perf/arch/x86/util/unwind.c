
#include <errno.h>
#include <libunwind.h>
#include "perf_regs.h"
#include "../../util/unwind.h"

#ifdef ARCH_X86_64
int unwind__arch_reg_id(int regnum)
{
	int id;

	switch (regnum) {
	case UNW_X86_64_RAX:
		id = PERF_REG_X86_AX;
		break;
	case UNW_X86_64_RDX:
		id = PERF_REG_X86_DX;
		break;
	case UNW_X86_64_RCX:
		id = PERF_REG_X86_CX;
		break;
	case UNW_X86_64_RBX:
		id = PERF_REG_X86_BX;
		break;
	case UNW_X86_64_RSI:
		id = PERF_REG_X86_SI;
		break;
	case UNW_X86_64_RDI:
		id = PERF_REG_X86_DI;
		break;
	case UNW_X86_64_RBP:
		id = PERF_REG_X86_BP;
		break;
	case UNW_X86_64_RSP:
		id = PERF_REG_X86_SP;
		break;
	case UNW_X86_64_R8:
		id = PERF_REG_X86_R8;
		break;
	case UNW_X86_64_R9:
		id = PERF_REG_X86_R9;
		break;
	case UNW_X86_64_R10:
		id = PERF_REG_X86_R10;
		break;
	case UNW_X86_64_R11:
		id = PERF_REG_X86_R11;
		break;
	case UNW_X86_64_R12:
		id = PERF_REG_X86_R12;
		break;
	case UNW_X86_64_R13:
		id = PERF_REG_X86_R13;
		break;
	case UNW_X86_64_R14:
		id = PERF_REG_X86_R14;
		break;
	case UNW_X86_64_R15:
		id = PERF_REG_X86_R15;
		break;
	case UNW_X86_64_RIP:
		id = PERF_REG_X86_IP;
		break;
	default:
		pr_err("unwind: invalid reg id %d\n", regnum);
		return -EINVAL;
	}

	return id;
}
#else
int unwind__arch_reg_id(int regnum)
{
	int id;

	switch (regnum) {
	case UNW_X86_EAX:
		id = PERF_REG_X86_AX;
		break;
	case UNW_X86_EDX:
		id = PERF_REG_X86_DX;
		break;
	case UNW_X86_ECX:
		id = PERF_REG_X86_CX;
		break;
	case UNW_X86_EBX:
		id = PERF_REG_X86_BX;
		break;
	case UNW_X86_ESI:
		id = PERF_REG_X86_SI;
		break;
	case UNW_X86_EDI:
		id = PERF_REG_X86_DI;
		break;
	case UNW_X86_EBP:
		id = PERF_REG_X86_BP;
		break;
	case UNW_X86_ESP:
		id = PERF_REG_X86_SP;
		break;
	case UNW_X86_EIP:
		id = PERF_REG_X86_IP;
		break;
	default:
		pr_err("unwind: invalid reg id %d\n", regnum);
		return -EINVAL;
	}

	return id;
}
#endif /* ARCH_X86_64 */

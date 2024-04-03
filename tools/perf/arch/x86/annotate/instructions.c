// SPDX-License-Identifier: GPL-2.0
/*
 * x86 instruction nmemonic table to parse disasm lines for annotate.
 * This table is searched twice - one for exact match and another for
 * match without a size suffix (b, w, l, q) in case of AT&T syntax.
 *
 * So this table should not have entries with the suffix unless it's
 * a complete different instruction than ones without the suffix.
 */
static struct ins x86__instructions[] = {
	{ .name = "adc",	.ops = &mov_ops,  },
	{ .name = "add",	.ops = &mov_ops,  },
	{ .name = "addsd",	.ops = &mov_ops,  },
	{ .name = "and",	.ops = &mov_ops,  },
	{ .name = "andpd",	.ops = &mov_ops,  },
	{ .name = "andps",	.ops = &mov_ops,  },
	{ .name = "bsr",	.ops = &mov_ops,  },
	{ .name = "bt",		.ops = &mov_ops,  },
	{ .name = "btr",	.ops = &mov_ops,  },
	{ .name = "bts",	.ops = &mov_ops,  },
	{ .name = "call",	.ops = &call_ops, },
	{ .name = "cmovbe",	.ops = &mov_ops,  },
	{ .name = "cmove",	.ops = &mov_ops,  },
	{ .name = "cmovae",	.ops = &mov_ops,  },
	{ .name = "cmp",	.ops = &mov_ops,  },
	{ .name = "cmpxch",	.ops = &mov_ops,  },
	{ .name = "cmpxchg",	.ops = &mov_ops,  },
	{ .name = "cs",		.ops = &mov_ops,  },
	{ .name = "dec",	.ops = &dec_ops,  },
	{ .name = "divsd",	.ops = &mov_ops,  },
	{ .name = "divss",	.ops = &mov_ops,  },
	{ .name = "gs",		.ops = &mov_ops,  },
	{ .name = "imul",	.ops = &mov_ops,  },
	{ .name = "inc",	.ops = &dec_ops,  },
	{ .name = "ja",		.ops = &jump_ops, },
	{ .name = "jae",	.ops = &jump_ops, },
	{ .name = "jb",		.ops = &jump_ops, },
	{ .name = "jbe",	.ops = &jump_ops, },
	{ .name = "jc",		.ops = &jump_ops, },
	{ .name = "jcxz",	.ops = &jump_ops, },
	{ .name = "je",		.ops = &jump_ops, },
	{ .name = "jecxz",	.ops = &jump_ops, },
	{ .name = "jg",		.ops = &jump_ops, },
	{ .name = "jge",	.ops = &jump_ops, },
	{ .name = "jl",		.ops = &jump_ops, },
	{ .name = "jle",	.ops = &jump_ops, },
	{ .name = "jmp",	.ops = &jump_ops, },
	{ .name = "jna",	.ops = &jump_ops, },
	{ .name = "jnae",	.ops = &jump_ops, },
	{ .name = "jnb",	.ops = &jump_ops, },
	{ .name = "jnbe",	.ops = &jump_ops, },
	{ .name = "jnc",	.ops = &jump_ops, },
	{ .name = "jne",	.ops = &jump_ops, },
	{ .name = "jng",	.ops = &jump_ops, },
	{ .name = "jnge",	.ops = &jump_ops, },
	{ .name = "jnl",	.ops = &jump_ops, },
	{ .name = "jnle",	.ops = &jump_ops, },
	{ .name = "jno",	.ops = &jump_ops, },
	{ .name = "jnp",	.ops = &jump_ops, },
	{ .name = "jns",	.ops = &jump_ops, },
	{ .name = "jnz",	.ops = &jump_ops, },
	{ .name = "jo",		.ops = &jump_ops, },
	{ .name = "jp",		.ops = &jump_ops, },
	{ .name = "jpe",	.ops = &jump_ops, },
	{ .name = "jpo",	.ops = &jump_ops, },
	{ .name = "jrcxz",	.ops = &jump_ops, },
	{ .name = "js",		.ops = &jump_ops, },
	{ .name = "jz",		.ops = &jump_ops, },
	{ .name = "lea",	.ops = &mov_ops,  },
	{ .name = "lock",	.ops = &lock_ops, },
	{ .name = "mov",	.ops = &mov_ops,  },
	{ .name = "movapd",	.ops = &mov_ops,  },
	{ .name = "movaps",	.ops = &mov_ops,  },
	{ .name = "movdqa",	.ops = &mov_ops,  },
	{ .name = "movdqu",	.ops = &mov_ops,  },
	{ .name = "movsd",	.ops = &mov_ops,  },
	{ .name = "movss",	.ops = &mov_ops,  },
	{ .name = "movsb",	.ops = &mov_ops,  },
	{ .name = "movsw",	.ops = &mov_ops,  },
	{ .name = "movsl",	.ops = &mov_ops,  },
	{ .name = "movupd",	.ops = &mov_ops,  },
	{ .name = "movups",	.ops = &mov_ops,  },
	{ .name = "movzb",	.ops = &mov_ops,  },
	{ .name = "movzw",	.ops = &mov_ops,  },
	{ .name = "movzl",	.ops = &mov_ops,  },
	{ .name = "mulsd",	.ops = &mov_ops,  },
	{ .name = "mulss",	.ops = &mov_ops,  },
	{ .name = "nop",	.ops = &nop_ops,  },
	{ .name = "or",		.ops = &mov_ops,  },
	{ .name = "orps",	.ops = &mov_ops,  },
	{ .name = "pand",	.ops = &mov_ops,  },
	{ .name = "paddq",	.ops = &mov_ops,  },
	{ .name = "pcmpeqb",	.ops = &mov_ops,  },
	{ .name = "por",	.ops = &mov_ops,  },
	{ .name = "rcl",	.ops = &mov_ops,  },
	{ .name = "ret",	.ops = &ret_ops,  },
	{ .name = "sbb",	.ops = &mov_ops,  },
	{ .name = "sete",	.ops = &mov_ops,  },
	{ .name = "sub",	.ops = &mov_ops,  },
	{ .name = "subsd",	.ops = &mov_ops,  },
	{ .name = "test",	.ops = &mov_ops,  },
	{ .name = "tzcnt",	.ops = &mov_ops,  },
	{ .name = "ucomisd",	.ops = &mov_ops,  },
	{ .name = "ucomiss",	.ops = &mov_ops,  },
	{ .name = "vaddsd",	.ops = &mov_ops,  },
	{ .name = "vandpd",	.ops = &mov_ops,  },
	{ .name = "vmovdqa",	.ops = &mov_ops,  },
	{ .name = "vmovq",	.ops = &mov_ops,  },
	{ .name = "vmovsd",	.ops = &mov_ops,  },
	{ .name = "vmulsd",	.ops = &mov_ops,  },
	{ .name = "vorpd",	.ops = &mov_ops,  },
	{ .name = "vsubsd",	.ops = &mov_ops,  },
	{ .name = "vucomisd",	.ops = &mov_ops,  },
	{ .name = "xadd",	.ops = &mov_ops,  },
	{ .name = "xbegin",	.ops = &jump_ops, },
	{ .name = "xchg",	.ops = &mov_ops,  },
	{ .name = "xor",	.ops = &mov_ops, },
	{ .name = "xorpd",	.ops = &mov_ops, },
	{ .name = "xorps",	.ops = &mov_ops, },
};

static bool amd__ins_is_fused(struct arch *arch, const char *ins1,
			      const char *ins2)
{
	if (strstr(ins2, "jmp"))
		return false;

	/* Family >= 15h supports cmp/test + branch fusion */
	if (arch->family >= 0x15 && (strstarts(ins1, "test") ||
	    (strstarts(ins1, "cmp") && !strstr(ins1, "xchg")))) {
		return true;
	}

	/* Family >= 19h supports some ALU + branch fusion */
	if (arch->family >= 0x19 && (strstarts(ins1, "add") ||
	    strstarts(ins1, "sub") || strstarts(ins1, "and") ||
	    strstarts(ins1, "inc") || strstarts(ins1, "dec") ||
	    strstarts(ins1, "or") || strstarts(ins1, "xor"))) {
		return true;
	}

	return false;
}

static bool intel__ins_is_fused(struct arch *arch, const char *ins1,
				const char *ins2)
{
	if (arch->family != 6 || arch->model < 0x1e || strstr(ins2, "jmp"))
		return false;

	if (arch->model == 0x1e) {
		/* Nehalem */
		if ((strstr(ins1, "cmp") && !strstr(ins1, "xchg")) ||
		     strstr(ins1, "test")) {
			return true;
		}
	} else {
		/* Newer platform */
		if ((strstr(ins1, "cmp") && !strstr(ins1, "xchg")) ||
		     strstr(ins1, "test") ||
		     strstr(ins1, "add") ||
		     strstr(ins1, "sub") ||
		     strstr(ins1, "and") ||
		     strstr(ins1, "inc") ||
		     strstr(ins1, "dec")) {
			return true;
		}
	}

	return false;
}

static int x86__cpuid_parse(struct arch *arch, char *cpuid)
{
	unsigned int family, model, stepping;
	int ret;

	/*
	 * cpuid = "GenuineIntel,family,model,stepping"
	 */
	ret = sscanf(cpuid, "%*[^,],%u,%u,%u", &family, &model, &stepping);
	if (ret == 3) {
		arch->family = family;
		arch->model = model;
		arch->ins_is_fused = strstarts(cpuid, "AuthenticAMD") ?
					amd__ins_is_fused :
					intel__ins_is_fused;
		return 0;
	}

	return -1;
}

static int x86__annotate_init(struct arch *arch, char *cpuid)
{
	int err = 0;

	if (arch->initialized)
		return 0;

	if (cpuid) {
		if (x86__cpuid_parse(arch, cpuid))
			err = SYMBOL_ANNOTATE_ERRNO__ARCH_INIT_CPUID_PARSING;
	}

	arch->initialized = true;
	return err;
}

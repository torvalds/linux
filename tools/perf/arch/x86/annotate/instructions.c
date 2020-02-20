// SPDX-License-Identifier: GPL-2.0
static struct ins x86__instructions[] = {
	{ .name = "adc",	.ops = &mov_ops,  },
	{ .name = "adcb",	.ops = &mov_ops,  },
	{ .name = "adcl",	.ops = &mov_ops,  },
	{ .name = "add",	.ops = &mov_ops,  },
	{ .name = "addl",	.ops = &mov_ops,  },
	{ .name = "addq",	.ops = &mov_ops,  },
	{ .name = "addsd",	.ops = &mov_ops,  },
	{ .name = "addw",	.ops = &mov_ops,  },
	{ .name = "and",	.ops = &mov_ops,  },
	{ .name = "andb",	.ops = &mov_ops,  },
	{ .name = "andl",	.ops = &mov_ops,  },
	{ .name = "andpd",	.ops = &mov_ops,  },
	{ .name = "andps",	.ops = &mov_ops,  },
	{ .name = "andq",	.ops = &mov_ops,  },
	{ .name = "andw",	.ops = &mov_ops,  },
	{ .name = "bsr",	.ops = &mov_ops,  },
	{ .name = "bt",		.ops = &mov_ops,  },
	{ .name = "btr",	.ops = &mov_ops,  },
	{ .name = "bts",	.ops = &mov_ops,  },
	{ .name = "btsq",	.ops = &mov_ops,  },
	{ .name = "call",	.ops = &call_ops, },
	{ .name = "callq",	.ops = &call_ops, },
	{ .name = "cmovbe",	.ops = &mov_ops,  },
	{ .name = "cmove",	.ops = &mov_ops,  },
	{ .name = "cmovae",	.ops = &mov_ops,  },
	{ .name = "cmp",	.ops = &mov_ops,  },
	{ .name = "cmpb",	.ops = &mov_ops,  },
	{ .name = "cmpl",	.ops = &mov_ops,  },
	{ .name = "cmpq",	.ops = &mov_ops,  },
	{ .name = "cmpw",	.ops = &mov_ops,  },
	{ .name = "cmpxch",	.ops = &mov_ops,  },
	{ .name = "cmpxchg",	.ops = &mov_ops,  },
	{ .name = "cs",		.ops = &mov_ops,  },
	{ .name = "dec",	.ops = &dec_ops,  },
	{ .name = "decl",	.ops = &dec_ops,  },
	{ .name = "divsd",	.ops = &mov_ops,  },
	{ .name = "divss",	.ops = &mov_ops,  },
	{ .name = "gs",		.ops = &mov_ops,  },
	{ .name = "imul",	.ops = &mov_ops,  },
	{ .name = "inc",	.ops = &dec_ops,  },
	{ .name = "incl",	.ops = &dec_ops,  },
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
	{ .name = "jmpq",	.ops = &jump_ops, },
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
	{ .name = "movb",	.ops = &mov_ops,  },
	{ .name = "movdqa",	.ops = &mov_ops,  },
	{ .name = "movdqu",	.ops = &mov_ops,  },
	{ .name = "movl",	.ops = &mov_ops,  },
	{ .name = "movq",	.ops = &mov_ops,  },
	{ .name = "movsd",	.ops = &mov_ops,  },
	{ .name = "movslq",	.ops = &mov_ops,  },
	{ .name = "movss",	.ops = &mov_ops,  },
	{ .name = "movupd",	.ops = &mov_ops,  },
	{ .name = "movups",	.ops = &mov_ops,  },
	{ .name = "movw",	.ops = &mov_ops,  },
	{ .name = "movzbl",	.ops = &mov_ops,  },
	{ .name = "movzwl",	.ops = &mov_ops,  },
	{ .name = "mulsd",	.ops = &mov_ops,  },
	{ .name = "mulss",	.ops = &mov_ops,  },
	{ .name = "nop",	.ops = &nop_ops,  },
	{ .name = "nopl",	.ops = &nop_ops,  },
	{ .name = "nopw",	.ops = &nop_ops,  },
	{ .name = "or",		.ops = &mov_ops,  },
	{ .name = "orb",	.ops = &mov_ops,  },
	{ .name = "orl",	.ops = &mov_ops,  },
	{ .name = "orps",	.ops = &mov_ops,  },
	{ .name = "orq",	.ops = &mov_ops,  },
	{ .name = "pand",	.ops = &mov_ops,  },
	{ .name = "paddq",	.ops = &mov_ops,  },
	{ .name = "pcmpeqb",	.ops = &mov_ops,  },
	{ .name = "por",	.ops = &mov_ops,  },
	{ .name = "rclb",	.ops = &mov_ops,  },
	{ .name = "rcll",	.ops = &mov_ops,  },
	{ .name = "retq",	.ops = &ret_ops,  },
	{ .name = "sbb",	.ops = &mov_ops,  },
	{ .name = "sbbl",	.ops = &mov_ops,  },
	{ .name = "sete",	.ops = &mov_ops,  },
	{ .name = "sub",	.ops = &mov_ops,  },
	{ .name = "subl",	.ops = &mov_ops,  },
	{ .name = "subq",	.ops = &mov_ops,  },
	{ .name = "subsd",	.ops = &mov_ops,  },
	{ .name = "subw",	.ops = &mov_ops,  },
	{ .name = "test",	.ops = &mov_ops,  },
	{ .name = "testb",	.ops = &mov_ops,  },
	{ .name = "testl",	.ops = &mov_ops,  },
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
	{ .name = "xbeginl",	.ops = &jump_ops, },
	{ .name = "xbeginq",	.ops = &jump_ops, },
	{ .name = "xchg",	.ops = &mov_ops,  },
	{ .name = "xor",	.ops = &mov_ops, },
	{ .name = "xorb",	.ops = &mov_ops, },
	{ .name = "xorpd",	.ops = &mov_ops, },
	{ .name = "xorps",	.ops = &mov_ops, },
};

static bool x86__ins_is_fused(struct arch *arch, const char *ins1,
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

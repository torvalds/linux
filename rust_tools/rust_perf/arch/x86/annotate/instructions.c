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
	arch->e_machine = EM_X86_64;
	arch->e_flags = 0;
	arch->initialized = true;
	return err;
}

#ifdef HAVE_LIBDW_SUPPORT
static void update_insn_state_x86(struct type_state *state,
				  struct data_loc_info *dloc, Dwarf_Die *cu_die,
				  struct disasm_line *dl)
{
	struct annotated_insn_loc loc;
	struct annotated_op_loc *src = &loc.ops[INSN_OP_SOURCE];
	struct annotated_op_loc *dst = &loc.ops[INSN_OP_TARGET];
	struct type_state_reg *tsr;
	Dwarf_Die type_die;
	u32 insn_offset = dl->al.offset;
	int fbreg = dloc->fbreg;
	int fboff = 0;

	if (annotate_get_insn_location(dloc->arch, dl, &loc) < 0)
		return;

	if (ins__is_call(&dl->ins)) {
		struct symbol *func = dl->ops.target.sym;

		if (func == NULL)
			return;

		/* __fentry__ will preserve all registers */
		if (!strcmp(func->name, "__fentry__"))
			return;

		pr_debug_dtp("call [%x] %s\n", insn_offset, func->name);

		/* Otherwise invalidate caller-saved registers after call */
		for (unsigned i = 0; i < ARRAY_SIZE(state->regs); i++) {
			if (state->regs[i].caller_saved)
				state->regs[i].ok = false;
		}

		/* Update register with the return type (if any) */
		if (die_find_func_rettype(cu_die, func->name, &type_die)) {
			tsr = &state->regs[state->ret_reg];
			tsr->type = type_die;
			tsr->kind = TSR_KIND_TYPE;
			tsr->ok = true;

			pr_debug_dtp("call [%x] return -> reg%d",
				     insn_offset, state->ret_reg);
			pr_debug_type_name(&type_die, tsr->kind);
		}
		return;
	}

	if (!strncmp(dl->ins.name, "add", 3)) {
		u64 imm_value = -1ULL;
		int offset;
		const char *var_name = NULL;
		struct map_symbol *ms = dloc->ms;
		u64 ip = ms->sym->start + dl->al.offset;

		if (!has_reg_type(state, dst->reg1))
			return;

		tsr = &state->regs[dst->reg1];
		tsr->copied_from = -1;

		if (src->imm)
			imm_value = src->offset;
		else if (has_reg_type(state, src->reg1) &&
			 state->regs[src->reg1].kind == TSR_KIND_CONST)
			imm_value = state->regs[src->reg1].imm_value;
		else if (src->reg1 == DWARF_REG_PC) {
			u64 var_addr = annotate_calc_pcrel(dloc->ms, ip,
							   src->offset, dl);

			if (get_global_var_info(dloc, var_addr,
						&var_name, &offset) &&
			    !strcmp(var_name, "this_cpu_off") &&
			    tsr->kind == TSR_KIND_CONST) {
				tsr->kind = TSR_KIND_PERCPU_BASE;
				tsr->ok = true;
				imm_value = tsr->imm_value;
			}
		}
		else
			return;

		if (tsr->kind != TSR_KIND_PERCPU_BASE)
			return;

		if (get_global_var_type(cu_die, dloc, ip, imm_value, &offset,
					&type_die) && offset == 0) {
			/*
			 * This is not a pointer type, but it should be treated
			 * as a pointer.
			 */
			tsr->type = type_die;
			tsr->kind = TSR_KIND_POINTER;
			tsr->ok = true;

			pr_debug_dtp("add [%x] percpu %#"PRIx64" -> reg%d",
				     insn_offset, imm_value, dst->reg1);
			pr_debug_type_name(&tsr->type, tsr->kind);
		}
		return;
	}

	if (strncmp(dl->ins.name, "mov", 3))
		return;

	if (dloc->fb_cfa) {
		u64 ip = dloc->ms->sym->start + dl->al.offset;
		u64 pc = map__rip_2objdump(dloc->ms->map, ip);

		if (die_get_cfa(dloc->di->dbg, pc, &fbreg, &fboff) < 0)
			fbreg = -1;
	}

	/* Case 1. register to register or segment:offset to register transfers */
	if (!src->mem_ref && !dst->mem_ref) {
		if (!has_reg_type(state, dst->reg1))
			return;

		tsr = &state->regs[dst->reg1];
		tsr->copied_from = -1;

		if (dso__kernel(map__dso(dloc->ms->map)) &&
		    src->segment == INSN_SEG_X86_GS && src->imm) {
			u64 ip = dloc->ms->sym->start + dl->al.offset;
			u64 var_addr;
			int offset;

			/*
			 * In kernel, %gs points to a per-cpu region for the
			 * current CPU.  Access with a constant offset should
			 * be treated as a global variable access.
			 */
			var_addr = src->offset;

			if (var_addr == 40) {
				tsr->kind = TSR_KIND_CANARY;
				tsr->ok = true;

				pr_debug_dtp("mov [%x] stack canary -> reg%d\n",
					     insn_offset, dst->reg1);
				return;
			}

			if (!get_global_var_type(cu_die, dloc, ip, var_addr,
						 &offset, &type_die) ||
			    !die_get_member_type(&type_die, offset, &type_die)) {
				tsr->ok = false;
				return;
			}

			tsr->type = type_die;
			tsr->kind = TSR_KIND_TYPE;
			tsr->ok = true;

			pr_debug_dtp("mov [%x] this-cpu addr=%#"PRIx64" -> reg%d",
				     insn_offset, var_addr, dst->reg1);
			pr_debug_type_name(&tsr->type, tsr->kind);
			return;
		}

		if (src->imm) {
			tsr->kind = TSR_KIND_CONST;
			tsr->imm_value = src->offset;
			tsr->ok = true;

			pr_debug_dtp("mov [%x] imm=%#x -> reg%d\n",
				     insn_offset, tsr->imm_value, dst->reg1);
			return;
		}

		if (!has_reg_type(state, src->reg1) ||
		    !state->regs[src->reg1].ok) {
			tsr->ok = false;
			return;
		}

		tsr->type = state->regs[src->reg1].type;
		tsr->kind = state->regs[src->reg1].kind;
		tsr->imm_value = state->regs[src->reg1].imm_value;
		tsr->ok = true;

		/* To copy back the variable type later (hopefully) */
		if (tsr->kind == TSR_KIND_TYPE)
			tsr->copied_from = src->reg1;

		pr_debug_dtp("mov [%x] reg%d -> reg%d",
			     insn_offset, src->reg1, dst->reg1);
		pr_debug_type_name(&tsr->type, tsr->kind);
	}
	/* Case 2. memory to register transers */
	if (src->mem_ref && !dst->mem_ref) {
		int sreg = src->reg1;

		if (!has_reg_type(state, dst->reg1))
			return;

		tsr = &state->regs[dst->reg1];
		tsr->copied_from = -1;

retry:
		/* Check stack variables with offset */
		if (sreg == fbreg || sreg == state->stack_reg) {
			struct type_state_stack *stack;
			int offset = src->offset - fboff;

			stack = find_stack_state(state, offset);
			if (stack == NULL) {
				tsr->ok = false;
				return;
			} else if (!stack->compound) {
				tsr->type = stack->type;
				tsr->kind = stack->kind;
				tsr->ok = true;
			} else if (die_get_member_type(&stack->type,
						       offset - stack->offset,
						       &type_die)) {
				tsr->type = type_die;
				tsr->kind = TSR_KIND_TYPE;
				tsr->ok = true;
			} else {
				tsr->ok = false;
				return;
			}

			if (sreg == fbreg) {
				pr_debug_dtp("mov [%x] -%#x(stack) -> reg%d",
					     insn_offset, -offset, dst->reg1);
			} else {
				pr_debug_dtp("mov [%x] %#x(reg%d) -> reg%d",
					     insn_offset, offset, sreg, dst->reg1);
			}
			pr_debug_type_name(&tsr->type, tsr->kind);
		}
		/* And then dereference the pointer if it has one */
		else if (has_reg_type(state, sreg) && state->regs[sreg].ok &&
			 state->regs[sreg].kind == TSR_KIND_TYPE &&
			 die_deref_ptr_type(&state->regs[sreg].type,
					    src->offset, &type_die)) {
			tsr->type = type_die;
			tsr->kind = TSR_KIND_TYPE;
			tsr->ok = true;

			pr_debug_dtp("mov [%x] %#x(reg%d) -> reg%d",
				     insn_offset, src->offset, sreg, dst->reg1);
			pr_debug_type_name(&tsr->type, tsr->kind);
		}
		/* Or check if it's a global variable */
		else if (sreg == DWARF_REG_PC) {
			struct map_symbol *ms = dloc->ms;
			u64 ip = ms->sym->start + dl->al.offset;
			u64 addr;
			int offset;

			addr = annotate_calc_pcrel(ms, ip, src->offset, dl);

			if (!get_global_var_type(cu_die, dloc, ip, addr, &offset,
						 &type_die) ||
			    !die_get_member_type(&type_die, offset, &type_die)) {
				tsr->ok = false;
				return;
			}

			tsr->type = type_die;
			tsr->kind = TSR_KIND_TYPE;
			tsr->ok = true;

			pr_debug_dtp("mov [%x] global addr=%"PRIx64" -> reg%d",
				     insn_offset, addr, dst->reg1);
			pr_debug_type_name(&type_die, tsr->kind);
		}
		/* And check percpu access with base register */
		else if (has_reg_type(state, sreg) &&
			 state->regs[sreg].kind == TSR_KIND_PERCPU_BASE) {
			u64 ip = dloc->ms->sym->start + dl->al.offset;
			u64 var_addr = src->offset;
			int offset;

			if (src->multi_regs) {
				int reg2 = (sreg == src->reg1) ? src->reg2 : src->reg1;

				if (has_reg_type(state, reg2) && state->regs[reg2].ok &&
				    state->regs[reg2].kind == TSR_KIND_CONST)
					var_addr += state->regs[reg2].imm_value;
			}

			/*
			 * In kernel, %gs points to a per-cpu region for the
			 * current CPU.  Access with a constant offset should
			 * be treated as a global variable access.
			 */
			if (get_global_var_type(cu_die, dloc, ip, var_addr,
						&offset, &type_die) &&
			    die_get_member_type(&type_die, offset, &type_die)) {
				tsr->type = type_die;
				tsr->kind = TSR_KIND_TYPE;
				tsr->ok = true;

				if (src->multi_regs) {
					pr_debug_dtp("mov [%x] percpu %#x(reg%d,reg%d) -> reg%d",
						     insn_offset, src->offset, src->reg1,
						     src->reg2, dst->reg1);
				} else {
					pr_debug_dtp("mov [%x] percpu %#x(reg%d) -> reg%d",
						     insn_offset, src->offset, sreg, dst->reg1);
				}
				pr_debug_type_name(&tsr->type, tsr->kind);
			} else {
				tsr->ok = false;
			}
		}
		/* And then dereference the calculated pointer if it has one */
		else if (has_reg_type(state, sreg) && state->regs[sreg].ok &&
			 state->regs[sreg].kind == TSR_KIND_POINTER &&
			 die_get_member_type(&state->regs[sreg].type,
					     src->offset, &type_die)) {
			tsr->type = type_die;
			tsr->kind = TSR_KIND_TYPE;
			tsr->ok = true;

			pr_debug_dtp("mov [%x] pointer %#x(reg%d) -> reg%d",
				     insn_offset, src->offset, sreg, dst->reg1);
			pr_debug_type_name(&tsr->type, tsr->kind);
		}
		/* Or try another register if any */
		else if (src->multi_regs && sreg == src->reg1 &&
			 src->reg1 != src->reg2) {
			sreg = src->reg2;
			goto retry;
		}
		else {
			int offset;
			const char *var_name = NULL;

			/* it might be per-cpu variable (in kernel) access */
			if (src->offset < 0) {
				if (get_global_var_info(dloc, (s64)src->offset,
							&var_name, &offset) &&
				    !strcmp(var_name, "__per_cpu_offset")) {
					tsr->kind = TSR_KIND_PERCPU_BASE;
					tsr->ok = true;

					pr_debug_dtp("mov [%x] percpu base reg%d\n",
						     insn_offset, dst->reg1);
					return;
				}
			}

			tsr->ok = false;
		}
	}
	/* Case 3. register to memory transfers */
	if (!src->mem_ref && dst->mem_ref) {
		if (!has_reg_type(state, src->reg1) ||
		    !state->regs[src->reg1].ok)
			return;

		/* Check stack variables with offset */
		if (dst->reg1 == fbreg || dst->reg1 == state->stack_reg) {
			struct type_state_stack *stack;
			int offset = dst->offset - fboff;

			tsr = &state->regs[src->reg1];

			stack = find_stack_state(state, offset);
			if (stack) {
				/*
				 * The source register is likely to hold a type
				 * of member if it's a compound type.  Do not
				 * update the stack variable type since we can
				 * get the member type later by using the
				 * die_get_member_type().
				 */
				if (!stack->compound)
					set_stack_state(stack, offset, tsr->kind,
							&tsr->type);
			} else {
				findnew_stack_state(state, offset, tsr->kind,
						    &tsr->type);
			}

			if (dst->reg1 == fbreg) {
				pr_debug_dtp("mov [%x] reg%d -> -%#x(stack)",
					     insn_offset, src->reg1, -offset);
			} else {
				pr_debug_dtp("mov [%x] reg%d -> %#x(reg%d)",
					     insn_offset, src->reg1, offset, dst->reg1);
			}
			pr_debug_type_name(&tsr->type, tsr->kind);
		}
		/*
		 * Ignore other transfers since it'd set a value in a struct
		 * and won't change the type.
		 */
	}
	/* Case 4. memory to memory transfers (not handled for now) */
}
#endif

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Traceprobe fetch helper inlines
 */

static nokprobe_inline void
fetch_store_raw(unsigned long val, struct fetch_insn *code, void *buf)
{
	switch (code->size) {
	case 1:
		*(u8 *)buf = (u8)val;
		break;
	case 2:
		*(u16 *)buf = (u16)val;
		break;
	case 4:
		*(u32 *)buf = (u32)val;
		break;
	case 8:
		//TBD: 32bit signed
		*(u64 *)buf = (u64)val;
		break;
	default:
		*(unsigned long *)buf = val;
	}
}

static nokprobe_inline void
fetch_apply_bitfield(struct fetch_insn *code, void *buf)
{
	switch (code->basesize) {
	case 1:
		*(u8 *)buf <<= code->lshift;
		*(u8 *)buf >>= code->rshift;
		break;
	case 2:
		*(u16 *)buf <<= code->lshift;
		*(u16 *)buf >>= code->rshift;
		break;
	case 4:
		*(u32 *)buf <<= code->lshift;
		*(u32 *)buf >>= code->rshift;
		break;
	case 8:
		*(u64 *)buf <<= code->lshift;
		*(u64 *)buf >>= code->rshift;
		break;
	}
}

/* Define this for each callsite */
static int
process_fetch_insn(struct fetch_insn *code, struct pt_regs *regs,
		   void *dest, bool pre);

/* Sum up total data length for dynamic arraies (strings) */
static nokprobe_inline int
__get_data_size(struct trace_probe *tp, struct pt_regs *regs)
{
	struct probe_arg *arg;
	int i, ret = 0;
	u32 len;

	for (i = 0; i < tp->nr_args; i++) {
		arg = tp->args + i;
		if (unlikely(arg->dynamic)) {
			process_fetch_insn(arg->code, regs, &len, true);
			ret += len;
		}
	}

	return ret;
}

/* Store the value of each argument */
static nokprobe_inline void
store_trace_args(int ent_size, struct trace_probe *tp, struct pt_regs *regs,
		 u8 *data, int maxlen)
{
	struct probe_arg *arg;
	u32 end = tp->size;
	u32 *dl;	/* Data (relative) location */
	int i;

	for (i = 0; i < tp->nr_args; i++) {
		arg = tp->args + i;
		if (unlikely(arg->dynamic)) {
			/*
			 * First, we set the relative location and
			 * maximum data length to *dl
			 */
			dl = (u32 *)(data + arg->offset);
			*dl = make_data_rloc(maxlen, end - arg->offset);
			/* Then try to fetch string or dynamic array data */
			process_fetch_insn(arg->code, regs, dl, false);
			/* Reduce maximum length */
			end += get_rloc_len(*dl);
			maxlen -= get_rloc_len(*dl);
			/* Trick here, convert data_rloc to data_loc */
			*dl = convert_rloc_to_loc(*dl, ent_size + arg->offset);
		} else
			/* Just fetching data normally */
			process_fetch_insn(arg->code, regs, data + arg->offset,
					   false);
	}
}

static inline int
print_probe_args(struct trace_seq *s, struct probe_arg *args, int nr_args,
		 u8 *data, void *field)
{
	int i;

	for (i = 0; i < nr_args; i++) {
		trace_seq_printf(s, " %s=", args[i].name);
		if (!args[i].type->print(s, data + args[i].offset, field))
			return -ENOMEM;
	}
	return 0;
}

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

/*
 * This must be defined for each callsite.
 * Return consumed dynamic data size (>= 0), or error (< 0).
 * If dest is NULL, don't store result and return required dynamic data size.
 */
static int
process_fetch_insn(struct fetch_insn *code, struct pt_regs *regs,
		   void *dest, void *base);

/* Sum up total data length for dynamic arraies (strings) */
static nokprobe_inline int
__get_data_size(struct trace_probe *tp, struct pt_regs *regs)
{
	struct probe_arg *arg;
	int i, len, ret = 0;

	for (i = 0; i < tp->nr_args; i++) {
		arg = tp->args + i;
		if (unlikely(arg->dynamic)) {
			len = process_fetch_insn(arg->code, regs, NULL, NULL);
			if (len > 0)
				ret += len;
		}
	}

	return ret;
}

/* Store the value of each argument */
static nokprobe_inline void
store_trace_args(void *data, struct trace_probe *tp, struct pt_regs *regs,
		 int header_size, int maxlen)
{
	struct probe_arg *arg;
	void *base = data - header_size;
	void *dyndata = data + tp->size;
	u32 *dl;	/* Data location */
	int ret, i;

	for (i = 0; i < tp->nr_args; i++) {
		arg = tp->args + i;
		dl = data + arg->offset;
		/* Point the dynamic data area if needed */
		if (unlikely(arg->dynamic))
			*dl = make_data_loc(maxlen, dyndata - base);
		ret = process_fetch_insn(arg->code, regs, dl, base);
		if (unlikely(ret < 0 && arg->dynamic))
			*dl = make_data_loc(0, dyndata - base);
		else
			dyndata += ret;
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

// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/perf_event.h>
#include "util/evsel_fprintf.h"

struct bit_names {
	int bit;
	const char *name;
};

static void __p_bits(char *buf, size_t size, u64 value, struct bit_names *bits)
{
	bool first_bit = true;
	int i = 0;

	do {
		if (value & bits[i].bit) {
			buf += scnprintf(buf, size, "%s%s", first_bit ? "" : "|", bits[i].name);
			first_bit = false;
		}
	} while (bits[++i].name != NULL);
}

static void __p_sample_type(char *buf, size_t size, u64 value)
{
#define bit_name(n) { PERF_SAMPLE_##n, #n }
	struct bit_names bits[] = {
		bit_name(IP), bit_name(TID), bit_name(TIME), bit_name(ADDR),
		bit_name(READ), bit_name(CALLCHAIN), bit_name(ID), bit_name(CPU),
		bit_name(PERIOD), bit_name(STREAM_ID), bit_name(RAW),
		bit_name(BRANCH_STACK), bit_name(REGS_USER), bit_name(STACK_USER),
		bit_name(IDENTIFIER), bit_name(REGS_INTR), bit_name(DATA_SRC),
		bit_name(WEIGHT), bit_name(PHYS_ADDR), bit_name(AUX),
		bit_name(CGROUP), bit_name(DATA_PAGE_SIZE), bit_name(CODE_PAGE_SIZE),
		bit_name(WEIGHT_STRUCT),
		{ .name = NULL, }
	};
#undef bit_name
	__p_bits(buf, size, value, bits);
}

static void __p_branch_sample_type(char *buf, size_t size, u64 value)
{
#define bit_name(n) { PERF_SAMPLE_BRANCH_##n, #n }
	struct bit_names bits[] = {
		bit_name(USER), bit_name(KERNEL), bit_name(HV), bit_name(ANY),
		bit_name(ANY_CALL), bit_name(ANY_RETURN), bit_name(IND_CALL),
		bit_name(ABORT_TX), bit_name(IN_TX), bit_name(NO_TX),
		bit_name(COND), bit_name(CALL_STACK), bit_name(IND_JUMP),
		bit_name(CALL), bit_name(NO_FLAGS), bit_name(NO_CYCLES),
		bit_name(TYPE_SAVE), bit_name(HW_INDEX), bit_name(PRIV_SAVE),
		{ .name = NULL, }
	};
#undef bit_name
	__p_bits(buf, size, value, bits);
}

static void __p_read_format(char *buf, size_t size, u64 value)
{
#define bit_name(n) { PERF_FORMAT_##n, #n }
	struct bit_names bits[] = {
		bit_name(TOTAL_TIME_ENABLED), bit_name(TOTAL_TIME_RUNNING),
		bit_name(ID), bit_name(GROUP), bit_name(LOST),
		{ .name = NULL, }
	};
#undef bit_name
	__p_bits(buf, size, value, bits);
}

#define ENUM_ID_TO_STR_CASE(x) case x: return (#x);
static const char *stringify_perf_type_id(u64 value)
{
	switch (value) {
	ENUM_ID_TO_STR_CASE(PERF_TYPE_HARDWARE)
	ENUM_ID_TO_STR_CASE(PERF_TYPE_SOFTWARE)
	ENUM_ID_TO_STR_CASE(PERF_TYPE_TRACEPOINT)
	ENUM_ID_TO_STR_CASE(PERF_TYPE_HW_CACHE)
	ENUM_ID_TO_STR_CASE(PERF_TYPE_RAW)
	ENUM_ID_TO_STR_CASE(PERF_TYPE_BREAKPOINT)
	default:
		return NULL;
	}
}
#undef ENUM_ID_TO_STR_CASE

#define PRINT_ID(_s, _f)					\
do {								\
	const char *__s = _s;					\
	if (__s == NULL)					\
		snprintf(buf, size, _f, value);			\
	else							\
		snprintf(buf, size, _f" (%s)", value, __s);	\
} while (0)
#define print_id_unsigned(_s)	PRINT_ID(_s, "%"PRIu64)

static void __p_type_id(char *buf, size_t size, u64 value)
{
	print_id_unsigned(stringify_perf_type_id(value));
}

#define BUF_SIZE		1024

#define p_hex(val)		snprintf(buf, BUF_SIZE, "%#"PRIx64, (uint64_t)(val))
#define p_unsigned(val)		snprintf(buf, BUF_SIZE, "%"PRIu64, (uint64_t)(val))
#define p_signed(val)		snprintf(buf, BUF_SIZE, "%"PRId64, (int64_t)(val))
#define p_sample_type(val)	__p_sample_type(buf, BUF_SIZE, val)
#define p_branch_sample_type(val) __p_branch_sample_type(buf, BUF_SIZE, val)
#define p_read_format(val)	__p_read_format(buf, BUF_SIZE, val)
#define p_type_id(val)		__p_type_id(buf, BUF_SIZE, val)

#define PRINT_ATTRn(_n, _f, _p, _a)			\
do {							\
	if (_a || attr->_f) {				\
		_p(attr->_f);				\
		ret += attr__fprintf(fp, _n, buf, priv);\
	}						\
} while (0)

#define PRINT_ATTRf(_f, _p)	PRINT_ATTRn(#_f, _f, _p, false)

int perf_event_attr__fprintf(FILE *fp, struct perf_event_attr *attr,
			     attr__fprintf_f attr__fprintf, void *priv)
{
	char buf[BUF_SIZE];
	int ret = 0;

	PRINT_ATTRn("type", type, p_type_id, true);
	PRINT_ATTRf(size, p_unsigned);
	PRINT_ATTRf(config, p_hex);
	PRINT_ATTRn("{ sample_period, sample_freq }", sample_period, p_unsigned, false);
	PRINT_ATTRf(sample_type, p_sample_type);
	PRINT_ATTRf(read_format, p_read_format);

	PRINT_ATTRf(disabled, p_unsigned);
	PRINT_ATTRf(inherit, p_unsigned);
	PRINT_ATTRf(pinned, p_unsigned);
	PRINT_ATTRf(exclusive, p_unsigned);
	PRINT_ATTRf(exclude_user, p_unsigned);
	PRINT_ATTRf(exclude_kernel, p_unsigned);
	PRINT_ATTRf(exclude_hv, p_unsigned);
	PRINT_ATTRf(exclude_idle, p_unsigned);
	PRINT_ATTRf(mmap, p_unsigned);
	PRINT_ATTRf(comm, p_unsigned);
	PRINT_ATTRf(freq, p_unsigned);
	PRINT_ATTRf(inherit_stat, p_unsigned);
	PRINT_ATTRf(enable_on_exec, p_unsigned);
	PRINT_ATTRf(task, p_unsigned);
	PRINT_ATTRf(watermark, p_unsigned);
	PRINT_ATTRf(precise_ip, p_unsigned);
	PRINT_ATTRf(mmap_data, p_unsigned);
	PRINT_ATTRf(sample_id_all, p_unsigned);
	PRINT_ATTRf(exclude_host, p_unsigned);
	PRINT_ATTRf(exclude_guest, p_unsigned);
	PRINT_ATTRf(exclude_callchain_kernel, p_unsigned);
	PRINT_ATTRf(exclude_callchain_user, p_unsigned);
	PRINT_ATTRf(mmap2, p_unsigned);
	PRINT_ATTRf(comm_exec, p_unsigned);
	PRINT_ATTRf(use_clockid, p_unsigned);
	PRINT_ATTRf(context_switch, p_unsigned);
	PRINT_ATTRf(write_backward, p_unsigned);
	PRINT_ATTRf(namespaces, p_unsigned);
	PRINT_ATTRf(ksymbol, p_unsigned);
	PRINT_ATTRf(bpf_event, p_unsigned);
	PRINT_ATTRf(aux_output, p_unsigned);
	PRINT_ATTRf(cgroup, p_unsigned);
	PRINT_ATTRf(text_poke, p_unsigned);
	PRINT_ATTRf(build_id, p_unsigned);
	PRINT_ATTRf(inherit_thread, p_unsigned);
	PRINT_ATTRf(remove_on_exec, p_unsigned);
	PRINT_ATTRf(sigtrap, p_unsigned);

	PRINT_ATTRn("{ wakeup_events, wakeup_watermark }", wakeup_events, p_unsigned, false);
	PRINT_ATTRf(bp_type, p_unsigned);
	PRINT_ATTRn("{ bp_addr, config1 }", bp_addr, p_hex, false);
	PRINT_ATTRn("{ bp_len, config2 }", bp_len, p_hex, false);
	PRINT_ATTRf(branch_sample_type, p_branch_sample_type);
	PRINT_ATTRf(sample_regs_user, p_hex);
	PRINT_ATTRf(sample_stack_user, p_unsigned);
	PRINT_ATTRf(clockid, p_signed);
	PRINT_ATTRf(sample_regs_intr, p_hex);
	PRINT_ATTRf(aux_watermark, p_unsigned);
	PRINT_ATTRf(sample_max_stack, p_unsigned);
	PRINT_ATTRf(aux_sample_size, p_unsigned);
	PRINT_ATTRf(sig_data, p_unsigned);

	return ret;
}

// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <linux/bpf.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>

typedef uint32_t pid_t;
struct task_struct {};

#define TASK_COMM_LEN 16
#define PERF_MAX_STACK_DEPTH 127

#define STROBE_TYPE_INVALID 0
#define STROBE_TYPE_INT 1
#define STROBE_TYPE_STR 2
#define STROBE_TYPE_MAP 3

#define STACK_TABLE_EPOCH_SHIFT 20
#define STROBE_MAX_STR_LEN 1
#define STROBE_MAX_CFGS 32
#define STROBE_MAX_PAYLOAD						\
	(STROBE_MAX_STRS * STROBE_MAX_STR_LEN +				\
	STROBE_MAX_MAPS * (1 + STROBE_MAX_MAP_ENTRIES * 2) * STROBE_MAX_STR_LEN)

struct strobe_value_header {
	/*
	 * meaning depends on type:
	 * 1. int: 0, if value not set, 1 otherwise
	 * 2. str: 1 always, whether value is set or not is determined by ptr
	 * 3. map: 1 always, pointer points to additional struct with number
	 *    of entries (up to STROBE_MAX_MAP_ENTRIES)
	 */
	uint16_t len;
	/*
	 * _reserved might be used for some future fields/flags, but we always
	 * want to keep strobe_value_header to be 8 bytes, so BPF can read 16
	 * bytes in one go and get both header and value
	 */
	uint8_t _reserved[6];
};

/*
 * strobe_value_generic is used from BPF probe only, but needs to be a union
 * of strobe_value_int/strobe_value_str/strobe_value_map
 */
struct strobe_value_generic {
	struct strobe_value_header header;
	union {
		int64_t val;
		void *ptr;
	};
};

struct strobe_value_int {
	struct strobe_value_header header;
	int64_t value;
};

struct strobe_value_str {
	struct strobe_value_header header;
	const char* value;
};

struct strobe_value_map {
	struct strobe_value_header header;
	const struct strobe_map_raw* value;
};

struct strobe_map_entry {
	const char* key;
	const char* val;
};

/*
 * Map of C-string key/value pairs with fixed maximum capacity. Each map has
 * corresponding int64 ID, which application can use (or ignore) in whatever
 * way appropriate. Map is "write-only", there is no way to get data out of
 * map. Map is intended to be used to provide metadata for profilers and is
 * not to be used for internal in-app communication. All methods are
 * thread-safe.
 */
struct strobe_map_raw {
	/*
	 * general purpose unique ID that's up to application to decide
	 * whether and how to use; for request metadata use case id is unique
	 * request ID that's used to match metadata with stack traces on
	 * Strobelight backend side
	 */
	int64_t id;
	/* number of used entries in map */
	int64_t cnt;
	/*
	 * having volatile doesn't change anything on BPF side, but clang
	 * emits warnings for passing `volatile const char *` into
	 * bpf_probe_read_user_str that expects just `const char *`
	 */
	const char* tag;
	/*
	 * key/value entries, each consisting of 2 pointers to key and value
	 * C strings
	 */
	struct strobe_map_entry entries[STROBE_MAX_MAP_ENTRIES];
};

/* Following values define supported values of TLS mode */
#define TLS_NOT_SET -1
#define TLS_LOCAL_EXEC 0
#define TLS_IMM_EXEC 1
#define TLS_GENERAL_DYN 2

/*
 * structure that universally represents TLS location (both for static
 * executables and shared libraries)
 */
struct strobe_value_loc {
	/*
	 * tls_mode defines what TLS mode was used for particular metavariable:
	 * - -1 (TLS_NOT_SET) - no metavariable;
	 * - 0 (TLS_LOCAL_EXEC) - Local Executable mode;
	 * - 1 (TLS_IMM_EXEC) - Immediate Executable mode;
	 * - 2 (TLS_GENERAL_DYN) - General Dynamic mode;
	 * Local Dynamic mode is not yet supported, because never seen in
	 * practice.  Mode defines how offset field is interpreted. See
	 * calc_location() in below for details.
	 */
	int64_t tls_mode;
	/*
	 * TLS_LOCAL_EXEC: offset from thread pointer (fs:0 for x86-64,
	 * tpidr_el0 for aarch64).
	 * TLS_IMM_EXEC: absolute address of GOT entry containing offset
	 * from thread pointer;
	 * TLS_GENERAL_DYN: absolute address of double GOT entry
	 * containing tls_index_t struct;
	 */
	int64_t offset;
};

struct strobemeta_cfg {
	int64_t req_meta_idx;
	struct strobe_value_loc int_locs[STROBE_MAX_INTS];
	struct strobe_value_loc str_locs[STROBE_MAX_STRS];
	struct strobe_value_loc map_locs[STROBE_MAX_MAPS];
};

struct strobe_map_descr {
	uint64_t id;
	int16_t tag_len;
	/*
	 * cnt <0 - map value isn't set;
	 * 0 - map has id set, but no key/value entries
	 */
	int16_t cnt;
	/*
	 * both key_lens[i] and val_lens[i] should be >0 for present key/value
	 * entry
	 */
	uint16_t key_lens[STROBE_MAX_MAP_ENTRIES];
	uint16_t val_lens[STROBE_MAX_MAP_ENTRIES];
};

struct strobemeta_payload {
	/* req_id has valid request ID, if req_meta_valid == 1 */
	int64_t req_id;
	uint8_t req_meta_valid;
	/*
	 * mask has Nth bit set to 1, if Nth metavar was present and
	 * successfully read
	 */
	uint64_t int_vals_set_mask;
	int64_t int_vals[STROBE_MAX_INTS];
	/* len is >0 for present values */
	uint16_t str_lens[STROBE_MAX_STRS];
	/* if map_descrs[i].cnt == -1, metavar is not present/set */
	struct strobe_map_descr map_descrs[STROBE_MAX_MAPS];
	/*
	 * payload has compactly packed values of str and map variables in the
	 * form: strval1\0strval2\0map1key1\0map1val1\0map2key1\0map2val1\0
	 * (and so on); str_lens[i], key_lens[i] and val_lens[i] determines
	 * value length
	 */
	char payload[STROBE_MAX_PAYLOAD];
};

struct strobelight_bpf_sample {
	uint64_t ktime;
	char comm[TASK_COMM_LEN];
	pid_t pid;
	int user_stack_id;
	int kernel_stack_id;
	int has_meta;
	struct strobemeta_payload metadata;
	/*
	 * makes it possible to pass (<real payload size> + 1) as data size to
	 * perf_submit() to avoid perf_submit's paranoia about passing zero as
	 * size, as it deduces that <real payload size> might be
	 * **theoretically** zero
	 */
	char dummy_safeguard;
};

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(max_entries, 32);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} samples SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(max_entries, 16);
	__uint(key_size, sizeof(uint32_t));
	__uint(value_size, sizeof(uint64_t) * PERF_MAX_STACK_DEPTH);
} stacks_0 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(max_entries, 16);
	__uint(key_size, sizeof(uint32_t));
	__uint(value_size, sizeof(uint64_t) * PERF_MAX_STACK_DEPTH);
} stacks_1 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, uint32_t);
	__type(value, struct strobelight_bpf_sample);
} sample_heap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, STROBE_MAX_CFGS);
	__type(key, pid_t);
	__type(value, struct strobemeta_cfg);
} strobemeta_cfgs SEC(".maps");

/* Type for the dtv.  */
/* https://github.com/lattera/glibc/blob/master/nptl/sysdeps/x86_64/tls.h#L34 */
typedef union dtv {
	size_t counter;
	struct {
		void* val;
		bool is_static;
	} pointer;
} dtv_t;

/* Partial definition for tcbhead_t */
/* https://github.com/bminor/glibc/blob/master/sysdeps/x86_64/nptl/tls.h#L42 */
struct tcbhead {
	void* tcb;
	dtv_t* dtv;
};

/*
 * TLS module/offset information for shared library case.
 * For x86-64, this is mapped onto two entries in GOT.
 * For aarch64, this is pointed to by second GOT entry.
 */
struct tls_index {
	uint64_t module;
	uint64_t offset;
};

#ifdef SUBPROGS
__noinline
#else
__always_inline
#endif
static void *calc_location(struct strobe_value_loc *loc, void *tls_base)
{
	/*
	 * tls_mode value is:
	 * - -1 (TLS_NOT_SET), if no metavar is present;
	 * - 0 (TLS_LOCAL_EXEC), if metavar uses Local Executable mode of TLS
	 * (offset from fs:0 for x86-64 or tpidr_el0 for aarch64);
	 * - 1 (TLS_IMM_EXEC), if metavar uses Immediate Executable mode of TLS;
	 * - 2 (TLS_GENERAL_DYN), if metavar uses General Dynamic mode of TLS;
	 * This schema allows to use something like:
	 * (tls_mode + 1) * (tls_base + offset)
	 * to get NULL for "no metavar" location, or correct pointer for local
	 * executable mode without doing extra ifs.
	 */
	if (loc->tls_mode <= TLS_LOCAL_EXEC) {
		/* static executable is simple, we just have offset from
		 * tls_base */
		void *addr = tls_base + loc->offset;
		/* multiply by (tls_mode + 1) to get NULL, if we have no
		 * metavar in this slot */
		return (void *)((loc->tls_mode + 1) * (int64_t)addr);
	}
	/*
	 * Other modes are more complicated, we need to jump through few hoops.
	 *
	 * For immediate executable mode (currently supported only for aarch64):
	 *  - loc->offset is pointing to a GOT entry containing fixed offset
	 *  relative to tls_base;
	 *
	 * For general dynamic mode:
	 *  - loc->offset is pointing to a beginning of double GOT entries;
	 *  - (for aarch64 only) second entry points to tls_index_t struct;
	 *  - (for x86-64 only) two GOT entries are already tls_index_t;
	 *  - tls_index_t->module is used to find start of TLS section in
	 *  which variable resides;
	 *  - tls_index_t->offset provides offset within that TLS section,
	 *  pointing to value of variable.
	 */
	struct tls_index tls_index;
	dtv_t *dtv;
	void *tls_ptr;

	bpf_probe_read_user(&tls_index, sizeof(struct tls_index),
			    (void *)loc->offset);
	/* valid module index is always positive */
	if (tls_index.module > 0) {
		/* dtv = ((struct tcbhead *)tls_base)->dtv[tls_index.module] */
		bpf_probe_read_user(&dtv, sizeof(dtv),
				    &((struct tcbhead *)tls_base)->dtv);
		dtv += tls_index.module;
	} else {
		dtv = NULL;
	}
	bpf_probe_read_user(&tls_ptr, sizeof(void *), dtv);
	/* if pointer has (void *)-1 value, then TLS wasn't initialized yet */
	return tls_ptr && tls_ptr != (void *)-1
		? tls_ptr + tls_index.offset
		: NULL;
}

#ifdef SUBPROGS
__noinline
#else
__always_inline
#endif
static void read_int_var(struct strobemeta_cfg *cfg,
			 size_t idx, void *tls_base,
			 struct strobe_value_generic *value,
			 struct strobemeta_payload *data)
{
	void *location = calc_location(&cfg->int_locs[idx], tls_base);
	if (!location)
		return;

	bpf_probe_read_user(value, sizeof(struct strobe_value_generic), location);
	data->int_vals[idx] = value->val;
	if (value->header.len)
		data->int_vals_set_mask |= (1 << idx);
}

static __always_inline uint64_t read_str_var(struct strobemeta_cfg *cfg,
					     size_t idx, void *tls_base,
					     struct strobe_value_generic *value,
					     struct strobemeta_payload *data,
					     void *payload)
{
	void *location;
	uint64_t len;

	data->str_lens[idx] = 0;
	location = calc_location(&cfg->str_locs[idx], tls_base);
	if (!location)
		return 0;

	bpf_probe_read_user(value, sizeof(struct strobe_value_generic), location);
	len = bpf_probe_read_user_str(payload, STROBE_MAX_STR_LEN, value->ptr);
	/*
	 * if bpf_probe_read_user_str returns error (<0), due to casting to
	 * unsinged int, it will become big number, so next check is
	 * sufficient to check for errors AND prove to BPF verifier, that
	 * bpf_probe_read_user_str won't return anything bigger than
	 * STROBE_MAX_STR_LEN
	 */
	if (len > STROBE_MAX_STR_LEN)
		return 0;

	data->str_lens[idx] = len;
	return len;
}

static __always_inline void *read_map_var(struct strobemeta_cfg *cfg,
					  size_t idx, void *tls_base,
					  struct strobe_value_generic *value,
					  struct strobemeta_payload *data,
					  void *payload)
{
	struct strobe_map_descr* descr = &data->map_descrs[idx];
	struct strobe_map_raw map;
	void *location;
	uint64_t len;
	int i;

	descr->tag_len = 0; /* presume no tag is set */
	descr->cnt = -1; /* presume no value is set */

	location = calc_location(&cfg->map_locs[idx], tls_base);
	if (!location)
		return payload;

	bpf_probe_read_user(value, sizeof(struct strobe_value_generic), location);
	if (bpf_probe_read_user(&map, sizeof(struct strobe_map_raw), value->ptr))
		return payload;

	descr->id = map.id;
	descr->cnt = map.cnt;
	if (cfg->req_meta_idx == idx) {
		data->req_id = map.id;
		data->req_meta_valid = 1;
	}

	len = bpf_probe_read_user_str(payload, STROBE_MAX_STR_LEN, map.tag);
	if (len <= STROBE_MAX_STR_LEN) {
		descr->tag_len = len;
		payload += len;
	}

#ifdef NO_UNROLL
#pragma clang loop unroll(disable)
#else
#pragma unroll
#endif
	for (int i = 0; i < STROBE_MAX_MAP_ENTRIES; ++i) {
		if (i >= map.cnt)
			break;

		descr->key_lens[i] = 0;
		len = bpf_probe_read_user_str(payload, STROBE_MAX_STR_LEN,
					      map.entries[i].key);
		if (len <= STROBE_MAX_STR_LEN) {
			descr->key_lens[i] = len;
			payload += len;
		}
		descr->val_lens[i] = 0;
		len = bpf_probe_read_user_str(payload, STROBE_MAX_STR_LEN,
					      map.entries[i].val);
		if (len <= STROBE_MAX_STR_LEN) {
			descr->val_lens[i] = len;
			payload += len;
		}
	}

	return payload;
}

#ifdef USE_BPF_LOOP
enum read_type {
	READ_INT_VAR,
	READ_MAP_VAR,
	READ_STR_VAR,
};

struct read_var_ctx {
	struct strobemeta_payload *data;
	void *tls_base;
	struct strobemeta_cfg *cfg;
	void *payload;
	/* value gets mutated */
	struct strobe_value_generic *value;
	enum read_type type;
};

static int read_var_callback(__u32 index, struct read_var_ctx *ctx)
{
	switch (ctx->type) {
	case READ_INT_VAR:
		if (index >= STROBE_MAX_INTS)
			return 1;
		read_int_var(ctx->cfg, index, ctx->tls_base, ctx->value, ctx->data);
		break;
	case READ_MAP_VAR:
		if (index >= STROBE_MAX_MAPS)
			return 1;
		ctx->payload = read_map_var(ctx->cfg, index, ctx->tls_base,
					    ctx->value, ctx->data, ctx->payload);
		break;
	case READ_STR_VAR:
		if (index >= STROBE_MAX_STRS)
			return 1;
		ctx->payload += read_str_var(ctx->cfg, index, ctx->tls_base,
					     ctx->value, ctx->data, ctx->payload);
		break;
	}
	return 0;
}
#endif /* USE_BPF_LOOP */

/*
 * read_strobe_meta returns NULL, if no metadata was read; otherwise returns
 * pointer to *right after* payload ends
 */
#ifdef SUBPROGS
__noinline
#else
__always_inline
#endif
static void *read_strobe_meta(struct task_struct *task,
			      struct strobemeta_payload *data)
{
	pid_t pid = bpf_get_current_pid_tgid() >> 32;
	struct strobe_value_generic value = {0};
	struct strobemeta_cfg *cfg;
	void *tls_base, *payload;

	cfg = bpf_map_lookup_elem(&strobemeta_cfgs, &pid);
	if (!cfg)
		return NULL;

	data->int_vals_set_mask = 0;
	data->req_meta_valid = 0;
	payload = data->payload;
	/*
	 * we don't have struct task_struct definition, it should be:
	 * tls_base = (void *)task->thread.fsbase;
	 */
	tls_base = (void *)task;

#ifdef USE_BPF_LOOP
	struct read_var_ctx ctx = {
		.cfg = cfg,
		.tls_base = tls_base,
		.value = &value,
		.data = data,
		.payload = payload,
	};
	int err;

	ctx.type = READ_INT_VAR;
	err = bpf_loop(STROBE_MAX_INTS, read_var_callback, &ctx, 0);
	if (err != STROBE_MAX_INTS)
		return NULL;

	ctx.type = READ_STR_VAR;
	err = bpf_loop(STROBE_MAX_STRS, read_var_callback, &ctx, 0);
	if (err != STROBE_MAX_STRS)
		return NULL;

	ctx.type = READ_MAP_VAR;
	err = bpf_loop(STROBE_MAX_MAPS, read_var_callback, &ctx, 0);
	if (err != STROBE_MAX_MAPS)
		return NULL;
#else
#ifdef NO_UNROLL
#pragma clang loop unroll(disable)
#else
#pragma unroll
#endif /* NO_UNROLL */
	for (int i = 0; i < STROBE_MAX_INTS; ++i) {
		read_int_var(cfg, i, tls_base, &value, data);
	}
#ifdef NO_UNROLL
#pragma clang loop unroll(disable)
#else
#pragma unroll
#endif /* NO_UNROLL */
	for (int i = 0; i < STROBE_MAX_STRS; ++i) {
		payload += read_str_var(cfg, i, tls_base, &value, data, payload);
	}
#ifdef NO_UNROLL
#pragma clang loop unroll(disable)
#else
#pragma unroll
#endif /* NO_UNROLL */
	for (int i = 0; i < STROBE_MAX_MAPS; ++i) {
		payload = read_map_var(cfg, i, tls_base, &value, data, payload);
	}
#endif /* USE_BPF_LOOP */

	/*
	 * return pointer right after end of payload, so it's possible to
	 * calculate exact amount of useful data that needs to be sent
	 */
	return payload;
}

SEC("raw_tracepoint/kfree_skb")
int on_event(struct pt_regs *ctx) {
	pid_t pid =  bpf_get_current_pid_tgid() >> 32;
	struct strobelight_bpf_sample* sample;
	struct task_struct *task;
	uint32_t zero = 0;
	uint64_t ktime_ns;
	void *sample_end;

	sample = bpf_map_lookup_elem(&sample_heap, &zero);
	if (!sample)
		return 0; /* this will never happen */

	sample->pid = pid;
	bpf_get_current_comm(&sample->comm, TASK_COMM_LEN);
	ktime_ns = bpf_ktime_get_ns();
	sample->ktime = ktime_ns;

	task = (struct task_struct *)bpf_get_current_task();
	sample_end = read_strobe_meta(task, &sample->metadata);
	sample->has_meta = sample_end != NULL;
	sample_end = sample_end ? : &sample->metadata;

	if ((ktime_ns >> STACK_TABLE_EPOCH_SHIFT) & 1) {
		sample->kernel_stack_id = bpf_get_stackid(ctx, &stacks_1, 0);
		sample->user_stack_id = bpf_get_stackid(ctx, &stacks_1, BPF_F_USER_STACK);
	} else {
		sample->kernel_stack_id = bpf_get_stackid(ctx, &stacks_0, 0);
		sample->user_stack_id = bpf_get_stackid(ctx, &stacks_0, BPF_F_USER_STACK);
	}

	uint64_t sample_size = sample_end - (void *)sample;
	/* should always be true */
	if (sample_size < sizeof(struct strobelight_bpf_sample))
		bpf_perf_event_output(ctx, &samples, 0, sample, 1 + sample_size);
	return 0;
}

char _license[] SEC("license") = "GPL";

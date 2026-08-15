// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 Meta Platforms, Inc. and affiliates.

#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/btf.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/seq_buf.h>
#include <linux/slab.h>
#include <linux/stdarg.h>
#include <linux/string.h>

#include "disasm.h"
#include "diagnostics.h"

#define BPF_DIAG_TEXT_WIDTH 100
#define BPF_DIAG_CONTEXT 2
#define BPF_DIAG_CONTEXT_CNT (1 + BPF_DIAG_CONTEXT * 2)
#define BPF_DIAG_SOURCE_LANE_WIDTH 88
#define BPF_DIAG_TAB_WIDTH 8
#define BPF_DIAG_FMT_CHUNK_SIZE (PAGE_SIZE - sizeof(struct diag_fmt_chunk))
#define BPF_DIAG_FMT_BUF_SIZE 256
#define BPF_DIAG_EVENT_LOG_MAX_SIZE (64U << 20)
#define DISASM_LINE_LEN 160

enum bpf_diag_mod_target_kind {
	BPF_DIAG_MOD_TARGET_NONE,
	BPF_DIAG_MOD_TARGET_REG,
	BPF_DIAG_MOD_TARGET_STACK_ARG,
	BPF_DIAG_MOD_TARGET_STACK_SLOT,
	BPF_DIAG_MOD_TARGET_STACK_RANGE,
};

struct bpf_diag_mod_target {
	u32 frame_id;
	union {
		struct {
			s16 min_off;
			s16 max_off;
		} range;
		u16 spi;
		u8 regno;
		u8 stack_arg;
	};
	u8 frameno;
	u8 kind;
};

static struct bpf_diag_mod_target diag_reg_target(u32 frame_id, u8 frameno, u8 regno)
{
	return (struct bpf_diag_mod_target){
		.frame_id = frame_id,
		.frameno = frameno,
		.kind = BPF_DIAG_MOD_TARGET_REG,
		.regno = regno,
	};
}

static struct bpf_diag_mod_target diag_stack_arg_target(u32 frame_id, u8 frameno, u8 slot)
{
	return (struct bpf_diag_mod_target){
		.frame_id = frame_id,
		.frameno = frameno,
		.kind = BPF_DIAG_MOD_TARGET_STACK_ARG,
		.stack_arg = slot,
	};
}

static struct bpf_diag_mod_target diag_stack_slot_target(u32 frame_id, u8 frameno, u16 spi)
{
	return (struct bpf_diag_mod_target){
		.frame_id = frame_id,
		.frameno = frameno,
		.kind = BPF_DIAG_MOD_TARGET_STACK_SLOT,
		.spi = spi,
	};
}

static struct bpf_diag_mod_target diag_stack_range_target(u32 frame_id, u8 frameno,
							  s16 min_off, s16 max_off)
{
	return (struct bpf_diag_mod_target){
		.frame_id = frame_id,
		.frameno = frameno,
		.kind = BPF_DIAG_MOD_TARGET_STACK_RANGE,
		.range.min_off = min_off,
		.range.max_off = max_off,
	};
}

struct bpf_diag_reg_snapshot {
	u32 type;
	u32 btf_id;
	const struct bpf_map *map_ptr;
	const struct btf *btf;
	struct tnum var_off;
	struct cnum64 r64;
};

enum bpf_diag_history_kind {
	BPF_DIAG_HISTORY_BRANCH,
	BPF_DIAG_HISTORY_MOD,
};

struct bpf_diag_history_event {
	u32 insn_idx : 24;
	u32 kind : 8;
	u8 in_lineage : 1;
	union {
		struct {
			bool cond_true;
		} branch;
		struct {
			struct bpf_diag_mod_target target;
			struct bpf_diag_mod_target origin;
			struct bpf_diag_reg_snapshot old, new;
			u8 reason;
			bool origin_valid;
		} mod;
	};
};

struct disasm_line {
	char text[DISASM_LINE_LEN];
	int idx;
	bool valid;
};

struct disasm_ctx {
	struct bpf_verifier_env *env;
	struct seq_buf seq;
};

struct diag_fmt_chunk {
	struct list_head node;
	struct seq_buf seq;
	char data[];
};

struct diag_fmt_mark {
	struct diag_fmt_chunk *chunk;
	size_t len;
};

struct bpf_diag_log {
	struct bpf_diag_history_event *events;
	/* Sequence number of the oldest retained event on the active path. */
	u64 first_seq;
	u32 cnt;
	u32 cap;
	u32 head;
	bool growth_failed;
};

struct bpf_diag_scratch {
	struct bpf_linfo_source source_lines[BPF_DIAG_CONTEXT_CNT];
	struct disasm_line disasm_lines[BPF_DIAG_CONTEXT_CNT];
};

struct bpf_diag_mod_scope {
	struct bpf_reg_state target_reg_snapshot;
	struct bpf_diag_mod_target target;
	struct bpf_diag_mod_target origin;
	enum bpf_diag_mod_reason reason;
	u32 insn_idx;
	bool active;
	bool origin_valid;
};

struct bpf_diag {
	struct bpf_diag_log log;
	struct bpf_diag_scratch scratch;
	struct list_head fmt_chunks;
	struct bpf_diag_mod_scope mod;
	u32 frame_id_gen;
};

bool bpf_diag_enabled(const struct bpf_verifier_env *env)
{
	return env->log.level & BPF_LOG_LEVEL;
}

static void diag_write(struct bpf_verifier_env *env, const char *fmt, ...) __printf(2, 3);

int bpf_diag_init(struct bpf_verifier_env *env)
{
	if (!bpf_diag_enabled(env))
		return 0;

	env->diag = kzalloc_obj(struct bpf_diag, GFP_KERNEL_ACCOUNT);
	if (!env->diag)
		return -ENOMEM;

	INIT_LIST_HEAD(&env->diag->fmt_chunks);
	return 0;
}

void bpf_diag_init_frame(struct bpf_verifier_env *env, struct bpf_func_state *state)
{
	if (env->diag)
		state->diag_frame_id = ++env->diag->frame_id_gen;
}

static char *diag_fmt_alloc(struct bpf_verifier_env *env, size_t size)
{
	struct bpf_diag *diag = env->diag;
	struct diag_fmt_chunk *chunk;
	size_t capacity, available;
	char *buf;

	if (!diag || !size || size > INT_MAX)
		return NULL;

	if (!list_empty(&diag->fmt_chunks)) {
		chunk = list_last_entry(&diag->fmt_chunks, struct diag_fmt_chunk, node);
		available = seq_buf_get_buf(&chunk->seq, &buf);
		if (available >= size)
			goto commit;
	}

	capacity = max_t(size_t, BPF_DIAG_FMT_CHUNK_SIZE, size);
	chunk = kmalloc(struct_size(chunk, data, capacity), GFP_KERNEL_ACCOUNT);
	if (!chunk)
		return NULL;

	seq_buf_init(&chunk->seq, chunk->data, capacity);
	list_add_tail(&chunk->node, &diag->fmt_chunks);
	available = seq_buf_get_buf(&chunk->seq, &buf);
	if (WARN_ON_ONCE(available < size))
		return NULL;

commit:
	seq_buf_commit(&chunk->seq, size);
	return buf;
}

char *bpf_diag_fmt_buf(struct bpf_verifier_env *env, size_t size)
{
	char *buf;

	buf = diag_fmt_alloc(env, size);
	if (buf)
		buf[0] = '\0';
	return buf;
}

const char *bpf_diag_vfmt(struct bpf_verifier_env *env, const char *fmt, va_list args)
{
	va_list copy;
	char *buf;
	int len;

	va_copy(copy, args);
	len = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);
	if (len < 0 || len == INT_MAX)
		return "";

	buf = diag_fmt_alloc(env, len + 1);
	if (buf)
		vsnprintf(buf, len + 1, fmt, args);
	return buf ?: "";
}

const char *bpf_diag_fmt(struct bpf_verifier_env *env, const char *fmt, ...)
{
	const char *buf;
	va_list args;

	va_start(args, fmt);
	buf = bpf_diag_vfmt(env, fmt, args);
	va_end(args);
	return buf;
}

static struct diag_fmt_mark diag_fmt_save(struct bpf_verifier_env *env)
{
	struct bpf_diag *diag = env->diag;
	struct diag_fmt_mark mark = {};

	if (!diag || list_empty(&diag->fmt_chunks))
		return mark;

	mark.chunk = list_last_entry(&diag->fmt_chunks, struct diag_fmt_chunk, node);
	mark.len = mark.chunk->seq.len;
	return mark;
}

static void diag_fmt_restore(struct bpf_verifier_env *env, struct diag_fmt_mark mark)
{
	struct bpf_diag *diag = env->diag;
	struct diag_fmt_chunk *chunk;

	if (!diag)
		return;

	while (!list_empty(&diag->fmt_chunks)) {
		chunk = list_last_entry(&diag->fmt_chunks, struct diag_fmt_chunk, node);
		if (chunk == mark.chunk)
			break;
		list_del(&chunk->node);
		kfree(chunk);
	}

	if (mark.chunk) {
		mark.chunk->seq.len = mark.len;
		seq_buf_str(&mark.chunk->seq);
	}
}

void bpf_diag_free(struct bpf_verifier_env *env)
{
	struct bpf_diag *diag = env->diag;

	if (!diag)
		return;

	diag_fmt_restore(env, (struct diag_fmt_mark){});
	kvfree(diag->log.events);
	kfree(diag);
	env->diag = NULL;
}

static void diag_write(struct bpf_verifier_env *env, const char *fmt, ...)
{
	va_list args;

	if (!bpf_diag_enabled(env))
		return;

	va_start(args, fmt);
	bpf_verifier_vlog(&env->log, fmt, args);
	va_end(args);
}

static u64 log_end(const struct bpf_diag_log *log)
{
	return log->first_seq + log->cnt;
}

static u32 log_pos(const struct bpf_diag_log *log, u32 idx)
{
	u32 pos = log->head + idx;

	return pos < log->cap ? pos : pos - log->cap;
}

u64 bpf_diag_event_log_save(struct bpf_verifier_env *env)
{
	struct bpf_diag *diag = env->diag;

	return diag ? log_end(&diag->log) : 0;
}

void bpf_diag_event_log_restore(struct bpf_verifier_env *env, u64 log_pos)
{
	struct bpf_diag *diag = env->diag;
	struct bpf_diag_log *log;
	u64 end_seq;

	if (!diag)
		return;

	log = &diag->log;
	end_seq = log_end(log);
	if (WARN_ON_ONCE(log_pos > end_seq))
		log_pos = end_seq;

	/*
	 * A deep abandoned path may have rotated away the shared prefix. In
	 * that case, restart with an empty retained suffix and remember that
	 * every event before the restored mark is unavailable.
	 */
	if (log_pos <= log->first_seq) {
		log->first_seq = log_pos;
		log->head = 0;
		log->cnt = 0;
		return;
	}

	log->cnt = log_pos - log->first_seq;
}

static void diag_append_history(struct bpf_verifier_env *env,
				const struct bpf_diag_history_event *event)
{
	struct bpf_diag_history_event *events;
	struct bpf_diag *diag = env->diag;
	struct bpf_diag_log *log;
	u32 cap, max_events;

	if (!diag)
		return;
	log = &diag->log;

	if (log->cnt < log->cap) {
		log->events[log_pos(log, log->cnt++)] = *event;
		return;
	}

	max_events = BPF_DIAG_EVENT_LOG_MAX_SIZE / sizeof(*events);
	if (log->growth_failed || log->cap == max_events)
		goto rotate;

	cap = min(log->cap ? log->cap * 2 : 64, max_events);
	events = kvrealloc(log->events, array_size(cap, sizeof(*events)), GFP_KERNEL_ACCOUNT);
	if (!events) {
		log->growth_failed = true;
		goto rotate;
	}
	log->events = events;
	log->cap = cap;
	log->events[log->cnt++] = *event;
	return;

rotate:
	if (log->cap) {
		log->events[log->head++] = *event;
		if (log->head == log->cap)
			log->head = 0;
	}
	log->first_seq++;
}

static void diag_print_wrapped_prefixed(struct bpf_verifier_env *env, const char *first_prefix,
					const char *next_prefix, const char *text)
{
	const char *prefix = first_prefix;

	while (*text) {
		const char *line = text;
		int prefix_len = strlen(prefix);
		int text_width = BPF_DIAG_TEXT_WIDTH - prefix_len;
		int len = 0, last_space = -1;

		if (text_width < 1)
			text_width = 1;

		while (line[len] && line[len] != '\n' && len < text_width) {
			if (line[len] == ' ')
				last_space = len;
			len++;
		}

		if (line[len] && line[len] != '\n' && line[len] != ' ' && last_space > 0)
			len = last_space;

		diag_write(env, "%s%.*s\n", prefix, len, line);

		text = line + len;
		while (*text == ' ')
			text++;
		if (*text == '\n')
			text++;

		prefix = next_prefix;
	}
}

const char *bpf_diag_fmt_btf_type(struct bpf_verifier_env *env, const struct btf *btf, u32 type_id)
{
	char *buf = bpf_diag_fmt_buf(env, BPF_DIAG_FMT_BUF_SIZE);
	size_t len;
	int ret;

	if (!buf)
		return "";

	buf[0] = '\0';
	ret = btf_type_name_to_buf(btf, type_id, buf, BPF_DIAG_FMT_BUF_SIZE);
	if (ret < 0 || !buf[0]) {
		scnprintf(buf, BPF_DIAG_FMT_BUF_SIZE, "BTF type ID %u", type_id);
		return buf;
	}

	len = strlen(buf);
	if (len && buf[len - 1] == '{')
		buf[len - 1] = '\0';
	return buf;
}

static int diag_line_width(unsigned int line)
{
	int width = 1;

	while (line >= 10) {
		line /= 10;
		width++;
	}

	return width;
}

static int diag_line_indent(const char *line)
{
	int indent = 0;

	while (*line == ' ' || *line == '\t') {
		if (*line == '\t')
			indent = round_up(indent + 1, BPF_DIAG_TAB_WIDTH);
		else
			indent++;
		line++;
	}

	return indent;
}

static void disasm_print(void *private_data, const char *fmt, ...) __printf(2, 3);

static void disasm_print(void *private_data, const char *fmt, ...)
{
	struct disasm_ctx *ctx = private_data;
	va_list args;

	va_start(args, fmt);
	seq_buf_vprintf(&ctx->seq, fmt, args);
	va_end(args);
}

static const char *disasm_kfunc_name(void *private_data, const struct bpf_insn *insn)
{
	struct disasm_ctx *ctx = private_data;

	return bpf_disasm_kfunc_name(ctx->env, insn);
}

static void format_disasm_line(struct bpf_verifier_env *env, int insn_idx,
			       struct disasm_line *line)
{
	struct disasm_ctx ctx = { .env = env };
	struct bpf_insn *insn;
	const struct bpf_insn_cbs cbs = {
		.cb_call = disasm_kfunc_name,
		.cb_print = disasm_print,
		.private_data = &ctx,
	};

	line->idx = insn_idx;
	line->valid = false;
	seq_buf_init(&ctx.seq, line->text, sizeof(line->text));

	if (insn_idx < 0 || insn_idx >= env->prog->len)
		return;

	if (insn_idx > 0 && bpf_is_ldimm64(&env->prog->insnsi[insn_idx - 1]))
		return;

	insn = &env->prog->insnsi[insn_idx];
	if (bpf_is_ldimm64(insn) && insn_idx + 1 >= env->prog->len)
		return;

	print_bpf_insn(&cbs, insn, env->allow_ptr_leaks);
	seq_buf_str(&ctx.seq);
	ctx.seq.len = strnlen(line->text, sizeof(line->text));
	while (ctx.seq.len && line->text[ctx.seq.len - 1] == '\n')
		seq_buf_pop(&ctx.seq);
	seq_buf_str(&ctx.seq);

	line->valid = true;
}

static void diag_format_source_text(char *buf, size_t size, const char *line, int width)
{
	int col = 0, len = 0;

	if (!size)
		return;
	if (width <= 0) {
		buf[0] = '\0';
		return;
	}

	line = line ?: "...";
	while (*line && col < width && len + 1 < size) {
		if (*line == '\t') {
			int next = round_up(col + 1, BPF_DIAG_TAB_WIDTH);

			while (col < next && col < width && len + 1 < size) {
				buf[len++] = ' ';
				col++;
			}
			line++;
			continue;
		}

		buf[len++] = *line++;
		col++;
	}

	if (*line) {
		int ellipsis_len = min(3, width);

		while (len > 0 && col > width - ellipsis_len) {
			len--;
			col--;
		}
		while (ellipsis_len-- && len + 1 < size)
			buf[len++] = '.';
	}

	buf[len] = '\0';
}

static void diag_format_source_lane(char *buf, size_t size, const char *source_prefix,
				    int source_line_width, int line_num, const char *line)
{
	int len, text_width;

	if (line_num <= 0) {
		buf[0] = '\0';
		return;
	}

	len = scnprintf(buf, size, "%s%*d | ", source_prefix, source_line_width, line_num);
	text_width = BPF_DIAG_SOURCE_LANE_WIDTH - len;
	diag_format_source_text(buf + len, size - len, line, text_width);
}

static void bpf_diag_header(struct bpf_verifier_env *env, const char *category,
			    const char *problem)
{
	char first;

	if (!bpf_diag_enabled(env))
		return;

	category = category ?: "Verifier Error";
	problem = problem ?: "";

	if (!problem[0]) {
		diag_write(env, "\nVerification failed: %s\n", category);
		return;
	}

	first = toupper(problem[0]);
	diag_write(env, "\nVerification failed: %s: %c%s\n", category, first, problem + 1);
}

static void diag_print_source_annotation(struct bpf_verifier_env *env, int line_width, int indent,
					 const char *label, const char *msg)
{
	const char *first_prefix, *next_prefix, *text;

	indent = min_t(int, indent, max_t(int, 0, BPF_DIAG_SOURCE_LANE_WIDTH - line_width - 8));
	text = bpf_diag_fmt(env, "%s: %s", label, msg);
	first_prefix = bpf_diag_fmt(env, "  %*s | %*s^-- ", line_width + 4, "", indent, "");
	next_prefix = bpf_diag_fmt(env, "  %*s | %*s    ", line_width + 4, "", indent, "");

	diag_print_wrapped_prefixed(env, first_prefix, next_prefix, text);
}

static void diag_print_insn_context(struct bpf_verifier_env *env, u32 insn_idx,
				    struct disasm_line *disasm_lines)
{
	int insn_width = diag_line_width(env->prog->len ? env->prog->len - 1 : 0);
	int i;

	for (i = 0; i < BPF_DIAG_CONTEXT_CNT; i++) {
		int row = i - BPF_DIAG_CONTEXT;

		format_disasm_line(env, insn_idx + row, &disasm_lines[i]);
	}

	diag_write(env, "  Instruction context:\n");
	for (i = 0; i < BPF_DIAG_CONTEXT_CNT; i++) {
		struct disasm_line *line = &disasm_lines[i];

		if (line->valid)
			diag_write(env, "  %s%*d | %s\n",
				   line->idx == insn_idx ? ">>> " : "    ",
				   insn_width, line->idx, line->text);
	}
}

static void bpf_diag_source(struct bpf_verifier_env *env, u32 insn_idx, const char *label,
			    const char *fmt, ...)
{
	struct bpf_diag_scratch *scratch;
	struct bpf_linfo_source *source_lines;
	struct disasm_line *disasm_lines;
	struct bpf_linfo_source src = {};
	struct diag_fmt_mark mark;
	const struct bpf_line_info *linfo;
	const struct bpf_subprog_info *subprog;
	struct btf *btf = env->prog->aux->btf;
	char *source_lane;
	const char *msg;
	const char *func;
	int start_line, end_line, width, indent, subprogno, linfo_start, linfo_end, i;
	va_list args;

	if (!bpf_diag_enabled(env))
		return;
	if (!env->diag)
		return;

	mark = diag_fmt_save(env);
	label = label ?: "note";
	scratch = &env->diag->scratch;
	source_lines = scratch->source_lines;
	disasm_lines = scratch->disasm_lines;
	memset(source_lines, 0, sizeof(scratch->source_lines));
	memset(disasm_lines, 0, sizeof(scratch->disasm_lines));

	va_start(args, fmt);
	msg = bpf_diag_vfmt(env, fmt, args);
	va_end(args);
	if (!*msg)
		msg = "<failed to allocate diagnostic text>";

	linfo = bpf_find_linfo(env->prog, insn_idx);
	if (btf && linfo)
		bpf_get_linfo_source(btf, linfo, &src);
	if (!src.file || !*src.file || !src.line || !*src.line) {
		diag_write(env, "  insn %u\n", insn_idx);
		diag_print_source_annotation(env, 0, 0, label, msg);
		diag_print_insn_context(env, insn_idx, disasm_lines);
		goto out_restore;
	}

	subprog = bpf_find_containing_subprog(env, insn_idx);
	subprogno = subprog ? subprog - env->subprog_info : -ENOENT;
	func = subprogno >= 0 ? bpf_subprog_name(env, subprogno) : NULL;
	if (func && *func)
		diag_write(env, "  %s @ %s:%d:%d\n", func, src.file, src.line_num, src.line_col);
	else
		diag_write(env, "  %s:%d:%d\n", src.file, src.line_num, src.line_col);

	start_line = src.line_num - BPF_DIAG_CONTEXT;
	end_line = src.line_num + BPF_DIAG_CONTEXT;
	width = diag_line_width(end_line);
	indent = diag_line_indent(src.line);
	for (i = 0; i < BPF_DIAG_CONTEXT_CNT; i++)
		source_lines[i].line_num = start_line + i;

	linfo = env->prog->aux->linfo;
	linfo_start = subprog ? subprog->linfo_idx : 0;
	linfo_end = subprogno >= 0 && subprogno + 1 < env->subprog_cnt ?
		    env->subprog_info[subprogno + 1].linfo_idx : env->prog->aux->nr_linfo;
	for (i = linfo_start; i < linfo_end; i++) {
		struct bpf_linfo_source line_src;
		int idx;

		bpf_get_linfo_source(btf, &linfo[i], &line_src);
		if (line_src.file_name_off != src.file_name_off ||
		    line_src.line_num < start_line || line_src.line_num > end_line ||
		    !line_src.line || !*line_src.line)
			continue;

		idx = line_src.line_num - start_line;
		if (!source_lines[idx].line)
			source_lines[idx] = line_src;
	}

	diag_write(env, "  Source context:\n");
	source_lane = bpf_diag_fmt_buf(env, BPF_DIAG_FMT_BUF_SIZE);
	if (!source_lane)
		goto out_restore;
	for (i = 0; i < BPF_DIAG_CONTEXT_CNT; i++) {
		const char *source_prefix;

		source_prefix = source_lines[i].line_num == src.line_num ? ">>> " : "    ";
		diag_format_source_lane(source_lane, BPF_DIAG_FMT_BUF_SIZE, source_prefix, width,
					source_lines[i].line_num, source_lines[i].line);
		diag_write(env, "  %s\n", source_lane);
		if (source_lines[i].line_num == src.line_num)
			diag_print_source_annotation(env, width, indent, label, msg);
	}
	diag_print_insn_context(env, insn_idx, disasm_lines);

out_restore:
	diag_fmt_restore(env, mark);
}

void bpf_diag_record_branch(struct bpf_verifier_env *env, u32 insn_idx, bool cond_true)
{
	struct bpf_diag_history_event event = {
		.insn_idx = insn_idx,
		.kind = BPF_DIAG_HISTORY_BRANCH,
		.branch = {
			.cond_true = cond_true,
		},
	};

	diag_append_history(env, &event);
}

static void diag_snapshot_reg(struct bpf_diag_reg_snapshot *snapshot,
			      const struct bpf_reg_state *reg)
{
	snapshot->type = reg->type;
	if (type_is_map_ptr(reg->type))
		snapshot->map_ptr = reg->map_ptr;
	if (base_type(reg->type) == PTR_TO_BTF_ID && reg->btf && reg->btf_id) {
		snapshot->btf_id = reg->btf_id;
		snapshot->btf = reg->btf;
	}
	snapshot->var_off = reg->var_off;
	snapshot->r64 = reg->r64;
}

static bool diag_mod_insn_origin(struct bpf_verifier_env *env, u32 insn_idx,
				 const struct bpf_diag_mod_target *target,
				 struct bpf_diag_mod_target *origin)
{
	const struct bpf_insn *insn = &env->prog->insnsi[insn_idx];
	u8 class = BPF_CLASS(insn->code);
	const struct bpf_func_state *state;

	if (target->kind == BPF_DIAG_MOD_TARGET_REG && (class == BPF_ALU || class == BPF_ALU64) &&
	    BPF_OP(insn->code) == BPF_MOV && BPF_SRC(insn->code) == BPF_X) {
		*origin = diag_reg_target(target->frame_id, target->frameno, insn->src_reg);
		return true;
	}

	if ((target->kind != BPF_DIAG_MOD_TARGET_STACK_ARG &&
	     target->kind != BPF_DIAG_MOD_TARGET_STACK_SLOT) ||
	    class != BPF_STX)
		return false;

	state = env->cur_state->frame[env->cur_state->curframe];
	*origin = diag_reg_target(state->diag_frame_id, state->frameno, insn->src_reg);
	return true;
}

static bool diag_mod_keeps_lineage(struct bpf_verifier_env *env,
				   const struct bpf_diag_history_event *event)
{
	const struct bpf_insn *insn;
	u8 class;

	if (event->mod.reason != BPF_DIAG_MOD_WRITE ||
	    event->mod.target.kind != BPF_DIAG_MOD_TARGET_REG)
		return false;

	insn = &env->prog->insnsi[event->insn_idx];
	class = BPF_CLASS(insn->code);
	if (class != BPF_ALU && class != BPF_ALU64)
		return false;

	switch (BPF_OP(insn->code)) {
	case BPF_ADD:
	case BPF_SUB:
	case BPF_MUL:
	case BPF_OR:
	case BPF_AND:
	case BPF_LSH:
	case BPF_RSH:
	case BPF_ARSH:
	case BPF_XOR:
	case BPF_NEG:
	case BPF_END:
		return true;
	default:
		return false;
	}
}

static void diag_record_mod(struct bpf_verifier_env *env, u32 insn_idx,
			    struct bpf_diag_mod_target target,
			    enum bpf_diag_mod_reason reason,
			    const struct bpf_reg_state *old_reg,
			    const struct bpf_reg_state *new_reg,
			    const struct bpf_diag_mod_target *origin)
{
	struct bpf_diag_history_event event = {
		.insn_idx = insn_idx,
		.kind = BPF_DIAG_HISTORY_MOD,
		.mod = {
			.target = target,
			.reason = reason,
		},
	};

	if (old_reg)
		diag_snapshot_reg(&event.mod.old, old_reg);
	if (new_reg)
		diag_snapshot_reg(&event.mod.new, new_reg);
	if (origin) {
		event.mod.origin = *origin;
		event.mod.origin_valid = true;
	} else if (diag_mod_insn_origin(env, insn_idx, &target, &event.mod.origin)) {
		event.mod.origin_valid = true;
	}
	if (old_reg && new_reg &&
	    (reason == BPF_DIAG_MOD_WRITE || reason == BPF_DIAG_MOD_SPILL) &&
	    !memcmp(&event.mod.old, &event.mod.new, sizeof(event.mod.old)) &&
	    !event.mod.origin_valid &&
	    diag_mod_keeps_lineage(env, &event))
		return;

	diag_append_history(env, &event);
}

static struct bpf_reg_state *target_to_reg(struct bpf_verifier_env *env,
					   const struct bpf_diag_mod_target *target)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	struct bpf_func_state *state;

	state = target->frameno <= vstate->curframe ? vstate->frame[target->frameno] : NULL;

	if (!state)
		return NULL;
	if (state->diag_frame_id != target->frame_id)
		return NULL;

	switch (target->kind) {
	case BPF_DIAG_MOD_TARGET_REG:
		if (target->regno >= MAX_BPF_REG)
			return NULL;
		return &state->regs[target->regno];
	case BPF_DIAG_MOD_TARGET_STACK_ARG:
		if (target->stack_arg >= state->out_stack_arg_cnt)
			return NULL;
		return &state->stack_arg_regs[target->stack_arg];
	case BPF_DIAG_MOD_TARGET_STACK_SLOT:
		if (target->spi >= state->allocated_stack / BPF_REG_SIZE)
			return NULL;
		return &state->stack[target->spi].spilled_ptr;
	default:
		return NULL;
	}
}

static bool reg_to_target(struct bpf_verifier_env *env, const struct bpf_reg_state *reg,
			  struct bpf_diag_mod_target *target)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	unsigned long addr = (unsigned long)reg;
	int frame;

	for (frame = 0; frame <= vstate->curframe; frame++) {
		struct bpf_func_state *state = vstate->frame[frame];
		unsigned long start, end;
		u32 nslots = state->allocated_stack / BPF_REG_SIZE;
		int spi;

		start = (unsigned long)state->regs;
		end = (unsigned long)(state->regs + MAX_BPF_REG);
		if (addr >= start && addr < end) {
			*target = diag_reg_target(state->diag_frame_id, state->frameno,
						  reg - state->regs);
			return true;
		}

		start = (unsigned long)state->stack_arg_regs;
		end = (unsigned long)(state->stack_arg_regs + state->out_stack_arg_cnt);
		if (state->out_stack_arg_cnt && addr >= start && addr < end) {
			*target = diag_stack_arg_target(state->diag_frame_id, state->frameno,
							reg - state->stack_arg_regs);
			return true;
		}

		start = (unsigned long)state->stack;
		end = (unsigned long)(state->stack + nslots);
		if (nslots && addr >= start && addr < end) {
			spi = ((const char *)reg - (const char *)state->stack) /
			      sizeof(*state->stack);
			*target = diag_stack_slot_target(state->diag_frame_id, state->frameno, spi);
			return true;
		}
	}
	return false;
}

void bpf_diag_mod_begin(struct bpf_verifier_env *env, const struct bpf_reg_state *reg,
			const struct bpf_reg_state *origin, enum bpf_diag_mod_reason reason)
{
	struct bpf_diag *diag = env->diag;

	if (!diag)
		return;
	diag->mod.active = reg_to_target(env, reg, &diag->mod.target);
	if (!diag->mod.active)
		return;
	diag->mod.target_reg_snapshot = *reg;
	diag->mod.insn_idx = env->insn_idx;
	diag->mod.reason = reason;
	diag->mod.origin_valid = origin && reg_to_target(env, origin, &diag->mod.origin);
}

void bpf_diag_mod_end(struct bpf_verifier_env *env)
{
	struct bpf_diag *diag = env->diag;
	const struct bpf_reg_state *new_reg;

	if (!diag || !diag->mod.active)
		return;
	diag->mod.active = false;
	/*
	 * Resolve the target again because the enclosing function state's stack
	 * may have been reallocated while the modification was in progress.
	 */
	new_reg = target_to_reg(env, &diag->mod.target);
	if (!new_reg)
		return;
	diag_record_mod(env, diag->mod.insn_idx, diag->mod.target, diag->mod.reason,
			&diag->mod.target_reg_snapshot, new_reg,
			diag->mod.origin_valid ? &diag->mod.origin : NULL);
}

void bpf_diag_record_scrub(struct bpf_verifier_env *env, const struct bpf_reg_state *reg,
			   enum bpf_diag_mod_reason reason)
{
	struct bpf_diag_mod_target target;

	if (!env->diag || reg->type == NOT_INIT || !reg_to_target(env, reg, &target))
		return;
	diag_record_mod(env, env->insn_idx, target, reason, reg, NULL, NULL);
}

void bpf_diag_record_scrub_stack(struct bpf_verifier_env *env,
				 const struct bpf_func_state *state, s16 min_off, s16 max_off,
				 enum bpf_diag_mod_reason reason)
{
	diag_record_mod(env, env->insn_idx,
			diag_stack_range_target(state->diag_frame_id, state->frameno, min_off, max_off),
			reason, NULL, NULL, NULL);
}

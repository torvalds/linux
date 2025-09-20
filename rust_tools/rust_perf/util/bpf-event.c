// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/err.h>
#include <linux/perf_event.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <internal/lib.h>
#include <perf/event.h>
#include <symbol/kallsyms.h>
#include "bpf-event.h"
#include "bpf-utils.h"
#include "debug.h"
#include "dso.h"
#include "symbol.h"
#include "machine.h"
#include "env.h"
#include "session.h"
#include "map.h"
#include "evlist.h"
#include "record.h"
#include "util/synthetic-events.h"

static int snprintf_hex(char *buf, size_t size, unsigned char *data, size_t len)
{
	int ret = 0;
	size_t i;

	for (i = 0; i < len; i++)
		ret += snprintf(buf + ret, size - ret, "%02x", data[i]);
	return ret;
}

static int machine__process_bpf_event_load(struct machine *machine,
					   union perf_event *event,
					   struct perf_sample *sample __maybe_unused)
{
	struct bpf_prog_info_node *info_node;
	struct perf_env *env = machine->env;
	struct perf_bpil *info_linear;
	int id = event->bpf.id;
	unsigned int i;

	/* perf-record, no need to handle bpf-event */
	if (env == NULL)
		return 0;

	info_node = perf_env__find_bpf_prog_info(env, id);
	if (!info_node)
		return 0;
	info_linear = info_node->info_linear;

	for (i = 0; i < info_linear->info.nr_jited_ksyms; i++) {
		u64 *addrs = (u64 *)(uintptr_t)(info_linear->info.jited_ksyms);
		u64 addr = addrs[i];
		struct map *map = maps__find(machine__kernel_maps(machine), addr);

		if (map) {
			struct dso *dso = map__dso(map);

			dso__set_binary_type(dso, DSO_BINARY_TYPE__BPF_PROG_INFO);
			dso__bpf_prog(dso)->id = id;
			dso__bpf_prog(dso)->sub_id = i;
			dso__bpf_prog(dso)->env = env;
			map__put(map);
		}
	}
	return 0;
}

int machine__process_bpf(struct machine *machine, union perf_event *event,
			 struct perf_sample *sample)
{
	if (dump_trace)
		perf_event__fprintf_bpf(event, stdout);

	switch (event->bpf.type) {
	case PERF_BPF_EVENT_PROG_LOAD:
		return machine__process_bpf_event_load(machine, event, sample);

	case PERF_BPF_EVENT_PROG_UNLOAD:
		/*
		 * Do not free bpf_prog_info and btf of the program here,
		 * as annotation still need them. They will be freed at
		 * the end of the session.
		 */
		break;
	default:
		pr_debug("unexpected bpf event type of %d\n", event->bpf.type);
		break;
	}
	return 0;
}

static int perf_env__fetch_btf(struct perf_env *env,
			       u32 btf_id,
			       struct btf *btf)
{
	struct btf_node *node;
	u32 data_size;
	const void *data;

	data = btf__raw_data(btf, &data_size);

	node = malloc(data_size + sizeof(struct btf_node));
	if (!node)
		return -1;

	node->id = btf_id;
	node->data_size = data_size;
	memcpy(node->data, data, data_size);

	if (!perf_env__insert_btf(env, node)) {
		/* Insertion failed because of a duplicate. */
		free(node);
		return -1;
	}
	return 0;
}

static int synthesize_bpf_prog_name(char *buf, int size,
				    struct bpf_prog_info *info,
				    struct btf *btf,
				    u32 sub_id)
{
	u8 (*prog_tags)[BPF_TAG_SIZE] = (void *)(uintptr_t)(info->prog_tags);
	void *func_infos = (void *)(uintptr_t)(info->func_info);
	u32 sub_prog_cnt = info->nr_jited_ksyms;
	const struct bpf_func_info *finfo;
	const char *short_name = NULL;
	const struct btf_type *t;
	int name_len;

	name_len = snprintf(buf, size, "bpf_prog_");
	name_len += snprintf_hex(buf + name_len, size - name_len,
				 prog_tags[sub_id], BPF_TAG_SIZE);
	if (btf) {
		finfo = func_infos + sub_id * info->func_info_rec_size;
		t = btf__type_by_id(btf, finfo->type_id);
		short_name = btf__name_by_offset(btf, t->name_off);
	} else if (sub_id == 0 && sub_prog_cnt == 1) {
		/* no subprog */
		if (info->name[0])
			short_name = info->name;
	} else
		short_name = "F";
	if (short_name)
		name_len += snprintf(buf + name_len, size - name_len,
				     "_%s", short_name);
	return name_len;
}

#ifdef HAVE_LIBBPF_STRINGS_SUPPORT

#define BPF_METADATA_PREFIX "bpf_metadata_"
#define BPF_METADATA_PREFIX_LEN (sizeof(BPF_METADATA_PREFIX) - 1)

static bool name_has_bpf_metadata_prefix(const char **s)
{
	if (strncmp(*s, BPF_METADATA_PREFIX, BPF_METADATA_PREFIX_LEN) != 0)
		return false;
	*s += BPF_METADATA_PREFIX_LEN;
	return true;
}

struct bpf_metadata_map {
	struct btf *btf;
	const struct btf_type *datasec;
	void *rodata;
	size_t rodata_size;
	unsigned int num_vars;
};

static int bpf_metadata_read_map_data(__u32 map_id, struct bpf_metadata_map *map)
{
	int map_fd;
	struct bpf_map_info map_info;
	__u32 map_info_len;
	int key;
	struct btf *btf;
	const struct btf_type *datasec;
	struct btf_var_secinfo *vsi;
	unsigned int vlen, vars;
	void *rodata;

	map_fd = bpf_map_get_fd_by_id(map_id);
	if (map_fd < 0)
		return -1;

	memset(&map_info, 0, sizeof(map_info));
	map_info_len = sizeof(map_info);
	if (bpf_obj_get_info_by_fd(map_fd, &map_info, &map_info_len) < 0)
		goto out_close;

	/* If it's not an .rodata map, don't bother. */
	if (map_info.type != BPF_MAP_TYPE_ARRAY ||
	    map_info.key_size != sizeof(int) ||
	    map_info.max_entries != 1 ||
	    !map_info.btf_value_type_id ||
	    !strstr(map_info.name, ".rodata")) {
		goto out_close;
	}

	btf = btf__load_from_kernel_by_id(map_info.btf_id);
	if (!btf)
		goto out_close;
	datasec = btf__type_by_id(btf, map_info.btf_value_type_id);
	if (!btf_is_datasec(datasec))
		goto out_free_btf;

	/*
	 * If there aren't any variables with the "bpf_metadata_" prefix,
	 * don't bother.
	 */
	vlen = btf_vlen(datasec);
	vsi = btf_var_secinfos(datasec);
	vars = 0;
	for (unsigned int i = 0; i < vlen; i++, vsi++) {
		const struct btf_type *t_var = btf__type_by_id(btf, vsi->type);
		const char *name = btf__name_by_offset(btf, t_var->name_off);

		if (name_has_bpf_metadata_prefix(&name))
			vars++;
	}
	if (vars == 0)
		goto out_free_btf;

	rodata = zalloc(map_info.value_size);
	if (!rodata)
		goto out_free_btf;
	key = 0;
	if (bpf_map_lookup_elem(map_fd, &key, rodata)) {
		free(rodata);
		goto out_free_btf;
	}
	close(map_fd);

	map->btf = btf;
	map->datasec = datasec;
	map->rodata = rodata;
	map->rodata_size = map_info.value_size;
	map->num_vars = vars;
	return 0;

out_free_btf:
	btf__free(btf);
out_close:
	close(map_fd);
	return -1;
}

struct format_btf_ctx {
	char *buf;
	size_t buf_size;
	size_t buf_idx;
};

static void format_btf_cb(void *arg, const char *fmt, va_list ap)
{
	int n;
	struct format_btf_ctx *ctx = (struct format_btf_ctx *)arg;

	n = vsnprintf(ctx->buf + ctx->buf_idx, ctx->buf_size - ctx->buf_idx,
		      fmt, ap);
	ctx->buf_idx += n;
	if (ctx->buf_idx >= ctx->buf_size)
		ctx->buf_idx = ctx->buf_size;
}

static void format_btf_variable(struct btf *btf, char *buf, size_t buf_size,
				const struct btf_type *t, const void *btf_data)
{
	struct format_btf_ctx ctx = {
		.buf = buf,
		.buf_idx = 0,
		.buf_size = buf_size,
	};
	const struct btf_dump_type_data_opts opts = {
		.sz = sizeof(struct btf_dump_type_data_opts),
		.skip_names = 1,
		.compact = 1,
		.emit_strings = 1,
	};
	struct btf_dump *d;
	size_t btf_size;

	d = btf_dump__new(btf, format_btf_cb, &ctx, NULL);
	btf_size = btf__resolve_size(btf, t->type);
	btf_dump__dump_type_data(d, t->type, btf_data, btf_size, &opts);
	btf_dump__free(d);
}

static void bpf_metadata_fill_event(struct bpf_metadata_map *map,
				    struct perf_record_bpf_metadata *bpf_metadata_event)
{
	struct btf_var_secinfo *vsi;
	unsigned int i, vlen;

	memset(bpf_metadata_event->prog_name, 0, BPF_PROG_NAME_LEN);
	vlen = btf_vlen(map->datasec);
	vsi = btf_var_secinfos(map->datasec);

	for (i = 0; i < vlen; i++, vsi++) {
		const struct btf_type *t_var = btf__type_by_id(map->btf,
							       vsi->type);
		const char *name = btf__name_by_offset(map->btf,
						       t_var->name_off);
		const __u64 nr_entries = bpf_metadata_event->nr_entries;
		struct perf_record_bpf_metadata_entry *entry;

		if (!name_has_bpf_metadata_prefix(&name))
			continue;

		if (nr_entries >= (__u64)map->num_vars)
			break;

		entry = &bpf_metadata_event->entries[nr_entries];
		memset(entry, 0, sizeof(*entry));
		snprintf(entry->key, BPF_METADATA_KEY_LEN, "%s", name);
		format_btf_variable(map->btf, entry->value,
				    BPF_METADATA_VALUE_LEN, t_var,
				    map->rodata + vsi->offset);
		bpf_metadata_event->nr_entries++;
	}
}

static void bpf_metadata_free_map_data(struct bpf_metadata_map *map)
{
	btf__free(map->btf);
	free(map->rodata);
}

static struct bpf_metadata *bpf_metadata_alloc(__u32 nr_prog_tags,
					       __u32 nr_variables)
{
	struct bpf_metadata *metadata;
	size_t event_size;

	metadata = zalloc(sizeof(struct bpf_metadata));
	if (!metadata)
		return NULL;

	metadata->prog_names = zalloc(nr_prog_tags * sizeof(char *));
	if (!metadata->prog_names) {
		bpf_metadata_free(metadata);
		return NULL;
	}
	for (__u32 prog_index = 0; prog_index < nr_prog_tags; prog_index++) {
		metadata->prog_names[prog_index] = zalloc(BPF_PROG_NAME_LEN);
		if (!metadata->prog_names[prog_index]) {
			bpf_metadata_free(metadata);
			return NULL;
		}
		metadata->nr_prog_names++;
	}

	event_size = sizeof(metadata->event->bpf_metadata) +
	    nr_variables * sizeof(metadata->event->bpf_metadata.entries[0]);
	metadata->event = zalloc(event_size);
	if (!metadata->event) {
		bpf_metadata_free(metadata);
		return NULL;
	}
	metadata->event->bpf_metadata = (struct perf_record_bpf_metadata) {
		.header = {
			.type = PERF_RECORD_BPF_METADATA,
			.size = event_size,
		},
		.nr_entries = 0,
	};

	return metadata;
}

static struct bpf_metadata *bpf_metadata_create(struct bpf_prog_info *info)
{
	struct bpf_metadata *metadata;
	const __u32 *map_ids = (__u32 *)(uintptr_t)info->map_ids;

	for (__u32 map_index = 0; map_index < info->nr_map_ids; map_index++) {
		struct bpf_metadata_map map;

		if (bpf_metadata_read_map_data(map_ids[map_index], &map) != 0)
			continue;

		metadata = bpf_metadata_alloc(info->nr_prog_tags, map.num_vars);
		if (!metadata)
			continue;

		bpf_metadata_fill_event(&map, &metadata->event->bpf_metadata);

		for (__u32 index = 0; index < info->nr_prog_tags; index++) {
			synthesize_bpf_prog_name(metadata->prog_names[index],
						 BPF_PROG_NAME_LEN, info,
						 map.btf, index);
		}

		bpf_metadata_free_map_data(&map);

		return metadata;
	}

	return NULL;
}

static int synthesize_perf_record_bpf_metadata(const struct bpf_metadata *metadata,
					       const struct perf_tool *tool,
					       perf_event__handler_t process,
					       struct machine *machine)
{
	const size_t event_size = metadata->event->header.size;
	union perf_event *event;
	int err = 0;

	event = zalloc(event_size + machine->id_hdr_size);
	if (!event)
		return -1;
	memcpy(event, metadata->event, event_size);
	memset((void *)event + event->header.size, 0, machine->id_hdr_size);
	event->header.size += machine->id_hdr_size;
	for (__u32 index = 0; index < metadata->nr_prog_names; index++) {
		memcpy(event->bpf_metadata.prog_name,
		       metadata->prog_names[index], BPF_PROG_NAME_LEN);
		err = perf_tool__process_synth_event(tool, event, machine,
						     process);
		if (err != 0)
			break;
	}

	free(event);
	return err;
}

void bpf_metadata_free(struct bpf_metadata *metadata)
{
	if (metadata == NULL)
		return;
	for (__u32 index = 0; index < metadata->nr_prog_names; index++)
		free(metadata->prog_names[index]);
	free(metadata->prog_names);
	free(metadata->event);
	free(metadata);
}

#else /* HAVE_LIBBPF_STRINGS_SUPPORT */

static struct bpf_metadata *bpf_metadata_create(struct bpf_prog_info *info __maybe_unused)
{
	return NULL;
}

static int synthesize_perf_record_bpf_metadata(const struct bpf_metadata *metadata __maybe_unused,
					       const struct perf_tool *tool __maybe_unused,
					       perf_event__handler_t process __maybe_unused,
					       struct machine *machine __maybe_unused)
{
	return 0;
}

void bpf_metadata_free(struct bpf_metadata *metadata __maybe_unused)
{
}

#endif /* HAVE_LIBBPF_STRINGS_SUPPORT */

struct bpf_metadata_final_ctx {
	const struct perf_tool *tool;
	perf_event__handler_t process;
	struct machine *machine;
};

static void synthesize_final_bpf_metadata_cb(struct bpf_prog_info_node *node,
					     void *data)
{
	struct bpf_metadata_final_ctx *ctx = (struct bpf_metadata_final_ctx *)data;
	struct bpf_metadata *metadata = node->metadata;
	int err;

	if (metadata == NULL)
		return;
	err = synthesize_perf_record_bpf_metadata(metadata, ctx->tool,
						  ctx->process, ctx->machine);
	if (err != 0) {
		const char *prog_name = metadata->prog_names[0];

		if (prog_name != NULL)
			pr_warning("Couldn't synthesize final BPF metadata for %s.\n", prog_name);
		else
			pr_warning("Couldn't synthesize final BPF metadata.\n");
	}
	bpf_metadata_free(metadata);
	node->metadata = NULL;
}

void perf_event__synthesize_final_bpf_metadata(struct perf_session *session,
					       perf_event__handler_t process)
{
	struct perf_env *env = &session->header.env;
	struct bpf_metadata_final_ctx ctx = {
		.tool = session->tool,
		.process = process,
		.machine = &session->machines.host,
	};

	perf_env__iterate_bpf_prog_info(env, synthesize_final_bpf_metadata_cb,
					&ctx);
}

/*
 * Synthesize PERF_RECORD_KSYMBOL and PERF_RECORD_BPF_EVENT for one bpf
 * program. One PERF_RECORD_BPF_EVENT is generated for the program. And
 * one PERF_RECORD_KSYMBOL is generated for each sub program.
 *
 * Returns:
 *    0 for success;
 *   -1 for failures;
 *   -2 for lack of kernel support.
 */
static int perf_event__synthesize_one_bpf_prog(struct perf_session *session,
					       perf_event__handler_t process,
					       struct machine *machine,
					       int fd,
					       union perf_event *event,
					       struct record_opts *opts)
{
	struct perf_record_ksymbol *ksymbol_event = &event->ksymbol;
	struct perf_record_bpf_event *bpf_event = &event->bpf;
	const struct perf_tool *tool = session->tool;
	struct bpf_prog_info_node *info_node;
	struct perf_bpil *info_linear;
	struct bpf_metadata *metadata;
	struct bpf_prog_info *info;
	struct btf *btf = NULL;
	struct perf_env *env;
	u32 sub_prog_cnt, i;
	int err = 0;
	u64 arrays;

	/*
	 * for perf-record and perf-report use header.env;
	 * otherwise, use global perf_env.
	 */
	env = perf_session__env(session);

	arrays = 1UL << PERF_BPIL_JITED_KSYMS;
	arrays |= 1UL << PERF_BPIL_JITED_FUNC_LENS;
	arrays |= 1UL << PERF_BPIL_FUNC_INFO;
	arrays |= 1UL << PERF_BPIL_PROG_TAGS;
	arrays |= 1UL << PERF_BPIL_JITED_INSNS;
	arrays |= 1UL << PERF_BPIL_LINE_INFO;
	arrays |= 1UL << PERF_BPIL_JITED_LINE_INFO;
	arrays |= 1UL << PERF_BPIL_MAP_IDS;

	info_linear = get_bpf_prog_info_linear(fd, arrays);
	if (IS_ERR_OR_NULL(info_linear)) {
		info_linear = NULL;
		pr_debug("%s: failed to get BPF program info. aborting\n", __func__);
		return -1;
	}

	if (info_linear->info_len < offsetof(struct bpf_prog_info, prog_tags)) {
		free(info_linear);
		pr_debug("%s: the kernel is too old, aborting\n", __func__);
		return -2;
	}

	info = &info_linear->info;
	if (!info->jited_ksyms) {
		free(info_linear);
		return -1;
	}

	/* number of ksyms, func_lengths, and tags should match */
	sub_prog_cnt = info->nr_jited_ksyms;
	if (sub_prog_cnt != info->nr_prog_tags ||
	    sub_prog_cnt != info->nr_jited_func_lens) {
		free(info_linear);
		return -1;
	}

	/* check BTF func info support */
	if (info->btf_id && info->nr_func_info && info->func_info_rec_size) {
		/* btf func info number should be same as sub_prog_cnt */
		if (sub_prog_cnt != info->nr_func_info) {
			pr_debug("%s: mismatch in BPF sub program count and BTF function info count, aborting\n", __func__);
			free(info_linear);
			return -1;
		}
		btf = btf__load_from_kernel_by_id(info->btf_id);
		if (libbpf_get_error(btf)) {
			pr_debug("%s: failed to get BTF of id %u, aborting\n", __func__, info->btf_id);
			err = -1;
			goto out;
		}
		perf_env__fetch_btf(env, info->btf_id, btf);
	}

	/* Synthesize PERF_RECORD_KSYMBOL */
	for (i = 0; i < sub_prog_cnt; i++) {
		__u32 *prog_lens = (__u32 *)(uintptr_t)(info->jited_func_lens);
		__u64 *prog_addrs = (__u64 *)(uintptr_t)(info->jited_ksyms);
		int name_len;

		*ksymbol_event = (struct perf_record_ksymbol) {
			.header = {
				.type = PERF_RECORD_KSYMBOL,
				.size = offsetof(struct perf_record_ksymbol, name),
			},
			.addr = prog_addrs[i],
			.len = prog_lens[i],
			.ksym_type = PERF_RECORD_KSYMBOL_TYPE_BPF,
			.flags = 0,
		};

		name_len = synthesize_bpf_prog_name(ksymbol_event->name,
						    KSYM_NAME_LEN, info, btf, i);
		ksymbol_event->header.size += PERF_ALIGN(name_len + 1,
							 sizeof(u64));

		memset((void *)event + event->header.size, 0, machine->id_hdr_size);
		event->header.size += machine->id_hdr_size;
		err = perf_tool__process_synth_event(tool, event,
						     machine, process);
	}

	if (!opts->no_bpf_event) {
		/* Synthesize PERF_RECORD_BPF_EVENT */
		*bpf_event = (struct perf_record_bpf_event) {
			.header = {
				.type = PERF_RECORD_BPF_EVENT,
				.size = sizeof(struct perf_record_bpf_event),
			},
			.type = PERF_BPF_EVENT_PROG_LOAD,
			.flags = 0,
			.id = info->id,
		};
		memcpy(bpf_event->tag, info->tag, BPF_TAG_SIZE);
		memset((void *)event + event->header.size, 0, machine->id_hdr_size);
		event->header.size += machine->id_hdr_size;

		/* save bpf_prog_info to env */
		info_node = malloc(sizeof(struct bpf_prog_info_node));
		if (!info_node) {
			err = -1;
			goto out;
		}

		info_node->info_linear = info_linear;
		info_node->metadata = NULL;
		if (!perf_env__insert_bpf_prog_info(env, info_node)) {
			/*
			 * Insert failed, likely because of a duplicate event
			 * made by the sideband thread. Ignore synthesizing the
			 * metadata.
			 */
			free(info_node);
			goto out;
		}
		/* info_linear is now owned by info_node and shouldn't be freed below. */
		info_linear = NULL;

		/*
		 * process after saving bpf_prog_info to env, so that
		 * required information is ready for look up
		 */
		err = perf_tool__process_synth_event(tool, event,
						     machine, process);

		/* Synthesize PERF_RECORD_BPF_METADATA */
		metadata = bpf_metadata_create(info);
		if (metadata != NULL) {
			err = synthesize_perf_record_bpf_metadata(metadata,
								  tool, process,
								  machine);
			bpf_metadata_free(metadata);
		}
	}

out:
	free(info_linear);
	btf__free(btf);
	return err ? -1 : 0;
}

struct kallsyms_parse {
	union perf_event	*event;
	perf_event__handler_t	 process;
	struct machine		*machine;
	const struct perf_tool	*tool;
};

static int
process_bpf_image(char *name, u64 addr, struct kallsyms_parse *data)
{
	struct machine *machine = data->machine;
	union perf_event *event = data->event;
	struct perf_record_ksymbol *ksymbol;
	int len;

	ksymbol = &event->ksymbol;

	*ksymbol = (struct perf_record_ksymbol) {
		.header = {
			.type = PERF_RECORD_KSYMBOL,
			.size = offsetof(struct perf_record_ksymbol, name),
		},
		.addr      = addr,
		.len       = page_size,
		.ksym_type = PERF_RECORD_KSYMBOL_TYPE_BPF,
		.flags     = 0,
	};

	len = scnprintf(ksymbol->name, KSYM_NAME_LEN, "%s", name);
	ksymbol->header.size += PERF_ALIGN(len + 1, sizeof(u64));
	memset((void *) event + event->header.size, 0, machine->id_hdr_size);
	event->header.size += machine->id_hdr_size;

	return perf_tool__process_synth_event(data->tool, event, machine,
					      data->process);
}

static int
kallsyms_process_symbol(void *data, const char *_name,
			char type __maybe_unused, u64 start)
{
	char disp[KSYM_NAME_LEN];
	char *module, *name;
	unsigned long id;
	int err = 0;

	module = strchr(_name, '\t');
	if (!module)
		return 0;

	/* We are going after [bpf] module ... */
	if (strcmp(module + 1, "[bpf]"))
		return 0;

	name = memdup(_name, (module - _name) + 1);
	if (!name)
		return -ENOMEM;

	name[module - _name] = 0;

	/* .. and only for trampolines and dispatchers */
	if ((sscanf(name, "bpf_trampoline_%lu", &id) == 1) ||
	    (sscanf(name, "bpf_dispatcher_%s", disp) == 1))
		err = process_bpf_image(name, start, data);

	free(name);
	return err;
}

int perf_event__synthesize_bpf_events(struct perf_session *session,
				      perf_event__handler_t process,
				      struct machine *machine,
				      struct record_opts *opts)
{
	const char *kallsyms_filename = "/proc/kallsyms";
	struct kallsyms_parse arg;
	union perf_event *event;
	__u32 id = 0;
	int err;
	int fd;

	if (opts->no_bpf_event)
		return 0;

	event = malloc(sizeof(event->bpf) + KSYM_NAME_LEN + machine->id_hdr_size);
	if (!event)
		return -1;

	/* Synthesize all the bpf programs in system. */
	while (true) {
		err = bpf_prog_get_next_id(id, &id);
		if (err) {
			if (errno == ENOENT) {
				err = 0;
				break;
			}
			pr_debug("%s: can't get next program: %s%s\n",
				 __func__, strerror(errno),
				 errno == EINVAL ? " -- kernel too old?" : "");
			/* don't report error on old kernel or EPERM  */
			err = (errno == EINVAL || errno == EPERM) ? 0 : -1;
			break;
		}
		fd = bpf_prog_get_fd_by_id(id);
		if (fd < 0) {
			pr_debug("%s: failed to get fd for prog_id %u\n",
				 __func__, id);
			continue;
		}

		err = perf_event__synthesize_one_bpf_prog(session, process,
							  machine, fd,
							  event, opts);
		close(fd);
		if (err) {
			/* do not return error for old kernel */
			if (err == -2)
				err = 0;
			break;
		}
	}

	/* Synthesize all the bpf images - trampolines/dispatchers. */
	if (symbol_conf.kallsyms_name != NULL)
		kallsyms_filename = symbol_conf.kallsyms_name;

	arg = (struct kallsyms_parse) {
		.event   = event,
		.process = process,
		.machine = machine,
		.tool    = session->tool,
	};

	if (kallsyms__parse(kallsyms_filename, &arg, kallsyms_process_symbol)) {
		pr_err("%s: failed to synthesize bpf images: %s\n",
		       __func__, strerror(errno));
	}

	free(event);
	return err;
}

static int perf_env__add_bpf_info(struct perf_env *env, u32 id)
{
	struct bpf_prog_info_node *info_node;
	struct perf_bpil *info_linear;
	struct btf *btf = NULL;
	u64 arrays;
	u32 btf_id;
	int fd, err = 0;

	fd = bpf_prog_get_fd_by_id(id);
	if (fd < 0)
		return -EINVAL;

	arrays = 1UL << PERF_BPIL_JITED_KSYMS;
	arrays |= 1UL << PERF_BPIL_JITED_FUNC_LENS;
	arrays |= 1UL << PERF_BPIL_FUNC_INFO;
	arrays |= 1UL << PERF_BPIL_PROG_TAGS;
	arrays |= 1UL << PERF_BPIL_JITED_INSNS;
	arrays |= 1UL << PERF_BPIL_LINE_INFO;
	arrays |= 1UL << PERF_BPIL_JITED_LINE_INFO;
	arrays |= 1UL << PERF_BPIL_MAP_IDS;

	info_linear = get_bpf_prog_info_linear(fd, arrays);
	if (IS_ERR_OR_NULL(info_linear)) {
		pr_debug("%s: failed to get BPF program info. aborting\n", __func__);
		err = PTR_ERR(info_linear);
		goto out;
	}

	btf_id = info_linear->info.btf_id;

	info_node = malloc(sizeof(struct bpf_prog_info_node));
	if (info_node) {
		info_node->info_linear = info_linear;
		info_node->metadata = bpf_metadata_create(&info_linear->info);
		if (!perf_env__insert_bpf_prog_info(env, info_node)) {
			pr_debug("%s: duplicate add bpf info request for id %u\n",
				 __func__, btf_id);
			free(info_linear);
			free(info_node);
			goto out;
		}
	} else {
		free(info_linear);
		err = -ENOMEM;
		goto out;
	}

	if (btf_id == 0)
		goto out;

	btf = btf__load_from_kernel_by_id(btf_id);
	if (!btf) {
		err = -errno;
		pr_debug("%s: failed to get BTF of id %u %d\n", __func__, btf_id, err);
	} else {
		perf_env__fetch_btf(env, btf_id, btf);
	}

out:
	btf__free(btf);
	close(fd);
	return err;
}

static int bpf_event__sb_cb(union perf_event *event, void *data)
{
	struct perf_env *env = data;
	int ret = 0;

	if (event->header.type != PERF_RECORD_BPF_EVENT)
		return -1;

	switch (event->bpf.type) {
	case PERF_BPF_EVENT_PROG_LOAD:
		ret = perf_env__add_bpf_info(env, event->bpf.id);

	case PERF_BPF_EVENT_PROG_UNLOAD:
		/*
		 * Do not free bpf_prog_info and btf of the program here,
		 * as annotation still need them. They will be freed at
		 * the end of the session.
		 */
		break;
	default:
		pr_debug("unexpected bpf event type of %d\n", event->bpf.type);
		break;
	}

	return ret;
}

int evlist__add_bpf_sb_event(struct evlist *evlist, struct perf_env *env)
{
	struct perf_event_attr attr = {
		.type	          = PERF_TYPE_SOFTWARE,
		.config           = PERF_COUNT_SW_DUMMY,
		.sample_id_all    = 1,
		.watermark        = 1,
		.bpf_event        = 1,
		.size	   = sizeof(attr), /* to capture ABI version */
	};

	/*
	 * Older gcc versions don't support designated initializers, like above,
	 * for unnamed union members, such as the following:
	 */
	attr.wakeup_watermark = 1;

	return evlist__add_sb_event(evlist, &attr, bpf_event__sb_cb, env);
}

void __bpf_event__print_bpf_prog_info(struct bpf_prog_info *info,
				      struct perf_env *env,
				      FILE *fp)
{
	__u32 *prog_lens = (__u32 *)(uintptr_t)(info->jited_func_lens);
	__u64 *prog_addrs = (__u64 *)(uintptr_t)(info->jited_ksyms);
	char name[KSYM_NAME_LEN];
	struct btf *btf = NULL;
	u32 sub_prog_cnt, i;

	sub_prog_cnt = info->nr_jited_ksyms;
	if (sub_prog_cnt != info->nr_prog_tags ||
	    sub_prog_cnt != info->nr_jited_func_lens)
		return;

	if (info->btf_id) {
		struct btf_node *node;

		node = __perf_env__find_btf(env, info->btf_id);
		if (node)
			btf = btf__new((__u8 *)(node->data),
				       node->data_size);
	}

	if (sub_prog_cnt == 1) {
		synthesize_bpf_prog_name(name, KSYM_NAME_LEN, info, btf, 0);
		fprintf(fp, "# bpf_prog_info %u: %s addr 0x%llx size %u\n",
			info->id, name, prog_addrs[0], prog_lens[0]);
		goto out;
	}

	fprintf(fp, "# bpf_prog_info %u:\n", info->id);
	for (i = 0; i < sub_prog_cnt; i++) {
		synthesize_bpf_prog_name(name, KSYM_NAME_LEN, info, btf, i);

		fprintf(fp, "# \tsub_prog %u: %s addr 0x%llx size %u\n",
			i, name, prog_addrs[i], prog_lens[i]);
	}
out:
	btf__free(btf);
}

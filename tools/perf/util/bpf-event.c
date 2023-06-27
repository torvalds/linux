// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdlib.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>
#include <linux/btf.h>
#include <linux/err.h>
#include <linux/string.h>
#include <internal/lib.h>
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

			dso->binary_type = DSO_BINARY_TYPE__BPF_PROG_INFO;
			dso->bpf_prog.id = id;
			dso->bpf_prog.sub_id = i;
			dso->bpf_prog.env = env;
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
	struct perf_tool *tool = session->tool;
	struct bpf_prog_info_node *info_node;
	struct perf_bpil *info_linear;
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
	env = session->data ? &session->header.env : &perf_env;

	arrays = 1UL << PERF_BPIL_JITED_KSYMS;
	arrays |= 1UL << PERF_BPIL_JITED_FUNC_LENS;
	arrays |= 1UL << PERF_BPIL_FUNC_INFO;
	arrays |= 1UL << PERF_BPIL_PROG_TAGS;
	arrays |= 1UL << PERF_BPIL_JITED_INSNS;
	arrays |= 1UL << PERF_BPIL_LINE_INFO;
	arrays |= 1UL << PERF_BPIL_JITED_LINE_INFO;

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
		perf_env__insert_bpf_prog_info(env, info_node);
		info_linear = NULL;

		/*
		 * process after saving bpf_prog_info to env, so that
		 * required information is ready for look up
		 */
		err = perf_tool__process_synth_event(tool, event,
						     machine, process);
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
	struct perf_tool	*tool;
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

static void perf_env__add_bpf_info(struct perf_env *env, u32 id)
{
	struct bpf_prog_info_node *info_node;
	struct perf_bpil *info_linear;
	struct btf *btf = NULL;
	u64 arrays;
	u32 btf_id;
	int fd;

	fd = bpf_prog_get_fd_by_id(id);
	if (fd < 0)
		return;

	arrays = 1UL << PERF_BPIL_JITED_KSYMS;
	arrays |= 1UL << PERF_BPIL_JITED_FUNC_LENS;
	arrays |= 1UL << PERF_BPIL_FUNC_INFO;
	arrays |= 1UL << PERF_BPIL_PROG_TAGS;
	arrays |= 1UL << PERF_BPIL_JITED_INSNS;
	arrays |= 1UL << PERF_BPIL_LINE_INFO;
	arrays |= 1UL << PERF_BPIL_JITED_LINE_INFO;

	info_linear = get_bpf_prog_info_linear(fd, arrays);
	if (IS_ERR_OR_NULL(info_linear)) {
		pr_debug("%s: failed to get BPF program info. aborting\n", __func__);
		goto out;
	}

	btf_id = info_linear->info.btf_id;

	info_node = malloc(sizeof(struct bpf_prog_info_node));
	if (info_node) {
		info_node->info_linear = info_linear;
		perf_env__insert_bpf_prog_info(env, info_node);
	} else
		free(info_linear);

	if (btf_id == 0)
		goto out;

	btf = btf__load_from_kernel_by_id(btf_id);
	if (libbpf_get_error(btf)) {
		pr_debug("%s: failed to get BTF of id %u, aborting\n",
			 __func__, btf_id);
		goto out;
	}
	perf_env__fetch_btf(env, btf_id, btf);

out:
	btf__free(btf);
	close(fd);
}

static int bpf_event__sb_cb(union perf_event *event, void *data)
{
	struct perf_env *env = data;

	if (event->header.type != PERF_RECORD_BPF_EVENT)
		return -1;

	switch (event->bpf.type) {
	case PERF_BPF_EVENT_PROG_LOAD:
		perf_env__add_bpf_info(env, event->bpf.id);

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

void bpf_event__print_bpf_prog_info(struct bpf_prog_info *info,
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

		node = perf_env__find_btf(env, info->btf_id);
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

// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdlib.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <linux/btf.h>
#include "bpf-event.h"
#include "debug.h"
#include "symbol.h"
#include "machine.h"

#define ptr_to_u64(ptr)    ((__u64)(unsigned long)(ptr))

static int snprintf_hex(char *buf, size_t size, unsigned char *data, size_t len)
{
	int ret = 0;
	size_t i;

	for (i = 0; i < len; i++)
		ret += snprintf(buf + ret, size - ret, "%02x", data[i]);
	return ret;
}

int machine__process_bpf_event(struct machine *machine __maybe_unused,
			       union perf_event *event,
			       struct perf_sample *sample __maybe_unused)
{
	if (dump_trace)
		perf_event__fprintf_bpf_event(event, stdout);
	return 0;
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
static int perf_event__synthesize_one_bpf_prog(struct perf_tool *tool,
					       perf_event__handler_t process,
					       struct machine *machine,
					       int fd,
					       union perf_event *event,
					       struct record_opts *opts)
{
	struct ksymbol_event *ksymbol_event = &event->ksymbol_event;
	struct bpf_event *bpf_event = &event->bpf_event;
	u32 sub_prog_cnt, i, func_info_rec_size = 0;
	u8 (*prog_tags)[BPF_TAG_SIZE] = NULL;
	struct bpf_prog_info info = { .type = 0, };
	u32 info_len = sizeof(info);
	void *func_infos = NULL;
	u64 *prog_addrs = NULL;
	struct btf *btf = NULL;
	u32 *prog_lens = NULL;
	bool has_btf = false;
	char errbuf[512];
	int err = 0;

	/* Call bpf_obj_get_info_by_fd() to get sizes of arrays */
	err = bpf_obj_get_info_by_fd(fd, &info, &info_len);

	if (err) {
		pr_debug("%s: failed to get BPF program info: %s, aborting\n",
			 __func__, str_error_r(errno, errbuf, sizeof(errbuf)));
		return -1;
	}
	if (info_len < offsetof(struct bpf_prog_info, prog_tags)) {
		pr_debug("%s: the kernel is too old, aborting\n", __func__);
		return -2;
	}

	/* number of ksyms, func_lengths, and tags should match */
	sub_prog_cnt = info.nr_jited_ksyms;
	if (sub_prog_cnt != info.nr_prog_tags ||
	    sub_prog_cnt != info.nr_jited_func_lens)
		return -1;

	/* check BTF func info support */
	if (info.btf_id && info.nr_func_info && info.func_info_rec_size) {
		/* btf func info number should be same as sub_prog_cnt */
		if (sub_prog_cnt != info.nr_func_info) {
			pr_debug("%s: mismatch in BPF sub program count and BTF function info count, aborting\n", __func__);
			return -1;
		}
		if (btf__get_from_id(info.btf_id, &btf)) {
			pr_debug("%s: failed to get BTF of id %u, aborting\n", __func__, info.btf_id);
			return -1;
		}
		func_info_rec_size = info.func_info_rec_size;
		func_infos = calloc(sub_prog_cnt, func_info_rec_size);
		if (!func_infos) {
			pr_debug("%s: failed to allocate memory for func_infos, aborting\n", __func__);
			return -1;
		}
		has_btf = true;
	}

	/*
	 * We need address, length, and tag for each sub program.
	 * Allocate memory and call bpf_obj_get_info_by_fd() again
	 */
	prog_addrs = calloc(sub_prog_cnt, sizeof(u64));
	if (!prog_addrs) {
		pr_debug("%s: failed to allocate memory for prog_addrs, aborting\n", __func__);
		goto out;
	}
	prog_lens = calloc(sub_prog_cnt, sizeof(u32));
	if (!prog_lens) {
		pr_debug("%s: failed to allocate memory for prog_lens, aborting\n", __func__);
		goto out;
	}
	prog_tags = calloc(sub_prog_cnt, BPF_TAG_SIZE);
	if (!prog_tags) {
		pr_debug("%s: failed to allocate memory for prog_tags, aborting\n", __func__);
		goto out;
	}

	memset(&info, 0, sizeof(info));
	info.nr_jited_ksyms = sub_prog_cnt;
	info.nr_jited_func_lens = sub_prog_cnt;
	info.nr_prog_tags = sub_prog_cnt;
	info.jited_ksyms = ptr_to_u64(prog_addrs);
	info.jited_func_lens = ptr_to_u64(prog_lens);
	info.prog_tags = ptr_to_u64(prog_tags);
	info_len = sizeof(info);
	if (has_btf) {
		info.nr_func_info = sub_prog_cnt;
		info.func_info_rec_size = func_info_rec_size;
		info.func_info = ptr_to_u64(func_infos);
	}

	err = bpf_obj_get_info_by_fd(fd, &info, &info_len);
	if (err) {
		pr_debug("%s: failed to get BPF program info, aborting\n", __func__);
		goto out;
	}

	/* Synthesize PERF_RECORD_KSYMBOL */
	for (i = 0; i < sub_prog_cnt; i++) {
		const struct bpf_func_info *finfo;
		const char *short_name = NULL;
		const struct btf_type *t;
		int name_len;

		*ksymbol_event = (struct ksymbol_event){
			.header = {
				.type = PERF_RECORD_KSYMBOL,
				.size = offsetof(struct ksymbol_event, name),
			},
			.addr = prog_addrs[i],
			.len = prog_lens[i],
			.ksym_type = PERF_RECORD_KSYMBOL_TYPE_BPF,
			.flags = 0,
		};
		name_len = snprintf(ksymbol_event->name, KSYM_NAME_LEN,
				    "bpf_prog_");
		name_len += snprintf_hex(ksymbol_event->name + name_len,
					 KSYM_NAME_LEN - name_len,
					 prog_tags[i], BPF_TAG_SIZE);
		if (has_btf) {
			finfo = func_infos + i * info.func_info_rec_size;
			t = btf__type_by_id(btf, finfo->type_id);
			short_name = btf__name_by_offset(btf, t->name_off);
		} else if (i == 0 && sub_prog_cnt == 1) {
			/* no subprog */
			if (info.name[0])
				short_name = info.name;
		} else
			short_name = "F";
		if (short_name)
			name_len += snprintf(ksymbol_event->name + name_len,
					     KSYM_NAME_LEN - name_len,
					     "_%s", short_name);

		ksymbol_event->header.size += PERF_ALIGN(name_len + 1,
							 sizeof(u64));

		memset((void *)event + event->header.size, 0, machine->id_hdr_size);
		event->header.size += machine->id_hdr_size;
		err = perf_tool__process_synth_event(tool, event,
						     machine, process);
	}

	/* Synthesize PERF_RECORD_BPF_EVENT */
	if (opts->bpf_event) {
		*bpf_event = (struct bpf_event){
			.header = {
				.type = PERF_RECORD_BPF_EVENT,
				.size = sizeof(struct bpf_event),
			},
			.type = PERF_BPF_EVENT_PROG_LOAD,
			.flags = 0,
			.id = info.id,
		};
		memcpy(bpf_event->tag, prog_tags[i], BPF_TAG_SIZE);
		memset((void *)event + event->header.size, 0, machine->id_hdr_size);
		event->header.size += machine->id_hdr_size;
		err = perf_tool__process_synth_event(tool, event,
						     machine, process);
	}

out:
	free(prog_tags);
	free(prog_lens);
	free(prog_addrs);
	free(func_infos);
	free(btf);
	return err ? -1 : 0;
}

int perf_event__synthesize_bpf_events(struct perf_tool *tool,
				      perf_event__handler_t process,
				      struct machine *machine,
				      struct record_opts *opts)
{
	union perf_event *event;
	__u32 id = 0;
	int err;
	int fd;

	event = malloc(sizeof(event->bpf_event) + KSYM_NAME_LEN + machine->id_hdr_size);
	if (!event)
		return -1;
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

		err = perf_event__synthesize_one_bpf_prog(tool, process,
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
	free(event);
	return err;
}

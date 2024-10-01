// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2020 Facebook */
#include <errno.h>
#include <linux/err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/hashmap.h>

#include "main.h"
#include "skeleton/pid_iter.h"

#ifdef BPFTOOL_WITHOUT_SKELETONS

int build_obj_refs_table(struct hashmap **map, enum bpf_obj_type type)
{
	return -ENOTSUP;
}
void delete_obj_refs_table(struct hashmap *map) {}
void emit_obj_refs_plain(struct hashmap *map, __u32 id, const char *prefix) {}
void emit_obj_refs_json(struct hashmap *map, __u32 id, json_writer_t *json_writer) {}

#else /* BPFTOOL_WITHOUT_SKELETONS */

#include "pid_iter.skel.h"

static void add_ref(struct hashmap *map, struct pid_iter_entry *e)
{
	struct hashmap_entry *entry;
	struct obj_refs *refs;
	struct obj_ref *ref;
	int err, i;
	void *tmp;

	hashmap__for_each_key_entry(map, entry, e->id) {
		refs = entry->pvalue;

		for (i = 0; i < refs->ref_cnt; i++) {
			if (refs->refs[i].pid == e->pid)
				return;
		}

		tmp = realloc(refs->refs, (refs->ref_cnt + 1) * sizeof(*ref));
		if (!tmp) {
			p_err("failed to re-alloc memory for ID %u, PID %d, COMM %s...",
			      e->id, e->pid, e->comm);
			return;
		}
		refs->refs = tmp;
		ref = &refs->refs[refs->ref_cnt];
		ref->pid = e->pid;
		memcpy(ref->comm, e->comm, sizeof(ref->comm));
		refs->ref_cnt++;

		return;
	}

	/* new ref */
	refs = calloc(1, sizeof(*refs));
	if (!refs) {
		p_err("failed to alloc memory for ID %u, PID %d, COMM %s...",
		      e->id, e->pid, e->comm);
		return;
	}

	refs->refs = malloc(sizeof(*refs->refs));
	if (!refs->refs) {
		free(refs);
		p_err("failed to alloc memory for ID %u, PID %d, COMM %s...",
		      e->id, e->pid, e->comm);
		return;
	}
	ref = &refs->refs[0];
	ref->pid = e->pid;
	memcpy(ref->comm, e->comm, sizeof(ref->comm));
	refs->ref_cnt = 1;
	refs->has_bpf_cookie = e->has_bpf_cookie;
	refs->bpf_cookie = e->bpf_cookie;

	err = hashmap__append(map, e->id, refs);
	if (err)
		p_err("failed to append entry to hashmap for ID %u: %s",
		      e->id, strerror(errno));
}

static int __printf(2, 0)
libbpf_print_none(__maybe_unused enum libbpf_print_level level,
		  __maybe_unused const char *format,
		  __maybe_unused va_list args)
{
	return 0;
}

int build_obj_refs_table(struct hashmap **map, enum bpf_obj_type type)
{
	struct pid_iter_entry *e;
	char buf[4096 / sizeof(*e) * sizeof(*e)];
	struct pid_iter_bpf *skel;
	int err, ret, fd = -1, i;

	*map = hashmap__new(hash_fn_for_key_as_id, equal_fn_for_key_as_id, NULL);
	if (IS_ERR(*map)) {
		p_err("failed to create hashmap for PID references");
		return -1;
	}
	set_max_rlimit();

	skel = pid_iter_bpf__open();
	if (!skel) {
		p_err("failed to open PID iterator skeleton");
		return -1;
	}

	skel->rodata->obj_type = type;

	if (!verifier_logs) {
		libbpf_print_fn_t default_print;

		/* Unless debug information is on, we don't want the output to
		 * be polluted with libbpf errors if bpf_iter is not supported.
		 */
		default_print = libbpf_set_print(libbpf_print_none);
		err = pid_iter_bpf__load(skel);
		libbpf_set_print(default_print);
	} else {
		err = pid_iter_bpf__load(skel);
	}
	if (err) {
		/* too bad, kernel doesn't support BPF iterators yet */
		err = 0;
		goto out;
	}
	err = pid_iter_bpf__attach(skel);
	if (err) {
		/* if we loaded above successfully, attach has to succeed */
		p_err("failed to attach PID iterator: %d", err);
		goto out;
	}

	fd = bpf_iter_create(bpf_link__fd(skel->links.iter));
	if (fd < 0) {
		err = -errno;
		p_err("failed to create PID iterator session: %d", err);
		goto out;
	}

	while (true) {
		ret = read(fd, buf, sizeof(buf));
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
			err = -errno;
			p_err("failed to read PID iterator output: %d", err);
			goto out;
		}
		if (ret == 0)
			break;
		if (ret % sizeof(*e)) {
			err = -EINVAL;
			p_err("invalid PID iterator output format");
			goto out;
		}
		ret /= sizeof(*e);

		e = (void *)buf;
		for (i = 0; i < ret; i++, e++) {
			add_ref(*map, e);
		}
	}
	err = 0;
out:
	if (fd >= 0)
		close(fd);
	pid_iter_bpf__destroy(skel);
	return err;
}

void delete_obj_refs_table(struct hashmap *map)
{
	struct hashmap_entry *entry;
	size_t bkt;

	if (!map)
		return;

	hashmap__for_each_entry(map, entry, bkt) {
		struct obj_refs *refs = entry->pvalue;

		free(refs->refs);
		free(refs);
	}

	hashmap__free(map);
}

void emit_obj_refs_json(struct hashmap *map, __u32 id,
			json_writer_t *json_writer)
{
	struct hashmap_entry *entry;

	if (hashmap__empty(map))
		return;

	hashmap__for_each_key_entry(map, entry, id) {
		struct obj_refs *refs = entry->pvalue;
		int i;

		if (refs->ref_cnt == 0)
			break;

		if (refs->has_bpf_cookie)
			jsonw_lluint_field(json_writer, "bpf_cookie", refs->bpf_cookie);

		jsonw_name(json_writer, "pids");
		jsonw_start_array(json_writer);
		for (i = 0; i < refs->ref_cnt; i++) {
			struct obj_ref *ref = &refs->refs[i];

			jsonw_start_object(json_writer);
			jsonw_int_field(json_writer, "pid", ref->pid);
			jsonw_string_field(json_writer, "comm", ref->comm);
			jsonw_end_object(json_writer);
		}
		jsonw_end_array(json_writer);
		break;
	}
}

void emit_obj_refs_plain(struct hashmap *map, __u32 id, const char *prefix)
{
	struct hashmap_entry *entry;

	if (hashmap__empty(map))
		return;

	hashmap__for_each_key_entry(map, entry, id) {
		struct obj_refs *refs = entry->pvalue;
		int i;

		if (refs->ref_cnt == 0)
			break;

		if (refs->has_bpf_cookie)
			printf("\n\tbpf_cookie %llu", (unsigned long long) refs->bpf_cookie);

		printf("%s", prefix);
		for (i = 0; i < refs->ref_cnt; i++) {
			struct obj_ref *ref = &refs->refs[i];

			printf("%s%s(%d)", i == 0 ? "" : ", ", ref->comm, ref->pid);
		}
		break;
	}
}


#endif

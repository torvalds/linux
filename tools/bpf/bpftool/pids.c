// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2020 Facebook */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <bpf/bpf.h>

#include "main.h"
#include "skeleton/pid_iter.h"

#ifdef BPFTOOL_WITHOUT_SKELETONS

int build_obj_refs_table(struct obj_refs_table *table, enum bpf_obj_type type)
{
	return -ENOTSUP;
}
void delete_obj_refs_table(struct obj_refs_table *table) {}
void emit_obj_refs_plain(struct obj_refs_table *table, __u32 id, const char *prefix) {}
void emit_obj_refs_json(struct obj_refs_table *table, __u32 id, json_writer_t *json_writer) {}

#else /* BPFTOOL_WITHOUT_SKELETONS */

#include "pid_iter.skel.h"

static void add_ref(struct obj_refs_table *table, struct pid_iter_entry *e)
{
	struct obj_refs *refs;
	struct obj_ref *ref;
	void *tmp;
	int i;

	hash_for_each_possible(table->table, refs, node, e->id) {
		if (refs->id != e->id)
			continue;

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

	refs->id = e->id;
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
	hash_add(table->table, &refs->node, e->id);
}

static int __printf(2, 0)
libbpf_print_none(__maybe_unused enum libbpf_print_level level,
		  __maybe_unused const char *format,
		  __maybe_unused va_list args)
{
	return 0;
}

int build_obj_refs_table(struct obj_refs_table *table, enum bpf_obj_type type)
{
	char buf[4096];
	struct pid_iter_bpf *skel;
	struct pid_iter_entry *e;
	int err, ret, fd = -1, i;
	libbpf_print_fn_t default_print;

	hash_init(table->table);
	set_max_rlimit();

	skel = pid_iter_bpf__open();
	if (!skel) {
		p_err("failed to open PID iterator skeleton");
		return -1;
	}

	skel->rodata->obj_type = type;

	/* we don't want output polluted with libbpf errors if bpf_iter is not
	 * supported
	 */
	default_print = libbpf_set_print(libbpf_print_none);
	err = pid_iter_bpf__load(skel);
	libbpf_set_print(default_print);
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
			add_ref(table, e);
		}
	}
	err = 0;
out:
	if (fd >= 0)
		close(fd);
	pid_iter_bpf__destroy(skel);
	return err;
}

void delete_obj_refs_table(struct obj_refs_table *table)
{
	struct obj_refs *refs;
	struct hlist_node *tmp;
	unsigned int bkt;

	hash_for_each_safe(table->table, bkt, tmp, refs, node) {
		hash_del(&refs->node);
		free(refs->refs);
		free(refs);
	}
}

void emit_obj_refs_json(struct obj_refs_table *table, __u32 id,
			json_writer_t *json_writer)
{
	struct obj_refs *refs;
	struct obj_ref *ref;
	int i;

	if (hash_empty(table->table))
		return;

	hash_for_each_possible(table->table, refs, node, id) {
		if (refs->id != id)
			continue;
		if (refs->ref_cnt == 0)
			break;

		jsonw_name(json_writer, "pids");
		jsonw_start_array(json_writer);
		for (i = 0; i < refs->ref_cnt; i++) {
			ref = &refs->refs[i];
			jsonw_start_object(json_writer);
			jsonw_int_field(json_writer, "pid", ref->pid);
			jsonw_string_field(json_writer, "comm", ref->comm);
			jsonw_end_object(json_writer);
		}
		jsonw_end_array(json_writer);
		break;
	}
}

void emit_obj_refs_plain(struct obj_refs_table *table, __u32 id, const char *prefix)
{
	struct obj_refs *refs;
	struct obj_ref *ref;
	int i;

	if (hash_empty(table->table))
		return;

	hash_for_each_possible(table->table, refs, node, id) {
		if (refs->id != id)
			continue;
		if (refs->ref_cnt == 0)
			break;

		printf("%s", prefix);
		for (i = 0; i < refs->ref_cnt; i++) {
			ref = &refs->refs[i];
			printf("%s%s(%d)", i == 0 ? "" : ", ", ref->comm, ref->pid);
		}
		break;
	}
}


#endif

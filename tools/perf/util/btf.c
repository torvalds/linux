// SPDX-License-Identifier: GPL-2.0
/*
 * Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Copyright (C) 2024, Red Hat, Inc
 */

#include <bpf/btf.h>
#include <util/btf.h>
#include <string.h>

const struct btf_member *__btf_type__find_member_by_name(struct btf *btf,
							 int type_id, const char *member_name)
{
	const struct btf_type *t = btf__type_by_id(btf, type_id);
	const struct btf_member *m;
	int i;

	for (i = 0, m = btf_members(t); i < btf_vlen(t); i++, m++) {
		const char *current_member_name = btf__name_by_offset(btf, m->name_off);

		if (!strcmp(current_member_name, member_name))
			return m;
	}

	return NULL;
}

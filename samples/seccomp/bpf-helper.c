// SPDX-License-Identifier: GPL-2.0
/*
 * Seccomp BPF helper functions
 *
 * Copyright (c) 2012 The Chromium OS Authors <chromium-os-dev@chromium.org>
 * Author: Will Drewry <wad@chromium.org>
 *
 * The code may be used by anyone for any purpose,
 * and can serve as a starting point for developing
 * applications using prctl(PR_ATTACH_SECCOMP_FILTER).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bpf-helper.h"

int bpf_resolve_jumps(struct bpf_labels *labels,
		      struct sock_filter *filter, size_t count)
{
	size_t i;

	if (count < 1 || count > BPF_MAXINSNS)
		return -1;
	/*
	* Walk it once, backwards, to build the label table and do fixups.
	* Since backward jumps are disallowed by BPF, this is easy.
	*/
	for (i = 0; i < count; ++i) {
		size_t offset = count - i - 1;
		struct sock_filter *instr = &filter[offset];
		if (instr->code != (BPF_JMP+BPF_JA))
			continue;
		switch ((instr->jt<<8)|instr->jf) {
		case (JUMP_JT<<8)|JUMP_JF:
			if (labels->labels[instr->k].location == 0xffffffff) {
				fprintf(stderr, "Unresolved label: '%s'\n",
					labels->labels[instr->k].label);
				return 1;
			}
			instr->k = labels->labels[instr->k].location -
				    (offset + 1);
			instr->jt = 0;
			instr->jf = 0;
			continue;
		case (LABEL_JT<<8)|LABEL_JF:
			if (labels->labels[instr->k].location != 0xffffffff) {
				fprintf(stderr, "Duplicate label use: '%s'\n",
					labels->labels[instr->k].label);
				return 1;
			}
			labels->labels[instr->k].location = offset;
			instr->k = 0; /* fall through */
			instr->jt = 0;
			instr->jf = 0;
			continue;
		}
	}
	return 0;
}

/* Simple lookup table for labels. */
__u32 seccomp_bpf_label(struct bpf_labels *labels, const char *label)
{
	struct __bpf_label *begin = labels->labels, *end;
	int id;

	if (labels->count == BPF_LABELS_MAX) {
		fprintf(stderr, "Too many labels\n");
		exit(1);
	}
	if (labels->count == 0) {
		begin->label = label;
		begin->location = 0xffffffff;
		labels->count++;
		return 0;
	}
	end = begin + labels->count;
	for (id = 0; begin < end; ++begin, ++id) {
		if (!strcmp(label, begin->label))
			return id;
	}
	begin->label = label;
	begin->location = 0xffffffff;
	labels->count++;
	return id;
}

void seccomp_bpf_print(struct sock_filter *filter, size_t count)
{
	struct sock_filter *end = filter + count;
	for ( ; filter < end; ++filter)
		printf("{ code=%u,jt=%u,jf=%u,k=%u },\n",
			filter->code, filter->jt, filter->jf, filter->k);
}

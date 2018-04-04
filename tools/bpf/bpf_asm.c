/*
 * Minimal BPF assembler
 *
 * Instead of libpcap high-level filter expressions, it can be quite
 * useful to define filters in low-level BPF assembler (that is kept
 * close to Steven McCanne and Van Jacobson's original BPF paper).
 * In particular for BPF JIT implementors, JIT security auditors, or
 * just for defining BPF expressions that contain extensions which are
 * not supported by compilers.
 *
 * How to get into it:
 *
 * 1) read Documentation/networking/filter.txt
 * 2) Run `bpf_asm [-c] <filter-prog file>` to translate into binary
 *    blob that is loadable with xt_bpf, cls_bpf et al. Note: -c will
 *    pretty print a C-like construct.
 *
 * Copyright 2013 Daniel Borkmann <borkmann@redhat.com>
 * Licensed under the GNU General Public License, version 2.0 (GPLv2)
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

extern void bpf_asm_compile(FILE *fp, bool cstyle);

int main(int argc, char **argv)
{
	FILE *fp = stdin;
	bool cstyle = false;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strncmp("-c", argv[i], 2)) {
			cstyle = true;
			continue;
		}

		fp = fopen(argv[i], "r");
		if (!fp) {
			fp = stdin;
			continue;
		}

		break;
	}

	bpf_asm_compile(fp, cstyle);

	return 0;
}

/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#include "libbpf.h"
#include "bpf.h"
#include "btf.h"

/* do nothing, just make sure we can link successfully */

int main(int argc, char *argv[])
{
	/* libbpf.h */
	libbpf_set_print(NULL);

	/* bpf.h */
	bpf_prog_get_fd_by_id(0);

	/* btf.h */
	btf__new(NULL, 0);

	return 0;
}

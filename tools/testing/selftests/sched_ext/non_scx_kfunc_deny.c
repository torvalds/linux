/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Verify that context-sensitive SCX kfuncs (even "unlocked" ones) are
 * restricted to only SCX struct_ops programs. Non-SCX struct_ops programs,
 * such as TCP congestion control programs, should be rejected by the BPF
 * verifier when attempting to call these kfuncs.
 *
 * Copyright (C) 2026 Ching-Chun (Jim) Huang <jserv@ccns.ncku.edu.tw>
 * Copyright (C) 2026 Cheng-Yang Chou <yphbchou0911@gmail.com>
 */

#include <bpf/bpf.h>
#include <scx/common.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include "non_scx_kfunc_deny.bpf.skel.h"
#include "scx_test.h"

static enum scx_test_status run(void *ctx)
{
	struct non_scx_kfunc_deny *skel;
	int err;

	skel = non_scx_kfunc_deny__open();
	if (!skel) {
		SCX_ERR("Failed to open skel");
		return SCX_TEST_FAIL;
	}

	err = non_scx_kfunc_deny__load(skel);
	non_scx_kfunc_deny__destroy(skel);

	if (err == 0) {
		SCX_ERR("non-SCX BPF program loaded when it should have been rejected");
		return SCX_TEST_FAIL;
	}

	return SCX_TEST_PASS;
}

struct scx_test non_scx_kfunc_deny = {
	.name = "non_scx_kfunc_deny",
	.description = "Verify that non-SCX struct_ops programs cannot call SCX kfuncs",
	.run = run,
};
REGISTER_SCX_TEST(&non_scx_kfunc_deny)

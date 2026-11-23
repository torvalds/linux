// SPDX-License-Identifier: GPL-2.0
/*
 * Verify that the BPF verifier rejects SCX_KF_ALLOW_UNLOCKED kfuncs from
 * ops.dump() and ops.init_cids(). Unlocked ops such as ops.init() and
 * ops.exit() must still be allowed to call them.
 *
 * Copyright (c) 2026 Google LLC.
 */
#include <bpf/bpf.h>
#include <errno.h>
#include <scx/common.h>
#include "scx_test.h"
#include "unlocked_kfunc.bpf.skel.h"
#include "unlocked_kfunc_cid.bpf.skel.h"
#include "unlocked_kfunc_fail_dump.bpf.skel.h"
#include "unlocked_kfunc_fail_init_cids.bpf.skel.h"

static enum scx_test_status run(void *ctx)
{
	struct unlocked_kfunc *skel;
	struct unlocked_kfunc_cid *cid_skel;
	struct unlocked_kfunc_fail_dump *fail_dump;
	struct unlocked_kfunc_fail_init_cids *fail_init_cids;
	int err;

	if (!__COMPAT_struct_has_field("sched_ext_ops_cid", "init_cids") ||
	    !__COMPAT_has_ksym("scx_bpf_dsq_reenq_task"))
		return SCX_TEST_SKIP;

	skel = unlocked_kfunc__open();
	SCX_FAIL_IF(!skel, "Failed to open unlocked_kfunc");
	SCX_ENUM_INIT(skel);
	err = unlocked_kfunc__load(skel);
	unlocked_kfunc__destroy(skel);
	SCX_FAIL_IF(err, "Failed to load unlocked_kfunc (%d)", err);

	cid_skel = unlocked_kfunc_cid__open();
	SCX_FAIL_IF(!cid_skel, "Failed to open unlocked_kfunc_cid");
	SCX_ENUM_INIT(cid_skel);
	err = unlocked_kfunc_cid__load(cid_skel);
	unlocked_kfunc_cid__destroy(cid_skel);
	SCX_FAIL_IF(err, "Failed to load unlocked_kfunc_cid (%d)", err);

	fail_dump = unlocked_kfunc_fail_dump__open();
	SCX_FAIL_IF(!fail_dump, "Failed to open unlocked_kfunc_fail_dump");
	SCX_ENUM_INIT(fail_dump);
	err = unlocked_kfunc_fail_dump__load(fail_dump);
	unlocked_kfunc_fail_dump__destroy(fail_dump);
	SCX_FAIL_IF(!err,
		    "unlocked_kfunc_fail_dump loaded when it should have been rejected");
	SCX_FAIL_IF(err != -EACCES,
		    "Expected -EACCES loading unlocked_kfunc_fail_dump, got %d", err);

	fail_init_cids = unlocked_kfunc_fail_init_cids__open();
	SCX_FAIL_IF(!fail_init_cids, "Failed to open unlocked_kfunc_fail_init_cids");
	SCX_ENUM_INIT(fail_init_cids);
	err = unlocked_kfunc_fail_init_cids__load(fail_init_cids);
	unlocked_kfunc_fail_init_cids__destroy(fail_init_cids);
	SCX_FAIL_IF(!err,
		    "unlocked_kfunc_fail_init_cids loaded when it should have been rejected");
	SCX_FAIL_IF(err != -EACCES,
		    "Expected -EACCES loading unlocked_kfunc_fail_init_cids, got %d", err);

	return SCX_TEST_PASS;
}

struct scx_test unlocked_kfunc = {
	.name = "unlocked_kfunc",
	.description = "Verify verifier kfunc restrictions on ops.dump() and ops.init_cids()",
	.run = run,
};

REGISTER_SCX_TEST(&unlocked_kfunc)

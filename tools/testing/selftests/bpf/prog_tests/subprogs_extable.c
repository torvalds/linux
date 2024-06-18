// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "test_subprogs_extable.skel.h"

void test_subprogs_extable(void)
{
	const int read_sz = 456;
	struct test_subprogs_extable *skel;
	int err;

	skel = test_subprogs_extable__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = test_subprogs_extable__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	/* trigger tracepoint */
	ASSERT_OK(trigger_module_test_read(read_sz), "trigger_read");

	ASSERT_NEQ(skel->bss->triggered, 0, "verify at least one program ran");

	test_subprogs_extable__detach(skel);

cleanup:
	test_subprogs_extable__destroy(skel);
}

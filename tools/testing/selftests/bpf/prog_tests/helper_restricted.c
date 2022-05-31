// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "test_helper_restricted.skel.h"

void test_helper_restricted(void)
{
	int prog_i = 0, prog_cnt;
	int duration = 0;

	do {
		struct test_helper_restricted *test;
		int maybeOK;

		test = test_helper_restricted__open();
		if (!ASSERT_OK_PTR(test, "open"))
			return;

		prog_cnt = test->skeleton->prog_cnt;

		for (int j = 0; j < prog_cnt; ++j) {
			struct bpf_program *prog = *test->skeleton->progs[j].prog;

			maybeOK = bpf_program__set_autoload(prog, prog_i == j);
			ASSERT_OK(maybeOK, "set autoload");
		}

		maybeOK = test_helper_restricted__load(test);
		CHECK(!maybeOK, test->skeleton->progs[prog_i].name, "helper isn't restricted");

		test_helper_restricted__destroy(test);
	} while (++prog_i < prog_cnt);
}

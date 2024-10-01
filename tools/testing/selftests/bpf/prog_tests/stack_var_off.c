// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "test_stack_var_off.skel.h"

/* Test read and writes to the stack performed with offsets that are not
 * statically known.
 */
void test_stack_var_off(void)
{
	int duration = 0;
	struct test_stack_var_off *skel;

	skel = test_stack_var_off__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;

	/* Give pid to bpf prog so it doesn't trigger for anyone else. */
	skel->bss->test_pid = getpid();
	/* Initialize the probe's input. */
	skel->bss->input[0] = 2;
	skel->bss->input[1] = 42;  /* This will be returned in probe_res. */

	if (!ASSERT_OK(test_stack_var_off__attach(skel), "skel_attach"))
		goto cleanup;

	/* Trigger probe. */
	usleep(1);

	if (CHECK(skel->bss->probe_res != 42, "check_probe_res",
		  "wrong probe res: %d\n", skel->bss->probe_res))
		goto cleanup;

cleanup:
	test_stack_var_off__destroy(skel);
}

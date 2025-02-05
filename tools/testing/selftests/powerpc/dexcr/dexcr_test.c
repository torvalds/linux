// SPDX-License-Identifier: GPL-2.0-or-later

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "dexcr.h"
#include "utils.h"

/*
 * Helper function for testing the behaviour of a newly exec-ed process
 */
static int dexcr_prctl_onexec_test_child(unsigned long which, const char *status)
{
	unsigned long dexcr = mfspr(SPRN_DEXCR_RO);
	unsigned long aspect = pr_which_to_aspect(which);
	int ctrl = pr_get_dexcr(which);

	if (!strcmp(status, "set")) {
		FAIL_IF_EXIT_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_SET),
				 "setting aspect across exec not applied");

		FAIL_IF_EXIT_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_SET_ONEXEC),
				 "setting aspect across exec not inherited");

		FAIL_IF_EXIT_MSG(!(aspect & dexcr), "setting aspect across exec did not take effect");
	} else if (!strcmp(status, "clear")) {
		FAIL_IF_EXIT_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_CLEAR),
				 "clearing aspect across exec not applied");

		FAIL_IF_EXIT_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC),
				 "clearing aspect across exec not inherited");

		FAIL_IF_EXIT_MSG(aspect & dexcr, "clearing aspect across exec did not take effect");
	} else {
		FAIL_IF_EXIT_MSG(true, "unknown expected status");
	}

	return 0;
}

/*
 * Test that the given prctl value can be manipulated freely
 */
static int dexcr_prctl_aspect_test(unsigned long which)
{
	unsigned long aspect = pr_which_to_aspect(which);
	pid_t pid;
	int ctrl;
	int err;
	int errno_save;

	SKIP_IF_MSG(!dexcr_exists(), "DEXCR not supported");
	SKIP_IF_MSG(!pr_dexcr_aspect_supported(which), "DEXCR aspect not supported");
	SKIP_IF_MSG(!pr_dexcr_aspect_editable(which), "DEXCR aspect not editable with prctl");

	/* We reject invalid combinations of arguments */
	err = pr_set_dexcr(which, PR_PPC_DEXCR_CTRL_SET | PR_PPC_DEXCR_CTRL_CLEAR);
	errno_save = errno;
	FAIL_IF_MSG(err != -1, "simultaneous set and clear should be rejected");
	FAIL_IF_MSG(errno_save != EINVAL, "simultaneous set and clear should be rejected with EINVAL");

	err = pr_set_dexcr(which, PR_PPC_DEXCR_CTRL_SET_ONEXEC | PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC);
	errno_save = errno;
	FAIL_IF_MSG(err != -1, "simultaneous set and clear on exec should be rejected");
	FAIL_IF_MSG(errno_save != EINVAL, "simultaneous set and clear on exec should be rejected with EINVAL");

	/* We set the aspect */
	err = pr_set_dexcr(which, PR_PPC_DEXCR_CTRL_SET);
	FAIL_IF_MSG(err, "PR_PPC_DEXCR_CTRL_SET failed");

	ctrl = pr_get_dexcr(which);
	FAIL_IF_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_SET), "config value not PR_PPC_DEXCR_CTRL_SET");
	FAIL_IF_MSG(ctrl & PR_PPC_DEXCR_CTRL_CLEAR, "config value unexpected clear flag");
	FAIL_IF_MSG(!(aspect & mfspr(SPRN_DEXCR_RO)), "setting aspect did not take effect");

	/* We clear the aspect */
	err = pr_set_dexcr(which, PR_PPC_DEXCR_CTRL_CLEAR);
	FAIL_IF_MSG(err, "PR_PPC_DEXCR_CTRL_CLEAR failed");

	ctrl = pr_get_dexcr(which);
	FAIL_IF_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_CLEAR), "config value not PR_PPC_DEXCR_CTRL_CLEAR");
	FAIL_IF_MSG(ctrl & PR_PPC_DEXCR_CTRL_SET, "config value unexpected set flag");
	FAIL_IF_MSG(aspect & mfspr(SPRN_DEXCR_RO), "clearing aspect did not take effect");

	/* We make it set on exec (doesn't change our current value) */
	err = pr_set_dexcr(which, PR_PPC_DEXCR_CTRL_SET_ONEXEC);
	FAIL_IF_MSG(err, "PR_PPC_DEXCR_CTRL_SET_ONEXEC failed");

	ctrl = pr_get_dexcr(which);
	FAIL_IF_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_CLEAR), "process aspect should still be cleared");
	FAIL_IF_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_SET_ONEXEC), "config value not PR_PPC_DEXCR_CTRL_SET_ONEXEC");
	FAIL_IF_MSG(ctrl & PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC, "config value unexpected clear on exec flag");
	FAIL_IF_MSG(aspect & mfspr(SPRN_DEXCR_RO), "scheduling aspect to set on exec should not change it now");

	/* We make it clear on exec (doesn't change our current value) */
	err = pr_set_dexcr(which, PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC);
	FAIL_IF_MSG(err, "PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC failed");

	ctrl = pr_get_dexcr(which);
	FAIL_IF_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_CLEAR), "process aspect config should still be cleared");
	FAIL_IF_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC), "config value not PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC");
	FAIL_IF_MSG(ctrl & PR_PPC_DEXCR_CTRL_SET_ONEXEC, "config value unexpected set on exec flag");
	FAIL_IF_MSG(aspect & mfspr(SPRN_DEXCR_RO), "process aspect should still be cleared");

	/* We allow setting the current and on-exec value in a single call */
	err = pr_set_dexcr(which, PR_PPC_DEXCR_CTRL_SET | PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC);
	FAIL_IF_MSG(err, "PR_PPC_DEXCR_CTRL_SET | PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC failed");

	ctrl = pr_get_dexcr(which);
	FAIL_IF_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_SET), "config value not PR_PPC_DEXCR_CTRL_SET");
	FAIL_IF_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC), "config value not PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC");
	FAIL_IF_MSG(!(aspect & mfspr(SPRN_DEXCR_RO)), "process aspect should be set");

	err = pr_set_dexcr(which, PR_PPC_DEXCR_CTRL_CLEAR | PR_PPC_DEXCR_CTRL_SET_ONEXEC);
	FAIL_IF_MSG(err, "PR_PPC_DEXCR_CTRL_CLEAR | PR_PPC_DEXCR_CTRL_SET_ONEXEC failed");

	ctrl = pr_get_dexcr(which);
	FAIL_IF_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_CLEAR), "config value not PR_PPC_DEXCR_CTRL_CLEAR");
	FAIL_IF_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_SET_ONEXEC), "config value not PR_PPC_DEXCR_CTRL_SET_ONEXEC");
	FAIL_IF_MSG(aspect & mfspr(SPRN_DEXCR_RO), "process aspect should be clear");

	/* Verify the onexec value is applied across exec */
	pid = fork();
	if (!pid) {
		char which_str[32] = {};
		char *args[] = { "dexcr_prctl_onexec_test_child", which_str, "set", NULL };
		unsigned int ctrl = pr_get_dexcr(which);

		sprintf(which_str, "%lu", which);

		FAIL_IF_EXIT_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_SET_ONEXEC),
				 "setting aspect on exec not copied across fork");

		FAIL_IF_EXIT_MSG(mfspr(SPRN_DEXCR_RO) & aspect,
				 "setting aspect on exec wrongly applied to fork");

		execve("/proc/self/exe", args, NULL);
		_exit(errno);
	}
	await_child_success(pid);

	err = pr_set_dexcr(which, PR_PPC_DEXCR_CTRL_SET | PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC);
	FAIL_IF_MSG(err, "PR_PPC_DEXCR_CTRL_SET | PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC failed");

	pid = fork();
	if (!pid) {
		char which_str[32] = {};
		char *args[] = { "dexcr_prctl_onexec_test_child", which_str, "clear", NULL };
		unsigned int ctrl = pr_get_dexcr(which);

		sprintf(which_str, "%lu", which);

		FAIL_IF_EXIT_MSG(!(ctrl & PR_PPC_DEXCR_CTRL_CLEAR_ONEXEC),
				 "clearing aspect on exec not copied across fork");

		FAIL_IF_EXIT_MSG(!(mfspr(SPRN_DEXCR_RO) & aspect),
				 "clearing aspect on exec wrongly applied to fork");

		execve("/proc/self/exe", args, NULL);
		_exit(errno);
	}
	await_child_success(pid);

	return 0;
}

static int dexcr_prctl_ibrtpd_test(void)
{
	return dexcr_prctl_aspect_test(PR_PPC_DEXCR_IBRTPD);
}

static int dexcr_prctl_srapd_test(void)
{
	return dexcr_prctl_aspect_test(PR_PPC_DEXCR_SRAPD);
}

static int dexcr_prctl_nphie_test(void)
{
	return dexcr_prctl_aspect_test(PR_PPC_DEXCR_NPHIE);
}

int main(int argc, char *argv[])
{
	int err = 0;

	/*
	 * Some tests require checking what happens across exec, so we may be
	 * invoked as the child of a particular test
	 */
	if (argc > 1) {
		if (argc == 3 && !strcmp(argv[0], "dexcr_prctl_onexec_test_child")) {
			unsigned long which;

			err = parse_ulong(argv[1], strlen(argv[1]), &which, 10);
			FAIL_IF_MSG(err, "failed to parse which value for child");

			return dexcr_prctl_onexec_test_child(which, argv[2]);
		}

		FAIL_IF_MSG(true, "unknown test case");
	}

	/*
	 * Otherwise we are the main test invocation and run the full suite
	 */
	err |= test_harness(dexcr_prctl_ibrtpd_test, "dexcr_prctl_ibrtpd");
	err |= test_harness(dexcr_prctl_srapd_test, "dexcr_prctl_srapd");
	err |= test_harness(dexcr_prctl_nphie_test, "dexcr_prctl_nphie");

	return err;
}

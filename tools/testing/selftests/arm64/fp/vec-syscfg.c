// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 ARM Limited.
 * Original author: Mark Brown <broonie@kernel.org>
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <asm/sigcontext.h>
#include <asm/hwcap.h>

#include "../../kselftest.h"
#include "rdvl.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define ARCH_MIN_VL SVE_VL_MIN

struct vec_data {
	const char *name;
	unsigned long hwcap_type;
	unsigned long hwcap;
	const char *rdvl_binary;
	int (*rdvl)(void);

	int prctl_get;
	int prctl_set;
	const char *default_vl_file;

	int default_vl;
	int min_vl;
	int max_vl;
};


static struct vec_data vec_data[] = {
	{
		.name = "SVE",
		.hwcap_type = AT_HWCAP,
		.hwcap = HWCAP_SVE,
		.rdvl = rdvl_sve,
		.rdvl_binary = "./rdvl-sve",
		.prctl_get = PR_SVE_GET_VL,
		.prctl_set = PR_SVE_SET_VL,
		.default_vl_file = "/proc/sys/abi/sve_default_vector_length",
	},
};

static int stdio_read_integer(FILE *f, const char *what, int *val)
{
	int n = 0;
	int ret;

	ret = fscanf(f, "%d%*1[\n]%n", val, &n);
	if (ret < 1 || n < 1) {
		ksft_print_msg("failed to parse integer from %s\n", what);
		return -1;
	}

	return 0;
}

/* Start a new process and return the vector length it sees */
static int get_child_rdvl(struct vec_data *data)
{
	FILE *out;
	int pipefd[2];
	pid_t pid, child;
	int read_vl, ret;

	ret = pipe(pipefd);
	if (ret == -1) {
		ksft_print_msg("pipe() failed: %d (%s)\n",
			       errno, strerror(errno));
		return -1;
	}

	fflush(stdout);

	child = fork();
	if (child == -1) {
		ksft_print_msg("fork() failed: %d (%s)\n",
			       errno, strerror(errno));
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}

	/* Child: put vector length on the pipe */
	if (child == 0) {
		/*
		 * Replace stdout with the pipe, errors to stderr from
		 * here as kselftest prints to stdout.
		 */
		ret = dup2(pipefd[1], 1);
		if (ret == -1) {
			fprintf(stderr, "dup2() %d\n", errno);
			exit(EXIT_FAILURE);
		}

		/* exec() a new binary which puts the VL on stdout */
		ret = execl(data->rdvl_binary, data->rdvl_binary, NULL);
		fprintf(stderr, "execl(%s) failed: %d (%s)\n",
			data->rdvl_binary, errno, strerror(errno));

		exit(EXIT_FAILURE);
	}

	close(pipefd[1]);

	/* Parent; wait for the exit status from the child & verify it */
	do {
		pid = wait(&ret);
		if (pid == -1) {
			ksft_print_msg("wait() failed: %d (%s)\n",
				       errno, strerror(errno));
			close(pipefd[0]);
			return -1;
		}
	} while (pid != child);

	assert(pid == child);

	if (!WIFEXITED(ret)) {
		ksft_print_msg("child exited abnormally\n");
		close(pipefd[0]);
		return -1;
	}

	if (WEXITSTATUS(ret) != 0) {
		ksft_print_msg("child returned error %d\n",
			       WEXITSTATUS(ret));
		close(pipefd[0]);
		return -1;
	}

	out = fdopen(pipefd[0], "r");
	if (!out) {
		ksft_print_msg("failed to open child stdout\n");
		close(pipefd[0]);
		return -1;
	}

	ret = stdio_read_integer(out, "child", &read_vl);
	fclose(out);
	if (ret != 0)
		return ret;

	return read_vl;
}

static int file_read_integer(const char *name, int *val)
{
	FILE *f;
	int ret;

	f = fopen(name, "r");
	if (!f) {
		ksft_test_result_fail("Unable to open %s: %d (%s)\n",
				      name, errno,
				      strerror(errno));
		return -1;
	}

	ret = stdio_read_integer(f, name, val);
	fclose(f);

	return ret;
}

static int file_write_integer(const char *name, int val)
{
	FILE *f;

	f = fopen(name, "w");
	if (!f) {
		ksft_test_result_fail("Unable to open %s: %d (%s)\n",
				      name, errno,
				      strerror(errno));
		return -1;
	}

	fprintf(f, "%d", val);
	fclose(f);

	return 0;
}

/*
 * Verify that we can read the default VL via proc, checking that it
 * is set in a freshly spawned child.
 */
static void proc_read_default(struct vec_data *data)
{
	int default_vl, child_vl, ret;

	ret = file_read_integer(data->default_vl_file, &default_vl);
	if (ret != 0)
		return;

	/* Is this the actual default seen by new processes? */
	child_vl = get_child_rdvl(data);
	if (child_vl != default_vl) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      default_vl, child_vl);
		return;
	}

	ksft_test_result_pass("%s default vector length %d\n", data->name,
			      default_vl);
	data->default_vl = default_vl;
}

/* Verify that we can write a minimum value and have it take effect */
static void proc_write_min(struct vec_data *data)
{
	int ret, new_default, child_vl;

	if (geteuid() != 0) {
		ksft_test_result_skip("Need to be root to write to /proc\n");
		return;
	}

	ret = file_write_integer(data->default_vl_file, ARCH_MIN_VL);
	if (ret != 0)
		return;

	/* What was the new value? */
	ret = file_read_integer(data->default_vl_file, &new_default);
	if (ret != 0)
		return;

	/* Did it take effect in a new process? */
	child_vl = get_child_rdvl(data);
	if (child_vl != new_default) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      new_default, child_vl);
		return;
	}

	ksft_test_result_pass("%s minimum vector length %d\n", data->name,
			      new_default);
	data->min_vl = new_default;

	file_write_integer(data->default_vl_file, data->default_vl);
}

/* Verify that we can write a maximum value and have it take effect */
static void proc_write_max(struct vec_data *data)
{
	int ret, new_default, child_vl;

	if (geteuid() != 0) {
		ksft_test_result_skip("Need to be root to write to /proc\n");
		return;
	}

	/* -1 is accepted by the /proc interface as the maximum VL */
	ret = file_write_integer(data->default_vl_file, -1);
	if (ret != 0)
		return;

	/* What was the new value? */
	ret = file_read_integer(data->default_vl_file, &new_default);
	if (ret != 0)
		return;

	/* Did it take effect in a new process? */
	child_vl = get_child_rdvl(data);
	if (child_vl != new_default) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      new_default, child_vl);
		return;
	}

	ksft_test_result_pass("%s maximum vector length %d\n", data->name,
			      new_default);
	data->max_vl = new_default;

	file_write_integer(data->default_vl_file, data->default_vl);
}

/* Can we read back a VL from prctl? */
static void prctl_get(struct vec_data *data)
{
	int ret;

	ret = prctl(data->prctl_get);
	if (ret == -1) {
		ksft_test_result_fail("%s prctl() read failed: %d (%s)\n",
				      data->name, errno, strerror(errno));
		return;
	}

	/* Mask out any flags */
	ret &= PR_SVE_VL_LEN_MASK;

	/* Is that what we can read back directly? */
	if (ret == data->rdvl())
		ksft_test_result_pass("%s current VL is %d\n",
				      data->name, ret);
	else
		ksft_test_result_fail("%s prctl() VL %d but RDVL is %d\n",
				      data->name, ret, data->rdvl());
}

/* Does the prctl let us set the VL we already have? */
static void prctl_set_same(struct vec_data *data)
{
	int cur_vl = data->rdvl();
	int ret;

	ret = prctl(data->prctl_set, cur_vl);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed: %d (%s)\n",
				      data->name, errno, strerror(errno));
		return;
	}

	ksft_test_result(cur_vl == data->rdvl(),
			 "%s set VL %d and have VL %d\n",
			 data->name, cur_vl, data->rdvl());
}

/* Can we set a new VL for this process? */
static void prctl_set(struct vec_data *data)
{
	int ret;

	if (data->min_vl == data->max_vl) {
		ksft_test_result_skip("%s only one VL supported\n",
				      data->name);
		return;
	}

	/* Try to set the minimum VL */
	ret = prctl(data->prctl_set, data->min_vl);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->min_vl,
				      errno, strerror(errno));
		return;
	}

	if ((ret & PR_SVE_VL_LEN_MASK) != data->min_vl) {
		ksft_test_result_fail("%s prctl set %d but return value is %d\n",
				      data->name, data->min_vl, data->rdvl());
		return;
	}

	if (data->rdvl() != data->min_vl) {
		ksft_test_result_fail("%s set %d but RDVL is %d\n",
				      data->name, data->min_vl, data->rdvl());
		return;
	}

	/* Try to set the maximum VL */
	ret = prctl(data->prctl_set, data->max_vl);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->max_vl,
				      errno, strerror(errno));
		return;
	}

	if ((ret & PR_SVE_VL_LEN_MASK) != data->max_vl) {
		ksft_test_result_fail("%s prctl() set %d but return value is %d\n",
				      data->name, data->max_vl, data->rdvl());
		return;
	}

	/* The _INHERIT flag should not be present when we read the VL */
	ret = prctl(data->prctl_get);
	if (ret == -1) {
		ksft_test_result_fail("%s prctl() read failed: %d (%s)\n",
				      data->name, errno, strerror(errno));
		return;
	}

	if (ret & PR_SVE_VL_INHERIT) {
		ksft_test_result_fail("%s prctl() reports _INHERIT\n",
				      data->name);
		return;
	}

	ksft_test_result_pass("%s prctl() set min/max\n", data->name);
}

/* If we didn't request it a new VL shouldn't affect the child */
static void prctl_set_no_child(struct vec_data *data)
{
	int ret, child_vl;

	if (data->min_vl == data->max_vl) {
		ksft_test_result_skip("%s only one VL supported\n",
				      data->name);
		return;
	}

	ret = prctl(data->prctl_set, data->min_vl);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->min_vl,
				      errno, strerror(errno));
		return;
	}

	/* Ensure the default VL is different */
	ret = file_write_integer(data->default_vl_file, data->max_vl);
	if (ret != 0)
		return;

	/* Check that the child has the default we just set */
	child_vl = get_child_rdvl(data);
	if (child_vl != data->max_vl) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      data->max_vl, child_vl);
		return;
	}

	ksft_test_result_pass("%s vector length used default\n", data->name);

	file_write_integer(data->default_vl_file, data->default_vl);
}

/* If we didn't request it a new VL shouldn't affect the child */
static void prctl_set_for_child(struct vec_data *data)
{
	int ret, child_vl;

	if (data->min_vl == data->max_vl) {
		ksft_test_result_skip("%s only one VL supported\n",
				      data->name);
		return;
	}

	ret = prctl(data->prctl_set, data->min_vl | PR_SVE_VL_INHERIT);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->min_vl,
				      errno, strerror(errno));
		return;
	}

	/* The _INHERIT flag should be present when we read the VL */
	ret = prctl(data->prctl_get);
	if (ret == -1) {
		ksft_test_result_fail("%s prctl() read failed: %d (%s)\n",
				      data->name, errno, strerror(errno));
		return;
	}
	if (!(ret & PR_SVE_VL_INHERIT)) {
		ksft_test_result_fail("%s prctl() does not report _INHERIT\n",
				      data->name);
		return;
	}

	/* Ensure the default VL is different */
	ret = file_write_integer(data->default_vl_file, data->max_vl);
	if (ret != 0)
		return;

	/* Check that the child inherited our VL */
	child_vl = get_child_rdvl(data);
	if (child_vl != data->min_vl) {
		ksft_test_result_fail("%s is %d but child VL is %d\n",
				      data->default_vl_file,
				      data->min_vl, child_vl);
		return;
	}

	ksft_test_result_pass("%s vector length was inherited\n", data->name);

	file_write_integer(data->default_vl_file, data->default_vl);
}

/* _ONEXEC takes effect only in the child process */
static void prctl_set_onexec(struct vec_data *data)
{
	int ret, child_vl;

	if (data->min_vl == data->max_vl) {
		ksft_test_result_skip("%s only one VL supported\n",
				      data->name);
		return;
	}

	/* Set a known value for the default and our current VL */
	ret = file_write_integer(data->default_vl_file, data->max_vl);
	if (ret != 0)
		return;

	ret = prctl(data->prctl_set, data->max_vl);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->min_vl,
				      errno, strerror(errno));
		return;
	}

	/* Set a different value for the child to have on exec */
	ret = prctl(data->prctl_set, data->min_vl | PR_SVE_SET_VL_ONEXEC);
	if (ret < 0) {
		ksft_test_result_fail("%s prctl set failed for %d: %d (%s)\n",
				      data->name, data->min_vl,
				      errno, strerror(errno));
		return;
	}

	/* Our current VL should stay the same */
	if (data->rdvl() != data->max_vl) {
		ksft_test_result_fail("%s VL changed by _ONEXEC prctl()\n",
				      data->name);
		return;
	}

	/* Check that the child inherited our VL */
	child_vl = get_child_rdvl(data);
	if (child_vl != data->min_vl) {
		ksft_test_result_fail("Set %d _ONEXEC but child VL is %d\n",
				      data->min_vl, child_vl);
		return;
	}

	ksft_test_result_pass("%s vector length set on exec\n", data->name);

	file_write_integer(data->default_vl_file, data->default_vl);
}

/* For each VQ verify that setting via prctl() does the right thing */
static void prctl_set_all_vqs(struct vec_data *data)
{
	int ret, vq, vl, new_vl;
	int errors = 0;

	if (!data->min_vl || !data->max_vl) {
		ksft_test_result_skip("%s Failed to enumerate VLs, not testing VL setting\n",
				      data->name);
		return;
	}

	for (vq = SVE_VQ_MIN; vq <= SVE_VQ_MAX; vq++) {
		vl = sve_vl_from_vq(vq);

		/* Attempt to set the VL */
		ret = prctl(data->prctl_set, vl);
		if (ret < 0) {
			errors++;
			ksft_print_msg("%s prctl set failed for %d: %d (%s)\n",
				       data->name, vl,
				       errno, strerror(errno));
			continue;
		}

		new_vl = ret & PR_SVE_VL_LEN_MASK;

		/* Check that we actually have the reported new VL */
		if (data->rdvl() != new_vl) {
			ksft_print_msg("Set %s VL %d but RDVL reports %d\n",
				       data->name, new_vl, data->rdvl());
			errors++;
		}

		/* Was that the VL we asked for? */
		if (new_vl == vl)
			continue;

		/* Should round up to the minimum VL if below it */
		if (vl < data->min_vl) {
			if (new_vl != data->min_vl) {
				ksft_print_msg("%s VL %d returned %d not minimum %d\n",
					       data->name, vl, new_vl,
					       data->min_vl);
				errors++;
			}

			continue;
		}

		/* Should round down to maximum VL if above it */
		if (vl > data->max_vl) {
			if (new_vl != data->max_vl) {
				ksft_print_msg("%s VL %d returned %d not maximum %d\n",
					       data->name, vl, new_vl,
					       data->max_vl);
				errors++;
			}

			continue;
		}

		/* Otherwise we should've rounded down */
		if (!(new_vl < vl)) {
			ksft_print_msg("%s VL %d returned %d, did not round down\n",
				       data->name, vl, new_vl);
			errors++;

			continue;
		}
	}

	ksft_test_result(errors == 0, "%s prctl() set all VLs, %d errors\n",
			 data->name, errors);
}

typedef void (*test_type)(struct vec_data *);

static const test_type tests[] = {
	/*
	 * The default/min/max tests must be first and in this order
	 * to provide data for other tests.
	 */
	proc_read_default,
	proc_write_min,
	proc_write_max,

	prctl_get,
	prctl_set_same,
	prctl_set,
	prctl_set_no_child,
	prctl_set_for_child,
	prctl_set_onexec,
	prctl_set_all_vqs,
};

int main(void)
{
	int i, j;

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(tests) * ARRAY_SIZE(vec_data));

	for (i = 0; i < ARRAY_SIZE(vec_data); i++) {
		struct vec_data *data = &vec_data[i];
		unsigned long supported;

		supported = getauxval(data->hwcap_type) & data->hwcap;

		for (j = 0; j < ARRAY_SIZE(tests); j++) {
			if (supported)
				tests[j](data);
			else
				ksft_test_result_skip("%s not supported\n",
						      data->name);
		}
	}

	ksft_exit_pass();
}

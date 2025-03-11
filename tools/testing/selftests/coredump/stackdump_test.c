// SPDX-License-Identifier: GPL-2.0

#include <fcntl.h>
#include <libgen.h>
#include <linux/limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "../kselftest_harness.h"

#define STACKDUMP_FILE "stack_values"
#define STACKDUMP_SCRIPT "stackdump"
#define NUM_THREAD_SPAWN 128

static void *do_nothing(void *)
{
	while (1)
		pause();
}

static void crashing_child(void)
{
	pthread_t thread;
	int i;

	for (i = 0; i < NUM_THREAD_SPAWN; ++i)
		pthread_create(&thread, NULL, do_nothing, NULL);

	/* crash on purpose */
	i = *(int *)NULL;
}

FIXTURE(coredump)
{
	char original_core_pattern[256];
};

FIXTURE_SETUP(coredump)
{
	char buf[PATH_MAX];
	FILE *file;
	char *dir;
	int ret;

	file = fopen("/proc/sys/kernel/core_pattern", "r");
	ASSERT_NE(NULL, file);

	ret = fread(self->original_core_pattern, 1, sizeof(self->original_core_pattern), file);
	ASSERT_TRUE(ret || feof(file));
	ASSERT_LT(ret, sizeof(self->original_core_pattern));

	self->original_core_pattern[ret] = '\0';

	ret = fclose(file);
	ASSERT_EQ(0, ret);
}

FIXTURE_TEARDOWN(coredump)
{
	const char *reason;
	FILE *file;
	int ret;

	unlink(STACKDUMP_FILE);

	file = fopen("/proc/sys/kernel/core_pattern", "w");
	if (!file) {
		reason = "Unable to open core_pattern";
		goto fail;
	}

	ret = fprintf(file, "%s", self->original_core_pattern);
	if (ret < 0) {
		reason = "Unable to write to core_pattern";
		goto fail;
	}

	ret = fclose(file);
	if (ret) {
		reason = "Unable to close core_pattern";
		goto fail;
	}

	return;
fail:
	/* This should never happen */
	fprintf(stderr, "Failed to cleanup stackdump test: %s\n", reason);
}

TEST_F(coredump, stackdump)
{
	struct sigaction action = {};
	unsigned long long stack;
	char *test_dir, *line;
	size_t line_length;
	char buf[PATH_MAX];
	int ret, i;
	FILE *file;
	pid_t pid;

	/*
	 * Step 1: Setup core_pattern so that the stackdump script is executed when the child
	 * process crashes
	 */
	ret = readlink("/proc/self/exe", buf, sizeof(buf));
	ASSERT_NE(-1, ret);
	ASSERT_LT(ret, sizeof(buf));
	buf[ret] = '\0';

	test_dir = dirname(buf);

	file = fopen("/proc/sys/kernel/core_pattern", "w");
	ASSERT_NE(NULL, file);

	ret = fprintf(file, "|%1$s/%2$s %%P %1$s/%3$s", test_dir, STACKDUMP_SCRIPT, STACKDUMP_FILE);
	ASSERT_LT(0, ret);

	ret = fclose(file);
	ASSERT_EQ(0, ret);

	/* Step 2: Create a process who spawns some threads then crashes */
	pid = fork();
	ASSERT_TRUE(pid >= 0);
	if (pid == 0)
		crashing_child();

	/*
	 * Step 3: Wait for the stackdump script to write the stack pointers to the stackdump file
	 */
	for (i = 0; i < 10; ++i) {
		file = fopen(STACKDUMP_FILE, "r");
		if (file)
			break;
		sleep(1);
	}
	ASSERT_NE(file, NULL);

	/* Step 4: Make sure all stack pointer values are non-zero */
	for (i = 0; -1 != getline(&line, &line_length, file); ++i) {
		stack = strtoull(line, NULL, 10);
		ASSERT_NE(stack, 0);
	}

	ASSERT_EQ(i, 1 + NUM_THREAD_SPAWN);

	fclose(file);
}

TEST_HARNESS_MAIN

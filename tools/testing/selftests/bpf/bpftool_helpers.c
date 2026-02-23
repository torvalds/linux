// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include "bpf_util.h"
#include "bpftool_helpers.h"

#define BPFTOOL_PATH_MAX_LEN		64
#define BPFTOOL_FULL_CMD_MAX_LEN	512

#define BPFTOOL_DEFAULT_PATH		"tools/sbin/bpftool"

static int detect_bpftool_path(char *buffer, size_t size)
{
	char tmp[BPFTOOL_PATH_MAX_LEN];
	const char *env_path;

	/* First, check if BPFTOOL environment variable is set */
	env_path = getenv("BPFTOOL");
	if (env_path && access(env_path, X_OK) == 0) {
		strscpy(buffer, env_path, size);
		return 0;
	} else if (env_path) {
		fprintf(stderr, "bpftool '%s' doesn't exist or is not executable\n", env_path);
		return 1;
	}

	/* Check default bpftool location (will work if we are running the
	 * default flavor of test_progs)
	 */
	snprintf(tmp, BPFTOOL_PATH_MAX_LEN, "./%s", BPFTOOL_DEFAULT_PATH);
	if (access(tmp, X_OK) == 0) {
		strscpy(buffer, tmp, size);
		return 0;
	}

	/* Check alternate bpftool location (will work if we are running a
	 * specific flavor of test_progs, e.g. cpuv4 or no_alu32)
	 */
	snprintf(tmp, BPFTOOL_PATH_MAX_LEN, "../%s", BPFTOOL_DEFAULT_PATH);
	if (access(tmp, X_OK) == 0) {
		strscpy(buffer, tmp, size);
		return 0;
	}

	fprintf(stderr, "Failed to detect bpftool path, use BPFTOOL env var to override\n");
	return 1;
}

static int run_command(char *args, char *output_buf, size_t output_max_len)
{
	static char bpftool_path[BPFTOOL_PATH_MAX_LEN] = {0};
	bool suppress_output = !(output_buf && output_max_len);
	char command[BPFTOOL_FULL_CMD_MAX_LEN];
	FILE *f;
	int ret;

	/* Detect and cache bpftool binary location */
	if (bpftool_path[0] == 0 && detect_bpftool_path(bpftool_path, sizeof(bpftool_path)))
		return 1;

	ret = snprintf(command, BPFTOOL_FULL_CMD_MAX_LEN, "%s %s%s",
		       bpftool_path, args,
		       suppress_output ? " > /dev/null 2>&1" : "");

	f = popen(command, "r");
	if (!f)
		return 1;

	if (!suppress_output)
		fread(output_buf, 1, output_max_len, f);
	ret = pclose(f);

	return ret;
}

int run_bpftool_command(char *args)
{
	return run_command(args, NULL, 0);
}

int get_bpftool_command_output(char *args, char *output_buf, size_t output_max_len)
{
	return run_command(args, output_buf, output_max_len);
}


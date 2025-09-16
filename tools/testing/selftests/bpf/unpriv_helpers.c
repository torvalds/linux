// SPDX-License-Identifier: GPL-2.0-only

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>

#include "unpriv_helpers.h"

static gzFile open_config(void)
{
	struct utsname uts;
	char buf[PATH_MAX];
	gzFile config;

	if (uname(&uts)) {
		perror("uname");
		goto config_gz;
	}

	snprintf(buf, sizeof(buf), "/boot/config-%s", uts.release);
	config = gzopen(buf, "rb");
	if (config)
		return config;
	fprintf(stderr, "gzopen %s: %s\n", buf, strerror(errno));

config_gz:
	config = gzopen("/proc/config.gz", "rb");
	if (!config)
		perror("gzopen /proc/config.gz");
	return config;
}

static int config_contains(const char *pat)
{
	const char *msg;
	char buf[1024];
	gzFile config;
	int n, err;

	config = open_config();
	if (!config)
		return -1;

	for (;;) {
		if (!gzgets(config, buf, sizeof(buf))) {
			msg = gzerror(config, &err);
			if (err == Z_ERRNO)
				perror("gzgets /proc/config.gz");
			else if (err != Z_OK)
				fprintf(stderr, "gzgets /proc/config.gz: %s", msg);
			gzclose(config);
			return -1;
		}
		n = strlen(buf);
		if (buf[n - 1] == '\n')
			buf[n - 1] = 0;
		if (strcmp(buf, pat) == 0) {
			gzclose(config);
			return 1;
		}
	}
	gzclose(config);
	return 0;
}

static bool cmdline_contains(const char *pat)
{
	char cmdline[4096], *c;
	int fd, ret = false;

	fd = open("/proc/cmdline", O_RDONLY);
	if (fd < 0) {
		perror("open /proc/cmdline");
		return false;
	}

	if (read(fd, cmdline, sizeof(cmdline) - 1) < 0) {
		perror("read /proc/cmdline");
		goto out;
	}

	cmdline[sizeof(cmdline) - 1] = '\0';
	for (c = strtok(cmdline, " \n"); c; c = strtok(NULL, " \n")) {
		if (strncmp(c, pat, strlen(c)))
			continue;
		ret = true;
		break;
	}
out:
	close(fd);
	return ret;
}

static int get_mitigations_off(void)
{
	int enabled_in_config;

	if (cmdline_contains("mitigations=off"))
		return 1;
	enabled_in_config = config_contains("CONFIG_CPU_MITIGATIONS=y");
	if (enabled_in_config < 0)
		return -1;
	return !enabled_in_config;
}

bool get_unpriv_disabled(void)
{
	int mitigations_off;
	bool disabled;
	char buf[2];
	FILE *fd;

	fd = fopen("/proc/sys/" UNPRIV_SYSCTL, "r");
	if (fd) {
		disabled = (fgets(buf, 2, fd) == buf && atoi(buf));
		fclose(fd);
	} else {
		perror("fopen /proc/sys/" UNPRIV_SYSCTL);
		disabled = true;
	}

	if (disabled)
		return true;

	/*
	 * Some unpriv tests rely on spectre mitigations being on.
	 * If mitigations are off or status can't be determined
	 * assume that unpriv tests are disabled.
	 */
	mitigations_off = get_mitigations_off();
	if (mitigations_off < 0) {
		fprintf(stderr,
			"Can't determine if mitigations are enabled, disabling unpriv tests.");
		return true;
	}
	return mitigations_off;
}

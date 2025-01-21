// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of get_cpuid().
 *
 * Author: Nikita Shubin <n.shubin@yadro.com>
 *         Bibo Mao <maobibo@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 */

#include <stdio.h>
#include <stdlib.h>
#include <api/fs/fs.h>
#include <errno.h>
#include "util/debug.h"
#include "util/header.h"

/*
 * Output example from /proc/cpuinfo
 *   CPU Family              : Loongson-64bit
 *   Model Name              : Loongson-3C5000
 *   CPU Revision            : 0x10
 *   FPU Revision            : 0x01
 */
#define CPUINFO_MODEL	"Model Name"
#define CPUINFO		"/proc/cpuinfo"

static char *_get_field(const char *line)
{
	char *line2, *nl;

	line2 = strrchr(line, ' ');
	if (!line2)
		return NULL;

	line2++;
	nl = strrchr(line, '\n');
	if (!nl)
		return NULL;

	return strndup(line2, nl - line2);
}

static char *_get_cpuid(void)
{
	unsigned long line_sz;
	char *line, *model, *cpuid;
	FILE *file;

	file = fopen(CPUINFO, "r");
	if (file == NULL)
		return NULL;

	line = model = cpuid = NULL;
	while (getline(&line, &line_sz, file) != -1) {
		if (strncmp(line, CPUINFO_MODEL, strlen(CPUINFO_MODEL)))
			continue;

		model = _get_field(line);
		if (!model)
			goto out_free;
		break;
	}

	if (model && (asprintf(&cpuid, "%s", model) < 0))
		cpuid = NULL;

out_free:
	fclose(file);
	free(model);
	return cpuid;
}

int get_cpuid(char *buffer, size_t sz, struct perf_cpu cpu __maybe_unused)
{
	int ret = 0;
	char *cpuid = _get_cpuid();

	if (!cpuid)
		return EINVAL;

	if (sz < strlen(cpuid)) {
		ret = ENOBUFS;
		goto out_free;
	}

	scnprintf(buffer, sz, "%s", cpuid);

out_free:
	free(cpuid);
	return ret;
}

char *get_cpuid_str(struct perf_cpu cpu __maybe_unused)
{
	return _get_cpuid();
}

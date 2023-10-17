// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/stddef.h>
#include <linux/perf_event.h>
#include <linux/zalloc.h>
#include <api/fs/fs.h>
#include <errno.h>

#include "../../../util/intel-pt.h"
#include "../../../util/intel-bts.h"
#include "../../../util/pmu.h"
#include "../../../util/fncache.h"
#include "../../../util/pmus.h"
#include "env.h"

struct pmu_alias {
	char *name;
	char *alias;
	struct list_head list;
};

static LIST_HEAD(pmu_alias_name_list);
static bool cached_list;

struct perf_event_attr *perf_pmu__get_default_config(struct perf_pmu *pmu __maybe_unused)
{
#ifdef HAVE_AUXTRACE_SUPPORT
	if (!strcmp(pmu->name, INTEL_PT_PMU_NAME)) {
		pmu->auxtrace = true;
		return intel_pt_pmu_default_config(pmu);
	}
	if (!strcmp(pmu->name, INTEL_BTS_PMU_NAME)) {
		pmu->auxtrace = true;
		pmu->selectable = true;
	}
#endif
	return NULL;
}

static void pmu_alias__delete(struct pmu_alias *pmu_alias)
{
	if (!pmu_alias)
		return;

	zfree(&pmu_alias->name);
	zfree(&pmu_alias->alias);
	free(pmu_alias);
}

static struct pmu_alias *pmu_alias__new(char *name, char *alias)
{
	struct pmu_alias *pmu_alias = zalloc(sizeof(*pmu_alias));

	if (pmu_alias) {
		pmu_alias->name = strdup(name);
		if (!pmu_alias->name)
			goto out_delete;

		pmu_alias->alias = strdup(alias);
		if (!pmu_alias->alias)
			goto out_delete;
	}
	return pmu_alias;

out_delete:
	pmu_alias__delete(pmu_alias);
	return NULL;
}

static int setup_pmu_alias_list(void)
{
	int fd, dirfd;
	DIR *dir;
	struct dirent *dent;
	struct pmu_alias *pmu_alias;
	char buf[MAX_PMU_NAME_LEN];
	FILE *file;
	int ret = -ENOMEM;

	dirfd = perf_pmu__event_source_devices_fd();
	if (dirfd < 0)
		return -1;

	dir = fdopendir(dirfd);
	if (!dir)
		return -errno;

	while ((dent = readdir(dir))) {
		if (!strcmp(dent->d_name, ".") ||
		    !strcmp(dent->d_name, ".."))
			continue;

		fd = perf_pmu__pathname_fd(dirfd, dent->d_name, "alias", O_RDONLY);
		if (fd < 0)
			continue;

		file = fdopen(fd, "r");
		if (!file)
			continue;

		if (!fgets(buf, sizeof(buf), file)) {
			fclose(file);
			continue;
		}

		fclose(file);

		/* Remove the last '\n' */
		buf[strlen(buf) - 1] = 0;

		pmu_alias = pmu_alias__new(dent->d_name, buf);
		if (!pmu_alias)
			goto close_dir;

		list_add_tail(&pmu_alias->list, &pmu_alias_name_list);
	}

	ret = 0;

close_dir:
	closedir(dir);
	return ret;
}

static const char *__pmu_find_real_name(const char *name)
{
	struct pmu_alias *pmu_alias;

	list_for_each_entry(pmu_alias, &pmu_alias_name_list, list) {
		if (!strcmp(name, pmu_alias->alias))
			return pmu_alias->name;
	}

	return name;
}

const char *pmu_find_real_name(const char *name)
{
	if (cached_list)
		return __pmu_find_real_name(name);

	setup_pmu_alias_list();
	cached_list = true;

	return __pmu_find_real_name(name);
}

static const char *__pmu_find_alias_name(const char *name)
{
	struct pmu_alias *pmu_alias;

	list_for_each_entry(pmu_alias, &pmu_alias_name_list, list) {
		if (!strcmp(name, pmu_alias->name))
			return pmu_alias->alias;
	}
	return NULL;
}

const char *pmu_find_alias_name(const char *name)
{
	if (cached_list)
		return __pmu_find_alias_name(name);

	setup_pmu_alias_list();
	cached_list = true;

	return __pmu_find_alias_name(name);
}

int perf_pmus__num_mem_pmus(void)
{
	/* AMD uses IBS OP pmu and not a core PMU for perf mem/c2c */
	if (x86__is_amd_cpu())
		return 1;

	/* Intel uses core pmus for perf mem/c2c */
	return perf_pmus__num_core_pmus();
}

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

#define TEMPLATE_ALIAS	"%s/bus/event_source/devices/%s/alias"

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
	if (!strcmp(pmu->name, INTEL_PT_PMU_NAME))
		return intel_pt_pmu_default_config(pmu);
	if (!strcmp(pmu->name, INTEL_BTS_PMU_NAME))
		pmu->selectable = true;
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
	char path[PATH_MAX];
	DIR *dir;
	struct dirent *dent;
	const char *sysfs = sysfs__mountpoint();
	struct pmu_alias *pmu_alias;
	char buf[MAX_PMU_NAME_LEN];
	FILE *file;
	int ret = -ENOMEM;

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH, sysfs);

	dir = opendir(path);
	if (!dir)
		return -errno;

	while ((dent = readdir(dir))) {
		if (!strcmp(dent->d_name, ".") ||
		    !strcmp(dent->d_name, ".."))
			continue;

		snprintf(path, PATH_MAX,
			 TEMPLATE_ALIAS, sysfs, dent->d_name);

		if (!file_available(path))
			continue;

		file = fopen(path, "r");
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

static char *__pmu_find_real_name(const char *name)
{
	struct pmu_alias *pmu_alias;

	list_for_each_entry(pmu_alias, &pmu_alias_name_list, list) {
		if (!strcmp(name, pmu_alias->alias))
			return pmu_alias->name;
	}

	return (char *)name;
}

char *pmu_find_real_name(const char *name)
{
	if (cached_list)
		return __pmu_find_real_name(name);

	setup_pmu_alias_list();
	cached_list = true;

	return __pmu_find_real_name(name);
}

static char *__pmu_find_alias_name(const char *name)
{
	struct pmu_alias *pmu_alias;

	list_for_each_entry(pmu_alias, &pmu_alias_name_list, list) {
		if (!strcmp(name, pmu_alias->name))
			return pmu_alias->alias;
	}
	return NULL;
}

char *pmu_find_alias_name(const char *name)
{
	if (cached_list)
		return __pmu_find_alias_name(name);

	setup_pmu_alias_list();
	cached_list = true;

	return __pmu_find_alias_name(name);
}

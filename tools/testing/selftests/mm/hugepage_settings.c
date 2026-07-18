// SPDX-License-Identifier: GPL-2.0
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vm_util.h"
#include "hugepage_settings.h"

#define THP_SYSFS "/sys/kernel/mm/transparent_hugepage/"
#define MAX_SETTINGS_DEPTH 4
static struct thp_settings settings_stack[MAX_SETTINGS_DEPTH];
static int settings_index;
static struct thp_settings saved_settings;
static char dev_queue_read_ahead_path[PATH_MAX];
static bool thp_settings_saved;

static const char * const thp_enabled_strings[] = {
	"never",
	"always",
	"inherit",
	"madvise",
	NULL
};

static const char * const thp_defrag_strings[] = {
	"always",
	"defer",
	"defer+madvise",
	"madvise",
	"never",
	NULL
};

static const char * const shmem_enabled_strings[] = {
	"never",
	"always",
	"within_size",
	"advise",
	"inherit",
	"deny",
	"force",
	NULL
};

int thp_read_string(const char *name, const char * const strings[])
{
	char path[PATH_MAX];
	char buf[256];
	char *c;
	int ret;

	ret = snprintf(path, PATH_MAX, THP_SYSFS "%s", name);
	if (ret >= PATH_MAX) {
		printf("%s: Pathname is too long\n", __func__);
		exit(EXIT_FAILURE);
	}

	if (!read_file(path, buf, sizeof(buf))) {
		perror(path);
		exit(EXIT_FAILURE);
	}

	c = strchr(buf, '[');
	if (!c) {
		printf("%s: Parse failure\n", __func__);
		exit(EXIT_FAILURE);
	}

	c++;
	memmove(buf, c, sizeof(buf) - (c - buf));

	c = strchr(buf, ']');
	if (!c) {
		printf("%s: Parse failure\n", __func__);
		exit(EXIT_FAILURE);
	}
	*c = '\0';

	ret = 0;
	while (strings[ret]) {
		if (!strcmp(strings[ret], buf))
			return ret;
		ret++;
	}

	printf("Failed to parse %s\n", name);
	exit(EXIT_FAILURE);
}

void thp_write_string(const char *name, const char *val)
{
	char path[PATH_MAX];
	int ret;

	ret = snprintf(path, PATH_MAX, THP_SYSFS "%s", name);
	if (ret >= PATH_MAX) {
		printf("%s: Pathname is too long\n", __func__);
		exit(EXIT_FAILURE);
	}
	write_file(path, val, strlen(val) + 1);
}

unsigned long thp_read_num(const char *name)
{
	char path[PATH_MAX];
	int ret;

	ret = snprintf(path, PATH_MAX, THP_SYSFS "%s", name);
	if (ret >= PATH_MAX) {
		printf("%s: Pathname is too long\n", __func__);
		exit(EXIT_FAILURE);
	}
	return read_num(path);
}

void thp_write_num(const char *name, unsigned long num)
{
	char path[PATH_MAX];
	int ret;

	ret = snprintf(path, PATH_MAX, THP_SYSFS "%s", name);
	if (ret >= PATH_MAX) {
		printf("%s: Pathname is too long\n", __func__);
		exit(EXIT_FAILURE);
	}
	write_num(path, num);
}

void thp_read_settings(struct thp_settings *settings)
{
	unsigned long orders = thp_supported_orders();
	unsigned long shmem_orders = thp_shmem_supported_orders();
	char path[PATH_MAX];
	int i;

	*settings = (struct thp_settings) {
		.thp_enabled = thp_read_string("enabled", thp_enabled_strings),
		.thp_defrag = thp_read_string("defrag", thp_defrag_strings),
		.shmem_enabled =
			thp_read_string("shmem_enabled", shmem_enabled_strings),
		.use_zero_page = thp_read_num("use_zero_page"),
	};
	settings->khugepaged = (struct khugepaged_settings) {
		.defrag = thp_read_num("khugepaged/defrag"),
		.alloc_sleep_millisecs =
			thp_read_num("khugepaged/alloc_sleep_millisecs"),
		.scan_sleep_millisecs =
			thp_read_num("khugepaged/scan_sleep_millisecs"),
		.max_ptes_none = thp_read_num("khugepaged/max_ptes_none"),
		.max_ptes_swap = thp_read_num("khugepaged/max_ptes_swap"),
		.max_ptes_shared = thp_read_num("khugepaged/max_ptes_shared"),
		.pages_to_scan = thp_read_num("khugepaged/pages_to_scan"),
	};
	if (dev_queue_read_ahead_path[0])
		settings->read_ahead_kb = read_num(dev_queue_read_ahead_path);

	for (i = 0; i < NR_ORDERS; i++) {
		if (!((1 << i) & orders)) {
			settings->hugepages[i].enabled = THP_NEVER;
			continue;
		}
		snprintf(path, PATH_MAX, "hugepages-%ukB/enabled",
			(getpagesize() >> 10) << i);
		settings->hugepages[i].enabled =
			thp_read_string(path, thp_enabled_strings);
	}

	for (i = 0; i < NR_ORDERS; i++) {
		if (!((1 << i) & shmem_orders)) {
			settings->shmem_hugepages[i].enabled = SHMEM_NEVER;
			continue;
		}
		snprintf(path, PATH_MAX, "hugepages-%ukB/shmem_enabled",
			(getpagesize() >> 10) << i);
		settings->shmem_hugepages[i].enabled =
			thp_read_string(path, shmem_enabled_strings);
	}
}

void thp_write_settings(struct thp_settings *settings)
{
	struct khugepaged_settings *khugepaged = &settings->khugepaged;
	unsigned long orders = thp_supported_orders();
	unsigned long shmem_orders = thp_shmem_supported_orders();
	char path[PATH_MAX];
	int enabled;
	int i;

	thp_write_string("enabled", thp_enabled_strings[settings->thp_enabled]);
	thp_write_string("defrag", thp_defrag_strings[settings->thp_defrag]);
	thp_write_string("shmem_enabled",
			shmem_enabled_strings[settings->shmem_enabled]);
	thp_write_num("use_zero_page", settings->use_zero_page);

	thp_write_num("khugepaged/defrag", khugepaged->defrag);
	thp_write_num("khugepaged/alloc_sleep_millisecs",
			khugepaged->alloc_sleep_millisecs);
	thp_write_num("khugepaged/scan_sleep_millisecs",
			khugepaged->scan_sleep_millisecs);
	thp_write_num("khugepaged/max_ptes_none", khugepaged->max_ptes_none);
	thp_write_num("khugepaged/max_ptes_swap", khugepaged->max_ptes_swap);
	thp_write_num("khugepaged/max_ptes_shared", khugepaged->max_ptes_shared);
	thp_write_num("khugepaged/pages_to_scan", khugepaged->pages_to_scan);

	if (dev_queue_read_ahead_path[0])
		write_num(dev_queue_read_ahead_path, settings->read_ahead_kb);

	for (i = 0; i < NR_ORDERS; i++) {
		if (!((1 << i) & orders))
			continue;
		snprintf(path, PATH_MAX, "hugepages-%ukB/enabled",
			(getpagesize() >> 10) << i);
		enabled = settings->hugepages[i].enabled;
		thp_write_string(path, thp_enabled_strings[enabled]);
	}

	for (i = 0; i < NR_ORDERS; i++) {
		if (!((1 << i) & shmem_orders))
			continue;
		snprintf(path, PATH_MAX, "hugepages-%ukB/shmem_enabled",
			(getpagesize() >> 10) << i);
		enabled = settings->shmem_hugepages[i].enabled;
		thp_write_string(path, shmem_enabled_strings[enabled]);
	}
}

struct thp_settings *thp_current_settings(void)
{
	if (!settings_index) {
		printf("Fail: No settings set");
		exit(EXIT_FAILURE);
	}
	return settings_stack + settings_index - 1;
}

void thp_push_settings(struct thp_settings *settings)
{
	if (settings_index >= MAX_SETTINGS_DEPTH) {
		printf("Fail: Settings stack exceeded");
		exit(EXIT_FAILURE);
	}
	settings_stack[settings_index++] = *settings;
	thp_write_settings(thp_current_settings());
}

void thp_pop_settings(void)
{
	if (settings_index <= 0) {
		printf("Fail: Settings stack empty");
		exit(EXIT_FAILURE);
	}
	--settings_index;
	thp_write_settings(thp_current_settings());
}

void thp_restore_settings(void)
{
	if (thp_settings_saved)
		thp_write_settings(&saved_settings);
}

static void __thp_save_settings(void)
{
	if (!thp_available())
		return;

	if (thp_settings_saved)
		return;

	thp_read_settings(&saved_settings);
	thp_settings_saved = true;
}

void thp_set_read_ahead_path(char *path)
{
	if (!path) {
		dev_queue_read_ahead_path[0] = '\0';
		return;
	}

	strncpy(dev_queue_read_ahead_path, path,
		sizeof(dev_queue_read_ahead_path));
	dev_queue_read_ahead_path[sizeof(dev_queue_read_ahead_path) - 1] = '\0';
}

static unsigned long __thp_supported_orders(bool is_shmem)
{
	unsigned long orders = 0;
	char path[PATH_MAX];
	char buf[256];
	int ret, i;
	char anon_dir[] = "enabled";
	char shmem_dir[] = "shmem_enabled";

	for (i = 0; i < NR_ORDERS; i++) {
		ret = snprintf(path, PATH_MAX, THP_SYSFS "hugepages-%ukB/%s",
			       (getpagesize() >> 10) << i, is_shmem ? shmem_dir : anon_dir);
		if (ret >= PATH_MAX) {
			printf("%s: Pathname is too long\n", __func__);
			exit(EXIT_FAILURE);
		}

		ret = read_file(path, buf, sizeof(buf));
		if (ret)
			orders |= 1UL << i;
	}

	return orders;
}

unsigned long thp_supported_orders(void)
{
	return __thp_supported_orders(false);
}

unsigned long thp_shmem_supported_orders(void)
{
	return __thp_supported_orders(true);
}

bool thp_available(void)
{
	if (access(THP_SYSFS, F_OK) != 0)
		return false;
	return true;
}

bool thp_is_enabled(void)
{
	if (!thp_available())
		return false;

	int mode = thp_read_string("enabled", thp_enabled_strings);

	/* THP is considered enabled if it's either "always" or "madvise" */
	return mode == 1 || mode == 3;
}

#define HUGETLB_MAX_NR_PAGESIZES 10
struct hugetlb_settings {
	unsigned long nr_hugepages[HUGETLB_MAX_NR_PAGESIZES];
	unsigned long sizes[HUGETLB_MAX_NR_PAGESIZES];
	unsigned long default_size;
	int nr_sizes;
};

static struct hugetlb_settings hugetlb_saved_settings;
static bool hugetlb_settings_saved;

int detect_hugetlb_page_sizes(unsigned long sizes[], int max)
{
	static struct hugetlb_settings *settings = &hugetlb_saved_settings;
	DIR *dir;
	int count = 0;

	if (settings->nr_sizes) {
		if (settings->nr_sizes < max)
			max = settings->nr_sizes;
		for (count = 0; count < max; count++)
			sizes[count] = settings->sizes[count];
		return count;
	}

	dir = opendir("/sys/kernel/mm/hugepages/");
	if (!dir)
		return 0;

	while (count < max) {
		struct dirent *entry = readdir(dir);
		size_t kb;

		if (!entry)
			break;
		if (entry->d_type != DT_DIR)
			continue;
		if (sscanf(entry->d_name, "hugepages-%zukB", &kb) != 1)
			continue;
		sizes[count++] = kb * 1024;
		ksft_print_msg("[INFO] detected hugetlb page size: %zu KiB\n",
			       kb);
	}
	closedir(dir);
	return count;
}

unsigned long default_huge_page_size(void)
{
	static struct hugetlb_settings *settings = &hugetlb_saved_settings;
	unsigned long hps = 0;
	char *line = NULL;
	size_t linelen = 0;
	FILE *f;

	if (settings->default_size)
		return settings->default_size;

	f = fopen("/proc/meminfo", "r");
	if (!f)
		return 0;
	while (getline(&line, &linelen, f) > 0) {
		if (sscanf(line, "Hugepagesize:       %lu kB", &hps) == 1) {
			hps <<= 10;
			break;
		}
	}

	free(line);
	fclose(f);
	return hps;
}

static void hugetlb_sysfs_path(char *buf, size_t buflen,
			       unsigned long size, const char *attr)
{
	snprintf(buf, buflen, "/sys/kernel/mm/hugepages/hugepages-%lukB/%s",
		 size / 1024, attr);
}

unsigned long hugetlb_nr_pages(unsigned long size)
{
	char path[PATH_MAX];

	hugetlb_sysfs_path(path, sizeof(path), size, "nr_hugepages");

	return read_num(path);
}

void hugetlb_set_nr_pages(unsigned long size, unsigned long nr)
{
	char path[PATH_MAX];

	hugetlb_sysfs_path(path, sizeof(path), size, "nr_hugepages");

	write_num(path, nr);
}

unsigned long hugetlb_free_pages(unsigned long size)
{
	char path[PATH_MAX];

	hugetlb_sysfs_path(path, sizeof(path), size, "free_hugepages");

	return read_num(path);
}

static bool __hugetlb_setup(unsigned long size, unsigned long nr)
{
	unsigned long free = hugetlb_free_pages(size);
	unsigned long total = hugetlb_nr_pages(size);

	if (free >= nr)
		return true;

	hugetlb_set_nr_pages(size, total + (nr - free));

	return hugetlb_free_pages(size) >= nr;
}

bool hugetlb_setup_default(unsigned long nr)
{
	unsigned long size;

	hugetlb_save_settings();
	size = default_huge_page_size();
	if (!size)
		return false;

	return __hugetlb_setup(size, nr);
}

bool hugetlb_setup_default_exact(unsigned long nr)
{
	unsigned long size;

	hugetlb_save_settings();
	size = default_huge_page_size();
	if (!size)
		return false;

	hugetlb_set_nr_pages(size, nr);

	return hugetlb_free_pages(size) == nr;
}

unsigned long hugetlb_setup(unsigned long nr, unsigned long sizes[],
			    int max)
{
	unsigned long enabled[10];
	int nr_sizes = 0;
	int nr_enabled;

	hugetlb_save_settings();

	nr_enabled = detect_hugetlb_page_sizes(enabled, ARRAY_SIZE(enabled));
	if (!nr_enabled)
		return 0;

	if (nr_enabled > max) {
		ksft_print_msg("detected %d huge page sizes, will only test %d\n", nr_enabled, max);
		nr_enabled = max;
	}

	/* request nr HugeTLB pages of every size. */
	for (int i = 0; i < nr_enabled; i++) {
		if (!__hugetlb_setup(enabled[i], nr))
			continue;
		sizes[nr_sizes++] = enabled[i];
	}

	return nr_sizes;
}

static void __hugetlb_save_settings(void)
{
	struct hugetlb_settings *settings = &hugetlb_saved_settings;
	int nr_sizes;

	if (hugetlb_settings_saved)
		return;

	settings->default_size = default_huge_page_size();
	if (!settings->default_size)
		return;

	nr_sizes = detect_hugetlb_page_sizes(settings->sizes,
					     HUGETLB_MAX_NR_PAGESIZES);
	if (!nr_sizes) {
		settings->default_size = 0;
		return;
	}

	for (int i = 0; i < nr_sizes; i++) {
		unsigned long sz = settings->sizes[i];

		if (!sz)
			continue;
		settings->nr_hugepages[i] = hugetlb_nr_pages(sz);
	}

	settings->nr_sizes = nr_sizes;
	hugetlb_settings_saved = true;
}

void hugetlb_restore_settings(void)
{
	struct hugetlb_settings *settings = &hugetlb_saved_settings;

	if (!hugetlb_settings_saved || !settings->default_size)
		return;

	for (int i = 0; i < HUGETLB_MAX_NR_PAGESIZES; i++) {
		unsigned long sz = settings->sizes[i];

		if (!sz)
			continue;

		hugetlb_set_nr_pages(sz, settings->nr_hugepages[i]);
	}
}

static void hugepage_restore_settings_atexit(void)
{
	if (thp_settings_saved)
		thp_restore_settings();
	if (hugetlb_settings_saved)
		hugetlb_restore_settings();
}

static void hugepage_restore_settings_sighandler(int sig)
{
	/* exit() will invoke the hugepage_restore_settings_atexit handler. */
	exit(KSFT_FAIL);
}

void hugepage_save_settings(bool thp, bool hugetlb)
{
	if (!thp && !hugetlb)
		return;

	if (thp)
		__thp_save_settings();
	if (hugetlb)
		__hugetlb_save_settings();

	/*
	 * setup exit hooks to make sure THP and HugeTLB settings are
	 * restored on graceful and error exits and signals
	 */
	atexit(hugepage_restore_settings_atexit);
	signal(SIGTERM, hugepage_restore_settings_sighandler);
	signal(SIGINT, hugepage_restore_settings_sighandler);
	signal(SIGHUP, hugepage_restore_settings_sighandler);
	signal(SIGQUIT, hugepage_restore_settings_sighandler);
}

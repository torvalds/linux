// SPDX-License-Identifier: GPL-2.0
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "thp_settings.h"

#define THP_SYSFS "/sys/kernel/mm/transparent_hugepage/"
#define MAX_SETTINGS_DEPTH 4
static struct thp_settings settings_stack[MAX_SETTINGS_DEPTH];
static int settings_index;
static struct thp_settings saved_settings;
static char dev_queue_read_ahead_path[PATH_MAX];

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

int read_file(const char *path, char *buf, size_t buflen)
{
	int fd;
	ssize_t numread;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return 0;

	numread = read(fd, buf, buflen - 1);
	if (numread < 1) {
		close(fd);
		return 0;
	}

	buf[numread] = '\0';
	close(fd);

	return (unsigned int) numread;
}

int write_file(const char *path, const char *buf, size_t buflen)
{
	int fd;
	ssize_t numwritten;

	fd = open(path, O_WRONLY);
	if (fd == -1) {
		printf("open(%s)\n", path);
		exit(EXIT_FAILURE);
		return 0;
	}

	numwritten = write(fd, buf, buflen - 1);
	close(fd);
	if (numwritten < 1) {
		printf("write(%s)\n", buf);
		exit(EXIT_FAILURE);
		return 0;
	}

	return (unsigned int) numwritten;
}

unsigned long read_num(const char *path)
{
	char buf[21];

	if (read_file(path, buf, sizeof(buf)) < 0) {
		perror("read_file()");
		exit(EXIT_FAILURE);
	}

	return strtoul(buf, NULL, 10);
}

void write_num(const char *path, unsigned long num)
{
	char buf[21];

	sprintf(buf, "%ld", num);
	if (!write_file(path, buf, strlen(buf) + 1)) {
		perror(path);
		exit(EXIT_FAILURE);
	}
}

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

	if (!write_file(path, val, strlen(val) + 1)) {
		perror(path);
		exit(EXIT_FAILURE);
	}
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
	thp_write_settings(&saved_settings);
}

void thp_save_settings(void)
{
	thp_read_settings(&saved_settings);
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

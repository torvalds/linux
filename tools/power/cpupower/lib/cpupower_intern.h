/* SPDX-License-Identifier: GPL-2.0 */
#define PATH_TO_CPU "/sys/devices/system/cpu/"

#ifndef MAX_LINE_LEN
#define MAX_LINE_LEN 4096
#endif

#define SYSFS_PATH_MAX 255

int is_valid_path(const char *path);
unsigned int cpupower_read_sysfs(const char *path, char *buf, size_t buflen);
unsigned int cpupower_write_sysfs(const char *path, char *buf, size_t buflen);

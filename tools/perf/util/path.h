/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_PATH_H
#define _PERF_PATH_H

#include <stdbool.h>
#include <stddef.h>

#include <linux/compiler.h>

struct dirent;

char *mkpath(char *path_buf, size_t sz, const char *fmt, ...) __printf(3, 4);

int path__join(char *bf, size_t size, const char *path1, const char *path2);
int path__join3(char *bf, size_t size, const char *path1, const char *path2, const char *path3);

bool is_regular_file(const char *file);
bool is_directory(const char *base_path, const struct dirent *dent);
bool is_directory_at(int dir_fd, const char *path);

#endif /* _PERF_PATH_H */

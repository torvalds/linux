/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_PATH_H
#define _PERF_PATH_H

int path__join(char *bf, size_t size, const char *path1, const char *path2);
int path__join3(char *bf, size_t size, const char *path1, const char *path2, const char *path3);

bool is_regular_file(const char *file);

#endif /* _PERF_PATH_H */

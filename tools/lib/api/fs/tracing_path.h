/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __API_FS_TRACING_PATH_H
#define __API_FS_TRACING_PATH_H

#include <linux/types.h>
#include <dirent.h>

DIR *tracing_events__opendir(void);
int tracing_events__scandir_alphasort(struct dirent ***namelist);

void tracing_path_set(const char *mountpoint);
const char *tracing_path_mount(void);

char *get_tracing_file(const char *name);
void put_tracing_file(char *file);

char *get_events_file(const char *name);
void put_events_file(char *file);

#define zput_events_file(ptr) ({ free(*ptr); *ptr = NULL; })

int tracing_path__strerror_open_tp(int err, char *buf, size_t size, const char *sys, const char *name);
#endif /* __API_FS_TRACING_PATH_H */

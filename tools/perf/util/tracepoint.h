/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_TRACEPOINT_H
#define __PERF_TRACEPOINT_H

#include <dirent.h>
#include <string.h>
#include <stdbool.h>

int tp_event_has_id(const char *dir_path, struct dirent *evt_dir);

#define for_each_event(dir_path, evt_dir, evt_dirent)		\
	while ((evt_dirent = readdir(evt_dir)) != NULL)		\
		if (evt_dirent->d_type == DT_DIR &&		\
		    (strcmp(evt_dirent->d_name, ".")) &&	\
		    (strcmp(evt_dirent->d_name, "..")) &&	\
		    (!tp_event_has_id(dir_path, evt_dirent)))

#define for_each_subsystem(sys_dir, sys_dirent)			\
	while ((sys_dirent = readdir(sys_dir)) != NULL)		\
		if (sys_dirent->d_type == DT_DIR &&		\
		    (strcmp(sys_dirent->d_name, ".")) &&	\
		    (strcmp(sys_dirent->d_name, "..")))

bool is_valid_tracepoint(const char *event_string);

#endif /* __PERF_TRACEPOINT_H */

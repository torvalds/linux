/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CGROUP_HELPERS_H
#define __CGROUP_HELPERS_H

#include <errno.h>
#include <string.h>

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_err(MSG, ...) fprintf(stderr, "(%s:%d: errno: %s) " MSG "\n", \
	__FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)

/* cgroupv2 related */
int enable_controllers(const char *relative_path, const char *controllers);
int write_cgroup_file(const char *relative_path, const char *file,
		      const char *buf);
int write_cgroup_file_parent(const char *relative_path, const char *file,
			     const char *buf);
int cgroup_setup_and_join(const char *relative_path);
int get_root_cgroup(void);
int create_and_get_cgroup(const char *relative_path);
void remove_cgroup(const char *relative_path);
void remove_cgroup_pid(const char *relative_path, int pid);
unsigned long long get_cgroup_id(const char *relative_path);
int get_cgroup1_hierarchy_id(const char *subsys_name);

int join_cgroup(const char *relative_path);
int join_root_cgroup(void);
int join_parent_cgroup(const char *relative_path);

int set_cgroup_xattr(const char *relative_path,
		     const char *name,
		     const char *value);

int setup_cgroup_environment(void);
void cleanup_cgroup_environment(void);

/* cgroupv1 related */
int set_classid(void);
int join_classid(void);
unsigned long long get_classid_cgroup_id(void);
int open_classid(void);

int setup_classid_environment(void);
void cleanup_classid_environment(void);

#endif /* __CGROUP_HELPERS_H */

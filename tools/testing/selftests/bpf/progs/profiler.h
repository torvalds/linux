// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#pragma once

#define TASK_COMM_LEN 16
#define MAX_ANCESTORS 4
#define MAX_PATH 256
#define KILL_TARGET_LEN 64
#define CTL_MAXNAME 10
#define MAX_ARGS_LEN 4096
#define MAX_FILENAME_LEN 512
#define MAX_ENVIRON_LEN 8192
#define MAX_PATH_DEPTH 32
#define MAX_FILEPATH_LENGTH (MAX_PATH_DEPTH * MAX_PATH)
#define MAX_CGROUPS_PATH_DEPTH 8

#define MAX_METADATA_PAYLOAD_LEN TASK_COMM_LEN

#define MAX_CGROUP_PAYLOAD_LEN \
	(MAX_PATH * 2 + (MAX_PATH * MAX_CGROUPS_PATH_DEPTH))

#define MAX_CAP_PAYLOAD_LEN (MAX_METADATA_PAYLOAD_LEN + MAX_CGROUP_PAYLOAD_LEN)

#define MAX_SYSCTL_PAYLOAD_LEN \
	(MAX_METADATA_PAYLOAD_LEN + MAX_CGROUP_PAYLOAD_LEN + CTL_MAXNAME + MAX_PATH)

#define MAX_KILL_PAYLOAD_LEN \
	(MAX_METADATA_PAYLOAD_LEN + MAX_CGROUP_PAYLOAD_LEN + TASK_COMM_LEN + \
	 KILL_TARGET_LEN)

#define MAX_EXEC_PAYLOAD_LEN \
	(MAX_METADATA_PAYLOAD_LEN + MAX_CGROUP_PAYLOAD_LEN + MAX_FILENAME_LEN + \
	 MAX_ARGS_LEN + MAX_ENVIRON_LEN)

#define MAX_FILEMOD_PAYLOAD_LEN \
	(MAX_METADATA_PAYLOAD_LEN + MAX_CGROUP_PAYLOAD_LEN + MAX_FILEPATH_LENGTH + \
	 MAX_FILEPATH_LENGTH)

enum data_type {
	INVALID_EVENT,
	EXEC_EVENT,
	FORK_EVENT,
	KILL_EVENT,
	SYSCTL_EVENT,
	FILEMOD_EVENT,
	MAX_DATA_TYPE_EVENT
};

enum filemod_type {
	FMOD_OPEN,
	FMOD_LINK,
	FMOD_SYMLINK,
};

struct ancestors_data_t {
	pid_t ancestor_pids[MAX_ANCESTORS];
	uint32_t ancestor_exec_ids[MAX_ANCESTORS];
	uint64_t ancestor_start_times[MAX_ANCESTORS];
	uint32_t num_ancestors;
};

struct var_metadata_t {
	enum data_type type;
	pid_t pid;
	uint32_t exec_id;
	uid_t uid;
	gid_t gid;
	uint64_t start_time;
	uint32_t cpu_id;
	uint64_t bpf_stats_num_perf_events;
	uint64_t bpf_stats_start_ktime_ns;
	uint8_t comm_length;
};

struct cgroup_data_t {
	ino_t cgroup_root_inode;
	ino_t cgroup_proc_inode;
	uint64_t cgroup_root_mtime;
	uint64_t cgroup_proc_mtime;
	uint16_t cgroup_root_length;
	uint16_t cgroup_proc_length;
	uint16_t cgroup_full_length;
	int cgroup_full_path_root_pos;
};

struct var_sysctl_data_t {
	struct var_metadata_t meta;
	struct cgroup_data_t cgroup_data;
	struct ancestors_data_t ancestors_info;
	uint8_t sysctl_val_length;
	uint16_t sysctl_path_length;
	char payload[MAX_SYSCTL_PAYLOAD_LEN];
};

struct var_kill_data_t {
	struct var_metadata_t meta;
	struct cgroup_data_t cgroup_data;
	struct ancestors_data_t ancestors_info;
	pid_t kill_target_pid;
	int kill_sig;
	uint32_t kill_count;
	uint64_t last_kill_time;
	uint8_t kill_target_name_length;
	uint8_t kill_target_cgroup_proc_length;
	char payload[MAX_KILL_PAYLOAD_LEN];
	size_t payload_length;
};

struct var_exec_data_t {
	struct var_metadata_t meta;
	struct cgroup_data_t cgroup_data;
	pid_t parent_pid;
	uint32_t parent_exec_id;
	uid_t parent_uid;
	uint64_t parent_start_time;
	uint16_t bin_path_length;
	uint16_t cmdline_length;
	uint16_t environment_length;
	char payload[MAX_EXEC_PAYLOAD_LEN];
};

struct var_fork_data_t {
	struct var_metadata_t meta;
	pid_t parent_pid;
	uint32_t parent_exec_id;
	uint64_t parent_start_time;
	char payload[MAX_METADATA_PAYLOAD_LEN];
};

struct var_filemod_data_t {
	struct var_metadata_t meta;
	struct cgroup_data_t cgroup_data;
	enum filemod_type fmod_type;
	unsigned int dst_flags;
	uint32_t src_device_id;
	uint32_t dst_device_id;
	ino_t src_inode;
	ino_t dst_inode;
	uint16_t src_filepath_length;
	uint16_t dst_filepath_length;
	char payload[MAX_FILEMOD_PAYLOAD_LEN];
};

struct profiler_config_struct {
	bool fetch_cgroups_from_bpf;
	ino_t cgroup_fs_inode;
	ino_t cgroup_login_session_inode;
	uint64_t kill_signals_mask;
	ino_t inode_filter;
	uint32_t stale_info_secs;
	bool use_variable_buffers;
	bool read_environ_from_exec;
	bool enable_cgroup_v1_resolver;
};

struct bpf_func_stats_data {
	uint64_t time_elapsed_ns;
	uint64_t num_executions;
	uint64_t num_perf_events;
};

struct bpf_func_stats_ctx {
	uint64_t start_time_ns;
	struct bpf_func_stats_data* bpf_func_stats_data_val;
};

enum bpf_function_id {
	profiler_bpf_proc_sys_write,
	profiler_bpf_sched_process_exec,
	profiler_bpf_sched_process_exit,
	profiler_bpf_sys_enter_kill,
	profiler_bpf_do_filp_open_ret,
	profiler_bpf_sched_process_fork,
	profiler_bpf_vfs_link,
	profiler_bpf_vfs_symlink,
	profiler_bpf_max_function_id
};

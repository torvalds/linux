// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "profiler.h"

#ifndef NULL
#define NULL 0
#endif

#define O_WRONLY 00000001
#define O_RDWR 00000002
#define O_DIRECTORY 00200000
#define __O_TMPFILE 020000000
#define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
#define MAX_ERRNO 4095
#define S_IFMT 00170000
#define S_IFSOCK 0140000
#define S_IFLNK 0120000
#define S_IFREG 0100000
#define S_IFBLK 0060000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000
#define S_ISLNK(m) (((m)&S_IFMT) == S_IFLNK)
#define S_ISDIR(m) (((m)&S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m)&S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m)&S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m)&S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m)&S_IFMT) == S_IFSOCK)
#define IS_ERR_VALUE(x) (unsigned long)(void*)(x) >= (unsigned long)-MAX_ERRNO

#define KILL_DATA_ARRAY_SIZE 8

struct var_kill_data_arr_t {
	struct var_kill_data_t array[KILL_DATA_ARRAY_SIZE];
};

union any_profiler_data_t {
	struct var_exec_data_t var_exec;
	struct var_kill_data_t var_kill;
	struct var_sysctl_data_t var_sysctl;
	struct var_filemod_data_t var_filemod;
	struct var_fork_data_t var_fork;
	struct var_kill_data_arr_t var_kill_data_arr;
};

volatile struct profiler_config_struct bpf_config = {};

#define FETCH_CGROUPS_FROM_BPF (bpf_config.fetch_cgroups_from_bpf)
#define CGROUP_FS_INODE (bpf_config.cgroup_fs_inode)
#define CGROUP_LOGIN_SESSION_INODE \
	(bpf_config.cgroup_login_session_inode)
#define KILL_SIGNALS (bpf_config.kill_signals_mask)
#define STALE_INFO (bpf_config.stale_info_secs)
#define INODE_FILTER (bpf_config.inode_filter)
#define READ_ENVIRON_FROM_EXEC (bpf_config.read_environ_from_exec)
#define ENABLE_CGROUP_V1_RESOLVER (bpf_config.enable_cgroup_v1_resolver)

struct kernfs_iattrs___52 {
	struct iattr ia_iattr;
};

struct kernfs_node___52 {
	union /* kernfs_node_id */ {
		struct {
			u32 ino;
			u32 generation;
		};
		u64 id;
	} id;
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, u32);
	__type(value, union any_profiler_data_t);
} data_heap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} events SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, KILL_DATA_ARRAY_SIZE);
	__type(key, u32);
	__type(value, struct var_kill_data_arr_t);
} var_tpid_to_data SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, profiler_bpf_max_function_id);
	__type(key, u32);
	__type(value, struct bpf_func_stats_data);
} bpf_func_stats SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u32);
	__type(value, bool);
	__uint(max_entries, 16);
} allowed_devices SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u64);
	__type(value, bool);
	__uint(max_entries, 1024);
} allowed_file_inodes SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u64);
	__type(value, bool);
	__uint(max_entries, 1024);
} allowed_directory_inodes SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, u32);
	__type(value, bool);
	__uint(max_entries, 16);
} disallowed_exec_inodes SEC(".maps");

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

static INLINE bool IS_ERR(const void* ptr)
{
	return IS_ERR_VALUE((unsigned long)ptr);
}

static INLINE u32 get_userspace_pid()
{
	return bpf_get_current_pid_tgid() >> 32;
}

static INLINE bool is_init_process(u32 tgid)
{
	return tgid == 1 || tgid == 0;
}

static INLINE unsigned long
probe_read_lim(void* dst, void* src, unsigned long len, unsigned long max)
{
	len = len < max ? len : max;
	if (len > 1) {
		if (bpf_probe_read(dst, len, src))
			return 0;
	} else if (len == 1) {
		if (bpf_probe_read(dst, 1, src))
			return 0;
	}
	return len;
}

static INLINE int get_var_spid_index(struct var_kill_data_arr_t* arr_struct,
				     int spid)
{
#ifdef UNROLL
#pragma unroll
#endif
	for (int i = 0; i < ARRAY_SIZE(arr_struct->array); i++)
		if (arr_struct->array[i].meta.pid == spid)
			return i;
	return -1;
}

static INLINE void populate_ancestors(struct task_struct* task,
				      struct ancestors_data_t* ancestors_data)
{
	struct task_struct* parent = task;
	u32 num_ancestors, ppid;

	ancestors_data->num_ancestors = 0;
#ifdef UNROLL
#pragma unroll
#endif
	for (num_ancestors = 0; num_ancestors < MAX_ANCESTORS; num_ancestors++) {
		parent = BPF_CORE_READ(parent, real_parent);
		if (parent == NULL)
			break;
		ppid = BPF_CORE_READ(parent, tgid);
		if (is_init_process(ppid))
			break;
		ancestors_data->ancestor_pids[num_ancestors] = ppid;
		ancestors_data->ancestor_exec_ids[num_ancestors] =
			BPF_CORE_READ(parent, self_exec_id);
		ancestors_data->ancestor_start_times[num_ancestors] =
			BPF_CORE_READ(parent, start_time);
		ancestors_data->num_ancestors = num_ancestors;
	}
}

static INLINE void* read_full_cgroup_path(struct kernfs_node* cgroup_node,
					  struct kernfs_node* cgroup_root_node,
					  void* payload,
					  int* root_pos)
{
	void* payload_start = payload;
	size_t filepart_length;

#ifdef UNROLL
#pragma unroll
#endif
	for (int i = 0; i < MAX_CGROUPS_PATH_DEPTH; i++) {
		filepart_length =
			bpf_probe_read_str(payload, MAX_PATH, BPF_CORE_READ(cgroup_node, name));
		if (!cgroup_node)
			return payload;
		if (cgroup_node == cgroup_root_node)
			*root_pos = payload - payload_start;
		if (filepart_length <= MAX_PATH) {
			barrier_var(filepart_length);
			payload += filepart_length;
		}
		cgroup_node = BPF_CORE_READ(cgroup_node, parent);
	}
	return payload;
}

static ino_t get_inode_from_kernfs(struct kernfs_node* node)
{
	struct kernfs_node___52* node52 = (void*)node;

	if (bpf_core_field_exists(node52->id.ino)) {
		barrier_var(node52);
		return BPF_CORE_READ(node52, id.ino);
	} else {
		barrier_var(node);
		return (u64)BPF_CORE_READ(node, id);
	}
}

int pids_cgrp_id = 1;

static INLINE void* populate_cgroup_info(struct cgroup_data_t* cgroup_data,
					 struct task_struct* task,
					 void* payload)
{
	struct kernfs_node* root_kernfs =
		BPF_CORE_READ(task, nsproxy, cgroup_ns, root_cset, dfl_cgrp, kn);
	struct kernfs_node* proc_kernfs = BPF_CORE_READ(task, cgroups, dfl_cgrp, kn);

	if (ENABLE_CGROUP_V1_RESOLVER) {
#ifdef UNROLL
#pragma unroll
#endif
		for (int i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
			struct cgroup_subsys_state* subsys =
				BPF_CORE_READ(task, cgroups, subsys[i]);
			if (subsys != NULL) {
				int subsys_id = BPF_CORE_READ(subsys, ss, id);
				if (subsys_id == pids_cgrp_id) {
					proc_kernfs = BPF_CORE_READ(subsys, cgroup, kn);
					root_kernfs = BPF_CORE_READ(subsys, ss, root, kf_root, kn);
					break;
				}
			}
		}
	}

	cgroup_data->cgroup_root_inode = get_inode_from_kernfs(root_kernfs);
	cgroup_data->cgroup_proc_inode = get_inode_from_kernfs(proc_kernfs);

	if (bpf_core_field_exists(root_kernfs->iattr->ia_mtime)) {
		cgroup_data->cgroup_root_mtime =
			BPF_CORE_READ(root_kernfs, iattr, ia_mtime.tv_nsec);
		cgroup_data->cgroup_proc_mtime =
			BPF_CORE_READ(proc_kernfs, iattr, ia_mtime.tv_nsec);
	} else {
		struct kernfs_iattrs___52* root_iattr =
			(struct kernfs_iattrs___52*)BPF_CORE_READ(root_kernfs, iattr);
		cgroup_data->cgroup_root_mtime =
			BPF_CORE_READ(root_iattr, ia_iattr.ia_mtime.tv_nsec);

		struct kernfs_iattrs___52* proc_iattr =
			(struct kernfs_iattrs___52*)BPF_CORE_READ(proc_kernfs, iattr);
		cgroup_data->cgroup_proc_mtime =
			BPF_CORE_READ(proc_iattr, ia_iattr.ia_mtime.tv_nsec);
	}

	cgroup_data->cgroup_root_length = 0;
	cgroup_data->cgroup_proc_length = 0;
	cgroup_data->cgroup_full_length = 0;

	size_t cgroup_root_length =
		bpf_probe_read_str(payload, MAX_PATH, BPF_CORE_READ(root_kernfs, name));
	barrier_var(cgroup_root_length);
	if (cgroup_root_length <= MAX_PATH) {
		barrier_var(cgroup_root_length);
		cgroup_data->cgroup_root_length = cgroup_root_length;
		payload += cgroup_root_length;
	}

	size_t cgroup_proc_length =
		bpf_probe_read_str(payload, MAX_PATH, BPF_CORE_READ(proc_kernfs, name));
	barrier_var(cgroup_proc_length);
	if (cgroup_proc_length <= MAX_PATH) {
		barrier_var(cgroup_proc_length);
		cgroup_data->cgroup_proc_length = cgroup_proc_length;
		payload += cgroup_proc_length;
	}

	if (FETCH_CGROUPS_FROM_BPF) {
		cgroup_data->cgroup_full_path_root_pos = -1;
		void* payload_end_pos = read_full_cgroup_path(proc_kernfs, root_kernfs, payload,
							      &cgroup_data->cgroup_full_path_root_pos);
		cgroup_data->cgroup_full_length = payload_end_pos - payload;
		payload = payload_end_pos;
	}

	return (void*)payload;
}

static INLINE void* populate_var_metadata(struct var_metadata_t* metadata,
					  struct task_struct* task,
					  u32 pid, void* payload)
{
	u64 uid_gid = bpf_get_current_uid_gid();

	metadata->uid = (u32)uid_gid;
	metadata->gid = uid_gid >> 32;
	metadata->pid = pid;
	metadata->exec_id = BPF_CORE_READ(task, self_exec_id);
	metadata->start_time = BPF_CORE_READ(task, start_time);
	metadata->comm_length = 0;

	size_t comm_length = bpf_core_read_str(payload, TASK_COMM_LEN, &task->comm);
	barrier_var(comm_length);
	if (comm_length <= TASK_COMM_LEN) {
		barrier_var(comm_length);
		metadata->comm_length = comm_length;
		payload += comm_length;
	}

	return (void*)payload;
}

static INLINE struct var_kill_data_t*
get_var_kill_data(struct pt_regs* ctx, int spid, int tpid, int sig)
{
	int zero = 0;
	struct var_kill_data_t* kill_data = bpf_map_lookup_elem(&data_heap, &zero);

	if (kill_data == NULL)
		return NULL;
	struct task_struct* task = (struct task_struct*)bpf_get_current_task();

	void* payload = populate_var_metadata(&kill_data->meta, task, spid, kill_data->payload);
	payload = populate_cgroup_info(&kill_data->cgroup_data, task, payload);
	size_t payload_length = payload - (void*)kill_data->payload;
	kill_data->payload_length = payload_length;
	populate_ancestors(task, &kill_data->ancestors_info);
	kill_data->meta.type = KILL_EVENT;
	kill_data->kill_target_pid = tpid;
	kill_data->kill_sig = sig;
	kill_data->kill_count = 1;
	kill_data->last_kill_time = bpf_ktime_get_ns();
	return kill_data;
}

static INLINE int trace_var_sys_kill(void* ctx, int tpid, int sig)
{
	if ((KILL_SIGNALS & (1ULL << sig)) == 0)
		return 0;

	u32 spid = get_userspace_pid();
	struct var_kill_data_arr_t* arr_struct = bpf_map_lookup_elem(&var_tpid_to_data, &tpid);

	if (arr_struct == NULL) {
		struct var_kill_data_t* kill_data = get_var_kill_data(ctx, spid, tpid, sig);
		int zero = 0;

		if (kill_data == NULL)
			return 0;
		arr_struct = bpf_map_lookup_elem(&data_heap, &zero);
		if (arr_struct == NULL)
			return 0;
		bpf_probe_read(&arr_struct->array[0], sizeof(arr_struct->array[0]), kill_data);
	} else {
		int index = get_var_spid_index(arr_struct, spid);

		if (index == -1) {
			struct var_kill_data_t* kill_data =
				get_var_kill_data(ctx, spid, tpid, sig);
			if (kill_data == NULL)
				return 0;
#ifdef UNROLL
#pragma unroll
#endif
			for (int i = 0; i < ARRAY_SIZE(arr_struct->array); i++)
				if (arr_struct->array[i].meta.pid == 0) {
					bpf_probe_read(&arr_struct->array[i],
						       sizeof(arr_struct->array[i]), kill_data);
					bpf_map_update_elem(&var_tpid_to_data, &tpid,
							    arr_struct, 0);

					return 0;
				}
			return 0;
		}

		struct var_kill_data_t* kill_data = &arr_struct->array[index];

		u64 delta_sec =
			(bpf_ktime_get_ns() - kill_data->last_kill_time) / 1000000000;

		if (delta_sec < STALE_INFO) {
			kill_data->kill_count++;
			kill_data->last_kill_time = bpf_ktime_get_ns();
			bpf_probe_read(&arr_struct->array[index],
				       sizeof(arr_struct->array[index]),
				       kill_data);
		} else {
			struct var_kill_data_t* kill_data =
				get_var_kill_data(ctx, spid, tpid, sig);
			if (kill_data == NULL)
				return 0;
			bpf_probe_read(&arr_struct->array[index],
				       sizeof(arr_struct->array[index]),
				       kill_data);
		}
	}
	bpf_map_update_elem(&var_tpid_to_data, &tpid, arr_struct, 0);
	return 0;
}

static INLINE void bpf_stats_enter(struct bpf_func_stats_ctx* bpf_stat_ctx,
				   enum bpf_function_id func_id)
{
	int func_id_key = func_id;

	bpf_stat_ctx->start_time_ns = bpf_ktime_get_ns();
	bpf_stat_ctx->bpf_func_stats_data_val =
		bpf_map_lookup_elem(&bpf_func_stats, &func_id_key);
	if (bpf_stat_ctx->bpf_func_stats_data_val)
		bpf_stat_ctx->bpf_func_stats_data_val->num_executions++;
}

static INLINE void bpf_stats_exit(struct bpf_func_stats_ctx* bpf_stat_ctx)
{
	if (bpf_stat_ctx->bpf_func_stats_data_val)
		bpf_stat_ctx->bpf_func_stats_data_val->time_elapsed_ns +=
			bpf_ktime_get_ns() - bpf_stat_ctx->start_time_ns;
}

static INLINE void
bpf_stats_pre_submit_var_perf_event(struct bpf_func_stats_ctx* bpf_stat_ctx,
				    struct var_metadata_t* meta)
{
	if (bpf_stat_ctx->bpf_func_stats_data_val) {
		bpf_stat_ctx->bpf_func_stats_data_val->num_perf_events++;
		meta->bpf_stats_num_perf_events =
			bpf_stat_ctx->bpf_func_stats_data_val->num_perf_events;
	}
	meta->bpf_stats_start_ktime_ns = bpf_stat_ctx->start_time_ns;
	meta->cpu_id = bpf_get_smp_processor_id();
}

static INLINE size_t
read_absolute_file_path_from_dentry(struct dentry* filp_dentry, void* payload)
{
	size_t length = 0;
	size_t filepart_length;
	struct dentry* parent_dentry;

#ifdef UNROLL
#pragma unroll
#endif
	for (int i = 0; i < MAX_PATH_DEPTH; i++) {
		filepart_length = bpf_probe_read_str(payload, MAX_PATH,
						     BPF_CORE_READ(filp_dentry, d_name.name));
		barrier_var(filepart_length);
		if (filepart_length > MAX_PATH)
			break;
		barrier_var(filepart_length);
		payload += filepart_length;
		length += filepart_length;

		parent_dentry = BPF_CORE_READ(filp_dentry, d_parent);
		if (filp_dentry == parent_dentry)
			break;
		filp_dentry = parent_dentry;
	}

	return length;
}

static INLINE bool
is_ancestor_in_allowed_inodes(struct dentry* filp_dentry)
{
	struct dentry* parent_dentry;
#ifdef UNROLL
#pragma unroll
#endif
	for (int i = 0; i < MAX_PATH_DEPTH; i++) {
		u64 dir_ino = BPF_CORE_READ(filp_dentry, d_inode, i_ino);
		bool* allowed_dir = bpf_map_lookup_elem(&allowed_directory_inodes, &dir_ino);

		if (allowed_dir != NULL)
			return true;
		parent_dentry = BPF_CORE_READ(filp_dentry, d_parent);
		if (filp_dentry == parent_dentry)
			break;
		filp_dentry = parent_dentry;
	}
	return false;
}

static INLINE bool is_dentry_allowed_for_filemod(struct dentry* file_dentry,
						 u32* device_id,
						 u64* file_ino)
{
	u32 dev_id = BPF_CORE_READ(file_dentry, d_sb, s_dev);
	*device_id = dev_id;
	bool* allowed_device = bpf_map_lookup_elem(&allowed_devices, &dev_id);

	if (allowed_device == NULL)
		return false;

	u64 ino = BPF_CORE_READ(file_dentry, d_inode, i_ino);
	*file_ino = ino;
	bool* allowed_file = bpf_map_lookup_elem(&allowed_file_inodes, &ino);

	if (allowed_file == NULL)
		if (!is_ancestor_in_allowed_inodes(BPF_CORE_READ(file_dentry, d_parent)))
			return false;
	return true;
}

SEC("kprobe/proc_sys_write")
ssize_t BPF_KPROBE(kprobe__proc_sys_write,
		   struct file* filp, const char* buf,
		   size_t count, loff_t* ppos)
{
	struct bpf_func_stats_ctx stats_ctx;
	bpf_stats_enter(&stats_ctx, profiler_bpf_proc_sys_write);

	u32 pid = get_userspace_pid();
	int zero = 0;
	struct var_sysctl_data_t* sysctl_data =
		bpf_map_lookup_elem(&data_heap, &zero);
	if (!sysctl_data)
		goto out;

	struct task_struct* task = (struct task_struct*)bpf_get_current_task();
	sysctl_data->meta.type = SYSCTL_EVENT;
	void* payload = populate_var_metadata(&sysctl_data->meta, task, pid, sysctl_data->payload);
	payload = populate_cgroup_info(&sysctl_data->cgroup_data, task, payload);

	populate_ancestors(task, &sysctl_data->ancestors_info);

	sysctl_data->sysctl_val_length = 0;
	sysctl_data->sysctl_path_length = 0;

	size_t sysctl_val_length = bpf_probe_read_str(payload, CTL_MAXNAME, buf);
	barrier_var(sysctl_val_length);
	if (sysctl_val_length <= CTL_MAXNAME) {
		barrier_var(sysctl_val_length);
		sysctl_data->sysctl_val_length = sysctl_val_length;
		payload += sysctl_val_length;
	}

	size_t sysctl_path_length = bpf_probe_read_str(payload, MAX_PATH,
						       BPF_CORE_READ(filp, f_path.dentry, d_name.name));
	barrier_var(sysctl_path_length);
	if (sysctl_path_length <= MAX_PATH) {
		barrier_var(sysctl_path_length);
		sysctl_data->sysctl_path_length = sysctl_path_length;
		payload += sysctl_path_length;
	}

	bpf_stats_pre_submit_var_perf_event(&stats_ctx, &sysctl_data->meta);
	unsigned long data_len = payload - (void*)sysctl_data;
	data_len = data_len > sizeof(struct var_sysctl_data_t)
		? sizeof(struct var_sysctl_data_t)
		: data_len;
	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, sysctl_data, data_len);
out:
	bpf_stats_exit(&stats_ctx);
	return 0;
}

SEC("tracepoint/syscalls/sys_enter_kill")
int tracepoint__syscalls__sys_enter_kill(struct trace_event_raw_sys_enter* ctx)
{
	struct bpf_func_stats_ctx stats_ctx;

	bpf_stats_enter(&stats_ctx, profiler_bpf_sys_enter_kill);
	int pid = ctx->args[0];
	int sig = ctx->args[1];
	int ret = trace_var_sys_kill(ctx, pid, sig);
	bpf_stats_exit(&stats_ctx);
	return ret;
};

SEC("raw_tracepoint/sched_process_exit")
int raw_tracepoint__sched_process_exit(void* ctx)
{
	int zero = 0;
	struct bpf_func_stats_ctx stats_ctx;
	bpf_stats_enter(&stats_ctx, profiler_bpf_sched_process_exit);

	u32 tpid = get_userspace_pid();

	struct var_kill_data_arr_t* arr_struct = bpf_map_lookup_elem(&var_tpid_to_data, &tpid);
	struct var_kill_data_t* kill_data = bpf_map_lookup_elem(&data_heap, &zero);

	if (arr_struct == NULL || kill_data == NULL)
		goto out;

	struct task_struct* task = (struct task_struct*)bpf_get_current_task();
	struct kernfs_node* proc_kernfs = BPF_CORE_READ(task, cgroups, dfl_cgrp, kn);

#ifdef UNROLL
#pragma unroll
#endif
	for (int i = 0; i < ARRAY_SIZE(arr_struct->array); i++) {
		struct var_kill_data_t* past_kill_data = &arr_struct->array[i];

		if (past_kill_data != NULL && past_kill_data->kill_target_pid == tpid) {
			bpf_probe_read(kill_data, sizeof(*past_kill_data), past_kill_data);
			void* payload = kill_data->payload;
			size_t offset = kill_data->payload_length;
			if (offset >= MAX_METADATA_PAYLOAD_LEN + MAX_CGROUP_PAYLOAD_LEN)
				return 0;
			payload += offset;

			kill_data->kill_target_name_length = 0;
			kill_data->kill_target_cgroup_proc_length = 0;

			size_t comm_length = bpf_core_read_str(payload, TASK_COMM_LEN, &task->comm);
			barrier_var(comm_length);
			if (comm_length <= TASK_COMM_LEN) {
				barrier_var(comm_length);
				kill_data->kill_target_name_length = comm_length;
				payload += comm_length;
			}

			size_t cgroup_proc_length = bpf_probe_read_str(payload, KILL_TARGET_LEN,
								       BPF_CORE_READ(proc_kernfs, name));
			barrier_var(cgroup_proc_length);
			if (cgroup_proc_length <= KILL_TARGET_LEN) {
				barrier_var(cgroup_proc_length);
				kill_data->kill_target_cgroup_proc_length = cgroup_proc_length;
				payload += cgroup_proc_length;
			}

			bpf_stats_pre_submit_var_perf_event(&stats_ctx, &kill_data->meta);
			unsigned long data_len = (void*)payload - (void*)kill_data;
			data_len = data_len > sizeof(struct var_kill_data_t)
				? sizeof(struct var_kill_data_t)
				: data_len;
			bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, kill_data, data_len);
		}
	}
	bpf_map_delete_elem(&var_tpid_to_data, &tpid);
out:
	bpf_stats_exit(&stats_ctx);
	return 0;
}

SEC("raw_tracepoint/sched_process_exec")
int raw_tracepoint__sched_process_exec(struct bpf_raw_tracepoint_args* ctx)
{
	struct bpf_func_stats_ctx stats_ctx;
	bpf_stats_enter(&stats_ctx, profiler_bpf_sched_process_exec);

	struct linux_binprm* bprm = (struct linux_binprm*)ctx->args[2];
	u64 inode = BPF_CORE_READ(bprm, file, f_inode, i_ino);

	bool* should_filter_binprm = bpf_map_lookup_elem(&disallowed_exec_inodes, &inode);
	if (should_filter_binprm != NULL)
		goto out;

	int zero = 0;
	struct var_exec_data_t* proc_exec_data = bpf_map_lookup_elem(&data_heap, &zero);
	if (!proc_exec_data)
		goto out;

	if (INODE_FILTER && inode != INODE_FILTER)
		return 0;

	u32 pid = get_userspace_pid();
	struct task_struct* task = (struct task_struct*)bpf_get_current_task();

	proc_exec_data->meta.type = EXEC_EVENT;
	proc_exec_data->bin_path_length = 0;
	proc_exec_data->cmdline_length = 0;
	proc_exec_data->environment_length = 0;
	void* payload = populate_var_metadata(&proc_exec_data->meta, task, pid,
					      proc_exec_data->payload);
	payload = populate_cgroup_info(&proc_exec_data->cgroup_data, task, payload);

	struct task_struct* parent_task = BPF_CORE_READ(task, real_parent);
	proc_exec_data->parent_pid = BPF_CORE_READ(parent_task, tgid);
	proc_exec_data->parent_uid = BPF_CORE_READ(parent_task, real_cred, uid.val);
	proc_exec_data->parent_exec_id = BPF_CORE_READ(parent_task, self_exec_id);
	proc_exec_data->parent_start_time = BPF_CORE_READ(parent_task, start_time);

	const char* filename = BPF_CORE_READ(bprm, filename);
	size_t bin_path_length = bpf_probe_read_str(payload, MAX_FILENAME_LEN, filename);
	barrier_var(bin_path_length);
	if (bin_path_length <= MAX_FILENAME_LEN) {
		barrier_var(bin_path_length);
		proc_exec_data->bin_path_length = bin_path_length;
		payload += bin_path_length;
	}

	void* arg_start = (void*)BPF_CORE_READ(task, mm, arg_start);
	void* arg_end = (void*)BPF_CORE_READ(task, mm, arg_end);
	unsigned int cmdline_length = probe_read_lim(payload, arg_start,
						     arg_end - arg_start, MAX_ARGS_LEN);

	if (cmdline_length <= MAX_ARGS_LEN) {
		barrier_var(cmdline_length);
		proc_exec_data->cmdline_length = cmdline_length;
		payload += cmdline_length;
	}

	if (READ_ENVIRON_FROM_EXEC) {
		void* env_start = (void*)BPF_CORE_READ(task, mm, env_start);
		void* env_end = (void*)BPF_CORE_READ(task, mm, env_end);
		unsigned long env_len = probe_read_lim(payload, env_start,
						       env_end - env_start, MAX_ENVIRON_LEN);
		if (cmdline_length <= MAX_ENVIRON_LEN) {
			proc_exec_data->environment_length = env_len;
			payload += env_len;
		}
	}

	bpf_stats_pre_submit_var_perf_event(&stats_ctx, &proc_exec_data->meta);
	unsigned long data_len = payload - (void*)proc_exec_data;
	data_len = data_len > sizeof(struct var_exec_data_t)
		? sizeof(struct var_exec_data_t)
		: data_len;
	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, proc_exec_data, data_len);
out:
	bpf_stats_exit(&stats_ctx);
	return 0;
}

SEC("kretprobe/do_filp_open")
int kprobe_ret__do_filp_open(struct pt_regs* ctx)
{
	struct bpf_func_stats_ctx stats_ctx;
	bpf_stats_enter(&stats_ctx, profiler_bpf_do_filp_open_ret);

	struct file* filp = (struct file*)PT_REGS_RC_CORE(ctx);

	if (filp == NULL || IS_ERR(filp))
		goto out;
	unsigned int flags = BPF_CORE_READ(filp, f_flags);
	if ((flags & (O_RDWR | O_WRONLY)) == 0)
		goto out;
	if ((flags & O_TMPFILE) > 0)
		goto out;
	struct inode* file_inode = BPF_CORE_READ(filp, f_inode);
	umode_t mode = BPF_CORE_READ(file_inode, i_mode);
	if (S_ISDIR(mode) || S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode) ||
	    S_ISSOCK(mode))
		goto out;

	struct dentry* filp_dentry = BPF_CORE_READ(filp, f_path.dentry);
	u32 device_id = 0;
	u64 file_ino = 0;
	if (!is_dentry_allowed_for_filemod(filp_dentry, &device_id, &file_ino))
		goto out;

	int zero = 0;
	struct var_filemod_data_t* filemod_data = bpf_map_lookup_elem(&data_heap, &zero);
	if (!filemod_data)
		goto out;

	u32 pid = get_userspace_pid();
	struct task_struct* task = (struct task_struct*)bpf_get_current_task();

	filemod_data->meta.type = FILEMOD_EVENT;
	filemod_data->fmod_type = FMOD_OPEN;
	filemod_data->dst_flags = flags;
	filemod_data->src_inode = 0;
	filemod_data->dst_inode = file_ino;
	filemod_data->src_device_id = 0;
	filemod_data->dst_device_id = device_id;
	filemod_data->src_filepath_length = 0;
	filemod_data->dst_filepath_length = 0;

	void* payload = populate_var_metadata(&filemod_data->meta, task, pid,
					      filemod_data->payload);
	payload = populate_cgroup_info(&filemod_data->cgroup_data, task, payload);

	size_t len = read_absolute_file_path_from_dentry(filp_dentry, payload);
	barrier_var(len);
	if (len <= MAX_FILEPATH_LENGTH) {
		barrier_var(len);
		payload += len;
		filemod_data->dst_filepath_length = len;
	}
	bpf_stats_pre_submit_var_perf_event(&stats_ctx, &filemod_data->meta);
	unsigned long data_len = payload - (void*)filemod_data;
	data_len = data_len > sizeof(*filemod_data) ? sizeof(*filemod_data) : data_len;
	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, filemod_data, data_len);
out:
	bpf_stats_exit(&stats_ctx);
	return 0;
}

SEC("kprobe/vfs_link")
int BPF_KPROBE(kprobe__vfs_link,
	       struct dentry* old_dentry, struct inode* dir,
	       struct dentry* new_dentry, struct inode** delegated_inode)
{
	struct bpf_func_stats_ctx stats_ctx;
	bpf_stats_enter(&stats_ctx, profiler_bpf_vfs_link);

	u32 src_device_id = 0;
	u64 src_file_ino = 0;
	u32 dst_device_id = 0;
	u64 dst_file_ino = 0;
	if (!is_dentry_allowed_for_filemod(old_dentry, &src_device_id, &src_file_ino) &&
	    !is_dentry_allowed_for_filemod(new_dentry, &dst_device_id, &dst_file_ino))
		goto out;

	int zero = 0;
	struct var_filemod_data_t* filemod_data = bpf_map_lookup_elem(&data_heap, &zero);
	if (!filemod_data)
		goto out;

	u32 pid = get_userspace_pid();
	struct task_struct* task = (struct task_struct*)bpf_get_current_task();

	filemod_data->meta.type = FILEMOD_EVENT;
	filemod_data->fmod_type = FMOD_LINK;
	filemod_data->dst_flags = 0;
	filemod_data->src_inode = src_file_ino;
	filemod_data->dst_inode = dst_file_ino;
	filemod_data->src_device_id = src_device_id;
	filemod_data->dst_device_id = dst_device_id;
	filemod_data->src_filepath_length = 0;
	filemod_data->dst_filepath_length = 0;

	void* payload = populate_var_metadata(&filemod_data->meta, task, pid,
					      filemod_data->payload);
	payload = populate_cgroup_info(&filemod_data->cgroup_data, task, payload);

	size_t len = read_absolute_file_path_from_dentry(old_dentry, payload);
	barrier_var(len);
	if (len <= MAX_FILEPATH_LENGTH) {
		barrier_var(len);
		payload += len;
		filemod_data->src_filepath_length = len;
	}

	len = read_absolute_file_path_from_dentry(new_dentry, payload);
	barrier_var(len);
	if (len <= MAX_FILEPATH_LENGTH) {
		barrier_var(len);
		payload += len;
		filemod_data->dst_filepath_length = len;
	}

	bpf_stats_pre_submit_var_perf_event(&stats_ctx, &filemod_data->meta);
	unsigned long data_len = payload - (void*)filemod_data;
	data_len = data_len > sizeof(*filemod_data) ? sizeof(*filemod_data) : data_len;
	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, filemod_data, data_len);
out:
	bpf_stats_exit(&stats_ctx);
	return 0;
}

SEC("kprobe/vfs_symlink")
int BPF_KPROBE(kprobe__vfs_symlink, struct inode* dir, struct dentry* dentry,
	       const char* oldname)
{
	struct bpf_func_stats_ctx stats_ctx;
	bpf_stats_enter(&stats_ctx, profiler_bpf_vfs_symlink);

	u32 dst_device_id = 0;
	u64 dst_file_ino = 0;
	if (!is_dentry_allowed_for_filemod(dentry, &dst_device_id, &dst_file_ino))
		goto out;

	int zero = 0;
	struct var_filemod_data_t* filemod_data = bpf_map_lookup_elem(&data_heap, &zero);
	if (!filemod_data)
		goto out;

	u32 pid = get_userspace_pid();
	struct task_struct* task = (struct task_struct*)bpf_get_current_task();

	filemod_data->meta.type = FILEMOD_EVENT;
	filemod_data->fmod_type = FMOD_SYMLINK;
	filemod_data->dst_flags = 0;
	filemod_data->src_inode = 0;
	filemod_data->dst_inode = dst_file_ino;
	filemod_data->src_device_id = 0;
	filemod_data->dst_device_id = dst_device_id;
	filemod_data->src_filepath_length = 0;
	filemod_data->dst_filepath_length = 0;

	void* payload = populate_var_metadata(&filemod_data->meta, task, pid,
					      filemod_data->payload);
	payload = populate_cgroup_info(&filemod_data->cgroup_data, task, payload);

	size_t len = bpf_probe_read_str(payload, MAX_FILEPATH_LENGTH, oldname);
	barrier_var(len);
	if (len <= MAX_FILEPATH_LENGTH) {
		barrier_var(len);
		payload += len;
		filemod_data->src_filepath_length = len;
	}
	len = read_absolute_file_path_from_dentry(dentry, payload);
	barrier_var(len);
	if (len <= MAX_FILEPATH_LENGTH) {
		barrier_var(len);
		payload += len;
		filemod_data->dst_filepath_length = len;
	}
	bpf_stats_pre_submit_var_perf_event(&stats_ctx, &filemod_data->meta);
	unsigned long data_len = payload - (void*)filemod_data;
	data_len = data_len > sizeof(*filemod_data) ? sizeof(*filemod_data) : data_len;
	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, filemod_data, data_len);
out:
	bpf_stats_exit(&stats_ctx);
	return 0;
}

SEC("raw_tracepoint/sched_process_fork")
int raw_tracepoint__sched_process_fork(struct bpf_raw_tracepoint_args* ctx)
{
	struct bpf_func_stats_ctx stats_ctx;
	bpf_stats_enter(&stats_ctx, profiler_bpf_sched_process_fork);

	int zero = 0;
	struct var_fork_data_t* fork_data = bpf_map_lookup_elem(&data_heap, &zero);
	if (!fork_data)
		goto out;

	struct task_struct* parent = (struct task_struct*)ctx->args[0];
	struct task_struct* child = (struct task_struct*)ctx->args[1];
	fork_data->meta.type = FORK_EVENT;

	void* payload = populate_var_metadata(&fork_data->meta, child,
					      BPF_CORE_READ(child, pid), fork_data->payload);
	fork_data->parent_pid = BPF_CORE_READ(parent, pid);
	fork_data->parent_exec_id = BPF_CORE_READ(parent, self_exec_id);
	fork_data->parent_start_time = BPF_CORE_READ(parent, start_time);
	bpf_stats_pre_submit_var_perf_event(&stats_ctx, &fork_data->meta);

	unsigned long data_len = payload - (void*)fork_data;
	data_len = data_len > sizeof(*fork_data) ? sizeof(*fork_data) : data_len;
	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, fork_data, data_len);
out:
	bpf_stats_exit(&stats_ctx);
	return 0;
}
char _license[] SEC("license") = "GPL";

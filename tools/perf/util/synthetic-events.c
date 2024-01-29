// SPDX-License-Identifier: GPL-2.0-only 

#include "util/cgroup.h"
#include "util/data.h"
#include "util/debug.h"
#include "util/dso.h"
#include "util/event.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/map.h"
#include "util/map_symbol.h"
#include "util/branch.h"
#include "util/memswap.h"
#include "util/namespaces.h"
#include "util/session.h"
#include "util/stat.h"
#include "util/symbol.h"
#include "util/synthetic-events.h"
#include "util/target.h"
#include "util/time-utils.h"
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <linux/perf_event.h>
#include <asm/bug.h>
#include <perf/evsel.h>
#include <perf/cpumap.h>
#include <internal/lib.h> // page_size
#include <internal/threadmap.h>
#include <perf/threadmap.h>
#include <symbol/kallsyms.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <uapi/linux/mman.h> /* To get things like MAP_HUGETLB even on older libc headers */
#include <api/fs/fs.h>
#include <api/io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define DEFAULT_PROC_MAP_PARSE_TIMEOUT 500

unsigned int proc_map_timeout = DEFAULT_PROC_MAP_PARSE_TIMEOUT;

int perf_tool__process_synth_event(struct perf_tool *tool,
				   union perf_event *event,
				   struct machine *machine,
				   perf_event__handler_t process)
{
	struct perf_sample synth_sample = {
		.pid	   = -1,
		.tid	   = -1,
		.time	   = -1,
		.stream_id = -1,
		.cpu	   = -1,
		.period	   = 1,
		.cpumode   = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK,
	};

	return process(tool, event, &synth_sample, machine);
};

/*
 * Assumes that the first 4095 bytes of /proc/pid/stat contains
 * the comm, tgid and ppid.
 */
static int perf_event__get_comm_ids(pid_t pid, pid_t tid, char *comm, size_t len,
				    pid_t *tgid, pid_t *ppid, bool *kernel)
{
	char bf[4096];
	int fd;
	size_t size = 0;
	ssize_t n;
	char *name, *tgids, *ppids, *vmpeak, *threads;

	*tgid = -1;
	*ppid = -1;

	if (pid)
		snprintf(bf, sizeof(bf), "/proc/%d/task/%d/status", pid, tid);
	else
		snprintf(bf, sizeof(bf), "/proc/%d/status", tid);

	fd = open(bf, O_RDONLY);
	if (fd < 0) {
		pr_debug("couldn't open %s\n", bf);
		return -1;
	}

	n = read(fd, bf, sizeof(bf) - 1);
	close(fd);
	if (n <= 0) {
		pr_warning("Couldn't get COMM, tigd and ppid for pid %d\n",
			   tid);
		return -1;
	}
	bf[n] = '\0';

	name = strstr(bf, "Name:");
	tgids = strstr(name ?: bf, "Tgid:");
	ppids = strstr(tgids ?: bf, "PPid:");
	vmpeak = strstr(ppids ?: bf, "VmPeak:");

	if (vmpeak)
		threads = NULL;
	else
		threads = strstr(ppids ?: bf, "Threads:");

	if (name) {
		char *nl;

		name = skip_spaces(name + 5);  /* strlen("Name:") */
		nl = strchr(name, '\n');
		if (nl)
			*nl = '\0';

		size = strlen(name);
		if (size >= len)
			size = len - 1;
		memcpy(comm, name, size);
		comm[size] = '\0';
	} else {
		pr_debug("Name: string not found for pid %d\n", tid);
	}

	if (tgids) {
		tgids += 5;  /* strlen("Tgid:") */
		*tgid = atoi(tgids);
	} else {
		pr_debug("Tgid: string not found for pid %d\n", tid);
	}

	if (ppids) {
		ppids += 5;  /* strlen("PPid:") */
		*ppid = atoi(ppids);
	} else {
		pr_debug("PPid: string not found for pid %d\n", tid);
	}

	if (!vmpeak && threads)
		*kernel = true;
	else
		*kernel = false;

	return 0;
}

static int perf_event__prepare_comm(union perf_event *event, pid_t pid, pid_t tid,
				    struct machine *machine,
				    pid_t *tgid, pid_t *ppid, bool *kernel)
{
	size_t size;

	*ppid = -1;

	memset(&event->comm, 0, sizeof(event->comm));

	if (machine__is_host(machine)) {
		if (perf_event__get_comm_ids(pid, tid, event->comm.comm,
					     sizeof(event->comm.comm),
					     tgid, ppid, kernel) != 0) {
			return -1;
		}
	} else {
		*tgid = machine->pid;
	}

	if (*tgid < 0)
		return -1;

	event->comm.pid = *tgid;
	event->comm.header.type = PERF_RECORD_COMM;

	size = strlen(event->comm.comm) + 1;
	size = PERF_ALIGN(size, sizeof(u64));
	memset(event->comm.comm + size, 0, machine->id_hdr_size);
	event->comm.header.size = (sizeof(event->comm) -
				(sizeof(event->comm.comm) - size) +
				machine->id_hdr_size);
	event->comm.tid = tid;

	return 0;
}

pid_t perf_event__synthesize_comm(struct perf_tool *tool,
					 union perf_event *event, pid_t pid,
					 perf_event__handler_t process,
					 struct machine *machine)
{
	pid_t tgid, ppid;
	bool kernel_thread;

	if (perf_event__prepare_comm(event, 0, pid, machine, &tgid, &ppid,
				     &kernel_thread) != 0)
		return -1;

	if (perf_tool__process_synth_event(tool, event, machine, process) != 0)
		return -1;

	return tgid;
}

static void perf_event__get_ns_link_info(pid_t pid, const char *ns,
					 struct perf_ns_link_info *ns_link_info)
{
	struct stat64 st;
	char proc_ns[128];

	sprintf(proc_ns, "/proc/%u/ns/%s", pid, ns);
	if (stat64(proc_ns, &st) == 0) {
		ns_link_info->dev = st.st_dev;
		ns_link_info->ino = st.st_ino;
	}
}

int perf_event__synthesize_namespaces(struct perf_tool *tool,
				      union perf_event *event,
				      pid_t pid, pid_t tgid,
				      perf_event__handler_t process,
				      struct machine *machine)
{
	u32 idx;
	struct perf_ns_link_info *ns_link_info;

	if (!tool || !tool->namespace_events)
		return 0;

	memset(&event->namespaces, 0, (sizeof(event->namespaces) +
	       (NR_NAMESPACES * sizeof(struct perf_ns_link_info)) +
	       machine->id_hdr_size));

	event->namespaces.pid = tgid;
	event->namespaces.tid = pid;

	event->namespaces.nr_namespaces = NR_NAMESPACES;

	ns_link_info = event->namespaces.link_info;

	for (idx = 0; idx < event->namespaces.nr_namespaces; idx++)
		perf_event__get_ns_link_info(pid, perf_ns__name(idx),
					     &ns_link_info[idx]);

	event->namespaces.header.type = PERF_RECORD_NAMESPACES;

	event->namespaces.header.size = (sizeof(event->namespaces) +
			(NR_NAMESPACES * sizeof(struct perf_ns_link_info)) +
			machine->id_hdr_size);

	if (perf_tool__process_synth_event(tool, event, machine, process) != 0)
		return -1;

	return 0;
}

static int perf_event__synthesize_fork(struct perf_tool *tool,
				       union perf_event *event,
				       pid_t pid, pid_t tgid, pid_t ppid,
				       perf_event__handler_t process,
				       struct machine *machine)
{
	memset(&event->fork, 0, sizeof(event->fork) + machine->id_hdr_size);

	/*
	 * for main thread set parent to ppid from status file. For other
	 * threads set parent pid to main thread. ie., assume main thread
	 * spawns all threads in a process
	*/
	if (tgid == pid) {
		event->fork.ppid = ppid;
		event->fork.ptid = ppid;
	} else {
		event->fork.ppid = tgid;
		event->fork.ptid = tgid;
	}
	event->fork.pid  = tgid;
	event->fork.tid  = pid;
	event->fork.header.type = PERF_RECORD_FORK;
	event->fork.header.misc = PERF_RECORD_MISC_FORK_EXEC;

	event->fork.header.size = (sizeof(event->fork) + machine->id_hdr_size);

	if (perf_tool__process_synth_event(tool, event, machine, process) != 0)
		return -1;

	return 0;
}

static bool read_proc_maps_line(struct io *io, __u64 *start, __u64 *end,
				u32 *prot, u32 *flags, __u64 *offset,
				u32 *maj, u32 *min,
				__u64 *inode,
				ssize_t pathname_size, char *pathname)
{
	__u64 temp;
	int ch;
	char *start_pathname = pathname;

	if (io__get_hex(io, start) != '-')
		return false;
	if (io__get_hex(io, end) != ' ')
		return false;

	/* map protection and flags bits */
	*prot = 0;
	ch = io__get_char(io);
	if (ch == 'r')
		*prot |= PROT_READ;
	else if (ch != '-')
		return false;
	ch = io__get_char(io);
	if (ch == 'w')
		*prot |= PROT_WRITE;
	else if (ch != '-')
		return false;
	ch = io__get_char(io);
	if (ch == 'x')
		*prot |= PROT_EXEC;
	else if (ch != '-')
		return false;
	ch = io__get_char(io);
	if (ch == 's')
		*flags = MAP_SHARED;
	else if (ch == 'p')
		*flags = MAP_PRIVATE;
	else
		return false;
	if (io__get_char(io) != ' ')
		return false;

	if (io__get_hex(io, offset) != ' ')
		return false;

	if (io__get_hex(io, &temp) != ':')
		return false;
	*maj = temp;
	if (io__get_hex(io, &temp) != ' ')
		return false;
	*min = temp;

	ch = io__get_dec(io, inode);
	if (ch != ' ') {
		*pathname = '\0';
		return ch == '\n';
	}
	do {
		ch = io__get_char(io);
	} while (ch == ' ');
	while (true) {
		if (ch < 0)
			return false;
		if (ch == '\0' || ch == '\n' ||
		    (pathname + 1 - start_pathname) >= pathname_size) {
			*pathname = '\0';
			return true;
		}
		*pathname++ = ch;
		ch = io__get_char(io);
	}
}

static void perf_record_mmap2__read_build_id(struct perf_record_mmap2 *event,
					     struct machine *machine,
					     bool is_kernel)
{
	struct build_id bid;
	struct nsinfo *nsi;
	struct nscookie nc;
	struct dso *dso = NULL;
	struct dso_id id;
	int rc;

	if (is_kernel) {
		rc = sysfs__read_build_id("/sys/kernel/notes", &bid);
		goto out;
	}

	id.maj = event->maj;
	id.min = event->min;
	id.ino = event->ino;
	id.ino_generation = event->ino_generation;

	dso = dsos__findnew_id(&machine->dsos, event->filename, &id);
	if (dso && dso->has_build_id) {
		bid = dso->bid;
		rc = 0;
		goto out;
	}

	nsi = nsinfo__new(event->pid);
	nsinfo__mountns_enter(nsi, &nc);

	rc = filename__read_build_id(event->filename, &bid) > 0 ? 0 : -1;

	nsinfo__mountns_exit(&nc);
	nsinfo__put(nsi);

out:
	if (rc == 0) {
		memcpy(event->build_id, bid.data, sizeof(bid.data));
		event->build_id_size = (u8) bid.size;
		event->header.misc |= PERF_RECORD_MISC_MMAP_BUILD_ID;
		event->__reserved_1 = 0;
		event->__reserved_2 = 0;

		if (dso && !dso->has_build_id)
			dso__set_build_id(dso, &bid);
	} else {
		if (event->filename[0] == '/') {
			pr_debug2("Failed to read build ID for %s\n",
				  event->filename);
		}
	}
	dso__put(dso);
}

int perf_event__synthesize_mmap_events(struct perf_tool *tool,
				       union perf_event *event,
				       pid_t pid, pid_t tgid,
				       perf_event__handler_t process,
				       struct machine *machine,
				       bool mmap_data)
{
	unsigned long long t;
	char bf[BUFSIZ];
	struct io io;
	bool truncation = false;
	unsigned long long timeout = proc_map_timeout * 1000000ULL;
	int rc = 0;
	const char *hugetlbfs_mnt = hugetlbfs__mountpoint();
	int hugetlbfs_mnt_len = hugetlbfs_mnt ? strlen(hugetlbfs_mnt) : 0;

	if (machine__is_default_guest(machine))
		return 0;

	snprintf(bf, sizeof(bf), "%s/proc/%d/task/%d/maps",
		machine->root_dir, pid, pid);

	io.fd = open(bf, O_RDONLY, 0);
	if (io.fd < 0) {
		/*
		 * We raced with a task exiting - just return:
		 */
		pr_debug("couldn't open %s\n", bf);
		return -1;
	}
	io__init(&io, io.fd, bf, sizeof(bf));

	event->header.type = PERF_RECORD_MMAP2;
	t = rdclock();

	while (!io.eof) {
		static const char anonstr[] = "//anon";
		size_t size, aligned_size;

		/* ensure null termination since stack will be reused. */
		event->mmap2.filename[0] = '\0';

		/* 00400000-0040c000 r-xp 00000000 fd:01 41038  /bin/cat */
		if (!read_proc_maps_line(&io,
					&event->mmap2.start,
					&event->mmap2.len,
					&event->mmap2.prot,
					&event->mmap2.flags,
					&event->mmap2.pgoff,
					&event->mmap2.maj,
					&event->mmap2.min,
					&event->mmap2.ino,
					sizeof(event->mmap2.filename),
					event->mmap2.filename))
			continue;

		if ((rdclock() - t) > timeout) {
			pr_warning("Reading %s/proc/%d/task/%d/maps time out. "
				   "You may want to increase "
				   "the time limit by --proc-map-timeout\n",
				   machine->root_dir, pid, pid);
			truncation = true;
			goto out;
		}

		event->mmap2.ino_generation = 0;

		/*
		 * Just like the kernel, see __perf_event_mmap in kernel/perf_event.c
		 */
		if (machine__is_host(machine))
			event->header.misc = PERF_RECORD_MISC_USER;
		else
			event->header.misc = PERF_RECORD_MISC_GUEST_USER;

		if ((event->mmap2.prot & PROT_EXEC) == 0) {
			if (!mmap_data || (event->mmap2.prot & PROT_READ) == 0)
				continue;

			event->header.misc |= PERF_RECORD_MISC_MMAP_DATA;
		}

out:
		if (truncation)
			event->header.misc |= PERF_RECORD_MISC_PROC_MAP_PARSE_TIMEOUT;

		if (!strcmp(event->mmap2.filename, ""))
			strcpy(event->mmap2.filename, anonstr);

		if (hugetlbfs_mnt_len &&
		    !strncmp(event->mmap2.filename, hugetlbfs_mnt,
			     hugetlbfs_mnt_len)) {
			strcpy(event->mmap2.filename, anonstr);
			event->mmap2.flags |= MAP_HUGETLB;
		}

		size = strlen(event->mmap2.filename) + 1;
		aligned_size = PERF_ALIGN(size, sizeof(u64));
		event->mmap2.len -= event->mmap.start;
		event->mmap2.header.size = (sizeof(event->mmap2) -
					(sizeof(event->mmap2.filename) - aligned_size));
		memset(event->mmap2.filename + size, 0, machine->id_hdr_size +
			(aligned_size - size));
		event->mmap2.header.size += machine->id_hdr_size;
		event->mmap2.pid = tgid;
		event->mmap2.tid = pid;

		if (symbol_conf.buildid_mmap2)
			perf_record_mmap2__read_build_id(&event->mmap2, machine, false);

		if (perf_tool__process_synth_event(tool, event, machine, process) != 0) {
			rc = -1;
			break;
		}

		if (truncation)
			break;
	}

	close(io.fd);
	return rc;
}

#ifdef HAVE_FILE_HANDLE
static int perf_event__synthesize_cgroup(struct perf_tool *tool,
					 union perf_event *event,
					 char *path, size_t mount_len,
					 perf_event__handler_t process,
					 struct machine *machine)
{
	size_t event_size = sizeof(event->cgroup) - sizeof(event->cgroup.path);
	size_t path_len = strlen(path) - mount_len + 1;
	struct {
		struct file_handle fh;
		uint64_t cgroup_id;
	} handle;
	int mount_id;

	while (path_len % sizeof(u64))
		path[mount_len + path_len++] = '\0';

	memset(&event->cgroup, 0, event_size);

	event->cgroup.header.type = PERF_RECORD_CGROUP;
	event->cgroup.header.size = event_size + path_len + machine->id_hdr_size;

	handle.fh.handle_bytes = sizeof(handle.cgroup_id);
	if (name_to_handle_at(AT_FDCWD, path, &handle.fh, &mount_id, 0) < 0) {
		pr_debug("stat failed: %s\n", path);
		return -1;
	}

	event->cgroup.id = handle.cgroup_id;
	strncpy(event->cgroup.path, path + mount_len, path_len);
	memset(event->cgroup.path + path_len, 0, machine->id_hdr_size);

	if (perf_tool__process_synth_event(tool, event, machine, process) < 0) {
		pr_debug("process synth event failed\n");
		return -1;
	}

	return 0;
}

static int perf_event__walk_cgroup_tree(struct perf_tool *tool,
					union perf_event *event,
					char *path, size_t mount_len,
					perf_event__handler_t process,
					struct machine *machine)
{
	size_t pos = strlen(path);
	DIR *d;
	struct dirent *dent;
	int ret = 0;

	if (perf_event__synthesize_cgroup(tool, event, path, mount_len,
					  process, machine) < 0)
		return -1;

	d = opendir(path);
	if (d == NULL) {
		pr_debug("failed to open directory: %s\n", path);
		return -1;
	}

	while ((dent = readdir(d)) != NULL) {
		if (dent->d_type != DT_DIR)
			continue;
		if (!strcmp(dent->d_name, ".") ||
		    !strcmp(dent->d_name, ".."))
			continue;

		/* any sane path should be less than PATH_MAX */
		if (strlen(path) + strlen(dent->d_name) + 1 >= PATH_MAX)
			continue;

		if (path[pos - 1] != '/')
			strcat(path, "/");
		strcat(path, dent->d_name);

		ret = perf_event__walk_cgroup_tree(tool, event, path,
						   mount_len, process, machine);
		if (ret < 0)
			break;

		path[pos] = '\0';
	}

	closedir(d);
	return ret;
}

int perf_event__synthesize_cgroups(struct perf_tool *tool,
				   perf_event__handler_t process,
				   struct machine *machine)
{
	union perf_event event;
	char cgrp_root[PATH_MAX];
	size_t mount_len;  /* length of mount point in the path */

	if (!tool || !tool->cgroup_events)
		return 0;

	if (cgroupfs_find_mountpoint(cgrp_root, PATH_MAX, "perf_event") < 0) {
		pr_debug("cannot find cgroup mount point\n");
		return -1;
	}

	mount_len = strlen(cgrp_root);
	/* make sure the path starts with a slash (after mount point) */
	strcat(cgrp_root, "/");

	if (perf_event__walk_cgroup_tree(tool, &event, cgrp_root, mount_len,
					 process, machine) < 0)
		return -1;

	return 0;
}
#else
int perf_event__synthesize_cgroups(struct perf_tool *tool __maybe_unused,
				   perf_event__handler_t process __maybe_unused,
				   struct machine *machine __maybe_unused)
{
	return -1;
}
#endif

struct perf_event__synthesize_modules_maps_cb_args {
	struct perf_tool *tool;
	perf_event__handler_t process;
	struct machine *machine;
	union perf_event *event;
};

static int perf_event__synthesize_modules_maps_cb(struct map *map, void *data)
{
	struct perf_event__synthesize_modules_maps_cb_args *args = data;
	union perf_event *event = args->event;
	struct dso *dso;
	size_t size;

	if (!__map__is_kmodule(map))
		return 0;

	dso = map__dso(map);
	if (symbol_conf.buildid_mmap2) {
		size = PERF_ALIGN(dso->long_name_len + 1, sizeof(u64));
		event->mmap2.header.type = PERF_RECORD_MMAP2;
		event->mmap2.header.size = (sizeof(event->mmap2) -
					(sizeof(event->mmap2.filename) - size));
		memset(event->mmap2.filename + size, 0, args->machine->id_hdr_size);
		event->mmap2.header.size += args->machine->id_hdr_size;
		event->mmap2.start = map__start(map);
		event->mmap2.len   = map__size(map);
		event->mmap2.pid   = args->machine->pid;

		memcpy(event->mmap2.filename, dso->long_name, dso->long_name_len + 1);

		perf_record_mmap2__read_build_id(&event->mmap2, args->machine, false);
	} else {
		size = PERF_ALIGN(dso->long_name_len + 1, sizeof(u64));
		event->mmap.header.type = PERF_RECORD_MMAP;
		event->mmap.header.size = (sizeof(event->mmap) -
					(sizeof(event->mmap.filename) - size));
		memset(event->mmap.filename + size, 0, args->machine->id_hdr_size);
		event->mmap.header.size += args->machine->id_hdr_size;
		event->mmap.start = map__start(map);
		event->mmap.len   = map__size(map);
		event->mmap.pid   = args->machine->pid;

		memcpy(event->mmap.filename, dso->long_name, dso->long_name_len + 1);
	}

	if (perf_tool__process_synth_event(args->tool, event, args->machine, args->process) != 0)
		return -1;

	return 0;
}

int perf_event__synthesize_modules(struct perf_tool *tool, perf_event__handler_t process,
				   struct machine *machine)
{
	int rc;
	struct maps *maps = machine__kernel_maps(machine);
	struct perf_event__synthesize_modules_maps_cb_args args = {
		.tool = tool,
		.process = process,
		.machine = machine,
	};
	size_t size = symbol_conf.buildid_mmap2
		? sizeof(args.event->mmap2)
		: sizeof(args.event->mmap);

	args.event = zalloc(size + machine->id_hdr_size);
	if (args.event == NULL) {
		pr_debug("Not enough memory synthesizing mmap event "
			 "for kernel modules\n");
		return -1;
	}

	/*
	 * kernel uses 0 for user space maps, see kernel/perf_event.c
	 * __perf_event_mmap
	 */
	if (machine__is_host(machine))
		args.event->header.misc = PERF_RECORD_MISC_KERNEL;
	else
		args.event->header.misc = PERF_RECORD_MISC_GUEST_KERNEL;

	rc = maps__for_each_map(maps, perf_event__synthesize_modules_maps_cb, &args);

	free(args.event);
	return rc;
}

static int filter_task(const struct dirent *dirent)
{
	return isdigit(dirent->d_name[0]);
}

static int __event__synthesize_thread(union perf_event *comm_event,
				      union perf_event *mmap_event,
				      union perf_event *fork_event,
				      union perf_event *namespaces_event,
				      pid_t pid, int full, perf_event__handler_t process,
				      struct perf_tool *tool, struct machine *machine,
				      bool needs_mmap, bool mmap_data)
{
	char filename[PATH_MAX];
	struct dirent **dirent;
	pid_t tgid, ppid;
	int rc = 0;
	int i, n;

	/* special case: only send one comm event using passed in pid */
	if (!full) {
		tgid = perf_event__synthesize_comm(tool, comm_event, pid,
						   process, machine);

		if (tgid == -1)
			return -1;

		if (perf_event__synthesize_namespaces(tool, namespaces_event, pid,
						      tgid, process, machine) < 0)
			return -1;

		/*
		 * send mmap only for thread group leader
		 * see thread__init_maps()
		 */
		if (pid == tgid && needs_mmap &&
		    perf_event__synthesize_mmap_events(tool, mmap_event, pid, tgid,
						       process, machine, mmap_data))
			return -1;

		return 0;
	}

	if (machine__is_default_guest(machine))
		return 0;

	snprintf(filename, sizeof(filename), "%s/proc/%d/task",
		 machine->root_dir, pid);

	n = scandir(filename, &dirent, filter_task, NULL);
	if (n < 0)
		return n;

	for (i = 0; i < n; i++) {
		char *end;
		pid_t _pid;
		bool kernel_thread = false;

		_pid = strtol(dirent[i]->d_name, &end, 10);
		if (*end)
			continue;

		/* some threads may exit just after scan, ignore it */
		if (perf_event__prepare_comm(comm_event, pid, _pid, machine,
					     &tgid, &ppid, &kernel_thread) != 0)
			continue;

		rc = -1;
		if (perf_event__synthesize_fork(tool, fork_event, _pid, tgid,
						ppid, process, machine) < 0)
			break;

		if (perf_event__synthesize_namespaces(tool, namespaces_event, _pid,
						      tgid, process, machine) < 0)
			break;

		/*
		 * Send the prepared comm event
		 */
		if (perf_tool__process_synth_event(tool, comm_event, machine, process) != 0)
			break;

		rc = 0;
		if (_pid == pid && !kernel_thread && needs_mmap) {
			/* process the parent's maps too */
			rc = perf_event__synthesize_mmap_events(tool, mmap_event, pid, tgid,
						process, machine, mmap_data);
			if (rc)
				break;
		}
	}

	for (i = 0; i < n; i++)
		zfree(&dirent[i]);
	free(dirent);

	return rc;
}

int perf_event__synthesize_thread_map(struct perf_tool *tool,
				      struct perf_thread_map *threads,
				      perf_event__handler_t process,
				      struct machine *machine,
				      bool needs_mmap, bool mmap_data)
{
	union perf_event *comm_event, *mmap_event, *fork_event;
	union perf_event *namespaces_event;
	int err = -1, thread, j;

	comm_event = malloc(sizeof(comm_event->comm) + machine->id_hdr_size);
	if (comm_event == NULL)
		goto out;

	mmap_event = malloc(sizeof(mmap_event->mmap2) + machine->id_hdr_size);
	if (mmap_event == NULL)
		goto out_free_comm;

	fork_event = malloc(sizeof(fork_event->fork) + machine->id_hdr_size);
	if (fork_event == NULL)
		goto out_free_mmap;

	namespaces_event = malloc(sizeof(namespaces_event->namespaces) +
				  (NR_NAMESPACES * sizeof(struct perf_ns_link_info)) +
				  machine->id_hdr_size);
	if (namespaces_event == NULL)
		goto out_free_fork;

	err = 0;
	for (thread = 0; thread < threads->nr; ++thread) {
		if (__event__synthesize_thread(comm_event, mmap_event,
					       fork_event, namespaces_event,
					       perf_thread_map__pid(threads, thread), 0,
					       process, tool, machine,
					       needs_mmap, mmap_data)) {
			err = -1;
			break;
		}

		/*
		 * comm.pid is set to thread group id by
		 * perf_event__synthesize_comm
		 */
		if ((int) comm_event->comm.pid != perf_thread_map__pid(threads, thread)) {
			bool need_leader = true;

			/* is thread group leader in thread_map? */
			for (j = 0; j < threads->nr; ++j) {
				if ((int) comm_event->comm.pid == perf_thread_map__pid(threads, j)) {
					need_leader = false;
					break;
				}
			}

			/* if not, generate events for it */
			if (need_leader &&
			    __event__synthesize_thread(comm_event, mmap_event,
						       fork_event, namespaces_event,
						       comm_event->comm.pid, 0,
						       process, tool, machine,
						       needs_mmap, mmap_data)) {
				err = -1;
				break;
			}
		}
	}
	free(namespaces_event);
out_free_fork:
	free(fork_event);
out_free_mmap:
	free(mmap_event);
out_free_comm:
	free(comm_event);
out:
	return err;
}

static int __perf_event__synthesize_threads(struct perf_tool *tool,
					    perf_event__handler_t process,
					    struct machine *machine,
					    bool needs_mmap,
					    bool mmap_data,
					    struct dirent **dirent,
					    int start,
					    int num)
{
	union perf_event *comm_event, *mmap_event, *fork_event;
	union perf_event *namespaces_event;
	int err = -1;
	char *end;
	pid_t pid;
	int i;

	comm_event = malloc(sizeof(comm_event->comm) + machine->id_hdr_size);
	if (comm_event == NULL)
		goto out;

	mmap_event = malloc(sizeof(mmap_event->mmap2) + machine->id_hdr_size);
	if (mmap_event == NULL)
		goto out_free_comm;

	fork_event = malloc(sizeof(fork_event->fork) + machine->id_hdr_size);
	if (fork_event == NULL)
		goto out_free_mmap;

	namespaces_event = malloc(sizeof(namespaces_event->namespaces) +
				  (NR_NAMESPACES * sizeof(struct perf_ns_link_info)) +
				  machine->id_hdr_size);
	if (namespaces_event == NULL)
		goto out_free_fork;

	for (i = start; i < start + num; i++) {
		if (!isdigit(dirent[i]->d_name[0]))
			continue;

		pid = (pid_t)strtol(dirent[i]->d_name, &end, 10);
		/* only interested in proper numerical dirents */
		if (*end)
			continue;
		/*
		 * We may race with exiting thread, so don't stop just because
		 * one thread couldn't be synthesized.
		 */
		__event__synthesize_thread(comm_event, mmap_event, fork_event,
					   namespaces_event, pid, 1, process,
					   tool, machine, needs_mmap, mmap_data);
	}
	err = 0;

	free(namespaces_event);
out_free_fork:
	free(fork_event);
out_free_mmap:
	free(mmap_event);
out_free_comm:
	free(comm_event);
out:
	return err;
}

struct synthesize_threads_arg {
	struct perf_tool *tool;
	perf_event__handler_t process;
	struct machine *machine;
	bool needs_mmap;
	bool mmap_data;
	struct dirent **dirent;
	int num;
	int start;
};

static void *synthesize_threads_worker(void *arg)
{
	struct synthesize_threads_arg *args = arg;

	__perf_event__synthesize_threads(args->tool, args->process,
					 args->machine,
					 args->needs_mmap, args->mmap_data,
					 args->dirent,
					 args->start, args->num);
	return NULL;
}

int perf_event__synthesize_threads(struct perf_tool *tool,
				   perf_event__handler_t process,
				   struct machine *machine,
				   bool needs_mmap, bool mmap_data,
				   unsigned int nr_threads_synthesize)
{
	struct synthesize_threads_arg *args = NULL;
	pthread_t *synthesize_threads = NULL;
	char proc_path[PATH_MAX];
	struct dirent **dirent;
	int num_per_thread;
	int m, n, i, j;
	int thread_nr;
	int base = 0;
	int err = -1;


	if (machine__is_default_guest(machine))
		return 0;

	snprintf(proc_path, sizeof(proc_path), "%s/proc", machine->root_dir);
	n = scandir(proc_path, &dirent, filter_task, NULL);
	if (n < 0)
		return err;

	if (nr_threads_synthesize == UINT_MAX)
		thread_nr = sysconf(_SC_NPROCESSORS_ONLN);
	else
		thread_nr = nr_threads_synthesize;

	if (thread_nr <= 1) {
		err = __perf_event__synthesize_threads(tool, process,
						       machine,
						       needs_mmap, mmap_data,
						       dirent, base, n);
		goto free_dirent;
	}
	if (thread_nr > n)
		thread_nr = n;

	synthesize_threads = calloc(sizeof(pthread_t), thread_nr);
	if (synthesize_threads == NULL)
		goto free_dirent;

	args = calloc(sizeof(*args), thread_nr);
	if (args == NULL)
		goto free_threads;

	num_per_thread = n / thread_nr;
	m = n % thread_nr;
	for (i = 0; i < thread_nr; i++) {
		args[i].tool = tool;
		args[i].process = process;
		args[i].machine = machine;
		args[i].needs_mmap = needs_mmap;
		args[i].mmap_data = mmap_data;
		args[i].dirent = dirent;
	}
	for (i = 0; i < m; i++) {
		args[i].num = num_per_thread + 1;
		args[i].start = i * args[i].num;
	}
	if (i != 0)
		base = args[i-1].start + args[i-1].num;
	for (j = i; j < thread_nr; j++) {
		args[j].num = num_per_thread;
		args[j].start = base + (j - i) * args[i].num;
	}

	for (i = 0; i < thread_nr; i++) {
		if (pthread_create(&synthesize_threads[i], NULL,
				   synthesize_threads_worker, &args[i]))
			goto out_join;
	}
	err = 0;
out_join:
	for (i = 0; i < thread_nr; i++)
		pthread_join(synthesize_threads[i], NULL);
	free(args);
free_threads:
	free(synthesize_threads);
free_dirent:
	for (i = 0; i < n; i++)
		zfree(&dirent[i]);
	free(dirent);

	return err;
}

int __weak perf_event__synthesize_extra_kmaps(struct perf_tool *tool __maybe_unused,
					      perf_event__handler_t process __maybe_unused,
					      struct machine *machine __maybe_unused)
{
	return 0;
}

static int __perf_event__synthesize_kernel_mmap(struct perf_tool *tool,
						perf_event__handler_t process,
						struct machine *machine)
{
	union perf_event *event;
	size_t size = symbol_conf.buildid_mmap2 ?
			sizeof(event->mmap2) : sizeof(event->mmap);
	struct map *map = machine__kernel_map(machine);
	struct kmap *kmap;
	int err;

	if (map == NULL)
		return -1;

	kmap = map__kmap(map);
	if (!kmap->ref_reloc_sym)
		return -1;

	/*
	 * We should get this from /sys/kernel/sections/.text, but till that is
	 * available use this, and after it is use this as a fallback for older
	 * kernels.
	 */
	event = zalloc(size + machine->id_hdr_size);
	if (event == NULL) {
		pr_debug("Not enough memory synthesizing mmap event "
			 "for kernel modules\n");
		return -1;
	}

	if (machine__is_host(machine)) {
		/*
		 * kernel uses PERF_RECORD_MISC_USER for user space maps,
		 * see kernel/perf_event.c __perf_event_mmap
		 */
		event->header.misc = PERF_RECORD_MISC_KERNEL;
	} else {
		event->header.misc = PERF_RECORD_MISC_GUEST_KERNEL;
	}

	if (symbol_conf.buildid_mmap2) {
		size = snprintf(event->mmap2.filename, sizeof(event->mmap2.filename),
				"%s%s", machine->mmap_name, kmap->ref_reloc_sym->name) + 1;
		size = PERF_ALIGN(size, sizeof(u64));
		event->mmap2.header.type = PERF_RECORD_MMAP2;
		event->mmap2.header.size = (sizeof(event->mmap2) -
				(sizeof(event->mmap2.filename) - size) + machine->id_hdr_size);
		event->mmap2.pgoff = kmap->ref_reloc_sym->addr;
		event->mmap2.start = map__start(map);
		event->mmap2.len   = map__end(map) - event->mmap.start;
		event->mmap2.pid   = machine->pid;

		perf_record_mmap2__read_build_id(&event->mmap2, machine, true);
	} else {
		size = snprintf(event->mmap.filename, sizeof(event->mmap.filename),
				"%s%s", machine->mmap_name, kmap->ref_reloc_sym->name) + 1;
		size = PERF_ALIGN(size, sizeof(u64));
		event->mmap.header.type = PERF_RECORD_MMAP;
		event->mmap.header.size = (sizeof(event->mmap) -
				(sizeof(event->mmap.filename) - size) + machine->id_hdr_size);
		event->mmap.pgoff = kmap->ref_reloc_sym->addr;
		event->mmap.start = map__start(map);
		event->mmap.len   = map__end(map) - event->mmap.start;
		event->mmap.pid   = machine->pid;
	}

	err = perf_tool__process_synth_event(tool, event, machine, process);
	free(event);

	return err;
}

int perf_event__synthesize_kernel_mmap(struct perf_tool *tool,
				       perf_event__handler_t process,
				       struct machine *machine)
{
	int err;

	err = __perf_event__synthesize_kernel_mmap(tool, process, machine);
	if (err < 0)
		return err;

	return perf_event__synthesize_extra_kmaps(tool, process, machine);
}

int perf_event__synthesize_thread_map2(struct perf_tool *tool,
				      struct perf_thread_map *threads,
				      perf_event__handler_t process,
				      struct machine *machine)
{
	union perf_event *event;
	int i, err, size;

	size  = sizeof(event->thread_map);
	size +=	threads->nr * sizeof(event->thread_map.entries[0]);

	event = zalloc(size);
	if (!event)
		return -ENOMEM;

	event->header.type = PERF_RECORD_THREAD_MAP;
	event->header.size = size;
	event->thread_map.nr = threads->nr;

	for (i = 0; i < threads->nr; i++) {
		struct perf_record_thread_map_entry *entry = &event->thread_map.entries[i];
		char *comm = perf_thread_map__comm(threads, i);

		if (!comm)
			comm = (char *) "";

		entry->pid = perf_thread_map__pid(threads, i);
		strncpy((char *) &entry->comm, comm, sizeof(entry->comm));
	}

	err = process(tool, event, NULL, machine);

	free(event);
	return err;
}

struct synthesize_cpu_map_data {
	const struct perf_cpu_map *map;
	int nr;
	int min_cpu;
	int max_cpu;
	int has_any_cpu;
	int type;
	size_t size;
	struct perf_record_cpu_map_data *data;
};

static void synthesize_cpus(struct synthesize_cpu_map_data *data)
{
	data->data->type = PERF_CPU_MAP__CPUS;
	data->data->cpus_data.nr = data->nr;
	for (int i = 0; i < data->nr; i++)
		data->data->cpus_data.cpu[i] = perf_cpu_map__cpu(data->map, i).cpu;
}

static void synthesize_mask(struct synthesize_cpu_map_data *data)
{
	int idx;
	struct perf_cpu cpu;

	/* Due to padding, the 4bytes per entry mask variant is always smaller. */
	data->data->type = PERF_CPU_MAP__MASK;
	data->data->mask32_data.nr = BITS_TO_U32(data->max_cpu);
	data->data->mask32_data.long_size = 4;

	perf_cpu_map__for_each_cpu(cpu, idx, data->map) {
		int bit_word = cpu.cpu / 32;
		u32 bit_mask = 1U << (cpu.cpu & 31);

		data->data->mask32_data.mask[bit_word] |= bit_mask;
	}
}

static void synthesize_range_cpus(struct synthesize_cpu_map_data *data)
{
	data->data->type = PERF_CPU_MAP__RANGE_CPUS;
	data->data->range_cpu_data.any_cpu = data->has_any_cpu;
	data->data->range_cpu_data.start_cpu = data->min_cpu;
	data->data->range_cpu_data.end_cpu = data->max_cpu;
}

static void *cpu_map_data__alloc(struct synthesize_cpu_map_data *syn_data,
				 size_t header_size)
{
	size_t size_cpus, size_mask;

	syn_data->nr = perf_cpu_map__nr(syn_data->map);
	syn_data->has_any_cpu = (perf_cpu_map__cpu(syn_data->map, 0).cpu == -1) ? 1 : 0;

	syn_data->min_cpu = perf_cpu_map__cpu(syn_data->map, syn_data->has_any_cpu).cpu;
	syn_data->max_cpu = perf_cpu_map__max(syn_data->map).cpu;
	if (syn_data->max_cpu - syn_data->min_cpu + 1 == syn_data->nr - syn_data->has_any_cpu) {
		/* A consecutive range of CPUs can be encoded using a range. */
		assert(sizeof(u16) + sizeof(struct perf_record_range_cpu_map) == sizeof(u64));
		syn_data->type = PERF_CPU_MAP__RANGE_CPUS;
		syn_data->size = header_size + sizeof(u64);
		return zalloc(syn_data->size);
	}

	size_cpus = sizeof(u16) + sizeof(struct cpu_map_entries) + syn_data->nr * sizeof(u16);
	/* Due to padding, the 4bytes per entry mask variant is always smaller. */
	size_mask = sizeof(u16) + sizeof(struct perf_record_mask_cpu_map32) +
		BITS_TO_U32(syn_data->max_cpu) * sizeof(__u32);
	if (syn_data->has_any_cpu || size_cpus < size_mask) {
		/* Follow the CPU map encoding. */
		syn_data->type = PERF_CPU_MAP__CPUS;
		syn_data->size = header_size + PERF_ALIGN(size_cpus, sizeof(u64));
		return zalloc(syn_data->size);
	}
	/* Encode using a bitmask. */
	syn_data->type = PERF_CPU_MAP__MASK;
	syn_data->size = header_size + PERF_ALIGN(size_mask, sizeof(u64));
	return zalloc(syn_data->size);
}

static void cpu_map_data__synthesize(struct synthesize_cpu_map_data *data)
{
	switch (data->type) {
	case PERF_CPU_MAP__CPUS:
		synthesize_cpus(data);
		break;
	case PERF_CPU_MAP__MASK:
		synthesize_mask(data);
		break;
	case PERF_CPU_MAP__RANGE_CPUS:
		synthesize_range_cpus(data);
		break;
	default:
		break;
	}
}

static struct perf_record_cpu_map *cpu_map_event__new(const struct perf_cpu_map *map)
{
	struct synthesize_cpu_map_data syn_data = { .map = map };
	struct perf_record_cpu_map *event;


	event = cpu_map_data__alloc(&syn_data, sizeof(struct perf_event_header));
	if (!event)
		return NULL;

	syn_data.data = &event->data;
	event->header.type = PERF_RECORD_CPU_MAP;
	event->header.size = syn_data.size;
	cpu_map_data__synthesize(&syn_data);
	return event;
}


int perf_event__synthesize_cpu_map(struct perf_tool *tool,
				   const struct perf_cpu_map *map,
				   perf_event__handler_t process,
				   struct machine *machine)
{
	struct perf_record_cpu_map *event;
	int err;

	event = cpu_map_event__new(map);
	if (!event)
		return -ENOMEM;

	err = process(tool, (union perf_event *) event, NULL, machine);

	free(event);
	return err;
}

int perf_event__synthesize_stat_config(struct perf_tool *tool,
				       struct perf_stat_config *config,
				       perf_event__handler_t process,
				       struct machine *machine)
{
	struct perf_record_stat_config *event;
	int size, i = 0, err;

	size  = sizeof(*event);
	size += (PERF_STAT_CONFIG_TERM__MAX * sizeof(event->data[0]));

	event = zalloc(size);
	if (!event)
		return -ENOMEM;

	event->header.type = PERF_RECORD_STAT_CONFIG;
	event->header.size = size;
	event->nr          = PERF_STAT_CONFIG_TERM__MAX;

#define ADD(__term, __val)					\
	event->data[i].tag = PERF_STAT_CONFIG_TERM__##__term;	\
	event->data[i].val = __val;				\
	i++;

	ADD(AGGR_MODE,	config->aggr_mode)
	ADD(INTERVAL,	config->interval)
	ADD(SCALE,	config->scale)
	ADD(AGGR_LEVEL,	config->aggr_level)

	WARN_ONCE(i != PERF_STAT_CONFIG_TERM__MAX,
		  "stat config terms unbalanced\n");
#undef ADD

	err = process(tool, (union perf_event *) event, NULL, machine);

	free(event);
	return err;
}

int perf_event__synthesize_stat(struct perf_tool *tool,
				struct perf_cpu cpu, u32 thread, u64 id,
				struct perf_counts_values *count,
				perf_event__handler_t process,
				struct machine *machine)
{
	struct perf_record_stat event;

	event.header.type = PERF_RECORD_STAT;
	event.header.size = sizeof(event);
	event.header.misc = 0;

	event.id        = id;
	event.cpu       = cpu.cpu;
	event.thread    = thread;
	event.val       = count->val;
	event.ena       = count->ena;
	event.run       = count->run;

	return process(tool, (union perf_event *) &event, NULL, machine);
}

int perf_event__synthesize_stat_round(struct perf_tool *tool,
				      u64 evtime, u64 type,
				      perf_event__handler_t process,
				      struct machine *machine)
{
	struct perf_record_stat_round event;

	event.header.type = PERF_RECORD_STAT_ROUND;
	event.header.size = sizeof(event);
	event.header.misc = 0;

	event.time = evtime;
	event.type = type;

	return process(tool, (union perf_event *) &event, NULL, machine);
}

size_t perf_event__sample_event_size(const struct perf_sample *sample, u64 type, u64 read_format)
{
	size_t sz, result = sizeof(struct perf_record_sample);

	if (type & PERF_SAMPLE_IDENTIFIER)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_IP)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_TID)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_TIME)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_ADDR)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_ID)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_STREAM_ID)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_CPU)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_PERIOD)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_READ) {
		result += sizeof(u64);
		if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
			result += sizeof(u64);
		if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
			result += sizeof(u64);
		/* PERF_FORMAT_ID is forced for PERF_SAMPLE_READ */
		if (read_format & PERF_FORMAT_GROUP) {
			sz = sample_read_value_size(read_format);
			result += sz * sample->read.group.nr;
		} else {
			result += sizeof(u64);
			if (read_format & PERF_FORMAT_LOST)
				result += sizeof(u64);
		}
	}

	if (type & PERF_SAMPLE_CALLCHAIN) {
		sz = (sample->callchain->nr + 1) * sizeof(u64);
		result += sz;
	}

	if (type & PERF_SAMPLE_RAW) {
		result += sizeof(u32);
		result += sample->raw_size;
	}

	if (type & PERF_SAMPLE_BRANCH_STACK) {
		sz = sample->branch_stack->nr * sizeof(struct branch_entry);
		/* nr, hw_idx */
		sz += 2 * sizeof(u64);
		result += sz;
	}

	if (type & PERF_SAMPLE_REGS_USER) {
		if (sample->user_regs.abi) {
			result += sizeof(u64);
			sz = hweight64(sample->user_regs.mask) * sizeof(u64);
			result += sz;
		} else {
			result += sizeof(u64);
		}
	}

	if (type & PERF_SAMPLE_STACK_USER) {
		sz = sample->user_stack.size;
		result += sizeof(u64);
		if (sz) {
			result += sz;
			result += sizeof(u64);
		}
	}

	if (type & PERF_SAMPLE_WEIGHT_TYPE)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_DATA_SRC)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_TRANSACTION)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_REGS_INTR) {
		if (sample->intr_regs.abi) {
			result += sizeof(u64);
			sz = hweight64(sample->intr_regs.mask) * sizeof(u64);
			result += sz;
		} else {
			result += sizeof(u64);
		}
	}

	if (type & PERF_SAMPLE_PHYS_ADDR)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_CGROUP)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_DATA_PAGE_SIZE)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_CODE_PAGE_SIZE)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_AUX) {
		result += sizeof(u64);
		result += sample->aux_sample.size;
	}

	return result;
}

void __weak arch_perf_synthesize_sample_weight(const struct perf_sample *data,
					       __u64 *array, u64 type __maybe_unused)
{
	*array = data->weight;
}

static __u64 *copy_read_group_values(__u64 *array, __u64 read_format,
				     const struct perf_sample *sample)
{
	size_t sz = sample_read_value_size(read_format);
	struct sample_read_value *v = sample->read.group.values;

	sample_read_group__for_each(v, sample->read.group.nr, read_format) {
		/* PERF_FORMAT_ID is forced for PERF_SAMPLE_READ */
		memcpy(array, v, sz);
		array = (void *)array + sz;
	}
	return array;
}

int perf_event__synthesize_sample(union perf_event *event, u64 type, u64 read_format,
				  const struct perf_sample *sample)
{
	__u64 *array;
	size_t sz;
	/*
	 * used for cross-endian analysis. See git commit 65014ab3
	 * for why this goofiness is needed.
	 */
	union u64_swap u;

	array = event->sample.array;

	if (type & PERF_SAMPLE_IDENTIFIER) {
		*array = sample->id;
		array++;
	}

	if (type & PERF_SAMPLE_IP) {
		*array = sample->ip;
		array++;
	}

	if (type & PERF_SAMPLE_TID) {
		u.val32[0] = sample->pid;
		u.val32[1] = sample->tid;
		*array = u.val64;
		array++;
	}

	if (type & PERF_SAMPLE_TIME) {
		*array = sample->time;
		array++;
	}

	if (type & PERF_SAMPLE_ADDR) {
		*array = sample->addr;
		array++;
	}

	if (type & PERF_SAMPLE_ID) {
		*array = sample->id;
		array++;
	}

	if (type & PERF_SAMPLE_STREAM_ID) {
		*array = sample->stream_id;
		array++;
	}

	if (type & PERF_SAMPLE_CPU) {
		u.val32[0] = sample->cpu;
		u.val32[1] = 0;
		*array = u.val64;
		array++;
	}

	if (type & PERF_SAMPLE_PERIOD) {
		*array = sample->period;
		array++;
	}

	if (type & PERF_SAMPLE_READ) {
		if (read_format & PERF_FORMAT_GROUP)
			*array = sample->read.group.nr;
		else
			*array = sample->read.one.value;
		array++;

		if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
			*array = sample->read.time_enabled;
			array++;
		}

		if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
			*array = sample->read.time_running;
			array++;
		}

		/* PERF_FORMAT_ID is forced for PERF_SAMPLE_READ */
		if (read_format & PERF_FORMAT_GROUP) {
			array = copy_read_group_values(array, read_format,
						       sample);
		} else {
			*array = sample->read.one.id;
			array++;

			if (read_format & PERF_FORMAT_LOST) {
				*array = sample->read.one.lost;
				array++;
			}
		}
	}

	if (type & PERF_SAMPLE_CALLCHAIN) {
		sz = (sample->callchain->nr + 1) * sizeof(u64);
		memcpy(array, sample->callchain, sz);
		array = (void *)array + sz;
	}

	if (type & PERF_SAMPLE_RAW) {
		u.val32[0] = sample->raw_size;
		*array = u.val64;
		array = (void *)array + sizeof(u32);

		memcpy(array, sample->raw_data, sample->raw_size);
		array = (void *)array + sample->raw_size;
	}

	if (type & PERF_SAMPLE_BRANCH_STACK) {
		sz = sample->branch_stack->nr * sizeof(struct branch_entry);
		/* nr, hw_idx */
		sz += 2 * sizeof(u64);
		memcpy(array, sample->branch_stack, sz);
		array = (void *)array + sz;
	}

	if (type & PERF_SAMPLE_REGS_USER) {
		if (sample->user_regs.abi) {
			*array++ = sample->user_regs.abi;
			sz = hweight64(sample->user_regs.mask) * sizeof(u64);
			memcpy(array, sample->user_regs.regs, sz);
			array = (void *)array + sz;
		} else {
			*array++ = 0;
		}
	}

	if (type & PERF_SAMPLE_STACK_USER) {
		sz = sample->user_stack.size;
		*array++ = sz;
		if (sz) {
			memcpy(array, sample->user_stack.data, sz);
			array = (void *)array + sz;
			*array++ = sz;
		}
	}

	if (type & PERF_SAMPLE_WEIGHT_TYPE) {
		arch_perf_synthesize_sample_weight(sample, array, type);
		array++;
	}

	if (type & PERF_SAMPLE_DATA_SRC) {
		*array = sample->data_src;
		array++;
	}

	if (type & PERF_SAMPLE_TRANSACTION) {
		*array = sample->transaction;
		array++;
	}

	if (type & PERF_SAMPLE_REGS_INTR) {
		if (sample->intr_regs.abi) {
			*array++ = sample->intr_regs.abi;
			sz = hweight64(sample->intr_regs.mask) * sizeof(u64);
			memcpy(array, sample->intr_regs.regs, sz);
			array = (void *)array + sz;
		} else {
			*array++ = 0;
		}
	}

	if (type & PERF_SAMPLE_PHYS_ADDR) {
		*array = sample->phys_addr;
		array++;
	}

	if (type & PERF_SAMPLE_CGROUP) {
		*array = sample->cgroup;
		array++;
	}

	if (type & PERF_SAMPLE_DATA_PAGE_SIZE) {
		*array = sample->data_page_size;
		array++;
	}

	if (type & PERF_SAMPLE_CODE_PAGE_SIZE) {
		*array = sample->code_page_size;
		array++;
	}

	if (type & PERF_SAMPLE_AUX) {
		sz = sample->aux_sample.size;
		*array++ = sz;
		memcpy(array, sample->aux_sample.data, sz);
		array = (void *)array + sz;
	}

	return 0;
}

int perf_event__synthesize_id_sample(__u64 *array, u64 type, const struct perf_sample *sample)
{
	__u64 *start = array;

	/*
	 * used for cross-endian analysis. See git commit 65014ab3
	 * for why this goofiness is needed.
	 */
	union u64_swap u;

	if (type & PERF_SAMPLE_TID) {
		u.val32[0] = sample->pid;
		u.val32[1] = sample->tid;
		*array = u.val64;
		array++;
	}

	if (type & PERF_SAMPLE_TIME) {
		*array = sample->time;
		array++;
	}

	if (type & PERF_SAMPLE_ID) {
		*array = sample->id;
		array++;
	}

	if (type & PERF_SAMPLE_STREAM_ID) {
		*array = sample->stream_id;
		array++;
	}

	if (type & PERF_SAMPLE_CPU) {
		u.val32[0] = sample->cpu;
		u.val32[1] = 0;
		*array = u.val64;
		array++;
	}

	if (type & PERF_SAMPLE_IDENTIFIER) {
		*array = sample->id;
		array++;
	}

	return (void *)array - (void *)start;
}

int __perf_event__synthesize_id_index(struct perf_tool *tool, perf_event__handler_t process,
				      struct evlist *evlist, struct machine *machine, size_t from)
{
	union perf_event *ev;
	struct evsel *evsel;
	size_t nr = 0, i = 0, sz, max_nr, n, pos;
	size_t e1_sz = sizeof(struct id_index_entry);
	size_t e2_sz = sizeof(struct id_index_entry_2);
	size_t etot_sz = e1_sz + e2_sz;
	bool e2_needed = false;
	int err;

	max_nr = (UINT16_MAX - sizeof(struct perf_record_id_index)) / etot_sz;

	pos = 0;
	evlist__for_each_entry(evlist, evsel) {
		if (pos++ < from)
			continue;
		nr += evsel->core.ids;
	}

	if (!nr)
		return 0;

	pr_debug2("Synthesizing id index\n");

	n = nr > max_nr ? max_nr : nr;
	sz = sizeof(struct perf_record_id_index) + n * etot_sz;
	ev = zalloc(sz);
	if (!ev)
		return -ENOMEM;

	sz = sizeof(struct perf_record_id_index) + n * e1_sz;

	ev->id_index.header.type = PERF_RECORD_ID_INDEX;
	ev->id_index.nr = n;

	pos = 0;
	evlist__for_each_entry(evlist, evsel) {
		u32 j;

		if (pos++ < from)
			continue;
		for (j = 0; j < evsel->core.ids; j++, i++) {
			struct id_index_entry *e;
			struct id_index_entry_2 *e2;
			struct perf_sample_id *sid;

			if (i >= n) {
				ev->id_index.header.size = sz + (e2_needed ? n * e2_sz : 0);
				err = process(tool, ev, NULL, machine);
				if (err)
					goto out_err;
				nr -= n;
				i = 0;
				e2_needed = false;
			}

			e = &ev->id_index.entries[i];

			e->id = evsel->core.id[j];

			sid = evlist__id2sid(evlist, e->id);
			if (!sid) {
				free(ev);
				return -ENOENT;
			}

			e->idx = sid->idx;
			e->cpu = sid->cpu.cpu;
			e->tid = sid->tid;

			if (sid->machine_pid)
				e2_needed = true;

			e2 = (void *)ev + sz;
			e2[i].machine_pid = sid->machine_pid;
			e2[i].vcpu        = sid->vcpu.cpu;
		}
	}

	sz = sizeof(struct perf_record_id_index) + nr * e1_sz;
	ev->id_index.header.size = sz + (e2_needed ? nr * e2_sz : 0);
	ev->id_index.nr = nr;

	err = process(tool, ev, NULL, machine);
out_err:
	free(ev);

	return err;
}

int perf_event__synthesize_id_index(struct perf_tool *tool, perf_event__handler_t process,
				    struct evlist *evlist, struct machine *machine)
{
	return __perf_event__synthesize_id_index(tool, process, evlist, machine, 0);
}

int __machine__synthesize_threads(struct machine *machine, struct perf_tool *tool,
				  struct target *target, struct perf_thread_map *threads,
				  perf_event__handler_t process, bool needs_mmap,
				  bool data_mmap, unsigned int nr_threads_synthesize)
{
	/*
	 * When perf runs in non-root PID namespace, and the namespace's proc FS
	 * is not mounted, nsinfo__is_in_root_namespace() returns false.
	 * In this case, the proc FS is coming for the parent namespace, thus
	 * perf tool will wrongly gather process info from its parent PID
	 * namespace.
	 *
	 * To avoid the confusion that the perf tool runs in a child PID
	 * namespace but it synthesizes thread info from its parent PID
	 * namespace, returns failure with warning.
	 */
	if (!nsinfo__is_in_root_namespace()) {
		pr_err("Perf runs in non-root PID namespace but it tries to ");
		pr_err("gather process info from its parent PID namespace.\n");
		pr_err("Please mount the proc file system properly, e.g. ");
		pr_err("add the option '--mount-proc' for unshare command.\n");
		return -EPERM;
	}

	if (target__has_task(target))
		return perf_event__synthesize_thread_map(tool, threads, process, machine,
							 needs_mmap, data_mmap);
	else if (target__has_cpu(target))
		return perf_event__synthesize_threads(tool, process, machine,
						      needs_mmap, data_mmap,
						      nr_threads_synthesize);
	/* command specified */
	return 0;
}

int machine__synthesize_threads(struct machine *machine, struct target *target,
				struct perf_thread_map *threads, bool needs_mmap,
				bool data_mmap, unsigned int nr_threads_synthesize)
{
	return __machine__synthesize_threads(machine, NULL, target, threads,
					     perf_event__process, needs_mmap,
					     data_mmap, nr_threads_synthesize);
}

static struct perf_record_event_update *event_update_event__new(size_t size, u64 type, u64 id)
{
	struct perf_record_event_update *ev;

	size += sizeof(*ev);
	size  = PERF_ALIGN(size, sizeof(u64));

	ev = zalloc(size);
	if (ev) {
		ev->header.type = PERF_RECORD_EVENT_UPDATE;
		ev->header.size = (u16)size;
		ev->type	= type;
		ev->id		= id;
	}
	return ev;
}

int perf_event__synthesize_event_update_unit(struct perf_tool *tool, struct evsel *evsel,
					     perf_event__handler_t process)
{
	size_t size = strlen(evsel->unit);
	struct perf_record_event_update *ev;
	int err;

	ev = event_update_event__new(size + 1, PERF_EVENT_UPDATE__UNIT, evsel->core.id[0]);
	if (ev == NULL)
		return -ENOMEM;

	strlcpy(ev->unit, evsel->unit, size + 1);
	err = process(tool, (union perf_event *)ev, NULL, NULL);
	free(ev);
	return err;
}

int perf_event__synthesize_event_update_scale(struct perf_tool *tool, struct evsel *evsel,
					      perf_event__handler_t process)
{
	struct perf_record_event_update *ev;
	struct perf_record_event_update_scale *ev_data;
	int err;

	ev = event_update_event__new(sizeof(*ev_data), PERF_EVENT_UPDATE__SCALE, evsel->core.id[0]);
	if (ev == NULL)
		return -ENOMEM;

	ev->scale.scale = evsel->scale;
	err = process(tool, (union perf_event *)ev, NULL, NULL);
	free(ev);
	return err;
}

int perf_event__synthesize_event_update_name(struct perf_tool *tool, struct evsel *evsel,
					     perf_event__handler_t process)
{
	struct perf_record_event_update *ev;
	size_t len = strlen(evsel__name(evsel));
	int err;

	ev = event_update_event__new(len + 1, PERF_EVENT_UPDATE__NAME, evsel->core.id[0]);
	if (ev == NULL)
		return -ENOMEM;

	strlcpy(ev->name, evsel->name, len + 1);
	err = process(tool, (union perf_event *)ev, NULL, NULL);
	free(ev);
	return err;
}

int perf_event__synthesize_event_update_cpus(struct perf_tool *tool, struct evsel *evsel,
					     perf_event__handler_t process)
{
	struct synthesize_cpu_map_data syn_data = { .map = evsel->core.own_cpus };
	struct perf_record_event_update *ev;
	int err;

	ev = cpu_map_data__alloc(&syn_data, sizeof(struct perf_event_header) + 2 * sizeof(u64));
	if (!ev)
		return -ENOMEM;

	syn_data.data = &ev->cpus.cpus;
	ev->header.type = PERF_RECORD_EVENT_UPDATE;
	ev->header.size = (u16)syn_data.size;
	ev->type	= PERF_EVENT_UPDATE__CPUS;
	ev->id		= evsel->core.id[0];
	cpu_map_data__synthesize(&syn_data);

	err = process(tool, (union perf_event *)ev, NULL, NULL);
	free(ev);
	return err;
}

int perf_event__synthesize_attrs(struct perf_tool *tool, struct evlist *evlist,
				 perf_event__handler_t process)
{
	struct evsel *evsel;
	int err = 0;

	evlist__for_each_entry(evlist, evsel) {
		err = perf_event__synthesize_attr(tool, &evsel->core.attr, evsel->core.ids,
						  evsel->core.id, process);
		if (err) {
			pr_debug("failed to create perf header attribute\n");
			return err;
		}
	}

	return err;
}

static bool has_unit(struct evsel *evsel)
{
	return evsel->unit && *evsel->unit;
}

static bool has_scale(struct evsel *evsel)
{
	return evsel->scale != 1;
}

int perf_event__synthesize_extra_attr(struct perf_tool *tool, struct evlist *evsel_list,
				      perf_event__handler_t process, bool is_pipe)
{
	struct evsel *evsel;
	int err;

	/*
	 * Synthesize other events stuff not carried within
	 * attr event - unit, scale, name
	 */
	evlist__for_each_entry(evsel_list, evsel) {
		if (!evsel->supported)
			continue;

		/*
		 * Synthesize unit and scale only if it's defined.
		 */
		if (has_unit(evsel)) {
			err = perf_event__synthesize_event_update_unit(tool, evsel, process);
			if (err < 0) {
				pr_err("Couldn't synthesize evsel unit.\n");
				return err;
			}
		}

		if (has_scale(evsel)) {
			err = perf_event__synthesize_event_update_scale(tool, evsel, process);
			if (err < 0) {
				pr_err("Couldn't synthesize evsel evsel.\n");
				return err;
			}
		}

		if (evsel->core.own_cpus) {
			err = perf_event__synthesize_event_update_cpus(tool, evsel, process);
			if (err < 0) {
				pr_err("Couldn't synthesize evsel cpus.\n");
				return err;
			}
		}

		/*
		 * Name is needed only for pipe output,
		 * perf.data carries event names.
		 */
		if (is_pipe) {
			err = perf_event__synthesize_event_update_name(tool, evsel, process);
			if (err < 0) {
				pr_err("Couldn't synthesize evsel name.\n");
				return err;
			}
		}
	}
	return 0;
}

int perf_event__synthesize_attr(struct perf_tool *tool, struct perf_event_attr *attr,
				u32 ids, u64 *id, perf_event__handler_t process)
{
	union perf_event *ev;
	size_t size;
	int err;

	size = sizeof(struct perf_event_attr);
	size = PERF_ALIGN(size, sizeof(u64));
	size += sizeof(struct perf_event_header);
	size += ids * sizeof(u64);

	ev = zalloc(size);

	if (ev == NULL)
		return -ENOMEM;

	ev->attr.attr = *attr;
	memcpy(perf_record_header_attr_id(ev), id, ids * sizeof(u64));

	ev->attr.header.type = PERF_RECORD_HEADER_ATTR;
	ev->attr.header.size = (u16)size;

	if (ev->attr.header.size == size)
		err = process(tool, ev, NULL, NULL);
	else
		err = -E2BIG;

	free(ev);

	return err;
}

#ifdef HAVE_LIBTRACEEVENT
int perf_event__synthesize_tracing_data(struct perf_tool *tool, int fd, struct evlist *evlist,
					perf_event__handler_t process)
{
	union perf_event ev;
	struct tracing_data *tdata;
	ssize_t size = 0, aligned_size = 0, padding;
	struct feat_fd ff;

	/*
	 * We are going to store the size of the data followed
	 * by the data contents. Since the fd descriptor is a pipe,
	 * we cannot seek back to store the size of the data once
	 * we know it. Instead we:
	 *
	 * - write the tracing data to the temp file
	 * - get/write the data size to pipe
	 * - write the tracing data from the temp file
	 *   to the pipe
	 */
	tdata = tracing_data_get(&evlist->core.entries, fd, true);
	if (!tdata)
		return -1;

	memset(&ev, 0, sizeof(ev));

	ev.tracing_data.header.type = PERF_RECORD_HEADER_TRACING_DATA;
	size = tdata->size;
	aligned_size = PERF_ALIGN(size, sizeof(u64));
	padding = aligned_size - size;
	ev.tracing_data.header.size = sizeof(ev.tracing_data);
	ev.tracing_data.size = aligned_size;

	process(tool, &ev, NULL, NULL);

	/*
	 * The put function will copy all the tracing data
	 * stored in temp file to the pipe.
	 */
	tracing_data_put(tdata);

	ff = (struct feat_fd){ .fd = fd };
	if (write_padded(&ff, NULL, 0, padding))
		return -1;

	return aligned_size;
}
#endif

int perf_event__synthesize_build_id(struct perf_tool *tool, struct dso *pos, u16 misc,
				    perf_event__handler_t process, struct machine *machine)
{
	union perf_event ev;
	size_t len;

	if (!pos->hit)
		return 0;

	memset(&ev, 0, sizeof(ev));

	len = pos->long_name_len + 1;
	len = PERF_ALIGN(len, NAME_ALIGN);
	ev.build_id.size = min(pos->bid.size, sizeof(pos->bid.data));
	memcpy(&ev.build_id.build_id, pos->bid.data, ev.build_id.size);
	ev.build_id.header.type = PERF_RECORD_HEADER_BUILD_ID;
	ev.build_id.header.misc = misc | PERF_RECORD_MISC_BUILD_ID_SIZE;
	ev.build_id.pid = machine->pid;
	ev.build_id.header.size = sizeof(ev.build_id) + len;
	memcpy(&ev.build_id.filename, pos->long_name, pos->long_name_len);

	return process(tool, &ev, NULL, machine);
}

int perf_event__synthesize_stat_events(struct perf_stat_config *config, struct perf_tool *tool,
				       struct evlist *evlist, perf_event__handler_t process, bool attrs)
{
	int err;

	if (attrs) {
		err = perf_event__synthesize_attrs(tool, evlist, process);
		if (err < 0) {
			pr_err("Couldn't synthesize attrs.\n");
			return err;
		}
	}

	err = perf_event__synthesize_extra_attr(tool, evlist, process, attrs);
	err = perf_event__synthesize_thread_map2(tool, evlist->core.threads, process, NULL);
	if (err < 0) {
		pr_err("Couldn't synthesize thread map.\n");
		return err;
	}

	err = perf_event__synthesize_cpu_map(tool, evlist->core.user_requested_cpus, process, NULL);
	if (err < 0) {
		pr_err("Couldn't synthesize thread map.\n");
		return err;
	}

	err = perf_event__synthesize_stat_config(tool, config, process, NULL);
	if (err < 0) {
		pr_err("Couldn't synthesize config.\n");
		return err;
	}

	return 0;
}

extern const struct perf_header_feature_ops feat_ops[HEADER_LAST_FEATURE];

int perf_event__synthesize_features(struct perf_tool *tool, struct perf_session *session,
				    struct evlist *evlist, perf_event__handler_t process)
{
	struct perf_header *header = &session->header;
	struct perf_record_header_feature *fe;
	struct feat_fd ff;
	size_t sz, sz_hdr;
	int feat, ret;

	sz_hdr = sizeof(fe->header);
	sz = sizeof(union perf_event);
	/* get a nice alignment */
	sz = PERF_ALIGN(sz, page_size);

	memset(&ff, 0, sizeof(ff));

	ff.buf = malloc(sz);
	if (!ff.buf)
		return -ENOMEM;

	ff.size = sz - sz_hdr;
	ff.ph = &session->header;

	for_each_set_bit(feat, header->adds_features, HEADER_FEAT_BITS) {
		if (!feat_ops[feat].synthesize) {
			pr_debug("No record header feature for header :%d\n", feat);
			continue;
		}

		ff.offset = sizeof(*fe);

		ret = feat_ops[feat].write(&ff, evlist);
		if (ret || ff.offset <= (ssize_t)sizeof(*fe)) {
			pr_debug("Error writing feature\n");
			continue;
		}
		/* ff.buf may have changed due to realloc in do_write() */
		fe = ff.buf;
		memset(fe, 0, sizeof(*fe));

		fe->feat_id = feat;
		fe->header.type = PERF_RECORD_HEADER_FEATURE;
		fe->header.size = ff.offset;

		ret = process(tool, ff.buf, NULL, NULL);
		if (ret) {
			free(ff.buf);
			return ret;
		}
	}

	/* Send HEADER_LAST_FEATURE mark. */
	fe = ff.buf;
	fe->feat_id     = HEADER_LAST_FEATURE;
	fe->header.type = PERF_RECORD_HEADER_FEATURE;
	fe->header.size = sizeof(*fe);

	ret = process(tool, ff.buf, NULL, NULL);

	free(ff.buf);
	return ret;
}

int perf_event__synthesize_for_pipe(struct perf_tool *tool,
				    struct perf_session *session,
				    struct perf_data *data,
				    perf_event__handler_t process)
{
	int err;
	int ret = 0;
	struct evlist *evlist = session->evlist;

	/*
	 * We need to synthesize events first, because some
	 * features works on top of them (on report side).
	 */
	err = perf_event__synthesize_attrs(tool, evlist, process);
	if (err < 0) {
		pr_err("Couldn't synthesize attrs.\n");
		return err;
	}
	ret += err;

	err = perf_event__synthesize_features(tool, session, evlist, process);
	if (err < 0) {
		pr_err("Couldn't synthesize features.\n");
		return err;
	}
	ret += err;

#ifdef HAVE_LIBTRACEEVENT
	if (have_tracepoints(&evlist->core.entries)) {
		int fd = perf_data__fd(data);

		/*
		 * FIXME err <= 0 here actually means that
		 * there were no tracepoints so its not really
		 * an error, just that we don't need to
		 * synthesize anything.  We really have to
		 * return this more properly and also
		 * propagate errors that now are calling die()
		 */
		err = perf_event__synthesize_tracing_data(tool,	fd, evlist,
							  process);
		if (err <= 0) {
			pr_err("Couldn't record tracing data.\n");
			return err;
		}
		ret += err;
	}
#else
	(void)data;
#endif

	return ret;
}

int parse_synth_opt(char *synth)
{
	char *p, *q;
	int ret = 0;

	if (synth == NULL)
		return -1;

	for (q = synth; (p = strsep(&q, ",")); p = q) {
		if (!strcasecmp(p, "no") || !strcasecmp(p, "none"))
			return 0;

		if (!strcasecmp(p, "all"))
			return PERF_SYNTH_ALL;

		if (!strcasecmp(p, "task"))
			ret |= PERF_SYNTH_TASK;
		else if (!strcasecmp(p, "mmap"))
			ret |= PERF_SYNTH_TASK | PERF_SYNTH_MMAP;
		else if (!strcasecmp(p, "cgroup"))
			ret |= PERF_SYNTH_CGROUP;
		else
			return -1;
	}

	return ret;
}

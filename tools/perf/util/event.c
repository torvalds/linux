// SPDX-License-Identifier: GPL-2.0
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uapi/linux/mman.h> /* To get things like MAP_HUGETLB even on older libc headers */
#include <api/fs/fs.h>
#include <linux/perf_event.h>
#include "event.h"
#include "debug.h"
#include "hist.h"
#include "machine.h"
#include "sort.h"
#include "string2.h"
#include "strlist.h"
#include "thread.h"
#include "thread_map.h"
#include "sane_ctype.h"
#include "symbol/kallsyms.h"
#include "asm/bug.h"
#include "stat.h"

static const char *perf_event__names[] = {
	[0]					= "TOTAL",
	[PERF_RECORD_MMAP]			= "MMAP",
	[PERF_RECORD_MMAP2]			= "MMAP2",
	[PERF_RECORD_LOST]			= "LOST",
	[PERF_RECORD_COMM]			= "COMM",
	[PERF_RECORD_EXIT]			= "EXIT",
	[PERF_RECORD_THROTTLE]			= "THROTTLE",
	[PERF_RECORD_UNTHROTTLE]		= "UNTHROTTLE",
	[PERF_RECORD_FORK]			= "FORK",
	[PERF_RECORD_READ]			= "READ",
	[PERF_RECORD_SAMPLE]			= "SAMPLE",
	[PERF_RECORD_AUX]			= "AUX",
	[PERF_RECORD_ITRACE_START]		= "ITRACE_START",
	[PERF_RECORD_LOST_SAMPLES]		= "LOST_SAMPLES",
	[PERF_RECORD_SWITCH]			= "SWITCH",
	[PERF_RECORD_SWITCH_CPU_WIDE]		= "SWITCH_CPU_WIDE",
	[PERF_RECORD_NAMESPACES]		= "NAMESPACES",
	[PERF_RECORD_HEADER_ATTR]		= "ATTR",
	[PERF_RECORD_HEADER_EVENT_TYPE]		= "EVENT_TYPE",
	[PERF_RECORD_HEADER_TRACING_DATA]	= "TRACING_DATA",
	[PERF_RECORD_HEADER_BUILD_ID]		= "BUILD_ID",
	[PERF_RECORD_FINISHED_ROUND]		= "FINISHED_ROUND",
	[PERF_RECORD_ID_INDEX]			= "ID_INDEX",
	[PERF_RECORD_AUXTRACE_INFO]		= "AUXTRACE_INFO",
	[PERF_RECORD_AUXTRACE]			= "AUXTRACE",
	[PERF_RECORD_AUXTRACE_ERROR]		= "AUXTRACE_ERROR",
	[PERF_RECORD_THREAD_MAP]		= "THREAD_MAP",
	[PERF_RECORD_CPU_MAP]			= "CPU_MAP",
	[PERF_RECORD_STAT_CONFIG]		= "STAT_CONFIG",
	[PERF_RECORD_STAT]			= "STAT",
	[PERF_RECORD_STAT_ROUND]		= "STAT_ROUND",
	[PERF_RECORD_EVENT_UPDATE]		= "EVENT_UPDATE",
	[PERF_RECORD_TIME_CONV]			= "TIME_CONV",
	[PERF_RECORD_HEADER_FEATURE]		= "FEATURE",
};

static const char *perf_ns__names[] = {
	[NET_NS_INDEX]		= "net",
	[UTS_NS_INDEX]		= "uts",
	[IPC_NS_INDEX]		= "ipc",
	[PID_NS_INDEX]		= "pid",
	[USER_NS_INDEX]		= "user",
	[MNT_NS_INDEX]		= "mnt",
	[CGROUP_NS_INDEX]	= "cgroup",
};

const char *perf_event__name(unsigned int id)
{
	if (id >= ARRAY_SIZE(perf_event__names))
		return "INVALID";
	if (!perf_event__names[id])
		return "UNKNOWN";
	return perf_event__names[id];
}

static const char *perf_ns__name(unsigned int id)
{
	if (id >= ARRAY_SIZE(perf_ns__names))
		return "UNKNOWN";
	return perf_ns__names[id];
}

static int perf_tool__process_synth_event(struct perf_tool *tool,
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
static int perf_event__get_comm_ids(pid_t pid, char *comm, size_t len,
				    pid_t *tgid, pid_t *ppid)
{
	char filename[PATH_MAX];
	char bf[4096];
	int fd;
	size_t size = 0;
	ssize_t n;
	char *name, *tgids, *ppids;

	*tgid = -1;
	*ppid = -1;

	snprintf(filename, sizeof(filename), "/proc/%d/status", pid);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		pr_debug("couldn't open %s\n", filename);
		return -1;
	}

	n = read(fd, bf, sizeof(bf) - 1);
	close(fd);
	if (n <= 0) {
		pr_warning("Couldn't get COMM, tigd and ppid for pid %d\n",
			   pid);
		return -1;
	}
	bf[n] = '\0';

	name = strstr(bf, "Name:");
	tgids = strstr(bf, "Tgid:");
	ppids = strstr(bf, "PPid:");

	if (name) {
		char *nl;

		name += 5;  /* strlen("Name:") */
		name = ltrim(name);

		nl = strchr(name, '\n');
		if (nl)
			*nl = '\0';

		size = strlen(name);
		if (size >= len)
			size = len - 1;
		memcpy(comm, name, size);
		comm[size] = '\0';
	} else {
		pr_debug("Name: string not found for pid %d\n", pid);
	}

	if (tgids) {
		tgids += 5;  /* strlen("Tgid:") */
		*tgid = atoi(tgids);
	} else {
		pr_debug("Tgid: string not found for pid %d\n", pid);
	}

	if (ppids) {
		ppids += 5;  /* strlen("PPid:") */
		*ppid = atoi(ppids);
	} else {
		pr_debug("PPid: string not found for pid %d\n", pid);
	}

	return 0;
}

static int perf_event__prepare_comm(union perf_event *event, pid_t pid,
				    struct machine *machine,
				    pid_t *tgid, pid_t *ppid)
{
	size_t size;

	*ppid = -1;

	memset(&event->comm, 0, sizeof(event->comm));

	if (machine__is_host(machine)) {
		if (perf_event__get_comm_ids(pid, event->comm.comm,
					     sizeof(event->comm.comm),
					     tgid, ppid) != 0) {
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
	event->comm.tid = pid;

	return 0;
}

pid_t perf_event__synthesize_comm(struct perf_tool *tool,
					 union perf_event *event, pid_t pid,
					 perf_event__handler_t process,
					 struct machine *machine)
{
	pid_t tgid, ppid;

	if (perf_event__prepare_comm(event, pid, machine, &tgid, &ppid) != 0)
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

	event->fork.header.size = (sizeof(event->fork) + machine->id_hdr_size);

	if (perf_tool__process_synth_event(tool, event, machine, process) != 0)
		return -1;

	return 0;
}

int perf_event__synthesize_mmap_events(struct perf_tool *tool,
				       union perf_event *event,
				       pid_t pid, pid_t tgid,
				       perf_event__handler_t process,
				       struct machine *machine,
				       bool mmap_data,
				       unsigned int proc_map_timeout)
{
	char filename[PATH_MAX];
	FILE *fp;
	unsigned long long t;
	bool truncation = false;
	unsigned long long timeout = proc_map_timeout * 1000000ULL;
	int rc = 0;
	const char *hugetlbfs_mnt = hugetlbfs__mountpoint();
	int hugetlbfs_mnt_len = hugetlbfs_mnt ? strlen(hugetlbfs_mnt) : 0;

	if (machine__is_default_guest(machine))
		return 0;

	snprintf(filename, sizeof(filename), "%s/proc/%d/task/%d/maps",
		 machine->root_dir, pid, pid);

	fp = fopen(filename, "r");
	if (fp == NULL) {
		/*
		 * We raced with a task exiting - just return:
		 */
		pr_debug("couldn't open %s\n", filename);
		return -1;
	}

	event->header.type = PERF_RECORD_MMAP2;
	t = rdclock();

	while (1) {
		char bf[BUFSIZ];
		char prot[5];
		char execname[PATH_MAX];
		char anonstr[] = "//anon";
		unsigned int ino;
		size_t size;
		ssize_t n;

		if (fgets(bf, sizeof(bf), fp) == NULL)
			break;

		if ((rdclock() - t) > timeout) {
			pr_warning("Reading %s time out. "
				   "You may want to increase "
				   "the time limit by --proc-map-timeout\n",
				   filename);
			truncation = true;
			goto out;
		}

		/* ensure null termination since stack will be reused. */
		strcpy(execname, "");

		/* 00400000-0040c000 r-xp 00000000 fd:01 41038  /bin/cat */
		n = sscanf(bf, "%"PRIx64"-%"PRIx64" %s %"PRIx64" %x:%x %u %[^\n]\n",
		       &event->mmap2.start, &event->mmap2.len, prot,
		       &event->mmap2.pgoff, &event->mmap2.maj,
		       &event->mmap2.min,
		       &ino, execname);

		/*
 		 * Anon maps don't have the execname.
 		 */
		if (n < 7)
			continue;

		event->mmap2.ino = (u64)ino;

		/*
		 * Just like the kernel, see __perf_event_mmap in kernel/perf_event.c
		 */
		if (machine__is_host(machine))
			event->header.misc = PERF_RECORD_MISC_USER;
		else
			event->header.misc = PERF_RECORD_MISC_GUEST_USER;

		/* map protection and flags bits */
		event->mmap2.prot = 0;
		event->mmap2.flags = 0;
		if (prot[0] == 'r')
			event->mmap2.prot |= PROT_READ;
		if (prot[1] == 'w')
			event->mmap2.prot |= PROT_WRITE;
		if (prot[2] == 'x')
			event->mmap2.prot |= PROT_EXEC;

		if (prot[3] == 's')
			event->mmap2.flags |= MAP_SHARED;
		else
			event->mmap2.flags |= MAP_PRIVATE;

		if (prot[2] != 'x') {
			if (!mmap_data || prot[0] != 'r')
				continue;

			event->header.misc |= PERF_RECORD_MISC_MMAP_DATA;
		}

out:
		if (truncation)
			event->header.misc |= PERF_RECORD_MISC_PROC_MAP_PARSE_TIMEOUT;

		if (!strcmp(execname, ""))
			strcpy(execname, anonstr);

		if (hugetlbfs_mnt_len &&
		    !strncmp(execname, hugetlbfs_mnt, hugetlbfs_mnt_len)) {
			strcpy(execname, anonstr);
			event->mmap2.flags |= MAP_HUGETLB;
		}

		size = strlen(execname) + 1;
		memcpy(event->mmap2.filename, execname, size);
		size = PERF_ALIGN(size, sizeof(u64));
		event->mmap2.len -= event->mmap.start;
		event->mmap2.header.size = (sizeof(event->mmap2) -
					(sizeof(event->mmap2.filename) - size));
		memset(event->mmap2.filename + size, 0, machine->id_hdr_size);
		event->mmap2.header.size += machine->id_hdr_size;
		event->mmap2.pid = tgid;
		event->mmap2.tid = pid;

		if (perf_tool__process_synth_event(tool, event, machine, process) != 0) {
			rc = -1;
			break;
		}

		if (truncation)
			break;
	}

	fclose(fp);
	return rc;
}

int perf_event__synthesize_modules(struct perf_tool *tool,
				   perf_event__handler_t process,
				   struct machine *machine)
{
	int rc = 0;
	struct map *pos;
	struct map_groups *kmaps = &machine->kmaps;
	struct maps *maps = &kmaps->maps[MAP__FUNCTION];
	union perf_event *event = zalloc((sizeof(event->mmap) +
					  machine->id_hdr_size));
	if (event == NULL) {
		pr_debug("Not enough memory synthesizing mmap event "
			 "for kernel modules\n");
		return -1;
	}

	event->header.type = PERF_RECORD_MMAP;

	/*
	 * kernel uses 0 for user space maps, see kernel/perf_event.c
	 * __perf_event_mmap
	 */
	if (machine__is_host(machine))
		event->header.misc = PERF_RECORD_MISC_KERNEL;
	else
		event->header.misc = PERF_RECORD_MISC_GUEST_KERNEL;

	for (pos = maps__first(maps); pos; pos = map__next(pos)) {
		size_t size;

		if (__map__is_kernel(pos))
			continue;

		size = PERF_ALIGN(pos->dso->long_name_len + 1, sizeof(u64));
		event->mmap.header.type = PERF_RECORD_MMAP;
		event->mmap.header.size = (sizeof(event->mmap) -
				        (sizeof(event->mmap.filename) - size));
		memset(event->mmap.filename + size, 0, machine->id_hdr_size);
		event->mmap.header.size += machine->id_hdr_size;
		event->mmap.start = pos->start;
		event->mmap.len   = pos->end - pos->start;
		event->mmap.pid   = machine->pid;

		memcpy(event->mmap.filename, pos->dso->long_name,
		       pos->dso->long_name_len + 1);
		if (perf_tool__process_synth_event(tool, event, machine, process) != 0) {
			rc = -1;
			break;
		}
	}

	free(event);
	return rc;
}

static int __event__synthesize_thread(union perf_event *comm_event,
				      union perf_event *mmap_event,
				      union perf_event *fork_event,
				      union perf_event *namespaces_event,
				      pid_t pid, int full,
				      perf_event__handler_t process,
				      struct perf_tool *tool,
				      struct machine *machine,
				      bool mmap_data,
				      unsigned int proc_map_timeout)
{
	char filename[PATH_MAX];
	DIR *tasks;
	struct dirent *dirent;
	pid_t tgid, ppid;
	int rc = 0;

	/* special case: only send one comm event using passed in pid */
	if (!full) {
		tgid = perf_event__synthesize_comm(tool, comm_event, pid,
						   process, machine);

		if (tgid == -1)
			return -1;

		if (perf_event__synthesize_namespaces(tool, namespaces_event, pid,
						      tgid, process, machine) < 0)
			return -1;


		return perf_event__synthesize_mmap_events(tool, mmap_event, pid, tgid,
							  process, machine, mmap_data,
							  proc_map_timeout);
	}

	if (machine__is_default_guest(machine))
		return 0;

	snprintf(filename, sizeof(filename), "%s/proc/%d/task",
		 machine->root_dir, pid);

	tasks = opendir(filename);
	if (tasks == NULL) {
		pr_debug("couldn't open %s\n", filename);
		return 0;
	}

	while ((dirent = readdir(tasks)) != NULL) {
		char *end;
		pid_t _pid;

		_pid = strtol(dirent->d_name, &end, 10);
		if (*end)
			continue;

		rc = -1;
		if (perf_event__prepare_comm(comm_event, _pid, machine,
					     &tgid, &ppid) != 0)
			break;

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
		if (_pid == pid) {
			/* process the parent's maps too */
			rc = perf_event__synthesize_mmap_events(tool, mmap_event, pid, tgid,
						process, machine, mmap_data, proc_map_timeout);
			if (rc)
				break;
		}
	}

	closedir(tasks);
	return rc;
}

int perf_event__synthesize_thread_map(struct perf_tool *tool,
				      struct thread_map *threads,
				      perf_event__handler_t process,
				      struct machine *machine,
				      bool mmap_data,
				      unsigned int proc_map_timeout)
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
					       thread_map__pid(threads, thread), 0,
					       process, tool, machine,
					       mmap_data, proc_map_timeout)) {
			err = -1;
			break;
		}

		/*
		 * comm.pid is set to thread group id by
		 * perf_event__synthesize_comm
		 */
		if ((int) comm_event->comm.pid != thread_map__pid(threads, thread)) {
			bool need_leader = true;

			/* is thread group leader in thread_map? */
			for (j = 0; j < threads->nr; ++j) {
				if ((int) comm_event->comm.pid == thread_map__pid(threads, j)) {
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
						       mmap_data, proc_map_timeout)) {
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
					    bool mmap_data,
					    unsigned int proc_map_timeout,
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
					   tool, machine, mmap_data,
					   proc_map_timeout);
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
	bool mmap_data;
	unsigned int proc_map_timeout;
	struct dirent **dirent;
	int num;
	int start;
};

static void *synthesize_threads_worker(void *arg)
{
	struct synthesize_threads_arg *args = arg;

	__perf_event__synthesize_threads(args->tool, args->process,
					 args->machine, args->mmap_data,
					 args->proc_map_timeout, args->dirent,
					 args->start, args->num);
	return NULL;
}

int perf_event__synthesize_threads(struct perf_tool *tool,
				   perf_event__handler_t process,
				   struct machine *machine,
				   bool mmap_data,
				   unsigned int proc_map_timeout,
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
	n = scandir(proc_path, &dirent, 0, alphasort);
	if (n < 0)
		return err;

	if (nr_threads_synthesize == UINT_MAX)
		thread_nr = sysconf(_SC_NPROCESSORS_ONLN);
	else
		thread_nr = nr_threads_synthesize;

	if (thread_nr <= 1) {
		err = __perf_event__synthesize_threads(tool, process,
						       machine, mmap_data,
						       proc_map_timeout,
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
		args[i].mmap_data = mmap_data;
		args[i].proc_map_timeout = proc_map_timeout;
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
		free(dirent[i]);
	free(dirent);

	return err;
}

struct process_symbol_args {
	const char *name;
	u64	   start;
};

static int find_symbol_cb(void *arg, const char *name, char type,
			  u64 start)
{
	struct process_symbol_args *args = arg;

	/*
	 * Must be a function or at least an alias, as in PARISC64, where "_text" is
	 * an 'A' to the same address as "_stext".
	 */
	if (!(symbol_type__is_a(type, MAP__FUNCTION) ||
	      type == 'A') || strcmp(name, args->name))
		return 0;

	args->start = start;
	return 1;
}

int kallsyms__get_function_start(const char *kallsyms_filename,
				 const char *symbol_name, u64 *addr)
{
	struct process_symbol_args args = { .name = symbol_name, };

	if (kallsyms__parse(kallsyms_filename, &args, find_symbol_cb) <= 0)
		return -1;

	*addr = args.start;
	return 0;
}

int perf_event__synthesize_kernel_mmap(struct perf_tool *tool,
				       perf_event__handler_t process,
				       struct machine *machine)
{
	size_t size;
	const char *mmap_name;
	char name_buff[PATH_MAX];
	struct map *map = machine__kernel_map(machine);
	struct kmap *kmap;
	int err;
	union perf_event *event;

	if (symbol_conf.kptr_restrict)
		return -1;
	if (map == NULL)
		return -1;

	/*
	 * We should get this from /sys/kernel/sections/.text, but till that is
	 * available use this, and after it is use this as a fallback for older
	 * kernels.
	 */
	event = zalloc((sizeof(event->mmap) + machine->id_hdr_size));
	if (event == NULL) {
		pr_debug("Not enough memory synthesizing mmap event "
			 "for kernel modules\n");
		return -1;
	}

	mmap_name = machine__mmap_name(machine, name_buff, sizeof(name_buff));
	if (machine__is_host(machine)) {
		/*
		 * kernel uses PERF_RECORD_MISC_USER for user space maps,
		 * see kernel/perf_event.c __perf_event_mmap
		 */
		event->header.misc = PERF_RECORD_MISC_KERNEL;
	} else {
		event->header.misc = PERF_RECORD_MISC_GUEST_KERNEL;
	}

	kmap = map__kmap(map);
	size = snprintf(event->mmap.filename, sizeof(event->mmap.filename),
			"%s%s", mmap_name, kmap->ref_reloc_sym->name) + 1;
	size = PERF_ALIGN(size, sizeof(u64));
	event->mmap.header.type = PERF_RECORD_MMAP;
	event->mmap.header.size = (sizeof(event->mmap) -
			(sizeof(event->mmap.filename) - size) + machine->id_hdr_size);
	event->mmap.pgoff = kmap->ref_reloc_sym->addr;
	event->mmap.start = map->start;
	event->mmap.len   = map->end - event->mmap.start;
	event->mmap.pid   = machine->pid;

	err = perf_tool__process_synth_event(tool, event, machine, process);
	free(event);

	return err;
}

int perf_event__synthesize_thread_map2(struct perf_tool *tool,
				      struct thread_map *threads,
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
		struct thread_map_event_entry *entry = &event->thread_map.entries[i];
		char *comm = thread_map__comm(threads, i);

		if (!comm)
			comm = (char *) "";

		entry->pid = thread_map__pid(threads, i);
		strncpy((char *) &entry->comm, comm, sizeof(entry->comm));
	}

	err = process(tool, event, NULL, machine);

	free(event);
	return err;
}

static void synthesize_cpus(struct cpu_map_entries *cpus,
			    struct cpu_map *map)
{
	int i;

	cpus->nr = map->nr;

	for (i = 0; i < map->nr; i++)
		cpus->cpu[i] = map->map[i];
}

static void synthesize_mask(struct cpu_map_mask *mask,
			    struct cpu_map *map, int max)
{
	int i;

	mask->nr = BITS_TO_LONGS(max);
	mask->long_size = sizeof(long);

	for (i = 0; i < map->nr; i++)
		set_bit(map->map[i], mask->mask);
}

static size_t cpus_size(struct cpu_map *map)
{
	return sizeof(struct cpu_map_entries) + map->nr * sizeof(u16);
}

static size_t mask_size(struct cpu_map *map, int *max)
{
	int i;

	*max = 0;

	for (i = 0; i < map->nr; i++) {
		/* bit possition of the cpu is + 1 */
		int bit = map->map[i] + 1;

		if (bit > *max)
			*max = bit;
	}

	return sizeof(struct cpu_map_mask) + BITS_TO_LONGS(*max) * sizeof(long);
}

void *cpu_map_data__alloc(struct cpu_map *map, size_t *size, u16 *type, int *max)
{
	size_t size_cpus, size_mask;
	bool is_dummy = cpu_map__empty(map);

	/*
	 * Both array and mask data have variable size based
	 * on the number of cpus and their actual values.
	 * The size of the 'struct cpu_map_data' is:
	 *
	 *   array = size of 'struct cpu_map_entries' +
	 *           number of cpus * sizeof(u64)
	 *
	 *   mask  = size of 'struct cpu_map_mask' +
	 *           maximum cpu bit converted to size of longs
	 *
	 * and finaly + the size of 'struct cpu_map_data'.
	 */
	size_cpus = cpus_size(map);
	size_mask = mask_size(map, max);

	if (is_dummy || (size_cpus < size_mask)) {
		*size += size_cpus;
		*type  = PERF_CPU_MAP__CPUS;
	} else {
		*size += size_mask;
		*type  = PERF_CPU_MAP__MASK;
	}

	*size += sizeof(struct cpu_map_data);
	return zalloc(*size);
}

void cpu_map_data__synthesize(struct cpu_map_data *data, struct cpu_map *map,
			      u16 type, int max)
{
	data->type = type;

	switch (type) {
	case PERF_CPU_MAP__CPUS:
		synthesize_cpus((struct cpu_map_entries *) data->data, map);
		break;
	case PERF_CPU_MAP__MASK:
		synthesize_mask((struct cpu_map_mask *) data->data, map, max);
	default:
		break;
	};
}

static struct cpu_map_event* cpu_map_event__new(struct cpu_map *map)
{
	size_t size = sizeof(struct cpu_map_event);
	struct cpu_map_event *event;
	int max;
	u16 type;

	event = cpu_map_data__alloc(map, &size, &type, &max);
	if (!event)
		return NULL;

	event->header.type = PERF_RECORD_CPU_MAP;
	event->header.size = size;
	event->data.type   = type;

	cpu_map_data__synthesize(&event->data, map, type, max);
	return event;
}

int perf_event__synthesize_cpu_map(struct perf_tool *tool,
				   struct cpu_map *map,
				   perf_event__handler_t process,
				   struct machine *machine)
{
	struct cpu_map_event *event;
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
	struct stat_config_event *event;
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

	WARN_ONCE(i != PERF_STAT_CONFIG_TERM__MAX,
		  "stat config terms unbalanced\n");
#undef ADD

	err = process(tool, (union perf_event *) event, NULL, machine);

	free(event);
	return err;
}

int perf_event__synthesize_stat(struct perf_tool *tool,
				u32 cpu, u32 thread, u64 id,
				struct perf_counts_values *count,
				perf_event__handler_t process,
				struct machine *machine)
{
	struct stat_event event;

	event.header.type = PERF_RECORD_STAT;
	event.header.size = sizeof(event);
	event.header.misc = 0;

	event.id        = id;
	event.cpu       = cpu;
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
	struct stat_round_event event;

	event.header.type = PERF_RECORD_STAT_ROUND;
	event.header.size = sizeof(event);
	event.header.misc = 0;

	event.time = evtime;
	event.type = type;

	return process(tool, (union perf_event *) &event, NULL, machine);
}

void perf_event__read_stat_config(struct perf_stat_config *config,
				  struct stat_config_event *event)
{
	unsigned i;

	for (i = 0; i < event->nr; i++) {

		switch (event->data[i].tag) {
#define CASE(__term, __val)					\
		case PERF_STAT_CONFIG_TERM__##__term:		\
			config->__val = event->data[i].val;	\
			break;

		CASE(AGGR_MODE, aggr_mode)
		CASE(SCALE,     scale)
		CASE(INTERVAL,  interval)
#undef CASE
		default:
			pr_warning("unknown stat config term %" PRIu64 "\n",
				   event->data[i].tag);
		}
	}
}

size_t perf_event__fprintf_comm(union perf_event *event, FILE *fp)
{
	const char *s;

	if (event->header.misc & PERF_RECORD_MISC_COMM_EXEC)
		s = " exec";
	else
		s = "";

	return fprintf(fp, "%s: %s:%d/%d\n", s, event->comm.comm, event->comm.pid, event->comm.tid);
}

size_t perf_event__fprintf_namespaces(union perf_event *event, FILE *fp)
{
	size_t ret = 0;
	struct perf_ns_link_info *ns_link_info;
	u32 nr_namespaces, idx;

	ns_link_info = event->namespaces.link_info;
	nr_namespaces = event->namespaces.nr_namespaces;

	ret += fprintf(fp, " %d/%d - nr_namespaces: %u\n\t\t[",
		       event->namespaces.pid,
		       event->namespaces.tid,
		       nr_namespaces);

	for (idx = 0; idx < nr_namespaces; idx++) {
		if (idx && (idx % 4 == 0))
			ret += fprintf(fp, "\n\t\t ");

		ret  += fprintf(fp, "%u/%s: %" PRIu64 "/%#" PRIx64 "%s", idx,
				perf_ns__name(idx), (u64)ns_link_info[idx].dev,
				(u64)ns_link_info[idx].ino,
				((idx + 1) != nr_namespaces) ? ", " : "]\n");
	}

	return ret;
}

int perf_event__process_comm(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_comm_event(machine, event, sample);
}

int perf_event__process_namespaces(struct perf_tool *tool __maybe_unused,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	return machine__process_namespaces_event(machine, event, sample);
}

int perf_event__process_lost(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_lost_event(machine, event, sample);
}

int perf_event__process_aux(struct perf_tool *tool __maybe_unused,
			    union perf_event *event,
			    struct perf_sample *sample __maybe_unused,
			    struct machine *machine)
{
	return machine__process_aux_event(machine, event);
}

int perf_event__process_itrace_start(struct perf_tool *tool __maybe_unused,
				     union perf_event *event,
				     struct perf_sample *sample __maybe_unused,
				     struct machine *machine)
{
	return machine__process_itrace_start_event(machine, event);
}

int perf_event__process_lost_samples(struct perf_tool *tool __maybe_unused,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine)
{
	return machine__process_lost_samples_event(machine, event, sample);
}

int perf_event__process_switch(struct perf_tool *tool __maybe_unused,
			       union perf_event *event,
			       struct perf_sample *sample __maybe_unused,
			       struct machine *machine)
{
	return machine__process_switch_event(machine, event);
}

size_t perf_event__fprintf_mmap(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " %d/%d: [%#" PRIx64 "(%#" PRIx64 ") @ %#" PRIx64 "]: %c %s\n",
		       event->mmap.pid, event->mmap.tid, event->mmap.start,
		       event->mmap.len, event->mmap.pgoff,
		       (event->header.misc & PERF_RECORD_MISC_MMAP_DATA) ? 'r' : 'x',
		       event->mmap.filename);
}

size_t perf_event__fprintf_mmap2(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " %d/%d: [%#" PRIx64 "(%#" PRIx64 ") @ %#" PRIx64
			   " %02x:%02x %"PRIu64" %"PRIu64"]: %c%c%c%c %s\n",
		       event->mmap2.pid, event->mmap2.tid, event->mmap2.start,
		       event->mmap2.len, event->mmap2.pgoff, event->mmap2.maj,
		       event->mmap2.min, event->mmap2.ino,
		       event->mmap2.ino_generation,
		       (event->mmap2.prot & PROT_READ) ? 'r' : '-',
		       (event->mmap2.prot & PROT_WRITE) ? 'w' : '-',
		       (event->mmap2.prot & PROT_EXEC) ? 'x' : '-',
		       (event->mmap2.flags & MAP_SHARED) ? 's' : 'p',
		       event->mmap2.filename);
}

size_t perf_event__fprintf_thread_map(union perf_event *event, FILE *fp)
{
	struct thread_map *threads = thread_map__new_event(&event->thread_map);
	size_t ret;

	ret = fprintf(fp, " nr: ");

	if (threads)
		ret += thread_map__fprintf(threads, fp);
	else
		ret += fprintf(fp, "failed to get threads from event\n");

	thread_map__put(threads);
	return ret;
}

size_t perf_event__fprintf_cpu_map(union perf_event *event, FILE *fp)
{
	struct cpu_map *cpus = cpu_map__new_data(&event->cpu_map.data);
	size_t ret;

	ret = fprintf(fp, ": ");

	if (cpus)
		ret += cpu_map__fprintf(cpus, fp);
	else
		ret += fprintf(fp, "failed to get cpumap from event\n");

	cpu_map__put(cpus);
	return ret;
}

int perf_event__process_mmap(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_mmap_event(machine, event, sample);
}

int perf_event__process_mmap2(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_mmap2_event(machine, event, sample);
}

size_t perf_event__fprintf_task(union perf_event *event, FILE *fp)
{
	return fprintf(fp, "(%d:%d):(%d:%d)\n",
		       event->fork.pid, event->fork.tid,
		       event->fork.ppid, event->fork.ptid);
}

int perf_event__process_fork(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_fork_event(machine, event, sample);
}

int perf_event__process_exit(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_exit_event(machine, event, sample);
}

size_t perf_event__fprintf_aux(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " offset: %#"PRIx64" size: %#"PRIx64" flags: %#"PRIx64" [%s%s%s]\n",
		       event->aux.aux_offset, event->aux.aux_size,
		       event->aux.flags,
		       event->aux.flags & PERF_AUX_FLAG_TRUNCATED ? "T" : "",
		       event->aux.flags & PERF_AUX_FLAG_OVERWRITE ? "O" : "",
		       event->aux.flags & PERF_AUX_FLAG_PARTIAL   ? "P" : "");
}

size_t perf_event__fprintf_itrace_start(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " pid: %u tid: %u\n",
		       event->itrace_start.pid, event->itrace_start.tid);
}

size_t perf_event__fprintf_switch(union perf_event *event, FILE *fp)
{
	bool out = event->header.misc & PERF_RECORD_MISC_SWITCH_OUT;
	const char *in_out = out ? "OUT" : "IN ";

	if (event->header.type == PERF_RECORD_SWITCH)
		return fprintf(fp, " %s\n", in_out);

	return fprintf(fp, " %s  %s pid/tid: %5u/%-5u\n",
		       in_out, out ? "next" : "prev",
		       event->context_switch.next_prev_pid,
		       event->context_switch.next_prev_tid);
}

size_t perf_event__fprintf(union perf_event *event, FILE *fp)
{
	size_t ret = fprintf(fp, "PERF_RECORD_%s",
			     perf_event__name(event->header.type));

	switch (event->header.type) {
	case PERF_RECORD_COMM:
		ret += perf_event__fprintf_comm(event, fp);
		break;
	case PERF_RECORD_FORK:
	case PERF_RECORD_EXIT:
		ret += perf_event__fprintf_task(event, fp);
		break;
	case PERF_RECORD_MMAP:
		ret += perf_event__fprintf_mmap(event, fp);
		break;
	case PERF_RECORD_NAMESPACES:
		ret += perf_event__fprintf_namespaces(event, fp);
		break;
	case PERF_RECORD_MMAP2:
		ret += perf_event__fprintf_mmap2(event, fp);
		break;
	case PERF_RECORD_AUX:
		ret += perf_event__fprintf_aux(event, fp);
		break;
	case PERF_RECORD_ITRACE_START:
		ret += perf_event__fprintf_itrace_start(event, fp);
		break;
	case PERF_RECORD_SWITCH:
	case PERF_RECORD_SWITCH_CPU_WIDE:
		ret += perf_event__fprintf_switch(event, fp);
		break;
	default:
		ret += fprintf(fp, "\n");
	}

	return ret;
}

int perf_event__process(struct perf_tool *tool __maybe_unused,
			union perf_event *event,
			struct perf_sample *sample,
			struct machine *machine)
{
	return machine__process_event(machine, event, sample);
}

void thread__find_addr_map(struct thread *thread, u8 cpumode,
			   enum map_type type, u64 addr,
			   struct addr_location *al)
{
	struct map_groups *mg = thread->mg;
	struct machine *machine = mg->machine;
	bool load_map = false;

	al->machine = machine;
	al->thread = thread;
	al->addr = addr;
	al->cpumode = cpumode;
	al->filtered = 0;

	if (machine == NULL) {
		al->map = NULL;
		return;
	}

	if (cpumode == PERF_RECORD_MISC_KERNEL && perf_host) {
		al->level = 'k';
		mg = &machine->kmaps;
		load_map = true;
	} else if (cpumode == PERF_RECORD_MISC_USER && perf_host) {
		al->level = '.';
	} else if (cpumode == PERF_RECORD_MISC_GUEST_KERNEL && perf_guest) {
		al->level = 'g';
		mg = &machine->kmaps;
		load_map = true;
	} else if (cpumode == PERF_RECORD_MISC_GUEST_USER && perf_guest) {
		al->level = 'u';
	} else {
		al->level = 'H';
		al->map = NULL;

		if ((cpumode == PERF_RECORD_MISC_GUEST_USER ||
			cpumode == PERF_RECORD_MISC_GUEST_KERNEL) &&
			!perf_guest)
			al->filtered |= (1 << HIST_FILTER__GUEST);
		if ((cpumode == PERF_RECORD_MISC_USER ||
			cpumode == PERF_RECORD_MISC_KERNEL) &&
			!perf_host)
			al->filtered |= (1 << HIST_FILTER__HOST);

		return;
	}
try_again:
	al->map = map_groups__find(mg, type, al->addr);
	if (al->map == NULL) {
		/*
		 * If this is outside of all known maps, and is a negative
		 * address, try to look it up in the kernel dso, as it might be
		 * a vsyscall or vdso (which executes in user-mode).
		 *
		 * XXX This is nasty, we should have a symbol list in the
		 * "[vdso]" dso, but for now lets use the old trick of looking
		 * in the whole kernel symbol list.
		 */
		if (cpumode == PERF_RECORD_MISC_USER && machine &&
		    mg != &machine->kmaps &&
		    machine__kernel_ip(machine, al->addr)) {
			mg = &machine->kmaps;
			load_map = true;
			goto try_again;
		}
	} else {
		/*
		 * Kernel maps might be changed when loading symbols so loading
		 * must be done prior to using kernel maps.
		 */
		if (load_map)
			map__load(al->map);
		al->addr = al->map->map_ip(al->map, al->addr);
	}
}

void thread__find_addr_location(struct thread *thread,
				u8 cpumode, enum map_type type, u64 addr,
				struct addr_location *al)
{
	thread__find_addr_map(thread, cpumode, type, addr, al);
	if (al->map != NULL)
		al->sym = map__find_symbol(al->map, al->addr);
	else
		al->sym = NULL;
}

/*
 * Callers need to drop the reference to al->thread, obtained in
 * machine__findnew_thread()
 */
int machine__resolve(struct machine *machine, struct addr_location *al,
		     struct perf_sample *sample)
{
	struct thread *thread = machine__findnew_thread(machine, sample->pid,
							sample->tid);

	if (thread == NULL)
		return -1;

	dump_printf(" ... thread: %s:%d\n", thread__comm_str(thread), thread->tid);
	/*
	 * Have we already created the kernel maps for this machine?
	 *
	 * This should have happened earlier, when we processed the kernel MMAP
	 * events, but for older perf.data files there was no such thing, so do
	 * it now.
	 */
	if (sample->cpumode == PERF_RECORD_MISC_KERNEL &&
	    machine__kernel_map(machine) == NULL)
		machine__create_kernel_maps(machine);

	thread__find_addr_map(thread, sample->cpumode, MAP__FUNCTION, sample->ip, al);
	dump_printf(" ...... dso: %s\n",
		    al->map ? al->map->dso->long_name :
			al->level == 'H' ? "[hypervisor]" : "<not found>");

	if (thread__is_filtered(thread))
		al->filtered |= (1 << HIST_FILTER__THREAD);

	al->sym = NULL;
	al->cpu = sample->cpu;
	al->socket = -1;
	al->srcline = NULL;

	if (al->cpu >= 0) {
		struct perf_env *env = machine->env;

		if (env && env->cpu)
			al->socket = env->cpu[al->cpu].socket_id;
	}

	if (al->map) {
		struct dso *dso = al->map->dso;

		if (symbol_conf.dso_list &&
		    (!dso || !(strlist__has_entry(symbol_conf.dso_list,
						  dso->short_name) ||
			       (dso->short_name != dso->long_name &&
				strlist__has_entry(symbol_conf.dso_list,
						   dso->long_name))))) {
			al->filtered |= (1 << HIST_FILTER__DSO);
		}

		al->sym = map__find_symbol(al->map, al->addr);
	}

	if (symbol_conf.sym_list &&
		(!al->sym || !strlist__has_entry(symbol_conf.sym_list,
						al->sym->name))) {
		al->filtered |= (1 << HIST_FILTER__SYMBOL);
	}

	return 0;
}

/*
 * The preprocess_sample method will return with reference counts for the
 * in it, when done using (and perhaps getting ref counts if needing to
 * keep a pointer to one of those entries) it must be paired with
 * addr_location__put(), so that the refcounts can be decremented.
 */
void addr_location__put(struct addr_location *al)
{
	thread__zput(al->thread);
}

bool is_bts_event(struct perf_event_attr *attr)
{
	return attr->type == PERF_TYPE_HARDWARE &&
	       (attr->config & PERF_COUNT_HW_BRANCH_INSTRUCTIONS) &&
	       attr->sample_period == 1;
}

bool sample_addr_correlates_sym(struct perf_event_attr *attr)
{
	if (attr->type == PERF_TYPE_SOFTWARE &&
	    (attr->config == PERF_COUNT_SW_PAGE_FAULTS ||
	     attr->config == PERF_COUNT_SW_PAGE_FAULTS_MIN ||
	     attr->config == PERF_COUNT_SW_PAGE_FAULTS_MAJ))
		return true;

	if (is_bts_event(attr))
		return true;

	return false;
}

void thread__resolve(struct thread *thread, struct addr_location *al,
		     struct perf_sample *sample)
{
	thread__find_addr_map(thread, sample->cpumode, MAP__FUNCTION, sample->addr, al);
	if (!al->map)
		thread__find_addr_map(thread, sample->cpumode, MAP__VARIABLE,
				      sample->addr, al);

	al->cpu = sample->cpu;
	al->sym = NULL;

	if (al->map)
		al->sym = map__find_symbol(al->map, al->addr);
}

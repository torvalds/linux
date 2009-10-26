#include <linux/types.h>
#include "event.h"
#include "debug.h"
#include "string.h"

static pid_t event__synthesize_comm(pid_t pid, int full,
				    int (*process)(event_t *event))
{
	event_t ev;
	char filename[PATH_MAX];
	char bf[BUFSIZ];
	FILE *fp;
	size_t size = 0;
	DIR *tasks;
	struct dirent dirent, *next;
	pid_t tgid = 0;

	snprintf(filename, sizeof(filename), "/proc/%d/status", pid);

	fp = fopen(filename, "r");
	if (fp == NULL) {
out_race:
		/*
		 * We raced with a task exiting - just return:
		 */
		pr_debug("couldn't open %s\n", filename);
		return 0;
	}

	memset(&ev.comm, 0, sizeof(ev.comm));
	while (!ev.comm.comm[0] || !ev.comm.pid) {
		if (fgets(bf, sizeof(bf), fp) == NULL)
			goto out_failure;

		if (memcmp(bf, "Name:", 5) == 0) {
			char *name = bf + 5;
			while (*name && isspace(*name))
				++name;
			size = strlen(name) - 1;
			memcpy(ev.comm.comm, name, size++);
		} else if (memcmp(bf, "Tgid:", 5) == 0) {
			char *tgids = bf + 5;
			while (*tgids && isspace(*tgids))
				++tgids;
			tgid = ev.comm.pid = atoi(tgids);
		}
	}

	ev.comm.header.type = PERF_RECORD_COMM;
	size = ALIGN(size, sizeof(u64));
	ev.comm.header.size = sizeof(ev.comm) - (sizeof(ev.comm.comm) - size);

	if (!full) {
		ev.comm.tid = pid;

		process(&ev);
		goto out_fclose;
	}

	snprintf(filename, sizeof(filename), "/proc/%d/task", pid);

	tasks = opendir(filename);
	if (tasks == NULL)
		goto out_race;

	while (!readdir_r(tasks, &dirent, &next) && next) {
		char *end;
		pid = strtol(dirent.d_name, &end, 10);
		if (*end)
			continue;

		ev.comm.tid = pid;

		process(&ev);
	}
	closedir(tasks);

out_fclose:
	fclose(fp);
	return tgid;

out_failure:
	pr_warning("couldn't get COMM and pgid, malformed %s\n", filename);
	return -1;
}

static int event__synthesize_mmap_events(pid_t pid, pid_t tgid,
					 int (*process)(event_t *event))
{
	char filename[PATH_MAX];
	FILE *fp;

	snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);

	fp = fopen(filename, "r");
	if (fp == NULL) {
		/*
		 * We raced with a task exiting - just return:
		 */
		pr_debug("couldn't open %s\n", filename);
		return -1;
	}

	while (1) {
		char bf[BUFSIZ], *pbf = bf;
		event_t ev = {
			.header = { .type = PERF_RECORD_MMAP },
		};
		int n;
		size_t size;
		if (fgets(bf, sizeof(bf), fp) == NULL)
			break;

		/* 00400000-0040c000 r-xp 00000000 fd:01 41038  /bin/cat */
		n = hex2u64(pbf, &ev.mmap.start);
		if (n < 0)
			continue;
		pbf += n + 1;
		n = hex2u64(pbf, &ev.mmap.len);
		if (n < 0)
			continue;
		pbf += n + 3;
		if (*pbf == 'x') { /* vm_exec */
			char *execname = strchr(bf, '/');

			/* Catch VDSO */
			if (execname == NULL)
				execname = strstr(bf, "[vdso]");

			if (execname == NULL)
				continue;

			size = strlen(execname);
			execname[size - 1] = '\0'; /* Remove \n */
			memcpy(ev.mmap.filename, execname, size);
			size = ALIGN(size, sizeof(u64));
			ev.mmap.len -= ev.mmap.start;
			ev.mmap.header.size = (sizeof(ev.mmap) -
					       (sizeof(ev.mmap.filename) - size));
			ev.mmap.pid = tgid;
			ev.mmap.tid = pid;

			process(&ev);
		}
	}

	fclose(fp);
	return 0;
}

int event__synthesize_thread(pid_t pid, int (*process)(event_t *event))
{
	pid_t tgid = event__synthesize_comm(pid, 1, process);
	if (tgid == -1)
		return -1;
	return event__synthesize_mmap_events(pid, tgid, process);
}

void event__synthesize_threads(int (*process)(event_t *event))
{
	DIR *proc;
	struct dirent dirent, *next;

	proc = opendir("/proc");

	while (!readdir_r(proc, &dirent, &next) && next) {
		char *end;
		pid_t pid = strtol(dirent.d_name, &end, 10);

		if (*end) /* only interested in proper numerical dirents */
			continue;

		event__synthesize_thread(pid, process);
	}

	closedir(proc);
}

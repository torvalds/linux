/*
 * probe-file.c : operate ftrace k/uprobe events files
 *
 * Written by Masami Hiramatsu <masami.hiramatsu.pt@hitachi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "util.h"
#include "event.h"
#include "strlist.h"
#include "debug.h"
#include "cache.h"
#include "color.h"
#include "symbol.h"
#include "thread.h"
#include <api/fs/tracing_path.h>
#include "probe-event.h"
#include "probe-file.h"
#include "session.h"

#define MAX_CMDLEN 256

static void print_open_warning(int err, bool uprobe)
{
	char sbuf[STRERR_BUFSIZE];

	if (err == -ENOENT) {
		const char *config;

		if (uprobe)
			config = "CONFIG_UPROBE_EVENTS";
		else
			config = "CONFIG_KPROBE_EVENTS";

		pr_warning("%cprobe_events file does not exist"
			   " - please rebuild kernel with %s.\n",
			   uprobe ? 'u' : 'k', config);
	} else if (err == -ENOTSUP)
		pr_warning("Tracefs or debugfs is not mounted.\n");
	else
		pr_warning("Failed to open %cprobe_events: %s\n",
			   uprobe ? 'u' : 'k',
			   strerror_r(-err, sbuf, sizeof(sbuf)));
}

static void print_both_open_warning(int kerr, int uerr)
{
	/* Both kprobes and uprobes are disabled, warn it. */
	if (kerr == -ENOTSUP && uerr == -ENOTSUP)
		pr_warning("Tracefs or debugfs is not mounted.\n");
	else if (kerr == -ENOENT && uerr == -ENOENT)
		pr_warning("Please rebuild kernel with CONFIG_KPROBE_EVENTS "
			   "or/and CONFIG_UPROBE_EVENTS.\n");
	else {
		char sbuf[STRERR_BUFSIZE];
		pr_warning("Failed to open kprobe events: %s.\n",
			   strerror_r(-kerr, sbuf, sizeof(sbuf)));
		pr_warning("Failed to open uprobe events: %s.\n",
			   strerror_r(-uerr, sbuf, sizeof(sbuf)));
	}
}

static int open_probe_events(const char *trace_file, bool readwrite)
{
	char buf[PATH_MAX];
	const char *tracing_dir = "";
	int ret;

	ret = e_snprintf(buf, PATH_MAX, "%s/%s%s",
			 tracing_path, tracing_dir, trace_file);
	if (ret >= 0) {
		pr_debug("Opening %s write=%d\n", buf, readwrite);
		if (readwrite && !probe_event_dry_run)
			ret = open(buf, O_RDWR | O_APPEND, 0);
		else
			ret = open(buf, O_RDONLY, 0);

		if (ret < 0)
			ret = -errno;
	}
	return ret;
}

static int open_kprobe_events(bool readwrite)
{
	return open_probe_events("kprobe_events", readwrite);
}

static int open_uprobe_events(bool readwrite)
{
	return open_probe_events("uprobe_events", readwrite);
}

int probe_file__open(int flag)
{
	int fd;

	if (flag & PF_FL_UPROBE)
		fd = open_uprobe_events(flag & PF_FL_RW);
	else
		fd = open_kprobe_events(flag & PF_FL_RW);
	if (fd < 0)
		print_open_warning(fd, flag & PF_FL_UPROBE);

	return fd;
}

int probe_file__open_both(int *kfd, int *ufd, int flag)
{
	if (!kfd || !ufd)
		return -EINVAL;

	*kfd = open_kprobe_events(flag & PF_FL_RW);
	*ufd = open_uprobe_events(flag & PF_FL_RW);
	if (*kfd < 0 && *ufd < 0) {
		print_both_open_warning(*kfd, *ufd);
		return *kfd;
	}

	return 0;
}

/* Get raw string list of current kprobe_events  or uprobe_events */
struct strlist *probe_file__get_rawlist(int fd)
{
	int ret, idx;
	FILE *fp;
	char buf[MAX_CMDLEN];
	char *p;
	struct strlist *sl;

	if (fd < 0)
		return NULL;

	sl = strlist__new(NULL, NULL);

	fp = fdopen(dup(fd), "r");
	while (!feof(fp)) {
		p = fgets(buf, MAX_CMDLEN, fp);
		if (!p)
			break;

		idx = strlen(p) - 1;
		if (p[idx] == '\n')
			p[idx] = '\0';
		ret = strlist__add(sl, buf);
		if (ret < 0) {
			pr_debug("strlist__add failed (%d)\n", ret);
			strlist__delete(sl);
			return NULL;
		}
	}
	fclose(fp);

	return sl;
}

static struct strlist *__probe_file__get_namelist(int fd, bool include_group)
{
	char buf[128];
	struct strlist *sl, *rawlist;
	struct str_node *ent;
	struct probe_trace_event tev;
	int ret = 0;

	memset(&tev, 0, sizeof(tev));
	rawlist = probe_file__get_rawlist(fd);
	if (!rawlist)
		return NULL;
	sl = strlist__new(NULL, NULL);
	strlist__for_each(ent, rawlist) {
		ret = parse_probe_trace_command(ent->s, &tev);
		if (ret < 0)
			break;
		if (include_group) {
			ret = e_snprintf(buf, 128, "%s:%s", tev.group,
					tev.event);
			if (ret >= 0)
				ret = strlist__add(sl, buf);
		} else
			ret = strlist__add(sl, tev.event);
		clear_probe_trace_event(&tev);
		if (ret < 0)
			break;
	}
	strlist__delete(rawlist);

	if (ret < 0) {
		strlist__delete(sl);
		return NULL;
	}
	return sl;
}

/* Get current perf-probe event names */
struct strlist *probe_file__get_namelist(int fd)
{
	return __probe_file__get_namelist(fd, false);
}

int probe_file__add_event(int fd, struct probe_trace_event *tev)
{
	int ret = 0;
	char *buf = synthesize_probe_trace_command(tev);
	char sbuf[STRERR_BUFSIZE];

	if (!buf) {
		pr_debug("Failed to synthesize probe trace event.\n");
		return -EINVAL;
	}

	pr_debug("Writing event: %s\n", buf);
	if (!probe_event_dry_run) {
		ret = write(fd, buf, strlen(buf));
		if (ret <= 0) {
			ret = -errno;
			pr_warning("Failed to write event: %s\n",
				   strerror_r(errno, sbuf, sizeof(sbuf)));
		}
	}
	free(buf);

	return ret;
}

static int __del_trace_probe_event(int fd, struct str_node *ent)
{
	char *p;
	char buf[128];
	int ret;

	/* Convert from perf-probe event to trace-probe event */
	ret = e_snprintf(buf, 128, "-:%s", ent->s);
	if (ret < 0)
		goto error;

	p = strchr(buf + 2, ':');
	if (!p) {
		pr_debug("Internal error: %s should have ':' but not.\n",
			 ent->s);
		ret = -ENOTSUP;
		goto error;
	}
	*p = '/';

	pr_debug("Writing event: %s\n", buf);
	ret = write(fd, buf, strlen(buf));
	if (ret < 0) {
		ret = -errno;
		goto error;
	}

	return 0;
error:
	pr_warning("Failed to delete event: %s\n",
		   strerror_r(-ret, buf, sizeof(buf)));
	return ret;
}

int probe_file__get_events(int fd, struct strfilter *filter,
			   struct strlist *plist)
{
	struct strlist *namelist;
	struct str_node *ent;
	const char *p;
	int ret = -ENOENT;

	if (!plist)
		return -EINVAL;

	namelist = __probe_file__get_namelist(fd, true);
	if (!namelist)
		return -ENOENT;

	strlist__for_each(ent, namelist) {
		p = strchr(ent->s, ':');
		if ((p && strfilter__compare(filter, p + 1)) ||
		    strfilter__compare(filter, ent->s)) {
			strlist__add(plist, ent->s);
			ret = 0;
		}
	}
	strlist__delete(namelist);

	return ret;
}

int probe_file__del_strlist(int fd, struct strlist *namelist)
{
	int ret = 0;
	struct str_node *ent;

	strlist__for_each(ent, namelist) {
		ret = __del_trace_probe_event(fd, ent);
		if (ret < 0)
			break;
	}
	return ret;
}

int probe_file__del_events(int fd, struct strfilter *filter)
{
	struct strlist *namelist;
	int ret;

	namelist = strlist__new(NULL, NULL);
	if (!namelist)
		return -ENOMEM;

	ret = probe_file__get_events(fd, filter, namelist);
	if (ret < 0)
		return ret;

	ret = probe_file__del_strlist(fd, namelist);
	strlist__delete(namelist);

	return ret;
}

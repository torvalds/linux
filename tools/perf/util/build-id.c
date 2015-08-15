/*
 * build-id.c
 *
 * build-id support
 *
 * Copyright (C) 2009, 2010 Red Hat Inc.
 * Copyright (C) 2009, 2010 Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "util.h"
#include <stdio.h>
#include "build-id.h"
#include "event.h"
#include "symbol.h"
#include <linux/kernel.h>
#include "debug.h"
#include "session.h"
#include "tool.h"
#include "header.h"
#include "vdso.h"


static bool no_buildid_cache;

int build_id__mark_dso_hit(struct perf_tool *tool __maybe_unused,
			   union perf_event *event,
			   struct perf_sample *sample,
			   struct perf_evsel *evsel __maybe_unused,
			   struct machine *machine)
{
	struct addr_location al;
	u8 cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	struct thread *thread = machine__findnew_thread(machine, sample->pid,
							sample->tid);

	if (thread == NULL) {
		pr_err("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	thread__find_addr_map(thread, cpumode, MAP__FUNCTION, sample->ip, &al);

	if (al.map != NULL)
		al.map->dso->hit = 1;

	thread__put(thread);
	return 0;
}

static int perf_event__exit_del_thread(struct perf_tool *tool __maybe_unused,
				       union perf_event *event,
				       struct perf_sample *sample
				       __maybe_unused,
				       struct machine *machine)
{
	struct thread *thread = machine__findnew_thread(machine,
							event->fork.pid,
							event->fork.tid);

	dump_printf("(%d:%d):(%d:%d)\n", event->fork.pid, event->fork.tid,
		    event->fork.ppid, event->fork.ptid);

	if (thread) {
		machine__remove_thread(machine, thread);
		thread__put(thread);
	}

	return 0;
}

struct perf_tool build_id__mark_dso_hit_ops = {
	.sample	= build_id__mark_dso_hit,
	.mmap	= perf_event__process_mmap,
	.mmap2	= perf_event__process_mmap2,
	.fork	= perf_event__process_fork,
	.exit	= perf_event__exit_del_thread,
	.attr		 = perf_event__process_attr,
	.build_id	 = perf_event__process_build_id,
};

int build_id__sprintf(const u8 *build_id, int len, char *bf)
{
	char *bid = bf;
	const u8 *raw = build_id;
	int i;

	for (i = 0; i < len; ++i) {
		sprintf(bid, "%02x", *raw);
		++raw;
		bid += 2;
	}

	return raw - build_id;
}

int sysfs__sprintf_build_id(const char *root_dir, char *sbuild_id)
{
	char notes[PATH_MAX];
	u8 build_id[BUILD_ID_SIZE];
	int ret;

	if (!root_dir)
		root_dir = "";

	scnprintf(notes, sizeof(notes), "%s/sys/kernel/notes", root_dir);

	ret = sysfs__read_build_id(notes, build_id, sizeof(build_id));
	if (ret < 0)
		return ret;

	return build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
}

int filename__sprintf_build_id(const char *pathname, char *sbuild_id)
{
	u8 build_id[BUILD_ID_SIZE];
	int ret;

	ret = filename__read_build_id(pathname, build_id, sizeof(build_id));
	if (ret < 0)
		return ret;
	else if (ret != sizeof(build_id))
		return -EINVAL;

	return build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
}

/* asnprintf consolidates asprintf and snprintf */
static int asnprintf(char **strp, size_t size, const char *fmt, ...)
{
	va_list ap;
	int ret;

	if (!strp)
		return -EINVAL;

	va_start(ap, fmt);
	if (*strp)
		ret = vsnprintf(*strp, size, fmt, ap);
	else
		ret = vasprintf(strp, fmt, ap);
	va_end(ap);

	return ret;
}

static char *build_id__filename(const char *sbuild_id, char *bf, size_t size)
{
	char *tmp = bf;
	int ret = asnprintf(&bf, size, "%s/.build-id/%.2s/%s", buildid_dir,
			    sbuild_id, sbuild_id + 2);
	if (ret < 0 || (tmp && size < (unsigned int)ret))
		return NULL;
	return bf;
}

char *dso__build_id_filename(const struct dso *dso, char *bf, size_t size)
{
	char build_id_hex[SBUILD_ID_SIZE];

	if (!dso->has_build_id)
		return NULL;

	build_id__sprintf(dso->build_id, sizeof(dso->build_id), build_id_hex);
	return build_id__filename(build_id_hex, bf, size);
}

#define dsos__for_each_with_build_id(pos, head)	\
	list_for_each_entry(pos, head, node)	\
		if (!pos->has_build_id)		\
			continue;		\
		else

static int write_buildid(const char *name, size_t name_len, u8 *build_id,
			 pid_t pid, u16 misc, int fd)
{
	int err;
	struct build_id_event b;
	size_t len;

	len = name_len + 1;
	len = PERF_ALIGN(len, NAME_ALIGN);

	memset(&b, 0, sizeof(b));
	memcpy(&b.build_id, build_id, BUILD_ID_SIZE);
	b.pid = pid;
	b.header.misc = misc;
	b.header.size = sizeof(b) + len;

	err = writen(fd, &b, sizeof(b));
	if (err < 0)
		return err;

	return write_padded(fd, name, name_len + 1, len);
}

static int machine__write_buildid_table(struct machine *machine, int fd)
{
	int err = 0;
	char nm[PATH_MAX];
	struct dso *pos;
	u16 kmisc = PERF_RECORD_MISC_KERNEL,
	    umisc = PERF_RECORD_MISC_USER;

	if (!machine__is_host(machine)) {
		kmisc = PERF_RECORD_MISC_GUEST_KERNEL;
		umisc = PERF_RECORD_MISC_GUEST_USER;
	}

	dsos__for_each_with_build_id(pos, &machine->dsos.head) {
		const char *name;
		size_t name_len;

		if (!pos->hit)
			continue;

		if (dso__is_vdso(pos)) {
			name = pos->short_name;
			name_len = pos->short_name_len + 1;
		} else if (dso__is_kcore(pos)) {
			machine__mmap_name(machine, nm, sizeof(nm));
			name = nm;
			name_len = strlen(nm) + 1;
		} else {
			name = pos->long_name;
			name_len = pos->long_name_len + 1;
		}

		err = write_buildid(name, name_len, pos->build_id, machine->pid,
				    pos->kernel ? kmisc : umisc, fd);
		if (err)
			break;
	}

	return err;
}

int perf_session__write_buildid_table(struct perf_session *session, int fd)
{
	struct rb_node *nd;
	int err = machine__write_buildid_table(&session->machines.host, fd);

	if (err)
		return err;

	for (nd = rb_first(&session->machines.guests); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);
		err = machine__write_buildid_table(pos, fd);
		if (err)
			break;
	}
	return err;
}

static int __dsos__hit_all(struct list_head *head)
{
	struct dso *pos;

	list_for_each_entry(pos, head, node)
		pos->hit = true;

	return 0;
}

static int machine__hit_all_dsos(struct machine *machine)
{
	return __dsos__hit_all(&machine->dsos.head);
}

int dsos__hit_all(struct perf_session *session)
{
	struct rb_node *nd;
	int err;

	err = machine__hit_all_dsos(&session->machines.host);
	if (err)
		return err;

	for (nd = rb_first(&session->machines.guests); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);

		err = machine__hit_all_dsos(pos);
		if (err)
			return err;
	}

	return 0;
}

void disable_buildid_cache(void)
{
	no_buildid_cache = true;
}

static char *build_id_cache__dirname_from_path(const char *name,
					       bool is_kallsyms, bool is_vdso)
{
	char *realname = (char *)name, *filename;
	bool slash = is_kallsyms || is_vdso;

	if (!slash) {
		realname = realpath(name, NULL);
		if (!realname)
			return NULL;
	}

	if (asprintf(&filename, "%s%s%s", buildid_dir, slash ? "/" : "",
		     is_vdso ? DSO__NAME_VDSO : realname) < 0)
		filename = NULL;

	if (!slash)
		free(realname);

	return filename;
}

int build_id_cache__list_build_ids(const char *pathname,
				   struct strlist **result)
{
	struct strlist *list;
	char *dir_name;
	DIR *dir;
	struct dirent *d;
	int ret = 0;

	list = strlist__new(NULL, NULL);
	dir_name = build_id_cache__dirname_from_path(pathname, false, false);
	if (!list || !dir_name) {
		ret = -ENOMEM;
		goto out;
	}

	/* List up all dirents */
	dir = opendir(dir_name);
	if (!dir) {
		ret = -errno;
		goto out;
	}

	while ((d = readdir(dir)) != NULL) {
		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;
		strlist__add(list, d->d_name);
	}
	closedir(dir);

out:
	free(dir_name);
	if (ret)
		strlist__delete(list);
	else
		*result = list;

	return ret;
}

int build_id_cache__add_s(const char *sbuild_id, const char *name,
			  bool is_kallsyms, bool is_vdso)
{
	const size_t size = PATH_MAX;
	char *realname = NULL, *filename = NULL, *dir_name = NULL,
	     *linkname = zalloc(size), *targetname, *tmp;
	int err = -1;

	if (!is_kallsyms) {
		realname = realpath(name, NULL);
		if (!realname)
			goto out_free;
	}

	dir_name = build_id_cache__dirname_from_path(name, is_kallsyms, is_vdso);
	if (!dir_name)
		goto out_free;

	if (mkdir_p(dir_name, 0755))
		goto out_free;

	if (asprintf(&filename, "%s/%s", dir_name, sbuild_id) < 0) {
		filename = NULL;
		goto out_free;
	}

	if (access(filename, F_OK)) {
		if (is_kallsyms) {
			 if (copyfile("/proc/kallsyms", filename))
				goto out_free;
		} else if (link(realname, filename) && errno != EEXIST &&
				copyfile(name, filename))
			goto out_free;
	}

	if (!build_id__filename(sbuild_id, linkname, size))
		goto out_free;
	tmp = strrchr(linkname, '/');
	*tmp = '\0';

	if (access(linkname, X_OK) && mkdir_p(linkname, 0755))
		goto out_free;

	*tmp = '/';
	targetname = filename + strlen(buildid_dir) - 5;
	memcpy(targetname, "../..", 5);

	if (symlink(targetname, linkname) == 0)
		err = 0;
out_free:
	if (!is_kallsyms)
		free(realname);
	free(filename);
	free(dir_name);
	free(linkname);
	return err;
}

static int build_id_cache__add_b(const u8 *build_id, size_t build_id_size,
				 const char *name, bool is_kallsyms,
				 bool is_vdso)
{
	char sbuild_id[SBUILD_ID_SIZE];

	build_id__sprintf(build_id, build_id_size, sbuild_id);

	return build_id_cache__add_s(sbuild_id, name, is_kallsyms, is_vdso);
}

bool build_id_cache__cached(const char *sbuild_id)
{
	bool ret = false;
	char *filename = build_id__filename(sbuild_id, NULL, 0);

	if (filename && !access(filename, F_OK))
		ret = true;
	free(filename);

	return ret;
}

int build_id_cache__remove_s(const char *sbuild_id)
{
	const size_t size = PATH_MAX;
	char *filename = zalloc(size),
	     *linkname = zalloc(size), *tmp;
	int err = -1;

	if (filename == NULL || linkname == NULL)
		goto out_free;

	if (!build_id__filename(sbuild_id, linkname, size))
		goto out_free;

	if (access(linkname, F_OK))
		goto out_free;

	if (readlink(linkname, filename, size - 1) < 0)
		goto out_free;

	if (unlink(linkname))
		goto out_free;

	/*
	 * Since the link is relative, we must make it absolute:
	 */
	tmp = strrchr(linkname, '/') + 1;
	snprintf(tmp, size - (tmp - linkname), "%s", filename);

	if (unlink(linkname))
		goto out_free;

	err = 0;
out_free:
	free(filename);
	free(linkname);
	return err;
}

static int dso__cache_build_id(struct dso *dso, struct machine *machine)
{
	bool is_kallsyms = dso->kernel && dso->long_name[0] != '/';
	bool is_vdso = dso__is_vdso(dso);
	const char *name = dso->long_name;
	char nm[PATH_MAX];

	if (dso__is_kcore(dso)) {
		is_kallsyms = true;
		machine__mmap_name(machine, nm, sizeof(nm));
		name = nm;
	}
	return build_id_cache__add_b(dso->build_id, sizeof(dso->build_id), name,
				     is_kallsyms, is_vdso);
}

static int __dsos__cache_build_ids(struct list_head *head,
				   struct machine *machine)
{
	struct dso *pos;
	int err = 0;

	dsos__for_each_with_build_id(pos, head)
		if (dso__cache_build_id(pos, machine))
			err = -1;

	return err;
}

static int machine__cache_build_ids(struct machine *machine)
{
	return __dsos__cache_build_ids(&machine->dsos.head, machine);
}

int perf_session__cache_build_ids(struct perf_session *session)
{
	struct rb_node *nd;
	int ret;

	if (no_buildid_cache)
		return 0;

	if (mkdir(buildid_dir, 0755) != 0 && errno != EEXIST)
		return -1;

	ret = machine__cache_build_ids(&session->machines.host);

	for (nd = rb_first(&session->machines.guests); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);
		ret |= machine__cache_build_ids(pos);
	}
	return ret ? -1 : 0;
}

static bool machine__read_build_ids(struct machine *machine, bool with_hits)
{
	return __dsos__read_build_ids(&machine->dsos.head, with_hits);
}

bool perf_session__read_build_ids(struct perf_session *session, bool with_hits)
{
	struct rb_node *nd;
	bool ret = machine__read_build_ids(&session->machines.host, with_hits);

	for (nd = rb_first(&session->machines.guests); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);
		ret |= machine__read_build_ids(pos, with_hits);
	}

	return ret;
}

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
#include "probe-file.h"


static bool no_buildid_cache;

int build_id__mark_dso_hit(struct perf_tool *tool __maybe_unused,
			   union perf_event *event,
			   struct perf_sample *sample,
			   struct perf_evsel *evsel __maybe_unused,
			   struct machine *machine)
{
	struct addr_location al;
	struct thread *thread = machine__findnew_thread(machine, sample->pid,
							sample->tid);

	if (thread == NULL) {
		pr_err("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	thread__find_addr_map(thread, sample->cpumode, MAP__FUNCTION, sample->ip, &al);

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
	.ordered_events	 = true,
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

	return (bid - bf) + 1;
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

char *build_id_cache__kallsyms_path(const char *sbuild_id, char *bf,
				    size_t size)
{
	bool retry_old = true;

	snprintf(bf, size, "%s/%s/%s/kallsyms",
		 buildid_dir, DSO__NAME_KALLSYMS, sbuild_id);
retry:
	if (!access(bf, F_OK))
		return bf;
	if (retry_old) {
		/* Try old style kallsyms cache */
		snprintf(bf, size, "%s/%s/%s",
			 buildid_dir, DSO__NAME_KALLSYMS, sbuild_id);
		retry_old = false;
		goto retry;
	}

	return NULL;
}

char *build_id_cache__linkname(const char *sbuild_id, char *bf, size_t size)
{
	char *tmp = bf;
	int ret = asnprintf(&bf, size, "%s/.build-id/%.2s/%s", buildid_dir,
			    sbuild_id, sbuild_id + 2);
	if (ret < 0 || (tmp && size < (unsigned int)ret))
		return NULL;
	return bf;
}

char *build_id_cache__origname(const char *sbuild_id)
{
	char *linkname;
	char buf[PATH_MAX];
	char *ret = NULL, *p;
	size_t offs = 5;	/* == strlen("../..") */

	linkname = build_id_cache__linkname(sbuild_id, NULL, 0);
	if (!linkname)
		return NULL;

	if (readlink(linkname, buf, PATH_MAX) < 0)
		goto out;
	/* The link should be "../..<origpath>/<sbuild_id>" */
	p = strrchr(buf, '/');	/* Cut off the "/<sbuild_id>" */
	if (p && (p > buf + offs)) {
		*p = '\0';
		if (buf[offs + 1] == '[')
			offs++;	/*
				 * This is a DSO name, like [kernel.kallsyms].
				 * Skip the first '/', since this is not the
				 * cache of a regular file.
				 */
		ret = strdup(buf + offs);	/* Skip "../..[/]" */
	}
out:
	free(linkname);
	return ret;
}

/* Check if the given build_id cache is valid on current running system */
static bool build_id_cache__valid_id(char *sbuild_id)
{
	char real_sbuild_id[SBUILD_ID_SIZE] = "";
	char *pathname;
	int ret = 0;
	bool result = false;

	pathname = build_id_cache__origname(sbuild_id);
	if (!pathname)
		return false;

	if (!strcmp(pathname, DSO__NAME_KALLSYMS))
		ret = sysfs__sprintf_build_id("/", real_sbuild_id);
	else if (pathname[0] == '/')
		ret = filename__sprintf_build_id(pathname, real_sbuild_id);
	else
		ret = -EINVAL;	/* Should we support other special DSO cache? */
	if (ret >= 0)
		result = (strcmp(sbuild_id, real_sbuild_id) == 0);
	free(pathname);

	return result;
}

static const char *build_id_cache__basename(bool is_kallsyms, bool is_vdso)
{
	return is_kallsyms ? "kallsyms" : (is_vdso ? "vdso" : "elf");
}

char *dso__build_id_filename(const struct dso *dso, char *bf, size_t size)
{
	bool is_kallsyms = dso__is_kallsyms((struct dso *)dso);
	bool is_vdso = dso__is_vdso((struct dso *)dso);
	char sbuild_id[SBUILD_ID_SIZE];
	char *linkname;
	bool alloc = (bf == NULL);
	int ret;

	if (!dso->has_build_id)
		return NULL;

	build_id__sprintf(dso->build_id, sizeof(dso->build_id), sbuild_id);
	linkname = build_id_cache__linkname(sbuild_id, NULL, 0);
	if (!linkname)
		return NULL;

	/* Check if old style build_id cache */
	if (is_regular_file(linkname))
		ret = asnprintf(&bf, size, "%s", linkname);
	else
		ret = asnprintf(&bf, size, "%s/%s", linkname,
			 build_id_cache__basename(is_kallsyms, is_vdso));
	if (ret < 0 || (!alloc && size < (unsigned int)ret))
		bf = NULL;
	free(linkname);

	return bf;
}

bool dso__build_id_is_kmod(const struct dso *dso, char *bf, size_t size)
{
	char *id_name = NULL, *ch;
	struct stat sb;
	char sbuild_id[SBUILD_ID_SIZE];

	if (!dso->has_build_id)
		goto err;

	build_id__sprintf(dso->build_id, sizeof(dso->build_id), sbuild_id);
	id_name = build_id_cache__linkname(sbuild_id, NULL, 0);
	if (!id_name)
		goto err;
	if (access(id_name, F_OK))
		goto err;
	if (lstat(id_name, &sb) == -1)
		goto err;
	if ((size_t)sb.st_size > size - 1)
		goto err;
	if (readlink(id_name, bf, size - 1) < 0)
		goto err;

	bf[sb.st_size] = '\0';

	/*
	 * link should be:
	 * ../../lib/modules/4.4.0-rc4/kernel/net/ipv4/netfilter/nf_nat_ipv4.ko/a09fe3eb3147dafa4e3b31dbd6257e4d696bdc92
	 */
	ch = strrchr(bf, '/');
	if (!ch)
		goto err;
	if (ch - 3 < bf)
		goto err;

	free(id_name);
	return strncmp(".ko", ch - 3, 3) == 0;
err:
	pr_err("Invalid build id: %s\n", id_name ? :
					 dso->long_name ? :
					 dso->short_name ? :
					 "[unknown]");
	free(id_name);
	return false;
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
		bool in_kernel = false;

		if (!pos->hit && !dso__is_vdso(pos))
			continue;

		if (dso__is_vdso(pos)) {
			name = pos->short_name;
			name_len = pos->short_name_len;
		} else if (dso__is_kcore(pos)) {
			machine__mmap_name(machine, nm, sizeof(nm));
			name = nm;
			name_len = strlen(nm);
		} else {
			name = pos->long_name;
			name_len = pos->long_name_len;
		}

		in_kernel = pos->kernel ||
				is_kernel_module(name,
					PERF_RECORD_MISC_CPUMODE_UNKNOWN);
		err = write_buildid(name, name_len, pos->build_id, machine->pid,
				    in_kernel ? kmisc : umisc, fd);
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

static bool lsdir_bid_head_filter(const char *name __maybe_unused,
				  struct dirent *d __maybe_unused)
{
	return (strlen(d->d_name) == 2) &&
		isxdigit(d->d_name[0]) && isxdigit(d->d_name[1]);
}

static bool lsdir_bid_tail_filter(const char *name __maybe_unused,
				  struct dirent *d __maybe_unused)
{
	int i = 0;
	while (isxdigit(d->d_name[i]) && i < SBUILD_ID_SIZE - 3)
		i++;
	return (i == SBUILD_ID_SIZE - 3) && (d->d_name[i] == '\0');
}

struct strlist *build_id_cache__list_all(bool validonly)
{
	struct strlist *toplist, *linklist = NULL, *bidlist;
	struct str_node *nd, *nd2;
	char *topdir, *linkdir = NULL;
	char sbuild_id[SBUILD_ID_SIZE];

	/* for filename__ functions */
	if (validonly)
		symbol__init(NULL);

	/* Open the top-level directory */
	if (asprintf(&topdir, "%s/.build-id/", buildid_dir) < 0)
		return NULL;

	bidlist = strlist__new(NULL, NULL);
	if (!bidlist)
		goto out;

	toplist = lsdir(topdir, lsdir_bid_head_filter);
	if (!toplist) {
		pr_debug("Error in lsdir(%s): %d\n", topdir, errno);
		/* If there is no buildid cache, return an empty list */
		if (errno == ENOENT)
			goto out;
		goto err_out;
	}

	strlist__for_each_entry(nd, toplist) {
		if (asprintf(&linkdir, "%s/%s", topdir, nd->s) < 0)
			goto err_out;
		/* Open the lower-level directory */
		linklist = lsdir(linkdir, lsdir_bid_tail_filter);
		if (!linklist) {
			pr_debug("Error in lsdir(%s): %d\n", linkdir, errno);
			goto err_out;
		}
		strlist__for_each_entry(nd2, linklist) {
			if (snprintf(sbuild_id, SBUILD_ID_SIZE, "%s%s",
				     nd->s, nd2->s) != SBUILD_ID_SIZE - 1)
				goto err_out;
			if (validonly && !build_id_cache__valid_id(sbuild_id))
				continue;
			if (strlist__add(bidlist, sbuild_id) < 0)
				goto err_out;
		}
		strlist__delete(linklist);
		zfree(&linkdir);
	}

out_free:
	strlist__delete(toplist);
out:
	free(topdir);

	return bidlist;

err_out:
	strlist__delete(linklist);
	zfree(&linkdir);
	strlist__delete(bidlist);
	bidlist = NULL;
	goto out_free;
}

static bool str_is_build_id(const char *maybe_sbuild_id, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (!isxdigit(maybe_sbuild_id[i]))
			return false;
	}
	return true;
}

/* Return the valid complete build-id */
char *build_id_cache__complement(const char *incomplete_sbuild_id)
{
	struct strlist *bidlist;
	struct str_node *nd, *cand = NULL;
	char *sbuild_id = NULL;
	size_t len = strlen(incomplete_sbuild_id);

	if (len >= SBUILD_ID_SIZE ||
	    !str_is_build_id(incomplete_sbuild_id, len))
		return NULL;

	bidlist = build_id_cache__list_all(true);
	if (!bidlist)
		return NULL;

	strlist__for_each_entry(nd, bidlist) {
		if (strncmp(nd->s, incomplete_sbuild_id, len) != 0)
			continue;
		if (cand) {	/* Error: There are more than 2 candidates. */
			cand = NULL;
			break;
		}
		cand = nd;
	}
	if (cand)
		sbuild_id = strdup(cand->s);
	strlist__delete(bidlist);

	return sbuild_id;
}

char *build_id_cache__cachedir(const char *sbuild_id, const char *name,
			       bool is_kallsyms, bool is_vdso)
{
	char *realname = (char *)name, *filename;
	bool slash = is_kallsyms || is_vdso;

	if (!slash) {
		realname = realpath(name, NULL);
		if (!realname)
			return NULL;
	}

	if (asprintf(&filename, "%s%s%s%s%s", buildid_dir, slash ? "/" : "",
		     is_vdso ? DSO__NAME_VDSO : realname,
		     sbuild_id ? "/" : "", sbuild_id ?: "") < 0)
		filename = NULL;

	if (!slash)
		free(realname);

	return filename;
}

int build_id_cache__list_build_ids(const char *pathname,
				   struct strlist **result)
{
	char *dir_name;
	int ret = 0;

	dir_name = build_id_cache__cachedir(NULL, pathname, false, false);
	if (!dir_name)
		return -ENOMEM;

	*result = lsdir(dir_name, lsdir_no_dot_filter);
	if (!*result)
		ret = -errno;
	free(dir_name);

	return ret;
}

#if defined(HAVE_LIBELF_SUPPORT) && defined(HAVE_GELF_GETNOTE_SUPPORT)
static int build_id_cache__add_sdt_cache(const char *sbuild_id,
					  const char *realname)
{
	struct probe_cache *cache;
	int ret;

	cache = probe_cache__new(sbuild_id);
	if (!cache)
		return -1;

	ret = probe_cache__scan_sdt(cache, realname);
	if (ret >= 0) {
		pr_debug4("Found %d SDTs in %s\n", ret, realname);
		if (probe_cache__commit(cache) < 0)
			ret = -1;
	}
	probe_cache__delete(cache);
	return ret;
}
#else
#define build_id_cache__add_sdt_cache(sbuild_id, realname) (0)
#endif

int build_id_cache__add_s(const char *sbuild_id, const char *name,
			  bool is_kallsyms, bool is_vdso)
{
	const size_t size = PATH_MAX;
	char *realname = NULL, *filename = NULL, *dir_name = NULL,
	     *linkname = zalloc(size), *tmp;
	int err = -1;

	if (!is_kallsyms) {
		realname = realpath(name, NULL);
		if (!realname)
			goto out_free;
	}

	dir_name = build_id_cache__cachedir(sbuild_id, name,
					    is_kallsyms, is_vdso);
	if (!dir_name)
		goto out_free;

	/* Remove old style build-id cache */
	if (is_regular_file(dir_name))
		if (unlink(dir_name))
			goto out_free;

	if (mkdir_p(dir_name, 0755))
		goto out_free;

	/* Save the allocated buildid dirname */
	if (asprintf(&filename, "%s/%s", dir_name,
		     build_id_cache__basename(is_kallsyms, is_vdso)) < 0) {
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

	if (!build_id_cache__linkname(sbuild_id, linkname, size))
		goto out_free;
	tmp = strrchr(linkname, '/');
	*tmp = '\0';

	if (access(linkname, X_OK) && mkdir_p(linkname, 0755))
		goto out_free;

	*tmp = '/';
	tmp = dir_name + strlen(buildid_dir) - 5;
	memcpy(tmp, "../..", 5);

	if (symlink(tmp, linkname) == 0)
		err = 0;

	/* Update SDT cache : error is just warned */
	if (build_id_cache__add_sdt_cache(sbuild_id, realname) < 0)
		pr_debug4("Failed to update/scan SDT cache for %s\n", realname);

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
	char *filename = build_id_cache__linkname(sbuild_id, NULL, 0);

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

	if (!build_id_cache__linkname(sbuild_id, linkname, size))
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

	if (rm_rf(linkname))
		goto out_free;

	err = 0;
out_free:
	free(filename);
	free(linkname);
	return err;
}

static int dso__cache_build_id(struct dso *dso, struct machine *machine)
{
	bool is_kallsyms = dso__is_kallsyms(dso);
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

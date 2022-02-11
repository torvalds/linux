// SPDX-License-Identifier: GPL-2.0
/*
 * build-id.c
 *
 * build-id support
 *
 * Copyright (C) 2009, 2010 Red Hat Inc.
 * Copyright (C) 2009, 2010 Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "util.h" // lsdir(), mkdir_p(), rm_rf()
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "util/copyfile.h"
#include "dso.h"
#include "build-id.h"
#include "event.h"
#include "namespaces.h"
#include "map.h"
#include "symbol.h"
#include "thread.h"
#include <linux/kernel.h>
#include "debug.h"
#include "session.h"
#include "tool.h"
#include "header.h"
#include "vdso.h"
#include "path.h"
#include "probe-file.h"
#include "strlist.h"

#ifdef HAVE_DEBUGINFOD_SUPPORT
#include <elfutils/debuginfod.h>
#endif

#include <linux/ctype.h>
#include <linux/zalloc.h>
#include <linux/string.h>
#include <asm/bug.h>

static bool no_buildid_cache;

int build_id__mark_dso_hit(struct perf_tool *tool __maybe_unused,
			   union perf_event *event,
			   struct perf_sample *sample,
			   struct evsel *evsel __maybe_unused,
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

	if (thread__find_map(thread, sample->cpumode, sample->ip, &al))
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

int build_id__sprintf(const struct build_id *build_id, char *bf)
{
	char *bid = bf;
	const u8 *raw = build_id->data;
	size_t i;

	bf[0] = 0x0;

	for (i = 0; i < build_id->size; ++i) {
		sprintf(bid, "%02x", *raw);
		++raw;
		bid += 2;
	}

	return (bid - bf) + 1;
}

int sysfs__sprintf_build_id(const char *root_dir, char *sbuild_id)
{
	char notes[PATH_MAX];
	struct build_id bid;
	int ret;

	if (!root_dir)
		root_dir = "";

	scnprintf(notes, sizeof(notes), "%s/sys/kernel/notes", root_dir);

	ret = sysfs__read_build_id(notes, &bid);
	if (ret < 0)
		return ret;

	return build_id__sprintf(&bid, sbuild_id);
}

int filename__sprintf_build_id(const char *pathname, char *sbuild_id)
{
	struct build_id bid;
	int ret;

	ret = filename__read_build_id(pathname, &bid);
	if (ret < 0)
		return ret;

	return build_id__sprintf(&bid, sbuild_id);
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

/* The caller is responsible to free the returned buffer. */
char *build_id_cache__origname(const char *sbuild_id)
{
	char *linkname;
	char buf[PATH_MAX];
	char *ret = NULL, *p;
	size_t offs = 5;	/* == strlen("../..") */
	ssize_t len;

	linkname = build_id_cache__linkname(sbuild_id, NULL, 0);
	if (!linkname)
		return NULL;

	len = readlink(linkname, buf, sizeof(buf) - 1);
	if (len <= 0)
		goto out;
	buf[len] = '\0';

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

static const char *build_id_cache__basename(bool is_kallsyms, bool is_vdso,
					    bool is_debug)
{
	return is_kallsyms ? "kallsyms" : (is_vdso ? "vdso" : (is_debug ?
	    "debug" : "elf"));
}

char *__dso__build_id_filename(const struct dso *dso, char *bf, size_t size,
			       bool is_debug, bool is_kallsyms)
{
	bool is_vdso = dso__is_vdso((struct dso *)dso);
	char sbuild_id[SBUILD_ID_SIZE];
	char *linkname;
	bool alloc = (bf == NULL);
	int ret;

	if (!dso->has_build_id)
		return NULL;

	build_id__sprintf(&dso->bid, sbuild_id);
	linkname = build_id_cache__linkname(sbuild_id, NULL, 0);
	if (!linkname)
		return NULL;

	/* Check if old style build_id cache */
	if (is_regular_file(linkname))
		ret = asnprintf(&bf, size, "%s", linkname);
	else
		ret = asnprintf(&bf, size, "%s/%s", linkname,
			 build_id_cache__basename(is_kallsyms, is_vdso,
						  is_debug));
	if (ret < 0 || (!alloc && size < (unsigned int)ret))
		bf = NULL;
	free(linkname);

	return bf;
}

char *dso__build_id_filename(const struct dso *dso, char *bf, size_t size,
			     bool is_debug)
{
	bool is_kallsyms = dso__is_kallsyms((struct dso *)dso);

	return __dso__build_id_filename(dso, bf, size, is_debug, is_kallsyms);
}

#define dsos__for_each_with_build_id(pos, head)	\
	list_for_each_entry(pos, head, node)	\
		if (!pos->has_build_id)		\
			continue;		\
		else

static int write_buildid(const char *name, size_t name_len, struct build_id *bid,
			 pid_t pid, u16 misc, struct feat_fd *fd)
{
	int err;
	struct perf_record_header_build_id b;
	size_t len;

	len = name_len + 1;
	len = PERF_ALIGN(len, NAME_ALIGN);

	memset(&b, 0, sizeof(b));
	memcpy(&b.data, bid->data, bid->size);
	b.size = (u8) bid->size;
	misc |= PERF_RECORD_MISC_BUILD_ID_SIZE;
	b.pid = pid;
	b.header.misc = misc;
	b.header.size = sizeof(b) + len;

	err = do_write(fd, &b, sizeof(b));
	if (err < 0)
		return err;

	return write_padded(fd, name, name_len + 1, len);
}

static int machine__write_buildid_table(struct machine *machine,
					struct feat_fd *fd)
{
	int err = 0;
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
			name = machine->mmap_name;
			name_len = strlen(name);
		} else {
			name = pos->long_name;
			name_len = pos->long_name_len;
		}

		in_kernel = pos->kernel ||
				is_kernel_module(name,
					PERF_RECORD_MISC_CPUMODE_UNKNOWN);
		err = write_buildid(name, name_len, &pos->bid, machine->pid,
				    in_kernel ? kmisc : umisc, fd);
		if (err)
			break;
	}

	return err;
}

int perf_session__write_buildid_table(struct perf_session *session,
				      struct feat_fd *fd)
{
	struct rb_node *nd;
	int err = machine__write_buildid_table(&session->machines.host, fd);

	if (err)
		return err;

	for (nd = rb_first_cached(&session->machines.guests); nd;
	     nd = rb_next(nd)) {
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

	for (nd = rb_first_cached(&session->machines.guests); nd;
	     nd = rb_next(nd)) {
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
				  struct dirent *d)
{
	return (strlen(d->d_name) == 2) &&
		isxdigit(d->d_name[0]) && isxdigit(d->d_name[1]);
}

static bool lsdir_bid_tail_filter(const char *name __maybe_unused,
				  struct dirent *d)
{
	int i = 0;
	while (isxdigit(d->d_name[i]) && i < SBUILD_ID_SIZE - 3)
		i++;
	return (i >= SBUILD_ID_MIN_SIZE - 3) && (i <= SBUILD_ID_SIZE - 3) &&
		(d->d_name[i] == '\0');
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
				     nd->s, nd2->s) > SBUILD_ID_SIZE - 1)
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
			       struct nsinfo *nsi, bool is_kallsyms,
			       bool is_vdso)
{
	char *realname = (char *)name, *filename;
	bool slash = is_kallsyms || is_vdso;

	if (!slash) {
		realname = nsinfo__realpath(name, nsi);
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

int build_id_cache__list_build_ids(const char *pathname, struct nsinfo *nsi,
				   struct strlist **result)
{
	char *dir_name;
	int ret = 0;

	dir_name = build_id_cache__cachedir(NULL, pathname, nsi, false, false);
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
					  const char *realname,
					  struct nsinfo *nsi)
{
	struct probe_cache *cache;
	int ret;
	struct nscookie nsc;

	cache = probe_cache__new(sbuild_id, nsi);
	if (!cache)
		return -1;

	nsinfo__mountns_enter(nsi, &nsc);
	ret = probe_cache__scan_sdt(cache, realname);
	nsinfo__mountns_exit(&nsc);
	if (ret >= 0) {
		pr_debug4("Found %d SDTs in %s\n", ret, realname);
		if (probe_cache__commit(cache) < 0)
			ret = -1;
	}
	probe_cache__delete(cache);
	return ret;
}
#else
#define build_id_cache__add_sdt_cache(sbuild_id, realname, nsi) (0)
#endif

static char *build_id_cache__find_debug(const char *sbuild_id,
					struct nsinfo *nsi)
{
	char *realname = NULL;
	char *debugfile;
	struct nscookie nsc;
	size_t len = 0;

	debugfile = calloc(1, PATH_MAX);
	if (!debugfile)
		goto out;

	len = __symbol__join_symfs(debugfile, PATH_MAX,
				   "/usr/lib/debug/.build-id/");
	snprintf(debugfile + len, PATH_MAX - len, "%.2s/%s.debug", sbuild_id,
		 sbuild_id + 2);

	nsinfo__mountns_enter(nsi, &nsc);
	realname = realpath(debugfile, NULL);
	if (realname && access(realname, R_OK))
		zfree(&realname);
	nsinfo__mountns_exit(&nsc);

#ifdef HAVE_DEBUGINFOD_SUPPORT
        if (realname == NULL) {
                debuginfod_client* c = debuginfod_begin();
                if (c != NULL) {
                        int fd = debuginfod_find_debuginfo(c,
                                                           (const unsigned char*)sbuild_id, 0,
                                                           &realname);
                        if (fd >= 0)
                                close(fd); /* retaining reference by realname */
                        debuginfod_end(c);
                }
        }
#endif

out:
	free(debugfile);
	return realname;
}

int
build_id_cache__add(const char *sbuild_id, const char *name, const char *realname,
		    struct nsinfo *nsi, bool is_kallsyms, bool is_vdso)
{
	const size_t size = PATH_MAX;
	char *filename = NULL, *dir_name = NULL, *linkname = zalloc(size), *tmp;
	char *debugfile = NULL;
	int err = -1;

	dir_name = build_id_cache__cachedir(sbuild_id, name, nsi, is_kallsyms,
					    is_vdso);
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
		     build_id_cache__basename(is_kallsyms, is_vdso,
		     false)) < 0) {
		filename = NULL;
		goto out_free;
	}

	if (access(filename, F_OK)) {
		if (is_kallsyms) {
			if (copyfile("/proc/kallsyms", filename))
				goto out_free;
		} else if (nsi && nsinfo__need_setns(nsi)) {
			if (copyfile_ns(name, filename, nsi))
				goto out_free;
		} else if (link(realname, filename) && errno != EEXIST &&
				copyfile(name, filename))
			goto out_free;
	}

	/* Some binaries are stripped, but have .debug files with their symbol
	 * table.  Check to see if we can locate one of those, since the elf
	 * file itself may not be very useful to users of our tools without a
	 * symtab.
	 */
	if (!is_kallsyms && !is_vdso &&
	    strncmp(".ko", name + strlen(name) - 3, 3)) {
		debugfile = build_id_cache__find_debug(sbuild_id, nsi);
		if (debugfile) {
			zfree(&filename);
			if (asprintf(&filename, "%s/%s", dir_name,
			    build_id_cache__basename(false, false, true)) < 0) {
				filename = NULL;
				goto out_free;
			}
			if (access(filename, F_OK)) {
				if (nsi && nsinfo__need_setns(nsi)) {
					if (copyfile_ns(debugfile, filename,
							nsi))
						goto out_free;
				} else if (link(debugfile, filename) &&
						errno != EEXIST &&
						copyfile(debugfile, filename))
					goto out_free;
			}
		}
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

	if (symlink(tmp, linkname) == 0) {
		err = 0;
	} else if (errno == EEXIST) {
		char path[PATH_MAX];
		ssize_t len;

		len = readlink(linkname, path, sizeof(path) - 1);
		if (len <= 0) {
			pr_err("Cant read link: %s\n", linkname);
			goto out_free;
		}
		path[len] = '\0';

		if (strcmp(tmp, path)) {
			pr_debug("build <%s> already linked to %s\n",
				 sbuild_id, linkname);
		}
		err = 0;
	}

	/* Update SDT cache : error is just warned */
	if (realname &&
	    build_id_cache__add_sdt_cache(sbuild_id, realname, nsi) < 0)
		pr_debug4("Failed to update/scan SDT cache for %s\n", realname);

out_free:
	free(filename);
	free(debugfile);
	free(dir_name);
	free(linkname);
	return err;
}

int build_id_cache__add_s(const char *sbuild_id, const char *name,
			  struct nsinfo *nsi, bool is_kallsyms, bool is_vdso)
{
	char *realname = NULL;
	int err = -1;

	if (!is_kallsyms) {
		if (!is_vdso)
			realname = nsinfo__realpath(name, nsi);
		else
			realname = realpath(name, NULL);
		if (!realname)
			goto out_free;
	}

	err = build_id_cache__add(sbuild_id, name, realname, nsi, is_kallsyms, is_vdso);

out_free:
	if (!is_kallsyms)
		free(realname);
	return err;
}

static int build_id_cache__add_b(const struct build_id *bid,
				 const char *name, struct nsinfo *nsi,
				 bool is_kallsyms, bool is_vdso)
{
	char sbuild_id[SBUILD_ID_SIZE];

	build_id__sprintf(bid, sbuild_id);

	return build_id_cache__add_s(sbuild_id, name, nsi, is_kallsyms,
				     is_vdso);
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

static int dso__cache_build_id(struct dso *dso, struct machine *machine,
			       void *priv __maybe_unused)
{
	bool is_kallsyms = dso__is_kallsyms(dso);
	bool is_vdso = dso__is_vdso(dso);
	const char *name = dso->long_name;

	if (!dso->has_build_id)
		return 0;

	if (dso__is_kcore(dso)) {
		is_kallsyms = true;
		name = machine->mmap_name;
	}
	return build_id_cache__add_b(&dso->bid, name, dso->nsinfo,
				     is_kallsyms, is_vdso);
}

static int
machines__for_each_dso(struct machines *machines, machine__dso_t fn, void *priv)
{
	int ret = machine__for_each_dso(&machines->host, fn, priv);
	struct rb_node *nd;

	for (nd = rb_first_cached(&machines->guests); nd;
	     nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);

		ret |= machine__for_each_dso(pos, fn, priv);
	}
	return ret ? -1 : 0;
}

int __perf_session__cache_build_ids(struct perf_session *session,
				    machine__dso_t fn, void *priv)
{
	if (no_buildid_cache)
		return 0;

	if (mkdir(buildid_dir, 0755) != 0 && errno != EEXIST)
		return -1;

	return machines__for_each_dso(&session->machines, fn, priv) ?  -1 : 0;
}

int perf_session__cache_build_ids(struct perf_session *session)
{
	return __perf_session__cache_build_ids(session, dso__cache_build_id, NULL);
}

static bool machine__read_build_ids(struct machine *machine, bool with_hits)
{
	return __dsos__read_build_ids(&machine->dsos.head, with_hits);
}

bool perf_session__read_build_ids(struct perf_session *session, bool with_hits)
{
	struct rb_node *nd;
	bool ret = machine__read_build_ids(&session->machines.host, with_hits);

	for (nd = rb_first_cached(&session->machines.guests); nd;
	     nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);
		ret |= machine__read_build_ids(pos, with_hits);
	}

	return ret;
}

void build_id__init(struct build_id *bid, const u8 *data, size_t size)
{
	WARN_ON(size > BUILD_ID_SIZE);
	memcpy(bid->data, data, size);
	bid->size = size;
}

bool build_id__is_defined(const struct build_id *bid)
{
	return bid && bid->size ? !!memchr_inv(bid->data, 0, bid->size) : false;
}

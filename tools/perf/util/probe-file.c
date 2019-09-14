// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * probe-file.c : operate ftrace k/uprobe events files
 *
 * Written by Masami Hiramatsu <masami.hiramatsu.pt@hitachi.com>
 */
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <linux/zalloc.h>
#include "namespaces.h"
#include "event.h"
#include "strlist.h"
#include "strfilter.h"
#include "debug.h"
#include "cache.h"
#include "color.h"
#include "symbol.h"
#include "thread.h"
#include <api/fs/tracing_path.h>
#include "probe-event.h"
#include "probe-file.h"
#include "session.h"
#include "perf_regs.h"
#include "string2.h"

/* 4096 - 2 ('\n' + '\0') */
#define MAX_CMDLEN 4094

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
			   str_error_r(-err, sbuf, sizeof(sbuf)));
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
			   str_error_r(-kerr, sbuf, sizeof(sbuf)));
		pr_warning("Failed to open uprobe events: %s.\n",
			   str_error_r(-uerr, sbuf, sizeof(sbuf)));
	}
}

int open_trace_file(const char *trace_file, bool readwrite)
{
	char buf[PATH_MAX];
	int ret;

	ret = e_snprintf(buf, PATH_MAX, "%s/%s", tracing_path_mount(), trace_file);
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
	return open_trace_file("kprobe_events", readwrite);
}

static int open_uprobe_events(bool readwrite)
{
	return open_trace_file("uprobe_events", readwrite);
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
	int ret, idx, fddup;
	FILE *fp;
	char buf[MAX_CMDLEN];
	char *p;
	struct strlist *sl;

	if (fd < 0)
		return NULL;

	sl = strlist__new(NULL, NULL);
	if (sl == NULL)
		return NULL;

	fddup = dup(fd);
	if (fddup < 0)
		goto out_free_sl;

	fp = fdopen(fddup, "r");
	if (!fp)
		goto out_close_fddup;

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
			goto out_close_fp;
		}
	}
	fclose(fp);

	return sl;

out_close_fp:
	fclose(fp);
	goto out_free_sl;
out_close_fddup:
	close(fddup);
out_free_sl:
	strlist__delete(sl);
	return NULL;
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
	strlist__for_each_entry(ent, rawlist) {
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
		if (write(fd, buf, strlen(buf)) < (int)strlen(buf)) {
			ret = -errno;
			pr_warning("Failed to write event: %s\n",
				   str_error_r(errno, sbuf, sizeof(sbuf)));
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
		   str_error_r(-ret, buf, sizeof(buf)));
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

	strlist__for_each_entry(ent, namelist) {
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

	strlist__for_each_entry(ent, namelist) {
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

/* Caller must ensure to remove this entry from list */
static void probe_cache_entry__delete(struct probe_cache_entry *entry)
{
	if (entry) {
		BUG_ON(!list_empty(&entry->node));

		strlist__delete(entry->tevlist);
		clear_perf_probe_event(&entry->pev);
		zfree(&entry->spev);
		free(entry);
	}
}

static struct probe_cache_entry *
probe_cache_entry__new(struct perf_probe_event *pev)
{
	struct probe_cache_entry *entry = zalloc(sizeof(*entry));

	if (entry) {
		INIT_LIST_HEAD(&entry->node);
		entry->tevlist = strlist__new(NULL, NULL);
		if (!entry->tevlist)
			zfree(&entry);
		else if (pev) {
			entry->spev = synthesize_perf_probe_command(pev);
			if (!entry->spev ||
			    perf_probe_event__copy(&entry->pev, pev) < 0) {
				probe_cache_entry__delete(entry);
				return NULL;
			}
		}
	}

	return entry;
}

int probe_cache_entry__get_event(struct probe_cache_entry *entry,
				 struct probe_trace_event **tevs)
{
	struct probe_trace_event *tev;
	struct str_node *node;
	int ret, i;

	ret = strlist__nr_entries(entry->tevlist);
	if (ret > probe_conf.max_probes)
		return -E2BIG;

	*tevs = zalloc(ret * sizeof(*tev));
	if (!*tevs)
		return -ENOMEM;

	i = 0;
	strlist__for_each_entry(node, entry->tevlist) {
		tev = &(*tevs)[i++];
		ret = parse_probe_trace_command(node->s, tev);
		if (ret < 0)
			break;
	}
	return i;
}

/* For the kernel probe caches, pass target = NULL or DSO__NAME_KALLSYMS */
static int probe_cache__open(struct probe_cache *pcache, const char *target,
			     struct nsinfo *nsi)
{
	char cpath[PATH_MAX];
	char sbuildid[SBUILD_ID_SIZE];
	char *dir_name = NULL;
	bool is_kallsyms = false;
	int ret, fd;
	struct nscookie nsc;

	if (target && build_id_cache__cached(target)) {
		/* This is a cached buildid */
		strlcpy(sbuildid, target, SBUILD_ID_SIZE);
		dir_name = build_id_cache__linkname(sbuildid, NULL, 0);
		goto found;
	}

	if (!target || !strcmp(target, DSO__NAME_KALLSYMS)) {
		target = DSO__NAME_KALLSYMS;
		is_kallsyms = true;
		ret = sysfs__sprintf_build_id("/", sbuildid);
	} else {
		nsinfo__mountns_enter(nsi, &nsc);
		ret = filename__sprintf_build_id(target, sbuildid);
		nsinfo__mountns_exit(&nsc);
	}

	if (ret < 0) {
		pr_debug("Failed to get build-id from %s.\n", target);
		return ret;
	}

	/* If we have no buildid cache, make it */
	if (!build_id_cache__cached(sbuildid)) {
		ret = build_id_cache__add_s(sbuildid, target, nsi,
					    is_kallsyms, NULL);
		if (ret < 0) {
			pr_debug("Failed to add build-id cache: %s\n", target);
			return ret;
		}
	}

	dir_name = build_id_cache__cachedir(sbuildid, target, nsi, is_kallsyms,
					    false);
found:
	if (!dir_name) {
		pr_debug("Failed to get cache from %s\n", target);
		return -ENOMEM;
	}

	snprintf(cpath, PATH_MAX, "%s/probes", dir_name);
	fd = open(cpath, O_CREAT | O_RDWR, 0644);
	if (fd < 0)
		pr_debug("Failed to open cache(%d): %s\n", fd, cpath);
	free(dir_name);
	pcache->fd = fd;

	return fd;
}

static int probe_cache__load(struct probe_cache *pcache)
{
	struct probe_cache_entry *entry = NULL;
	char buf[MAX_CMDLEN], *p;
	int ret = 0, fddup;
	FILE *fp;

	fddup = dup(pcache->fd);
	if (fddup < 0)
		return -errno;
	fp = fdopen(fddup, "r");
	if (!fp) {
		close(fddup);
		return -EINVAL;
	}

	while (!feof(fp)) {
		if (!fgets(buf, MAX_CMDLEN, fp))
			break;
		p = strchr(buf, '\n');
		if (p)
			*p = '\0';
		/* #perf_probe_event or %sdt_event */
		if (buf[0] == '#' || buf[0] == '%') {
			entry = probe_cache_entry__new(NULL);
			if (!entry) {
				ret = -ENOMEM;
				goto out;
			}
			if (buf[0] == '%')
				entry->sdt = true;
			entry->spev = strdup(buf + 1);
			if (entry->spev)
				ret = parse_perf_probe_command(buf + 1,
								&entry->pev);
			else
				ret = -ENOMEM;
			if (ret < 0) {
				probe_cache_entry__delete(entry);
				goto out;
			}
			list_add_tail(&entry->node, &pcache->entries);
		} else {	/* trace_probe_event */
			if (!entry) {
				ret = -EINVAL;
				goto out;
			}
			strlist__add(entry->tevlist, buf);
		}
	}
out:
	fclose(fp);
	return ret;
}

static struct probe_cache *probe_cache__alloc(void)
{
	struct probe_cache *pcache = zalloc(sizeof(*pcache));

	if (pcache) {
		INIT_LIST_HEAD(&pcache->entries);
		pcache->fd = -EINVAL;
	}
	return pcache;
}

void probe_cache__purge(struct probe_cache *pcache)
{
	struct probe_cache_entry *entry, *n;

	list_for_each_entry_safe(entry, n, &pcache->entries, node) {
		list_del_init(&entry->node);
		probe_cache_entry__delete(entry);
	}
}

void probe_cache__delete(struct probe_cache *pcache)
{
	if (!pcache)
		return;

	probe_cache__purge(pcache);
	if (pcache->fd > 0)
		close(pcache->fd);
	free(pcache);
}

struct probe_cache *probe_cache__new(const char *target, struct nsinfo *nsi)
{
	struct probe_cache *pcache = probe_cache__alloc();
	int ret;

	if (!pcache)
		return NULL;

	ret = probe_cache__open(pcache, target, nsi);
	if (ret < 0) {
		pr_debug("Cache open error: %d\n", ret);
		goto out_err;
	}

	ret = probe_cache__load(pcache);
	if (ret < 0) {
		pr_debug("Cache read error: %d\n", ret);
		goto out_err;
	}

	return pcache;

out_err:
	probe_cache__delete(pcache);
	return NULL;
}

static bool streql(const char *a, const char *b)
{
	if (a == b)
		return true;

	if (!a || !b)
		return false;

	return !strcmp(a, b);
}

struct probe_cache_entry *
probe_cache__find(struct probe_cache *pcache, struct perf_probe_event *pev)
{
	struct probe_cache_entry *entry = NULL;
	char *cmd = synthesize_perf_probe_command(pev);

	if (!cmd)
		return NULL;

	for_each_probe_cache_entry(entry, pcache) {
		if (pev->sdt) {
			if (entry->pev.event &&
			    streql(entry->pev.event, pev->event) &&
			    (!pev->group ||
			     streql(entry->pev.group, pev->group)))
				goto found;

			continue;
		}
		/* Hit if same event name or same command-string */
		if ((pev->event &&
		     (streql(entry->pev.group, pev->group) &&
		      streql(entry->pev.event, pev->event))) ||
		    (!strcmp(entry->spev, cmd)))
			goto found;
	}
	entry = NULL;

found:
	free(cmd);
	return entry;
}

struct probe_cache_entry *
probe_cache__find_by_name(struct probe_cache *pcache,
			  const char *group, const char *event)
{
	struct probe_cache_entry *entry = NULL;

	for_each_probe_cache_entry(entry, pcache) {
		/* Hit if same event name or same command-string */
		if (streql(entry->pev.group, group) &&
		    streql(entry->pev.event, event))
			goto found;
	}
	entry = NULL;

found:
	return entry;
}

int probe_cache__add_entry(struct probe_cache *pcache,
			   struct perf_probe_event *pev,
			   struct probe_trace_event *tevs, int ntevs)
{
	struct probe_cache_entry *entry = NULL;
	char *command;
	int i, ret = 0;

	if (!pcache || !pev || !tevs || ntevs <= 0) {
		ret = -EINVAL;
		goto out_err;
	}

	/* Remove old cache entry */
	entry = probe_cache__find(pcache, pev);
	if (entry) {
		list_del_init(&entry->node);
		probe_cache_entry__delete(entry);
	}

	ret = -ENOMEM;
	entry = probe_cache_entry__new(pev);
	if (!entry)
		goto out_err;

	for (i = 0; i < ntevs; i++) {
		if (!tevs[i].point.symbol)
			continue;

		command = synthesize_probe_trace_command(&tevs[i]);
		if (!command)
			goto out_err;
		strlist__add(entry->tevlist, command);
		free(command);
	}
	list_add_tail(&entry->node, &pcache->entries);
	pr_debug("Added probe cache: %d\n", ntevs);
	return 0;

out_err:
	pr_debug("Failed to add probe caches\n");
	probe_cache_entry__delete(entry);
	return ret;
}

#ifdef HAVE_GELF_GETNOTE_SUPPORT
static unsigned long long sdt_note__get_addr(struct sdt_note *note)
{
	return note->bit32 ?
		(unsigned long long)note->addr.a32[SDT_NOTE_IDX_LOC] :
		(unsigned long long)note->addr.a64[SDT_NOTE_IDX_LOC];
}

static unsigned long long sdt_note__get_ref_ctr_offset(struct sdt_note *note)
{
	return note->bit32 ?
		(unsigned long long)note->addr.a32[SDT_NOTE_IDX_REFCTR] :
		(unsigned long long)note->addr.a64[SDT_NOTE_IDX_REFCTR];
}

static const char * const type_to_suffix[] = {
	":s64", "", "", "", ":s32", "", ":s16", ":s8",
	"", ":u8", ":u16", "", ":u32", "", "", "", ":u64"
};

/*
 * Isolate the string number and convert it into a decimal value;
 * this will be an index to get suffix of the uprobe name (defining
 * the type)
 */
static int sdt_arg_parse_size(char *n_ptr, const char **suffix)
{
	long type_idx;

	type_idx = strtol(n_ptr, NULL, 10);
	if (type_idx < -8 || type_idx > 8) {
		pr_debug4("Failed to get a valid sdt type\n");
		return -1;
	}

	*suffix = type_to_suffix[type_idx + 8];
	return 0;
}

static int synthesize_sdt_probe_arg(struct strbuf *buf, int i, const char *arg)
{
	char *op, *desc = strdup(arg), *new_op = NULL;
	const char *suffix = "";
	int ret = -1;

	if (desc == NULL) {
		pr_debug4("Allocation error\n");
		return ret;
	}

	/*
	 * Argument is in N@OP format. N is size of the argument and OP is
	 * the actual assembly operand. N can be omitted; in that case
	 * argument is just OP(without @).
	 */
	op = strchr(desc, '@');
	if (op) {
		op[0] = '\0';
		op++;

		if (sdt_arg_parse_size(desc, &suffix))
			goto error;
	} else {
		op = desc;
	}

	ret = arch_sdt_arg_parse_op(op, &new_op);

	if (ret < 0)
		goto error;

	if (ret == SDT_ARG_VALID) {
		ret = strbuf_addf(buf, " arg%d=%s%s", i + 1, new_op, suffix);
		if (ret < 0)
			goto error;
	}

	ret = 0;
error:
	free(desc);
	free(new_op);
	return ret;
}

static char *synthesize_sdt_probe_command(struct sdt_note *note,
					const char *pathname,
					const char *sdtgrp)
{
	struct strbuf buf;
	char *ret = NULL, **args;
	int i, args_count, err;
	unsigned long long ref_ctr_offset;

	if (strbuf_init(&buf, 32) < 0)
		return NULL;

	err = strbuf_addf(&buf, "p:%s/%s %s:0x%llx",
			sdtgrp, note->name, pathname,
			sdt_note__get_addr(note));

	ref_ctr_offset = sdt_note__get_ref_ctr_offset(note);
	if (ref_ctr_offset && err >= 0)
		err = strbuf_addf(&buf, "(0x%llx)", ref_ctr_offset);

	if (err < 0)
		goto error;

	if (!note->args)
		goto out;

	if (note->args) {
		args = argv_split(note->args, &args_count);

		for (i = 0; i < args_count; ++i) {
			if (synthesize_sdt_probe_arg(&buf, i, args[i]) < 0)
				goto error;
		}
	}

out:
	ret = strbuf_detach(&buf, NULL);
error:
	strbuf_release(&buf);
	return ret;
}

int probe_cache__scan_sdt(struct probe_cache *pcache, const char *pathname)
{
	struct probe_cache_entry *entry = NULL;
	struct list_head sdtlist;
	struct sdt_note *note;
	char *buf;
	char sdtgrp[64];
	int ret;

	INIT_LIST_HEAD(&sdtlist);
	ret = get_sdt_note_list(&sdtlist, pathname);
	if (ret < 0) {
		pr_debug4("Failed to get sdt note: %d\n", ret);
		return ret;
	}
	list_for_each_entry(note, &sdtlist, note_list) {
		ret = snprintf(sdtgrp, 64, "sdt_%s", note->provider);
		if (ret < 0)
			break;
		/* Try to find same-name entry */
		entry = probe_cache__find_by_name(pcache, sdtgrp, note->name);
		if (!entry) {
			entry = probe_cache_entry__new(NULL);
			if (!entry) {
				ret = -ENOMEM;
				break;
			}
			entry->sdt = true;
			ret = asprintf(&entry->spev, "%s:%s=%s", sdtgrp,
					note->name, note->name);
			if (ret < 0)
				break;
			entry->pev.event = strdup(note->name);
			entry->pev.group = strdup(sdtgrp);
			list_add_tail(&entry->node, &pcache->entries);
		}
		buf = synthesize_sdt_probe_command(note, pathname, sdtgrp);
		if (!buf) {
			ret = -ENOMEM;
			break;
		}

		strlist__add(entry->tevlist, buf);
		free(buf);
		entry = NULL;
	}
	if (entry) {
		list_del_init(&entry->node);
		probe_cache_entry__delete(entry);
	}
	cleanup_sdt_note_list(&sdtlist);
	return ret;
}
#endif

static int probe_cache_entry__write(struct probe_cache_entry *entry, int fd)
{
	struct str_node *snode;
	struct stat st;
	struct iovec iov[3];
	const char *prefix = entry->sdt ? "%" : "#";
	int ret;
	/* Save stat for rollback */
	ret = fstat(fd, &st);
	if (ret < 0)
		return ret;

	pr_debug("Writing cache: %s%s\n", prefix, entry->spev);
	iov[0].iov_base = (void *)prefix; iov[0].iov_len = 1;
	iov[1].iov_base = entry->spev; iov[1].iov_len = strlen(entry->spev);
	iov[2].iov_base = (void *)"\n"; iov[2].iov_len = 1;
	ret = writev(fd, iov, 3);
	if (ret < (int)iov[1].iov_len + 2)
		goto rollback;

	strlist__for_each_entry(snode, entry->tevlist) {
		iov[0].iov_base = (void *)snode->s;
		iov[0].iov_len = strlen(snode->s);
		iov[1].iov_base = (void *)"\n"; iov[1].iov_len = 1;
		ret = writev(fd, iov, 2);
		if (ret < (int)iov[0].iov_len + 1)
			goto rollback;
	}
	return 0;

rollback:
	/* Rollback to avoid cache file corruption */
	if (ret > 0)
		ret = -1;
	if (ftruncate(fd, st.st_size) < 0)
		ret = -2;

	return ret;
}

int probe_cache__commit(struct probe_cache *pcache)
{
	struct probe_cache_entry *entry;
	int ret = 0;

	/* TBD: if we do not update existing entries, skip it */
	ret = lseek(pcache->fd, 0, SEEK_SET);
	if (ret < 0)
		goto out;

	ret = ftruncate(pcache->fd, 0);
	if (ret < 0)
		goto out;

	for_each_probe_cache_entry(entry, pcache) {
		ret = probe_cache_entry__write(entry, pcache->fd);
		pr_debug("Cache committed: %d\n", ret);
		if (ret < 0)
			break;
	}
out:
	return ret;
}

static bool probe_cache_entry__compare(struct probe_cache_entry *entry,
				       struct strfilter *filter)
{
	char buf[128], *ptr = entry->spev;

	if (entry->pev.event) {
		snprintf(buf, 128, "%s:%s", entry->pev.group, entry->pev.event);
		ptr = buf;
	}
	return strfilter__compare(filter, ptr);
}

int probe_cache__filter_purge(struct probe_cache *pcache,
			      struct strfilter *filter)
{
	struct probe_cache_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &pcache->entries, node) {
		if (probe_cache_entry__compare(entry, filter)) {
			pr_info("Removed cached event: %s\n", entry->spev);
			list_del_init(&entry->node);
			probe_cache_entry__delete(entry);
		}
	}
	return 0;
}

static int probe_cache__show_entries(struct probe_cache *pcache,
				     struct strfilter *filter)
{
	struct probe_cache_entry *entry;

	for_each_probe_cache_entry(entry, pcache) {
		if (probe_cache_entry__compare(entry, filter))
			printf("%s\n", entry->spev);
	}
	return 0;
}

/* Show all cached probes */
int probe_cache__show_all_caches(struct strfilter *filter)
{
	struct probe_cache *pcache;
	struct strlist *bidlist;
	struct str_node *nd;
	char *buf = strfilter__string(filter);

	pr_debug("list cache with filter: %s\n", buf);
	free(buf);

	bidlist = build_id_cache__list_all(true);
	if (!bidlist) {
		pr_debug("Failed to get buildids: %d\n", errno);
		return -EINVAL;
	}
	strlist__for_each_entry(nd, bidlist) {
		pcache = probe_cache__new(nd->s, NULL);
		if (!pcache)
			continue;
		if (!list_empty(&pcache->entries)) {
			buf = build_id_cache__origname(nd->s);
			printf("%s (%s):\n", buf, nd->s);
			free(buf);
			probe_cache__show_entries(pcache, filter);
		}
		probe_cache__delete(pcache);
	}
	strlist__delete(bidlist);

	return 0;
}

enum ftrace_readme {
	FTRACE_README_PROBE_TYPE_X = 0,
	FTRACE_README_KRETPROBE_OFFSET,
	FTRACE_README_UPROBE_REF_CTR,
	FTRACE_README_USER_ACCESS,
	FTRACE_README_END,
};

static struct {
	const char *pattern;
	bool avail;
} ftrace_readme_table[] = {
#define DEFINE_TYPE(idx, pat)			\
	[idx] = {.pattern = pat, .avail = false}
	DEFINE_TYPE(FTRACE_README_PROBE_TYPE_X, "*type: * x8/16/32/64,*"),
	DEFINE_TYPE(FTRACE_README_KRETPROBE_OFFSET, "*place (kretprobe): *"),
	DEFINE_TYPE(FTRACE_README_UPROBE_REF_CTR, "*ref_ctr_offset*"),
	DEFINE_TYPE(FTRACE_README_USER_ACCESS, "*[u]<offset>*"),
};

static bool scan_ftrace_readme(enum ftrace_readme type)
{
	int fd;
	FILE *fp;
	char *buf = NULL;
	size_t len = 0;
	bool ret = false;
	static bool scanned = false;

	if (scanned)
		goto result;

	fd = open_trace_file("README", false);
	if (fd < 0)
		return ret;

	fp = fdopen(fd, "r");
	if (!fp) {
		close(fd);
		return ret;
	}

	while (getline(&buf, &len, fp) > 0)
		for (enum ftrace_readme i = 0; i < FTRACE_README_END; i++)
			if (!ftrace_readme_table[i].avail)
				ftrace_readme_table[i].avail =
					strglobmatch(buf, ftrace_readme_table[i].pattern);
	scanned = true;

	fclose(fp);
	free(buf);

result:
	if (type >= FTRACE_README_END)
		return false;

	return ftrace_readme_table[type].avail;
}

bool probe_type_is_available(enum probe_type type)
{
	if (type >= PROBE_TYPE_END)
		return false;
	else if (type == PROBE_TYPE_X)
		return scan_ftrace_readme(FTRACE_README_PROBE_TYPE_X);

	return true;
}

bool kretprobe_offset_is_supported(void)
{
	return scan_ftrace_readme(FTRACE_README_KRETPROBE_OFFSET);
}

bool uprobe_ref_ctr_is_supported(void)
{
	return scan_ftrace_readme(FTRACE_README_UPROBE_REF_CTR);
}

bool user_access_is_supported(void)
{
	return scan_ftrace_readme(FTRACE_README_USER_ACCESS);
}

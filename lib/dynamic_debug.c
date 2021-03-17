/*
 * lib/dynamic_debug.c
 *
 * make pr_debug()/dev_dbg() calls runtime configurable based upon their
 * source module.
 *
 * Copyright (C) 2008 Jason Baron <jbaron@redhat.com>
 * By Greg Banks <gnb@melbourne.sgi.com>
 * Copyright (c) 2008 Silicon Graphics Inc.  All Rights Reserved.
 * Copyright (C) 2011 Bart Van Assche.  All Rights Reserved.
 * Copyright (C) 2013 Du, Changbin <changbin.du@gmail.com>
 */

#define pr_fmt(fmt) "dyndbg: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kallsyms.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/sysctl.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/parser.h>
#include <linux/string_helpers.h>
#include <linux/uaccess.h>
#include <linux/dynamic_debug.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/jump_label.h>
#include <linux/hardirq.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/netdevice.h>

#include <rdma/ib_verbs.h>

extern struct _ddebug __start___dyndbg[];
extern struct _ddebug __stop___dyndbg[];

struct ddebug_table {
	struct list_head link;
	const char *mod_name;
	unsigned int num_ddebugs;
	struct _ddebug *ddebugs;
};

struct ddebug_query {
	const char *filename;
	const char *module;
	const char *function;
	const char *format;
	unsigned int first_lineno, last_lineno;
};

struct ddebug_iter {
	struct ddebug_table *table;
	unsigned int idx;
};

struct flag_settings {
	unsigned int flags;
	unsigned int mask;
};

static DEFINE_MUTEX(ddebug_lock);
static LIST_HEAD(ddebug_tables);
static int verbose;
module_param(verbose, int, 0644);

/* Return the path relative to source root */
static inline const char *trim_prefix(const char *path)
{
	int skip = strlen(__FILE__) - strlen("lib/dynamic_debug.c");

	if (strncmp(path, __FILE__, skip))
		skip = 0; /* prefix mismatch, don't skip */

	return path + skip;
}

static struct { unsigned flag:8; char opt_char; } opt_array[] = {
	{ _DPRINTK_FLAGS_PRINT, 'p' },
	{ _DPRINTK_FLAGS_INCL_MODNAME, 'm' },
	{ _DPRINTK_FLAGS_INCL_FUNCNAME, 'f' },
	{ _DPRINTK_FLAGS_INCL_LINENO, 'l' },
	{ _DPRINTK_FLAGS_INCL_TID, 't' },
	{ _DPRINTK_FLAGS_NONE, '_' },
};

struct flagsbuf { char buf[ARRAY_SIZE(opt_array)+1]; };

/* format a string into buf[] which describes the _ddebug's flags */
static char *ddebug_describe_flags(unsigned int flags, struct flagsbuf *fb)
{
	char *p = fb->buf;
	int i;

	for (i = 0; i < ARRAY_SIZE(opt_array); ++i)
		if (flags & opt_array[i].flag)
			*p++ = opt_array[i].opt_char;
	if (p == fb->buf)
		*p++ = '_';
	*p = '\0';

	return fb->buf;
}

#define vnpr_info(lvl, fmt, ...)				\
do {								\
	if (verbose >= lvl)					\
		pr_info(fmt, ##__VA_ARGS__);			\
} while (0)

#define vpr_info(fmt, ...)	vnpr_info(1, fmt, ##__VA_ARGS__)
#define v2pr_info(fmt, ...)	vnpr_info(2, fmt, ##__VA_ARGS__)

static void vpr_info_dq(const struct ddebug_query *query, const char *msg)
{
	/* trim any trailing newlines */
	int fmtlen = 0;

	if (query->format) {
		fmtlen = strlen(query->format);
		while (fmtlen && query->format[fmtlen - 1] == '\n')
			fmtlen--;
	}

	vpr_info("%s: func=\"%s\" file=\"%s\" module=\"%s\" format=\"%.*s\" lineno=%u-%u\n",
		 msg,
		 query->function ?: "",
		 query->filename ?: "",
		 query->module ?: "",
		 fmtlen, query->format ?: "",
		 query->first_lineno, query->last_lineno);
}

/*
 * Search the tables for _ddebug's which match the given `query' and
 * apply the `flags' and `mask' to them.  Returns number of matching
 * callsites, normally the same as number of changes.  If verbose,
 * logs the changes.  Takes ddebug_lock.
 */
static int ddebug_change(const struct ddebug_query *query,
			 struct flag_settings *modifiers)
{
	int i;
	struct ddebug_table *dt;
	unsigned int newflags;
	unsigned int nfound = 0;
	struct flagsbuf fbuf;

	/* search for matching ddebugs */
	mutex_lock(&ddebug_lock);
	list_for_each_entry(dt, &ddebug_tables, link) {

		/* match against the module name */
		if (query->module &&
		    !match_wildcard(query->module, dt->mod_name))
			continue;

		for (i = 0; i < dt->num_ddebugs; i++) {
			struct _ddebug *dp = &dt->ddebugs[i];

			/* match against the source filename */
			if (query->filename &&
			    !match_wildcard(query->filename, dp->filename) &&
			    !match_wildcard(query->filename,
					   kbasename(dp->filename)) &&
			    !match_wildcard(query->filename,
					   trim_prefix(dp->filename)))
				continue;

			/* match against the function */
			if (query->function &&
			    !match_wildcard(query->function, dp->function))
				continue;

			/* match against the format */
			if (query->format) {
				if (*query->format == '^') {
					char *p;
					/* anchored search. match must be at beginning */
					p = strstr(dp->format, query->format+1);
					if (p != dp->format)
						continue;
				} else if (!strstr(dp->format, query->format))
					continue;
			}

			/* match against the line number range */
			if (query->first_lineno &&
			    dp->lineno < query->first_lineno)
				continue;
			if (query->last_lineno &&
			    dp->lineno > query->last_lineno)
				continue;

			nfound++;

			newflags = (dp->flags & modifiers->mask) | modifiers->flags;
			if (newflags == dp->flags)
				continue;
#ifdef CONFIG_JUMP_LABEL
			if (dp->flags & _DPRINTK_FLAGS_PRINT) {
				if (!(modifiers->flags & _DPRINTK_FLAGS_PRINT))
					static_branch_disable(&dp->key.dd_key_true);
			} else if (modifiers->flags & _DPRINTK_FLAGS_PRINT)
				static_branch_enable(&dp->key.dd_key_true);
#endif
			dp->flags = newflags;
			v2pr_info("changed %s:%d [%s]%s =%s\n",
				 trim_prefix(dp->filename), dp->lineno,
				 dt->mod_name, dp->function,
				 ddebug_describe_flags(dp->flags, &fbuf));
		}
	}
	mutex_unlock(&ddebug_lock);

	if (!nfound && verbose)
		pr_info("no matches for query\n");

	return nfound;
}

/*
 * Split the buffer `buf' into space-separated words.
 * Handles simple " and ' quoting, i.e. without nested,
 * embedded or escaped \".  Return the number of words
 * or <0 on error.
 */
static int ddebug_tokenize(char *buf, char *words[], int maxwords)
{
	int nwords = 0;

	while (*buf) {
		char *end;

		/* Skip leading whitespace */
		buf = skip_spaces(buf);
		if (!*buf)
			break;	/* oh, it was trailing whitespace */
		if (*buf == '#')
			break;	/* token starts comment, skip rest of line */

		/* find `end' of word, whitespace separated or quoted */
		if (*buf == '"' || *buf == '\'') {
			int quote = *buf++;
			for (end = buf; *end && *end != quote; end++)
				;
			if (!*end) {
				pr_err("unclosed quote: %s\n", buf);
				return -EINVAL;	/* unclosed quote */
			}
		} else {
			for (end = buf; *end && !isspace(*end); end++)
				;
			BUG_ON(end == buf);
		}

		/* `buf' is start of word, `end' is one past its end */
		if (nwords == maxwords) {
			pr_err("too many words, legal max <=%d\n", maxwords);
			return -EINVAL;	/* ran out of words[] before bytes */
		}
		if (*end)
			*end++ = '\0';	/* terminate the word */
		words[nwords++] = buf;
		buf = end;
	}

	if (verbose) {
		int i;
		pr_info("split into words:");
		for (i = 0; i < nwords; i++)
			pr_cont(" \"%s\"", words[i]);
		pr_cont("\n");
	}

	return nwords;
}

/*
 * Parse a single line number.  Note that the empty string ""
 * is treated as a special case and converted to zero, which
 * is later treated as a "don't care" value.
 */
static inline int parse_lineno(const char *str, unsigned int *val)
{
	BUG_ON(str == NULL);
	if (*str == '\0') {
		*val = 0;
		return 0;
	}
	if (kstrtouint(str, 10, val) < 0) {
		pr_err("bad line-number: %s\n", str);
		return -EINVAL;
	}
	return 0;
}

static int parse_linerange(struct ddebug_query *query, const char *first)
{
	char *last = strchr(first, '-');

	if (query->first_lineno || query->last_lineno) {
		pr_err("match-spec: line used 2x\n");
		return -EINVAL;
	}
	if (last)
		*last++ = '\0';
	if (parse_lineno(first, &query->first_lineno) < 0)
		return -EINVAL;
	if (last) {
		/* range <first>-<last> */
		if (parse_lineno(last, &query->last_lineno) < 0)
			return -EINVAL;

		/* special case for last lineno not specified */
		if (query->last_lineno == 0)
			query->last_lineno = UINT_MAX;

		if (query->last_lineno < query->first_lineno) {
			pr_err("last-line:%d < 1st-line:%d\n",
			       query->last_lineno,
			       query->first_lineno);
			return -EINVAL;
		}
	} else {
		query->last_lineno = query->first_lineno;
	}
	vpr_info("parsed line %d-%d\n", query->first_lineno,
		 query->last_lineno);
	return 0;
}

static int check_set(const char **dest, char *src, char *name)
{
	int rc = 0;

	if (*dest) {
		rc = -EINVAL;
		pr_err("match-spec:%s val:%s overridden by %s\n",
		       name, *dest, src);
	}
	*dest = src;
	return rc;
}

/*
 * Parse words[] as a ddebug query specification, which is a series
 * of (keyword, value) pairs chosen from these possibilities:
 *
 * func <function-name>
 * file <full-pathname>
 * file <base-filename>
 * module <module-name>
 * format <escaped-string-to-find-in-format>
 * line <lineno>
 * line <first-lineno>-<last-lineno> // where either may be empty
 *
 * Only 1 of each type is allowed.
 * Returns 0 on success, <0 on error.
 */
static int ddebug_parse_query(char *words[], int nwords,
			struct ddebug_query *query, const char *modname)
{
	unsigned int i;
	int rc = 0;
	char *fline;

	/* check we have an even number of words */
	if (nwords % 2 != 0) {
		pr_err("expecting pairs of match-spec <value>\n");
		return -EINVAL;
	}

	if (modname)
		/* support $modname.dyndbg=<multiple queries> */
		query->module = modname;

	for (i = 0; i < nwords; i += 2) {
		char *keyword = words[i];
		char *arg = words[i+1];

		if (!strcmp(keyword, "func")) {
			rc = check_set(&query->function, arg, "func");
		} else if (!strcmp(keyword, "file")) {
			if (check_set(&query->filename, arg, "file"))
				return -EINVAL;

			/* tail :$info is function or line-range */
			fline = strchr(query->filename, ':');
			if (!fline)
				break;
			*fline++ = '\0';
			if (isalpha(*fline) || *fline == '*' || *fline == '?') {
				/* take as function name */
				if (check_set(&query->function, fline, "func"))
					return -EINVAL;
			} else {
				if (parse_linerange(query, fline))
					return -EINVAL;
			}
		} else if (!strcmp(keyword, "module")) {
			rc = check_set(&query->module, arg, "module");
		} else if (!strcmp(keyword, "format")) {
			string_unescape_inplace(arg, UNESCAPE_SPACE |
							    UNESCAPE_OCTAL |
							    UNESCAPE_SPECIAL);
			rc = check_set(&query->format, arg, "format");
		} else if (!strcmp(keyword, "line")) {
			if (parse_linerange(query, arg))
				return -EINVAL;
		} else {
			pr_err("unknown keyword \"%s\"\n", keyword);
			return -EINVAL;
		}
		if (rc)
			return rc;
	}
	vpr_info_dq(query, "parsed");
	return 0;
}

/*
 * Parse `str' as a flags specification, format [-+=][p]+.
 * Sets up *maskp and *flagsp to be used when changing the
 * flags fields of matched _ddebug's.  Returns 0 on success
 * or <0 on error.
 */
static int ddebug_parse_flags(const char *str, struct flag_settings *modifiers)
{
	int op, i;

	switch (*str) {
	case '+':
	case '-':
	case '=':
		op = *str++;
		break;
	default:
		pr_err("bad flag-op %c, at start of %s\n", *str, str);
		return -EINVAL;
	}
	vpr_info("op='%c'\n", op);

	for (; *str ; ++str) {
		for (i = ARRAY_SIZE(opt_array) - 1; i >= 0; i--) {
			if (*str == opt_array[i].opt_char) {
				modifiers->flags |= opt_array[i].flag;
				break;
			}
		}
		if (i < 0) {
			pr_err("unknown flag '%c'\n", *str);
			return -EINVAL;
		}
	}
	vpr_info("flags=0x%x\n", modifiers->flags);

	/* calculate final flags, mask based upon op */
	switch (op) {
	case '=':
		/* modifiers->flags already set */
		modifiers->mask = 0;
		break;
	case '+':
		modifiers->mask = ~0U;
		break;
	case '-':
		modifiers->mask = ~modifiers->flags;
		modifiers->flags = 0;
		break;
	}
	vpr_info("*flagsp=0x%x *maskp=0x%x\n", modifiers->flags, modifiers->mask);

	return 0;
}

static int ddebug_exec_query(char *query_string, const char *modname)
{
	struct flag_settings modifiers = {};
	struct ddebug_query query = {};
#define MAXWORDS 9
	int nwords, nfound;
	char *words[MAXWORDS];

	nwords = ddebug_tokenize(query_string, words, MAXWORDS);
	if (nwords <= 0) {
		pr_err("tokenize failed\n");
		return -EINVAL;
	}
	/* check flags 1st (last arg) so query is pairs of spec,val */
	if (ddebug_parse_flags(words[nwords-1], &modifiers)) {
		pr_err("flags parse failed\n");
		return -EINVAL;
	}
	if (ddebug_parse_query(words, nwords-1, &query, modname)) {
		pr_err("query parse failed\n");
		return -EINVAL;
	}
	/* actually go and implement the change */
	nfound = ddebug_change(&query, &modifiers);
	vpr_info_dq(&query, nfound ? "applied" : "no-match");

	return nfound;
}

/* handle multiple queries in query string, continue on error, return
   last error or number of matching callsites.  Module name is either
   in param (for boot arg) or perhaps in query string.
*/
static int ddebug_exec_queries(char *query, const char *modname)
{
	char *split;
	int i, errs = 0, exitcode = 0, rc, nfound = 0;

	for (i = 0; query; query = split) {
		split = strpbrk(query, ";\n");
		if (split)
			*split++ = '\0';

		query = skip_spaces(query);
		if (!query || !*query || *query == '#')
			continue;

		vpr_info("query %d: \"%s\"\n", i, query);

		rc = ddebug_exec_query(query, modname);
		if (rc < 0) {
			errs++;
			exitcode = rc;
		} else {
			nfound += rc;
		}
		i++;
	}
	vpr_info("processed %d queries, with %d matches, %d errs\n",
		 i, nfound, errs);

	if (exitcode)
		return exitcode;
	return nfound;
}

/**
 * dynamic_debug_exec_queries - select and change dynamic-debug prints
 * @query: query-string described in admin-guide/dynamic-debug-howto
 * @modname: string containing module name, usually &module.mod_name
 *
 * This uses the >/proc/dynamic_debug/control reader, allowing module
 * authors to modify their dynamic-debug callsites. The modname is
 * canonically struct module.mod_name, but can also be null or a
 * module-wildcard, for example: "drm*".
 */
int dynamic_debug_exec_queries(const char *query, const char *modname)
{
	int rc;
	char *qry; /* writable copy of query */

	if (!query) {
		pr_err("non-null query/command string expected\n");
		return -EINVAL;
	}
	qry = kstrndup(query, PAGE_SIZE, GFP_KERNEL);
	if (!qry)
		return -ENOMEM;

	rc = ddebug_exec_queries(qry, modname);
	kfree(qry);
	return rc;
}
EXPORT_SYMBOL_GPL(dynamic_debug_exec_queries);

#define PREFIX_SIZE 64

static int remaining(int wrote)
{
	if (PREFIX_SIZE - wrote > 0)
		return PREFIX_SIZE - wrote;
	return 0;
}

static char *dynamic_emit_prefix(const struct _ddebug *desc, char *buf)
{
	int pos_after_tid;
	int pos = 0;

	*buf = '\0';

	if (desc->flags & _DPRINTK_FLAGS_INCL_TID) {
		if (in_interrupt())
			pos += snprintf(buf + pos, remaining(pos), "<intr> ");
		else
			pos += snprintf(buf + pos, remaining(pos), "[%d] ",
					task_pid_vnr(current));
	}
	pos_after_tid = pos;
	if (desc->flags & _DPRINTK_FLAGS_INCL_MODNAME)
		pos += snprintf(buf + pos, remaining(pos), "%s:",
				desc->modname);
	if (desc->flags & _DPRINTK_FLAGS_INCL_FUNCNAME)
		pos += snprintf(buf + pos, remaining(pos), "%s:",
				desc->function);
	if (desc->flags & _DPRINTK_FLAGS_INCL_LINENO)
		pos += snprintf(buf + pos, remaining(pos), "%d:",
				desc->lineno);
	if (pos - pos_after_tid)
		pos += snprintf(buf + pos, remaining(pos), " ");
	if (pos >= PREFIX_SIZE)
		buf[PREFIX_SIZE - 1] = '\0';

	return buf;
}

void __dynamic_pr_debug(struct _ddebug *descriptor, const char *fmt, ...)
{
	va_list args;
	struct va_format vaf;
	char buf[PREFIX_SIZE];

	BUG_ON(!descriptor);
	BUG_ON(!fmt);

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_DEBUG "%s%pV", dynamic_emit_prefix(descriptor, buf), &vaf);

	va_end(args);
}
EXPORT_SYMBOL(__dynamic_pr_debug);

void __dynamic_dev_dbg(struct _ddebug *descriptor,
		      const struct device *dev, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	BUG_ON(!descriptor);
	BUG_ON(!fmt);

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (!dev) {
		printk(KERN_DEBUG "(NULL device *): %pV", &vaf);
	} else {
		char buf[PREFIX_SIZE];

		dev_printk_emit(LOGLEVEL_DEBUG, dev, "%s%s %s: %pV",
				dynamic_emit_prefix(descriptor, buf),
				dev_driver_string(dev), dev_name(dev),
				&vaf);
	}

	va_end(args);
}
EXPORT_SYMBOL(__dynamic_dev_dbg);

#ifdef CONFIG_NET

void __dynamic_netdev_dbg(struct _ddebug *descriptor,
			  const struct net_device *dev, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	BUG_ON(!descriptor);
	BUG_ON(!fmt);

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (dev && dev->dev.parent) {
		char buf[PREFIX_SIZE];

		dev_printk_emit(LOGLEVEL_DEBUG, dev->dev.parent,
				"%s%s %s %s%s: %pV",
				dynamic_emit_prefix(descriptor, buf),
				dev_driver_string(dev->dev.parent),
				dev_name(dev->dev.parent),
				netdev_name(dev), netdev_reg_state(dev),
				&vaf);
	} else if (dev) {
		printk(KERN_DEBUG "%s%s: %pV", netdev_name(dev),
		       netdev_reg_state(dev), &vaf);
	} else {
		printk(KERN_DEBUG "(NULL net_device): %pV", &vaf);
	}

	va_end(args);
}
EXPORT_SYMBOL(__dynamic_netdev_dbg);

#endif

#if IS_ENABLED(CONFIG_INFINIBAND)

void __dynamic_ibdev_dbg(struct _ddebug *descriptor,
			 const struct ib_device *ibdev, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (ibdev && ibdev->dev.parent) {
		char buf[PREFIX_SIZE];

		dev_printk_emit(LOGLEVEL_DEBUG, ibdev->dev.parent,
				"%s%s %s %s: %pV",
				dynamic_emit_prefix(descriptor, buf),
				dev_driver_string(ibdev->dev.parent),
				dev_name(ibdev->dev.parent),
				dev_name(&ibdev->dev),
				&vaf);
	} else if (ibdev) {
		printk(KERN_DEBUG "%s: %pV", dev_name(&ibdev->dev), &vaf);
	} else {
		printk(KERN_DEBUG "(NULL ib_device): %pV", &vaf);
	}

	va_end(args);
}
EXPORT_SYMBOL(__dynamic_ibdev_dbg);

#endif

#define DDEBUG_STRING_SIZE 1024
static __initdata char ddebug_setup_string[DDEBUG_STRING_SIZE];

static __init int ddebug_setup_query(char *str)
{
	if (strlen(str) >= DDEBUG_STRING_SIZE) {
		pr_warn("ddebug boot param string too large\n");
		return 0;
	}
	strlcpy(ddebug_setup_string, str, DDEBUG_STRING_SIZE);
	return 1;
}

__setup("ddebug_query=", ddebug_setup_query);

/*
 * File_ops->write method for <debugfs>/dynamic_debug/control.  Gathers the
 * command text from userspace, parses and executes it.
 */
#define USER_BUF_PAGE 4096
static ssize_t ddebug_proc_write(struct file *file, const char __user *ubuf,
				  size_t len, loff_t *offp)
{
	char *tmpbuf;
	int ret;

	if (len == 0)
		return 0;
	if (len > USER_BUF_PAGE - 1) {
		pr_warn("expected <%d bytes into control\n", USER_BUF_PAGE);
		return -E2BIG;
	}
	tmpbuf = memdup_user_nul(ubuf, len);
	if (IS_ERR(tmpbuf))
		return PTR_ERR(tmpbuf);
	vpr_info("read %d bytes from userspace\n", (int)len);

	ret = ddebug_exec_queries(tmpbuf, NULL);
	kfree(tmpbuf);
	if (ret < 0)
		return ret;

	*offp += len;
	return len;
}

/*
 * Set the iterator to point to the first _ddebug object
 * and return a pointer to that first object.  Returns
 * NULL if there are no _ddebugs at all.
 */
static struct _ddebug *ddebug_iter_first(struct ddebug_iter *iter)
{
	if (list_empty(&ddebug_tables)) {
		iter->table = NULL;
		iter->idx = 0;
		return NULL;
	}
	iter->table = list_entry(ddebug_tables.next,
				 struct ddebug_table, link);
	iter->idx = 0;
	return &iter->table->ddebugs[iter->idx];
}

/*
 * Advance the iterator to point to the next _ddebug
 * object from the one the iterator currently points at,
 * and returns a pointer to the new _ddebug.  Returns
 * NULL if the iterator has seen all the _ddebugs.
 */
static struct _ddebug *ddebug_iter_next(struct ddebug_iter *iter)
{
	if (iter->table == NULL)
		return NULL;
	if (++iter->idx == iter->table->num_ddebugs) {
		/* iterate to next table */
		iter->idx = 0;
		if (list_is_last(&iter->table->link, &ddebug_tables)) {
			iter->table = NULL;
			return NULL;
		}
		iter->table = list_entry(iter->table->link.next,
					 struct ddebug_table, link);
	}
	return &iter->table->ddebugs[iter->idx];
}

/*
 * Seq_ops start method.  Called at the start of every
 * read() call from userspace.  Takes the ddebug_lock and
 * seeks the seq_file's iterator to the given position.
 */
static void *ddebug_proc_start(struct seq_file *m, loff_t *pos)
{
	struct ddebug_iter *iter = m->private;
	struct _ddebug *dp;
	int n = *pos;

	mutex_lock(&ddebug_lock);

	if (!n)
		return SEQ_START_TOKEN;
	if (n < 0)
		return NULL;
	dp = ddebug_iter_first(iter);
	while (dp != NULL && --n > 0)
		dp = ddebug_iter_next(iter);
	return dp;
}

/*
 * Seq_ops next method.  Called several times within a read()
 * call from userspace, with ddebug_lock held.  Walks to the
 * next _ddebug object with a special case for the header line.
 */
static void *ddebug_proc_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct ddebug_iter *iter = m->private;
	struct _ddebug *dp;

	if (p == SEQ_START_TOKEN)
		dp = ddebug_iter_first(iter);
	else
		dp = ddebug_iter_next(iter);
	++*pos;
	return dp;
}

/*
 * Seq_ops show method.  Called several times within a read()
 * call from userspace, with ddebug_lock held.  Formats the
 * current _ddebug as a single human-readable line, with a
 * special case for the header line.
 */
static int ddebug_proc_show(struct seq_file *m, void *p)
{
	struct ddebug_iter *iter = m->private;
	struct _ddebug *dp = p;
	struct flagsbuf flags;

	if (p == SEQ_START_TOKEN) {
		seq_puts(m,
			 "# filename:lineno [module]function flags format\n");
		return 0;
	}

	seq_printf(m, "%s:%u [%s]%s =%s \"",
		   trim_prefix(dp->filename), dp->lineno,
		   iter->table->mod_name, dp->function,
		   ddebug_describe_flags(dp->flags, &flags));
	seq_escape(m, dp->format, "\t\r\n\"");
	seq_puts(m, "\"\n");

	return 0;
}

/*
 * Seq_ops stop method.  Called at the end of each read()
 * call from userspace.  Drops ddebug_lock.
 */
static void ddebug_proc_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&ddebug_lock);
}

static const struct seq_operations ddebug_proc_seqops = {
	.start = ddebug_proc_start,
	.next = ddebug_proc_next,
	.show = ddebug_proc_show,
	.stop = ddebug_proc_stop
};

static int ddebug_proc_open(struct inode *inode, struct file *file)
{
	vpr_info("called\n");
	return seq_open_private(file, &ddebug_proc_seqops,
				sizeof(struct ddebug_iter));
}

static const struct file_operations ddebug_proc_fops = {
	.owner = THIS_MODULE,
	.open = ddebug_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_private,
	.write = ddebug_proc_write
};

static const struct proc_ops proc_fops = {
	.proc_open = ddebug_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release_private,
	.proc_write = ddebug_proc_write
};

/*
 * Allocate a new ddebug_table for the given module
 * and add it to the global list.
 */
int ddebug_add_module(struct _ddebug *tab, unsigned int n,
			     const char *name)
{
	struct ddebug_table *dt;

	dt = kzalloc(sizeof(*dt), GFP_KERNEL);
	if (dt == NULL) {
		pr_err("error adding module: %s\n", name);
		return -ENOMEM;
	}
	/*
	 * For built-in modules, name lives in .rodata and is
	 * immortal. For loaded modules, name points at the name[]
	 * member of struct module, which lives at least as long as
	 * this struct ddebug_table.
	 */
	dt->mod_name = name;
	dt->num_ddebugs = n;
	dt->ddebugs = tab;

	mutex_lock(&ddebug_lock);
	list_add(&dt->link, &ddebug_tables);
	mutex_unlock(&ddebug_lock);

	v2pr_info("%3u debug prints in module %s\n", n, dt->mod_name);
	return 0;
}

/* helper for ddebug_dyndbg_(boot|module)_param_cb */
static int ddebug_dyndbg_param_cb(char *param, char *val,
				const char *modname, int on_err)
{
	char *sep;

	sep = strchr(param, '.');
	if (sep) {
		/* needed only for ddebug_dyndbg_boot_param_cb */
		*sep = '\0';
		modname = param;
		param = sep + 1;
	}
	if (strcmp(param, "dyndbg"))
		return on_err; /* determined by caller */

	ddebug_exec_queries((val ? val : "+p"), modname);

	return 0; /* query failure shouldnt stop module load */
}

/* handle both dyndbg and $module.dyndbg params at boot */
static int ddebug_dyndbg_boot_param_cb(char *param, char *val,
				const char *unused, void *arg)
{
	vpr_info("%s=\"%s\"\n", param, val);
	return ddebug_dyndbg_param_cb(param, val, NULL, 0);
}

/*
 * modprobe foo finds foo.params in boot-args, strips "foo.", and
 * passes them to load_module().  This callback gets unknown params,
 * processes dyndbg params, rejects others.
 */
int ddebug_dyndbg_module_param_cb(char *param, char *val, const char *module)
{
	vpr_info("module: %s %s=\"%s\"\n", module, param, val);
	return ddebug_dyndbg_param_cb(param, val, module, -ENOENT);
}

static void ddebug_table_free(struct ddebug_table *dt)
{
	list_del_init(&dt->link);
	kfree(dt);
}

/*
 * Called in response to a module being unloaded.  Removes
 * any ddebug_table's which point at the module.
 */
int ddebug_remove_module(const char *mod_name)
{
	struct ddebug_table *dt, *nextdt;
	int ret = -ENOENT;

	v2pr_info("removing module \"%s\"\n", mod_name);

	mutex_lock(&ddebug_lock);
	list_for_each_entry_safe(dt, nextdt, &ddebug_tables, link) {
		if (dt->mod_name == mod_name) {
			ddebug_table_free(dt);
			ret = 0;
			break;
		}
	}
	mutex_unlock(&ddebug_lock);
	return ret;
}

static void ddebug_remove_all_tables(void)
{
	mutex_lock(&ddebug_lock);
	while (!list_empty(&ddebug_tables)) {
		struct ddebug_table *dt = list_entry(ddebug_tables.next,
						      struct ddebug_table,
						      link);
		ddebug_table_free(dt);
	}
	mutex_unlock(&ddebug_lock);
}

static __initdata int ddebug_init_success;

static int __init dynamic_debug_init_control(void)
{
	struct proc_dir_entry *procfs_dir;
	struct dentry *debugfs_dir;

	if (!ddebug_init_success)
		return -ENODEV;

	/* Create the control file in debugfs if it is enabled */
	if (debugfs_initialized()) {
		debugfs_dir = debugfs_create_dir("dynamic_debug", NULL);
		debugfs_create_file("control", 0644, debugfs_dir, NULL,
				    &ddebug_proc_fops);
	}

	/* Also create the control file in procfs */
	procfs_dir = proc_mkdir("dynamic_debug", NULL);
	if (procfs_dir)
		proc_create("control", 0644, procfs_dir, &proc_fops);

	return 0;
}

static int __init dynamic_debug_init(void)
{
	struct _ddebug *iter, *iter_start;
	const char *modname = NULL;
	char *cmdline;
	int ret = 0;
	int n = 0, entries = 0, modct = 0;

	if (&__start___dyndbg == &__stop___dyndbg) {
		if (IS_ENABLED(CONFIG_DYNAMIC_DEBUG)) {
			pr_warn("_ddebug table is empty in a CONFIG_DYNAMIC_DEBUG build\n");
			return 1;
		}
		pr_info("Ignore empty _ddebug table in a CONFIG_DYNAMIC_DEBUG_CORE build\n");
		ddebug_init_success = 1;
		return 0;
	}
	iter = __start___dyndbg;
	modname = iter->modname;
	iter_start = iter;
	for (; iter < __stop___dyndbg; iter++) {
		entries++;
		if (strcmp(modname, iter->modname)) {
			modct++;
			ret = ddebug_add_module(iter_start, n, modname);
			if (ret)
				goto out_err;
			n = 0;
			modname = iter->modname;
			iter_start = iter;
		}
		n++;
	}
	ret = ddebug_add_module(iter_start, n, modname);
	if (ret)
		goto out_err;

	ddebug_init_success = 1;
	vpr_info("%d modules, %d entries and %d bytes in ddebug tables, %d bytes in __dyndbg section\n",
		 modct, entries, (int)(modct * sizeof(struct ddebug_table)),
		 (int)(entries * sizeof(struct _ddebug)));

	/* apply ddebug_query boot param, dont unload tables on err */
	if (ddebug_setup_string[0] != '\0') {
		pr_warn("ddebug_query param name is deprecated, change it to dyndbg\n");
		ret = ddebug_exec_queries(ddebug_setup_string, NULL);
		if (ret < 0)
			pr_warn("Invalid ddebug boot param %s\n",
				ddebug_setup_string);
		else
			pr_info("%d changes by ddebug_query\n", ret);
	}
	/* now that ddebug tables are loaded, process all boot args
	 * again to find and activate queries given in dyndbg params.
	 * While this has already been done for known boot params, it
	 * ignored the unknown ones (dyndbg in particular).  Reusing
	 * parse_args avoids ad-hoc parsing.  This will also attempt
	 * to activate queries for not-yet-loaded modules, which is
	 * slightly noisy if verbose, but harmless.
	 */
	cmdline = kstrdup(saved_command_line, GFP_KERNEL);
	parse_args("dyndbg params", cmdline, NULL,
		   0, 0, 0, NULL, &ddebug_dyndbg_boot_param_cb);
	kfree(cmdline);
	return 0;

out_err:
	ddebug_remove_all_tables();
	return 0;
}
/* Allow early initialization for boot messages via boot param */
early_initcall(dynamic_debug_init);

/* Debugfs setup must be done later */
fs_initcall(dynamic_debug_init_control);

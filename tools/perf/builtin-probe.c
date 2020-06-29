// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * builtin-probe.c
 *
 * Builtin probe command: Set up probe events by C expression
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
 */
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "builtin.h"
#include "namespaces.h"
#include "util/build-id.h"
#include "util/strlist.h"
#include "util/strfilter.h"
#include "util/symbol_conf.h"
#include "util/debug.h"
#include <subcmd/parse-options.h>
#include "util/probe-finder.h"
#include "util/probe-event.h"
#include "util/probe-file.h"
#include <linux/string.h>
#include <linux/zalloc.h>

#define DEFAULT_VAR_FILTER "!__k???tab_* & !__crc_*"
#define DEFAULT_FUNC_FILTER "!_*"
#define DEFAULT_LIST_FILTER "*"

/* Session management structure */
static struct {
	int command;	/* Command short_name */
	bool list_events;
	bool uprobes;
	bool quiet;
	bool target_used;
	int nevents;
	struct perf_probe_event events[MAX_PROBES];
	struct line_range line_range;
	char *target;
	struct strfilter *filter;
	struct nsinfo *nsi;
} params;

/* Parse an event definition. Note that any error must die. */
static int parse_probe_event(const char *str)
{
	struct perf_probe_event *pev = &params.events[params.nevents];
	int ret;

	pr_debug("probe-definition(%d): %s\n", params.nevents, str);
	if (++params.nevents == MAX_PROBES) {
		pr_err("Too many probes (> %d) were specified.", MAX_PROBES);
		return -1;
	}

	pev->uprobes = params.uprobes;
	if (params.target) {
		pev->target = strdup(params.target);
		if (!pev->target)
			return -ENOMEM;
		params.target_used = true;
	}

	pev->nsi = nsinfo__get(params.nsi);

	/* Parse a perf-probe command into event */
	ret = parse_perf_probe_command(str, pev);
	pr_debug("%d arguments\n", pev->nargs);

	return ret;
}

static int params_add_filter(const char *str)
{
	const char *err = NULL;
	int ret = 0;

	pr_debug2("Add filter: %s\n", str);
	if (!params.filter) {
		params.filter = strfilter__new(str, &err);
		if (!params.filter)
			ret = err ? -EINVAL : -ENOMEM;
	} else
		ret = strfilter__or(params.filter, str, &err);

	if (ret == -EINVAL) {
		pr_err("Filter parse error at %td.\n", err - str + 1);
		pr_err("Source: \"%s\"\n", str);
		pr_err("         %*c\n", (int)(err - str + 1), '^');
	}

	return ret;
}

static int set_target(const char *ptr)
{
	int found = 0;
	const char *buf;

	/*
	 * The first argument after options can be an absolute path
	 * to an executable / library or kernel module.
	 *
	 * TODO: Support relative path, and $PATH, $LD_LIBRARY_PATH,
	 * short module name.
	 */
	if (!params.target && ptr && *ptr == '/') {
		params.target = strdup(ptr);
		if (!params.target)
			return -ENOMEM;
		params.target_used = false;

		found = 1;
		buf = ptr + (strlen(ptr) - 3);

		if (strcmp(buf, ".ko"))
			params.uprobes = true;

	}

	return found;
}

static int parse_probe_event_argv(int argc, const char **argv)
{
	int i, len, ret, found_target;
	char *buf;

	found_target = set_target(argv[0]);
	if (found_target < 0)
		return found_target;

	if (found_target && argc == 1)
		return 0;

	/* Bind up rest arguments */
	len = 0;
	for (i = 0; i < argc; i++) {
		if (i == 0 && found_target)
			continue;

		len += strlen(argv[i]) + 1;
	}
	buf = zalloc(len + 1);
	if (buf == NULL)
		return -ENOMEM;
	len = 0;
	for (i = 0; i < argc; i++) {
		if (i == 0 && found_target)
			continue;

		len += sprintf(&buf[len], "%s ", argv[i]);
	}
	ret = parse_probe_event(buf);
	free(buf);
	return ret;
}

static int opt_set_target(const struct option *opt, const char *str,
			int unset __maybe_unused)
{
	int ret = -ENOENT;
	char *tmp;

	if  (str) {
		if (!strcmp(opt->long_name, "exec"))
			params.uprobes = true;
		else if (!strcmp(opt->long_name, "module"))
			params.uprobes = false;
		else
			return ret;

		/* Expand given path to absolute path, except for modulename */
		if (params.uprobes || strchr(str, '/')) {
			tmp = nsinfo__realpath(str, params.nsi);
			if (!tmp) {
				pr_warning("Failed to get the absolute path of %s: %m\n", str);
				return ret;
			}
		} else {
			tmp = strdup(str);
			if (!tmp)
				return -ENOMEM;
		}
		free(params.target);
		params.target = tmp;
		params.target_used = false;
		ret = 0;
	}

	return ret;
}

static int opt_set_target_ns(const struct option *opt __maybe_unused,
			     const char *str, int unset __maybe_unused)
{
	int ret = -ENOENT;
	pid_t ns_pid;
	struct nsinfo *nsip;

	if (str) {
		errno = 0;
		ns_pid = (pid_t)strtol(str, NULL, 10);
		if (errno != 0) {
			ret = -errno;
			pr_warning("Failed to parse %s as a pid: %s\n", str,
				   strerror(errno));
			return ret;
		}
		nsip = nsinfo__new(ns_pid);
		if (nsip && nsip->need_setns)
			params.nsi = nsinfo__get(nsip);
		nsinfo__put(nsip);

		ret = 0;
	}

	return ret;
}


/* Command option callbacks */

#ifdef HAVE_DWARF_SUPPORT
static int opt_show_lines(const struct option *opt,
			  const char *str, int unset __maybe_unused)
{
	int ret = 0;

	if (!str)
		return 0;

	if (params.command == 'L') {
		pr_warning("Warning: more than one --line options are"
			   " detected. Only the first one is valid.\n");
		return 0;
	}

	params.command = opt->short_name;
	ret = parse_line_range_desc(str, &params.line_range);

	return ret;
}

static int opt_show_vars(const struct option *opt,
			 const char *str, int unset __maybe_unused)
{
	struct perf_probe_event *pev = &params.events[params.nevents];
	int ret;

	if (!str)
		return 0;

	ret = parse_probe_event(str);
	if (!ret && pev->nargs != 0) {
		pr_err("  Error: '--vars' doesn't accept arguments.\n");
		return -EINVAL;
	}
	params.command = opt->short_name;

	return ret;
}
#else
# define opt_show_lines NULL
# define opt_show_vars NULL
#endif
static int opt_add_probe_event(const struct option *opt,
			      const char *str, int unset __maybe_unused)
{
	if (str) {
		params.command = opt->short_name;
		return parse_probe_event(str);
	}

	return 0;
}

static int opt_set_filter_with_command(const struct option *opt,
				       const char *str, int unset)
{
	if (!unset)
		params.command = opt->short_name;

	if (str)
		return params_add_filter(str);

	return 0;
}

static int opt_set_filter(const struct option *opt __maybe_unused,
			  const char *str, int unset __maybe_unused)
{
	if (str)
		return params_add_filter(str);

	return 0;
}

static int init_params(void)
{
	return line_range__init(&params.line_range);
}

static void cleanup_params(void)
{
	int i;

	for (i = 0; i < params.nevents; i++)
		clear_perf_probe_event(params.events + i);
	line_range__clear(&params.line_range);
	free(params.target);
	strfilter__delete(params.filter);
	nsinfo__put(params.nsi);
	memset(&params, 0, sizeof(params));
}

static void pr_err_with_code(const char *msg, int err)
{
	char sbuf[STRERR_BUFSIZE];

	pr_err("%s", msg);
	pr_debug(" Reason: %s (Code: %d)",
		 str_error_r(-err, sbuf, sizeof(sbuf)), err);
	pr_err("\n");
}

static int perf_add_probe_events(struct perf_probe_event *pevs, int npevs)
{
	int ret;
	int i, k;
	const char *event = NULL, *group = NULL;

	ret = init_probe_symbol_maps(pevs->uprobes);
	if (ret < 0)
		return ret;

	ret = convert_perf_probe_events(pevs, npevs);
	if (ret < 0)
		goto out_cleanup;

	if (params.command == 'D') {	/* it shows definition */
		ret = show_probe_trace_events(pevs, npevs);
		goto out_cleanup;
	}

	ret = apply_perf_probe_events(pevs, npevs);
	if (ret < 0)
		goto out_cleanup;

	for (i = k = 0; i < npevs; i++)
		k += pevs[i].ntevs;

	pr_info("Added new event%s\n", (k > 1) ? "s:" : ":");
	for (i = 0; i < npevs; i++) {
		struct perf_probe_event *pev = &pevs[i];

		for (k = 0; k < pev->ntevs; k++) {
			struct probe_trace_event *tev = &pev->tevs[k];
			/* Skipped events have no event name */
			if (!tev->event)
				continue;

			/* We use tev's name for showing new events */
			show_perf_probe_event(tev->group, tev->event, pev,
					      tev->point.module, false);

			/* Save the last valid name */
			event = tev->event;
			group = tev->group;
		}
	}

	/* Note that it is possible to skip all events because of blacklist */
	if (event) {
		/* Show how to use the event. */
		pr_info("\nYou can now use it in all perf tools, such as:\n\n");
		pr_info("\tperf record -e %s:%s -aR sleep 1\n\n", group, event);
	}

out_cleanup:
	cleanup_perf_probe_events(pevs, npevs);
	exit_probe_symbol_maps();
	return ret;
}

static int del_perf_probe_caches(struct strfilter *filter)
{
	struct probe_cache *cache;
	struct strlist *bidlist;
	struct str_node *nd;
	int ret;

	bidlist = build_id_cache__list_all(false);
	if (!bidlist) {
		ret = -errno;
		pr_debug("Failed to get buildids: %d\n", ret);
		return ret ?: -ENOMEM;
	}

	strlist__for_each_entry(nd, bidlist) {
		cache = probe_cache__new(nd->s, NULL);
		if (!cache)
			continue;
		if (probe_cache__filter_purge(cache, filter) < 0 ||
		    probe_cache__commit(cache) < 0)
			pr_warning("Failed to remove entries for %s\n", nd->s);
		probe_cache__delete(cache);
	}
	return 0;
}

static int perf_del_probe_events(struct strfilter *filter)
{
	int ret, ret2, ufd = -1, kfd = -1;
	char *str = strfilter__string(filter);
	struct strlist *klist = NULL, *ulist = NULL;
	struct str_node *ent;

	if (!str)
		return -EINVAL;

	pr_debug("Delete filter: \'%s\'\n", str);

	if (probe_conf.cache)
		return del_perf_probe_caches(filter);

	/* Get current event names */
	ret = probe_file__open_both(&kfd, &ufd, PF_FL_RW);
	if (ret < 0)
		goto out;

	klist = strlist__new(NULL, NULL);
	ulist = strlist__new(NULL, NULL);
	if (!klist || !ulist) {
		ret = -ENOMEM;
		goto out;
	}

	ret = probe_file__get_events(kfd, filter, klist);
	if (ret == 0) {
		strlist__for_each_entry(ent, klist)
			pr_info("Removed event: %s\n", ent->s);

		ret = probe_file__del_strlist(kfd, klist);
		if (ret < 0)
			goto error;
	} else if (ret == -ENOMEM)
		goto error;

	ret2 = probe_file__get_events(ufd, filter, ulist);
	if (ret2 == 0) {
		strlist__for_each_entry(ent, ulist)
			pr_info("Removed event: %s\n", ent->s);

		ret2 = probe_file__del_strlist(ufd, ulist);
		if (ret2 < 0)
			goto error;
	} else if (ret2 == -ENOMEM)
		goto error;

	if (ret == -ENOENT && ret2 == -ENOENT)
		pr_warning("\"%s\" does not hit any event.\n", str);
	else
		ret = 0;

error:
	if (kfd >= 0)
		close(kfd);
	if (ufd >= 0)
		close(ufd);
out:
	strlist__delete(klist);
	strlist__delete(ulist);
	free(str);

	return ret;
}

#ifdef HAVE_DWARF_SUPPORT
#define PROBEDEF_STR	\
	"[EVENT=]FUNC[@SRC][+OFF|%return|:RL|;PT]|SRC:AL|SRC;PT [[NAME=]ARG ...]"
#else
#define PROBEDEF_STR	"[EVENT=]FUNC[+OFF|%return] [[NAME=]ARG ...]"
#endif


static int
__cmd_probe(int argc, const char **argv)
{
	const char * const probe_usage[] = {
		"perf probe [<options>] 'PROBEDEF' ['PROBEDEF' ...]",
		"perf probe [<options>] --add 'PROBEDEF' [--add 'PROBEDEF' ...]",
		"perf probe [<options>] --del '[GROUP:]EVENT' ...",
		"perf probe --list [GROUP:]EVENT ...",
#ifdef HAVE_DWARF_SUPPORT
		"perf probe [<options>] --line 'LINEDESC'",
		"perf probe [<options>] --vars 'PROBEPOINT'",
#endif
		"perf probe [<options>] --funcs",
		NULL
	};
	struct option options[] = {
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show parsed arguments, etc)"),
	OPT_BOOLEAN('q', "quiet", &params.quiet,
		    "be quiet (do not show any messages)"),
	OPT_CALLBACK_DEFAULT('l', "list", NULL, "[GROUP:]EVENT",
			     "list up probe events",
			     opt_set_filter_with_command, DEFAULT_LIST_FILTER),
	OPT_CALLBACK('d', "del", NULL, "[GROUP:]EVENT", "delete a probe event.",
		     opt_set_filter_with_command),
	OPT_CALLBACK('a', "add", NULL, PROBEDEF_STR,
		"probe point definition, where\n"
		"\t\tGROUP:\tGroup name (optional)\n"
		"\t\tEVENT:\tEvent name\n"
		"\t\tFUNC:\tFunction name\n"
		"\t\tOFF:\tOffset from function entry (in byte)\n"
		"\t\t%return:\tPut the probe at function return\n"
#ifdef HAVE_DWARF_SUPPORT
		"\t\tSRC:\tSource code path\n"
		"\t\tRL:\tRelative line number from function entry.\n"
		"\t\tAL:\tAbsolute line number in file.\n"
		"\t\tPT:\tLazy expression of line code.\n"
		"\t\tARG:\tProbe argument (local variable name or\n"
		"\t\t\tkprobe-tracer argument format.)\n",
#else
		"\t\tARG:\tProbe argument (kprobe-tracer argument format.)\n",
#endif
		opt_add_probe_event),
	OPT_CALLBACK('D', "definition", NULL, PROBEDEF_STR,
		"Show trace event definition of given traceevent for k/uprobe_events.",
		opt_add_probe_event),
	OPT_BOOLEAN('f', "force", &probe_conf.force_add, "forcibly add events"
		    " with existing name"),
	OPT_CALLBACK('L', "line", NULL,
		     "FUNC[:RLN[+NUM|-RLN2]]|SRC:ALN[+NUM|-ALN2]",
		     "Show source code lines.", opt_show_lines),
	OPT_CALLBACK('V', "vars", NULL,
		     "FUNC[@SRC][+OFF|%return|:RL|;PT]|SRC:AL|SRC;PT",
		     "Show accessible variables on PROBEDEF", opt_show_vars),
	OPT_BOOLEAN('\0', "externs", &probe_conf.show_ext_vars,
		    "Show external variables too (with --vars only)"),
	OPT_BOOLEAN('\0', "range", &probe_conf.show_location_range,
		"Show variables location range in scope (with --vars only)"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_STRING('s', "source", &symbol_conf.source_prefix,
		   "directory", "path to kernel source"),
	OPT_BOOLEAN('\0', "no-inlines", &probe_conf.no_inlines,
		"Don't search inlined functions"),
	OPT__DRY_RUN(&probe_event_dry_run),
	OPT_INTEGER('\0', "max-probes", &probe_conf.max_probes,
		 "Set how many probe points can be found for a probe."),
	OPT_CALLBACK_DEFAULT('F', "funcs", NULL, "[FILTER]",
			     "Show potential probe-able functions.",
			     opt_set_filter_with_command, DEFAULT_FUNC_FILTER),
	OPT_CALLBACK('\0', "filter", NULL,
		     "[!]FILTER", "Set a filter (with --vars/funcs only)\n"
		     "\t\t\t(default: \"" DEFAULT_VAR_FILTER "\" for --vars,\n"
		     "\t\t\t \"" DEFAULT_FUNC_FILTER "\" for --funcs)",
		     opt_set_filter),
	OPT_CALLBACK('x', "exec", NULL, "executable|path",
			"target executable name or path", opt_set_target),
	OPT_CALLBACK('m', "module", NULL, "modname|path",
		"target module name (for online) or path (for offline)",
		opt_set_target),
	OPT_BOOLEAN(0, "demangle", &symbol_conf.demangle,
		    "Enable symbol demangling"),
	OPT_BOOLEAN(0, "demangle-kernel", &symbol_conf.demangle_kernel,
		    "Enable kernel symbol demangling"),
	OPT_BOOLEAN(0, "cache", &probe_conf.cache, "Manipulate probe cache"),
	OPT_STRING(0, "symfs", &symbol_conf.symfs, "directory",
		   "Look for files with symbols relative to this directory"),
	OPT_CALLBACK(0, "target-ns", NULL, "pid",
		     "target pid for namespace contexts", opt_set_target_ns),
	OPT_END()
	};
	int ret;

	set_option_flag(options, 'a', "add", PARSE_OPT_EXCLUSIVE);
	set_option_flag(options, 'd', "del", PARSE_OPT_EXCLUSIVE);
	set_option_flag(options, 'D', "definition", PARSE_OPT_EXCLUSIVE);
	set_option_flag(options, 'l', "list", PARSE_OPT_EXCLUSIVE);
#ifdef HAVE_DWARF_SUPPORT
	set_option_flag(options, 'L', "line", PARSE_OPT_EXCLUSIVE);
	set_option_flag(options, 'V', "vars", PARSE_OPT_EXCLUSIVE);
#else
# define set_nobuild(s, l, c) set_option_nobuild(options, s, l, "NO_DWARF=1", c)
	set_nobuild('L', "line", false);
	set_nobuild('V', "vars", false);
	set_nobuild('\0', "externs", false);
	set_nobuild('\0', "range", false);
	set_nobuild('k', "vmlinux", true);
	set_nobuild('s', "source", true);
	set_nobuild('\0', "no-inlines", true);
# undef set_nobuild
#endif
	set_option_flag(options, 'F', "funcs", PARSE_OPT_EXCLUSIVE);

	argc = parse_options(argc, argv, options, probe_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc > 0) {
		if (strcmp(argv[0], "-") == 0) {
			usage_with_options_msg(probe_usage, options,
				"'-' is not supported.\n");
		}
		if (params.command && params.command != 'a') {
			usage_with_options_msg(probe_usage, options,
				"another command except --add is set.\n");
		}
		ret = parse_probe_event_argv(argc, argv);
		if (ret < 0) {
			pr_err_with_code("  Error: Command Parse Error.", ret);
			return ret;
		}
		params.command = 'a';
	}

	if (params.quiet) {
		if (verbose != 0) {
			pr_err("  Error: -v and -q are exclusive.\n");
			return -EINVAL;
		}
		verbose = -1;
	}

	if (probe_conf.max_probes == 0)
		probe_conf.max_probes = MAX_PROBES;

	/*
	 * Only consider the user's kernel image path if given.
	 */
	symbol_conf.try_vmlinux_path = (symbol_conf.vmlinux_name == NULL);

	/*
	 * Except for --list, --del and --add, other command doesn't depend
	 * nor change running kernel. So if user gives offline vmlinux,
	 * ignore its buildid.
	 */
	if (!strchr("lda", params.command) && symbol_conf.vmlinux_name)
		symbol_conf.ignore_vmlinux_buildid = true;

	switch (params.command) {
	case 'l':
		if (params.uprobes) {
			pr_err("  Error: Don't use --list with --exec.\n");
			parse_options_usage(probe_usage, options, "l", true);
			parse_options_usage(NULL, options, "x", true);
			return -EINVAL;
		}
		ret = show_perf_probe_events(params.filter);
		if (ret < 0)
			pr_err_with_code("  Error: Failed to show event list.", ret);
		return ret;
	case 'F':
		ret = show_available_funcs(params.target, params.nsi,
					   params.filter, params.uprobes);
		if (ret < 0)
			pr_err_with_code("  Error: Failed to show functions.", ret);
		return ret;
#ifdef HAVE_DWARF_SUPPORT
	case 'L':
		ret = show_line_range(&params.line_range, params.target,
				      params.nsi, params.uprobes);
		if (ret < 0)
			pr_err_with_code("  Error: Failed to show lines.", ret);
		return ret;
	case 'V':
		if (!params.filter)
			params.filter = strfilter__new(DEFAULT_VAR_FILTER,
						       NULL);

		ret = show_available_vars(params.events, params.nevents,
					  params.filter);
		if (ret < 0)
			pr_err_with_code("  Error: Failed to show vars.", ret);
		return ret;
#endif
	case 'd':
		ret = perf_del_probe_events(params.filter);
		if (ret < 0) {
			pr_err_with_code("  Error: Failed to delete events.", ret);
			return ret;
		}
		break;
	case 'D':
	case 'a':

		/* Ensure the last given target is used */
		if (params.target && !params.target_used) {
			pr_err("  Error: -x/-m must follow the probe definitions.\n");
			parse_options_usage(probe_usage, options, "m", true);
			parse_options_usage(NULL, options, "x", true);
			return -EINVAL;
		}

		ret = perf_add_probe_events(params.events, params.nevents);
		if (ret < 0) {

			/*
			 * When perf_add_probe_events() fails it calls
			 * cleanup_perf_probe_events(pevs, npevs), i.e.
			 * cleanup_perf_probe_events(params.events, params.nevents), which
			 * will call clear_perf_probe_event(), so set nevents to zero
			 * to avoid cleanup_params() to call clear_perf_probe_event() again
			 * on the same pevs.
			 */
			params.nevents = 0;
			pr_err_with_code("  Error: Failed to add events.", ret);
			return ret;
		}
		break;
	default:
		usage_with_options(probe_usage, options);
	}
	return 0;
}

int cmd_probe(int argc, const char **argv)
{
	int ret;

	ret = init_params();
	if (!ret) {
		ret = __cmd_probe(argc, argv);
		cleanup_params();
	}

	return ret < 0 ? ret : 0;
}

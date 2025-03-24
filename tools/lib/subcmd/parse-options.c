// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "subcmd-util.h"
#include "parse-options.h"
#include "subcmd-config.h"
#include "pager.h"

#define OPT_SHORT 1
#define OPT_UNSET 2

char *error_buf;

static int opterror(const struct option *opt, const char *reason, int flags)
{
	if (flags & OPT_SHORT)
		fprintf(stderr, " Error: switch `%c' %s", opt->short_name, reason);
	else if (flags & OPT_UNSET)
		fprintf(stderr, " Error: option `no-%s' %s", opt->long_name, reason);
	else
		fprintf(stderr, " Error: option `%s' %s", opt->long_name, reason);

	return -1;
}

static const char *skip_prefix(const char *str, const char *prefix)
{
	size_t len = strlen(prefix);
	return strncmp(str, prefix, len) ? NULL : str + len;
}

static void optwarning(const struct option *opt, const char *reason, int flags)
{
	if (flags & OPT_SHORT)
		fprintf(stderr, " Warning: switch `%c' %s", opt->short_name, reason);
	else if (flags & OPT_UNSET)
		fprintf(stderr, " Warning: option `no-%s' %s", opt->long_name, reason);
	else
		fprintf(stderr, " Warning: option `%s' %s", opt->long_name, reason);
}

static int get_arg(struct parse_opt_ctx_t *p, const struct option *opt,
		   int flags, const char **arg)
{
	const char *res;

	if (p->opt) {
		res = p->opt;
		p->opt = NULL;
	} else if ((opt->flags & PARSE_OPT_LASTARG_DEFAULT) && (p->argc == 1 ||
		    **(p->argv + 1) == '-')) {
		res = (const char *)opt->defval;
	} else if (p->argc > 1) {
		p->argc--;
		res = *++p->argv;
	} else
		return opterror(opt, "requires a value", flags);
	if (arg)
		*arg = res;
	return 0;
}

static int get_value(struct parse_opt_ctx_t *p,
		     const struct option *opt, int flags)
{
	const char *s, *arg = NULL;
	const int unset = flags & OPT_UNSET;
	int err;

	if (unset && p->opt)
		return opterror(opt, "takes no value", flags);
	if (unset && (opt->flags & PARSE_OPT_NONEG))
		return opterror(opt, "isn't available", flags);
	if (opt->flags & PARSE_OPT_DISABLED)
		return opterror(opt, "is not usable", flags);

	if (opt->flags & PARSE_OPT_EXCLUSIVE) {
		if (p->excl_opt && p->excl_opt != opt) {
			char msg[128];

			if (((flags & OPT_SHORT) && p->excl_opt->short_name) ||
			    p->excl_opt->long_name == NULL) {
				snprintf(msg, sizeof(msg), "cannot be used with switch `%c'",
					 p->excl_opt->short_name);
			} else {
				snprintf(msg, sizeof(msg), "cannot be used with %s",
					 p->excl_opt->long_name);
			}
			opterror(opt, msg, flags);
			return -3;
		}
		p->excl_opt = opt;
	}
	if (!(flags & OPT_SHORT) && p->opt) {
		switch (opt->type) {
		case OPTION_CALLBACK:
			if (!(opt->flags & PARSE_OPT_NOARG))
				break;
			/* FALLTHROUGH */
		case OPTION_BOOLEAN:
		case OPTION_INCR:
		case OPTION_BIT:
		case OPTION_SET_UINT:
		case OPTION_SET_PTR:
			return opterror(opt, "takes no value", flags);
		case OPTION_END:
		case OPTION_ARGUMENT:
		case OPTION_GROUP:
		case OPTION_STRING:
		case OPTION_INTEGER:
		case OPTION_UINTEGER:
		case OPTION_LONG:
		case OPTION_ULONG:
		case OPTION_U64:
		default:
			break;
		}
	}

	if (opt->flags & PARSE_OPT_NOBUILD) {
		char reason[128];
		bool noarg = false;

		err = snprintf(reason, sizeof(reason),
				opt->flags & PARSE_OPT_CANSKIP ?
					"is being ignored because %s " :
					"is not available because %s",
				opt->build_opt);
		reason[sizeof(reason) - 1] = '\0';

		if (err < 0)
			strncpy(reason, opt->flags & PARSE_OPT_CANSKIP ?
					"is being ignored" :
					"is not available",
					sizeof(reason));

		if (!(opt->flags & PARSE_OPT_CANSKIP))
			return opterror(opt, reason, flags);

		err = 0;
		if (unset)
			noarg = true;
		if (opt->flags & PARSE_OPT_NOARG)
			noarg = true;
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt)
			noarg = true;

		switch (opt->type) {
		case OPTION_BOOLEAN:
		case OPTION_INCR:
		case OPTION_BIT:
		case OPTION_SET_UINT:
		case OPTION_SET_PTR:
		case OPTION_END:
		case OPTION_ARGUMENT:
		case OPTION_GROUP:
			noarg = true;
			break;
		case OPTION_CALLBACK:
		case OPTION_STRING:
		case OPTION_INTEGER:
		case OPTION_UINTEGER:
		case OPTION_LONG:
		case OPTION_ULONG:
		case OPTION_U64:
		default:
			break;
		}

		if (!noarg)
			err = get_arg(p, opt, flags, NULL);
		if (err)
			return err;

		optwarning(opt, reason, flags);
		return 0;
	}

	switch (opt->type) {
	case OPTION_BIT:
		if (unset)
			*(int *)opt->value &= ~opt->defval;
		else
			*(int *)opt->value |= opt->defval;
		return 0;

	case OPTION_BOOLEAN:
		*(bool *)opt->value = unset ? false : true;
		if (opt->set)
			*(bool *)opt->set = true;
		return 0;

	case OPTION_INCR:
		*(int *)opt->value = unset ? 0 : *(int *)opt->value + 1;
		return 0;

	case OPTION_SET_UINT:
		*(unsigned int *)opt->value = unset ? 0 : opt->defval;
		return 0;

	case OPTION_SET_PTR:
		*(void **)opt->value = unset ? NULL : (void *)opt->defval;
		return 0;

	case OPTION_STRING:
		err = 0;
		if (unset)
			*(const char **)opt->value = NULL;
		else if (opt->flags & PARSE_OPT_OPTARG && !p->opt)
			*(const char **)opt->value = (const char *)opt->defval;
		else
			err = get_arg(p, opt, flags, (const char **)opt->value);

		if (opt->set)
			*(bool *)opt->set = true;

		/* PARSE_OPT_NOEMPTY: Allow NULL but disallow empty string. */
		if (opt->flags & PARSE_OPT_NOEMPTY) {
			const char *val = *(const char **)opt->value;

			if (!val)
				return err;

			/* Similar to unset if we are given an empty string. */
			if (val[0] == '\0') {
				*(const char **)opt->value = NULL;
				return 0;
			}
		}

		return err;

	case OPTION_CALLBACK:
		if (opt->set)
			*(bool *)opt->set = true;

		if (unset)
			return (*opt->callback)(opt, NULL, 1) ? (-1) : 0;
		if (opt->flags & PARSE_OPT_NOARG)
			return (*opt->callback)(opt, NULL, 0) ? (-1) : 0;
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt)
			return (*opt->callback)(opt, NULL, 0) ? (-1) : 0;
		if (get_arg(p, opt, flags, &arg))
			return -1;
		return (*opt->callback)(opt, arg, 0) ? (-1) : 0;

	case OPTION_INTEGER:
		if (unset) {
			*(int *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(int *)opt->value = opt->defval;
			return 0;
		}
		if (get_arg(p, opt, flags, &arg))
			return -1;
		*(int *)opt->value = strtol(arg, (char **)&s, 10);
		if (*s)
			return opterror(opt, "expects a numerical value", flags);
		return 0;

	case OPTION_UINTEGER:
		if (unset) {
			*(unsigned int *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(unsigned int *)opt->value = opt->defval;
			return 0;
		}
		if (get_arg(p, opt, flags, &arg))
			return -1;
		if (arg[0] == '-')
			return opterror(opt, "expects an unsigned numerical value", flags);
		*(unsigned int *)opt->value = strtol(arg, (char **)&s, 10);
		if (*s)
			return opterror(opt, "expects a numerical value", flags);
		return 0;

	case OPTION_LONG:
		if (unset) {
			*(long *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(long *)opt->value = opt->defval;
			return 0;
		}
		if (get_arg(p, opt, flags, &arg))
			return -1;
		*(long *)opt->value = strtol(arg, (char **)&s, 10);
		if (*s)
			return opterror(opt, "expects a numerical value", flags);
		return 0;

	case OPTION_ULONG:
		if (unset) {
			*(unsigned long *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(unsigned long *)opt->value = opt->defval;
			return 0;
		}
		if (get_arg(p, opt, flags, &arg))
			return -1;
		*(unsigned long *)opt->value = strtoul(arg, (char **)&s, 10);
		if (*s)
			return opterror(opt, "expects a numerical value", flags);
		return 0;

	case OPTION_U64:
		if (unset) {
			*(u64 *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(u64 *)opt->value = opt->defval;
			return 0;
		}
		if (get_arg(p, opt, flags, &arg))
			return -1;
		if (arg[0] == '-')
			return opterror(opt, "expects an unsigned numerical value", flags);
		*(u64 *)opt->value = strtoull(arg, (char **)&s, 10);
		if (*s)
			return opterror(opt, "expects a numerical value", flags);
		return 0;

	case OPTION_END:
	case OPTION_ARGUMENT:
	case OPTION_GROUP:
	default:
		die("should not happen, someone must be hit on the forehead");
	}
}

static int parse_short_opt(struct parse_opt_ctx_t *p, const struct option *options)
{
retry:
	for (; options->type != OPTION_END; options++) {
		if (options->short_name == *p->opt) {
			p->opt = p->opt[1] ? p->opt + 1 : NULL;
			return get_value(p, options, OPT_SHORT);
		}
	}

	if (options->parent) {
		options = options->parent;
		goto retry;
	}

	return -2;
}

static int parse_long_opt(struct parse_opt_ctx_t *p, const char *arg,
                          const struct option *options)
{
	const char *arg_end = strchr(arg, '=');
	const struct option *abbrev_option = NULL, *ambiguous_option = NULL;
	int abbrev_flags = 0, ambiguous_flags = 0;

	if (!arg_end)
		arg_end = arg + strlen(arg);

retry:
	for (; options->type != OPTION_END; options++) {
		const char *rest;
		int flags = 0;

		if (!options->long_name)
			continue;

		rest = skip_prefix(arg, options->long_name);
		if (options->type == OPTION_ARGUMENT) {
			if (!rest)
				continue;
			if (*rest == '=')
				return opterror(options, "takes no value", flags);
			if (*rest)
				continue;
			p->out[p->cpidx++] = arg - 2;
			return 0;
		}
		if (!rest) {
			if (strstarts(options->long_name, "no-")) {
				/*
				 * The long name itself starts with "no-", so
				 * accept the option without "no-" so that users
				 * do not have to enter "no-no-" to get the
				 * negation.
				 */
				rest = skip_prefix(arg, options->long_name + 3);
				if (rest) {
					flags |= OPT_UNSET;
					goto match;
				}
				/* Abbreviated case */
				if (strstarts(options->long_name + 3, arg)) {
					flags |= OPT_UNSET;
					goto is_abbreviated;
				}
			}
			/* abbreviated? */
			if (!strncmp(options->long_name, arg, arg_end - arg)) {
is_abbreviated:
				if (abbrev_option) {
					/*
					 * If this is abbreviated, it is
					 * ambiguous. So when there is no
					 * exact match later, we need to
					 * error out.
					 */
					ambiguous_option = abbrev_option;
					ambiguous_flags = abbrev_flags;
				}
				if (!(flags & OPT_UNSET) && *arg_end)
					p->opt = arg_end + 1;
				abbrev_option = options;
				abbrev_flags = flags;
				continue;
			}
			/* negated and abbreviated very much? */
			if (strstarts("no-", arg)) {
				flags |= OPT_UNSET;
				goto is_abbreviated;
			}
			/* negated? */
			if (strncmp(arg, "no-", 3))
				continue;
			flags |= OPT_UNSET;
			rest = skip_prefix(arg + 3, options->long_name);
			/* abbreviated and negated? */
			if (!rest && strstarts(options->long_name, arg + 3))
				goto is_abbreviated;
			if (!rest)
				continue;
		}
match:
		if (*rest) {
			if (*rest != '=')
				continue;
			p->opt = rest + 1;
		}
		return get_value(p, options, flags);
	}

	if (ambiguous_option) {
		 fprintf(stderr,
			 " Error: Ambiguous option: %s (could be --%s%s or --%s%s)\n",
			 arg,
			 (ambiguous_flags & OPT_UNSET) ?  "no-" : "",
			 ambiguous_option->long_name,
			 (abbrev_flags & OPT_UNSET) ?  "no-" : "",
			 abbrev_option->long_name);
		 return -1;
	}
	if (abbrev_option)
		return get_value(p, abbrev_option, abbrev_flags);

	if (options->parent) {
		options = options->parent;
		goto retry;
	}

	return -2;
}

static void check_typos(const char *arg, const struct option *options)
{
	if (strlen(arg) < 3)
		return;

	if (strstarts(arg, "no-")) {
		fprintf(stderr, " Error: did you mean `--%s` (with two dashes ?)\n", arg);
		exit(129);
	}

	for (; options->type != OPTION_END; options++) {
		if (!options->long_name)
			continue;
		if (strstarts(options->long_name, arg)) {
			fprintf(stderr, " Error: did you mean `--%s` (with two dashes ?)\n", arg);
			exit(129);
		}
	}
}

static void parse_options_start(struct parse_opt_ctx_t *ctx,
				int argc, const char **argv, int flags)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->argc = argc - 1;
	ctx->argv = argv + 1;
	ctx->out  = argv;
	ctx->cpidx = ((flags & PARSE_OPT_KEEP_ARGV0) != 0);
	ctx->flags = flags;
	if ((flags & PARSE_OPT_KEEP_UNKNOWN) &&
	    (flags & PARSE_OPT_STOP_AT_NON_OPTION))
		die("STOP_AT_NON_OPTION and KEEP_UNKNOWN don't go together");
}

static int usage_with_options_internal(const char * const *,
				       const struct option *, int,
				       struct parse_opt_ctx_t *);

static int parse_options_step(struct parse_opt_ctx_t *ctx,
			      const struct option *options,
			      const char * const usagestr[])
{
	int internal_help = !(ctx->flags & PARSE_OPT_NO_INTERNAL_HELP);
	int excl_short_opt = 1;
	const char *arg;

	/* we must reset ->opt, unknown short option leave it dangling */
	ctx->opt = NULL;

	for (; ctx->argc; ctx->argc--, ctx->argv++) {
		arg = ctx->argv[0];
		if (*arg != '-' || !arg[1]) {
			if (ctx->flags & PARSE_OPT_STOP_AT_NON_OPTION)
				break;
			ctx->out[ctx->cpidx++] = ctx->argv[0];
			continue;
		}

		if (arg[1] != '-') {
			ctx->opt = ++arg;
			if (internal_help && *ctx->opt == 'h') {
				return usage_with_options_internal(usagestr, options, 0, ctx);
			}
			switch (parse_short_opt(ctx, options)) {
			case -1:
				return parse_options_usage(usagestr, options, arg, 1);
			case -2:
				goto unknown;
			case -3:
				goto exclusive;
			default:
				break;
			}
			if (ctx->opt)
				check_typos(arg, options);
			while (ctx->opt) {
				if (internal_help && *ctx->opt == 'h')
					return usage_with_options_internal(usagestr, options, 0, ctx);
				arg = ctx->opt;
				switch (parse_short_opt(ctx, options)) {
				case -1:
					return parse_options_usage(usagestr, options, arg, 1);
				case -2:
					/* fake a short option thing to hide the fact that we may have
					 * started to parse aggregated stuff
					 *
					 * This is leaky, too bad.
					 */
					ctx->argv[0] = strdup(ctx->opt - 1);
					*(char *)ctx->argv[0] = '-';
					goto unknown;
				case -3:
					goto exclusive;
				default:
					break;
				}
			}
			continue;
		}

		if (!arg[2]) { /* "--" */
			if (!(ctx->flags & PARSE_OPT_KEEP_DASHDASH)) {
				ctx->argc--;
				ctx->argv++;
			}
			break;
		}

		arg += 2;
		if (internal_help && !strcmp(arg, "help-all"))
			return usage_with_options_internal(usagestr, options, 1, ctx);
		if (internal_help && !strcmp(arg, "help"))
			return usage_with_options_internal(usagestr, options, 0, ctx);
		if (!strcmp(arg, "list-opts"))
			return PARSE_OPT_LIST_OPTS;
		if (!strcmp(arg, "list-cmds"))
			return PARSE_OPT_LIST_SUBCMDS;
		switch (parse_long_opt(ctx, arg, options)) {
		case -1:
			return parse_options_usage(usagestr, options, arg, 0);
		case -2:
			goto unknown;
		case -3:
			excl_short_opt = 0;
			goto exclusive;
		default:
			break;
		}
		continue;
unknown:
		if (!(ctx->flags & PARSE_OPT_KEEP_UNKNOWN))
			return PARSE_OPT_UNKNOWN;
		ctx->out[ctx->cpidx++] = ctx->argv[0];
		ctx->opt = NULL;
	}
	return PARSE_OPT_DONE;

exclusive:
	parse_options_usage(usagestr, options, arg, excl_short_opt);
	if ((excl_short_opt && ctx->excl_opt->short_name) ||
	    ctx->excl_opt->long_name == NULL) {
		char opt = ctx->excl_opt->short_name;
		parse_options_usage(NULL, options, &opt, 1);
	} else {
		parse_options_usage(NULL, options, ctx->excl_opt->long_name, 0);
	}
	return PARSE_OPT_HELP;
}

static int parse_options_end(struct parse_opt_ctx_t *ctx)
{
	memmove(ctx->out + ctx->cpidx, ctx->argv, ctx->argc * sizeof(*ctx->out));
	ctx->out[ctx->cpidx + ctx->argc] = NULL;
	return ctx->cpidx + ctx->argc;
}

int parse_options_subcommand(int argc, const char **argv, const struct option *options,
			const char *const subcommands[], const char *usagestr[], int flags)
{
	struct parse_opt_ctx_t ctx;

	/* build usage string if it's not provided */
	if (subcommands && !usagestr[0]) {
		char *buf = NULL;

		astrcatf(&buf, "%s %s [<options>] {", subcmd_config.exec_name, argv[0]);

		for (int i = 0; subcommands[i]; i++) {
			if (i)
				astrcat(&buf, "|");
			astrcat(&buf, subcommands[i]);
		}
		astrcat(&buf, "}");

		usagestr[0] = buf;
	}

	parse_options_start(&ctx, argc, argv, flags);
	switch (parse_options_step(&ctx, options, usagestr)) {
	case PARSE_OPT_HELP:
		exit(129);
	case PARSE_OPT_DONE:
		break;
	case PARSE_OPT_LIST_OPTS:
		while (options->type != OPTION_END) {
			if (options->long_name)
				printf("--%s ", options->long_name);
			options++;
		}
		putchar('\n');
		exit(130);
	case PARSE_OPT_LIST_SUBCMDS:
		if (subcommands) {
			for (int i = 0; subcommands[i]; i++)
				printf("%s ", subcommands[i]);
		}
		putchar('\n');
		exit(130);
	default: /* PARSE_OPT_UNKNOWN */
		if (ctx.argv[0][1] == '-')
			astrcatf(&error_buf, "unknown option `%s'",
				 ctx.argv[0] + 2);
		else
			astrcatf(&error_buf, "unknown switch `%c'", *ctx.opt);
		usage_with_options(usagestr, options);
	}

	return parse_options_end(&ctx);
}

int parse_options(int argc, const char **argv, const struct option *options,
		  const char * const usagestr[], int flags)
{
	return parse_options_subcommand(argc, argv, options, NULL,
					(const char **) usagestr, flags);
}

#define USAGE_OPTS_WIDTH 24
#define USAGE_GAP         2

static void print_option_help(const struct option *opts, int full)
{
	size_t pos;
	int pad;

	if (opts->type == OPTION_GROUP) {
		fputc('\n', stderr);
		if (*opts->help)
			fprintf(stderr, "%s\n", opts->help);
		return;
	}
	if (!full && (opts->flags & PARSE_OPT_HIDDEN))
		return;
	if (opts->flags & PARSE_OPT_DISABLED)
		return;

	pos = fprintf(stderr, "    ");
	if (opts->short_name)
		pos += fprintf(stderr, "-%c", opts->short_name);
	else
		pos += fprintf(stderr, "    ");

	if (opts->long_name && opts->short_name)
		pos += fprintf(stderr, ", ");
	if (opts->long_name)
		pos += fprintf(stderr, "--%s", opts->long_name);

	switch (opts->type) {
	case OPTION_ARGUMENT:
		break;
	case OPTION_LONG:
	case OPTION_ULONG:
	case OPTION_U64:
	case OPTION_INTEGER:
	case OPTION_UINTEGER:
		if (opts->flags & PARSE_OPT_OPTARG)
			if (opts->long_name)
				pos += fprintf(stderr, "[=<n>]");
			else
				pos += fprintf(stderr, "[<n>]");
		else
			pos += fprintf(stderr, " <n>");
		break;
	case OPTION_CALLBACK:
		if (opts->flags & PARSE_OPT_NOARG)
			break;
		/* FALLTHROUGH */
	case OPTION_STRING:
		if (opts->argh) {
			if (opts->flags & PARSE_OPT_OPTARG)
				if (opts->long_name)
					pos += fprintf(stderr, "[=<%s>]", opts->argh);
				else
					pos += fprintf(stderr, "[<%s>]", opts->argh);
			else
				pos += fprintf(stderr, " <%s>", opts->argh);
		} else {
			if (opts->flags & PARSE_OPT_OPTARG)
				if (opts->long_name)
					pos += fprintf(stderr, "[=...]");
				else
					pos += fprintf(stderr, "[...]");
			else
				pos += fprintf(stderr, " ...");
		}
		break;
	default: /* OPTION_{BIT,BOOLEAN,SET_UINT,SET_PTR} */
	case OPTION_END:
	case OPTION_GROUP:
	case OPTION_BIT:
	case OPTION_BOOLEAN:
	case OPTION_INCR:
	case OPTION_SET_UINT:
	case OPTION_SET_PTR:
		break;
	}

	if (pos <= USAGE_OPTS_WIDTH)
		pad = USAGE_OPTS_WIDTH - pos;
	else {
		fputc('\n', stderr);
		pad = USAGE_OPTS_WIDTH;
	}
	fprintf(stderr, "%*s%s\n", pad + USAGE_GAP, "", opts->help);
	if (opts->flags & PARSE_OPT_NOBUILD)
		fprintf(stderr, "%*s(not built-in because %s)\n",
			USAGE_OPTS_WIDTH + USAGE_GAP, "",
			opts->build_opt);
}

static int option__cmp(const void *va, const void *vb)
{
	const struct option *a = va, *b = vb;
	int sa = tolower(a->short_name), sb = tolower(b->short_name), ret;

	if (sa == 0)
		sa = 'z' + 1;
	if (sb == 0)
		sb = 'z' + 1;

	ret = sa - sb;

	if (ret == 0) {
		const char *la = a->long_name ?: "",
			   *lb = b->long_name ?: "";
		ret = strcmp(la, lb);
	}

	return ret;
}

static struct option *options__order(const struct option *opts)
{
	int nr_opts = 0, nr_group = 0, nr_parent = 0, len;
	const struct option *o = NULL, *p = opts;
	struct option *opt, *ordered = NULL, *group;

	/* flatten the options that have parents */
	for (p = opts; p != NULL; p = o->parent) {
		for (o = p; o->type != OPTION_END; o++)
			++nr_opts;

		/*
		 * the length is given by the number of options plus a null
		 * terminator for the last loop iteration.
		 */
		len = sizeof(*o) * (nr_opts + !o->parent);
		group = realloc(ordered, len);
		if (!group)
			goto out;
		ordered = group;
		memcpy(&ordered[nr_parent], p, sizeof(*o) * (nr_opts - nr_parent));

		nr_parent = nr_opts;
	}
	/* copy the last OPTION_END */
	memcpy(&ordered[nr_opts], o, sizeof(*o));

	/* sort each option group individually */
	for (opt = group = ordered; opt->type != OPTION_END; opt++) {
		if (opt->type == OPTION_GROUP) {
			qsort(group, nr_group, sizeof(*opt), option__cmp);
			group = opt + 1;
			nr_group = 0;
			continue;
		}
		nr_group++;
	}
	qsort(group, nr_group, sizeof(*opt), option__cmp);

out:
	return ordered;
}

static bool option__in_argv(const struct option *opt, const struct parse_opt_ctx_t *ctx)
{
	int i;

	for (i = 1; i < ctx->argc; ++i) {
		const char *arg = ctx->argv[i];

		if (arg[0] != '-') {
			if (arg[1] == '\0') {
				if (arg[0] == opt->short_name)
					return true;
				continue;
			}

			if (opt->long_name && strcmp(opt->long_name, arg) == 0)
				return true;

			if (opt->help && strcasestr(opt->help, arg) != NULL)
				return true;

			continue;
		}

		if (arg[1] == opt->short_name ||
		    (arg[1] == '-' && opt->long_name && strcmp(opt->long_name, arg + 2) == 0))
			return true;
	}

	return false;
}

static int usage_with_options_internal(const char * const *usagestr,
				       const struct option *opts, int full,
				       struct parse_opt_ctx_t *ctx)
{
	struct option *ordered;

	if (!usagestr)
		return PARSE_OPT_HELP;

	setup_pager();

	if (error_buf) {
		fprintf(stderr, "  Error: %s\n", error_buf);
		zfree(&error_buf);
	}

	fprintf(stderr, "\n Usage: %s\n", *usagestr++);
	while (*usagestr && **usagestr)
		fprintf(stderr, "    or: %s\n", *usagestr++);
	while (*usagestr) {
		fprintf(stderr, "%s%s\n",
				**usagestr ? "    " : "",
				*usagestr);
		usagestr++;
	}

	if (opts->type != OPTION_GROUP)
		fputc('\n', stderr);

	ordered = options__order(opts);
	if (ordered)
		opts = ordered;

	for (  ; opts->type != OPTION_END; opts++) {
		if (ctx && ctx->argc > 1 && !option__in_argv(opts, ctx))
			continue;
		print_option_help(opts, full);
	}

	fputc('\n', stderr);

	free(ordered);

	return PARSE_OPT_HELP;
}

void usage_with_options(const char * const *usagestr,
			const struct option *opts)
{
	usage_with_options_internal(usagestr, opts, 0, NULL);
	exit(129);
}

void usage_with_options_msg(const char * const *usagestr,
			    const struct option *opts, const char *fmt, ...)
{
	va_list ap;
	char *tmp = error_buf;

	va_start(ap, fmt);
	if (vasprintf(&error_buf, fmt, ap) == -1)
		die("vasprintf failed");
	va_end(ap);

	free(tmp);

	usage_with_options_internal(usagestr, opts, 0, NULL);
	exit(129);
}

int parse_options_usage(const char * const *usagestr,
			const struct option *opts,
			const char *optstr, bool short_opt)
{
	if (!usagestr)
		goto opt;

	fprintf(stderr, "\n Usage: %s\n", *usagestr++);
	while (*usagestr && **usagestr)
		fprintf(stderr, "    or: %s\n", *usagestr++);
	while (*usagestr) {
		fprintf(stderr, "%s%s\n",
				**usagestr ? "    " : "",
				*usagestr);
		usagestr++;
	}
	fputc('\n', stderr);

opt:
	for (  ; opts->type != OPTION_END; opts++) {
		if (short_opt) {
			if (opts->short_name == *optstr) {
				print_option_help(opts, 0);
				break;
			}
			continue;
		}

		if (opts->long_name == NULL)
			continue;

		if (strstarts(opts->long_name, optstr))
			print_option_help(opts, 0);
		if (strstarts("no-", optstr) &&
		    strstarts(opts->long_name, optstr + 3))
			print_option_help(opts, 0);
	}

	return PARSE_OPT_HELP;
}


int parse_opt_verbosity_cb(const struct option *opt,
			   const char *arg __maybe_unused,
			   int unset)
{
	int *target = opt->value;

	if (unset)
		/* --no-quiet, --no-verbose */
		*target = 0;
	else if (opt->short_name == 'v') {
		if (*target >= 0)
			(*target)++;
		else
			*target = 1;
	} else {
		if (*target <= 0)
			(*target)--;
		else
			*target = -1;
	}
	return 0;
}

static struct option *
find_option(struct option *opts, int shortopt, const char *longopt)
{
	for (; opts->type != OPTION_END; opts++) {
		if ((shortopt && opts->short_name == shortopt) ||
		    (opts->long_name && longopt &&
		     !strcmp(opts->long_name, longopt)))
			return opts;
	}
	return NULL;
}

void set_option_flag(struct option *opts, int shortopt, const char *longopt,
		     int flag)
{
	struct option *opt = find_option(opts, shortopt, longopt);

	if (opt)
		opt->flags |= flag;
	return;
}

void set_option_nobuild(struct option *opts, int shortopt,
			const char *longopt,
			const char *build_opt,
			bool can_skip)
{
	struct option *opt = find_option(opts, shortopt, longopt);

	if (!opt)
		return;

	opt->flags |= PARSE_OPT_NOBUILD;
	opt->flags |= can_skip ? PARSE_OPT_CANSKIP : 0;
	opt->build_opt = build_opt;
}

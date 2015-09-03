#include <linux/compiler.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "parse-options.h"
#include "data-convert-bt.h"

typedef int (*data_cmd_fn_t)(int argc, const char **argv, const char *prefix);

struct data_cmd {
	const char	*name;
	const char	*summary;
	data_cmd_fn_t	fn;
};

static struct data_cmd data_cmds[];

#define for_each_cmd(cmd) \
	for (cmd = data_cmds; cmd && cmd->name; cmd++)

static const struct option data_options[] = {
	OPT_END()
};

static const char * const data_subcommands[] = { "convert", NULL };

static const char *data_usage[] = {
	"perf data [<common options>] <command> [<options>]",
	NULL
};

static void print_usage(void)
{
	struct data_cmd *cmd;

	printf("Usage:\n");
	printf("\t%s\n\n", data_usage[0]);
	printf("\tAvailable commands:\n");

	for_each_cmd(cmd) {
		printf("\t %s\t- %s\n", cmd->name, cmd->summary);
	}

	printf("\n");
}

static const char * const data_convert_usage[] = {
	"perf data convert [<options>]",
	NULL
};

static int cmd_data_convert(int argc, const char **argv,
			    const char *prefix __maybe_unused)
{
	const char *to_ctf     = NULL;
	bool force = false;
	const struct option options[] = {
		OPT_INCR('v', "verbose", &verbose, "be more verbose"),
		OPT_STRING('i', "input", &input_name, "file", "input file name"),
#ifdef HAVE_LIBBABELTRACE_SUPPORT
		OPT_STRING(0, "to-ctf", &to_ctf, NULL, "Convert to CTF format"),
#endif
		OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
		OPT_END()
	};

#ifndef HAVE_LIBBABELTRACE_SUPPORT
	pr_err("No conversion support compiled in.\n");
	return -1;
#endif

	argc = parse_options(argc, argv, options,
			     data_convert_usage, 0);
	if (argc) {
		usage_with_options(data_convert_usage, options);
		return -1;
	}

	if (to_ctf) {
#ifdef HAVE_LIBBABELTRACE_SUPPORT
		return bt_convert__perf2ctf(input_name, to_ctf, force);
#else
		pr_err("The libbabeltrace support is not compiled in.\n");
		return -1;
#endif
	}

	return 0;
}

static struct data_cmd data_cmds[] = {
	{ "convert", "converts data file between formats", cmd_data_convert },
	{ .name = NULL, },
};

int cmd_data(int argc, const char **argv, const char *prefix)
{
	struct data_cmd *cmd;
	const char *cmdstr;

	/* No command specified. */
	if (argc < 2)
		goto usage;

	argc = parse_options_subcommand(argc, argv, data_options, data_subcommands, data_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc < 1)
		goto usage;

	cmdstr = argv[0];

	for_each_cmd(cmd) {
		if (strcmp(cmd->name, cmdstr))
			continue;

		return cmd->fn(argc, argv, prefix);
	}

	pr_err("Unknown command: %s\n", cmdstr);
usage:
	print_usage();
	return -1;
}

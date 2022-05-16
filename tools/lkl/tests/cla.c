#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#ifdef __MINGW32__
#include <winsock2.h>
#else
#ifdef __MSYS__
#include <cygwin/socket.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "cla.h"

static int cl_arg_parse_bool(struct cl_arg *arg, const char *value)
{
	*((int *)arg->store) = 1;
	return 0;
}

static int cl_arg_parse_str(struct cl_arg *arg, const char *value)
{
	*((const char **)arg->store) = value;
	return 0;
}

static int cl_arg_parse_int(struct cl_arg *arg, const char *value)
{
	errno = 0;
	*((int *)arg->store) = strtol(value, NULL, 0);
	return errno == 0;
}

static int cl_arg_parse_str_set(struct cl_arg *arg, const char *value)
{
	const char **set = arg->set;
	int i;

	for (i = 0; set[i] != NULL; i++) {
		if (strcmp(set[i], value) == 0) {
			*((int *)arg->store) = i;
			return 0;
		}
	}

	return -1;
}

static int cl_arg_parse_ipv4(struct cl_arg *arg, const char *value)
{
	unsigned int addr;

	if (!value)
		return -1;

	addr = inet_addr(value);
	if (addr == INADDR_NONE)
		return -1;
	*((unsigned int *)arg->store) = addr;
	return 0;
}

static cl_arg_parser_t parsers[] = {
	[CL_ARG_BOOL] = cl_arg_parse_bool,
	[CL_ARG_INT] = cl_arg_parse_int,
	[CL_ARG_STR] = cl_arg_parse_str,
	[CL_ARG_STR_SET] = cl_arg_parse_str_set,
	[CL_ARG_IPV4] = cl_arg_parse_ipv4,
};

static struct cl_arg *find_short_arg(char name, struct cl_arg *args)
{
	struct cl_arg *arg;

	for (arg = args; arg->short_name != 0; arg++) {
		if (arg->short_name == name)
			return arg;
	}

	return NULL;
}

static struct cl_arg *find_long_arg(const char *name, struct cl_arg *args)
{
	struct cl_arg *arg;

	for (arg = args; arg->long_name; arg++) {
		if (strcmp(arg->long_name, name) == 0)
			return arg;
	}

	return NULL;
}

static void print_help(struct cl_arg *args)
{
	struct cl_arg *arg;

	fprintf(stderr, "usage:\n");
	for (arg = args; arg->long_name; arg++) {
		fprintf(stderr, "-%c, --%-20s %s", arg->short_name,
			arg->long_name, arg->help);
		if (arg->type == CL_ARG_STR_SET) {
			const char **set = arg->set;

			fprintf(stderr, " [ ");
			while (*set != NULL)
				fprintf(stderr, "%s ", *(set++));
			fprintf(stderr, "]");
		}
		fprintf(stderr, "\n");
	}
}

int parse_args(int argc, const char **argv, struct cl_arg *args)
{
	int i;

	for (i = 1; i < argc; i++) {
		struct cl_arg *arg = NULL;
		cl_arg_parser_t parser;

		if (argv[i][0] == '-') {
			if (argv[i][1] != '-')
				arg = find_short_arg(argv[i][1], args);
			else
				arg = find_long_arg(&argv[i][2], args);
		}

		if (!arg) {
			fprintf(stderr, "unknown option '%s'\n", argv[i]);
			print_help(args);
			return -1;
		}

		if (arg->type == CL_ARG_USER || arg->type >= CL_ARG_END)
			parser = arg->parser;
		else
			parser = parsers[arg->type];

		if (!parser) {
			fprintf(stderr, "can't parse --'%s'/-'%c'\n",
				arg->long_name, args->short_name);
			return -1;
		}

		if (parser(arg, argv[i + 1]) < 0) {
			fprintf(stderr, "can't parse '%s'\n", argv[i]);
			print_help(args);
			return -1;
		}

		if (arg->has_arg)
			i++;
	}

	return 0;
}

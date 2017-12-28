#ifndef _LKL_TEST_CLA_H
#define _LKL_TEST_CLA_H

enum cl_arg_type {
	CL_ARG_USER = 0,
	CL_ARG_BOOL,
	CL_ARG_INT,
	CL_ARG_STR,
	CL_ARG_STR_SET,
	CL_ARG_IPV4,
	CL_ARG_END,
};

struct cl_arg;

typedef int (*cl_arg_parser_t)(struct cl_arg *arg, const char *value);

struct cl_arg {
	const char *long_name;
	char short_name;
	const char *help;
	int has_arg;
	enum cl_arg_type type;
	void *store;
	void *set;
	cl_arg_parser_t parser;
};

int parse_args(int argc, const char **argv, struct cl_arg *args);


#endif /* _LKL_TEST_CLA_H */

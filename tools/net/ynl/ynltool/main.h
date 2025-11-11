/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */
/* Copyright Meta Platforms, Inc. and affiliates */

#ifndef __YNLTOOL_H
#define __YNLTOOL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "json_writer.h"

#define NEXT_ARG()	({ argc--; argv++; if (argc < 0) usage(); })
#define NEXT_ARGP()	({ (*argc)--; (*argv)++; if (*argc < 0) usage(); })
#define BAD_ARG()	({ p_err("what is '%s'?", *argv); -1; })
#define GET_ARG()	({ argc--; *argv++; })
#define REQ_ARGS(cnt)							\
	({								\
		int _cnt = (cnt);					\
		bool _res;						\
									\
		if (argc < _cnt) {					\
			p_err("'%s' needs at least %d arguments, %d found", \
			      argv[-1], _cnt, argc);			\
			_res = false;					\
		} else {						\
			_res = true;					\
		}							\
		_res;							\
	})

#define HELP_SPEC_OPTIONS						\
	"OPTIONS := { {-j|--json} [{-p|--pretty}] }"

extern const char *bin_name;

extern json_writer_t *json_wtr;
extern bool json_output;
extern bool pretty_output;

void __attribute__((format(printf, 1, 2))) p_err(const char *fmt, ...);
void __attribute__((format(printf, 1, 2))) p_info(const char *fmt, ...);

bool is_prefix(const char *pfx, const char *str);
int detect_common_prefix(const char *arg, ...);
void usage(void) __attribute__((noreturn));

struct cmd {
	const char *cmd;
	int (*func)(int argc, char **argv);
};

int cmd_select(const struct cmd *cmds, int argc, char **argv,
	       int (*help)(int argc, char **argv));

/* subcommands */
int do_page_pool(int argc, char **argv);
int do_qstats(int argc, char **argv);

#endif /* __YNLTOOL_H */

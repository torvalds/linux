/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Author: Jakub Kicinski <kubakici@wp.pl> */

#ifndef __BPF_TOOL_H
#define __BPF_TOOL_H

#include <stdbool.h>
#include <stdio.h>
#include <linux/bpf.h>

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof(a[0]))

#define err(msg...)	fprintf(stderr, "Error: " msg)
#define warn(msg...)	fprintf(stderr, "Warning: " msg)
#define info(msg...)	fprintf(stderr, msg)

#define ptr_to_u64(ptr)	((__u64)(unsigned long)(ptr))

#define min(a, b)							\
	({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _b : _a; })
#define max(a, b)							\
	({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _b : _a; })

#define NEXT_ARG()	({ argc--; argv++; if (argc < 0) usage(); })
#define NEXT_ARGP()	({ (*argc)--; (*argv)++; if (*argc < 0) usage(); })
#define BAD_ARG()	({ err("what is '%s'?\n", *argv); -1; })

#define BPF_TAG_FMT	"%02hhx:%02hhx:%02hhx:%02hhx:"	\
			"%02hhx:%02hhx:%02hhx:%02hhx"

#define HELP_SPEC_PROGRAM						\
	"PROG := { id PROG_ID | pinned FILE | tag PROG_TAG }"

enum bpf_obj_type {
	BPF_OBJ_UNKNOWN,
	BPF_OBJ_PROG,
	BPF_OBJ_MAP,
};

extern const char *bin_name;

bool is_prefix(const char *pfx, const char *str);
void print_hex(void *arg, unsigned int n, const char *sep);
void usage(void) __attribute__((noreturn));

struct cmd {
	const char *cmd;
	int (*func)(int argc, char **argv);
};

int cmd_select(const struct cmd *cmds, int argc, char **argv,
	       int (*help)(int argc, char **argv));

int get_fd_type(int fd);
const char *get_fd_type_name(enum bpf_obj_type type);
char *get_fdinfo(int fd, const char *key);
int open_obj_pinned_any(char *path, enum bpf_obj_type exp_type);
int do_pin_any(int argc, char **argv, int (*get_fd_by_id)(__u32));

int do_prog(int argc, char **arg);
int do_map(int argc, char **arg);

int prog_parse_fd(int *argc, char ***argv);

void disasm_print_insn(unsigned char *image, ssize_t len, int opcodes);

#endif

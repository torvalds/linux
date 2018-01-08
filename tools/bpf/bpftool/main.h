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

/* BFD and kernel.h both define GCC_VERSION, differently */
#undef GCC_VERSION
#include <stdbool.h>
#include <stdio.h>
#include <linux/bpf.h>
#include <linux/kernel.h>
#include <linux/hashtable.h>

#include "json_writer.h"

#define ptr_to_u64(ptr)	((__u64)(unsigned long)(ptr))

#define NEXT_ARG()	({ argc--; argv++; if (argc < 0) usage(); })
#define NEXT_ARGP()	({ (*argc)--; (*argv)++; if (*argc < 0) usage(); })
#define BAD_ARG()	({ p_err("what is '%s'?\n", *argv); -1; })

#define ERR_MAX_LEN	1024

#define BPF_TAG_FMT	"%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"

#define HELP_SPEC_PROGRAM						\
	"PROG := { id PROG_ID | pinned FILE | tag PROG_TAG }"
#define HELP_SPEC_OPTIONS						\
	"OPTIONS := { {-j|--json} [{-p|--pretty}] | {-f|--bpffs} }"

enum bpf_obj_type {
	BPF_OBJ_UNKNOWN,
	BPF_OBJ_PROG,
	BPF_OBJ_MAP,
};

extern const char *bin_name;

extern json_writer_t *json_wtr;
extern bool json_output;
extern bool show_pinned;
extern struct pinned_obj_table prog_table;
extern struct pinned_obj_table map_table;

void p_err(const char *fmt, ...);
void p_info(const char *fmt, ...);

bool is_prefix(const char *pfx, const char *str);
void fprint_hex(FILE *f, void *arg, unsigned int n, const char *sep);
void usage(void) __attribute__((noreturn));

struct pinned_obj_table {
	DECLARE_HASHTABLE(table, 16);
};

struct pinned_obj {
	__u32 id;
	char *path;
	struct hlist_node hash;
};

int build_pinned_obj_table(struct pinned_obj_table *table,
			   enum bpf_obj_type type);
void delete_pinned_obj_table(struct pinned_obj_table *tab);

struct cmd {
	const char *cmd;
	int (*func)(int argc, char **argv);
};

int cmd_select(const struct cmd *cmds, int argc, char **argv,
	       int (*help)(int argc, char **argv));

int get_fd_type(int fd);
const char *get_fd_type_name(enum bpf_obj_type type);
char *get_fdinfo(int fd, const char *key);
int open_obj_pinned(char *path);
int open_obj_pinned_any(char *path, enum bpf_obj_type exp_type);
int do_pin_any(int argc, char **argv, int (*get_fd_by_id)(__u32));

int do_prog(int argc, char **arg);
int do_map(int argc, char **arg);

int prog_parse_fd(int *argc, char ***argv);

void disasm_print_insn(unsigned char *image, ssize_t len, int opcodes);
void print_hex_data_json(uint8_t *data, size_t len);

#endif

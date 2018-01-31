/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#ifndef __BPF_DISASM_H__
#define __BPF_DISASM_H__

#include <linux/bpf.h>
#include <linux/kernel.h>
#include <linux/stringify.h>

extern const char *const bpf_alu_string[16];
extern const char *const bpf_class_string[8];

const char *func_id_name(int id);

struct bpf_verifier_env;
typedef void (*bpf_insn_print_cb)(struct bpf_verifier_env *env,
				  const char *, ...);
void print_bpf_insn(bpf_insn_print_cb verbose, struct bpf_verifier_env *env,
		    const struct bpf_insn *insn, bool allow_ptr_leaks);

#endif

// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * Copyright (C) 2018 Netronome Systems, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __BPF_TOOL_XLATED_DUMPER_H
#define __BPF_TOOL_XLATED_DUMPER_H

#define SYM_MAX_NAME	256

struct kernel_sym {
	unsigned long address;
	char name[SYM_MAX_NAME];
};

struct dump_data {
	unsigned long address_call_base;
	struct kernel_sym *sym_mapping;
	__u32 sym_count;
	__u64 *jited_ksyms;
	__u32 nr_jited_ksyms;
	char scratch_buff[SYM_MAX_NAME + 8];
};

void kernel_syms_load(struct dump_data *dd);
void kernel_syms_destroy(struct dump_data *dd);
struct kernel_sym *kernel_syms_search(struct dump_data *dd, unsigned long key);
void dump_xlated_json(struct dump_data *dd, void *buf, unsigned int len,
		      bool opcodes);
void dump_xlated_plain(struct dump_data *dd, void *buf, unsigned int len,
		       bool opcodes);
void dump_xlated_for_graph(struct dump_data *dd, void *buf, void *buf_end,
			   unsigned int start_index);

#endif

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

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <bpf.h>

#include "main.h"
#include "disasm.h"

static const char * const prog_type_name[] = {
	[BPF_PROG_TYPE_UNSPEC]		= "unspec",
	[BPF_PROG_TYPE_SOCKET_FILTER]	= "socket_filter",
	[BPF_PROG_TYPE_KPROBE]		= "kprobe",
	[BPF_PROG_TYPE_SCHED_CLS]	= "sched_cls",
	[BPF_PROG_TYPE_SCHED_ACT]	= "sched_act",
	[BPF_PROG_TYPE_TRACEPOINT]	= "tracepoint",
	[BPF_PROG_TYPE_XDP]		= "xdp",
	[BPF_PROG_TYPE_PERF_EVENT]	= "perf_event",
	[BPF_PROG_TYPE_CGROUP_SKB]	= "cgroup_skb",
	[BPF_PROG_TYPE_CGROUP_SOCK]	= "cgroup_sock",
	[BPF_PROG_TYPE_LWT_IN]		= "lwt_in",
	[BPF_PROG_TYPE_LWT_OUT]		= "lwt_out",
	[BPF_PROG_TYPE_LWT_XMIT]	= "lwt_xmit",
	[BPF_PROG_TYPE_SOCK_OPS]	= "sock_ops",
	[BPF_PROG_TYPE_SK_SKB]		= "sk_skb",
};

static void print_boot_time(__u64 nsecs, char *buf, unsigned int size)
{
	struct timespec real_time_ts, boot_time_ts;
	time_t wallclock_secs;
	struct tm load_tm;

	buf[--size] = '\0';

	if (clock_gettime(CLOCK_REALTIME, &real_time_ts) ||
	    clock_gettime(CLOCK_BOOTTIME, &boot_time_ts)) {
		perror("Can't read clocks");
		snprintf(buf, size, "%llu", nsecs / 1000000000);
		return;
	}

	wallclock_secs = (real_time_ts.tv_sec - boot_time_ts.tv_sec) +
		nsecs / 1000000000;

	if (!localtime_r(&wallclock_secs, &load_tm)) {
		snprintf(buf, size, "%llu", nsecs / 1000000000);
		return;
	}

	strftime(buf, size, "%b %d/%H:%M", &load_tm);
}

static int prog_fd_by_tag(unsigned char *tag)
{
	struct bpf_prog_info info = {};
	__u32 len = sizeof(info);
	unsigned int id = 0;
	int err;
	int fd;

	while (true) {
		err = bpf_prog_get_next_id(id, &id);
		if (err) {
			p_err("%s", strerror(errno));
			return -1;
		}

		fd = bpf_prog_get_fd_by_id(id);
		if (fd < 0) {
			p_err("can't get prog by id (%u): %s",
			      id, strerror(errno));
			return -1;
		}

		err = bpf_obj_get_info_by_fd(fd, &info, &len);
		if (err) {
			p_err("can't get prog info (%u): %s",
			      id, strerror(errno));
			close(fd);
			return -1;
		}

		if (!memcmp(tag, info.tag, BPF_TAG_SIZE))
			return fd;

		close(fd);
	}
}

int prog_parse_fd(int *argc, char ***argv)
{
	int fd;

	if (is_prefix(**argv, "id")) {
		unsigned int id;
		char *endptr;

		NEXT_ARGP();

		id = strtoul(**argv, &endptr, 0);
		if (*endptr) {
			p_err("can't parse %s as ID", **argv);
			return -1;
		}
		NEXT_ARGP();

		fd = bpf_prog_get_fd_by_id(id);
		if (fd < 0)
			p_err("get by id (%u): %s", id, strerror(errno));
		return fd;
	} else if (is_prefix(**argv, "tag")) {
		unsigned char tag[BPF_TAG_SIZE];

		NEXT_ARGP();

		if (sscanf(**argv, BPF_TAG_FMT, tag, tag + 1, tag + 2,
			   tag + 3, tag + 4, tag + 5, tag + 6, tag + 7)
		    != BPF_TAG_SIZE) {
			p_err("can't parse tag");
			return -1;
		}
		NEXT_ARGP();

		return prog_fd_by_tag(tag);
	} else if (is_prefix(**argv, "pinned")) {
		char *path;

		NEXT_ARGP();

		path = **argv;
		NEXT_ARGP();

		return open_obj_pinned_any(path, BPF_OBJ_PROG);
	}

	p_err("expected 'id', 'tag' or 'pinned', got: '%s'?", **argv);
	return -1;
}

static void show_prog_maps(int fd, u32 num_maps)
{
	struct bpf_prog_info info = {};
	__u32 len = sizeof(info);
	__u32 map_ids[num_maps];
	unsigned int i;
	int err;

	info.nr_map_ids = num_maps;
	info.map_ids = ptr_to_u64(map_ids);

	err = bpf_obj_get_info_by_fd(fd, &info, &len);
	if (err || !info.nr_map_ids)
		return;

	if (json_output) {
		jsonw_name(json_wtr, "map_ids");
		jsonw_start_array(json_wtr);
		for (i = 0; i < info.nr_map_ids; i++)
			jsonw_uint(json_wtr, map_ids[i]);
		jsonw_end_array(json_wtr);
	} else {
		printf("  map_ids ");
		for (i = 0; i < info.nr_map_ids; i++)
			printf("%u%s", map_ids[i],
			       i == info.nr_map_ids - 1 ? "" : ",");
	}
}

static void print_prog_json(struct bpf_prog_info *info, int fd)
{
	char *memlock;

	jsonw_start_object(json_wtr);
	jsonw_uint_field(json_wtr, "id", info->id);
	if (info->type < ARRAY_SIZE(prog_type_name))
		jsonw_string_field(json_wtr, "type",
				   prog_type_name[info->type]);
	else
		jsonw_uint_field(json_wtr, "type", info->type);

	if (*info->name)
		jsonw_string_field(json_wtr, "name", info->name);

	jsonw_name(json_wtr, "tag");
	jsonw_printf(json_wtr, "\"" BPF_TAG_FMT "\"",
		     info->tag[0], info->tag[1], info->tag[2], info->tag[3],
		     info->tag[4], info->tag[5], info->tag[6], info->tag[7]);

	if (info->load_time) {
		char buf[32];

		print_boot_time(info->load_time, buf, sizeof(buf));

		/* Piggy back on load_time, since 0 uid is a valid one */
		jsonw_string_field(json_wtr, "loaded_at", buf);
		jsonw_uint_field(json_wtr, "uid", info->created_by_uid);
	}

	jsonw_uint_field(json_wtr, "bytes_xlated", info->xlated_prog_len);

	if (info->jited_prog_len) {
		jsonw_bool_field(json_wtr, "jited", true);
		jsonw_uint_field(json_wtr, "bytes_jited", info->jited_prog_len);
	} else {
		jsonw_bool_field(json_wtr, "jited", false);
	}

	memlock = get_fdinfo(fd, "memlock");
	if (memlock)
		jsonw_int_field(json_wtr, "bytes_memlock", atoi(memlock));
	free(memlock);

	if (info->nr_map_ids)
		show_prog_maps(fd, info->nr_map_ids);

	if (!hash_empty(prog_table.table)) {
		struct pinned_obj *obj;

		jsonw_name(json_wtr, "pinned");
		jsonw_start_array(json_wtr);
		hash_for_each_possible(prog_table.table, obj, hash, info->id) {
			if (obj->id == info->id)
				jsonw_string(json_wtr, obj->path);
		}
		jsonw_end_array(json_wtr);
	}

	jsonw_end_object(json_wtr);
}

static void print_prog_plain(struct bpf_prog_info *info, int fd)
{
	char *memlock;

	printf("%u: ", info->id);
	if (info->type < ARRAY_SIZE(prog_type_name))
		printf("%s  ", prog_type_name[info->type]);
	else
		printf("type %u  ", info->type);

	if (*info->name)
		printf("name %s  ", info->name);

	printf("tag ");
	fprint_hex(stdout, info->tag, BPF_TAG_SIZE, "");
	printf("\n");

	if (info->load_time) {
		char buf[32];

		print_boot_time(info->load_time, buf, sizeof(buf));

		/* Piggy back on load_time, since 0 uid is a valid one */
		printf("\tloaded_at %s  uid %u\n", buf, info->created_by_uid);
	}

	printf("\txlated %uB", info->xlated_prog_len);

	if (info->jited_prog_len)
		printf("  jited %uB", info->jited_prog_len);
	else
		printf("  not jited");

	memlock = get_fdinfo(fd, "memlock");
	if (memlock)
		printf("  memlock %sB", memlock);
	free(memlock);

	if (info->nr_map_ids)
		show_prog_maps(fd, info->nr_map_ids);

	if (!hash_empty(prog_table.table)) {
		struct pinned_obj *obj;

		printf("\n");
		hash_for_each_possible(prog_table.table, obj, hash, info->id) {
			if (obj->id == info->id)
				printf("\tpinned %s\n", obj->path);
		}
	}

	printf("\n");
}

static int show_prog(int fd)
{
	struct bpf_prog_info info = {};
	__u32 len = sizeof(info);
	int err;

	err = bpf_obj_get_info_by_fd(fd, &info, &len);
	if (err) {
		p_err("can't get prog info: %s", strerror(errno));
		return -1;
	}

	if (json_output)
		print_prog_json(&info, fd);
	else
		print_prog_plain(&info, fd);

	return 0;
}

static int do_show(int argc, char **argv)
{
	__u32 id = 0;
	int err;
	int fd;

	if (show_pinned)
		build_pinned_obj_table(&prog_table, BPF_OBJ_PROG);

	if (argc == 2) {
		fd = prog_parse_fd(&argc, &argv);
		if (fd < 0)
			return -1;

		return show_prog(fd);
	}

	if (argc)
		return BAD_ARG();

	if (json_output)
		jsonw_start_array(json_wtr);
	while (true) {
		err = bpf_prog_get_next_id(id, &id);
		if (err) {
			if (errno == ENOENT) {
				err = 0;
				break;
			}
			p_err("can't get next program: %s%s", strerror(errno),
			      errno == EINVAL ? " -- kernel too old?" : "");
			err = -1;
			break;
		}

		fd = bpf_prog_get_fd_by_id(id);
		if (fd < 0) {
			p_err("can't get prog by id (%u): %s",
			      id, strerror(errno));
			err = -1;
			break;
		}

		err = show_prog(fd);
		close(fd);
		if (err)
			break;
	}

	if (json_output)
		jsonw_end_array(json_wtr);

	return err;
}

static void print_insn(struct bpf_verifier_env *env, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

static void dump_xlated_plain(void *buf, unsigned int len, bool opcodes)
{
	struct bpf_insn *insn = buf;
	bool double_insn = false;
	unsigned int i;

	for (i = 0; i < len / sizeof(*insn); i++) {
		if (double_insn) {
			double_insn = false;
			continue;
		}

		double_insn = insn[i].code == (BPF_LD | BPF_IMM | BPF_DW);

		printf("% 4d: ", i);
		print_bpf_insn(print_insn, NULL, insn + i, true);

		if (opcodes) {
			printf("       ");
			fprint_hex(stdout, insn + i, 8, " ");
			if (double_insn && i < len - 1) {
				printf(" ");
				fprint_hex(stdout, insn + i + 1, 8, " ");
			}
			printf("\n");
		}
	}
}

static void print_insn_json(struct bpf_verifier_env *env, const char *fmt, ...)
{
	unsigned int l = strlen(fmt);
	char chomped_fmt[l];
	va_list args;

	va_start(args, fmt);
	if (l > 0) {
		strncpy(chomped_fmt, fmt, l - 1);
		chomped_fmt[l - 1] = '\0';
	}
	jsonw_vprintf_enquote(json_wtr, chomped_fmt, args);
	va_end(args);
}

static void dump_xlated_json(void *buf, unsigned int len, bool opcodes)
{
	struct bpf_insn *insn = buf;
	bool double_insn = false;
	unsigned int i;

	jsonw_start_array(json_wtr);
	for (i = 0; i < len / sizeof(*insn); i++) {
		if (double_insn) {
			double_insn = false;
			continue;
		}
		double_insn = insn[i].code == (BPF_LD | BPF_IMM | BPF_DW);

		jsonw_start_object(json_wtr);
		jsonw_name(json_wtr, "disasm");
		print_bpf_insn(print_insn_json, NULL, insn + i, true);

		if (opcodes) {
			jsonw_name(json_wtr, "opcodes");
			jsonw_start_object(json_wtr);

			jsonw_name(json_wtr, "code");
			jsonw_printf(json_wtr, "\"0x%02hhx\"", insn[i].code);

			jsonw_name(json_wtr, "src_reg");
			jsonw_printf(json_wtr, "\"0x%hhx\"", insn[i].src_reg);

			jsonw_name(json_wtr, "dst_reg");
			jsonw_printf(json_wtr, "\"0x%hhx\"", insn[i].dst_reg);

			jsonw_name(json_wtr, "off");
			print_hex_data_json((uint8_t *)(&insn[i].off), 2);

			jsonw_name(json_wtr, "imm");
			if (double_insn && i < len - 1)
				print_hex_data_json((uint8_t *)(&insn[i].imm),
						    12);
			else
				print_hex_data_json((uint8_t *)(&insn[i].imm),
						    4);
			jsonw_end_object(json_wtr);
		}
		jsonw_end_object(json_wtr);
	}
	jsonw_end_array(json_wtr);
}

static int do_dump(int argc, char **argv)
{
	struct bpf_prog_info info = {};
	__u32 len = sizeof(info);
	unsigned int buf_size;
	char *filepath = NULL;
	bool opcodes = false;
	unsigned char *buf;
	__u32 *member_len;
	__u64 *member_ptr;
	ssize_t n;
	int err;
	int fd;

	if (is_prefix(*argv, "jited")) {
		member_len = &info.jited_prog_len;
		member_ptr = &info.jited_prog_insns;
	} else if (is_prefix(*argv, "xlated")) {
		member_len = &info.xlated_prog_len;
		member_ptr = &info.xlated_prog_insns;
	} else {
		p_err("expected 'xlated' or 'jited', got: %s", *argv);
		return -1;
	}
	NEXT_ARG();

	if (argc < 2)
		usage();

	fd = prog_parse_fd(&argc, &argv);
	if (fd < 0)
		return -1;

	if (is_prefix(*argv, "file")) {
		NEXT_ARG();
		if (!argc) {
			p_err("expected file path");
			return -1;
		}

		filepath = *argv;
		NEXT_ARG();
	} else if (is_prefix(*argv, "opcodes")) {
		opcodes = true;
		NEXT_ARG();
	}

	if (argc) {
		usage();
		return -1;
	}

	err = bpf_obj_get_info_by_fd(fd, &info, &len);
	if (err) {
		p_err("can't get prog info: %s", strerror(errno));
		return -1;
	}

	if (!*member_len) {
		p_info("no instructions returned");
		close(fd);
		return 0;
	}

	buf_size = *member_len;

	buf = malloc(buf_size);
	if (!buf) {
		p_err("mem alloc failed");
		close(fd);
		return -1;
	}

	memset(&info, 0, sizeof(info));

	*member_ptr = ptr_to_u64(buf);
	*member_len = buf_size;

	err = bpf_obj_get_info_by_fd(fd, &info, &len);
	close(fd);
	if (err) {
		p_err("can't get prog info: %s", strerror(errno));
		goto err_free;
	}

	if (*member_len > buf_size) {
		p_err("too many instructions returned");
		goto err_free;
	}

	if (filepath) {
		fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0) {
			p_err("can't open file %s: %s", filepath,
			      strerror(errno));
			goto err_free;
		}

		n = write(fd, buf, *member_len);
		close(fd);
		if (n != *member_len) {
			p_err("error writing output file: %s",
			      n < 0 ? strerror(errno) : "short write");
			goto err_free;
		}
	} else {
		if (member_len == &info.jited_prog_len)
			disasm_print_insn(buf, *member_len, opcodes);
		else
			if (json_output)
				dump_xlated_json(buf, *member_len, opcodes);
			else
				dump_xlated_plain(buf, *member_len, opcodes);
	}

	free(buf);

	return 0;

err_free:
	free(buf);
	return -1;
}

static int do_pin(int argc, char **argv)
{
	int err;

	err = do_pin_any(argc, argv, bpf_prog_get_fd_by_id);
	if (!err && json_output)
		jsonw_null(json_wtr);
	return err;
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s %s show [PROG]\n"
		"       %s %s dump xlated PROG [{ file FILE | opcodes }]\n"
		"       %s %s dump jited  PROG [{ file FILE | opcodes }]\n"
		"       %s %s pin   PROG FILE\n"
		"       %s %s help\n"
		"\n"
		"       " HELP_SPEC_PROGRAM "\n"
		"       " HELP_SPEC_OPTIONS "\n"
		"",
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2], bin_name, argv[-2]);

	return 0;
}

static const struct cmd cmds[] = {
	{ "show",	do_show },
	{ "help",	do_help },
	{ "dump",	do_dump },
	{ "pin",	do_pin },
	{ 0 }
};

int do_prog(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}

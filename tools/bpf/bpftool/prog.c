/*
 * Copyright (C) 2017-2018 Netronome Systems, Inc.
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

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/err.h>

#include <bpf.h>
#include <libbpf.h>

#include "cfg.h"
#include "main.h"
#include "xlated_dumper.h"

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
	[BPF_PROG_TYPE_CGROUP_DEVICE]	= "cgroup_device",
	[BPF_PROG_TYPE_SK_MSG]		= "sk_msg",
	[BPF_PROG_TYPE_RAW_TRACEPOINT]	= "raw_tracepoint",
	[BPF_PROG_TYPE_CGROUP_SOCK_ADDR] = "cgroup_sock_addr",
	[BPF_PROG_TYPE_LIRC_MODE2]	= "lirc_mode2",
	[BPF_PROG_TYPE_FLOW_DISSECTOR]	= "flow_dissector",
};

static const char * const attach_type_strings[] = {
	[BPF_SK_SKB_STREAM_PARSER] = "stream_parser",
	[BPF_SK_SKB_STREAM_VERDICT] = "stream_verdict",
	[BPF_SK_MSG_VERDICT] = "msg_verdict",
	[__MAX_BPF_ATTACH_TYPE] = NULL,
};

enum bpf_attach_type parse_attach_type(const char *str)
{
	enum bpf_attach_type type;

	for (type = 0; type < __MAX_BPF_ATTACH_TYPE; type++) {
		if (attach_type_strings[type] &&
		    is_prefix(str, attach_type_strings[type]))
			return type;
	}

	return __MAX_BPF_ATTACH_TYPE;
}

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
		(real_time_ts.tv_nsec - boot_time_ts.tv_nsec + nsecs) /
		1000000000;


	if (!localtime_r(&wallclock_secs, &load_tm)) {
		snprintf(buf, size, "%llu", nsecs / 1000000000);
		return;
	}

	if (json_output)
		strftime(buf, size, "%s", &load_tm);
	else
		strftime(buf, size, "%FT%T%z", &load_tm);
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

	jsonw_bool_field(json_wtr, "gpl_compatible", info->gpl_compatible);

	print_dev_json(info->ifindex, info->netns_dev, info->netns_ino);

	if (info->load_time) {
		char buf[32];

		print_boot_time(info->load_time, buf, sizeof(buf));

		/* Piggy back on load_time, since 0 uid is a valid one */
		jsonw_name(json_wtr, "loaded_at");
		jsonw_printf(json_wtr, "%s", buf);
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
	print_dev_plain(info->ifindex, info->netns_dev, info->netns_ino);
	printf("%s", info->gpl_compatible ? "  gpl" : "");
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
			if (errno == ENOENT)
				continue;
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

static int do_dump(int argc, char **argv)
{
	unsigned long *func_ksyms = NULL;
	struct bpf_prog_info info = {};
	unsigned int *func_lens = NULL;
	const char *disasm_opt = NULL;
	unsigned int nr_func_ksyms;
	unsigned int nr_func_lens;
	struct dump_data dd = {};
	__u32 len = sizeof(info);
	unsigned int buf_size;
	char *filepath = NULL;
	bool opcodes = false;
	bool visual = false;
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
	} else if (is_prefix(*argv, "visual")) {
		visual = true;
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

	nr_func_ksyms = info.nr_jited_ksyms;
	if (nr_func_ksyms) {
		func_ksyms = malloc(nr_func_ksyms * sizeof(__u64));
		if (!func_ksyms) {
			p_err("mem alloc failed");
			close(fd);
			goto err_free;
		}
	}

	nr_func_lens = info.nr_jited_func_lens;
	if (nr_func_lens) {
		func_lens = malloc(nr_func_lens * sizeof(__u32));
		if (!func_lens) {
			p_err("mem alloc failed");
			close(fd);
			goto err_free;
		}
	}

	memset(&info, 0, sizeof(info));

	*member_ptr = ptr_to_u64(buf);
	*member_len = buf_size;
	info.jited_ksyms = ptr_to_u64(func_ksyms);
	info.nr_jited_ksyms = nr_func_ksyms;
	info.jited_func_lens = ptr_to_u64(func_lens);
	info.nr_jited_func_lens = nr_func_lens;

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

	if (info.nr_jited_ksyms > nr_func_ksyms) {
		p_err("too many addresses returned");
		goto err_free;
	}

	if (info.nr_jited_func_lens > nr_func_lens) {
		p_err("too many values returned");
		goto err_free;
	}

	if ((member_len == &info.jited_prog_len &&
	     info.jited_prog_insns == 0) ||
	    (member_len == &info.xlated_prog_len &&
	     info.xlated_prog_insns == 0)) {
		p_err("error retrieving insn dump: kernel.kptr_restrict set?");
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

		if (json_output)
			jsonw_null(json_wtr);
	} else if (member_len == &info.jited_prog_len) {
		const char *name = NULL;

		if (info.ifindex) {
			name = ifindex_to_bfd_params(info.ifindex,
						     info.netns_dev,
						     info.netns_ino,
						     &disasm_opt);
			if (!name)
				goto err_free;
		}

		if (info.nr_jited_func_lens && info.jited_func_lens) {
			struct kernel_sym *sym = NULL;
			char sym_name[SYM_MAX_NAME];
			unsigned char *img = buf;
			__u64 *ksyms = NULL;
			__u32 *lens;
			__u32 i;

			if (info.nr_jited_ksyms) {
				kernel_syms_load(&dd);
				ksyms = (__u64 *) info.jited_ksyms;
			}

			if (json_output)
				jsonw_start_array(json_wtr);

			lens = (__u32 *) info.jited_func_lens;
			for (i = 0; i < info.nr_jited_func_lens; i++) {
				if (ksyms) {
					sym = kernel_syms_search(&dd, ksyms[i]);
					if (sym)
						sprintf(sym_name, "%s", sym->name);
					else
						sprintf(sym_name, "0x%016llx", ksyms[i]);
				} else {
					strcpy(sym_name, "unknown");
				}

				if (json_output) {
					jsonw_start_object(json_wtr);
					jsonw_name(json_wtr, "name");
					jsonw_string(json_wtr, sym_name);
					jsonw_name(json_wtr, "insns");
				} else {
					printf("%s:\n", sym_name);
				}

				disasm_print_insn(img, lens[i], opcodes, name,
						  disasm_opt);
				img += lens[i];

				if (json_output)
					jsonw_end_object(json_wtr);
				else
					printf("\n");
			}

			if (json_output)
				jsonw_end_array(json_wtr);
		} else {
			disasm_print_insn(buf, *member_len, opcodes, name,
					  disasm_opt);
		}
	} else if (visual) {
		if (json_output)
			jsonw_null(json_wtr);
		else
			dump_xlated_cfg(buf, *member_len);
	} else {
		kernel_syms_load(&dd);
		dd.nr_jited_ksyms = info.nr_jited_ksyms;
		dd.jited_ksyms = (__u64 *) info.jited_ksyms;

		if (json_output)
			dump_xlated_json(&dd, buf, *member_len, opcodes);
		else
			dump_xlated_plain(&dd, buf, *member_len, opcodes);
		kernel_syms_destroy(&dd);
	}

	free(buf);
	free(func_ksyms);
	free(func_lens);
	return 0;

err_free:
	free(buf);
	free(func_ksyms);
	free(func_lens);
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

struct map_replace {
	int idx;
	int fd;
	char *name;
};

int map_replace_compar(const void *p1, const void *p2)
{
	const struct map_replace *a = p1, *b = p2;

	return a->idx - b->idx;
}

static int do_attach(int argc, char **argv)
{
	enum bpf_attach_type attach_type;
	int err, mapfd, progfd;

	if (!REQ_ARGS(5)) {
		p_err("too few parameters for map attach");
		return -EINVAL;
	}

	progfd = prog_parse_fd(&argc, &argv);
	if (progfd < 0)
		return progfd;

	attach_type = parse_attach_type(*argv);
	if (attach_type == __MAX_BPF_ATTACH_TYPE) {
		p_err("invalid attach type");
		return -EINVAL;
	}
	NEXT_ARG();

	mapfd = map_parse_fd(&argc, &argv);
	if (mapfd < 0)
		return mapfd;

	err = bpf_prog_attach(progfd, mapfd, attach_type, 0);
	if (err) {
		p_err("failed prog attach to map");
		return -EINVAL;
	}

	if (json_output)
		jsonw_null(json_wtr);
	return 0;
}

static int do_detach(int argc, char **argv)
{
	enum bpf_attach_type attach_type;
	int err, mapfd, progfd;

	if (!REQ_ARGS(5)) {
		p_err("too few parameters for map detach");
		return -EINVAL;
	}

	progfd = prog_parse_fd(&argc, &argv);
	if (progfd < 0)
		return progfd;

	attach_type = parse_attach_type(*argv);
	if (attach_type == __MAX_BPF_ATTACH_TYPE) {
		p_err("invalid attach type");
		return -EINVAL;
	}
	NEXT_ARG();

	mapfd = map_parse_fd(&argc, &argv);
	if (mapfd < 0)
		return mapfd;

	err = bpf_prog_detach2(progfd, mapfd, attach_type);
	if (err) {
		p_err("failed prog detach from map");
		return -EINVAL;
	}

	if (json_output)
		jsonw_null(json_wtr);
	return 0;
}
static int do_load(int argc, char **argv)
{
	enum bpf_attach_type expected_attach_type;
	struct bpf_object_open_attr attr = {
		.prog_type	= BPF_PROG_TYPE_UNSPEC,
	};
	struct map_replace *map_replace = NULL;
	unsigned int old_map_fds = 0;
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct bpf_map *map;
	const char *pinfile;
	unsigned int i, j;
	__u32 ifindex = 0;
	int idx, err;

	if (!REQ_ARGS(2))
		return -1;
	attr.file = GET_ARG();
	pinfile = GET_ARG();

	while (argc) {
		if (is_prefix(*argv, "type")) {
			char *type;

			NEXT_ARG();

			if (attr.prog_type != BPF_PROG_TYPE_UNSPEC) {
				p_err("program type already specified");
				goto err_free_reuse_maps;
			}
			if (!REQ_ARGS(1))
				goto err_free_reuse_maps;

			/* Put a '/' at the end of type to appease libbpf */
			type = malloc(strlen(*argv) + 2);
			if (!type) {
				p_err("mem alloc failed");
				goto err_free_reuse_maps;
			}
			*type = 0;
			strcat(type, *argv);
			strcat(type, "/");

			err = libbpf_prog_type_by_name(type, &attr.prog_type,
						       &expected_attach_type);
			free(type);
			if (err < 0) {
				p_err("unknown program type '%s'", *argv);
				goto err_free_reuse_maps;
			}
			NEXT_ARG();
		} else if (is_prefix(*argv, "map")) {
			char *endptr, *name;
			int fd;

			NEXT_ARG();

			if (!REQ_ARGS(4))
				goto err_free_reuse_maps;

			if (is_prefix(*argv, "idx")) {
				NEXT_ARG();

				idx = strtoul(*argv, &endptr, 0);
				if (*endptr) {
					p_err("can't parse %s as IDX", *argv);
					goto err_free_reuse_maps;
				}
				name = NULL;
			} else if (is_prefix(*argv, "name")) {
				NEXT_ARG();

				name = *argv;
				idx = -1;
			} else {
				p_err("expected 'idx' or 'name', got: '%s'?",
				      *argv);
				goto err_free_reuse_maps;
			}
			NEXT_ARG();

			fd = map_parse_fd(&argc, &argv);
			if (fd < 0)
				goto err_free_reuse_maps;

			map_replace = reallocarray(map_replace, old_map_fds + 1,
						   sizeof(*map_replace));
			if (!map_replace) {
				p_err("mem alloc failed");
				goto err_free_reuse_maps;
			}
			map_replace[old_map_fds].idx = idx;
			map_replace[old_map_fds].name = name;
			map_replace[old_map_fds].fd = fd;
			old_map_fds++;
		} else if (is_prefix(*argv, "dev")) {
			NEXT_ARG();

			if (ifindex) {
				p_err("offload device already specified");
				goto err_free_reuse_maps;
			}
			if (!REQ_ARGS(1))
				goto err_free_reuse_maps;

			ifindex = if_nametoindex(*argv);
			if (!ifindex) {
				p_err("unrecognized netdevice '%s': %s",
				      *argv, strerror(errno));
				goto err_free_reuse_maps;
			}
			NEXT_ARG();
		} else {
			p_err("expected no more arguments, 'type', 'map' or 'dev', got: '%s'?",
			      *argv);
			goto err_free_reuse_maps;
		}
	}

	obj = __bpf_object__open_xattr(&attr, bpf_flags);
	if (IS_ERR_OR_NULL(obj)) {
		p_err("failed to open object file");
		goto err_free_reuse_maps;
	}

	prog = bpf_program__next(NULL, obj);
	if (!prog) {
		p_err("object file doesn't contain any bpf program");
		goto err_close_obj;
	}

	bpf_program__set_ifindex(prog, ifindex);
	if (attr.prog_type == BPF_PROG_TYPE_UNSPEC) {
		const char *sec_name = bpf_program__title(prog, false);

		err = libbpf_prog_type_by_name(sec_name, &attr.prog_type,
					       &expected_attach_type);
		if (err < 0) {
			p_err("failed to guess program type based on section name %s\n",
			      sec_name);
			goto err_close_obj;
		}
	}
	bpf_program__set_type(prog, attr.prog_type);
	bpf_program__set_expected_attach_type(prog, expected_attach_type);

	qsort(map_replace, old_map_fds, sizeof(*map_replace),
	      map_replace_compar);

	/* After the sort maps by name will be first on the list, because they
	 * have idx == -1.  Resolve them.
	 */
	j = 0;
	while (j < old_map_fds && map_replace[j].name) {
		i = 0;
		bpf_map__for_each(map, obj) {
			if (!strcmp(bpf_map__name(map), map_replace[j].name)) {
				map_replace[j].idx = i;
				break;
			}
			i++;
		}
		if (map_replace[j].idx == -1) {
			p_err("unable to find map '%s'", map_replace[j].name);
			goto err_close_obj;
		}
		j++;
	}
	/* Resort if any names were resolved */
	if (j)
		qsort(map_replace, old_map_fds, sizeof(*map_replace),
		      map_replace_compar);

	/* Set ifindex and name reuse */
	j = 0;
	idx = 0;
	bpf_map__for_each(map, obj) {
		if (!bpf_map__is_offload_neutral(map))
			bpf_map__set_ifindex(map, ifindex);

		if (j < old_map_fds && idx == map_replace[j].idx) {
			err = bpf_map__reuse_fd(map, map_replace[j++].fd);
			if (err) {
				p_err("unable to set up map reuse: %d", err);
				goto err_close_obj;
			}

			/* Next reuse wants to apply to the same map */
			if (j < old_map_fds && map_replace[j].idx == idx) {
				p_err("replacement for map idx %d specified more than once",
				      idx);
				goto err_close_obj;
			}
		}

		idx++;
	}
	if (j < old_map_fds) {
		p_err("map idx '%d' not used", map_replace[j].idx);
		goto err_close_obj;
	}

	set_max_rlimit();

	err = bpf_object__load(obj);
	if (err) {
		p_err("failed to load object file");
		goto err_close_obj;
	}

	if (do_pin_fd(bpf_program__fd(prog), pinfile))
		goto err_close_obj;

	if (json_output)
		jsonw_null(json_wtr);

	bpf_object__close(obj);
	for (i = 0; i < old_map_fds; i++)
		close(map_replace[i].fd);
	free(map_replace);

	return 0;

err_close_obj:
	bpf_object__close(obj);
err_free_reuse_maps:
	for (i = 0; i < old_map_fds; i++)
		close(map_replace[i].fd);
	free(map_replace);
	return -1;
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s %s { show | list } [PROG]\n"
		"       %s %s dump xlated PROG [{ file FILE | opcodes | visual }]\n"
		"       %s %s dump jited  PROG [{ file FILE | opcodes }]\n"
		"       %s %s pin   PROG FILE\n"
		"       %s %s load  OBJ  FILE [type TYPE] [dev NAME] \\\n"
		"                         [map { idx IDX | name NAME } MAP]\n"
		"       %s %s attach PROG ATTACH_TYPE MAP\n"
		"       %s %s detach PROG ATTACH_TYPE MAP\n"
		"       %s %s help\n"
		"\n"
		"       " HELP_SPEC_MAP "\n"
		"       " HELP_SPEC_PROGRAM "\n"
		"       TYPE := { socket | kprobe | kretprobe | classifier | action |\n"
		"                 tracepoint | raw_tracepoint | xdp | perf_event | cgroup/skb |\n"
		"                 cgroup/sock | cgroup/dev | lwt_in | lwt_out | lwt_xmit |\n"
		"                 lwt_seg6local | sockops | sk_skb | sk_msg | lirc_mode2 |\n"
		"                 cgroup/bind4 | cgroup/bind6 | cgroup/post_bind4 |\n"
		"                 cgroup/post_bind6 | cgroup/connect4 | cgroup/connect6 |\n"
		"                 cgroup/sendmsg4 | cgroup/sendmsg6 }\n"
		"       ATTACH_TYPE := { msg_verdict | skb_verdict | skb_parse }\n"
		"       " HELP_SPEC_OPTIONS "\n"
		"",
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2], bin_name, argv[-2]);

	return 0;
}

static const struct cmd cmds[] = {
	{ "show",	do_show },
	{ "list",	do_show },
	{ "help",	do_help },
	{ "dump",	do_dump },
	{ "pin",	do_pin },
	{ "load",	do_load },
	{ "attach",	do_attach },
	{ "detach",	do_detach },
	{ 0 }
};

int do_prog(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}

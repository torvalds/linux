// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

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
#include <btf.h>
#include <libbpf.h>

#include "cfg.h"
#include "main.h"
#include "xlated_dumper.h"

static const char * const attach_type_strings[] = {
	[BPF_SK_SKB_STREAM_PARSER] = "stream_parser",
	[BPF_SK_SKB_STREAM_VERDICT] = "stream_verdict",
	[BPF_SK_MSG_VERDICT] = "msg_verdict",
	[BPF_FLOW_DISSECTOR] = "flow_dissector",
	[__MAX_BPF_ATTACH_TYPE] = NULL,
};

static enum bpf_attach_type parse_attach_type(const char *str)
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
	unsigned int id = 0;
	int err;
	int fd;

	while (true) {
		struct bpf_prog_info info = {};
		__u32 len = sizeof(info);

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
	if (info->run_time_ns) {
		jsonw_uint_field(json_wtr, "run_time_ns", info->run_time_ns);
		jsonw_uint_field(json_wtr, "run_cnt", info->run_cnt);
	}

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

	if (info->btf_id)
		jsonw_int_field(json_wtr, "btf_id", info->btf_id);

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
	if (info->run_time_ns)
		printf(" run_time_ns %lld run_cnt %lld",
		       info->run_time_ns, info->run_cnt);
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

		hash_for_each_possible(prog_table.table, obj, hash, info->id) {
			if (obj->id == info->id)
				printf("\n\tpinned %s", obj->path);
		}
	}

	if (info->btf_id)
		printf("\n\tbtf_id %d", info->btf_id);

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
	struct bpf_prog_info_linear *info_linear;
	struct bpf_prog_linfo *prog_linfo = NULL;
	enum {DUMP_JITED, DUMP_XLATED} mode;
	const char *disasm_opt = NULL;
	struct bpf_prog_info *info;
	struct dump_data dd = {};
	void *func_info = NULL;
	struct btf *btf = NULL;
	char *filepath = NULL;
	bool opcodes = false;
	bool visual = false;
	char func_sig[1024];
	unsigned char *buf;
	bool linum = false;
	__u32 member_len;
	__u64 arrays;
	ssize_t n;
	int fd;

	if (is_prefix(*argv, "jited")) {
		if (disasm_init())
			return -1;
		mode = DUMP_JITED;
	} else if (is_prefix(*argv, "xlated")) {
		mode = DUMP_XLATED;
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
	} else if (is_prefix(*argv, "linum")) {
		linum = true;
		NEXT_ARG();
	}

	if (argc) {
		usage();
		return -1;
	}

	if (mode == DUMP_JITED)
		arrays = 1UL << BPF_PROG_INFO_JITED_INSNS;
	else
		arrays = 1UL << BPF_PROG_INFO_XLATED_INSNS;

	arrays |= 1UL << BPF_PROG_INFO_JITED_KSYMS;
	arrays |= 1UL << BPF_PROG_INFO_JITED_FUNC_LENS;
	arrays |= 1UL << BPF_PROG_INFO_FUNC_INFO;
	arrays |= 1UL << BPF_PROG_INFO_LINE_INFO;
	arrays |= 1UL << BPF_PROG_INFO_JITED_LINE_INFO;

	info_linear = bpf_program__get_prog_info_linear(fd, arrays);
	close(fd);
	if (IS_ERR_OR_NULL(info_linear)) {
		p_err("can't get prog info: %s", strerror(errno));
		return -1;
	}

	info = &info_linear->info;
	if (mode == DUMP_JITED) {
		if (info->jited_prog_len == 0) {
			p_info("no instructions returned");
			goto err_free;
		}
		buf = (unsigned char *)(info->jited_prog_insns);
		member_len = info->jited_prog_len;
	} else {	/* DUMP_XLATED */
		if (info->xlated_prog_len == 0) {
			p_err("error retrieving insn dump: kernel.kptr_restrict set?");
			goto err_free;
		}
		buf = (unsigned char *)info->xlated_prog_insns;
		member_len = info->xlated_prog_len;
	}

	if (info->btf_id && btf__get_from_id(info->btf_id, &btf)) {
		p_err("failed to get btf");
		goto err_free;
	}

	func_info = (void *)info->func_info;

	if (info->nr_line_info) {
		prog_linfo = bpf_prog_linfo__new(info);
		if (!prog_linfo)
			p_info("error in processing bpf_line_info.  continue without it.");
	}

	if (filepath) {
		fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0) {
			p_err("can't open file %s: %s", filepath,
			      strerror(errno));
			goto err_free;
		}

		n = write(fd, buf, member_len);
		close(fd);
		if (n != member_len) {
			p_err("error writing output file: %s",
			      n < 0 ? strerror(errno) : "short write");
			goto err_free;
		}

		if (json_output)
			jsonw_null(json_wtr);
	} else if (mode == DUMP_JITED) {
		const char *name = NULL;

		if (info->ifindex) {
			name = ifindex_to_bfd_params(info->ifindex,
						     info->netns_dev,
						     info->netns_ino,
						     &disasm_opt);
			if (!name)
				goto err_free;
		}

		if (info->nr_jited_func_lens && info->jited_func_lens) {
			struct kernel_sym *sym = NULL;
			struct bpf_func_info *record;
			char sym_name[SYM_MAX_NAME];
			unsigned char *img = buf;
			__u64 *ksyms = NULL;
			__u32 *lens;
			__u32 i;
			if (info->nr_jited_ksyms) {
				kernel_syms_load(&dd);
				ksyms = (__u64 *) info->jited_ksyms;
			}

			if (json_output)
				jsonw_start_array(json_wtr);

			lens = (__u32 *) info->jited_func_lens;
			for (i = 0; i < info->nr_jited_func_lens; i++) {
				if (ksyms) {
					sym = kernel_syms_search(&dd, ksyms[i]);
					if (sym)
						sprintf(sym_name, "%s", sym->name);
					else
						sprintf(sym_name, "0x%016llx", ksyms[i]);
				} else {
					strcpy(sym_name, "unknown");
				}

				if (func_info) {
					record = func_info + i * info->func_info_rec_size;
					btf_dumper_type_only(btf, record->type_id,
							     func_sig,
							     sizeof(func_sig));
				}

				if (json_output) {
					jsonw_start_object(json_wtr);
					if (func_info && func_sig[0] != '\0') {
						jsonw_name(json_wtr, "proto");
						jsonw_string(json_wtr, func_sig);
					}
					jsonw_name(json_wtr, "name");
					jsonw_string(json_wtr, sym_name);
					jsonw_name(json_wtr, "insns");
				} else {
					if (func_info && func_sig[0] != '\0')
						printf("%s:\n", func_sig);
					printf("%s:\n", sym_name);
				}

				disasm_print_insn(img, lens[i], opcodes,
						  name, disasm_opt, btf,
						  prog_linfo, ksyms[i], i,
						  linum);

				img += lens[i];

				if (json_output)
					jsonw_end_object(json_wtr);
				else
					printf("\n");
			}

			if (json_output)
				jsonw_end_array(json_wtr);
		} else {
			disasm_print_insn(buf, member_len, opcodes, name,
					  disasm_opt, btf, NULL, 0, 0, false);
		}
	} else if (visual) {
		if (json_output)
			jsonw_null(json_wtr);
		else
			dump_xlated_cfg(buf, member_len);
	} else {
		kernel_syms_load(&dd);
		dd.nr_jited_ksyms = info->nr_jited_ksyms;
		dd.jited_ksyms = (__u64 *) info->jited_ksyms;
		dd.btf = btf;
		dd.func_info = func_info;
		dd.finfo_rec_size = info->func_info_rec_size;
		dd.prog_linfo = prog_linfo;

		if (json_output)
			dump_xlated_json(&dd, buf, member_len, opcodes,
					 linum);
		else
			dump_xlated_plain(&dd, buf, member_len, opcodes,
					  linum);
		kernel_syms_destroy(&dd);
	}

	free(info_linear);
	return 0;

err_free:
	free(info_linear);
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

static int map_replace_compar(const void *p1, const void *p2)
{
	const struct map_replace *a = p1, *b = p2;

	return a->idx - b->idx;
}

static int parse_attach_detach_args(int argc, char **argv, int *progfd,
				    enum bpf_attach_type *attach_type,
				    int *mapfd)
{
	if (!REQ_ARGS(3))
		return -EINVAL;

	*progfd = prog_parse_fd(&argc, &argv);
	if (*progfd < 0)
		return *progfd;

	*attach_type = parse_attach_type(*argv);
	if (*attach_type == __MAX_BPF_ATTACH_TYPE) {
		p_err("invalid attach/detach type");
		return -EINVAL;
	}

	if (*attach_type == BPF_FLOW_DISSECTOR) {
		*mapfd = -1;
		return 0;
	}

	NEXT_ARG();
	if (!REQ_ARGS(2))
		return -EINVAL;

	*mapfd = map_parse_fd(&argc, &argv);
	if (*mapfd < 0)
		return *mapfd;

	return 0;
}

static int do_attach(int argc, char **argv)
{
	enum bpf_attach_type attach_type;
	int err, progfd;
	int mapfd;

	err = parse_attach_detach_args(argc, argv,
				       &progfd, &attach_type, &mapfd);
	if (err)
		return err;

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
	int err, progfd;
	int mapfd;

	err = parse_attach_detach_args(argc, argv,
				       &progfd, &attach_type, &mapfd);
	if (err)
		return err;

	err = bpf_prog_detach2(progfd, mapfd, attach_type);
	if (err) {
		p_err("failed prog detach from map");
		return -EINVAL;
	}

	if (json_output)
		jsonw_null(json_wtr);
	return 0;
}

static int load_with_options(int argc, char **argv, bool first_prog_only)
{
	struct bpf_object_load_attr load_attr = { 0 };
	struct bpf_object_open_attr open_attr = {
		.prog_type = BPF_PROG_TYPE_UNSPEC,
	};
	enum bpf_attach_type expected_attach_type;
	struct map_replace *map_replace = NULL;
	struct bpf_program *prog = NULL, *pos;
	unsigned int old_map_fds = 0;
	const char *pinmaps = NULL;
	struct bpf_object *obj;
	struct bpf_map *map;
	const char *pinfile;
	unsigned int i, j;
	__u32 ifindex = 0;
	int idx, err;

	if (!REQ_ARGS(2))
		return -1;
	open_attr.file = GET_ARG();
	pinfile = GET_ARG();

	while (argc) {
		if (is_prefix(*argv, "type")) {
			char *type;

			NEXT_ARG();

			if (open_attr.prog_type != BPF_PROG_TYPE_UNSPEC) {
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

			err = libbpf_prog_type_by_name(type,
						       &open_attr.prog_type,
						       &expected_attach_type);
			free(type);
			if (err < 0)
				goto err_free_reuse_maps;

			NEXT_ARG();
		} else if (is_prefix(*argv, "map")) {
			void *new_map_replace;
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

			new_map_replace = reallocarray(map_replace,
						       old_map_fds + 1,
						       sizeof(*map_replace));
			if (!new_map_replace) {
				p_err("mem alloc failed");
				goto err_free_reuse_maps;
			}
			map_replace = new_map_replace;

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
		} else if (is_prefix(*argv, "pinmaps")) {
			NEXT_ARG();

			if (!REQ_ARGS(1))
				goto err_free_reuse_maps;

			pinmaps = GET_ARG();
		} else {
			p_err("expected no more arguments, 'type', 'map' or 'dev', got: '%s'?",
			      *argv);
			goto err_free_reuse_maps;
		}
	}

	set_max_rlimit();

	obj = __bpf_object__open_xattr(&open_attr, bpf_flags);
	if (IS_ERR_OR_NULL(obj)) {
		p_err("failed to open object file");
		goto err_free_reuse_maps;
	}

	bpf_object__for_each_program(pos, obj) {
		enum bpf_prog_type prog_type = open_attr.prog_type;

		if (open_attr.prog_type == BPF_PROG_TYPE_UNSPEC) {
			const char *sec_name = bpf_program__title(pos, false);

			err = libbpf_prog_type_by_name(sec_name, &prog_type,
						       &expected_attach_type);
			if (err < 0)
				goto err_close_obj;
		}

		bpf_program__set_ifindex(pos, ifindex);
		bpf_program__set_type(pos, prog_type);
		bpf_program__set_expected_attach_type(pos, expected_attach_type);
	}

	qsort(map_replace, old_map_fds, sizeof(*map_replace),
	      map_replace_compar);

	/* After the sort maps by name will be first on the list, because they
	 * have idx == -1.  Resolve them.
	 */
	j = 0;
	while (j < old_map_fds && map_replace[j].name) {
		i = 0;
		bpf_object__for_each_map(map, obj) {
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
	bpf_object__for_each_map(map, obj) {
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

	load_attr.obj = obj;
	if (verifier_logs)
		/* log_level1 + log_level2 + stats, but not stable UAPI */
		load_attr.log_level = 1 + 2 + 4;

	err = bpf_object__load_xattr(&load_attr);
	if (err) {
		p_err("failed to load object file");
		goto err_close_obj;
	}

	err = mount_bpffs_for_pin(pinfile);
	if (err)
		goto err_close_obj;

	if (first_prog_only) {
		prog = bpf_program__next(NULL, obj);
		if (!prog) {
			p_err("object file doesn't contain any bpf program");
			goto err_close_obj;
		}

		err = bpf_obj_pin(bpf_program__fd(prog), pinfile);
		if (err) {
			p_err("failed to pin program %s",
			      bpf_program__title(prog, false));
			goto err_close_obj;
		}
	} else {
		err = bpf_object__pin_programs(obj, pinfile);
		if (err) {
			p_err("failed to pin all programs");
			goto err_close_obj;
		}
	}

	if (pinmaps) {
		err = bpf_object__pin_maps(obj, pinmaps);
		if (err) {
			p_err("failed to pin all maps");
			goto err_unpin;
		}
	}

	if (json_output)
		jsonw_null(json_wtr);

	bpf_object__close(obj);
	for (i = 0; i < old_map_fds; i++)
		close(map_replace[i].fd);
	free(map_replace);

	return 0;

err_unpin:
	if (first_prog_only)
		unlink(pinfile);
	else
		bpf_object__unpin_programs(obj, pinfile);
err_close_obj:
	bpf_object__close(obj);
err_free_reuse_maps:
	for (i = 0; i < old_map_fds; i++)
		close(map_replace[i].fd);
	free(map_replace);
	return -1;
}

static int do_load(int argc, char **argv)
{
	return load_with_options(argc, argv, true);
}

static int do_loadall(int argc, char **argv)
{
	return load_with_options(argc, argv, false);
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s %s { show | list } [PROG]\n"
		"       %s %s dump xlated PROG [{ file FILE | opcodes | visual | linum }]\n"
		"       %s %s dump jited  PROG [{ file FILE | opcodes | linum }]\n"
		"       %s %s pin   PROG FILE\n"
		"       %s %s { load | loadall } OBJ  PATH \\\n"
		"                         [type TYPE] [dev NAME] \\\n"
		"                         [map { idx IDX | name NAME } MAP]\\\n"
		"                         [pinmaps MAP_DIR]\n"
		"       %s %s attach PROG ATTACH_TYPE [MAP]\n"
		"       %s %s detach PROG ATTACH_TYPE [MAP]\n"
		"       %s %s tracelog\n"
		"       %s %s help\n"
		"\n"
		"       " HELP_SPEC_MAP "\n"
		"       " HELP_SPEC_PROGRAM "\n"
		"       TYPE := { socket | kprobe | kretprobe | classifier | action |\n"
		"                 tracepoint | raw_tracepoint | xdp | perf_event | cgroup/skb |\n"
		"                 cgroup/sock | cgroup/dev | lwt_in | lwt_out | lwt_xmit |\n"
		"                 lwt_seg6local | sockops | sk_skb | sk_msg | lirc_mode2 |\n"
		"                 sk_reuseport | flow_dissector | cgroup/sysctl |\n"
		"                 cgroup/bind4 | cgroup/bind6 | cgroup/post_bind4 |\n"
		"                 cgroup/post_bind6 | cgroup/connect4 | cgroup/connect6 |\n"
		"                 cgroup/sendmsg4 | cgroup/sendmsg6 | cgroup/recvmsg4 |\n"
		"                 cgroup/recvmsg6 | cgroup/getsockopt |\n"
		"                 cgroup/setsockopt }\n"
		"       ATTACH_TYPE := { msg_verdict | stream_verdict | stream_parser |\n"
		"                        flow_dissector }\n"
		"       " HELP_SPEC_OPTIONS "\n"
		"",
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2]);

	return 0;
}

static const struct cmd cmds[] = {
	{ "show",	do_show },
	{ "list",	do_show },
	{ "help",	do_help },
	{ "dump",	do_dump },
	{ "pin",	do_pin },
	{ "load",	do_load },
	{ "loadall",	do_loadall },
	{ "attach",	do_attach },
	{ "detach",	do_detach },
	{ "tracelog",	do_tracelog },
	{ 0 }
};

int do_prog(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}

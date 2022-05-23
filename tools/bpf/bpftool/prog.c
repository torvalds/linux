// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <dirent.h>

#include <linux/err.h>
#include <linux/perf_event.h>
#include <linux/sizes.h>

#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/hashmap.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_internal.h>
#include <bpf/skel_internal.h>

#include "cfg.h"
#include "main.h"
#include "xlated_dumper.h"

#define BPF_METADATA_PREFIX "bpf_metadata_"
#define BPF_METADATA_PREFIX_LEN (sizeof(BPF_METADATA_PREFIX) - 1)

enum dump_mode {
	DUMP_JITED,
	DUMP_XLATED,
};

static const char * const attach_type_strings[] = {
	[BPF_SK_SKB_STREAM_PARSER] = "stream_parser",
	[BPF_SK_SKB_STREAM_VERDICT] = "stream_verdict",
	[BPF_SK_SKB_VERDICT] = "skb_verdict",
	[BPF_SK_MSG_VERDICT] = "msg_verdict",
	[BPF_FLOW_DISSECTOR] = "flow_dissector",
	[__MAX_BPF_ATTACH_TYPE] = NULL,
};

static struct hashmap *prog_table;

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

static int prep_prog_info(struct bpf_prog_info *const info, enum dump_mode mode,
			  void **info_data, size_t *const info_data_sz)
{
	struct bpf_prog_info holder = {};
	size_t needed = 0;
	void *ptr;

	if (mode == DUMP_JITED) {
		holder.jited_prog_len = info->jited_prog_len;
		needed += info->jited_prog_len;
	} else {
		holder.xlated_prog_len = info->xlated_prog_len;
		needed += info->xlated_prog_len;
	}

	holder.nr_jited_ksyms = info->nr_jited_ksyms;
	needed += info->nr_jited_ksyms * sizeof(__u64);

	holder.nr_jited_func_lens = info->nr_jited_func_lens;
	needed += info->nr_jited_func_lens * sizeof(__u32);

	holder.nr_func_info = info->nr_func_info;
	holder.func_info_rec_size = info->func_info_rec_size;
	needed += info->nr_func_info * info->func_info_rec_size;

	holder.nr_line_info = info->nr_line_info;
	holder.line_info_rec_size = info->line_info_rec_size;
	needed += info->nr_line_info * info->line_info_rec_size;

	holder.nr_jited_line_info = info->nr_jited_line_info;
	holder.jited_line_info_rec_size = info->jited_line_info_rec_size;
	needed += info->nr_jited_line_info * info->jited_line_info_rec_size;

	if (needed > *info_data_sz) {
		ptr = realloc(*info_data, needed);
		if (!ptr)
			return -1;

		*info_data = ptr;
		*info_data_sz = needed;
	}
	ptr = *info_data;

	if (mode == DUMP_JITED) {
		holder.jited_prog_insns = ptr_to_u64(ptr);
		ptr += holder.jited_prog_len;
	} else {
		holder.xlated_prog_insns = ptr_to_u64(ptr);
		ptr += holder.xlated_prog_len;
	}

	holder.jited_ksyms = ptr_to_u64(ptr);
	ptr += holder.nr_jited_ksyms * sizeof(__u64);

	holder.jited_func_lens = ptr_to_u64(ptr);
	ptr += holder.nr_jited_func_lens * sizeof(__u32);

	holder.func_info = ptr_to_u64(ptr);
	ptr += holder.nr_func_info * holder.func_info_rec_size;

	holder.line_info = ptr_to_u64(ptr);
	ptr += holder.nr_line_info * holder.line_info_rec_size;

	holder.jited_line_info = ptr_to_u64(ptr);
	ptr += holder.nr_jited_line_info * holder.jited_line_info_rec_size;

	*info = holder;
	return 0;
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

static void show_prog_maps(int fd, __u32 num_maps)
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

static void *find_metadata(int prog_fd, struct bpf_map_info *map_info)
{
	struct bpf_prog_info prog_info;
	__u32 prog_info_len;
	__u32 map_info_len;
	void *value = NULL;
	__u32 *map_ids;
	int nr_maps;
	int key = 0;
	int map_fd;
	int ret;
	__u32 i;

	memset(&prog_info, 0, sizeof(prog_info));
	prog_info_len = sizeof(prog_info);
	ret = bpf_obj_get_info_by_fd(prog_fd, &prog_info, &prog_info_len);
	if (ret)
		return NULL;

	if (!prog_info.nr_map_ids)
		return NULL;

	map_ids = calloc(prog_info.nr_map_ids, sizeof(__u32));
	if (!map_ids)
		return NULL;

	nr_maps = prog_info.nr_map_ids;
	memset(&prog_info, 0, sizeof(prog_info));
	prog_info.nr_map_ids = nr_maps;
	prog_info.map_ids = ptr_to_u64(map_ids);
	prog_info_len = sizeof(prog_info);

	ret = bpf_obj_get_info_by_fd(prog_fd, &prog_info, &prog_info_len);
	if (ret)
		goto free_map_ids;

	for (i = 0; i < prog_info.nr_map_ids; i++) {
		map_fd = bpf_map_get_fd_by_id(map_ids[i]);
		if (map_fd < 0)
			goto free_map_ids;

		memset(map_info, 0, sizeof(*map_info));
		map_info_len = sizeof(*map_info);
		ret = bpf_obj_get_info_by_fd(map_fd, map_info, &map_info_len);
		if (ret < 0) {
			close(map_fd);
			goto free_map_ids;
		}

		if (map_info->type != BPF_MAP_TYPE_ARRAY ||
		    map_info->key_size != sizeof(int) ||
		    map_info->max_entries != 1 ||
		    !map_info->btf_value_type_id ||
		    !strstr(map_info->name, ".rodata")) {
			close(map_fd);
			continue;
		}

		value = malloc(map_info->value_size);
		if (!value) {
			close(map_fd);
			goto free_map_ids;
		}

		if (bpf_map_lookup_elem(map_fd, &key, value)) {
			close(map_fd);
			free(value);
			value = NULL;
			goto free_map_ids;
		}

		close(map_fd);
		break;
	}

free_map_ids:
	free(map_ids);
	return value;
}

static bool has_metadata_prefix(const char *s)
{
	return strncmp(s, BPF_METADATA_PREFIX, BPF_METADATA_PREFIX_LEN) == 0;
}

static void show_prog_metadata(int fd, __u32 num_maps)
{
	const struct btf_type *t_datasec, *t_var;
	struct bpf_map_info map_info;
	struct btf_var_secinfo *vsi;
	bool printed_header = false;
	unsigned int i, vlen;
	void *value = NULL;
	const char *name;
	struct btf *btf;
	int err;

	if (!num_maps)
		return;

	memset(&map_info, 0, sizeof(map_info));
	value = find_metadata(fd, &map_info);
	if (!value)
		return;

	btf = btf__load_from_kernel_by_id(map_info.btf_id);
	if (libbpf_get_error(btf))
		goto out_free;

	t_datasec = btf__type_by_id(btf, map_info.btf_value_type_id);
	if (!btf_is_datasec(t_datasec))
		goto out_free;

	vlen = btf_vlen(t_datasec);
	vsi = btf_var_secinfos(t_datasec);

	/* We don't proceed to check the kinds of the elements of the DATASEC.
	 * The verifier enforces them to be BTF_KIND_VAR.
	 */

	if (json_output) {
		struct btf_dumper d = {
			.btf = btf,
			.jw = json_wtr,
			.is_plain_text = false,
		};

		for (i = 0; i < vlen; i++, vsi++) {
			t_var = btf__type_by_id(btf, vsi->type);
			name = btf__name_by_offset(btf, t_var->name_off);

			if (!has_metadata_prefix(name))
				continue;

			if (!printed_header) {
				jsonw_name(json_wtr, "metadata");
				jsonw_start_object(json_wtr);
				printed_header = true;
			}

			jsonw_name(json_wtr, name + BPF_METADATA_PREFIX_LEN);
			err = btf_dumper_type(&d, t_var->type, value + vsi->offset);
			if (err) {
				p_err("btf dump failed: %d", err);
				break;
			}
		}
		if (printed_header)
			jsonw_end_object(json_wtr);
	} else {
		json_writer_t *btf_wtr;
		struct btf_dumper d = {
			.btf = btf,
			.is_plain_text = true,
		};

		for (i = 0; i < vlen; i++, vsi++) {
			t_var = btf__type_by_id(btf, vsi->type);
			name = btf__name_by_offset(btf, t_var->name_off);

			if (!has_metadata_prefix(name))
				continue;

			if (!printed_header) {
				printf("\tmetadata:");

				btf_wtr = jsonw_new(stdout);
				if (!btf_wtr) {
					p_err("jsonw alloc failed");
					goto out_free;
				}
				d.jw = btf_wtr,

				printed_header = true;
			}

			printf("\n\t\t%s = ", name + BPF_METADATA_PREFIX_LEN);

			jsonw_reset(btf_wtr);
			err = btf_dumper_type(&d, t_var->type, value + vsi->offset);
			if (err) {
				p_err("btf dump failed: %d", err);
				break;
			}
		}
		if (printed_header)
			jsonw_destroy(&btf_wtr);
	}

out_free:
	btf__free(btf);
	free(value);
}

static void print_prog_header_json(struct bpf_prog_info *info, int fd)
{
	const char *prog_type_str;
	char prog_name[MAX_PROG_FULL_NAME];

	jsonw_uint_field(json_wtr, "id", info->id);
	prog_type_str = libbpf_bpf_prog_type_str(info->type);

	if (prog_type_str)
		jsonw_string_field(json_wtr, "type", prog_type_str);
	else
		jsonw_uint_field(json_wtr, "type", info->type);

	if (*info->name) {
		get_prog_full_name(info, fd, prog_name, sizeof(prog_name));
		jsonw_string_field(json_wtr, "name", prog_name);
	}

	jsonw_name(json_wtr, "tag");
	jsonw_printf(json_wtr, "\"" BPF_TAG_FMT "\"",
		     info->tag[0], info->tag[1], info->tag[2], info->tag[3],
		     info->tag[4], info->tag[5], info->tag[6], info->tag[7]);

	jsonw_bool_field(json_wtr, "gpl_compatible", info->gpl_compatible);
	if (info->run_time_ns) {
		jsonw_uint_field(json_wtr, "run_time_ns", info->run_time_ns);
		jsonw_uint_field(json_wtr, "run_cnt", info->run_cnt);
	}
	if (info->recursion_misses)
		jsonw_uint_field(json_wtr, "recursion_misses", info->recursion_misses);
}

static void print_prog_json(struct bpf_prog_info *info, int fd)
{
	char *memlock;

	jsonw_start_object(json_wtr);
	print_prog_header_json(info, fd);
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
		jsonw_int_field(json_wtr, "bytes_memlock", atoll(memlock));
	free(memlock);

	if (info->nr_map_ids)
		show_prog_maps(fd, info->nr_map_ids);

	if (info->btf_id)
		jsonw_int_field(json_wtr, "btf_id", info->btf_id);

	if (!hashmap__empty(prog_table)) {
		struct hashmap_entry *entry;

		jsonw_name(json_wtr, "pinned");
		jsonw_start_array(json_wtr);
		hashmap__for_each_key_entry(prog_table, entry,
					    u32_as_hash_field(info->id))
			jsonw_string(json_wtr, entry->value);
		jsonw_end_array(json_wtr);
	}

	emit_obj_refs_json(refs_table, info->id, json_wtr);

	show_prog_metadata(fd, info->nr_map_ids);

	jsonw_end_object(json_wtr);
}

static void print_prog_header_plain(struct bpf_prog_info *info, int fd)
{
	const char *prog_type_str;
	char prog_name[MAX_PROG_FULL_NAME];

	printf("%u: ", info->id);
	prog_type_str = libbpf_bpf_prog_type_str(info->type);
	if (prog_type_str)
		printf("%s  ", prog_type_str);
	else
		printf("type %u  ", info->type);

	if (*info->name) {
		get_prog_full_name(info, fd, prog_name, sizeof(prog_name));
		printf("name %s  ", prog_name);
	}

	printf("tag ");
	fprint_hex(stdout, info->tag, BPF_TAG_SIZE, "");
	print_dev_plain(info->ifindex, info->netns_dev, info->netns_ino);
	printf("%s", info->gpl_compatible ? "  gpl" : "");
	if (info->run_time_ns)
		printf(" run_time_ns %lld run_cnt %lld",
		       info->run_time_ns, info->run_cnt);
	if (info->recursion_misses)
		printf(" recursion_misses %lld", info->recursion_misses);
	printf("\n");
}

static void print_prog_plain(struct bpf_prog_info *info, int fd)
{
	char *memlock;

	print_prog_header_plain(info, fd);

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

	if (!hashmap__empty(prog_table)) {
		struct hashmap_entry *entry;

		hashmap__for_each_key_entry(prog_table, entry,
					    u32_as_hash_field(info->id))
			printf("\n\tpinned %s", (char *)entry->value);
	}

	if (info->btf_id)
		printf("\n\tbtf_id %d", info->btf_id);

	emit_obj_refs_plain(refs_table, info->id, "\n\tpids ");

	printf("\n");

	show_prog_metadata(fd, info->nr_map_ids);
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

static int do_show_subset(int argc, char **argv)
{
	int *fds = NULL;
	int nb_fds, i;
	int err = -1;

	fds = malloc(sizeof(int));
	if (!fds) {
		p_err("mem alloc failed");
		return -1;
	}
	nb_fds = prog_parse_fds(&argc, &argv, &fds);
	if (nb_fds < 1)
		goto exit_free;

	if (json_output && nb_fds > 1)
		jsonw_start_array(json_wtr);	/* root array */
	for (i = 0; i < nb_fds; i++) {
		err = show_prog(fds[i]);
		if (err) {
			for (; i < nb_fds; i++)
				close(fds[i]);
			break;
		}
		close(fds[i]);
	}
	if (json_output && nb_fds > 1)
		jsonw_end_array(json_wtr);	/* root array */

exit_free:
	free(fds);
	return err;
}

static int do_show(int argc, char **argv)
{
	__u32 id = 0;
	int err;
	int fd;

	if (show_pinned) {
		prog_table = hashmap__new(hash_fn_for_key_as_id,
					  equal_fn_for_key_as_id, NULL);
		if (IS_ERR(prog_table)) {
			p_err("failed to create hashmap for pinned paths");
			return -1;
		}
		build_pinned_obj_table(prog_table, BPF_OBJ_PROG);
	}
	build_obj_refs_table(&refs_table, BPF_OBJ_PROG);

	if (argc == 2)
		return do_show_subset(argc, argv);

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

	delete_obj_refs_table(refs_table);

	if (show_pinned)
		delete_pinned_obj_table(prog_table);

	return err;
}

static int
prog_dump(struct bpf_prog_info *info, enum dump_mode mode,
	  char *filepath, bool opcodes, bool visual, bool linum)
{
	struct bpf_prog_linfo *prog_linfo = NULL;
	const char *disasm_opt = NULL;
	struct dump_data dd = {};
	void *func_info = NULL;
	struct btf *btf = NULL;
	char func_sig[1024];
	unsigned char *buf;
	__u32 member_len;
	int fd, err = -1;
	ssize_t n;

	if (mode == DUMP_JITED) {
		if (info->jited_prog_len == 0 || !info->jited_prog_insns) {
			p_info("no instructions returned");
			return -1;
		}
		buf = u64_to_ptr(info->jited_prog_insns);
		member_len = info->jited_prog_len;
	} else {	/* DUMP_XLATED */
		if (info->xlated_prog_len == 0 || !info->xlated_prog_insns) {
			p_err("error retrieving insn dump: kernel.kptr_restrict set?");
			return -1;
		}
		buf = u64_to_ptr(info->xlated_prog_insns);
		member_len = info->xlated_prog_len;
	}

	if (info->btf_id) {
		btf = btf__load_from_kernel_by_id(info->btf_id);
		if (libbpf_get_error(btf)) {
			p_err("failed to get btf");
			return -1;
		}
	}

	func_info = u64_to_ptr(info->func_info);

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
			goto exit_free;
		}

		n = write(fd, buf, member_len);
		close(fd);
		if (n != (ssize_t)member_len) {
			p_err("error writing output file: %s",
			      n < 0 ? strerror(errno) : "short write");
			goto exit_free;
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
				goto exit_free;
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
				ksyms = u64_to_ptr(info->jited_ksyms);
			}

			if (json_output)
				jsonw_start_array(json_wtr);

			lens = u64_to_ptr(info->jited_func_lens);
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
		dd.jited_ksyms = u64_to_ptr(info->jited_ksyms);
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

	err = 0;

exit_free:
	btf__free(btf);
	bpf_prog_linfo__free(prog_linfo);
	return err;
}

static int do_dump(int argc, char **argv)
{
	struct bpf_prog_info info;
	__u32 info_len = sizeof(info);
	size_t info_data_sz = 0;
	void *info_data = NULL;
	char *filepath = NULL;
	bool opcodes = false;
	bool visual = false;
	enum dump_mode mode;
	bool linum = false;
	int nb_fds, i = 0;
	int *fds = NULL;
	int err = -1;

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

	fds = malloc(sizeof(int));
	if (!fds) {
		p_err("mem alloc failed");
		return -1;
	}
	nb_fds = prog_parse_fds(&argc, &argv, &fds);
	if (nb_fds < 1)
		goto exit_free;

	if (is_prefix(*argv, "file")) {
		NEXT_ARG();
		if (!argc) {
			p_err("expected file path");
			goto exit_close;
		}
		if (nb_fds > 1) {
			p_err("several programs matched");
			goto exit_close;
		}

		filepath = *argv;
		NEXT_ARG();
	} else if (is_prefix(*argv, "opcodes")) {
		opcodes = true;
		NEXT_ARG();
	} else if (is_prefix(*argv, "visual")) {
		if (nb_fds > 1) {
			p_err("several programs matched");
			goto exit_close;
		}

		visual = true;
		NEXT_ARG();
	} else if (is_prefix(*argv, "linum")) {
		linum = true;
		NEXT_ARG();
	}

	if (argc) {
		usage();
		goto exit_close;
	}

	if (json_output && nb_fds > 1)
		jsonw_start_array(json_wtr);	/* root array */
	for (i = 0; i < nb_fds; i++) {
		memset(&info, 0, sizeof(info));

		err = bpf_obj_get_info_by_fd(fds[i], &info, &info_len);
		if (err) {
			p_err("can't get prog info: %s", strerror(errno));
			break;
		}

		err = prep_prog_info(&info, mode, &info_data, &info_data_sz);
		if (err) {
			p_err("can't grow prog info_data");
			break;
		}

		err = bpf_obj_get_info_by_fd(fds[i], &info, &info_len);
		if (err) {
			p_err("can't get prog info: %s", strerror(errno));
			break;
		}

		if (json_output && nb_fds > 1) {
			jsonw_start_object(json_wtr);	/* prog object */
			print_prog_header_json(&info, fds[i]);
			jsonw_name(json_wtr, "insns");
		} else if (nb_fds > 1) {
			print_prog_header_plain(&info, fds[i]);
		}

		err = prog_dump(&info, mode, filepath, opcodes, visual, linum);

		if (json_output && nb_fds > 1)
			jsonw_end_object(json_wtr);	/* prog object */
		else if (i != nb_fds - 1 && nb_fds > 1)
			printf("\n");

		if (err)
			break;
		close(fds[i]);
	}
	if (json_output && nb_fds > 1)
		jsonw_end_array(json_wtr);	/* root array */

exit_close:
	for (; i < nb_fds; i++)
		close(fds[i]);
exit_free:
	free(info_data);
	free(fds);
	return err;
}

static int do_pin(int argc, char **argv)
{
	int err;

	err = do_pin_any(argc, argv, prog_parse_fd);
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
		*mapfd = 0;
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

static int check_single_stdin(char *file_data_in, char *file_ctx_in)
{
	if (file_data_in && file_ctx_in &&
	    !strcmp(file_data_in, "-") && !strcmp(file_ctx_in, "-")) {
		p_err("cannot use standard input for both data_in and ctx_in");
		return -1;
	}

	return 0;
}

static int get_run_data(const char *fname, void **data_ptr, unsigned int *size)
{
	size_t block_size = 256;
	size_t buf_size = block_size;
	size_t nb_read = 0;
	void *tmp;
	FILE *f;

	if (!fname) {
		*data_ptr = NULL;
		*size = 0;
		return 0;
	}

	if (!strcmp(fname, "-"))
		f = stdin;
	else
		f = fopen(fname, "r");
	if (!f) {
		p_err("failed to open %s: %s", fname, strerror(errno));
		return -1;
	}

	*data_ptr = malloc(block_size);
	if (!*data_ptr) {
		p_err("failed to allocate memory for data_in/ctx_in: %s",
		      strerror(errno));
		goto err_fclose;
	}

	while ((nb_read += fread(*data_ptr + nb_read, 1, block_size, f))) {
		if (feof(f))
			break;
		if (ferror(f)) {
			p_err("failed to read data_in/ctx_in from %s: %s",
			      fname, strerror(errno));
			goto err_free;
		}
		if (nb_read > buf_size - block_size) {
			if (buf_size == UINT32_MAX) {
				p_err("data_in/ctx_in is too long (max: %d)",
				      UINT32_MAX);
				goto err_free;
			}
			/* No space for fread()-ing next chunk; realloc() */
			buf_size *= 2;
			tmp = realloc(*data_ptr, buf_size);
			if (!tmp) {
				p_err("failed to reallocate data_in/ctx_in: %s",
				      strerror(errno));
				goto err_free;
			}
			*data_ptr = tmp;
		}
	}
	if (f != stdin)
		fclose(f);

	*size = nb_read;
	return 0;

err_free:
	free(*data_ptr);
	*data_ptr = NULL;
err_fclose:
	if (f != stdin)
		fclose(f);
	return -1;
}

static void hex_print(void *data, unsigned int size, FILE *f)
{
	size_t i, j;
	char c;

	for (i = 0; i < size; i += 16) {
		/* Row offset */
		fprintf(f, "%07zx\t", i);

		/* Hexadecimal values */
		for (j = i; j < i + 16 && j < size; j++)
			fprintf(f, "%02x%s", *(uint8_t *)(data + j),
				j % 2 ? " " : "");
		for (; j < i + 16; j++)
			fprintf(f, "  %s", j % 2 ? " " : "");

		/* ASCII values (if relevant), '.' otherwise */
		fprintf(f, "| ");
		for (j = i; j < i + 16 && j < size; j++) {
			c = *(char *)(data + j);
			if (c < ' ' || c > '~')
				c = '.';
			fprintf(f, "%c%s", c, j == i + 7 ? " " : "");
		}

		fprintf(f, "\n");
	}
}

static int
print_run_output(void *data, unsigned int size, const char *fname,
		 const char *json_key)
{
	size_t nb_written;
	FILE *f;

	if (!fname)
		return 0;

	if (!strcmp(fname, "-")) {
		f = stdout;
		if (json_output) {
			jsonw_name(json_wtr, json_key);
			print_data_json(data, size);
		} else {
			hex_print(data, size, f);
		}
		return 0;
	}

	f = fopen(fname, "w");
	if (!f) {
		p_err("failed to open %s: %s", fname, strerror(errno));
		return -1;
	}

	nb_written = fwrite(data, 1, size, f);
	fclose(f);
	if (nb_written != size) {
		p_err("failed to write output data/ctx: %s", strerror(errno));
		return -1;
	}

	return 0;
}

static int alloc_run_data(void **data_ptr, unsigned int size_out)
{
	*data_ptr = calloc(size_out, 1);
	if (!*data_ptr) {
		p_err("failed to allocate memory for output data/ctx: %s",
		      strerror(errno));
		return -1;
	}

	return 0;
}

static int do_run(int argc, char **argv)
{
	char *data_fname_in = NULL, *data_fname_out = NULL;
	char *ctx_fname_in = NULL, *ctx_fname_out = NULL;
	const unsigned int default_size = SZ_32K;
	void *data_in = NULL, *data_out = NULL;
	void *ctx_in = NULL, *ctx_out = NULL;
	unsigned int repeat = 1;
	int fd, err;
	LIBBPF_OPTS(bpf_test_run_opts, test_attr);

	if (!REQ_ARGS(4))
		return -1;

	fd = prog_parse_fd(&argc, &argv);
	if (fd < 0)
		return -1;

	while (argc) {
		if (detect_common_prefix(*argv, "data_in", "data_out",
					 "data_size_out", NULL))
			return -1;
		if (detect_common_prefix(*argv, "ctx_in", "ctx_out",
					 "ctx_size_out", NULL))
			return -1;

		if (is_prefix(*argv, "data_in")) {
			NEXT_ARG();
			if (!REQ_ARGS(1))
				return -1;

			data_fname_in = GET_ARG();
			if (check_single_stdin(data_fname_in, ctx_fname_in))
				return -1;
		} else if (is_prefix(*argv, "data_out")) {
			NEXT_ARG();
			if (!REQ_ARGS(1))
				return -1;

			data_fname_out = GET_ARG();
		} else if (is_prefix(*argv, "data_size_out")) {
			char *endptr;

			NEXT_ARG();
			if (!REQ_ARGS(1))
				return -1;

			test_attr.data_size_out = strtoul(*argv, &endptr, 0);
			if (*endptr) {
				p_err("can't parse %s as output data size",
				      *argv);
				return -1;
			}
			NEXT_ARG();
		} else if (is_prefix(*argv, "ctx_in")) {
			NEXT_ARG();
			if (!REQ_ARGS(1))
				return -1;

			ctx_fname_in = GET_ARG();
			if (check_single_stdin(data_fname_in, ctx_fname_in))
				return -1;
		} else if (is_prefix(*argv, "ctx_out")) {
			NEXT_ARG();
			if (!REQ_ARGS(1))
				return -1;

			ctx_fname_out = GET_ARG();
		} else if (is_prefix(*argv, "ctx_size_out")) {
			char *endptr;

			NEXT_ARG();
			if (!REQ_ARGS(1))
				return -1;

			test_attr.ctx_size_out = strtoul(*argv, &endptr, 0);
			if (*endptr) {
				p_err("can't parse %s as output context size",
				      *argv);
				return -1;
			}
			NEXT_ARG();
		} else if (is_prefix(*argv, "repeat")) {
			char *endptr;

			NEXT_ARG();
			if (!REQ_ARGS(1))
				return -1;

			repeat = strtoul(*argv, &endptr, 0);
			if (*endptr) {
				p_err("can't parse %s as repeat number",
				      *argv);
				return -1;
			}
			NEXT_ARG();
		} else {
			p_err("expected no more arguments, 'data_in', 'data_out', 'data_size_out', 'ctx_in', 'ctx_out', 'ctx_size_out' or 'repeat', got: '%s'?",
			      *argv);
			return -1;
		}
	}

	err = get_run_data(data_fname_in, &data_in, &test_attr.data_size_in);
	if (err)
		return -1;

	if (data_in) {
		if (!test_attr.data_size_out)
			test_attr.data_size_out = default_size;
		err = alloc_run_data(&data_out, test_attr.data_size_out);
		if (err)
			goto free_data_in;
	}

	err = get_run_data(ctx_fname_in, &ctx_in, &test_attr.ctx_size_in);
	if (err)
		goto free_data_out;

	if (ctx_in) {
		if (!test_attr.ctx_size_out)
			test_attr.ctx_size_out = default_size;
		err = alloc_run_data(&ctx_out, test_attr.ctx_size_out);
		if (err)
			goto free_ctx_in;
	}

	test_attr.repeat	= repeat;
	test_attr.data_in	= data_in;
	test_attr.data_out	= data_out;
	test_attr.ctx_in	= ctx_in;
	test_attr.ctx_out	= ctx_out;

	err = bpf_prog_test_run_opts(fd, &test_attr);
	if (err) {
		p_err("failed to run program: %s", strerror(errno));
		goto free_ctx_out;
	}

	err = 0;

	if (json_output)
		jsonw_start_object(json_wtr);	/* root */

	/* Do not exit on errors occurring when printing output data/context,
	 * we still want to print return value and duration for program run.
	 */
	if (test_attr.data_size_out)
		err += print_run_output(test_attr.data_out,
					test_attr.data_size_out,
					data_fname_out, "data_out");
	if (test_attr.ctx_size_out)
		err += print_run_output(test_attr.ctx_out,
					test_attr.ctx_size_out,
					ctx_fname_out, "ctx_out");

	if (json_output) {
		jsonw_uint_field(json_wtr, "retval", test_attr.retval);
		jsonw_uint_field(json_wtr, "duration", test_attr.duration);
		jsonw_end_object(json_wtr);	/* root */
	} else {
		fprintf(stdout, "Return value: %u, duration%s: %uns\n",
			test_attr.retval,
			repeat > 1 ? " (average)" : "", test_attr.duration);
	}

free_ctx_out:
	free(ctx_out);
free_ctx_in:
	free(ctx_in);
free_data_out:
	free(data_out);
free_data_in:
	free(data_in);

	return err;
}

static int
get_prog_type_by_name(const char *name, enum bpf_prog_type *prog_type,
		      enum bpf_attach_type *expected_attach_type)
{
	libbpf_print_fn_t print_backup;
	int ret;

	ret = libbpf_prog_type_by_name(name, prog_type, expected_attach_type);
	if (!ret)
		return ret;

	/* libbpf_prog_type_by_name() failed, let's re-run with debug level */
	print_backup = libbpf_set_print(print_all_levels);
	ret = libbpf_prog_type_by_name(name, prog_type, expected_attach_type);
	libbpf_set_print(print_backup);

	return ret;
}

static int load_with_options(int argc, char **argv, bool first_prog_only)
{
	enum bpf_prog_type common_prog_type = BPF_PROG_TYPE_UNSPEC;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, open_opts,
		.relaxed_maps = relaxed_maps,
	);
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
	const char *file;
	int idx, err;


	if (!REQ_ARGS(2))
		return -1;
	file = GET_ARG();
	pinfile = GET_ARG();

	while (argc) {
		if (is_prefix(*argv, "type")) {
			NEXT_ARG();

			if (common_prog_type != BPF_PROG_TYPE_UNSPEC) {
				p_err("program type already specified");
				goto err_free_reuse_maps;
			}
			if (!REQ_ARGS(1))
				goto err_free_reuse_maps;

			err = libbpf_prog_type_by_name(*argv, &common_prog_type,
						       &expected_attach_type);
			if (err < 0) {
				/* Put a '/' at the end of type to appease libbpf */
				char *type = malloc(strlen(*argv) + 2);

				if (!type) {
					p_err("mem alloc failed");
					goto err_free_reuse_maps;
				}
				*type = 0;
				strcat(type, *argv);
				strcat(type, "/");

				err = get_prog_type_by_name(type, &common_prog_type,
							    &expected_attach_type);
				free(type);
				if (err < 0)
					goto err_free_reuse_maps;
			}

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

			new_map_replace = libbpf_reallocarray(map_replace,
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

	if (verifier_logs)
		/* log_level1 + log_level2 + stats, but not stable UAPI */
		open_opts.kernel_log_level = 1 + 2 + 4;

	obj = bpf_object__open_file(file, &open_opts);
	if (libbpf_get_error(obj)) {
		p_err("failed to open object file");
		goto err_free_reuse_maps;
	}

	bpf_object__for_each_program(pos, obj) {
		enum bpf_prog_type prog_type = common_prog_type;

		if (prog_type == BPF_PROG_TYPE_UNSPEC) {
			const char *sec_name = bpf_program__section_name(pos);

			err = get_prog_type_by_name(sec_name, &prog_type,
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
		if (bpf_map__type(map) != BPF_MAP_TYPE_PERF_EVENT_ARRAY)
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

	err = bpf_object__load(obj);
	if (err) {
		p_err("failed to load object file");
		goto err_close_obj;
	}

	err = mount_bpffs_for_pin(pinfile);
	if (err)
		goto err_close_obj;

	if (first_prog_only) {
		prog = bpf_object__next_program(obj, NULL);
		if (!prog) {
			p_err("object file doesn't contain any bpf program");
			goto err_close_obj;
		}

		err = bpf_obj_pin(bpf_program__fd(prog), pinfile);
		if (err) {
			p_err("failed to pin program %s",
			      bpf_program__section_name(prog));
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
	if (!legacy_libbpf) {
		p_info("Warning: bpftool is now running in libbpf strict mode and has more stringent requirements about BPF programs.\n"
		       "If it used to work for this object file but now doesn't, see --legacy option for more details.\n");
	}

	bpf_object__close(obj);
err_free_reuse_maps:
	for (i = 0; i < old_map_fds; i++)
		close(map_replace[i].fd);
	free(map_replace);
	return -1;
}

static int count_open_fds(void)
{
	DIR *dp = opendir("/proc/self/fd");
	struct dirent *de;
	int cnt = -3;

	if (!dp)
		return -1;

	while ((de = readdir(dp)))
		cnt++;

	closedir(dp);
	return cnt;
}

static int try_loader(struct gen_loader_opts *gen)
{
	struct bpf_load_and_run_opts opts = {};
	struct bpf_loader_ctx *ctx;
	int ctx_sz = sizeof(*ctx) + 64 * max(sizeof(struct bpf_map_desc),
					     sizeof(struct bpf_prog_desc));
	int log_buf_sz = (1u << 24) - 1;
	int err, fds_before, fd_delta;
	char *log_buf = NULL;

	ctx = alloca(ctx_sz);
	memset(ctx, 0, ctx_sz);
	ctx->sz = ctx_sz;
	if (verifier_logs) {
		ctx->log_level = 1 + 2 + 4;
		ctx->log_size = log_buf_sz;
		log_buf = malloc(log_buf_sz);
		if (!log_buf)
			return -ENOMEM;
		ctx->log_buf = (long) log_buf;
	}
	opts.ctx = ctx;
	opts.data = gen->data;
	opts.data_sz = gen->data_sz;
	opts.insns = gen->insns;
	opts.insns_sz = gen->insns_sz;
	fds_before = count_open_fds();
	err = bpf_load_and_run(&opts);
	fd_delta = count_open_fds() - fds_before;
	if (err < 0 || verifier_logs) {
		fprintf(stderr, "err %d\n%s\n%s", err, opts.errstr, log_buf);
		if (fd_delta && err < 0)
			fprintf(stderr, "loader prog leaked %d FDs\n",
				fd_delta);
	}
	free(log_buf);
	return err;
}

static int do_loader(int argc, char **argv)
{
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, open_opts);
	DECLARE_LIBBPF_OPTS(gen_loader_opts, gen);
	struct bpf_object *obj;
	const char *file;
	int err = 0;

	if (!REQ_ARGS(1))
		return -1;
	file = GET_ARG();

	if (verifier_logs)
		/* log_level1 + log_level2 + stats, but not stable UAPI */
		open_opts.kernel_log_level = 1 + 2 + 4;

	obj = bpf_object__open_file(file, &open_opts);
	if (libbpf_get_error(obj)) {
		p_err("failed to open object file");
		goto err_close_obj;
	}

	err = bpf_object__gen_loader(obj, &gen);
	if (err)
		goto err_close_obj;

	err = bpf_object__load(obj);
	if (err) {
		p_err("failed to load object file");
		goto err_close_obj;
	}

	if (verifier_logs) {
		struct dump_data dd = {};

		kernel_syms_load(&dd);
		dump_xlated_plain(&dd, (void *)gen.insns, gen.insns_sz, false, false);
		kernel_syms_destroy(&dd);
	}
	err = try_loader(&gen);
err_close_obj:
	bpf_object__close(obj);
	return err;
}

static int do_load(int argc, char **argv)
{
	if (use_loader)
		return do_loader(argc, argv);
	return load_with_options(argc, argv, true);
}

static int do_loadall(int argc, char **argv)
{
	return load_with_options(argc, argv, false);
}

#ifdef BPFTOOL_WITHOUT_SKELETONS

static int do_profile(int argc, char **argv)
{
	p_err("bpftool prog profile command is not supported. Please build bpftool with clang >= 10.0.0");
	return 0;
}

#else /* BPFTOOL_WITHOUT_SKELETONS */

#include "profiler.skel.h"

struct profile_metric {
	const char *name;
	struct bpf_perf_event_value val;
	struct perf_event_attr attr;
	bool selected;

	/* calculate ratios like instructions per cycle */
	const int ratio_metric; /* 0 for N/A, 1 for index 0 (cycles) */
	const char *ratio_desc;
	const float ratio_mul;
} metrics[] = {
	{
		.name = "cycles",
		.attr = {
			.type = PERF_TYPE_HARDWARE,
			.config = PERF_COUNT_HW_CPU_CYCLES,
			.exclude_user = 1,
		},
	},
	{
		.name = "instructions",
		.attr = {
			.type = PERF_TYPE_HARDWARE,
			.config = PERF_COUNT_HW_INSTRUCTIONS,
			.exclude_user = 1,
		},
		.ratio_metric = 1,
		.ratio_desc = "insns per cycle",
		.ratio_mul = 1.0,
	},
	{
		.name = "l1d_loads",
		.attr = {
			.type = PERF_TYPE_HW_CACHE,
			.config =
				PERF_COUNT_HW_CACHE_L1D |
				(PERF_COUNT_HW_CACHE_OP_READ << 8) |
				(PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16),
			.exclude_user = 1,
		},
	},
	{
		.name = "llc_misses",
		.attr = {
			.type = PERF_TYPE_HW_CACHE,
			.config =
				PERF_COUNT_HW_CACHE_LL |
				(PERF_COUNT_HW_CACHE_OP_READ << 8) |
				(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
			.exclude_user = 1
		},
		.ratio_metric = 2,
		.ratio_desc = "LLC misses per million insns",
		.ratio_mul = 1e6,
	},
	{
		.name = "itlb_misses",
		.attr = {
			.type = PERF_TYPE_HW_CACHE,
			.config =
				PERF_COUNT_HW_CACHE_ITLB |
				(PERF_COUNT_HW_CACHE_OP_READ << 8) |
				(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
			.exclude_user = 1
		},
		.ratio_metric = 2,
		.ratio_desc = "itlb misses per million insns",
		.ratio_mul = 1e6,
	},
	{
		.name = "dtlb_misses",
		.attr = {
			.type = PERF_TYPE_HW_CACHE,
			.config =
				PERF_COUNT_HW_CACHE_DTLB |
				(PERF_COUNT_HW_CACHE_OP_READ << 8) |
				(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
			.exclude_user = 1
		},
		.ratio_metric = 2,
		.ratio_desc = "dtlb misses per million insns",
		.ratio_mul = 1e6,
	},
};

static __u64 profile_total_count;

#define MAX_NUM_PROFILE_METRICS 4

static int profile_parse_metrics(int argc, char **argv)
{
	unsigned int metric_cnt;
	int selected_cnt = 0;
	unsigned int i;

	metric_cnt = sizeof(metrics) / sizeof(struct profile_metric);

	while (argc > 0) {
		for (i = 0; i < metric_cnt; i++) {
			if (is_prefix(argv[0], metrics[i].name)) {
				if (!metrics[i].selected)
					selected_cnt++;
				metrics[i].selected = true;
				break;
			}
		}
		if (i == metric_cnt) {
			p_err("unknown metric %s", argv[0]);
			return -1;
		}
		NEXT_ARG();
	}
	if (selected_cnt > MAX_NUM_PROFILE_METRICS) {
		p_err("too many (%d) metrics, please specify no more than %d metrics at at time",
		      selected_cnt, MAX_NUM_PROFILE_METRICS);
		return -1;
	}
	return selected_cnt;
}

static void profile_read_values(struct profiler_bpf *obj)
{
	__u32 m, cpu, num_cpu = obj->rodata->num_cpu;
	int reading_map_fd, count_map_fd;
	__u64 counts[num_cpu];
	__u32 key = 0;
	int err;

	reading_map_fd = bpf_map__fd(obj->maps.accum_readings);
	count_map_fd = bpf_map__fd(obj->maps.counts);
	if (reading_map_fd < 0 || count_map_fd < 0) {
		p_err("failed to get fd for map");
		return;
	}

	err = bpf_map_lookup_elem(count_map_fd, &key, counts);
	if (err) {
		p_err("failed to read count_map: %s", strerror(errno));
		return;
	}

	profile_total_count = 0;
	for (cpu = 0; cpu < num_cpu; cpu++)
		profile_total_count += counts[cpu];

	for (m = 0; m < ARRAY_SIZE(metrics); m++) {
		struct bpf_perf_event_value values[num_cpu];

		if (!metrics[m].selected)
			continue;

		err = bpf_map_lookup_elem(reading_map_fd, &key, values);
		if (err) {
			p_err("failed to read reading_map: %s",
			      strerror(errno));
			return;
		}
		for (cpu = 0; cpu < num_cpu; cpu++) {
			metrics[m].val.counter += values[cpu].counter;
			metrics[m].val.enabled += values[cpu].enabled;
			metrics[m].val.running += values[cpu].running;
		}
		key++;
	}
}

static void profile_print_readings_json(void)
{
	__u32 m;

	jsonw_start_array(json_wtr);
	for (m = 0; m < ARRAY_SIZE(metrics); m++) {
		if (!metrics[m].selected)
			continue;
		jsonw_start_object(json_wtr);
		jsonw_string_field(json_wtr, "metric", metrics[m].name);
		jsonw_lluint_field(json_wtr, "run_cnt", profile_total_count);
		jsonw_lluint_field(json_wtr, "value", metrics[m].val.counter);
		jsonw_lluint_field(json_wtr, "enabled", metrics[m].val.enabled);
		jsonw_lluint_field(json_wtr, "running", metrics[m].val.running);

		jsonw_end_object(json_wtr);
	}
	jsonw_end_array(json_wtr);
}

static void profile_print_readings_plain(void)
{
	__u32 m;

	printf("\n%18llu %-20s\n", profile_total_count, "run_cnt");
	for (m = 0; m < ARRAY_SIZE(metrics); m++) {
		struct bpf_perf_event_value *val = &metrics[m].val;
		int r;

		if (!metrics[m].selected)
			continue;
		printf("%18llu %-20s", val->counter, metrics[m].name);

		r = metrics[m].ratio_metric - 1;
		if (r >= 0 && metrics[r].selected &&
		    metrics[r].val.counter > 0) {
			printf("# %8.2f %-30s",
			       val->counter * metrics[m].ratio_mul /
			       metrics[r].val.counter,
			       metrics[m].ratio_desc);
		} else {
			printf("%-41s", "");
		}

		if (val->enabled > val->running)
			printf("(%4.2f%%)",
			       val->running * 100.0 / val->enabled);
		printf("\n");
	}
}

static void profile_print_readings(void)
{
	if (json_output)
		profile_print_readings_json();
	else
		profile_print_readings_plain();
}

static char *profile_target_name(int tgt_fd)
{
	struct bpf_func_info func_info;
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	const struct btf_type *t;
	__u32 func_info_rec_size;
	struct btf *btf = NULL;
	char *name = NULL;
	int err;

	err = bpf_obj_get_info_by_fd(tgt_fd, &info, &info_len);
	if (err) {
		p_err("failed to bpf_obj_get_info_by_fd for prog FD %d", tgt_fd);
		goto out;
	}

	if (info.btf_id == 0) {
		p_err("prog FD %d doesn't have valid btf", tgt_fd);
		goto out;
	}

	func_info_rec_size = info.func_info_rec_size;
	if (info.nr_func_info == 0) {
		p_err("bpf_obj_get_info_by_fd for prog FD %d found 0 func_info", tgt_fd);
		goto out;
	}

	memset(&info, 0, sizeof(info));
	info.nr_func_info = 1;
	info.func_info_rec_size = func_info_rec_size;
	info.func_info = ptr_to_u64(&func_info);

	err = bpf_obj_get_info_by_fd(tgt_fd, &info, &info_len);
	if (err) {
		p_err("failed to get func_info for prog FD %d", tgt_fd);
		goto out;
	}

	btf = btf__load_from_kernel_by_id(info.btf_id);
	if (libbpf_get_error(btf)) {
		p_err("failed to load btf for prog FD %d", tgt_fd);
		goto out;
	}

	t = btf__type_by_id(btf, func_info.type_id);
	if (!t) {
		p_err("btf %d doesn't have type %d",
		      info.btf_id, func_info.type_id);
		goto out;
	}
	name = strdup(btf__name_by_offset(btf, t->name_off));
out:
	btf__free(btf);
	return name;
}

static struct profiler_bpf *profile_obj;
static int profile_tgt_fd = -1;
static char *profile_tgt_name;
static int *profile_perf_events;
static int profile_perf_event_cnt;

static void profile_close_perf_events(struct profiler_bpf *obj)
{
	int i;

	for (i = profile_perf_event_cnt - 1; i >= 0; i--)
		close(profile_perf_events[i]);

	free(profile_perf_events);
	profile_perf_event_cnt = 0;
}

static int profile_open_perf_events(struct profiler_bpf *obj)
{
	unsigned int cpu, m;
	int map_fd, pmu_fd;

	profile_perf_events = calloc(
		sizeof(int), obj->rodata->num_cpu * obj->rodata->num_metric);
	if (!profile_perf_events) {
		p_err("failed to allocate memory for perf_event array: %s",
		      strerror(errno));
		return -1;
	}
	map_fd = bpf_map__fd(obj->maps.events);
	if (map_fd < 0) {
		p_err("failed to get fd for events map");
		return -1;
	}

	for (m = 0; m < ARRAY_SIZE(metrics); m++) {
		if (!metrics[m].selected)
			continue;
		for (cpu = 0; cpu < obj->rodata->num_cpu; cpu++) {
			pmu_fd = syscall(__NR_perf_event_open, &metrics[m].attr,
					 -1/*pid*/, cpu, -1/*group_fd*/, 0);
			if (pmu_fd < 0 ||
			    bpf_map_update_elem(map_fd, &profile_perf_event_cnt,
						&pmu_fd, BPF_ANY) ||
			    ioctl(pmu_fd, PERF_EVENT_IOC_ENABLE, 0)) {
				p_err("failed to create event %s on cpu %d",
				      metrics[m].name, cpu);
				return -1;
			}
			profile_perf_events[profile_perf_event_cnt++] = pmu_fd;
		}
	}
	return 0;
}

static void profile_print_and_cleanup(void)
{
	profile_close_perf_events(profile_obj);
	profile_read_values(profile_obj);
	profile_print_readings();
	profiler_bpf__destroy(profile_obj);

	close(profile_tgt_fd);
	free(profile_tgt_name);
}

static void int_exit(int signo)
{
	profile_print_and_cleanup();
	exit(0);
}

static int do_profile(int argc, char **argv)
{
	int num_metric, num_cpu, err = -1;
	struct bpf_program *prog;
	unsigned long duration;
	char *endptr;

	/* we at least need two args for the prog and one metric */
	if (!REQ_ARGS(3))
		return -EINVAL;

	/* parse target fd */
	profile_tgt_fd = prog_parse_fd(&argc, &argv);
	if (profile_tgt_fd < 0) {
		p_err("failed to parse fd");
		return -1;
	}

	/* parse profiling optional duration */
	if (argc > 2 && is_prefix(argv[0], "duration")) {
		NEXT_ARG();
		duration = strtoul(*argv, &endptr, 0);
		if (*endptr)
			usage();
		NEXT_ARG();
	} else {
		duration = UINT_MAX;
	}

	num_metric = profile_parse_metrics(argc, argv);
	if (num_metric <= 0)
		goto out;

	num_cpu = libbpf_num_possible_cpus();
	if (num_cpu <= 0) {
		p_err("failed to identify number of CPUs");
		goto out;
	}

	profile_obj = profiler_bpf__open();
	if (!profile_obj) {
		p_err("failed to open and/or load BPF object");
		goto out;
	}

	profile_obj->rodata->num_cpu = num_cpu;
	profile_obj->rodata->num_metric = num_metric;

	/* adjust map sizes */
	bpf_map__set_max_entries(profile_obj->maps.events, num_metric * num_cpu);
	bpf_map__set_max_entries(profile_obj->maps.fentry_readings, num_metric);
	bpf_map__set_max_entries(profile_obj->maps.accum_readings, num_metric);
	bpf_map__set_max_entries(profile_obj->maps.counts, 1);

	/* change target name */
	profile_tgt_name = profile_target_name(profile_tgt_fd);
	if (!profile_tgt_name)
		goto out;

	bpf_object__for_each_program(prog, profile_obj->obj) {
		err = bpf_program__set_attach_target(prog, profile_tgt_fd,
						     profile_tgt_name);
		if (err) {
			p_err("failed to set attach target\n");
			goto out;
		}
	}

	err = profiler_bpf__load(profile_obj);
	if (err) {
		p_err("failed to load profile_obj");
		goto out;
	}

	err = profile_open_perf_events(profile_obj);
	if (err)
		goto out;

	err = profiler_bpf__attach(profile_obj);
	if (err) {
		p_err("failed to attach profile_obj");
		goto out;
	}
	signal(SIGINT, int_exit);

	sleep(duration);
	profile_print_and_cleanup();
	return 0;

out:
	profile_close_perf_events(profile_obj);
	if (profile_obj)
		profiler_bpf__destroy(profile_obj);
	close(profile_tgt_fd);
	free(profile_tgt_name);
	return err;
}

#endif /* BPFTOOL_WITHOUT_SKELETONS */

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %1$s %2$s { show | list } [PROG]\n"
		"       %1$s %2$s dump xlated PROG [{ file FILE | opcodes | visual | linum }]\n"
		"       %1$s %2$s dump jited  PROG [{ file FILE | opcodes | linum }]\n"
		"       %1$s %2$s pin   PROG FILE\n"
		"       %1$s %2$s { load | loadall } OBJ  PATH \\\n"
		"                         [type TYPE] [dev NAME] \\\n"
		"                         [map { idx IDX | name NAME } MAP]\\\n"
		"                         [pinmaps MAP_DIR]\n"
		"       %1$s %2$s attach PROG ATTACH_TYPE [MAP]\n"
		"       %1$s %2$s detach PROG ATTACH_TYPE [MAP]\n"
		"       %1$s %2$s run PROG \\\n"
		"                         data_in FILE \\\n"
		"                         [data_out FILE [data_size_out L]] \\\n"
		"                         [ctx_in FILE [ctx_out FILE [ctx_size_out M]]] \\\n"
		"                         [repeat N]\n"
		"       %1$s %2$s profile PROG [duration DURATION] METRICs\n"
		"       %1$s %2$s tracelog\n"
		"       %1$s %2$s help\n"
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
		"                 cgroup/getpeername4 | cgroup/getpeername6 |\n"
		"                 cgroup/getsockname4 | cgroup/getsockname6 | cgroup/sendmsg4 |\n"
		"                 cgroup/sendmsg6 | cgroup/recvmsg4 | cgroup/recvmsg6 |\n"
		"                 cgroup/getsockopt | cgroup/setsockopt | cgroup/sock_release |\n"
		"                 struct_ops | fentry | fexit | freplace | sk_lookup }\n"
		"       ATTACH_TYPE := { msg_verdict | skb_verdict | stream_verdict |\n"
		"                        stream_parser | flow_dissector }\n"
		"       METRIC := { cycles | instructions | l1d_loads | llc_misses | itlb_misses | dtlb_misses }\n"
		"       " HELP_SPEC_OPTIONS " |\n"
		"                    {-f|--bpffs} | {-m|--mapcompat} | {-n|--nomount} |\n"
		"                    {-L|--use-loader} }\n"
		"",
		bin_name, argv[-2]);

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
	{ "run",	do_run },
	{ "profile",	do_profile },
	{ 0 }
};

int do_prog(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}

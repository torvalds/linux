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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <bpf.h>

#include "btf.h"
#include "json_writer.h"
#include "main.h"

static const char * const map_type_name[] = {
	[BPF_MAP_TYPE_UNSPEC]		= "unspec",
	[BPF_MAP_TYPE_HASH]		= "hash",
	[BPF_MAP_TYPE_ARRAY]		= "array",
	[BPF_MAP_TYPE_PROG_ARRAY]	= "prog_array",
	[BPF_MAP_TYPE_PERF_EVENT_ARRAY]	= "perf_event_array",
	[BPF_MAP_TYPE_PERCPU_HASH]	= "percpu_hash",
	[BPF_MAP_TYPE_PERCPU_ARRAY]	= "percpu_array",
	[BPF_MAP_TYPE_STACK_TRACE]	= "stack_trace",
	[BPF_MAP_TYPE_CGROUP_ARRAY]	= "cgroup_array",
	[BPF_MAP_TYPE_LRU_HASH]		= "lru_hash",
	[BPF_MAP_TYPE_LRU_PERCPU_HASH]	= "lru_percpu_hash",
	[BPF_MAP_TYPE_LPM_TRIE]		= "lpm_trie",
	[BPF_MAP_TYPE_ARRAY_OF_MAPS]	= "array_of_maps",
	[BPF_MAP_TYPE_HASH_OF_MAPS]	= "hash_of_maps",
	[BPF_MAP_TYPE_DEVMAP]		= "devmap",
	[BPF_MAP_TYPE_SOCKMAP]		= "sockmap",
	[BPF_MAP_TYPE_CPUMAP]		= "cpumap",
	[BPF_MAP_TYPE_XSKMAP]           = "xskmap",
	[BPF_MAP_TYPE_SOCKHASH]		= "sockhash",
	[BPF_MAP_TYPE_CGROUP_STORAGE]	= "cgroup_storage",
};

static bool map_is_per_cpu(__u32 type)
{
	return type == BPF_MAP_TYPE_PERCPU_HASH ||
	       type == BPF_MAP_TYPE_PERCPU_ARRAY ||
	       type == BPF_MAP_TYPE_LRU_PERCPU_HASH;
}

static bool map_is_map_of_maps(__u32 type)
{
	return type == BPF_MAP_TYPE_ARRAY_OF_MAPS ||
	       type == BPF_MAP_TYPE_HASH_OF_MAPS;
}

static bool map_is_map_of_progs(__u32 type)
{
	return type == BPF_MAP_TYPE_PROG_ARRAY;
}

static void *alloc_value(struct bpf_map_info *info)
{
	if (map_is_per_cpu(info->type))
		return malloc(round_up(info->value_size, 8) *
			      get_possible_cpus());
	else
		return malloc(info->value_size);
}

int map_parse_fd(int *argc, char ***argv)
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

		fd = bpf_map_get_fd_by_id(id);
		if (fd < 0)
			p_err("get map by id (%u): %s", id, strerror(errno));
		return fd;
	} else if (is_prefix(**argv, "pinned")) {
		char *path;

		NEXT_ARGP();

		path = **argv;
		NEXT_ARGP();

		return open_obj_pinned_any(path, BPF_OBJ_MAP);
	}

	p_err("expected 'id' or 'pinned', got: '%s'?", **argv);
	return -1;
}

int map_parse_fd_and_info(int *argc, char ***argv, void *info, __u32 *info_len)
{
	int err;
	int fd;

	fd = map_parse_fd(argc, argv);
	if (fd < 0)
		return -1;

	err = bpf_obj_get_info_by_fd(fd, info, info_len);
	if (err) {
		p_err("can't get map info: %s", strerror(errno));
		close(fd);
		return err;
	}

	return fd;
}

static int do_dump_btf(const struct btf_dumper *d,
		       struct bpf_map_info *map_info, void *key,
		       void *value)
{
	int ret;

	/* start of key-value pair */
	jsonw_start_object(d->jw);

	jsonw_name(d->jw, "key");

	ret = btf_dumper_type(d, map_info->btf_key_type_id, key);
	if (ret)
		goto err_end_obj;

	jsonw_name(d->jw, "value");

	ret = btf_dumper_type(d, map_info->btf_value_type_id, value);

err_end_obj:
	/* end of key-value pair */
	jsonw_end_object(d->jw);

	return ret;
}

static int get_btf(struct bpf_map_info *map_info, struct btf **btf)
{
	struct bpf_btf_info btf_info = { 0 };
	__u32 len = sizeof(btf_info);
	__u32 last_size;
	int btf_fd;
	void *ptr;
	int err;

	err = 0;
	*btf = NULL;
	btf_fd = bpf_btf_get_fd_by_id(map_info->btf_id);
	if (btf_fd < 0)
		return 0;

	/* we won't know btf_size until we call bpf_obj_get_info_by_fd(). so
	 * let's start with a sane default - 4KiB here - and resize it only if
	 * bpf_obj_get_info_by_fd() needs a bigger buffer.
	 */
	btf_info.btf_size = 4096;
	last_size = btf_info.btf_size;
	ptr = malloc(last_size);
	if (!ptr) {
		err = -ENOMEM;
		goto exit_free;
	}

	bzero(ptr, last_size);
	btf_info.btf = ptr_to_u64(ptr);
	err = bpf_obj_get_info_by_fd(btf_fd, &btf_info, &len);

	if (!err && btf_info.btf_size > last_size) {
		void *temp_ptr;

		last_size = btf_info.btf_size;
		temp_ptr = realloc(ptr, last_size);
		if (!temp_ptr) {
			err = -ENOMEM;
			goto exit_free;
		}
		ptr = temp_ptr;
		bzero(ptr, last_size);
		btf_info.btf = ptr_to_u64(ptr);
		err = bpf_obj_get_info_by_fd(btf_fd, &btf_info, &len);
	}

	if (err || btf_info.btf_size > last_size) {
		err = errno;
		goto exit_free;
	}

	*btf = btf__new((__u8 *)btf_info.btf, btf_info.btf_size, NULL);
	if (IS_ERR(*btf)) {
		err = PTR_ERR(*btf);
		*btf = NULL;
	}

exit_free:
	close(btf_fd);
	free(ptr);

	return err;
}

static json_writer_t *get_btf_writer(void)
{
	json_writer_t *jw = jsonw_new(stdout);

	if (!jw)
		return NULL;
	jsonw_pretty(jw, true);

	return jw;
}

static void print_entry_json(struct bpf_map_info *info, unsigned char *key,
			     unsigned char *value, struct btf *btf)
{
	jsonw_start_object(json_wtr);

	if (!map_is_per_cpu(info->type)) {
		jsonw_name(json_wtr, "key");
		print_hex_data_json(key, info->key_size);
		jsonw_name(json_wtr, "value");
		print_hex_data_json(value, info->value_size);
		if (btf) {
			struct btf_dumper d = {
				.btf = btf,
				.jw = json_wtr,
				.is_plain_text = false,
			};

			jsonw_name(json_wtr, "formatted");
			do_dump_btf(&d, info, key, value);
		}
	} else {
		unsigned int i, n, step;

		n = get_possible_cpus();
		step = round_up(info->value_size, 8);

		jsonw_name(json_wtr, "key");
		print_hex_data_json(key, info->key_size);

		jsonw_name(json_wtr, "values");
		jsonw_start_array(json_wtr);
		for (i = 0; i < n; i++) {
			jsonw_start_object(json_wtr);

			jsonw_int_field(json_wtr, "cpu", i);

			jsonw_name(json_wtr, "value");
			print_hex_data_json(value + i * step,
					    info->value_size);

			jsonw_end_object(json_wtr);
		}
		jsonw_end_array(json_wtr);
	}

	jsonw_end_object(json_wtr);
}

static void print_entry_plain(struct bpf_map_info *info, unsigned char *key,
			      unsigned char *value)
{
	if (!map_is_per_cpu(info->type)) {
		bool single_line, break_names;

		break_names = info->key_size > 16 || info->value_size > 16;
		single_line = info->key_size + info->value_size <= 24 &&
			!break_names;

		printf("key:%c", break_names ? '\n' : ' ');
		fprint_hex(stdout, key, info->key_size, " ");

		printf(single_line ? "  " : "\n");

		printf("value:%c", break_names ? '\n' : ' ');
		fprint_hex(stdout, value, info->value_size, " ");

		printf("\n");
	} else {
		unsigned int i, n, step;

		n = get_possible_cpus();
		step = round_up(info->value_size, 8);

		printf("key:\n");
		fprint_hex(stdout, key, info->key_size, " ");
		printf("\n");
		for (i = 0; i < n; i++) {
			printf("value (CPU %02d):%c",
			       i, info->value_size > 16 ? '\n' : ' ');
			fprint_hex(stdout, value + i * step,
				   info->value_size, " ");
			printf("\n");
		}
	}
}

static char **parse_bytes(char **argv, const char *name, unsigned char *val,
			  unsigned int n)
{
	unsigned int i = 0, base = 0;
	char *endptr;

	if (is_prefix(*argv, "hex")) {
		base = 16;
		argv++;
	}

	while (i < n && argv[i]) {
		val[i] = strtoul(argv[i], &endptr, base);
		if (*endptr) {
			p_err("error parsing byte: %s", argv[i]);
			return NULL;
		}
		i++;
	}

	if (i != n) {
		p_err("%s expected %d bytes got %d", name, n, i);
		return NULL;
	}

	return argv + i;
}

/* on per cpu maps we must copy the provided value on all value instances */
static void fill_per_cpu_value(struct bpf_map_info *info, void *value)
{
	unsigned int i, n, step;

	if (!map_is_per_cpu(info->type))
		return;

	n = get_possible_cpus();
	step = round_up(info->value_size, 8);
	for (i = 1; i < n; i++)
		memcpy(value + i * step, value, info->value_size);
}

static int parse_elem(char **argv, struct bpf_map_info *info,
		      void *key, void *value, __u32 key_size, __u32 value_size,
		      __u32 *flags, __u32 **value_fd)
{
	if (!*argv) {
		if (!key && !value)
			return 0;
		p_err("did not find %s", key ? "key" : "value");
		return -1;
	}

	if (is_prefix(*argv, "key")) {
		if (!key) {
			if (key_size)
				p_err("duplicate key");
			else
				p_err("unnecessary key");
			return -1;
		}

		argv = parse_bytes(argv + 1, "key", key, key_size);
		if (!argv)
			return -1;

		return parse_elem(argv, info, NULL, value, key_size, value_size,
				  flags, value_fd);
	} else if (is_prefix(*argv, "value")) {
		int fd;

		if (!value) {
			if (value_size)
				p_err("duplicate value");
			else
				p_err("unnecessary value");
			return -1;
		}

		argv++;

		if (map_is_map_of_maps(info->type)) {
			int argc = 2;

			if (value_size != 4) {
				p_err("value smaller than 4B for map in map?");
				return -1;
			}
			if (!argv[0] || !argv[1]) {
				p_err("not enough value arguments for map in map");
				return -1;
			}

			fd = map_parse_fd(&argc, &argv);
			if (fd < 0)
				return -1;

			*value_fd = value;
			**value_fd = fd;
		} else if (map_is_map_of_progs(info->type)) {
			int argc = 2;

			if (value_size != 4) {
				p_err("value smaller than 4B for map of progs?");
				return -1;
			}
			if (!argv[0] || !argv[1]) {
				p_err("not enough value arguments for map of progs");
				return -1;
			}

			fd = prog_parse_fd(&argc, &argv);
			if (fd < 0)
				return -1;

			*value_fd = value;
			**value_fd = fd;
		} else {
			argv = parse_bytes(argv, "value", value, value_size);
			if (!argv)
				return -1;

			fill_per_cpu_value(info, value);
		}

		return parse_elem(argv, info, key, NULL, key_size, value_size,
				  flags, NULL);
	} else if (is_prefix(*argv, "any") || is_prefix(*argv, "noexist") ||
		   is_prefix(*argv, "exist")) {
		if (!flags) {
			p_err("flags specified multiple times: %s", *argv);
			return -1;
		}

		if (is_prefix(*argv, "any"))
			*flags = BPF_ANY;
		else if (is_prefix(*argv, "noexist"))
			*flags = BPF_NOEXIST;
		else if (is_prefix(*argv, "exist"))
			*flags = BPF_EXIST;

		return parse_elem(argv + 1, info, key, value, key_size,
				  value_size, NULL, value_fd);
	}

	p_err("expected key or value, got: %s", *argv);
	return -1;
}

static int show_map_close_json(int fd, struct bpf_map_info *info)
{
	char *memlock;

	memlock = get_fdinfo(fd, "memlock");
	close(fd);

	jsonw_start_object(json_wtr);

	jsonw_uint_field(json_wtr, "id", info->id);
	if (info->type < ARRAY_SIZE(map_type_name))
		jsonw_string_field(json_wtr, "type",
				   map_type_name[info->type]);
	else
		jsonw_uint_field(json_wtr, "type", info->type);

	if (*info->name)
		jsonw_string_field(json_wtr, "name", info->name);

	jsonw_name(json_wtr, "flags");
	jsonw_printf(json_wtr, "%d", info->map_flags);

	print_dev_json(info->ifindex, info->netns_dev, info->netns_ino);

	jsonw_uint_field(json_wtr, "bytes_key", info->key_size);
	jsonw_uint_field(json_wtr, "bytes_value", info->value_size);
	jsonw_uint_field(json_wtr, "max_entries", info->max_entries);

	if (memlock)
		jsonw_int_field(json_wtr, "bytes_memlock", atoi(memlock));
	free(memlock);

	if (!hash_empty(map_table.table)) {
		struct pinned_obj *obj;

		jsonw_name(json_wtr, "pinned");
		jsonw_start_array(json_wtr);
		hash_for_each_possible(map_table.table, obj, hash, info->id) {
			if (obj->id == info->id)
				jsonw_string(json_wtr, obj->path);
		}
		jsonw_end_array(json_wtr);
	}

	jsonw_end_object(json_wtr);

	return 0;
}

static int show_map_close_plain(int fd, struct bpf_map_info *info)
{
	char *memlock;

	memlock = get_fdinfo(fd, "memlock");
	close(fd);

	printf("%u: ", info->id);
	if (info->type < ARRAY_SIZE(map_type_name))
		printf("%s  ", map_type_name[info->type]);
	else
		printf("type %u  ", info->type);

	if (*info->name)
		printf("name %s  ", info->name);

	printf("flags 0x%x", info->map_flags);
	print_dev_plain(info->ifindex, info->netns_dev, info->netns_ino);
	printf("\n");
	printf("\tkey %uB  value %uB  max_entries %u",
	       info->key_size, info->value_size, info->max_entries);

	if (memlock)
		printf("  memlock %sB", memlock);
	free(memlock);

	printf("\n");
	if (!hash_empty(map_table.table)) {
		struct pinned_obj *obj;

		hash_for_each_possible(map_table.table, obj, hash, info->id) {
			if (obj->id == info->id)
				printf("\tpinned %s\n", obj->path);
		}
	}
	return 0;
}

static int do_show(int argc, char **argv)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	__u32 id = 0;
	int err;
	int fd;

	if (show_pinned)
		build_pinned_obj_table(&map_table, BPF_OBJ_MAP);

	if (argc == 2) {
		fd = map_parse_fd_and_info(&argc, &argv, &info, &len);
		if (fd < 0)
			return -1;

		if (json_output)
			return show_map_close_json(fd, &info);
		else
			return show_map_close_plain(fd, &info);
	}

	if (argc)
		return BAD_ARG();

	if (json_output)
		jsonw_start_array(json_wtr);
	while (true) {
		err = bpf_map_get_next_id(id, &id);
		if (err) {
			if (errno == ENOENT)
				break;
			p_err("can't get next map: %s%s", strerror(errno),
			      errno == EINVAL ? " -- kernel too old?" : "");
			break;
		}

		fd = bpf_map_get_fd_by_id(id);
		if (fd < 0) {
			if (errno == ENOENT)
				continue;
			p_err("can't get map by id (%u): %s",
			      id, strerror(errno));
			break;
		}

		err = bpf_obj_get_info_by_fd(fd, &info, &len);
		if (err) {
			p_err("can't get map info: %s", strerror(errno));
			close(fd);
			break;
		}

		if (json_output)
			show_map_close_json(fd, &info);
		else
			show_map_close_plain(fd, &info);
	}
	if (json_output)
		jsonw_end_array(json_wtr);

	return errno == ENOENT ? 0 : -1;
}

static int do_dump(int argc, char **argv)
{
	struct bpf_map_info info = {};
	void *key, *value, *prev_key;
	unsigned int num_elems = 0;
	__u32 len = sizeof(info);
	json_writer_t *btf_wtr;
	struct btf *btf = NULL;
	int err;
	int fd;

	if (argc != 2)
		usage();

	fd = map_parse_fd_and_info(&argc, &argv, &info, &len);
	if (fd < 0)
		return -1;

	if (map_is_map_of_maps(info.type) || map_is_map_of_progs(info.type)) {
		p_err("Dumping maps of maps and program maps not supported");
		close(fd);
		return -1;
	}

	key = malloc(info.key_size);
	value = alloc_value(&info);
	if (!key || !value) {
		p_err("mem alloc failed");
		err = -1;
		goto exit_free;
	}

	prev_key = NULL;

	err = get_btf(&info, &btf);
	if (err) {
		p_err("failed to get btf");
		goto exit_free;
	}

	if (json_output)
		jsonw_start_array(json_wtr);
	else
		if (btf) {
			btf_wtr = get_btf_writer();
			if (!btf_wtr) {
				p_info("failed to create json writer for btf. falling back to plain output");
				btf__free(btf);
				btf = NULL;
			} else {
				jsonw_start_array(btf_wtr);
			}
		}

	while (true) {
		err = bpf_map_get_next_key(fd, prev_key, key);
		if (err) {
			if (errno == ENOENT)
				err = 0;
			break;
		}

		if (!bpf_map_lookup_elem(fd, key, value)) {
			if (json_output)
				print_entry_json(&info, key, value, btf);
			else
				if (btf) {
					struct btf_dumper d = {
						.btf = btf,
						.jw = btf_wtr,
						.is_plain_text = true,
					};

					do_dump_btf(&d, &info, key, value);
				} else {
					print_entry_plain(&info, key, value);
				}
		} else {
			if (json_output) {
				jsonw_name(json_wtr, "key");
				print_hex_data_json(key, info.key_size);
				jsonw_name(json_wtr, "value");
				jsonw_start_object(json_wtr);
				jsonw_string_field(json_wtr, "error",
						   "can't lookup element");
				jsonw_end_object(json_wtr);
			} else {
				p_info("can't lookup element with key: ");
				fprint_hex(stderr, key, info.key_size, " ");
				fprintf(stderr, "\n");
			}
		}

		prev_key = key;
		num_elems++;
	}

	if (json_output)
		jsonw_end_array(json_wtr);
	else if (btf) {
		jsonw_end_array(btf_wtr);
		jsonw_destroy(&btf_wtr);
	} else {
		printf("Found %u element%s\n", num_elems,
		       num_elems != 1 ? "s" : "");
	}

exit_free:
	free(key);
	free(value);
	close(fd);
	btf__free(btf);

	return err;
}

static int do_update(int argc, char **argv)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	__u32 *value_fd = NULL;
	__u32 flags = BPF_ANY;
	void *key, *value;
	int fd, err;

	if (argc < 2)
		usage();

	fd = map_parse_fd_and_info(&argc, &argv, &info, &len);
	if (fd < 0)
		return -1;

	key = malloc(info.key_size);
	value = alloc_value(&info);
	if (!key || !value) {
		p_err("mem alloc failed");
		err = -1;
		goto exit_free;
	}

	err = parse_elem(argv, &info, key, value, info.key_size,
			 info.value_size, &flags, &value_fd);
	if (err)
		goto exit_free;

	err = bpf_map_update_elem(fd, key, value, flags);
	if (err) {
		p_err("update failed: %s", strerror(errno));
		goto exit_free;
	}

exit_free:
	if (value_fd)
		close(*value_fd);
	free(key);
	free(value);
	close(fd);

	if (!err && json_output)
		jsonw_null(json_wtr);
	return err;
}

static int do_lookup(int argc, char **argv)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	json_writer_t *btf_wtr;
	struct btf *btf = NULL;
	void *key, *value;
	int err;
	int fd;

	if (argc < 2)
		usage();

	fd = map_parse_fd_and_info(&argc, &argv, &info, &len);
	if (fd < 0)
		return -1;

	key = malloc(info.key_size);
	value = alloc_value(&info);
	if (!key || !value) {
		p_err("mem alloc failed");
		err = -1;
		goto exit_free;
	}

	err = parse_elem(argv, &info, key, NULL, info.key_size, 0, NULL, NULL);
	if (err)
		goto exit_free;

	err = bpf_map_lookup_elem(fd, key, value);
	if (err) {
		if (errno == ENOENT) {
			if (json_output) {
				jsonw_null(json_wtr);
			} else {
				printf("key:\n");
				fprint_hex(stdout, key, info.key_size, " ");
				printf("\n\nNot found\n");
			}
		} else {
			p_err("lookup failed: %s", strerror(errno));
		}

		goto exit_free;
	}

	/* here means bpf_map_lookup_elem() succeeded */
	err = get_btf(&info, &btf);
	if (err) {
		p_err("failed to get btf");
		goto exit_free;
	}

	if (json_output) {
		print_entry_json(&info, key, value, btf);
	} else if (btf) {
		/* if here json_wtr wouldn't have been initialised,
		 * so let's create separate writer for btf
		 */
		btf_wtr = get_btf_writer();
		if (!btf_wtr) {
			p_info("failed to create json writer for btf. falling back to plain output");
			btf__free(btf);
			btf = NULL;
			print_entry_plain(&info, key, value);
		} else {
			struct btf_dumper d = {
				.btf = btf,
				.jw = btf_wtr,
				.is_plain_text = true,
			};

			do_dump_btf(&d, &info, key, value);
			jsonw_destroy(&btf_wtr);
		}
	} else {
		print_entry_plain(&info, key, value);
	}

exit_free:
	free(key);
	free(value);
	close(fd);
	btf__free(btf);

	return err;
}

static int do_getnext(int argc, char **argv)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	void *key, *nextkey;
	int err;
	int fd;

	if (argc < 2)
		usage();

	fd = map_parse_fd_and_info(&argc, &argv, &info, &len);
	if (fd < 0)
		return -1;

	key = malloc(info.key_size);
	nextkey = malloc(info.key_size);
	if (!key || !nextkey) {
		p_err("mem alloc failed");
		err = -1;
		goto exit_free;
	}

	if (argc) {
		err = parse_elem(argv, &info, key, NULL, info.key_size, 0,
				 NULL, NULL);
		if (err)
			goto exit_free;
	} else {
		free(key);
		key = NULL;
	}

	err = bpf_map_get_next_key(fd, key, nextkey);
	if (err) {
		p_err("can't get next key: %s", strerror(errno));
		goto exit_free;
	}

	if (json_output) {
		jsonw_start_object(json_wtr);
		if (key) {
			jsonw_name(json_wtr, "key");
			print_hex_data_json(key, info.key_size);
		} else {
			jsonw_null_field(json_wtr, "key");
		}
		jsonw_name(json_wtr, "next_key");
		print_hex_data_json(nextkey, info.key_size);
		jsonw_end_object(json_wtr);
	} else {
		if (key) {
			printf("key:\n");
			fprint_hex(stdout, key, info.key_size, " ");
			printf("\n");
		} else {
			printf("key: None\n");
		}
		printf("next key:\n");
		fprint_hex(stdout, nextkey, info.key_size, " ");
		printf("\n");
	}

exit_free:
	free(nextkey);
	free(key);
	close(fd);

	return err;
}

static int do_delete(int argc, char **argv)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	void *key;
	int err;
	int fd;

	if (argc < 2)
		usage();

	fd = map_parse_fd_and_info(&argc, &argv, &info, &len);
	if (fd < 0)
		return -1;

	key = malloc(info.key_size);
	if (!key) {
		p_err("mem alloc failed");
		err = -1;
		goto exit_free;
	}

	err = parse_elem(argv, &info, key, NULL, info.key_size, 0, NULL, NULL);
	if (err)
		goto exit_free;

	err = bpf_map_delete_elem(fd, key);
	if (err)
		p_err("delete failed: %s", strerror(errno));

exit_free:
	free(key);
	close(fd);

	if (!err && json_output)
		jsonw_null(json_wtr);
	return err;
}

static int do_pin(int argc, char **argv)
{
	int err;

	err = do_pin_any(argc, argv, bpf_map_get_fd_by_id);
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
		"Usage: %s %s { show | list }   [MAP]\n"
		"       %s %s dump       MAP\n"
		"       %s %s update     MAP  key DATA value VALUE [UPDATE_FLAGS]\n"
		"       %s %s lookup     MAP  key DATA\n"
		"       %s %s getnext    MAP [key DATA]\n"
		"       %s %s delete     MAP  key DATA\n"
		"       %s %s pin        MAP  FILE\n"
		"       %s %s event_pipe MAP [cpu N index M]\n"
		"       %s %s help\n"
		"\n"
		"       " HELP_SPEC_MAP "\n"
		"       DATA := { [hex] BYTES }\n"
		"       " HELP_SPEC_PROGRAM "\n"
		"       VALUE := { DATA | MAP | PROG }\n"
		"       UPDATE_FLAGS := { any | exist | noexist }\n"
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
	{ "update",	do_update },
	{ "lookup",	do_lookup },
	{ "getnext",	do_getnext },
	{ "delete",	do_delete },
	{ "pin",	do_pin },
	{ "event_pipe",	do_event_pipe },
	{ 0 }
};

int do_map(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}

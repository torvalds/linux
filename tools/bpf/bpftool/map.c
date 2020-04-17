// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <bpf/bpf.h>
#include <bpf/btf.h>

#include "json_writer.h"
#include "main.h"

const char * const map_type_name[] = {
	[BPF_MAP_TYPE_UNSPEC]			= "unspec",
	[BPF_MAP_TYPE_HASH]			= "hash",
	[BPF_MAP_TYPE_ARRAY]			= "array",
	[BPF_MAP_TYPE_PROG_ARRAY]		= "prog_array",
	[BPF_MAP_TYPE_PERF_EVENT_ARRAY]		= "perf_event_array",
	[BPF_MAP_TYPE_PERCPU_HASH]		= "percpu_hash",
	[BPF_MAP_TYPE_PERCPU_ARRAY]		= "percpu_array",
	[BPF_MAP_TYPE_STACK_TRACE]		= "stack_trace",
	[BPF_MAP_TYPE_CGROUP_ARRAY]		= "cgroup_array",
	[BPF_MAP_TYPE_LRU_HASH]			= "lru_hash",
	[BPF_MAP_TYPE_LRU_PERCPU_HASH]		= "lru_percpu_hash",
	[BPF_MAP_TYPE_LPM_TRIE]			= "lpm_trie",
	[BPF_MAP_TYPE_ARRAY_OF_MAPS]		= "array_of_maps",
	[BPF_MAP_TYPE_HASH_OF_MAPS]		= "hash_of_maps",
	[BPF_MAP_TYPE_DEVMAP]			= "devmap",
	[BPF_MAP_TYPE_DEVMAP_HASH]		= "devmap_hash",
	[BPF_MAP_TYPE_SOCKMAP]			= "sockmap",
	[BPF_MAP_TYPE_CPUMAP]			= "cpumap",
	[BPF_MAP_TYPE_XSKMAP]			= "xskmap",
	[BPF_MAP_TYPE_SOCKHASH]			= "sockhash",
	[BPF_MAP_TYPE_CGROUP_STORAGE]		= "cgroup_storage",
	[BPF_MAP_TYPE_REUSEPORT_SOCKARRAY]	= "reuseport_sockarray",
	[BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE]	= "percpu_cgroup_storage",
	[BPF_MAP_TYPE_QUEUE]			= "queue",
	[BPF_MAP_TYPE_STACK]			= "stack",
	[BPF_MAP_TYPE_SK_STORAGE]		= "sk_storage",
	[BPF_MAP_TYPE_STRUCT_OPS]		= "struct_ops",
};

const size_t map_type_name_size = ARRAY_SIZE(map_type_name);

static bool map_is_per_cpu(__u32 type)
{
	return type == BPF_MAP_TYPE_PERCPU_HASH ||
	       type == BPF_MAP_TYPE_PERCPU_ARRAY ||
	       type == BPF_MAP_TYPE_LRU_PERCPU_HASH ||
	       type == BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE;
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

static int map_type_from_str(const char *type)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(map_type_name); i++)
		/* Don't allow prefixing in case of possible future shadowing */
		if (map_type_name[i] && !strcmp(map_type_name[i], type))
			return i;
	return -1;
}

static void *alloc_value(struct bpf_map_info *info)
{
	if (map_is_per_cpu(info->type))
		return malloc(round_up(info->value_size, 8) *
			      get_possible_cpus());
	else
		return malloc(info->value_size);
}

static int map_fd_by_name(char *name, int **fds)
{
	unsigned int id = 0;
	int fd, nb_fds = 0;
	void *tmp;
	int err;

	while (true) {
		struct bpf_map_info info = {};
		__u32 len = sizeof(info);

		err = bpf_map_get_next_id(id, &id);
		if (err) {
			if (errno != ENOENT) {
				p_err("%s", strerror(errno));
				goto err_close_fds;
			}
			return nb_fds;
		}

		fd = bpf_map_get_fd_by_id(id);
		if (fd < 0) {
			p_err("can't get map by id (%u): %s",
			      id, strerror(errno));
			goto err_close_fds;
		}

		err = bpf_obj_get_info_by_fd(fd, &info, &len);
		if (err) {
			p_err("can't get map info (%u): %s",
			      id, strerror(errno));
			goto err_close_fd;
		}

		if (strncmp(name, info.name, BPF_OBJ_NAME_LEN)) {
			close(fd);
			continue;
		}

		if (nb_fds > 0) {
			tmp = realloc(*fds, (nb_fds + 1) * sizeof(int));
			if (!tmp) {
				p_err("failed to realloc");
				goto err_close_fd;
			}
			*fds = tmp;
		}
		(*fds)[nb_fds++] = fd;
	}

err_close_fd:
	close(fd);
err_close_fds:
	while (--nb_fds >= 0)
		close((*fds)[nb_fds]);
	return -1;
}

static int map_parse_fds(int *argc, char ***argv, int **fds)
{
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

		(*fds)[0] = bpf_map_get_fd_by_id(id);
		if ((*fds)[0] < 0) {
			p_err("get map by id (%u): %s", id, strerror(errno));
			return -1;
		}
		return 1;
	} else if (is_prefix(**argv, "name")) {
		char *name;

		NEXT_ARGP();

		name = **argv;
		if (strlen(name) > BPF_OBJ_NAME_LEN - 1) {
			p_err("can't parse name");
			return -1;
		}
		NEXT_ARGP();

		return map_fd_by_name(name, fds);
	} else if (is_prefix(**argv, "pinned")) {
		char *path;

		NEXT_ARGP();

		path = **argv;
		NEXT_ARGP();

		(*fds)[0] = open_obj_pinned_any(path, BPF_OBJ_MAP);
		if ((*fds)[0] < 0)
			return -1;
		return 1;
	}

	p_err("expected 'id', 'name' or 'pinned', got: '%s'?", **argv);
	return -1;
}

int map_parse_fd(int *argc, char ***argv)
{
	int *fds = NULL;
	int nb_fds, fd;

	fds = malloc(sizeof(int));
	if (!fds) {
		p_err("mem alloc failed");
		return -1;
	}
	nb_fds = map_parse_fds(argc, argv, &fds);
	if (nb_fds != 1) {
		if (nb_fds > 1) {
			p_err("several maps match this handle");
			while (nb_fds--)
				close(fds[nb_fds]);
		}
		fd = -1;
		goto exit_free;
	}

	fd = fds[0];
exit_free:
	free(fds);
	return fd;
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
	__u32 value_id;
	int ret;

	/* start of key-value pair */
	jsonw_start_object(d->jw);

	if (map_info->btf_key_type_id) {
		jsonw_name(d->jw, "key");

		ret = btf_dumper_type(d, map_info->btf_key_type_id, key);
		if (ret)
			goto err_end_obj;
	}

	value_id = map_info->btf_vmlinux_value_type_id ?
		: map_info->btf_value_type_id;

	if (!map_is_per_cpu(map_info->type)) {
		jsonw_name(d->jw, "value");
		ret = btf_dumper_type(d, value_id, value);
	} else {
		unsigned int i, n, step;

		jsonw_name(d->jw, "values");
		jsonw_start_array(d->jw);
		n = get_possible_cpus();
		step = round_up(map_info->value_size, 8);
		for (i = 0; i < n; i++) {
			jsonw_start_object(d->jw);
			jsonw_int_field(d->jw, "cpu", i);
			jsonw_name(d->jw, "value");
			ret = btf_dumper_type(d, value_id, value + i * step);
			jsonw_end_object(d->jw);
			if (ret)
				break;
		}
		jsonw_end_array(d->jw);
	}

err_end_obj:
	/* end of key-value pair */
	jsonw_end_object(d->jw);

	return ret;
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
		if (btf) {
			struct btf_dumper d = {
				.btf = btf,
				.jw = json_wtr,
				.is_plain_text = false,
			};

			jsonw_name(json_wtr, "formatted");
			do_dump_btf(&d, info, key, value);
		}
	}

	jsonw_end_object(json_wtr);
}

static void print_entry_error(struct bpf_map_info *info, unsigned char *key,
			      const char *error_msg)
{
	int msg_size = strlen(error_msg);
	bool single_line, break_names;

	break_names = info->key_size > 16 || msg_size > 16;
	single_line = info->key_size + msg_size <= 24 && !break_names;

	printf("key:%c", break_names ? '\n' : ' ');
	fprint_hex(stdout, key, info->key_size, " ");

	printf(single_line ? "  " : "\n");

	printf("value:%c%s", break_names ? '\n' : ' ', error_msg);

	printf("\n");
}

static void print_entry_plain(struct bpf_map_info *info, unsigned char *key,
			      unsigned char *value)
{
	if (!map_is_per_cpu(info->type)) {
		bool single_line, break_names;

		break_names = info->key_size > 16 || info->value_size > 16;
		single_line = info->key_size + info->value_size <= 24 &&
			!break_names;

		if (info->key_size) {
			printf("key:%c", break_names ? '\n' : ' ');
			fprint_hex(stdout, key, info->key_size, " ");

			printf(single_line ? "  " : "\n");
		}

		if (info->value_size) {
			printf("value:%c", break_names ? '\n' : ' ');
			fprint_hex(stdout, value, info->value_size, " ");
		}

		printf("\n");
	} else {
		unsigned int i, n, step;

		n = get_possible_cpus();
		step = round_up(info->value_size, 8);

		if (info->key_size) {
			printf("key:\n");
			fprint_hex(stdout, key, info->key_size, " ");
			printf("\n");
		}
		if (info->value_size) {
			for (i = 0; i < n; i++) {
				printf("value (CPU %02d):%c",
				       i, info->value_size > 16 ? '\n' : ' ');
				fprint_hex(stdout, value + i * step,
					   info->value_size, " ");
				printf("\n");
			}
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
			if (is_prefix(*argv, "id"))
				p_info("Warning: updating program array via MAP_ID, make sure this map is kept open\n"
				       "         by some process or pinned otherwise update will be lost");

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

static void show_map_header_json(struct bpf_map_info *info, json_writer_t *wtr)
{
	jsonw_uint_field(wtr, "id", info->id);
	if (info->type < ARRAY_SIZE(map_type_name))
		jsonw_string_field(wtr, "type", map_type_name[info->type]);
	else
		jsonw_uint_field(wtr, "type", info->type);

	if (*info->name)
		jsonw_string_field(wtr, "name", info->name);

	jsonw_name(wtr, "flags");
	jsonw_printf(wtr, "%d", info->map_flags);
}

static int show_map_close_json(int fd, struct bpf_map_info *info)
{
	char *memlock, *frozen_str;
	int frozen = 0;

	memlock = get_fdinfo(fd, "memlock");
	frozen_str = get_fdinfo(fd, "frozen");

	jsonw_start_object(json_wtr);

	show_map_header_json(info, json_wtr);

	print_dev_json(info->ifindex, info->netns_dev, info->netns_ino);

	jsonw_uint_field(json_wtr, "bytes_key", info->key_size);
	jsonw_uint_field(json_wtr, "bytes_value", info->value_size);
	jsonw_uint_field(json_wtr, "max_entries", info->max_entries);

	if (memlock)
		jsonw_int_field(json_wtr, "bytes_memlock", atoi(memlock));
	free(memlock);

	if (info->type == BPF_MAP_TYPE_PROG_ARRAY) {
		char *owner_prog_type = get_fdinfo(fd, "owner_prog_type");
		char *owner_jited = get_fdinfo(fd, "owner_jited");

		if (owner_prog_type) {
			unsigned int prog_type = atoi(owner_prog_type);

			if (prog_type < ARRAY_SIZE(prog_type_name))
				jsonw_string_field(json_wtr, "owner_prog_type",
						   prog_type_name[prog_type]);
			else
				jsonw_uint_field(json_wtr, "owner_prog_type",
						 prog_type);
		}
		if (owner_jited)
			jsonw_bool_field(json_wtr, "owner_jited",
					 !!atoi(owner_jited));

		free(owner_prog_type);
		free(owner_jited);
	}
	close(fd);

	if (frozen_str) {
		frozen = atoi(frozen_str);
		free(frozen_str);
	}
	jsonw_int_field(json_wtr, "frozen", frozen);

	if (info->btf_id)
		jsonw_int_field(json_wtr, "btf_id", info->btf_id);

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

static void show_map_header_plain(struct bpf_map_info *info)
{
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
}

static int show_map_close_plain(int fd, struct bpf_map_info *info)
{
	char *memlock, *frozen_str;
	int frozen = 0;

	memlock = get_fdinfo(fd, "memlock");
	frozen_str = get_fdinfo(fd, "frozen");

	show_map_header_plain(info);
	printf("\tkey %uB  value %uB  max_entries %u",
	       info->key_size, info->value_size, info->max_entries);

	if (memlock)
		printf("  memlock %sB", memlock);
	free(memlock);

	if (info->type == BPF_MAP_TYPE_PROG_ARRAY) {
		char *owner_prog_type = get_fdinfo(fd, "owner_prog_type");
		char *owner_jited = get_fdinfo(fd, "owner_jited");

		if (owner_prog_type || owner_jited)
			printf("\n\t");
		if (owner_prog_type) {
			unsigned int prog_type = atoi(owner_prog_type);

			if (prog_type < ARRAY_SIZE(prog_type_name))
				printf("owner_prog_type %s  ",
				       prog_type_name[prog_type]);
			else
				printf("owner_prog_type %d  ", prog_type);
		}
		if (owner_jited)
			printf("owner%s jited",
			       atoi(owner_jited) ? "" : " not");

		free(owner_prog_type);
		free(owner_jited);
	}
	close(fd);

	if (!hash_empty(map_table.table)) {
		struct pinned_obj *obj;

		hash_for_each_possible(map_table.table, obj, hash, info->id) {
			if (obj->id == info->id)
				printf("\n\tpinned %s", obj->path);
		}
	}
	printf("\n");

	if (frozen_str) {
		frozen = atoi(frozen_str);
		free(frozen_str);
	}

	if (!info->btf_id && !frozen)
		return 0;

	printf("\t");

	if (info->btf_id)
		printf("btf_id %d", info->btf_id);

	if (frozen)
		printf("%sfrozen", info->btf_id ? "  " : "");

	printf("\n");
	return 0;
}

static int do_show_subset(int argc, char **argv)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	int *fds = NULL;
	int nb_fds, i;
	int err = -1;

	fds = malloc(sizeof(int));
	if (!fds) {
		p_err("mem alloc failed");
		return -1;
	}
	nb_fds = map_parse_fds(&argc, &argv, &fds);
	if (nb_fds < 1)
		goto exit_free;

	if (json_output && nb_fds > 1)
		jsonw_start_array(json_wtr);	/* root array */
	for (i = 0; i < nb_fds; i++) {
		err = bpf_obj_get_info_by_fd(fds[i], &info, &len);
		if (err) {
			p_err("can't get map info: %s",
			      strerror(errno));
			for (; i < nb_fds; i++)
				close(fds[i]);
			break;
		}

		if (json_output)
			show_map_close_json(fds[i], &info);
		else
			show_map_close_plain(fds[i], &info);

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
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	__u32 id = 0;
	int err;
	int fd;

	if (show_pinned)
		build_pinned_obj_table(&map_table, BPF_OBJ_MAP);

	if (argc == 2)
		return do_show_subset(argc, argv);

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

static int dump_map_elem(int fd, void *key, void *value,
			 struct bpf_map_info *map_info, struct btf *btf,
			 json_writer_t *btf_wtr)
{
	int num_elems = 0;
	int lookup_errno;

	if (!bpf_map_lookup_elem(fd, key, value)) {
		if (json_output) {
			print_entry_json(map_info, key, value, btf);
		} else {
			if (btf) {
				struct btf_dumper d = {
					.btf = btf,
					.jw = btf_wtr,
					.is_plain_text = true,
				};

				do_dump_btf(&d, map_info, key, value);
			} else {
				print_entry_plain(map_info, key, value);
			}
			num_elems++;
		}
		return num_elems;
	}

	/* lookup error handling */
	lookup_errno = errno;

	if (map_is_map_of_maps(map_info->type) ||
	    map_is_map_of_progs(map_info->type))
		return 0;

	if (json_output) {
		jsonw_start_object(json_wtr);
		jsonw_name(json_wtr, "key");
		print_hex_data_json(key, map_info->key_size);
		jsonw_name(json_wtr, "value");
		jsonw_start_object(json_wtr);
		jsonw_string_field(json_wtr, "error", strerror(lookup_errno));
		jsonw_end_object(json_wtr);
		jsonw_end_object(json_wtr);
	} else {
		const char *msg = NULL;

		if (lookup_errno == ENOENT)
			msg = "<no entry>";
		else if (lookup_errno == ENOSPC &&
			 map_info->type == BPF_MAP_TYPE_REUSEPORT_SOCKARRAY)
			msg = "<cannot read>";

		print_entry_error(map_info, key,
				  msg ? : strerror(lookup_errno));
	}

	return 0;
}

static int maps_have_btf(int *fds, int nb_fds)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	int err, i;

	for (i = 0; i < nb_fds; i++) {
		err = bpf_obj_get_info_by_fd(fds[i], &info, &len);
		if (err) {
			p_err("can't get map info: %s", strerror(errno));
			return -1;
		}

		if (!info.btf_id)
			return 0;
	}

	return 1;
}

static struct btf *btf_vmlinux;

static struct btf *get_map_kv_btf(const struct bpf_map_info *info)
{
	struct btf *btf = NULL;

	if (info->btf_vmlinux_value_type_id) {
		if (!btf_vmlinux) {
			btf_vmlinux = libbpf_find_kernel_btf();
			if (IS_ERR(btf_vmlinux))
				p_err("failed to get kernel btf");
		}
		return btf_vmlinux;
	} else if (info->btf_value_type_id) {
		int err;

		err = btf__get_from_id(info->btf_id, &btf);
		if (err || !btf) {
			p_err("failed to get btf");
			btf = err ? ERR_PTR(err) : ERR_PTR(-ESRCH);
		}
	}

	return btf;
}

static void free_map_kv_btf(struct btf *btf)
{
	if (!IS_ERR(btf) && btf != btf_vmlinux)
		btf__free(btf);
}

static void free_btf_vmlinux(void)
{
	if (!IS_ERR(btf_vmlinux))
		btf__free(btf_vmlinux);
}

static int
map_dump(int fd, struct bpf_map_info *info, json_writer_t *wtr,
	 bool show_header)
{
	void *key, *value, *prev_key;
	unsigned int num_elems = 0;
	struct btf *btf = NULL;
	int err;

	key = malloc(info->key_size);
	value = alloc_value(info);
	if (!key || !value) {
		p_err("mem alloc failed");
		err = -1;
		goto exit_free;
	}

	prev_key = NULL;

	if (wtr) {
		btf = get_map_kv_btf(info);
		if (IS_ERR(btf)) {
			err = PTR_ERR(btf);
			goto exit_free;
		}

		if (show_header) {
			jsonw_start_object(wtr);	/* map object */
			show_map_header_json(info, wtr);
			jsonw_name(wtr, "elements");
		}
		jsonw_start_array(wtr);		/* elements */
	} else if (show_header) {
		show_map_header_plain(info);
	}

	if (info->type == BPF_MAP_TYPE_REUSEPORT_SOCKARRAY &&
	    info->value_size != 8)
		p_info("Warning: cannot read values from %s map with value_size != 8",
		       map_type_name[info->type]);
	while (true) {
		err = bpf_map_get_next_key(fd, prev_key, key);
		if (err) {
			if (errno == ENOENT)
				err = 0;
			break;
		}
		num_elems += dump_map_elem(fd, key, value, info, btf, wtr);
		prev_key = key;
	}

	if (wtr) {
		jsonw_end_array(wtr);	/* elements */
		if (show_header)
			jsonw_end_object(wtr);	/* map object */
	} else {
		printf("Found %u element%s\n", num_elems,
		       num_elems != 1 ? "s" : "");
	}

exit_free:
	free(key);
	free(value);
	close(fd);
	free_map_kv_btf(btf);

	return err;
}

static int do_dump(int argc, char **argv)
{
	json_writer_t *wtr = NULL, *btf_wtr = NULL;
	struct bpf_map_info info = {};
	int nb_fds, i = 0;
	__u32 len = sizeof(info);
	int *fds = NULL;
	int err = -1;

	if (argc != 2)
		usage();

	fds = malloc(sizeof(int));
	if (!fds) {
		p_err("mem alloc failed");
		return -1;
	}
	nb_fds = map_parse_fds(&argc, &argv, &fds);
	if (nb_fds < 1)
		goto exit_free;

	if (json_output) {
		wtr = json_wtr;
	} else {
		int do_plain_btf;

		do_plain_btf = maps_have_btf(fds, nb_fds);
		if (do_plain_btf < 0)
			goto exit_close;

		if (do_plain_btf) {
			btf_wtr = get_btf_writer();
			wtr = btf_wtr;
			if (!btf_wtr)
				p_info("failed to create json writer for btf. falling back to plain output");
		}
	}

	if (wtr && nb_fds > 1)
		jsonw_start_array(wtr);	/* root array */
	for (i = 0; i < nb_fds; i++) {
		if (bpf_obj_get_info_by_fd(fds[i], &info, &len)) {
			p_err("can't get map info: %s", strerror(errno));
			break;
		}
		err = map_dump(fds[i], &info, wtr, nb_fds > 1);
		if (!wtr && i != nb_fds - 1)
			printf("\n");

		if (err)
			break;
		close(fds[i]);
	}
	if (wtr && nb_fds > 1)
		jsonw_end_array(wtr);	/* root array */

	if (btf_wtr)
		jsonw_destroy(&btf_wtr);
exit_close:
	for (; i < nb_fds; i++)
		close(fds[i]);
exit_free:
	free(fds);
	free_btf_vmlinux();
	return err;
}

static int alloc_key_value(struct bpf_map_info *info, void **key, void **value)
{
	*key = NULL;
	*value = NULL;

	if (info->key_size) {
		*key = malloc(info->key_size);
		if (!*key) {
			p_err("key mem alloc failed");
			return -1;
		}
	}

	if (info->value_size) {
		*value = alloc_value(info);
		if (!*value) {
			p_err("value mem alloc failed");
			free(*key);
			*key = NULL;
			return -1;
		}
	}

	return 0;
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

	err = alloc_key_value(&info, &key, &value);
	if (err)
		goto exit_free;

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

static void print_key_value(struct bpf_map_info *info, void *key,
			    void *value)
{
	json_writer_t *btf_wtr;
	struct btf *btf = NULL;
	int err;

	err = btf__get_from_id(info->btf_id, &btf);
	if (err) {
		p_err("failed to get btf");
		return;
	}

	if (json_output) {
		print_entry_json(info, key, value, btf);
	} else if (btf) {
		/* if here json_wtr wouldn't have been initialised,
		 * so let's create separate writer for btf
		 */
		btf_wtr = get_btf_writer();
		if (!btf_wtr) {
			p_info("failed to create json writer for btf. falling back to plain output");
			btf__free(btf);
			btf = NULL;
			print_entry_plain(info, key, value);
		} else {
			struct btf_dumper d = {
				.btf = btf,
				.jw = btf_wtr,
				.is_plain_text = true,
			};

			do_dump_btf(&d, info, key, value);
			jsonw_destroy(&btf_wtr);
		}
	} else {
		print_entry_plain(info, key, value);
	}
	btf__free(btf);
}

static int do_lookup(int argc, char **argv)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	void *key, *value;
	int err;
	int fd;

	if (argc < 2)
		usage();

	fd = map_parse_fd_and_info(&argc, &argv, &info, &len);
	if (fd < 0)
		return -1;

	err = alloc_key_value(&info, &key, &value);
	if (err)
		goto exit_free;

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
	print_key_value(&info, key, value);

exit_free:
	free(key);
	free(value);
	close(fd);

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

	err = do_pin_any(argc, argv, map_parse_fd);
	if (!err && json_output)
		jsonw_null(json_wtr);
	return err;
}

static int do_create(int argc, char **argv)
{
	struct bpf_create_map_attr attr = { NULL, };
	const char *pinfile;
	int err, fd;

	if (!REQ_ARGS(7))
		return -1;
	pinfile = GET_ARG();

	while (argc) {
		if (!REQ_ARGS(2))
			return -1;

		if (is_prefix(*argv, "type")) {
			NEXT_ARG();

			if (attr.map_type) {
				p_err("map type already specified");
				return -1;
			}

			attr.map_type = map_type_from_str(*argv);
			if ((int)attr.map_type < 0) {
				p_err("unrecognized map type: %s", *argv);
				return -1;
			}
			NEXT_ARG();
		} else if (is_prefix(*argv, "name")) {
			NEXT_ARG();
			attr.name = GET_ARG();
		} else if (is_prefix(*argv, "key")) {
			if (parse_u32_arg(&argc, &argv, &attr.key_size,
					  "key size"))
				return -1;
		} else if (is_prefix(*argv, "value")) {
			if (parse_u32_arg(&argc, &argv, &attr.value_size,
					  "value size"))
				return -1;
		} else if (is_prefix(*argv, "entries")) {
			if (parse_u32_arg(&argc, &argv, &attr.max_entries,
					  "max entries"))
				return -1;
		} else if (is_prefix(*argv, "flags")) {
			if (parse_u32_arg(&argc, &argv, &attr.map_flags,
					  "flags"))
				return -1;
		} else if (is_prefix(*argv, "dev")) {
			NEXT_ARG();

			if (attr.map_ifindex) {
				p_err("offload device already specified");
				return -1;
			}

			attr.map_ifindex = if_nametoindex(*argv);
			if (!attr.map_ifindex) {
				p_err("unrecognized netdevice '%s': %s",
				      *argv, strerror(errno));
				return -1;
			}
			NEXT_ARG();
		} else {
			p_err("unknown arg %s", *argv);
			return -1;
		}
	}

	if (!attr.name) {
		p_err("map name not specified");
		return -1;
	}

	set_max_rlimit();

	fd = bpf_create_map_xattr(&attr);
	if (fd < 0) {
		p_err("map create failed: %s", strerror(errno));
		return -1;
	}

	err = do_pin_fd(fd, pinfile);
	close(fd);
	if (err)
		return err;

	if (json_output)
		jsonw_null(json_wtr);
	return 0;
}

static int do_pop_dequeue(int argc, char **argv)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	void *key, *value;
	int err;
	int fd;

	if (argc < 2)
		usage();

	fd = map_parse_fd_and_info(&argc, &argv, &info, &len);
	if (fd < 0)
		return -1;

	err = alloc_key_value(&info, &key, &value);
	if (err)
		goto exit_free;

	err = bpf_map_lookup_and_delete_elem(fd, key, value);
	if (err) {
		if (errno == ENOENT) {
			if (json_output)
				jsonw_null(json_wtr);
			else
				printf("Error: empty map\n");
		} else {
			p_err("pop failed: %s", strerror(errno));
		}

		goto exit_free;
	}

	print_key_value(&info, key, value);

exit_free:
	free(key);
	free(value);
	close(fd);

	return err;
}

static int do_freeze(int argc, char **argv)
{
	int err, fd;

	if (!REQ_ARGS(2))
		return -1;

	fd = map_parse_fd(&argc, &argv);
	if (fd < 0)
		return -1;

	if (argc) {
		close(fd);
		return BAD_ARG();
	}

	err = bpf_map_freeze(fd);
	close(fd);
	if (err) {
		p_err("failed to freeze map: %s", strerror(errno));
		return err;
	}

	if (json_output)
		jsonw_null(json_wtr);

	return 0;
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s %s { show | list }   [MAP]\n"
		"       %s %s create     FILE type TYPE key KEY_SIZE value VALUE_SIZE \\\n"
		"                              entries MAX_ENTRIES name NAME [flags FLAGS] \\\n"
		"                              [dev NAME]\n"
		"       %s %s dump       MAP\n"
		"       %s %s update     MAP [key DATA] [value VALUE] [UPDATE_FLAGS]\n"
		"       %s %s lookup     MAP [key DATA]\n"
		"       %s %s getnext    MAP [key DATA]\n"
		"       %s %s delete     MAP  key DATA\n"
		"       %s %s pin        MAP  FILE\n"
		"       %s %s event_pipe MAP [cpu N index M]\n"
		"       %s %s peek       MAP\n"
		"       %s %s push       MAP value VALUE\n"
		"       %s %s pop        MAP\n"
		"       %s %s enqueue    MAP value VALUE\n"
		"       %s %s dequeue    MAP\n"
		"       %s %s freeze     MAP\n"
		"       %s %s help\n"
		"\n"
		"       " HELP_SPEC_MAP "\n"
		"       DATA := { [hex] BYTES }\n"
		"       " HELP_SPEC_PROGRAM "\n"
		"       VALUE := { DATA | MAP | PROG }\n"
		"       UPDATE_FLAGS := { any | exist | noexist }\n"
		"       TYPE := { hash | array | prog_array | perf_event_array | percpu_hash |\n"
		"                 percpu_array | stack_trace | cgroup_array | lru_hash |\n"
		"                 lru_percpu_hash | lpm_trie | array_of_maps | hash_of_maps |\n"
		"                 devmap | devmap_hash | sockmap | cpumap | xskmap | sockhash |\n"
		"                 cgroup_storage | reuseport_sockarray | percpu_cgroup_storage }\n"
		"       " HELP_SPEC_OPTIONS "\n"
		"",
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2], bin_name, argv[-2], bin_name, argv[-2],
		bin_name, argv[-2]);

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
	{ "create",	do_create },
	{ "peek",	do_lookup },
	{ "push",	do_update },
	{ "enqueue",	do_update },
	{ "pop",	do_pop_dequeue },
	{ "dequeue",	do_pop_dequeue },
	{ "freeze",	do_freeze },
	{ 0 }
};

int do_map(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}

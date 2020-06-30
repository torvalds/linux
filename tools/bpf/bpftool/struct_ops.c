// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2020 Facebook */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <linux/err.h>

#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>

#include "json_writer.h"
#include "main.h"

#define STRUCT_OPS_VALUE_PREFIX "bpf_struct_ops_"

static const struct btf_type *map_info_type;
static __u32 map_info_alloc_len;
static struct btf *btf_vmlinux;
static __s32 map_info_type_id;

struct res {
	unsigned int nr_maps;
	unsigned int nr_errs;
};

static const struct btf *get_btf_vmlinux(void)
{
	if (btf_vmlinux)
		return btf_vmlinux;

	btf_vmlinux = libbpf_find_kernel_btf();
	if (IS_ERR(btf_vmlinux))
		p_err("struct_ops requires kernel CONFIG_DEBUG_INFO_BTF=y");

	return btf_vmlinux;
}

static const char *get_kern_struct_ops_name(const struct bpf_map_info *info)
{
	const struct btf *kern_btf;
	const struct btf_type *t;
	const char *st_ops_name;

	kern_btf = get_btf_vmlinux();
	if (IS_ERR(kern_btf))
		return "<btf_vmlinux_not_found>";

	t = btf__type_by_id(kern_btf, info->btf_vmlinux_value_type_id);
	st_ops_name = btf__name_by_offset(kern_btf, t->name_off);
	st_ops_name += strlen(STRUCT_OPS_VALUE_PREFIX);

	return st_ops_name;
}

static __s32 get_map_info_type_id(void)
{
	const struct btf *kern_btf;

	if (map_info_type_id)
		return map_info_type_id;

	kern_btf = get_btf_vmlinux();
	if (IS_ERR(kern_btf)) {
		map_info_type_id = PTR_ERR(kern_btf);
		return map_info_type_id;
	}

	map_info_type_id = btf__find_by_name_kind(kern_btf, "bpf_map_info",
						  BTF_KIND_STRUCT);
	if (map_info_type_id < 0) {
		p_err("can't find bpf_map_info from btf_vmlinux");
		return map_info_type_id;
	}
	map_info_type = btf__type_by_id(kern_btf, map_info_type_id);

	/* Ensure map_info_alloc() has at least what the bpftool needs */
	map_info_alloc_len = map_info_type->size;
	if (map_info_alloc_len < sizeof(struct bpf_map_info))
		map_info_alloc_len = sizeof(struct bpf_map_info);

	return map_info_type_id;
}

/* If the subcmd needs to print out the bpf_map_info,
 * it should always call map_info_alloc to allocate
 * a bpf_map_info object instead of allocating it
 * on the stack.
 *
 * map_info_alloc() will take the running kernel's btf
 * into account.  i.e. it will consider the
 * sizeof(struct bpf_map_info) of the running kernel.
 *
 * It will enable the "struct_ops" cmd to print the latest
 * "struct bpf_map_info".
 *
 * [ Recall that "struct_ops" requires the kernel's btf to
 *   be available ]
 */
static struct bpf_map_info *map_info_alloc(__u32 *alloc_len)
{
	struct bpf_map_info *info;

	if (get_map_info_type_id() < 0)
		return NULL;

	info = calloc(1, map_info_alloc_len);
	if (!info)
		p_err("mem alloc failed");
	else
		*alloc_len = map_info_alloc_len;

	return info;
}

/* It iterates all struct_ops maps of the system.
 * It returns the fd in "*res_fd" and map_info in "*info".
 * In the very first iteration, info->id should be 0.
 * An optional map "*name" filter can be specified.
 * The filter can be made more flexible in the future.
 * e.g. filter by kernel-struct-ops-name, regex-name, glob-name, ...etc.
 *
 * Return value:
 *     1: A struct_ops map found.  It is returned in "*res_fd" and "*info".
 *	  The caller can continue to call get_next in the future.
 *     0: No struct_ops map is returned.
 *        All struct_ops map has been found.
 *    -1: Error and the caller should abort the iteration.
 */
static int get_next_struct_ops_map(const char *name, int *res_fd,
				   struct bpf_map_info *info, __u32 info_len)
{
	__u32 id = info->id;
	int err, fd;

	while (true) {
		err = bpf_map_get_next_id(id, &id);
		if (err) {
			if (errno == ENOENT)
				return 0;
			p_err("can't get next map: %s", strerror(errno));
			return -1;
		}

		fd = bpf_map_get_fd_by_id(id);
		if (fd < 0) {
			if (errno == ENOENT)
				continue;
			p_err("can't get map by id (%u): %s",
			      id, strerror(errno));
			return -1;
		}

		err = bpf_obj_get_info_by_fd(fd, info, &info_len);
		if (err) {
			p_err("can't get map info: %s", strerror(errno));
			close(fd);
			return -1;
		}

		if (info->type == BPF_MAP_TYPE_STRUCT_OPS &&
		    (!name || !strcmp(name, info->name))) {
			*res_fd = fd;
			return 1;
		}
		close(fd);
	}
}

static int cmd_retval(const struct res *res, bool must_have_one_map)
{
	if (res->nr_errs || (!res->nr_maps && must_have_one_map))
		return -1;

	return 0;
}

/* "data" is the work_func private storage */
typedef int (*work_func)(int fd, const struct bpf_map_info *info, void *data,
			 struct json_writer *wtr);

/* Find all struct_ops map in the system.
 * Filter out by "name" (if specified).
 * Then call "func(fd, info, data, wtr)" on each struct_ops map found.
 */
static struct res do_search(const char *name, work_func func, void *data,
			    struct json_writer *wtr)
{
	struct bpf_map_info *info;
	struct res res = {};
	__u32 info_len;
	int fd, err;

	info = map_info_alloc(&info_len);
	if (!info) {
		res.nr_errs++;
		return res;
	}

	if (wtr)
		jsonw_start_array(wtr);
	while ((err = get_next_struct_ops_map(name, &fd, info, info_len)) == 1) {
		res.nr_maps++;
		err = func(fd, info, data, wtr);
		if (err)
			res.nr_errs++;
		close(fd);
	}
	if (wtr)
		jsonw_end_array(wtr);

	if (err)
		res.nr_errs++;

	if (!wtr && name && !res.nr_errs && !res.nr_maps)
		/* It is not printing empty [].
		 * Thus, needs to specifically say nothing found
		 * for "name" here.
		 */
		p_err("no struct_ops found for %s", name);
	else if (!wtr && json_output && !res.nr_errs)
		/* The "func()" above is not writing any json (i.e. !wtr
		 * test here).
		 *
		 * However, "-j" is enabled and there is no errs here,
		 * so call json_null() as the current convention of
		 * other cmds.
		 */
		jsonw_null(json_wtr);

	free(info);
	return res;
}

static struct res do_one_id(const char *id_str, work_func func, void *data,
			    struct json_writer *wtr)
{
	struct bpf_map_info *info;
	struct res res = {};
	unsigned long id;
	__u32 info_len;
	char *endptr;
	int fd;

	id = strtoul(id_str, &endptr, 0);
	if (*endptr || !id || id > UINT32_MAX) {
		p_err("invalid id %s", id_str);
		res.nr_errs++;
		return res;
	}

	fd = bpf_map_get_fd_by_id(id);
	if (fd == -1) {
		p_err("can't get map by id (%lu): %s", id, strerror(errno));
		res.nr_errs++;
		return res;
	}

	info = map_info_alloc(&info_len);
	if (!info) {
		res.nr_errs++;
		goto done;
	}

	if (bpf_obj_get_info_by_fd(fd, info, &info_len)) {
		p_err("can't get map info: %s", strerror(errno));
		res.nr_errs++;
		goto done;
	}

	if (info->type != BPF_MAP_TYPE_STRUCT_OPS) {
		p_err("%s id %u is not a struct_ops map", info->name, info->id);
		res.nr_errs++;
		goto done;
	}

	res.nr_maps++;

	if (func(fd, info, data, wtr))
		res.nr_errs++;
	else if (!wtr && json_output)
		/* The "func()" above is not writing any json (i.e. !wtr
		 * test here).
		 *
		 * However, "-j" is enabled and there is no errs here,
		 * so call json_null() as the current convention of
		 * other cmds.
		 */
		jsonw_null(json_wtr);

done:
	free(info);
	close(fd);

	return res;
}

static struct res do_work_on_struct_ops(const char *search_type,
					const char *search_term,
					work_func func, void *data,
					struct json_writer *wtr)
{
	if (search_type) {
		if (is_prefix(search_type, "id"))
			return do_one_id(search_term, func, data, wtr);
		else if (!is_prefix(search_type, "name"))
			usage();
	}

	return do_search(search_term, func, data, wtr);
}

static int __do_show(int fd, const struct bpf_map_info *info, void *data,
		     struct json_writer *wtr)
{
	if (wtr) {
		jsonw_start_object(wtr);
		jsonw_uint_field(wtr, "id", info->id);
		jsonw_string_field(wtr, "name", info->name);
		jsonw_string_field(wtr, "kernel_struct_ops",
				   get_kern_struct_ops_name(info));
		jsonw_end_object(wtr);
	} else {
		printf("%u: %-15s %-32s\n", info->id, info->name,
		       get_kern_struct_ops_name(info));
	}

	return 0;
}

static int do_show(int argc, char **argv)
{
	const char *search_type = NULL, *search_term = NULL;
	struct res res;

	if (argc && argc != 2)
		usage();

	if (argc == 2) {
		search_type = GET_ARG();
		search_term = GET_ARG();
	}

	res = do_work_on_struct_ops(search_type, search_term, __do_show,
				    NULL, json_wtr);

	return cmd_retval(&res, !!search_term);
}

static int __do_dump(int fd, const struct bpf_map_info *info, void *data,
		     struct json_writer *wtr)
{
	struct btf_dumper *d = (struct btf_dumper *)data;
	const struct btf_type *struct_ops_type;
	const struct btf *kern_btf = d->btf;
	const char *struct_ops_name;
	int zero = 0;
	void *value;

	/* note: d->jw == wtr */

	kern_btf = d->btf;

	/* The kernel supporting BPF_MAP_TYPE_STRUCT_OPS must have
	 * btf_vmlinux_value_type_id.
	 */
	struct_ops_type = btf__type_by_id(kern_btf,
					  info->btf_vmlinux_value_type_id);
	struct_ops_name = btf__name_by_offset(kern_btf,
					      struct_ops_type->name_off);
	value = calloc(1, info->value_size);
	if (!value) {
		p_err("mem alloc failed");
		return -1;
	}

	if (bpf_map_lookup_elem(fd, &zero, value)) {
		p_err("can't lookup struct_ops map %s id %u",
		      info->name, info->id);
		free(value);
		return -1;
	}

	jsonw_start_object(wtr);
	jsonw_name(wtr, "bpf_map_info");
	btf_dumper_type(d, map_info_type_id, (void *)info);
	jsonw_end_object(wtr);

	jsonw_start_object(wtr);
	jsonw_name(wtr, struct_ops_name);
	btf_dumper_type(d, info->btf_vmlinux_value_type_id, value);
	jsonw_end_object(wtr);

	free(value);

	return 0;
}

static int do_dump(int argc, char **argv)
{
	const char *search_type = NULL, *search_term = NULL;
	json_writer_t *wtr = json_wtr;
	const struct btf *kern_btf;
	struct btf_dumper d = {};
	struct res res;

	if (argc && argc != 2)
		usage();

	if (argc == 2) {
		search_type = GET_ARG();
		search_term = GET_ARG();
	}

	kern_btf = get_btf_vmlinux();
	if (IS_ERR(kern_btf))
		return -1;

	if (!json_output) {
		wtr = jsonw_new(stdout);
		if (!wtr) {
			p_err("can't create json writer");
			return -1;
		}
		jsonw_pretty(wtr, true);
	}

	d.btf = kern_btf;
	d.jw = wtr;
	d.is_plain_text = !json_output;
	d.prog_id_as_func_ptr = true;

	res = do_work_on_struct_ops(search_type, search_term, __do_dump, &d,
				    wtr);

	if (!json_output)
		jsonw_destroy(&wtr);

	return cmd_retval(&res, !!search_term);
}

static int __do_unregister(int fd, const struct bpf_map_info *info, void *data,
			   struct json_writer *wtr)
{
	int zero = 0;

	if (bpf_map_delete_elem(fd, &zero)) {
		p_err("can't unload %s %s id %u: %s",
		      get_kern_struct_ops_name(info), info->name,
		      info->id, strerror(errno));
		return -1;
	}

	p_info("Unregistered %s %s id %u",
	       get_kern_struct_ops_name(info), info->name,
	       info->id);

	return 0;
}

static int do_unregister(int argc, char **argv)
{
	const char *search_type, *search_term;
	struct res res;

	if (argc != 2)
		usage();

	search_type = GET_ARG();
	search_term = GET_ARG();

	res = do_work_on_struct_ops(search_type, search_term,
				    __do_unregister, NULL, NULL);

	return cmd_retval(&res, true);
}

static int do_register(int argc, char **argv)
{
	struct bpf_object_load_attr load_attr = {};
	const struct bpf_map_def *def;
	struct bpf_map_info info = {};
	__u32 info_len = sizeof(info);
	int nr_errs = 0, nr_maps = 0;
	struct bpf_object *obj;
	struct bpf_link *link;
	struct bpf_map *map;
	const char *file;

	if (argc != 1)
		usage();

	file = GET_ARG();

	obj = bpf_object__open(file);
	if (IS_ERR_OR_NULL(obj))
		return -1;

	set_max_rlimit();

	load_attr.obj = obj;
	if (verifier_logs)
		/* log_level1 + log_level2 + stats, but not stable UAPI */
		load_attr.log_level = 1 + 2 + 4;

	if (bpf_object__load_xattr(&load_attr)) {
		bpf_object__close(obj);
		return -1;
	}

	bpf_object__for_each_map(map, obj) {
		def = bpf_map__def(map);
		if (def->type != BPF_MAP_TYPE_STRUCT_OPS)
			continue;

		link = bpf_map__attach_struct_ops(map);
		if (IS_ERR(link)) {
			p_err("can't register struct_ops %s: %s",
			      bpf_map__name(map),
			      strerror(-PTR_ERR(link)));
			nr_errs++;
			continue;
		}
		nr_maps++;

		bpf_link__disconnect(link);
		bpf_link__destroy(link);

		if (!bpf_obj_get_info_by_fd(bpf_map__fd(map), &info,
					    &info_len))
			p_info("Registered %s %s id %u",
			       get_kern_struct_ops_name(&info),
			       bpf_map__name(map),
			       info.id);
		else
			/* Not p_err.  The struct_ops was attached
			 * successfully.
			 */
			p_info("Registered %s but can't find id: %s",
			       bpf_map__name(map), strerror(errno));
	}

	bpf_object__close(obj);

	if (nr_errs)
		return -1;

	if (!nr_maps) {
		p_err("no struct_ops found in %s", file);
		return -1;
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
		"Usage: %1$s %2$s { show | list } [STRUCT_OPS_MAP]\n"
		"       %1$s %2$s dump [STRUCT_OPS_MAP]\n"
		"       %1$s %2$s register OBJ\n"
		"       %1$s %2$s unregister STRUCT_OPS_MAP\n"
		"       %1$s %2$s help\n"
		"\n"
		"       OPTIONS := { {-j|--json} [{-p|--pretty}] }\n"
		"       STRUCT_OPS_MAP := [ id STRUCT_OPS_MAP_ID | name STRUCT_OPS_MAP_NAME ]\n"
		"",
		bin_name, argv[-2]);

	return 0;
}

static const struct cmd cmds[] = {
	{ "show",	do_show },
	{ "list",	do_show },
	{ "register",	do_register },
	{ "unregister",	do_unregister },
	{ "dump",	do_dump },
	{ "help",	do_help },
	{ 0 }
};

int do_struct_ops(int argc, char **argv)
{
	int err;

	err = cmd_select(cmds, argc, argv, do_help);

	if (!IS_ERR(btf_vmlinux))
		btf__free(btf_vmlinux);

	return err;
}

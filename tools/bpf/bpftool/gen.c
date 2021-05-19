// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Facebook */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/err.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <bpf/btf.h>
#include <bpf/bpf_gen_internal.h>

#include "json_writer.h"
#include "main.h"

#define MAX_OBJ_NAME_LEN 64

static void sanitize_identifier(char *name)
{
	int i;

	for (i = 0; name[i]; i++)
		if (!isalnum(name[i]) && name[i] != '_')
			name[i] = '_';
}

static bool str_has_suffix(const char *str, const char *suffix)
{
	size_t i, n1 = strlen(str), n2 = strlen(suffix);

	if (n1 < n2)
		return false;

	for (i = 0; i < n2; i++) {
		if (str[n1 - i - 1] != suffix[n2 - i - 1])
			return false;
	}

	return true;
}

static void get_obj_name(char *name, const char *file)
{
	/* Using basename() GNU version which doesn't modify arg. */
	strncpy(name, basename(file), MAX_OBJ_NAME_LEN - 1);
	name[MAX_OBJ_NAME_LEN - 1] = '\0';
	if (str_has_suffix(name, ".o"))
		name[strlen(name) - 2] = '\0';
	sanitize_identifier(name);
}

static void get_header_guard(char *guard, const char *obj_name)
{
	int i;

	sprintf(guard, "__%s_SKEL_H__", obj_name);
	for (i = 0; guard[i]; i++)
		guard[i] = toupper(guard[i]);
}

static const char *get_map_ident(const struct bpf_map *map)
{
	const char *name = bpf_map__name(map);

	if (!bpf_map__is_internal(map))
		return name;

	if (str_has_suffix(name, ".data"))
		return "data";
	else if (str_has_suffix(name, ".rodata"))
		return "rodata";
	else if (str_has_suffix(name, ".bss"))
		return "bss";
	else if (str_has_suffix(name, ".kconfig"))
		return "kconfig";
	else
		return NULL;
}

static void codegen_btf_dump_printf(void *ctx, const char *fmt, va_list args)
{
	vprintf(fmt, args);
}

static int codegen_datasec_def(struct bpf_object *obj,
			       struct btf *btf,
			       struct btf_dump *d,
			       const struct btf_type *sec,
			       const char *obj_name)
{
	const char *sec_name = btf__name_by_offset(btf, sec->name_off);
	const struct btf_var_secinfo *sec_var = btf_var_secinfos(sec);
	int i, err, off = 0, pad_cnt = 0, vlen = btf_vlen(sec);
	const char *sec_ident;
	char var_ident[256];
	bool strip_mods = false;

	if (strcmp(sec_name, ".data") == 0) {
		sec_ident = "data";
		strip_mods = true;
	} else if (strcmp(sec_name, ".bss") == 0) {
		sec_ident = "bss";
		strip_mods = true;
	} else if (strcmp(sec_name, ".rodata") == 0) {
		sec_ident = "rodata";
		strip_mods = true;
	} else if (strcmp(sec_name, ".kconfig") == 0) {
		sec_ident = "kconfig";
	} else {
		return 0;
	}

	printf("	struct %s__%s {\n", obj_name, sec_ident);
	for (i = 0; i < vlen; i++, sec_var++) {
		const struct btf_type *var = btf__type_by_id(btf, sec_var->type);
		const char *var_name = btf__name_by_offset(btf, var->name_off);
		DECLARE_LIBBPF_OPTS(btf_dump_emit_type_decl_opts, opts,
			.field_name = var_ident,
			.indent_level = 2,
			.strip_mods = strip_mods,
		);
		int need_off = sec_var->offset, align_off, align;
		__u32 var_type_id = var->type;

		/* static variables are not exposed through BPF skeleton */
		if (btf_var(var)->linkage == BTF_VAR_STATIC)
			continue;

		if (off > need_off) {
			p_err("Something is wrong for %s's variable #%d: need offset %d, already at %d.\n",
			      sec_name, i, need_off, off);
			return -EINVAL;
		}

		align = btf__align_of(btf, var->type);
		if (align <= 0) {
			p_err("Failed to determine alignment of variable '%s': %d",
			      var_name, align);
			return -EINVAL;
		}
		/* Assume 32-bit architectures when generating data section
		 * struct memory layout. Given bpftool can't know which target
		 * host architecture it's emitting skeleton for, we need to be
		 * conservative and assume 32-bit one to ensure enough padding
		 * bytes are generated for pointer and long types. This will
		 * still work correctly for 64-bit architectures, because in
		 * the worst case we'll generate unnecessary padding field,
		 * which on 64-bit architectures is not strictly necessary and
		 * would be handled by natural 8-byte alignment. But it still
		 * will be a correct memory layout, based on recorded offsets
		 * in BTF.
		 */
		if (align > 4)
			align = 4;

		align_off = (off + align - 1) / align * align;
		if (align_off != need_off) {
			printf("\t\tchar __pad%d[%d];\n",
			       pad_cnt, need_off - off);
			pad_cnt++;
		}

		/* sanitize variable name, e.g., for static vars inside
		 * a function, it's name is '<function name>.<variable name>',
		 * which we'll turn into a '<function name>_<variable name>'
		 */
		var_ident[0] = '\0';
		strncat(var_ident, var_name, sizeof(var_ident) - 1);
		sanitize_identifier(var_ident);

		printf("\t\t");
		err = btf_dump__emit_type_decl(d, var_type_id, &opts);
		if (err)
			return err;
		printf(";\n");

		off = sec_var->offset + sec_var->size;
	}
	printf("	} *%s;\n", sec_ident);
	return 0;
}

static int codegen_datasecs(struct bpf_object *obj, const char *obj_name)
{
	struct btf *btf = bpf_object__btf(obj);
	int n = btf__get_nr_types(btf);
	struct btf_dump *d;
	int i, err = 0;

	d = btf_dump__new(btf, NULL, NULL, codegen_btf_dump_printf);
	if (IS_ERR(d))
		return PTR_ERR(d);

	for (i = 1; i <= n; i++) {
		const struct btf_type *t = btf__type_by_id(btf, i);

		if (!btf_is_datasec(t))
			continue;

		err = codegen_datasec_def(obj, btf, d, t, obj_name);
		if (err)
			goto out;
	}
out:
	btf_dump__free(d);
	return err;
}

static void codegen(const char *template, ...)
{
	const char *src, *end;
	int skip_tabs = 0, n;
	char *s, *dst;
	va_list args;
	char c;

	n = strlen(template);
	s = malloc(n + 1);
	if (!s)
		exit(-1);
	src = template;
	dst = s;

	/* find out "baseline" indentation to skip */
	while ((c = *src++)) {
		if (c == '\t') {
			skip_tabs++;
		} else if (c == '\n') {
			break;
		} else {
			p_err("unrecognized character at pos %td in template '%s'",
			      src - template - 1, template);
			free(s);
			exit(-1);
		}
	}

	while (*src) {
		/* skip baseline indentation tabs */
		for (n = skip_tabs; n > 0; n--, src++) {
			if (*src != '\t') {
				p_err("not enough tabs at pos %td in template '%s'",
				      src - template - 1, template);
				free(s);
				exit(-1);
			}
		}
		/* trim trailing whitespace */
		end = strchrnul(src, '\n');
		for (n = end - src; n > 0 && isspace(src[n - 1]); n--)
			;
		memcpy(dst, src, n);
		dst += n;
		if (*end)
			*dst++ = '\n';
		src = *end ? end + 1 : end;
	}
	*dst++ = '\0';

	/* print out using adjusted template */
	va_start(args, template);
	n = vprintf(s, args);
	va_end(args);

	free(s);
}

static void print_hex(const char *data, int data_sz)
{
	int i, len;

	for (i = 0, len = 0; i < data_sz; i++) {
		int w = data[i] ? 4 : 2;

		len += w;
		if (len > 78) {
			printf("\\\n");
			len = w;
		}
		if (!data[i])
			printf("\\0");
		else
			printf("\\x%02x", (unsigned char)data[i]);
	}
}

static size_t bpf_map_mmap_sz(const struct bpf_map *map)
{
	long page_sz = sysconf(_SC_PAGE_SIZE);
	size_t map_sz;

	map_sz = (size_t)roundup(bpf_map__value_size(map), 8) * bpf_map__max_entries(map);
	map_sz = roundup(map_sz, page_sz);
	return map_sz;
}

static void codegen_attach_detach(struct bpf_object *obj, const char *obj_name)
{
	struct bpf_program *prog;

	bpf_object__for_each_program(prog, obj) {
		const char *tp_name;

		codegen("\
			\n\
			\n\
			static inline int					    \n\
			%1$s__%2$s__attach(struct %1$s *skel)			    \n\
			{							    \n\
				int prog_fd = skel->progs.%2$s.prog_fd;		    \n\
			", obj_name, bpf_program__name(prog));

		switch (bpf_program__get_type(prog)) {
		case BPF_PROG_TYPE_RAW_TRACEPOINT:
			tp_name = strchr(bpf_program__section_name(prog), '/') + 1;
			printf("\tint fd = bpf_raw_tracepoint_open(\"%s\", prog_fd);\n", tp_name);
			break;
		case BPF_PROG_TYPE_TRACING:
			printf("\tint fd = bpf_raw_tracepoint_open(NULL, prog_fd);\n");
			break;
		default:
			printf("\tint fd = ((void)prog_fd, 0); /* auto-attach not supported */\n");
			break;
		}
		codegen("\
			\n\
										    \n\
				if (fd > 0)					    \n\
					skel->links.%1$s_fd = fd;		    \n\
				return fd;					    \n\
			}							    \n\
			", bpf_program__name(prog));
	}

	codegen("\
		\n\
									    \n\
		static inline int					    \n\
		%1$s__attach(struct %1$s *skel)				    \n\
		{							    \n\
			int ret = 0;					    \n\
									    \n\
		", obj_name);

	bpf_object__for_each_program(prog, obj) {
		codegen("\
			\n\
				ret = ret < 0 ? ret : %1$s__%2$s__attach(skel);   \n\
			", obj_name, bpf_program__name(prog));
	}

	codegen("\
		\n\
			return ret < 0 ? ret : 0;			    \n\
		}							    \n\
									    \n\
		static inline void					    \n\
		%1$s__detach(struct %1$s *skel)				    \n\
		{							    \n\
		", obj_name);

	bpf_object__for_each_program(prog, obj) {
		codegen("\
			\n\
				skel_closenz(skel->links.%1$s_fd);	    \n\
			", bpf_program__name(prog));
	}

	codegen("\
		\n\
		}							    \n\
		");
}

static void codegen_destroy(struct bpf_object *obj, const char *obj_name)
{
	struct bpf_program *prog;
	struct bpf_map *map;

	codegen("\
		\n\
		static void						    \n\
		%1$s__destroy(struct %1$s *skel)			    \n\
		{							    \n\
			if (!skel)					    \n\
				return;					    \n\
			%1$s__detach(skel);				    \n\
		",
		obj_name);

	bpf_object__for_each_program(prog, obj) {
		codegen("\
			\n\
				skel_closenz(skel->progs.%1$s.prog_fd);	    \n\
			", bpf_program__name(prog));
	}

	bpf_object__for_each_map(map, obj) {
		const char * ident;

		ident = get_map_ident(map);
		if (!ident)
			continue;
		if (bpf_map__is_internal(map) &&
		    (bpf_map__def(map)->map_flags & BPF_F_MMAPABLE))
			printf("\tmunmap(skel->%1$s, %2$zd);\n",
			       ident, bpf_map_mmap_sz(map));
		codegen("\
			\n\
				skel_closenz(skel->maps.%1$s.map_fd);	    \n\
			", ident);
	}
	codegen("\
		\n\
			free(skel);					    \n\
		}							    \n\
		",
		obj_name);
}

static int gen_trace(struct bpf_object *obj, const char *obj_name, const char *header_guard)
{
	struct bpf_object_load_attr load_attr = {};
	DECLARE_LIBBPF_OPTS(gen_loader_opts, opts);
	struct bpf_map *map;
	int err = 0;

	err = bpf_object__gen_loader(obj, &opts);
	if (err)
		return err;

	load_attr.obj = obj;
	if (verifier_logs)
		/* log_level1 + log_level2 + stats, but not stable UAPI */
		load_attr.log_level = 1 + 2 + 4;

	err = bpf_object__load_xattr(&load_attr);
	if (err) {
		p_err("failed to load object file");
		goto out;
	}
	/* If there was no error during load then gen_loader_opts
	 * are populated with the loader program.
	 */

	/* finish generating 'struct skel' */
	codegen("\
		\n\
		};							    \n\
		", obj_name);


	codegen_attach_detach(obj, obj_name);

	codegen_destroy(obj, obj_name);

	codegen("\
		\n\
		static inline struct %1$s *				    \n\
		%1$s__open(void)					    \n\
		{							    \n\
			struct %1$s *skel;				    \n\
									    \n\
			skel = calloc(sizeof(*skel), 1);		    \n\
			if (!skel)					    \n\
				goto cleanup;				    \n\
			skel->ctx.sz = (void *)&skel->links - (void *)skel; \n\
		",
		obj_name, opts.data_sz);
	bpf_object__for_each_map(map, obj) {
		const char *ident;
		const void *mmap_data = NULL;
		size_t mmap_size = 0;

		ident = get_map_ident(map);
		if (!ident)
			continue;

		if (!bpf_map__is_internal(map) ||
		    !(bpf_map__def(map)->map_flags & BPF_F_MMAPABLE))
			continue;

		codegen("\
			\n\
				skel->%1$s =					 \n\
					mmap(NULL, %2$zd, PROT_READ | PROT_WRITE,\n\
					     MAP_SHARED | MAP_ANONYMOUS, -1, 0); \n\
				if (skel->%1$s == (void *) -1)			 \n\
					goto cleanup;				 \n\
				memcpy(skel->%1$s, (void *)\"\\			 \n\
			", ident, bpf_map_mmap_sz(map));
		mmap_data = bpf_map__initial_value(map, &mmap_size);
		print_hex(mmap_data, mmap_size);
		printf("\", %2$zd);\n"
		       "\tskel->maps.%1$s.initial_value = (__u64)(long)skel->%1$s;\n",
		       ident, mmap_size);
	}
	codegen("\
		\n\
			return skel;					    \n\
		cleanup:						    \n\
			%1$s__destroy(skel);				    \n\
			return NULL;					    \n\
		}							    \n\
									    \n\
		static inline int					    \n\
		%1$s__load(struct %1$s *skel)				    \n\
		{							    \n\
			struct bpf_load_and_run_opts opts = {};		    \n\
			int err;					    \n\
									    \n\
			opts.ctx = (struct bpf_loader_ctx *)skel;	    \n\
			opts.data_sz = %2$d;				    \n\
			opts.data = (void *)\"\\			    \n\
		",
		obj_name, opts.data_sz);
	print_hex(opts.data, opts.data_sz);
	codegen("\
		\n\
		\";							    \n\
		");

	codegen("\
		\n\
			opts.insns_sz = %d;				    \n\
			opts.insns = (void *)\"\\			    \n\
		",
		opts.insns_sz);
	print_hex(opts.insns, opts.insns_sz);
	codegen("\
		\n\
		\";							    \n\
			err = bpf_load_and_run(&opts);			    \n\
			if (err < 0)					    \n\
				return err;				    \n\
		", obj_name);
	bpf_object__for_each_map(map, obj) {
		const char *ident, *mmap_flags;

		ident = get_map_ident(map);
		if (!ident)
			continue;

		if (!bpf_map__is_internal(map) ||
		    !(bpf_map__def(map)->map_flags & BPF_F_MMAPABLE))
			continue;
		if (bpf_map__def(map)->map_flags & BPF_F_RDONLY_PROG)
			mmap_flags = "PROT_READ";
		else
			mmap_flags = "PROT_READ | PROT_WRITE";

		printf("\tskel->%1$s =\n"
		       "\t\tmmap(skel->%1$s, %2$zd, %3$s, MAP_SHARED | MAP_FIXED,\n"
		       "\t\t\tskel->maps.%1$s.map_fd, 0);\n",
		       ident, bpf_map_mmap_sz(map), mmap_flags);
	}
	codegen("\
		\n\
			return 0;					    \n\
		}							    \n\
									    \n\
		static inline struct %1$s *				    \n\
		%1$s__open_and_load(void)				    \n\
		{							    \n\
			struct %1$s *skel;				    \n\
									    \n\
			skel = %1$s__open();				    \n\
			if (!skel)					    \n\
				return NULL;				    \n\
			if (%1$s__load(skel)) {				    \n\
				%1$s__destroy(skel);			    \n\
				return NULL;				    \n\
			}						    \n\
			return skel;					    \n\
		}							    \n\
		", obj_name);

	codegen("\
		\n\
									    \n\
		#endif /* %s */						    \n\
		",
		header_guard);
	err = 0;
out:
	return err;
}

static int do_skeleton(int argc, char **argv)
{
	char header_guard[MAX_OBJ_NAME_LEN + sizeof("__SKEL_H__")];
	size_t i, map_cnt = 0, prog_cnt = 0, file_sz, mmap_sz;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
	char obj_name[MAX_OBJ_NAME_LEN] = "", *obj_data;
	struct bpf_object *obj = NULL;
	const char *file, *ident;
	struct bpf_program *prog;
	int fd, err = -1;
	struct bpf_map *map;
	struct btf *btf;
	struct stat st;

	if (!REQ_ARGS(1)) {
		usage();
		return -1;
	}
	file = GET_ARG();

	while (argc) {
		if (!REQ_ARGS(2))
			return -1;

		if (is_prefix(*argv, "name")) {
			NEXT_ARG();

			if (obj_name[0] != '\0') {
				p_err("object name already specified");
				return -1;
			}

			strncpy(obj_name, *argv, MAX_OBJ_NAME_LEN - 1);
			obj_name[MAX_OBJ_NAME_LEN - 1] = '\0';
		} else {
			p_err("unknown arg %s", *argv);
			return -1;
		}

		NEXT_ARG();
	}

	if (argc) {
		p_err("extra unknown arguments");
		return -1;
	}

	if (stat(file, &st)) {
		p_err("failed to stat() %s: %s", file, strerror(errno));
		return -1;
	}
	file_sz = st.st_size;
	mmap_sz = roundup(file_sz, sysconf(_SC_PAGE_SIZE));
	fd = open(file, O_RDONLY);
	if (fd < 0) {
		p_err("failed to open() %s: %s", file, strerror(errno));
		return -1;
	}
	obj_data = mmap(NULL, mmap_sz, PROT_READ, MAP_PRIVATE, fd, 0);
	if (obj_data == MAP_FAILED) {
		obj_data = NULL;
		p_err("failed to mmap() %s: %s", file, strerror(errno));
		goto out;
	}
	if (obj_name[0] == '\0')
		get_obj_name(obj_name, file);
	opts.object_name = obj_name;
	obj = bpf_object__open_mem(obj_data, file_sz, &opts);
	if (IS_ERR(obj)) {
		char err_buf[256];

		libbpf_strerror(PTR_ERR(obj), err_buf, sizeof(err_buf));
		p_err("failed to open BPF object file: %s", err_buf);
		obj = NULL;
		goto out;
	}

	bpf_object__for_each_map(map, obj) {
		ident = get_map_ident(map);
		if (!ident) {
			p_err("ignoring unrecognized internal map '%s'...",
			      bpf_map__name(map));
			continue;
		}
		map_cnt++;
	}
	bpf_object__for_each_program(prog, obj) {
		prog_cnt++;
	}

	get_header_guard(header_guard, obj_name);
	if (use_loader) {
		codegen("\
		\n\
		/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */   \n\
		/* THIS FILE IS AUTOGENERATED! */			    \n\
		#ifndef %2$s						    \n\
		#define %2$s						    \n\
									    \n\
		#include <stdlib.h>					    \n\
		#include <bpf/bpf.h>					    \n\
		#include <bpf/skel_internal.h>				    \n\
									    \n\
		struct %1$s {						    \n\
			struct bpf_loader_ctx ctx;			    \n\
		",
		obj_name, header_guard
		);
	} else {
		codegen("\
		\n\
		/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */   \n\
									    \n\
		/* THIS FILE IS AUTOGENERATED! */			    \n\
		#ifndef %2$s						    \n\
		#define %2$s						    \n\
									    \n\
		#include <stdlib.h>					    \n\
		#include <bpf/libbpf.h>					    \n\
									    \n\
		struct %1$s {						    \n\
			struct bpf_object_skeleton *skeleton;		    \n\
			struct bpf_object *obj;				    \n\
		",
		obj_name, header_guard
		);
	}

	if (map_cnt) {
		printf("\tstruct {\n");
		bpf_object__for_each_map(map, obj) {
			ident = get_map_ident(map);
			if (!ident)
				continue;
			if (use_loader)
				printf("\t\tstruct bpf_map_desc %s;\n", ident);
			else
				printf("\t\tstruct bpf_map *%s;\n", ident);
		}
		printf("\t} maps;\n");
	}

	if (prog_cnt) {
		printf("\tstruct {\n");
		bpf_object__for_each_program(prog, obj) {
			if (use_loader)
				printf("\t\tstruct bpf_prog_desc %s;\n",
				       bpf_program__name(prog));
			else
				printf("\t\tstruct bpf_program *%s;\n",
				       bpf_program__name(prog));
		}
		printf("\t} progs;\n");
		printf("\tstruct {\n");
		bpf_object__for_each_program(prog, obj) {
			if (use_loader)
				printf("\t\tint %s_fd;\n",
				       bpf_program__name(prog));
			else
				printf("\t\tstruct bpf_link *%s;\n",
				       bpf_program__name(prog));
		}
		printf("\t} links;\n");
	}

	btf = bpf_object__btf(obj);
	if (btf) {
		err = codegen_datasecs(obj, obj_name);
		if (err)
			goto out;
	}
	if (use_loader) {
		err = gen_trace(obj, obj_name, header_guard);
		goto out;
	}

	codegen("\
		\n\
		};							    \n\
									    \n\
		static void						    \n\
		%1$s__destroy(struct %1$s *obj)				    \n\
		{							    \n\
			if (!obj)					    \n\
				return;					    \n\
			if (obj->skeleton)				    \n\
				bpf_object__destroy_skeleton(obj->skeleton);\n\
			free(obj);					    \n\
		}							    \n\
									    \n\
		static inline int					    \n\
		%1$s__create_skeleton(struct %1$s *obj);		    \n\
									    \n\
		static inline struct %1$s *				    \n\
		%1$s__open_opts(const struct bpf_object_open_opts *opts)    \n\
		{							    \n\
			struct %1$s *obj;				    \n\
									    \n\
			obj = (struct %1$s *)calloc(1, sizeof(*obj));	    \n\
			if (!obj)					    \n\
				return NULL;				    \n\
			if (%1$s__create_skeleton(obj))			    \n\
				goto err;				    \n\
			if (bpf_object__open_skeleton(obj->skeleton, opts)) \n\
				goto err;				    \n\
									    \n\
			return obj;					    \n\
		err:							    \n\
			%1$s__destroy(obj);				    \n\
			return NULL;					    \n\
		}							    \n\
									    \n\
		static inline struct %1$s *				    \n\
		%1$s__open(void)					    \n\
		{							    \n\
			return %1$s__open_opts(NULL);			    \n\
		}							    \n\
									    \n\
		static inline int					    \n\
		%1$s__load(struct %1$s *obj)				    \n\
		{							    \n\
			return bpf_object__load_skeleton(obj->skeleton);    \n\
		}							    \n\
									    \n\
		static inline struct %1$s *				    \n\
		%1$s__open_and_load(void)				    \n\
		{							    \n\
			struct %1$s *obj;				    \n\
									    \n\
			obj = %1$s__open();				    \n\
			if (!obj)					    \n\
				return NULL;				    \n\
			if (%1$s__load(obj)) {				    \n\
				%1$s__destroy(obj);			    \n\
				return NULL;				    \n\
			}						    \n\
			return obj;					    \n\
		}							    \n\
									    \n\
		static inline int					    \n\
		%1$s__attach(struct %1$s *obj)				    \n\
		{							    \n\
			return bpf_object__attach_skeleton(obj->skeleton);  \n\
		}							    \n\
									    \n\
		static inline void					    \n\
		%1$s__detach(struct %1$s *obj)				    \n\
		{							    \n\
			return bpf_object__detach_skeleton(obj->skeleton);  \n\
		}							    \n\
		",
		obj_name
	);

	codegen("\
		\n\
									    \n\
		static inline int					    \n\
		%1$s__create_skeleton(struct %1$s *obj)			    \n\
		{							    \n\
			struct bpf_object_skeleton *s;			    \n\
									    \n\
			s = (struct bpf_object_skeleton *)calloc(1, sizeof(*s));\n\
			if (!s)						    \n\
				return -1;				    \n\
			obj->skeleton = s;				    \n\
									    \n\
			s->sz = sizeof(*s);				    \n\
			s->name = \"%1$s\";				    \n\
			s->obj = &obj->obj;				    \n\
		",
		obj_name
	);
	if (map_cnt) {
		codegen("\
			\n\
									    \n\
				/* maps */				    \n\
				s->map_cnt = %zu;			    \n\
				s->map_skel_sz = sizeof(*s->maps);	    \n\
				s->maps = (struct bpf_map_skeleton *)calloc(s->map_cnt, s->map_skel_sz);\n\
				if (!s->maps)				    \n\
					goto err;			    \n\
			",
			map_cnt
		);
		i = 0;
		bpf_object__for_each_map(map, obj) {
			ident = get_map_ident(map);

			if (!ident)
				continue;

			codegen("\
				\n\
									    \n\
					s->maps[%zu].name = \"%s\";	    \n\
					s->maps[%zu].map = &obj->maps.%s;   \n\
				",
				i, bpf_map__name(map), i, ident);
			/* memory-mapped internal maps */
			if (bpf_map__is_internal(map) &&
			    (bpf_map__def(map)->map_flags & BPF_F_MMAPABLE)) {
				printf("\ts->maps[%zu].mmaped = (void **)&obj->%s;\n",
				       i, ident);
			}
			i++;
		}
	}
	if (prog_cnt) {
		codegen("\
			\n\
									    \n\
				/* programs */				    \n\
				s->prog_cnt = %zu;			    \n\
				s->prog_skel_sz = sizeof(*s->progs);	    \n\
				s->progs = (struct bpf_prog_skeleton *)calloc(s->prog_cnt, s->prog_skel_sz);\n\
				if (!s->progs)				    \n\
					goto err;			    \n\
			",
			prog_cnt
		);
		i = 0;
		bpf_object__for_each_program(prog, obj) {
			codegen("\
				\n\
									    \n\
					s->progs[%1$zu].name = \"%2$s\";    \n\
					s->progs[%1$zu].prog = &obj->progs.%2$s;\n\
					s->progs[%1$zu].link = &obj->links.%2$s;\n\
				",
				i, bpf_program__name(prog));
			i++;
		}
	}
	codegen("\
		\n\
									    \n\
			s->data_sz = %d;				    \n\
			s->data = (void *)\"\\				    \n\
		",
		file_sz);

	/* embed contents of BPF object file */
	print_hex(obj_data, file_sz);

	codegen("\
		\n\
		\";							    \n\
									    \n\
			return 0;					    \n\
		err:							    \n\
			bpf_object__destroy_skeleton(s);		    \n\
			return -1;					    \n\
		}							    \n\
									    \n\
		#endif /* %s */						    \n\
		",
		header_guard);
	err = 0;
out:
	bpf_object__close(obj);
	if (obj_data)
		munmap(obj_data, mmap_sz);
	close(fd);
	return err;
}

static int do_object(int argc, char **argv)
{
	struct bpf_linker *linker;
	const char *output_file, *file;
	int err = 0;

	if (!REQ_ARGS(2)) {
		usage();
		return -1;
	}

	output_file = GET_ARG();

	linker = bpf_linker__new(output_file, NULL);
	if (!linker) {
		p_err("failed to create BPF linker instance");
		return -1;
	}

	while (argc) {
		file = GET_ARG();

		err = bpf_linker__add_file(linker, file, NULL);
		if (err) {
			p_err("failed to link '%s': %s (%d)", file, strerror(err), err);
			goto out;
		}
	}

	err = bpf_linker__finalize(linker);
	if (err) {
		p_err("failed to finalize ELF file: %s (%d)", strerror(err), err);
		goto out;
	}

	err = 0;
out:
	bpf_linker__free(linker);
	return err;
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %1$s %2$s object OUTPUT_FILE INPUT_FILE [INPUT_FILE...]\n"
		"       %1$s %2$s skeleton FILE [name OBJECT_NAME]\n"
		"       %1$s %2$s help\n"
		"\n"
		"       " HELP_SPEC_OPTIONS "\n"
		"",
		bin_name, "gen");

	return 0;
}

static const struct cmd cmds[] = {
	{ "object",	do_object },
	{ "skeleton",	do_skeleton },
	{ "help",	do_help },
	{ 0 }
};

int do_gen(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}

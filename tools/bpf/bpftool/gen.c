// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Facebook */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <linux/err.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_internal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <bpf/btf.h>

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

static bool str_has_prefix(const char *str, const char *prefix)
{
	return strncmp(str, prefix, strlen(prefix)) == 0;
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

static const struct btf_type *
resolve_func_ptr(const struct btf *btf, __u32 id, __u32 *res_id)
{
	const struct btf_type *t;

	t = skip_mods_and_typedefs(btf, id, NULL);
	if (!btf_is_ptr(t))
		return NULL;

	t = skip_mods_and_typedefs(btf, t->type, res_id);

	return btf_is_func_proto(t) ? t : NULL;
}

static void get_obj_name(char *name, const char *file)
{
	char file_copy[PATH_MAX];

	/* Using basename() POSIX version to be more portable. */
	strncpy(file_copy, file, PATH_MAX - 1)[PATH_MAX - 1] = '\0';
	strncpy(name, basename(file_copy), MAX_OBJ_NAME_LEN - 1)[MAX_OBJ_NAME_LEN - 1] = '\0';
	if (str_has_suffix(name, ".o"))
		name[strlen(name) - 2] = '\0';
	sanitize_identifier(name);
}

static void get_header_guard(char *guard, const char *obj_name, const char *suffix)
{
	int i;

	sprintf(guard, "__%s_%s__", obj_name, suffix);
	for (i = 0; guard[i]; i++)
		guard[i] = toupper(guard[i]);
}

static bool get_map_ident(const struct bpf_map *map, char *buf, size_t buf_sz)
{
	static const char *sfxs[] = { ".data", ".rodata", ".bss", ".kconfig" };
	const char *name = bpf_map__name(map);
	int i, n;

	if (!bpf_map__is_internal(map)) {
		snprintf(buf, buf_sz, "%s", name);
		return true;
	}

	for  (i = 0, n = ARRAY_SIZE(sfxs); i < n; i++) {
		const char *sfx = sfxs[i], *p;

		p = strstr(name, sfx);
		if (p) {
			snprintf(buf, buf_sz, "%s", p + 1);
			sanitize_identifier(buf);
			return true;
		}
	}

	return false;
}

static bool get_datasec_ident(const char *sec_name, char *buf, size_t buf_sz)
{
	static const char *pfxs[] = { ".data", ".rodata", ".bss", ".kconfig" };
	int i, n;

	/* recognize hard coded LLVM section name */
	if (strcmp(sec_name, ".addr_space.1") == 0) {
		/* this is the name to use in skeleton */
		snprintf(buf, buf_sz, "arena");
		return true;
	}
	for  (i = 0, n = ARRAY_SIZE(pfxs); i < n; i++) {
		const char *pfx = pfxs[i];

		if (str_has_prefix(sec_name, pfx)) {
			snprintf(buf, buf_sz, "%s", sec_name + 1);
			sanitize_identifier(buf);
			return true;
		}
	}

	return false;
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
	char var_ident[256], sec_ident[256];
	bool strip_mods = false;

	if (!get_datasec_ident(sec_name, sec_ident, sizeof(sec_ident)))
		return 0;

	if (strcmp(sec_name, ".kconfig") != 0)
		strip_mods = true;

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

static const struct btf_type *find_type_for_map(struct btf *btf, const char *map_ident)
{
	int n = btf__type_cnt(btf), i;
	char sec_ident[256];

	for (i = 1; i < n; i++) {
		const struct btf_type *t = btf__type_by_id(btf, i);
		const char *name;

		if (!btf_is_datasec(t))
			continue;

		name = btf__str_by_offset(btf, t->name_off);
		if (!get_datasec_ident(name, sec_ident, sizeof(sec_ident)))
			continue;

		if (strcmp(sec_ident, map_ident) == 0)
			return t;
	}
	return NULL;
}

static bool is_mmapable_map(const struct bpf_map *map, char *buf, size_t sz)
{
	size_t tmp_sz;

	if (bpf_map__type(map) == BPF_MAP_TYPE_ARENA && bpf_map__initial_value(map, &tmp_sz)) {
		snprintf(buf, sz, "arena");
		return true;
	}

	if (!bpf_map__is_internal(map) || !(bpf_map__map_flags(map) & BPF_F_MMAPABLE))
		return false;

	if (!get_map_ident(map, buf, sz))
		return false;

	return true;
}

static int codegen_datasecs(struct bpf_object *obj, const char *obj_name)
{
	struct btf *btf = bpf_object__btf(obj);
	struct btf_dump *d;
	struct bpf_map *map;
	const struct btf_type *sec;
	char map_ident[256];
	int err = 0;

	d = btf_dump__new(btf, codegen_btf_dump_printf, NULL, NULL);
	if (!d)
		return -errno;

	bpf_object__for_each_map(map, obj) {
		/* only generate definitions for memory-mapped internal maps */
		if (!is_mmapable_map(map, map_ident, sizeof(map_ident)))
			continue;

		sec = find_type_for_map(btf, map_ident);

		/* In some cases (e.g., sections like .rodata.cst16 containing
		 * compiler allocated string constants only) there will be
		 * special internal maps with no corresponding DATASEC BTF
		 * type. In such case, generate empty structs for each such
		 * map. It will still be memory-mapped and its contents
		 * accessible from user-space through BPF skeleton.
		 */
		if (!sec) {
			printf("	struct %s__%s {\n", obj_name, map_ident);
			printf("	} *%s;\n", map_ident);
		} else {
			err = codegen_datasec_def(obj, btf, d, sec, obj_name);
			if (err)
				goto out;
		}
	}


out:
	btf_dump__free(d);
	return err;
}

static bool btf_is_ptr_to_func_proto(const struct btf *btf,
				     const struct btf_type *v)
{
	return btf_is_ptr(v) && btf_is_func_proto(btf__type_by_id(btf, v->type));
}

static int codegen_subskel_datasecs(struct bpf_object *obj, const char *obj_name)
{
	struct btf *btf = bpf_object__btf(obj);
	struct btf_dump *d;
	struct bpf_map *map;
	const struct btf_type *sec, *var;
	const struct btf_var_secinfo *sec_var;
	int i, err = 0, vlen;
	char map_ident[256], sec_ident[256];
	bool strip_mods = false, needs_typeof = false;
	const char *sec_name, *var_name;
	__u32 var_type_id;

	d = btf_dump__new(btf, codegen_btf_dump_printf, NULL, NULL);
	if (!d)
		return -errno;

	bpf_object__for_each_map(map, obj) {
		/* only generate definitions for memory-mapped internal maps */
		if (!is_mmapable_map(map, map_ident, sizeof(map_ident)))
			continue;

		sec = find_type_for_map(btf, map_ident);
		if (!sec)
			continue;

		sec_name = btf__name_by_offset(btf, sec->name_off);
		if (!get_datasec_ident(sec_name, sec_ident, sizeof(sec_ident)))
			continue;

		strip_mods = strcmp(sec_name, ".kconfig") != 0;
		printf("	struct %s__%s {\n", obj_name, sec_ident);

		sec_var = btf_var_secinfos(sec);
		vlen = btf_vlen(sec);
		for (i = 0; i < vlen; i++, sec_var++) {
			DECLARE_LIBBPF_OPTS(btf_dump_emit_type_decl_opts, opts,
				.indent_level = 2,
				.strip_mods = strip_mods,
				/* we'll print the name separately */
				.field_name = "",
			);

			var = btf__type_by_id(btf, sec_var->type);
			var_name = btf__name_by_offset(btf, var->name_off);
			var_type_id = var->type;

			/* static variables are not exposed through BPF skeleton */
			if (btf_var(var)->linkage == BTF_VAR_STATIC)
				continue;

			/* The datasec member has KIND_VAR but we want the
			 * underlying type of the variable (e.g. KIND_INT).
			 */
			var = skip_mods_and_typedefs(btf, var->type, NULL);

			printf("\t\t");
			/* Func and array members require special handling.
			 * Instead of producing `typename *var`, they produce
			 * `typeof(typename) *var`. This allows us to keep a
			 * similar syntax where the identifier is just prefixed
			 * by *, allowing us to ignore C declaration minutiae.
			 */
			needs_typeof = btf_is_array(var) || btf_is_ptr_to_func_proto(btf, var);
			if (needs_typeof)
				printf("__typeof__(");

			err = btf_dump__emit_type_decl(d, var_type_id, &opts);
			if (err)
				goto out;

			if (needs_typeof)
				printf(")");

			printf(" *%s;\n", var_name);
		}
		printf("	} %s;\n", sec_ident);
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
			p_err("unrecognized character at pos %td in template '%s': '%c'",
			      src - template - 1, template, c);
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

/* Emit type size asserts for all top-level fields in memory-mapped internal maps. */
static void codegen_asserts(struct bpf_object *obj, const char *obj_name)
{
	struct btf *btf = bpf_object__btf(obj);
	struct bpf_map *map;
	struct btf_var_secinfo *sec_var;
	int i, vlen;
	const struct btf_type *sec;
	char map_ident[256], var_ident[256];

	if (!btf)
		return;

	codegen("\
		\n\
		__attribute__((unused)) static void			    \n\
		%1$s__assert(struct %1$s *s __attribute__((unused)))	    \n\
		{							    \n\
		#ifdef __cplusplus					    \n\
		#define _Static_assert static_assert			    \n\
		#endif							    \n\
		", obj_name);

	bpf_object__for_each_map(map, obj) {
		if (!is_mmapable_map(map, map_ident, sizeof(map_ident)))
			continue;

		sec = find_type_for_map(btf, map_ident);
		if (!sec) {
			/* best effort, couldn't find the type for this map */
			continue;
		}

		sec_var = btf_var_secinfos(sec);
		vlen =  btf_vlen(sec);

		for (i = 0; i < vlen; i++, sec_var++) {
			const struct btf_type *var = btf__type_by_id(btf, sec_var->type);
			const char *var_name = btf__name_by_offset(btf, var->name_off);
			long var_size;

			/* static variables are not exposed through BPF skeleton */
			if (btf_var(var)->linkage == BTF_VAR_STATIC)
				continue;

			var_size = btf__resolve_size(btf, var->type);
			if (var_size < 0)
				continue;

			var_ident[0] = '\0';
			strncat(var_ident, var_name, sizeof(var_ident) - 1);
			sanitize_identifier(var_ident);

			printf("\t_Static_assert(sizeof(s->%s->%s) == %ld, \"unexpected size of '%s'\");\n",
			       map_ident, var_ident, var_size, var_ident);
		}
	}
	codegen("\
		\n\
		#ifdef __cplusplus					    \n\
		#undef _Static_assert					    \n\
		#endif							    \n\
		}							    \n\
		");
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

		switch (bpf_program__type(prog)) {
		case BPF_PROG_TYPE_RAW_TRACEPOINT:
			tp_name = strchr(bpf_program__section_name(prog), '/') + 1;
			printf("\tint fd = skel_raw_tracepoint_open(\"%s\", prog_fd);\n", tp_name);
			break;
		case BPF_PROG_TYPE_TRACING:
		case BPF_PROG_TYPE_LSM:
			if (bpf_program__expected_attach_type(prog) == BPF_TRACE_ITER)
				printf("\tint fd = skel_link_create(prog_fd, 0, BPF_TRACE_ITER);\n");
			else
				printf("\tint fd = skel_raw_tracepoint_open(NULL, prog_fd);\n");
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
	char ident[256];

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
		if (!get_map_ident(map, ident, sizeof(ident)))
			continue;
		if (bpf_map__is_internal(map) &&
		    (bpf_map__map_flags(map) & BPF_F_MMAPABLE))
			printf("\tskel_free_map_data(skel->%1$s, skel->maps.%1$s.initial_value, %2$zu);\n",
			       ident, bpf_map_mmap_sz(map));
		codegen("\
			\n\
				skel_closenz(skel->maps.%1$s.map_fd);	    \n\
			", ident);
	}
	codegen("\
		\n\
			skel_free(skel);				    \n\
		}							    \n\
		",
		obj_name);
}

static int gen_trace(struct bpf_object *obj, const char *obj_name, const char *header_guard)
{
	DECLARE_LIBBPF_OPTS(gen_loader_opts, opts);
	struct bpf_load_and_run_opts sopts = {};
	char sig_buf[MAX_SIG_SIZE];
	__u8 prog_sha[SHA256_DIGEST_LENGTH];
	struct bpf_map *map;

	char ident[256];
	int err = 0;

	if (sign_progs)
		opts.gen_hash = true;

	err = bpf_object__gen_loader(obj, &opts);
	if (err)
		return err;

	err = bpf_object__load(obj);
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
			skel = skel_alloc(sizeof(*skel));		    \n\
			if (!skel)					    \n\
				goto cleanup;				    \n\
			skel->ctx.sz = (void *)&skel->links - (void *)skel; \n\
		",
		obj_name, opts.data_sz);
	bpf_object__for_each_map(map, obj) {
		const void *mmap_data = NULL;
		size_t mmap_size = 0;

		if (!is_mmapable_map(map, ident, sizeof(ident)))
			continue;

		codegen("\
		\n\
			{						    \n\
				static const char data[] __attribute__((__aligned__(8))) = \"\\\n\
		");
		mmap_data = bpf_map__initial_value(map, &mmap_size);
		print_hex(mmap_data, mmap_size);
		codegen("\
		\n\
		\";							    \n\
									    \n\
				skel->%1$s = skel_prep_map_data((void *)data, %2$zd,\n\
								sizeof(data) - 1);\n\
				if (!skel->%1$s)			    \n\
					goto cleanup;			    \n\
				skel->maps.%1$s.initial_value = (__u64) (long) skel->%1$s;\n\
			}						    \n\
			", ident, bpf_map_mmap_sz(map));
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
			static const char opts_data[] __attribute__((__aligned__(8))) = \"\\\n\
		",
		obj_name);
	print_hex(opts.data, opts.data_sz);
	codegen("\
		\n\
		\";							    \n\
			static const char opts_insn[] __attribute__((__aligned__(8))) = \"\\\n\
		");
	print_hex(opts.insns, opts.insns_sz);
	codegen("\
		\n\
		\";\n");

	if (sign_progs) {
		sopts.insns = opts.insns;
		sopts.insns_sz = opts.insns_sz;
		sopts.excl_prog_hash = prog_sha;
		sopts.excl_prog_hash_sz = sizeof(prog_sha);
		sopts.signature = sig_buf;
		sopts.signature_sz = MAX_SIG_SIZE;

		err = bpftool_prog_sign(&sopts);
		if (err < 0) {
			p_err("failed to sign program");
			goto out;
		}

		codegen("\
		\n\
			static const char opts_sig[] __attribute__((__aligned__(8))) = \"\\\n\
		");
		print_hex((const void *)sig_buf, sopts.signature_sz);
		codegen("\
		\n\
		\";\n");

		codegen("\
		\n\
			static const char opts_excl_hash[] __attribute__((__aligned__(8))) = \"\\\n\
		");
		print_hex((const void *)prog_sha, sizeof(prog_sha));
		codegen("\
		\n\
		\";\n");

		codegen("\
		\n\
			opts.signature = (void *)opts_sig;			\n\
			opts.signature_sz = sizeof(opts_sig) - 1;		\n\
			opts.excl_prog_hash = (void *)opts_excl_hash;		\n\
			opts.excl_prog_hash_sz = sizeof(opts_excl_hash) - 1;	\n\
			opts.keyring_id = skel->keyring_id;			\n\
		");
	}

	codegen("\
		\n\
			opts.ctx = (struct bpf_loader_ctx *)skel;	    \n\
			opts.data_sz = sizeof(opts_data) - 1;		    \n\
			opts.data = (void *)opts_data;			    \n\
			opts.insns_sz = sizeof(opts_insn) - 1;		    \n\
			opts.insns = (void *)opts_insn;			    \n\
									    \n\
			err = bpf_load_and_run(&opts);			    \n\
			if (err < 0)					    \n\
				return err;				    \n\
		");
	bpf_object__for_each_map(map, obj) {
		const char *mmap_flags;

		if (!is_mmapable_map(map, ident, sizeof(ident)))
			continue;

		if (bpf_map__map_flags(map) & BPF_F_RDONLY_PROG)
			mmap_flags = "PROT_READ";
		else
			mmap_flags = "PROT_READ | PROT_WRITE";

		codegen("\
		\n\
			skel->%1$s = skel_finalize_map_data(&skel->maps.%1$s.initial_value,  \n\
							%2$zd, %3$s, skel->maps.%1$s.map_fd);\n\
			if (!skel->%1$s)				    \n\
				return -ENOMEM;				    \n\
			",
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
									    \n\
		", obj_name);

	codegen_asserts(obj, obj_name);

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

static void
codegen_maps_skeleton(struct bpf_object *obj, size_t map_cnt, bool mmaped, bool populate_links)
{
	struct bpf_map *map;
	char ident[256];
	size_t i, map_sz;

	if (!map_cnt)
		return;

	/* for backward compatibility with old libbpf versions that don't
	 * handle new BPF skeleton with new struct bpf_map_skeleton definition
	 * that includes link field, avoid specifying new increased size,
	 * unless we absolutely have to (i.e., if there are struct_ops maps
	 * present)
	 */
	map_sz = offsetof(struct bpf_map_skeleton, link);
	if (populate_links) {
		bpf_object__for_each_map(map, obj) {
			if (bpf_map__type(map) == BPF_MAP_TYPE_STRUCT_OPS) {
				map_sz = sizeof(struct bpf_map_skeleton);
				break;
			}
		}
	}

	codegen("\
		\n\
								    \n\
			/* maps */				    \n\
			s->map_cnt = %zu;			    \n\
			s->map_skel_sz = %zu;			    \n\
			s->maps = (struct bpf_map_skeleton *)calloc(s->map_cnt,\n\
					sizeof(*s->maps) > %zu ? sizeof(*s->maps) : %zu);\n\
			if (!s->maps) {				    \n\
				err = -ENOMEM;			    \n\
				goto err;			    \n\
			}					    \n\
		",
		map_cnt, map_sz, map_sz, map_sz
	);
	i = 0;
	bpf_object__for_each_map(map, obj) {
		if (!get_map_ident(map, ident, sizeof(ident)))
			continue;

		codegen("\
			\n\
								    \n\
				map = (struct bpf_map_skeleton *)((char *)s->maps + %zu * s->map_skel_sz);\n\
				map->name = \"%s\";		    \n\
				map->map = &obj->maps.%s;	    \n\
			",
			i, bpf_map__name(map), ident);
		/* memory-mapped internal maps */
		if (mmaped && is_mmapable_map(map, ident, sizeof(ident))) {
			printf("\tmap->mmaped = (void **)&obj->%s;\n", ident);
		}

		if (populate_links && bpf_map__type(map) == BPF_MAP_TYPE_STRUCT_OPS) {
			codegen("\
				\n\
					map->link = &obj->links.%s; \n\
				", ident);
		}
		i++;
	}
}

static void
codegen_progs_skeleton(struct bpf_object *obj, size_t prog_cnt, bool populate_links)
{
	struct bpf_program *prog;
	int i;

	if (!prog_cnt)
		return;

	codegen("\
		\n\
									\n\
			/* programs */				    \n\
			s->prog_cnt = %zu;			    \n\
			s->prog_skel_sz = sizeof(*s->progs);	    \n\
			s->progs = (struct bpf_prog_skeleton *)calloc(s->prog_cnt, s->prog_skel_sz);\n\
			if (!s->progs) {			    \n\
				err = -ENOMEM;			    \n\
				goto err;			    \n\
			}					    \n\
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
			",
			i, bpf_program__name(prog));

		if (populate_links) {
			codegen("\
				\n\
					s->progs[%1$zu].link = &obj->links.%2$s;\n\
				",
				i, bpf_program__name(prog));
		}
		i++;
	}
}

static int walk_st_ops_shadow_vars(struct btf *btf, const char *ident,
				   const struct btf_type *map_type, __u32 map_type_id)
{
	LIBBPF_OPTS(btf_dump_emit_type_decl_opts, opts, .indent_level = 3);
	const struct btf_type *member_type;
	__u32 offset, next_offset = 0;
	const struct btf_member *m;
	struct btf_dump *d = NULL;
	const char *member_name;
	__u32 member_type_id;
	int i, err = 0, n;
	int size;

	d = btf_dump__new(btf, codegen_btf_dump_printf, NULL, NULL);
	if (!d)
		return -errno;

	n = btf_vlen(map_type);
	for (i = 0, m = btf_members(map_type); i < n; i++, m++) {
		member_type = skip_mods_and_typedefs(btf, m->type, &member_type_id);
		member_name = btf__name_by_offset(btf, m->name_off);

		offset = m->offset / 8;
		if (next_offset < offset)
			printf("\t\t\tchar __padding_%d[%u];\n", i, offset - next_offset);

		switch (btf_kind(member_type)) {
		case BTF_KIND_INT:
		case BTF_KIND_FLOAT:
		case BTF_KIND_ENUM:
		case BTF_KIND_ENUM64:
			/* scalar type */
			printf("\t\t\t");
			opts.field_name = member_name;
			err = btf_dump__emit_type_decl(d, member_type_id, &opts);
			if (err) {
				p_err("Failed to emit type declaration for %s: %d", member_name, err);
				goto out;
			}
			printf(";\n");

			size = btf__resolve_size(btf, member_type_id);
			if (size < 0) {
				p_err("Failed to resolve size of %s: %d\n", member_name, size);
				err = size;
				goto out;
			}

			next_offset = offset + size;
			break;

		case BTF_KIND_PTR:
			if (resolve_func_ptr(btf, m->type, NULL)) {
				/* Function pointer */
				printf("\t\t\tstruct bpf_program *%s;\n", member_name);

				next_offset = offset + sizeof(void *);
				break;
			}
			/* All pointer types are unsupported except for
			 * function pointers.
			 */
			fallthrough;

		default:
			/* Unsupported types
			 *
			 * Types other than scalar types and function
			 * pointers are currently not supported in order to
			 * prevent conflicts in the generated code caused
			 * by multiple definitions. For instance, if the
			 * struct type FOO is used in a struct_ops map,
			 * bpftool has to generate definitions for FOO,
			 * which may result in conflicts if FOO is defined
			 * in different skeleton files.
			 */
			size = btf__resolve_size(btf, member_type_id);
			if (size < 0) {
				p_err("Failed to resolve size of %s: %d\n", member_name, size);
				err = size;
				goto out;
			}
			printf("\t\t\tchar __unsupported_%d[%d];\n", i, size);

			next_offset = offset + size;
			break;
		}
	}

	/* Cannot fail since it must be a struct type */
	size = btf__resolve_size(btf, map_type_id);
	if (next_offset < (__u32)size)
		printf("\t\t\tchar __padding_end[%u];\n", size - next_offset);

out:
	btf_dump__free(d);

	return err;
}

/* Generate the pointer of the shadow type for a struct_ops map.
 *
 * This function adds a pointer of the shadow type for a struct_ops map.
 * The members of a struct_ops map can be exported through a pointer to a
 * shadow type. The user can access these members through the pointer.
 *
 * A shadow type includes not all members, only members of some types.
 * They are scalar types and function pointers. The function pointers are
 * translated to the pointer of the struct bpf_program. The scalar types
 * are translated to the original type without any modifiers.
 *
 * Unsupported types will be translated to a char array to occupy the same
 * space as the original field, being renamed as __unsupported_*.  The user
 * should treat these fields as opaque data.
 */
static int gen_st_ops_shadow_type(const char *obj_name, struct btf *btf, const char *ident,
				  const struct bpf_map *map)
{
	const struct btf_type *map_type;
	const char *type_name;
	__u32 map_type_id;
	int err;

	map_type_id = bpf_map__btf_value_type_id(map);
	if (map_type_id == 0)
		return -EINVAL;
	map_type = btf__type_by_id(btf, map_type_id);
	if (!map_type)
		return -EINVAL;

	type_name = btf__name_by_offset(btf, map_type->name_off);

	printf("\t\tstruct %s__%s__%s {\n", obj_name, ident, type_name);

	err = walk_st_ops_shadow_vars(btf, ident, map_type, map_type_id);
	if (err)
		return err;

	printf("\t\t} *%s;\n", ident);

	return 0;
}

static int gen_st_ops_shadow(const char *obj_name, struct btf *btf, struct bpf_object *obj)
{
	int err, st_ops_cnt = 0;
	struct bpf_map *map;
	char ident[256];

	if (!btf)
		return 0;

	/* Generate the pointers to shadow types of
	 * struct_ops maps.
	 */
	bpf_object__for_each_map(map, obj) {
		if (bpf_map__type(map) != BPF_MAP_TYPE_STRUCT_OPS)
			continue;
		if (!get_map_ident(map, ident, sizeof(ident)))
			continue;

		if (st_ops_cnt == 0) /* first struct_ops map */
			printf("\tstruct {\n");
		st_ops_cnt++;

		err = gen_st_ops_shadow_type(obj_name, btf, ident, map);
		if (err)
			return err;
	}

	if (st_ops_cnt)
		printf("\t} struct_ops;\n");

	return 0;
}

/* Generate the code to initialize the pointers of shadow types. */
static void gen_st_ops_shadow_init(struct btf *btf, struct bpf_object *obj)
{
	struct bpf_map *map;
	char ident[256];

	if (!btf)
		return;

	/* Initialize the pointers to_ops shadow types of
	 * struct_ops maps.
	 */
	bpf_object__for_each_map(map, obj) {
		if (bpf_map__type(map) != BPF_MAP_TYPE_STRUCT_OPS)
			continue;
		if (!get_map_ident(map, ident, sizeof(ident)))
			continue;
		codegen("\
			\n\
				obj->struct_ops.%1$s = (__typeof__(obj->struct_ops.%1$s))\n\
					bpf_map__initial_value(obj->maps.%1$s, NULL);\n\
			\n\
			", ident);
	}
}

static int do_skeleton(int argc, char **argv)
{
	char header_guard[MAX_OBJ_NAME_LEN + sizeof("__SKEL_H__")];
	size_t map_cnt = 0, prog_cnt = 0, attach_map_cnt = 0, file_sz, mmap_sz;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
	char obj_name[MAX_OBJ_NAME_LEN] = "", *obj_data;
	struct bpf_object *obj = NULL;
	const char *file;
	char ident[256];
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
	if (verifier_logs)
		/* log_level1 + log_level2 + stats, but not stable UAPI */
		opts.kernel_log_level = 1 + 2 + 4;
	obj = bpf_object__open_mem(obj_data, file_sz, &opts);
	if (!obj) {
		char err_buf[256];

		err = -errno;
		libbpf_strerror(err, err_buf, sizeof(err_buf));
		p_err("failed to open BPF object file: %s", err_buf);
		goto out_obj;
	}

	bpf_object__for_each_map(map, obj) {
		if (!get_map_ident(map, ident, sizeof(ident))) {
			p_err("ignoring unrecognized internal map '%s'...",
			      bpf_map__name(map));
			continue;
		}

		if (bpf_map__type(map) == BPF_MAP_TYPE_STRUCT_OPS)
			attach_map_cnt++;

		map_cnt++;
	}
	bpf_object__for_each_program(prog, obj) {
		prog_cnt++;
	}

	get_header_guard(header_guard, obj_name, "SKEL_H");
	if (use_loader) {
		codegen("\
		\n\
		/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */   \n\
		/* THIS FILE IS AUTOGENERATED BY BPFTOOL! */		    \n\
		#ifndef %2$s						    \n\
		#define %2$s						    \n\
									    \n\
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
		/* THIS FILE IS AUTOGENERATED BY BPFTOOL! */		    \n\
		#ifndef %2$s						    \n\
		#define %2$s						    \n\
									    \n\
		#include <errno.h>					    \n\
		#include <stdlib.h>					    \n\
		#include <bpf/libbpf.h>					    \n\
									    \n\
		#define BPF_SKEL_SUPPORTS_MAP_AUTO_ATTACH 1		    \n\
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
			if (!get_map_ident(map, ident, sizeof(ident)))
				continue;
			if (use_loader)
				printf("\t\tstruct bpf_map_desc %s;\n", ident);
			else
				printf("\t\tstruct bpf_map *%s;\n", ident);
		}
		printf("\t} maps;\n");
	}

	btf = bpf_object__btf(obj);
	err = gen_st_ops_shadow(obj_name, btf, obj);
	if (err)
		goto out;

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
	}

	if (prog_cnt + attach_map_cnt) {
		printf("\tstruct {\n");
		bpf_object__for_each_program(prog, obj) {
			if (use_loader)
				printf("\t\tint %s_fd;\n",
				       bpf_program__name(prog));
			else
				printf("\t\tstruct bpf_link *%s;\n",
				       bpf_program__name(prog));
		}

		bpf_object__for_each_map(map, obj) {
			if (!get_map_ident(map, ident, sizeof(ident)))
				continue;
			if (bpf_map__type(map) != BPF_MAP_TYPE_STRUCT_OPS)
				continue;

			if (use_loader)
				printf("t\tint %s_fd;\n", ident);
			else
				printf("\t\tstruct bpf_link *%s;\n", ident);
		}

		printf("\t} links;\n");
	}

	if (sign_progs) {
		codegen("\
		\n\
			__s32 keyring_id;				   \n\
		");
	}

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
									    \n\
		#ifdef __cplusplus					    \n\
			static inline struct %1$s *open(const struct bpf_object_open_opts *opts = nullptr);\n\
			static inline struct %1$s *open_and_load();	    \n\
			static inline int load(struct %1$s *skel);	    \n\
			static inline int attach(struct %1$s *skel);	    \n\
			static inline void detach(struct %1$s *skel);	    \n\
			static inline void destroy(struct %1$s *skel);	    \n\
			static inline const void *elf_bytes(size_t *sz);    \n\
		#endif /* __cplusplus */				    \n\
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
			int err;					    \n\
									    \n\
			obj = (struct %1$s *)calloc(1, sizeof(*obj));	    \n\
			if (!obj) {					    \n\
				errno = ENOMEM;				    \n\
				return NULL;				    \n\
			}						    \n\
									    \n\
			err = %1$s__create_skeleton(obj);		    \n\
			if (err)					    \n\
				goto err_out;				    \n\
									    \n\
			err = bpf_object__open_skeleton(obj->skeleton, opts);\n\
			if (err)					    \n\
				goto err_out;				    \n\
									    \n\
		", obj_name);

	gen_st_ops_shadow_init(btf, obj);

	codegen("\
		\n\
			return obj;					    \n\
		err_out:						    \n\
			%1$s__destroy(obj);				    \n\
			errno = -err;					    \n\
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
			int err;					    \n\
									    \n\
			obj = %1$s__open();				    \n\
			if (!obj)					    \n\
				return NULL;				    \n\
			err = %1$s__load(obj);				    \n\
			if (err) {					    \n\
				%1$s__destroy(obj);			    \n\
				errno = -err;				    \n\
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
			bpf_object__detach_skeleton(obj->skeleton);	    \n\
		}							    \n\
		",
		obj_name
	);

	codegen("\
		\n\
									    \n\
		static inline const void *%1$s__elf_bytes(size_t *sz);	    \n\
									    \n\
		static inline int					    \n\
		%1$s__create_skeleton(struct %1$s *obj)			    \n\
		{							    \n\
			struct bpf_object_skeleton *s;			    \n\
			struct bpf_map_skeleton *map __attribute__((unused));\n\
			int err;					    \n\
									    \n\
			s = (struct bpf_object_skeleton *)calloc(1, sizeof(*s));\n\
			if (!s)	{					    \n\
				err = -ENOMEM;				    \n\
				goto err;				    \n\
			}						    \n\
									    \n\
			s->sz = sizeof(*s);				    \n\
			s->name = \"%1$s\";				    \n\
			s->obj = &obj->obj;				    \n\
		",
		obj_name
	);

	codegen_maps_skeleton(obj, map_cnt, true /*mmaped*/, true /*links*/);
	codegen_progs_skeleton(obj, prog_cnt, true /*populate_links*/);

	codegen("\
		\n\
									    \n\
			s->data = %1$s__elf_bytes(&s->data_sz);		    \n\
									    \n\
			obj->skeleton = s;				    \n\
			return 0;					    \n\
		err:							    \n\
			bpf_object__destroy_skeleton(s);		    \n\
			return err;					    \n\
		}							    \n\
									    \n\
		static inline const void *%1$s__elf_bytes(size_t *sz)	    \n\
		{							    \n\
			static const char data[] __attribute__((__aligned__(8))) = \"\\\n\
		",
		obj_name
	);

	/* embed contents of BPF object file */
	print_hex(obj_data, file_sz);

	codegen("\
		\n\
		\";							    \n\
									    \n\
			*sz = sizeof(data) - 1;				    \n\
			return (const void *)data;			    \n\
		}							    \n\
									    \n\
		#ifdef __cplusplus					    \n\
		struct %1$s *%1$s::open(const struct bpf_object_open_opts *opts) { return %1$s__open_opts(opts); }\n\
		struct %1$s *%1$s::open_and_load() { return %1$s__open_and_load(); }	\n\
		int %1$s::load(struct %1$s *skel) { return %1$s__load(skel); }		\n\
		int %1$s::attach(struct %1$s *skel) { return %1$s__attach(skel); }	\n\
		void %1$s::detach(struct %1$s *skel) { %1$s__detach(skel); }		\n\
		void %1$s::destroy(struct %1$s *skel) { %1$s__destroy(skel); }		\n\
		const void *%1$s::elf_bytes(size_t *sz) { return %1$s__elf_bytes(sz); } \n\
		#endif /* __cplusplus */				    \n\
									    \n\
		",
		obj_name);

	codegen_asserts(obj, obj_name);

	codegen("\
		\n\
									    \n\
		#endif /* %1$s */					    \n\
		",
		header_guard);
	err = 0;
out:
	bpf_object__close(obj);
out_obj:
	if (obj_data)
		munmap(obj_data, mmap_sz);
	close(fd);
	return err;
}

/* Subskeletons are like skeletons, except they don't own the bpf_object,
 * associated maps, links, etc. Instead, they know about the existence of
 * variables, maps, programs and are able to find their locations
 * _at runtime_ from an already loaded bpf_object.
 *
 * This allows for library-like BPF objects to have userspace counterparts
 * with access to their own items without having to know anything about the
 * final BPF object that the library was linked into.
 */
static int do_subskeleton(int argc, char **argv)
{
	char header_guard[MAX_OBJ_NAME_LEN + sizeof("__SUBSKEL_H__")];
	size_t i, len, file_sz, map_cnt = 0, prog_cnt = 0, mmap_sz, var_cnt = 0, var_idx = 0;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
	char obj_name[MAX_OBJ_NAME_LEN] = "", *obj_data;
	struct bpf_object *obj = NULL;
	const char *file, *var_name;
	char ident[256];
	int fd, err = -1, map_type_id;
	const struct bpf_map *map;
	struct bpf_program *prog;
	struct btf *btf;
	const struct btf_type *map_type, *var_type;
	const struct btf_var_secinfo *var;
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

	if (use_loader) {
		p_err("cannot use loader for subskeletons");
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

	/* The empty object name allows us to use bpf_map__name and produce
	 * ELF section names out of it. (".data" instead of "obj.data")
	 */
	opts.object_name = "";
	obj = bpf_object__open_mem(obj_data, file_sz, &opts);
	if (!obj) {
		char err_buf[256];

		libbpf_strerror(errno, err_buf, sizeof(err_buf));
		p_err("failed to open BPF object file: %s", err_buf);
		obj = NULL;
		goto out;
	}

	btf = bpf_object__btf(obj);
	if (!btf) {
		err = -1;
		p_err("need btf type information for %s", obj_name);
		goto out;
	}

	bpf_object__for_each_program(prog, obj) {
		prog_cnt++;
	}

	/* First, count how many variables we have to find.
	 * We need this in advance so the subskel can allocate the right
	 * amount of storage.
	 */
	bpf_object__for_each_map(map, obj) {
		if (!get_map_ident(map, ident, sizeof(ident)))
			continue;

		/* Also count all maps that have a name */
		map_cnt++;

		if (!is_mmapable_map(map, ident, sizeof(ident)))
			continue;

		map_type_id = bpf_map__btf_value_type_id(map);
		if (map_type_id <= 0) {
			err = map_type_id;
			goto out;
		}
		map_type = btf__type_by_id(btf, map_type_id);

		var = btf_var_secinfos(map_type);
		len = btf_vlen(map_type);
		for (i = 0; i < len; i++, var++) {
			var_type = btf__type_by_id(btf, var->type);

			if (btf_var(var_type)->linkage == BTF_VAR_STATIC)
				continue;

			var_cnt++;
		}
	}

	get_header_guard(header_guard, obj_name, "SUBSKEL_H");
	codegen("\
	\n\
	/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */	    \n\
									    \n\
	/* THIS FILE IS AUTOGENERATED! */				    \n\
	#ifndef %2$s							    \n\
	#define %2$s							    \n\
									    \n\
	#include <errno.h>						    \n\
	#include <stdlib.h>						    \n\
	#include <bpf/libbpf.h>						    \n\
									    \n\
	struct %1$s {							    \n\
		struct bpf_object *obj;					    \n\
		struct bpf_object_subskeleton *subskel;			    \n\
	", obj_name, header_guard);

	if (map_cnt) {
		printf("\tstruct {\n");
		bpf_object__for_each_map(map, obj) {
			if (!get_map_ident(map, ident, sizeof(ident)))
				continue;
			printf("\t\tstruct bpf_map *%s;\n", ident);
		}
		printf("\t} maps;\n");
	}

	err = gen_st_ops_shadow(obj_name, btf, obj);
	if (err)
		goto out;

	if (prog_cnt) {
		printf("\tstruct {\n");
		bpf_object__for_each_program(prog, obj) {
			printf("\t\tstruct bpf_program *%s;\n",
				bpf_program__name(prog));
		}
		printf("\t} progs;\n");
	}

	err = codegen_subskel_datasecs(obj, obj_name);
	if (err)
		goto out;

	/* emit code that will allocate enough storage for all symbols */
	codegen("\
		\n\
									    \n\
		#ifdef __cplusplus					    \n\
			static inline struct %1$s *open(const struct bpf_object *src);\n\
			static inline void destroy(struct %1$s *skel);	    \n\
		#endif /* __cplusplus */				    \n\
		};							    \n\
									    \n\
		static inline void					    \n\
		%1$s__destroy(struct %1$s *skel)			    \n\
		{							    \n\
			if (!skel)					    \n\
				return;					    \n\
			if (skel->subskel)				    \n\
				bpf_object__destroy_subskeleton(skel->subskel);\n\
			free(skel);					    \n\
		}							    \n\
									    \n\
		static inline struct %1$s *				    \n\
		%1$s__open(const struct bpf_object *src)		    \n\
		{							    \n\
			struct %1$s *obj;				    \n\
			struct bpf_object_subskeleton *s;		    \n\
			struct bpf_map_skeleton *map __attribute__((unused));\n\
			int err;					    \n\
									    \n\
			obj = (struct %1$s *)calloc(1, sizeof(*obj));	    \n\
			if (!obj) {					    \n\
				err = -ENOMEM;				    \n\
				goto err;				    \n\
			}						    \n\
			s = (struct bpf_object_subskeleton *)calloc(1, sizeof(*s));\n\
			if (!s) {					    \n\
				err = -ENOMEM;				    \n\
				goto err;				    \n\
			}						    \n\
			s->sz = sizeof(*s);				    \n\
			s->obj = src;					    \n\
			s->var_skel_sz = sizeof(*s->vars);		    \n\
			obj->subskel = s;				    \n\
									    \n\
			/* vars */					    \n\
			s->var_cnt = %2$d;				    \n\
			s->vars = (struct bpf_var_skeleton *)calloc(%2$d, sizeof(*s->vars));\n\
			if (!s->vars) {					    \n\
				err = -ENOMEM;				    \n\
				goto err;				    \n\
			}						    \n\
		",
		obj_name, var_cnt
	);

	/* walk through each symbol and emit the runtime representation */
	bpf_object__for_each_map(map, obj) {
		if (!is_mmapable_map(map, ident, sizeof(ident)))
			continue;

		map_type_id = bpf_map__btf_value_type_id(map);
		if (map_type_id <= 0)
			/* skip over internal maps with no type*/
			continue;

		map_type = btf__type_by_id(btf, map_type_id);
		var = btf_var_secinfos(map_type);
		len = btf_vlen(map_type);
		for (i = 0; i < len; i++, var++) {
			var_type = btf__type_by_id(btf, var->type);
			var_name = btf__name_by_offset(btf, var_type->name_off);

			if (btf_var(var_type)->linkage == BTF_VAR_STATIC)
				continue;

			/* Note that we use the dot prefix in .data as the
			 * field access operator i.e. maps%s becomes maps.data
			 */
			codegen("\
			\n\
									    \n\
				s->vars[%3$d].name = \"%1$s\";		    \n\
				s->vars[%3$d].map = &obj->maps.%2$s;	    \n\
				s->vars[%3$d].addr = (void **) &obj->%2$s.%1$s;\n\
			", var_name, ident, var_idx);

			var_idx++;
		}
	}

	codegen_maps_skeleton(obj, map_cnt, false /*mmaped*/, false /*links*/);
	codegen_progs_skeleton(obj, prog_cnt, false /*links*/);

	codegen("\
		\n\
									    \n\
			err = bpf_object__open_subskeleton(s);		    \n\
			if (err)					    \n\
				goto err;				    \n\
									    \n\
		");

	gen_st_ops_shadow_init(btf, obj);

	codegen("\
		\n\
			return obj;					    \n\
		err:							    \n\
			%1$s__destroy(obj);				    \n\
			errno = -err;					    \n\
			return NULL;					    \n\
		}							    \n\
									    \n\
		#ifdef __cplusplus					    \n\
		struct %1$s *%1$s::open(const struct bpf_object *src) { return %1$s__open(src); }\n\
		void %1$s::destroy(struct %1$s *skel) { %1$s__destroy(skel); }\n\
		#endif /* __cplusplus */				    \n\
									    \n\
		#endif /* %2$s */					    \n\
		",
		obj_name, header_guard);
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
			p_err("failed to link '%s': %s (%d)", file, strerror(errno), errno);
			goto out;
		}
	}

	err = bpf_linker__finalize(linker);
	if (err) {
		p_err("failed to finalize ELF file: %s (%d)", strerror(errno), errno);
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
		"       %1$s %2$s subskeleton FILE [name OBJECT_NAME]\n"
		"       %1$s %2$s min_core_btf INPUT OUTPUT OBJECT [OBJECT...]\n"
		"       %1$s %2$s help\n"
		"\n"
		"       " HELP_SPEC_OPTIONS " |\n"
		"                    {-L|--use-loader} | [ {-S|--sign } {-k} <private_key.pem> {-i} <certificate.x509> ]}\n"
		"",
		bin_name, "gen");

	return 0;
}

static int btf_save_raw(const struct btf *btf, const char *path)
{
	const void *data;
	FILE *f = NULL;
	__u32 data_sz;
	int err = 0;

	data = btf__raw_data(btf, &data_sz);
	if (!data)
		return -ENOMEM;

	f = fopen(path, "wb");
	if (!f)
		return -errno;

	if (fwrite(data, 1, data_sz, f) != data_sz)
		err = -errno;

	fclose(f);
	return err;
}

struct btfgen_info {
	struct btf *src_btf;
	struct btf *marked_btf; /* btf structure used to mark used types */
};

static size_t btfgen_hash_fn(long key, void *ctx)
{
	return key;
}

static bool btfgen_equal_fn(long k1, long k2, void *ctx)
{
	return k1 == k2;
}

static void btfgen_free_info(struct btfgen_info *info)
{
	if (!info)
		return;

	btf__free(info->src_btf);
	btf__free(info->marked_btf);

	free(info);
}

static struct btfgen_info *
btfgen_new_info(const char *targ_btf_path)
{
	struct btfgen_info *info;
	int err;

	info = calloc(1, sizeof(*info));
	if (!info)
		return NULL;

	info->src_btf = btf__parse(targ_btf_path, NULL);
	if (!info->src_btf) {
		err = -errno;
		p_err("failed parsing '%s' BTF file: %s", targ_btf_path, strerror(errno));
		goto err_out;
	}

	info->marked_btf = btf__parse(targ_btf_path, NULL);
	if (!info->marked_btf) {
		err = -errno;
		p_err("failed parsing '%s' BTF file: %s", targ_btf_path, strerror(errno));
		goto err_out;
	}

	return info;

err_out:
	btfgen_free_info(info);
	errno = -err;
	return NULL;
}

#define MARKED UINT32_MAX

static void btfgen_mark_member(struct btfgen_info *info, int type_id, int idx)
{
	const struct btf_type *t = btf__type_by_id(info->marked_btf, type_id);
	struct btf_member *m = btf_members(t) + idx;

	m->name_off = MARKED;
}

static int
btfgen_mark_type(struct btfgen_info *info, unsigned int type_id, bool follow_pointers)
{
	const struct btf_type *btf_type = btf__type_by_id(info->src_btf, type_id);
	struct btf_type *cloned_type;
	struct btf_param *param;
	struct btf_array *array;
	int err, i;

	if (type_id == 0)
		return 0;

	/* mark type on cloned BTF as used */
	cloned_type = (struct btf_type *) btf__type_by_id(info->marked_btf, type_id);
	cloned_type->name_off = MARKED;

	/* recursively mark other types needed by it */
	switch (btf_kind(btf_type)) {
	case BTF_KIND_UNKN:
	case BTF_KIND_INT:
	case BTF_KIND_FLOAT:
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
		break;
	case BTF_KIND_PTR:
		if (follow_pointers) {
			err = btfgen_mark_type(info, btf_type->type, follow_pointers);
			if (err)
				return err;
		}
		break;
	case BTF_KIND_CONST:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_TYPEDEF:
		err = btfgen_mark_type(info, btf_type->type, follow_pointers);
		if (err)
			return err;
		break;
	case BTF_KIND_ARRAY:
		array = btf_array(btf_type);

		/* mark array type */
		err = btfgen_mark_type(info, array->type, follow_pointers);
		/* mark array's index type */
		err = err ? : btfgen_mark_type(info, array->index_type, follow_pointers);
		if (err)
			return err;
		break;
	case BTF_KIND_FUNC_PROTO:
		/* mark ret type */
		err = btfgen_mark_type(info, btf_type->type, follow_pointers);
		if (err)
			return err;

		/* mark parameters types */
		param = btf_params(btf_type);
		for (i = 0; i < btf_vlen(btf_type); i++) {
			err = btfgen_mark_type(info, param->type, follow_pointers);
			if (err)
				return err;
			param++;
		}
		break;
	/* tells if some other type needs to be handled */
	default:
		p_err("unsupported kind: %s (%u)", btf_kind_str(btf_type), type_id);
		return -EINVAL;
	}

	return 0;
}

static int btfgen_record_field_relo(struct btfgen_info *info, struct bpf_core_spec *targ_spec)
{
	struct btf *btf = info->src_btf;
	const struct btf_type *btf_type;
	struct btf_member *btf_member;
	struct btf_array *array;
	unsigned int type_id = targ_spec->root_type_id;
	int idx, err;

	/* mark root type */
	btf_type = btf__type_by_id(btf, type_id);
	err = btfgen_mark_type(info, type_id, false);
	if (err)
		return err;

	/* mark types for complex types (arrays, unions, structures) */
	for (int i = 1; i < targ_spec->raw_len; i++) {
		/* skip typedefs and mods */
		while (btf_is_mod(btf_type) || btf_is_typedef(btf_type)) {
			type_id = btf_type->type;
			btf_type = btf__type_by_id(btf, type_id);
		}

		switch (btf_kind(btf_type)) {
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
			idx = targ_spec->raw_spec[i];
			btf_member = btf_members(btf_type) + idx;

			/* mark member */
			btfgen_mark_member(info, type_id, idx);

			/* mark member's type */
			type_id = btf_member->type;
			btf_type = btf__type_by_id(btf, type_id);
			err = btfgen_mark_type(info, type_id, false);
			if (err)
				return err;
			break;
		case BTF_KIND_ARRAY:
			array = btf_array(btf_type);
			type_id = array->type;
			btf_type = btf__type_by_id(btf, type_id);
			break;
		default:
			p_err("unsupported kind: %s (%u)",
			      btf_kind_str(btf_type), btf_type->type);
			return -EINVAL;
		}
	}

	return 0;
}

/* Mark types, members, and member types. Compared to btfgen_record_field_relo,
 * this function does not rely on the target spec for inferring members, but
 * uses the associated BTF.
 *
 * The `behind_ptr` argument is used to stop marking of composite types reached
 * through a pointer. This way, we can keep BTF size in check while providing
 * reasonable match semantics.
 */
static int btfgen_mark_type_match(struct btfgen_info *info, __u32 type_id, bool behind_ptr)
{
	const struct btf_type *btf_type;
	struct btf *btf = info->src_btf;
	struct btf_type *cloned_type;
	int i, err;

	if (type_id == 0)
		return 0;

	btf_type = btf__type_by_id(btf, type_id);
	/* mark type on cloned BTF as used */
	cloned_type = (struct btf_type *)btf__type_by_id(info->marked_btf, type_id);
	cloned_type->name_off = MARKED;

	switch (btf_kind(btf_type)) {
	case BTF_KIND_UNKN:
	case BTF_KIND_INT:
	case BTF_KIND_FLOAT:
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		break;
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		struct btf_member *m = btf_members(btf_type);
		__u16 vlen = btf_vlen(btf_type);

		if (behind_ptr)
			break;

		for (i = 0; i < vlen; i++, m++) {
			/* mark member */
			btfgen_mark_member(info, type_id, i);

			/* mark member's type */
			err = btfgen_mark_type_match(info, m->type, false);
			if (err)
				return err;
		}
		break;
	}
	case BTF_KIND_CONST:
	case BTF_KIND_FWD:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_VOLATILE:
		return btfgen_mark_type_match(info, btf_type->type, behind_ptr);
	case BTF_KIND_PTR:
		return btfgen_mark_type_match(info, btf_type->type, true);
	case BTF_KIND_ARRAY: {
		struct btf_array *array;

		array = btf_array(btf_type);
		/* mark array type */
		err = btfgen_mark_type_match(info, array->type, false);
		/* mark array's index type */
		err = err ? : btfgen_mark_type_match(info, array->index_type, false);
		if (err)
			return err;
		break;
	}
	case BTF_KIND_FUNC_PROTO: {
		__u16 vlen = btf_vlen(btf_type);
		struct btf_param *param;

		/* mark ret type */
		err = btfgen_mark_type_match(info, btf_type->type, false);
		if (err)
			return err;

		/* mark parameters types */
		param = btf_params(btf_type);
		for (i = 0; i < vlen; i++) {
			err = btfgen_mark_type_match(info, param->type, false);
			if (err)
				return err;
			param++;
		}
		break;
	}
	/* tells if some other type needs to be handled */
	default:
		p_err("unsupported kind: %s (%u)", btf_kind_str(btf_type), type_id);
		return -EINVAL;
	}

	return 0;
}

/* Mark types, members, and member types. Compared to btfgen_record_field_relo,
 * this function does not rely on the target spec for inferring members, but
 * uses the associated BTF.
 */
static int btfgen_record_type_match_relo(struct btfgen_info *info, struct bpf_core_spec *targ_spec)
{
	return btfgen_mark_type_match(info, targ_spec->root_type_id, false);
}

static int btfgen_record_type_relo(struct btfgen_info *info, struct bpf_core_spec *targ_spec)
{
	return btfgen_mark_type(info, targ_spec->root_type_id, true);
}

static int btfgen_record_enumval_relo(struct btfgen_info *info, struct bpf_core_spec *targ_spec)
{
	return btfgen_mark_type(info, targ_spec->root_type_id, false);
}

static int btfgen_record_reloc(struct btfgen_info *info, struct bpf_core_spec *res)
{
	switch (res->relo_kind) {
	case BPF_CORE_FIELD_BYTE_OFFSET:
	case BPF_CORE_FIELD_BYTE_SIZE:
	case BPF_CORE_FIELD_EXISTS:
	case BPF_CORE_FIELD_SIGNED:
	case BPF_CORE_FIELD_LSHIFT_U64:
	case BPF_CORE_FIELD_RSHIFT_U64:
		return btfgen_record_field_relo(info, res);
	case BPF_CORE_TYPE_ID_LOCAL: /* BPF_CORE_TYPE_ID_LOCAL doesn't require kernel BTF */
		return 0;
	case BPF_CORE_TYPE_ID_TARGET:
	case BPF_CORE_TYPE_EXISTS:
	case BPF_CORE_TYPE_SIZE:
		return btfgen_record_type_relo(info, res);
	case BPF_CORE_TYPE_MATCHES:
		return btfgen_record_type_match_relo(info, res);
	case BPF_CORE_ENUMVAL_EXISTS:
	case BPF_CORE_ENUMVAL_VALUE:
		return btfgen_record_enumval_relo(info, res);
	default:
		return -EINVAL;
	}
}

static struct bpf_core_cand_list *
btfgen_find_cands(const struct btf *local_btf, const struct btf *targ_btf, __u32 local_id)
{
	const struct btf_type *local_type;
	struct bpf_core_cand_list *cands = NULL;
	struct bpf_core_cand local_cand = {};
	size_t local_essent_len;
	const char *local_name;
	int err;

	local_cand.btf = local_btf;
	local_cand.id = local_id;

	local_type = btf__type_by_id(local_btf, local_id);
	if (!local_type) {
		err = -EINVAL;
		goto err_out;
	}

	local_name = btf__name_by_offset(local_btf, local_type->name_off);
	if (!local_name) {
		err = -EINVAL;
		goto err_out;
	}
	local_essent_len = bpf_core_essential_name_len(local_name);

	cands = calloc(1, sizeof(*cands));
	if (!cands)
		return NULL;

	err = bpf_core_add_cands(&local_cand, local_essent_len, targ_btf, "vmlinux", 1, cands);
	if (err)
		goto err_out;

	return cands;

err_out:
	bpf_core_free_cands(cands);
	errno = -err;
	return NULL;
}

/* Record relocation information for a single BPF object */
static int btfgen_record_obj(struct btfgen_info *info, const char *obj_path)
{
	const struct btf_ext_info_sec *sec;
	const struct bpf_core_relo *relo;
	const struct btf_ext_info *seg;
	struct hashmap_entry *entry;
	struct hashmap *cand_cache = NULL;
	struct btf_ext *btf_ext = NULL;
	unsigned int relo_idx;
	struct btf *btf = NULL;
	size_t i;
	int err;

	btf = btf__parse(obj_path, &btf_ext);
	if (!btf) {
		err = -errno;
		p_err("failed to parse BPF object '%s': %s", obj_path, strerror(errno));
		return err;
	}

	if (!btf_ext) {
		p_err("failed to parse BPF object '%s': section %s not found",
		      obj_path, BTF_EXT_ELF_SEC);
		err = -EINVAL;
		goto out;
	}

	if (btf_ext->core_relo_info.len == 0) {
		err = 0;
		goto out;
	}

	cand_cache = hashmap__new(btfgen_hash_fn, btfgen_equal_fn, NULL);
	if (IS_ERR(cand_cache)) {
		err = PTR_ERR(cand_cache);
		goto out;
	}

	seg = &btf_ext->core_relo_info;
	for_each_btf_ext_sec(seg, sec) {
		for_each_btf_ext_rec(seg, sec, relo_idx, relo) {
			struct bpf_core_spec specs_scratch[3] = {};
			struct bpf_core_relo_res targ_res = {};
			struct bpf_core_cand_list *cands = NULL;
			const char *sec_name = btf__name_by_offset(btf, sec->sec_name_off);

			if (relo->kind != BPF_CORE_TYPE_ID_LOCAL &&
			    !hashmap__find(cand_cache, relo->type_id, &cands)) {
				cands = btfgen_find_cands(btf, info->src_btf, relo->type_id);
				if (!cands) {
					err = -errno;
					goto out;
				}

				err = hashmap__set(cand_cache, relo->type_id, cands,
						   NULL, NULL);
				if (err)
					goto out;
			}

			err = bpf_core_calc_relo_insn(sec_name, relo, relo_idx, btf, cands,
						      specs_scratch, &targ_res);
			if (err)
				goto out;

			/* specs_scratch[2] is the target spec */
			err = btfgen_record_reloc(info, &specs_scratch[2]);
			if (err)
				goto out;
		}
	}

out:
	btf__free(btf);
	btf_ext__free(btf_ext);

	if (!IS_ERR_OR_NULL(cand_cache)) {
		hashmap__for_each_entry(cand_cache, entry, i) {
			bpf_core_free_cands(entry->pvalue);
		}
		hashmap__free(cand_cache);
	}

	return err;
}

/* Generate BTF from relocation information previously recorded */
static struct btf *btfgen_get_btf(struct btfgen_info *info)
{
	struct btf *btf_new = NULL;
	unsigned int *ids = NULL;
	unsigned int i, n = btf__type_cnt(info->marked_btf);
	int err = 0;

	btf_new = btf__new_empty();
	if (!btf_new) {
		err = -errno;
		goto err_out;
	}

	ids = calloc(n, sizeof(*ids));
	if (!ids) {
		err = -errno;
		goto err_out;
	}

	/* first pass: add all marked types to btf_new and add their new ids to the ids map */
	for (i = 1; i < n; i++) {
		const struct btf_type *cloned_type, *type;
		const char *name;
		int new_id;

		cloned_type = btf__type_by_id(info->marked_btf, i);

		if (cloned_type->name_off != MARKED)
			continue;

		type = btf__type_by_id(info->src_btf, i);

		/* add members for struct and union */
		if (btf_is_composite(type)) {
			struct btf_member *cloned_m, *m;
			unsigned short vlen;
			int idx_src;

			name = btf__str_by_offset(info->src_btf, type->name_off);

			if (btf_is_struct(type))
				err = btf__add_struct(btf_new, name, type->size);
			else
				err = btf__add_union(btf_new, name, type->size);

			if (err < 0)
				goto err_out;
			new_id = err;

			cloned_m = btf_members(cloned_type);
			m = btf_members(type);
			vlen = btf_vlen(cloned_type);
			for (idx_src = 0; idx_src < vlen; idx_src++, cloned_m++, m++) {
				/* add only members that are marked as used */
				if (cloned_m->name_off != MARKED)
					continue;

				name = btf__str_by_offset(info->src_btf, m->name_off);
				err = btf__add_field(btf_new, name, m->type,
						     btf_member_bit_offset(cloned_type, idx_src),
						     btf_member_bitfield_size(cloned_type, idx_src));
				if (err < 0)
					goto err_out;
			}
		} else {
			err = btf__add_type(btf_new, info->src_btf, type);
			if (err < 0)
				goto err_out;
			new_id = err;
		}

		/* add ID mapping */
		ids[i] = new_id;
	}

	/* second pass: fix up type ids */
	for (i = 1; i < btf__type_cnt(btf_new); i++) {
		struct btf_type *btf_type = (struct btf_type *) btf__type_by_id(btf_new, i);
		struct btf_field_iter it;
		__u32 *type_id;

		err = btf_field_iter_init(&it, btf_type, BTF_FIELD_ITER_IDS);
		if (err)
			goto err_out;

		while ((type_id = btf_field_iter_next(&it)))
			*type_id = ids[*type_id];
	}

	free(ids);
	return btf_new;

err_out:
	btf__free(btf_new);
	free(ids);
	errno = -err;
	return NULL;
}

/* Create minimized BTF file for a set of BPF objects.
 *
 * The BTFGen algorithm is divided in two main parts: (1) collect the
 * BTF types that are involved in relocations and (2) generate the BTF
 * object using the collected types.
 *
 * In order to collect the types involved in the relocations, we parse
 * the BTF and BTF.ext sections of the BPF objects and use
 * bpf_core_calc_relo_insn() to get the target specification, this
 * indicates how the types and fields are used in a relocation.
 *
 * Types are recorded in different ways according to the kind of the
 * relocation. For field-based relocations only the members that are
 * actually used are saved in order to reduce the size of the generated
 * BTF file. For type-based relocations empty struct / unions are
 * generated and for enum-based relocations the whole type is saved.
 *
 * The second part of the algorithm generates the BTF object. It creates
 * an empty BTF object and fills it with the types recorded in the
 * previous step. This function takes care of only adding the structure
 * and union members that were marked as used and it also fixes up the
 * type IDs on the generated BTF object.
 */
static int minimize_btf(const char *src_btf, const char *dst_btf, const char *objspaths[])
{
	struct btfgen_info *info;
	struct btf *btf_new = NULL;
	int err, i;

	info = btfgen_new_info(src_btf);
	if (!info) {
		err = -errno;
		p_err("failed to allocate info structure: %s", strerror(errno));
		goto out;
	}

	for (i = 0; objspaths[i] != NULL; i++) {
		err = btfgen_record_obj(info, objspaths[i]);
		if (err) {
			p_err("error recording relocations for %s: %s", objspaths[i],
			      strerror(errno));
			goto out;
		}
	}

	btf_new = btfgen_get_btf(info);
	if (!btf_new) {
		err = -errno;
		p_err("error generating BTF: %s", strerror(errno));
		goto out;
	}

	err = btf_save_raw(btf_new, dst_btf);
	if (err) {
		p_err("error saving btf file: %s", strerror(errno));
		goto out;
	}

out:
	btf__free(btf_new);
	btfgen_free_info(info);

	return err;
}

static int do_min_core_btf(int argc, char **argv)
{
	const char *input, *output, **objs;
	int i, err;

	if (!REQ_ARGS(3)) {
		usage();
		return -1;
	}

	input = GET_ARG();
	output = GET_ARG();

	objs = (const char **) calloc(argc + 1, sizeof(*objs));
	if (!objs) {
		p_err("failed to allocate array for object names");
		return -ENOMEM;
	}

	i = 0;
	while (argc)
		objs[i++] = GET_ARG();

	err = minimize_btf(input, output, objs);
	free(objs);
	return err;
}

static const struct cmd cmds[] = {
	{ "object",		do_object },
	{ "skeleton",		do_skeleton },
	{ "subskeleton",	do_subskeleton },
	{ "min_core_btf",	do_min_core_btf},
	{ "help",		do_help },
	{ 0 }
};

int do_gen(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}

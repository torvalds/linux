// SPDX-License-Identifier: GPL-2.0
/*
 * bpf-loader.c
 *
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 */

#include <linux/bpf.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <errno.h>
#include <stdlib.h>
#include "debug.h"
#include "evlist.h"
#include "bpf-loader.h"
#include "bpf-prologue.h"
#include "probe-event.h"
#include "probe-finder.h" // for MAX_PROBES
#include "parse-events.h"
#include "strfilter.h"
#include "util.h"
#include "llvm-utils.h"
#include "c++/clang-c.h"

#include <internal/xyarray.h>

/* temporarily disable libbpf deprecation warnings */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static int libbpf_perf_print(enum libbpf_print_level level __attribute__((unused)),
			      const char *fmt, va_list args)
{
	return veprintf(1, verbose, pr_fmt(fmt), args);
}

struct bpf_prog_priv {
	bool is_tp;
	char *sys_name;
	char *evt_name;
	struct perf_probe_event pev;
	bool need_prologue;
	struct bpf_insn *insns_buf;
	int nr_types;
	int *type_mapping;
};

struct bpf_perf_object {
	struct list_head list;
	struct bpf_object *obj;
};

static LIST_HEAD(bpf_objects_list);

static struct bpf_perf_object *
bpf_perf_object__next(struct bpf_perf_object *prev)
{
	struct bpf_perf_object *next;

	if (!prev)
		next = list_first_entry(&bpf_objects_list,
					struct bpf_perf_object,
					list);
	else
		next = list_next_entry(prev, list);

	/* Empty list is noticed here so don't need checking on entry. */
	if (&next->list == &bpf_objects_list)
		return NULL;

	return next;
}

#define bpf_perf_object__for_each(perf_obj, tmp)	\
	for ((perf_obj) = bpf_perf_object__next(NULL),	\
	     (tmp) = bpf_perf_object__next(perf_obj);	\
	     (perf_obj) != NULL;			\
	     (perf_obj) = (tmp), (tmp) = bpf_perf_object__next(tmp))

static bool libbpf_initialized;

static int bpf_perf_object__add(struct bpf_object *obj)
{
	struct bpf_perf_object *perf_obj = zalloc(sizeof(*perf_obj));

	if (perf_obj) {
		INIT_LIST_HEAD(&perf_obj->list);
		perf_obj->obj = obj;
		list_add_tail(&perf_obj->list, &bpf_objects_list);
	}
	return perf_obj ? 0 : -ENOMEM;
}

struct bpf_object *
bpf__prepare_load_buffer(void *obj_buf, size_t obj_buf_sz, const char *name)
{
	struct bpf_object *obj;

	if (!libbpf_initialized) {
		libbpf_set_print(libbpf_perf_print);
		libbpf_initialized = true;
	}

	obj = bpf_object__open_buffer(obj_buf, obj_buf_sz, name);
	if (IS_ERR_OR_NULL(obj)) {
		pr_debug("bpf: failed to load buffer\n");
		return ERR_PTR(-EINVAL);
	}

	if (bpf_perf_object__add(obj)) {
		bpf_object__close(obj);
		return ERR_PTR(-ENOMEM);
	}

	return obj;
}

static void bpf_perf_object__close(struct bpf_perf_object *perf_obj)
{
	list_del(&perf_obj->list);
	bpf_object__close(perf_obj->obj);
	free(perf_obj);
}

struct bpf_object *bpf__prepare_load(const char *filename, bool source)
{
	struct bpf_object *obj;

	if (!libbpf_initialized) {
		libbpf_set_print(libbpf_perf_print);
		libbpf_initialized = true;
	}

	if (source) {
		int err;
		void *obj_buf;
		size_t obj_buf_sz;

		perf_clang__init();
		err = perf_clang__compile_bpf(filename, &obj_buf, &obj_buf_sz);
		perf_clang__cleanup();
		if (err) {
			pr_debug("bpf: builtin compilation failed: %d, try external compiler\n", err);
			err = llvm__compile_bpf(filename, &obj_buf, &obj_buf_sz);
			if (err)
				return ERR_PTR(-BPF_LOADER_ERRNO__COMPILE);
		} else
			pr_debug("bpf: successful builtin compilation\n");
		obj = bpf_object__open_buffer(obj_buf, obj_buf_sz, filename);

		if (!IS_ERR_OR_NULL(obj) && llvm_param.dump_obj)
			llvm__dump_obj(filename, obj_buf, obj_buf_sz);

		free(obj_buf);
	} else {
		obj = bpf_object__open(filename);
	}

	if (IS_ERR_OR_NULL(obj)) {
		pr_debug("bpf: failed to load %s\n", filename);
		return obj;
	}

	if (bpf_perf_object__add(obj)) {
		bpf_object__close(obj);
		return ERR_PTR(-BPF_LOADER_ERRNO__COMPILE);
	}

	return obj;
}

void bpf__clear(void)
{
	struct bpf_perf_object *perf_obj, *tmp;

	bpf_perf_object__for_each(perf_obj, tmp) {
		bpf__unprobe(perf_obj->obj);
		bpf_perf_object__close(perf_obj);
	}
}

static void
clear_prog_priv(struct bpf_program *prog __maybe_unused,
		void *_priv)
{
	struct bpf_prog_priv *priv = _priv;

	cleanup_perf_probe_events(&priv->pev, 1);
	zfree(&priv->insns_buf);
	zfree(&priv->type_mapping);
	zfree(&priv->sys_name);
	zfree(&priv->evt_name);
	free(priv);
}

static int
prog_config__exec(const char *value, struct perf_probe_event *pev)
{
	pev->uprobes = true;
	pev->target = strdup(value);
	if (!pev->target)
		return -ENOMEM;
	return 0;
}

static int
prog_config__module(const char *value, struct perf_probe_event *pev)
{
	pev->uprobes = false;
	pev->target = strdup(value);
	if (!pev->target)
		return -ENOMEM;
	return 0;
}

static int
prog_config__bool(const char *value, bool *pbool, bool invert)
{
	int err;
	bool bool_value;

	if (!pbool)
		return -EINVAL;

	err = strtobool(value, &bool_value);
	if (err)
		return err;

	*pbool = invert ? !bool_value : bool_value;
	return 0;
}

static int
prog_config__inlines(const char *value,
		     struct perf_probe_event *pev __maybe_unused)
{
	return prog_config__bool(value, &probe_conf.no_inlines, true);
}

static int
prog_config__force(const char *value,
		   struct perf_probe_event *pev __maybe_unused)
{
	return prog_config__bool(value, &probe_conf.force_add, false);
}

static struct {
	const char *key;
	const char *usage;
	const char *desc;
	int (*func)(const char *, struct perf_probe_event *);
} bpf_prog_config_terms[] = {
	{
		.key	= "exec",
		.usage	= "exec=<full path of file>",
		.desc	= "Set uprobe target",
		.func	= prog_config__exec,
	},
	{
		.key	= "module",
		.usage	= "module=<module name>    ",
		.desc	= "Set kprobe module",
		.func	= prog_config__module,
	},
	{
		.key	= "inlines",
		.usage	= "inlines=[yes|no]        ",
		.desc	= "Probe at inline symbol",
		.func	= prog_config__inlines,
	},
	{
		.key	= "force",
		.usage	= "force=[yes|no]          ",
		.desc	= "Forcibly add events with existing name",
		.func	= prog_config__force,
	},
};

static int
do_prog_config(const char *key, const char *value,
	       struct perf_probe_event *pev)
{
	unsigned int i;

	pr_debug("config bpf program: %s=%s\n", key, value);
	for (i = 0; i < ARRAY_SIZE(bpf_prog_config_terms); i++)
		if (strcmp(key, bpf_prog_config_terms[i].key) == 0)
			return bpf_prog_config_terms[i].func(value, pev);

	pr_debug("BPF: ERROR: invalid program config option: %s=%s\n",
		 key, value);

	pr_debug("\nHint: Valid options are:\n");
	for (i = 0; i < ARRAY_SIZE(bpf_prog_config_terms); i++)
		pr_debug("\t%s:\t%s\n", bpf_prog_config_terms[i].usage,
			 bpf_prog_config_terms[i].desc);
	pr_debug("\n");

	return -BPF_LOADER_ERRNO__PROGCONF_TERM;
}

static const char *
parse_prog_config_kvpair(const char *config_str, struct perf_probe_event *pev)
{
	char *text = strdup(config_str);
	char *sep, *line;
	const char *main_str = NULL;
	int err = 0;

	if (!text) {
		pr_debug("Not enough memory: dup config_str failed\n");
		return ERR_PTR(-ENOMEM);
	}

	line = text;
	while ((sep = strchr(line, ';'))) {
		char *equ;

		*sep = '\0';
		equ = strchr(line, '=');
		if (!equ) {
			pr_warning("WARNING: invalid config in BPF object: %s\n",
				   line);
			pr_warning("\tShould be 'key=value'.\n");
			goto nextline;
		}
		*equ = '\0';

		err = do_prog_config(line, equ + 1, pev);
		if (err)
			break;
nextline:
		line = sep + 1;
	}

	if (!err)
		main_str = config_str + (line - text);
	free(text);

	return err ? ERR_PTR(err) : main_str;
}

static int
parse_prog_config(const char *config_str, const char **p_main_str,
		  bool *is_tp, struct perf_probe_event *pev)
{
	int err;
	const char *main_str = parse_prog_config_kvpair(config_str, pev);

	if (IS_ERR(main_str))
		return PTR_ERR(main_str);

	*p_main_str = main_str;
	if (!strchr(main_str, '=')) {
		/* Is a tracepoint event? */
		const char *s = strchr(main_str, ':');

		if (!s) {
			pr_debug("bpf: '%s' is not a valid tracepoint\n",
				 config_str);
			return -BPF_LOADER_ERRNO__CONFIG;
		}

		*is_tp = true;
		return 0;
	}

	*is_tp = false;
	err = parse_perf_probe_command(main_str, pev);
	if (err < 0) {
		pr_debug("bpf: '%s' is not a valid config string\n",
			 config_str);
		/* parse failed, don't need clear pev. */
		return -BPF_LOADER_ERRNO__CONFIG;
	}
	return 0;
}

static int
config_bpf_program(struct bpf_program *prog)
{
	struct perf_probe_event *pev = NULL;
	struct bpf_prog_priv *priv = NULL;
	const char *config_str, *main_str;
	bool is_tp = false;
	int err;

	/* Initialize per-program probing setting */
	probe_conf.no_inlines = false;
	probe_conf.force_add = false;

	priv = calloc(sizeof(*priv), 1);
	if (!priv) {
		pr_debug("bpf: failed to alloc priv\n");
		return -ENOMEM;
	}
	pev = &priv->pev;

	config_str = bpf_program__section_name(prog);
	pr_debug("bpf: config program '%s'\n", config_str);
	err = parse_prog_config(config_str, &main_str, &is_tp, pev);
	if (err)
		goto errout;

	if (is_tp) {
		char *s = strchr(main_str, ':');

		priv->is_tp = true;
		priv->sys_name = strndup(main_str, s - main_str);
		priv->evt_name = strdup(s + 1);
		goto set_priv;
	}

	if (pev->group && strcmp(pev->group, PERF_BPF_PROBE_GROUP)) {
		pr_debug("bpf: '%s': group for event is set and not '%s'.\n",
			 config_str, PERF_BPF_PROBE_GROUP);
		err = -BPF_LOADER_ERRNO__GROUP;
		goto errout;
	} else if (!pev->group)
		pev->group = strdup(PERF_BPF_PROBE_GROUP);

	if (!pev->group) {
		pr_debug("bpf: strdup failed\n");
		err = -ENOMEM;
		goto errout;
	}

	if (!pev->event) {
		pr_debug("bpf: '%s': event name is missing. Section name should be 'key=value'\n",
			 config_str);
		err = -BPF_LOADER_ERRNO__EVENTNAME;
		goto errout;
	}
	pr_debug("bpf: config '%s' is ok\n", config_str);

set_priv:
	err = bpf_program__set_priv(prog, priv, clear_prog_priv);
	if (err) {
		pr_debug("Failed to set priv for program '%s'\n", config_str);
		goto errout;
	}

	return 0;

errout:
	if (pev)
		clear_perf_probe_event(pev);
	free(priv);
	return err;
}

static int bpf__prepare_probe(void)
{
	static int err = 0;
	static bool initialized = false;

	/*
	 * Make err static, so if init failed the first, bpf__prepare_probe()
	 * fails each time without calling init_probe_symbol_maps multiple
	 * times.
	 */
	if (initialized)
		return err;

	initialized = true;
	err = init_probe_symbol_maps(false);
	if (err < 0)
		pr_debug("Failed to init_probe_symbol_maps\n");
	probe_conf.max_probes = MAX_PROBES;
	return err;
}

static int
preproc_gen_prologue(struct bpf_program *prog, int n,
		     struct bpf_insn *orig_insns, int orig_insns_cnt,
		     struct bpf_prog_prep_result *res)
{
	struct bpf_prog_priv *priv = bpf_program__priv(prog);
	struct probe_trace_event *tev;
	struct perf_probe_event *pev;
	struct bpf_insn *buf;
	size_t prologue_cnt = 0;
	int i, err;

	if (IS_ERR_OR_NULL(priv) || priv->is_tp)
		goto errout;

	pev = &priv->pev;

	if (n < 0 || n >= priv->nr_types)
		goto errout;

	/* Find a tev belongs to that type */
	for (i = 0; i < pev->ntevs; i++) {
		if (priv->type_mapping[i] == n)
			break;
	}

	if (i >= pev->ntevs) {
		pr_debug("Internal error: prologue type %d not found\n", n);
		return -BPF_LOADER_ERRNO__PROLOGUE;
	}

	tev = &pev->tevs[i];

	buf = priv->insns_buf;
	err = bpf__gen_prologue(tev->args, tev->nargs,
				buf, &prologue_cnt,
				BPF_MAXINSNS - orig_insns_cnt);
	if (err) {
		const char *title;

		title = bpf_program__section_name(prog);
		pr_debug("Failed to generate prologue for program %s\n",
			 title);
		return err;
	}

	memcpy(&buf[prologue_cnt], orig_insns,
	       sizeof(struct bpf_insn) * orig_insns_cnt);

	res->new_insn_ptr = buf;
	res->new_insn_cnt = prologue_cnt + orig_insns_cnt;
	res->pfd = NULL;
	return 0;

errout:
	pr_debug("Internal error in preproc_gen_prologue\n");
	return -BPF_LOADER_ERRNO__PROLOGUE;
}

/*
 * compare_tev_args is reflexive, transitive and antisymmetric.
 * I can proof it but this margin is too narrow to contain.
 */
static int compare_tev_args(const void *ptev1, const void *ptev2)
{
	int i, ret;
	const struct probe_trace_event *tev1 =
		*(const struct probe_trace_event **)ptev1;
	const struct probe_trace_event *tev2 =
		*(const struct probe_trace_event **)ptev2;

	ret = tev2->nargs - tev1->nargs;
	if (ret)
		return ret;

	for (i = 0; i < tev1->nargs; i++) {
		struct probe_trace_arg *arg1, *arg2;
		struct probe_trace_arg_ref *ref1, *ref2;

		arg1 = &tev1->args[i];
		arg2 = &tev2->args[i];

		ret = strcmp(arg1->value, arg2->value);
		if (ret)
			return ret;

		ref1 = arg1->ref;
		ref2 = arg2->ref;

		while (ref1 && ref2) {
			ret = ref2->offset - ref1->offset;
			if (ret)
				return ret;

			ref1 = ref1->next;
			ref2 = ref2->next;
		}

		if (ref1 || ref2)
			return ref2 ? 1 : -1;
	}

	return 0;
}

/*
 * Assign a type number to each tevs in a pev.
 * mapping is an array with same slots as tevs in that pev.
 * nr_types will be set to number of types.
 */
static int map_prologue(struct perf_probe_event *pev, int *mapping,
			int *nr_types)
{
	int i, type = 0;
	struct probe_trace_event **ptevs;

	size_t array_sz = sizeof(*ptevs) * pev->ntevs;

	ptevs = malloc(array_sz);
	if (!ptevs) {
		pr_debug("Not enough memory: alloc ptevs failed\n");
		return -ENOMEM;
	}

	pr_debug("In map_prologue, ntevs=%d\n", pev->ntevs);
	for (i = 0; i < pev->ntevs; i++)
		ptevs[i] = &pev->tevs[i];

	qsort(ptevs, pev->ntevs, sizeof(*ptevs),
	      compare_tev_args);

	for (i = 0; i < pev->ntevs; i++) {
		int n;

		n = ptevs[i] - pev->tevs;
		if (i == 0) {
			mapping[n] = type;
			pr_debug("mapping[%d]=%d\n", n, type);
			continue;
		}

		if (compare_tev_args(ptevs + i, ptevs + i - 1) == 0)
			mapping[n] = type;
		else
			mapping[n] = ++type;

		pr_debug("mapping[%d]=%d\n", n, mapping[n]);
	}
	free(ptevs);
	*nr_types = type + 1;

	return 0;
}

static int hook_load_preprocessor(struct bpf_program *prog)
{
	struct bpf_prog_priv *priv = bpf_program__priv(prog);
	struct perf_probe_event *pev;
	bool need_prologue = false;
	int err, i;

	if (IS_ERR_OR_NULL(priv)) {
		pr_debug("Internal error when hook preprocessor\n");
		return -BPF_LOADER_ERRNO__INTERNAL;
	}

	if (priv->is_tp) {
		priv->need_prologue = false;
		return 0;
	}

	pev = &priv->pev;
	for (i = 0; i < pev->ntevs; i++) {
		struct probe_trace_event *tev = &pev->tevs[i];

		if (tev->nargs > 0) {
			need_prologue = true;
			break;
		}
	}

	/*
	 * Since all tevs don't have argument, we don't need generate
	 * prologue.
	 */
	if (!need_prologue) {
		priv->need_prologue = false;
		return 0;
	}

	priv->need_prologue = true;
	priv->insns_buf = malloc(sizeof(struct bpf_insn) * BPF_MAXINSNS);
	if (!priv->insns_buf) {
		pr_debug("Not enough memory: alloc insns_buf failed\n");
		return -ENOMEM;
	}

	priv->type_mapping = malloc(sizeof(int) * pev->ntevs);
	if (!priv->type_mapping) {
		pr_debug("Not enough memory: alloc type_mapping failed\n");
		return -ENOMEM;
	}
	memset(priv->type_mapping, -1,
	       sizeof(int) * pev->ntevs);

	err = map_prologue(pev, priv->type_mapping, &priv->nr_types);
	if (err)
		return err;

	err = bpf_program__set_prep(prog, priv->nr_types,
				    preproc_gen_prologue);
	return err;
}

int bpf__probe(struct bpf_object *obj)
{
	int err = 0;
	struct bpf_program *prog;
	struct bpf_prog_priv *priv;
	struct perf_probe_event *pev;

	err = bpf__prepare_probe();
	if (err) {
		pr_debug("bpf__prepare_probe failed\n");
		return err;
	}

	bpf_object__for_each_program(prog, obj) {
		err = config_bpf_program(prog);
		if (err)
			goto out;

		priv = bpf_program__priv(prog);
		if (IS_ERR_OR_NULL(priv)) {
			if (!priv)
				err = -BPF_LOADER_ERRNO__INTERNAL;
			else
				err = PTR_ERR(priv);
			goto out;
		}

		if (priv->is_tp) {
			bpf_program__set_tracepoint(prog);
			continue;
		}

		bpf_program__set_kprobe(prog);
		pev = &priv->pev;

		err = convert_perf_probe_events(pev, 1);
		if (err < 0) {
			pr_debug("bpf_probe: failed to convert perf probe events\n");
			goto out;
		}

		err = apply_perf_probe_events(pev, 1);
		if (err < 0) {
			pr_debug("bpf_probe: failed to apply perf probe events\n");
			goto out;
		}

		/*
		 * After probing, let's consider prologue, which
		 * adds program fetcher to BPF programs.
		 *
		 * hook_load_preprocessor() hooks pre-processor
		 * to bpf_program, let it generate prologue
		 * dynamically during loading.
		 */
		err = hook_load_preprocessor(prog);
		if (err)
			goto out;
	}
out:
	return err < 0 ? err : 0;
}

#define EVENTS_WRITE_BUFSIZE  4096
int bpf__unprobe(struct bpf_object *obj)
{
	int err, ret = 0;
	struct bpf_program *prog;

	bpf_object__for_each_program(prog, obj) {
		struct bpf_prog_priv *priv = bpf_program__priv(prog);
		int i;

		if (IS_ERR_OR_NULL(priv) || priv->is_tp)
			continue;

		for (i = 0; i < priv->pev.ntevs; i++) {
			struct probe_trace_event *tev = &priv->pev.tevs[i];
			char name_buf[EVENTS_WRITE_BUFSIZE];
			struct strfilter *delfilter;

			snprintf(name_buf, EVENTS_WRITE_BUFSIZE,
				 "%s:%s", tev->group, tev->event);
			name_buf[EVENTS_WRITE_BUFSIZE - 1] = '\0';

			delfilter = strfilter__new(name_buf, NULL);
			if (!delfilter) {
				pr_debug("Failed to create filter for unprobing\n");
				ret = -ENOMEM;
				continue;
			}

			err = del_perf_probe_events(delfilter);
			strfilter__delete(delfilter);
			if (err) {
				pr_debug("Failed to delete %s\n", name_buf);
				ret = err;
				continue;
			}
		}
	}
	return ret;
}

int bpf__load(struct bpf_object *obj)
{
	int err;

	err = bpf_object__load(obj);
	if (err) {
		char bf[128];
		libbpf_strerror(err, bf, sizeof(bf));
		pr_debug("bpf: load objects failed: err=%d: (%s)\n", err, bf);
		return err;
	}
	return 0;
}

int bpf__foreach_event(struct bpf_object *obj,
		       bpf_prog_iter_callback_t func,
		       void *arg)
{
	struct bpf_program *prog;
	int err;

	bpf_object__for_each_program(prog, obj) {
		struct bpf_prog_priv *priv = bpf_program__priv(prog);
		struct probe_trace_event *tev;
		struct perf_probe_event *pev;
		int i, fd;

		if (IS_ERR_OR_NULL(priv)) {
			pr_debug("bpf: failed to get private field\n");
			return -BPF_LOADER_ERRNO__INTERNAL;
		}

		if (priv->is_tp) {
			fd = bpf_program__fd(prog);
			err = (*func)(priv->sys_name, priv->evt_name, fd, obj, arg);
			if (err) {
				pr_debug("bpf: tracepoint call back failed, stop iterate\n");
				return err;
			}
			continue;
		}

		pev = &priv->pev;
		for (i = 0; i < pev->ntevs; i++) {
			tev = &pev->tevs[i];

			if (priv->need_prologue) {
				int type = priv->type_mapping[i];

				fd = bpf_program__nth_fd(prog, type);
			} else {
				fd = bpf_program__fd(prog);
			}

			if (fd < 0) {
				pr_debug("bpf: failed to get file descriptor\n");
				return fd;
			}

			err = (*func)(tev->group, tev->event, fd, obj, arg);
			if (err) {
				pr_debug("bpf: call back failed, stop iterate\n");
				return err;
			}
		}
	}
	return 0;
}

enum bpf_map_op_type {
	BPF_MAP_OP_SET_VALUE,
	BPF_MAP_OP_SET_EVSEL,
};

enum bpf_map_key_type {
	BPF_MAP_KEY_ALL,
	BPF_MAP_KEY_RANGES,
};

struct bpf_map_op {
	struct list_head list;
	enum bpf_map_op_type op_type;
	enum bpf_map_key_type key_type;
	union {
		struct parse_events_array array;
	} k;
	union {
		u64 value;
		struct evsel *evsel;
	} v;
};

struct bpf_map_priv {
	struct list_head ops_list;
};

static void
bpf_map_op__delete(struct bpf_map_op *op)
{
	if (!list_empty(&op->list))
		list_del_init(&op->list);
	if (op->key_type == BPF_MAP_KEY_RANGES)
		parse_events__clear_array(&op->k.array);
	free(op);
}

static void
bpf_map_priv__purge(struct bpf_map_priv *priv)
{
	struct bpf_map_op *pos, *n;

	list_for_each_entry_safe(pos, n, &priv->ops_list, list) {
		list_del_init(&pos->list);
		bpf_map_op__delete(pos);
	}
}

static void
bpf_map_priv__clear(struct bpf_map *map __maybe_unused,
		    void *_priv)
{
	struct bpf_map_priv *priv = _priv;

	bpf_map_priv__purge(priv);
	free(priv);
}

static int
bpf_map_op_setkey(struct bpf_map_op *op, struct parse_events_term *term)
{
	op->key_type = BPF_MAP_KEY_ALL;
	if (!term)
		return 0;

	if (term->array.nr_ranges) {
		size_t memsz = term->array.nr_ranges *
				sizeof(op->k.array.ranges[0]);

		op->k.array.ranges = memdup(term->array.ranges, memsz);
		if (!op->k.array.ranges) {
			pr_debug("Not enough memory to alloc indices for map\n");
			return -ENOMEM;
		}
		op->key_type = BPF_MAP_KEY_RANGES;
		op->k.array.nr_ranges = term->array.nr_ranges;
	}
	return 0;
}

static struct bpf_map_op *
bpf_map_op__new(struct parse_events_term *term)
{
	struct bpf_map_op *op;
	int err;

	op = zalloc(sizeof(*op));
	if (!op) {
		pr_debug("Failed to alloc bpf_map_op\n");
		return ERR_PTR(-ENOMEM);
	}
	INIT_LIST_HEAD(&op->list);

	err = bpf_map_op_setkey(op, term);
	if (err) {
		free(op);
		return ERR_PTR(err);
	}
	return op;
}

static struct bpf_map_op *
bpf_map_op__clone(struct bpf_map_op *op)
{
	struct bpf_map_op *newop;

	newop = memdup(op, sizeof(*op));
	if (!newop) {
		pr_debug("Failed to alloc bpf_map_op\n");
		return NULL;
	}

	INIT_LIST_HEAD(&newop->list);
	if (op->key_type == BPF_MAP_KEY_RANGES) {
		size_t memsz = op->k.array.nr_ranges *
			       sizeof(op->k.array.ranges[0]);

		newop->k.array.ranges = memdup(op->k.array.ranges, memsz);
		if (!newop->k.array.ranges) {
			pr_debug("Failed to alloc indices for map\n");
			free(newop);
			return NULL;
		}
	}

	return newop;
}

static struct bpf_map_priv *
bpf_map_priv__clone(struct bpf_map_priv *priv)
{
	struct bpf_map_priv *newpriv;
	struct bpf_map_op *pos, *newop;

	newpriv = zalloc(sizeof(*newpriv));
	if (!newpriv) {
		pr_debug("Not enough memory to alloc map private\n");
		return NULL;
	}
	INIT_LIST_HEAD(&newpriv->ops_list);

	list_for_each_entry(pos, &priv->ops_list, list) {
		newop = bpf_map_op__clone(pos);
		if (!newop) {
			bpf_map_priv__purge(newpriv);
			return NULL;
		}
		list_add_tail(&newop->list, &newpriv->ops_list);
	}

	return newpriv;
}

static int
bpf_map__add_op(struct bpf_map *map, struct bpf_map_op *op)
{
	const char *map_name = bpf_map__name(map);
	struct bpf_map_priv *priv = bpf_map__priv(map);

	if (IS_ERR(priv)) {
		pr_debug("Failed to get private from map %s\n", map_name);
		return PTR_ERR(priv);
	}

	if (!priv) {
		priv = zalloc(sizeof(*priv));
		if (!priv) {
			pr_debug("Not enough memory to alloc map private\n");
			return -ENOMEM;
		}
		INIT_LIST_HEAD(&priv->ops_list);

		if (bpf_map__set_priv(map, priv, bpf_map_priv__clear)) {
			free(priv);
			return -BPF_LOADER_ERRNO__INTERNAL;
		}
	}

	list_add_tail(&op->list, &priv->ops_list);
	return 0;
}

static struct bpf_map_op *
bpf_map__add_newop(struct bpf_map *map, struct parse_events_term *term)
{
	struct bpf_map_op *op;
	int err;

	op = bpf_map_op__new(term);
	if (IS_ERR(op))
		return op;

	err = bpf_map__add_op(map, op);
	if (err) {
		bpf_map_op__delete(op);
		return ERR_PTR(err);
	}
	return op;
}

static int
__bpf_map__config_value(struct bpf_map *map,
			struct parse_events_term *term)
{
	struct bpf_map_op *op;
	const char *map_name = bpf_map__name(map);
	const struct bpf_map_def *def = bpf_map__def(map);

	if (IS_ERR(def)) {
		pr_debug("Unable to get map definition from '%s'\n",
			 map_name);
		return -BPF_LOADER_ERRNO__INTERNAL;
	}

	if (def->type != BPF_MAP_TYPE_ARRAY) {
		pr_debug("Map %s type is not BPF_MAP_TYPE_ARRAY\n",
			 map_name);
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_TYPE;
	}
	if (def->key_size < sizeof(unsigned int)) {
		pr_debug("Map %s has incorrect key size\n", map_name);
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_KEYSIZE;
	}
	switch (def->value_size) {
	case 1:
	case 2:
	case 4:
	case 8:
		break;
	default:
		pr_debug("Map %s has incorrect value size\n", map_name);
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_VALUESIZE;
	}

	op = bpf_map__add_newop(map, term);
	if (IS_ERR(op))
		return PTR_ERR(op);
	op->op_type = BPF_MAP_OP_SET_VALUE;
	op->v.value = term->val.num;
	return 0;
}

static int
bpf_map__config_value(struct bpf_map *map,
		      struct parse_events_term *term,
		      struct evlist *evlist __maybe_unused)
{
	if (!term->err_val) {
		pr_debug("Config value not set\n");
		return -BPF_LOADER_ERRNO__OBJCONF_CONF;
	}

	if (term->type_val != PARSE_EVENTS__TERM_TYPE_NUM) {
		pr_debug("ERROR: wrong value type for 'value'\n");
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_VALUE;
	}

	return __bpf_map__config_value(map, term);
}

static int
__bpf_map__config_event(struct bpf_map *map,
			struct parse_events_term *term,
			struct evlist *evlist)
{
	const struct bpf_map_def *def;
	struct bpf_map_op *op;
	const char *map_name = bpf_map__name(map);
	struct evsel *evsel = evlist__find_evsel_by_str(evlist, term->val.str);

	if (!evsel) {
		pr_debug("Event (for '%s') '%s' doesn't exist\n",
			 map_name, term->val.str);
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_NOEVT;
	}

	def = bpf_map__def(map);
	if (IS_ERR(def)) {
		pr_debug("Unable to get map definition from '%s'\n",
			 map_name);
		return PTR_ERR(def);
	}

	/*
	 * No need to check key_size and value_size:
	 * kernel has already checked them.
	 */
	if (def->type != BPF_MAP_TYPE_PERF_EVENT_ARRAY) {
		pr_debug("Map %s type is not BPF_MAP_TYPE_PERF_EVENT_ARRAY\n",
			 map_name);
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_TYPE;
	}

	op = bpf_map__add_newop(map, term);
	if (IS_ERR(op))
		return PTR_ERR(op);
	op->op_type = BPF_MAP_OP_SET_EVSEL;
	op->v.evsel = evsel;
	return 0;
}

static int
bpf_map__config_event(struct bpf_map *map,
		      struct parse_events_term *term,
		      struct evlist *evlist)
{
	if (!term->err_val) {
		pr_debug("Config value not set\n");
		return -BPF_LOADER_ERRNO__OBJCONF_CONF;
	}

	if (term->type_val != PARSE_EVENTS__TERM_TYPE_STR) {
		pr_debug("ERROR: wrong value type for 'event'\n");
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_VALUE;
	}

	return __bpf_map__config_event(map, term, evlist);
}

struct bpf_obj_config__map_func {
	const char *config_opt;
	int (*config_func)(struct bpf_map *, struct parse_events_term *,
			   struct evlist *);
};

struct bpf_obj_config__map_func bpf_obj_config__map_funcs[] = {
	{"value", bpf_map__config_value},
	{"event", bpf_map__config_event},
};

static int
config_map_indices_range_check(struct parse_events_term *term,
			       struct bpf_map *map,
			       const char *map_name)
{
	struct parse_events_array *array = &term->array;
	const struct bpf_map_def *def;
	unsigned int i;

	if (!array->nr_ranges)
		return 0;
	if (!array->ranges) {
		pr_debug("ERROR: map %s: array->nr_ranges is %d but range array is NULL\n",
			 map_name, (int)array->nr_ranges);
		return -BPF_LOADER_ERRNO__INTERNAL;
	}

	def = bpf_map__def(map);
	if (IS_ERR(def)) {
		pr_debug("ERROR: Unable to get map definition from '%s'\n",
			 map_name);
		return -BPF_LOADER_ERRNO__INTERNAL;
	}

	for (i = 0; i < array->nr_ranges; i++) {
		unsigned int start = array->ranges[i].start;
		size_t length = array->ranges[i].length;
		unsigned int idx = start + length - 1;

		if (idx >= def->max_entries) {
			pr_debug("ERROR: index %d too large\n", idx);
			return -BPF_LOADER_ERRNO__OBJCONF_MAP_IDX2BIG;
		}
	}
	return 0;
}

static int
bpf__obj_config_map(struct bpf_object *obj,
		    struct parse_events_term *term,
		    struct evlist *evlist,
		    int *key_scan_pos)
{
	/* key is "map:<mapname>.<config opt>" */
	char *map_name = strdup(term->config + sizeof("map:") - 1);
	struct bpf_map *map;
	int err = -BPF_LOADER_ERRNO__OBJCONF_OPT;
	char *map_opt;
	size_t i;

	if (!map_name)
		return -ENOMEM;

	map_opt = strchr(map_name, '.');
	if (!map_opt) {
		pr_debug("ERROR: Invalid map config: %s\n", map_name);
		goto out;
	}

	*map_opt++ = '\0';
	if (*map_opt == '\0') {
		pr_debug("ERROR: Invalid map option: %s\n", term->config);
		goto out;
	}

	map = bpf_object__find_map_by_name(obj, map_name);
	if (!map) {
		pr_debug("ERROR: Map %s doesn't exist\n", map_name);
		err = -BPF_LOADER_ERRNO__OBJCONF_MAP_NOTEXIST;
		goto out;
	}

	*key_scan_pos += strlen(map_opt);
	err = config_map_indices_range_check(term, map, map_name);
	if (err)
		goto out;
	*key_scan_pos -= strlen(map_opt);

	for (i = 0; i < ARRAY_SIZE(bpf_obj_config__map_funcs); i++) {
		struct bpf_obj_config__map_func *func =
				&bpf_obj_config__map_funcs[i];

		if (strcmp(map_opt, func->config_opt) == 0) {
			err = func->config_func(map, term, evlist);
			goto out;
		}
	}

	pr_debug("ERROR: Invalid map config option '%s'\n", map_opt);
	err = -BPF_LOADER_ERRNO__OBJCONF_MAP_OPT;
out:
	free(map_name);
	if (!err)
		*key_scan_pos += strlen(map_opt);
	return err;
}

int bpf__config_obj(struct bpf_object *obj,
		    struct parse_events_term *term,
		    struct evlist *evlist,
		    int *error_pos)
{
	int key_scan_pos = 0;
	int err;

	if (!obj || !term || !term->config)
		return -EINVAL;

	if (strstarts(term->config, "map:")) {
		key_scan_pos = sizeof("map:") - 1;
		err = bpf__obj_config_map(obj, term, evlist, &key_scan_pos);
		goto out;
	}
	err = -BPF_LOADER_ERRNO__OBJCONF_OPT;
out:
	if (error_pos)
		*error_pos = key_scan_pos;
	return err;

}

typedef int (*map_config_func_t)(const char *name, int map_fd,
				 const struct bpf_map_def *pdef,
				 struct bpf_map_op *op,
				 void *pkey, void *arg);

static int
foreach_key_array_all(map_config_func_t func,
		      void *arg, const char *name,
		      int map_fd, const struct bpf_map_def *pdef,
		      struct bpf_map_op *op)
{
	unsigned int i;
	int err;

	for (i = 0; i < pdef->max_entries; i++) {
		err = func(name, map_fd, pdef, op, &i, arg);
		if (err) {
			pr_debug("ERROR: failed to insert value to %s[%u]\n",
				 name, i);
			return err;
		}
	}
	return 0;
}

static int
foreach_key_array_ranges(map_config_func_t func, void *arg,
			 const char *name, int map_fd,
			 const struct bpf_map_def *pdef,
			 struct bpf_map_op *op)
{
	unsigned int i, j;
	int err;

	for (i = 0; i < op->k.array.nr_ranges; i++) {
		unsigned int start = op->k.array.ranges[i].start;
		size_t length = op->k.array.ranges[i].length;

		for (j = 0; j < length; j++) {
			unsigned int idx = start + j;

			err = func(name, map_fd, pdef, op, &idx, arg);
			if (err) {
				pr_debug("ERROR: failed to insert value to %s[%u]\n",
					 name, idx);
				return err;
			}
		}
	}
	return 0;
}

static int
bpf_map_config_foreach_key(struct bpf_map *map,
			   map_config_func_t func,
			   void *arg)
{
	int err, map_fd;
	struct bpf_map_op *op;
	const struct bpf_map_def *def;
	const char *name = bpf_map__name(map);
	struct bpf_map_priv *priv = bpf_map__priv(map);

	if (IS_ERR(priv)) {
		pr_debug("ERROR: failed to get private from map %s\n", name);
		return -BPF_LOADER_ERRNO__INTERNAL;
	}
	if (!priv || list_empty(&priv->ops_list)) {
		pr_debug("INFO: nothing to config for map %s\n", name);
		return 0;
	}

	def = bpf_map__def(map);
	if (IS_ERR(def)) {
		pr_debug("ERROR: failed to get definition from map %s\n", name);
		return -BPF_LOADER_ERRNO__INTERNAL;
	}
	map_fd = bpf_map__fd(map);
	if (map_fd < 0) {
		pr_debug("ERROR: failed to get fd from map %s\n", name);
		return map_fd;
	}

	list_for_each_entry(op, &priv->ops_list, list) {
		switch (def->type) {
		case BPF_MAP_TYPE_ARRAY:
		case BPF_MAP_TYPE_PERF_EVENT_ARRAY:
			switch (op->key_type) {
			case BPF_MAP_KEY_ALL:
				err = foreach_key_array_all(func, arg, name,
							    map_fd, def, op);
				break;
			case BPF_MAP_KEY_RANGES:
				err = foreach_key_array_ranges(func, arg, name,
							       map_fd, def,
							       op);
				break;
			default:
				pr_debug("ERROR: keytype for map '%s' invalid\n",
					 name);
				return -BPF_LOADER_ERRNO__INTERNAL;
			}
			if (err)
				return err;
			break;
		default:
			pr_debug("ERROR: type of '%s' incorrect\n", name);
			return -BPF_LOADER_ERRNO__OBJCONF_MAP_TYPE;
		}
	}

	return 0;
}

static int
apply_config_value_for_key(int map_fd, void *pkey,
			   size_t val_size, u64 val)
{
	int err = 0;

	switch (val_size) {
	case 1: {
		u8 _val = (u8)(val);
		err = bpf_map_update_elem(map_fd, pkey, &_val, BPF_ANY);
		break;
	}
	case 2: {
		u16 _val = (u16)(val);
		err = bpf_map_update_elem(map_fd, pkey, &_val, BPF_ANY);
		break;
	}
	case 4: {
		u32 _val = (u32)(val);
		err = bpf_map_update_elem(map_fd, pkey, &_val, BPF_ANY);
		break;
	}
	case 8: {
		err = bpf_map_update_elem(map_fd, pkey, &val, BPF_ANY);
		break;
	}
	default:
		pr_debug("ERROR: invalid value size\n");
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_VALUESIZE;
	}
	if (err && errno)
		err = -errno;
	return err;
}

static int
apply_config_evsel_for_key(const char *name, int map_fd, void *pkey,
			   struct evsel *evsel)
{
	struct xyarray *xy = evsel->core.fd;
	struct perf_event_attr *attr;
	unsigned int key, events;
	bool check_pass = false;
	int *evt_fd;
	int err;

	if (!xy) {
		pr_debug("ERROR: evsel not ready for map %s\n", name);
		return -BPF_LOADER_ERRNO__INTERNAL;
	}

	if (xy->row_size / xy->entry_size != 1) {
		pr_debug("ERROR: Dimension of target event is incorrect for map %s\n",
			 name);
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_EVTDIM;
	}

	attr = &evsel->core.attr;
	if (attr->inherit) {
		pr_debug("ERROR: Can't put inherit event into map %s\n", name);
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_EVTINH;
	}

	if (evsel__is_bpf_output(evsel))
		check_pass = true;
	if (attr->type == PERF_TYPE_RAW)
		check_pass = true;
	if (attr->type == PERF_TYPE_HARDWARE)
		check_pass = true;
	if (!check_pass) {
		pr_debug("ERROR: Event type is wrong for map %s\n", name);
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_EVTTYPE;
	}

	events = xy->entries / (xy->row_size / xy->entry_size);
	key = *((unsigned int *)pkey);
	if (key >= events) {
		pr_debug("ERROR: there is no event %d for map %s\n",
			 key, name);
		return -BPF_LOADER_ERRNO__OBJCONF_MAP_MAPSIZE;
	}
	evt_fd = xyarray__entry(xy, key, 0);
	err = bpf_map_update_elem(map_fd, pkey, evt_fd, BPF_ANY);
	if (err && errno)
		err = -errno;
	return err;
}

static int
apply_obj_config_map_for_key(const char *name, int map_fd,
			     const struct bpf_map_def *pdef,
			     struct bpf_map_op *op,
			     void *pkey, void *arg __maybe_unused)
{
	int err;

	switch (op->op_type) {
	case BPF_MAP_OP_SET_VALUE:
		err = apply_config_value_for_key(map_fd, pkey,
						 pdef->value_size,
						 op->v.value);
		break;
	case BPF_MAP_OP_SET_EVSEL:
		err = apply_config_evsel_for_key(name, map_fd, pkey,
						 op->v.evsel);
		break;
	default:
		pr_debug("ERROR: unknown value type for '%s'\n", name);
		err = -BPF_LOADER_ERRNO__INTERNAL;
	}
	return err;
}

static int
apply_obj_config_map(struct bpf_map *map)
{
	return bpf_map_config_foreach_key(map,
					  apply_obj_config_map_for_key,
					  NULL);
}

static int
apply_obj_config_object(struct bpf_object *obj)
{
	struct bpf_map *map;
	int err;

	bpf_object__for_each_map(map, obj) {
		err = apply_obj_config_map(map);
		if (err)
			return err;
	}
	return 0;
}

int bpf__apply_obj_config(void)
{
	struct bpf_perf_object *perf_obj, *tmp;
	int err;

	bpf_perf_object__for_each(perf_obj, tmp) {
		err = apply_obj_config_object(perf_obj->obj);
		if (err)
			return err;
	}

	return 0;
}

#define bpf__perf_for_each_map(map, pobj, tmp)			\
	bpf_perf_object__for_each(pobj, tmp)			\
		bpf_object__for_each_map(map, pobj->obj)

#define bpf__perf_for_each_map_named(map, pobj, pobjtmp, name)	\
	bpf__perf_for_each_map(map, pobj, pobjtmp)		\
		if (bpf_map__name(map) && (strcmp(name, bpf_map__name(map)) == 0))

struct evsel *bpf__setup_output_event(struct evlist *evlist, const char *name)
{
	struct bpf_map_priv *tmpl_priv = NULL;
	struct bpf_perf_object *perf_obj, *tmp;
	struct evsel *evsel = NULL;
	struct bpf_map *map;
	int err;
	bool need_init = false;

	bpf__perf_for_each_map_named(map, perf_obj, tmp, name) {
		struct bpf_map_priv *priv = bpf_map__priv(map);

		if (IS_ERR(priv))
			return ERR_PTR(-BPF_LOADER_ERRNO__INTERNAL);

		/*
		 * No need to check map type: type should have been
		 * verified by kernel.
		 */
		if (!need_init && !priv)
			need_init = !priv;
		if (!tmpl_priv && priv)
			tmpl_priv = priv;
	}

	if (!need_init)
		return NULL;

	if (!tmpl_priv) {
		char *event_definition = NULL;

		if (asprintf(&event_definition, "bpf-output/no-inherit=1,name=%s/", name) < 0)
			return ERR_PTR(-ENOMEM);

		err = parse_events(evlist, event_definition, NULL);
		free(event_definition);

		if (err) {
			pr_debug("ERROR: failed to create the \"%s\" bpf-output event\n", name);
			return ERR_PTR(-err);
		}

		evsel = evlist__last(evlist);
	}

	bpf__perf_for_each_map_named(map, perf_obj, tmp, name) {
		struct bpf_map_priv *priv = bpf_map__priv(map);

		if (IS_ERR(priv))
			return ERR_PTR(-BPF_LOADER_ERRNO__INTERNAL);
		if (priv)
			continue;

		if (tmpl_priv) {
			priv = bpf_map_priv__clone(tmpl_priv);
			if (!priv)
				return ERR_PTR(-ENOMEM);

			err = bpf_map__set_priv(map, priv, bpf_map_priv__clear);
			if (err) {
				bpf_map_priv__clear(map, priv);
				return ERR_PTR(err);
			}
		} else if (evsel) {
			struct bpf_map_op *op;

			op = bpf_map__add_newop(map, NULL);
			if (IS_ERR(op))
				return ERR_CAST(op);
			op->op_type = BPF_MAP_OP_SET_EVSEL;
			op->v.evsel = evsel;
		}
	}

	return evsel;
}

int bpf__setup_stdout(struct evlist *evlist)
{
	struct evsel *evsel = bpf__setup_output_event(evlist, "__bpf_stdout__");
	return PTR_ERR_OR_ZERO(evsel);
}

#define ERRNO_OFFSET(e)		((e) - __BPF_LOADER_ERRNO__START)
#define ERRCODE_OFFSET(c)	ERRNO_OFFSET(BPF_LOADER_ERRNO__##c)
#define NR_ERRNO	(__BPF_LOADER_ERRNO__END - __BPF_LOADER_ERRNO__START)

static const char *bpf_loader_strerror_table[NR_ERRNO] = {
	[ERRCODE_OFFSET(CONFIG)]	= "Invalid config string",
	[ERRCODE_OFFSET(GROUP)]		= "Invalid group name",
	[ERRCODE_OFFSET(EVENTNAME)]	= "No event name found in config string",
	[ERRCODE_OFFSET(INTERNAL)]	= "BPF loader internal error",
	[ERRCODE_OFFSET(COMPILE)]	= "Error when compiling BPF scriptlet",
	[ERRCODE_OFFSET(PROGCONF_TERM)]	= "Invalid program config term in config string",
	[ERRCODE_OFFSET(PROLOGUE)]	= "Failed to generate prologue",
	[ERRCODE_OFFSET(PROLOGUE2BIG)]	= "Prologue too big for program",
	[ERRCODE_OFFSET(PROLOGUEOOB)]	= "Offset out of bound for prologue",
	[ERRCODE_OFFSET(OBJCONF_OPT)]	= "Invalid object config option",
	[ERRCODE_OFFSET(OBJCONF_CONF)]	= "Config value not set (missing '=')",
	[ERRCODE_OFFSET(OBJCONF_MAP_OPT)]	= "Invalid object map config option",
	[ERRCODE_OFFSET(OBJCONF_MAP_NOTEXIST)]	= "Target map doesn't exist",
	[ERRCODE_OFFSET(OBJCONF_MAP_VALUE)]	= "Incorrect value type for map",
	[ERRCODE_OFFSET(OBJCONF_MAP_TYPE)]	= "Incorrect map type",
	[ERRCODE_OFFSET(OBJCONF_MAP_KEYSIZE)]	= "Incorrect map key size",
	[ERRCODE_OFFSET(OBJCONF_MAP_VALUESIZE)]	= "Incorrect map value size",
	[ERRCODE_OFFSET(OBJCONF_MAP_NOEVT)]	= "Event not found for map setting",
	[ERRCODE_OFFSET(OBJCONF_MAP_MAPSIZE)]	= "Invalid map size for event setting",
	[ERRCODE_OFFSET(OBJCONF_MAP_EVTDIM)]	= "Event dimension too large",
	[ERRCODE_OFFSET(OBJCONF_MAP_EVTINH)]	= "Doesn't support inherit event",
	[ERRCODE_OFFSET(OBJCONF_MAP_EVTTYPE)]	= "Wrong event type for map",
	[ERRCODE_OFFSET(OBJCONF_MAP_IDX2BIG)]	= "Index too large",
};

static int
bpf_loader_strerror(int err, char *buf, size_t size)
{
	char sbuf[STRERR_BUFSIZE];
	const char *msg;

	if (!buf || !size)
		return -1;

	err = err > 0 ? err : -err;

	if (err >= __LIBBPF_ERRNO__START)
		return libbpf_strerror(err, buf, size);

	if (err >= __BPF_LOADER_ERRNO__START && err < __BPF_LOADER_ERRNO__END) {
		msg = bpf_loader_strerror_table[ERRNO_OFFSET(err)];
		snprintf(buf, size, "%s", msg);
		buf[size - 1] = '\0';
		return 0;
	}

	if (err >= __BPF_LOADER_ERRNO__END)
		snprintf(buf, size, "Unknown bpf loader error %d", err);
	else
		snprintf(buf, size, "%s",
			 str_error_r(err, sbuf, sizeof(sbuf)));

	buf[size - 1] = '\0';
	return -1;
}

#define bpf__strerror_head(err, buf, size) \
	char sbuf[STRERR_BUFSIZE], *emsg;\
	if (!size)\
		return 0;\
	if (err < 0)\
		err = -err;\
	bpf_loader_strerror(err, sbuf, sizeof(sbuf));\
	emsg = sbuf;\
	switch (err) {\
	default:\
		scnprintf(buf, size, "%s", emsg);\
		break;

#define bpf__strerror_entry(val, fmt...)\
	case val: {\
		scnprintf(buf, size, fmt);\
		break;\
	}

#define bpf__strerror_end(buf, size)\
	}\
	buf[size - 1] = '\0';

int bpf__strerror_prepare_load(const char *filename, bool source,
			       int err, char *buf, size_t size)
{
	size_t n;
	int ret;

	n = snprintf(buf, size, "Failed to load %s%s: ",
			 filename, source ? " from source" : "");
	if (n >= size) {
		buf[size - 1] = '\0';
		return 0;
	}
	buf += n;
	size -= n;

	ret = bpf_loader_strerror(err, buf, size);
	buf[size - 1] = '\0';
	return ret;
}

int bpf__strerror_probe(struct bpf_object *obj __maybe_unused,
			int err, char *buf, size_t size)
{
	bpf__strerror_head(err, buf, size);
	case BPF_LOADER_ERRNO__PROGCONF_TERM: {
		scnprintf(buf, size, "%s (add -v to see detail)", emsg);
		break;
	}
	bpf__strerror_entry(EEXIST, "Probe point exist. Try 'perf probe -d \"*\"' and set 'force=yes'");
	bpf__strerror_entry(EACCES, "You need to be root");
	bpf__strerror_entry(EPERM, "You need to be root, and /proc/sys/kernel/kptr_restrict should be 0");
	bpf__strerror_entry(ENOENT, "You need to check probing points in BPF file");
	bpf__strerror_end(buf, size);
	return 0;
}

int bpf__strerror_load(struct bpf_object *obj,
		       int err, char *buf, size_t size)
{
	bpf__strerror_head(err, buf, size);
	case LIBBPF_ERRNO__KVER: {
		unsigned int obj_kver = bpf_object__kversion(obj);
		unsigned int real_kver;

		if (fetch_kernel_version(&real_kver, NULL, 0)) {
			scnprintf(buf, size, "Unable to fetch kernel version");
			break;
		}

		if (obj_kver != real_kver) {
			scnprintf(buf, size,
				  "'version' ("KVER_FMT") doesn't match running kernel ("KVER_FMT")",
				  KVER_PARAM(obj_kver),
				  KVER_PARAM(real_kver));
			break;
		}

		scnprintf(buf, size, "Failed to load program for unknown reason");
		break;
	}
	bpf__strerror_end(buf, size);
	return 0;
}

int bpf__strerror_config_obj(struct bpf_object *obj __maybe_unused,
			     struct parse_events_term *term __maybe_unused,
			     struct evlist *evlist __maybe_unused,
			     int *error_pos __maybe_unused, int err,
			     char *buf, size_t size)
{
	bpf__strerror_head(err, buf, size);
	bpf__strerror_entry(BPF_LOADER_ERRNO__OBJCONF_MAP_TYPE,
			    "Can't use this config term with this map type");
	bpf__strerror_end(buf, size);
	return 0;
}

int bpf__strerror_apply_obj_config(int err, char *buf, size_t size)
{
	bpf__strerror_head(err, buf, size);
	bpf__strerror_entry(BPF_LOADER_ERRNO__OBJCONF_MAP_EVTDIM,
			    "Cannot set event to BPF map in multi-thread tracing");
	bpf__strerror_entry(BPF_LOADER_ERRNO__OBJCONF_MAP_EVTINH,
			    "%s (Hint: use -i to turn off inherit)", emsg);
	bpf__strerror_entry(BPF_LOADER_ERRNO__OBJCONF_MAP_EVTTYPE,
			    "Can only put raw, hardware and BPF output event into a BPF map");
	bpf__strerror_end(buf, size);
	return 0;
}

int bpf__strerror_setup_output_event(struct evlist *evlist __maybe_unused,
				     int err, char *buf, size_t size)
{
	bpf__strerror_head(err, buf, size);
	bpf__strerror_end(buf, size);
	return 0;
}

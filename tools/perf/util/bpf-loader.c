/*
 * bpf-loader.c
 *
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 */

#include <bpf/libbpf.h>
#include <linux/err.h>
#include "perf.h"
#include "debug.h"
#include "bpf-loader.h"
#include "probe-event.h"
#include "probe-finder.h" // for MAX_PROBES
#include "llvm-utils.h"

#define DEFINE_PRINT_FN(name, level) \
static int libbpf_##name(const char *fmt, ...)	\
{						\
	va_list args;				\
	int ret;				\
						\
	va_start(args, fmt);			\
	ret = veprintf(level, verbose, pr_fmt(fmt), args);\
	va_end(args);				\
	return ret;				\
}

DEFINE_PRINT_FN(warning, 1)
DEFINE_PRINT_FN(info, 1)
DEFINE_PRINT_FN(debug, 1)

struct bpf_prog_priv {
	struct perf_probe_event pev;
};

static bool libbpf_initialized;

struct bpf_object *
bpf__prepare_load_buffer(void *obj_buf, size_t obj_buf_sz, const char *name)
{
	struct bpf_object *obj;

	if (!libbpf_initialized) {
		libbpf_set_print(libbpf_warning,
				 libbpf_info,
				 libbpf_debug);
		libbpf_initialized = true;
	}

	obj = bpf_object__open_buffer(obj_buf, obj_buf_sz, name);
	if (IS_ERR(obj)) {
		pr_debug("bpf: failed to load buffer\n");
		return ERR_PTR(-EINVAL);
	}

	return obj;
}

struct bpf_object *bpf__prepare_load(const char *filename, bool source)
{
	struct bpf_object *obj;

	if (!libbpf_initialized) {
		libbpf_set_print(libbpf_warning,
				 libbpf_info,
				 libbpf_debug);
		libbpf_initialized = true;
	}

	if (source) {
		int err;
		void *obj_buf;
		size_t obj_buf_sz;

		err = llvm__compile_bpf(filename, &obj_buf, &obj_buf_sz);
		if (err)
			return ERR_PTR(-BPF_LOADER_ERRNO__COMPILE);
		obj = bpf_object__open_buffer(obj_buf, obj_buf_sz, filename);
		free(obj_buf);
	} else
		obj = bpf_object__open(filename);

	if (IS_ERR(obj)) {
		pr_debug("bpf: failed to load %s\n", filename);
		return obj;
	}

	return obj;
}

void bpf__clear(void)
{
	struct bpf_object *obj, *tmp;

	bpf_object__for_each_safe(obj, tmp) {
		bpf__unprobe(obj);
		bpf_object__close(obj);
	}
}

static void
bpf_prog_priv__clear(struct bpf_program *prog __maybe_unused,
		     void *_priv)
{
	struct bpf_prog_priv *priv = _priv;

	cleanup_perf_probe_events(&priv->pev, 1);
	free(priv);
}

static int
config_bpf_program(struct bpf_program *prog)
{
	struct perf_probe_event *pev = NULL;
	struct bpf_prog_priv *priv = NULL;
	const char *config_str;
	int err;

	config_str = bpf_program__title(prog, false);
	if (IS_ERR(config_str)) {
		pr_debug("bpf: unable to get title for program\n");
		return PTR_ERR(config_str);
	}

	priv = calloc(sizeof(*priv), 1);
	if (!priv) {
		pr_debug("bpf: failed to alloc priv\n");
		return -ENOMEM;
	}
	pev = &priv->pev;

	pr_debug("bpf: config program '%s'\n", config_str);
	err = parse_perf_probe_command(config_str, pev);
	if (err < 0) {
		pr_debug("bpf: '%s' is not a valid config string\n",
			 config_str);
		err = -BPF_LOADER_ERRNO__CONFIG;
		goto errout;
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

	err = bpf_program__set_private(prog, priv, bpf_prog_priv__clear);
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

		err = bpf_program__get_private(prog, (void **)&priv);
		if (err || !priv)
			goto out;
		pev = &priv->pev;

		err = convert_perf_probe_events(pev, 1);
		if (err < 0) {
			pr_debug("bpf_probe: failed to convert perf probe events");
			goto out;
		}

		err = apply_perf_probe_events(pev, 1);
		if (err < 0) {
			pr_debug("bpf_probe: failed to apply perf probe events");
			goto out;
		}
	}
out:
	return err < 0 ? err : 0;
}

#define EVENTS_WRITE_BUFSIZE  4096
int bpf__unprobe(struct bpf_object *obj)
{
	int err, ret = 0;
	struct bpf_program *prog;
	struct bpf_prog_priv *priv;

	bpf_object__for_each_program(prog, obj) {
		int i;

		err = bpf_program__get_private(prog, (void **)&priv);
		if (err || !priv)
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
		pr_debug("bpf: load objects failed\n");
		return err;
	}
	return 0;
}

int bpf__foreach_tev(struct bpf_object *obj,
		     bpf_prog_iter_callback_t func,
		     void *arg)
{
	struct bpf_program *prog;
	int err;

	bpf_object__for_each_program(prog, obj) {
		struct probe_trace_event *tev;
		struct perf_probe_event *pev;
		struct bpf_prog_priv *priv;
		int i, fd;

		err = bpf_program__get_private(prog,
				(void **)&priv);
		if (err || !priv) {
			pr_debug("bpf: failed to get private field\n");
			return -BPF_LOADER_ERRNO__INTERNAL;
		}

		pev = &priv->pev;
		for (i = 0; i < pev->ntevs; i++) {
			tev = &pev->tevs[i];

			fd = bpf_program__fd(prog);
			if (fd < 0) {
				pr_debug("bpf: failed to get file descriptor\n");
				return fd;
			}

			err = (*func)(tev, fd, arg);
			if (err) {
				pr_debug("bpf: call back failed, stop iterate\n");
				return err;
			}
		}
	}
	return 0;
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
			 strerror_r(err, sbuf, sizeof(sbuf)));

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
	bpf__strerror_entry(EEXIST, "Probe point exist. Try use 'perf probe -d \"*\"'");
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
		unsigned int obj_kver = bpf_object__get_kversion(obj);
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

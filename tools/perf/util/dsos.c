// SPDX-License-Identifier: GPL-2.0
#include "debug.h"
#include "dsos.h"
#include "dso.h"
#include "util.h"
#include "vdso.h"
#include "namespaces.h"
#include <errno.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <symbol.h> // filename__read_build_id
#include <unistd.h>

void dsos__init(struct dsos *dsos)
{
	init_rwsem(&dsos->lock);

	dsos->cnt = 0;
	dsos->allocated = 0;
	dsos->dsos = NULL;
	dsos->sorted = true;
}

static void dsos__purge(struct dsos *dsos)
{
	down_write(&dsos->lock);

	for (unsigned int i = 0; i < dsos->cnt; i++) {
		struct dso *dso = dsos->dsos[i];

		dso__set_dsos(dso, NULL);
		dso__put(dso);
	}

	zfree(&dsos->dsos);
	dsos->cnt = 0;
	dsos->allocated = 0;
	dsos->sorted = true;

	up_write(&dsos->lock);
}

void dsos__exit(struct dsos *dsos)
{
	dsos__purge(dsos);
	exit_rwsem(&dsos->lock);
}


static int __dsos__for_each_dso(struct dsos *dsos,
				int (*cb)(struct dso *dso, void *data),
				void *data)
{
	for (unsigned int i = 0; i < dsos->cnt; i++) {
		struct dso *dso = dsos->dsos[i];
		int err;

		err = cb(dso, data);
		if (err)
			return err;
	}
	return 0;
}

struct dsos__read_build_ids_cb_args {
	bool with_hits;
	bool have_build_id;
};

static int dsos__read_build_ids_cb(struct dso *dso, void *data)
{
	struct dsos__read_build_ids_cb_args *args = data;
	struct nscookie nsc;
	struct build_id bid = { .size = 0, };

	if (args->with_hits && !dso__hit(dso) && !dso__is_vdso(dso))
		return 0;
	if (dso__has_build_id(dso)) {
		args->have_build_id = true;
		return 0;
	}
	nsinfo__mountns_enter(dso__nsinfo(dso), &nsc);
	if (filename__read_build_id(dso__long_name(dso), &bid, /*block=*/true) > 0) {
		dso__set_build_id(dso, &bid);
		args->have_build_id = true;
	} else if (errno == ENOENT && dso__nsinfo(dso)) {
		char *new_name = dso__filename_with_chroot(dso, dso__long_name(dso));

		if (new_name && filename__read_build_id(new_name, &bid, /*block=*/true) > 0) {
			dso__set_build_id(dso, &bid);
			args->have_build_id = true;
		}
		free(new_name);
	}
	nsinfo__mountns_exit(&nsc);
	return 0;
}

bool dsos__read_build_ids(struct dsos *dsos, bool with_hits)
{
	struct dsos__read_build_ids_cb_args args = {
		.with_hits = with_hits,
		.have_build_id = false,
	};

	dsos__for_each_dso(dsos, dsos__read_build_ids_cb, &args);
	return args.have_build_id;
}

static int __dso__cmp_long_name(const char *long_name, const struct dso_id *id,
				const struct dso *b)
{
	int rc = strcmp(long_name, dso__long_name(b));
	return rc ?: dso_id__cmp(id, dso__id_const(b));
}

static int __dso__cmp_short_name(const char *short_name, const struct dso_id *id,
				 const struct dso *b)
{
	int rc = strcmp(short_name, dso__short_name(b));
	return rc ?: dso_id__cmp(id, dso__id_const(b));
}

static int dsos__cmp_long_name_id_short_name(const void *va, const void *vb)
{
	const struct dso *a = *((const struct dso **)va);
	const struct dso *b = *((const struct dso **)vb);
	int rc = strcmp(dso__long_name(a), dso__long_name(b));

	if (!rc) {
		rc = dso_id__cmp(dso__id_const(a), dso__id_const(b));
		if (!rc)
			rc = strcmp(dso__short_name(a), dso__short_name(b));
	}
	return rc;
}

struct dsos__key {
	const char *long_name;
	const struct dso_id *id;
};

static int dsos__cmp_key_long_name_id(const void *vkey, const void *vdso)
{
	const struct dsos__key *key = vkey;
	const struct dso *dso = *((const struct dso **)vdso);

	return __dso__cmp_long_name(key->long_name, key->id, dso);
}

/*
 * Find a matching entry and/or link current entry to RB tree.
 * Either one of the dso or name parameter must be non-NULL or the
 * function will not work.
 */
static struct dso *__dsos__find_by_longname_id(struct dsos *dsos,
					       const char *name,
					       const struct dso_id *id,
					       bool write_locked)
	SHARED_LOCKS_REQUIRED(dsos->lock)
{
	struct dsos__key key = {
		.long_name = name,
		.id = id,
	};
	struct dso **res;

	if (dsos->dsos == NULL)
		return NULL;

	if (!dsos->sorted) {
		if (!write_locked) {
			struct dso *dso;

			up_read(&dsos->lock);
			down_write(&dsos->lock);
			dso = __dsos__find_by_longname_id(dsos, name, id,
							  /*write_locked=*/true);
			up_write(&dsos->lock);
			down_read(&dsos->lock);
			return dso;
		}
		qsort(dsos->dsos, dsos->cnt, sizeof(struct dso *),
		      dsos__cmp_long_name_id_short_name);
		dsos->sorted = true;
	}

	res = bsearch(&key, dsos->dsos, dsos->cnt, sizeof(struct dso *),
		      dsos__cmp_key_long_name_id);
	if (!res)
		return NULL;

	return dso__get(*res);
}

int __dsos__add(struct dsos *dsos, struct dso *dso)
{
	if (dsos->cnt == dsos->allocated) {
		unsigned int to_allocate = 2;
		struct dso **temp;

		if (dsos->allocated > 0)
			to_allocate = dsos->allocated * 2;
		temp = realloc(dsos->dsos, sizeof(struct dso *) * to_allocate);
		if (!temp)
			return -ENOMEM;
		dsos->dsos = temp;
		dsos->allocated = to_allocate;
	}
	if (!dsos->sorted) {
		dsos->dsos[dsos->cnt++] = dso__get(dso);
	} else {
		int low = 0, high = dsos->cnt - 1;
		int insert = dsos->cnt; /* Default to inserting at the end. */

		while (low <= high) {
			int mid = low + (high - low) / 2;
			int cmp = dsos__cmp_long_name_id_short_name(&dsos->dsos[mid], &dso);

			if (cmp < 0) {
				low = mid + 1;
			} else {
				high = mid - 1;
				insert = mid;
			}
		}
		memmove(&dsos->dsos[insert + 1], &dsos->dsos[insert],
			(dsos->cnt - insert) * sizeof(struct dso *));
		dsos->cnt++;
		dsos->dsos[insert] = dso__get(dso);
	}
	dso__set_dsos(dso, dsos);
	return 0;
}

int dsos__add(struct dsos *dsos, struct dso *dso)
{
	int ret;

	down_write(&dsos->lock);
	ret = __dsos__add(dsos, dso);
	up_write(&dsos->lock);
	return ret;
}

struct dsos__find_id_cb_args {
	const char *name;
	const struct dso_id *id;
	struct dso *res;
};

static int dsos__find_id_cb(struct dso *dso, void *data)
{
	struct dsos__find_id_cb_args *args = data;

	if (__dso__cmp_short_name(args->name, args->id, dso) == 0) {
		args->res = dso__get(dso);
		return 1;
	}
	return 0;

}

static struct dso *__dsos__find_id(struct dsos *dsos, const char *name, const struct dso_id *id,
				   bool cmp_short, bool write_locked)
	SHARED_LOCKS_REQUIRED(dsos->lock)
{
	struct dso *res;

	if (cmp_short) {
		struct dsos__find_id_cb_args args = {
			.name = name,
			.id = id,
			.res = NULL,
		};

		__dsos__for_each_dso(dsos, dsos__find_id_cb, &args);
		return args.res;
	}
	res = __dsos__find_by_longname_id(dsos, name, id, write_locked);
	return res;
}

struct dso *dsos__find(struct dsos *dsos, const char *name, bool cmp_short)
{
	struct dso *res;

	down_read(&dsos->lock);
	res = __dsos__find_id(dsos, name, &dso_id_empty, cmp_short, /*write_locked=*/false);
	up_read(&dsos->lock);
	return res;
}

static void dso__set_basename(struct dso *dso)
{
	char *base, *lname;
	int tid;

	if (perf_pid_map_tid(dso__long_name(dso), &tid)) {
		if (asprintf(&base, "[JIT] tid %d", tid) < 0)
			return;
	} else {
	      /*
	       * basename() may modify path buffer, so we must pass
               * a copy.
               */
		lname = strdup(dso__long_name(dso));
		if (!lname)
			return;

		/*
		 * basename() may return a pointer to internal
		 * storage which is reused in subsequent calls
		 * so copy the result.
		 */
		base = strdup(basename(lname));

		free(lname);

		if (!base)
			return;
	}
	dso__set_short_name(dso, base, true);
}

static struct dso *__dsos__addnew_id(struct dsos *dsos, const char *name, const struct dso_id *id)
{
	struct dso *dso = dso__new_id(name, id);

	if (dso != NULL) {
		/*
		 * The dsos lock is held on entry, so rename the dso before
		 * adding it to avoid needing to take the dsos lock again to say
		 * the array isn't sorted.
		 */
		dso__set_basename(dso);
		__dsos__add(dsos, dso);
	}
	return dso;
}

static struct dso *__dsos__findnew_id(struct dsos *dsos, const char *name, const struct dso_id *id)
	SHARED_LOCKS_REQUIRED(dsos->lock)
{
	struct dso *dso = __dsos__find_id(dsos, name, id, false, /*write_locked=*/true);

	if (dso)
		__dso__improve_id(dso, id);

	return dso ? dso : __dsos__addnew_id(dsos, name, id);
}

struct dso *dsos__findnew_id(struct dsos *dsos, const char *name, const struct dso_id *id)
{
	struct dso *dso;
	down_write(&dsos->lock);
	dso = __dsos__findnew_id(dsos, name, id);
	up_write(&dsos->lock);
	return dso;
}

struct dsos__fprintf_buildid_cb_args {
	FILE *fp;
	bool (*skip)(struct dso *dso, int parm);
	int parm;
	size_t ret;
};

static int dsos__fprintf_buildid_cb(struct dso *dso, void *data)
{
	struct dsos__fprintf_buildid_cb_args *args = data;
	char sbuild_id[SBUILD_ID_SIZE];

	if (args->skip && args->skip(dso, args->parm))
		return 0;
	build_id__snprintf(dso__bid(dso), sbuild_id, sizeof(sbuild_id));
	args->ret += fprintf(args->fp, "%-40s %s\n", sbuild_id, dso__long_name(dso));
	return 0;
}

size_t dsos__fprintf_buildid(struct dsos *dsos, FILE *fp,
			       bool (*skip)(struct dso *dso, int parm), int parm)
{
	struct dsos__fprintf_buildid_cb_args args = {
		.fp = fp,
		.skip = skip,
		.parm = parm,
		.ret = 0,
	};

	dsos__for_each_dso(dsos, dsos__fprintf_buildid_cb, &args);
	return args.ret;
}

struct dsos__fprintf_cb_args {
	FILE *fp;
	size_t ret;
};

static int dsos__fprintf_cb(struct dso *dso, void *data)
{
	struct dsos__fprintf_cb_args *args = data;

	args->ret += dso__fprintf(dso, args->fp);
	return 0;
}

size_t dsos__fprintf(struct dsos *dsos, FILE *fp)
{
	struct dsos__fprintf_cb_args args = {
		.fp = fp,
		.ret = 0,
	};

	dsos__for_each_dso(dsos, dsos__fprintf_cb, &args);
	return args.ret;
}

static int dsos__hit_all_cb(struct dso *dso, void *data __maybe_unused)
{
	dso__set_hit(dso);
	return 0;
}

int dsos__hit_all(struct dsos *dsos)
{
	return dsos__for_each_dso(dsos, dsos__hit_all_cb, NULL);
}

struct dso *dsos__findnew_module_dso(struct dsos *dsos,
				     struct machine *machine,
				     struct kmod_path *m,
				     const char *filename)
{
	struct dso *dso;

	down_write(&dsos->lock);

	dso = __dsos__find_id(dsos, m->name, &dso_id_empty, /*cmp_short=*/true,
			      /*write_locked=*/true);
	if (dso) {
		up_write(&dsos->lock);
		return dso;
	}
	/*
	 * Failed to find the dso so create it. Change the name before adding it
	 * to the array, to avoid unnecessary sorts and potential locking
	 * issues.
	 */
	dso = dso__new_id(m->name, /*id=*/NULL);
	if (!dso) {
		up_write(&dsos->lock);
		return NULL;
	}
	dso__set_basename(dso);
	dso__set_module_info(dso, m, machine);
	dso__set_long_name(dso,	strdup(filename), true);
	dso__set_kernel(dso, DSO_SPACE__KERNEL);
	__dsos__add(dsos, dso);

	up_write(&dsos->lock);
	return dso;
}

static int dsos__find_kernel_dso_cb(struct dso *dso, void *data)
{
	struct dso **res = data;
	/*
	 * The cpumode passed to is_kernel_module is not the cpumode of *this*
	 * event. If we insist on passing correct cpumode to is_kernel_module,
	 * we should record the cpumode when we adding this dso to the linked
	 * list.
	 *
	 * However we don't really need passing correct cpumode.  We know the
	 * correct cpumode must be kernel mode (if not, we should not link it
	 * onto kernel_dsos list).
	 *
	 * Therefore, we pass PERF_RECORD_MISC_CPUMODE_UNKNOWN.
	 * is_kernel_module() treats it as a kernel cpumode.
	 */
	if (!dso__kernel(dso) ||
	    is_kernel_module(dso__long_name(dso), PERF_RECORD_MISC_CPUMODE_UNKNOWN))
		return 0;

	*res = dso__get(dso);
	return 1;
}

struct dso *dsos__find_kernel_dso(struct dsos *dsos)
{
	struct dso *res = NULL;

	dsos__for_each_dso(dsos, dsos__find_kernel_dso_cb, &res);
	return res;
}

int dsos__for_each_dso(struct dsos *dsos, int (*cb)(struct dso *dso, void *data), void *data)
{
	int err;

	down_read(&dsos->lock);
	err = __dsos__for_each_dso(dsos, cb, data);
	up_read(&dsos->lock);
	return err;
}

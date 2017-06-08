#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/kernel.h>

#include "vdso.h"
#include "util.h"
#include "symbol.h"
#include "machine.h"
#include "thread.h"
#include "linux/string.h"
#include "debug.h"

/*
 * Include definition of find_vdso_map() also used in perf-read-vdso.c for
 * building perf-read-vdso32 and perf-read-vdsox32.
 */
#include "find-vdso-map.c"

#define VDSO__TEMP_FILE_NAME "/tmp/perf-vdso.so-XXXXXX"

struct vdso_file {
	bool found;
	bool error;
	char temp_file_name[sizeof(VDSO__TEMP_FILE_NAME)];
	const char *dso_name;
	const char *read_prog;
};

struct vdso_info {
	struct vdso_file vdso;
#if BITS_PER_LONG == 64
	struct vdso_file vdso32;
	struct vdso_file vdsox32;
#endif
};

static struct vdso_info *vdso_info__new(void)
{
	static const struct vdso_info vdso_info_init = {
		.vdso    = {
			.temp_file_name = VDSO__TEMP_FILE_NAME,
			.dso_name = DSO__NAME_VDSO,
		},
#if BITS_PER_LONG == 64
		.vdso32  = {
			.temp_file_name = VDSO__TEMP_FILE_NAME,
			.dso_name = DSO__NAME_VDSO32,
			.read_prog = "perf-read-vdso32",
		},
		.vdsox32  = {
			.temp_file_name = VDSO__TEMP_FILE_NAME,
			.dso_name = DSO__NAME_VDSOX32,
			.read_prog = "perf-read-vdsox32",
		},
#endif
	};

	return memdup(&vdso_info_init, sizeof(vdso_info_init));
}

static char *get_file(struct vdso_file *vdso_file)
{
	char *vdso = NULL;
	char *buf = NULL;
	void *start, *end;
	size_t size;
	int fd;

	if (vdso_file->found)
		return vdso_file->temp_file_name;

	if (vdso_file->error || find_vdso_map(&start, &end))
		return NULL;

	size = end - start;

	buf = memdup(start, size);
	if (!buf)
		return NULL;

	fd = mkstemp(vdso_file->temp_file_name);
	if (fd < 0)
		goto out;

	if (size == (size_t) write(fd, buf, size))
		vdso = vdso_file->temp_file_name;

	close(fd);

 out:
	free(buf);

	vdso_file->found = (vdso != NULL);
	vdso_file->error = !vdso_file->found;
	return vdso;
}

void machine__exit_vdso(struct machine *machine)
{
	struct vdso_info *vdso_info = machine->vdso_info;

	if (!vdso_info)
		return;

	if (vdso_info->vdso.found)
		unlink(vdso_info->vdso.temp_file_name);
#if BITS_PER_LONG == 64
	if (vdso_info->vdso32.found)
		unlink(vdso_info->vdso32.temp_file_name);
	if (vdso_info->vdsox32.found)
		unlink(vdso_info->vdsox32.temp_file_name);
#endif

	zfree(&machine->vdso_info);
}

static struct dso *__machine__addnew_vdso(struct machine *machine, const char *short_name,
					  const char *long_name)
{
	struct dso *dso;

	dso = dso__new(short_name);
	if (dso != NULL) {
		__dsos__add(&machine->dsos, dso);
		dso__set_long_name(dso, long_name, false);
	}

	return dso;
}

static enum dso_type machine__thread_dso_type(struct machine *machine,
					      struct thread *thread)
{
	enum dso_type dso_type = DSO__TYPE_UNKNOWN;
	struct map *map;
	struct dso *dso;

	map = map_groups__first(thread->mg, MAP__FUNCTION);
	for (; map ; map = map_groups__next(map)) {
		dso = map->dso;
		if (!dso || dso->long_name[0] != '/')
			continue;
		dso_type = dso__type(dso, machine);
		if (dso_type != DSO__TYPE_UNKNOWN)
			break;
	}

	return dso_type;
}

#if BITS_PER_LONG == 64

static int vdso__do_copy_compat(FILE *f, int fd)
{
	char buf[4096];
	size_t count;

	while (1) {
		count = fread(buf, 1, sizeof(buf), f);
		if (ferror(f))
			return -errno;
		if (feof(f))
			break;
		if (count && writen(fd, buf, count) != (ssize_t)count)
			return -errno;
	}

	return 0;
}

static int vdso__copy_compat(const char *prog, int fd)
{
	FILE *f;
	int err;

	f = popen(prog, "r");
	if (!f)
		return -errno;

	err = vdso__do_copy_compat(f, fd);

	if (pclose(f) == -1)
		return -errno;

	return err;
}

static int vdso__create_compat_file(const char *prog, char *temp_name)
{
	int fd, err;

	fd = mkstemp(temp_name);
	if (fd < 0)
		return -errno;

	err = vdso__copy_compat(prog, fd);

	if (close(fd) == -1)
		return -errno;

	return err;
}

static const char *vdso__get_compat_file(struct vdso_file *vdso_file)
{
	int err;

	if (vdso_file->found)
		return vdso_file->temp_file_name;

	if (vdso_file->error)
		return NULL;

	err = vdso__create_compat_file(vdso_file->read_prog,
				       vdso_file->temp_file_name);
	if (err) {
		pr_err("%s failed, error %d\n", vdso_file->read_prog, err);
		vdso_file->error = true;
		return NULL;
	}

	vdso_file->found = true;

	return vdso_file->temp_file_name;
}

static struct dso *__machine__findnew_compat(struct machine *machine,
					     struct vdso_file *vdso_file)
{
	const char *file_name;
	struct dso *dso;

	dso = __dsos__find(&machine->dsos, vdso_file->dso_name, true);
	if (dso)
		goto out;

	file_name = vdso__get_compat_file(vdso_file);
	if (!file_name)
		goto out;

	dso = __machine__addnew_vdso(machine, vdso_file->dso_name, file_name);
out:
	return dso;
}

static int __machine__findnew_vdso_compat(struct machine *machine,
					  struct thread *thread,
					  struct vdso_info *vdso_info,
					  struct dso **dso)
{
	enum dso_type dso_type;

	dso_type = machine__thread_dso_type(machine, thread);

#ifndef HAVE_PERF_READ_VDSO32
	if (dso_type == DSO__TYPE_32BIT)
		return 0;
#endif
#ifndef HAVE_PERF_READ_VDSOX32
	if (dso_type == DSO__TYPE_X32BIT)
		return 0;
#endif

	switch (dso_type) {
	case DSO__TYPE_32BIT:
		*dso = __machine__findnew_compat(machine, &vdso_info->vdso32);
		return 1;
	case DSO__TYPE_X32BIT:
		*dso = __machine__findnew_compat(machine, &vdso_info->vdsox32);
		return 1;
	case DSO__TYPE_UNKNOWN:
	case DSO__TYPE_64BIT:
	default:
		return 0;
	}
}

#endif

static struct dso *machine__find_vdso(struct machine *machine,
				      struct thread *thread)
{
	struct dso *dso = NULL;
	enum dso_type dso_type;

	dso_type = machine__thread_dso_type(machine, thread);
	switch (dso_type) {
	case DSO__TYPE_32BIT:
		dso = __dsos__find(&machine->dsos, DSO__NAME_VDSO32, true);
		if (!dso) {
			dso = __dsos__find(&machine->dsos, DSO__NAME_VDSO,
					   true);
			if (dso && dso_type != dso__type(dso, machine))
				dso = NULL;
		}
		break;
	case DSO__TYPE_X32BIT:
		dso = __dsos__find(&machine->dsos, DSO__NAME_VDSOX32, true);
		break;
	case DSO__TYPE_64BIT:
	case DSO__TYPE_UNKNOWN:
	default:
		dso = __dsos__find(&machine->dsos, DSO__NAME_VDSO, true);
		break;
	}

	return dso;
}

struct dso *machine__findnew_vdso(struct machine *machine,
				  struct thread *thread)
{
	struct vdso_info *vdso_info;
	struct dso *dso = NULL;

	pthread_rwlock_wrlock(&machine->dsos.lock);
	if (!machine->vdso_info)
		machine->vdso_info = vdso_info__new();

	vdso_info = machine->vdso_info;
	if (!vdso_info)
		goto out_unlock;

	dso = machine__find_vdso(machine, thread);
	if (dso)
		goto out_unlock;

#if BITS_PER_LONG == 64
	if (__machine__findnew_vdso_compat(machine, thread, vdso_info, &dso))
		goto out_unlock;
#endif

	dso = __dsos__find(&machine->dsos, DSO__NAME_VDSO, true);
	if (!dso) {
		char *file;

		file = get_file(&vdso_info->vdso);
		if (file)
			dso = __machine__addnew_vdso(machine, DSO__NAME_VDSO, file);
	}

out_unlock:
	dso__get(dso);
	pthread_rwlock_unlock(&machine->dsos.lock);
	return dso;
}

bool dso__is_vdso(struct dso *dso)
{
	return !strcmp(dso->short_name, DSO__NAME_VDSO) ||
	       !strcmp(dso->short_name, DSO__NAME_VDSO32) ||
	       !strcmp(dso->short_name, DSO__NAME_VDSOX32);
}

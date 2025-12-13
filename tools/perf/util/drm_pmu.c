// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "drm_pmu.h"
#include "counts.h"
#include "cpumap.h"
#include "debug.h"
#include "evsel.h"
#include "pmu.h"
#include <perf/threadmap.h>
#include <api/fs/fs.h>
#include <api/io.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <linux/kcmp.h>
#include <linux/zalloc.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

enum drm_pmu_unit {
	DRM_PMU_UNIT_BYTES,
	DRM_PMU_UNIT_CAPACITY,
	DRM_PMU_UNIT_CYCLES,
	DRM_PMU_UNIT_HZ,
	DRM_PMU_UNIT_NS,

	DRM_PMU_UNIT_MAX,
};

struct drm_pmu_event {
	const char *name;
	const char *desc;
	enum drm_pmu_unit unit;
};

struct drm_pmu {
	struct perf_pmu pmu;
	struct drm_pmu_event *events;
	int num_events;
};

static const char * const drm_pmu_unit_strs[DRM_PMU_UNIT_MAX] = {
	"bytes",
	"capacity",
	"cycles",
	"hz",
	"ns",
};

static const char * const drm_pmu_scale_unit_strs[DRM_PMU_UNIT_MAX] = {
	"1bytes",
	"1capacity",
	"1cycles",
	"1hz",
	"1ns",
};

bool perf_pmu__is_drm(const struct perf_pmu *pmu)
{
	return pmu && pmu->type >= PERF_PMU_TYPE_DRM_START &&
		pmu->type <= PERF_PMU_TYPE_DRM_END;
}

bool evsel__is_drm(const struct evsel *evsel)
{
	return perf_pmu__is_drm(evsel->pmu);
}

static struct drm_pmu *add_drm_pmu(struct list_head *pmus, char *line, size_t line_len)
{
	struct drm_pmu *drm;
	struct perf_pmu *pmu;
	const char *name;
	__u32 max_drm_pmu_type = 0, type;
	int i = 12;

	if (line[line_len - 1] == '\n')
		line[line_len - 1] = '\0';
	while (isspace(line[i]))
		i++;

	line[--i] = '_';
	line[--i] = 'm';
	line[--i] = 'r';
	line[--i] = 'd';
	name = &line[i];

	list_for_each_entry(pmu, pmus, list) {
		if (!perf_pmu__is_drm(pmu))
			continue;
		if (pmu->type > max_drm_pmu_type)
			max_drm_pmu_type = pmu->type;
		if (!strcmp(pmu->name, name)) {
			/* PMU already exists. */
			return NULL;
		}
	}

	if (max_drm_pmu_type != 0)
		type = max_drm_pmu_type + 1;
	else
		type = PERF_PMU_TYPE_DRM_START;

	if (type > PERF_PMU_TYPE_DRM_END) {
		zfree(&drm);
		pr_err("Unable to encode DRM PMU type for %s\n", name);
		return NULL;
	}

	drm = zalloc(sizeof(*drm));
	if (!drm)
		return NULL;

	if (perf_pmu__init(&drm->pmu, type, name) != 0) {
		perf_pmu__delete(&drm->pmu);
		return NULL;
	}

	drm->pmu.cpus = perf_cpu_map__new("0");
	if (!drm->pmu.cpus) {
		perf_pmu__delete(&drm->pmu);
		return NULL;
	}
	return drm;
}


static bool starts_with(const char *str, const char *prefix)
{
	return !strncmp(prefix, str, strlen(prefix));
}

static int add_event(struct drm_pmu_event **events, int *num_events,
		     const char *line, enum drm_pmu_unit unit, const char *desc)
{
	const char *colon = strchr(line, ':');
	struct drm_pmu_event *tmp;

	if (!colon)
		return -EINVAL;

	tmp = reallocarray(*events, *num_events + 1, sizeof(struct drm_pmu_event));
	if (!tmp)
		return -ENOMEM;
	tmp[*num_events].unit = unit;
	tmp[*num_events].desc = desc;
	tmp[*num_events].name = strndup(line, colon - line);
	if (!tmp[*num_events].name)
		return -ENOMEM;
	(*num_events)++;
	*events = tmp;
	return 0;
}

static int read_drm_pmus_cb(void *args, int fdinfo_dir_fd, const char *fd_name)
{
	struct list_head *pmus = args;
	char buf[640];
	struct io io;
	char *line = NULL;
	size_t line_len;
	struct drm_pmu *drm = NULL;
	struct drm_pmu_event *events = NULL;
	int num_events = 0;

	io__init(&io, openat(fdinfo_dir_fd, fd_name, O_RDONLY), buf, sizeof(buf));
	if (io.fd == -1) {
		/* Failed to open file, ignore. */
		return 0;
	}

	while (io__getline(&io, &line, &line_len) > 0) {
		if (starts_with(line, "drm-driver:")) {
			drm = add_drm_pmu(pmus, line, line_len);
			if (!drm)
				break;
			continue;
		}
		/*
		 * Note the string matching below is alphabetical, with more
		 * specific matches appearing before less specific.
		 */
		if (starts_with(line, "drm-active-")) {
			add_event(&events, &num_events, line, DRM_PMU_UNIT_BYTES,
				  "Total memory active in one or more engines");
			continue;
		}
		if (starts_with(line, "drm-cycles-")) {
			add_event(&events, &num_events, line, DRM_PMU_UNIT_CYCLES,
				"Busy cycles");
			continue;
		}
		if (starts_with(line, "drm-engine-capacity-")) {
			add_event(&events, &num_events, line, DRM_PMU_UNIT_CAPACITY,
				"Engine capacity");
			continue;
		}
		if (starts_with(line, "drm-engine-")) {
			add_event(&events, &num_events, line, DRM_PMU_UNIT_NS,
				  "Utilization in ns");
			continue;
		}
		if (starts_with(line, "drm-maxfreq-")) {
			add_event(&events, &num_events, line, DRM_PMU_UNIT_HZ,
				  "Maximum frequency");
			continue;
		}
		if (starts_with(line, "drm-purgeable-")) {
			add_event(&events, &num_events, line, DRM_PMU_UNIT_BYTES,
				  "Size of resident and purgeable memory buffers");
			continue;
		}
		if (starts_with(line, "drm-resident-")) {
			add_event(&events, &num_events, line, DRM_PMU_UNIT_BYTES,
				  "Size of resident memory buffers");
			continue;
		}
		if (starts_with(line, "drm-shared-")) {
			add_event(&events, &num_events, line, DRM_PMU_UNIT_BYTES,
				  "Size of shared memory buffers");
			continue;
		}
		if (starts_with(line, "drm-total-cycles-")) {
			add_event(&events, &num_events, line, DRM_PMU_UNIT_BYTES,
				  "Total busy cycles");
			continue;
		}
		if (starts_with(line, "drm-total-")) {
			add_event(&events, &num_events, line, DRM_PMU_UNIT_BYTES,
				  "Size of shared and private memory");
			continue;
		}
		if (verbose > 1 && starts_with(line, "drm-") &&
		    !starts_with(line, "drm-client-id:") &&
		    !starts_with(line, "drm-pdev:"))
			pr_debug("Unhandled DRM PMU fdinfo line match '%s'\n", line);
	}
	if (drm) {
		drm->events = events;
		drm->num_events = num_events;
		list_add_tail(&drm->pmu.list, pmus);
	}
	free(line);
	if (io.fd != -1)
		close(io.fd);
	return 0;
}

void drm_pmu__exit(struct perf_pmu *pmu)
{
	struct drm_pmu *drm = container_of(pmu, struct drm_pmu, pmu);

	free(drm->events);
}

bool drm_pmu__have_event(const struct perf_pmu *pmu, const char *name)
{
	struct drm_pmu *drm = container_of(pmu, struct drm_pmu, pmu);

	if (!starts_with(name, "drm-"))
		return false;

	for (int i = 0; i < drm->num_events; i++) {
		if (!strcasecmp(drm->events[i].name, name))
			return true;
	}
	return false;
}

int drm_pmu__for_each_event(const struct perf_pmu *pmu, void *state, pmu_event_callback cb)
{
	struct drm_pmu *drm = container_of(pmu, struct drm_pmu, pmu);

	for (int i = 0; i < drm->num_events; i++) {
		char encoding_buf[128];
		struct pmu_event_info info = {
			.pmu = pmu,
			.name = drm->events[i].name,
			.alias = NULL,
			.scale_unit = drm_pmu_scale_unit_strs[drm->events[i].unit],
			.desc = drm->events[i].desc,
			.long_desc = NULL,
			.encoding_desc = encoding_buf,
			.topic = "drm",
			.pmu_name = pmu->name,
			.event_type_desc = "DRM event",
		};
		int ret;

		snprintf(encoding_buf, sizeof(encoding_buf), "%s/config=0x%x/", pmu->name, i);

		ret = cb(state, &info);
		if (ret)
			return ret;
	}
	return 0;
}

size_t drm_pmu__num_events(const struct perf_pmu *pmu)
{
	const struct drm_pmu *drm = container_of(pmu, struct drm_pmu, pmu);

	return drm->num_events;
}

static int drm_pmu__index_for_event(const struct drm_pmu *drm, const char *name)
{
	for (int i = 0; i < drm->num_events; i++) {
		if (!strcmp(drm->events[i].name, name))
			return i;
	}
	return -1;
}

static int drm_pmu__config_term(const struct drm_pmu *drm,
				  struct perf_event_attr *attr,
				  struct parse_events_term *term,
				  struct parse_events_error *err)
{
	if (term->type_term == PARSE_EVENTS__TERM_TYPE_USER) {
		int i = drm_pmu__index_for_event(drm, term->config);

		if (i >= 0) {
			attr->config = i;
			return 0;
		}
	}
	if (err) {
		char *err_str;

		parse_events_error__handle(err, term->err_val,
					asprintf(&err_str,
						"unexpected drm event term (%s) %s",
						parse_events__term_type_str(term->type_term),
						term->config) < 0
					? strdup("unexpected drm event term")
					: err_str,
					NULL);
	}
	return -EINVAL;
}

int drm_pmu__config_terms(const struct perf_pmu *pmu,
			    struct perf_event_attr *attr,
			    struct parse_events_terms *terms,
			    struct parse_events_error *err)
{
	struct drm_pmu *drm = container_of(pmu, struct drm_pmu, pmu);
	struct parse_events_term *term;

	list_for_each_entry(term, &terms->terms, list) {
		if (drm_pmu__config_term(drm, attr, term, err))
			return -EINVAL;
	}

	return 0;
}

int drm_pmu__check_alias(const struct perf_pmu *pmu, struct parse_events_terms *terms,
			 struct perf_pmu_info *info, struct parse_events_error *err)
{
	struct drm_pmu *drm = container_of(pmu, struct drm_pmu, pmu);
	struct parse_events_term *term =
		list_first_entry(&terms->terms, struct parse_events_term, list);

	if (term->type_term == PARSE_EVENTS__TERM_TYPE_USER) {
		int i = drm_pmu__index_for_event(drm, term->config);

		if (i >= 0) {
			info->unit = drm_pmu_unit_strs[drm->events[i].unit];
			info->scale = 1;
			return 0;
		}
	}
	if (err) {
		char *err_str;

		parse_events_error__handle(err, term->err_val,
					asprintf(&err_str,
						"unexpected drm event term (%s) %s",
						parse_events__term_type_str(term->type_term),
						term->config) < 0
					? strdup("unexpected drm event term")
					: err_str,
					NULL);
	}
	return -EINVAL;
}

struct minor_info {
	unsigned int *minors;
	int minors_num, minors_len;
};

static int for_each_drm_fdinfo_in_dir(int (*cb)(void *args, int fdinfo_dir_fd, const char *fd_name),
				      void *args, int proc_dir, const char *pid_name,
				      struct minor_info *minors)
{
	char buf[256];
	DIR *fd_dir;
	struct dirent *fd_entry;
	int fd_dir_fd, fdinfo_dir_fd = -1;


	scnprintf(buf, sizeof(buf), "%s/fd", pid_name);
	fd_dir_fd = openat(proc_dir, buf, O_DIRECTORY);
	if (fd_dir_fd == -1)
		return 0; /* Presumably lost race to open. */
	fd_dir = fdopendir(fd_dir_fd);
	if (!fd_dir) {
		close(fd_dir_fd);
		return -ENOMEM;
	}
	while ((fd_entry = readdir(fd_dir)) != NULL) {
		struct stat stat;
		unsigned int minor;
		bool is_dup = false;
		int ret;

		if (fd_entry->d_type != DT_LNK)
			continue;

		if (fstatat(fd_dir_fd, fd_entry->d_name, &stat, 0) != 0)
			continue;

		if ((stat.st_mode & S_IFMT) != S_IFCHR || major(stat.st_rdev) != 226)
			continue;

		minor = minor(stat.st_rdev);
		for (int i = 0; i < minors->minors_num; i++) {
			if (minor(stat.st_rdev) == minors->minors[i]) {
				is_dup = true;
				break;
			}
		}
		if (is_dup)
			continue;

		if (minors->minors_num == minors->minors_len) {
			unsigned int *tmp = reallocarray(minors->minors, minors->minors_len + 4,
							 sizeof(unsigned int));

			if (tmp) {
				minors->minors = tmp;
				minors->minors_len += 4;
			}
		}
		minors->minors[minors->minors_num++] = minor;
		if (fdinfo_dir_fd == -1) {
			/* Open fdinfo dir if we have a DRM fd. */
			scnprintf(buf, sizeof(buf), "%s/fdinfo", pid_name);
			fdinfo_dir_fd = openat(proc_dir, buf, O_DIRECTORY);
			if (fdinfo_dir_fd == -1)
				continue;
		}
		ret = cb(args, fdinfo_dir_fd, fd_entry->d_name);
		if (ret)
			goto close_fdinfo;
	}

close_fdinfo:
	if (fdinfo_dir_fd != -1)
		close(fdinfo_dir_fd);
	closedir(fd_dir);
	return 0;
}

static int for_each_drm_fdinfo(bool skip_all_duplicates,
			       int (*cb)(void *args, int fdinfo_dir_fd, const char *fd_name),
			       void *args)
{
	DIR *proc_dir;
	struct dirent *proc_entry;
	int ret;
	/*
	 * minors maintains an array of DRM minor device numbers seen for a pid,
	 * or for all pids if skip_all_duplicates is true, so that duplicates
	 * are ignored.
	 */
	struct minor_info minors = {
		.minors = NULL,
		.minors_num = 0,
		.minors_len = 0,
	};

	proc_dir = opendir(procfs__mountpoint());
	if (!proc_dir)
		return 0;

	/* Walk through the /proc directory. */
	while ((proc_entry = readdir(proc_dir)) != NULL) {
		if (proc_entry->d_type != DT_DIR ||
		    !isdigit(proc_entry->d_name[0]))
			continue;
		if (!skip_all_duplicates) {
			/* Reset the seen minor numbers for each pid. */
			minors.minors_num = 0;
		}
		ret = for_each_drm_fdinfo_in_dir(cb, args,
						 dirfd(proc_dir), proc_entry->d_name,
						 &minors);
		if (ret)
			break;
	}
	free(minors.minors);
	closedir(proc_dir);
	return ret;
}

int perf_pmus__read_drm_pmus(struct list_head *pmus)
{
	return for_each_drm_fdinfo(/*skip_all_duplicates=*/true, read_drm_pmus_cb, pmus);
}

int evsel__drm_pmu_open(struct evsel *evsel,
			struct perf_thread_map *threads,
			int start_cpu_map_idx, int end_cpu_map_idx)
{
	(void)evsel;
	(void)threads;
	(void)start_cpu_map_idx;
	(void)end_cpu_map_idx;
	return 0;
}

static uint64_t read_count_and_apply_unit(const char *count_and_unit, enum drm_pmu_unit unit)
{
	char *unit_ptr = NULL;
	uint64_t count = strtoul(count_and_unit, &unit_ptr, 10);

	if (!unit_ptr)
		return 0;

	while (isblank(*unit_ptr))
		unit_ptr++;

	switch (unit) {
	case DRM_PMU_UNIT_BYTES:
		if (*unit_ptr == '\0')
			assert(count == 0); /* Generally undocumented, happens for 0. */
		else if (!strcmp(unit_ptr, "KiB"))
			count *= 1024;
		else if (!strcmp(unit_ptr, "MiB"))
			count *= 1024 * 1024;
		else
			pr_err("Unexpected bytes unit '%s'\n", unit_ptr);
		break;
	case DRM_PMU_UNIT_CAPACITY:
		/* No units expected. */
		break;
	case DRM_PMU_UNIT_CYCLES:
		/* No units expected. */
		break;
	case DRM_PMU_UNIT_HZ:
		if (!strcmp(unit_ptr, "Hz"))
			count *= 1;
		else if (!strcmp(unit_ptr, "KHz"))
			count *= 1000;
		else if (!strcmp(unit_ptr, "MHz"))
			count *= 1000000;
		else
			pr_err("Unexpected hz unit '%s'\n", unit_ptr);
		break;
	case DRM_PMU_UNIT_NS:
		/* Only unit ns expected. */
		break;
	case DRM_PMU_UNIT_MAX:
	default:
		break;
	}
	return count;
}

static uint64_t read_drm_event(int fdinfo_dir_fd, const char *fd_name,
			       const char *match, enum drm_pmu_unit unit)
{
	char buf[640];
	struct io io;
	char *line = NULL;
	size_t line_len;
	uint64_t count = 0;

	io__init(&io, openat(fdinfo_dir_fd, fd_name, O_RDONLY), buf, sizeof(buf));
	if (io.fd == -1) {
		/* Failed to open file, ignore. */
		return 0;
	}
	while (io__getline(&io, &line, &line_len) > 0) {
		size_t i = strlen(match);

		if (strncmp(line, match, i))
			continue;
		if (line[i] != ':')
			continue;
		while (isblank(line[++i]))
			;
		if (line[line_len - 1] == '\n')
			line[line_len - 1] = '\0';
		count = read_count_and_apply_unit(&line[i], unit);
		break;
	}
	free(line);
	close(io.fd);
	return count;
}

struct read_drm_event_cb_args {
	const char *match;
	uint64_t count;
	enum drm_pmu_unit unit;
};

static int read_drm_event_cb(void *vargs, int fdinfo_dir_fd, const char *fd_name)
{
	struct read_drm_event_cb_args *args = vargs;

	args->count += read_drm_event(fdinfo_dir_fd, fd_name, args->match, args->unit);
	return 0;
}

static uint64_t drm_pmu__read_system_wide(struct drm_pmu *drm, struct evsel *evsel)
{
	struct read_drm_event_cb_args args = {
		.count = 0,
		.match = drm->events[evsel->core.attr.config].name,
		.unit = drm->events[evsel->core.attr.config].unit,
	};

	for_each_drm_fdinfo(/*skip_all_duplicates=*/false, read_drm_event_cb, &args);
	return args.count;
}

static uint64_t drm_pmu__read_for_pid(struct drm_pmu *drm, struct evsel *evsel, int pid)
{
	struct read_drm_event_cb_args args = {
		.count = 0,
		.match = drm->events[evsel->core.attr.config].name,
		.unit = drm->events[evsel->core.attr.config].unit,
	};
	struct minor_info minors = {
		.minors = NULL,
		.minors_num = 0,
		.minors_len = 0,
	};
	int proc_dir = open(procfs__mountpoint(), O_DIRECTORY);
	char pid_name[12];
	int ret;

	if (proc_dir < 0)
		return 0;

	snprintf(pid_name, sizeof(pid_name), "%d", pid);
	ret = for_each_drm_fdinfo_in_dir(read_drm_event_cb, &args, proc_dir, pid_name, &minors);
	free(minors.minors);
	close(proc_dir);
	return ret == 0 ? args.count : 0;
}

int evsel__drm_pmu_read(struct evsel *evsel, int cpu_map_idx, int thread)
{
	struct drm_pmu *drm = container_of(evsel->pmu, struct drm_pmu, pmu);
	struct perf_counts_values *count, *old_count = NULL;
	int pid = perf_thread_map__pid(evsel->core.threads, thread);
	uint64_t counter;

	if (pid != -1)
		counter = drm_pmu__read_for_pid(drm, evsel, pid);
	else
		counter = drm_pmu__read_system_wide(drm, evsel);

	if (evsel->prev_raw_counts)
		old_count = perf_counts(evsel->prev_raw_counts, cpu_map_idx, thread);

	count = perf_counts(evsel->counts, cpu_map_idx, thread);
	if (old_count) {
		count->val = old_count->val + counter;
		count->run = old_count->run + 1;
		count->ena = old_count->ena + 1;
	} else {
		count->val = counter;
		count->run++;
		count->ena++;
	}
	return 0;
}

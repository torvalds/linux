// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "counts.h"
#include "debug.h"
#include "evsel.h"
#include "hashmap.h"
#include "hwmon_pmu.h"
#include "pmu.h"
#include <internal/xyarray.h>
#include <internal/threadmap.h>
#include <perf/threadmap.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <api/fs/fs.h>
#include <api/io.h>
#include <api/io_dir.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>

/** Strings that correspond to enum hwmon_type. */
static const char * const hwmon_type_strs[HWMON_TYPE_MAX] = {
	NULL,
	"cpu",
	"curr",
	"energy",
	"fan",
	"humidity",
	"in",
	"intrusion",
	"power",
	"pwm",
	"temp",
};
#define LONGEST_HWMON_TYPE_STR "intrusion"

/** Strings that correspond to enum hwmon_item. */
static const char * const hwmon_item_strs[HWMON_ITEM__MAX] = {
	NULL,
	"accuracy",
	"alarm",
	"auto_channels_temp",
	"average",
	"average_highest",
	"average_interval",
	"average_interval_max",
	"average_interval_min",
	"average_lowest",
	"average_max",
	"average_min",
	"beep",
	"cap",
	"cap_hyst",
	"cap_max",
	"cap_min",
	"crit",
	"crit_hyst",
	"div",
	"emergency",
	"emergency_hist",
	"enable",
	"fault",
	"freq",
	"highest",
	"input",
	"label",
	"lcrit",
	"lcrit_hyst",
	"lowest",
	"max",
	"max_hyst",
	"min",
	"min_hyst",
	"mod",
	"offset",
	"pulses",
	"rated_max",
	"rated_min",
	"reset_history",
	"target",
	"type",
	"vid",
};
#define LONGEST_HWMON_ITEM_STR "average_interval_max"

static const char *const hwmon_units[HWMON_TYPE_MAX] = {
	NULL,
	"V",   /* cpu */
	"A",   /* curr */
	"J",   /* energy */
	"rpm", /* fan */
	"%",   /* humidity */
	"V",   /* in */
	"",    /* intrusion */
	"W",   /* power */
	"Hz",  /* pwm */
	"'C",  /* temp */
};

struct hwmon_pmu {
	struct perf_pmu pmu;
	struct hashmap events;
	int hwmon_dir_fd;
};

/**
 * struct hwmon_pmu_event_value: Value in hwmon_pmu->events.
 *
 * Hwmon files are of the form <type><number>_<item> and may have a suffix
 * _alarm.
 */
struct hwmon_pmu_event_value {
	/** @items: which item files are present. */
	DECLARE_BITMAP(items, HWMON_ITEM__MAX);
	/** @alarm_items: which item files are present. */
	DECLARE_BITMAP(alarm_items, HWMON_ITEM__MAX);
	/** @label: contents of <type><number>_label if present. */
	char *label;
	/** @name: name computed from label of the form <type>_<label>. */
	char *name;
};

bool perf_pmu__is_hwmon(const struct perf_pmu *pmu)
{
	return pmu && pmu->type >= PERF_PMU_TYPE_HWMON_START &&
		pmu->type <= PERF_PMU_TYPE_HWMON_END;
}

bool evsel__is_hwmon(const struct evsel *evsel)
{
	return perf_pmu__is_hwmon(evsel->pmu);
}

static size_t hwmon_pmu__event_hashmap_hash(long key, void *ctx __maybe_unused)
{
	return ((union hwmon_pmu_event_key)key).type_and_num;
}

static bool hwmon_pmu__event_hashmap_equal(long key1, long key2, void *ctx __maybe_unused)
{
	return ((union hwmon_pmu_event_key)key1).type_and_num ==
	       ((union hwmon_pmu_event_key)key2).type_and_num;
}

static int hwmon_strcmp(const void *a, const void *b)
{
	const char *sa = a;
	const char * const *sb = b;

	return strcmp(sa, *sb);
}

bool parse_hwmon_filename(const char *filename,
			  enum hwmon_type *type,
			  int *number,
			  enum hwmon_item *item,
			  bool *alarm)
{
	char fn_type[24];
	const char **elem;
	const char *fn_item = NULL;
	size_t fn_item_len;

	assert(strlen(LONGEST_HWMON_TYPE_STR) < sizeof(fn_type));
	strlcpy(fn_type, filename, sizeof(fn_type));
	for (size_t i = 0; fn_type[i] != '\0'; i++) {
		if (fn_type[i] >= '0' && fn_type[i] <= '9') {
			fn_type[i] = '\0';
			*number = strtoul(&filename[i], (char **)&fn_item, 10);
			if (*fn_item == '_')
				fn_item++;
			break;
		}
		if (fn_type[i] == '_') {
			fn_type[i] = '\0';
			*number = -1;
			fn_item = &filename[i + 1];
			break;
		}
	}
	if (fn_item == NULL || fn_type[0] == '\0' || (item != NULL && fn_item[0] == '\0')) {
		pr_debug3("hwmon_pmu: not a hwmon file '%s'\n", filename);
		return false;
	}
	elem = bsearch(&fn_type, hwmon_type_strs + 1, ARRAY_SIZE(hwmon_type_strs) - 1,
		       sizeof(hwmon_type_strs[0]), hwmon_strcmp);
	if (!elem) {
		pr_debug3("hwmon_pmu: not a hwmon type '%s' in file name '%s'\n",
			 fn_type, filename);
		return false;
	}

	*type = elem - &hwmon_type_strs[0];
	if (!item)
		return true;

	*alarm = false;
	fn_item_len = strlen(fn_item);
	if (fn_item_len > 6 && !strcmp(&fn_item[fn_item_len - 6], "_alarm")) {
		assert(strlen(LONGEST_HWMON_ITEM_STR) < sizeof(fn_type));
		strlcpy(fn_type, fn_item, fn_item_len - 5);
		fn_item = fn_type;
		*alarm = true;
	}
	elem = bsearch(fn_item, hwmon_item_strs + 1, ARRAY_SIZE(hwmon_item_strs) - 1,
		       sizeof(hwmon_item_strs[0]), hwmon_strcmp);
	if (!elem) {
		pr_debug3("hwmon_pmu: not a hwmon item '%s' in file name '%s'\n",
			 fn_item, filename);
		return false;
	}
	*item = elem - &hwmon_item_strs[0];
	return true;
}

static void fix_name(char *p)
{
	char *s = strchr(p, '\n');

	if (s)
		*s = '\0';

	while (*p != '\0') {
		if (strchr(" :,/\n\t", *p))
			*p = '_';
		else
			*p = tolower(*p);
		p++;
	}
}

static int hwmon_pmu__read_events(struct hwmon_pmu *pmu)
{
	int err = 0;
	struct hashmap_entry *cur, *tmp;
	size_t bkt;
	struct io_dirent64 *ent;
	struct io_dir dir;

	if (pmu->pmu.sysfs_aliases_loaded)
		return 0;

	/* Use openat so that the directory contents are refreshed. */
	io_dir__init(&dir, openat(pmu->hwmon_dir_fd, ".", O_CLOEXEC | O_DIRECTORY | O_RDONLY));

	if (dir.dirfd < 0)
		return -ENOENT;

	while ((ent = io_dir__readdir(&dir)) != NULL) {
		enum hwmon_type type;
		int number;
		enum hwmon_item item;
		bool alarm;
		union hwmon_pmu_event_key key = { .type_and_num = 0 };
		struct hwmon_pmu_event_value *value;

		if (ent->d_type != DT_REG)
			continue;

		if (!parse_hwmon_filename(ent->d_name, &type, &number, &item, &alarm)) {
			pr_debug3("Not a hwmon file '%s'\n", ent->d_name);
			continue;
		}
		key.num = number;
		key.type = type;
		if (!hashmap__find(&pmu->events, key.type_and_num, &value)) {
			value = zalloc(sizeof(*value));
			if (!value) {
				err = -ENOMEM;
				goto err_out;
			}
			err = hashmap__add(&pmu->events, key.type_and_num, value);
			if (err) {
				free(value);
				err = -ENOMEM;
				goto err_out;
			}
		}
		__set_bit(item, alarm ? value->alarm_items : value->items);
		if (item == HWMON_ITEM_LABEL) {
			char buf[128];
			int fd = openat(pmu->hwmon_dir_fd, ent->d_name, O_RDONLY);
			ssize_t read_len;

			if (fd < 0)
				continue;

			read_len = read(fd, buf, sizeof(buf));

			while (read_len > 0 && buf[read_len - 1] == '\n')
				read_len--;

			if (read_len > 0)
				buf[read_len] = '\0';

			if (buf[0] == '\0') {
				pr_debug("hwmon_pmu: empty label file %s %s\n",
					 pmu->pmu.name, ent->d_name);
				close(fd);
				continue;
			}
			value->label = strdup(buf);
			if (!value->label) {
				pr_debug("hwmon_pmu: memory allocation failure\n");
				close(fd);
				continue;
			}
			snprintf(buf, sizeof(buf), "%s_%s", hwmon_type_strs[type], value->label);
			fix_name(buf);
			value->name = strdup(buf);
			if (!value->name)
				pr_debug("hwmon_pmu: memory allocation failure\n");
			close(fd);
		}
	}
	if (hashmap__size(&pmu->events) == 0)
		pr_debug2("hwmon_pmu: %s has no events\n", pmu->pmu.name);

	hashmap__for_each_entry_safe((&pmu->events), cur, tmp, bkt) {
		union hwmon_pmu_event_key key = {
			.type_and_num = cur->key,
		};
		struct hwmon_pmu_event_value *value = cur->pvalue;

		if (!test_bit(HWMON_ITEM_INPUT, value->items)) {
			pr_debug("hwmon_pmu: %s removing event '%s%d' that has no input file\n",
				pmu->pmu.name, hwmon_type_strs[key.type], key.num);
			hashmap__delete(&pmu->events, key.type_and_num, &key, &value);
			zfree(&value->label);
			zfree(&value->name);
			free(value);
		}
	}
	pmu->pmu.sysfs_aliases_loaded = true;

err_out:
	close(dir.dirfd);
	return err;
}

struct perf_pmu *hwmon_pmu__new(struct list_head *pmus, int hwmon_dir, const char *sysfs_name, const char *name)
{
	char buf[32];
	struct hwmon_pmu *hwm;

	hwm = zalloc(sizeof(*hwm));
	if (!hwm)
		return NULL;

	hwm->hwmon_dir_fd = hwmon_dir;
	hwm->pmu.type = PERF_PMU_TYPE_HWMON_START + strtoul(sysfs_name + 5, NULL, 10);
	if (hwm->pmu.type > PERF_PMU_TYPE_HWMON_END) {
		pr_err("Unable to encode hwmon type from %s in valid PMU type\n", sysfs_name);
		goto err_out;
	}
	snprintf(buf, sizeof(buf), "hwmon_%s", name);
	fix_name(buf + 6);
	hwm->pmu.name = strdup(buf);
	if (!hwm->pmu.name)
		goto err_out;
	hwm->pmu.alias_name = strdup(sysfs_name);
	if (!hwm->pmu.alias_name)
		goto err_out;
	hwm->pmu.cpus = perf_cpu_map__new("0");
	if (!hwm->pmu.cpus)
		goto err_out;
	INIT_LIST_HEAD(&hwm->pmu.format);
	INIT_LIST_HEAD(&hwm->pmu.aliases);
	INIT_LIST_HEAD(&hwm->pmu.caps);
	hashmap__init(&hwm->events, hwmon_pmu__event_hashmap_hash,
		      hwmon_pmu__event_hashmap_equal, /*ctx=*/NULL);

	list_add_tail(&hwm->pmu.list, pmus);
	return &hwm->pmu;
err_out:
	free((char *)hwm->pmu.name);
	free(hwm->pmu.alias_name);
	free(hwm);
	close(hwmon_dir);
	return NULL;
}

void hwmon_pmu__exit(struct perf_pmu *pmu)
{
	struct hwmon_pmu *hwm = container_of(pmu, struct hwmon_pmu, pmu);
	struct hashmap_entry *cur, *tmp;
	size_t bkt;

	hashmap__for_each_entry_safe((&hwm->events), cur, tmp, bkt) {
		struct hwmon_pmu_event_value *value = cur->pvalue;

		zfree(&value->label);
		zfree(&value->name);
		free(value);
	}
	hashmap__clear(&hwm->events);
	close(hwm->hwmon_dir_fd);
}

static size_t hwmon_pmu__describe_items(struct hwmon_pmu *hwm, char *out_buf, size_t out_buf_len,
					union hwmon_pmu_event_key key,
					const unsigned long *items, bool is_alarm)
{
	size_t bit;
	char buf[64];
	size_t len = 0;

	for_each_set_bit(bit, items, HWMON_ITEM__MAX) {
		int fd;

		if (bit == HWMON_ITEM_LABEL || bit == HWMON_ITEM_INPUT)
			continue;

		snprintf(buf, sizeof(buf), "%s%d_%s%s",
			hwmon_type_strs[key.type],
			key.num,
			hwmon_item_strs[bit],
			is_alarm ? "_alarm" : "");
		fd = openat(hwm->hwmon_dir_fd, buf, O_RDONLY);
		if (fd > 0) {
			ssize_t read_len = read(fd, buf, sizeof(buf));

			while (read_len > 0 && buf[read_len - 1] == '\n')
				read_len--;

			if (read_len > 0) {
				long long val;

				buf[read_len] = '\0';
				val = strtoll(buf, /*endptr=*/NULL, 10);
				len += snprintf(out_buf + len, out_buf_len - len, "%s%s%s=%g%s",
						len == 0 ? " " : ", ",
						hwmon_item_strs[bit],
						is_alarm ? "_alarm" : "",
						(double)val / 1000.0,
						hwmon_units[key.type]);
			}
			close(fd);
		}
	}
	return len;
}

int hwmon_pmu__for_each_event(struct perf_pmu *pmu, void *state, pmu_event_callback cb)
{
	struct hwmon_pmu *hwm = container_of(pmu, struct hwmon_pmu, pmu);
	struct hashmap_entry *cur;
	size_t bkt;

	if (hwmon_pmu__read_events(hwm))
		return false;

	hashmap__for_each_entry((&hwm->events), cur, bkt) {
		static const char *const hwmon_scale_units[HWMON_TYPE_MAX] = {
			NULL,
			"0.001V", /* cpu */
			"0.001A", /* curr */
			"0.001J", /* energy */
			"1rpm",   /* fan */
			"0.001%", /* humidity */
			"0.001V", /* in */
			NULL,     /* intrusion */
			"0.001W", /* power */
			"1Hz",    /* pwm */
			"0.001'C", /* temp */
		};
		static const char *const hwmon_desc[HWMON_TYPE_MAX] = {
			NULL,
			"CPU core reference voltage",   /* cpu */
			"Current",                      /* curr */
			"Cumulative energy use",        /* energy */
			"Fan",                          /* fan */
			"Humidity",                     /* humidity */
			"Voltage",                      /* in */
			"Chassis intrusion detection",  /* intrusion */
			"Power use",                    /* power */
			"Pulse width modulation fan control", /* pwm */
			"Temperature",                  /* temp */
		};
		char alias_buf[64];
		char desc_buf[256];
		char encoding_buf[128];
		union hwmon_pmu_event_key key = {
			.type_and_num = cur->key,
		};
		struct hwmon_pmu_event_value *value = cur->pvalue;
		struct pmu_event_info info = {
			.pmu = pmu,
			.name = value->name,
			.alias = alias_buf,
			.scale_unit = hwmon_scale_units[key.type],
			.desc = desc_buf,
			.long_desc = NULL,
			.encoding_desc = encoding_buf,
			.topic = "hwmon",
			.pmu_name = pmu->name,
			.event_type_desc = "Hwmon event",
		};
		int ret;
		size_t len;

		len = snprintf(alias_buf, sizeof(alias_buf), "%s%d",
			       hwmon_type_strs[key.type], key.num);
		if (!info.name) {
			info.name = info.alias;
			info.alias = NULL;
		}

		len = snprintf(desc_buf, sizeof(desc_buf), "%s in unit %s named %s.",
			hwmon_desc[key.type],
			pmu->name + 6,
			value->label ?: info.name);

		len += hwmon_pmu__describe_items(hwm, desc_buf + len, sizeof(desc_buf) - len,
						key, value->items, /*is_alarm=*/false);

		len += hwmon_pmu__describe_items(hwm, desc_buf + len, sizeof(desc_buf) - len,
						key, value->alarm_items, /*is_alarm=*/true);

		snprintf(encoding_buf, sizeof(encoding_buf), "%s/config=0x%lx/",
			 pmu->name, cur->key);

		ret = cb(state, &info);
		if (ret)
			return ret;
	}
	return 0;
}

size_t hwmon_pmu__num_events(struct perf_pmu *pmu)
{
	struct hwmon_pmu *hwm = container_of(pmu, struct hwmon_pmu, pmu);

	hwmon_pmu__read_events(hwm);
	return hashmap__size(&hwm->events);
}

bool hwmon_pmu__have_event(struct perf_pmu *pmu, const char *name)
{
	struct hwmon_pmu *hwm = container_of(pmu, struct hwmon_pmu, pmu);
	enum hwmon_type type;
	int number;
	union hwmon_pmu_event_key key = { .type_and_num = 0 };
	struct hashmap_entry *cur;
	size_t bkt;

	if (!parse_hwmon_filename(name, &type, &number, /*item=*/NULL, /*is_alarm=*/NULL))
		return false;

	if (hwmon_pmu__read_events(hwm))
		return false;

	key.type = type;
	key.num = number;
	if (hashmap_find(&hwm->events, key.type_and_num, /*value=*/NULL))
		return true;
	if (key.num != -1)
		return false;
	/* Item is of form <type>_ which means we should match <type>_<label>. */
	hashmap__for_each_entry((&hwm->events), cur, bkt) {
		struct hwmon_pmu_event_value *value = cur->pvalue;

		key.type_and_num = cur->key;
		if (key.type == type && value->name && !strcasecmp(name, value->name))
			return true;
	}
	return false;
}

static int hwmon_pmu__config_term(const struct hwmon_pmu *hwm,
				  struct perf_event_attr *attr,
				  struct parse_events_term *term,
				  struct parse_events_error *err)
{
	if (term->type_term == PARSE_EVENTS__TERM_TYPE_USER) {
		enum hwmon_type type;
		int number;

		if (parse_hwmon_filename(term->config, &type, &number,
					 /*item=*/NULL, /*is_alarm=*/NULL)) {
			if (number == -1) {
				/*
				 * Item is of form <type>_ which means we should
				 * match <type>_<label>.
				 */
				struct hashmap_entry *cur;
				size_t bkt;

				attr->config = 0;
				hashmap__for_each_entry((&hwm->events), cur, bkt) {
					union hwmon_pmu_event_key key = {
						.type_and_num = cur->key,
					};
					struct hwmon_pmu_event_value *value = cur->pvalue;

					if (key.type == type && value->name &&
					    !strcasecmp(term->config, value->name)) {
						attr->config = key.type_and_num;
						break;
					}
				}
				if (attr->config == 0)
					return -EINVAL;
			} else {
				union hwmon_pmu_event_key key = {
					.type_and_num = 0,
				};

				key.type = type;
				key.num = number;
				attr->config = key.type_and_num;
			}
			return 0;
		}
	}
	if (err) {
		char *err_str;

		parse_events_error__handle(err, term->err_val,
					asprintf(&err_str,
						"unexpected hwmon event term (%s) %s",
						parse_events__term_type_str(term->type_term),
						term->config) < 0
					? strdup("unexpected hwmon event term")
					: err_str,
					NULL);
	}
	return -EINVAL;
}

int hwmon_pmu__config_terms(const struct perf_pmu *pmu,
			    struct perf_event_attr *attr,
			    struct parse_events_terms *terms,
			    struct parse_events_error *err)
{
	struct hwmon_pmu *hwm = container_of(pmu, struct hwmon_pmu, pmu);
	struct parse_events_term *term;
	int ret;

	ret = hwmon_pmu__read_events(hwm);
	if (ret)
		return ret;

	list_for_each_entry(term, &terms->terms, list) {
		if (hwmon_pmu__config_term(hwm, attr, term, err))
			return -EINVAL;
	}

	return 0;

}

int hwmon_pmu__check_alias(struct parse_events_terms *terms, struct perf_pmu_info *info,
			   struct parse_events_error *err)
{
	struct parse_events_term *term =
		list_first_entry(&terms->terms, struct parse_events_term, list);

	if (term->type_term == PARSE_EVENTS__TERM_TYPE_USER) {
		enum hwmon_type type;
		int number;

		if (parse_hwmon_filename(term->config, &type, &number,
					 /*item=*/NULL, /*is_alarm=*/NULL)) {
			info->unit = hwmon_units[type];
			if (type == HWMON_TYPE_FAN || type == HWMON_TYPE_PWM ||
			    type == HWMON_TYPE_INTRUSION)
				info->scale = 1;
			else
				info->scale = 0.001;
		}
		return 0;
	}
	if (err) {
		char *err_str;

		parse_events_error__handle(err, term->err_val,
					asprintf(&err_str,
						"unexpected hwmon event term (%s) %s",
						parse_events__term_type_str(term->type_term),
						term->config) < 0
					? strdup("unexpected hwmon event term")
					: err_str,
					NULL);
	}
	return -EINVAL;
}

int perf_pmus__read_hwmon_pmus(struct list_head *pmus)
{
	char *line = NULL;
	struct io_dirent64 *class_hwmon_ent;
	struct io_dir class_hwmon_dir;
	char buf[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return 0;

	scnprintf(buf, sizeof(buf), "%s/class/hwmon/", sysfs);
	io_dir__init(&class_hwmon_dir, open(buf, O_CLOEXEC | O_DIRECTORY | O_RDONLY));

	if (class_hwmon_dir.dirfd < 0)
		return 0;

	while ((class_hwmon_ent = io_dir__readdir(&class_hwmon_dir)) != NULL) {
		size_t line_len;
		int hwmon_dir, name_fd;
		struct io io;

		if (class_hwmon_ent->d_type != DT_LNK)
			continue;

		scnprintf(buf, sizeof(buf), "%s/class/hwmon/%s", sysfs, class_hwmon_ent->d_name);
		hwmon_dir = open(buf, O_DIRECTORY);
		if (hwmon_dir == -1) {
			pr_debug("hwmon_pmu: not a directory: '%s/class/hwmon/%s'\n",
				 sysfs, class_hwmon_ent->d_name);
			continue;
		}
		name_fd = openat(hwmon_dir, "name", O_RDONLY);
		if (name_fd == -1) {
			pr_debug("hwmon_pmu: failure to open '%s/class/hwmon/%s/name'\n",
				  sysfs, class_hwmon_ent->d_name);
			close(hwmon_dir);
			continue;
		}
		io__init(&io, name_fd, buf, sizeof(buf));
		io__getline(&io, &line, &line_len);
		if (line_len > 0 && line[line_len - 1] == '\n')
			line[line_len - 1] = '\0';
		hwmon_pmu__new(pmus, hwmon_dir, class_hwmon_ent->d_name, line);
		close(name_fd);
	}
	free(line);
	close(class_hwmon_dir.dirfd);
	return 0;
}

#define FD(e, x, y) (*(int *)xyarray__entry(e->core.fd, x, y))

int evsel__hwmon_pmu_open(struct evsel *evsel,
			  struct perf_thread_map *threads,
			  int start_cpu_map_idx, int end_cpu_map_idx)
{
	struct hwmon_pmu *hwm = container_of(evsel->pmu, struct hwmon_pmu, pmu);
	union hwmon_pmu_event_key key = {
		.type_and_num = evsel->core.attr.config,
	};
	int idx = 0, thread = 0, nthreads, err = 0;

	nthreads = perf_thread_map__nr(threads);
	for (idx = start_cpu_map_idx; idx < end_cpu_map_idx; idx++) {
		for (thread = 0; thread < nthreads; thread++) {
			char buf[64];
			int fd;

			snprintf(buf, sizeof(buf), "%s%d_input",
				 hwmon_type_strs[key.type], key.num);

			fd = openat(hwm->hwmon_dir_fd, buf, O_RDONLY);
			FD(evsel, idx, thread) = fd;
			if (fd < 0) {
				err = -errno;
				goto out_close;
			}
		}
	}
	return 0;
out_close:
	if (err)
		threads->err_thread = thread;

	do {
		while (--thread >= 0) {
			if (FD(evsel, idx, thread) >= 0)
				close(FD(evsel, idx, thread));
			FD(evsel, idx, thread) = -1;
		}
		thread = nthreads;
	} while (--idx >= 0);
	return err;
}

int evsel__hwmon_pmu_read(struct evsel *evsel, int cpu_map_idx, int thread)
{
	char buf[32];
	int fd;
	ssize_t len;
	struct perf_counts_values *count, *old_count = NULL;

	if (evsel->prev_raw_counts)
		old_count = perf_counts(evsel->prev_raw_counts, cpu_map_idx, thread);

	count = perf_counts(evsel->counts, cpu_map_idx, thread);
	fd = FD(evsel, cpu_map_idx, thread);
	len = pread(fd, buf, sizeof(buf), 0);
	if (len <= 0) {
		count->lost++;
		return -EINVAL;
	}
	buf[len] = '\0';
	if (old_count) {
		count->val = old_count->val + strtoll(buf, NULL, 10);
		count->run = old_count->run + 1;
		count->ena = old_count->ena + 1;
	} else {
		count->val = strtoll(buf, NULL, 10);
		count->run++;
		count->ena++;
	}
	return 0;
}

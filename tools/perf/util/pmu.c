#include <linux/list.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <api/fs/fs.h>
#include <locale.h>
#include "util.h"
#include "pmu.h"
#include "parse-events.h"
#include "cpumap.h"

#define UNIT_MAX_LEN	31 /* max length for event unit name */

struct perf_pmu_alias {
	char *name;
	struct list_head terms;
	struct list_head list;
	char unit[UNIT_MAX_LEN+1];
	double scale;
};

struct perf_pmu_format {
	char *name;
	int value;
	DECLARE_BITMAP(bits, PERF_PMU_FORMAT_BITS);
	struct list_head list;
};

#define EVENT_SOURCE_DEVICE_PATH "/bus/event_source/devices/"

int perf_pmu_parse(struct list_head *list, char *name);
extern FILE *perf_pmu_in;

static LIST_HEAD(pmus);

/*
 * Parse & process all the sysfs attributes located under
 * the directory specified in 'dir' parameter.
 */
int perf_pmu__format_parse(char *dir, struct list_head *head)
{
	struct dirent *evt_ent;
	DIR *format_dir;
	int ret = 0;

	format_dir = opendir(dir);
	if (!format_dir)
		return -EINVAL;

	while (!ret && (evt_ent = readdir(format_dir))) {
		char path[PATH_MAX];
		char *name = evt_ent->d_name;
		FILE *file;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		snprintf(path, PATH_MAX, "%s/%s", dir, name);

		ret = -EINVAL;
		file = fopen(path, "r");
		if (!file)
			break;

		perf_pmu_in = file;
		ret = perf_pmu_parse(head, name);
		fclose(file);
	}

	closedir(format_dir);
	return ret;
}

/*
 * Reading/parsing the default pmu format definition, which should be
 * located at:
 * /sys/bus/event_source/devices/<dev>/format as sysfs group attributes.
 */
static int pmu_format(const char *name, struct list_head *format)
{
	struct stat st;
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH "%s/format", sysfs, name);

	if (stat(path, &st) < 0)
		return 0;	/* no error if format does not exist */

	if (perf_pmu__format_parse(path, format))
		return -1;

	return 0;
}

static int perf_pmu__parse_scale(struct perf_pmu_alias *alias, char *dir, char *name)
{
	struct stat st;
	ssize_t sret;
	char scale[128];
	int fd, ret = -1;
	char path[PATH_MAX];
	const char *lc;

	snprintf(path, PATH_MAX, "%s/%s.scale", dir, name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	if (fstat(fd, &st) < 0)
		goto error;

	sret = read(fd, scale, sizeof(scale)-1);
	if (sret < 0)
		goto error;

	scale[sret] = '\0';
	/*
	 * save current locale
	 */
	lc = setlocale(LC_NUMERIC, NULL);

	/*
	 * force to C locale to ensure kernel
	 * scale string is converted correctly.
	 * kernel uses default C locale.
	 */
	setlocale(LC_NUMERIC, "C");

	alias->scale = strtod(scale, NULL);

	/* restore locale */
	setlocale(LC_NUMERIC, lc);

	ret = 0;
error:
	close(fd);
	return ret;
}

static int perf_pmu__parse_unit(struct perf_pmu_alias *alias, char *dir, char *name)
{
	char path[PATH_MAX];
	ssize_t sret;
	int fd;

	snprintf(path, PATH_MAX, "%s/%s.unit", dir, name);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

		sret = read(fd, alias->unit, UNIT_MAX_LEN);
	if (sret < 0)
		goto error;

	close(fd);

	alias->unit[sret] = '\0';

	return 0;
error:
	close(fd);
	alias->unit[0] = '\0';
	return -1;
}

static int perf_pmu__new_alias(struct list_head *list, char *dir, char *name, FILE *file)
{
	struct perf_pmu_alias *alias;
	char buf[256];
	int ret;

	ret = fread(buf, 1, sizeof(buf), file);
	if (ret == 0)
		return -EINVAL;
	buf[ret] = 0;

	alias = malloc(sizeof(*alias));
	if (!alias)
		return -ENOMEM;

	INIT_LIST_HEAD(&alias->terms);
	alias->scale = 1.0;
	alias->unit[0] = '\0';

	ret = parse_events_terms(&alias->terms, buf);
	if (ret) {
		free(alias);
		return ret;
	}

	alias->name = strdup(name);
	/*
	 * load unit name and scale if available
	 */
	perf_pmu__parse_unit(alias, dir, name);
	perf_pmu__parse_scale(alias, dir, name);

	list_add_tail(&alias->list, list);

	return 0;
}

/*
 * Process all the sysfs attributes located under the directory
 * specified in 'dir' parameter.
 */
static int pmu_aliases_parse(char *dir, struct list_head *head)
{
	struct dirent *evt_ent;
	DIR *event_dir;
	size_t len;
	int ret = 0;

	event_dir = opendir(dir);
	if (!event_dir)
		return -EINVAL;

	while (!ret && (evt_ent = readdir(event_dir))) {
		char path[PATH_MAX];
		char *name = evt_ent->d_name;
		FILE *file;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;

		/*
		 * skip .unit and .scale info files
		 * parsed in perf_pmu__new_alias()
		 */
		len = strlen(name);
		if (len > 5 && !strcmp(name + len - 5, ".unit"))
			continue;
		if (len > 6 && !strcmp(name + len - 6, ".scale"))
			continue;

		snprintf(path, PATH_MAX, "%s/%s", dir, name);

		ret = -EINVAL;
		file = fopen(path, "r");
		if (!file)
			break;

		ret = perf_pmu__new_alias(head, dir, name, file);
		fclose(file);
	}

	closedir(event_dir);
	return ret;
}

/*
 * Reading the pmu event aliases definition, which should be located at:
 * /sys/bus/event_source/devices/<dev>/events as sysfs group attributes.
 */
static int pmu_aliases(const char *name, struct list_head *head)
{
	struct stat st;
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s/bus/event_source/devices/%s/events", sysfs, name);

	if (stat(path, &st) < 0)
		return 0;	 /* no error if 'events' does not exist */

	if (pmu_aliases_parse(path, head))
		return -1;

	return 0;
}

static int pmu_alias_terms(struct perf_pmu_alias *alias,
			   struct list_head *terms)
{
	struct parse_events_term *term, *cloned;
	LIST_HEAD(list);
	int ret;

	list_for_each_entry(term, &alias->terms, list) {
		ret = parse_events_term__clone(&cloned, term);
		if (ret) {
			parse_events__free_terms(&list);
			return ret;
		}
		list_add_tail(&cloned->list, &list);
	}
	list_splice(&list, terms);
	return 0;
}

/*
 * Reading/parsing the default pmu type value, which should be
 * located at:
 * /sys/bus/event_source/devices/<dev>/type as sysfs attribute.
 */
static int pmu_type(const char *name, __u32 *type)
{
	struct stat st;
	char path[PATH_MAX];
	FILE *file;
	int ret = 0;
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH "%s/type", sysfs, name);

	if (stat(path, &st) < 0)
		return -1;

	file = fopen(path, "r");
	if (!file)
		return -EINVAL;

	if (1 != fscanf(file, "%u", type))
		ret = -1;

	fclose(file);
	return ret;
}

/* Add all pmus in sysfs to pmu list: */
static void pmu_read_sysfs(void)
{
	char path[PATH_MAX];
	DIR *dir;
	struct dirent *dent;
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return;

	snprintf(path, PATH_MAX,
		 "%s" EVENT_SOURCE_DEVICE_PATH, sysfs);

	dir = opendir(path);
	if (!dir)
		return;

	while ((dent = readdir(dir))) {
		if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
			continue;
		/* add to static LIST_HEAD(pmus): */
		perf_pmu__find(dent->d_name);
	}

	closedir(dir);
}

static struct cpu_map *pmu_cpumask(const char *name)
{
	struct stat st;
	char path[PATH_MAX];
	FILE *file;
	struct cpu_map *cpus;
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return NULL;

	snprintf(path, PATH_MAX,
		 "%s/bus/event_source/devices/%s/cpumask", sysfs, name);

	if (stat(path, &st) < 0)
		return NULL;

	file = fopen(path, "r");
	if (!file)
		return NULL;

	cpus = cpu_map__read(file);
	fclose(file);
	return cpus;
}

static struct perf_pmu *pmu_lookup(const char *name)
{
	struct perf_pmu *pmu;
	LIST_HEAD(format);
	LIST_HEAD(aliases);
	__u32 type;

	/*
	 * The pmu data we store & need consists of the pmu
	 * type value and format definitions. Load both right
	 * now.
	 */
	if (pmu_format(name, &format))
		return NULL;

	if (pmu_aliases(name, &aliases))
		return NULL;

	if (pmu_type(name, &type))
		return NULL;

	pmu = zalloc(sizeof(*pmu));
	if (!pmu)
		return NULL;

	pmu->cpus = pmu_cpumask(name);

	INIT_LIST_HEAD(&pmu->format);
	INIT_LIST_HEAD(&pmu->aliases);
	list_splice(&format, &pmu->format);
	list_splice(&aliases, &pmu->aliases);
	pmu->name = strdup(name);
	pmu->type = type;
	list_add_tail(&pmu->list, &pmus);
	return pmu;
}

static struct perf_pmu *pmu_find(const char *name)
{
	struct perf_pmu *pmu;

	list_for_each_entry(pmu, &pmus, list)
		if (!strcmp(pmu->name, name))
			return pmu;

	return NULL;
}

struct perf_pmu *perf_pmu__scan(struct perf_pmu *pmu)
{
	/*
	 * pmu iterator: If pmu is NULL, we start at the begin,
	 * otherwise return the next pmu. Returns NULL on end.
	 */
	if (!pmu) {
		pmu_read_sysfs();
		pmu = list_prepare_entry(pmu, &pmus, list);
	}
	list_for_each_entry_continue(pmu, &pmus, list)
		return pmu;
	return NULL;
}

struct perf_pmu *perf_pmu__find(const char *name)
{
	struct perf_pmu *pmu;

	/*
	 * Once PMU is loaded it stays in the list,
	 * so we keep us from multiple reading/parsing
	 * the pmu format definitions.
	 */
	pmu = pmu_find(name);
	if (pmu)
		return pmu;

	return pmu_lookup(name);
}

static struct perf_pmu_format *
pmu_find_format(struct list_head *formats, char *name)
{
	struct perf_pmu_format *format;

	list_for_each_entry(format, formats, list)
		if (!strcmp(format->name, name))
			return format;

	return NULL;
}

/*
 * Returns value based on the format definition (format parameter)
 * and unformated value (value parameter).
 *
 * TODO maybe optimize a little ;)
 */
static __u64 pmu_format_value(unsigned long *format, __u64 value)
{
	unsigned long fbit, vbit;
	__u64 v = 0;

	for (fbit = 0, vbit = 0; fbit < PERF_PMU_FORMAT_BITS; fbit++) {

		if (!test_bit(fbit, format))
			continue;

		if (!(value & (1llu << vbit++)))
			continue;

		v |= (1llu << fbit);
	}

	return v;
}

/*
 * Setup one of config[12] attr members based on the
 * user input data - term parameter.
 */
static int pmu_config_term(struct list_head *formats,
			   struct perf_event_attr *attr,
			   struct parse_events_term *term)
{
	struct perf_pmu_format *format;
	__u64 *vp;

	/*
	 * Support only for hardcoded and numnerial terms.
	 * Hardcoded terms should be already in, so nothing
	 * to be done for them.
	 */
	if (parse_events__is_hardcoded_term(term))
		return 0;

	if (term->type_val != PARSE_EVENTS__TERM_TYPE_NUM)
		return -EINVAL;

	format = pmu_find_format(formats, term->config);
	if (!format)
		return -EINVAL;

	switch (format->value) {
	case PERF_PMU_FORMAT_VALUE_CONFIG:
		vp = &attr->config;
		break;
	case PERF_PMU_FORMAT_VALUE_CONFIG1:
		vp = &attr->config1;
		break;
	case PERF_PMU_FORMAT_VALUE_CONFIG2:
		vp = &attr->config2;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * XXX If we ever decide to go with string values for
	 * non-hardcoded terms, here's the place to translate
	 * them into value.
	 */
	*vp |= pmu_format_value(format->bits, term->val.num);
	return 0;
}

int perf_pmu__config_terms(struct list_head *formats,
			   struct perf_event_attr *attr,
			   struct list_head *head_terms)
{
	struct parse_events_term *term;

	list_for_each_entry(term, head_terms, list)
		if (pmu_config_term(formats, attr, term))
			return -EINVAL;

	return 0;
}

/*
 * Configures event's 'attr' parameter based on the:
 * 1) users input - specified in terms parameter
 * 2) pmu format definitions - specified by pmu parameter
 */
int perf_pmu__config(struct perf_pmu *pmu, struct perf_event_attr *attr,
		     struct list_head *head_terms)
{
	attr->type = pmu->type;
	return perf_pmu__config_terms(&pmu->format, attr, head_terms);
}

static struct perf_pmu_alias *pmu_find_alias(struct perf_pmu *pmu,
					     struct parse_events_term *term)
{
	struct perf_pmu_alias *alias;
	char *name;

	if (parse_events__is_hardcoded_term(term))
		return NULL;

	if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM) {
		if (term->val.num != 1)
			return NULL;
		if (pmu_find_format(&pmu->format, term->config))
			return NULL;
		name = term->config;
	} else if (term->type_val == PARSE_EVENTS__TERM_TYPE_STR) {
		if (strcasecmp(term->config, "event"))
			return NULL;
		name = term->val.str;
	} else {
		return NULL;
	}

	list_for_each_entry(alias, &pmu->aliases, list) {
		if (!strcasecmp(alias->name, name))
			return alias;
	}
	return NULL;
}


static int check_unit_scale(struct perf_pmu_alias *alias,
			    const char **unit, double *scale)
{
	/*
	 * Only one term in event definition can
	 * define unit and scale, fail if there's
	 * more than one.
	 */
	if ((*unit && alias->unit) ||
	    (*scale && alias->scale))
		return -EINVAL;

	if (alias->unit)
		*unit = alias->unit;

	if (alias->scale)
		*scale = alias->scale;

	return 0;
}

/*
 * Find alias in the terms list and replace it with the terms
 * defined for the alias
 */
int perf_pmu__check_alias(struct perf_pmu *pmu, struct list_head *head_terms,
			  const char **unit, double *scale)
{
	struct parse_events_term *term, *h;
	struct perf_pmu_alias *alias;
	int ret;

	/*
	 * Mark unit and scale as not set
	 * (different from default values, see below)
	 */
	*unit   = NULL;
	*scale  = 0.0;

	list_for_each_entry_safe(term, h, head_terms, list) {
		alias = pmu_find_alias(pmu, term);
		if (!alias)
			continue;
		ret = pmu_alias_terms(alias, &term->list);
		if (ret)
			return ret;

		ret = check_unit_scale(alias, unit, scale);
		if (ret)
			return ret;

		list_del(&term->list);
		free(term);
	}

	/*
	 * if no unit or scale foundin aliases, then
	 * set defaults as for evsel
	 * unit cannot left to NULL
	 */
	if (*unit == NULL)
		*unit   = "";

	if (*scale == 0.0)
		*scale  = 1.0;

	return 0;
}

int perf_pmu__new_format(struct list_head *list, char *name,
			 int config, unsigned long *bits)
{
	struct perf_pmu_format *format;

	format = zalloc(sizeof(*format));
	if (!format)
		return -ENOMEM;

	format->name = strdup(name);
	format->value = config;
	memcpy(format->bits, bits, sizeof(format->bits));

	list_add_tail(&format->list, list);
	return 0;
}

void perf_pmu__set_format(unsigned long *bits, long from, long to)
{
	long b;

	if (!to)
		to = from;

	memset(bits, 0, BITS_TO_BYTES(PERF_PMU_FORMAT_BITS));
	for (b = from; b <= to; b++)
		set_bit(b, bits);
}

static char *format_alias(char *buf, int len, struct perf_pmu *pmu,
			  struct perf_pmu_alias *alias)
{
	snprintf(buf, len, "%s/%s/", pmu->name, alias->name);
	return buf;
}

static char *format_alias_or(char *buf, int len, struct perf_pmu *pmu,
			     struct perf_pmu_alias *alias)
{
	snprintf(buf, len, "%s OR %s/%s/", alias->name, pmu->name, alias->name);
	return buf;
}

static int cmp_string(const void *a, const void *b)
{
	const char * const *as = a;
	const char * const *bs = b;
	return strcmp(*as, *bs);
}

void print_pmu_events(const char *event_glob, bool name_only)
{
	struct perf_pmu *pmu;
	struct perf_pmu_alias *alias;
	char buf[1024];
	int printed = 0;
	int len, j;
	char **aliases;

	pmu = NULL;
	len = 0;
	while ((pmu = perf_pmu__scan(pmu)) != NULL)
		list_for_each_entry(alias, &pmu->aliases, list)
			len++;
	aliases = malloc(sizeof(char *) * len);
	if (!aliases)
		return;
	pmu = NULL;
	j = 0;
	while ((pmu = perf_pmu__scan(pmu)) != NULL)
		list_for_each_entry(alias, &pmu->aliases, list) {
			char *name = format_alias(buf, sizeof(buf), pmu, alias);
			bool is_cpu = !strcmp(pmu->name, "cpu");

			if (event_glob != NULL &&
			    !(strglobmatch(name, event_glob) ||
			      (!is_cpu && strglobmatch(alias->name,
						       event_glob))))
				continue;
			aliases[j] = name;
			if (is_cpu && !name_only)
				aliases[j] = format_alias_or(buf, sizeof(buf),
							      pmu, alias);
			aliases[j] = strdup(aliases[j]);
			j++;
		}
	len = j;
	qsort(aliases, len, sizeof(char *), cmp_string);
	for (j = 0; j < len; j++) {
		if (name_only) {
			printf("%s ", aliases[j]);
			continue;
		}
		printf("  %-50s [Kernel PMU event]\n", aliases[j]);
		zfree(&aliases[j]);
		printed++;
	}
	if (printed)
		printf("\n");
	free(aliases);
}

bool pmu_have_event(const char *pname, const char *name)
{
	struct perf_pmu *pmu;
	struct perf_pmu_alias *alias;

	pmu = NULL;
	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		if (strcmp(pname, pmu->name))
			continue;
		list_for_each_entry(alias, &pmu->aliases, list)
			if (!strcmp(alias->name, name))
				return true;
	}
	return false;
}

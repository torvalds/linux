
#include <linux/list.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include "sysfs.h"
#include "util.h"
#include "pmu.h"
#include "parse-events.h"

int perf_pmu_parse(struct list_head *list, char *name);
extern FILE *perf_pmu_in;

static LIST_HEAD(pmus);

/*
 * Parse & process all the sysfs attributes located under
 * the directory specified in 'dir' parameter.
 */
static int pmu_format_parse(char *dir, struct list_head *head)
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
static int pmu_format(char *name, struct list_head *format)
{
	struct stat st;
	char path[PATH_MAX];
	const char *sysfs;

	sysfs = sysfs_find_mountpoint();
	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s/bus/event_source/devices/%s/format", sysfs, name);

	if (stat(path, &st) < 0)
		return -1;

	if (pmu_format_parse(path, format))
		return -1;

	return 0;
}

/*
 * Reading/parsing the default pmu type value, which should be
 * located at:
 * /sys/bus/event_source/devices/<dev>/type as sysfs attribute.
 */
static int pmu_type(char *name, __u32 *type)
{
	struct stat st;
	char path[PATH_MAX];
	const char *sysfs;
	FILE *file;
	int ret = 0;

	sysfs = sysfs_find_mountpoint();
	if (!sysfs)
		return -1;

	snprintf(path, PATH_MAX,
		 "%s/bus/event_source/devices/%s/type", sysfs, name);

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

static struct perf_pmu *pmu_lookup(char *name)
{
	struct perf_pmu *pmu;
	LIST_HEAD(format);
	__u32 type;

	/*
	 * The pmu data we store & need consists of the pmu
	 * type value and format definitions. Load both right
	 * now.
	 */
	if (pmu_format(name, &format))
		return NULL;

	if (pmu_type(name, &type))
		return NULL;

	pmu = zalloc(sizeof(*pmu));
	if (!pmu)
		return NULL;

	INIT_LIST_HEAD(&pmu->format);
	list_splice(&format, &pmu->format);
	pmu->name = strdup(name);
	pmu->type = type;
	return pmu;
}

static struct perf_pmu *pmu_find(char *name)
{
	struct perf_pmu *pmu;

	list_for_each_entry(pmu, &pmus, list)
		if (!strcmp(pmu->name, name))
			return pmu;

	return NULL;
}

struct perf_pmu *perf_pmu__find(char *name)
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

static struct perf_pmu__format*
pmu_find_format(struct list_head *formats, char *name)
{
	struct perf_pmu__format *format;

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
 * user input data - temr parameter.
 */
static int pmu_config_term(struct list_head *formats,
			   struct perf_event_attr *attr,
			   struct parse_events__term *term)
{
	struct perf_pmu__format *format;
	__u64 *vp;

	/*
	 * Support only for hardcoded and numnerial terms.
	 * Hardcoded terms should be already in, so nothing
	 * to be done for them.
	 */
	if (parse_events__is_hardcoded_term(term))
		return 0;

	if (term->type != PARSE_EVENTS__TERM_TYPE_NUM)
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

	*vp |= pmu_format_value(format->bits, term->val.num);
	return 0;
}

static int pmu_config(struct list_head *formats, struct perf_event_attr *attr,
		      struct list_head *head_terms)
{
	struct parse_events__term *term, *h;

	list_for_each_entry_safe(term, h, head_terms, list)
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
	return pmu_config(&pmu->format, attr, head_terms);
}

int perf_pmu__new_format(struct list_head *list, char *name,
			 int config, unsigned long *bits)
{
	struct perf_pmu__format *format;

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

	memset(bits, 0, BITS_TO_LONGS(PERF_PMU_FORMAT_BITS));
	for (b = from; b <= to; b++)
		set_bit(b, bits);
}

/* Simulated format definitions. */
static struct test_format {
	const char *name;
	const char *value;
} test_formats[] = {
	{ "krava01", "config:0-1,62-63\n", },
	{ "krava02", "config:10-17\n", },
	{ "krava03", "config:5\n", },
	{ "krava11", "config1:0,2,4,6,8,20-28\n", },
	{ "krava12", "config1:63\n", },
	{ "krava13", "config1:45-47\n", },
	{ "krava21", "config2:0-3,10-13,20-23,30-33,40-43,50-53,60-63\n", },
	{ "krava22", "config2:8,18,48,58\n", },
	{ "krava23", "config2:28-29,38\n", },
};

#define TEST_FORMATS_CNT (sizeof(test_formats) / sizeof(struct test_format))

/* Simulated users input. */
static struct parse_events__term test_terms[] = {
	{
		.config  = (char *) "krava01",
		.val.num = 15,
		.type    = PARSE_EVENTS__TERM_TYPE_NUM,
	},
	{
		.config  = (char *) "krava02",
		.val.num = 170,
		.type    = PARSE_EVENTS__TERM_TYPE_NUM,
	},
	{
		.config  = (char *) "krava03",
		.val.num = 1,
		.type    = PARSE_EVENTS__TERM_TYPE_NUM,
	},
	{
		.config  = (char *) "krava11",
		.val.num = 27,
		.type    = PARSE_EVENTS__TERM_TYPE_NUM,
	},
	{
		.config  = (char *) "krava12",
		.val.num = 1,
		.type    = PARSE_EVENTS__TERM_TYPE_NUM,
	},
	{
		.config  = (char *) "krava13",
		.val.num = 2,
		.type    = PARSE_EVENTS__TERM_TYPE_NUM,
	},
	{
		.config  = (char *) "krava21",
		.val.num = 119,
		.type    = PARSE_EVENTS__TERM_TYPE_NUM,
	},
	{
		.config  = (char *) "krava22",
		.val.num = 11,
		.type    = PARSE_EVENTS__TERM_TYPE_NUM,
	},
	{
		.config  = (char *) "krava23",
		.val.num = 2,
		.type    = PARSE_EVENTS__TERM_TYPE_NUM,
	},
};
#define TERMS_CNT (sizeof(test_terms) / sizeof(struct parse_events__term))

/*
 * Prepare format directory data, exported by kernel
 * at /sys/bus/event_source/devices/<dev>/format.
 */
static char *test_format_dir_get(void)
{
	static char dir[PATH_MAX];
	unsigned int i;

	snprintf(dir, PATH_MAX, "/tmp/perf-pmu-test-format-XXXXXX");
	if (!mkdtemp(dir))
		return NULL;

	for (i = 0; i < TEST_FORMATS_CNT; i++) {
		static char name[PATH_MAX];
		struct test_format *format = &test_formats[i];
		FILE *file;

		snprintf(name, PATH_MAX, "%s/%s", dir, format->name);

		file = fopen(name, "w");
		if (!file)
			return NULL;

		if (1 != fwrite(format->value, strlen(format->value), 1, file))
			break;

		fclose(file);
	}

	return dir;
}

/* Cleanup format directory. */
static int test_format_dir_put(char *dir)
{
	char buf[PATH_MAX];
	snprintf(buf, PATH_MAX, "rm -f %s/*\n", dir);
	if (system(buf))
		return -1;

	snprintf(buf, PATH_MAX, "rmdir %s\n", dir);
	return system(buf);
}

static struct list_head *test_terms_list(void)
{
	static LIST_HEAD(terms);
	unsigned int i;

	for (i = 0; i < TERMS_CNT; i++)
		list_add_tail(&test_terms[i].list, &terms);

	return &terms;
}

#undef TERMS_CNT

int perf_pmu__test(void)
{
	char *format = test_format_dir_get();
	LIST_HEAD(formats);
	struct list_head *terms = test_terms_list();
	int ret;

	if (!format)
		return -EINVAL;

	do {
		struct perf_event_attr attr;

		memset(&attr, 0, sizeof(attr));

		ret = pmu_format_parse(format, &formats);
		if (ret)
			break;

		ret = pmu_config(&formats, &attr, terms);
		if (ret)
			break;

		ret = -EINVAL;

		if (attr.config  != 0xc00000000002a823)
			break;
		if (attr.config1 != 0x8000400000000145)
			break;
		if (attr.config2 != 0x0400000020041d07)
			break;

		ret = 0;
	} while (0);

	test_format_dir_put(format);
	return ret;
}

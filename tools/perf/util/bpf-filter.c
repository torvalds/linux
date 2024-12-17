/* SPDX-License-Identifier: GPL-2.0 */
/**
 * Generic event filter for sampling events in BPF.
 *
 * The BPF program is fixed and just to read filter expressions in the 'filters'
 * map and compare the sample data in order to reject samples that don't match.
 * Each filter expression contains a sample flag (term) to compare, an operation
 * (==, >=, and so on) and a value.
 *
 * Note that each entry has an array of filter expressions and it only succeeds
 * when all of the expressions are satisfied.  But it supports the logical OR
 * using a GROUP operation which is satisfied when any of its member expression
 * is evaluated to true.  But it doesn't allow nested GROUP operations for now.
 *
 * To support non-root users, the filters map can be loaded and pinned in the BPF
 * filesystem by root (perf record --setup-filter pin).  Then each user will get
 * a new entry in the shared filters map to fill the filter expressions.  And the
 * BPF program will find the filter using (task-id, event-id) as a key.
 *
 * The pinned BPF object (shared for regular users) has:
 *
 *                  event_hash                   |
 *                  |        |                   |
 *   event->id ---> |   id   | ---+   idx_hash   |     filters
 *                  |        |    |   |      |   |    |       |
 *                  |  ....  |    +-> |  idx | --+--> | exprs | --->  perf_bpf_filter_entry[]
 *                                |   |      |   |    |       |               .op
 *   task id (tgid) --------------+   | .... |   |    |  ...  |               .term (+ part)
 *                                               |                            .value
 *                                               |
 *   ======= (root would skip this part) ========                     (compares it in a loop)
 *
 * This is used for per-task use cases while system-wide profiling (normally from
 * root user) uses a separate copy of the program and the maps for its own so that
 * it can proceed even if a lot of non-root users are using the filters at the
 * same time.  In this case the filters map has a single entry and no need to use
 * the hash maps to get the index (key) of the filters map (IOW it's always 0).
 *
 * The BPF program returns 1 to accept the sample or 0 to drop it.
 * The 'dropped' map is to keep how many samples it dropped by the filter and
 * it will be reported as lost samples.
 */
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <bpf/bpf.h>
#include <linux/err.h>
#include <linux/list.h>
#include <api/fs/fs.h>
#include <internal/xyarray.h>
#include <perf/threadmap.h>

#include "util/debug.h"
#include "util/evsel.h"
#include "util/target.h"

#include "util/bpf-filter.h"
#include <util/bpf-filter-flex.h>
#include <util/bpf-filter-bison.h>

#include "bpf_skel/sample-filter.h"
#include "bpf_skel/sample_filter.skel.h"

#define FD(e, x, y) (*(int *)xyarray__entry(e->core.fd, x, y))

#define __PERF_SAMPLE_TYPE(tt, st, opt)	{ tt, #st, opt }
#define PERF_SAMPLE_TYPE(_st, opt)	__PERF_SAMPLE_TYPE(PBF_TERM_##_st, PERF_SAMPLE_##_st, opt)

/* Index in the pinned 'filters' map.  Should be released after use. */
struct pinned_filter_idx {
	struct list_head list;
	struct evsel *evsel;
	u64 event_id;
	int hash_idx;
};

static LIST_HEAD(pinned_filters);

static const struct perf_sample_info {
	enum perf_bpf_filter_term type;
	const char *name;
	const char *option;
} sample_table[] = {
	/* default sample flags */
	PERF_SAMPLE_TYPE(IP, NULL),
	PERF_SAMPLE_TYPE(TID, NULL),
	PERF_SAMPLE_TYPE(PERIOD, NULL),
	/* flags mostly set by default, but still have options */
	PERF_SAMPLE_TYPE(ID, "--sample-identifier"),
	PERF_SAMPLE_TYPE(CPU, "--sample-cpu"),
	PERF_SAMPLE_TYPE(TIME, "-T"),
	/* optional sample flags */
	PERF_SAMPLE_TYPE(ADDR, "-d"),
	PERF_SAMPLE_TYPE(DATA_SRC, "-d"),
	PERF_SAMPLE_TYPE(PHYS_ADDR, "--phys-data"),
	PERF_SAMPLE_TYPE(WEIGHT, "-W"),
	PERF_SAMPLE_TYPE(WEIGHT_STRUCT, "-W"),
	PERF_SAMPLE_TYPE(TRANSACTION, "--transaction"),
	PERF_SAMPLE_TYPE(CODE_PAGE_SIZE, "--code-page-size"),
	PERF_SAMPLE_TYPE(DATA_PAGE_SIZE, "--data-page-size"),
	PERF_SAMPLE_TYPE(CGROUP, "--all-cgroups"),
};

static int get_pinned_fd(const char *name);

static const struct perf_sample_info *get_sample_info(enum perf_bpf_filter_term type)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sample_table); i++) {
		if (sample_table[i].type == type)
			return &sample_table[i];
	}
	return NULL;
}

static int check_sample_flags(struct evsel *evsel, struct perf_bpf_filter_expr *expr)
{
	const struct perf_sample_info *info;

	if (expr->term >= PBF_TERM_SAMPLE_START && expr->term <= PBF_TERM_SAMPLE_END &&
	    (evsel->core.attr.sample_type & (1 << (expr->term - PBF_TERM_SAMPLE_START))))
		return 0;

	if (expr->term == PBF_TERM_UID || expr->term == PBF_TERM_GID) {
		/* Not dependent on the sample_type as computed from a BPF helper. */
		return 0;
	}

	if (expr->op == PBF_OP_GROUP_BEGIN) {
		struct perf_bpf_filter_expr *group;

		list_for_each_entry(group, &expr->groups, list) {
			if (check_sample_flags(evsel, group) < 0)
				return -1;
		}
		return 0;
	}

	info = get_sample_info(expr->term);
	if (info == NULL) {
		pr_err("Error: %s event does not have sample flags %d\n",
		       evsel__name(evsel), expr->term);
		return -1;
	}

	pr_err("Error: %s event does not have %s\n", evsel__name(evsel), info->name);
	if (info->option)
		pr_err(" Hint: please add %s option to perf record\n", info->option);
	return -1;
}

static int get_filter_entries(struct evsel *evsel, struct perf_bpf_filter_entry *entry)
{
	int i = 0;
	struct perf_bpf_filter_expr *expr;

	list_for_each_entry(expr, &evsel->bpf_filters, list) {
		if (check_sample_flags(evsel, expr) < 0)
			return -EINVAL;

		if (i == MAX_FILTERS)
			return -E2BIG;

		entry[i].op = expr->op;
		entry[i].part = expr->part;
		entry[i].term = expr->term;
		entry[i].value = expr->val;
		i++;

		if (expr->op == PBF_OP_GROUP_BEGIN) {
			struct perf_bpf_filter_expr *group;

			list_for_each_entry(group, &expr->groups, list) {
				if (i == MAX_FILTERS)
					return -E2BIG;

				entry[i].op = group->op;
				entry[i].part = group->part;
				entry[i].term = group->term;
				entry[i].value = group->val;
				i++;
			}

			if (i == MAX_FILTERS)
				return -E2BIG;

			entry[i].op = PBF_OP_GROUP_END;
			i++;
		}
	}

	if (i < MAX_FILTERS) {
		/* to terminate the loop early */
		entry[i].op = PBF_OP_DONE;
		i++;
	}
	return 0;
}

static int convert_to_tgid(int tid)
{
	char path[128];
	char *buf, *p, *q;
	int tgid;
	size_t len;

	scnprintf(path, sizeof(path), "%d/status", tid);
	if (procfs__read_str(path, &buf, &len) < 0)
		return -1;

	p = strstr(buf, "Tgid:");
	if (p == NULL) {
		free(buf);
		return -1;
	}

	tgid = strtol(p + 6, &q, 0);
	free(buf);
	if (*q != '\n')
		return -1;

	return tgid;
}

/*
 * The event might be closed already so we cannot get the list of ids using FD
 * like in create_event_hash() below, let's iterate the event_hash map and
 * delete all entries that have the event id as a key.
 */
static void destroy_event_hash(u64 event_id)
{
	int fd;
	u64 key, *prev_key = NULL;
	int num = 0, alloced = 32;
	u64 *ids = calloc(alloced, sizeof(*ids));

	if (ids == NULL)
		return;

	fd = get_pinned_fd("event_hash");
	if (fd < 0) {
		pr_debug("cannot get fd for 'event_hash' map\n");
		free(ids);
		return;
	}

	/* Iterate the whole map to collect keys for the event id. */
	while (!bpf_map_get_next_key(fd, prev_key, &key)) {
		u64 id;

		if (bpf_map_lookup_elem(fd, &key, &id) == 0 && id == event_id) {
			if (num == alloced) {
				void *tmp;

				alloced *= 2;
				tmp = realloc(ids, alloced * sizeof(*ids));
				if (tmp == NULL)
					break;

				ids = tmp;
			}
			ids[num++] = key;
		}

		prev_key = &key;
	}

	for (int i = 0; i < num; i++)
		bpf_map_delete_elem(fd, &ids[i]);

	free(ids);
	close(fd);
}

/*
 * Return a representative id if ok, or 0 for failures.
 *
 * The perf_event->id is good for this, but an evsel would have multiple
 * instances for CPUs and tasks.  So pick up the first id and setup a hash
 * from id of each instance to the representative id (the first one).
 */
static u64 create_event_hash(struct evsel *evsel)
{
	int x, y, fd;
	u64 the_id = 0, id;

	fd = get_pinned_fd("event_hash");
	if (fd < 0) {
		pr_err("cannot get fd for 'event_hash' map\n");
		return 0;
	}

	for (x = 0; x < xyarray__max_x(evsel->core.fd); x++) {
		for (y = 0; y < xyarray__max_y(evsel->core.fd); y++) {
			int ret = ioctl(FD(evsel, x, y), PERF_EVENT_IOC_ID, &id);

			if (ret < 0) {
				pr_err("Failed to get the event id\n");
				if (the_id)
					destroy_event_hash(the_id);
				return 0;
			}

			if (the_id == 0)
				the_id = id;

			bpf_map_update_elem(fd, &id, &the_id, BPF_ANY);
		}
	}

	close(fd);
	return the_id;
}

static void destroy_idx_hash(struct pinned_filter_idx *pfi)
{
	int fd, nr;
	struct perf_thread_map *threads;

	fd = get_pinned_fd("filters");
	bpf_map_delete_elem(fd, &pfi->hash_idx);
	close(fd);

	if (pfi->event_id)
		destroy_event_hash(pfi->event_id);

	threads = perf_evsel__threads(&pfi->evsel->core);
	if (threads == NULL)
		return;

	fd = get_pinned_fd("idx_hash");
	nr = perf_thread_map__nr(threads);
	for (int i = 0; i < nr; i++) {
		/* The target task might be dead already, just try the pid */
		struct idx_hash_key key = {
			.evt_id = pfi->event_id,
			.tgid = perf_thread_map__pid(threads, i),
		};

		bpf_map_delete_elem(fd, &key);
	}
	close(fd);
}

/* Maintain a hashmap from (tgid, event-id) to filter index */
static int create_idx_hash(struct evsel *evsel, struct perf_bpf_filter_entry *entry)
{
	int filter_idx;
	int fd, nr, last;
	u64 event_id = 0;
	struct pinned_filter_idx *pfi = NULL;
	struct perf_thread_map *threads;

	fd = get_pinned_fd("filters");
	if (fd < 0) {
		pr_err("cannot get fd for 'filters' map\n");
		return fd;
	}

	/* Find the first available entry in the filters map */
	for (filter_idx = 0; filter_idx < MAX_FILTERS; filter_idx++) {
		if (bpf_map_update_elem(fd, &filter_idx, entry, BPF_NOEXIST) == 0)
			break;
	}
	close(fd);

	if (filter_idx == MAX_FILTERS) {
		pr_err("Too many users for the filter map\n");
		return -EBUSY;
	}

	pfi = zalloc(sizeof(*pfi));
	if (pfi == NULL) {
		pr_err("Cannot save pinned filter index\n");
		return -ENOMEM;
	}

	pfi->evsel = evsel;
	pfi->hash_idx = filter_idx;

	event_id = create_event_hash(evsel);
	if (event_id == 0) {
		pr_err("Cannot update the event hash\n");
		goto err;
	}

	pfi->event_id = event_id;

	threads = perf_evsel__threads(&evsel->core);
	if (threads == NULL) {
		pr_err("Cannot get the thread list of the event\n");
		goto err;
	}

	/* save the index to a hash map */
	fd = get_pinned_fd("idx_hash");
	if (fd < 0) {
		pr_err("cannot get fd for 'idx_hash' map\n");
		goto err;
	}

	last = -1;
	nr = perf_thread_map__nr(threads);
	for (int i = 0; i < nr; i++) {
		int pid = perf_thread_map__pid(threads, i);
		int tgid;
		struct idx_hash_key key = {
			.evt_id = event_id,
		};

		/* it actually needs tgid, let's get tgid from /proc. */
		tgid = convert_to_tgid(pid);
		if (tgid < 0) {
			/* the thread may be dead, ignore. */
			continue;
		}

		if (tgid == last)
			continue;
		last = tgid;
		key.tgid = tgid;

		if (bpf_map_update_elem(fd, &key, &filter_idx, BPF_ANY) < 0) {
			pr_err("Failed to update the idx_hash\n");
			close(fd);
			goto err;
		}
		pr_debug("bpf-filter: idx_hash (task=%d,%s) -> %d\n",
			 tgid, evsel__name(evsel), filter_idx);
	}

	list_add(&pfi->list, &pinned_filters);
	close(fd);
	return filter_idx;

err:
	destroy_idx_hash(pfi);
	free(pfi);
	return -1;
}

int perf_bpf_filter__prepare(struct evsel *evsel, struct target *target)
{
	int i, x, y, fd, ret;
	struct sample_filter_bpf *skel = NULL;
	struct bpf_program *prog;
	struct bpf_link *link;
	struct perf_bpf_filter_entry *entry;
	bool needs_idx_hash = !target__has_cpu(target) && !target->uid_str;

	entry = calloc(MAX_FILTERS, sizeof(*entry));
	if (entry == NULL)
		return -1;

	ret = get_filter_entries(evsel, entry);
	if (ret < 0) {
		pr_err("Failed to process filter entries\n");
		goto err;
	}

	if (needs_idx_hash && geteuid() != 0) {
		int zero = 0;

		/* The filters map is shared among other processes */
		ret = create_idx_hash(evsel, entry);
		if (ret < 0)
			goto err;

		fd = get_pinned_fd("dropped");
		if (fd < 0) {
			ret = fd;
			goto err;
		}

		/* Reset the lost count */
		bpf_map_update_elem(fd, &ret, &zero, BPF_ANY);
		close(fd);

		fd = get_pinned_fd("perf_sample_filter");
		if (fd < 0) {
			ret = fd;
			goto err;
		}

		for (x = 0; x < xyarray__max_x(evsel->core.fd); x++) {
			for (y = 0; y < xyarray__max_y(evsel->core.fd); y++) {
				ret = ioctl(FD(evsel, x, y), PERF_EVENT_IOC_SET_BPF, fd);
				if (ret < 0) {
					pr_err("Failed to attach perf sample-filter\n");
					close(fd);
					goto err;
				}
			}
		}

		close(fd);
		free(entry);
		return 0;
	}

	skel = sample_filter_bpf__open_and_load();
	if (!skel) {
		ret = -errno;
		pr_err("Failed to load perf sample-filter BPF skeleton\n");
		goto err;
	}

	i = 0;
	fd = bpf_map__fd(skel->maps.filters);

	/* The filters map has only one entry in this case */
	if (bpf_map_update_elem(fd, &i, entry, BPF_ANY) < 0) {
		ret = -errno;
		pr_err("Failed to update the filter map\n");
		goto err;
	}

	prog = skel->progs.perf_sample_filter;
	for (x = 0; x < xyarray__max_x(evsel->core.fd); x++) {
		for (y = 0; y < xyarray__max_y(evsel->core.fd); y++) {
			link = bpf_program__attach_perf_event(prog, FD(evsel, x, y));
			if (IS_ERR(link)) {
				pr_err("Failed to attach perf sample-filter program\n");
				ret = PTR_ERR(link);
				goto err;
			}
		}
	}
	free(entry);
	evsel->bpf_skel = skel;
	return 0;

err:
	free(entry);
	if (!list_empty(&pinned_filters)) {
		struct pinned_filter_idx *pfi, *tmp;

		list_for_each_entry_safe(pfi, tmp, &pinned_filters, list) {
			destroy_idx_hash(pfi);
			list_del(&pfi->list);
			free(pfi);
		}
	}
	sample_filter_bpf__destroy(skel);
	return ret;
}

int perf_bpf_filter__destroy(struct evsel *evsel)
{
	struct perf_bpf_filter_expr *expr, *tmp;
	struct pinned_filter_idx *pfi, *pos;

	list_for_each_entry_safe(expr, tmp, &evsel->bpf_filters, list) {
		list_del(&expr->list);
		free(expr);
	}
	sample_filter_bpf__destroy(evsel->bpf_skel);

	list_for_each_entry_safe(pfi, pos, &pinned_filters, list) {
		destroy_idx_hash(pfi);
		list_del(&pfi->list);
		free(pfi);
	}
	return 0;
}

u64 perf_bpf_filter__lost_count(struct evsel *evsel)
{
	int count = 0;

	if (list_empty(&evsel->bpf_filters))
		return 0;

	if (!list_empty(&pinned_filters)) {
		int fd = get_pinned_fd("dropped");
		struct pinned_filter_idx *pfi;

		if (fd < 0)
			return 0;

		list_for_each_entry(pfi, &pinned_filters, list) {
			if (pfi->evsel != evsel)
				continue;

			bpf_map_lookup_elem(fd, &pfi->hash_idx, &count);
			break;
		}
		close(fd);
	} else if (evsel->bpf_skel) {
		struct sample_filter_bpf *skel = evsel->bpf_skel;
		int fd = bpf_map__fd(skel->maps.dropped);
		int idx = 0;

		bpf_map_lookup_elem(fd, &idx, &count);
	}

	return count;
}

struct perf_bpf_filter_expr *perf_bpf_filter_expr__new(enum perf_bpf_filter_term term,
						       int part,
						       enum perf_bpf_filter_op op,
						       unsigned long val)
{
	struct perf_bpf_filter_expr *expr;

	expr = malloc(sizeof(*expr));
	if (expr != NULL) {
		expr->term = term;
		expr->part = part;
		expr->op = op;
		expr->val = val;
		INIT_LIST_HEAD(&expr->groups);
	}
	return expr;
}

int perf_bpf_filter__parse(struct list_head *expr_head, const char *str)
{
	YY_BUFFER_STATE buffer;
	int ret;

	buffer = perf_bpf_filter__scan_string(str);

	ret = perf_bpf_filter_parse(expr_head);

	perf_bpf_filter__flush_buffer(buffer);
	perf_bpf_filter__delete_buffer(buffer);
	perf_bpf_filter_lex_destroy();

	return ret;
}

int perf_bpf_filter__pin(void)
{
	struct sample_filter_bpf *skel;
	char *path = NULL;
	int dir_fd, ret = -1;

	skel = sample_filter_bpf__open();
	if (!skel) {
		ret = -errno;
		pr_err("Failed to open perf sample-filter BPF skeleton\n");
		goto err;
	}

	/* pinned program will use pid-hash */
	bpf_map__set_max_entries(skel->maps.filters, MAX_FILTERS);
	bpf_map__set_max_entries(skel->maps.event_hash, MAX_EVT_HASH);
	bpf_map__set_max_entries(skel->maps.idx_hash, MAX_IDX_HASH);
	bpf_map__set_max_entries(skel->maps.dropped, MAX_FILTERS);
	skel->rodata->use_idx_hash = 1;

	if (sample_filter_bpf__load(skel) < 0) {
		ret = -errno;
		pr_err("Failed to load perf sample-filter BPF skeleton\n");
		goto err;
	}

	if (asprintf(&path, "%s/fs/bpf/%s", sysfs__mountpoint(),
		     PERF_BPF_FILTER_PIN_PATH) < 0) {
		ret = -errno;
		pr_err("Failed to allocate pathname in the BPF-fs\n");
		goto err;
	}

	ret = bpf_object__pin(skel->obj, path);
	if (ret < 0) {
		pr_err("Failed to pin BPF filter objects\n");
		goto err;
	}

	/* setup access permissions for the pinned objects */
	dir_fd = open(path, O_PATH);
	if (dir_fd < 0) {
		bpf_object__unpin(skel->obj, path);
		ret = dir_fd;
		goto err;
	}

	/* BPF-fs root has the sticky bit */
	if (fchmodat(dir_fd, "..", 01755, 0) < 0) {
		pr_debug("chmod for BPF-fs failed\n");
		ret = -errno;
		goto err_close;
	}

	/* perf_filter directory */
	if (fchmodat(dir_fd, ".", 0755, 0) < 0) {
		pr_debug("chmod for perf_filter directory failed?\n");
		ret = -errno;
		goto err_close;
	}

	/* programs need write permission for some reason */
	if (fchmodat(dir_fd, "perf_sample_filter", 0777, 0) < 0) {
		pr_debug("chmod for perf_sample_filter failed\n");
		ret = -errno;
	}
	/* maps */
	if (fchmodat(dir_fd, "filters", 0666, 0) < 0) {
		pr_debug("chmod for filters failed\n");
		ret = -errno;
	}
	if (fchmodat(dir_fd, "event_hash", 0666, 0) < 0) {
		pr_debug("chmod for event_hash failed\n");
		ret = -errno;
	}
	if (fchmodat(dir_fd, "idx_hash", 0666, 0) < 0) {
		pr_debug("chmod for idx_hash failed\n");
		ret = -errno;
	}
	if (fchmodat(dir_fd, "dropped", 0666, 0) < 0) {
		pr_debug("chmod for dropped failed\n");
		ret = -errno;
	}

err_close:
	close(dir_fd);

err:
	free(path);
	sample_filter_bpf__destroy(skel);
	return ret;
}

int perf_bpf_filter__unpin(void)
{
	struct sample_filter_bpf *skel;
	char *path = NULL;
	int ret = -1;

	skel = sample_filter_bpf__open_and_load();
	if (!skel) {
		ret = -errno;
		pr_err("Failed to open perf sample-filter BPF skeleton\n");
		goto err;
	}

	if (asprintf(&path, "%s/fs/bpf/%s", sysfs__mountpoint(),
		     PERF_BPF_FILTER_PIN_PATH) < 0) {
		ret = -errno;
		pr_err("Failed to allocate pathname in the BPF-fs\n");
		goto err;
	}

	ret = bpf_object__unpin(skel->obj, path);

err:
	free(path);
	sample_filter_bpf__destroy(skel);
	return ret;
}

static int get_pinned_fd(const char *name)
{
	char *path = NULL;
	int fd;

	if (asprintf(&path, "%s/fs/bpf/%s/%s", sysfs__mountpoint(),
		     PERF_BPF_FILTER_PIN_PATH, name) < 0)
		return -1;

	fd = bpf_obj_get(path);

	free(path);
	return fd;
}

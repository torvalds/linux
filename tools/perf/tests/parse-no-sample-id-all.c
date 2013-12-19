#include <sys/types.h>
#include <stddef.h>

#include "tests.h"

#include "event.h"
#include "evlist.h"
#include "header.h"
#include "util.h"

static int process_event(struct perf_evlist **pevlist, union perf_event *event)
{
	struct perf_sample sample;

	if (event->header.type == PERF_RECORD_HEADER_ATTR) {
		if (perf_event__process_attr(NULL, event, pevlist)) {
			pr_debug("perf_event__process_attr failed\n");
			return -1;
		}
		return 0;
	}

	if (event->header.type >= PERF_RECORD_USER_TYPE_START)
		return -1;

	if (!*pevlist)
		return -1;

	if (perf_evlist__parse_sample(*pevlist, event, &sample)) {
		pr_debug("perf_evlist__parse_sample failed\n");
		return -1;
	}

	return 0;
}

static int process_events(union perf_event **events, size_t count)
{
	struct perf_evlist *evlist = NULL;
	int err = 0;
	size_t i;

	for (i = 0; i < count && !err; i++)
		err = process_event(&evlist, events[i]);

	if (evlist)
		perf_evlist__delete(evlist);

	return err;
}

struct test_attr_event {
	struct attr_event attr;
	u64 id;
};

/**
 * test__parse_no_sample_id_all - test parsing with no sample_id_all bit set.
 *
 * This function tests parsing data produced on kernel's that do not support the
 * sample_id_all bit.  Without the sample_id_all bit, non-sample events (such as
 * mmap events) do not have an id sample appended, and consequently logic
 * designed to determine the id will not work.  That case happens when there is
 * more than one selected event, so this test processes three events: 2
 * attributes representing the selected events and one mmap event.
 *
 * Return: %0 on success, %-1 if the test fails.
 */
int test__parse_no_sample_id_all(void)
{
	int err;

	struct test_attr_event event1 = {
		.attr = {
			.header = {
				.type = PERF_RECORD_HEADER_ATTR,
				.size = sizeof(struct test_attr_event),
			},
		},
		.id = 1,
	};
	struct test_attr_event event2 = {
		.attr = {
			.header = {
				.type = PERF_RECORD_HEADER_ATTR,
				.size = sizeof(struct test_attr_event),
			},
		},
		.id = 2,
	};
	struct mmap_event event3 = {
		.header = {
			.type = PERF_RECORD_MMAP,
			.size = sizeof(struct mmap_event),
		},
	};
	union perf_event *events[] = {
		(union perf_event *)&event1,
		(union perf_event *)&event2,
		(union perf_event *)&event3,
	};

	err = process_events(events, ARRAY_SIZE(events));
	if (err)
		return -1;

	return 0;
}

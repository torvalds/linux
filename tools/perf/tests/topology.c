// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "tests.h"
#include "util.h"
#include "session.h"
#include "evlist.h"
#include "debug.h"

#define TEMPL "/tmp/perf-test-XXXXXX"
#define DATA_SIZE	10

static int get_temp(char *path)
{
	int fd;

	strcpy(path, TEMPL);

	fd = mkstemp(path);
	if (fd < 0) {
		perror("mkstemp failed");
		return -1;
	}

	close(fd);
	return 0;
}

static int session_write_header(char *path)
{
	struct perf_session *session;
	struct perf_data data = {
		.file      = {
			.path = path,
		},
		.mode      = PERF_DATA_MODE_WRITE,
	};

	session = perf_session__new(&data, false, NULL);
	TEST_ASSERT_VAL("can't get session", session);

	session->evlist = perf_evlist__new_default();
	TEST_ASSERT_VAL("can't get evlist", session->evlist);

	perf_header__set_feat(&session->header, HEADER_CPU_TOPOLOGY);
	perf_header__set_feat(&session->header, HEADER_NRCPUS);

	session->header.data_size += DATA_SIZE;

	TEST_ASSERT_VAL("failed to write header",
			!perf_session__write_header(session, session->evlist, data.file.fd, true));

	perf_session__delete(session);

	return 0;
}

static int check_cpu_topology(char *path, struct cpu_map *map)
{
	struct perf_session *session;
	struct perf_data data = {
		.file      = {
			.path = path,
		},
		.mode      = PERF_DATA_MODE_READ,
	};
	int i;

	session = perf_session__new(&data, false, NULL);
	TEST_ASSERT_VAL("can't get session", session);

	/* On platforms with large numbers of CPUs process_cpu_topology()
	 * might issue an error while reading the perf.data file section
	 * HEADER_CPU_TOPOLOGY and the cpu_topology_map pointed to by member
	 * cpu is a NULL pointer.
	 * Example: On s390
	 *   CPU 0 is on core_id 0 and physical_package_id 6
	 *   CPU 1 is on core_id 1 and physical_package_id 3
	 *
	 *   Core_id and physical_package_id are platform and architecture
	 *   dependend and might have higher numbers than the CPU id.
	 *   This actually depends on the configuration.
	 *
	 *  In this case process_cpu_topology() prints error message:
	 *  "socket_id number is too big. You may need to upgrade the
	 *  perf tool."
	 *
	 *  This is the reason why this test might be skipped.
	 */
	if (!session->header.env.cpu)
		return TEST_SKIP;

	for (i = 0; i < session->header.env.nr_cpus_avail; i++) {
		if (!cpu_map__has(map, i))
			continue;
		pr_debug("CPU %d, core %d, socket %d\n", i,
			 session->header.env.cpu[i].core_id,
			 session->header.env.cpu[i].socket_id);
	}

	for (i = 0; i < map->nr; i++) {
		TEST_ASSERT_VAL("Core ID doesn't match",
			(session->header.env.cpu[map->map[i]].core_id == (cpu_map__get_core(map, i, NULL) & 0xffff)));

		TEST_ASSERT_VAL("Socket ID doesn't match",
			(session->header.env.cpu[map->map[i]].socket_id == cpu_map__get_socket(map, i, NULL)));
	}

	perf_session__delete(session);

	return 0;
}

int test__session_topology(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	char path[PATH_MAX];
	struct cpu_map *map;
	int ret = TEST_FAIL;

	TEST_ASSERT_VAL("can't get templ file", !get_temp(path));

	pr_debug("templ file: %s\n", path);

	if (session_write_header(path))
		goto free_path;

	map = cpu_map__new(NULL);
	if (map == NULL) {
		pr_debug("failed to get system cpumap\n");
		goto free_path;
	}

	ret = check_cpu_topology(path, map);
	cpu_map__put(map);

free_path:
	unlink(path);
	return ret;
}

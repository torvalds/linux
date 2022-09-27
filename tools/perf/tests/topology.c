// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <perf/cpumap.h>
#include "cpumap.h"
#include "tests.h"
#include "session.h"
#include "evlist.h"
#include "debug.h"
#include "pmu.h"
#include <linux/err.h>

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
		.path = path,
		.mode = PERF_DATA_MODE_WRITE,
	};

	session = perf_session__new(&data, NULL);
	TEST_ASSERT_VAL("can't get session", !IS_ERR(session));

	if (!perf_pmu__has_hybrid()) {
		session->evlist = evlist__new_default();
		TEST_ASSERT_VAL("can't get evlist", session->evlist);
	} else {
		struct parse_events_error err;

		session->evlist = evlist__new();
		TEST_ASSERT_VAL("can't get evlist", session->evlist);
		parse_events_error__init(&err);
		parse_events(session->evlist, "cpu_core/cycles/", &err);
		parse_events_error__exit(&err);
	}

	perf_header__set_feat(&session->header, HEADER_CPU_TOPOLOGY);
	perf_header__set_feat(&session->header, HEADER_NRCPUS);
	perf_header__set_feat(&session->header, HEADER_ARCH);

	session->header.data_size += DATA_SIZE;

	TEST_ASSERT_VAL("failed to write header",
			!perf_session__write_header(session, session->evlist, data.file.fd, true));

	evlist__delete(session->evlist);
	perf_session__delete(session);

	return 0;
}

static int check_cpu_topology(char *path, struct perf_cpu_map *map)
{
	struct perf_session *session;
	struct perf_data data = {
		.path = path,
		.mode = PERF_DATA_MODE_READ,
	};
	int i;
	struct aggr_cpu_id id;

	session = perf_session__new(&data, NULL);
	TEST_ASSERT_VAL("can't get session", !IS_ERR(session));
	cpu__setup_cpunode_map();

	/* On platforms with large numbers of CPUs process_cpu_topology()
	 * might issue an error while reading the perf.data file section
	 * HEADER_CPU_TOPOLOGY and the cpu_topology_map pointed to by member
	 * cpu is a NULL pointer.
	 * Example: On s390
	 *   CPU 0 is on core_id 0 and physical_package_id 6
	 *   CPU 1 is on core_id 1 and physical_package_id 3
	 *
	 *   Core_id and physical_package_id are platform and architecture
	 *   dependent and might have higher numbers than the CPU id.
	 *   This actually depends on the configuration.
	 *
	 *  In this case process_cpu_topology() prints error message:
	 *  "socket_id number is too big. You may need to upgrade the
	 *  perf tool."
	 *
	 *  This is the reason why this test might be skipped. aarch64 and
	 *  s390 always write this part of the header, even when the above
	 *  condition is true (see do_core_id_test in header.c). So always
	 *  run this test on those platforms.
	 */
	if (!session->header.env.cpu
			&& strncmp(session->header.env.arch, "s390", 4)
			&& strncmp(session->header.env.arch, "aarch64", 7))
		return TEST_SKIP;

	/*
	 * In powerpc pSeries platform, not all the topology information
	 * are exposed via sysfs. Due to restriction, detail like
	 * physical_package_id will be set to -1. Hence skip this
	 * test if physical_package_id returns -1 for cpu from perf_cpu_map.
	 */
	if (!strncmp(session->header.env.arch, "ppc64le", 7)) {
		if (cpu__get_socket_id(perf_cpu_map__cpu(map, 0)) == -1)
			return TEST_SKIP;
	}

	TEST_ASSERT_VAL("Session header CPU map not set", session->header.env.cpu);

	for (i = 0; i < session->header.env.nr_cpus_avail; i++) {
		struct perf_cpu cpu = { .cpu = i };

		if (!perf_cpu_map__has(map, cpu))
			continue;
		pr_debug("CPU %d, core %d, socket %d\n", i,
			 session->header.env.cpu[i].core_id,
			 session->header.env.cpu[i].socket_id);
	}

	// Test that CPU ID contains socket, die, core and CPU
	for (i = 0; i < perf_cpu_map__nr(map); i++) {
		id = aggr_cpu_id__cpu(perf_cpu_map__cpu(map, i), NULL);
		TEST_ASSERT_VAL("Cpu map - CPU ID doesn't match",
				perf_cpu_map__cpu(map, i).cpu == id.cpu.cpu);

		TEST_ASSERT_VAL("Cpu map - Core ID doesn't match",
			session->header.env.cpu[perf_cpu_map__cpu(map, i).cpu].core_id == id.core);
		TEST_ASSERT_VAL("Cpu map - Socket ID doesn't match",
			session->header.env.cpu[perf_cpu_map__cpu(map, i).cpu].socket_id ==
			id.socket);

		TEST_ASSERT_VAL("Cpu map - Die ID doesn't match",
			session->header.env.cpu[perf_cpu_map__cpu(map, i).cpu].die_id == id.die);
		TEST_ASSERT_VAL("Cpu map - Node ID is set", id.node == -1);
		TEST_ASSERT_VAL("Cpu map - Thread is set", id.thread == -1);
	}

	// Test that core ID contains socket, die and core
	for (i = 0; i < perf_cpu_map__nr(map); i++) {
		id = aggr_cpu_id__core(perf_cpu_map__cpu(map, i), NULL);
		TEST_ASSERT_VAL("Core map - Core ID doesn't match",
			session->header.env.cpu[perf_cpu_map__cpu(map, i).cpu].core_id == id.core);

		TEST_ASSERT_VAL("Core map - Socket ID doesn't match",
			session->header.env.cpu[perf_cpu_map__cpu(map, i).cpu].socket_id ==
			id.socket);

		TEST_ASSERT_VAL("Core map - Die ID doesn't match",
			session->header.env.cpu[perf_cpu_map__cpu(map, i).cpu].die_id == id.die);
		TEST_ASSERT_VAL("Core map - Node ID is set", id.node == -1);
		TEST_ASSERT_VAL("Core map - Thread is set", id.thread == -1);
	}

	// Test that die ID contains socket and die
	for (i = 0; i < perf_cpu_map__nr(map); i++) {
		id = aggr_cpu_id__die(perf_cpu_map__cpu(map, i), NULL);
		TEST_ASSERT_VAL("Die map - Socket ID doesn't match",
			session->header.env.cpu[perf_cpu_map__cpu(map, i).cpu].socket_id ==
			id.socket);

		TEST_ASSERT_VAL("Die map - Die ID doesn't match",
			session->header.env.cpu[perf_cpu_map__cpu(map, i).cpu].die_id == id.die);

		TEST_ASSERT_VAL("Die map - Node ID is set", id.node == -1);
		TEST_ASSERT_VAL("Die map - Core is set", id.core == -1);
		TEST_ASSERT_VAL("Die map - CPU is set", id.cpu.cpu == -1);
		TEST_ASSERT_VAL("Die map - Thread is set", id.thread == -1);
	}

	// Test that socket ID contains only socket
	for (i = 0; i < perf_cpu_map__nr(map); i++) {
		id = aggr_cpu_id__socket(perf_cpu_map__cpu(map, i), NULL);
		TEST_ASSERT_VAL("Socket map - Socket ID doesn't match",
			session->header.env.cpu[perf_cpu_map__cpu(map, i).cpu].socket_id ==
			id.socket);

		TEST_ASSERT_VAL("Socket map - Node ID is set", id.node == -1);
		TEST_ASSERT_VAL("Socket map - Die ID is set", id.die == -1);
		TEST_ASSERT_VAL("Socket map - Core is set", id.core == -1);
		TEST_ASSERT_VAL("Socket map - CPU is set", id.cpu.cpu == -1);
		TEST_ASSERT_VAL("Socket map - Thread is set", id.thread == -1);
	}

	// Test that node ID contains only node
	for (i = 0; i < perf_cpu_map__nr(map); i++) {
		id = aggr_cpu_id__node(perf_cpu_map__cpu(map, i), NULL);
		TEST_ASSERT_VAL("Node map - Node ID doesn't match",
				cpu__get_node(perf_cpu_map__cpu(map, i)) == id.node);
		TEST_ASSERT_VAL("Node map - Socket is set", id.socket == -1);
		TEST_ASSERT_VAL("Node map - Die ID is set", id.die == -1);
		TEST_ASSERT_VAL("Node map - Core is set", id.core == -1);
		TEST_ASSERT_VAL("Node map - CPU is set", id.cpu.cpu == -1);
		TEST_ASSERT_VAL("Node map - Thread is set", id.thread == -1);
	}
	perf_session__delete(session);

	return 0;
}

static int test__session_topology(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	char path[PATH_MAX];
	struct perf_cpu_map *map;
	int ret = TEST_FAIL;

	TEST_ASSERT_VAL("can't get templ file", !get_temp(path));

	pr_debug("templ file: %s\n", path);

	if (session_write_header(path))
		goto free_path;

	map = perf_cpu_map__new(NULL);
	if (map == NULL) {
		pr_debug("failed to get system cpumap\n");
		goto free_path;
	}

	ret = check_cpu_topology(path, map);
	perf_cpu_map__put(map);

free_path:
	unlink(path);
	return ret;
}

DEFINE_SUITE("Session topology", session_topology);

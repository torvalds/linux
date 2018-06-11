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
	struct perf_data_file file = {
		.path = path,
		.mode = PERF_DATA_MODE_WRITE,
	};

	session = perf_session__new(&file, false, NULL);
	TEST_ASSERT_VAL("can't get session", session);

	session->evlist = perf_evlist__new_default();
	TEST_ASSERT_VAL("can't get evlist", session->evlist);

	perf_header__set_feat(&session->header, HEADER_CPU_TOPOLOGY);
	perf_header__set_feat(&session->header, HEADER_NRCPUS);
	perf_header__set_feat(&session->header, HEADER_ARCH);

	session->header.data_size += DATA_SIZE;

	TEST_ASSERT_VAL("failed to write header",
			!perf_session__write_header(session, session->evlist, file.fd, true));

	perf_session__delete(session);

	return 0;
}

static int check_cpu_topology(char *path, struct cpu_map *map)
{
	struct perf_session *session;
	struct perf_data_file file = {
		.path = path,
		.mode = PERF_DATA_MODE_READ,
	};
	int i;

	session = perf_session__new(&file, false, NULL);
	TEST_ASSERT_VAL("can't get session", session);

	for (i = 0; i < session->header.env.nr_cpus_online; i++) {
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

int test_session_topology(void)
{
	char path[PATH_MAX];
	struct cpu_map *map;
	int ret = -1;

	TEST_ASSERT_VAL("can't get templ file", !get_temp(path));

	pr_debug("templ file: %s\n", path);

	if (session_write_header(path))
		goto free_path;

	map = cpu_map__new(NULL);
	if (map == NULL) {
		pr_debug("failed to get system cpumap\n");
		goto free_path;
	}

	if (check_cpu_topology(path, map))
		goto free_map;
	ret = 0;

free_map:
	cpu_map__put(map);
free_path:
	unlink(path);
	return ret;
}

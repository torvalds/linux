// SPDX-License-Identifier: GPL-2.0-only

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/libbpf.h>
#include <bpftool_helpers.h>
#include <test_progs.h>
#include <bpf/bpf.h>
#include "security_bpf_map.skel.h"

#define PROTECTED_MAP_NAME	"prot_map"
#define UNPROTECTED_MAP_NAME	"not_prot_map"
#define BPF_ITER_FILE		"bpf_iter_map_elem.bpf.o"
#define BPFFS_PIN_DIR		"/sys/fs/bpf/test_bpftool_map"
#define INNER_MAP_NAME		"inner_map_tt"
#define OUTER_MAP_NAME		"outer_map_tt"

#define MAP_NAME_MAX_LEN	64
#define PATH_MAX_LEN		128

enum map_protection {
	PROTECTED,
	UNPROTECTED
};

struct test_desc {
	char *name;
	enum map_protection protection;
	struct bpf_map *map;
	char *map_name;
	bool pinned;
	char pin_path[PATH_MAX_LEN];
	bool write_must_fail;
};

static struct security_bpf_map *general_setup(void)
{
	struct security_bpf_map *skel;
	uint32_t key, value;
	int ret, i;

	skel = security_bpf_map__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open and load skeleton"))
		goto end;

	struct bpf_map *maps[] = {skel->maps.prot_map, skel->maps.not_prot_map};

	ret = security_bpf_map__attach(skel);
	if (!ASSERT_OK(ret, "attach maps security programs"))
		goto end_destroy;

	for (i = 0; i < sizeof(maps)/sizeof(struct bpf_map *); i++) {
		for (key = 0; key < 2; key++) {
			int ret = bpf_map__update_elem(maps[i], &key,
					sizeof(key), &key, sizeof(key),
					0);
			if (!ASSERT_OK(ret, "set initial map value"))
				goto end_destroy;
		}
	}

	key = 0;
	value = 1;
	ret = bpf_map__update_elem(skel->maps.prot_status_map, &key,
			sizeof(key), &value, sizeof(value), 0);
	if (!ASSERT_OK(ret, "configure map protection"))
		goto end_destroy;

	if (!ASSERT_OK(mkdir(BPFFS_PIN_DIR, S_IFDIR), "create bpffs pin dir"))
		goto end_destroy;

	return skel;
end_destroy:
	security_bpf_map__destroy(skel);
end:
	return NULL;
}

static void general_cleanup(struct security_bpf_map *skel)
{
	rmdir(BPFFS_PIN_DIR);
	security_bpf_map__destroy(skel);
}

static void update_test_desc(struct security_bpf_map *skel,
			      struct test_desc *test)
{
	/* Now that the skeleton is loaded, update all missing fields to
	 * have the subtest properly configured
	 */
	if (test->protection == PROTECTED) {
		test->map = skel->maps.prot_map;
		test->map_name = PROTECTED_MAP_NAME;
	} else {
		test->map = skel->maps.not_prot_map;
		test->map_name = UNPROTECTED_MAP_NAME;
	}
}

static int test_setup(struct security_bpf_map *skel, struct test_desc *desc)
{
	int ret;

	update_test_desc(skel, desc);

	if (desc->pinned) {
		ret = snprintf(desc->pin_path, PATH_MAX_LEN, "%s/%s", BPFFS_PIN_DIR,
				desc->name);
		if (!ASSERT_GT(ret, 0, "format pin path"))
			return 1;
		ret = bpf_map__pin(desc->map, desc->pin_path);
		if (!ASSERT_OK(ret, "pin map"))
			return 1;
	}

	return 0;
}

static void test_cleanup(struct test_desc *desc)
{
	if (desc->pinned)
		bpf_map__unpin(desc->map, NULL);
}

static int lookup_map_value(char *map_handle)
{
	char cmd[MAX_BPFTOOL_CMD_LEN];
	int ret = 0;

	ret = snprintf(cmd, MAX_BPFTOOL_CMD_LEN, "map lookup %s key 0 0 0 0",
			map_handle);
	if (!ASSERT_GT(ret, 0, "format map lookup cmd"))
		return 1;
	return run_bpftool_command(cmd);
}

static int read_map_btf_data(char *map_handle)
{
	char cmd[MAX_BPFTOOL_CMD_LEN];
	int ret = 0;

	ret = snprintf(cmd, MAX_BPFTOOL_CMD_LEN, "btf dump map %s",
			map_handle);
	if (!ASSERT_GT(ret, 0, "format map btf dump cmd"))
		return 1;
	return run_bpftool_command(cmd);
}

static int write_map_value(char *map_handle)
{
	char cmd[MAX_BPFTOOL_CMD_LEN];
	int ret = 0;

	ret = snprintf(cmd, MAX_BPFTOOL_CMD_LEN,
		       "map update %s key 0 0 0 0 value 1 1 1 1", map_handle);
	if (!ASSERT_GT(ret, 0, "format value write cmd"))
		return 1;
	return run_bpftool_command(cmd);
}

static int delete_map_value(char *map_handle)
{
	char cmd[MAX_BPFTOOL_CMD_LEN];
	int ret = 0;

	ret = snprintf(cmd, MAX_BPFTOOL_CMD_LEN,
		       "map delete %s key 0 0 0 0", map_handle);
	if (!ASSERT_GT(ret, 0, "format value deletion cmd"))
		return 1;
	return run_bpftool_command(cmd);
}

static int iterate_on_map_values(char *map_handle, char *iter_pin_path)
{
	char cmd[MAX_BPFTOOL_CMD_LEN];
	int ret = 0;


	ret = snprintf(cmd, MAX_BPFTOOL_CMD_LEN, "iter pin %s %s map %s",
		       BPF_ITER_FILE, iter_pin_path, map_handle);
	if (!ASSERT_GT(ret, 0, "format iterator creation cmd"))
		return 1;
	ret = run_bpftool_command(cmd);
	if (ret)
		return ret;
	ret = snprintf(cmd, MAP_NAME_MAX_LEN, "cat %s", iter_pin_path);
	if (ret < 0)
		goto cleanup;
	ret = system(cmd);

cleanup:
	unlink(iter_pin_path);
	return ret;
}

static int create_inner_map(void)
{
	char cmd[MAX_BPFTOOL_CMD_LEN];
	int ret = 0;

	ret = snprintf(
		cmd, MAX_BPFTOOL_CMD_LEN,
		"map create %s/%s type array key 4 value 4 entries 4 name %s",
		BPFFS_PIN_DIR, INNER_MAP_NAME, INNER_MAP_NAME);
	if (!ASSERT_GT(ret, 0, "format inner map create cmd"))
		return 1;
	return run_bpftool_command(cmd);
}

static int create_outer_map(void)
{
	char cmd[MAX_BPFTOOL_CMD_LEN];
	int ret = 0;

	ret = snprintf(
		cmd, MAX_BPFTOOL_CMD_LEN,
		"map create %s/%s type hash_of_maps key 4 value 4 entries 2 name %s inner_map name %s",
		BPFFS_PIN_DIR, OUTER_MAP_NAME, OUTER_MAP_NAME, INNER_MAP_NAME);
	if (!ASSERT_GT(ret, 0, "format outer map create cmd"))
		return 1;
	return run_bpftool_command(cmd);
}

static void delete_pinned_map(char *map_name)
{
	char pin_path[PATH_MAX_LEN];
	int ret;

	ret = snprintf(pin_path, PATH_MAX_LEN, "%s/%s", BPFFS_PIN_DIR,
		       map_name);
	if (ret >= 0)
		unlink(pin_path);
}

static int add_outer_map_entry(int key)
{
	char cmd[MAX_BPFTOOL_CMD_LEN];
	int ret = 0;

	ret = snprintf(
		cmd, MAX_BPFTOOL_CMD_LEN,
		"map update pinned %s/%s key %d 0 0 0 value name %s",
		BPFFS_PIN_DIR, OUTER_MAP_NAME, key, INNER_MAP_NAME);
	if (!ASSERT_GT(ret, 0, "format outer map value addition cmd"))
		return 1;
	return run_bpftool_command(cmd);
}

static void test_basic_access(struct test_desc *desc)
{
	char map_handle[MAP_NAME_MAX_LEN];
	char iter_pin_path[PATH_MAX_LEN];
	int ret;

	if (desc->pinned)
		ret = snprintf(map_handle, MAP_NAME_MAX_LEN, "pinned %s",
			       desc->pin_path);
	else
		ret = snprintf(map_handle, MAP_NAME_MAX_LEN, "name %s",
			       desc->map_name);
	if (!ASSERT_GT(ret, 0, "format map handle"))
		return;

	ret = lookup_map_value(map_handle);
	ASSERT_OK(ret, "read map value");

	ret = read_map_btf_data(map_handle);
	ASSERT_OK(ret, "read map btf data");

	ret = write_map_value(map_handle);
	ASSERT_OK(desc->write_must_fail ? !ret : ret, "write map value");

	ret = delete_map_value(map_handle);
	ASSERT_OK(desc->write_must_fail ? !ret : ret, "delete map value");
	/* Restore deleted value */
	if (!ret)
		write_map_value(map_handle);

	ret = snprintf(iter_pin_path, PATH_MAX_LEN, "%s/iter", BPFFS_PIN_DIR);
	if (ASSERT_GT(ret, 0, "format iter pin path")) {
		ret = iterate_on_map_values(map_handle, iter_pin_path);
		ASSERT_OK(ret, "iterate on map values");
	}
}

static void test_create_nested_maps(void)
{
	if (!ASSERT_OK(create_inner_map(), "create inner map"))
		return;
	if (!ASSERT_OK(create_outer_map(), "create outer map"))
		goto end_cleanup_inner;
	ASSERT_OK(add_outer_map_entry(0), "add a first entry in outer map");
	ASSERT_OK(add_outer_map_entry(1), "add a second entry in outer map");
	ASSERT_NEQ(add_outer_map_entry(2), 0, "add a third entry in outer map");

	delete_pinned_map(OUTER_MAP_NAME);
end_cleanup_inner:
	delete_pinned_map(INNER_MAP_NAME);
}

static void test_btf_list(void)
{
	ASSERT_OK(run_bpftool_command("btf list"), "list btf data");
}

static struct test_desc tests[] = {
	{
		.name = "unprotected_unpinned",
		.protection = UNPROTECTED,
		.map_name = UNPROTECTED_MAP_NAME,
		.pinned = false,
		.write_must_fail = false,
	},
	{
		.name = "unprotected_pinned",
		.protection = UNPROTECTED,
		.map_name = UNPROTECTED_MAP_NAME,
		.pinned = true,
		.write_must_fail = false,
	},
	{
		.name = "protected_unpinned",
		.protection = PROTECTED,
		.map_name = UNPROTECTED_MAP_NAME,
		.pinned = false,
		.write_must_fail = true,
	},
	{
		.name = "protected_pinned",
		.protection = PROTECTED,
		.map_name = UNPROTECTED_MAP_NAME,
		.pinned = true,
		.write_must_fail = true,
	}
};

static const size_t tests_count = ARRAY_SIZE(tests);

void test_bpftool_maps_access(void)
{
	struct security_bpf_map *skel;
	struct test_desc *current;
	int i;

	skel = general_setup();
	if (!ASSERT_OK_PTR(skel, "prepare programs"))
		goto cleanup;

	for (i = 0; i < tests_count; i++) {
		current = &tests[i];
		if (!test__start_subtest(current->name))
			continue;
		if (ASSERT_OK(test_setup(skel, current), "subtest setup")) {
			test_basic_access(current);
			test_cleanup(current);
		}
	}
	if (test__start_subtest("nested_maps"))
		test_create_nested_maps();
	if (test__start_subtest("btf_list"))
		test_btf_list();

cleanup:
	general_cleanup(skel);
}


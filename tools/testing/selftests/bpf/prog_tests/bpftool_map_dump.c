// SPDX-License-Identifier: GPL-2.0-only
#include <sys/socket.h>
#include <unistd.h>
#include <bpf/btf.h>
#include <test_progs.h>
#include "bpftool_helpers.h"

#define PIN_PATH	"/sys/fs/bpf/test_bpftool_map_dump_sk_storage"
#define QUEUE_PIN_PATH	"/sys/fs/bpf/test_bpftool_map_dump_queue"
#define JSON_ERROR	"{\"error\":\"can't dump sk_storage map"

/* An empty socket storage map with an int value. */
static int create_sk_storage_map(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		    .map_flags = BPF_F_NO_PREALLOC);
	struct btf *btf;
	int int_id, fd = -1;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "btf__new_empty"))
		return -1;

	int_id = btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	if (!ASSERT_GT(int_id, 0, "btf__add_int"))
		goto out;
	if (!ASSERT_OK(btf__load_into_kernel(btf), "btf__load_into_kernel"))
		goto out;

	opts.btf_fd = btf__fd(btf);
	opts.btf_key_type_id = int_id;
	opts.btf_value_type_id = int_id;
	fd = bpf_map_create(BPF_MAP_TYPE_SK_STORAGE, "sk_storage", sizeof(int),
			    sizeof(int), 0, &opts);
	ASSERT_OK_FD(fd, "bpf_map_create");
out:
	btf__free(btf);
	return fd;
}

static void test_sk_storage(void)
{
	int map_fd, sk_fd = -1, value = 42, ret;
	char output[1024] = {};

	map_fd = create_sk_storage_map();
	if (map_fd < 0)
		return;

	sk_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (!ASSERT_OK_FD(sk_fd, "socket"))
		goto out;
	if (!ASSERT_OK(bpf_map_update_elem(map_fd, &sk_fd, &value, BPF_NOEXIST),
		       "add socket storage"))
		goto out;
	if (!ASSERT_OK(bpf_obj_pin(map_fd, PIN_PATH), "pin map"))
		goto out;

	/*
	 * Socket storage can't be iterated, so the dump must fail with an
	 * error and nothing else, rather than print an empty map that holds
	 * an element. The buffer is cleared before each run so that a run
	 * that fails to start can't match stale or uninitialised output.
	 */
	ret = get_bpftool_command_output("-j map dump pinned " PIN_PATH,
					 output, sizeof(output));
	ASSERT_NEQ(ret, 0, "json dump fails");
	ASSERT_STRNEQ(output, JSON_ERROR, strlen(JSON_ERROR),
		      "json dump is only the error");

	memset(output, 0, sizeof(output));
	ret = get_bpftool_command_output("map dump pinned " PIN_PATH,
					 output, sizeof(output));
	ASSERT_NEQ(ret, 0, "plain dump fails");
	ASSERT_NULL(strchr(output, '['), "plain dump prints no elements");

	memset(output, 0, sizeof(output));
	get_bpftool_command_output("map dump pinned " PIN_PATH " 2>&1 >/dev/null",
				   output, sizeof(output));
	ASSERT_HAS_SUBSTR(output,
			  "can't dump sk_storage map: Operation not supported",
			  "plain dump explains why on stderr");

out:
	unlink(PIN_PATH);
	if (sk_fd >= 0)
		close(sk_fd);
	close(map_fd);
}

/*
 * A queue map has no BTF, so its plain dump prints an element count
 * rather than JSON. A dump that ignored the error would print "Found 0
 * elements" for a queue that holds one.
 */
static void test_queue(void)
{
	char output[1024] = {};
	int map_fd, value = 42, ret;

	map_fd = bpf_map_create(BPF_MAP_TYPE_QUEUE, "queue", 0, sizeof(int), 1,
				NULL);
	if (!ASSERT_OK_FD(map_fd, "bpf_map_create"))
		return;
	if (!ASSERT_OK(bpf_map_update_elem(map_fd, NULL, &value, BPF_ANY),
		       "push to queue"))
		goto out;
	if (!ASSERT_OK(bpf_obj_pin(map_fd, QUEUE_PIN_PATH), "pin map"))
		goto out;

	ret = get_bpftool_command_output("map dump pinned " QUEUE_PIN_PATH,
					 output, sizeof(output));
	ASSERT_NEQ(ret, 0, "plain dump fails");
	ASSERT_NULL(strstr(output, "Found"), "plain dump prints no count");

	memset(output, 0, sizeof(output));
	get_bpftool_command_output("map dump pinned " QUEUE_PIN_PATH " 2>&1 >/dev/null",
				   output, sizeof(output));
	ASSERT_HAS_SUBSTR(output, "can't dump queue map",
			  "plain dump explains why on stderr");

out:
	unlink(QUEUE_PIN_PATH);
	close(map_fd);
}

void test_bpftool_map_dump(void)
{
	if (test__start_subtest("sk_storage"))
		test_sk_storage();
	if (test__start_subtest("queue"))
		test_queue();
}

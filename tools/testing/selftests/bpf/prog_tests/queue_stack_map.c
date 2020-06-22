// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

enum {
	QUEUE,
	STACK,
};

static void test_queue_stack_map_by_type(int type)
{
	const int MAP_SIZE = 32;
	__u32 vals[MAP_SIZE], duration, retval, size, val;
	int i, err, prog_fd, map_in_fd, map_out_fd;
	char file[32], buf[128];
	struct bpf_object *obj;
	struct iphdr *iph = (void *)buf + sizeof(struct ethhdr);

	/* Fill test values to be used */
	for (i = 0; i < MAP_SIZE; i++)
		vals[i] = rand();

	if (type == QUEUE)
		strncpy(file, "./test_queue_map.o", sizeof(file));
	else if (type == STACK)
		strncpy(file, "./test_stack_map.o", sizeof(file));
	else
		return;

	err = bpf_prog_load(file, BPF_PROG_TYPE_SCHED_CLS, &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	map_in_fd = bpf_find_map(__func__, obj, "map_in");
	if (map_in_fd < 0)
		goto out;

	map_out_fd = bpf_find_map(__func__, obj, "map_out");
	if (map_out_fd < 0)
		goto out;

	/* Push 32 elements to the input map */
	for (i = 0; i < MAP_SIZE; i++) {
		err = bpf_map_update_elem(map_in_fd, NULL, &vals[i], 0);
		if (CHECK_FAIL(err))
			goto out;
	}

	/* The eBPF program pushes iph.saddr in the output map,
	 * pops the input map and saves this value in iph.daddr
	 */
	for (i = 0; i < MAP_SIZE; i++) {
		if (type == QUEUE) {
			val = vals[i];
			pkt_v4.iph.saddr = vals[i] * 5;
		} else if (type == STACK) {
			val = vals[MAP_SIZE - 1 - i];
			pkt_v4.iph.saddr = vals[MAP_SIZE - 1 - i] * 5;
		}

		err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
					buf, &size, &retval, &duration);
		if (err || retval || size != sizeof(pkt_v4) ||
		    iph->daddr != val)
			break;
	}

	CHECK(err || retval || size != sizeof(pkt_v4) || iph->daddr != val,
	      "bpf_map_pop_elem",
	      "err %d errno %d retval %d size %d iph->daddr %u\n",
	      err, errno, retval, size, iph->daddr);

	/* Queue is empty, program should return TC_ACT_SHOT */
	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				buf, &size, &retval, &duration);
	CHECK(err || retval != 2 /* TC_ACT_SHOT */|| size != sizeof(pkt_v4),
	      "check-queue-stack-map-empty",
	      "err %d errno %d retval %d size %d\n",
	      err, errno, retval, size);

	/* Check that the program pushed elements correctly */
	for (i = 0; i < MAP_SIZE; i++) {
		err = bpf_map_lookup_and_delete_elem(map_out_fd, NULL, &val);
		if (err || val != vals[i] * 5)
			break;
	}

	CHECK(i != MAP_SIZE && (err || val != vals[i] * 5),
	      "bpf_map_push_elem", "err %d value %u\n", err, val);

out:
	pkt_v4.iph.saddr = 0;
	bpf_object__close(obj);
}

void test_queue_stack_map(void)
{
	test_queue_stack_map_by_type(QUEUE);
	test_queue_stack_map_by_type(STACK);
}

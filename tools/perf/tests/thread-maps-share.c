// SPDX-License-Identifier: GPL-2.0
#include "tests.h"
#include "machine.h"
#include "thread.h"
#include "debug.h"

int test__thread_maps_share(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	struct machines machines;
	struct machine *machine;

	/* thread group */
	struct thread *leader;
	struct thread *t1, *t2, *t3;
	struct maps *maps;

	/* other process */
	struct thread *other, *other_leader;
	struct maps *other_maps;

	/*
	 * This test create 2 processes abstractions (struct thread)
	 * with several threads and checks they properly share and
	 * maintain maps info (struct maps).
	 *
	 * thread group (pid: 0, tids: 0, 1, 2, 3)
	 * other  group (pid: 4, tids: 4, 5)
	*/

	machines__init(&machines);
	machine = &machines.host;

	/* create process with 4 threads */
	leader = machine__findnew_thread(machine, 0, 0);
	t1     = machine__findnew_thread(machine, 0, 1);
	t2     = machine__findnew_thread(machine, 0, 2);
	t3     = machine__findnew_thread(machine, 0, 3);

	/* and create 1 separated process, without thread leader */
	other  = machine__findnew_thread(machine, 4, 5);

	TEST_ASSERT_VAL("failed to create threads",
			leader && t1 && t2 && t3 && other);

	maps = leader->maps;
	TEST_ASSERT_EQUAL("wrong refcnt", refcount_read(&maps->refcnt), 4);

	/* test the maps pointer is shared */
	TEST_ASSERT_VAL("maps don't match", maps == t1->maps);
	TEST_ASSERT_VAL("maps don't match", maps == t2->maps);
	TEST_ASSERT_VAL("maps don't match", maps == t3->maps);

	/*
	 * Verify the other leader was created by previous call.
	 * It should have shared maps with no change in
	 * refcnt.
	 */
	other_leader = machine__find_thread(machine, 4, 4);
	TEST_ASSERT_VAL("failed to find other leader", other_leader);

	/*
	 * Ok, now that all the rbtree related operations were done,
	 * lets remove all of them from there so that we can do the
	 * refcounting tests.
	 */
	machine__remove_thread(machine, leader);
	machine__remove_thread(machine, t1);
	machine__remove_thread(machine, t2);
	machine__remove_thread(machine, t3);
	machine__remove_thread(machine, other);
	machine__remove_thread(machine, other_leader);

	other_maps = other->maps;
	TEST_ASSERT_EQUAL("wrong refcnt", refcount_read(&other_maps->refcnt), 2);

	TEST_ASSERT_VAL("maps don't match", other_maps == other_leader->maps);

	/* release thread group */
	thread__put(leader);
	TEST_ASSERT_EQUAL("wrong refcnt", refcount_read(&maps->refcnt), 3);

	thread__put(t1);
	TEST_ASSERT_EQUAL("wrong refcnt", refcount_read(&maps->refcnt), 2);

	thread__put(t2);
	TEST_ASSERT_EQUAL("wrong refcnt", refcount_read(&maps->refcnt), 1);

	thread__put(t3);

	/* release other group  */
	thread__put(other_leader);
	TEST_ASSERT_EQUAL("wrong refcnt", refcount_read(&other_maps->refcnt), 1);

	thread__put(other);

	machines__exit(&machines);
	return 0;
}

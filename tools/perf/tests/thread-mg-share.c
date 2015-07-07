#include "tests.h"
#include "machine.h"
#include "thread.h"
#include "map.h"
#include "debug.h"

int test__thread_mg_share(void)
{
	struct machines machines;
	struct machine *machine;

	/* thread group */
	struct thread *leader;
	struct thread *t1, *t2, *t3;
	struct map_groups *mg;

	/* other process */
	struct thread *other, *other_leader;
	struct map_groups *other_mg;

	/*
	 * This test create 2 processes abstractions (struct thread)
	 * with several threads and checks they properly share and
	 * maintain map groups info (struct map_groups).
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

	mg = leader->mg;
	TEST_ASSERT_EQUAL("wrong refcnt", atomic_read(&mg->refcnt), 4);

	/* test the map groups pointer is shared */
	TEST_ASSERT_VAL("map groups don't match", mg == t1->mg);
	TEST_ASSERT_VAL("map groups don't match", mg == t2->mg);
	TEST_ASSERT_VAL("map groups don't match", mg == t3->mg);

	/*
	 * Verify the other leader was created by previous call.
	 * It should have shared map groups with no change in
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

	other_mg = other->mg;
	TEST_ASSERT_EQUAL("wrong refcnt", atomic_read(&other_mg->refcnt), 2);

	TEST_ASSERT_VAL("map groups don't match", other_mg == other_leader->mg);

	/* release thread group */
	thread__put(leader);
	TEST_ASSERT_EQUAL("wrong refcnt", atomic_read(&mg->refcnt), 3);

	thread__put(t1);
	TEST_ASSERT_EQUAL("wrong refcnt", atomic_read(&mg->refcnt), 2);

	thread__put(t2);
	TEST_ASSERT_EQUAL("wrong refcnt", atomic_read(&mg->refcnt), 1);

	thread__put(t3);

	/* release other group  */
	thread__put(other_leader);
	TEST_ASSERT_EQUAL("wrong refcnt", atomic_read(&other_mg->refcnt), 1);

	thread__put(other);

	machines__exit(&machines);
	return 0;
}

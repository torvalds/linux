#include <src/combined_source.c>

int x;
int y;

int __unbuffered_tpr_x;
int __unbuffered_tpr_y;

DEFINE_SRCU(ss);

void rcu_reader(void)
{
	int idx;

#ifndef FORCE_FAILURE_3
	idx = srcu_read_lock(&ss);
#endif
	might_sleep();

	__unbuffered_tpr_y = READ_ONCE(y);
#ifdef FORCE_FAILURE
	srcu_read_unlock(&ss, idx);
	idx = srcu_read_lock(&ss);
#endif
	WRITE_ONCE(x, 1);

#ifndef FORCE_FAILURE_3
	srcu_read_unlock(&ss, idx);
#endif
	might_sleep();
}

void *thread_update(void *arg)
{
	WRITE_ONCE(y, 1);
#ifndef FORCE_FAILURE_2
	synchronize_srcu(&ss);
#endif
	might_sleep();
	__unbuffered_tpr_x = READ_ONCE(x);

	return NULL;
}

void *thread_process_reader(void *arg)
{
	rcu_reader();

	return NULL;
}

int main(int argc, char *argv[])
{
	pthread_t tu;
	pthread_t tpr;

	if (pthread_create(&tu, NULL, thread_update, NULL))
		abort();
	if (pthread_create(&tpr, NULL, thread_process_reader, NULL))
		abort();
	if (pthread_join(tu, NULL))
		abort();
	if (pthread_join(tpr, NULL))
		abort();
	assert(__unbuffered_tpr_y != 0 || __unbuffered_tpr_x != 0);

#ifdef ASSERT_END
	assert(0);
#endif

	return 0;
}

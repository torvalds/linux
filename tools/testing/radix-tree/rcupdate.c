#include <linux/rcupdate.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>

static pthread_mutex_t rculock = PTHREAD_MUTEX_INITIALIZER;
static struct rcu_head *rcuhead_global = NULL;
static __thread int nr_rcuhead = 0;
static __thread struct rcu_head *rcuhead = NULL;
static __thread struct rcu_head *rcutail = NULL;

static pthread_cond_t rcu_worker_cond = PTHREAD_COND_INITIALIZER;

/* switch to urcu implementation when it is merged. */
void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *head))
{
	head->func = func;
	head->next = rcuhead;
	rcuhead = head;
	if (!rcutail)
		rcutail = head;
	nr_rcuhead++;
	if (nr_rcuhead >= 1000) {
		int signal = 0;

		pthread_mutex_lock(&rculock);
		if (!rcuhead_global)
			signal = 1;
		rcutail->next = rcuhead_global;
		rcuhead_global = head;
		pthread_mutex_unlock(&rculock);

		nr_rcuhead = 0;
		rcuhead = NULL;
		rcutail = NULL;

		if (signal) {
			pthread_cond_signal(&rcu_worker_cond);
		}
	}
}

static void *rcu_worker(void *arg)
{
	struct rcu_head *r;

	rcupdate_thread_init();

	while (1) {
		pthread_mutex_lock(&rculock);
		while (!rcuhead_global) {
			pthread_cond_wait(&rcu_worker_cond, &rculock);
		}
		r = rcuhead_global;
		rcuhead_global = NULL;

		pthread_mutex_unlock(&rculock);

		synchronize_rcu();

		while (r) {
			struct rcu_head *tmp = r->next;
			r->func(r);
			r = tmp;
		}
	}

	rcupdate_thread_exit();

	return NULL;
}

static pthread_t worker_thread;
void rcupdate_init(void)
{
	pthread_create(&worker_thread, NULL, rcu_worker, NULL);
}

void rcupdate_thread_init(void)
{
	rcu_register_thread();
}
void rcupdate_thread_exit(void)
{
	rcu_unregister_thread();
}

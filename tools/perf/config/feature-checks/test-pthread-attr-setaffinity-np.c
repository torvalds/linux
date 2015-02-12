#include <stdint.h>
#include <pthread.h>

int main(void)
{
	int ret = 0;
	pthread_attr_t thread_attr;

	pthread_attr_init(&thread_attr);
	/* don't care abt exact args, just the API itself in libpthread */
	ret = pthread_attr_setaffinity_np(&thread_attr, 0, NULL);

	return ret;
}

// SPDX-License-Identifier: GPL-2.0
#include <liblockdep/mutex.h>

int main(void)
{
	pthread_mutex_t a;

	pthread_mutex_init(&a, NULL);

	pthread_mutex_lock(&a);
	pthread_mutex_lock(&a);

	return 0;
}

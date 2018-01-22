// SPDX-License-Identifier: GPL-2.0
#include <liblockdep/mutex.h>

void main(void)
{
	pthread_mutex_t a, b;

	pthread_mutex_init(&a, NULL);
	pthread_mutex_init(&b, NULL);

	pthread_mutex_lock(&a);
	pthread_mutex_lock(&b);
	pthread_mutex_lock(&a);
}

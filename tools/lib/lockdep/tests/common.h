#ifndef _LIBLOCKDEP_TEST_COMMON_H
#define _LIBLOCKDEP_TEST_COMMON_H

#define LOCK_UNLOCK_2(a, b)			\
	do {					\
		pthread_mutex_lock(&(a));	\
		pthread_mutex_lock(&(b));	\
		pthread_mutex_unlock(&(b));	\
		pthread_mutex_unlock(&(a));	\
	} while(0)

#endif

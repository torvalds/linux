// SPDX-License-Identifier: GPL-2.0
#include "mutex.h"

#include "debug.h"
#include <linux/string.h>
#include <errno.h>

static void check_err(const char *fn, int err)
{
	char sbuf[STRERR_BUFSIZE];

	if (err == 0)
		return;

	pr_err("%s error: '%s'\n", fn, str_error_r(err, sbuf, sizeof(sbuf)));
}

#define CHECK_ERR(err) check_err(__func__, err)

static void __mutex_init(struct mutex *mtx, bool pshared)
{
	pthread_mutexattr_t attr;

	CHECK_ERR(pthread_mutexattr_init(&attr));

#ifndef NDEBUG
	/* In normal builds enable error checking, such as recursive usage. */
	CHECK_ERR(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
#endif
	if (pshared)
		CHECK_ERR(pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED));

	CHECK_ERR(pthread_mutex_init(&mtx->lock, &attr));
	CHECK_ERR(pthread_mutexattr_destroy(&attr));
}

void mutex_init(struct mutex *mtx)
{
	__mutex_init(mtx, /*pshared=*/false);
}

void mutex_init_pshared(struct mutex *mtx)
{
	__mutex_init(mtx, /*pshared=*/true);
}

void mutex_destroy(struct mutex *mtx)
{
	CHECK_ERR(pthread_mutex_destroy(&mtx->lock));
}

void mutex_lock(struct mutex *mtx)
{
	CHECK_ERR(pthread_mutex_lock(&mtx->lock));
}

void mutex_unlock(struct mutex *mtx)
{
	CHECK_ERR(pthread_mutex_unlock(&mtx->lock));
}

bool mutex_trylock(struct mutex *mtx)
{
	int ret = pthread_mutex_trylock(&mtx->lock);

	if (ret == 0)
		return true; /* Lock acquired. */

	if (ret == EBUSY)
		return false; /* Lock busy. */

	/* Print error. */
	CHECK_ERR(ret);
	return false;
}

static void __cond_init(struct cond *cnd, bool pshared)
{
	pthread_condattr_t attr;

	CHECK_ERR(pthread_condattr_init(&attr));
	if (pshared)
		CHECK_ERR(pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED));

	CHECK_ERR(pthread_cond_init(&cnd->cond, &attr));
	CHECK_ERR(pthread_condattr_destroy(&attr));
}

void cond_init(struct cond *cnd)
{
	__cond_init(cnd, /*pshared=*/false);
}

void cond_init_pshared(struct cond *cnd)
{
	__cond_init(cnd, /*pshared=*/true);
}

void cond_destroy(struct cond *cnd)
{
	CHECK_ERR(pthread_cond_destroy(&cnd->cond));
}

void cond_wait(struct cond *cnd, struct mutex *mtx)
{
	CHECK_ERR(pthread_cond_wait(&cnd->cond, &mtx->lock));
}

void cond_signal(struct cond *cnd)
{
	CHECK_ERR(pthread_cond_signal(&cnd->cond));
}

void cond_broadcast(struct cond *cnd)
{
	CHECK_ERR(pthread_cond_broadcast(&cnd->cond));
}

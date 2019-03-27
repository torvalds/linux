/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2007-2009 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * 6.1 : Mutual Exclusion and Synchronisation
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#define	_COMPONENT	ACPI_OS_SERVICES
ACPI_MODULE_NAME("SYNCH")

static MALLOC_DEFINE(M_ACPISEM, "acpisem", "ACPI semaphore");

/*
 * Convert milliseconds to ticks.
 */
static int
timeout2hz(UINT16 Timeout)
{
	struct timeval		tv;

	tv.tv_sec = (time_t)(Timeout / 1000);
	tv.tv_usec = (suseconds_t)(Timeout % 1000) * 1000;

	return (tvtohz(&tv));
}

/*
 * ACPI_SEMAPHORE
 */
struct acpi_sema {
	struct mtx	as_lock;
	char		as_name[32];
	struct cv	as_cv;
	UINT32		as_maxunits;
	UINT32		as_units;
	int		as_waiters;
	int		as_reset;
};

ACPI_STATUS
AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits,
    ACPI_SEMAPHORE *OutHandle)
{
	struct acpi_sema	*as;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (OutHandle == NULL || MaxUnits == 0 || InitialUnits > MaxUnits)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	if ((as = malloc(sizeof(*as), M_ACPISEM, M_NOWAIT | M_ZERO)) == NULL)
		return_ACPI_STATUS (AE_NO_MEMORY);

	snprintf(as->as_name, sizeof(as->as_name), "ACPI sema (%p)", as);
	mtx_init(&as->as_lock, as->as_name, NULL, MTX_DEF);
	cv_init(&as->as_cv, as->as_name);
	as->as_maxunits = MaxUnits;
	as->as_units = InitialUnits;

	*OutHandle = (ACPI_SEMAPHORE)as;

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "created %s, max %u, initial %u\n",
	    as->as_name, MaxUnits, InitialUnits));

	return_ACPI_STATUS (AE_OK);
}

ACPI_STATUS
AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
	struct acpi_sema	*as = (struct acpi_sema *)Handle;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (as == NULL)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	mtx_lock(&as->as_lock);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "delete %s\n", as->as_name));

	if (as->as_waiters > 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
		    "reset %s, units %u, waiters %d\n",
		    as->as_name, as->as_units, as->as_waiters));
		as->as_reset = 1;
		cv_broadcast(&as->as_cv);
		while (as->as_waiters > 0) {
			if (mtx_sleep(&as->as_reset, &as->as_lock,
			    PCATCH, "acsrst", hz) == EINTR) {
				ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
				    "failed to reset %s, waiters %d\n",
				    as->as_name, as->as_waiters));
				mtx_unlock(&as->as_lock);
				return_ACPI_STATUS (AE_ERROR);
			}
			ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
			    "wait %s, units %u, waiters %d\n",
			    as->as_name, as->as_units, as->as_waiters));
		}
	}

	mtx_unlock(&as->as_lock);

	mtx_destroy(&as->as_lock);
	cv_destroy(&as->as_cv);
	free(as, M_ACPISEM);

	return_ACPI_STATUS (AE_OK);
}

#define	ACPISEM_AVAIL(s, u)	((s)->as_units >= (u))

ACPI_STATUS
AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout)
{
	struct acpi_sema	*as = (struct acpi_sema *)Handle;
	int			error, prevtick, slptick, tmo;
	ACPI_STATUS		status = AE_OK;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (as == NULL || Units == 0)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	mtx_lock(&as->as_lock);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
	    "get %u unit(s) from %s, units %u, waiters %d, timeout %u\n",
	    Units, as->as_name, as->as_units, as->as_waiters, Timeout));

	if (as->as_maxunits != ACPI_NO_UNIT_LIMIT && as->as_maxunits < Units) {
		mtx_unlock(&as->as_lock);
		return_ACPI_STATUS (AE_LIMIT);
	}

	switch (Timeout) {
	case ACPI_DO_NOT_WAIT:
		if (!ACPISEM_AVAIL(as, Units))
			status = AE_TIME;
		break;
	case ACPI_WAIT_FOREVER:
		while (!ACPISEM_AVAIL(as, Units)) {
			as->as_waiters++;
			error = cv_wait_sig(&as->as_cv, &as->as_lock);
			as->as_waiters--;
			if (error == EINTR || as->as_reset) {
				status = AE_ERROR;
				break;
			}
		}
		break;
	default:
		if (cold) {
			/*
			 * Just spin polling the semaphore once a
			 * millisecond.
			 */
			while (!ACPISEM_AVAIL(as, Units)) {
				if (Timeout == 0) {
					status = AE_TIME;
					break;
				}
				Timeout--;
				mtx_unlock(&as->as_lock);
				DELAY(1000);
				mtx_lock(&as->as_lock);
			}
			break;
		}
		tmo = timeout2hz(Timeout);
		while (!ACPISEM_AVAIL(as, Units)) {
			prevtick = ticks;
			as->as_waiters++;
			error = cv_timedwait_sig(&as->as_cv, &as->as_lock, tmo);
			as->as_waiters--;
			if (error == EINTR || as->as_reset) {
				status = AE_ERROR;
				break;
			}
			if (ACPISEM_AVAIL(as, Units))
				break;
			slptick = ticks - prevtick;
			if (slptick >= tmo || slptick < 0) {
				status = AE_TIME;
				break;
			}
			tmo -= slptick;
		}
	}
	if (ACPI_SUCCESS(status))
		as->as_units -= Units;

	mtx_unlock(&as->as_lock);

	return_ACPI_STATUS (status);
}

ACPI_STATUS
AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)
{
	struct acpi_sema	*as = (struct acpi_sema *)Handle;
	UINT32			i;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (as == NULL || Units == 0)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	mtx_lock(&as->as_lock);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
	    "return %u units to %s, units %u, waiters %d\n",
	    Units, as->as_name, as->as_units, as->as_waiters));

	if (as->as_maxunits != ACPI_NO_UNIT_LIMIT &&
	    (as->as_maxunits < Units ||
	    as->as_maxunits - Units < as->as_units)) {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
		    "exceeded max units %u\n", as->as_maxunits));
		mtx_unlock(&as->as_lock);
		return_ACPI_STATUS (AE_LIMIT);
	}

	as->as_units += Units;
	if (as->as_waiters > 0 && ACPISEM_AVAIL(as, Units))
		for (i = 0; i < Units; i++)
			cv_signal(&as->as_cv);

	mtx_unlock(&as->as_lock);

	return_ACPI_STATUS (AE_OK);
}

#undef ACPISEM_AVAIL

/*
 * ACPI_MUTEX
 */
struct acpi_mutex {
	struct mtx	am_lock;
	char		am_name[32];
	struct thread	*am_owner;
	int		am_nested;
	int		am_waiters;
	int		am_reset;
};

ACPI_STATUS
AcpiOsCreateMutex(ACPI_MUTEX *OutHandle)
{
	struct acpi_mutex	*am;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (OutHandle == NULL)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	if ((am = malloc(sizeof(*am), M_ACPISEM, M_NOWAIT | M_ZERO)) == NULL)
		return_ACPI_STATUS (AE_NO_MEMORY);

	snprintf(am->am_name, sizeof(am->am_name), "ACPI mutex (%p)", am);
	mtx_init(&am->am_lock, am->am_name, NULL, MTX_DEF);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "created %s\n", am->am_name));

	*OutHandle = (ACPI_MUTEX)am;

	return_ACPI_STATUS (AE_OK);
}

#define	ACPIMTX_AVAIL(m)	((m)->am_owner == NULL)
#define	ACPIMTX_OWNED(m)	((m)->am_owner == curthread)

void
AcpiOsDeleteMutex(ACPI_MUTEX Handle)
{
	struct acpi_mutex	*am = (struct acpi_mutex *)Handle;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (am == NULL) {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "cannot delete null mutex\n"));
		return_VOID;
	}

	mtx_lock(&am->am_lock);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "delete %s\n", am->am_name));

	if (am->am_waiters > 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
		    "reset %s, owner %p\n", am->am_name, am->am_owner));
		am->am_reset = 1;
		wakeup(am);
		while (am->am_waiters > 0) {
			if (mtx_sleep(&am->am_reset, &am->am_lock,
			    PCATCH, "acmrst", hz) == EINTR) {
				ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
				    "failed to reset %s, waiters %d\n",
				    am->am_name, am->am_waiters));
				mtx_unlock(&am->am_lock);
				return_VOID;
			}
			if (ACPIMTX_AVAIL(am))
				ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
				    "wait %s, waiters %d\n",
				    am->am_name, am->am_waiters));
			else
				ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
				    "wait %s, owner %p, waiters %d\n",
				    am->am_name, am->am_owner, am->am_waiters));
		}
	}

	mtx_unlock(&am->am_lock);

	mtx_destroy(&am->am_lock);
	free(am, M_ACPISEM);
}

ACPI_STATUS
AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout)
{
	struct acpi_mutex	*am = (struct acpi_mutex *)Handle;
	int			error, prevtick, slptick, tmo;
	ACPI_STATUS		status = AE_OK;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (am == NULL)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	mtx_lock(&am->am_lock);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "acquire %s\n", am->am_name));

	if (ACPIMTX_OWNED(am)) {
		am->am_nested++;
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
		    "acquire nested %s, depth %d\n",
		    am->am_name, am->am_nested));
		mtx_unlock(&am->am_lock);
		return_ACPI_STATUS (AE_OK);
	}

	switch (Timeout) {
	case ACPI_DO_NOT_WAIT:
		if (!ACPIMTX_AVAIL(am))
			status = AE_TIME;
		break;
	case ACPI_WAIT_FOREVER:
		while (!ACPIMTX_AVAIL(am)) {
			am->am_waiters++;
			error = mtx_sleep(am, &am->am_lock, PCATCH, "acmtx", 0);
			am->am_waiters--;
			if (error == EINTR || am->am_reset) {
				status = AE_ERROR;
				break;
			}
		}
		break;
	default:
		if (cold) {
			/*
			 * Just spin polling the mutex once a
			 * millisecond.
			 */
			while (!ACPIMTX_AVAIL(am)) {
				if (Timeout == 0) {
					status = AE_TIME;
					break;
				}
				Timeout--;
				mtx_unlock(&am->am_lock);
				DELAY(1000);
				mtx_lock(&am->am_lock);
			}
			break;
		}
		tmo = timeout2hz(Timeout);
		while (!ACPIMTX_AVAIL(am)) {
			prevtick = ticks;
			am->am_waiters++;
			error = mtx_sleep(am, &am->am_lock, PCATCH,
			    "acmtx", tmo);
			am->am_waiters--;
			if (error == EINTR || am->am_reset) {
				status = AE_ERROR;
				break;
			}
			if (ACPIMTX_AVAIL(am))
				break;
			slptick = ticks - prevtick;
			if (slptick >= tmo || slptick < 0) {
				status = AE_TIME;
				break;
			}
			tmo -= slptick;
		}
	}
	if (ACPI_SUCCESS(status))
		am->am_owner = curthread;

	mtx_unlock(&am->am_lock);

	return_ACPI_STATUS (status);
}

void
AcpiOsReleaseMutex(ACPI_MUTEX Handle)
{
	struct acpi_mutex	*am = (struct acpi_mutex *)Handle;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (am == NULL) {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
		    "cannot release null mutex\n"));
		return_VOID;
	}

	mtx_lock(&am->am_lock);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "release %s\n", am->am_name));

	if (ACPIMTX_OWNED(am)) {
		if (am->am_nested > 0) {
			ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
			    "release nested %s, depth %d\n",
			    am->am_name, am->am_nested));
			am->am_nested--;
		} else
			am->am_owner = NULL;
	} else {
		if (ACPIMTX_AVAIL(am))
			ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
			    "release already available %s\n", am->am_name));
		else
			ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
			    "release unowned %s from %p, depth %d\n",
			    am->am_name, am->am_owner, am->am_nested));
	}
	if (am->am_waiters > 0 && ACPIMTX_AVAIL(am))
		wakeup_one(am);

	mtx_unlock(&am->am_lock);
}

#undef ACPIMTX_AVAIL
#undef ACPIMTX_OWNED

/*
 * ACPI_SPINLOCK
 */
struct acpi_spinlock {
	struct mtx	al_lock;
	char		al_name[32];
	int		al_nested;
};

ACPI_STATUS
AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
	struct acpi_spinlock	*al;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (OutHandle == NULL)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	if ((al = malloc(sizeof(*al), M_ACPISEM, M_NOWAIT | M_ZERO)) == NULL)
		return_ACPI_STATUS (AE_NO_MEMORY);

#ifdef ACPI_DEBUG
	if (OutHandle == &AcpiGbl_GpeLock)
		snprintf(al->al_name, sizeof(al->al_name), "ACPI lock (GPE)");
	else if (OutHandle == &AcpiGbl_HardwareLock)
		snprintf(al->al_name, sizeof(al->al_name), "ACPI lock (HW)");
	else
#endif
	snprintf(al->al_name, sizeof(al->al_name), "ACPI lock (%p)", al);
	mtx_init(&al->al_lock, al->al_name, NULL, MTX_SPIN);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "created %s\n", al->al_name));

	*OutHandle = (ACPI_SPINLOCK)al;

	return_ACPI_STATUS (AE_OK);
}

void
AcpiOsDeleteLock(ACPI_SPINLOCK Handle)
{
	struct acpi_spinlock	*al = (struct acpi_spinlock *)Handle;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (al == NULL) {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
		    "cannot delete null spinlock\n"));
		return_VOID;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "delete %s\n", al->al_name));

	mtx_destroy(&al->al_lock);
	free(al, M_ACPISEM);
}

ACPI_CPU_FLAGS
AcpiOsAcquireLock(ACPI_SPINLOCK Handle)
{
	struct acpi_spinlock	*al = (struct acpi_spinlock *)Handle;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (al == NULL) {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
		    "cannot acquire null spinlock\n"));
		return (0);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "acquire %s\n", al->al_name));

	if (mtx_owned(&al->al_lock)) {
		al->al_nested++;
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
		    "acquire nested %s, depth %d\n",
		    al->al_name, al->al_nested));
	} else
		mtx_lock_spin(&al->al_lock);

	return (0);
}

void
AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags)
{
	struct acpi_spinlock	*al = (struct acpi_spinlock *)Handle;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (al == NULL) {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
		    "cannot release null spinlock\n"));
		return_VOID;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "release %s\n", al->al_name));

	if (mtx_owned(&al->al_lock)) {
		if (al->al_nested > 0) {
			ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
			    "release nested %s, depth %d\n",
			    al->al_name, al->al_nested));
			al->al_nested--;
		} else
			mtx_unlock_spin(&al->al_lock);
	} else
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
		    "cannot release unowned %s\n", al->al_name));
}

/* Section 5.2.10.1: global lock acquire/release functions */

/*
 * Acquire the global lock.  If busy, set the pending bit.  The caller
 * will wait for notification from the BIOS that the lock is available
 * and then attempt to acquire it again.
 */
int
acpi_acquire_global_lock(volatile uint32_t *lock)
{
	uint32_t	new, old;

	do {
		old = *lock;
		new = (old & ~ACPI_GLOCK_PENDING) | ACPI_GLOCK_OWNED;
		if ((old & ACPI_GLOCK_OWNED) != 0)
			new |= ACPI_GLOCK_PENDING;
	} while (atomic_cmpset_32(lock, old, new) == 0);

	return ((new & ACPI_GLOCK_PENDING) == 0);
}

/*
 * Release the global lock, returning whether there is a waiter pending.
 * If the BIOS set the pending bit, OSPM must notify the BIOS when it
 * releases the lock.
 */
int
acpi_release_global_lock(volatile uint32_t *lock)
{
	uint32_t	new, old;

	do {
		old = *lock;
		new = old & ~(ACPI_GLOCK_PENDING | ACPI_GLOCK_OWNED);
	} while (atomic_cmpset_32(lock, old, new) == 0);

	return ((old & ACPI_GLOCK_PENDING) != 0);
}

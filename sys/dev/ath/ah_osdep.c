/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#include "opt_ah.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>

#include <machine/stdarg.h>

#include <net/ethernet.h>		/* XXX for ether_sprintf */

#include <dev/ath/ath_hal/ah.h>
#include <dev/ath/ath_hal/ah_debug.h>

/*
 * WiSoC boards overload the bus tag with information about the
 * board layout.  We must extract the bus space tag from that
 * indirect structure.  For everyone else the tag is passed in
 * directly.
 * XXX cache indirect ref privately
 */
#ifdef AH_SUPPORT_AR5312
#define	BUSTAG(ah) \
	((bus_space_tag_t) ((struct ar531x_config *)((ah)->ah_st))->tag)
#else
#define	BUSTAG(ah)	((ah)->ah_st)
#endif

/*
 * This lock is used to seralise register access for chips which have
 * problems w/ SMP CPUs issuing concurrent PCI transactions.
 *
 * XXX This is a global lock for now; it should be pushed to
 * a per-device lock in some platform-independent fashion.
 */
struct mtx ah_regser_mtx;
MTX_SYSINIT(ah_regser, &ah_regser_mtx, "Atheros register access mutex",
    MTX_SPIN);

extern	void ath_hal_printf(struct ath_hal *, const char*, ...)
		__printflike(2,3);
extern	void ath_hal_vprintf(struct ath_hal *, const char*, __va_list)
		__printflike(2, 0);
extern	const char* ath_hal_ether_sprintf(const u_int8_t *mac);
extern	void *ath_hal_malloc(size_t);
extern	void ath_hal_free(void *);
#ifdef AH_ASSERT
extern	void ath_hal_assert_failed(const char* filename,
		int lineno, const char* msg);
#endif
#ifdef AH_DEBUG
extern	void DO_HALDEBUG(struct ath_hal *ah, u_int mask, const char* fmt, ...);
#endif /* AH_DEBUG */

/* NB: put this here instead of the driver to avoid circular references */
SYSCTL_NODE(_hw, OID_AUTO, ath, CTLFLAG_RD, 0, "Atheros driver parameters");
static SYSCTL_NODE(_hw_ath, OID_AUTO, hal, CTLFLAG_RD, 0,
    "Atheros HAL parameters");

#ifdef AH_DEBUG
int ath_hal_debug = 0;
SYSCTL_INT(_hw_ath_hal, OID_AUTO, debug, CTLFLAG_RWTUN, &ath_hal_debug,
    0, "Atheros HAL debugging printfs");
#endif /* AH_DEBUG */

static MALLOC_DEFINE(M_ATH_HAL, "ath_hal", "ath hal data");

void*
ath_hal_malloc(size_t size)
{
	return malloc(size, M_ATH_HAL, M_NOWAIT | M_ZERO);
}

void
ath_hal_free(void* p)
{
	free(p, M_ATH_HAL);
}

void
ath_hal_vprintf(struct ath_hal *ah, const char* fmt, va_list ap)
{
	vprintf(fmt, ap);
}

void
ath_hal_printf(struct ath_hal *ah, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ath_hal_vprintf(ah, fmt, ap);
	va_end(ap);
}

const char*
ath_hal_ether_sprintf(const u_int8_t *mac)
{
	return ether_sprintf(mac);
}

#ifdef AH_DEBUG

/*
 * XXX This is highly relevant only for the AR5416 and later
 * PCI/PCIe NICs.  It'll need adjustment for other hardware
 * variations.
 */
static int
ath_hal_reg_whilst_asleep(struct ath_hal *ah, uint32_t reg)
{

	if (reg >= 0x4000 && reg < 0x5000)
		return (1);
	if (reg >= 0x6000 && reg < 0x7000)
		return (1);
	if (reg >= 0x7000 && reg < 0x8000)
		return (1);
	return (0);
}

void
DO_HALDEBUG(struct ath_hal *ah, u_int mask, const char* fmt, ...)
{
	if ((mask == HAL_DEBUG_UNMASKABLE) ||
	    (ah != NULL && ah->ah_config.ah_debug & mask) ||
	    (ath_hal_debug & mask)) {
		__va_list ap;
		va_start(ap, fmt);
		ath_hal_vprintf(ah, fmt, ap);
		va_end(ap);
	}
}
#undef	HAL_DEBUG_UNMASKABLE
#endif /* AH_DEBUG */

#ifdef AH_DEBUG_ALQ
/*
 * ALQ register tracing support.
 *
 * Setting hw.ath.hal.alq=1 enables tracing of all register reads and
 * writes to the file /tmp/ath_hal.log.  The file format is a simple
 * fixed-size array of records.  When done logging set hw.ath.hal.alq=0
 * and then decode the file with the arcode program (that is part of the
 * HAL).  If you start+stop tracing the data will be appended to an
 * existing file.
 *
 * NB: doesn't handle multiple devices properly; only one DEVICE record
 *     is emitted and the different devices are not identified.
 */
#include <sys/alq.h>
#include <sys/pcpu.h>
#include <dev/ath/ath_hal/ah_decode.h>

static	struct alq *ath_hal_alq;
static	int ath_hal_alq_emitdev;	/* need to emit DEVICE record */
static	u_int ath_hal_alq_lost;		/* count of lost records */
static	char ath_hal_logfile[MAXPATHLEN] = "/tmp/ath_hal.log";

SYSCTL_STRING(_hw_ath_hal, OID_AUTO, alq_logfile, CTLFLAG_RW,
    &ath_hal_logfile, sizeof(kernelname), "Name of ALQ logfile");

static	u_int ath_hal_alq_qsize = 64*1024;

static int
ath_hal_setlogging(int enable)
{
	int error;

	if (enable) {
		error = alq_open(&ath_hal_alq, ath_hal_logfile,
			curthread->td_ucred, ALQ_DEFAULT_CMODE,
			sizeof (struct athregrec), ath_hal_alq_qsize);
		ath_hal_alq_lost = 0;
		ath_hal_alq_emitdev = 1;
		printf("ath_hal: logging to %s enabled\n",
			ath_hal_logfile);
	} else {
		if (ath_hal_alq)
			alq_close(ath_hal_alq);
		ath_hal_alq = NULL;
		printf("ath_hal: logging disabled\n");
		error = 0;
	}
	return (error);
}

static int
sysctl_hw_ath_hal_log(SYSCTL_HANDLER_ARGS)
{
	int error, enable;

	enable = (ath_hal_alq != NULL);
        error = sysctl_handle_int(oidp, &enable, 0, req);
        if (error || !req->newptr)
                return (error);
	else
		return (ath_hal_setlogging(enable));
}
SYSCTL_PROC(_hw_ath_hal, OID_AUTO, alq, CTLTYPE_INT|CTLFLAG_RW,
	0, 0, sysctl_hw_ath_hal_log, "I", "Enable HAL register logging");
SYSCTL_INT(_hw_ath_hal, OID_AUTO, alq_size, CTLFLAG_RW,
	&ath_hal_alq_qsize, 0, "In-memory log size (#records)");
SYSCTL_INT(_hw_ath_hal, OID_AUTO, alq_lost, CTLFLAG_RW,
	&ath_hal_alq_lost, 0, "Register operations not logged");

static struct ale *
ath_hal_alq_get(struct ath_hal *ah)
{
	struct ale *ale;

	if (ath_hal_alq_emitdev) {
		ale = alq_get(ath_hal_alq, ALQ_NOWAIT);
		if (ale) {
			struct athregrec *r =
				(struct athregrec *) ale->ae_data;
			r->op = OP_DEVICE;
			r->reg = 0;
			r->val = ah->ah_devid;
			alq_post(ath_hal_alq, ale);
			ath_hal_alq_emitdev = 0;
		} else
			ath_hal_alq_lost++;
	}
	ale = alq_get(ath_hal_alq, ALQ_NOWAIT);
	if (!ale)
		ath_hal_alq_lost++;
	return ale;
}

void
ath_hal_reg_write(struct ath_hal *ah, u_int32_t reg, u_int32_t val)
{
	bus_space_tag_t tag = BUSTAG(ah);
	bus_space_handle_t h = ah->ah_sh;

#ifdef	AH_DEBUG
	/* Debug - complain if we haven't fully waken things up */
	if (! ath_hal_reg_whilst_asleep(ah, reg) &&
	    ah->ah_powerMode != HAL_PM_AWAKE) {
		ath_hal_printf(ah, "%s: reg=0x%08x, val=0x%08x, pm=%d\n",
		    __func__, reg, val, ah->ah_powerMode);
	}
#endif

	if (ath_hal_alq) {
		struct ale *ale = ath_hal_alq_get(ah);
		if (ale) {
			struct athregrec *r = (struct athregrec *) ale->ae_data;
			r->threadid = curthread->td_tid;
			r->op = OP_WRITE;
			r->reg = reg;
			r->val = val;
			alq_post(ath_hal_alq, ale);
		}
	}
	if (ah->ah_config.ah_serialise_reg_war)
		mtx_lock_spin(&ah_regser_mtx);
	bus_space_write_4(tag, h, reg, val);
	OS_BUS_BARRIER_REG(ah, reg, OS_BUS_BARRIER_WRITE);
	if (ah->ah_config.ah_serialise_reg_war)
		mtx_unlock_spin(&ah_regser_mtx);
}

u_int32_t
ath_hal_reg_read(struct ath_hal *ah, u_int32_t reg)
{
	bus_space_tag_t tag = BUSTAG(ah);
	bus_space_handle_t h = ah->ah_sh;
	u_int32_t val;

#ifdef	AH_DEBUG
	/* Debug - complain if we haven't fully waken things up */
	if (! ath_hal_reg_whilst_asleep(ah, reg) &&
	    ah->ah_powerMode != HAL_PM_AWAKE) {
		ath_hal_printf(ah, "%s: reg=0x%08x, pm=%d\n",
		    __func__, reg, ah->ah_powerMode);
	}
#endif

	if (ah->ah_config.ah_serialise_reg_war)
		mtx_lock_spin(&ah_regser_mtx);
	OS_BUS_BARRIER_REG(ah, reg, OS_BUS_BARRIER_READ);
	val = bus_space_read_4(tag, h, reg);
	if (ah->ah_config.ah_serialise_reg_war)
		mtx_unlock_spin(&ah_regser_mtx);
	if (ath_hal_alq) {
		struct ale *ale = ath_hal_alq_get(ah);
		if (ale) {
			struct athregrec *r = (struct athregrec *) ale->ae_data;
			r->threadid = curthread->td_tid;
			r->op = OP_READ;
			r->reg = reg;
			r->val = val;
			alq_post(ath_hal_alq, ale);
		}
	}
	return val;
}

void
OS_MARK(struct ath_hal *ah, u_int id, u_int32_t v)
{
	if (ath_hal_alq) {
		struct ale *ale = ath_hal_alq_get(ah);
		if (ale) {
			struct athregrec *r = (struct athregrec *) ale->ae_data;
			r->threadid = curthread->td_tid;
			r->op = OP_MARK;
			r->reg = id;
			r->val = v;
			alq_post(ath_hal_alq, ale);
		}
	}
}
#else /* AH_DEBUG_ALQ */

/*
 * Memory-mapped device register read/write.  These are here
 * as routines when debugging support is enabled and/or when
 * explicitly configured to use function calls.  The latter is
 * for architectures that might need to do something before
 * referencing memory (e.g. remap an i/o window).
 *
 * NB: see the comments in ah_osdep.h about byte-swapping register
 *     reads and writes to understand what's going on below.
 */

void
ath_hal_reg_write(struct ath_hal *ah, u_int32_t reg, u_int32_t val)
{
	bus_space_tag_t tag = BUSTAG(ah);
	bus_space_handle_t h = ah->ah_sh;

#ifdef	AH_DEBUG
	/* Debug - complain if we haven't fully waken things up */
	if (! ath_hal_reg_whilst_asleep(ah, reg) &&
	    ah->ah_powerMode != HAL_PM_AWAKE) {
		ath_hal_printf(ah, "%s: reg=0x%08x, val=0x%08x, pm=%d\n",
		    __func__, reg, val, ah->ah_powerMode);
	}
#endif

	if (ah->ah_config.ah_serialise_reg_war)
		mtx_lock_spin(&ah_regser_mtx);
	bus_space_write_4(tag, h, reg, val);
	OS_BUS_BARRIER_REG(ah, reg, OS_BUS_BARRIER_WRITE);
	if (ah->ah_config.ah_serialise_reg_war)
		mtx_unlock_spin(&ah_regser_mtx);
}

u_int32_t
ath_hal_reg_read(struct ath_hal *ah, u_int32_t reg)
{
	bus_space_tag_t tag = BUSTAG(ah);
	bus_space_handle_t h = ah->ah_sh;
	u_int32_t val;

#ifdef	AH_DEBUG
	/* Debug - complain if we haven't fully waken things up */
	if (! ath_hal_reg_whilst_asleep(ah, reg) &&
	    ah->ah_powerMode != HAL_PM_AWAKE) {
		ath_hal_printf(ah, "%s: reg=0x%08x, pm=%d\n",
		    __func__, reg, ah->ah_powerMode);
	}
#endif

	if (ah->ah_config.ah_serialise_reg_war)
		mtx_lock_spin(&ah_regser_mtx);
	OS_BUS_BARRIER_REG(ah, reg, OS_BUS_BARRIER_READ);
	val = bus_space_read_4(tag, h, reg);
	if (ah->ah_config.ah_serialise_reg_war)
		mtx_unlock_spin(&ah_regser_mtx);
	return val;
}
#endif /* AH_DEBUG_ALQ */

#ifdef AH_ASSERT
void
ath_hal_assert_failed(const char* filename, int lineno, const char *msg)
{
	printf("Atheros HAL assertion failure: %s: line %u: %s\n",
		filename, lineno, msg);
	panic("ath_hal_assert");
}
#endif /* AH_ASSERT */

static int
ath_hal_modevent(module_t mod __unused, int type, void *data __unused)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		printf("[ath_hal] loaded\n");
		break;

	case MOD_UNLOAD:
		printf("[ath_hal] unloaded\n");
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		error = EOPNOTSUPP;
		break;

	}
	return (error);
}

DEV_MODULE(ath_hal, ath_hal_modevent, NULL);
MODULE_VERSION(ath_hal, 1);
#if	defined(AH_DEBUG_ALQ)
MODULE_DEPEND(ath_hal, alq, 1, 1, 1);
#endif

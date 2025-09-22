/*	$OpenBSD: apm.c,v 1.133 2024/10/07 01:31:22 jsg Exp $	*/

/*-
 * Copyright (c) 1998-2001 Michael Shalayeff. All rights reserved.
 * Copyright (c) 1995 John T. Kohl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the authors nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "apm.h"

#if NAPM > 1
#error only one APM device may be configured
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/rwlock.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/event.h>

#include <machine/conf.h>
#include <machine/cpufunc.h>
#include <machine/gdt.h>

#include <dev/isa/isareg.h>
#include <dev/wscons/wsdisplayvar.h>

#include <machine/acpiapm.h>
#include <machine/biosvar.h>
#include <machine/apmvar.h>

#include "wsdisplay.h"

#if defined(APMDEBUG)
#define DPRINTF(x)	printf x
#else
#define	DPRINTF(x)	/**/
#endif

struct cfdriver apm_cd = {
	NULL, "apm", DV_DULL
};

struct apm_softc {
	struct device sc_dev;
	struct klist sc_note;
	int	sc_flags;
	int	batt_life;
	int	be_batt;
	struct proc *sc_thread;
	struct rwlock sc_lock;
};
#define	SCFLAG_OREAD	0x0000001
#define	SCFLAG_OWRITE	0x0000002
#define	SCFLAG_OPEN	(SCFLAG_OREAD|SCFLAG_OWRITE)

int	apmprobe(struct device *, void *, void *);
void	apmattach(struct device *, struct device *, void *);

const struct cfattach apm_ca = {
	sizeof(struct apm_softc), apmprobe, apmattach
};

void	filt_apmrdetach(struct knote *kn);
int	filt_apmread(struct knote *kn, long hint);

const struct filterops apmread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_apmrdetach,
	.f_event	= filt_apmread,
};

#define	APM_RESUME_HOLDOFF	3

/*
 * Flags to control kernel display
 *	SCFLAG_NOPRINT:		do not output APM power messages due to
 *				a power change event.
 *
 *	SCFLAG_PCTPRINT:	do not output APM power messages due to
 *				to a power change event unless the battery
 *				percentage changes.
 */
#define SCFLAG_NOPRINT	0x0008000
#define SCFLAG_PCTPRINT	0x0004000
#define SCFLAG_PRINT	(SCFLAG_NOPRINT|SCFLAG_PCTPRINT)

#define	APMUNIT(dev)	(minor(dev)&0xf0)
#define	APMDEV(dev)	(minor(dev)&0x0f)
#define APMDEV_NORMAL	0
#define APMDEV_CTL	8

int	apm_standbys;
int	apm_lidclose;
int	apm_userstandbys;
int	apm_suspends;
int	apm_resumes;
int	apm_battlow;
int	apm_evindex;
int	apm_error;
int	apm_op_inprog;

u_int	apm_flags;
u_char	apm_majver;
u_char	apm_minver;
int	apm_attached = 0;
static int	apm_slow_called = 0;

struct {
	u_int32_t entry;
	u_int16_t seg;
	u_int16_t pad;
} apm_ep;

struct apmregs {
	u_int32_t	ax;
	u_int32_t	bx;
	u_int32_t	cx;
	u_int32_t	dx;
};

int  apmcall(u_int, u_int, struct apmregs *);
int  apm_handle_event(struct apm_softc *, struct apmregs *);
void apm_set_ver(struct apm_softc *);
int  apm_periodic_check(struct apm_softc *);
void apm_thread_create(void *v);
void apm_thread(void *);
void apm_disconnect(struct apm_softc *);
void apm_perror(const char *, struct apmregs *);
void apm_powmgt_enable(int onoff);
void apm_powmgt_engage(int onoff, u_int devid);
/* void apm_devpowmgt_enable(int onoff, u_int devid); */
int  apm_record_event(struct apm_softc *sc, u_int type);
const char *apm_err_translate(int code);

#define	apm_get_powstat(r) apmcall(APM_POWER_STATUS, APM_DEV_ALLDEVS, r)
void	apm_suspend(int);
void	apm_resume(struct apm_softc *, struct apmregs *);
void	apm_cpu_slow(void);

static int __inline
apm_get_event(struct apmregs *r)
{
	int rv;

	bzero(r, sizeof(*r));
	rv = apmcall(APM_GET_PM_EVENT, 0, r);
	return rv;
}

const char *
apm_err_translate(int code)
{
	switch (code) {
	case APM_ERR_PM_DISABLED:
		return "power management disabled";
	case APM_ERR_REALALREADY:
		return "real mode interface already connected";
	case APM_ERR_NOTCONN:
		return "interface not connected";
	case APM_ERR_16ALREADY:
		return "16-bit interface already connected";
	case APM_ERR_16NOTSUPP:
		return "16-bit interface not supported";
	case APM_ERR_32ALREADY:
		return "32-bit interface already connected";
	case APM_ERR_32NOTSUPP:
		return "32-bit interface not supported";
	case APM_ERR_UNRECOG_DEV:
		return "unrecognized device ID";
	case APM_ERR_ERANGE:
		return "parameter out of range";
	case APM_ERR_NOTENGAGED:
		return "interface not engaged";
	case APM_ERR_UNABLE:
		return "unable to enter requested state";
	case APM_ERR_NOEVENTS:
		return "No pending events";
	case APM_ERR_NOT_PRESENT:
		return "No APM present";
	default:
		return "unknown error code?";
	}
}

int apmerrors = 0;

void
apm_perror(const char *str, struct apmregs *regs)
{
	printf("apm0: APM %s: %s (%d)\n", str,
	    apm_err_translate(APM_ERR_CODE(regs)),
	    APM_ERR_CODE(regs));
	delay(1000000);

	apmerrors++;
}

void
apm_suspend(int state)
{
	extern int perflevel;
	int s;

#if NWSDISPLAY > 0
	wsdisplay_suspend();
#endif /* NWSDISPLAY > 0 */
	stop_periodic_resettodr();
	config_suspend_all(DVACT_QUIESCE);
	bufq_quiesce();

	s = splhigh();
	intr_disable();
	cold = 2;
	config_suspend_all(DVACT_SUSPEND);
	suspend_randomness();

	/* XXX
	 * Flag to disk drivers that they should "power down" the disk
	 * when we get to DVACT_POWERDOWN.
	 */
	boothowto |= RB_POWERDOWN;
	config_suspend_all(DVACT_POWERDOWN);
	boothowto &= ~RB_POWERDOWN;

	/* Send machine to sleep */
	apm_set_powstate(APM_DEV_ALLDEVS, state);
	/* Wake up  */

	/* They say that some machines may require reinitializing the clocks */
	i8254_startclock();
	if (initclock_func == i8254_initclocks)
		rtcstart();		/* in i8254 mode, rtc is profclock */
	inittodr(gettime());

	clockintr_cpu_init(NULL);
	clockintr_trigger();

	config_suspend_all(DVACT_RESUME);
	cold = 0;
	intr_enable();
	splx(s);

	resume_randomness(NULL, 0);	/* force RNG upper level reseed */
	bufq_restart();

	config_suspend_all(DVACT_WAKEUP);
	start_periodic_resettodr();

#if NWSDISPLAY > 0
	wsdisplay_resume();
#endif /* NWSDISPLAY > 0 */

	/* restore hw.setperf */
	if (cpu_setperf != NULL)
		cpu_setperf(perflevel);
}

void
apm_resume(struct apm_softc *sc, struct apmregs *regs)
{

	apm_resumes = APM_RESUME_HOLDOFF;

	/* lower bit in cx means pccard was powered down */

	apm_record_event(sc, regs->bx);
}

int
apm_record_event(struct apm_softc *sc, u_int type)
{
	if (!apm_error && (sc->sc_flags & SCFLAG_OPEN) == 0) {
		DPRINTF(("apm_record_event: no user waiting\n"));
		apm_error++;
		return 1;
	}

	apm_evindex++;
	knote_locked(&sc->sc_note, APM_EVENT_COMPOSE(type, apm_evindex));
	return (0);
}

int
apm_handle_event(struct apm_softc *sc, struct apmregs *regs)
{
	struct apmregs nregs;
	int ret = 0;

	switch (regs->bx) {
	case APM_NOEVENT:
		ret++;
		break;

	case APM_USER_STANDBY_REQ:
		if (apm_resumes || apm_op_inprog)
			break;
		DPRINTF(("user wants STANDBY--fat chance\n"));
		apm_op_inprog++;
		if (apm_record_event(sc, regs->bx)) {
			DPRINTF(("standby ourselves\n"));
			apm_userstandbys++;
		}
		break;
	case APM_STANDBY_REQ:
		if (apm_resumes || apm_op_inprog)
			break;
		DPRINTF(("standby requested\n"));
		if (apm_standbys || apm_suspends) {
			DPRINTF(("premature standby\n"));
			apm_error++;
			ret++;
		}
		apm_op_inprog++;
		if (apm_record_event(sc, regs->bx)) {
			DPRINTF(("standby ourselves\n"));
			apm_standbys++;
		}
		break;
	case APM_USER_SUSPEND_REQ:
		if (apm_resumes || apm_op_inprog)
			break;
		DPRINTF(("user wants suspend--fat chance!\n"));
		apm_op_inprog++;
		if (apm_record_event(sc, regs->bx)) {
			DPRINTF(("suspend ourselves\n"));
			apm_suspends++;
		}
		break;
	case APM_SUSPEND_REQ:
		if (apm_resumes || apm_op_inprog)
			break;
		DPRINTF(("suspend requested\n"));
		if (apm_standbys || apm_suspends) {
			DPRINTF(("premature suspend\n"));
			apm_error++;
			ret++;
		}
		apm_op_inprog++;
		if (apm_record_event(sc, regs->bx)) {
			DPRINTF(("suspend ourselves\n"));
			apm_suspends++;
		}
		break;
	case APM_POWER_CHANGE:
		DPRINTF(("power status change\n"));
		apm_get_powstat(&nregs);
		apm_record_event(sc, regs->bx);
		break;
	case APM_NORMAL_RESUME:
		DPRINTF(("system resumed\n"));
		apm_resume(sc, regs);
		break;
	case APM_CRIT_RESUME:
		DPRINTF(("system resumed without us!\n"));
		apm_resume(sc, regs);
		break;
	case APM_SYS_STANDBY_RESUME:
		DPRINTF(("system standby resume\n"));
		apm_resume(sc, regs);
		break;
	case APM_UPDATE_TIME:
		DPRINTF(("update time, please\n"));
		apm_record_event(sc, regs->bx);
		break;
	case APM_CRIT_SUSPEND_REQ:
		DPRINTF(("suspend required immediately\n"));
		apm_record_event(sc, regs->bx);
		apm_suspend(APM_SYS_SUSPEND);
		break;
	case APM_BATTERY_LOW:
		DPRINTF(("Battery low!\n"));
		apm_battlow++;
		apm_record_event(sc, regs->bx);
		break;
	case APM_CAPABILITY_CHANGE:
		DPRINTF(("capability change\n"));
		if (apm_minver < 2) {
			DPRINTF(("adult event\n"));
		} else {
			if (apmcall(APM_GET_CAPABILITIES, APM_DEV_APM_BIOS,
			    &nregs) != 0) {
				apm_perror("get capabilities", &nregs);
			} else {
				apm_get_powstat(&nregs);
			}
		}
		break;
	default: {
#ifdef APMDEBUG
		char *p;
		switch (regs->bx >> 8) {
		case 0:	p = "reserved system";	break;
		case 1:	p = "reserved device";	break;
		case 2:	p = "OEM defined";	break;
		default:p = "reserved";		break;
		}
#endif
		DPRINTF(("apm_handle_event: %s event, code %d\n", p, regs->bx));
	    }
	}
	return ret;
}

int
apm_periodic_check(struct apm_softc *sc)
{
	struct apmregs regs;
	int ret = 0;

	if (apm_op_inprog)
		apm_set_powstate(APM_DEV_ALLDEVS, APM_LASTREQ_INPROG);

	while (1) {
		if (apm_get_event(&regs) != 0) {
			/* i think some bioses combine the error codes */
			if (!(APM_ERR_CODE(&regs) & APM_ERR_NOEVENTS))
				apm_perror("get event", &regs);
			break;
		}

		/* If the APM BIOS tells us to suspend, don't do it twice */
		if (regs.bx == APM_SUSPEND_REQ)
			apm_lidclose = 0;
		if (apm_handle_event(sc, &regs))
			break;
	}

	if (apm_error || APM_ERR_CODE(&regs) == APM_ERR_NOTCONN)
		ret = -1;

	if (apm_lidclose) {
		apm_lidclose = 0;
		/* Fake a suspend request */
		regs.bx = APM_SUSPEND_REQ;
		apm_handle_event(sc, &regs);
	}
	if (apm_suspends /*|| (apm_battlow && apm_userstandbys)*/) {
		apm_op_inprog = 0;
		apm_suspend(APM_SYS_SUSPEND);
	} else if (apm_standbys || apm_userstandbys) {
		apm_op_inprog = 0;
		apm_suspend(APM_SYS_STANDBY);
	}
	apm_suspends = apm_standbys = apm_battlow = apm_userstandbys = 0;
	apm_error = 0;

	if (apm_resumes)
		apm_resumes--;
	return (ret);
}

void
apm_powmgt_enable(int onoff)
{
	struct apmregs regs;

	bzero(&regs, sizeof(regs));
	regs.cx = onoff ? APM_MGT_ENABLE : APM_MGT_DISABLE;
	if (apmcall(APM_PWR_MGT_ENABLE,
	    (apm_minver? APM_DEV_APM_BIOS : APM_MGT_ALL), &regs) != 0)
		apm_perror("power management enable", &regs);
}

void
apm_powmgt_engage(int onoff, u_int dev)
{
	struct apmregs regs;

	if (apm_minver == 0)
		return;
	bzero(&regs, sizeof(regs));
	regs.cx = onoff ? APM_MGT_ENGAGE : APM_MGT_DISENGAGE;
	if (apmcall(APM_PWR_MGT_ENGAGE, dev, &regs) != 0)
		printf("apm0: APM engage (device %x): %s (%d)\n",
		    dev, apm_err_translate(APM_ERR_CODE(&regs)),
		    APM_ERR_CODE(&regs));
}

#ifdef notused
void
apm_devpowmgt_enable(int onoff, u_int dev)
{
	struct apmregs regs;

	if (apm_minver == 0)
		return;
	/* enable is auto BIOS management.
	 * disable is program control.
	 */
	bzero(&regs, sizeof(regs));
	regs.cx = onoff ? APM_MGT_ENABLE : APM_MGT_DISABLE;
	if (apmcall(APM_DEVICE_MGMT_ENABLE, dev, &regs) != 0)
		printf("APM device engage (device %x): %s (%d)\n",
		    dev, apm_err_translate(APM_ERR_CODE(&regs)),
		    APM_ERR_CODE(&regs));
}
#endif

int
apm_set_powstate(u_int dev, u_int state)
{
	struct apmregs regs;

	if (!apm_cd.cd_ndevs || (apm_minver == 0 && state > APM_SYS_OFF))
		return EINVAL;
	bzero(&regs, sizeof(regs));
	regs.cx = state;
	if (apmcall(APM_SET_PWR_STATE, dev, &regs) != 0) {
		apm_perror("set power state", &regs);
		if (APM_ERR_CODE(&regs) == APM_ERR_UNRECOG_DEV)
			return ENXIO;
		else
			return EIO;
	}
	return 0;
}

void
apm_cpu_slow(void)
{
	struct apmregs regs;
	static u_int64_t call_apm_slow = 0;

	if  (call_apm_slow != curcpu()->ci_schedstate.spc_cp_time[CP_IDLE]) {
		/* Always call BIOS halt/idle stuff */
		bzero(&regs, sizeof(regs));
		if (apmcall(APM_CPU_IDLE, 0, &regs) != 0) {
#ifdef DIAGNOSTIC
			apm_perror("set CPU slow", &regs);
#endif
		}
		apm_slow_called = 1;
		call_apm_slow = curcpu()->ci_schedstate.spc_cp_time[CP_IDLE];
	}
}

void
apm_cpu_busy(void)
{
	struct apmregs regs;

	if (!apm_slow_called)
		return;

	if (apm_flags & APM_IDLE_SLOWS) {
		bzero(&regs, sizeof(regs));
		if (apmcall(APM_CPU_BUSY, 0, &regs) != 0) {
#ifdef DIAGNOSTIC
			apm_perror("set CPU busy", &regs);
#endif
		}
		apm_slow_called = 0;
	}
}

void
apm_cpu_idle(void)
{
	struct apmregs regs;
	static u_int64_t call_apm_idle = 0;

	/*
	 * We call the bios APM_IDLE routine here only when we
	 * have been idle for some time - otherwise we just hlt.
	 */

	if  (call_apm_idle != curcpu()->ci_schedstate.spc_cp_time[CP_IDLE]) {
		/* Always call BIOS halt/idle stuff */
		bzero(&regs, sizeof(regs));
		if (apmcall(APM_CPU_IDLE, 0, &regs) != 0) {
#ifdef DIAGNOSTIC
			apm_perror("set CPU idle", &regs);
#endif
		}

		/* If BIOS did not halt, halt now! */
		if (apm_flags & APM_IDLE_SLOWS) {
			__asm volatile("sti;hlt");
		}
		call_apm_idle = curcpu()->ci_schedstate.spc_cp_time[CP_IDLE];
	} else {
		__asm volatile("sti;hlt");
	}
}

void
apm_set_ver(struct apm_softc *self)
{
	struct apmregs regs;
	int rv = 0;

	bzero(&regs, sizeof(regs));
	regs.cx = APM_VERSION;

	if (APM_MAJOR(apm_flags) == 1 && APM_MINOR(apm_flags) == 2 &&
	    (rv = apmcall(APM_DRIVER_VERSION, APM_DEV_APM_BIOS, &regs)) == 0) {
		apm_majver = APM_CONN_MAJOR(&regs);
		apm_minver = APM_CONN_MINOR(&regs);
	} else {
#ifdef APMDEBUG
		if (rv)
			apm_perror("set version 1.2", &regs);
#endif
		/* try downgrading to 1.1 */
		bzero(&regs, sizeof(regs));
		regs.cx = 0x0101;

		if (apmcall(APM_DRIVER_VERSION, APM_DEV_APM_BIOS, &regs) == 0) {
			apm_majver = 1;
			apm_minver = 1;
		} else {
#ifdef APMDEBUG
			apm_perror("set version 1.1", &regs);
#endif
			/* stay w/ flags then */
			apm_majver = APM_MAJOR(apm_flags);
			apm_minver = APM_MINOR(apm_flags);

			/* fix version for some endianess-challenged compaqs */
			if (!apm_majver) {
				apm_majver = 1;
				apm_minver = 0;
			}
		}
	}
	printf(": Power Management spec V%d.%d", apm_majver, apm_minver);
#ifdef DIAGNOSTIC
	if (apm_flags & APM_IDLE_SLOWS)
		printf(" (slowidle)");
	if (apm_flags & APM_BIOS_PM_DISABLED)
		printf(" (BIOS management disabled)");
	if (apm_flags & APM_BIOS_PM_DISENGAGED)
		printf(" (BIOS managing devices)");
#endif
	printf("\n");
}

void
apm_disconnect(struct apm_softc *sc)
{
	struct apmregs regs;

	bzero(&regs, sizeof(regs));
	if (apmcall(APM_SYSTEM_DEFAULTS,
	    (apm_minver == 1 ? APM_DEV_ALLDEVS : APM_DEFAULTS_ALL), &regs))
		apm_perror("system defaults failed", &regs);

	if (apmcall(APM_DISCONNECT, APM_DEV_APM_BIOS, &regs))
		apm_perror("disconnect failed", &regs);
	else
		printf("%s: disconnected\n", sc->sc_dev.dv_xname);
	apm_flags |= APM_BIOS_PM_DISABLED;
}

int
apmprobe(struct device *parent, void *match, void *aux)
{
	struct bios_attach_args *ba = aux;
	bios_apminfo_t *ap = ba->ba_apmp;
	bus_space_handle_t ch, dh;

	if (apm_cd.cd_ndevs || strcmp(ba->ba_name, "apm") ||
	    !(ap->apm_detail & APM_32BIT_SUPPORTED))
		return 0;

	/* addresses check
	   since pc* console and vga* probes much later
	   we cannot check for video memory being mapped
	   for apm stuff w/ bus_space_map() */
	if (ap->apm_code_len == 0 ||
	    (ap->apm_code32_base < IOM_BEGIN &&
	     ap->apm_code32_base + ap->apm_code_len > IOM_BEGIN) ||
	    (ap->apm_code16_base < IOM_BEGIN &&
	     ap->apm_code16_base + ap->apm_code16_len > IOM_BEGIN) ||
	    (ap->apm_data_base < IOM_BEGIN &&
	     ap->apm_data_base + ap->apm_data_len > IOM_BEGIN))
		return 0;

	if (bus_space_map(ba->ba_memt, ap->apm_code32_base,
	    ap->apm_code_len, 1, &ch) != 0) {
		DPRINTF(("apm0: can't map code\n"));
		return 0;
	}
	bus_space_unmap(ba->ba_memt, ch, ap->apm_code_len);

	if (bus_space_map(ba->ba_memt, ap->apm_data_base,
	    ap->apm_data_len, 1, &dh) != 0) {
		DPRINTF(("apm0: can't map data\n"));
		return 0;
	}
	bus_space_unmap(ba->ba_memt, dh, ap->apm_data_len);
	return 1;
}

void
apmattach(struct device *parent, struct device *self, void *aux)
{
	struct bios_attach_args *ba = aux;
	bios_apminfo_t *ap = ba->ba_apmp;
	struct apm_softc *sc = (void *)self;
	struct apmregs regs;
	u_int cbase, clen, l;
	bus_space_handle_t ch16, ch32, dh;

	apm_flags = ap->apm_detail;
	/*
	 * set up GDT descriptors for APM
	 */
	if (apm_flags & APM_32BIT_SUPPORTED) {

		/* truncate segments' limits to a page */
		ap->apm_code_len -= (ap->apm_code32_base +
		    ap->apm_code_len + 1) & 0xfff;
		ap->apm_code16_len -= (ap->apm_code16_base +
		    ap->apm_code16_len + 1) & 0xfff;
		ap->apm_data_len -= (ap->apm_data_base +
		    ap->apm_data_len + 1) & 0xfff;

		/* adjust version */
		if ((sc->sc_dev.dv_cfdata->cf_flags & APM_VERMASK) &&
		    (apm_flags & APM_VERMASK) !=
		    (sc->sc_dev.dv_cfdata->cf_flags & APM_VERMASK))
			apm_flags = (apm_flags & ~APM_VERMASK) |
			    (sc->sc_dev.dv_cfdata->cf_flags & APM_VERMASK);
		if (sc->sc_dev.dv_cfdata->cf_flags & APM_NOCLI) {
			extern int apm_cli; /* from apmcall.S */
			apm_cli = 0;
		}
		if (sc->sc_dev.dv_cfdata->cf_flags & APM_BEBATT)
			sc->be_batt = 1;
		apm_ep.seg = GSEL(GAPM32CODE_SEL,SEL_KPL);
		apm_ep.entry = ap->apm_entry;
		cbase = min(ap->apm_code32_base, ap->apm_code16_base);
		clen = max(ap->apm_code32_base + ap->apm_code_len,
			   ap->apm_code16_base + ap->apm_code16_len) - cbase;
		if ((cbase <= ap->apm_data_base &&
		     cbase + clen >= ap->apm_data_base) ||
		    (ap->apm_data_base <= cbase &&
		     ap->apm_data_base + ap->apm_data_len >= cbase)) {
			l = max(ap->apm_data_base + ap->apm_data_len + 1,
				cbase + clen + 1) -
			    min(ap->apm_data_base, cbase);
			bus_space_map(ba->ba_memt,
				min(ap->apm_data_base, cbase),
				l, 1, &dh);
			ch16 = dh;
			if (ap->apm_data_base < cbase)
				ch16 += cbase - ap->apm_data_base;
			else
				dh += ap->apm_data_base - cbase;
		} else {

			bus_space_map(ba->ba_memt, cbase, clen + 1, 1, &ch16);
			bus_space_map(ba->ba_memt, ap->apm_data_base,
			    ap->apm_data_len + 1, 1, &dh);
		}
		ch32 = ch16;
		if (ap->apm_code16_base == cbase)
			ch32 += ap->apm_code32_base - cbase;
		else
			ch16 += ap->apm_code16_base - cbase;

		setgdt(GAPM32CODE_SEL, (void *)ch32, ap->apm_code_len,
		    SDT_MEMERA, SEL_KPL, 1, 0);
		setgdt(GAPM16CODE_SEL, (void *)ch16, ap->apm_code16_len,
		    SDT_MEMERA, SEL_KPL, 0, 0);
		setgdt(GAPMDATA_SEL, (void *)dh, ap->apm_data_len, SDT_MEMRWA,
		    SEL_KPL, 1, 0);
		DPRINTF((": flags %x code 32:%x/%lx[%x] 16:%x/%lx[%x] "
		    "data %x/%lx/%x ep %x (%x:%lx)\n%s", apm_flags,
		    ap->apm_code32_base, ch32, ap->apm_code_len,
		    ap->apm_code16_base, ch16, ap->apm_code16_len,
		    ap->apm_data_base, dh, ap->apm_data_len,
		    ap->apm_entry, apm_ep.seg, ap->apm_entry+ch32,
		    sc->sc_dev.dv_xname));

		apm_set_ver(sc);

		if (apm_flags & APM_BIOS_PM_DISABLED)
			apm_powmgt_enable(1);

		/* Engage cooperative power management on all devices (v1.1) */
		apm_powmgt_engage(1, APM_DEV_ALLDEVS);

		bzero(&regs, sizeof(regs));
		if (apm_get_powstat(&regs) != 0)
			apm_perror("get power status", &regs);
		apm_cpu_busy();

		rw_init(&sc->sc_lock, "apmlk");

		/*
		 * Do a check once, ignoring any errors. This avoids
		 * gratuitous APM disconnects on laptops where the first
		 * event in the queue (after a boot) is non-recognizable.
		 * The IBM ThinkPad 770Z is one of those.
		 */
		apm_periodic_check(sc);

		if (apm_periodic_check(sc) == -1) {
			apm_disconnect(sc);

			/* Failed, nuke APM idle loop */
			cpu_idle_enter_fcn = NULL;
			cpu_idle_cycle_fcn = NULL;
			cpu_idle_leave_fcn = NULL;
		} else {
			kthread_create_deferred(apm_thread_create, sc);

			/* Setup APM idle loop */
			if (apm_flags & APM_IDLE_SLOWS) {
				cpu_idle_enter_fcn = apm_cpu_slow;
				cpu_idle_cycle_fcn = NULL;
				cpu_idle_leave_fcn = apm_cpu_busy;
			} else {
				cpu_idle_enter_fcn = NULL;
				cpu_idle_cycle_fcn = apm_cpu_idle;
				cpu_idle_leave_fcn = NULL;
			}

			/* All is well, let the rest of the world know */
			acpiapm_open = apmopen;
			acpiapm_close = apmclose;
			acpiapm_ioctl = apmioctl;
			acpiapm_kqfilter = apmkqfilter;
			apm_attached = 1;
		}
	} else {
		setgdt(GAPM32CODE_SEL, NULL, 0, 0, 0, 0, 0);
		setgdt(GAPM16CODE_SEL, NULL, 0, 0, 0, 0, 0);
		setgdt(GAPMDATA_SEL, NULL, 0, 0, 0, 0, 0);
	}
}

void
apm_thread_create(void *v)
{
	struct apm_softc *sc = v;

#ifdef MULTIPROCESSOR
	if (ncpus > 1) {
		apm_disconnect(sc);

		/* Nuke APM idle loop */
		cpu_idle_enter_fcn = NULL;
		cpu_idle_cycle_fcn = NULL;
		cpu_idle_leave_fcn = NULL;

		return;
	}
#endif

	if (kthread_create(apm_thread, sc, &sc->sc_thread,
	    sc->sc_dev.dv_xname)) {
		apm_disconnect(sc);
		printf("%s: failed to create kernel thread, disabled",
		    sc->sc_dev.dv_xname);

		/* Nuke APM idle loop */
		cpu_idle_enter_fcn = NULL;
		cpu_idle_cycle_fcn = NULL;
		cpu_idle_leave_fcn = NULL;
	}
}

void
apm_thread(void *v)
{
	struct apm_softc *sc = v;

	for (;;) {
		rw_enter_write(&sc->sc_lock);
		(void) apm_periodic_check(sc);
		rw_exit_write(&sc->sc_lock);
		tsleep_nsec(&nowake, PWAIT, "apmev", SEC_TO_NSEC(1));
	}
}

int
apmopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct apm_softc *sc;
	int error = 0;

	/* apm0 only */
	if (!apm_cd.cd_ndevs || APMUNIT(dev) != 0 ||
	    !(sc = apm_cd.cd_devs[APMUNIT(dev)]))
		return ENXIO;

	if (apm_flags & APM_BIOS_PM_DISABLED)
		return ENXIO;

	DPRINTF(("apmopen: dev %d pid %d flag %x mode %x\n",
	    APMDEV(dev), p->p_p->ps_pid, flag, mode));

	rw_enter_write(&sc->sc_lock);
	switch (APMDEV(dev)) {
	case APMDEV_CTL:
		if (!(flag & FWRITE)) {
			error = EINVAL;
			break;
		}
		if (sc->sc_flags & SCFLAG_OWRITE) {
			error = EBUSY;
			break;
		}
		sc->sc_flags |= SCFLAG_OWRITE;
		break;
	case APMDEV_NORMAL:
		if (!(flag & FREAD) || (flag & FWRITE)) {
			error = EINVAL;
			break;
		}
		sc->sc_flags |= SCFLAG_OREAD;
		break;
	default:
		error = ENXIO;
		break;
	}
	rw_exit_write(&sc->sc_lock);
	return error;
}

int
apmclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct apm_softc *sc;

	/* apm0 only */
	if (!apm_cd.cd_ndevs || APMUNIT(dev) != 0 ||
	    !(sc = apm_cd.cd_devs[APMUNIT(dev)]))
		return ENXIO;

	DPRINTF(("apmclose: pid %d flag %x mode %x\n",
	    p->p_p->ps_pid, flag, mode));

	rw_enter_write(&sc->sc_lock);
	switch (APMDEV(dev)) {
	case APMDEV_CTL:
		sc->sc_flags &= ~SCFLAG_OWRITE;
		break;
	case APMDEV_NORMAL:
		sc->sc_flags &= ~SCFLAG_OREAD;
		break;
	}
	rw_exit_write(&sc->sc_lock);
	return 0;
}

int
apmioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct apm_softc *sc;
	struct apmregs regs;
	int error = 0;

	/* apm0 only */
	if (!apm_cd.cd_ndevs || APMUNIT(dev) != 0 ||
	    !(sc = apm_cd.cd_devs[APMUNIT(dev)]))
		return ENXIO;

	rw_enter_write(&sc->sc_lock);
	switch (cmd) {
		/* some ioctl names from linux */
	case APM_IOC_STANDBY:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		else
			apm_userstandbys++;
		break;
	case APM_IOC_SUSPEND:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		else
			apm_suspends++;
		break;
	case APM_IOC_PRN_CTL:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		else {
			int flag = *(int *)data;
			DPRINTF(( "APM_IOC_PRN_CTL: %d\n", flag ));
			switch (flag) {
			case APM_PRINT_ON:	/* enable printing */
				sc->sc_flags &= ~SCFLAG_PRINT;
				break;
			case APM_PRINT_OFF: /* disable printing */
				sc->sc_flags &= ~SCFLAG_PRINT;
				sc->sc_flags |= SCFLAG_NOPRINT;
				break;
			case APM_PRINT_PCT: /* disable some printing */
				sc->sc_flags &= ~SCFLAG_PRINT;
				sc->sc_flags |= SCFLAG_PCTPRINT;
				break;
			default:
				error = EINVAL;
				break;
			}
		}
		break;
	case APM_IOC_DEV_CTL:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		else {
			struct apm_ctl *actl = (struct apm_ctl *)data;

			bzero(&regs, sizeof(regs));
			if (!apmcall(APM_GET_POWER_STATE, actl->dev, &regs))
				printf("%s: dev %04x state %04x\n",
				    sc->sc_dev.dv_xname, dev, regs.cx);

			error = apm_set_powstate(actl->dev, actl->mode);
		}
		break;
	case APM_IOC_GETPOWER:
		if (apm_get_powstat(&regs) == 0) {
			struct apm_power_info *powerp =
			    (struct apm_power_info *)data;

			bzero(powerp, sizeof(*powerp));
			if (BATT_LIFE(&regs) != APM_BATT_LIFE_UNKNOWN)
				powerp->battery_life = BATT_LIFE(&regs);
			powerp->ac_state = AC_STATE(&regs);
			switch (apm_minver) {
			case 0:
				if (!(BATT_FLAGS(&regs) & APM_BATT_FLAG_NOBATTERY))
					powerp->battery_state = BATT_STATE(&regs);
				break;
			case 1:
			default:
				if (BATT_FLAGS(&regs) & APM_BATT_FLAG_HIGH)
					powerp->battery_state = APM_BATT_HIGH;
				else if (BATT_FLAGS(&regs) & APM_BATT_FLAG_LOW)
					powerp->battery_state = APM_BATT_LOW;
				else if (BATT_FLAGS(&regs) & APM_BATT_FLAG_CRITICAL)
					powerp->battery_state = APM_BATT_CRITICAL;
				else if (BATT_FLAGS(&regs) & APM_BATT_FLAG_CHARGING)
					powerp->battery_state = APM_BATT_CHARGING;
				else if (BATT_FLAGS(&regs) & APM_BATT_FLAG_NOBATTERY)
					powerp->battery_state = APM_BATTERY_ABSENT;
				else
					powerp->battery_state = APM_BATT_UNKNOWN;
				if (BATT_REM_VALID(&regs)) {
					powerp->minutes_left = BATT_REMAINING(&regs);
					if (sc->be_batt)
						powerp->minutes_left =
						    swap16(powerp->minutes_left);
				}
			}
		} else {
			apm_perror("ioctl get power status", &regs);
			error = EIO;
		}
		break;
	case APM_IOC_STANDBY_REQ:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		/* only fails if no one cares. apmd at least should */
		else if (apm_record_event(sc, APM_USER_STANDBY_REQ))
			error = EINVAL; /* ? */
		break;
	case APM_IOC_SUSPEND_REQ:
		if ((flag & FWRITE) == 0)
			error = EBADF;
		/* only fails if no one cares. apmd at least should */
		else if (apm_record_event(sc, APM_USER_SUSPEND_REQ))
			error = EINVAL; /* ? */
		break;
	default:
		error = ENOTTY;
	}

	rw_exit_write(&sc->sc_lock);
	return error;
}

void
filt_apmrdetach(struct knote *kn)
{
	struct apm_softc *sc = (struct apm_softc *)kn->kn_hook;

	rw_enter_write(&sc->sc_lock);
	klist_remove_locked(&sc->sc_note, kn);
	rw_exit_write(&sc->sc_lock);
}

int
filt_apmread(struct knote *kn, long hint)
{
	/* XXX weird kqueue_scan() semantics */
	if (hint && !kn->kn_data)
		kn->kn_data = (int)hint;
	return (1);
}

int
apmkqfilter(dev_t dev, struct knote *kn)
{
	struct apm_softc *sc;

	/* apm0 only */
	if (!apm_cd.cd_ndevs || APMUNIT(dev) != 0 ||
	    !(sc = apm_cd.cd_devs[APMUNIT(dev)]))
		return ENXIO;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &apmread_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = (caddr_t)sc;

	rw_enter_write(&sc->sc_lock);
	klist_insert_locked(&sc->sc_note, kn);
	rw_exit_write(&sc->sc_lock);
	return (0);
}

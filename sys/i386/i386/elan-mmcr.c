/*-
 * SPDX-License-Identifier: Beerware
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 *
 * The AMD Elan sc520 is a system-on-chip gadget which is used in embedded
 * kind of things, see www.soekris.com for instance, and it has a few quirks
 * we need to deal with.
 * Unfortunately we cannot identify the gadget by CPUID output because it
 * depends on strapping options and only the stepping field may be useful
 * and those are undocumented from AMDs side.
 *
 * So instead we recognize the on-chip host-PCI bridge and call back from
 * sys/i386/pci/pci_bus.c to here if we find it.
 *
 * #ifdef CPU_ELAN_PPS
 *   The Elan has three general purpose counters, and when two of these
 *   are used just right they can hardware timestamp external events with
 *   approx 125 nsec resolution and +/- 125 nsec precision.
 *
 *   Connect the signal to TMR1IN and a GPIO pin, and configure the GPIO pin
 *   with a 'P' in sysctl machdep.elan_gpio_config.
 *
 *   The rising edge of the signal will start timer 1 counting up from
 *   zero, and when the timecounter polls for PPS, both counter 1 & 2 is
 *   read, as well as the GPIO bit.  If a rising edge has happened, the
 *   contents of timer 1 which is how long time ago the edge happened,
 *   is subtracted from timer 2 to give us a "true time stamp".
 *
 *   Echoing the PPS signal on any GPIO pin is supported (set it to 'e'
 *   or 'E' (inverted) in the sysctl)  The echo signal should only be
 *   used as a visual indication, not for calibration since it suffers
 *   from 1/hz (or more) jitter which the timestamps are compensated for.
 * #endif CPU_ELAN_PPS
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_cpu.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/timetc.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/timepps.h>
#include <sys/watchdog.h>

#include <dev/led/led.h>
#include <machine/md_var.h>
#include <machine/elan_mmcr.h>
#include <machine/pc/bios.h>

#include <vm/vm.h>
#include <vm/pmap.h>

static char gpio_config[33];

static volatile uint16_t *mmcrptr;
volatile struct elan_mmcr *elan_mmcr;

#ifdef CPU_ELAN_PPS
static struct pps_state elan_pps;
static volatile uint16_t *pps_ap[3];
static u_int	pps_a, pps_d;
static u_int	echo_a, echo_d;
#endif /* CPU_ELAN_PPS */

#ifdef CPU_SOEKRIS

static struct bios_oem bios_soekris = {
	{ 0xf0000, 0xf1000 },
	{
		{ "Soekris", 0, 8 },	/* Soekris Engineering. */
		{ "net4", 0, 8 },	/* net45xx */
		{ "comBIOS", 0, 54 },	/* comBIOS ver. 1.26a  20040819 ... */
		{ NULL, 0, 0 },
	}
};

#endif

static u_int	led_cookie[32];
static struct cdev *led_dev[32];

static void
gpio_led(void *cookie, int state)
{
	u_int u, v;

	u = *(int *)cookie;
	v = u & 0xffff;
	u >>= 16;
	if (!state)
		v ^= 0xc;
	mmcrptr[v / 2] = u;
}

static int
sysctl_machdep_elan_gpio_config(SYSCTL_HANDLER_ARGS)
{
	u_int u, v;
	int i, np, ne;
	int error;
	char buf[32];
	char tmp[10];

	error = SYSCTL_OUT(req, gpio_config, 33);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (req->newlen != 32)
		return (EINVAL);
	error = SYSCTL_IN(req, buf, 32);
	if (error != 0)
		return (error);
	/* Disallow any disabled pins and count pps and echo */
	np = ne = 0;
	for (i = 0; i < 32; i++) {
		if (gpio_config[i] == '-' && buf[i] == '.')
			buf[i] = gpio_config[i];
		if (gpio_config[i] == '-' && buf[i] != '-')
			return (EPERM);
		if (buf[i] == 'P') {
			np++;
			if (np > 1)
				return (EINVAL);
		}
		if (buf[i] == 'e' || buf[i] == 'E') {
			ne++;
			if (ne > 1)
				return (EINVAL);
		}
		if (buf[i] != 'L' && buf[i] != 'l'
#ifdef CPU_ELAN_PPS
		    && buf[i] != 'P' && buf[i] != 'E' && buf[i] != 'e'
#endif /* CPU_ELAN_PPS */
		    && buf[i] != '.' && buf[i] != '-')
			return (EINVAL);
	}
#ifdef CPU_ELAN_PPS
	if (np == 0)
		pps_a = pps_d = 0;
	if (ne == 0)
		echo_a = echo_d = 0;
#endif
	for (i = 0; i < 32; i++) {
		u = 1 << (i & 0xf);
		if (i >= 16)
			v = 2;
		else
			v = 0;
#ifdef CPU_SOEKRIS
		if (i == 9)
			;
		else
#endif
		if (buf[i] != 'l' && buf[i] != 'L' && led_dev[i] != NULL) {
			led_destroy(led_dev[i]);	
			led_dev[i] = NULL;
			mmcrptr[(0xc2a + v) / 2] &= ~u;
		}
		switch (buf[i]) {
#ifdef CPU_ELAN_PPS
		case 'P':
			pps_d = u;
			pps_a = 0xc30 + v;
			pps_ap[0] = &mmcrptr[pps_a / 2];
			pps_ap[1] = &elan_mmcr->GPTMR2CNT;
			pps_ap[2] = &elan_mmcr->GPTMR1CNT;
			mmcrptr[(0xc2a + v) / 2] &= ~u;
			gpio_config[i] = buf[i];
			break;
		case 'e':
		case 'E':
			echo_d = u;
			if (buf[i] == 'E')
				echo_a = 0xc34 + v;
			else
				echo_a = 0xc38 + v;
			mmcrptr[(0xc2a + v) / 2] |= u;
			gpio_config[i] = buf[i];
			break;
#endif /* CPU_ELAN_PPS */
		case 'l':
		case 'L':
			if (buf[i] == 'L')
				led_cookie[i] = (0xc34 + v) | (u << 16);
			else
				led_cookie[i] = (0xc38 + v) | (u << 16);
			if (led_dev[i])
				break;
			sprintf(tmp, "gpio%d", i);
			mmcrptr[(0xc2a + v) / 2] |= u;
			gpio_config[i] = buf[i];
			led_dev[i] =
			    led_create(gpio_led, &led_cookie[i], tmp);
			break;
		case '.':
			gpio_config[i] = buf[i];
			break;
		case '-':
		default:
			break;
		}
	}
	return (0);
}

SYSCTL_OID(_machdep, OID_AUTO, elan_gpio_config, CTLTYPE_STRING | CTLFLAG_RW,
    NULL, 0, sysctl_machdep_elan_gpio_config, "A", "Elan CPU GPIO pin config");

#ifdef CPU_ELAN_PPS
static void
elan_poll_pps(struct timecounter *tc)
{
	static int state;
	int i;
	uint16_t u, x, y, z;
	register_t saveintr;

	/*
	 * Grab the HW state as quickly and compactly as we can.  Disable
	 * interrupts to avoid measuring our interrupt service time on
	 * hw with quality clock sources.
	 */
	saveintr = intr_disable();
	x = *pps_ap[0];	/* state, must be first, see below */
	y = *pps_ap[1]; /* timer2 */
	z = *pps_ap[2]; /* timer1 */
	intr_restore(saveintr);

	/*
	 * Order is important here.  We need to check the state of the GPIO
	 * pin first, in order to avoid reading timer 1 right before the
	 * state change.  Technically pps_a may be zero in which case we
	 * harmlessly read the REVID register and the contents of pps_d is
	 * of no concern.
	 */

	i = x & pps_d;

	/* If state did not change or we don't have a GPIO pin, return */
	if (i == state || pps_a == 0)
		return;

	state = i;

	/* If the state is "low", flip the echo GPIO and return.  */
	if (!i) {
		if (echo_a)
			mmcrptr[(echo_a ^ 0xc) / 2] = echo_d;
		return;
	}

	/*
	 * Subtract timer1 from timer2 to compensate for time from the
	 * edge until we read the counters.
	 */
	u = y - z;

	pps_capture(&elan_pps);
	elan_pps.capcount = u;
	pps_event(&elan_pps, PPS_CAPTUREASSERT);

	/* Twiddle echo bit */
	if (echo_a)
		mmcrptr[echo_a / 2] = echo_d;
}
#endif /* CPU_ELAN_PPS */

static unsigned
elan_get_timecount(struct timecounter *tc)
{

	/* Read timer2, end of story */
	return (elan_mmcr->GPTMR2CNT);
}

/*
 * The Elan CPU can be run from a number of clock frequencies, this
 * allows you to override the default 33.3 MHZ.
 */
#ifndef CPU_ELAN_XTAL
#define CPU_ELAN_XTAL 33333333
#endif

static struct timecounter elan_timecounter = {
	elan_get_timecount,
	NULL,
	0xffff,
	CPU_ELAN_XTAL / 4,
	"ELAN",
	1000
};

static int
sysctl_machdep_elan_freq(SYSCTL_HANDLER_ARGS)
{
	u_int f;
	int error;

	f = elan_timecounter.tc_frequency * 4;
	error = sysctl_handle_int(oidp, &f, 0, req);
	if (error == 0 && req->newptr != NULL) 
		elan_timecounter.tc_frequency = (f + 3) / 4;
	return (error);
}

SYSCTL_PROC(_machdep, OID_AUTO, elan_freq, CTLTYPE_UINT | CTLFLAG_RW,
    0, sizeof (u_int), sysctl_machdep_elan_freq, "IU", "");

/*
 * Positively identifying the Elan can only be done through the PCI id of
 * the host-bridge, this function is called from i386/pci/pci_bus.c.
 */
void
init_AMD_Elan_sc520(void)
{
	u_int new;
	int i;

	mmcrptr = pmap_mapdev(0xfffef000, 0x1000);
	elan_mmcr = (volatile struct elan_mmcr *)mmcrptr;

	/*-
	 * The i8254 is driven with a nonstandard frequency which is
	 * derived thusly:
	 *   f = 32768 * 45 * 25 / 31 = 1189161.29...
	 * We use the sysctl to get the i8254 (timecounter etc) into whack.
	 */
	
	new = 1189161;
	i = kernel_sysctlbyname(&thread0, "machdep.i8254_freq", 
	    NULL, 0, &new, sizeof new, NULL, 0);
	if (bootverbose || 1)
		printf("sysctl machdep.i8254_freq=%d returns %d\n", new, i);

	/* Start GP timer #2 and use it as timecounter, hz permitting */
	elan_mmcr->GPTMR2MAXCMPA = 0;
	elan_mmcr->GPTMR2CTL = 0xc001;

#ifdef CPU_ELAN_PPS
	/* Set up GP timer #1 as pps counter */
	elan_mmcr->CSPFS &= ~0x10;
	elan_mmcr->GPTMR1CTL = 0x8000 | 0x4000 | 0x10 | 0x1;
	elan_mmcr->GPTMR1MAXCMPA = 0x0;
	elan_mmcr->GPTMR1MAXCMPB = 0x0;
	elan_pps.ppscap |= PPS_CAPTUREASSERT;
	pps_init(&elan_pps);
#endif
	tc_init(&elan_timecounter);
}

static void
elan_watchdog(void *foo __unused, u_int spec, int *error)
{
	u_int u, v, w;
	static u_int cur;

	u = spec & WD_INTERVAL;
	if (u > 0 && u <= 35) {
		u = imax(u - 5, 24);
		v = 2 << (u - 24);
		v |= 0xc000;

		/*
		 * There is a bug in some silicon which prevents us from
		 * writing to the WDTMRCTL register if the GP echo mode is
		 * enabled.  GP echo mode on the other hand is desirable
		 * for other reasons.  Save and restore the GP echo mode
		 * around our hardware tom-foolery.
		 */
		w = elan_mmcr->GPECHO;
		elan_mmcr->GPECHO = 0;
		if (v != cur) {
			/* Clear the ENB bit */
			elan_mmcr->WDTMRCTL = 0x3333;
			elan_mmcr->WDTMRCTL = 0xcccc;
			elan_mmcr->WDTMRCTL = 0;

			/* Set new value */
			elan_mmcr->WDTMRCTL = 0x3333;
			elan_mmcr->WDTMRCTL = 0xcccc;
			elan_mmcr->WDTMRCTL = v;
			cur = v;
		} else {
			/* Just reset timer */
			elan_mmcr->WDTMRCTL = 0xaaaa;
			elan_mmcr->WDTMRCTL = 0x5555;
		}
		elan_mmcr->GPECHO = w;
		*error = 0;
	} else {
		w = elan_mmcr->GPECHO;
		elan_mmcr->GPECHO = 0;
		elan_mmcr->WDTMRCTL = 0x3333;
		elan_mmcr->WDTMRCTL = 0xcccc;
		elan_mmcr->WDTMRCTL = 0x4080;
		elan_mmcr->WDTMRCTL = w;		/* XXX What does this statement do? */
		elan_mmcr->GPECHO = w;
		cur = 0;
	}
}

static int
elan_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{

	if (offset >= 0x1000) 
		return (-1);
	*paddr = 0xfffef000;
	return (0);
}
static int
elan_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int flag, struct  thread *tdr)
{
	int error;

	error = ENOIOCTL;

#ifdef CPU_ELAN_PPS
	if (pps_a != 0)
		error = pps_ioctl(cmd, arg, &elan_pps);
	/*
	 * We only want to incur the overhead of the PPS polling if we
	 * are actually asked to timestamp.
	 */
	if (elan_pps.ppsparam.mode & PPS_CAPTUREASSERT) {
		elan_timecounter.tc_poll_pps = elan_poll_pps;
	} else {
		elan_timecounter.tc_poll_pps = NULL;
	}
	if (error != ENOIOCTL)
		return (error);
#endif

	return(error);
}

static struct cdevsw elan_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_ioctl =	elan_ioctl,
	.d_mmap =	elan_mmap,
	.d_name =	"elan",
};

static void
elan_drvinit(void)
{

#ifdef CPU_SOEKRIS
#define BIOS_OEM_MAXLEN 72
        static u_char bios_oem[BIOS_OEM_MAXLEN] = "\0";
#endif /* CPU_SOEKRIS */

	/* If no elan found, just return */
	if (mmcrptr == NULL)
		return;

	printf("Elan-mmcr driver: MMCR at %p.%s\n", 
	    mmcrptr,
#ifdef CPU_ELAN_PPS
	    " PPS support."
#else
	    ""
#endif
	    );

	make_dev(&elan_cdevsw, 0,
	    UID_ROOT, GID_WHEEL, 0600, "elan-mmcr");

#ifdef CPU_SOEKRIS
	if ( bios_oem_strings(&bios_soekris, bios_oem, BIOS_OEM_MAXLEN) > 0 )
		printf("Elan-mmcr %s\n", bios_oem);

	/* Create the error LED on GPIO9 */
	led_cookie[9] = 0x02000c34;
	led_dev[9] = led_create(gpio_led, &led_cookie[9], "error");
	
	/* Disable the unavailable GPIO pins */
	strcpy(gpio_config, "-----....--..--------..---------");
#else /* !CPU_SOEKRIS */
	/* We don't know which pins are available so enable them all */
	strcpy(gpio_config, "................................");
#endif /* CPU_SOEKRIS */

	EVENTHANDLER_REGISTER(watchdog_list, elan_watchdog, NULL, 0);
}

SYSINIT(elan, SI_SUB_PSEUDO, SI_ORDER_MIDDLE, elan_drvinit, NULL);


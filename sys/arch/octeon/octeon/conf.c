/*	$OpenBSD: conf.c,v 1.30 2023/04/13 02:19:05 jsg Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/tty.h>

#include <machine/conf.h>

/*
 *	Block devices.
 */

#include "vnd.h"
#include "sd.h"
#include "cd.h"
#include "wd.h"
bdev_decl(wd);
#include "rd.h"
#include "hotplug.h"

#define NOCTCF 1
bdev_decl(octcf);

#define NAMDCF 1
bdev_decl(amdcf);

struct bdevsw	bdevsw[] =
{
	bdev_disk_init(NSD,sd),		/* 0: SCSI disk */
	bdev_swap_init(1,sw),		/* 1: should be here swap pseudo-dev */
	bdev_disk_init(NVND,vnd),	/* 2: vnode disk driver */
	bdev_disk_init(NCD,cd),		/* 3: SCSI CD-ROM */
	bdev_disk_init(NWD,wd),		/* 4: ST506/ESDI/IDE disk */
	bdev_notdef(),			/* 5:  */
	bdev_notdef(),			/* 6: was: concatenated disk driver */
	bdev_notdef(),			/* 7:  */
	bdev_disk_init(NRD,rd),		/* 8: RAM disk (for install) */
	bdev_notdef(),			/* 9:  */
	bdev_notdef(),			/* 10: was: SCSI tape */
	bdev_notdef(),			/* 11:  */
	bdev_notdef(),			/* 12:  */
	bdev_notdef(),			/* 13:  */
	bdev_notdef(),			/* 14:  */
	bdev_disk_init(NOCTCF,octcf),	/* 15: CF disk */
	bdev_notdef(),			/* 16:  */
	bdev_notdef(),			/* 17:  */
	bdev_notdef(),			/* 18:  */
	bdev_disk_init(NAMDCF,amdcf),	/* 19: CF disk */
};

int	nblkdev = nitems(bdevsw);

/*
 *	Character devices.
 */

#define mmread mmrw
#define mmwrite mmrw
dev_type_read(mmrw);
cdev_decl(mm);
#include "bio.h"
#include "pty.h"
cdev_decl(fd);
#include "st.h"
#include "bpfilter.h"
#include "tun.h"
#if 0
#include "apm.h"
#endif
#include "com.h"
cdev_decl(com);
#include "lpt.h"
cdev_decl(lpt);
#include "ch.h"
#include "uk.h"
cdev_decl(wd);
#include "audio.h"
#include "video.h"
cdev_decl(octcf);
cdev_decl(amdcf);

#include "ksyms.h"
#include "kstat.h"

#include "wsdisplay.h"
#include "wskbd.h"
#include "wsmouse.h"
#include "wsmux.h"
#include "pci.h"
cdev_decl(pci);

#include "dt.h"
#include "pf.h"

#include "usb.h"
#include "uhid.h"
#include "fido.h"
#include "ujoy.h"
#include "ugen.h"
#include "ulpt.h"
#include "ucom.h"

#include "vscsi.h"
#include "pppx.h"
#include "fuse.h"
#include "octboot.h"
#include "openprom.h"

struct cdevsw	cdevsw[] =
{
	cdev_cn_init(1,cn),		/* 0: virtual console */
	cdev_notdef(),			/* 1: was /dev/drum */
	cdev_ctty_init(1,ctty),		/* 2: controlling terminal */
	cdev_mm_init(1,mm),		/* 3: /dev/{null,mem,kmem,...} */
	cdev_tty_init(NPTY,pts),	/* 4: pseudo-tty slave */
	cdev_ptc_init(NPTY,ptc),	/* 5: pseudo-tty master */
	cdev_log_init(1,log),		/* 6: /dev/klog */
	cdev_fd_init(1,filedesc),	/* 7: file descriptor pseudo-dev */
	cdev_disk_init(NCD,cd),		/* 8: SCSI CD */
	cdev_disk_init(NSD,sd),		/* 9: SCSI disk */
	cdev_tape_init(NST,st),		/* 10: SCSI tape */
	cdev_disk_init(NVND,vnd),	/* 11: vnode disk */
	cdev_bpf_init(NBPFILTER,bpf),	/* 12: berkeley packet filter */
	cdev_tun_init(NTUN,tun),	/* 13: network tunnel */
#if 0
	cdev_apm_init(NAPM,apm),	/* 14: apm */
#else
	cdev_notdef(),			/* 14: */
#endif
	cdev_disk_init(NOCTCF,octcf),	/* 15: CF disk */
	cdev_lpt_init(NLPT,lpt),	/* 16: Parallel printer interface */
	cdev_tty_init(NCOM,com),	/* 17: 16C450 serial interface */
	cdev_disk_init(NWD,wd),		/* 18: ST506/ESDI/IDE disk */
	cdev_disk_init(NAMDCF,amdcf),	/* 19: CF disk */
	cdev_openprom_init(NOPENPROM,openprom),	/* 20: /dev/openprom */
	cdev_octboot_init(NOCTBOOT,octboot),	/* 21: /dev/octboot */
	cdev_disk_init(NRD,rd),		/* 22: ramdisk device */
	cdev_notdef(),			/* 23: was: concatenated disk driver */
	cdev_notdef(),			/* 24: */
	cdev_wsdisplay_init(NWSDISPLAY, wsdisplay),	/* 25: */
	cdev_mouse_init(NWSKBD, wskbd),	/* 26: */
	cdev_mouse_init(NWSMOUSE, wsmouse),	/* 27: */
	cdev_mouse_init(NWSMUX, wsmux),	/* 28: */
#ifdef USER_PCICONF
	cdev_pci_init(NPCI,pci),	/* 29: PCI user */
#else
	cdev_notdef(),			/* 29 */
#endif
	cdev_dt_init(NDT,dt),		/* 30: dynamic tracer */
	cdev_pf_init(NPF,pf),		/* 31: packet filter */
	cdev_uk_init(NUK,uk),		/* 32: unknown SCSI */
	cdev_random_init(1,random),	/* 33: random data source */
	cdev_notdef(),			/* 34: */
	cdev_ksyms_init(NKSYMS,ksyms),	/* 35: Kernel symbols device */
	cdev_ch_init(NCH,ch),		/* 36: SCSI autochanger */
	cdev_notdef(),			/* 37: */
	cdev_notdef(),			/* 38: */
	cdev_notdef(),			/* 39: */
	cdev_notdef(),			/* 40: */
	cdev_notdef(),			/* 41: */
	cdev_notdef(),			/* 42: */
	cdev_notdef(),			/* 43: */
	cdev_audio_init(NAUDIO,audio),	/* 44: /dev/audio */
	cdev_video_init(NVIDEO,video),	/* 45: generic video I/O */
	cdev_notdef(),			/* 46: */
	cdev_notdef(),			/* 47: was: /dev/crypto */
	cdev_notdef(),			/* 48: */
	cdev_bio_init(NBIO,bio),	/* 49: ioctl tunnel */
	cdev_notdef(),			/* 50: */
	cdev_kstat_init(NKSTAT,kstat),	/* 51: kernel statistics */
	cdev_ptm_init(NPTY,ptm),	/* 52: pseudo-tty ptm device */
	cdev_fuse_init(NFUSE,fuse),	/* 53: fuse */
	cdev_notdef(),			/* 54: */
	cdev_notdef(),			/* 55: */
	cdev_notdef(),			/* 56: */
	cdev_notdef(),			/* 57: */
	cdev_notdef(),			/* 58: */
	cdev_notdef(),			/* 59: */
	cdev_notdef(),			/* 60: */
	cdev_usb_init(NUSB,usb),	/* 61: USB controller */
	cdev_usbdev_init(NUHID,uhid),	/* 62: USB generic HID */
	cdev_usbdev_init(NUGEN,ugen),	/* 63: USB generic driver */
	cdev_ulpt_init(NULPT,ulpt),	/* 64: USB printers */
	cdev_notdef(),			/* 65: */
	cdev_tty_init(NUCOM,ucom),	/* 66: USB tty */
	cdev_hotplug_init(NHOTPLUG,hotplug), /* 67: devices hotplugging */
	cdev_notdef(),			/* 68: */
	cdev_vscsi_init(NVSCSI,vscsi),	/* 69: vscsi */
	cdev_disk_init(1,diskmap),	/* 70: disk mapper */
	cdev_pppx_init(NPPPX,pppx),	/* 71: pppx */
	cdev_notdef(),			/* 72: was USB scanners */
	cdev_notdef(),			/* 73: fuse on other mips64 */
	cdev_tun_init(NTUN,tap),	/* 74: Ethernet network tunnel */
	cdev_notdef(),			/* 75: was switch(4) */
	cdev_fido_init(NFIDO,fido),	/* 76: FIDO/U2F security key */
	cdev_pppx_init(NPPPX,pppac),	/* 77: PPP Access Concentrator */
	cdev_ujoy_init(NUJOY,ujoy),	/* 78: USB joystick/gamecontroller */
};

int	nchrdev = nitems(cdevsw);

/*
 * Swapdev is a fake device implemented
 * in sw.c used only internally to get to swstrategy.
 * It cannot be provided to the users, because the
 * swstrategy routine munches the b_dev and b_blkno entries
 * before calling the appropriate driver.  This would horribly
 * confuse, e.g. the hashing routines. Instead, /dev/drum is
 * provided as a character (raw) device.
 */
dev_t	swapdev = makedev(1, 0);

/*
 * Routine that identifies /dev/mem and /dev/kmem.
 *
 * A minimal stub routine can always return 0.
 */
int
iskmemdev(dev_t dev)
{

	if (major(dev) == 3 && (minor(dev) == 0 || minor(dev) == 1))
		return (1);
	return (0);
}

/*
 * Returns true if def is /dev/zero
 */
int
iszerodev(dev_t dev)
{
	return (major(dev) == 3 && minor(dev) == 12);
}

dev_t
getnulldev(void)
{
	return(makedev(3, 2));
}


const int chrtoblktbl[] =  {
	/* VCHR         VBLK */
	/* 0 */		NODEV,
	/* 1 */		NODEV,
	/* 2 */		NODEV,
	/* 3 */		NODEV,
	/* 4 */		NODEV,
	/* 5 */		NODEV,
	/* 6 */		NODEV,
	/* 7 */		NODEV,
	/* 8 */		3,		/* cd */
	/* 9 */		0,		/* sd */
	/* 10 */	NODEV,
	/* 11 */	2,		/* vnd */
	/* 12 */	NODEV,
	/* 13 */	NODEV,
	/* 14 */	NODEV,
	/* 15 */	15,		/* octcf */
	/* 16 */	NODEV,
	/* 17 */	NODEV,
	/* 18 */	4,		/* wd */
	/* 19 */	19,		/* amdcf */
	/* 20 */	NODEV,
	/* 21 */	NODEV,
	/* 22 */	8		/* rd */
};

const int nchrtoblktbl = nitems(chrtoblktbl);

#include <dev/cons.h>

cons_decl(ws);
cons_decl(com);

struct	consdev constab[] = {
#if NWSDISPLAY > 0
	cons_init(ws),
#endif
#if NCOM > 0
	cons_init(com),
#endif
	{ 0 },
};

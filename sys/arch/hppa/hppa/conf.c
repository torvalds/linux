/*	$OpenBSD: conf.c,v 1.75 2022/10/15 10:12:13 jsg Exp $	*/

/*-
 * Copyright (c) 1991 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
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
 *
 *     @(#)conf.c	7.9 (Berkeley) 5/28/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/vnode.h>

#include <machine/conf.h>

#include "vnd.h"
#include "rd.h"
#include "sd.h"
#include "st.h"
#include "cd.h"
#include "ch.h"
#include "uk.h"
#include "wd.h"
bdev_decl(wd);
cdev_decl(wd);
#if 0
#include "fd.h"
#else
#define NFD 0
#endif
bdev_decl(fd);
cdev_decl(fd);

struct bdevsw   bdevsw[] =
{
	bdev_swap_init(1,sw),		/*  0: swap pseudo-device */
	bdev_notdef(),			/*  1: was: concatenated disk driver */
	bdev_disk_init(NVND,vnd),	/*  2: vnode disk driver */
	bdev_disk_init(NRD,rd),		/*  3: RAM disk */
	bdev_disk_init(NSD,sd),		/*  4: SCSI disk */
	bdev_notdef(),			/*  5: was: SCSI tape */
	bdev_disk_init(NCD,cd),		/*  6: SCSI CD-ROM */
	bdev_disk_init(NFD,fd),		/*  7: floppy drive */
	bdev_disk_init(NWD,wd),		/*  8: ST506 drive */
	bdev_notdef(),			/*  9: */
	bdev_notdef(),			/* 10: */
	bdev_notdef(),			/* 11: */
	bdev_notdef(),			/* 12: */
	bdev_notdef(),			/* 13: */
	bdev_notdef(),			/* 14: */
};
int	nblkdev = nitems(bdevsw);

#include "audio.h"
#include "video.h"
#include "bio.h"
#include "pty.h"
#include "wsdisplay.h"
#include "wskbd.h"
#include "wsmouse.h"
#include "wsmux.h"

#include "bpfilter.h"
#include "tun.h"

#include "ksyms.h"
#include "kstat.h"

#include "lpt.h"
cdev_decl(lpt);

#include "com.h"
cdev_decl(com);

#include "dt.h"
#include "pf.h"

#include "hotplug.h"
#include "vscsi.h"
#include "pppx.h"

#ifdef USER_PCICONF
#include "pci.h"
cdev_decl(pci);
#endif

#include "usb.h"
#include "uhid.h"
#include "fido.h"
#include "ujoy.h"
#include "ugen.h"
#include "ulpt.h"
#include "ucom.h"

#include "fuse.h"

struct cdevsw   cdevsw[] =
{
	cdev_cn_init(1,cn),		/*  0: virtual console */
	cdev_ctty_init(1,ctty),		/*  1: controlling terminal */
	cdev_mm_init(1,mm),		/*  2: /dev/{null,mem,kmem,...} */
	cdev_notdef(),			/*  3 was /dev/drum */
	cdev_tty_init(NPTY,pts),	/*  4: pseudo-tty slave */
	cdev_ptc_init(NPTY,ptc),	/*  5: pseudo-tty master */
	cdev_log_init(1,log),		/*  6: /dev/klog */
	cdev_notdef(),			/*  7: was: concatenated disk */
	cdev_disk_init(NVND,vnd),	/*  8: vnode disk driver */
	cdev_disk_init(NRD,rd),		/*  9: RAM disk */
	cdev_disk_init(NSD,sd),		/* 10: SCSI disk */
	cdev_tape_init(NST,st),		/* 11: SCSI tape */
	cdev_disk_init(NCD,cd),		/* 12: SCSI cd-rom */
	cdev_ch_init(NCH,ch),		/* 13: SCSI changer */
	cdev_notdef(),			/* 14: */
	cdev_uk_init(NUK,uk),		/* 15: SCSI unknown */
	cdev_fd_init(1,filedesc),	/* 16: file descriptor pseudo-device */
	cdev_bpf_init(NBPFILTER,bpf),	/* 17: Berkeley packet filter */
	cdev_tun_init(NTUN,tun),	/* 18: network tunnel */
	cdev_notdef(),			/* 19: was LKM */
	cdev_random_init(1,random),	/* 20: random generator */
	cdev_pf_init(NPF,pf),		/* 21: packet filter */
	cdev_tty_init(1,pdc),		/* 22: PDC device */
	cdev_tty_init(NCOM,com),	/* 23: RS232 */
	cdev_disk_init(NFD,fd),		/* 24: floppy drive */
	cdev_ksyms_init(NKSYMS,ksyms),	/* 25: Kernel symbols device */
	cdev_lpt_init(NLPT,lpt),	/* 26: parallel printer */
	cdev_wsdisplay_init(NWSDISPLAY,	/* 27: workstation console */
	    wsdisplay),
	cdev_mouse_init(NWSKBD,wskbd),	/* 28: keyboards */
	cdev_mouse_init(NWSMOUSE,wsmouse), /* 29: mice */
	cdev_mouse_init(NWSMUX,wsmux),	/* 30: mux */
#ifdef USER_PCICONF
	cdev_pci_init(NPCI,pci),	/* 31: PCI user */
#else
	cdev_notdef(),			/* 31: */
#endif
	cdev_dt_init(NDT,dt),		/* 32: dynamic tracer */
	cdev_video_init(NVIDEO,video),	/* 33: generic video I/O */
	cdev_notdef(),			/* 34 */
	cdev_audio_init(NAUDIO,audio),	/* 35: /dev/audio */
	cdev_notdef(),			/* 36: was: /dev/crypto */
	cdev_bio_init(NBIO,bio),	/* 37: ioctl tunnel */
	cdev_ptm_init(NPTY,ptm),	/* 38: pseudo-tty ptm device */
	cdev_disk_init(NWD,wd),		/* 39: ST506 disk */
	cdev_usb_init(NUSB,usb),	/* 40: USB controller */
	cdev_usbdev_init(NUHID,uhid),	/* 41: USB generic HID */
	cdev_usbdev_init(NUGEN,ugen),	/* 42: USB generic driver */
	cdev_ulpt_init(NULPT,ulpt),	/* 43: USB printers */
	cdev_notdef(),			/* 44: was urio */
	cdev_tty_init(NUCOM,ucom),	/* 45: USB tty */
	cdev_notdef(),			/* 46: was USB scanners */
	cdev_hotplug_init(NHOTPLUG,hotplug), /* 47: devices hot plugging */
	cdev_notdef(),			/* 48: */
	cdev_notdef(),			/* 49: */
	cdev_notdef(),			/* 50: */
	cdev_kstat_init(NKSTAT,kstat),	/* 51: kernel statistics */
	cdev_notdef(),			/* 52: */
	cdev_notdef(),			/* 53: */
	cdev_vscsi_init(NVSCSI,vscsi),	/* 54: vscsi */
	cdev_notdef(),
	cdev_disk_init(1,diskmap),	/* 56: disk mapper */
	cdev_pppx_init(NPPPX,pppx),	/* 57: pppx */
	cdev_fuse_init(NFUSE,fuse),	/* 58: fuse */
	cdev_tun_init(NTUN,tap),	/* 59: Ethernet network tunnel */
	cdev_notdef(),			/* 60: was switch(4) */
	cdev_fido_init(NFIDO,fido),	/* 61: FIDO/U2F security key */
	cdev_pppx_init(NPPPX,pppac),	/* 62: PPP Access Concentrator */
	cdev_ujoy_init(NUJOY,ujoy),	/* 63: USB joystick/gamecontroller */
};
int nchrdev = nitems(cdevsw);

int mem_no = 2;		/* major device number of memory special file */

/*
 * Swapdev is a fake device implemented
 * in sw.c used only internally to get to swstrategy.
 * It cannot be provided to the users, because the
 * swstrategy routine munches the b_dev and b_blkno entries
 * before calling the appropriate driver.  This would horribly
 * confuse, e.g. the hashing routines. Instead, /dev/drum is
 * provided as a character (raw) device.
 */
dev_t   swapdev = makedev(0, 0);

const int chrtoblktbl[] = {
	/*VCHR*/	/*VBLK*/
	/*  0 */	NODEV,
	/*  1 */	NODEV,
	/*  2 */	NODEV,
	/*  3 */	NODEV,
	/*  4 */	NODEV,
	/*  5 */	NODEV,
	/*  6 */	NODEV,
	/*  7 */	NODEV,
	/*  8 */	2,		/* vnd */
	/*  9 */	3,		/* rd */
	/* 10 */	4,		/* sd */
	/* 11 */	NODEV,
	/* 12 */	6,		/* cd */
	/* 13 */	NODEV,
	/* 14 */	NODEV,
	/* 15 */	NODEV,
	/* 16 */	NODEV,
	/* 17 */	NODEV,
	/* 18 */	NODEV,
	/* 19 */	NODEV,
	/* 20 */	NODEV,
	/* 21 */	NODEV,
	/* 22 */	NODEV,
	/* 23 */	NODEV,
	/* 24 */	7,		/* fd */
	/* 25 */	NODEV,
	/* 26 */	NODEV,
	/* 27 */	NODEV,
	/* 28 */	NODEV,
	/* 29 */	NODEV,
	/* 30 */	NODEV,
	/* 31 */	NODEV,
	/* 32 */	NODEV,
	/* 33 */	NODEV,
	/* 34 */	NODEV,
	/* 35 */	NODEV,
	/* 36 */	NODEV,
	/* 37 */	NODEV,
	/* 38 */	NODEV,
	/* 39 */	8,		/* wd */
};
const int nchrtoblktbl = nitems(chrtoblktbl);

/*
 * Returns true if dev is /dev/zero.
 */
int
iszerodev(dev_t dev)
{
	return (major(dev) == mem_no && minor(dev) == 12);
}

dev_t
getnulldev(void)
{
	return makedev(mem_no, 2);
}

/*
 * Returns true if dev is /dev/mem or /dev/kmem.
 */
int
iskmemdev(dev_t dev)
{
	return (major(dev) == mem_no && minor(dev) < 2);
}

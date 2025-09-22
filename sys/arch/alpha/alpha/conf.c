/*	$OpenBSD: conf.c,v 1.94 2025/06/29 15:55:21 miod Exp $	*/
/*	$NetBSD: conf.c,v 1.16 1996/10/18 21:26:57 cgd Exp $	*/

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
 *      @(#)conf.c	7.9 (Berkeley) 5/28/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/vnode.h>

#include "wd.h"
bdev_decl(wd);
#include "fd.h"
bdev_decl(fd);
#include "st.h"
#include "cd.h"
#include "sd.h"
#include "uk.h"
#include "vnd.h"
#include "rd.h"
#include "bktr.h"
#include "radio.h"

struct bdevsw	bdevsw[] =
{
	bdev_disk_init(NWD,wd),		/* 0: ST506/ESDI/IDE disk */
	bdev_swap_init(1,sw),		/* 1: swap pseudo-device */
	bdev_notdef(),			/* 2: was: SCSI tape */
	bdev_disk_init(NCD,cd),		/* 3: SCSI CD-ROM */
	bdev_disk_init(NFD,fd),		/* 4: Floppy disk */
	bdev_notdef(),			/* 5 */
	bdev_disk_init(NRD,rd),		/* 6: ram disk driver */
	bdev_notdef(),			/* 7: was: concatenated disk driver */
	bdev_disk_init(NSD,sd),		/* 8: SCSI disk */
	bdev_disk_init(NVND,vnd),	/* 9: vnode disk driver */
	bdev_notdef(),			/* 10 */
	bdev_notdef(),			/* 11 */
	bdev_notdef(),			/* 12 */
	bdev_notdef(),			/* 13 */
	bdev_notdef(),			/* 14 */
	bdev_notdef(),			/* 15 */
	bdev_notdef(),			/* 16 was: RAIDframe disk driver */
};
int	nblkdev = nitems(bdevsw);

#define	mmread  mmrw
#define	mmwrite mmrw
cdev_decl(mm);
#include "pty.h"
#include "tun.h"
#include "bpfilter.h"
#include "ch.h"
#include "zs.h"
cdev_decl(zs);
#include "audio.h"
#include "video.h"
#include "com.h"
cdev_decl(com);
#include "wsdisplay.h"
#include "wskbd.h"
#include "wsmouse.h"
#include "wsmux.h"
#include "midi.h"
cdev_decl(midi);

#include "spkr.h"
cdev_decl(spkr);

#include "bio.h"
#include "lpt.h"
cdev_decl(lpt);
cdev_decl(wd);
cdev_decl(fd);
#include "cy.h"
cdev_decl(cy);
#include "ksyms.h"
#include "kstat.h"

/* USB Devices */
#include "usb.h"
#include "uhid.h"
#include "fido.h"
#include "ujoy.h"
#include "ugen.h"
#include "ulpt.h"
#include "ucom.h"
#include "dt.h"
#include "pf.h"
#ifdef USER_PCICONF
#include "pci.h"
cdev_decl(pci);
#endif

#include "hotplug.h"
#include "vscsi.h"
#include "pppx.h"
#include "fuse.h"

struct cdevsw	cdevsw[] =
{
	cdev_cn_init(1,cn),		/* 0: virtual console */
	cdev_ctty_init(1,ctty),		/* 1: controlling terminal */
	cdev_mm_init(1,mm),		/* 2: /dev/{null,mem,kmem,...} */
	cdev_notdef(),			/* 3 was /dev/drum */
	cdev_tty_init(NPTY,pts),	/* 4: pseudo-tty slave */
	cdev_ptc_init(NPTY,ptc),	/* 5: pseudo-tty master */
	cdev_log_init(1,log),		/* 6: /dev/klog */
	cdev_tun_init(NTUN,tun),	/* 7: network tunnel */
	cdev_disk_init(NSD,sd),		/* 8: SCSI disk */
	cdev_disk_init(NVND,vnd),	/* 9: vnode disk driver */
	cdev_fd_init(1,filedesc),	/* 10: file descriptor pseudo-dev */
	cdev_bpf_init(NBPFILTER,bpf),	/* 11: Berkeley packet filter */
	cdev_tape_init(NST,st),		/* 12: SCSI tape */
	cdev_disk_init(NCD,cd),		/* 13: SCSI CD-ROM */
	cdev_ch_init(NCH,ch),		/* 14: SCSI autochanger */
	cdev_tty_init(NZS,zs),		/* 15: Z8530 serial interface */
	cdev_notdef(),			/* 16 was lkm */
	cdev_notdef(),			/* 17 */
	cdev_notdef(),			/* 18 */
	cdev_notdef(),			/* 19 */
	cdev_notdef(),			/* 20 */
	cdev_notdef(),			/* 21 */
	cdev_notdef(),			/* 22 */
	cdev_notdef(),			/* 23 reserved for PROM console */
	cdev_audio_init(NAUDIO,audio),	/* 24: generic audio I/O */
	cdev_wsdisplay_init(NWSDISPLAY,wsdisplay), /* 25: workstation console */
	cdev_tty_init(NCOM,com),	/* 26: ns16550 UART */
	cdev_notdef(),			/* 27: was: concatenated disk driver */
	cdev_disk_init(NRD,rd),		/* 28: ram disk driver */
	cdev_mouse_init(NWSKBD,wskbd),	/* 29: /dev/kbd XXX */
	cdev_mouse_init(NWSMOUSE,wsmouse),	/* 30: /dev/mouse XXX */
	cdev_lpt_init(NLPT,lpt),	/* 31: parallel printer */
	cdev_dt_init(NDT,dt),		/* 32: dynamic tracer */
	cdev_uk_init(NUK,uk),		/* 33: SCSI unknown */
	cdev_random_init(1,random),	/* 34: random data source */
	cdev_pf_init(NPF, pf),		/* 35: packet filter */
	cdev_disk_init(NWD,wd), 	/* 36: ST506/ESDI/IDE disk */
	cdev_disk_init(NFD,fd),		/* 37: Floppy disk */
	cdev_tty_init(NCY,cy),		/* 38: Cyclom serial port */
	cdev_ksyms_init(NKSYMS,ksyms),	/* 39: Kernel symbols device */
	cdev_spkr_init(NSPKR,spkr),	/* 40: PC speaker */
	cdev_midi_init(NMIDI,midi),     /* 41: MIDI I/O */
	cdev_notdef(),   		/* 42 was: sequencer I/O */
	cdev_notdef(),			/* 43 was: RAIDframe disk driver */
	cdev_video_init(NVIDEO,video),	/* 44: generic video I/O */
	cdev_usb_init(NUSB,usb),	/* 45: USB controller */
	cdev_usbdev_init(NUHID,uhid),	/* 46: USB generic HID */
	cdev_ulpt_init(NULPT,ulpt),	/* 47: USB printer */
	cdev_usbdev_init(NUGEN,ugen),	/* 48: USB generic driver */
	cdev_tty_init(NUCOM, ucom),	/* 49: USB tty */
	cdev_notdef(),			/* 50 */
	cdev_kstat_init(NKSTAT,kstat),	/* 51: kernel statistics */
#ifdef USER_PCICONF
	cdev_pci_init(NPCI,pci),	/* 52: PCI user */
#else
	cdev_notdef(),
#endif
	cdev_bio_init(NBIO,bio),	/* 53: ioctl tunnel */
	cdev_notdef(),
	cdev_ptm_init(NPTY,ptm),	/* 55: pseudo-tty ptm device */
	cdev_hotplug_init(NHOTPLUG,hotplug), /* 56: devices hot plugging */
	cdev_notdef(),			/* 57: was: /dev/crypto */
	cdev_bktr_init(NBKTR,bktr),	/* 58: Bt848 video capture device */
	cdev_radio_init(NRADIO,radio), /* 59: generic radio I/O */
	cdev_mouse_init(NWSMUX, wsmux),	/* 60: ws multiplexor */
	cdev_vscsi_init(NVSCSI, vscsi),	/* 61: vscsi */
	cdev_notdef(),
	cdev_disk_init(1,diskmap),	/* 63: disk mapper */
	cdev_pppx_init(NPPPX,pppx),	/* 64: pppx */
	cdev_notdef(),			/* 65: was urio */
	cdev_notdef(),			/* 66: was USB scanners */
	cdev_fuse_init(NFUSE,fuse),	/* 67: fuse */
	cdev_tun_init(NTUN,tap),	/* 68: Ethernet network tunnel */
	cdev_notdef(),			/* 69: was switch(4) */
	cdev_fido_init(NFIDO,fido),	/* 70: FIDO/U2F security key */
	cdev_pppx_init(NPPPX,pppac),	/* 71: PPP Access Concentrator */
	cdev_ujoy_init(NUJOY,ujoy),	/* 72: USB joystick/gamecontroller */
};
int	nchrdev = nitems(cdevsw);

int	mem_no = 2; 	/* major device number of memory special file */

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
 * Returns true if dev is /dev/mem or /dev/kmem.
 */
int
iskmemdev(dev_t dev)
{

	return (major(dev) == mem_no && minor(dev) < 2);
}

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
	/*  8 */	8,		/* sd */
	/*  9 */	9,		/* vnd */
	/* 10 */	NODEV,
	/* 11 */	NODEV,
	/* 12 */	NODEV,
	/* 13 */	3,		/* cd */
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
	/* 24 */	NODEV,
	/* 25 */	NODEV,
	/* 26 */	NODEV,
	/* 27 */	NODEV,
	/* 28 */	6,		/* rd */
	/* 29 */	NODEV,
	/* 30 */	NODEV,
	/* 31 */	NODEV,
	/* 32 */	NODEV,
	/* 33 */	NODEV,
	/* 34 */	NODEV,
	/* 35 */	NODEV,
	/* 36 */	0,
	/* 37 */	4,		/* fd */
};
const int nchrtoblktbl = nitems(chrtoblktbl);

/*	$OpenBSD: conf.c,v 1.46 2022/10/15 10:12:13 jsg Exp $	*/

/*
 * Copyright (c) 1994-1998 Mark Brinicombe.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Mark Brinicombe
 *	for the NetBSD Project.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * conf.c
 *
 * Character and Block Device configuration
 * Console configuration
 *
 * Defines the structures [bc]devsw
 *
 * Created      : 17/09/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/vnode.h>

#include <machine/conf.h>

/*
 * From this point, these need to be MI foo.h files.
 */

/*
 * Standard MI devices (e.g. ones in dev/ic)
 */
#include "com.h"		/* NS164x0 serial ports */

/*
 * Standard pseudo-devices
 */
#include "bpfilter.h"
#include "dt.h"
#include "pf.h"
#include "bio.h"
#include "pty.h"
#include "tun.h"
#include "ksyms.h"
#include "kstat.h"

/*
 * Disk/Filesystem pseudo-devices
 */
#include "rd.h"				/* memory disk driver */
#include "vnd.h"			/* vnode disk driver */

/*
 * WD/ATA devices
 */
#include "wd.h"
bdev_decl(wd);

#ifdef USER_PCICONF
#include "pci.h"
cdev_decl(pci);
#endif

/*
 * SCSI/ATAPI devices
 */
#include "sd.h"
#include "st.h"
#include "cd.h"
#include "ch.h"
#include "uk.h"

/*
 * Audio devices
 */
#include "audio.h"
#include "video.h"
#include "midi.h"

/*
 * USB devices
 */
#include "usb.h"
#include "ucom.h"
#include "ugen.h"
#include "uhid.h"
#include "fido.h"
#include "ujoy.h"
#include "ulpt.h"

/*
 * WSCONS devices
 */
#include "wsdisplay.h"
/*
#include "wsfont.h"
*/
#include "wskbd.h"
#include "wsmouse.h"
#include "wsmux.h"
cdev_decl(wskbd);
cdev_decl(wsmouse);

#include "lpt.h"
#include "radio.h"
cdev_decl(radio);

/* Block devices */

struct bdevsw bdevsw[] = {
	bdev_notdef(),			/*  0: */
	bdev_swap_init(1, sw),		/*  1: swap pseudo-device */
	bdev_notdef(),			/*  2: */
	bdev_notdef(),			/*  3: */
	bdev_notdef(),			/*  4: */
	bdev_notdef(),			/*  5: */
	bdev_notdef(),			/*  6: */
	bdev_notdef(),			/*  7: */
	bdev_notdef(),			/*  8: */
	bdev_notdef(),			/*  9: */
	bdev_notdef(),			/* 10: */
	bdev_notdef(),			/* 11: */
	bdev_notdef(),			/* 12: */
	bdev_notdef(),			/* 13: */
	bdev_notdef(),			/* 14: */
	bdev_notdef(),			/* 15: */
	bdev_disk_init(NWD,wd),		/* 16: Internal IDE disk */
	bdev_notdef(),			/* 17: */
	bdev_disk_init(NRD,rd),		/* 18: memory disk */
	bdev_disk_init(NVND,vnd),	/* 19: vnode disk driver */
	bdev_notdef(),			/* 20: */
 	bdev_notdef(),			/* 21: was: concatenated disk driver */
	bdev_notdef(),			/* 22: */
	bdev_notdef(),			/* 23: */
	bdev_disk_init(NSD,sd),		/* 24: SCSI disk */
	bdev_notdef(),			/* 25: was: SCSI tape */
	bdev_disk_init(NCD,cd),		/* 26: SCSI cdrom */
	bdev_notdef(),			/* 27: */
	bdev_notdef(),			/* 28: */
	bdev_notdef(),			/* 29: */
	bdev_notdef(),			/* 30: */
	bdev_notdef(),			/* 31: */
	bdev_notdef(),			/* 32: */
	bdev_notdef(),			/* 33: */
	bdev_notdef(),			/* 34: */
	bdev_notdef(),			/* 35: */
	bdev_notdef(),			/* 36: */
	bdev_notdef(),			/* 37: */
	bdev_notdef(),			/* 38: */
	bdev_notdef(),			/* 39: */
	bdev_notdef(),			/* 40: */
	bdev_notdef(),			/* 41: */
	bdev_notdef(),			/* 42: */
	bdev_notdef(),			/* 43: */
	bdev_notdef(),			/* 44: */
	bdev_notdef(),			/* 45: */
	bdev_notdef(),			/* 46: */
	bdev_notdef(),			/* 47: */
	bdev_notdef(),			/* 48: */
	bdev_notdef(),			/* 49: */
	bdev_notdef(),			/* 50: */
	bdev_notdef(),			/* 51: */
	bdev_notdef(),			/* 52: */
	bdev_notdef(),			/* 53: */
	bdev_notdef(),			/* 54: */
	bdev_notdef(),			/* 55: */
	bdev_notdef(),			/* 56: */
	bdev_notdef(),			/* 57: */
	bdev_notdef(),			/* 58: */
	bdev_notdef(),			/* 59: */
	bdev_notdef(),			/* 60: */
	bdev_notdef(),			/* 61: */
	bdev_notdef(),			/* 62: */
	bdev_notdef(),			/* 63: */
	bdev_notdef(),			/* 64: */
	bdev_notdef(),			/* 65: */
	bdev_notdef(),			/* 66: */
	bdev_notdef(),			/* 67: */
	bdev_notdef(),			/* 68: */
	bdev_notdef(),			/* 69: */
	bdev_notdef(),			/* 70: */
	bdev_notdef(),			/* 71 was: RAIDframe disk driver */
	bdev_notdef(),			/* 72: */
	bdev_notdef(),			/* 73: */
	bdev_notdef(),			/* 74: */
	bdev_notdef(),			/* 75: */
	bdev_notdef(),			/* 76: */
	bdev_notdef(),			/* 77: */
	bdev_notdef(),			/* 78: */
	bdev_notdef(),			/* 79: */
	bdev_notdef(),			/* 80: */
	bdev_notdef(),			/* 81: */
	bdev_notdef(),			/* 82: */
	bdev_notdef(),			/* 83: */
	bdev_notdef(),			/* 84: */
	bdev_notdef(),			/* 85: */
	bdev_notdef(),			/* 86: */
	bdev_notdef(),			/* 87: */
	bdev_notdef(),			/* 88: */
	bdev_notdef(),			/* 89: */
	bdev_notdef(),			/* 90: */
	bdev_notdef(),			/* 91: */
	bdev_notdef(),			/* 93: */
	bdev_notdef(),			/* 94: */
	bdev_notdef(),			/* 95: */
	bdev_notdef(),			/* 96: */
	bdev_notdef(),			/* 97: */
};

#include "hotplug.h"
#include "scif.h"
#include "vscsi.h"
#include "pppx.h"
#include "fuse.h"

struct cdevsw cdevsw[] = {
	cdev_cn_init(1,cn),			/*  0: virtual console */
	cdev_ctty_init(1,ctty),			/*  1: controlling terminal */
	cdev_mm_init(1,mm),			/*  2: /dev/{null,mem,kmem,...} */
	cdev_notdef(),				/*  3 was /dev/drum */
	cdev_tty_init(NPTY,pts),		/*  4: pseudo-tty slave */
	cdev_ptc_init(NPTY,ptc),		/*  5: pseudo-tty master */
	cdev_log_init(1,log),			/*  6: /dev/klog */
	cdev_fd_init(1,filedesc),		/*  7: file descriptor pseudo-device */
	cdev_ksyms_init(NKSYMS,ksyms),		/*  8: Kernel symbols device */
	cdev_notdef(),				/*  9: */
	cdev_notdef(),				/* 10: */
	cdev_tty_init(NSCIF,scif),		/* 11: scif */
	cdev_tty_init(NCOM,com),		/* 12: serial port */
	cdev_notdef(),				/* 13: */
	cdev_notdef(),				/* 14: */
	cdev_notdef(),				/* 15: */
	cdev_disk_init(NWD,wd),			/* 16: ST506/ESDI/IDE disk */
	cdev_notdef(),				/* 17: */
	cdev_disk_init(NRD,rd),			/* 18: ram disk driver */
	cdev_disk_init(NVND,vnd),		/* 19: vnode disk driver */
	cdev_notdef(),				/* 20: */
	cdev_notdef(),				/* 21: was: concatenated disk driver */
	cdev_bpf_init(NBPFILTER,bpf),		/* 22: Berkeley packet filter */
	cdev_notdef(),				/* 23: */
	cdev_disk_init(NSD,sd),			/* 24: SCSI disk */
	cdev_tape_init(NST,st),			/* 25: SCSI tape */
	cdev_disk_init(NCD,cd),			/* 26: SCSI CD-ROM */
	cdev_ch_init(NCH,ch),	 		/* 27: SCSI autochanger */
	cdev_uk_init(NUK,uk),	 		/* 28: SCSI unknown */
	cdev_notdef(),				/* 29: */
	cdev_dt_init(NDT,dt),			/* 30: dynamic tracer */
	cdev_notdef(),				/* 31: */
	cdev_notdef(),				/* 32: */
	cdev_tun_init(NTUN,tun),		/* 33: network tunnel */
	cdev_notdef(),				/* 34: */
	cdev_notdef(),				/* 35: was LKM */
	cdev_audio_init(NAUDIO,audio),		/* 36: generic audio I/O */
	cdev_hotplug_init(NHOTPLUG,hotplug),	/* 37: devices hot plugging*/
	cdev_bio_init(NBIO,bio),		/* 38: ioctl tunnel */
	cdev_notdef(),				/* 39: reserved */
	cdev_random_init(1,random),		/* 40: random generator */
	cdev_notdef(),				/* 41: reserved */
	cdev_notdef(),				/* 42: reserved */
	cdev_notdef(),				/* 43: reserved */
	cdev_notdef(),				/* 44: reserved */
	cdev_notdef(),				/* 45: reserved */
	cdev_pf_init(NPF,pf),           	/* 46: packet filter */
	cdev_notdef(),				/* 47: was /dev/crypto */
	cdev_notdef(),				/* 48: reserved */
	cdev_notdef(),				/* 49: reserved */
	cdev_notdef(),				/* 50: reserved */
	cdev_kstat_init(NKSTAT,kstat),		/* 51: kernel statistics */
	cdev_notdef(),				/* 52: reserved */
	cdev_notdef(),				/* 53: reserved */
	cdev_notdef(),				/* 54: reserved */
	cdev_notdef(),				/* 55: Reserved for bypass device */	
	cdev_notdef(),				/* 56: reserved */
	cdev_midi_init(NMIDI,midi),		/* 57: MIDI I/O */
	cdev_notdef(),				/* 58 was: sequencer I/O */
	cdev_notdef(),				/* 59: reserved */
	cdev_wsdisplay_init(NWSDISPLAY,wsdisplay), /* 60: frame buffers, etc.*/
	cdev_mouse_init(NWSKBD,wskbd),		/* 61: keyboards */
	cdev_mouse_init(NWSMOUSE,wsmouse),	/* 62: mice */
	cdev_mouse_init(NWSMUX,wsmux),		/* 63: ws multiplexor */
	cdev_usb_init(NUSB,usb),		/* 64: USB controller */
	cdev_usbdev_init(NUHID,uhid),		/* 65: USB generic HID */
	cdev_ulpt_init(NULPT,ulpt),		/* 66: USB printer */
	cdev_notdef(),				/* 67: was urio */
	cdev_tty_init(NUCOM,ucom),		/* 68: USB tty */
	cdev_notdef(),				/* 69: was USB scanners */
	cdev_usbdev_init(NUGEN,ugen),		/* 70: USB generic driver */
	cdev_notdef(),		    		/* 71 was: RAIDframe disk driver */
	cdev_notdef(),				/* 72: reserved */
	cdev_notdef(),				/* 73: reserved */
	cdev_notdef(),				/* 74: reserved */
	cdev_notdef(),				/* 75: reserved */
	cdev_notdef(),				/* 76: reserved */
	cdev_video_init(NVIDEO,video),		/* 77: generic video I/O */
	cdev_notdef(),                          /* 78: removed device */
	cdev_notdef(),                          /* 79: removed device */
	cdev_notdef(),                          /* 80: removed device */
	cdev_notdef(),                          /* 81: removed device */
	cdev_notdef(),                          /* 82: removed device */
	cdev_notdef(),                          /* 83: removed device */
	cdev_notdef(),                          /* 84: removed device */
	cdev_notdef(),                          /* 85: removed device */
	cdev_notdef(),                          /* 86: removed device */
	cdev_notdef(),                          /* 87: removed device */
#ifdef USER_PCICONF
	cdev_pci_init(NPCI,pci),		/* 88: PCI user */
#else
	cdev_notdef(),
#endif
	cdev_notdef(),                          /* 89: removed device */
	cdev_notdef(),                          /* 90: removed device */
	cdev_notdef(),                          /* 91: removed device */
	cdev_notdef(),                          /* 92: removed device */
	cdev_notdef(),                          /* 93: removed device */
	cdev_notdef(),                          /* 94: removed device */
	cdev_notdef(),                          /* 95: removed device */
	cdev_notdef(),                          /* 96: removed device */
	cdev_radio_init(NRADIO,radio),		/* 97: generic radio I/O */
	cdev_ptm_init(NPTY,ptm),		/* 98: pseudo-tty ptm device */
	cdev_vscsi_init(NVSCSI,vscsi),		/* 99: vscsi */
	cdev_notdef(),
	cdev_disk_init(1,diskmap),		/* 101: disk mapper */
	cdev_pppx_init(NPPPX,pppx),		/* 102: pppx */
	cdev_fuse_init(NFUSE,fuse),		/* 103: fuse */
	cdev_tun_init(NTUN,tap),		/* 104: Ethernet network tap */
	cdev_notdef(),				/* 105: was switch(4) */
	cdev_fido_init(NFIDO,fido),		/* 106: FIDO/U2F security key */
	cdev_pppx_init(NPPPX,pppac),		/* 107: PPP Access Concentrator */
	cdev_ujoy_init(NUJOY,ujoy),		/* 108: USB joystick/gamecontroller */
};

int nblkdev = nitems(bdevsw);
int nchrdev = nitems(cdevsw);

int mem_no = 2; 	/* major device number of memory special file */

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
	return (major(dev) == mem_no && minor(dev) == 3);
}


const int chrtoblktbl[] = {
    /*VCHR*/        /*VBLK*/
    /*  0 */        NODEV,
    /*  1 */        NODEV,
    /*  2 */        NODEV,
    /*  3 */        NODEV,
    /*  4 */        NODEV,
    /*  5 */        NODEV,
    /*  6 */        NODEV,
    /*  7 */        NODEV,
    /*  8 */        NODEV,
    /*  9 */        NODEV,
    /* 10 */        NODEV,
    /* 11 */        NODEV,
    /* 12 */        NODEV,
    /* 13 */        NODEV,
    /* 14 */        NODEV,
    /* 15 */        NODEV,
    /* 16 */        16,			/* wd */
    /* 17 */        NODEV,
    /* 18 */        18,			/* rd */
    /* 19 */        19,			/* vnd */
    /* 20 */        NODEV,
    /* 21 */        NODEV,
    /* 22 */        NODEV,
    /* 23 */        NODEV,
    /* 24 */        24,			/* sd */
    /* 25 */        NODEV,
    /* 26 */        26,			/* cd */
};
const int nchrtoblktbl = nitems(chrtoblktbl);


dev_t
getnulldev(void)
{
	return makedev(mem_no, 2);
}

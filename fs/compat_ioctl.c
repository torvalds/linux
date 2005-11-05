/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 * Copyright (C) 2003       Pavel Machek (pavel@suse.cz)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#ifdef INCLUDES
#include <linux/config.h>
#include <linux/types.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/ioctl.h>
#include <linux/if.h>
#include <linux/if_bridge.h>
#include <linux/slab.h>
#include <linux/hdreg.h>
#include <linux/raid/md.h>
#include <linux/kd.h>
#include <linux/dirent.h>
#include <linux/route.h>
#include <linux/in6.h>
#include <linux/ipv6_route.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/vt.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fd.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/if_pppox.h>
#include <linux/mtio.h>
#include <linux/cdrom.h>
#include <linux/loop.h>
#include <linux/auto_fs.h>
#include <linux/auto_fs4.h>
#include <linux/devfs_fs.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/fb.h>
#include <linux/ext2_fs.h>
#include <linux/videodev.h>
#include <linux/netdevice.h>
#include <linux/raw.h>
#include <linux/smb_fs.h>
#include <linux/blkpg.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/rtc.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/reiserfs_fs.h>
#include <linux/if_tun.h>
#include <linux/ctype.h>
#include <linux/ioctl32.h>
#include <linux/syscalls.h>
#include <linux/ncp_fs.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/wireless.h>
#include <linux/atalk.h>

#include <net/sock.h>          /* siocdevprivate_ioctl */
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <net/bluetooth/rfcomm.h>

#include <linux/capi.h>

#include <scsi/scsi.h>
/* Ugly hack. */
#undef __KERNEL__
#include <scsi/scsi_ioctl.h>
#define __KERNEL__
#include <scsi/sg.h>

#include <asm/types.h>
#include <asm/uaccess.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_bonding.h>
#include <linux/watchdog.h>
#include <linux/dm-ioctl.h>

#include <asm/module.h>
#include <linux/soundcard.h>
#include <linux/lp.h>
#include <linux/ppdev.h>

#include <linux/atm.h>
#include <linux/atmarp.h>
#include <linux/atmclip.h>
#include <linux/atmdev.h>
#include <linux/atmioc.h>
#include <linux/atmlec.h>
#include <linux/atmmpc.h>
#include <linux/atmsvc.h>
#include <linux/atm_tcp.h>
#include <linux/sonet.h>
#include <linux/atm_suni.h>
#include <linux/mtd/mtd.h>

#include <linux/usb.h>
#include <linux/usbdevice_fs.h>
#include <linux/nbd.h>
#include <linux/random.h>
#include <linux/filter.h>
#include <linux/msdos_fs.h>
#include <linux/pktcdvd.h>

#include <linux/hiddev.h>

#undef INCLUDES
#endif

#ifdef CODE

/* Aiee. Someone does not find a difference between int and long */
#define EXT2_IOC32_GETFLAGS               _IOR('f', 1, int)
#define EXT2_IOC32_SETFLAGS               _IOW('f', 2, int)
#define EXT2_IOC32_GETVERSION             _IOR('v', 1, int)
#define EXT2_IOC32_SETVERSION             _IOW('v', 2, int)

static int w_long(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	int err;
	unsigned long val;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&val);
	set_fs (old_fs);
	if (!err && put_user(val, (u32 __user *)compat_ptr(arg)))
		return -EFAULT;
	return err;
}
 
static int rw_long(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	u32 __user *argptr = compat_ptr(arg);
	int err;
	unsigned long val;
	
	if(get_user(val, argptr))
		return -EFAULT;
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&val);
	set_fs (old_fs);
	if (!err && put_user(val, argptr))
		return -EFAULT;
	return err;
}

static int do_ext2_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case EXT2_IOC32_GETFLAGS: cmd = EXT2_IOC_GETFLAGS; break;
	case EXT2_IOC32_SETFLAGS: cmd = EXT2_IOC_SETFLAGS; break;
	case EXT2_IOC32_GETVERSION: cmd = EXT2_IOC_GETVERSION; break;
	case EXT2_IOC32_SETVERSION: cmd = EXT2_IOC_SETVERSION; break;
	}
	return sys_ioctl(fd, cmd, (unsigned long)compat_ptr(arg));
}

struct video_tuner32 {
	compat_int_t tuner;
	char name[32];
	compat_ulong_t rangelow, rangehigh;
	u32 flags;	/* It is really u32 in videodev.h */
	u16 mode, signal;
};

static int get_video_tuner32(struct video_tuner *kp, struct video_tuner32 __user *up)
{
	int i;

	if(get_user(kp->tuner, &up->tuner))
		return -EFAULT;
	for(i = 0; i < 32; i++)
		__get_user(kp->name[i], &up->name[i]);
	__get_user(kp->rangelow, &up->rangelow);
	__get_user(kp->rangehigh, &up->rangehigh);
	__get_user(kp->flags, &up->flags);
	__get_user(kp->mode, &up->mode);
	__get_user(kp->signal, &up->signal);
	return 0;
}

static int put_video_tuner32(struct video_tuner *kp, struct video_tuner32 __user *up)
{
	int i;

	if(put_user(kp->tuner, &up->tuner))
		return -EFAULT;
	for(i = 0; i < 32; i++)
		__put_user(kp->name[i], &up->name[i]);
	__put_user(kp->rangelow, &up->rangelow);
	__put_user(kp->rangehigh, &up->rangehigh);
	__put_user(kp->flags, &up->flags);
	__put_user(kp->mode, &up->mode);
	__put_user(kp->signal, &up->signal);
	return 0;
}

struct video_buffer32 {
	compat_caddr_t base;
	compat_int_t height, width, depth, bytesperline;
};

static int get_video_buffer32(struct video_buffer *kp, struct video_buffer32 __user *up)
{
	u32 tmp;

	if (get_user(tmp, &up->base))
		return -EFAULT;

	/* This is actually a physical address stored
	 * as a void pointer.
	 */
	kp->base = (void *)(unsigned long) tmp;

	__get_user(kp->height, &up->height);
	__get_user(kp->width, &up->width);
	__get_user(kp->depth, &up->depth);
	__get_user(kp->bytesperline, &up->bytesperline);
	return 0;
}

static int put_video_buffer32(struct video_buffer *kp, struct video_buffer32 __user *up)
{
	u32 tmp = (u32)((unsigned long)kp->base);

	if(put_user(tmp, &up->base))
		return -EFAULT;
	__put_user(kp->height, &up->height);
	__put_user(kp->width, &up->width);
	__put_user(kp->depth, &up->depth);
	__put_user(kp->bytesperline, &up->bytesperline);
	return 0;
}

struct video_clip32 {
	s32 x, y, width, height;	/* Its really s32 in videodev.h */
	compat_caddr_t next;
};

struct video_window32 {
	u32 x, y, width, height, chromakey, flags;
	compat_caddr_t clips;
	compat_int_t clipcount;
};

/* You get back everything except the clips... */
static int put_video_window32(struct video_window *kp, struct video_window32 __user *up)
{
	if(put_user(kp->x, &up->x))
		return -EFAULT;
	__put_user(kp->y, &up->y);
	__put_user(kp->width, &up->width);
	__put_user(kp->height, &up->height);
	__put_user(kp->chromakey, &up->chromakey);
	__put_user(kp->flags, &up->flags);
	__put_user(kp->clipcount, &up->clipcount);
	return 0;
}

#define VIDIOCGTUNER32		_IOWR('v',4, struct video_tuner32)
#define VIDIOCSTUNER32		_IOW('v',5, struct video_tuner32)
#define VIDIOCGWIN32		_IOR('v',9, struct video_window32)
#define VIDIOCSWIN32		_IOW('v',10, struct video_window32)
#define VIDIOCGFBUF32		_IOR('v',11, struct video_buffer32)
#define VIDIOCSFBUF32		_IOW('v',12, struct video_buffer32)
#define VIDIOCGFREQ32		_IOR('v',14, u32)
#define VIDIOCSFREQ32		_IOW('v',15, u32)

enum {
	MaxClips = (~0U-sizeof(struct video_window))/sizeof(struct video_clip)
};

static int do_set_window(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct video_window32 __user *up = compat_ptr(arg);
	struct video_window __user *vw;
	struct video_clip __user *p;
	int nclips;
	u32 n;

	if (get_user(nclips, &up->clipcount))
		return -EFAULT;

	/* Peculiar interface... */
	if (nclips < 0)
		nclips = VIDEO_CLIPMAP_SIZE;

	if (nclips > MaxClips)
		return -ENOMEM;

	vw = compat_alloc_user_space(sizeof(struct video_window) +
				    nclips * sizeof(struct video_clip));

	p = nclips ? (struct video_clip __user *)(vw + 1) : NULL;

	if (get_user(n, &up->x) || put_user(n, &vw->x) ||
	    get_user(n, &up->y) || put_user(n, &vw->y) ||
	    get_user(n, &up->width) || put_user(n, &vw->width) ||
	    get_user(n, &up->height) || put_user(n, &vw->height) ||
	    get_user(n, &up->chromakey) || put_user(n, &vw->chromakey) ||
	    get_user(n, &up->flags) || put_user(n, &vw->flags) ||
	    get_user(n, &up->clipcount) || put_user(n, &vw->clipcount) ||
	    get_user(n, &up->clips) || put_user(p, &vw->clips))
		return -EFAULT;

	if (nclips) {
		struct video_clip32 __user *u = compat_ptr(n);
		int i;
		if (!u)
			return -EINVAL;
		for (i = 0; i < nclips; i++, u++, p++) {
			s32 v;
			if (get_user(v, &u->x) ||
			    put_user(v, &p->x) ||
			    get_user(v, &u->y) ||
			    put_user(v, &p->y) ||
			    get_user(v, &u->width) ||
			    put_user(v, &p->width) ||
			    get_user(v, &u->height) ||
			    put_user(v, &p->height) ||
			    put_user(NULL, &p->next))
				return -EFAULT;
		}
	}

	return sys_ioctl(fd, VIDIOCSWIN, (unsigned long)p);
}

static int do_video_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	union {
		struct video_tuner vt;
		struct video_buffer vb;
		struct video_window vw;
		unsigned long vx;
	} karg;
	mm_segment_t old_fs = get_fs();
	void __user *up = compat_ptr(arg);
	int err = 0;

	/* First, convert the command. */
	switch(cmd) {
	case VIDIOCGTUNER32: cmd = VIDIOCGTUNER; break;
	case VIDIOCSTUNER32: cmd = VIDIOCSTUNER; break;
	case VIDIOCGWIN32: cmd = VIDIOCGWIN; break;
	case VIDIOCGFBUF32: cmd = VIDIOCGFBUF; break;
	case VIDIOCSFBUF32: cmd = VIDIOCSFBUF; break;
	case VIDIOCGFREQ32: cmd = VIDIOCGFREQ; break;
	case VIDIOCSFREQ32: cmd = VIDIOCSFREQ; break;
	};

	switch(cmd) {
	case VIDIOCSTUNER:
	case VIDIOCGTUNER:
		err = get_video_tuner32(&karg.vt, up);
		break;

	case VIDIOCSFBUF:
		err = get_video_buffer32(&karg.vb, up);
		break;

	case VIDIOCSFREQ:
		err = get_user(karg.vx, (u32 __user *)up);
		break;
	};
	if(err)
		goto out;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&karg);
	set_fs(old_fs);

	if(err == 0) {
		switch(cmd) {
		case VIDIOCGTUNER:
			err = put_video_tuner32(&karg.vt, up);
			break;

		case VIDIOCGWIN:
			err = put_video_window32(&karg.vw, up);
			break;

		case VIDIOCGFBUF:
			err = put_video_buffer32(&karg.vb, up);
			break;

		case VIDIOCGFREQ:
			err = put_user(((u32)karg.vx), (u32 __user *)up);
			break;
		};
	}
out:
	return err;
}

#ifdef CONFIG_NET
static int do_siocgstamp(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct compat_timeval __user *up = compat_ptr(arg);
	struct timeval ktv;
	mm_segment_t old_fs = get_fs();
	int err;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&ktv);
	set_fs(old_fs);
	if(!err) {
		err = put_user(ktv.tv_sec, &up->tv_sec);
		err |= __put_user(ktv.tv_usec, &up->tv_usec);
	}
	return err;
}

struct ifmap32 {
	compat_ulong_t mem_start;
	compat_ulong_t mem_end;
	unsigned short base_addr;
	unsigned char irq;
	unsigned char dma;
	unsigned char port;
};

struct ifreq32 {
#define IFHWADDRLEN     6
#define IFNAMSIZ        16
        union {
                char    ifrn_name[IFNAMSIZ];            /* if name, e.g. "en0" */
        } ifr_ifrn;
        union {
                struct  sockaddr ifru_addr;
                struct  sockaddr ifru_dstaddr;
                struct  sockaddr ifru_broadaddr;
                struct  sockaddr ifru_netmask;
                struct  sockaddr ifru_hwaddr;
                short   ifru_flags;
                compat_int_t     ifru_ivalue;
                compat_int_t     ifru_mtu;
                struct  ifmap32 ifru_map;
                char    ifru_slave[IFNAMSIZ];   /* Just fits the size */
		char	ifru_newname[IFNAMSIZ];
                compat_caddr_t ifru_data;
	    /* XXXX? ifru_settings should be here */
        } ifr_ifru;
};

struct ifconf32 {
        compat_int_t	ifc_len;                        /* size of buffer       */
        compat_caddr_t  ifcbuf;
};

static int dev_ifname32(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct net_device *dev;
	struct ifreq32 ifr32;
	int err;

	if (copy_from_user(&ifr32, compat_ptr(arg), sizeof(ifr32)))
		return -EFAULT;

	dev = dev_get_by_index(ifr32.ifr_ifindex);
	if (!dev)
		return -ENODEV;

	strlcpy(ifr32.ifr_name, dev->name, sizeof(ifr32.ifr_name));
	dev_put(dev);
	
	err = copy_to_user(compat_ptr(arg), &ifr32, sizeof(ifr32));
	return (err ? -EFAULT : 0);
}

static int dev_ifconf(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifconf32 ifc32;
	struct ifconf ifc;
	struct ifconf __user *uifc;
	struct ifreq32 __user *ifr32;
	struct ifreq __user *ifr;
	unsigned int i, j;
	int err;

	if (copy_from_user(&ifc32, compat_ptr(arg), sizeof(struct ifconf32)))
		return -EFAULT;

	if (ifc32.ifcbuf == 0) {
		ifc32.ifc_len = 0;
		ifc.ifc_len = 0;
		ifc.ifc_req = NULL;
		uifc = compat_alloc_user_space(sizeof(struct ifconf));
	} else {
		size_t len =((ifc32.ifc_len / sizeof (struct ifreq32)) + 1) *
			sizeof (struct ifreq);
		uifc = compat_alloc_user_space(sizeof(struct ifconf) + len);
		ifc.ifc_len = len;
		ifr = ifc.ifc_req = (void __user *)(uifc + 1);
		ifr32 = compat_ptr(ifc32.ifcbuf);
		for (i = 0; i < ifc32.ifc_len; i += sizeof (struct ifreq32)) {
			if (copy_in_user(ifr, ifr32, sizeof(struct ifreq32)))
				return -EFAULT;
			ifr++;
			ifr32++; 
		}
	}
	if (copy_to_user(uifc, &ifc, sizeof(struct ifconf)))
		return -EFAULT;

	err = sys_ioctl (fd, SIOCGIFCONF, (unsigned long)uifc);	
	if (err)
		return err;

	if (copy_from_user(&ifc, uifc, sizeof(struct ifconf))) 
		return -EFAULT;

	ifr = ifc.ifc_req;
	ifr32 = compat_ptr(ifc32.ifcbuf);
	for (i = 0, j = 0; i < ifc32.ifc_len && j < ifc.ifc_len;
	     i += sizeof (struct ifreq32), j += sizeof (struct ifreq)) {
		if (copy_in_user(ifr32, ifr, sizeof (struct ifreq32)))
			return -EFAULT;
		ifr32++;
		ifr++;
	}

	if (ifc32.ifcbuf == 0) {
		/* Translate from 64-bit structure multiple to
		 * a 32-bit one.
		 */
		i = ifc.ifc_len;
		i = ((i / sizeof(struct ifreq)) * sizeof(struct ifreq32));
		ifc32.ifc_len = i;
	} else {
		if (i <= ifc32.ifc_len)
			ifc32.ifc_len = i;
		else
			ifc32.ifc_len = i - sizeof (struct ifreq32);
	}
	if (copy_to_user(compat_ptr(arg), &ifc32, sizeof(struct ifconf32)))
		return -EFAULT;

	return 0;
}

static int ethtool_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq __user *ifr;
	struct ifreq32 __user *ifr32;
	u32 data;
	void __user *datap;
	
	ifr = compat_alloc_user_space(sizeof(*ifr));
	ifr32 = compat_ptr(arg);

	if (copy_in_user(&ifr->ifr_name, &ifr32->ifr_name, IFNAMSIZ))
		return -EFAULT;

	if (get_user(data, &ifr32->ifr_ifru.ifru_data))
		return -EFAULT;

	datap = compat_ptr(data);
	if (put_user(datap, &ifr->ifr_ifru.ifru_data))
		return -EFAULT;

	return sys_ioctl(fd, cmd, (unsigned long) ifr);
}

static int bond_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq kifr;
	struct ifreq __user *uifr;
	struct ifreq32 __user *ifr32 = compat_ptr(arg);
	mm_segment_t old_fs;
	int err;
	u32 data;
	void __user *datap;

	switch (cmd) {
	case SIOCBONDENSLAVE:
	case SIOCBONDRELEASE:
	case SIOCBONDSETHWADDR:
	case SIOCBONDCHANGEACTIVE:
		if (copy_from_user(&kifr, ifr32, sizeof(struct ifreq32)))
			return -EFAULT;

		old_fs = get_fs();
		set_fs (KERNEL_DS);
		err = sys_ioctl (fd, cmd, (unsigned long)&kifr);
		set_fs (old_fs);

		return err;
	case SIOCBONDSLAVEINFOQUERY:
	case SIOCBONDINFOQUERY:
		uifr = compat_alloc_user_space(sizeof(*uifr));
		if (copy_in_user(&uifr->ifr_name, &ifr32->ifr_name, IFNAMSIZ))
			return -EFAULT;

		if (get_user(data, &ifr32->ifr_ifru.ifru_data))
			return -EFAULT;

		datap = compat_ptr(data);
		if (put_user(datap, &uifr->ifr_ifru.ifru_data))
			return -EFAULT;

		return sys_ioctl (fd, cmd, (unsigned long)uifr);
	default:
		return -EINVAL;
	};
}

int siocdevprivate_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq __user *u_ifreq64;
	struct ifreq32 __user *u_ifreq32 = compat_ptr(arg);
	char tmp_buf[IFNAMSIZ];
	void __user *data64;
	u32 data32;

	if (copy_from_user(&tmp_buf[0], &(u_ifreq32->ifr_ifrn.ifrn_name[0]),
			   IFNAMSIZ))
		return -EFAULT;
	if (__get_user(data32, &u_ifreq32->ifr_ifru.ifru_data))
		return -EFAULT;
	data64 = compat_ptr(data32);

	u_ifreq64 = compat_alloc_user_space(sizeof(*u_ifreq64));

	/* Don't check these user accesses, just let that get trapped
	 * in the ioctl handler instead.
	 */
	if (copy_to_user(&u_ifreq64->ifr_ifrn.ifrn_name[0], &tmp_buf[0],
			 IFNAMSIZ))
		return -EFAULT;
	if (__put_user(data64, &u_ifreq64->ifr_ifru.ifru_data))
		return -EFAULT;

	return sys_ioctl(fd, cmd, (unsigned long) u_ifreq64);
}

static int dev_ifsioc(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq ifr;
	struct ifreq32 __user *uifr32;
	struct ifmap32 __user *uifmap32;
	mm_segment_t old_fs;
	int err;
	
	uifr32 = compat_ptr(arg);
	uifmap32 = &uifr32->ifr_ifru.ifru_map;
	switch (cmd) {
	case SIOCSIFMAP:
		err = copy_from_user(&ifr, uifr32, sizeof(ifr.ifr_name));
		err |= __get_user(ifr.ifr_map.mem_start, &uifmap32->mem_start);
		err |= __get_user(ifr.ifr_map.mem_end, &uifmap32->mem_end);
		err |= __get_user(ifr.ifr_map.base_addr, &uifmap32->base_addr);
		err |= __get_user(ifr.ifr_map.irq, &uifmap32->irq);
		err |= __get_user(ifr.ifr_map.dma, &uifmap32->dma);
		err |= __get_user(ifr.ifr_map.port, &uifmap32->port);
		if (err)
			return -EFAULT;
		break;
	default:
		if (copy_from_user(&ifr, uifr32, sizeof(*uifr32)))
			return -EFAULT;
		break;
	}
	old_fs = get_fs();
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, cmd, (unsigned long)&ifr);
	set_fs (old_fs);
	if (!err) {
		switch (cmd) {
		/* TUNSETIFF is defined as _IOW, it should be _IORW
		 * as the data is copied back to user space, but that
		 * cannot be fixed without breaking all existing apps.
		 */
		case TUNSETIFF:
		case SIOCGIFFLAGS:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFMEM:
		case SIOCGIFHWADDR:
		case SIOCGIFINDEX:
		case SIOCGIFADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFDSTADDR:
		case SIOCGIFNETMASK:
		case SIOCGIFTXQLEN:
			if (copy_to_user(uifr32, &ifr, sizeof(*uifr32)))
				return -EFAULT;
			break;
		case SIOCGIFMAP:
			err = copy_to_user(uifr32, &ifr, sizeof(ifr.ifr_name));
			err |= __put_user(ifr.ifr_map.mem_start, &uifmap32->mem_start);
			err |= __put_user(ifr.ifr_map.mem_end, &uifmap32->mem_end);
			err |= __put_user(ifr.ifr_map.base_addr, &uifmap32->base_addr);
			err |= __put_user(ifr.ifr_map.irq, &uifmap32->irq);
			err |= __put_user(ifr.ifr_map.dma, &uifmap32->dma);
			err |= __put_user(ifr.ifr_map.port, &uifmap32->port);
			if (err)
				err = -EFAULT;
			break;
		}
	}
	return err;
}

struct rtentry32 {
        u32   		rt_pad1;
        struct sockaddr rt_dst;         /* target address               */
        struct sockaddr rt_gateway;     /* gateway addr (RTF_GATEWAY)   */
        struct sockaddr rt_genmask;     /* target network mask (IP)     */
        unsigned short  rt_flags;
        short           rt_pad2;
        u32   		rt_pad3;
        unsigned char   rt_tos;
        unsigned char   rt_class;
        short           rt_pad4;
        short           rt_metric;      /* +1 for binary compatibility! */
        /* char * */ u32 rt_dev;        /* forcing the device at add    */
        u32   		rt_mtu;         /* per route MTU/Window         */
        u32   		rt_window;      /* Window clamping              */
        unsigned short  rt_irtt;        /* Initial RTT                  */

};

struct in6_rtmsg32 {
	struct in6_addr		rtmsg_dst;
	struct in6_addr		rtmsg_src;
	struct in6_addr		rtmsg_gateway;
	u32			rtmsg_type;
	u16			rtmsg_dst_len;
	u16			rtmsg_src_len;
	u32			rtmsg_metric;
	u32			rtmsg_info;
	u32			rtmsg_flags;
	s32			rtmsg_ifindex;
};

static int routing_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	int ret;
	void *r = NULL;
	struct in6_rtmsg r6;
	struct rtentry r4;
	char devname[16];
	u32 rtdev;
	mm_segment_t old_fs = get_fs();
	
	struct socket *mysock = sockfd_lookup(fd, &ret);

	if (mysock && mysock->sk && mysock->sk->sk_family == AF_INET6) { /* ipv6 */
		struct in6_rtmsg32 __user *ur6 = compat_ptr(arg);
		ret = copy_from_user (&r6.rtmsg_dst, &(ur6->rtmsg_dst),
			3 * sizeof(struct in6_addr));
		ret |= __get_user (r6.rtmsg_type, &(ur6->rtmsg_type));
		ret |= __get_user (r6.rtmsg_dst_len, &(ur6->rtmsg_dst_len));
		ret |= __get_user (r6.rtmsg_src_len, &(ur6->rtmsg_src_len));
		ret |= __get_user (r6.rtmsg_metric, &(ur6->rtmsg_metric));
		ret |= __get_user (r6.rtmsg_info, &(ur6->rtmsg_info));
		ret |= __get_user (r6.rtmsg_flags, &(ur6->rtmsg_flags));
		ret |= __get_user (r6.rtmsg_ifindex, &(ur6->rtmsg_ifindex));
		
		r = (void *) &r6;
	} else { /* ipv4 */
		struct rtentry32 __user *ur4 = compat_ptr(arg);
		ret = copy_from_user (&r4.rt_dst, &(ur4->rt_dst),
					3 * sizeof(struct sockaddr));
		ret |= __get_user (r4.rt_flags, &(ur4->rt_flags));
		ret |= __get_user (r4.rt_metric, &(ur4->rt_metric));
		ret |= __get_user (r4.rt_mtu, &(ur4->rt_mtu));
		ret |= __get_user (r4.rt_window, &(ur4->rt_window));
		ret |= __get_user (r4.rt_irtt, &(ur4->rt_irtt));
		ret |= __get_user (rtdev, &(ur4->rt_dev));
		if (rtdev) {
			ret |= copy_from_user (devname, compat_ptr(rtdev), 15);
			r4.rt_dev = devname; devname[15] = 0;
		} else
			r4.rt_dev = NULL;

		r = (void *) &r4;
	}

	if (ret) {
		ret = -EFAULT;
		goto out;
	}

	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, cmd, (unsigned long) r);
	set_fs (old_fs);

out:
	if (mysock)
		sockfd_put(mysock);

	return ret;
}
#endif

struct hd_geometry32 {
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	u32 start;
};
                        
static int hdio_getgeo(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct hd_geometry geo;
	struct hd_geometry32 __user *ugeo;
	int err;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, HDIO_GETGEO, (unsigned long)&geo);
	set_fs (old_fs);
	ugeo = compat_ptr(arg);
	if (!err) {
		err = copy_to_user (ugeo, &geo, 4);
		err |= __put_user (geo.start, &ugeo->start);
	}
	return err ? -EFAULT : 0;
}

struct fb_fix_screeninfo32 {
	char			id[16];
        compat_caddr_t	smem_start;
	u32			smem_len;
	u32			type;
	u32			type_aux;
	u32			visual;
	u16			xpanstep;
	u16			ypanstep;
	u16			ywrapstep;
	u32			line_length;
        compat_caddr_t	mmio_start;
	u32			mmio_len;
	u32			accel;
	u16			reserved[3];
};

struct fb_cmap32 {
	u32			start;
	u32			len;
	compat_caddr_t	red;
	compat_caddr_t	green;
	compat_caddr_t	blue;
	compat_caddr_t	transp;
};

static int fb_getput_cmap(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fb_cmap_user __user *cmap;
	struct fb_cmap32 __user *cmap32;
	__u32 data;
	int err;

	cmap = compat_alloc_user_space(sizeof(*cmap));
	cmap32 = compat_ptr(arg);

	if (copy_in_user(&cmap->start, &cmap32->start, 2 * sizeof(__u32)))
		return -EFAULT;

	if (get_user(data, &cmap32->red) ||
	    put_user(compat_ptr(data), &cmap->red) ||
	    get_user(data, &cmap32->green) ||
	    put_user(compat_ptr(data), &cmap->green) ||
	    get_user(data, &cmap32->blue) ||
	    put_user(compat_ptr(data), &cmap->blue) ||
	    get_user(data, &cmap32->transp) ||
	    put_user(compat_ptr(data), &cmap->transp))
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long) cmap);

	if (!err) {
		if (copy_in_user(&cmap32->start,
				 &cmap->start,
				 2 * sizeof(__u32)))
			err = -EFAULT;
	}
	return err;
}

static int do_fscreeninfo_to_user(struct fb_fix_screeninfo *fix,
				  struct fb_fix_screeninfo32 __user *fix32)
{
	__u32 data;
	int err;

	err = copy_to_user(&fix32->id, &fix->id, sizeof(fix32->id));

	data = (__u32) (unsigned long) fix->smem_start;
	err |= put_user(data, &fix32->smem_start);

	err |= put_user(fix->smem_len, &fix32->smem_len);
	err |= put_user(fix->type, &fix32->type);
	err |= put_user(fix->type_aux, &fix32->type_aux);
	err |= put_user(fix->visual, &fix32->visual);
	err |= put_user(fix->xpanstep, &fix32->xpanstep);
	err |= put_user(fix->ypanstep, &fix32->ypanstep);
	err |= put_user(fix->ywrapstep, &fix32->ywrapstep);
	err |= put_user(fix->line_length, &fix32->line_length);

	data = (__u32) (unsigned long) fix->mmio_start;
	err |= put_user(data, &fix32->mmio_start);

	err |= put_user(fix->mmio_len, &fix32->mmio_len);
	err |= put_user(fix->accel, &fix32->accel);
	err |= copy_to_user(fix32->reserved, fix->reserved,
			    sizeof(fix->reserved));

	return err;
}

static int fb_get_fscreeninfo(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs;
	struct fb_fix_screeninfo fix;
	struct fb_fix_screeninfo32 __user *fix32;
	int err;

	fix32 = compat_ptr(arg);

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long) &fix);
	set_fs(old_fs);

	if (!err)
		err = do_fscreeninfo_to_user(&fix, fix32);

	return err;
}

static int fb_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	int err;

	switch (cmd) {
	case FBIOGET_FSCREENINFO:
		err = fb_get_fscreeninfo(fd,cmd, arg);
		break;

  	case FBIOGETCMAP:
	case FBIOPUTCMAP:
		err = fb_getput_cmap(fd, cmd, arg);
		break;

	default:
		do {
			static int count;
			if (++count <= 20)
				printk("%s: Unknown fb ioctl cmd fd(%d) "
				       "cmd(%08x) arg(%08lx)\n",
				       __FUNCTION__, fd, cmd, arg);
		} while(0);
		err = -ENOSYS;
		break;
	};

	return err;
}

static int hdio_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	unsigned long kval;
	unsigned int __user *uvp;
	int error;

	set_fs(KERNEL_DS);
	error = sys_ioctl(fd, cmd, (long)&kval);
	set_fs(old_fs);

	if(error == 0) {
		uvp = compat_ptr(arg);
		if(put_user(kval, uvp))
			error = -EFAULT;
	}
	return error;
}


typedef struct sg_io_hdr32 {
	compat_int_t interface_id;	/* [i] 'S' for SCSI generic (required) */
	compat_int_t dxfer_direction;	/* [i] data transfer direction  */
	unsigned char cmd_len;		/* [i] SCSI command length ( <= 16 bytes) */
	unsigned char mx_sb_len;		/* [i] max length to write to sbp */
	unsigned short iovec_count;	/* [i] 0 implies no scatter gather */
	compat_uint_t dxfer_len;		/* [i] byte count of data transfer */
	compat_uint_t dxferp;		/* [i], [*io] points to data transfer memory
					      or scatter gather list */
	compat_uptr_t cmdp;		/* [i], [*i] points to command to perform */
	compat_uptr_t sbp;		/* [i], [*o] points to sense_buffer memory */
	compat_uint_t timeout;		/* [i] MAX_UINT->no timeout (unit: millisec) */
	compat_uint_t flags;		/* [i] 0 -> default, see SG_FLAG... */
	compat_int_t pack_id;		/* [i->o] unused internally (normally) */
	compat_uptr_t usr_ptr;		/* [i->o] unused internally */
	unsigned char status;		/* [o] scsi status */
	unsigned char masked_status;	/* [o] shifted, masked scsi status */
	unsigned char msg_status;		/* [o] messaging level data (optional) */
	unsigned char sb_len_wr;		/* [o] byte count actually written to sbp */
	unsigned short host_status;	/* [o] errors from host adapter */
	unsigned short driver_status;	/* [o] errors from software driver */
	compat_int_t resid;		/* [o] dxfer_len - actual_transferred */
	compat_uint_t duration;		/* [o] time taken by cmd (unit: millisec) */
	compat_uint_t info;		/* [o] auxiliary information */
} sg_io_hdr32_t;  /* 64 bytes long (on sparc32) */

typedef struct sg_iovec32 {
	compat_uint_t iov_base;
	compat_uint_t iov_len;
} sg_iovec32_t;

static int sg_build_iovec(sg_io_hdr_t __user *sgio, void __user *dxferp, u16 iovec_count)
{
	sg_iovec_t __user *iov = (sg_iovec_t __user *) (sgio + 1);
	sg_iovec32_t __user *iov32 = dxferp;
	int i;

	for (i = 0; i < iovec_count; i++) {
		u32 base, len;

		if (get_user(base, &iov32[i].iov_base) ||
		    get_user(len, &iov32[i].iov_len) ||
		    put_user(compat_ptr(base), &iov[i].iov_base) ||
		    put_user(len, &iov[i].iov_len))
			return -EFAULT;
	}

	if (put_user(iov, &sgio->dxferp))
		return -EFAULT;
	return 0;
}

static int sg_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	sg_io_hdr_t __user *sgio;
	sg_io_hdr32_t __user *sgio32;
	u16 iovec_count;
	u32 data;
	void __user *dxferp;
	int err;

	sgio32 = compat_ptr(arg);
	if (get_user(iovec_count, &sgio32->iovec_count))
		return -EFAULT;

	{
		void __user *top = compat_alloc_user_space(0);
		void __user *new = compat_alloc_user_space(sizeof(sg_io_hdr_t) +
				       (iovec_count * sizeof(sg_iovec_t)));
		if (new > top)
			return -EINVAL;

		sgio = new;
	}

	/* Ok, now construct.  */
	if (copy_in_user(&sgio->interface_id, &sgio32->interface_id,
			 (2 * sizeof(int)) +
			 (2 * sizeof(unsigned char)) +
			 (1 * sizeof(unsigned short)) +
			 (1 * sizeof(unsigned int))))
		return -EFAULT;

	if (get_user(data, &sgio32->dxferp))
		return -EFAULT;
	dxferp = compat_ptr(data);
	if (iovec_count) {
		if (sg_build_iovec(sgio, dxferp, iovec_count))
			return -EFAULT;
	} else {
		if (put_user(dxferp, &sgio->dxferp))
			return -EFAULT;
	}

	{
		unsigned char __user *cmdp;
		unsigned char __user *sbp;

		if (get_user(data, &sgio32->cmdp))
			return -EFAULT;
		cmdp = compat_ptr(data);

		if (get_user(data, &sgio32->sbp))
			return -EFAULT;
		sbp = compat_ptr(data);

		if (put_user(cmdp, &sgio->cmdp) ||
		    put_user(sbp, &sgio->sbp))
			return -EFAULT;
	}

	if (copy_in_user(&sgio->timeout, &sgio32->timeout,
			 3 * sizeof(int)))
		return -EFAULT;

	if (get_user(data, &sgio32->usr_ptr))
		return -EFAULT;
	if (put_user(compat_ptr(data), &sgio->usr_ptr))
		return -EFAULT;

	if (copy_in_user(&sgio->status, &sgio32->status,
			 (4 * sizeof(unsigned char)) +
			 (2 * sizeof(unsigned (short))) +
			 (3 * sizeof(int))))
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long) sgio);

	if (err >= 0) {
		void __user *datap;

		if (copy_in_user(&sgio32->pack_id, &sgio->pack_id,
				 sizeof(int)) ||
		    get_user(datap, &sgio->usr_ptr) ||
		    put_user((u32)(unsigned long)datap,
			     &sgio32->usr_ptr) ||
		    copy_in_user(&sgio32->status, &sgio->status,
				 (4 * sizeof(unsigned char)) +
				 (2 * sizeof(unsigned short)) +
				 (3 * sizeof(int))))
			err = -EFAULT;
	}

	return err;
}

struct sock_fprog32 {
	unsigned short	len;
	compat_caddr_t	filter;
};

#define PPPIOCSPASS32	_IOW('t', 71, struct sock_fprog32)
#define PPPIOCSACTIVE32	_IOW('t', 70, struct sock_fprog32)

static int ppp_sock_fprog_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct sock_fprog32 __user *u_fprog32 = compat_ptr(arg);
	struct sock_fprog __user *u_fprog64 = compat_alloc_user_space(sizeof(struct sock_fprog));
	void __user *fptr64;
	u32 fptr32;
	u16 flen;

	if (get_user(flen, &u_fprog32->len) ||
	    get_user(fptr32, &u_fprog32->filter))
		return -EFAULT;

	fptr64 = compat_ptr(fptr32);

	if (put_user(flen, &u_fprog64->len) ||
	    put_user(fptr64, &u_fprog64->filter))
		return -EFAULT;

	if (cmd == PPPIOCSPASS32)
		cmd = PPPIOCSPASS;
	else
		cmd = PPPIOCSACTIVE;

	return sys_ioctl(fd, cmd, (unsigned long) u_fprog64);
}

struct ppp_option_data32 {
	compat_caddr_t	ptr;
	u32			length;
	compat_int_t		transmit;
};
#define PPPIOCSCOMPRESS32	_IOW('t', 77, struct ppp_option_data32)

struct ppp_idle32 {
	compat_time_t xmit_idle;
	compat_time_t recv_idle;
};
#define PPPIOCGIDLE32		_IOR('t', 63, struct ppp_idle32)

static int ppp_gidle(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ppp_idle __user *idle;
	struct ppp_idle32 __user *idle32;
	__kernel_time_t xmit, recv;
	int err;

	idle = compat_alloc_user_space(sizeof(*idle));
	idle32 = compat_ptr(arg);

	err = sys_ioctl(fd, PPPIOCGIDLE, (unsigned long) idle);

	if (!err) {
		if (get_user(xmit, &idle->xmit_idle) ||
		    get_user(recv, &idle->recv_idle) ||
		    put_user(xmit, &idle32->xmit_idle) ||
		    put_user(recv, &idle32->recv_idle))
			err = -EFAULT;
	}
	return err;
}

static int ppp_scompress(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ppp_option_data __user *odata;
	struct ppp_option_data32 __user *odata32;
	__u32 data;
	void __user *datap;

	odata = compat_alloc_user_space(sizeof(*odata));
	odata32 = compat_ptr(arg);

	if (get_user(data, &odata32->ptr))
		return -EFAULT;

	datap = compat_ptr(data);
	if (put_user(datap, &odata->ptr))
		return -EFAULT;

	if (copy_in_user(&odata->length, &odata32->length,
			 sizeof(__u32) + sizeof(int)))
		return -EFAULT;

	return sys_ioctl(fd, PPPIOCSCOMPRESS, (unsigned long) odata);
}

static int ppp_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	int err;

	switch (cmd) {
	case PPPIOCGIDLE32:
		err = ppp_gidle(fd, cmd, arg);
		break;

	case PPPIOCSCOMPRESS32:
		err = ppp_scompress(fd, cmd, arg);
		break;

	default:
		do {
			static int count;
			if (++count <= 20)
				printk("ppp_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		err = -EINVAL;
		break;
	};

	return err;
}


struct mtget32 {
	compat_long_t	mt_type;
	compat_long_t	mt_resid;
	compat_long_t	mt_dsreg;
	compat_long_t	mt_gstat;
	compat_long_t	mt_erreg;
	compat_daddr_t	mt_fileno;
	compat_daddr_t	mt_blkno;
};
#define MTIOCGET32	_IOR('m', 2, struct mtget32)

struct mtpos32 {
	compat_long_t	mt_blkno;
};
#define MTIOCPOS32	_IOR('m', 3, struct mtpos32)

static int mt_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct mtget get;
	struct mtget32 __user *umget32;
	struct mtpos pos;
	struct mtpos32 __user *upos32;
	unsigned long kcmd;
	void *karg;
	int err = 0;

	switch(cmd) {
	case MTIOCPOS32:
		kcmd = MTIOCPOS;
		karg = &pos;
		break;
	case MTIOCGET32:
		kcmd = MTIOCGET;
		karg = &get;
		break;
	default:
		do {
			static int count;
			if (++count <= 20)
				printk("mt_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		return -EINVAL;
	}
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, kcmd, (unsigned long)karg);
	set_fs (old_fs);
	if (err)
		return err;
	switch (cmd) {
	case MTIOCPOS32:
		upos32 = compat_ptr(arg);
		err = __put_user(pos.mt_blkno, &upos32->mt_blkno);
		break;
	case MTIOCGET32:
		umget32 = compat_ptr(arg);
		err = __put_user(get.mt_type, &umget32->mt_type);
		err |= __put_user(get.mt_resid, &umget32->mt_resid);
		err |= __put_user(get.mt_dsreg, &umget32->mt_dsreg);
		err |= __put_user(get.mt_gstat, &umget32->mt_gstat);
		err |= __put_user(get.mt_erreg, &umget32->mt_erreg);
		err |= __put_user(get.mt_fileno, &umget32->mt_fileno);
		err |= __put_user(get.mt_blkno, &umget32->mt_blkno);
		break;
	}
	return err ? -EFAULT: 0;
}

struct cdrom_read_audio32 {
	union cdrom_addr	addr;
	u8			addr_format;
	compat_int_t		nframes;
	compat_caddr_t		buf;
};

struct cdrom_generic_command32 {
	unsigned char	cmd[CDROM_PACKET_SIZE];
	compat_caddr_t	buffer;
	compat_uint_t	buflen;
	compat_int_t	stat;
	compat_caddr_t	sense;
	unsigned char	data_direction;
	compat_int_t	quiet;
	compat_int_t	timeout;
	compat_caddr_t	reserved[1];
};
  
static int cdrom_do_read_audio(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct cdrom_read_audio __user *cdread_audio;
	struct cdrom_read_audio32 __user *cdread_audio32;
	__u32 data;
	void __user *datap;

	cdread_audio = compat_alloc_user_space(sizeof(*cdread_audio));
	cdread_audio32 = compat_ptr(arg);

	if (copy_in_user(&cdread_audio->addr,
			 &cdread_audio32->addr,
			 (sizeof(*cdread_audio32) -
			  sizeof(compat_caddr_t))))
	 	return -EFAULT;

	if (get_user(data, &cdread_audio32->buf))
		return -EFAULT;
	datap = compat_ptr(data);
	if (put_user(datap, &cdread_audio->buf))
		return -EFAULT;

	return sys_ioctl(fd, cmd, (unsigned long) cdread_audio);
}

static int cdrom_do_generic_command(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct cdrom_generic_command __user *cgc;
	struct cdrom_generic_command32 __user *cgc32;
	u32 data;
	unsigned char dir;
	int itmp;

	cgc = compat_alloc_user_space(sizeof(*cgc));
	cgc32 = compat_ptr(arg);

	if (copy_in_user(&cgc->cmd, &cgc32->cmd, sizeof(cgc->cmd)) ||
	    get_user(data, &cgc32->buffer) ||
	    put_user(compat_ptr(data), &cgc->buffer) ||
	    copy_in_user(&cgc->buflen, &cgc32->buflen,
			 (sizeof(unsigned int) + sizeof(int))) ||
	    get_user(data, &cgc32->sense) ||
	    put_user(compat_ptr(data), &cgc->sense) ||
	    get_user(dir, &cgc32->data_direction) ||
	    put_user(dir, &cgc->data_direction) ||
	    get_user(itmp, &cgc32->quiet) ||
	    put_user(itmp, &cgc->quiet) ||
	    get_user(itmp, &cgc32->timeout) ||
	    put_user(itmp, &cgc->timeout) ||
	    get_user(data, &cgc32->reserved[0]) ||
	    put_user(compat_ptr(data), &cgc->reserved[0]))
		return -EFAULT;

	return sys_ioctl(fd, cmd, (unsigned long) cgc);
}

static int cdrom_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	int err;

	switch(cmd) {
	case CDROMREADAUDIO:
		err = cdrom_do_read_audio(fd, cmd, arg);
		break;

	case CDROM_SEND_PACKET:
		err = cdrom_do_generic_command(fd, cmd, arg);
		break;

	default:
		do {
			static int count;
			if (++count <= 20)
				printk("cdrom_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		err = -EINVAL;
		break;
	};

	return err;
}

struct loop_info32 {
	compat_int_t	lo_number;      /* ioctl r/o */
	compat_dev_t	lo_device;      /* ioctl r/o */
	compat_ulong_t	lo_inode;       /* ioctl r/o */
	compat_dev_t	lo_rdevice;     /* ioctl r/o */
	compat_int_t	lo_offset;
	compat_int_t	lo_encrypt_type;
	compat_int_t	lo_encrypt_key_size;    /* ioctl w/o */
	compat_int_t	lo_flags;       /* ioctl r/o */
	char		lo_name[LO_NAME_SIZE];
	unsigned char	lo_encrypt_key[LO_KEY_SIZE]; /* ioctl w/o */
	compat_ulong_t	lo_init[2];
	char		reserved[4];
};

static int loop_status(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct loop_info l;
	struct loop_info32 __user *ul;
	int err = -EINVAL;

	ul = compat_ptr(arg);
	switch(cmd) {
	case LOOP_SET_STATUS:
		err = get_user(l.lo_number, &ul->lo_number);
		err |= __get_user(l.lo_device, &ul->lo_device);
		err |= __get_user(l.lo_inode, &ul->lo_inode);
		err |= __get_user(l.lo_rdevice, &ul->lo_rdevice);
		err |= __copy_from_user(&l.lo_offset, &ul->lo_offset,
		        8 + (unsigned long)l.lo_init - (unsigned long)&l.lo_offset);
		if (err) {
			err = -EFAULT;
		} else {
			set_fs (KERNEL_DS);
			err = sys_ioctl (fd, cmd, (unsigned long)&l);
			set_fs (old_fs);
		}
		break;
	case LOOP_GET_STATUS:
		set_fs (KERNEL_DS);
		err = sys_ioctl (fd, cmd, (unsigned long)&l);
		set_fs (old_fs);
		if (!err) {
			err = put_user(l.lo_number, &ul->lo_number);
			err |= __put_user(l.lo_device, &ul->lo_device);
			err |= __put_user(l.lo_inode, &ul->lo_inode);
			err |= __put_user(l.lo_rdevice, &ul->lo_rdevice);
			err |= __copy_to_user(&ul->lo_offset, &l.lo_offset,
				(unsigned long)l.lo_init - (unsigned long)&l.lo_offset);
			if (err)
				err = -EFAULT;
		}
		break;
	default: {
		static int count;
		if (++count <= 20)
			printk("%s: Unknown loop ioctl cmd, fd(%d) "
			       "cmd(%08x) arg(%08lx)\n",
			       __FUNCTION__, fd, cmd, arg);
	}
	}
	return err;
}

extern int tty_ioctl(struct inode * inode, struct file * file, unsigned int cmd, unsigned long arg);

#ifdef CONFIG_VT

static int vt_check(struct file *file)
{
	struct tty_struct *tty;
	struct inode *inode = file->f_dentry->d_inode;
	
	if (file->f_op->ioctl != tty_ioctl)
		return -EINVAL;
	                
	tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode, "tty_ioctl"))
		return -EINVAL;
	                                                
	if (tty->driver->ioctl != vt_ioctl)
		return -EINVAL;
	
	/*
	 * To have permissions to do most of the vt ioctls, we either have
	 * to be the owner of the tty, or super-user.
	 */
	if (current->signal->tty == tty || capable(CAP_SYS_ADMIN))
		return 1;
	return 0;                                                    
}

struct consolefontdesc32 {
	unsigned short charcount;       /* characters in font (256 or 512) */
	unsigned short charheight;      /* scan lines per character (1-32) */
	compat_caddr_t chardata;	/* font data in expanded form */
};

static int do_fontx_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file)
{
	struct consolefontdesc32 __user *user_cfd = compat_ptr(arg);
	struct console_font_op op;
	compat_caddr_t data;
	int i, perm;

	perm = vt_check(file);
	if (perm < 0) return perm;
	
	switch (cmd) {
	case PIO_FONTX:
		if (!perm)
			return -EPERM;
		op.op = KD_FONT_OP_SET;
		op.flags = 0;
		op.width = 8;
		if (get_user(op.height, &user_cfd->charheight) ||
		    get_user(op.charcount, &user_cfd->charcount) ||
		    get_user(data, &user_cfd->chardata))
			return -EFAULT;
		op.data = compat_ptr(data);
		return con_font_op(vc_cons[fg_console].d, &op);
	case GIO_FONTX:
		op.op = KD_FONT_OP_GET;
		op.flags = 0;
		op.width = 8;
		if (get_user(op.height, &user_cfd->charheight) ||
		    get_user(op.charcount, &user_cfd->charcount) ||
		    get_user(data, &user_cfd->chardata))
			return -EFAULT;
		if (!data)
			return 0;
		op.data = compat_ptr(data);
		i = con_font_op(vc_cons[fg_console].d, &op);
		if (i)
			return i;
		if (put_user(op.height, &user_cfd->charheight) ||
		    put_user(op.charcount, &user_cfd->charcount) ||
		    put_user((compat_caddr_t)(unsigned long)op.data,
				&user_cfd->chardata))
			return -EFAULT;
		return 0;
	}
	return -EINVAL;
}

struct console_font_op32 {
	compat_uint_t op;        /* operation code KD_FONT_OP_* */
	compat_uint_t flags;     /* KD_FONT_FLAG_* */
	compat_uint_t width, height;     /* font size */
	compat_uint_t charcount;
	compat_caddr_t data;    /* font data with height fixed to 32 */
};
                                        
static int do_kdfontop_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file)
{
	struct console_font_op op;
	struct console_font_op32 __user *fontop = compat_ptr(arg);
	int perm = vt_check(file), i;
	struct vc_data *vc;
	
	if (perm < 0) return perm;
	
	if (copy_from_user(&op, fontop, sizeof(struct console_font_op32)))
		return -EFAULT;
	if (!perm && op.op != KD_FONT_OP_GET)
		return -EPERM;
	op.data = compat_ptr(((struct console_font_op32 *)&op)->data);
	op.flags |= KD_FONT_FLAG_OLD;
	vc = ((struct tty_struct *)file->private_data)->driver_data;
	i = con_font_op(vc, &op);
	if (i)
		return i;
	((struct console_font_op32 *)&op)->data = (unsigned long)op.data;
	if (copy_to_user(fontop, &op, sizeof(struct console_font_op32)))
		return -EFAULT;
	return 0;
}

struct unimapdesc32 {
	unsigned short entry_ct;
	compat_caddr_t entries;
};

static int do_unimap_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file)
{
	struct unimapdesc32 tmp;
	struct unimapdesc32 __user *user_ud = compat_ptr(arg);
	int perm = vt_check(file);
	
	if (perm < 0) return perm;
	if (copy_from_user(&tmp, user_ud, sizeof tmp))
		return -EFAULT;
	switch (cmd) {
	case PIO_UNIMAP:
		if (!perm) return -EPERM;
		return con_set_unimap(vc_cons[fg_console].d, tmp.entry_ct, compat_ptr(tmp.entries));
	case GIO_UNIMAP:
		return con_get_unimap(vc_cons[fg_console].d, tmp.entry_ct, &(user_ud->entry_ct), compat_ptr(tmp.entries));
	}
	return 0;
}

#endif /* CONFIG_VT */

static int do_smb_getmountuid(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	__kernel_uid_t kuid;
	int err;

	cmd = SMB_IOC_GETMOUNTUID;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&kuid);
	set_fs(old_fs);

	if (err >= 0)
		err = put_user(kuid, (compat_uid_t __user *)compat_ptr(arg));

	return err;
}

struct atmif_sioc32 {
        compat_int_t	number;
        compat_int_t	length;
        compat_caddr_t	arg;
};

struct atm_iobuf32 {
	compat_int_t	length;
	compat_caddr_t	buffer;
};

#define ATM_GETLINKRATE32 _IOW('a', ATMIOC_ITF+1, struct atmif_sioc32)
#define ATM_GETNAMES32    _IOW('a', ATMIOC_ITF+3, struct atm_iobuf32)
#define ATM_GETTYPE32     _IOW('a', ATMIOC_ITF+4, struct atmif_sioc32)
#define ATM_GETESI32	  _IOW('a', ATMIOC_ITF+5, struct atmif_sioc32)
#define ATM_GETADDR32	  _IOW('a', ATMIOC_ITF+6, struct atmif_sioc32)
#define ATM_RSTADDR32	  _IOW('a', ATMIOC_ITF+7, struct atmif_sioc32)
#define ATM_ADDADDR32	  _IOW('a', ATMIOC_ITF+8, struct atmif_sioc32)
#define ATM_DELADDR32	  _IOW('a', ATMIOC_ITF+9, struct atmif_sioc32)
#define ATM_GETCIRANGE32  _IOW('a', ATMIOC_ITF+10, struct atmif_sioc32)
#define ATM_SETCIRANGE32  _IOW('a', ATMIOC_ITF+11, struct atmif_sioc32)
#define ATM_SETESI32      _IOW('a', ATMIOC_ITF+12, struct atmif_sioc32)
#define ATM_SETESIF32     _IOW('a', ATMIOC_ITF+13, struct atmif_sioc32)
#define ATM_GETSTAT32     _IOW('a', ATMIOC_SARCOM+0, struct atmif_sioc32)
#define ATM_GETSTATZ32    _IOW('a', ATMIOC_SARCOM+1, struct atmif_sioc32)
#define ATM_GETLOOP32	  _IOW('a', ATMIOC_SARCOM+2, struct atmif_sioc32)
#define ATM_SETLOOP32	  _IOW('a', ATMIOC_SARCOM+3, struct atmif_sioc32)
#define ATM_QUERYLOOP32	  _IOW('a', ATMIOC_SARCOM+4, struct atmif_sioc32)

static struct {
        unsigned int cmd32;
        unsigned int cmd;
} atm_ioctl_map[] = {
        { ATM_GETLINKRATE32, ATM_GETLINKRATE },
	{ ATM_GETNAMES32,    ATM_GETNAMES },
        { ATM_GETTYPE32,     ATM_GETTYPE },
        { ATM_GETESI32,      ATM_GETESI },
        { ATM_GETADDR32,     ATM_GETADDR },
        { ATM_RSTADDR32,     ATM_RSTADDR },
        { ATM_ADDADDR32,     ATM_ADDADDR },
        { ATM_DELADDR32,     ATM_DELADDR },
        { ATM_GETCIRANGE32,  ATM_GETCIRANGE },
	{ ATM_SETCIRANGE32,  ATM_SETCIRANGE },
	{ ATM_SETESI32,      ATM_SETESI },
	{ ATM_SETESIF32,     ATM_SETESIF },
	{ ATM_GETSTAT32,     ATM_GETSTAT },
	{ ATM_GETSTATZ32,    ATM_GETSTATZ },
	{ ATM_GETLOOP32,     ATM_GETLOOP },
	{ ATM_SETLOOP32,     ATM_SETLOOP },
	{ ATM_QUERYLOOP32,   ATM_QUERYLOOP }
};

#define NR_ATM_IOCTL (sizeof(atm_ioctl_map)/sizeof(atm_ioctl_map[0]))


static int do_atm_iobuf(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct atm_iobuf   __user *iobuf;
	struct atm_iobuf32 __user *iobuf32;
	u32 data;
	void __user *datap;
	int len, err;

	iobuf = compat_alloc_user_space(sizeof(*iobuf));
	iobuf32 = compat_ptr(arg);

	if (get_user(len, &iobuf32->length) ||
	    get_user(data, &iobuf32->buffer))
		return -EFAULT;
	datap = compat_ptr(data);
	if (put_user(len, &iobuf->length) ||
	    put_user(datap, &iobuf->buffer))
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long)iobuf);

	if (!err) {
		if (copy_in_user(&iobuf32->length, &iobuf->length,
				 sizeof(int)))
			err = -EFAULT;
	}

	return err;
}

static int do_atmif_sioc(unsigned int fd, unsigned int cmd, unsigned long arg)
{
        struct atmif_sioc   __user *sioc;
	struct atmif_sioc32 __user *sioc32;
	u32 data;
	void __user *datap;
	int err;
        
	sioc = compat_alloc_user_space(sizeof(*sioc));
	sioc32 = compat_ptr(arg);

	if (copy_in_user(&sioc->number, &sioc32->number, 2 * sizeof(int)) ||
	    get_user(data, &sioc32->arg))
		return -EFAULT;
	datap = compat_ptr(data);
	if (put_user(datap, &sioc->arg))
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long) sioc);

	if (!err) {
		if (copy_in_user(&sioc32->length, &sioc->length,
				 sizeof(int)))
			err = -EFAULT;
	}
	return err;
}

static int do_atm_ioctl(unsigned int fd, unsigned int cmd32, unsigned long arg)
{
        int i;
        unsigned int cmd = 0;
        
	switch (cmd32) {
	case SONET_GETSTAT:
	case SONET_GETSTATZ:
	case SONET_GETDIAG:
	case SONET_SETDIAG:
	case SONET_CLRDIAG:
	case SONET_SETFRAMING:
	case SONET_GETFRAMING:
	case SONET_GETFRSENSE:
		return do_atmif_sioc(fd, cmd32, arg);
	}

	for (i = 0; i < NR_ATM_IOCTL; i++) {
		if (cmd32 == atm_ioctl_map[i].cmd32) {
			cmd = atm_ioctl_map[i].cmd;
			break;
		}
	}
	if (i == NR_ATM_IOCTL)
	        return -EINVAL;
        
        switch (cmd) {
	case ATM_GETNAMES:
		return do_atm_iobuf(fd, cmd, arg);
	    
	case ATM_GETLINKRATE:
        case ATM_GETTYPE:
        case ATM_GETESI:
        case ATM_GETADDR:
        case ATM_RSTADDR:
        case ATM_ADDADDR:
        case ATM_DELADDR:
        case ATM_GETCIRANGE:
	case ATM_SETCIRANGE:
	case ATM_SETESI:
	case ATM_SETESIF:
	case ATM_GETSTAT:
	case ATM_GETSTATZ:
	case ATM_GETLOOP:
	case ATM_SETLOOP:
	case ATM_QUERYLOOP:
                return do_atmif_sioc(fd, cmd, arg);
        }

        return -EINVAL;
}

static __attribute_used__ int 
ret_einval(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static int broken_blkgetsize(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	/* The mkswap binary hard codes it to Intel value :-((( */
	return w_long(fd, BLKGETSIZE, arg);
}

struct blkpg_ioctl_arg32 {
	compat_int_t op;
	compat_int_t flags;
	compat_int_t datalen;
	compat_caddr_t data;
};

static int blkpg_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct blkpg_ioctl_arg32 __user *ua32 = compat_ptr(arg);
	struct blkpg_ioctl_arg __user *a = compat_alloc_user_space(sizeof(*a));
	compat_caddr_t udata;
	compat_int_t n;
	int err;
	
	err = get_user(n, &ua32->op);
	err |= put_user(n, &a->op);
	err |= get_user(n, &ua32->flags);
	err |= put_user(n, &a->flags);
	err |= get_user(n, &ua32->datalen);
	err |= put_user(n, &a->datalen);
	err |= get_user(udata, &ua32->data);
	err |= put_user(compat_ptr(udata), &a->data);
	if (err)
		return err;

	return sys_ioctl(fd, cmd, (unsigned long)a);
}

static int ioc_settimeout(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return rw_long(fd, AUTOFS_IOC_SETTIMEOUT, arg);
}

/* Fix sizeof(sizeof()) breakage */
#define BLKBSZGET_32   _IOR(0x12,112,int)
#define BLKBSZSET_32   _IOW(0x12,113,int)
#define BLKGETSIZE64_32        _IOR(0x12,114,int)

static int do_blkbszget(unsigned int fd, unsigned int cmd, unsigned long arg)
{
       return sys_ioctl(fd, BLKBSZGET, (unsigned long)compat_ptr(arg));
}

static int do_blkbszset(unsigned int fd, unsigned int cmd, unsigned long arg)
{
       return sys_ioctl(fd, BLKBSZSET, (unsigned long)compat_ptr(arg));
}

static int do_blkgetsize64(unsigned int fd, unsigned int cmd,
                          unsigned long arg)
{
       return sys_ioctl(fd, BLKGETSIZE64, (unsigned long)compat_ptr(arg));
}

/* Bluetooth ioctls */
#define HCIUARTSETPROTO	_IOW('U', 200, int)
#define HCIUARTGETPROTO	_IOR('U', 201, int)

#define BNEPCONNADD	_IOW('B', 200, int)
#define BNEPCONNDEL	_IOW('B', 201, int)
#define BNEPGETCONNLIST	_IOR('B', 210, int)
#define BNEPGETCONNINFO	_IOR('B', 211, int)

#define CMTPCONNADD	_IOW('C', 200, int)
#define CMTPCONNDEL	_IOW('C', 201, int)
#define CMTPGETCONNLIST	_IOR('C', 210, int)
#define CMTPGETCONNINFO	_IOR('C', 211, int)

#define HIDPCONNADD	_IOW('H', 200, int)
#define HIDPCONNDEL	_IOW('H', 201, int)
#define HIDPGETCONNLIST	_IOR('H', 210, int)
#define HIDPGETCONNINFO	_IOR('H', 211, int)

struct floppy_struct32 {
	compat_uint_t	size;
	compat_uint_t	sect;
	compat_uint_t	head;
	compat_uint_t	track;
	compat_uint_t	stretch;
	unsigned char	gap;
	unsigned char	rate;
	unsigned char	spec1;
	unsigned char	fmt_gap;
	const compat_caddr_t name;
};

struct floppy_drive_params32 {
	char		cmos;
	compat_ulong_t	max_dtr;
	compat_ulong_t	hlt;
	compat_ulong_t	hut;
	compat_ulong_t	srt;
	compat_ulong_t	spinup;
	compat_ulong_t	spindown;
	unsigned char	spindown_offset;
	unsigned char	select_delay;
	unsigned char	rps;
	unsigned char	tracks;
	compat_ulong_t	timeout;
	unsigned char	interleave_sect;
	struct floppy_max_errors max_errors;
	char		flags;
	char		read_track;
	short		autodetect[8];
	compat_int_t	checkfreq;
	compat_int_t	native_format;
};

struct floppy_drive_struct32 {
	signed char	flags;
	compat_ulong_t	spinup_date;
	compat_ulong_t	select_date;
	compat_ulong_t	first_read_date;
	short		probed_format;
	short		track;
	short		maxblock;
	short		maxtrack;
	compat_int_t	generation;
	compat_int_t	keep_data;
	compat_int_t	fd_ref;
	compat_int_t	fd_device;
	compat_int_t	last_checked;
	compat_caddr_t dmabuf;
	compat_int_t	bufblocks;
};

struct floppy_fdc_state32 {
	compat_int_t	spec1;
	compat_int_t	spec2;
	compat_int_t	dtr;
	unsigned char	version;
	unsigned char	dor;
	compat_ulong_t	address;
	unsigned int	rawcmd:2;
	unsigned int	reset:1;
	unsigned int	need_configure:1;
	unsigned int	perp_mode:2;
	unsigned int	has_fifo:1;
	unsigned int	driver_version;
	unsigned char	track[4];
};

struct floppy_write_errors32 {
	unsigned int	write_errors;
	compat_ulong_t	first_error_sector;
	compat_int_t	first_error_generation;
	compat_ulong_t	last_error_sector;
	compat_int_t	last_error_generation;
	compat_uint_t	badness;
};

#define FDSETPRM32 _IOW(2, 0x42, struct floppy_struct32)
#define FDDEFPRM32 _IOW(2, 0x43, struct floppy_struct32)
#define FDGETPRM32 _IOR(2, 0x04, struct floppy_struct32)
#define FDSETDRVPRM32 _IOW(2, 0x90, struct floppy_drive_params32)
#define FDGETDRVPRM32 _IOR(2, 0x11, struct floppy_drive_params32)
#define FDGETDRVSTAT32 _IOR(2, 0x12, struct floppy_drive_struct32)
#define FDPOLLDRVSTAT32 _IOR(2, 0x13, struct floppy_drive_struct32)
#define FDGETFDCSTAT32 _IOR(2, 0x15, struct floppy_fdc_state32)
#define FDWERRORGET32  _IOR(2, 0x17, struct floppy_write_errors32)

static struct {
	unsigned int	cmd32;
	unsigned int	cmd;
} fd_ioctl_trans_table[] = {
	{ FDSETPRM32, FDSETPRM },
	{ FDDEFPRM32, FDDEFPRM },
	{ FDGETPRM32, FDGETPRM },
	{ FDSETDRVPRM32, FDSETDRVPRM },
	{ FDGETDRVPRM32, FDGETDRVPRM },
	{ FDGETDRVSTAT32, FDGETDRVSTAT },
	{ FDPOLLDRVSTAT32, FDPOLLDRVSTAT },
	{ FDGETFDCSTAT32, FDGETFDCSTAT },
	{ FDWERRORGET32, FDWERRORGET }
};

#define NR_FD_IOCTL_TRANS (sizeof(fd_ioctl_trans_table)/sizeof(fd_ioctl_trans_table[0]))

static int fd_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	void *karg = NULL;
	unsigned int kcmd = 0;
	int i, err;

	for (i = 0; i < NR_FD_IOCTL_TRANS; i++)
		if (cmd == fd_ioctl_trans_table[i].cmd32) {
			kcmd = fd_ioctl_trans_table[i].cmd;
			break;
		}
	if (!kcmd)
		return -EINVAL;

	switch (cmd) {
		case FDSETPRM32:
		case FDDEFPRM32:
		case FDGETPRM32:
		{
			compat_uptr_t name;
			struct floppy_struct32 __user *uf;
			struct floppy_struct *f;

			uf = compat_ptr(arg);
			f = karg = kmalloc(sizeof(struct floppy_struct), GFP_KERNEL);
			if (!karg)
				return -ENOMEM;
			if (cmd == FDGETPRM32)
				break;
			err = __get_user(f->size, &uf->size);
			err |= __get_user(f->sect, &uf->sect);
			err |= __get_user(f->head, &uf->head);
			err |= __get_user(f->track, &uf->track);
			err |= __get_user(f->stretch, &uf->stretch);
			err |= __get_user(f->gap, &uf->gap);
			err |= __get_user(f->rate, &uf->rate);
			err |= __get_user(f->spec1, &uf->spec1);
			err |= __get_user(f->fmt_gap, &uf->fmt_gap);
			err |= __get_user(name, &uf->name);
			f->name = compat_ptr(name);
			if (err) {
				err = -EFAULT;
				goto out;
			}
			break;
		}
		case FDSETDRVPRM32:
		case FDGETDRVPRM32:
		{
			struct floppy_drive_params32 __user *uf;
			struct floppy_drive_params *f;

			uf = compat_ptr(arg);
			f = karg = kmalloc(sizeof(struct floppy_drive_params), GFP_KERNEL);
			if (!karg)
				return -ENOMEM;
			if (cmd == FDGETDRVPRM32)
				break;
			err = __get_user(f->cmos, &uf->cmos);
			err |= __get_user(f->max_dtr, &uf->max_dtr);
			err |= __get_user(f->hlt, &uf->hlt);
			err |= __get_user(f->hut, &uf->hut);
			err |= __get_user(f->srt, &uf->srt);
			err |= __get_user(f->spinup, &uf->spinup);
			err |= __get_user(f->spindown, &uf->spindown);
			err |= __get_user(f->spindown_offset, &uf->spindown_offset);
			err |= __get_user(f->select_delay, &uf->select_delay);
			err |= __get_user(f->rps, &uf->rps);
			err |= __get_user(f->tracks, &uf->tracks);
			err |= __get_user(f->timeout, &uf->timeout);
			err |= __get_user(f->interleave_sect, &uf->interleave_sect);
			err |= __copy_from_user(&f->max_errors, &uf->max_errors, sizeof(f->max_errors));
			err |= __get_user(f->flags, &uf->flags);
			err |= __get_user(f->read_track, &uf->read_track);
			err |= __copy_from_user(f->autodetect, uf->autodetect, sizeof(f->autodetect));
			err |= __get_user(f->checkfreq, &uf->checkfreq);
			err |= __get_user(f->native_format, &uf->native_format);
			if (err) {
				err = -EFAULT;
				goto out;
			}
			break;
		}
		case FDGETDRVSTAT32:
		case FDPOLLDRVSTAT32:
			karg = kmalloc(sizeof(struct floppy_drive_struct), GFP_KERNEL);
			if (!karg)
				return -ENOMEM;
			break;
		case FDGETFDCSTAT32:
			karg = kmalloc(sizeof(struct floppy_fdc_state), GFP_KERNEL);
			if (!karg)
				return -ENOMEM;
			break;
		case FDWERRORGET32:
			karg = kmalloc(sizeof(struct floppy_write_errors), GFP_KERNEL);
			if (!karg)
				return -ENOMEM;
			break;
		default:
			return -EINVAL;
	}
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, kcmd, (unsigned long)karg);
	set_fs (old_fs);
	if (err)
		goto out;
	switch (cmd) {
		case FDGETPRM32:
		{
			struct floppy_struct *f = karg;
			struct floppy_struct32 __user *uf = compat_ptr(arg);

			err = __put_user(f->size, &uf->size);
			err |= __put_user(f->sect, &uf->sect);
			err |= __put_user(f->head, &uf->head);
			err |= __put_user(f->track, &uf->track);
			err |= __put_user(f->stretch, &uf->stretch);
			err |= __put_user(f->gap, &uf->gap);
			err |= __put_user(f->rate, &uf->rate);
			err |= __put_user(f->spec1, &uf->spec1);
			err |= __put_user(f->fmt_gap, &uf->fmt_gap);
			err |= __put_user((u64)f->name, (compat_caddr_t __user *)&uf->name);
			break;
		}
		case FDGETDRVPRM32:
		{
			struct floppy_drive_params32 __user *uf;
			struct floppy_drive_params *f = karg;

			uf = compat_ptr(arg);
			err = __put_user(f->cmos, &uf->cmos);
			err |= __put_user(f->max_dtr, &uf->max_dtr);
			err |= __put_user(f->hlt, &uf->hlt);
			err |= __put_user(f->hut, &uf->hut);
			err |= __put_user(f->srt, &uf->srt);
			err |= __put_user(f->spinup, &uf->spinup);
			err |= __put_user(f->spindown, &uf->spindown);
			err |= __put_user(f->spindown_offset, &uf->spindown_offset);
			err |= __put_user(f->select_delay, &uf->select_delay);
			err |= __put_user(f->rps, &uf->rps);
			err |= __put_user(f->tracks, &uf->tracks);
			err |= __put_user(f->timeout, &uf->timeout);
			err |= __put_user(f->interleave_sect, &uf->interleave_sect);
			err |= __copy_to_user(&uf->max_errors, &f->max_errors, sizeof(f->max_errors));
			err |= __put_user(f->flags, &uf->flags);
			err |= __put_user(f->read_track, &uf->read_track);
			err |= __copy_to_user(uf->autodetect, f->autodetect, sizeof(f->autodetect));
			err |= __put_user(f->checkfreq, &uf->checkfreq);
			err |= __put_user(f->native_format, &uf->native_format);
			break;
		}
		case FDGETDRVSTAT32:
		case FDPOLLDRVSTAT32:
		{
			struct floppy_drive_struct32 __user *uf;
			struct floppy_drive_struct *f = karg;

			uf = compat_ptr(arg);
			err = __put_user(f->flags, &uf->flags);
			err |= __put_user(f->spinup_date, &uf->spinup_date);
			err |= __put_user(f->select_date, &uf->select_date);
			err |= __put_user(f->first_read_date, &uf->first_read_date);
			err |= __put_user(f->probed_format, &uf->probed_format);
			err |= __put_user(f->track, &uf->track);
			err |= __put_user(f->maxblock, &uf->maxblock);
			err |= __put_user(f->maxtrack, &uf->maxtrack);
			err |= __put_user(f->generation, &uf->generation);
			err |= __put_user(f->keep_data, &uf->keep_data);
			err |= __put_user(f->fd_ref, &uf->fd_ref);
			err |= __put_user(f->fd_device, &uf->fd_device);
			err |= __put_user(f->last_checked, &uf->last_checked);
			err |= __put_user((u64)f->dmabuf, &uf->dmabuf);
			err |= __put_user((u64)f->bufblocks, &uf->bufblocks);
			break;
		}
		case FDGETFDCSTAT32:
		{
			struct floppy_fdc_state32 __user *uf;
			struct floppy_fdc_state *f = karg;

			uf = compat_ptr(arg);
			err = __put_user(f->spec1, &uf->spec1);
			err |= __put_user(f->spec2, &uf->spec2);
			err |= __put_user(f->dtr, &uf->dtr);
			err |= __put_user(f->version, &uf->version);
			err |= __put_user(f->dor, &uf->dor);
			err |= __put_user(f->address, &uf->address);
			err |= __copy_to_user((char __user *)&uf->address + sizeof(uf->address),
					   (char *)&f->address + sizeof(f->address), sizeof(int));
			err |= __put_user(f->driver_version, &uf->driver_version);
			err |= __copy_to_user(uf->track, f->track, sizeof(f->track));
			break;
		}
		case FDWERRORGET32:
		{
			struct floppy_write_errors32 __user *uf;
			struct floppy_write_errors *f = karg;

			uf = compat_ptr(arg);
			err = __put_user(f->write_errors, &uf->write_errors);
			err |= __put_user(f->first_error_sector, &uf->first_error_sector);
			err |= __put_user(f->first_error_generation, &uf->first_error_generation);
			err |= __put_user(f->last_error_sector, &uf->last_error_sector);
			err |= __put_user(f->last_error_generation, &uf->last_error_generation);
			err |= __put_user(f->badness, &uf->badness);
			break;
		}
		default:
			break;
	}
	if (err)
		err = -EFAULT;

out:	if (karg) kfree(karg);
	return err;
}

struct mtd_oob_buf32 {
	u_int32_t start;
	u_int32_t length;
	compat_caddr_t ptr;	/* unsigned char* */
};

#define MEMWRITEOOB32 	_IOWR('M',3,struct mtd_oob_buf32)
#define MEMREADOOB32 	_IOWR('M',4,struct mtd_oob_buf32)

static int mtd_rw_oob(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct mtd_oob_buf __user *buf = compat_alloc_user_space(sizeof(*buf));
	struct mtd_oob_buf32 __user *buf32 = compat_ptr(arg);
	u32 data;
	char __user *datap;
	unsigned int real_cmd;
	int err;

	real_cmd = (cmd == MEMREADOOB32) ?
		MEMREADOOB : MEMWRITEOOB;

	if (copy_in_user(&buf->start, &buf32->start,
			 2 * sizeof(u32)) ||
	    get_user(data, &buf32->ptr))
		return -EFAULT;
	datap = compat_ptr(data);
	if (put_user(datap, &buf->ptr))
		return -EFAULT;

	err = sys_ioctl(fd, real_cmd, (unsigned long) buf);

	if (!err) {
		if (copy_in_user(&buf32->start, &buf->start,
				 2 * sizeof(u32)))
			err = -EFAULT;
	}

	return err;
}	

#define	VFAT_IOCTL_READDIR_BOTH32	_IOR('r', 1, struct compat_dirent[2])
#define	VFAT_IOCTL_READDIR_SHORT32	_IOR('r', 2, struct compat_dirent[2])

static long
put_dirent32 (struct dirent *d, struct compat_dirent __user *d32)
{
        if (!access_ok(VERIFY_WRITE, d32, sizeof(struct compat_dirent)))
                return -EFAULT;

        __put_user(d->d_ino, &d32->d_ino);
        __put_user(d->d_off, &d32->d_off);
        __put_user(d->d_reclen, &d32->d_reclen);
        if (__copy_to_user(d32->d_name, d->d_name, d->d_reclen))
		return -EFAULT;

        return 0;
}

static int vfat_ioctl32(unsigned fd, unsigned cmd, unsigned long arg)
{
	struct compat_dirent __user *p = compat_ptr(arg);
	int ret;
	mm_segment_t oldfs = get_fs();
	struct dirent d[2];

	switch(cmd)
	{
        	case VFAT_IOCTL_READDIR_BOTH32:
                	cmd = VFAT_IOCTL_READDIR_BOTH;
                	break;
        	case VFAT_IOCTL_READDIR_SHORT32:
                	cmd = VFAT_IOCTL_READDIR_SHORT;
                	break;
	}

	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd,cmd,(unsigned long)&d);
	set_fs(oldfs);
	if (ret >= 0) {
		ret |= put_dirent32(&d[0], p);
		ret |= put_dirent32(&d[1], p + 1);
	}
	return ret;
}

#define REISERFS_IOC_UNPACK32               _IOW(0xCD,1,int)

static int reiserfs_ioctl32(unsigned fd, unsigned cmd, unsigned long ptr)
{
        if (cmd == REISERFS_IOC_UNPACK32)
                cmd = REISERFS_IOC_UNPACK;

        return sys_ioctl(fd,cmd,ptr);
}

struct raw32_config_request
{
        compat_int_t    raw_minor;
        __u64   block_major;
        __u64   block_minor;
} __attribute__((packed));

static int get_raw32_request(struct raw_config_request *req, struct raw32_config_request __user *user_req)
{
        int ret;

        if (!access_ok(VERIFY_READ, user_req, sizeof(struct raw32_config_request)))
                return -EFAULT;

        ret = __get_user(req->raw_minor, &user_req->raw_minor);
        ret |= __get_user(req->block_major, &user_req->block_major);
        ret |= __get_user(req->block_minor, &user_req->block_minor);

        return ret ? -EFAULT : 0;
}

static int set_raw32_request(struct raw_config_request *req, struct raw32_config_request __user *user_req)
{
	int ret;

        if (!access_ok(VERIFY_WRITE, user_req, sizeof(struct raw32_config_request)))
                return -EFAULT;

        ret = __put_user(req->raw_minor, &user_req->raw_minor);
        ret |= __put_user(req->block_major, &user_req->block_major);
        ret |= __put_user(req->block_minor, &user_req->block_minor);

        return ret ? -EFAULT : 0;
}

static int raw_ioctl(unsigned fd, unsigned cmd, unsigned long arg)
{
        int ret;

        switch (cmd) {
        case RAW_SETBIND:
        case RAW_GETBIND: {
                struct raw_config_request req;
                struct raw32_config_request __user *user_req = compat_ptr(arg);
                mm_segment_t oldfs = get_fs();

                if ((ret = get_raw32_request(&req, user_req)))
                        return ret;

                set_fs(KERNEL_DS);
                ret = sys_ioctl(fd,cmd,(unsigned long)&req);
                set_fs(oldfs);

                if ((!ret) && (cmd == RAW_GETBIND)) {
                        ret = set_raw32_request(&req, user_req);
                }
                break;
        }
        default:
                ret = sys_ioctl(fd, cmd, arg);
                break;
        }
        return ret;
}

struct serial_struct32 {
        compat_int_t    type;
        compat_int_t    line;
        compat_uint_t   port;
        compat_int_t    irq;
        compat_int_t    flags;
        compat_int_t    xmit_fifo_size;
        compat_int_t    custom_divisor;
        compat_int_t    baud_base;
        unsigned short  close_delay;
        char    io_type;
        char    reserved_char[1];
        compat_int_t    hub6;
        unsigned short  closing_wait; /* time to wait before closing */
        unsigned short  closing_wait2; /* no longer used... */
        compat_uint_t   iomem_base;
        unsigned short  iomem_reg_shift;
        unsigned int    port_high;
     /* compat_ulong_t  iomap_base FIXME */
        compat_int_t    reserved[1];
};

static int serial_struct_ioctl(unsigned fd, unsigned cmd, unsigned long arg)
{
        typedef struct serial_struct SS;
        typedef struct serial_struct32 SS32;
        struct serial_struct32 __user *ss32 = compat_ptr(arg);
        int err;
        struct serial_struct ss;
        mm_segment_t oldseg = get_fs();
        __u32 udata;

        if (cmd == TIOCSSERIAL) {
                if (!access_ok(VERIFY_READ, ss32, sizeof(SS32)))
                        return -EFAULT;
                if (__copy_from_user(&ss, ss32, offsetof(SS32, iomem_base)))
			return -EFAULT;
                __get_user(udata, &ss32->iomem_base);
                ss.iomem_base = compat_ptr(udata);
                __get_user(ss.iomem_reg_shift, &ss32->iomem_reg_shift);
                __get_user(ss.port_high, &ss32->port_high);
                ss.iomap_base = 0UL;
        }
        set_fs(KERNEL_DS);
                err = sys_ioctl(fd,cmd,(unsigned long)(&ss));
        set_fs(oldseg);
        if (cmd == TIOCGSERIAL && err >= 0) {
                if (!access_ok(VERIFY_WRITE, ss32, sizeof(SS32)))
                        return -EFAULT;
                if (__copy_to_user(ss32,&ss,offsetof(SS32,iomem_base)))
			return -EFAULT;
                __put_user((unsigned long)ss.iomem_base  >> 32 ?
                            0xffffffff : (unsigned)(unsigned long)ss.iomem_base,
                            &ss32->iomem_base);
                __put_user(ss.iomem_reg_shift, &ss32->iomem_reg_shift);
                __put_user(ss.port_high, &ss32->port_high);

        }
        return err;
}

struct usbdevfs_ctrltransfer32 {
        u8 bRequestType;
        u8 bRequest;
        u16 wValue;
        u16 wIndex;
        u16 wLength;
        u32 timeout;  /* in milliseconds */
        compat_caddr_t data;
};

#define USBDEVFS_CONTROL32           _IOWR('U', 0, struct usbdevfs_ctrltransfer32)

static int do_usbdevfs_control(unsigned int fd, unsigned int cmd, unsigned long arg)
{
        struct usbdevfs_ctrltransfer32 __user *p32 = compat_ptr(arg);
        struct usbdevfs_ctrltransfer __user *p;
        __u32 udata;
        p = compat_alloc_user_space(sizeof(*p));
        if (copy_in_user(p, p32, (sizeof(*p32) - sizeof(compat_caddr_t))) ||
            get_user(udata, &p32->data) ||
	    put_user(compat_ptr(udata), &p->data))
		return -EFAULT;
        return sys_ioctl(fd, USBDEVFS_CONTROL, (unsigned long)p);
}


struct usbdevfs_bulktransfer32 {
        compat_uint_t ep;
        compat_uint_t len;
        compat_uint_t timeout; /* in milliseconds */
        compat_caddr_t data;
};

#define USBDEVFS_BULK32              _IOWR('U', 2, struct usbdevfs_bulktransfer32)

static int do_usbdevfs_bulk(unsigned int fd, unsigned int cmd, unsigned long arg)
{
        struct usbdevfs_bulktransfer32 __user *p32 = compat_ptr(arg);
        struct usbdevfs_bulktransfer __user *p;
        compat_uint_t n;
        compat_caddr_t addr;

        p = compat_alloc_user_space(sizeof(*p));

        if (get_user(n, &p32->ep) || put_user(n, &p->ep) ||
            get_user(n, &p32->len) || put_user(n, &p->len) ||
            get_user(n, &p32->timeout) || put_user(n, &p->timeout) ||
            get_user(addr, &p32->data) || put_user(compat_ptr(addr), &p->data))
                return -EFAULT;

        return sys_ioctl(fd, USBDEVFS_BULK, (unsigned long)p);
}


/*
 *  USBDEVFS_SUBMITURB, USBDEVFS_REAPURB and USBDEVFS_REAPURBNDELAY
 *  are handled in usbdevfs core.			-Christopher Li
 */

struct usbdevfs_disconnectsignal32 {
        compat_int_t signr;
        compat_caddr_t context;
};

#define USBDEVFS_DISCSIGNAL32      _IOR('U', 14, struct usbdevfs_disconnectsignal32)

static int do_usbdevfs_discsignal(unsigned int fd, unsigned int cmd, unsigned long arg)
{
        struct usbdevfs_disconnectsignal kdis;
        struct usbdevfs_disconnectsignal32 __user *udis;
        mm_segment_t old_fs;
        u32 uctx;
        int err;

        udis = compat_ptr(arg);

        if (get_user(kdis.signr, &udis->signr) ||
            __get_user(uctx, &udis->context))
                return -EFAULT;

        kdis.context = compat_ptr(uctx);

        old_fs = get_fs();
        set_fs(KERNEL_DS);
        err = sys_ioctl(fd, USBDEVFS_DISCSIGNAL, (unsigned long) &kdis);
        set_fs(old_fs);

        return err;
}

/*
 * I2C layer ioctls
 */

struct i2c_msg32 {
	u16 addr;
	u16 flags;
	u16 len;
	compat_caddr_t buf;
};

struct i2c_rdwr_ioctl_data32 {
	compat_caddr_t msgs; /* struct i2c_msg __user *msgs */
	u32 nmsgs;
};

struct i2c_smbus_ioctl_data32 {
	u8 read_write;
	u8 command;
	u32 size;
	compat_caddr_t data; /* union i2c_smbus_data *data */
};

struct i2c_rdwr_aligned {
	struct i2c_rdwr_ioctl_data cmd;
	struct i2c_msg msgs[0];
};

static int do_i2c_rdwr_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct i2c_rdwr_ioctl_data32	__user *udata = compat_ptr(arg);
	struct i2c_rdwr_aligned		__user *tdata;
	struct i2c_msg			__user *tmsgs;
	struct i2c_msg32		__user *umsgs;
	compat_caddr_t			datap;
	int				nmsgs, i;

	if (get_user(nmsgs, &udata->nmsgs))
		return -EFAULT;
	if (nmsgs > I2C_RDRW_IOCTL_MAX_MSGS)
		return -EINVAL;

	if (get_user(datap, &udata->msgs))
		return -EFAULT;
	umsgs = compat_ptr(datap);

	tdata = compat_alloc_user_space(sizeof(*tdata) +
				      nmsgs * sizeof(struct i2c_msg));
	tmsgs = &tdata->msgs[0];

	if (put_user(nmsgs, &tdata->cmd.nmsgs) ||
	    put_user(tmsgs, &tdata->cmd.msgs))
		return -EFAULT;

	for (i = 0; i < nmsgs; i++) {
		if (copy_in_user(&tmsgs[i].addr, &umsgs[i].addr, 3*sizeof(u16)))
			return -EFAULT;
		if (get_user(datap, &umsgs[i].buf) ||
		    put_user(compat_ptr(datap), &tmsgs[i].buf))
			return -EFAULT;
	}
	return sys_ioctl(fd, cmd, (unsigned long)tdata);
}

static int do_i2c_smbus_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct i2c_smbus_ioctl_data	__user *tdata;
	struct i2c_smbus_ioctl_data32	__user *udata;
	compat_caddr_t			datap;

	tdata = compat_alloc_user_space(sizeof(*tdata));
	if (tdata == NULL)
		return -ENOMEM;
	if (!access_ok(VERIFY_WRITE, tdata, sizeof(*tdata)))
		return -EFAULT;

	udata = compat_ptr(arg);
	if (!access_ok(VERIFY_READ, udata, sizeof(*udata)))
		return -EFAULT;

	if (__copy_in_user(&tdata->read_write, &udata->read_write, 2 * sizeof(u8)))
		return -EFAULT;
	if (__copy_in_user(&tdata->size, &udata->size, 2 * sizeof(u32)))
		return -EFAULT;
	if (__get_user(datap, &udata->data) ||
	    __put_user(compat_ptr(datap), &tdata->data))
		return -EFAULT;

	return sys_ioctl(fd, cmd, (unsigned long)tdata);
}

struct compat_iw_point {
	compat_caddr_t pointer;
	__u16 length;
	__u16 flags;
};

static int do_wireless_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct iwreq __user *iwr;
	struct iwreq __user *iwr_u;
	struct iw_point __user *iwp;
	struct compat_iw_point __user *iwp_u;
	compat_caddr_t pointer;
	__u16 length, flags;

	iwr_u = compat_ptr(arg);
	iwp_u = (struct compat_iw_point __user *) &iwr_u->u.data;
	iwr = compat_alloc_user_space(sizeof(*iwr));
	if (iwr == NULL)
		return -ENOMEM;

	iwp = &iwr->u.data;

	if (!access_ok(VERIFY_WRITE, iwr, sizeof(*iwr)))
		return -EFAULT;

	if (__copy_in_user(&iwr->ifr_ifrn.ifrn_name[0],
			   &iwr_u->ifr_ifrn.ifrn_name[0],
			   sizeof(iwr->ifr_ifrn.ifrn_name)))
		return -EFAULT;

	if (__get_user(pointer, &iwp_u->pointer) ||
	    __get_user(length, &iwp_u->length) ||
	    __get_user(flags, &iwp_u->flags))
		return -EFAULT;

	if (__put_user(compat_ptr(pointer), &iwp->pointer) ||
	    __put_user(length, &iwp->length) ||
	    __put_user(flags, &iwp->flags))
		return -EFAULT;

	return sys_ioctl(fd, cmd, (unsigned long) iwr);
}

/* Since old style bridge ioctl's endup using SIOCDEVPRIVATE
 * for some operations; this forces use of the newer bridge-utils that
 * use compatiable ioctls
 */
static int old_bridge_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	u32 tmp;

	if (get_user(tmp, (u32 __user *) arg))
		return -EFAULT;
	if (tmp == BRCTL_GET_VERSION)
		return BRCTL_VERSION + 1;
	return -EINVAL;
}

#if defined(CONFIG_NCP_FS) || defined(CONFIG_NCP_FS_MODULE)
struct ncp_ioctl_request_32 {
	u32 function;
	u32 size;
	compat_caddr_t data;
};

struct ncp_fs_info_v2_32 {
	s32 version;
	u32 mounted_uid;
	u32 connection;
	u32 buffer_size;

	u32 volume_number;
	u32 directory_id;

	u32 dummy1;
	u32 dummy2;
	u32 dummy3;
};

struct ncp_objectname_ioctl_32
{
	s32		auth_type;
	u32		object_name_len;
	compat_caddr_t	object_name;	/* an userspace data, in most cases user name */
};

struct ncp_privatedata_ioctl_32
{
	u32		len;
	compat_caddr_t	data;		/* ~1000 for NDS */
};

#define	NCP_IOC_NCPREQUEST_32		_IOR('n', 1, struct ncp_ioctl_request_32)
#define NCP_IOC_GETMOUNTUID2_32		_IOW('n', 2, u32)
#define NCP_IOC_GET_FS_INFO_V2_32	_IOWR('n', 4, struct ncp_fs_info_v2_32)
#define NCP_IOC_GETOBJECTNAME_32	_IOWR('n', 9, struct ncp_objectname_ioctl_32)
#define NCP_IOC_SETOBJECTNAME_32	_IOR('n', 9, struct ncp_objectname_ioctl_32)
#define NCP_IOC_GETPRIVATEDATA_32	_IOWR('n', 10, struct ncp_privatedata_ioctl_32)
#define NCP_IOC_SETPRIVATEDATA_32	_IOR('n', 10, struct ncp_privatedata_ioctl_32)

static int do_ncp_ncprequest(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_ioctl_request_32 n32;
	struct ncp_ioctl_request __user *p = compat_alloc_user_space(sizeof(*p));

	if (copy_from_user(&n32, compat_ptr(arg), sizeof(n32)) ||
	    put_user(n32.function, &p->function) ||
	    put_user(n32.size, &p->size) ||
	    put_user(compat_ptr(n32.data), &p->data))
		return -EFAULT;

	return sys_ioctl(fd, NCP_IOC_NCPREQUEST, (unsigned long)p);
}

static int do_ncp_getmountuid2(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	__kernel_uid_t kuid;
	int err;

	cmd = NCP_IOC_GETMOUNTUID2;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&kuid);
	set_fs(old_fs);

	if (!err)
		err = put_user(kuid,
			       (unsigned int __user *) compat_ptr(arg));

	return err;
}

static int do_ncp_getfsinfo2(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct ncp_fs_info_v2_32 n32;
	struct ncp_fs_info_v2 n;
	int err;

	if (copy_from_user(&n32, compat_ptr(arg), sizeof(n32)))
		return -EFAULT;
	if (n32.version != NCP_GET_FS_INFO_VERSION_V2)
		return -EINVAL;
	n.version = NCP_GET_FS_INFO_VERSION_V2;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, NCP_IOC_GET_FS_INFO_V2, (unsigned long)&n);
	set_fs(old_fs);

	if (!err) {
		n32.version = n.version;
		n32.mounted_uid = n.mounted_uid;
		n32.connection = n.connection;
		n32.buffer_size = n.buffer_size;
		n32.volume_number = n.volume_number;
		n32.directory_id = n.directory_id;
		n32.dummy1 = n.dummy1;
		n32.dummy2 = n.dummy2;
		n32.dummy3 = n.dummy3;
		err = copy_to_user(compat_ptr(arg), &n32, sizeof(n32)) ? -EFAULT : 0;
	}
	return err;
}

static int do_ncp_getobjectname(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_objectname_ioctl_32 n32, __user *p32 = compat_ptr(arg);
	struct ncp_objectname_ioctl __user *p = compat_alloc_user_space(sizeof(*p));
	s32 auth_type;
	u32 name_len;
	int err;

	if (copy_from_user(&n32, p32, sizeof(n32)) ||
	    put_user(n32.object_name_len, &p->object_name_len) ||
	    put_user(compat_ptr(n32.object_name), &p->object_name))
		return -EFAULT;

	err = sys_ioctl(fd, NCP_IOC_GETOBJECTNAME, (unsigned long)p);
        if (err)
		return err;

	if (get_user(auth_type, &p->auth_type) ||
	    put_user(auth_type, &p32->auth_type) ||
	    get_user(name_len, &p->object_name_len) ||
	    put_user(name_len, &p32->object_name_len))
		return -EFAULT;

	return 0;
}

static int do_ncp_setobjectname(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_objectname_ioctl_32 n32, __user *p32 = compat_ptr(arg);
	struct ncp_objectname_ioctl __user *p = compat_alloc_user_space(sizeof(*p));

	if (copy_from_user(&n32, p32, sizeof(n32)) ||
	    put_user(n32.auth_type, &p->auth_type) ||
	    put_user(n32.object_name_len, &p->object_name_len) ||
	    put_user(compat_ptr(n32.object_name), &p->object_name))
		return -EFAULT;

	return sys_ioctl(fd, NCP_IOC_SETOBJECTNAME, (unsigned long)p);
}

static int do_ncp_getprivatedata(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_privatedata_ioctl_32 n32, __user *p32 = compat_ptr(arg);
	struct ncp_privatedata_ioctl __user *p =
		compat_alloc_user_space(sizeof(*p));
	u32 len;
	int err;

	if (copy_from_user(&n32, p32, sizeof(n32)) ||
	    put_user(n32.len, &p->len) ||
	    put_user(compat_ptr(n32.data), &p->data))
		return -EFAULT;

	err = sys_ioctl(fd, NCP_IOC_GETPRIVATEDATA, (unsigned long)p);
        if (err)
		return err;

	if (get_user(len, &p->len) ||
	    put_user(len, &p32->len))
		return -EFAULT;

	return 0;
}

static int do_ncp_setprivatedata(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ncp_privatedata_ioctl_32 n32;
	struct ncp_privatedata_ioctl_32 __user *p32 = compat_ptr(arg);
	struct ncp_privatedata_ioctl __user *p =
		compat_alloc_user_space(sizeof(*p));

	if (copy_from_user(&n32, p32, sizeof(n32)) ||
	    put_user(n32.len, &p->len) ||
	    put_user(compat_ptr(n32.data), &p->data))
		return -EFAULT;

	return sys_ioctl(fd, NCP_IOC_SETPRIVATEDATA, (unsigned long)p);
}
#endif

#undef CODE
#endif

#ifdef DECLARES
HANDLE_IOCTL(MEMREADOOB32, mtd_rw_oob)
HANDLE_IOCTL(MEMWRITEOOB32, mtd_rw_oob)
#ifdef CONFIG_NET
HANDLE_IOCTL(SIOCGIFNAME, dev_ifname32)
HANDLE_IOCTL(SIOCGIFCONF, dev_ifconf)
HANDLE_IOCTL(SIOCGIFFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMETRIC, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMETRIC, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMTU, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMTU, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMEM, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMEM, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFHWADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFHWADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCADDMULTI, dev_ifsioc)
HANDLE_IOCTL(SIOCDELMULTI, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFINDEX, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMAP, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMAP, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFADDR, dev_ifsioc)

/* ioctls used by appletalk ddp.c */
HANDLE_IOCTL(SIOCATALKDIFADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCDIFADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSARP, dev_ifsioc)
HANDLE_IOCTL(SIOCDARP, dev_ifsioc)

HANDLE_IOCTL(SIOCGIFBRDADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFBRDADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFDSTADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFDSTADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFNETMASK, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFNETMASK, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFPFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFPFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFTXQLEN, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFTXQLEN, dev_ifsioc)
HANDLE_IOCTL(TUNSETIFF, dev_ifsioc)
HANDLE_IOCTL(SIOCETHTOOL, ethtool_ioctl)
HANDLE_IOCTL(SIOCBONDENSLAVE, bond_ioctl)
HANDLE_IOCTL(SIOCBONDRELEASE, bond_ioctl)
HANDLE_IOCTL(SIOCBONDSETHWADDR, bond_ioctl)
HANDLE_IOCTL(SIOCBONDSLAVEINFOQUERY, bond_ioctl)
HANDLE_IOCTL(SIOCBONDINFOQUERY, bond_ioctl)
HANDLE_IOCTL(SIOCBONDCHANGEACTIVE, bond_ioctl)
HANDLE_IOCTL(SIOCADDRT, routing_ioctl)
HANDLE_IOCTL(SIOCDELRT, routing_ioctl)
HANDLE_IOCTL(SIOCBRADDIF, dev_ifsioc)
HANDLE_IOCTL(SIOCBRDELIF, dev_ifsioc)
/* Note SIOCRTMSG is no longer, so this is safe and * the user would have seen just an -EINVAL anyways. */
HANDLE_IOCTL(SIOCRTMSG, ret_einval)
HANDLE_IOCTL(SIOCGSTAMP, do_siocgstamp)
#endif
HANDLE_IOCTL(HDIO_GETGEO, hdio_getgeo)
HANDLE_IOCTL(BLKRAGET, w_long)
HANDLE_IOCTL(BLKGETSIZE, w_long)
HANDLE_IOCTL(0x1260, broken_blkgetsize)
HANDLE_IOCTL(BLKFRAGET, w_long)
HANDLE_IOCTL(BLKSECTGET, w_long)
HANDLE_IOCTL(FBIOGET_FSCREENINFO, fb_ioctl_trans)
HANDLE_IOCTL(BLKPG, blkpg_ioctl_trans)
HANDLE_IOCTL(FBIOGETCMAP, fb_ioctl_trans)
HANDLE_IOCTL(FBIOPUTCMAP, fb_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_KEEPSETTINGS, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_UNMASKINTR, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_DMA, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_32BIT, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_MULTCOUNT, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_NOWERR, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_NICE, hdio_ioctl_trans)
HANDLE_IOCTL(FDSETPRM32, fd_ioctl_trans)
HANDLE_IOCTL(FDDEFPRM32, fd_ioctl_trans)
HANDLE_IOCTL(FDGETPRM32, fd_ioctl_trans)
HANDLE_IOCTL(FDSETDRVPRM32, fd_ioctl_trans)
HANDLE_IOCTL(FDGETDRVPRM32, fd_ioctl_trans)
HANDLE_IOCTL(FDGETDRVSTAT32, fd_ioctl_trans)
HANDLE_IOCTL(FDPOLLDRVSTAT32, fd_ioctl_trans)
HANDLE_IOCTL(FDGETFDCSTAT32, fd_ioctl_trans)
HANDLE_IOCTL(FDWERRORGET32, fd_ioctl_trans)
HANDLE_IOCTL(SG_IO,sg_ioctl_trans)
HANDLE_IOCTL(PPPIOCGIDLE32, ppp_ioctl_trans)
HANDLE_IOCTL(PPPIOCSCOMPRESS32, ppp_ioctl_trans)
HANDLE_IOCTL(PPPIOCSPASS32, ppp_sock_fprog_ioctl_trans)
HANDLE_IOCTL(PPPIOCSACTIVE32, ppp_sock_fprog_ioctl_trans)
HANDLE_IOCTL(MTIOCGET32, mt_ioctl_trans)
HANDLE_IOCTL(MTIOCPOS32, mt_ioctl_trans)
HANDLE_IOCTL(CDROMREADAUDIO, cdrom_ioctl_trans)
HANDLE_IOCTL(CDROM_SEND_PACKET, cdrom_ioctl_trans)
HANDLE_IOCTL(LOOP_SET_STATUS, loop_status)
HANDLE_IOCTL(LOOP_GET_STATUS, loop_status)
#define AUTOFS_IOC_SETTIMEOUT32 _IOWR(0x93,0x64,unsigned int)
HANDLE_IOCTL(AUTOFS_IOC_SETTIMEOUT32, ioc_settimeout)
#ifdef CONFIG_VT
HANDLE_IOCTL(PIO_FONTX, do_fontx_ioctl)
HANDLE_IOCTL(GIO_FONTX, do_fontx_ioctl)
HANDLE_IOCTL(PIO_UNIMAP, do_unimap_ioctl)
HANDLE_IOCTL(GIO_UNIMAP, do_unimap_ioctl)
HANDLE_IOCTL(KDFONTOP, do_kdfontop_ioctl)
#endif
HANDLE_IOCTL(EXT2_IOC32_GETFLAGS, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_SETFLAGS, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_GETVERSION, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_SETVERSION, do_ext2_ioctl)
HANDLE_IOCTL(VIDIOCGTUNER32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCSTUNER32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCGWIN32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCSWIN32, do_set_window)
HANDLE_IOCTL(VIDIOCGFBUF32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCSFBUF32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCGFREQ32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCSFREQ32, do_video_ioctl)
/* One SMB ioctl needs translations. */
#define SMB_IOC_GETMOUNTUID_32 _IOR('u', 1, compat_uid_t)
HANDLE_IOCTL(SMB_IOC_GETMOUNTUID_32, do_smb_getmountuid)
HANDLE_IOCTL(ATM_GETLINKRATE32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETNAMES32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETTYPE32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETESI32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETADDR32, do_atm_ioctl)
HANDLE_IOCTL(ATM_RSTADDR32, do_atm_ioctl)
HANDLE_IOCTL(ATM_ADDADDR32, do_atm_ioctl)
HANDLE_IOCTL(ATM_DELADDR32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETCIRANGE32, do_atm_ioctl)
HANDLE_IOCTL(ATM_SETCIRANGE32, do_atm_ioctl)
HANDLE_IOCTL(ATM_SETESI32, do_atm_ioctl)
HANDLE_IOCTL(ATM_SETESIF32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETSTAT32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETSTATZ32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETLOOP32, do_atm_ioctl)
HANDLE_IOCTL(ATM_SETLOOP32, do_atm_ioctl)
HANDLE_IOCTL(ATM_QUERYLOOP32, do_atm_ioctl)
HANDLE_IOCTL(SONET_GETSTAT, do_atm_ioctl)
HANDLE_IOCTL(SONET_GETSTATZ, do_atm_ioctl)
HANDLE_IOCTL(SONET_GETDIAG, do_atm_ioctl)
HANDLE_IOCTL(SONET_SETDIAG, do_atm_ioctl)
HANDLE_IOCTL(SONET_CLRDIAG, do_atm_ioctl)
HANDLE_IOCTL(SONET_SETFRAMING, do_atm_ioctl)
HANDLE_IOCTL(SONET_GETFRAMING, do_atm_ioctl)
HANDLE_IOCTL(SONET_GETFRSENSE, do_atm_ioctl)
/* block stuff */
HANDLE_IOCTL(BLKBSZGET_32, do_blkbszget)
HANDLE_IOCTL(BLKBSZSET_32, do_blkbszset)
HANDLE_IOCTL(BLKGETSIZE64_32, do_blkgetsize64)
/* vfat */
HANDLE_IOCTL(VFAT_IOCTL_READDIR_BOTH32, vfat_ioctl32)
HANDLE_IOCTL(VFAT_IOCTL_READDIR_SHORT32, vfat_ioctl32)
HANDLE_IOCTL(REISERFS_IOC_UNPACK32, reiserfs_ioctl32)
/* Raw devices */
HANDLE_IOCTL(RAW_SETBIND, raw_ioctl)
HANDLE_IOCTL(RAW_GETBIND, raw_ioctl)
/* Serial */
HANDLE_IOCTL(TIOCGSERIAL, serial_struct_ioctl)
HANDLE_IOCTL(TIOCSSERIAL, serial_struct_ioctl)
#ifdef TIOCGLTC
COMPATIBLE_IOCTL(TIOCGLTC)
COMPATIBLE_IOCTL(TIOCSLTC)
#endif
/* Usbdevfs */
HANDLE_IOCTL(USBDEVFS_CONTROL32, do_usbdevfs_control)
HANDLE_IOCTL(USBDEVFS_BULK32, do_usbdevfs_bulk)
HANDLE_IOCTL(USBDEVFS_DISCSIGNAL32, do_usbdevfs_discsignal)
COMPATIBLE_IOCTL(USBDEVFS_IOCTL32)
/* i2c */
HANDLE_IOCTL(I2C_FUNCS, w_long)
HANDLE_IOCTL(I2C_RDWR, do_i2c_rdwr_ioctl)
HANDLE_IOCTL(I2C_SMBUS, do_i2c_smbus_ioctl)
/* wireless */
HANDLE_IOCTL(SIOCGIWRANGE, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWSPY, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWSPY, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWTHRSPY, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWTHRSPY, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWAPLIST, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWSCAN, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWESSID, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWESSID, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWNICKN, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWNICKN, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWENCODE, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWENCODE, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIFBR, old_bridge_ioctl)
HANDLE_IOCTL(SIOCGIFBR, old_bridge_ioctl)

#if defined(CONFIG_NCP_FS) || defined(CONFIG_NCP_FS_MODULE)
HANDLE_IOCTL(NCP_IOC_NCPREQUEST_32, do_ncp_ncprequest)
HANDLE_IOCTL(NCP_IOC_GETMOUNTUID2_32, do_ncp_getmountuid2)
HANDLE_IOCTL(NCP_IOC_GET_FS_INFO_V2_32, do_ncp_getfsinfo2)
HANDLE_IOCTL(NCP_IOC_GETOBJECTNAME_32, do_ncp_getobjectname)
HANDLE_IOCTL(NCP_IOC_SETOBJECTNAME_32, do_ncp_setobjectname)
HANDLE_IOCTL(NCP_IOC_GETPRIVATEDATA_32, do_ncp_getprivatedata)
HANDLE_IOCTL(NCP_IOC_SETPRIVATEDATA_32, do_ncp_setprivatedata)
#endif

#undef DECLARES
#endif

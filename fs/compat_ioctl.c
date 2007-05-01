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

#include <linux/types.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/capability.h>
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
#include <linux/auto_fs.h>
#include <linux/auto_fs4.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/fb.h>
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
#include <linux/if_tun.h>
#include <linux/ctype.h>
#include <linux/ioctl32.h>
#include <linux/syscalls.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/wireless.h>
#include <linux/atalk.h>
#include <linux/blktrace_api.h>

#include <net/sock.h>          /* siocdevprivate_ioctl */
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <net/bluetooth/rfcomm.h>

#include <linux/capi.h>
#include <linux/gigaset_dev.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>

#include <asm/uaccess.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_bonding.h>
#include <linux/watchdog.h>
#include <linux/dm-ioctl.h>

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
#include <linux/pktcdvd.h>

#include <linux/hiddev.h>

#include <linux/dvb/audio.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/video.h>
#include <linux/lp.h>

static int do_ioctl32_pointer(unsigned int fd, unsigned int cmd,
			      unsigned long arg, struct file *f)
{
	return sys_ioctl(fd, cmd, (unsigned long)compat_ptr(arg));
}

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

struct compat_video_event {
	int32_t		type;
	compat_time_t	timestamp;
	union {
	        video_size_t size;
		unsigned int frame_rate;
	} u;
};

static int do_video_get_event(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct video_event kevent;
	mm_segment_t old_fs = get_fs();
	int err;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long) &kevent);
	set_fs(old_fs);

	if (!err) {
		struct compat_video_event __user *up = compat_ptr(arg);

		err  = put_user(kevent.type, &up->type);
		err |= put_user(kevent.timestamp, &up->timestamp);
		err |= put_user(kevent.u.size.w, &up->u.size.w);
		err |= put_user(kevent.u.size.h, &up->u.size.h);
		err |= put_user(kevent.u.size.aspect_ratio,
				&up->u.size.aspect_ratio);
		if (err)
			err = -EFAULT;
	}

	return err;
}

struct compat_video_still_picture {
        compat_uptr_t iFrame;
        int32_t size;
};

static int do_video_stillpicture(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct compat_video_still_picture __user *up;
	struct video_still_picture __user *up_native;
	compat_uptr_t fp;
	int32_t size;
	int err;

	up = (struct compat_video_still_picture __user *) arg;
	err  = get_user(fp, &up->iFrame);
	err |= get_user(size, &up->size);
	if (err)
		return -EFAULT;

	up_native =
		compat_alloc_user_space(sizeof(struct video_still_picture));

	err =  put_user(compat_ptr(fp), &up_native->iFrame);
	err |= put_user(size, &up_native->size);
	if (err)
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long) up_native);

	return err;
}

struct compat_video_spu_palette {
	int length;
	compat_uptr_t palette;
};

static int do_video_set_spu_palette(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct compat_video_spu_palette __user *up;
	struct video_spu_palette __user *up_native;
	compat_uptr_t palp;
	int length, err;

	up = (struct compat_video_spu_palette __user *) arg;
	err  = get_user(palp, &up->palette);
	err |= get_user(length, &up->length);

	up_native = compat_alloc_user_space(sizeof(struct video_spu_palette));
	err  = put_user(compat_ptr(palp), &up_native->palette);
	err |= put_user(length, &up_native->length);
	if (err)
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long) up_native);

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

static int do_siocgstampns(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct compat_timespec __user *up = compat_ptr(arg);
	struct timespec kts;
	mm_segment_t old_fs = get_fs();
	int err;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&kts);
	set_fs(old_fs);
	if (!err) {
		err = put_user(kts.tv_sec, &up->tv_sec);
		err |= __put_user(kts.tv_nsec, &up->tv_nsec);
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
	for (i = 0, j = 0;
             i + sizeof (struct ifreq32) <= ifc32.ifc_len && j < ifc.ifc_len;
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
		ifc32.ifc_len = i;
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

#ifdef CONFIG_BLOCK
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

struct compat_sg_req_info { /* used by SG_GET_REQUEST_TABLE ioctl() */
	char req_state;
	char orphan;
	char sg_io_owned;
	char problem;
	int pack_id;
	compat_uptr_t usr_ptr;
	unsigned int duration;
	int unused;
};

static int sg_grt_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	int err, i;
	sg_req_info_t __user *r;
	struct compat_sg_req_info __user *o = (void __user *)arg;
	r = compat_alloc_user_space(sizeof(sg_req_info_t)*SG_MAX_QUEUE);
	err = sys_ioctl(fd,cmd,(unsigned long)r);
	if (err < 0)
		return err;
	for (i = 0; i < SG_MAX_QUEUE; i++) {
		void __user *ptr;
		int d;

		if (copy_in_user(o + i, r + i, offsetof(sg_req_info_t, usr_ptr)) ||
		    get_user(ptr, &r[i].usr_ptr) ||
		    get_user(d, &r[i].duration) ||
		    put_user((u32)(unsigned long)(ptr), &o[i].usr_ptr) ||
		    put_user(d, &o[i].duration))
			return -EFAULT;
	}
	return err;
}
#endif /* CONFIG_BLOCK */

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


#ifdef CONFIG_BLOCK
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
#endif /* CONFIG_BLOCK */

#ifdef CONFIG_VT

static int vt_check(struct file *file)
{
	struct tty_struct *tty;
	struct inode *inode = file->f_path.dentry->d_inode;
	
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

#define NR_ATM_IOCTL ARRAY_SIZE(atm_ioctl_map)

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

#ifdef CONFIG_BLOCK
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
#endif

static int ioc_settimeout(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return rw_long(fd, AUTOFS_IOC_SETTIMEOUT, arg);
}

#ifdef CONFIG_BLOCK
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
#endif

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

#ifdef CONFIG_BLOCK
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

#define NR_FD_IOCTL_TRANS ARRAY_SIZE(fd_ioctl_trans_table)

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

out:
	kfree(karg);
	return err;
}
#endif

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

#ifdef CONFIG_BLOCK
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
#endif /* CONFIG_BLOCK */

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
	unsigned int base;

        if (cmd == TIOCSSERIAL) {
                if (!access_ok(VERIFY_READ, ss32, sizeof(SS32)))
                        return -EFAULT;
                if (__copy_from_user(&ss, ss32, offsetof(SS32, iomem_base)))
			return -EFAULT;
                if (__get_user(udata, &ss32->iomem_base))
			return -EFAULT;
                ss.iomem_base = compat_ptr(udata);
                if (__get_user(ss.iomem_reg_shift, &ss32->iomem_reg_shift) ||
		    __get_user(ss.port_high, &ss32->port_high))
			return -EFAULT;
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
		base = (unsigned long)ss.iomem_base  >> 32 ?
			0xffffffff : (unsigned)(unsigned long)ss.iomem_base;
		if (__put_user(base, &ss32->iomem_base) ||
		    __put_user(ss.iomem_reg_shift, &ss32->iomem_reg_shift) ||
		    __put_user(ss.port_high, &ss32->port_high))
			return -EFAULT;
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

#define RTC_IRQP_READ32		_IOR('p', 0x0b, compat_ulong_t)
#define RTC_IRQP_SET32		_IOW('p', 0x0c, compat_ulong_t)
#define RTC_EPOCH_READ32	_IOR('p', 0x0d, compat_ulong_t)
#define RTC_EPOCH_SET32		_IOW('p', 0x0e, compat_ulong_t)

static int rtc_ioctl(unsigned fd, unsigned cmd, unsigned long arg)
{
	mm_segment_t oldfs = get_fs();
	compat_ulong_t val32;
	unsigned long kval;
	int ret;

	switch (cmd) {
	case RTC_IRQP_READ32:
	case RTC_EPOCH_READ32:
		set_fs(KERNEL_DS);
		ret = sys_ioctl(fd, (cmd == RTC_IRQP_READ32) ?
					RTC_IRQP_READ : RTC_EPOCH_READ,
					(unsigned long)&kval);
		set_fs(oldfs);
		if (ret)
			return ret;
		val32 = kval;
		return put_user(val32, (unsigned int __user *)arg);
	case RTC_IRQP_SET32:
		return sys_ioctl(fd, RTC_IRQP_SET, arg); 
	case RTC_EPOCH_SET32:
		return sys_ioctl(fd, RTC_EPOCH_SET, arg);
	default:
		/* unreached */
		return -ENOIOCTLCMD;
	}
}

static int
lp_timeout_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct compat_timeval __user *tc = (struct compat_timeval __user *)arg;
	struct timeval __user *tn = compat_alloc_user_space(sizeof(struct timeval));
	struct timeval ts;
	if (get_user(ts.tv_sec, &tc->tv_sec) ||
	    get_user(ts.tv_usec, &tc->tv_usec) ||
	    put_user(ts.tv_sec, &tn->tv_sec) ||
	    put_user(ts.tv_usec, &tn->tv_usec))
		return -EFAULT;
	return sys_ioctl(fd, cmd, (unsigned long)tn);
}

#define HANDLE_IOCTL(cmd,handler) \
	{ (cmd), (ioctl_trans_handler_t)(handler) },

/* pointer to compatible structure or no argument */
#define COMPATIBLE_IOCTL(cmd) \
	{ (cmd), do_ioctl32_pointer },

/* argument is an unsigned long integer, not a pointer */
#define ULONG_IOCTL(cmd) \
	{ (cmd), (ioctl_trans_handler_t)sys_ioctl },


struct ioctl_trans ioctl_start[] = {
#include <linux/compat_ioctl.h>
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
HANDLE_IOCTL(SIOCSIFHWBROADCAST, dev_ifsioc)

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
HANDLE_IOCTL(SIOCGSTAMPNS, do_siocgstampns)
#endif
#ifdef CONFIG_BLOCK
HANDLE_IOCTL(HDIO_GETGEO, hdio_getgeo)
HANDLE_IOCTL(BLKRAGET, w_long)
HANDLE_IOCTL(BLKGETSIZE, w_long)
HANDLE_IOCTL(0x1260, broken_blkgetsize)
HANDLE_IOCTL(BLKFRAGET, w_long)
HANDLE_IOCTL(BLKSECTGET, w_long)
HANDLE_IOCTL(BLKPG, blkpg_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_UNMASKINTR, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_MULTCOUNT, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_KEEPSETTINGS, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_32BIT, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_NOWERR, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_DMA, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_NICE, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_WCACHE, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_ACOUSTIC, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_ADDRESS, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_BUSSTATE, hdio_ioctl_trans)
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
HANDLE_IOCTL(SG_GET_REQUEST_TABLE, sg_grt_trans)
#endif
HANDLE_IOCTL(PPPIOCGIDLE32, ppp_ioctl_trans)
HANDLE_IOCTL(PPPIOCSCOMPRESS32, ppp_ioctl_trans)
HANDLE_IOCTL(PPPIOCSPASS32, ppp_sock_fprog_ioctl_trans)
HANDLE_IOCTL(PPPIOCSACTIVE32, ppp_sock_fprog_ioctl_trans)
#ifdef CONFIG_BLOCK
HANDLE_IOCTL(MTIOCGET32, mt_ioctl_trans)
HANDLE_IOCTL(MTIOCPOS32, mt_ioctl_trans)
HANDLE_IOCTL(CDROMREADAUDIO, cdrom_ioctl_trans)
HANDLE_IOCTL(CDROM_SEND_PACKET, cdrom_ioctl_trans)
#endif
#define AUTOFS_IOC_SETTIMEOUT32 _IOWR(0x93,0x64,unsigned int)
HANDLE_IOCTL(AUTOFS_IOC_SETTIMEOUT32, ioc_settimeout)
#ifdef CONFIG_VT
HANDLE_IOCTL(PIO_FONTX, do_fontx_ioctl)
HANDLE_IOCTL(GIO_FONTX, do_fontx_ioctl)
HANDLE_IOCTL(PIO_UNIMAP, do_unimap_ioctl)
HANDLE_IOCTL(GIO_UNIMAP, do_unimap_ioctl)
HANDLE_IOCTL(KDFONTOP, do_kdfontop_ioctl)
#endif
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
#ifdef CONFIG_BLOCK
HANDLE_IOCTL(BLKBSZGET_32, do_blkbszget)
HANDLE_IOCTL(BLKBSZSET_32, do_blkbszset)
HANDLE_IOCTL(BLKGETSIZE64_32, do_blkgetsize64)
/* Raw devices */
HANDLE_IOCTL(RAW_SETBIND, raw_ioctl)
HANDLE_IOCTL(RAW_GETBIND, raw_ioctl)
#endif
/* Serial */
HANDLE_IOCTL(TIOCGSERIAL, serial_struct_ioctl)
HANDLE_IOCTL(TIOCSSERIAL, serial_struct_ioctl)
#ifdef TIOCGLTC
COMPATIBLE_IOCTL(TIOCGLTC)
COMPATIBLE_IOCTL(TIOCSLTC)
#endif
#ifdef TIOCSTART
/*
 * For these two we have defintions in ioctls.h and/or termios.h on
 * some architectures but no actual implemention.  Some applications
 * like bash call them if they are defined in the headers, so we provide
 * entries here to avoid syslog message spew.
 */
COMPATIBLE_IOCTL(TIOCSTART)
COMPATIBLE_IOCTL(TIOCSTOP)
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
HANDLE_IOCTL(SIOCGIWPRIV, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWSTATS, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWSPY, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWSPY, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWTHRSPY, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWTHRSPY, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWMLME, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWAPLIST, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWSCAN, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWSCAN, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWESSID, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWESSID, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWNICKN, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWNICKN, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWENCODE, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWENCODE, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWGENIE, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWGENIE, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWENCODEEXT, do_wireless_ioctl)
HANDLE_IOCTL(SIOCGIWENCODEEXT, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIWPMKSA, do_wireless_ioctl)
HANDLE_IOCTL(SIOCSIFBR, old_bridge_ioctl)
HANDLE_IOCTL(SIOCGIFBR, old_bridge_ioctl)
HANDLE_IOCTL(RTC_IRQP_READ32, rtc_ioctl)
HANDLE_IOCTL(RTC_IRQP_SET32, rtc_ioctl)
HANDLE_IOCTL(RTC_EPOCH_READ32, rtc_ioctl)
HANDLE_IOCTL(RTC_EPOCH_SET32, rtc_ioctl)

/* dvb */
HANDLE_IOCTL(VIDEO_GET_EVENT, do_video_get_event)
HANDLE_IOCTL(VIDEO_STILLPICTURE, do_video_stillpicture)
HANDLE_IOCTL(VIDEO_SET_SPU_PALETTE, do_video_set_spu_palette)

/* parport */
COMPATIBLE_IOCTL(LPTIME)
COMPATIBLE_IOCTL(LPCHAR)
COMPATIBLE_IOCTL(LPABORTOPEN)
COMPATIBLE_IOCTL(LPCAREFUL)
COMPATIBLE_IOCTL(LPWAIT)
COMPATIBLE_IOCTL(LPSETIRQ)
COMPATIBLE_IOCTL(LPGETSTATUS)
COMPATIBLE_IOCTL(LPGETSTATUS)
COMPATIBLE_IOCTL(LPRESET)
/*LPGETSTATS not implemented, but no kernels seem to compile it in anyways*/
COMPATIBLE_IOCTL(LPGETFLAGS)
HANDLE_IOCTL(LPSETTIMEOUT, lp_timeout_trans)
};

int ioctl_table_size = ARRAY_SIZE(ioctl_start);

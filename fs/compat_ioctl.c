/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 * Copyright (C) 2003       Pavel Machek (pavel@ucw.cz)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#include <linux/joystick.h>

#include <linux/types.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/capability.h>
#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/ioctl.h>
#include <linux/if.h>
#include <linux/if_bridge.h>
#include <linux/raid/md_u.h>
#include <linux/kd.h>
#include <linux/route.h>
#include <linux/in6.h>
#include <linux/ipv6_route.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/vt.h>
#include <linux/falloc.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/ppp_defs.h>
#include <linux/ppp-ioctl.h>
#include <linux/if_pppox.h>
#include <linux/mtio.h>
#include <linux/auto_fs.h>
#include <linux/auto_fs4.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <linux/netdevice.h>
#include <linux/raw.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/rtc.h>
#include <linux/pci.h>
#include <linux/serial.h>
#include <linux/if_tun.h>
#include <linux/ctype.h>
#include <linux/syscalls.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/atalk.h>
#include <linux/gfp.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <net/bluetooth/rfcomm.h>

#include <linux/capi.h>
#include <linux/gigaset_dev.h>

#ifdef CONFIG_BLOCK
#include <linux/loop.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>
#endif

#include <asm/uaccess.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_bonding.h>
#include <linux/watchdog.h>

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

#include <linux/usb.h>
#include <linux/usbdevice_fs.h>
#include <linux/nbd.h>
#include <linux/random.h>
#include <linux/filter.h>

#include <linux/hiddev.h>

#define __DVB_CORE__
#include <linux/dvb/audio.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/video.h>

#include <linux/sort.h>

#ifdef CONFIG_SPARC
#include <asm/fbio.h>
#endif

static int w_long(unsigned int fd, unsigned int cmd,
		compat_ulong_t __user *argp)
{
	mm_segment_t old_fs = get_fs();
	int err;
	unsigned long val;

	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&val);
	set_fs (old_fs);
	if (!err && put_user(val, argp))
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

static int do_video_get_event(unsigned int fd, unsigned int cmd,
		struct compat_video_event __user *up)
{
	struct video_event kevent;
	mm_segment_t old_fs = get_fs();
	int err;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long) &kevent);
	set_fs(old_fs);

	if (!err) {
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

static int do_video_stillpicture(unsigned int fd, unsigned int cmd,
	struct compat_video_still_picture __user *up)
{
	struct video_still_picture __user *up_native;
	compat_uptr_t fp;
	int32_t size;
	int err;

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

static int do_video_set_spu_palette(unsigned int fd, unsigned int cmd,
		struct compat_video_spu_palette __user *up)
{
	struct video_spu_palette __user *up_native;
	compat_uptr_t palp;
	int length, err;

	err  = get_user(palp, &up->palette);
	err |= get_user(length, &up->length);
	if (err)
		return -EFAULT;

	up_native = compat_alloc_user_space(sizeof(struct video_spu_palette));
	err  = put_user(compat_ptr(palp), &up_native->palette);
	err |= put_user(length, &up_native->length);
	if (err)
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long) up_native);

	return err;
}

#ifdef CONFIG_BLOCK
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

static int sg_ioctl_trans(unsigned int fd, unsigned int cmd,
			sg_io_hdr32_t __user *sgio32)
{
	sg_io_hdr_t __user *sgio;
	u16 iovec_count;
	u32 data;
	void __user *dxferp;
	int err;
	int interface_id;

	if (get_user(interface_id, &sgio32->interface_id))
		return -EFAULT;
	if (interface_id != 'S')
		return sys_ioctl(fd, cmd, (unsigned long)sgio32);

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

static int sg_grt_trans(unsigned int fd, unsigned int cmd, struct
			compat_sg_req_info __user *o)
{
	int err, i;
	sg_req_info_t __user *r;
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

static int ppp_sock_fprog_ioctl_trans(unsigned int fd, unsigned int cmd,
			struct sock_fprog32 __user *u_fprog32)
{
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

static int ppp_gidle(unsigned int fd, unsigned int cmd,
		struct ppp_idle32 __user *idle32)
{
	struct ppp_idle __user *idle;
	__kernel_time_t xmit, recv;
	int err;

	idle = compat_alloc_user_space(sizeof(*idle));

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

static int ppp_scompress(unsigned int fd, unsigned int cmd,
	struct ppp_option_data32 __user *odata32)
{
	struct ppp_option_data __user *odata;
	__u32 data;
	void __user *datap;

	odata = compat_alloc_user_space(sizeof(*odata));

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

static int mt_ioctl_trans(unsigned int fd, unsigned int cmd, void __user *argp)
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
	default:	/* MTIOCGET32 */
		kcmd = MTIOCGET;
		karg = &get;
		break;
	}
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, kcmd, (unsigned long)karg);
	set_fs (old_fs);
	if (err)
		return err;
	switch (cmd) {
	case MTIOCPOS32:
		upos32 = argp;
		err = __put_user(pos.mt_blkno, &upos32->mt_blkno);
		break;
	case MTIOCGET32:
		umget32 = argp;
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

#endif /* CONFIG_BLOCK */

/* Bluetooth ioctls */
#define HCIUARTSETPROTO		_IOW('U', 200, int)
#define HCIUARTGETPROTO		_IOR('U', 201, int)
#define HCIUARTGETDEVICE	_IOR('U', 202, int)
#define HCIUARTSETFLAGS		_IOW('U', 203, int)
#define HCIUARTGETFLAGS		_IOR('U', 204, int)

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

static int serial_struct_ioctl(unsigned fd, unsigned cmd,
			struct serial_struct32 __user *ss32)
{
        typedef struct serial_struct SS;
        typedef struct serial_struct32 SS32;
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

static int do_i2c_rdwr_ioctl(unsigned int fd, unsigned int cmd,
			struct i2c_rdwr_ioctl_data32    __user *udata)
{
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

static int do_i2c_smbus_ioctl(unsigned int fd, unsigned int cmd,
			struct i2c_smbus_ioctl_data32   __user *udata)
{
	struct i2c_smbus_ioctl_data	__user *tdata;
	compat_caddr_t			datap;

	tdata = compat_alloc_user_space(sizeof(*tdata));
	if (tdata == NULL)
		return -ENOMEM;
	if (!access_ok(VERIFY_WRITE, tdata, sizeof(*tdata)))
		return -EFAULT;

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

#define RTC_IRQP_READ32		_IOR('p', 0x0b, compat_ulong_t)
#define RTC_IRQP_SET32		_IOW('p', 0x0c, compat_ulong_t)
#define RTC_EPOCH_READ32	_IOR('p', 0x0d, compat_ulong_t)
#define RTC_EPOCH_SET32		_IOW('p', 0x0e, compat_ulong_t)

static int rtc_ioctl(unsigned fd, unsigned cmd, void __user *argp)
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
		return put_user(val32, (unsigned int __user *)argp);
	case RTC_IRQP_SET32:
		return sys_ioctl(fd, RTC_IRQP_SET, (unsigned long)argp);
	case RTC_EPOCH_SET32:
		return sys_ioctl(fd, RTC_EPOCH_SET, (unsigned long)argp);
	}

	return -ENOIOCTLCMD;
}

/* on ia32 l_start is on a 32-bit boundary */
#if defined(CONFIG_IA64) || defined(CONFIG_X86_64)
struct space_resv_32 {
	__s16		l_type;
	__s16		l_whence;
	__s64		l_start	__attribute__((packed));
			/* len == 0 means until end of file */
	__s64		l_len __attribute__((packed));
	__s32		l_sysid;
	__u32		l_pid;
	__s32		l_pad[4];	/* reserve area */
};

#define FS_IOC_RESVSP_32		_IOW ('X', 40, struct space_resv_32)
#define FS_IOC_RESVSP64_32	_IOW ('X', 42, struct space_resv_32)

/* just account for different alignment */
static int compat_ioctl_preallocate(struct file *file,
			struct space_resv_32    __user *p32)
{
	struct space_resv	__user *p = compat_alloc_user_space(sizeof(*p));

	if (copy_in_user(&p->l_type,	&p32->l_type,	sizeof(s16)) ||
	    copy_in_user(&p->l_whence,	&p32->l_whence, sizeof(s16)) ||
	    copy_in_user(&p->l_start,	&p32->l_start,	sizeof(s64)) ||
	    copy_in_user(&p->l_len,	&p32->l_len,	sizeof(s64)) ||
	    copy_in_user(&p->l_sysid,	&p32->l_sysid,	sizeof(s32)) ||
	    copy_in_user(&p->l_pid,	&p32->l_pid,	sizeof(u32)) ||
	    copy_in_user(&p->l_pad,	&p32->l_pad,	4*sizeof(u32)))
		return -EFAULT;

	return ioctl_preallocate(file, p);
}
#endif

/*
 * simple reversible transform to make our table more evenly
 * distributed after sorting.
 */
#define XFORM(i) (((i) ^ ((i) << 27) ^ ((i) << 17)) & 0xffffffff)

#define COMPATIBLE_IOCTL(cmd) XFORM(cmd),
/* ioctl should not be warned about even if it's not implemented.
   Valid reasons to use this:
   - It is implemented with ->compat_ioctl on some device, but programs
   call it on others too.
   - The ioctl is not implemented in the native kernel, but programs
   call it commonly anyways.
   Most other reasons are not valid. */
#define IGNORE_IOCTL(cmd) COMPATIBLE_IOCTL(cmd)

static unsigned int ioctl_pointer[] = {
/* compatible ioctls first */
COMPATIBLE_IOCTL(0x4B50)   /* KDGHWCLK - not in the kernel, but don't complain */
COMPATIBLE_IOCTL(0x4B51)   /* KDSHWCLK - not in the kernel, but don't complain */

/* Big T */
COMPATIBLE_IOCTL(TCGETA)
COMPATIBLE_IOCTL(TCSETA)
COMPATIBLE_IOCTL(TCSETAW)
COMPATIBLE_IOCTL(TCSETAF)
COMPATIBLE_IOCTL(TCSBRK)
COMPATIBLE_IOCTL(TCXONC)
COMPATIBLE_IOCTL(TCFLSH)
COMPATIBLE_IOCTL(TCGETS)
COMPATIBLE_IOCTL(TCSETS)
COMPATIBLE_IOCTL(TCSETSW)
COMPATIBLE_IOCTL(TCSETSF)
COMPATIBLE_IOCTL(TIOCLINUX)
COMPATIBLE_IOCTL(TIOCSBRK)
COMPATIBLE_IOCTL(TIOCGDEV)
COMPATIBLE_IOCTL(TIOCCBRK)
COMPATIBLE_IOCTL(TIOCGSID)
COMPATIBLE_IOCTL(TIOCGICOUNT)
COMPATIBLE_IOCTL(TIOCGPKT)
COMPATIBLE_IOCTL(TIOCGPTLCK)
COMPATIBLE_IOCTL(TIOCGEXCL)
/* Little t */
COMPATIBLE_IOCTL(TIOCGETD)
COMPATIBLE_IOCTL(TIOCSETD)
COMPATIBLE_IOCTL(TIOCEXCL)
COMPATIBLE_IOCTL(TIOCNXCL)
COMPATIBLE_IOCTL(TIOCCONS)
COMPATIBLE_IOCTL(TIOCGSOFTCAR)
COMPATIBLE_IOCTL(TIOCSSOFTCAR)
COMPATIBLE_IOCTL(TIOCSWINSZ)
COMPATIBLE_IOCTL(TIOCGWINSZ)
COMPATIBLE_IOCTL(TIOCMGET)
COMPATIBLE_IOCTL(TIOCMBIC)
COMPATIBLE_IOCTL(TIOCMBIS)
COMPATIBLE_IOCTL(TIOCMSET)
COMPATIBLE_IOCTL(TIOCPKT)
COMPATIBLE_IOCTL(TIOCNOTTY)
COMPATIBLE_IOCTL(TIOCSTI)
COMPATIBLE_IOCTL(TIOCOUTQ)
COMPATIBLE_IOCTL(TIOCSPGRP)
COMPATIBLE_IOCTL(TIOCGPGRP)
COMPATIBLE_IOCTL(TIOCGPTN)
COMPATIBLE_IOCTL(TIOCSPTLCK)
COMPATIBLE_IOCTL(TIOCSERGETLSR)
COMPATIBLE_IOCTL(TIOCSIG)
#ifdef TIOCSRS485
COMPATIBLE_IOCTL(TIOCSRS485)
#endif
#ifdef TIOCGRS485
COMPATIBLE_IOCTL(TIOCGRS485)
#endif
#ifdef TCGETS2
COMPATIBLE_IOCTL(TCGETS2)
COMPATIBLE_IOCTL(TCSETS2)
COMPATIBLE_IOCTL(TCSETSW2)
COMPATIBLE_IOCTL(TCSETSF2)
#endif
/* Little f */
COMPATIBLE_IOCTL(FIOCLEX)
COMPATIBLE_IOCTL(FIONCLEX)
COMPATIBLE_IOCTL(FIOASYNC)
COMPATIBLE_IOCTL(FIONBIO)
COMPATIBLE_IOCTL(FIONREAD)  /* This is also TIOCINQ */
COMPATIBLE_IOCTL(FS_IOC_FIEMAP)
/* 0x00 */
COMPATIBLE_IOCTL(FIBMAP)
COMPATIBLE_IOCTL(FIGETBSZ)
/* 'X' - originally XFS but some now in the VFS */
COMPATIBLE_IOCTL(FIFREEZE)
COMPATIBLE_IOCTL(FITHAW)
COMPATIBLE_IOCTL(KDGETKEYCODE)
COMPATIBLE_IOCTL(KDSETKEYCODE)
COMPATIBLE_IOCTL(KDGKBTYPE)
COMPATIBLE_IOCTL(KDGETMODE)
COMPATIBLE_IOCTL(KDGKBMODE)
COMPATIBLE_IOCTL(KDGKBMETA)
COMPATIBLE_IOCTL(KDGKBENT)
COMPATIBLE_IOCTL(KDSKBENT)
COMPATIBLE_IOCTL(KDGKBSENT)
COMPATIBLE_IOCTL(KDSKBSENT)
COMPATIBLE_IOCTL(KDGKBDIACR)
COMPATIBLE_IOCTL(KDSKBDIACR)
COMPATIBLE_IOCTL(KDGKBDIACRUC)
COMPATIBLE_IOCTL(KDSKBDIACRUC)
COMPATIBLE_IOCTL(KDKBDREP)
COMPATIBLE_IOCTL(KDGKBLED)
COMPATIBLE_IOCTL(KDGETLED)
#ifdef CONFIG_BLOCK
/* Big S */
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_IDLUN)
COMPATIBLE_IOCTL(SCSI_IOCTL_DOORLOCK)
COMPATIBLE_IOCTL(SCSI_IOCTL_DOORUNLOCK)
COMPATIBLE_IOCTL(SCSI_IOCTL_TEST_UNIT_READY)
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_BUS_NUMBER)
COMPATIBLE_IOCTL(SCSI_IOCTL_SEND_COMMAND)
COMPATIBLE_IOCTL(SCSI_IOCTL_PROBE_HOST)
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_PCI)
#endif
/* Big V (don't complain on serial console) */
IGNORE_IOCTL(VT_OPENQRY)
IGNORE_IOCTL(VT_GETMODE)
/* Little p (/dev/rtc, /dev/envctrl, etc.) */
COMPATIBLE_IOCTL(RTC_AIE_ON)
COMPATIBLE_IOCTL(RTC_AIE_OFF)
COMPATIBLE_IOCTL(RTC_UIE_ON)
COMPATIBLE_IOCTL(RTC_UIE_OFF)
COMPATIBLE_IOCTL(RTC_PIE_ON)
COMPATIBLE_IOCTL(RTC_PIE_OFF)
COMPATIBLE_IOCTL(RTC_WIE_ON)
COMPATIBLE_IOCTL(RTC_WIE_OFF)
COMPATIBLE_IOCTL(RTC_ALM_SET)
COMPATIBLE_IOCTL(RTC_ALM_READ)
COMPATIBLE_IOCTL(RTC_RD_TIME)
COMPATIBLE_IOCTL(RTC_SET_TIME)
COMPATIBLE_IOCTL(RTC_WKALM_SET)
COMPATIBLE_IOCTL(RTC_WKALM_RD)
/*
 * These two are only for the sbus rtc driver, but
 * hwclock tries them on every rtc device first when
 * running on sparc.  On other architectures the entries
 * are useless but harmless.
 */
COMPATIBLE_IOCTL(_IOR('p', 20, int[7])) /* RTCGET */
COMPATIBLE_IOCTL(_IOW('p', 21, int[7])) /* RTCSET */
/* Little m */
COMPATIBLE_IOCTL(MTIOCTOP)
/* Socket level stuff */
COMPATIBLE_IOCTL(FIOQSIZE)
#ifdef CONFIG_BLOCK
/* loop */
IGNORE_IOCTL(LOOP_CLR_FD)
/* md calls this on random blockdevs */
IGNORE_IOCTL(RAID_VERSION)
/* qemu/qemu-img might call these two on plain files for probing */
IGNORE_IOCTL(CDROM_DRIVE_STATUS)
IGNORE_IOCTL(FDGETPRM32)
/* SG stuff */
COMPATIBLE_IOCTL(SG_SET_TIMEOUT)
COMPATIBLE_IOCTL(SG_GET_TIMEOUT)
COMPATIBLE_IOCTL(SG_EMULATED_HOST)
COMPATIBLE_IOCTL(SG_GET_TRANSFORM)
COMPATIBLE_IOCTL(SG_SET_RESERVED_SIZE)
COMPATIBLE_IOCTL(SG_GET_RESERVED_SIZE)
COMPATIBLE_IOCTL(SG_GET_SCSI_ID)
COMPATIBLE_IOCTL(SG_SET_FORCE_LOW_DMA)
COMPATIBLE_IOCTL(SG_GET_LOW_DMA)
COMPATIBLE_IOCTL(SG_SET_FORCE_PACK_ID)
COMPATIBLE_IOCTL(SG_GET_PACK_ID)
COMPATIBLE_IOCTL(SG_GET_NUM_WAITING)
COMPATIBLE_IOCTL(SG_SET_DEBUG)
COMPATIBLE_IOCTL(SG_GET_SG_TABLESIZE)
COMPATIBLE_IOCTL(SG_GET_COMMAND_Q)
COMPATIBLE_IOCTL(SG_SET_COMMAND_Q)
COMPATIBLE_IOCTL(SG_GET_VERSION_NUM)
COMPATIBLE_IOCTL(SG_NEXT_CMD_LEN)
COMPATIBLE_IOCTL(SG_SCSI_RESET)
COMPATIBLE_IOCTL(SG_GET_REQUEST_TABLE)
COMPATIBLE_IOCTL(SG_SET_KEEP_ORPHAN)
COMPATIBLE_IOCTL(SG_GET_KEEP_ORPHAN)
#endif
/* PPP stuff */
COMPATIBLE_IOCTL(PPPIOCGFLAGS)
COMPATIBLE_IOCTL(PPPIOCSFLAGS)
COMPATIBLE_IOCTL(PPPIOCGASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCGUNIT)
COMPATIBLE_IOCTL(PPPIOCGRASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSRASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCGMRU)
COMPATIBLE_IOCTL(PPPIOCSMRU)
COMPATIBLE_IOCTL(PPPIOCSMAXCID)
COMPATIBLE_IOCTL(PPPIOCGXASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSXASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCXFERUNIT)
/* PPPIOCSCOMPRESS is translated */
COMPATIBLE_IOCTL(PPPIOCGNPMODE)
COMPATIBLE_IOCTL(PPPIOCSNPMODE)
COMPATIBLE_IOCTL(PPPIOCGDEBUG)
COMPATIBLE_IOCTL(PPPIOCSDEBUG)
/* PPPIOCSPASS is translated */
/* PPPIOCSACTIVE is translated */
/* PPPIOCGIDLE is translated */
COMPATIBLE_IOCTL(PPPIOCNEWUNIT)
COMPATIBLE_IOCTL(PPPIOCATTACH)
COMPATIBLE_IOCTL(PPPIOCDETACH)
COMPATIBLE_IOCTL(PPPIOCSMRRU)
COMPATIBLE_IOCTL(PPPIOCCONNECT)
COMPATIBLE_IOCTL(PPPIOCDISCONN)
COMPATIBLE_IOCTL(PPPIOCATTCHAN)
COMPATIBLE_IOCTL(PPPIOCGCHAN)
COMPATIBLE_IOCTL(PPPIOCGL2TPSTATS)
/* PPPOX */
COMPATIBLE_IOCTL(PPPOEIOCSFWD)
COMPATIBLE_IOCTL(PPPOEIOCDFWD)
/* ppdev */
COMPATIBLE_IOCTL(PPSETMODE)
COMPATIBLE_IOCTL(PPRSTATUS)
COMPATIBLE_IOCTL(PPRCONTROL)
COMPATIBLE_IOCTL(PPWCONTROL)
COMPATIBLE_IOCTL(PPFCONTROL)
COMPATIBLE_IOCTL(PPRDATA)
COMPATIBLE_IOCTL(PPWDATA)
COMPATIBLE_IOCTL(PPCLAIM)
COMPATIBLE_IOCTL(PPRELEASE)
COMPATIBLE_IOCTL(PPYIELD)
COMPATIBLE_IOCTL(PPEXCL)
COMPATIBLE_IOCTL(PPDATADIR)
COMPATIBLE_IOCTL(PPNEGOT)
COMPATIBLE_IOCTL(PPWCTLONIRQ)
COMPATIBLE_IOCTL(PPCLRIRQ)
COMPATIBLE_IOCTL(PPSETPHASE)
COMPATIBLE_IOCTL(PPGETMODES)
COMPATIBLE_IOCTL(PPGETMODE)
COMPATIBLE_IOCTL(PPGETPHASE)
COMPATIBLE_IOCTL(PPGETFLAGS)
COMPATIBLE_IOCTL(PPSETFLAGS)
/* Big A */
/* sparc only */
/* Big Q for sound/OSS */
COMPATIBLE_IOCTL(SNDCTL_SEQ_RESET)
COMPATIBLE_IOCTL(SNDCTL_SEQ_SYNC)
COMPATIBLE_IOCTL(SNDCTL_SYNTH_INFO)
COMPATIBLE_IOCTL(SNDCTL_SEQ_CTRLRATE)
COMPATIBLE_IOCTL(SNDCTL_SEQ_GETOUTCOUNT)
COMPATIBLE_IOCTL(SNDCTL_SEQ_GETINCOUNT)
COMPATIBLE_IOCTL(SNDCTL_SEQ_PERCMODE)
COMPATIBLE_IOCTL(SNDCTL_FM_LOAD_INSTR)
COMPATIBLE_IOCTL(SNDCTL_SEQ_TESTMIDI)
COMPATIBLE_IOCTL(SNDCTL_SEQ_RESETSAMPLES)
COMPATIBLE_IOCTL(SNDCTL_SEQ_NRSYNTHS)
COMPATIBLE_IOCTL(SNDCTL_SEQ_NRMIDIS)
COMPATIBLE_IOCTL(SNDCTL_MIDI_INFO)
COMPATIBLE_IOCTL(SNDCTL_SEQ_THRESHOLD)
COMPATIBLE_IOCTL(SNDCTL_SYNTH_MEMAVL)
COMPATIBLE_IOCTL(SNDCTL_FM_4OP_ENABLE)
COMPATIBLE_IOCTL(SNDCTL_SEQ_PANIC)
COMPATIBLE_IOCTL(SNDCTL_SEQ_OUTOFBAND)
COMPATIBLE_IOCTL(SNDCTL_SEQ_GETTIME)
COMPATIBLE_IOCTL(SNDCTL_SYNTH_ID)
COMPATIBLE_IOCTL(SNDCTL_SYNTH_CONTROL)
COMPATIBLE_IOCTL(SNDCTL_SYNTH_REMOVESAMPLE)
/* Big T for sound/OSS */
COMPATIBLE_IOCTL(SNDCTL_TMR_TIMEBASE)
COMPATIBLE_IOCTL(SNDCTL_TMR_START)
COMPATIBLE_IOCTL(SNDCTL_TMR_STOP)
COMPATIBLE_IOCTL(SNDCTL_TMR_CONTINUE)
COMPATIBLE_IOCTL(SNDCTL_TMR_TEMPO)
COMPATIBLE_IOCTL(SNDCTL_TMR_SOURCE)
COMPATIBLE_IOCTL(SNDCTL_TMR_METRONOME)
COMPATIBLE_IOCTL(SNDCTL_TMR_SELECT)
/* Little m for sound/OSS */
COMPATIBLE_IOCTL(SNDCTL_MIDI_PRETIME)
COMPATIBLE_IOCTL(SNDCTL_MIDI_MPUMODE)
COMPATIBLE_IOCTL(SNDCTL_MIDI_MPUCMD)
/* Big P for sound/OSS */
COMPATIBLE_IOCTL(SNDCTL_DSP_RESET)
COMPATIBLE_IOCTL(SNDCTL_DSP_SYNC)
COMPATIBLE_IOCTL(SNDCTL_DSP_SPEED)
COMPATIBLE_IOCTL(SNDCTL_DSP_STEREO)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETBLKSIZE)
COMPATIBLE_IOCTL(SNDCTL_DSP_CHANNELS)
COMPATIBLE_IOCTL(SOUND_PCM_WRITE_FILTER)
COMPATIBLE_IOCTL(SNDCTL_DSP_POST)
COMPATIBLE_IOCTL(SNDCTL_DSP_SUBDIVIDE)
COMPATIBLE_IOCTL(SNDCTL_DSP_SETFRAGMENT)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETFMTS)
COMPATIBLE_IOCTL(SNDCTL_DSP_SETFMT)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETOSPACE)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETISPACE)
COMPATIBLE_IOCTL(SNDCTL_DSP_NONBLOCK)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETCAPS)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETTRIGGER)
COMPATIBLE_IOCTL(SNDCTL_DSP_SETTRIGGER)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETIPTR)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETOPTR)
/* SNDCTL_DSP_MAPINBUF,  XXX needs translation */
/* SNDCTL_DSP_MAPOUTBUF,  XXX needs translation */
COMPATIBLE_IOCTL(SNDCTL_DSP_SETSYNCRO)
COMPATIBLE_IOCTL(SNDCTL_DSP_SETDUPLEX)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETODELAY)
COMPATIBLE_IOCTL(SNDCTL_DSP_PROFILE)
COMPATIBLE_IOCTL(SOUND_PCM_READ_RATE)
COMPATIBLE_IOCTL(SOUND_PCM_READ_CHANNELS)
COMPATIBLE_IOCTL(SOUND_PCM_READ_BITS)
COMPATIBLE_IOCTL(SOUND_PCM_READ_FILTER)
/* Big C for sound/OSS */
COMPATIBLE_IOCTL(SNDCTL_COPR_RESET)
COMPATIBLE_IOCTL(SNDCTL_COPR_LOAD)
COMPATIBLE_IOCTL(SNDCTL_COPR_RDATA)
COMPATIBLE_IOCTL(SNDCTL_COPR_RCODE)
COMPATIBLE_IOCTL(SNDCTL_COPR_WDATA)
COMPATIBLE_IOCTL(SNDCTL_COPR_WCODE)
COMPATIBLE_IOCTL(SNDCTL_COPR_RUN)
COMPATIBLE_IOCTL(SNDCTL_COPR_HALT)
COMPATIBLE_IOCTL(SNDCTL_COPR_SENDMSG)
COMPATIBLE_IOCTL(SNDCTL_COPR_RCVMSG)
/* Big M for sound/OSS */
COMPATIBLE_IOCTL(SOUND_MIXER_READ_VOLUME)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_BASS)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_TREBLE)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_SYNTH)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_PCM)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_SPEAKER)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_LINE)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_MIC)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_CD)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_IMIX)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_ALTPCM)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_RECLEV)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_IGAIN)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_OGAIN)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_LINE1)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_LINE2)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_LINE3)
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_DIGITAL1))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_DIGITAL2))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_DIGITAL3))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_PHONEIN))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_PHONEOUT))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_VIDEO))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_RADIO))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_MONITOR))
COMPATIBLE_IOCTL(SOUND_MIXER_READ_MUTE)
/* SOUND_MIXER_READ_ENHANCE,  same value as READ_MUTE */
/* SOUND_MIXER_READ_LOUD,  same value as READ_MUTE */
COMPATIBLE_IOCTL(SOUND_MIXER_READ_RECSRC)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_DEVMASK)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_RECMASK)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_STEREODEVS)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_CAPS)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_VOLUME)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_BASS)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_TREBLE)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_SYNTH)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_PCM)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_SPEAKER)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_LINE)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_MIC)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_CD)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_IMIX)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_ALTPCM)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_RECLEV)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_IGAIN)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_OGAIN)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_LINE1)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_LINE2)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_LINE3)
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_DIGITAL1))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_DIGITAL2))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_DIGITAL3))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_PHONEIN))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_PHONEOUT))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_VIDEO))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_RADIO))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_MONITOR))
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_MUTE)
/* SOUND_MIXER_WRITE_ENHANCE,  same value as WRITE_MUTE */
/* SOUND_MIXER_WRITE_LOUD,  same value as WRITE_MUTE */
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_RECSRC)
COMPATIBLE_IOCTL(SOUND_MIXER_INFO)
COMPATIBLE_IOCTL(SOUND_OLD_MIXER_INFO)
COMPATIBLE_IOCTL(SOUND_MIXER_ACCESS)
COMPATIBLE_IOCTL(SOUND_MIXER_AGC)
COMPATIBLE_IOCTL(SOUND_MIXER_3DSE)
COMPATIBLE_IOCTL(SOUND_MIXER_PRIVATE1)
COMPATIBLE_IOCTL(SOUND_MIXER_PRIVATE2)
COMPATIBLE_IOCTL(SOUND_MIXER_PRIVATE3)
COMPATIBLE_IOCTL(SOUND_MIXER_PRIVATE4)
COMPATIBLE_IOCTL(SOUND_MIXER_PRIVATE5)
COMPATIBLE_IOCTL(SOUND_MIXER_GETLEVELS)
COMPATIBLE_IOCTL(SOUND_MIXER_SETLEVELS)
COMPATIBLE_IOCTL(OSS_GETVERSION)
/* Raw devices */
COMPATIBLE_IOCTL(RAW_SETBIND)
COMPATIBLE_IOCTL(RAW_GETBIND)
/* Watchdog */
COMPATIBLE_IOCTL(WDIOC_GETSUPPORT)
COMPATIBLE_IOCTL(WDIOC_GETSTATUS)
COMPATIBLE_IOCTL(WDIOC_GETBOOTSTATUS)
COMPATIBLE_IOCTL(WDIOC_GETTEMP)
COMPATIBLE_IOCTL(WDIOC_SETOPTIONS)
COMPATIBLE_IOCTL(WDIOC_KEEPALIVE)
COMPATIBLE_IOCTL(WDIOC_SETTIMEOUT)
COMPATIBLE_IOCTL(WDIOC_GETTIMEOUT)
/* Big R */
COMPATIBLE_IOCTL(RNDGETENTCNT)
COMPATIBLE_IOCTL(RNDADDTOENTCNT)
COMPATIBLE_IOCTL(RNDGETPOOL)
COMPATIBLE_IOCTL(RNDADDENTROPY)
COMPATIBLE_IOCTL(RNDZAPENTCNT)
COMPATIBLE_IOCTL(RNDCLEARPOOL)
/* Bluetooth */
COMPATIBLE_IOCTL(HCIDEVUP)
COMPATIBLE_IOCTL(HCIDEVDOWN)
COMPATIBLE_IOCTL(HCIDEVRESET)
COMPATIBLE_IOCTL(HCIDEVRESTAT)
COMPATIBLE_IOCTL(HCIGETDEVLIST)
COMPATIBLE_IOCTL(HCIGETDEVINFO)
COMPATIBLE_IOCTL(HCIGETCONNLIST)
COMPATIBLE_IOCTL(HCIGETCONNINFO)
COMPATIBLE_IOCTL(HCIGETAUTHINFO)
COMPATIBLE_IOCTL(HCISETRAW)
COMPATIBLE_IOCTL(HCISETSCAN)
COMPATIBLE_IOCTL(HCISETAUTH)
COMPATIBLE_IOCTL(HCISETENCRYPT)
COMPATIBLE_IOCTL(HCISETPTYPE)
COMPATIBLE_IOCTL(HCISETLINKPOL)
COMPATIBLE_IOCTL(HCISETLINKMODE)
COMPATIBLE_IOCTL(HCISETACLMTU)
COMPATIBLE_IOCTL(HCISETSCOMTU)
COMPATIBLE_IOCTL(HCIBLOCKADDR)
COMPATIBLE_IOCTL(HCIUNBLOCKADDR)
COMPATIBLE_IOCTL(HCIINQUIRY)
COMPATIBLE_IOCTL(HCIUARTSETPROTO)
COMPATIBLE_IOCTL(HCIUARTGETPROTO)
COMPATIBLE_IOCTL(RFCOMMCREATEDEV)
COMPATIBLE_IOCTL(RFCOMMRELEASEDEV)
COMPATIBLE_IOCTL(RFCOMMGETDEVLIST)
COMPATIBLE_IOCTL(RFCOMMGETDEVINFO)
COMPATIBLE_IOCTL(RFCOMMSTEALDLC)
COMPATIBLE_IOCTL(BNEPCONNADD)
COMPATIBLE_IOCTL(BNEPCONNDEL)
COMPATIBLE_IOCTL(BNEPGETCONNLIST)
COMPATIBLE_IOCTL(BNEPGETCONNINFO)
COMPATIBLE_IOCTL(CMTPCONNADD)
COMPATIBLE_IOCTL(CMTPCONNDEL)
COMPATIBLE_IOCTL(CMTPGETCONNLIST)
COMPATIBLE_IOCTL(CMTPGETCONNINFO)
COMPATIBLE_IOCTL(HIDPCONNADD)
COMPATIBLE_IOCTL(HIDPCONNDEL)
COMPATIBLE_IOCTL(HIDPGETCONNLIST)
COMPATIBLE_IOCTL(HIDPGETCONNINFO)
/* CAPI */
COMPATIBLE_IOCTL(CAPI_REGISTER)
COMPATIBLE_IOCTL(CAPI_GET_MANUFACTURER)
COMPATIBLE_IOCTL(CAPI_GET_VERSION)
COMPATIBLE_IOCTL(CAPI_GET_SERIAL)
COMPATIBLE_IOCTL(CAPI_GET_PROFILE)
COMPATIBLE_IOCTL(CAPI_MANUFACTURER_CMD)
COMPATIBLE_IOCTL(CAPI_GET_ERRCODE)
COMPATIBLE_IOCTL(CAPI_INSTALLED)
COMPATIBLE_IOCTL(CAPI_GET_FLAGS)
COMPATIBLE_IOCTL(CAPI_SET_FLAGS)
COMPATIBLE_IOCTL(CAPI_CLR_FLAGS)
COMPATIBLE_IOCTL(CAPI_NCCI_OPENCOUNT)
COMPATIBLE_IOCTL(CAPI_NCCI_GETUNIT)
/* Siemens Gigaset */
COMPATIBLE_IOCTL(GIGASET_REDIR)
COMPATIBLE_IOCTL(GIGASET_CONFIG)
COMPATIBLE_IOCTL(GIGASET_BRKCHARS)
COMPATIBLE_IOCTL(GIGASET_VERSION)
/* Misc. */
COMPATIBLE_IOCTL(0x41545900)		/* ATYIO_CLKR */
COMPATIBLE_IOCTL(0x41545901)		/* ATYIO_CLKW */
COMPATIBLE_IOCTL(PCIIOC_CONTROLLER)
COMPATIBLE_IOCTL(PCIIOC_MMAP_IS_IO)
COMPATIBLE_IOCTL(PCIIOC_MMAP_IS_MEM)
COMPATIBLE_IOCTL(PCIIOC_WRITE_COMBINE)
/* NBD */
COMPATIBLE_IOCTL(NBD_DO_IT)
COMPATIBLE_IOCTL(NBD_CLEAR_SOCK)
COMPATIBLE_IOCTL(NBD_CLEAR_QUE)
COMPATIBLE_IOCTL(NBD_PRINT_DEBUG)
COMPATIBLE_IOCTL(NBD_DISCONNECT)
/* i2c */
COMPATIBLE_IOCTL(I2C_SLAVE)
COMPATIBLE_IOCTL(I2C_SLAVE_FORCE)
COMPATIBLE_IOCTL(I2C_TENBIT)
COMPATIBLE_IOCTL(I2C_PEC)
COMPATIBLE_IOCTL(I2C_RETRIES)
COMPATIBLE_IOCTL(I2C_TIMEOUT)
/* hiddev */
COMPATIBLE_IOCTL(HIDIOCGVERSION)
COMPATIBLE_IOCTL(HIDIOCAPPLICATION)
COMPATIBLE_IOCTL(HIDIOCGDEVINFO)
COMPATIBLE_IOCTL(HIDIOCGSTRING)
COMPATIBLE_IOCTL(HIDIOCINITREPORT)
COMPATIBLE_IOCTL(HIDIOCGREPORT)
COMPATIBLE_IOCTL(HIDIOCSREPORT)
COMPATIBLE_IOCTL(HIDIOCGREPORTINFO)
COMPATIBLE_IOCTL(HIDIOCGFIELDINFO)
COMPATIBLE_IOCTL(HIDIOCGUSAGE)
COMPATIBLE_IOCTL(HIDIOCSUSAGE)
COMPATIBLE_IOCTL(HIDIOCGUCODE)
COMPATIBLE_IOCTL(HIDIOCGFLAG)
COMPATIBLE_IOCTL(HIDIOCSFLAG)
COMPATIBLE_IOCTL(HIDIOCGCOLLECTIONINDEX)
COMPATIBLE_IOCTL(HIDIOCGCOLLECTIONINFO)
/* dvb */
COMPATIBLE_IOCTL(AUDIO_STOP)
COMPATIBLE_IOCTL(AUDIO_PLAY)
COMPATIBLE_IOCTL(AUDIO_PAUSE)
COMPATIBLE_IOCTL(AUDIO_CONTINUE)
COMPATIBLE_IOCTL(AUDIO_SELECT_SOURCE)
COMPATIBLE_IOCTL(AUDIO_SET_MUTE)
COMPATIBLE_IOCTL(AUDIO_SET_AV_SYNC)
COMPATIBLE_IOCTL(AUDIO_SET_BYPASS_MODE)
COMPATIBLE_IOCTL(AUDIO_CHANNEL_SELECT)
COMPATIBLE_IOCTL(AUDIO_GET_STATUS)
COMPATIBLE_IOCTL(AUDIO_GET_CAPABILITIES)
COMPATIBLE_IOCTL(AUDIO_CLEAR_BUFFER)
COMPATIBLE_IOCTL(AUDIO_SET_ID)
COMPATIBLE_IOCTL(AUDIO_SET_MIXER)
COMPATIBLE_IOCTL(AUDIO_SET_STREAMTYPE)
COMPATIBLE_IOCTL(AUDIO_SET_EXT_ID)
COMPATIBLE_IOCTL(AUDIO_SET_ATTRIBUTES)
COMPATIBLE_IOCTL(AUDIO_SET_KARAOKE)
COMPATIBLE_IOCTL(DMX_START)
COMPATIBLE_IOCTL(DMX_STOP)
COMPATIBLE_IOCTL(DMX_SET_FILTER)
COMPATIBLE_IOCTL(DMX_SET_PES_FILTER)
COMPATIBLE_IOCTL(DMX_SET_BUFFER_SIZE)
COMPATIBLE_IOCTL(DMX_GET_PES_PIDS)
COMPATIBLE_IOCTL(DMX_GET_CAPS)
COMPATIBLE_IOCTL(DMX_SET_SOURCE)
COMPATIBLE_IOCTL(DMX_GET_STC)
COMPATIBLE_IOCTL(FE_GET_INFO)
COMPATIBLE_IOCTL(FE_DISEQC_RESET_OVERLOAD)
COMPATIBLE_IOCTL(FE_DISEQC_SEND_MASTER_CMD)
COMPATIBLE_IOCTL(FE_DISEQC_RECV_SLAVE_REPLY)
COMPATIBLE_IOCTL(FE_DISEQC_SEND_BURST)
COMPATIBLE_IOCTL(FE_SET_TONE)
COMPATIBLE_IOCTL(FE_SET_VOLTAGE)
COMPATIBLE_IOCTL(FE_ENABLE_HIGH_LNB_VOLTAGE)
COMPATIBLE_IOCTL(FE_READ_STATUS)
COMPATIBLE_IOCTL(FE_READ_BER)
COMPATIBLE_IOCTL(FE_READ_SIGNAL_STRENGTH)
COMPATIBLE_IOCTL(FE_READ_SNR)
COMPATIBLE_IOCTL(FE_READ_UNCORRECTED_BLOCKS)
COMPATIBLE_IOCTL(FE_SET_FRONTEND)
COMPATIBLE_IOCTL(FE_GET_FRONTEND)
COMPATIBLE_IOCTL(FE_GET_EVENT)
COMPATIBLE_IOCTL(FE_DISHNETWORK_SEND_LEGACY_CMD)
COMPATIBLE_IOCTL(VIDEO_STOP)
COMPATIBLE_IOCTL(VIDEO_PLAY)
COMPATIBLE_IOCTL(VIDEO_FREEZE)
COMPATIBLE_IOCTL(VIDEO_CONTINUE)
COMPATIBLE_IOCTL(VIDEO_SELECT_SOURCE)
COMPATIBLE_IOCTL(VIDEO_SET_BLANK)
COMPATIBLE_IOCTL(VIDEO_GET_STATUS)
COMPATIBLE_IOCTL(VIDEO_SET_DISPLAY_FORMAT)
COMPATIBLE_IOCTL(VIDEO_FAST_FORWARD)
COMPATIBLE_IOCTL(VIDEO_SLOWMOTION)
COMPATIBLE_IOCTL(VIDEO_GET_CAPABILITIES)
COMPATIBLE_IOCTL(VIDEO_CLEAR_BUFFER)
COMPATIBLE_IOCTL(VIDEO_SET_ID)
COMPATIBLE_IOCTL(VIDEO_SET_STREAMTYPE)
COMPATIBLE_IOCTL(VIDEO_SET_FORMAT)
COMPATIBLE_IOCTL(VIDEO_SET_SYSTEM)
COMPATIBLE_IOCTL(VIDEO_SET_HIGHLIGHT)
COMPATIBLE_IOCTL(VIDEO_SET_SPU)
COMPATIBLE_IOCTL(VIDEO_GET_NAVI)
COMPATIBLE_IOCTL(VIDEO_SET_ATTRIBUTES)
COMPATIBLE_IOCTL(VIDEO_GET_SIZE)
COMPATIBLE_IOCTL(VIDEO_GET_FRAME_RATE)

/* joystick */
COMPATIBLE_IOCTL(JSIOCGVERSION)
COMPATIBLE_IOCTL(JSIOCGAXES)
COMPATIBLE_IOCTL(JSIOCGBUTTONS)
COMPATIBLE_IOCTL(JSIOCGNAME(0))

#ifdef TIOCGLTC
COMPATIBLE_IOCTL(TIOCGLTC)
COMPATIBLE_IOCTL(TIOCSLTC)
#endif
#ifdef TIOCSTART
/*
 * For these two we have definitions in ioctls.h and/or termios.h on
 * some architectures but no actual implemention.  Some applications
 * like bash call them if they are defined in the headers, so we provide
 * entries here to avoid syslog message spew.
 */
COMPATIBLE_IOCTL(TIOCSTART)
COMPATIBLE_IOCTL(TIOCSTOP)
#endif

/* fat 'r' ioctls. These are handled by fat with ->compat_ioctl,
   but we don't want warnings on other file systems. So declare
   them as compatible here. */
#define VFAT_IOCTL_READDIR_BOTH32       _IOR('r', 1, struct compat_dirent[2])
#define VFAT_IOCTL_READDIR_SHORT32      _IOR('r', 2, struct compat_dirent[2])

IGNORE_IOCTL(VFAT_IOCTL_READDIR_BOTH32)
IGNORE_IOCTL(VFAT_IOCTL_READDIR_SHORT32)

#ifdef CONFIG_SPARC
/* Sparc framebuffers, handled in sbusfb_compat_ioctl() */
IGNORE_IOCTL(FBIOGTYPE)
IGNORE_IOCTL(FBIOSATTR)
IGNORE_IOCTL(FBIOGATTR)
IGNORE_IOCTL(FBIOSVIDEO)
IGNORE_IOCTL(FBIOGVIDEO)
IGNORE_IOCTL(FBIOSCURPOS)
IGNORE_IOCTL(FBIOGCURPOS)
IGNORE_IOCTL(FBIOGCURMAX)
IGNORE_IOCTL(FBIOPUTCMAP32)
IGNORE_IOCTL(FBIOGETCMAP32)
IGNORE_IOCTL(FBIOSCURSOR32)
IGNORE_IOCTL(FBIOGCURSOR32)
#endif
};

/*
 * Convert common ioctl arguments based on their command number
 *
 * Please do not add any code in here. Instead, implement
 * a compat_ioctl operation in the place that handleѕ the
 * ioctl for the native case.
 */
static long do_ioctl_trans(int fd, unsigned int cmd,
		 unsigned long arg, struct file *file)
{
	void __user *argp = compat_ptr(arg);

	switch (cmd) {
	case PPPIOCGIDLE32:
		return ppp_gidle(fd, cmd, argp);
	case PPPIOCSCOMPRESS32:
		return ppp_scompress(fd, cmd, argp);
	case PPPIOCSPASS32:
	case PPPIOCSACTIVE32:
		return ppp_sock_fprog_ioctl_trans(fd, cmd, argp);
#ifdef CONFIG_BLOCK
	case SG_IO:
		return sg_ioctl_trans(fd, cmd, argp);
	case SG_GET_REQUEST_TABLE:
		return sg_grt_trans(fd, cmd, argp);
	case MTIOCGET32:
	case MTIOCPOS32:
		return mt_ioctl_trans(fd, cmd, argp);
#endif
	/* Serial */
	case TIOCGSERIAL:
	case TIOCSSERIAL:
		return serial_struct_ioctl(fd, cmd, argp);
	/* i2c */
	case I2C_FUNCS:
		return w_long(fd, cmd, argp);
	case I2C_RDWR:
		return do_i2c_rdwr_ioctl(fd, cmd, argp);
	case I2C_SMBUS:
		return do_i2c_smbus_ioctl(fd, cmd, argp);
	/* Not implemented in the native kernel */
	case RTC_IRQP_READ32:
	case RTC_IRQP_SET32:
	case RTC_EPOCH_READ32:
	case RTC_EPOCH_SET32:
		return rtc_ioctl(fd, cmd, argp);

	/* dvb */
	case VIDEO_GET_EVENT:
		return do_video_get_event(fd, cmd, argp);
	case VIDEO_STILLPICTURE:
		return do_video_stillpicture(fd, cmd, argp);
	case VIDEO_SET_SPU_PALETTE:
		return do_video_set_spu_palette(fd, cmd, argp);
	}

	/*
	 * These take an integer instead of a pointer as 'arg',
	 * so we must not do a compat_ptr() translation.
	 */
	switch (cmd) {
	/* Big T */
	case TCSBRKP:
	case TIOCMIWAIT:
	case TIOCSCTTY:
	/* RAID */
	case HOT_REMOVE_DISK:
	case HOT_ADD_DISK:
	case SET_DISK_FAULTY:
	case SET_BITMAP_FILE:
	/* Big K */
	case KDSIGACCEPT:
	case KIOCSOUND:
	case KDMKTONE:
	case KDSETMODE:
	case KDSKBMODE:
	case KDSKBMETA:
	case KDSKBLED:
	case KDSETLED:
	/* NBD */
	case NBD_SET_SOCK:
	case NBD_SET_BLKSIZE:
	case NBD_SET_SIZE:
	case NBD_SET_SIZE_BLOCKS:
		return do_vfs_ioctl(file, fd, cmd, arg);
	}

	return -ENOIOCTLCMD;
}

static int compat_ioctl_check_table(unsigned int xcmd)
{
	int i;
	const int max = ARRAY_SIZE(ioctl_pointer) - 1;

	BUILD_BUG_ON(max >= (1 << 16));

	/* guess initial offset into table, assuming a
	   normalized distribution */
	i = ((xcmd >> 16) * max) >> 16;

	/* do linear search up first, until greater or equal */
	while (ioctl_pointer[i] < xcmd && i < max)
		i++;

	/* then do linear search down */
	while (ioctl_pointer[i] > xcmd && i > 0)
		i--;

	return ioctl_pointer[i] == xcmd;
}

asmlinkage long compat_sys_ioctl(unsigned int fd, unsigned int cmd,
				unsigned long arg)
{
	struct fd f = fdget(fd);
	int error = -EBADF;
	if (!f.file)
		goto out;

	/* RED-PEN how should LSM module know it's handling 32bit? */
	error = security_file_ioctl(f.file, cmd, arg);
	if (error)
		goto out_fput;

	/*
	 * To allow the compat_ioctl handlers to be self contained
	 * we need to check the common ioctls here first.
	 * Just handle them with the standard handlers below.
	 */
	switch (cmd) {
	case FIOCLEX:
	case FIONCLEX:
	case FIONBIO:
	case FIOASYNC:
	case FIOQSIZE:
		break;

#if defined(CONFIG_IA64) || defined(CONFIG_X86_64)
	case FS_IOC_RESVSP_32:
	case FS_IOC_RESVSP64_32:
		error = compat_ioctl_preallocate(f.file, compat_ptr(arg));
		goto out_fput;
#else
	case FS_IOC_RESVSP:
	case FS_IOC_RESVSP64:
		error = ioctl_preallocate(f.file, compat_ptr(arg));
		goto out_fput;
#endif

	case FIBMAP:
	case FIGETBSZ:
	case FIONREAD:
		if (S_ISREG(file_inode(f.file)->i_mode))
			break;
		/*FALL THROUGH*/

	default:
		if (f.file->f_op && f.file->f_op->compat_ioctl) {
			error = f.file->f_op->compat_ioctl(f.file, cmd, arg);
			if (error != -ENOIOCTLCMD)
				goto out_fput;
		}

		if (!f.file->f_op || !f.file->f_op->unlocked_ioctl)
			goto do_ioctl;
		break;
	}

	if (compat_ioctl_check_table(XFORM(cmd)))
		goto found_handler;

	error = do_ioctl_trans(fd, cmd, arg, f.file);
	if (error == -ENOIOCTLCMD)
		error = -ENOTTY;

	goto out_fput;

 found_handler:
	arg = (unsigned long)compat_ptr(arg);
 do_ioctl:
	error = do_vfs_ioctl(f.file, fd, cmd, arg);
 out_fput:
	fdput(f);
 out:
	return error;
}

static int __init init_sys32_ioctl_cmp(const void *p, const void *q)
{
	unsigned int a, b;
	a = *(unsigned int *)p;
	b = *(unsigned int *)q;
	if (a > b)
		return 1;
	if (a < b)
		return -1;
	return 0;
}

static int __init init_sys32_ioctl(void)
{
	sort(ioctl_pointer, ARRAY_SIZE(ioctl_pointer), sizeof(*ioctl_pointer),
		init_sys32_ioctl_cmp, NULL);
	return 0;
}
__initcall(init_sys32_ioctl);

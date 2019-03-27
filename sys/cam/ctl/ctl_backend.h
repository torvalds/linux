/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Silicon Graphics International Corp.
 * Copyright (c) 2014-2017 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_backend.h#2 $
 * $FreeBSD$
 */
/*
 * CTL backend driver definitions
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#ifndef	_CTL_BACKEND_H_
#define	_CTL_BACKEND_H_

#include <cam/ctl/ctl_ioctl.h>
#include <sys/nv.h>

typedef enum {
	CTL_LUN_SERSEQ_OFF,
	CTL_LUN_SERSEQ_READ,
	CTL_LUN_SERSEQ_ON
} ctl_lun_serseq;

#ifdef _KERNEL

#define CTL_BACKEND_DECLARE(name, driver) \
	static int name ## _modevent(module_t mod, int type, void *data) \
	{ \
		switch (type) { \
		case MOD_LOAD: \
			return (ctl_backend_register( \
				(struct ctl_backend_driver *)data)); \
			break; \
		case MOD_UNLOAD: \
			return (ctl_backend_deregister( \
				(struct ctl_backend_driver *)data)); \
			break; \
		default: \
			return EOPNOTSUPP; \
		} \
		return 0; \
	} \
	static moduledata_t name ## _mod = { \
		#name, \
		name ## _modevent, \
		(void *)&driver \
	}; \
	DECLARE_MODULE(name, name ## _mod, SI_SUB_CONFIGURE, SI_ORDER_FOURTH); \
	MODULE_DEPEND(name, ctl, 1, 1, 1); \
	MODULE_DEPEND(name, cam, 1, 1, 1)


typedef enum {
	CTL_LUN_CONFIG_OK,
	CTL_LUN_CONFIG_FAILURE
} ctl_lun_config_status;

typedef void (*be_callback_t)(void *be_lun);
typedef void (*be_lun_config_t)(void *be_lun,
				ctl_lun_config_status status);

/*
 * The lun_type field is the SCSI device type of this particular LUN.  In
 * general, this should be T_DIRECT, although backends will want to create
 * a processor LUN, typically at LUN 0.  See scsi_all.h for the defines for
 * the various SCSI device types.
 *
 * The flags are described above.
 *
 * The be_lun field is the backend driver's own context that will get
 * passsed back so that it can tell which LUN CTL is referencing.
 *
 * maxlba is the maximum accessible LBA on the LUN.  Note that this is
 * different from the capacity of the array.  capacity = maxlba + 1
 *
 * blocksize is the size, in bytes, of each LBA on the LUN.  In general
 * this should be 512.  In theory CTL should be able to handle other block
 * sizes.  Host application software may not deal with it very well, though.
 *
 * pblockexp is the log2() of number of LBAs on the LUN per physical sector.
 *
 * pblockoff is the lowest LBA on the LUN aligned to physical sector.
 *
 * ublockexp is the log2() of number of LBAs on the LUN per UNMAP block.
 *
 * ublockoff is the lowest LBA on the LUN aligned to UNMAP block.
 *
 * atomicblock is the number of blocks that can be written atomically.
 *
 * opttxferlen is the number of blocks that can be written in one operation.
 *
 * req_lun_id is the requested LUN ID.  CTL only pays attention to this
 * field if the CTL_LUN_FLAG_ID_REQ flag is set.  If the requested LUN ID is
 * not available, the LUN addition will fail.  If a particular LUN ID isn't
 * requested, the first available LUN ID will be allocated.
 *
 * serial_num is the device serial number returned in the SCSI INQUIRY VPD
 * page 0x80.  This should be a unique, per-shelf value.  The data inside
 * this field should be ASCII only, left aligned, and any unused space
 * should be padded out with ASCII spaces.  This field should NOT be NULL
 * terminated.
 *
 * device_id is the T10 device identifier returned in the SCSI INQUIRY VPD
 * page 0x83.  This should be a unique, per-LUN value.  The data inside
 * this field should be ASCII only, left aligned, and any unused space
 * should be padded with ASCII spaces.  This field should NOT be NULL
 * terminated.
 *
 * The lun_shutdown() method is the callback for the ctl_invalidate_lun()
 * call.  It is called when all outstanding I/O for that LUN has been
 * completed and CTL has deleted the resources for that LUN.  When the CTL
 * backend gets this call, it can safely free its per-LUN resources.
 *
 * The lun_config_status() method is the callback for the ctl_add_lun()
 * call.  It is called when the LUN is successfully added, or when LUN
 * addition fails.  If the LUN is successfully added, the backend may call
 * the ctl_enable_lun() method to enable the LUN.
 *
 * The be field is a pointer to the ctl_backend_driver structure, which
 * contains the backend methods to be called by CTL.
 *
 * The ctl_lun field is for CTL internal use only, and should not be used
 * by the backend.
 *
 * The links field is for CTL internal use only, and should not be used by
 * the backend.
 */
struct ctl_be_lun {
	uint8_t			lun_type;	/* passed to CTL */
	ctl_backend_lun_flags	flags;		/* passed to CTL */
	ctl_lun_serseq		serseq;		/* passed to CTL */
	void			*be_lun;	/* passed to CTL */
	uint64_t		maxlba;		/* passed to CTL */
	uint32_t		blocksize;	/* passed to CTL */
	uint16_t		pblockexp;	/* passed to CTL */
	uint16_t		pblockoff;	/* passed to CTL */
	uint16_t		ublockexp;	/* passed to CTL */
	uint16_t		ublockoff;	/* passed to CTL */
	uint32_t		atomicblock;	/* passed to CTL */
	uint32_t		opttxferlen;	/* passed to CTL */
	uint32_t		req_lun_id;	/* passed to CTL */
	uint32_t		lun_id;		/* returned from CTL */
	uint8_t			serial_num[CTL_SN_LEN];	 /* passed to CTL */
	uint8_t			device_id[CTL_DEVID_LEN];/* passed to CTL */
	be_callback_t		lun_shutdown;	/* passed to CTL */
	be_lun_config_t		lun_config_status; /* passed to CTL */
	struct ctl_backend_driver *be;		/* passed to CTL */
	void			*ctl_lun;	/* used by CTL */
	nvlist_t	 	*options;	/* passed to CTL */
	STAILQ_ENTRY(ctl_be_lun) links;		/* used by CTL */
};

typedef enum {
	CTL_BE_FLAG_NONE	= 0x00,	/* no flags */
	CTL_BE_FLAG_HAS_CONFIG	= 0x01,	/* can do config reads, writes */
} ctl_backend_flags;

typedef int (*be_init_t)(void);
typedef int (*be_shutdown_t)(void);
typedef int (*be_func_t)(union ctl_io *io);
typedef void (*be_vfunc_t)(union ctl_io *io);
typedef int (*be_ioctl_t)(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
			  struct thread *td);
typedef int (*be_luninfo_t)(void *be_lun, struct sbuf *sb);
typedef uint64_t (*be_lunattr_t)(void *be_lun, const char *attrname);

struct ctl_backend_driver {
	char		  name[CTL_BE_NAME_LEN]; /* passed to CTL */
	ctl_backend_flags flags;	         /* passed to CTL */
	be_init_t	  init;			 /* passed to CTL */
	be_shutdown_t	  shutdown;		 /* passed to CTL */
	be_func_t	  data_submit;		 /* passed to CTL */
	be_func_t	  data_move_done;	 /* passed to CTL */
	be_func_t	  config_read;		 /* passed to CTL */
	be_func_t	  config_write;		 /* passed to CTL */
	be_ioctl_t	  ioctl;		 /* passed to CTL */
	be_luninfo_t	  lun_info;		 /* passed to CTL */
	be_lunattr_t	  lun_attr;		 /* passed to CTL */
#ifdef CS_BE_CONFIG_MOVE_DONE_IS_NOT_USED
	be_func_t	  config_move_done;	 /* passed to backend */
#endif
#if 0
	be_vfunc_t	  config_write_done;	 /* passed to backend */
#endif
	u_int		  num_luns;		 /* used by CTL */
	STAILQ_ENTRY(ctl_backend_driver) links;	 /* used by CTL */
};

int ctl_backend_register(struct ctl_backend_driver *be);
int ctl_backend_deregister(struct ctl_backend_driver *be);
struct ctl_backend_driver *ctl_backend_find(char *backend_name);

/*
 * To add a LUN, first call ctl_add_lun().  You will get the lun_config_status()
 * callback when the LUN addition has either succeeded or failed.
 *
 * Once you get that callback, you can then call ctl_enable_lun() to enable
 * the LUN.
 */
int ctl_add_lun(struct ctl_be_lun *be_lun);
int ctl_enable_lun(struct ctl_be_lun *be_lun);

/*
 * To delete a LUN, first call ctl_disable_lun(), then
 * ctl_invalidate_lun().  You will get the lun_shutdown() callback when all
 * I/O to the LUN has completed and the LUN has been deleted.
 */
int ctl_disable_lun(struct ctl_be_lun *be_lun);
int ctl_invalidate_lun(struct ctl_be_lun *be_lun);

/*
 * To start a LUN (transition from powered off to powered on state) call
 * ctl_start_lun().  To stop a LUN (transition from powered on to powered
 * off state) call ctl_stop_lun().
 */
int ctl_start_lun(struct ctl_be_lun *be_lun);
int ctl_stop_lun(struct ctl_be_lun *be_lun);

/*
 * Methods to notify about media and tray status changes.
 */
int ctl_lun_no_media(struct ctl_be_lun *be_lun);
int ctl_lun_has_media(struct ctl_be_lun *be_lun);
int ctl_lun_ejected(struct ctl_be_lun *be_lun);

/*
 * Called on LUN HA role change.
 */
int ctl_lun_primary(struct ctl_be_lun *be_lun);
int ctl_lun_secondary(struct ctl_be_lun *be_lun);

/*
 * Let the backend notify the initiators about changes.
 */
void ctl_lun_capacity_changed(struct ctl_be_lun *be_lun);

#endif /* _KERNEL */
#endif /* _CTL_BACKEND_H_ */

/*
 * vim: ts=8
 */

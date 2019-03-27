/*-
 * CAM ioctl compatibility shims
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Scott Long
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _CAM_CAM_COMPAT_H
#define _CAM_CAM_COMPAT_H

/* No user-serviceable parts in here. */
#ifdef _KERNEL

int cam_compat_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
    struct thread *td, int(*cbfnp)(struct cdev *, u_long, caddr_t, int,
    struct thread *));


/* Version 0x16 compatibility */
#define CAM_VERSION_0x16	0x16

/* The size of the union ccb didn't change when going to 0x17 */
#define	CAMIOCOMMAND_0x16	_IOC(IOC_INOUT, CAM_VERSION_0x16, 2, CAM_0X17_LEN)
#define	CAMGETPASSTHRU_0x16	_IOC(IOC_INOUT, CAM_VERSION_0x16, 3, CAM_0X17_LEN)

#define CAM_SCATTER_VALID_0x16	0x00000010
#define CAM_SG_LIST_PHYS_0x16	0x00040000
#define CAM_DATA_PHYS_0x16	0x00200000

/* Version 0x17 compatibility */
#define CAM_VERSION_0x17	0x17

struct ccb_hdr_0x17 {
	cam_pinfo	pinfo;		/* Info for priority scheduling */
	camq_entry	xpt_links;	/* For chaining in the XPT layer */	
	camq_entry	sim_links;	/* For chaining in the SIM layer */	
	camq_entry	periph_links;	/* For chaining in the type driver */
	u_int32_t	retry_count;
	void		(*cbfcnp)(struct cam_periph *, union ccb *);
	xpt_opcode	func_code;	/* XPT function code */
	u_int32_t	status;		/* Status returned by CAM subsystem */
	struct		cam_path *path;	/* Compiled path for this ccb */
	path_id_t	path_id;	/* Path ID for the request */
	target_id_t	target_id;	/* Target device ID */
	u_int		target_lun;	/* Target LUN number */
	u_int32_t	flags;		/* ccb_flags */
	ccb_ppriv_area	periph_priv;
	ccb_spriv_area	sim_priv;
	u_int32_t	timeout;	/* Hard timeout value in seconds */
	struct callout_handle timeout_ch;
};

struct ccb_pathinq_0x17 {
	struct ccb_hdr_0x17 ccb_h;
	u_int8_t    version_num;	/* Version number for the SIM/HBA */
	u_int8_t    hba_inquiry;	/* Mimic of INQ byte 7 for the HBA */
	u_int8_t    target_sprt;	/* Flags for target mode support */
	u_int8_t    hba_misc;		/* Misc HBA features */
	u_int16_t   hba_eng_cnt;	/* HBA engine count */
					/* Vendor Unique capabilities */
	u_int8_t    vuhba_flags[VUHBALEN];
	u_int32_t   max_target;		/* Maximum supported Target */
	u_int32_t   max_lun;		/* Maximum supported Lun */
	u_int32_t   async_flags;	/* Installed Async handlers */
	path_id_t   hpath_id;		/* Highest Path ID in the subsystem */
	target_id_t initiator_id;	/* ID of the HBA on the SCSI bus */
	char	    sim_vid[SIM_IDLEN];	/* Vendor ID of the SIM */
	char	    hba_vid[HBA_IDLEN];	/* Vendor ID of the HBA */
	char 	    dev_name[DEV_IDLEN];/* Device name for SIM */
	u_int32_t   unit_number;	/* Unit number for SIM */
	u_int32_t   bus_id;		/* Bus ID for SIM */
	u_int32_t   base_transfer_speed;/* Base bus speed in KB/sec */
	cam_proto   protocol;
	u_int	    protocol_version;
	cam_xport   transport;
	u_int	    transport_version;
	union {
		struct ccb_pathinq_settings_spi spi;
		struct ccb_pathinq_settings_fc fc;
		struct ccb_pathinq_settings_sas sas;
		char ccb_pathinq_settings_opaque[PATHINQ_SETTINGS_SIZE];
	} xport_specific;
	u_int		maxio;		/* Max supported I/O size, in bytes. */
	u_int16_t	hba_vendor;	/* HBA vendor ID */
	u_int16_t	hba_device;	/* HBA device ID */
	u_int16_t	hba_subvendor;	/* HBA subvendor ID */
	u_int16_t	hba_subdevice;	/* HBA subdevice ID */
};

struct ccb_trans_settings_0x17 {
	struct	  ccb_hdr_0x17 ccb_h;
	cts_type  type;		/* Current or User settings */
	cam_proto protocol;
	u_int	  protocol_version;
	cam_xport transport;
	u_int	  transport_version;
	union {
		u_int  valid;	/* Which fields to honor */
		struct ccb_trans_settings_ata ata;
		struct ccb_trans_settings_scsi scsi;
	} proto_specific;
	union {
		u_int  valid;	/* Which fields to honor */
		struct ccb_trans_settings_spi spi;
		struct ccb_trans_settings_fc fc;
		struct ccb_trans_settings_sas sas;
		struct ccb_trans_settings_pata ata;
		struct ccb_trans_settings_sata sata;
	} xport_specific;
};

#define CAM_0X17_DATA_LEN	CAM_0X18_DATA_LEN
#define CAM_0X17_LEN		(sizeof(struct ccb_hdr_0x17) + CAM_0X17_DATA_LEN)

#define	CAMIOCOMMAND_0x17	_IOC(IOC_INOUT, CAM_VERSION_0x17, 2, CAM_0X17_LEN)
#define CAMGETPASSTHRU_0x17	_IOC(IOC_INOUT, CAM_VERSION_0x17, 3, CAM_0X17_LEN)

/* Version 0x18 compatibility */
#define CAM_VERSION_0x18	0x18

struct ccb_hdr_0x18 {
	cam_pinfo	pinfo;		/* Info for priority scheduling */
	camq_entry	xpt_links;	/* For chaining in the XPT layer */	
	camq_entry	sim_links;	/* For chaining in the SIM layer */	
	camq_entry	periph_links;	/* For chaining in the type driver */
	u_int32_t	retry_count;
	void		(*cbfcnp)(struct cam_periph *, union ccb *);
	xpt_opcode	func_code;	/* XPT function code */
	u_int32_t	status;		/* Status returned by CAM subsystem */
	struct		cam_path *path;	/* Compiled path for this ccb */
	path_id_t	path_id;	/* Path ID for the request */
	target_id_t	target_id;	/* Target device ID */
	u_int		target_lun;	/* Target LUN number */
	u_int64_t	ext_lun;	/* 64-bit LUN, more or less */
	u_int32_t	flags;		/* ccb_flags */
	u_int32_t	xflags;		/* extended ccb_flags */
	ccb_ppriv_area	periph_priv;
	ccb_spriv_area	sim_priv;
	ccb_qos_area	qos;
	u_int32_t	timeout;	/* Hard timeout value in seconds */
	struct timeval	softtimeout;	/* Soft timeout value in sec + usec */
};

typedef enum {
	CAM_EXTLUN_VALID_0x18	= 0x00000001,/* 64bit lun field is valid      */
} ccb_xflags_0x18;

struct ccb_trans_settings_0x18 {
	struct	  ccb_hdr_0x18 ccb_h;
	cts_type  type;		/* Current or User settings */
	cam_proto protocol;
	u_int	  protocol_version;
	cam_xport transport;
	u_int	  transport_version;
	union {
		u_int  valid;	/* Which fields to honor */
		struct ccb_trans_settings_ata ata;
		struct ccb_trans_settings_scsi scsi;
	} proto_specific;
	union {
		u_int  valid;	/* Which fields to honor */
		struct ccb_trans_settings_spi spi;
		struct ccb_trans_settings_fc fc;
		struct ccb_trans_settings_sas sas;
		struct ccb_trans_settings_pata ata;
		struct ccb_trans_settings_sata sata;
	} xport_specific;
};

struct dev_match_result_0x18 {
        dev_match_type          type;
        union {
		struct {
			char periph_name[DEV_IDLEN];
			u_int32_t unit_number;
			path_id_t path_id;
			target_id_t target_id;
			u_int target_lun;
		} periph_result;
		struct {
			path_id_t	path_id;
			target_id_t	target_id;
			u_int		target_lun;
			cam_proto	protocol;
			struct scsi_inquiry_data inq_data;
			struct ata_params ident_data;
			dev_result_flags flags;
		} device_result;
		struct bus_match_result	bus_result;
	} result;
};

#define CAM_0X18_DATA_LEN	(sizeof(union ccb) - 2*sizeof(void *) - sizeof(struct ccb_hdr))
#define CAM_0X18_LEN		(sizeof(struct ccb_hdr_0x18) + CAM_0X18_DATA_LEN)

#define	CAMIOCOMMAND_0x18	_IOC(IOC_INOUT, CAM_VERSION_0x18, 2, CAM_0X18_LEN)
#define CAMGETPASSTHRU_0x18	_IOC(IOC_INOUT, CAM_VERSION_0x18, 3, CAM_0X18_LEN)

#endif
#endif

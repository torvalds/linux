/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Silicon Graphics International Corp.
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
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl.h#5 $
 * $FreeBSD$
 */
/*
 * Function definitions used both within CTL and potentially in various CTL
 * clients.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#ifndef	_CTL_H_
#define	_CTL_H_

#define	CTL_RETVAL_COMPLETE	0
#define	CTL_RETVAL_QUEUED	1
#define	CTL_RETVAL_ALLOCATED	2
#define	CTL_RETVAL_ERROR	3

typedef enum {
	CTL_PORT_NONE		= 0x00,
	CTL_PORT_FC		= 0x01,
	CTL_PORT_SCSI		= 0x02,
	CTL_PORT_IOCTL		= 0x04,
	CTL_PORT_INTERNAL	= 0x08,
	CTL_PORT_ISCSI		= 0x10,
	CTL_PORT_SAS		= 0x20,
	CTL_PORT_UMASS		= 0x40,
	CTL_PORT_ALL		= 0xff,
	CTL_PORT_ISC		= 0x100 // FC port for inter-shelf communication
} ctl_port_type;

struct ctl_port_entry {
	ctl_port_type		port_type;
	char			port_name[64];
	int32_t			targ_port;
	int			physical_port;
	int			virtual_port;
	u_int			flags;
#define	CTL_PORT_WWNN_VALID	0x01
#define	CTL_PORT_WWPN_VALID	0x02
	uint64_t		wwnn;
	uint64_t		wwpn;
	int			online;
};

struct ctl_modepage_header {
	uint8_t			page_code;
	uint8_t			subpage;
	uint16_t		len_used;
	uint16_t		len_left;
};

union ctl_modepage_info {
	struct ctl_modepage_header header;
};

/*
 * Serial number length, for VPD page 0x80.
 */
#define	CTL_SN_LEN	16

/*
 * Device ID length, for VPD page 0x83.
 */
#define	CTL_DEVID_LEN	64
#define	CTL_DEVID_MIN_LEN	16
/*
 * WWPN length, for VPD page 0x83.
 */
#define CTL_WWPN_LEN   8

#define	CTL_DRIVER_NAME_LEN	32

/*
 * Unit attention types. ASC/ASCQ values for these should be placed in
 * ctl_build_ua.  These are also listed in order of reporting priority.
 * i.e. a poweron UA is reported first, bus reset second, etc.
 */
typedef enum {
	CTL_UA_NONE		= 0x0000,
	CTL_UA_POWERON		= 0x0001,
	CTL_UA_BUS_RESET	= 0x0002,
	CTL_UA_TARG_RESET	= 0x0004,
	CTL_UA_I_T_NEXUS_LOSS	= 0x0008,
	CTL_UA_LUN_RESET	= 0x0010,
	CTL_UA_LUN_CHANGE	= 0x0020,
	CTL_UA_MODE_CHANGE	= 0x0040,
	CTL_UA_LOG_CHANGE	= 0x0080,
	CTL_UA_INQ_CHANGE	= 0x0100,
	CTL_UA_RES_PREEMPT	= 0x0400,
	CTL_UA_RES_RELEASE	= 0x0800,
	CTL_UA_REG_PREEMPT	= 0x1000,
	CTL_UA_ASYM_ACC_CHANGE	= 0x2000,
	CTL_UA_CAPACITY_CHANGE	= 0x4000,
	CTL_UA_THIN_PROV_THRES	= 0x8000,
	CTL_UA_MEDIUM_CHANGE	= 0x10000,
	CTL_UA_IE		= 0x20000
} ctl_ua_type;

#ifdef	_KERNEL

MALLOC_DECLARE(M_CTL);

struct ctl_page_index;

#ifdef SYSCTL_DECL	/* from sysctl.h */
SYSCTL_DECL(_kern_cam_ctl);
#endif

struct ctl_lun;
struct ctl_port;
struct ctl_softc;

/*
 * Put a string into an sbuf, escaping characters that are illegal or not
 * recommended in XML.  Note this doesn't escape everything, just > < and &.
 */
int ctl_sbuf_printf_esc(struct sbuf *sb, char *str, int size);

int ctl_ffz(uint32_t *mask, uint32_t first, uint32_t last);
int ctl_set_mask(uint32_t *mask, uint32_t bit);
int ctl_clear_mask(uint32_t *mask, uint32_t bit);
int ctl_is_set(uint32_t *mask, uint32_t bit);
int ctl_default_page_handler(struct ctl_scsiio *ctsio,
			     struct ctl_page_index *page_index,
			     uint8_t *page_ptr);
int ctl_ie_page_handler(struct ctl_scsiio *ctsio,
			struct ctl_page_index *page_index,
			uint8_t *page_ptr);
int ctl_lbp_log_sense_handler(struct ctl_scsiio *ctsio,
				   struct ctl_page_index *page_index,
				   int pc);
int ctl_sap_log_sense_handler(struct ctl_scsiio *ctsio,
				   struct ctl_page_index *page_index,
				   int pc);
int ctl_ie_log_sense_handler(struct ctl_scsiio *ctsio,
				   struct ctl_page_index *page_index,
				   int pc);
int ctl_config_move_done(union ctl_io *io);
void ctl_datamove(union ctl_io *io);
void ctl_serseq_done(union ctl_io *io);
void ctl_done(union ctl_io *io);
void ctl_data_submit_done(union ctl_io *io);
void ctl_config_read_done(union ctl_io *io);
void ctl_config_write_done(union ctl_io *io);
void ctl_portDB_changed(int portnum);
int ctl_ioctl_io(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
		 struct thread *td);

void ctl_est_ua(struct ctl_lun *lun, uint32_t initidx, ctl_ua_type ua);
void ctl_est_ua_port(struct ctl_lun *lun, int port, uint32_t except,
    ctl_ua_type ua);
void ctl_est_ua_all(struct ctl_lun *lun, uint32_t except, ctl_ua_type ua);
void ctl_clr_ua(struct ctl_lun *lun, uint32_t initidx, ctl_ua_type ua);
void ctl_clr_ua_all(struct ctl_lun *lun, uint32_t except, ctl_ua_type ua);
void ctl_clr_ua_allluns(struct ctl_softc *ctl_softc, uint32_t initidx,
    ctl_ua_type ua_type);

uint32_t ctl_decode_lun(uint64_t encoded);
uint64_t ctl_encode_lun(uint32_t decoded);

void ctl_isc_announce_lun(struct ctl_lun *lun);
void ctl_isc_announce_port(struct ctl_port *port);
void ctl_isc_announce_iid(struct ctl_port *port, int iid);
void ctl_isc_announce_mode(struct ctl_lun *lun, uint32_t initidx,
    uint8_t page, uint8_t subpage);

int ctl_expand_number(const char *buf, uint64_t *num);

#endif	/* _KERNEL */

#endif	/* _CTL_H_ */

/*
 * vim: ts=8
 */

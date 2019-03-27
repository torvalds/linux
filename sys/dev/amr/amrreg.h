/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c) 1999,2000 Michael Smith
 * Copyright (c) 2000 BSDi
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 2002 Eric Moore
 * Copyright (c) 2002 LSI Logic Corporation
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
 * 3. The party using or redistributing the source code and binary forms
 *    agrees to the disclaimer below and the terms and conditions set forth
 *    herein.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 *      $FreeBSD$
 */

/********************************************************************************
 ********************************************************************************
                                                                Driver parameters
 ********************************************************************************
 ********************************************************************************/

/*
 * We could actually use all 17 segments, but using only 16 means that
 * each scatter/gather map is 128 bytes in size, and thus we don't have to worry about
 * maps crossing page boundaries.
 *
 * The AMI documentation says that the limit is 26.  Unfortunately, there's no way to
 * cleanly fit more than 16 entries in without a page boundary.  But is this a concern,
 * since we allocate the s/g maps contiguously anyway?
 */
/*
 * emoore - Oct 21, 2002
 * firmware doesn't have sglist boundary restrictions.
 * The sgelem can be set to 26
 */
#define AMR_NSEG		26

#define AMR_MAXCMD		255		/* ident = 0 not allowed */
#define AMR_LIMITCMD		120		/* maximum count of outstanding commands */
#define AMR_MAXLD      		40

#define AMR_MAX_CHANNELS	8
#define AMR_MAX_TARGETS		15
#define AMR_MAX_LUNS		7
#define AMR_MAX_SCSI_CMDS	(15 * AMR_MAX_CHANNELS)	/* one for every target? */

#define AMR_MAX_CDB_LEN		0x0a
#define AMR_MAX_EXTCDB_LEN	0x10
#define AMR_MAX_REQ_SENSE_LEN	0x20

#define AMR_BLKSIZE		512		/* constant for all controllers */

/*
 * Perform at-startup board initialisation.
 * At this point in time, this code doesn't work correctly, so leave it disabled.
 */
/*#define AMR_BOARD_INIT*/

/********************************************************************************
 ********************************************************************************
                                                          Interface Magic Numbers
 ********************************************************************************
 ********************************************************************************/

/*
 * Mailbox commands
 */
#define AMR_CMD_LREAD		0x01
#define AMR_CMD_LWRITE		0x02
#define AMR_CMD_PASS		0x03
#define AMR_CMD_EXT_ENQUIRY	0x04
#define AMR_CMD_ENQUIRY		0x05
#define AMR_CMD_FLUSH		0x0a
#define AMR_CMD_EXT_ENQUIRY2	0x0c
#define AMR_CONFIG_PRODINFO	0x0e
#define AMR_CMD_GET_MACHINEID	0x36
#define AMR_CMD_GET_INITIATOR	0x7d	/* returns one byte */
#define AMR_CMD_CONFIG		0xa1
#define AMR_CMD_LREAD64		0xa7
#define AMR_CMD_LWRITE64	0xa8
#define AMR_CMD_PASS_64		0xc3
#define AMR_CMD_EXTPASS		0xe3

#define AMR_CONFIG_READ_NVRAM_CONFIG	0x04
#define AMR_CONFIG_WRITE_NVRAM_CONFIG	0x0d
#define AMR_CONFIG_PRODUCT_INFO		0x0e
#define AMR_CONFIG_ENQ3			0x0f
#define AMR_CONFIG_ENQ3_SOLICITED_NOTIFY	0x01
#define AMR_CONFIG_ENQ3_SOLICITED_FULL		0x02
#define AMR_CONFIG_ENQ3_UNSOLICITED		0x03

/*
 * Command for random deletion of logical drives
 */
#define FC_DEL_LOGDRV		0xA4
#define OP_SUP_DEL_LOGDRV	0x2A
#define OP_GET_LDID_MAP		0x18
#define OP_DEL_LOGDRV		0x1C

/*
 * Command for random deletion of logical drives
 */
#define FC_DEL_LOGDRV		0xA4
#define OP_SUP_DEL_LOGDRV	0x2A
#define OP_GET_LDID_MAP		0x18
#define OP_DEL_LOGDRV		0x1C

/*
 * Command results
 */
#define AMR_STATUS_SUCCESS	0x00
#define AMR_STATUS_ABORTED	0x02
#define AMR_STATUS_FAILED	0x80

/*
 * Physical/logical drive states
 */
#define AMR_DRV_CURSTATE(x)	((x) & 0x0f)
#define AMR_DRV_PREVSTATE(x)	(((x) >> 4) & 0x0f)
#define AMR_DRV_OFFLINE		0x00
#define AMR_DRV_DEGRADED	0x01
#define AMR_DRV_OPTIMAL		0x02
#define AMR_DRV_ONLINE		0x03
#define AMR_DRV_FAILED		0x04
#define AMR_DRV_REBUILD		0x05
#define AMR_DRV_HOTSPARE	0x06

/*
 * Logical drive properties
 */
#define AMR_DRV_RAID_MASK	0x0f	/* RAID level 0, 1, 3, 5, etc. */
#define AMR_DRV_WRITEBACK	0x10	/* write-back enabled */
#define AMR_DRV_READHEAD	0x20	/* readhead policy enabled */
#define AMR_DRV_ADAPTIVE	0x40	/* adaptive I/O policy enabled */

/*
 * Battery status
 */
#define AMR_BATT_MODULE_MISSING		0x01
#define AMR_BATT_LOW_VOLTAGE		0x02
#define AMR_BATT_TEMP_HIGH		0x04
#define AMR_BATT_PACK_MISSING		0x08
#define AMR_BATT_CHARGE_MASK		0x30
#define AMR_BATT_CHARGE_DONE		0x00
#define AMR_BATT_CHARGE_INPROG		0x10
#define AMR_BATT_CHARGE_FAIL		0x20
#define AMR_BATT_CYCLES_EXCEEDED	0x40


/********************************************************************************
 ********************************************************************************
                                                           8LD Firmware Interface
 ********************************************************************************
 ********************************************************************************/

/*
 * Array constraints
 */
#define AMR_8LD_MAXDRIVES	8
#define AMR_8LD_MAXCHAN		5
#define AMR_8LD_MAXTARG		15
#define AMR_8LD_MAXPHYSDRIVES	(AMR_8LD_MAXCHAN * AMR_8LD_MAXTARG)

/*
 * Adapter Info structure
 */
struct amr_adapter_info
{
    u_int8_t	aa_maxio;
    u_int8_t	aa_rebuild_rate;
    u_int8_t	aa_maxtargchan;
    u_int8_t	aa_channels;
    u_int8_t	aa_firmware[4];
    u_int16_t	aa_flashage;
    u_int8_t	aa_chipsetvalue;
    u_int8_t	aa_memorysize;
    u_int8_t	aa_cacheflush;
    u_int8_t	aa_bios[4];
    u_int8_t	aa_boardtype;
    u_int8_t	aa_scsisensealert;
    u_int8_t	aa_writeconfigcount;
    u_int8_t	aa_driveinsertioncount;
    u_int8_t	aa_inserteddrive;
    u_int8_t	aa_batterystatus;
    u_int8_t   	res1;
} __packed;

/*
 * Logical Drive info structure
 */
struct amr_logdrive_info
{
    u_int8_t	al_numdrives;
    u_int8_t	res1[3];
    u_int32_t	al_size[AMR_8LD_MAXDRIVES];
    u_int8_t	al_properties[AMR_8LD_MAXDRIVES];
    u_int8_t	al_state[AMR_8LD_MAXDRIVES];
} __packed;

/*
 * Physical Drive info structure
 */
struct amr_physdrive_info
{
    u_int8_t	ap_state[AMR_8LD_MAXPHYSDRIVES];	/* low nibble current state, high nibble previous state */
    u_int8_t	ap_predictivefailure;
} __packed;

/*
 * Enquiry response structure for AMR_CMD_ENQUIRY, AMR_CMD_EXT_ENQUIRY and
 * AMR_CMD_EXT_ENQUIRY2.
 *								ENQUIRY EXT_ENQUIRY EXT_ENQUIRY2 
 */
struct amr_enquiry
{
    struct amr_adapter_info	ae_adapter;			/* X	  X		X */
    struct amr_logdrive_info	ae_ldrv;			/* X	  X		X */
    struct amr_physdrive_info	ae_pdrv;			/* X	  X		X */
    u_int8_t			ae_formatting[AMR_8LD_MAXDRIVES];/*	  X		X */
    u_int8_t			res1[AMR_8LD_MAXDRIVES];	/*	  X		X */
    u_int32_t			ae_extlen;			/*			X */
    u_int16_t			ae_subsystem;			/*			X */
    u_int16_t			ae_subvendor;			/*			X */
    u_int32_t			ae_signature;			/*			X */
#define AMR_SIG_431	0xfffe0001
#define AMR_SIG_438	0xfffd0002
#define AMR_SIG_762	0xfffc0003
#define AMR_SIG_T5	0xfffb0004
#define AMR_SIG_466	0xfffa0005
#define AMR_SIG_467	0xfff90006
#define AMR_SIG_T7	0xfff80007
#define AMR_SIG_490	0xfff70008
    u_int8_t			res2[844];			/*			X */
} __packed;


/********************************************************************************
 ********************************************************************************
                                                          40LD Firmware Interface
 ********************************************************************************
 ********************************************************************************/

/*
 * Array constraints
 */
#define AMR_40LD_MAXDRIVES	40
#define AMR_40LD_MAXCHAN	16
#define AMR_40LD_MAXTARG	16
#define AMR_40LD_MAXPHYSDRIVES	256

/*
 * Product Info structure
 */
struct amr_prodinfo
{
    u_int32_t	ap_size;		/* current size in bytes (not including resvd) */
    u_int32_t	ap_configsig;		/* default is 0x00282008, indicating 0x28 maximum
					 * logical drives, 0x20 maximum stripes and 0x08
					 * maximum spans */
    u_int8_t	ap_firmware[16];	/* printable identifiers */
    u_int8_t	ap_bios[16];
    u_int8_t	ap_product[80];
    u_int8_t	ap_maxio;		/* maximum number of concurrent commands supported */
    u_int8_t	ap_nschan;		/* number of SCSI channels present */
    u_int8_t	ap_fcloops;		/* number of fibre loops present */
    u_int8_t	ap_memtype;		/* memory type */
    u_int32_t	ap_signature;
    u_int16_t	ap_memsize;		/* onboard memory in MB */
    u_int16_t	ap_subsystem;		/* subsystem identifier */
    u_int16_t	ap_subvendor;		/* subsystem vendor ID */
    u_int8_t	ap_numnotifyctr;	/* number of notify counters */
} __packed;

/*
 * Notify structure
 */
struct amr_notify
{
    u_int32_t	an_globalcounter;	/* change counter */

    u_int8_t	an_paramcounter;	/* parameter change counter */
    u_int8_t	an_paramid;
#define AMR_PARAM_REBUILD_RATE		0x01	/* value = new rebuild rate */
#define AMR_PARAM_FLUSH_INTERVAL	0x02	/* value = new flush interval */
#define AMR_PARAM_SENSE_ALERT		0x03	/* value = last physical drive with check condition set */
#define AMR_PARAM_DRIVE_INSERTED	0x04	/* value = last physical drive inserted */
#define AMR_PARAM_BATTERY_STATUS	0x05	/* value = battery status */
    u_int16_t	an_paramval;

    u_int8_t	an_writeconfigcounter;	/* write config occurred */
    u_int8_t	res1[3];

    u_int8_t	an_ldrvopcounter;	/* logical drive operation started/completed */
    u_int8_t	an_ldrvopid;
    u_int8_t	an_ldrvopcmd;
#define AMR_LDRVOP_CHECK	0x01
#define AMR_LDRVOP_INIT		0x02
#define AMR_LDRVOP_REBUILD	0x03
    u_int8_t	an_ldrvopstatus;
#define AMR_LDRVOP_SUCCESS	0x00
#define AMR_LDRVOP_FAILED	0x01
#define AMR_LDRVOP_ABORTED	0x02
#define AMR_LDRVOP_CORRECTED	0x03
#define AMR_LDRVOP_STARTED	0x04

    u_int8_t	an_ldrvstatecounter;	/* logical drive state change occurred */
    u_int8_t	an_ldrvstateid;
    u_int8_t	an_ldrvstatenew;
    u_int8_t	an_ldrvstateold;

    u_int8_t	an_pdrvstatecounter;	/* physical drive state change occurred */
    u_int8_t	an_pdrvstateid;
    u_int8_t	an_pdrvstatenew;
    u_int8_t	an_pdrvstateold;

    u_int8_t	an_pdrvfmtcounter;
    u_int8_t	an_pdrvfmtid;
    u_int8_t	an_pdrvfmtval;
#define AMR_FORMAT_START	0x01
#define AMR_FORMAT_COMPLETE	0x02
    u_int8_t	res2;

    u_int8_t	an_targxfercounter;	/* scsi xfer rate change */
    u_int8_t	an_targxferid;
    u_int8_t	an_targxferval;
    u_int8_t	res3;

    u_int8_t	an_fcloopidcounter;	/* FC/AL loop ID changed */
    u_int8_t	an_fcloopidpdrvid;
    u_int8_t	an_fcloopid0;
    u_int8_t	an_fcloopid1;

    u_int8_t	an_fcloopstatecounter;	/* FC/AL loop status changed */
    u_int8_t	an_fcloopstate0;
    u_int8_t	an_fcloopstate1;
    u_int8_t	res4;
} __packed;

/*
 * Enquiry3 structure
 */
struct amr_enquiry3
{
    u_int32_t	ae_datasize;		/* valid data size in this structure */
    union {				/* event notify structure */
	struct amr_notify	n;
	u_int8_t		pad[0x80];
    } 		ae_notify;
    u_int8_t	ae_rebuildrate;		/* current rebuild rate in % */
    u_int8_t	ae_cacheflush;		/* flush interval in seconds */
    u_int8_t	ae_sensealert;
    u_int8_t	ae_driveinsertcount;	/* count of inserted drives */
    u_int8_t	ae_batterystatus;
    u_int8_t	ae_numldrives;
    u_int8_t	ae_reconstate[AMR_40LD_MAXDRIVES / 8];	/* reconstruction state */
    u_int16_t	ae_opstatus[AMR_40LD_MAXDRIVES / 8];	/* operation status per drive */
    u_int32_t	ae_drivesize[AMR_40LD_MAXDRIVES];	/* logical drive size */
    u_int8_t	ae_driveprop[AMR_40LD_MAXDRIVES];	/* logical drive properties */
    u_int8_t	ae_drivestate[AMR_40LD_MAXDRIVES];	/* logical drive state */
    u_int8_t	ae_pdrivestate[AMR_40LD_MAXPHYSDRIVES];	/* physical drive state */
    u_int16_t	ae_pdriveformat[AMR_40LD_MAXPHYSDRIVES / 16];
    u_int8_t	ae_targxfer[80];			/* physical drive transfer rates */

    u_int8_t	res1[263];		/* pad to 1024 bytes */
} __packed;


/********************************************************************************
 ********************************************************************************
                                                   Mailbox and Command Structures
 ********************************************************************************
 ********************************************************************************/

#define AMR_MBOX_CMDSIZE	0x10	/* portion worth copying for controller */

struct amr_mailbox
{
    u_int8_t	mb_command;
    u_int8_t	mb_ident;
    u_int16_t	mb_blkcount;		/* u_int8_t opcode */
					/* u_int8_t subopcode */
    u_int32_t	mb_lba;
    u_int32_t	mb_physaddr;
    u_int8_t	mb_drive;
    u_int8_t	mb_nsgelem;		/* u_int8_t rserv[0] */
    u_int8_t	res1;			/* u_int8_t rserv[1] */
    u_int8_t	mb_busy;		/* u_int8_t rserv[2] */
    u_int8_t	mb_nstatus;
    u_int8_t	mb_status;
    u_int8_t	mb_completed[46];
    u_int8_t	mb_poll;
    u_int8_t	mb_ack;
    u_int8_t	res2[16];
} __packed;

struct amr_mailbox64
{
    u_int8_t	pad[8];		/* Needed for alignment */
    u_int32_t	sg64_lo;	/* S/G pointer for 64-bit commands */
    u_int32_t	sg64_hi;	/* S/G pointer for 64-bit commands */
    struct amr_mailbox	mb;
} __packed;

struct amr_mailbox_ioctl
{
    u_int8_t	mb_command;
    u_int8_t	mb_ident;
    u_int8_t	mb_channel;
    u_int8_t	mb_param;
    u_int8_t	mb_pad[4];
    u_int32_t	mb_physaddr;
    u_int8_t	mb_drive;
    u_int8_t	mb_nsgelem;
    u_int8_t	res1;
    u_int8_t	mb_busy;
    u_int8_t	mb_nstatus;
    u_int8_t	mb_completed[46];
    u_int8_t	mb_poll;
    u_int8_t	mb_ack;
    u_int8_t	res4[16];
} __packed;

struct amr_sgentry
{
    u_int32_t	sg_addr;
    u_int32_t	sg_count;
} __packed;

struct amr_sg64entry
{
    u_int64_t	sg_addr;
    u_int32_t	sg_count;
} __packed;

struct amr_passthrough
{
    u_int8_t	ap_timeout:3;
    u_int8_t	ap_ars:1;
    u_int8_t	ap_dummy:3;
    u_int8_t	ap_islogical:1;
    u_int8_t	ap_logical_drive_no;
    u_int8_t	ap_channel;
    u_int8_t	ap_scsi_id;
    u_int8_t	ap_queue_tag;
    u_int8_t	ap_queue_action;
    u_int8_t	ap_cdb[AMR_MAX_CDB_LEN];
    u_int8_t	ap_cdb_length;
    u_int8_t	ap_request_sense_length;
    u_int8_t	ap_request_sense_area[AMR_MAX_REQ_SENSE_LEN];
    u_int8_t	ap_no_sg_elements;
    u_int8_t	ap_scsi_status;
    u_int32_t	ap_data_transfer_address;
    u_int32_t	ap_data_transfer_length;
} __packed;

struct amr_ext_passthrough
{
    u_int8_t	ap_timeout:3;
    u_int8_t	ap_ars:1;
    u_int8_t	ap_rsvd1:1;
    u_int8_t	ap_cd_rom:1;
    u_int8_t	ap_rsvd2:1;
    u_int8_t	ap_islogical:1;
    u_int8_t	ap_logical_drive_no;
    u_int8_t	ap_channel;
    u_int8_t	ap_scsi_id;
    u_int8_t	ap_queue_tag;
    u_int8_t	ap_queue_action;
    u_int8_t	ap_cdb_length;
    u_int8_t	ap_rsvd3;
    u_int8_t	ap_cdb[AMR_MAX_EXTCDB_LEN];
    u_int8_t	ap_no_sg_elements;
    u_int8_t	ap_scsi_status;
    u_int8_t	ap_request_sense_length;
    u_int8_t	ap_request_sense_area[AMR_MAX_REQ_SENSE_LEN];
    u_int8_t	ap_rsvd4;
    u_int32_t	ap_data_transfer_address;
    u_int32_t	ap_data_transfer_length;
} __packed;

struct amr_linux_ioctl {
    u_int32_t	inlen;
    u_int32_t	outlen;
    union {
	u_int8_t	fca[16];
	struct {
	    u_int8_t	opcode;
	    u_int8_t	subopcode;
	    u_int16_t	adapno;
	    u_int32_t	buffer;
	    u_int8_t	pad[4];
	    u_int32_t	length;
	} __packed fcs;
    } __packed ui;
    u_int8_t	mbox[18];
    struct amr_passthrough	pthru;
    u_int32_t	data;
    u_int8_t	pad[4];
} __packed;

#ifdef _KERNEL
/********************************************************************************
 ********************************************************************************
                                               "Quartz" i960 PCI bridge interface
 ********************************************************************************
 ********************************************************************************/

#define AMR_CFG_SIG		0xa0		/* PCI config register for signature */
#define AMR_SIGNATURE_1		0xCCCC		/* i960 signature (older adapters) */
#define AMR_SIGNATURE_2		0x3344		/* i960 signature (newer adapters) */

/*
 * Doorbell registers
 */
#define AMR_QIDB		0x20
#define AMR_QODB		0x2c
#define AMR_QIDB_SUBMIT		0x00000001	/* mailbox ready for work */
#define AMR_QIDB_ACK		0x00000002	/* mailbox done */
#define AMR_QODB_READY		0x10001234	/* work ready to be processed */

/*
 * Initialisation status
 */
#define AMR_QINIT_SCAN		0x01	/* init scanning drives */
#define AMR_QINIT_SCANINIT	0x02	/* init scanning initialising */
#define AMR_QINIT_FIRMWARE	0x03	/* init firmware initing */
#define AMR_QINIT_INPROG	0xdc	/* init in progress */
#define AMR_QINIT_SPINUP	0x2c	/* init spinning drives */
#define AMR_QINIT_NOMEM		0xac	/* insufficient memory */
#define AMR_QINIT_CACHEFLUSH	0xbc	/* init flushing cache */
#define AMR_QINIT_DONE		0x9c	/* init successfully done */

/*
 * I/O primitives
 */
#define AMR_QPUT_IDB(sc, val)	bus_space_write_4(sc->amr_btag, sc->amr_bhandle, AMR_QIDB, val)
#define AMR_QGET_IDB(sc)	bus_space_read_4 (sc->amr_btag, sc->amr_bhandle, AMR_QIDB)
#define AMR_QPUT_ODB(sc, val)	bus_space_write_4(sc->amr_btag, sc->amr_bhandle, AMR_QODB, val)
#define AMR_QGET_ODB(sc)	bus_space_read_4 (sc->amr_btag, sc->amr_bhandle, AMR_QODB)

#ifdef AMR_BOARD_INIT
#define AMR_QRESET(sc)											\
	do { 												\
	    pci_write_config((sc)->amr_dev, 0x40, pci_read_config((sc)->amr_dev, 0x40, 1) | 0x20, 1); 	\
	    pci_write_config((sc)->amr_dev, 0x64, 0x1122, 1);						\
	} while (0)
#define AMR_QGET_INITSTATUS(sc)	pci_read_config((sc)->amr_dev, 0x9c, 1)
#define AMR_QGET_INITCHAN(sc)	pci_read_config((sc)->amr_dev, 0x9f, 1)
#define AMR_QGET_INITTARG(sc)	pci_read_config((sc)->amr_dev, 0x9e, 1)
#endif

/********************************************************************************
 ********************************************************************************
                                       "Standard" old-style ASIC bridge interface
 ********************************************************************************
 ********************************************************************************/

/*
 * I/O registers
 */
#define AMR_SCMD		0x10	/* command/ack register (write) */
#define AMR_SMBOX_BUSY		0x10	/* mailbox status (read) */
#define AMR_STOGGLE		0x11	/* interrupt enable bit here */
#define AMR_SMBOX_0		0x14	/* mailbox physical address low byte */
#define AMR_SMBOX_1		0x15
#define AMR_SMBOX_2		0x16
#define AMR_SMBOX_3		0x17	/*                          high byte */
#define AMR_SMBOX_ENABLE	0x18	/* atomic mailbox address enable */
#define AMR_SINTR		0x1a	/* interrupt status */

/*
 * I/O magic numbers
 */
#define AMR_SCMD_POST		0x10	/* -> SCMD to initiate action on mailbox */
#define AMR_SCMD_ACKINTR	0x08	/* -> SCMD to ack mailbox retrieved */
#define AMR_STOGL_IENABLE	0xc0	/* in STOGGLE */
#define AMR_SINTR_VALID		0x40	/* in SINTR */
#define AMR_SMBOX_BUSYFLAG	0x10	/* in SMBOX_BUSY */
#define AMR_SMBOX_ADDR		0x00	/* -> SMBOX_ENABLE */

/*
 * Initialisation status
 */
#define AMR_SINIT_ABEND		0xee	/* init abnormal terminated */
#define AMR_SINIT_NOMEM		0xca	/* insufficient memory */
#define AMR_SINIT_CACHEFLUSH	0xbb	/* firmware flushing cache */
#define AMR_SINIT_INPROG	0x11	/* init in progress */
#define AMR_SINIT_SPINUP	0x22	/* firmware spinning drives */
#define AMR_SINIT_DONE		0x99	/* init successfully done */

/*
 * I/O primitives
 */
#define AMR_SPUT_ISTAT(sc, val)	bus_space_write_1(sc->amr_btag, sc->amr_bhandle, AMR_SINTR, val)
#define AMR_SGET_ISTAT(sc)	bus_space_read_1 (sc->amr_btag, sc->amr_bhandle, AMR_SINTR)
#define AMR_SACK_INTERRUPT(sc)	bus_space_write_1(sc->amr_btag, sc->amr_bhandle, AMR_SCMD, AMR_SCMD_ACKINTR)
#define AMR_SPOST_COMMAND(sc)	bus_space_write_1(sc->amr_btag, sc->amr_bhandle, AMR_SCMD, AMR_SCMD_POST)
#define AMR_SGET_MBSTAT(sc)	bus_space_read_1 (sc->amr_btag, sc->amr_bhandle, AMR_SMBOX_BUSY)
#define AMR_SENABLE_INTR(sc)											\
	bus_space_write_1(sc->amr_btag, sc->amr_bhandle, AMR_STOGGLE, 						\
			  bus_space_read_1(sc->amr_btag, sc->amr_bhandle, AMR_STOGGLE) | AMR_STOGL_IENABLE)
#define AMR_SDISABLE_INTR(sc)											\
	bus_space_write_1(sc->amr_btag, sc->amr_bhandle, AMR_STOGGLE, 						\
			  bus_space_read_1(sc->amr_btag, sc->amr_bhandle, AMR_STOGGLE) & ~AMR_STOGL_IENABLE)
#define AMR_SBYTE_SET(sc, reg, val)	bus_space_write_1(sc->amr_btag, sc->amr_bhandle, reg, val)

#ifdef AMR_BOARD_INIT
#define AMR_SRESET(sc)		bus_space_write_1(sc->amr_btag, sc->amr_bhandle, 0, 0x80)
#define AMR_SGET_INITSTATUS(sc)	bus_space_read_1 (sc->amr_btag, sc->amr_bhandle, AMR_SMBOX_ENABLE)
#define AMR_SGET_FAILDRIVE(sc)	bus_space_read_1 (sc->amr_btag, sc->amr_bhandle, AMR_SMBOX_ENABLE + 1)
#define AMR_SGET_INITCHAN(sc)	bus_space_read_1 (sc->amr_btag, sc->amr_bhandle, AMR_SMBOX_ENABLE + 2)
#define AMR_SGET_INITTARG(sc)	bus_space_read_1 (sc->amr_btag, sc->amr_bhandle, AMR_SMBOX_ENABLE + 3)
#endif

#endif /* _KERNEL */

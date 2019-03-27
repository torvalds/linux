/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2009 Silicon Graphics International Corp.
 * Copyright (c) 2012 The FreeBSD Foundation
 * Copyright (c) 2014-2017 Alexander Motin <mav@FreeBSD.org>
 * Copyright (c) 2017 Jakub Wojciech Klama <jceel@FreeBSD.org>
 * Copyright (c) 2018 Marcelo Araujo <araujo@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed by Edward Tomasz Napierala
 * under sponsorship from the FreeBSD Foundation.
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
 * $Id$
 */
/*
 * CAM Target Layer, a SCSI device emulation subsystem.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ctype.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/kthread.h>
#include <sys/bio.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/ioccom.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/endian.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/nv.h>
#include <sys/dnv.h>
#include <vm/uma.h>

#include <cam/cam.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_cd.h>
#include <cam/scsi/scsi_da.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_util.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_ha.h>
#include <cam/ctl/ctl_private.h>
#include <cam/ctl/ctl_debug.h>
#include <cam/ctl/ctl_scsi_all.h>
#include <cam/ctl/ctl_error.h>

struct ctl_softc *control_softc = NULL;

/*
 * Template mode pages.
 */

/*
 * Note that these are default values only.  The actual values will be
 * filled in when the user does a mode sense.
 */
const static struct scsi_da_rw_recovery_page rw_er_page_default = {
	/*page_code*/SMS_RW_ERROR_RECOVERY_PAGE,
	/*page_length*/sizeof(struct scsi_da_rw_recovery_page) - 2,
	/*byte3*/SMS_RWER_AWRE|SMS_RWER_ARRE,
	/*read_retry_count*/0,
	/*correction_span*/0,
	/*head_offset_count*/0,
	/*data_strobe_offset_cnt*/0,
	/*byte8*/SMS_RWER_LBPERE,
	/*write_retry_count*/0,
	/*reserved2*/0,
	/*recovery_time_limit*/{0, 0},
};

const static struct scsi_da_rw_recovery_page rw_er_page_changeable = {
	/*page_code*/SMS_RW_ERROR_RECOVERY_PAGE,
	/*page_length*/sizeof(struct scsi_da_rw_recovery_page) - 2,
	/*byte3*/SMS_RWER_PER,
	/*read_retry_count*/0,
	/*correction_span*/0,
	/*head_offset_count*/0,
	/*data_strobe_offset_cnt*/0,
	/*byte8*/SMS_RWER_LBPERE,
	/*write_retry_count*/0,
	/*reserved2*/0,
	/*recovery_time_limit*/{0, 0},
};

const static struct scsi_format_page format_page_default = {
	/*page_code*/SMS_FORMAT_DEVICE_PAGE,
	/*page_length*/sizeof(struct scsi_format_page) - 2,
	/*tracks_per_zone*/ {0, 0},
	/*alt_sectors_per_zone*/ {0, 0},
	/*alt_tracks_per_zone*/ {0, 0},
	/*alt_tracks_per_lun*/ {0, 0},
	/*sectors_per_track*/ {(CTL_DEFAULT_SECTORS_PER_TRACK >> 8) & 0xff,
			        CTL_DEFAULT_SECTORS_PER_TRACK & 0xff},
	/*bytes_per_sector*/ {0, 0},
	/*interleave*/ {0, 0},
	/*track_skew*/ {0, 0},
	/*cylinder_skew*/ {0, 0},
	/*flags*/ SFP_HSEC,
	/*reserved*/ {0, 0, 0}
};

const static struct scsi_format_page format_page_changeable = {
	/*page_code*/SMS_FORMAT_DEVICE_PAGE,
	/*page_length*/sizeof(struct scsi_format_page) - 2,
	/*tracks_per_zone*/ {0, 0},
	/*alt_sectors_per_zone*/ {0, 0},
	/*alt_tracks_per_zone*/ {0, 0},
	/*alt_tracks_per_lun*/ {0, 0},
	/*sectors_per_track*/ {0, 0},
	/*bytes_per_sector*/ {0, 0},
	/*interleave*/ {0, 0},
	/*track_skew*/ {0, 0},
	/*cylinder_skew*/ {0, 0},
	/*flags*/ 0,
	/*reserved*/ {0, 0, 0}
};

const static struct scsi_rigid_disk_page rigid_disk_page_default = {
	/*page_code*/SMS_RIGID_DISK_PAGE,
	/*page_length*/sizeof(struct scsi_rigid_disk_page) - 2,
	/*cylinders*/ {0, 0, 0},
	/*heads*/ CTL_DEFAULT_HEADS,
	/*start_write_precomp*/ {0, 0, 0},
	/*start_reduced_current*/ {0, 0, 0},
	/*step_rate*/ {0, 0},
	/*landing_zone_cylinder*/ {0, 0, 0},
	/*rpl*/ SRDP_RPL_DISABLED,
	/*rotational_offset*/ 0,
	/*reserved1*/ 0,
	/*rotation_rate*/ {(CTL_DEFAULT_ROTATION_RATE >> 8) & 0xff,
			   CTL_DEFAULT_ROTATION_RATE & 0xff},
	/*reserved2*/ {0, 0}
};

const static struct scsi_rigid_disk_page rigid_disk_page_changeable = {
	/*page_code*/SMS_RIGID_DISK_PAGE,
	/*page_length*/sizeof(struct scsi_rigid_disk_page) - 2,
	/*cylinders*/ {0, 0, 0},
	/*heads*/ 0,
	/*start_write_precomp*/ {0, 0, 0},
	/*start_reduced_current*/ {0, 0, 0},
	/*step_rate*/ {0, 0},
	/*landing_zone_cylinder*/ {0, 0, 0},
	/*rpl*/ 0,
	/*rotational_offset*/ 0,
	/*reserved1*/ 0,
	/*rotation_rate*/ {0, 0},
	/*reserved2*/ {0, 0}
};

const static struct scsi_da_verify_recovery_page verify_er_page_default = {
	/*page_code*/SMS_VERIFY_ERROR_RECOVERY_PAGE,
	/*page_length*/sizeof(struct scsi_da_verify_recovery_page) - 2,
	/*byte3*/0,
	/*read_retry_count*/0,
	/*reserved*/{ 0, 0, 0, 0, 0, 0 },
	/*recovery_time_limit*/{0, 0},
};

const static struct scsi_da_verify_recovery_page verify_er_page_changeable = {
	/*page_code*/SMS_VERIFY_ERROR_RECOVERY_PAGE,
	/*page_length*/sizeof(struct scsi_da_verify_recovery_page) - 2,
	/*byte3*/SMS_VER_PER,
	/*read_retry_count*/0,
	/*reserved*/{ 0, 0, 0, 0, 0, 0 },
	/*recovery_time_limit*/{0, 0},
};

const static struct scsi_caching_page caching_page_default = {
	/*page_code*/SMS_CACHING_PAGE,
	/*page_length*/sizeof(struct scsi_caching_page) - 2,
	/*flags1*/ SCP_DISC | SCP_WCE,
	/*ret_priority*/ 0,
	/*disable_pf_transfer_len*/ {0xff, 0xff},
	/*min_prefetch*/ {0, 0},
	/*max_prefetch*/ {0xff, 0xff},
	/*max_pf_ceiling*/ {0xff, 0xff},
	/*flags2*/ 0,
	/*cache_segments*/ 0,
	/*cache_seg_size*/ {0, 0},
	/*reserved*/ 0,
	/*non_cache_seg_size*/ {0, 0, 0}
};

const static struct scsi_caching_page caching_page_changeable = {
	/*page_code*/SMS_CACHING_PAGE,
	/*page_length*/sizeof(struct scsi_caching_page) - 2,
	/*flags1*/ SCP_WCE | SCP_RCD,
	/*ret_priority*/ 0,
	/*disable_pf_transfer_len*/ {0, 0},
	/*min_prefetch*/ {0, 0},
	/*max_prefetch*/ {0, 0},
	/*max_pf_ceiling*/ {0, 0},
	/*flags2*/ 0,
	/*cache_segments*/ 0,
	/*cache_seg_size*/ {0, 0},
	/*reserved*/ 0,
	/*non_cache_seg_size*/ {0, 0, 0}
};

const static struct scsi_control_page control_page_default = {
	/*page_code*/SMS_CONTROL_MODE_PAGE,
	/*page_length*/sizeof(struct scsi_control_page) - 2,
	/*rlec*/0,
	/*queue_flags*/SCP_QUEUE_ALG_RESTRICTED,
	/*eca_and_aen*/0,
	/*flags4*/SCP_TAS,
	/*aen_holdoff_period*/{0, 0},
	/*busy_timeout_period*/{0, 0},
	/*extended_selftest_completion_time*/{0, 0}
};

const static struct scsi_control_page control_page_changeable = {
	/*page_code*/SMS_CONTROL_MODE_PAGE,
	/*page_length*/sizeof(struct scsi_control_page) - 2,
	/*rlec*/SCP_DSENSE,
	/*queue_flags*/SCP_QUEUE_ALG_MASK | SCP_NUAR,
	/*eca_and_aen*/SCP_SWP,
	/*flags4*/0,
	/*aen_holdoff_period*/{0, 0},
	/*busy_timeout_period*/{0, 0},
	/*extended_selftest_completion_time*/{0, 0}
};

#define CTL_CEM_LEN	(sizeof(struct scsi_control_ext_page) - 4)

const static struct scsi_control_ext_page control_ext_page_default = {
	/*page_code*/SMS_CONTROL_MODE_PAGE | SMPH_SPF,
	/*subpage_code*/0x01,
	/*page_length*/{CTL_CEM_LEN >> 8, CTL_CEM_LEN},
	/*flags*/0,
	/*prio*/0,
	/*max_sense*/0
};

const static struct scsi_control_ext_page control_ext_page_changeable = {
	/*page_code*/SMS_CONTROL_MODE_PAGE | SMPH_SPF,
	/*subpage_code*/0x01,
	/*page_length*/{CTL_CEM_LEN >> 8, CTL_CEM_LEN},
	/*flags*/0,
	/*prio*/0,
	/*max_sense*/0xff
};

const static struct scsi_info_exceptions_page ie_page_default = {
	/*page_code*/SMS_INFO_EXCEPTIONS_PAGE,
	/*page_length*/sizeof(struct scsi_info_exceptions_page) - 2,
	/*info_flags*/SIEP_FLAGS_EWASC,
	/*mrie*/SIEP_MRIE_NO,
	/*interval_timer*/{0, 0, 0, 0},
	/*report_count*/{0, 0, 0, 1}
};

const static struct scsi_info_exceptions_page ie_page_changeable = {
	/*page_code*/SMS_INFO_EXCEPTIONS_PAGE,
	/*page_length*/sizeof(struct scsi_info_exceptions_page) - 2,
	/*info_flags*/SIEP_FLAGS_EWASC | SIEP_FLAGS_DEXCPT | SIEP_FLAGS_TEST |
	    SIEP_FLAGS_LOGERR,
	/*mrie*/0x0f,
	/*interval_timer*/{0xff, 0xff, 0xff, 0xff},
	/*report_count*/{0xff, 0xff, 0xff, 0xff}
};

#define CTL_LBPM_LEN	(sizeof(struct ctl_logical_block_provisioning_page) - 4)

const static struct ctl_logical_block_provisioning_page lbp_page_default = {{
	/*page_code*/SMS_INFO_EXCEPTIONS_PAGE | SMPH_SPF,
	/*subpage_code*/0x02,
	/*page_length*/{CTL_LBPM_LEN >> 8, CTL_LBPM_LEN},
	/*flags*/0,
	/*reserved*/{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/*descr*/{}},
	{{/*flags*/0,
	  /*resource*/0x01,
	  /*reserved*/{0, 0},
	  /*count*/{0, 0, 0, 0}},
	 {/*flags*/0,
	  /*resource*/0x02,
	  /*reserved*/{0, 0},
	  /*count*/{0, 0, 0, 0}},
	 {/*flags*/0,
	  /*resource*/0xf1,
	  /*reserved*/{0, 0},
	  /*count*/{0, 0, 0, 0}},
	 {/*flags*/0,
	  /*resource*/0xf2,
	  /*reserved*/{0, 0},
	  /*count*/{0, 0, 0, 0}}
	}
};

const static struct ctl_logical_block_provisioning_page lbp_page_changeable = {{
	/*page_code*/SMS_INFO_EXCEPTIONS_PAGE | SMPH_SPF,
	/*subpage_code*/0x02,
	/*page_length*/{CTL_LBPM_LEN >> 8, CTL_LBPM_LEN},
	/*flags*/SLBPP_SITUA,
	/*reserved*/{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	/*descr*/{}},
	{{/*flags*/0,
	  /*resource*/0,
	  /*reserved*/{0, 0},
	  /*count*/{0, 0, 0, 0}},
	 {/*flags*/0,
	  /*resource*/0,
	  /*reserved*/{0, 0},
	  /*count*/{0, 0, 0, 0}},
	 {/*flags*/0,
	  /*resource*/0,
	  /*reserved*/{0, 0},
	  /*count*/{0, 0, 0, 0}},
	 {/*flags*/0,
	  /*resource*/0,
	  /*reserved*/{0, 0},
	  /*count*/{0, 0, 0, 0}}
	}
};

const static struct scsi_cddvd_capabilities_page cddvd_page_default = {
	/*page_code*/SMS_CDDVD_CAPS_PAGE,
	/*page_length*/sizeof(struct scsi_cddvd_capabilities_page) - 2,
	/*caps1*/0x3f,
	/*caps2*/0x00,
	/*caps3*/0xf0,
	/*caps4*/0x00,
	/*caps5*/0x29,
	/*caps6*/0x00,
	/*obsolete*/{0, 0},
	/*nvol_levels*/{0, 0},
	/*buffer_size*/{8, 0},
	/*obsolete2*/{0, 0},
	/*reserved*/0,
	/*digital*/0,
	/*obsolete3*/0,
	/*copy_management*/0,
	/*reserved2*/0,
	/*rotation_control*/0,
	/*cur_write_speed*/0,
	/*num_speed_descr*/0,
};

const static struct scsi_cddvd_capabilities_page cddvd_page_changeable = {
	/*page_code*/SMS_CDDVD_CAPS_PAGE,
	/*page_length*/sizeof(struct scsi_cddvd_capabilities_page) - 2,
	/*caps1*/0,
	/*caps2*/0,
	/*caps3*/0,
	/*caps4*/0,
	/*caps5*/0,
	/*caps6*/0,
	/*obsolete*/{0, 0},
	/*nvol_levels*/{0, 0},
	/*buffer_size*/{0, 0},
	/*obsolete2*/{0, 0},
	/*reserved*/0,
	/*digital*/0,
	/*obsolete3*/0,
	/*copy_management*/0,
	/*reserved2*/0,
	/*rotation_control*/0,
	/*cur_write_speed*/0,
	/*num_speed_descr*/0,
};

SYSCTL_NODE(_kern_cam, OID_AUTO, ctl, CTLFLAG_RD, 0, "CAM Target Layer");
static int worker_threads = -1;
SYSCTL_INT(_kern_cam_ctl, OID_AUTO, worker_threads, CTLFLAG_RDTUN,
    &worker_threads, 1, "Number of worker threads");
static int ctl_debug = CTL_DEBUG_NONE;
SYSCTL_INT(_kern_cam_ctl, OID_AUTO, debug, CTLFLAG_RWTUN,
    &ctl_debug, 0, "Enabled debug flags");
static int ctl_lun_map_size = 1024;
SYSCTL_INT(_kern_cam_ctl, OID_AUTO, lun_map_size, CTLFLAG_RWTUN,
    &ctl_lun_map_size, 0, "Size of per-port LUN map (max LUN + 1)");
#ifdef  CTL_TIME_IO
static int ctl_time_io_secs = CTL_TIME_IO_DEFAULT_SECS;
SYSCTL_INT(_kern_cam_ctl, OID_AUTO, time_io_secs, CTLFLAG_RWTUN,
    &ctl_time_io_secs, 0, "Log requests taking more seconds");
#endif

/*
 * Maximum number of LUNs we support.  MUST be a power of 2.
 */
#define	CTL_DEFAULT_MAX_LUNS	1024
static int ctl_max_luns = CTL_DEFAULT_MAX_LUNS;
TUNABLE_INT("kern.cam.ctl.max_luns", &ctl_max_luns);
SYSCTL_INT(_kern_cam_ctl, OID_AUTO, max_luns, CTLFLAG_RDTUN,
    &ctl_max_luns, CTL_DEFAULT_MAX_LUNS, "Maximum number of LUNs");

/*
 * Maximum number of ports registered at one time.
 */
#define	CTL_DEFAULT_MAX_PORTS		256
static int ctl_max_ports = CTL_DEFAULT_MAX_PORTS;
TUNABLE_INT("kern.cam.ctl.max_ports", &ctl_max_ports);
SYSCTL_INT(_kern_cam_ctl, OID_AUTO, max_ports, CTLFLAG_RDTUN,
    &ctl_max_ports, CTL_DEFAULT_MAX_LUNS, "Maximum number of ports");

/*
 * Maximum number of initiators we support.
 */
#define	CTL_MAX_INITIATORS	(CTL_MAX_INIT_PER_PORT * ctl_max_ports)

/*
 * Supported pages (0x00), Serial number (0x80), Device ID (0x83),
 * Extended INQUIRY Data (0x86), Mode Page Policy (0x87),
 * SCSI Ports (0x88), Third-party Copy (0x8F), Block limits (0xB0),
 * Block Device Characteristics (0xB1) and Logical Block Provisioning (0xB2)
 */
#define SCSI_EVPD_NUM_SUPPORTED_PAGES	10

static void ctl_isc_event_handler(ctl_ha_channel chanel, ctl_ha_event event,
				  int param);
static void ctl_copy_sense_data(union ctl_ha_msg *src, union ctl_io *dest);
static void ctl_copy_sense_data_back(union ctl_io *src, union ctl_ha_msg *dest);
static int ctl_init(void);
static int ctl_shutdown(void);
static int ctl_open(struct cdev *dev, int flags, int fmt, struct thread *td);
static int ctl_close(struct cdev *dev, int flags, int fmt, struct thread *td);
static void ctl_serialize_other_sc_cmd(struct ctl_scsiio *ctsio);
static void ctl_ioctl_fill_ooa(struct ctl_lun *lun, uint32_t *cur_fill_num,
			      struct ctl_ooa *ooa_hdr,
			      struct ctl_ooa_entry *kern_entries);
static int ctl_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
		     struct thread *td);
static int ctl_alloc_lun(struct ctl_softc *ctl_softc, struct ctl_lun *lun,
			 struct ctl_be_lun *be_lun);
static int ctl_free_lun(struct ctl_lun *lun);
static void ctl_create_lun(struct ctl_be_lun *be_lun);

static int ctl_do_mode_select(union ctl_io *io);
static int ctl_pro_preempt(struct ctl_softc *softc, struct ctl_lun *lun,
			   uint64_t res_key, uint64_t sa_res_key,
			   uint8_t type, uint32_t residx,
			   struct ctl_scsiio *ctsio,
			   struct scsi_per_res_out *cdb,
			   struct scsi_per_res_out_parms* param);
static void ctl_pro_preempt_other(struct ctl_lun *lun,
				  union ctl_ha_msg *msg);
static void ctl_hndl_per_res_out_on_other_sc(union ctl_io *io);
static int ctl_inquiry_evpd_supported(struct ctl_scsiio *ctsio, int alloc_len);
static int ctl_inquiry_evpd_serial(struct ctl_scsiio *ctsio, int alloc_len);
static int ctl_inquiry_evpd_devid(struct ctl_scsiio *ctsio, int alloc_len);
static int ctl_inquiry_evpd_eid(struct ctl_scsiio *ctsio, int alloc_len);
static int ctl_inquiry_evpd_mpp(struct ctl_scsiio *ctsio, int alloc_len);
static int ctl_inquiry_evpd_scsi_ports(struct ctl_scsiio *ctsio,
					 int alloc_len);
static int ctl_inquiry_evpd_block_limits(struct ctl_scsiio *ctsio,
					 int alloc_len);
static int ctl_inquiry_evpd_bdc(struct ctl_scsiio *ctsio, int alloc_len);
static int ctl_inquiry_evpd_lbp(struct ctl_scsiio *ctsio, int alloc_len);
static int ctl_inquiry_evpd(struct ctl_scsiio *ctsio);
static int ctl_inquiry_std(struct ctl_scsiio *ctsio);
static int ctl_get_lba_len(union ctl_io *io, uint64_t *lba, uint64_t *len);
static ctl_action ctl_extent_check(union ctl_io *io1, union ctl_io *io2,
    bool seq);
static ctl_action ctl_extent_check_seq(union ctl_io *io1, union ctl_io *io2);
static ctl_action ctl_check_for_blockage(struct ctl_lun *lun,
    union ctl_io *pending_io, union ctl_io *ooa_io);
static ctl_action ctl_check_ooa(struct ctl_lun *lun, union ctl_io *pending_io,
				union ctl_io **starting_io);
static void ctl_try_unblock_io(struct ctl_lun *lun, union ctl_io *io,
    bool skip);
static void ctl_try_unblock_others(struct ctl_lun *lun, union ctl_io *io,
    bool skip);
static int ctl_scsiio_lun_check(struct ctl_lun *lun,
				const struct ctl_cmd_entry *entry,
				struct ctl_scsiio *ctsio);
static void ctl_failover_lun(union ctl_io *io);
static int ctl_scsiio_precheck(struct ctl_softc *ctl_softc,
			       struct ctl_scsiio *ctsio);
static int ctl_scsiio(struct ctl_scsiio *ctsio);

static int ctl_target_reset(union ctl_io *io);
static void ctl_do_lun_reset(struct ctl_lun *lun, uint32_t initidx,
			 ctl_ua_type ua_type);
static int ctl_lun_reset(union ctl_io *io);
static int ctl_abort_task(union ctl_io *io);
static int ctl_abort_task_set(union ctl_io *io);
static int ctl_query_task(union ctl_io *io, int task_set);
static void ctl_i_t_nexus_loss(struct ctl_softc *softc, uint32_t initidx,
			      ctl_ua_type ua_type);
static int ctl_i_t_nexus_reset(union ctl_io *io);
static int ctl_query_async_event(union ctl_io *io);
static void ctl_run_task(union ctl_io *io);
#ifdef CTL_IO_DELAY
static void ctl_datamove_timer_wakeup(void *arg);
static void ctl_done_timer_wakeup(void *arg);
#endif /* CTL_IO_DELAY */

static void ctl_send_datamove_done(union ctl_io *io, int have_lock);
static void ctl_datamove_remote_write_cb(struct ctl_ha_dt_req *rq);
static int ctl_datamove_remote_dm_write_cb(union ctl_io *io);
static void ctl_datamove_remote_write(union ctl_io *io);
static int ctl_datamove_remote_dm_read_cb(union ctl_io *io);
static void ctl_datamove_remote_read_cb(struct ctl_ha_dt_req *rq);
static int ctl_datamove_remote_sgl_setup(union ctl_io *io);
static int ctl_datamove_remote_xfer(union ctl_io *io, unsigned command,
				    ctl_ha_dt_cb callback);
static void ctl_datamove_remote_read(union ctl_io *io);
static void ctl_datamove_remote(union ctl_io *io);
static void ctl_process_done(union ctl_io *io);
static void ctl_lun_thread(void *arg);
static void ctl_thresh_thread(void *arg);
static void ctl_work_thread(void *arg);
static void ctl_enqueue_incoming(union ctl_io *io);
static void ctl_enqueue_rtr(union ctl_io *io);
static void ctl_enqueue_done(union ctl_io *io);
static void ctl_enqueue_isc(union ctl_io *io);
static const struct ctl_cmd_entry *
    ctl_get_cmd_entry(struct ctl_scsiio *ctsio, int *sa);
static const struct ctl_cmd_entry *
    ctl_validate_command(struct ctl_scsiio *ctsio);
static int ctl_cmd_applicable(uint8_t lun_type,
    const struct ctl_cmd_entry *entry);
static int ctl_ha_init(void);
static int ctl_ha_shutdown(void);

static uint64_t ctl_get_prkey(struct ctl_lun *lun, uint32_t residx);
static void ctl_clr_prkey(struct ctl_lun *lun, uint32_t residx);
static void ctl_alloc_prkey(struct ctl_lun *lun, uint32_t residx);
static void ctl_set_prkey(struct ctl_lun *lun, uint32_t residx, uint64_t key);

/*
 * Load the serialization table.  This isn't very pretty, but is probably
 * the easiest way to do it.
 */
#include "ctl_ser_table.c"

/*
 * We only need to define open, close and ioctl routines for this driver.
 */
static struct cdevsw ctl_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_open =	ctl_open,
	.d_close =	ctl_close,
	.d_ioctl =	ctl_ioctl,
	.d_name =	"ctl",
};


MALLOC_DEFINE(M_CTL, "ctlmem", "Memory used for CTL");

static int ctl_module_event_handler(module_t, int /*modeventtype_t*/, void *);

static moduledata_t ctl_moduledata = {
	"ctl",
	ctl_module_event_handler,
	NULL
};

DECLARE_MODULE(ctl, ctl_moduledata, SI_SUB_CONFIGURE, SI_ORDER_THIRD);
MODULE_VERSION(ctl, 1);

static struct ctl_frontend ha_frontend =
{
	.name = "ha",
	.init = ctl_ha_init,
	.shutdown = ctl_ha_shutdown,
};

static int
ctl_ha_init(void)
{
	struct ctl_softc *softc = control_softc;

	if (ctl_pool_create(softc, "othersc", CTL_POOL_ENTRIES_OTHER_SC,
	                    &softc->othersc_pool) != 0)
		return (ENOMEM);
	if (ctl_ha_msg_init(softc) != CTL_HA_STATUS_SUCCESS) {
		ctl_pool_free(softc->othersc_pool);
		return (EIO);
	}
	if (ctl_ha_msg_register(CTL_HA_CHAN_CTL, ctl_isc_event_handler)
	    != CTL_HA_STATUS_SUCCESS) {
		ctl_ha_msg_destroy(softc);
		ctl_pool_free(softc->othersc_pool);
		return (EIO);
	}
	return (0);
};

static int
ctl_ha_shutdown(void)
{
	struct ctl_softc *softc = control_softc;
	struct ctl_port *port;

	ctl_ha_msg_shutdown(softc);
	if (ctl_ha_msg_deregister(CTL_HA_CHAN_CTL) != CTL_HA_STATUS_SUCCESS)
		return (EIO);
	if (ctl_ha_msg_destroy(softc) != CTL_HA_STATUS_SUCCESS)
		return (EIO);
	ctl_pool_free(softc->othersc_pool);
	while ((port = STAILQ_FIRST(&ha_frontend.port_list)) != NULL) {
		ctl_port_deregister(port);
		free(port->port_name, M_CTL);
		free(port, M_CTL);
	}
	return (0);
};

static void
ctl_ha_datamove(union ctl_io *io)
{
	struct ctl_lun *lun = CTL_LUN(io);
	struct ctl_sg_entry *sgl;
	union ctl_ha_msg msg;
	uint32_t sg_entries_sent;
	int do_sg_copy, i, j;

	memset(&msg.dt, 0, sizeof(msg.dt));
	msg.hdr.msg_type = CTL_MSG_DATAMOVE;
	msg.hdr.original_sc = io->io_hdr.remote_io;
	msg.hdr.serializing_sc = io;
	msg.hdr.nexus = io->io_hdr.nexus;
	msg.hdr.status = io->io_hdr.status;
	msg.dt.flags = io->io_hdr.flags;

	/*
	 * We convert everything into a S/G list here.  We can't
	 * pass by reference, only by value between controllers.
	 * So we can't pass a pointer to the S/G list, only as many
	 * S/G entries as we can fit in here.  If it's possible for
	 * us to get more than CTL_HA_MAX_SG_ENTRIES S/G entries,
	 * then we need to break this up into multiple transfers.
	 */
	if (io->scsiio.kern_sg_entries == 0) {
		msg.dt.kern_sg_entries = 1;
#if 0
		if (io->io_hdr.flags & CTL_FLAG_BUS_ADDR) {
			msg.dt.sg_list[0].addr = io->scsiio.kern_data_ptr;
		} else {
			/* XXX KDM use busdma here! */
			msg.dt.sg_list[0].addr =
			    (void *)vtophys(io->scsiio.kern_data_ptr);
		}
#else
		KASSERT((io->io_hdr.flags & CTL_FLAG_BUS_ADDR) == 0,
		    ("HA does not support BUS_ADDR"));
		msg.dt.sg_list[0].addr = io->scsiio.kern_data_ptr;
#endif
		msg.dt.sg_list[0].len = io->scsiio.kern_data_len;
		do_sg_copy = 0;
	} else {
		msg.dt.kern_sg_entries = io->scsiio.kern_sg_entries;
		do_sg_copy = 1;
	}

	msg.dt.kern_data_len = io->scsiio.kern_data_len;
	msg.dt.kern_total_len = io->scsiio.kern_total_len;
	msg.dt.kern_data_resid = io->scsiio.kern_data_resid;
	msg.dt.kern_rel_offset = io->scsiio.kern_rel_offset;
	msg.dt.sg_sequence = 0;

	/*
	 * Loop until we've sent all of the S/G entries.  On the
	 * other end, we'll recompose these S/G entries into one
	 * contiguous list before processing.
	 */
	for (sg_entries_sent = 0; sg_entries_sent < msg.dt.kern_sg_entries;
	    msg.dt.sg_sequence++) {
		msg.dt.cur_sg_entries = MIN((sizeof(msg.dt.sg_list) /
		    sizeof(msg.dt.sg_list[0])),
		    msg.dt.kern_sg_entries - sg_entries_sent);
		if (do_sg_copy != 0) {
			sgl = (struct ctl_sg_entry *)io->scsiio.kern_data_ptr;
			for (i = sg_entries_sent, j = 0;
			     i < msg.dt.cur_sg_entries; i++, j++) {
#if 0
				if (io->io_hdr.flags & CTL_FLAG_BUS_ADDR) {
					msg.dt.sg_list[j].addr = sgl[i].addr;
				} else {
					/* XXX KDM use busdma here! */
					msg.dt.sg_list[j].addr =
					    (void *)vtophys(sgl[i].addr);
				}
#else
				KASSERT((io->io_hdr.flags &
				    CTL_FLAG_BUS_ADDR) == 0,
				    ("HA does not support BUS_ADDR"));
				msg.dt.sg_list[j].addr = sgl[i].addr;
#endif
				msg.dt.sg_list[j].len = sgl[i].len;
			}
		}

		sg_entries_sent += msg.dt.cur_sg_entries;
		msg.dt.sg_last = (sg_entries_sent >= msg.dt.kern_sg_entries);
		if (ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg,
		    sizeof(msg.dt) - sizeof(msg.dt.sg_list) +
		    sizeof(struct ctl_sg_entry) * msg.dt.cur_sg_entries,
		    M_WAITOK) > CTL_HA_STATUS_SUCCESS) {
			io->io_hdr.port_status = 31341;
			io->scsiio.be_move_done(io);
			return;
		}
		msg.dt.sent_sg_entries = sg_entries_sent;
	}

	/*
	 * Officially handover the request from us to peer.
	 * If failover has just happened, then we must return error.
	 * If failover happen just after, then it is not our problem.
	 */
	if (lun)
		mtx_lock(&lun->lun_lock);
	if (io->io_hdr.flags & CTL_FLAG_FAILOVER) {
		if (lun)
			mtx_unlock(&lun->lun_lock);
		io->io_hdr.port_status = 31342;
		io->scsiio.be_move_done(io);
		return;
	}
	io->io_hdr.flags &= ~CTL_FLAG_IO_ACTIVE;
	io->io_hdr.flags |= CTL_FLAG_DMA_INPROG;
	if (lun)
		mtx_unlock(&lun->lun_lock);
}

static void
ctl_ha_done(union ctl_io *io)
{
	union ctl_ha_msg msg;

	if (io->io_hdr.io_type == CTL_IO_SCSI) {
		memset(&msg, 0, sizeof(msg));
		msg.hdr.msg_type = CTL_MSG_FINISH_IO;
		msg.hdr.original_sc = io->io_hdr.remote_io;
		msg.hdr.nexus = io->io_hdr.nexus;
		msg.hdr.status = io->io_hdr.status;
		msg.scsi.scsi_status = io->scsiio.scsi_status;
		msg.scsi.tag_num = io->scsiio.tag_num;
		msg.scsi.tag_type = io->scsiio.tag_type;
		msg.scsi.sense_len = io->scsiio.sense_len;
		memcpy(&msg.scsi.sense_data, &io->scsiio.sense_data,
		    io->scsiio.sense_len);
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg,
		    sizeof(msg.scsi) - sizeof(msg.scsi.sense_data) +
		    msg.scsi.sense_len, M_WAITOK);
	}
	ctl_free_io(io);
}

static void
ctl_isc_handler_finish_xfer(struct ctl_softc *ctl_softc,
			    union ctl_ha_msg *msg_info)
{
	struct ctl_scsiio *ctsio;

	if (msg_info->hdr.original_sc == NULL) {
		printf("%s: original_sc == NULL!\n", __func__);
		/* XXX KDM now what? */
		return;
	}

	ctsio = &msg_info->hdr.original_sc->scsiio;
	ctsio->io_hdr.flags |= CTL_FLAG_IO_ACTIVE;
	ctsio->io_hdr.msg_type = CTL_MSG_FINISH_IO;
	ctsio->io_hdr.status = msg_info->hdr.status;
	ctsio->scsi_status = msg_info->scsi.scsi_status;
	ctsio->sense_len = msg_info->scsi.sense_len;
	memcpy(&ctsio->sense_data, &msg_info->scsi.sense_data,
	       msg_info->scsi.sense_len);
	ctl_enqueue_isc((union ctl_io *)ctsio);
}

static void
ctl_isc_handler_finish_ser_only(struct ctl_softc *ctl_softc,
				union ctl_ha_msg *msg_info)
{
	struct ctl_scsiio *ctsio;

	if (msg_info->hdr.serializing_sc == NULL) {
		printf("%s: serializing_sc == NULL!\n", __func__);
		/* XXX KDM now what? */
		return;
	}

	ctsio = &msg_info->hdr.serializing_sc->scsiio;
	ctsio->io_hdr.msg_type = CTL_MSG_FINISH_IO;
	ctl_enqueue_isc((union ctl_io *)ctsio);
}

void
ctl_isc_announce_lun(struct ctl_lun *lun)
{
	struct ctl_softc *softc = lun->ctl_softc;
	union ctl_ha_msg *msg;
	struct ctl_ha_msg_lun_pr_key pr_key;
	int i, k;

	if (softc->ha_link != CTL_HA_LINK_ONLINE)
		return;
	mtx_lock(&lun->lun_lock);
	i = sizeof(msg->lun);
	if (lun->lun_devid)
		i += lun->lun_devid->len;
	i += sizeof(pr_key) * lun->pr_key_count;
alloc:
	mtx_unlock(&lun->lun_lock);
	msg = malloc(i, M_CTL, M_WAITOK);
	mtx_lock(&lun->lun_lock);
	k = sizeof(msg->lun);
	if (lun->lun_devid)
		k += lun->lun_devid->len;
	k += sizeof(pr_key) * lun->pr_key_count;
	if (i < k) {
		free(msg, M_CTL);
		i = k;
		goto alloc;
	}
	bzero(&msg->lun, sizeof(msg->lun));
	msg->hdr.msg_type = CTL_MSG_LUN_SYNC;
	msg->hdr.nexus.targ_lun = lun->lun;
	msg->hdr.nexus.targ_mapped_lun = lun->lun;
	msg->lun.flags = lun->flags;
	msg->lun.pr_generation = lun->pr_generation;
	msg->lun.pr_res_idx = lun->pr_res_idx;
	msg->lun.pr_res_type = lun->pr_res_type;
	msg->lun.pr_key_count = lun->pr_key_count;
	i = 0;
	if (lun->lun_devid) {
		msg->lun.lun_devid_len = lun->lun_devid->len;
		memcpy(&msg->lun.data[i], lun->lun_devid->data,
		    msg->lun.lun_devid_len);
		i += msg->lun.lun_devid_len;
	}
	for (k = 0; k < CTL_MAX_INITIATORS; k++) {
		if ((pr_key.pr_key = ctl_get_prkey(lun, k)) == 0)
			continue;
		pr_key.pr_iid = k;
		memcpy(&msg->lun.data[i], &pr_key, sizeof(pr_key));
		i += sizeof(pr_key);
	}
	mtx_unlock(&lun->lun_lock);
	ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg->port, sizeof(msg->port) + i,
	    M_WAITOK);
	free(msg, M_CTL);

	if (lun->flags & CTL_LUN_PRIMARY_SC) {
		for (i = 0; i < CTL_NUM_MODE_PAGES; i++) {
			ctl_isc_announce_mode(lun, -1,
			    lun->mode_pages.index[i].page_code & SMPH_PC_MASK,
			    lun->mode_pages.index[i].subpage);
		}
	}
}

void
ctl_isc_announce_port(struct ctl_port *port)
{
	struct ctl_softc *softc = port->ctl_softc;
	union ctl_ha_msg *msg;
	int i;

	if (port->targ_port < softc->port_min ||
	    port->targ_port >= softc->port_max ||
	    softc->ha_link != CTL_HA_LINK_ONLINE)
		return;
	i = sizeof(msg->port) + strlen(port->port_name) + 1;
	if (port->lun_map)
		i += port->lun_map_size * sizeof(uint32_t);
	if (port->port_devid)
		i += port->port_devid->len;
	if (port->target_devid)
		i += port->target_devid->len;
	if (port->init_devid)
		i += port->init_devid->len;
	msg = malloc(i, M_CTL, M_WAITOK);
	bzero(&msg->port, sizeof(msg->port));
	msg->hdr.msg_type = CTL_MSG_PORT_SYNC;
	msg->hdr.nexus.targ_port = port->targ_port;
	msg->port.port_type = port->port_type;
	msg->port.physical_port = port->physical_port;
	msg->port.virtual_port = port->virtual_port;
	msg->port.status = port->status;
	i = 0;
	msg->port.name_len = sprintf(&msg->port.data[i],
	    "%d:%s", softc->ha_id, port->port_name) + 1;
	i += msg->port.name_len;
	if (port->lun_map) {
		msg->port.lun_map_len = port->lun_map_size * sizeof(uint32_t);
		memcpy(&msg->port.data[i], port->lun_map,
		    msg->port.lun_map_len);
		i += msg->port.lun_map_len;
	}
	if (port->port_devid) {
		msg->port.port_devid_len = port->port_devid->len;
		memcpy(&msg->port.data[i], port->port_devid->data,
		    msg->port.port_devid_len);
		i += msg->port.port_devid_len;
	}
	if (port->target_devid) {
		msg->port.target_devid_len = port->target_devid->len;
		memcpy(&msg->port.data[i], port->target_devid->data,
		    msg->port.target_devid_len);
		i += msg->port.target_devid_len;
	}
	if (port->init_devid) {
		msg->port.init_devid_len = port->init_devid->len;
		memcpy(&msg->port.data[i], port->init_devid->data,
		    msg->port.init_devid_len);
		i += msg->port.init_devid_len;
	}
	ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg->port, sizeof(msg->port) + i,
	    M_WAITOK);
	free(msg, M_CTL);
}

void
ctl_isc_announce_iid(struct ctl_port *port, int iid)
{
	struct ctl_softc *softc = port->ctl_softc;
	union ctl_ha_msg *msg;
	int i, l;

	if (port->targ_port < softc->port_min ||
	    port->targ_port >= softc->port_max ||
	    softc->ha_link != CTL_HA_LINK_ONLINE)
		return;
	mtx_lock(&softc->ctl_lock);
	i = sizeof(msg->iid);
	l = 0;
	if (port->wwpn_iid[iid].name)
		l = strlen(port->wwpn_iid[iid].name) + 1;
	i += l;
	msg = malloc(i, M_CTL, M_NOWAIT);
	if (msg == NULL) {
		mtx_unlock(&softc->ctl_lock);
		return;
	}
	bzero(&msg->iid, sizeof(msg->iid));
	msg->hdr.msg_type = CTL_MSG_IID_SYNC;
	msg->hdr.nexus.targ_port = port->targ_port;
	msg->hdr.nexus.initid = iid;
	msg->iid.in_use = port->wwpn_iid[iid].in_use;
	msg->iid.name_len = l;
	msg->iid.wwpn = port->wwpn_iid[iid].wwpn;
	if (port->wwpn_iid[iid].name)
		strlcpy(msg->iid.data, port->wwpn_iid[iid].name, l);
	mtx_unlock(&softc->ctl_lock);
	ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg->iid, i, M_NOWAIT);
	free(msg, M_CTL);
}

void
ctl_isc_announce_mode(struct ctl_lun *lun, uint32_t initidx,
    uint8_t page, uint8_t subpage)
{
	struct ctl_softc *softc = lun->ctl_softc;
	union ctl_ha_msg msg;
	u_int i;

	if (softc->ha_link != CTL_HA_LINK_ONLINE)
		return;
	for (i = 0; i < CTL_NUM_MODE_PAGES; i++) {
		if ((lun->mode_pages.index[i].page_code & SMPH_PC_MASK) ==
		    page && lun->mode_pages.index[i].subpage == subpage)
			break;
	}
	if (i == CTL_NUM_MODE_PAGES)
		return;

	/* Don't try to replicate pages not present on this device. */
	if (lun->mode_pages.index[i].page_data == NULL)
		return;

	bzero(&msg.mode, sizeof(msg.mode));
	msg.hdr.msg_type = CTL_MSG_MODE_SYNC;
	msg.hdr.nexus.targ_port = initidx / CTL_MAX_INIT_PER_PORT;
	msg.hdr.nexus.initid = initidx % CTL_MAX_INIT_PER_PORT;
	msg.hdr.nexus.targ_lun = lun->lun;
	msg.hdr.nexus.targ_mapped_lun = lun->lun;
	msg.mode.page_code = page;
	msg.mode.subpage = subpage;
	msg.mode.page_len = lun->mode_pages.index[i].page_len;
	memcpy(msg.mode.data, lun->mode_pages.index[i].page_data,
	    msg.mode.page_len);
	ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg.mode, sizeof(msg.mode),
	    M_WAITOK);
}

static void
ctl_isc_ha_link_up(struct ctl_softc *softc)
{
	struct ctl_port *port;
	struct ctl_lun *lun;
	union ctl_ha_msg msg;
	int i;

	/* Announce this node parameters to peer for validation. */
	msg.login.msg_type = CTL_MSG_LOGIN;
	msg.login.version = CTL_HA_VERSION;
	msg.login.ha_mode = softc->ha_mode;
	msg.login.ha_id = softc->ha_id;
	msg.login.max_luns = ctl_max_luns;
	msg.login.max_ports = ctl_max_ports;
	msg.login.max_init_per_port = CTL_MAX_INIT_PER_PORT;
	ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg.login, sizeof(msg.login),
	    M_WAITOK);

	STAILQ_FOREACH(port, &softc->port_list, links) {
		ctl_isc_announce_port(port);
		for (i = 0; i < CTL_MAX_INIT_PER_PORT; i++) {
			if (port->wwpn_iid[i].in_use)
				ctl_isc_announce_iid(port, i);
		}
	}
	STAILQ_FOREACH(lun, &softc->lun_list, links)
		ctl_isc_announce_lun(lun);
}

static void
ctl_isc_ha_link_down(struct ctl_softc *softc)
{
	struct ctl_port *port;
	struct ctl_lun *lun;
	union ctl_io *io;
	int i;

	mtx_lock(&softc->ctl_lock);
	STAILQ_FOREACH(lun, &softc->lun_list, links) {
		mtx_lock(&lun->lun_lock);
		if (lun->flags & CTL_LUN_PEER_SC_PRIMARY) {
			lun->flags &= ~CTL_LUN_PEER_SC_PRIMARY;
			ctl_est_ua_all(lun, -1, CTL_UA_ASYM_ACC_CHANGE);
		}
		mtx_unlock(&lun->lun_lock);

		mtx_unlock(&softc->ctl_lock);
		io = ctl_alloc_io(softc->othersc_pool);
		mtx_lock(&softc->ctl_lock);
		ctl_zero_io(io);
		io->io_hdr.msg_type = CTL_MSG_FAILOVER;
		io->io_hdr.nexus.targ_mapped_lun = lun->lun;
		ctl_enqueue_isc(io);
	}

	STAILQ_FOREACH(port, &softc->port_list, links) {
		if (port->targ_port >= softc->port_min &&
		    port->targ_port < softc->port_max)
			continue;
		port->status &= ~CTL_PORT_STATUS_ONLINE;
		for (i = 0; i < CTL_MAX_INIT_PER_PORT; i++) {
			port->wwpn_iid[i].in_use = 0;
			free(port->wwpn_iid[i].name, M_CTL);
			port->wwpn_iid[i].name = NULL;
		}
	}
	mtx_unlock(&softc->ctl_lock);
}

static void
ctl_isc_ua(struct ctl_softc *softc, union ctl_ha_msg *msg, int len)
{
	struct ctl_lun *lun;
	uint32_t iid = ctl_get_initindex(&msg->hdr.nexus);

	mtx_lock(&softc->ctl_lock);
	if (msg->hdr.nexus.targ_mapped_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[msg->hdr.nexus.targ_mapped_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		return;
	}
	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);
	if (msg->ua.ua_type == CTL_UA_THIN_PROV_THRES && msg->ua.ua_set)
		memcpy(lun->ua_tpt_info, msg->ua.ua_info, 8);
	if (msg->ua.ua_all) {
		if (msg->ua.ua_set)
			ctl_est_ua_all(lun, iid, msg->ua.ua_type);
		else
			ctl_clr_ua_all(lun, iid, msg->ua.ua_type);
	} else {
		if (msg->ua.ua_set)
			ctl_est_ua(lun, iid, msg->ua.ua_type);
		else
			ctl_clr_ua(lun, iid, msg->ua.ua_type);
	}
	mtx_unlock(&lun->lun_lock);
}

static void
ctl_isc_lun_sync(struct ctl_softc *softc, union ctl_ha_msg *msg, int len)
{
	struct ctl_lun *lun;
	struct ctl_ha_msg_lun_pr_key pr_key;
	int i, k;
	ctl_lun_flags oflags;
	uint32_t targ_lun;

	targ_lun = msg->hdr.nexus.targ_mapped_lun;
	mtx_lock(&softc->ctl_lock);
	if (targ_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		return;
	}
	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);
	if (lun->flags & CTL_LUN_DISABLED) {
		mtx_unlock(&lun->lun_lock);
		return;
	}
	i = (lun->lun_devid != NULL) ? lun->lun_devid->len : 0;
	if (msg->lun.lun_devid_len != i || (i > 0 &&
	    memcmp(&msg->lun.data[0], lun->lun_devid->data, i) != 0)) {
		mtx_unlock(&lun->lun_lock);
		printf("%s: Received conflicting HA LUN %d\n",
		    __func__, targ_lun);
		return;
	} else {
		/* Record whether peer is primary. */
		oflags = lun->flags;
		if ((msg->lun.flags & CTL_LUN_PRIMARY_SC) &&
		    (msg->lun.flags & CTL_LUN_DISABLED) == 0)
			lun->flags |= CTL_LUN_PEER_SC_PRIMARY;
		else
			lun->flags &= ~CTL_LUN_PEER_SC_PRIMARY;
		if (oflags != lun->flags)
			ctl_est_ua_all(lun, -1, CTL_UA_ASYM_ACC_CHANGE);

		/* If peer is primary and we are not -- use data */
		if ((lun->flags & CTL_LUN_PRIMARY_SC) == 0 &&
		    (lun->flags & CTL_LUN_PEER_SC_PRIMARY)) {
			lun->pr_generation = msg->lun.pr_generation;
			lun->pr_res_idx = msg->lun.pr_res_idx;
			lun->pr_res_type = msg->lun.pr_res_type;
			lun->pr_key_count = msg->lun.pr_key_count;
			for (k = 0; k < CTL_MAX_INITIATORS; k++)
				ctl_clr_prkey(lun, k);
			for (k = 0; k < msg->lun.pr_key_count; k++) {
				memcpy(&pr_key, &msg->lun.data[i],
				    sizeof(pr_key));
				ctl_alloc_prkey(lun, pr_key.pr_iid);
				ctl_set_prkey(lun, pr_key.pr_iid,
				    pr_key.pr_key);
				i += sizeof(pr_key);
			}
		}

		mtx_unlock(&lun->lun_lock);
		CTL_DEBUG_PRINT(("%s: Known LUN %d, peer is %s\n",
		    __func__, targ_lun,
		    (msg->lun.flags & CTL_LUN_PRIMARY_SC) ?
		    "primary" : "secondary"));

		/* If we are primary but peer doesn't know -- notify */
		if ((lun->flags & CTL_LUN_PRIMARY_SC) &&
		    (msg->lun.flags & CTL_LUN_PEER_SC_PRIMARY) == 0)
			ctl_isc_announce_lun(lun);
	}
}

static void
ctl_isc_port_sync(struct ctl_softc *softc, union ctl_ha_msg *msg, int len)
{
	struct ctl_port *port;
	struct ctl_lun *lun;
	int i, new;

	port = softc->ctl_ports[msg->hdr.nexus.targ_port];
	if (port == NULL) {
		CTL_DEBUG_PRINT(("%s: New port %d\n", __func__,
		    msg->hdr.nexus.targ_port));
		new = 1;
		port = malloc(sizeof(*port), M_CTL, M_WAITOK | M_ZERO);
		port->frontend = &ha_frontend;
		port->targ_port = msg->hdr.nexus.targ_port;
		port->fe_datamove = ctl_ha_datamove;
		port->fe_done = ctl_ha_done;
	} else if (port->frontend == &ha_frontend) {
		CTL_DEBUG_PRINT(("%s: Updated port %d\n", __func__,
		    msg->hdr.nexus.targ_port));
		new = 0;
	} else {
		printf("%s: Received conflicting HA port %d\n",
		    __func__, msg->hdr.nexus.targ_port);
		return;
	}
	port->port_type = msg->port.port_type;
	port->physical_port = msg->port.physical_port;
	port->virtual_port = msg->port.virtual_port;
	port->status = msg->port.status;
	i = 0;
	free(port->port_name, M_CTL);
	port->port_name = strndup(&msg->port.data[i], msg->port.name_len,
	    M_CTL);
	i += msg->port.name_len;
	if (msg->port.lun_map_len != 0) {
		if (port->lun_map == NULL ||
		    port->lun_map_size * sizeof(uint32_t) <
		    msg->port.lun_map_len) {
			port->lun_map_size = 0;
			free(port->lun_map, M_CTL);
			port->lun_map = malloc(msg->port.lun_map_len,
			    M_CTL, M_WAITOK);
		}
		memcpy(port->lun_map, &msg->port.data[i], msg->port.lun_map_len);
		port->lun_map_size = msg->port.lun_map_len / sizeof(uint32_t);
		i += msg->port.lun_map_len;
	} else {
		port->lun_map_size = 0;
		free(port->lun_map, M_CTL);
		port->lun_map = NULL;
	}
	if (msg->port.port_devid_len != 0) {
		if (port->port_devid == NULL ||
		    port->port_devid->len < msg->port.port_devid_len) {
			free(port->port_devid, M_CTL);
			port->port_devid = malloc(sizeof(struct ctl_devid) +
			    msg->port.port_devid_len, M_CTL, M_WAITOK);
		}
		memcpy(port->port_devid->data, &msg->port.data[i],
		    msg->port.port_devid_len);
		port->port_devid->len = msg->port.port_devid_len;
		i += msg->port.port_devid_len;
	} else {
		free(port->port_devid, M_CTL);
		port->port_devid = NULL;
	}
	if (msg->port.target_devid_len != 0) {
		if (port->target_devid == NULL ||
		    port->target_devid->len < msg->port.target_devid_len) {
			free(port->target_devid, M_CTL);
			port->target_devid = malloc(sizeof(struct ctl_devid) +
			    msg->port.target_devid_len, M_CTL, M_WAITOK);
		}
		memcpy(port->target_devid->data, &msg->port.data[i],
		    msg->port.target_devid_len);
		port->target_devid->len = msg->port.target_devid_len;
		i += msg->port.target_devid_len;
	} else {
		free(port->target_devid, M_CTL);
		port->target_devid = NULL;
	}
	if (msg->port.init_devid_len != 0) {
		if (port->init_devid == NULL ||
		    port->init_devid->len < msg->port.init_devid_len) {
			free(port->init_devid, M_CTL);
			port->init_devid = malloc(sizeof(struct ctl_devid) +
			    msg->port.init_devid_len, M_CTL, M_WAITOK);
		}
		memcpy(port->init_devid->data, &msg->port.data[i],
		    msg->port.init_devid_len);
		port->init_devid->len = msg->port.init_devid_len;
		i += msg->port.init_devid_len;
	} else {
		free(port->init_devid, M_CTL);
		port->init_devid = NULL;
	}
	if (new) {
		if (ctl_port_register(port) != 0) {
			printf("%s: ctl_port_register() failed with error\n",
			    __func__);
		}
	}
	mtx_lock(&softc->ctl_lock);
	STAILQ_FOREACH(lun, &softc->lun_list, links) {
		if (ctl_lun_map_to_port(port, lun->lun) == UINT32_MAX)
			continue;
		mtx_lock(&lun->lun_lock);
		ctl_est_ua_all(lun, -1, CTL_UA_INQ_CHANGE);
		mtx_unlock(&lun->lun_lock);
	}
	mtx_unlock(&softc->ctl_lock);
}

static void
ctl_isc_iid_sync(struct ctl_softc *softc, union ctl_ha_msg *msg, int len)
{
	struct ctl_port *port;
	int iid;

	port = softc->ctl_ports[msg->hdr.nexus.targ_port];
	if (port == NULL) {
		printf("%s: Received IID for unknown port %d\n",
		    __func__, msg->hdr.nexus.targ_port);
		return;
	}
	iid = msg->hdr.nexus.initid;
	if (port->wwpn_iid[iid].in_use != 0 &&
	    msg->iid.in_use == 0)
		ctl_i_t_nexus_loss(softc, iid, CTL_UA_POWERON);
	port->wwpn_iid[iid].in_use = msg->iid.in_use;
	port->wwpn_iid[iid].wwpn = msg->iid.wwpn;
	free(port->wwpn_iid[iid].name, M_CTL);
	if (msg->iid.name_len) {
		port->wwpn_iid[iid].name = strndup(&msg->iid.data[0],
		    msg->iid.name_len, M_CTL);
	} else
		port->wwpn_iid[iid].name = NULL;
}

static void
ctl_isc_login(struct ctl_softc *softc, union ctl_ha_msg *msg, int len)
{

	if (msg->login.version != CTL_HA_VERSION) {
		printf("CTL HA peers have different versions %d != %d\n",
		    msg->login.version, CTL_HA_VERSION);
		ctl_ha_msg_abort(CTL_HA_CHAN_CTL);
		return;
	}
	if (msg->login.ha_mode != softc->ha_mode) {
		printf("CTL HA peers have different ha_mode %d != %d\n",
		    msg->login.ha_mode, softc->ha_mode);
		ctl_ha_msg_abort(CTL_HA_CHAN_CTL);
		return;
	}
	if (msg->login.ha_id == softc->ha_id) {
		printf("CTL HA peers have same ha_id %d\n", msg->login.ha_id);
		ctl_ha_msg_abort(CTL_HA_CHAN_CTL);
		return;
	}
	if (msg->login.max_luns != ctl_max_luns ||
	    msg->login.max_ports != ctl_max_ports ||
	    msg->login.max_init_per_port != CTL_MAX_INIT_PER_PORT) {
		printf("CTL HA peers have different limits\n");
		ctl_ha_msg_abort(CTL_HA_CHAN_CTL);
		return;
	}
}

static void
ctl_isc_mode_sync(struct ctl_softc *softc, union ctl_ha_msg *msg, int len)
{
	struct ctl_lun *lun;
	u_int i;
	uint32_t initidx, targ_lun;

	targ_lun = msg->hdr.nexus.targ_mapped_lun;
	mtx_lock(&softc->ctl_lock);
	if (targ_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		return;
	}
	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);
	if (lun->flags & CTL_LUN_DISABLED) {
		mtx_unlock(&lun->lun_lock);
		return;
	}
	for (i = 0; i < CTL_NUM_MODE_PAGES; i++) {
		if ((lun->mode_pages.index[i].page_code & SMPH_PC_MASK) ==
		    msg->mode.page_code &&
		    lun->mode_pages.index[i].subpage == msg->mode.subpage)
			break;
	}
	if (i == CTL_NUM_MODE_PAGES) {
		mtx_unlock(&lun->lun_lock);
		return;
	}
	memcpy(lun->mode_pages.index[i].page_data, msg->mode.data,
	    lun->mode_pages.index[i].page_len);
	initidx = ctl_get_initindex(&msg->hdr.nexus);
	if (initidx != -1)
		ctl_est_ua_all(lun, initidx, CTL_UA_MODE_CHANGE);
	mtx_unlock(&lun->lun_lock);
}

/*
 * ISC (Inter Shelf Communication) event handler.  Events from the HA
 * subsystem come in here.
 */
static void
ctl_isc_event_handler(ctl_ha_channel channel, ctl_ha_event event, int param)
{
	struct ctl_softc *softc = control_softc;
	union ctl_io *io;
	struct ctl_prio *presio;
	ctl_ha_status isc_status;

	CTL_DEBUG_PRINT(("CTL: Isc Msg event %d\n", event));
	if (event == CTL_HA_EVT_MSG_RECV) {
		union ctl_ha_msg *msg, msgbuf;

		if (param > sizeof(msgbuf))
			msg = malloc(param, M_CTL, M_WAITOK);
		else
			msg = &msgbuf;
		isc_status = ctl_ha_msg_recv(CTL_HA_CHAN_CTL, msg, param,
		    M_WAITOK);
		if (isc_status != CTL_HA_STATUS_SUCCESS) {
			printf("%s: Error receiving message: %d\n",
			    __func__, isc_status);
			if (msg != &msgbuf)
				free(msg, M_CTL);
			return;
		}

		CTL_DEBUG_PRINT(("CTL: msg_type %d\n", msg->msg_type));
		switch (msg->hdr.msg_type) {
		case CTL_MSG_SERIALIZE:
			io = ctl_alloc_io(softc->othersc_pool);
			ctl_zero_io(io);
			// populate ctsio from msg
			io->io_hdr.io_type = CTL_IO_SCSI;
			io->io_hdr.msg_type = CTL_MSG_SERIALIZE;
			io->io_hdr.remote_io = msg->hdr.original_sc;
			io->io_hdr.flags |= CTL_FLAG_FROM_OTHER_SC |
					    CTL_FLAG_IO_ACTIVE;
			/*
			 * If we're in serialization-only mode, we don't
			 * want to go through full done processing.  Thus
			 * the COPY flag.
			 *
			 * XXX KDM add another flag that is more specific.
			 */
			if (softc->ha_mode != CTL_HA_MODE_XFER)
				io->io_hdr.flags |= CTL_FLAG_INT_COPY;
			io->io_hdr.nexus = msg->hdr.nexus;
			io->scsiio.tag_num = msg->scsi.tag_num;
			io->scsiio.tag_type = msg->scsi.tag_type;
#ifdef CTL_TIME_IO
			io->io_hdr.start_time = time_uptime;
			getbinuptime(&io->io_hdr.start_bt);
#endif /* CTL_TIME_IO */
			io->scsiio.cdb_len = msg->scsi.cdb_len;
			memcpy(io->scsiio.cdb, msg->scsi.cdb,
			       CTL_MAX_CDBLEN);
			if (softc->ha_mode == CTL_HA_MODE_XFER) {
				const struct ctl_cmd_entry *entry;

				entry = ctl_get_cmd_entry(&io->scsiio, NULL);
				io->io_hdr.flags &= ~CTL_FLAG_DATA_MASK;
				io->io_hdr.flags |=
					entry->flags & CTL_FLAG_DATA_MASK;
			}
			ctl_enqueue_isc(io);
			break;

		/* Performed on the Originating SC, XFER mode only */
		case CTL_MSG_DATAMOVE: {
			struct ctl_sg_entry *sgl;
			int i, j;

			io = msg->hdr.original_sc;
			if (io == NULL) {
				printf("%s: original_sc == NULL!\n", __func__);
				/* XXX KDM do something here */
				break;
			}
			io->io_hdr.msg_type = CTL_MSG_DATAMOVE;
			io->io_hdr.flags |= CTL_FLAG_IO_ACTIVE;
			/*
			 * Keep track of this, we need to send it back over
			 * when the datamove is complete.
			 */
			io->io_hdr.remote_io = msg->hdr.serializing_sc;
			if (msg->hdr.status == CTL_SUCCESS)
				io->io_hdr.status = msg->hdr.status;

			if (msg->dt.sg_sequence == 0) {
#ifdef CTL_TIME_IO
				getbinuptime(&io->io_hdr.dma_start_bt);
#endif
				i = msg->dt.kern_sg_entries +
				    msg->dt.kern_data_len /
				    CTL_HA_DATAMOVE_SEGMENT + 1;
				sgl = malloc(sizeof(*sgl) * i, M_CTL,
				    M_WAITOK | M_ZERO);
				CTL_RSGL(io) = sgl;
				CTL_LSGL(io) = &sgl[msg->dt.kern_sg_entries];

				io->scsiio.kern_data_ptr = (uint8_t *)sgl;

				io->scsiio.kern_sg_entries =
					msg->dt.kern_sg_entries;
				io->scsiio.rem_sg_entries =
					msg->dt.kern_sg_entries;
				io->scsiio.kern_data_len =
					msg->dt.kern_data_len;
				io->scsiio.kern_total_len =
					msg->dt.kern_total_len;
				io->scsiio.kern_data_resid =
					msg->dt.kern_data_resid;
				io->scsiio.kern_rel_offset =
					msg->dt.kern_rel_offset;
				io->io_hdr.flags &= ~CTL_FLAG_BUS_ADDR;
				io->io_hdr.flags |= msg->dt.flags &
				    CTL_FLAG_BUS_ADDR;
			} else
				sgl = (struct ctl_sg_entry *)
					io->scsiio.kern_data_ptr;

			for (i = msg->dt.sent_sg_entries, j = 0;
			     i < (msg->dt.sent_sg_entries +
			     msg->dt.cur_sg_entries); i++, j++) {
				sgl[i].addr = msg->dt.sg_list[j].addr;
				sgl[i].len = msg->dt.sg_list[j].len;
			}

			/*
			 * If this is the last piece of the I/O, we've got
			 * the full S/G list.  Queue processing in the thread.
			 * Otherwise wait for the next piece.
			 */
			if (msg->dt.sg_last != 0)
				ctl_enqueue_isc(io);
			break;
		}
		/* Performed on the Serializing (primary) SC, XFER mode only */
		case CTL_MSG_DATAMOVE_DONE: {
			if (msg->hdr.serializing_sc == NULL) {
				printf("%s: serializing_sc == NULL!\n",
				       __func__);
				/* XXX KDM now what? */
				break;
			}
			/*
			 * We grab the sense information here in case
			 * there was a failure, so we can return status
			 * back to the initiator.
			 */
			io = msg->hdr.serializing_sc;
			io->io_hdr.msg_type = CTL_MSG_DATAMOVE_DONE;
			io->io_hdr.flags &= ~CTL_FLAG_DMA_INPROG;
			io->io_hdr.flags |= CTL_FLAG_IO_ACTIVE;
			io->io_hdr.port_status = msg->scsi.port_status;
			io->scsiio.kern_data_resid = msg->scsi.kern_data_resid;
			if (msg->hdr.status != CTL_STATUS_NONE) {
				io->io_hdr.status = msg->hdr.status;
				io->scsiio.scsi_status = msg->scsi.scsi_status;
				io->scsiio.sense_len = msg->scsi.sense_len;
				memcpy(&io->scsiio.sense_data,
				    &msg->scsi.sense_data,
				    msg->scsi.sense_len);
				if (msg->hdr.status == CTL_SUCCESS)
					io->io_hdr.flags |= CTL_FLAG_STATUS_SENT;
			}
			ctl_enqueue_isc(io);
			break;
		}

		/* Preformed on Originating SC, SER_ONLY mode */
		case CTL_MSG_R2R:
			io = msg->hdr.original_sc;
			if (io == NULL) {
				printf("%s: original_sc == NULL!\n",
				    __func__);
				break;
			}
			io->io_hdr.flags |= CTL_FLAG_IO_ACTIVE;
			io->io_hdr.msg_type = CTL_MSG_R2R;
			io->io_hdr.remote_io = msg->hdr.serializing_sc;
			ctl_enqueue_isc(io);
			break;

		/*
		 * Performed on Serializing(i.e. primary SC) SC in SER_ONLY
		 * mode.
		 * Performed on the Originating (i.e. secondary) SC in XFER
		 * mode
		 */
		case CTL_MSG_FINISH_IO:
			if (softc->ha_mode == CTL_HA_MODE_XFER)
				ctl_isc_handler_finish_xfer(softc, msg);
			else
				ctl_isc_handler_finish_ser_only(softc, msg);
			break;

		/* Preformed on Originating SC */
		case CTL_MSG_BAD_JUJU:
			io = msg->hdr.original_sc;
			if (io == NULL) {
				printf("%s: Bad JUJU!, original_sc is NULL!\n",
				       __func__);
				break;
			}
			ctl_copy_sense_data(msg, io);
			/*
			 * IO should have already been cleaned up on other
			 * SC so clear this flag so we won't send a message
			 * back to finish the IO there.
			 */
			io->io_hdr.flags &= ~CTL_FLAG_SENT_2OTHER_SC;
			io->io_hdr.flags |= CTL_FLAG_IO_ACTIVE;

			/* io = msg->hdr.serializing_sc; */
			io->io_hdr.msg_type = CTL_MSG_BAD_JUJU;
			ctl_enqueue_isc(io);
			break;

		/* Handle resets sent from the other side */
		case CTL_MSG_MANAGE_TASKS: {
			struct ctl_taskio *taskio;
			taskio = (struct ctl_taskio *)ctl_alloc_io(
			    softc->othersc_pool);
			ctl_zero_io((union ctl_io *)taskio);
			taskio->io_hdr.io_type = CTL_IO_TASK;
			taskio->io_hdr.flags |= CTL_FLAG_FROM_OTHER_SC;
			taskio->io_hdr.nexus = msg->hdr.nexus;
			taskio->task_action = msg->task.task_action;
			taskio->tag_num = msg->task.tag_num;
			taskio->tag_type = msg->task.tag_type;
#ifdef CTL_TIME_IO
			taskio->io_hdr.start_time = time_uptime;
			getbinuptime(&taskio->io_hdr.start_bt);
#endif /* CTL_TIME_IO */
			ctl_run_task((union ctl_io *)taskio);
			break;
		}
		/* Persistent Reserve action which needs attention */
		case CTL_MSG_PERS_ACTION:
			presio = (struct ctl_prio *)ctl_alloc_io(
			    softc->othersc_pool);
			ctl_zero_io((union ctl_io *)presio);
			presio->io_hdr.msg_type = CTL_MSG_PERS_ACTION;
			presio->io_hdr.flags |= CTL_FLAG_FROM_OTHER_SC;
			presio->io_hdr.nexus = msg->hdr.nexus;
			presio->pr_msg = msg->pr;
			ctl_enqueue_isc((union ctl_io *)presio);
			break;
		case CTL_MSG_UA:
			ctl_isc_ua(softc, msg, param);
			break;
		case CTL_MSG_PORT_SYNC:
			ctl_isc_port_sync(softc, msg, param);
			break;
		case CTL_MSG_LUN_SYNC:
			ctl_isc_lun_sync(softc, msg, param);
			break;
		case CTL_MSG_IID_SYNC:
			ctl_isc_iid_sync(softc, msg, param);
			break;
		case CTL_MSG_LOGIN:
			ctl_isc_login(softc, msg, param);
			break;
		case CTL_MSG_MODE_SYNC:
			ctl_isc_mode_sync(softc, msg, param);
			break;
		default:
			printf("Received HA message of unknown type %d\n",
			    msg->hdr.msg_type);
			ctl_ha_msg_abort(CTL_HA_CHAN_CTL);
			break;
		}
		if (msg != &msgbuf)
			free(msg, M_CTL);
	} else if (event == CTL_HA_EVT_LINK_CHANGE) {
		printf("CTL: HA link status changed from %d to %d\n",
		    softc->ha_link, param);
		if (param == softc->ha_link)
			return;
		if (softc->ha_link == CTL_HA_LINK_ONLINE) {
			softc->ha_link = param;
			ctl_isc_ha_link_down(softc);
		} else {
			softc->ha_link = param;
			if (softc->ha_link == CTL_HA_LINK_ONLINE)
				ctl_isc_ha_link_up(softc);
		}
		return;
	} else {
		printf("ctl_isc_event_handler: Unknown event %d\n", event);
		return;
	}
}

static void
ctl_copy_sense_data(union ctl_ha_msg *src, union ctl_io *dest)
{

	memcpy(&dest->scsiio.sense_data, &src->scsi.sense_data,
	    src->scsi.sense_len);
	dest->scsiio.scsi_status = src->scsi.scsi_status;
	dest->scsiio.sense_len = src->scsi.sense_len;
	dest->io_hdr.status = src->hdr.status;
}

static void
ctl_copy_sense_data_back(union ctl_io *src, union ctl_ha_msg *dest)
{

	memcpy(&dest->scsi.sense_data, &src->scsiio.sense_data,
	    src->scsiio.sense_len);
	dest->scsi.scsi_status = src->scsiio.scsi_status;
	dest->scsi.sense_len = src->scsiio.sense_len;
	dest->hdr.status = src->io_hdr.status;
}

void
ctl_est_ua(struct ctl_lun *lun, uint32_t initidx, ctl_ua_type ua)
{
	struct ctl_softc *softc = lun->ctl_softc;
	ctl_ua_type *pu;

	if (initidx < softc->init_min || initidx >= softc->init_max)
		return;
	mtx_assert(&lun->lun_lock, MA_OWNED);
	pu = lun->pending_ua[initidx / CTL_MAX_INIT_PER_PORT];
	if (pu == NULL)
		return;
	pu[initidx % CTL_MAX_INIT_PER_PORT] |= ua;
}

void
ctl_est_ua_port(struct ctl_lun *lun, int port, uint32_t except, ctl_ua_type ua)
{
	int i;

	mtx_assert(&lun->lun_lock, MA_OWNED);
	if (lun->pending_ua[port] == NULL)
		return;
	for (i = 0; i < CTL_MAX_INIT_PER_PORT; i++) {
		if (port * CTL_MAX_INIT_PER_PORT + i == except)
			continue;
		lun->pending_ua[port][i] |= ua;
	}
}

void
ctl_est_ua_all(struct ctl_lun *lun, uint32_t except, ctl_ua_type ua)
{
	struct ctl_softc *softc = lun->ctl_softc;
	int i;

	mtx_assert(&lun->lun_lock, MA_OWNED);
	for (i = softc->port_min; i < softc->port_max; i++)
		ctl_est_ua_port(lun, i, except, ua);
}

void
ctl_clr_ua(struct ctl_lun *lun, uint32_t initidx, ctl_ua_type ua)
{
	struct ctl_softc *softc = lun->ctl_softc;
	ctl_ua_type *pu;

	if (initidx < softc->init_min || initidx >= softc->init_max)
		return;
	mtx_assert(&lun->lun_lock, MA_OWNED);
	pu = lun->pending_ua[initidx / CTL_MAX_INIT_PER_PORT];
	if (pu == NULL)
		return;
	pu[initidx % CTL_MAX_INIT_PER_PORT] &= ~ua;
}

void
ctl_clr_ua_all(struct ctl_lun *lun, uint32_t except, ctl_ua_type ua)
{
	struct ctl_softc *softc = lun->ctl_softc;
	int i, j;

	mtx_assert(&lun->lun_lock, MA_OWNED);
	for (i = softc->port_min; i < softc->port_max; i++) {
		if (lun->pending_ua[i] == NULL)
			continue;
		for (j = 0; j < CTL_MAX_INIT_PER_PORT; j++) {
			if (i * CTL_MAX_INIT_PER_PORT + j == except)
				continue;
			lun->pending_ua[i][j] &= ~ua;
		}
	}
}

void
ctl_clr_ua_allluns(struct ctl_softc *ctl_softc, uint32_t initidx,
    ctl_ua_type ua_type)
{
	struct ctl_lun *lun;

	mtx_assert(&ctl_softc->ctl_lock, MA_OWNED);
	STAILQ_FOREACH(lun, &ctl_softc->lun_list, links) {
		mtx_lock(&lun->lun_lock);
		ctl_clr_ua(lun, initidx, ua_type);
		mtx_unlock(&lun->lun_lock);
	}
}

static int
ctl_ha_role_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct ctl_softc *softc = (struct ctl_softc *)arg1;
	struct ctl_lun *lun;
	struct ctl_lun_req ireq;
	int error, value;

	value = (softc->flags & CTL_FLAG_ACTIVE_SHELF) ? 0 : 1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if ((error != 0) || (req->newptr == NULL))
		return (error);

	mtx_lock(&softc->ctl_lock);
	if (value == 0)
		softc->flags |= CTL_FLAG_ACTIVE_SHELF;
	else
		softc->flags &= ~CTL_FLAG_ACTIVE_SHELF;
	STAILQ_FOREACH(lun, &softc->lun_list, links) {
		mtx_unlock(&softc->ctl_lock);
		bzero(&ireq, sizeof(ireq));
		ireq.reqtype = CTL_LUNREQ_MODIFY;
		ireq.reqdata.modify.lun_id = lun->lun;
		lun->backend->ioctl(NULL, CTL_LUN_REQ, (caddr_t)&ireq, 0,
		    curthread);
		if (ireq.status != CTL_LUN_OK) {
			printf("%s: CTL_LUNREQ_MODIFY returned %d '%s'\n",
			    __func__, ireq.status, ireq.error_str);
		}
		mtx_lock(&softc->ctl_lock);
	}
	mtx_unlock(&softc->ctl_lock);
	return (0);
}

static int
ctl_init(void)
{
	struct make_dev_args args;
	struct ctl_softc *softc;
	int i, error;

	softc = control_softc = malloc(sizeof(*control_softc), M_DEVBUF,
			       M_WAITOK | M_ZERO);

	make_dev_args_init(&args);
	args.mda_devsw = &ctl_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0600;
	args.mda_si_drv1 = softc;
	args.mda_si_drv2 = NULL;
	error = make_dev_s(&args, &softc->dev, "cam/ctl");
	if (error != 0) {
		free(softc, M_DEVBUF);
		control_softc = NULL;
		return (error);
	}

	sysctl_ctx_init(&softc->sysctl_ctx);
	softc->sysctl_tree = SYSCTL_ADD_NODE(&softc->sysctl_ctx,
		SYSCTL_STATIC_CHILDREN(_kern_cam), OID_AUTO, "ctl",
		CTLFLAG_RD, 0, "CAM Target Layer");

	if (softc->sysctl_tree == NULL) {
		printf("%s: unable to allocate sysctl tree\n", __func__);
		destroy_dev(softc->dev);
		free(softc, M_DEVBUF);
		control_softc = NULL;
		return (ENOMEM);
	}

	mtx_init(&softc->ctl_lock, "CTL mutex", NULL, MTX_DEF);
	softc->io_zone = uma_zcreate("CTL IO", sizeof(union ctl_io),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	softc->flags = 0;

	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "ha_mode", CTLFLAG_RDTUN, (int *)&softc->ha_mode, 0,
	    "HA mode (0 - act/stby, 1 - serialize only, 2 - xfer)");

	if (ctl_max_luns <= 0 || powerof2(ctl_max_luns) == 0) {
		printf("Bad value %d for kern.cam.ctl.max_luns, must be a power of two, using %d\n",
		    ctl_max_luns, CTL_DEFAULT_MAX_LUNS);
		ctl_max_luns = CTL_DEFAULT_MAX_LUNS;
	}
	softc->ctl_luns = malloc(sizeof(struct ctl_lun *) * ctl_max_luns,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	softc->ctl_lun_mask = malloc(sizeof(uint32_t) *
	    ((ctl_max_luns + 31) / 32), M_DEVBUF, M_WAITOK | M_ZERO);
	if (ctl_max_ports <= 0 || powerof2(ctl_max_ports) == 0) {
		printf("Bad value %d for kern.cam.ctl.max_ports, must be a power of two, using %d\n",
		    ctl_max_ports, CTL_DEFAULT_MAX_PORTS);
		ctl_max_ports = CTL_DEFAULT_MAX_PORTS;
	}
	softc->ctl_port_mask = malloc(sizeof(uint32_t) *
	  ((ctl_max_ports + 31) / 32), M_DEVBUF, M_WAITOK | M_ZERO);
	softc->ctl_ports = malloc(sizeof(struct ctl_port *) * ctl_max_ports,
	     M_DEVBUF, M_WAITOK | M_ZERO);


	/*
	 * In Copan's HA scheme, the "master" and "slave" roles are
	 * figured out through the slot the controller is in.  Although it
	 * is an active/active system, someone has to be in charge.
	 */
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "ha_id", CTLFLAG_RDTUN, &softc->ha_id, 0,
	    "HA head ID (0 - no HA)");
	if (softc->ha_id == 0 || softc->ha_id > NUM_HA_SHELVES) {
		softc->flags |= CTL_FLAG_ACTIVE_SHELF;
		softc->is_single = 1;
		softc->port_cnt = ctl_max_ports;
		softc->port_min = 0;
	} else {
		softc->port_cnt = ctl_max_ports / NUM_HA_SHELVES;
		softc->port_min = (softc->ha_id - 1) * softc->port_cnt;
	}
	softc->port_max = softc->port_min + softc->port_cnt;
	softc->init_min = softc->port_min * CTL_MAX_INIT_PER_PORT;
	softc->init_max = softc->port_max * CTL_MAX_INIT_PER_PORT;

	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "ha_link", CTLFLAG_RD, (int *)&softc->ha_link, 0,
	    "HA link state (0 - offline, 1 - unknown, 2 - online)");

	STAILQ_INIT(&softc->lun_list);
	STAILQ_INIT(&softc->pending_lun_queue);
	STAILQ_INIT(&softc->fe_list);
	STAILQ_INIT(&softc->port_list);
	STAILQ_INIT(&softc->be_list);
	ctl_tpc_init(softc);

	if (worker_threads <= 0)
		worker_threads = max(1, mp_ncpus / 4);
	if (worker_threads > CTL_MAX_THREADS)
		worker_threads = CTL_MAX_THREADS;

	for (i = 0; i < worker_threads; i++) {
		struct ctl_thread *thr = &softc->threads[i];

		mtx_init(&thr->queue_lock, "CTL queue mutex", NULL, MTX_DEF);
		thr->ctl_softc = softc;
		STAILQ_INIT(&thr->incoming_queue);
		STAILQ_INIT(&thr->rtr_queue);
		STAILQ_INIT(&thr->done_queue);
		STAILQ_INIT(&thr->isc_queue);

		error = kproc_kthread_add(ctl_work_thread, thr,
		    &softc->ctl_proc, &thr->thread, 0, 0, "ctl", "work%d", i);
		if (error != 0) {
			printf("error creating CTL work thread!\n");
			return (error);
		}
	}
	error = kproc_kthread_add(ctl_lun_thread, softc,
	    &softc->ctl_proc, &softc->lun_thread, 0, 0, "ctl", "lun");
	if (error != 0) {
		printf("error creating CTL lun thread!\n");
		return (error);
	}
	error = kproc_kthread_add(ctl_thresh_thread, softc,
	    &softc->ctl_proc, &softc->thresh_thread, 0, 0, "ctl", "thresh");
	if (error != 0) {
		printf("error creating CTL threshold thread!\n");
		return (error);
	}

	SYSCTL_ADD_PROC(&softc->sysctl_ctx,SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "ha_role", CTLTYPE_INT | CTLFLAG_RWTUN,
	    softc, 0, ctl_ha_role_sysctl, "I", "HA role for this head");

	if (softc->is_single == 0) {
		if (ctl_frontend_register(&ha_frontend) != 0)
			softc->is_single = 1;
	}
	return (0);
}

static int
ctl_shutdown(void)
{
	struct ctl_softc *softc = control_softc;
	int i;

	if (softc->is_single == 0)
		ctl_frontend_deregister(&ha_frontend);

	destroy_dev(softc->dev);

	/* Shutdown CTL threads. */
	softc->shutdown = 1;
	for (i = 0; i < worker_threads; i++) {
		struct ctl_thread *thr = &softc->threads[i];
		while (thr->thread != NULL) {
			wakeup(thr);
			if (thr->thread != NULL)
				pause("CTL thr shutdown", 1);
		}
		mtx_destroy(&thr->queue_lock);
	}
	while (softc->lun_thread != NULL) {
		wakeup(&softc->pending_lun_queue);
		if (softc->lun_thread != NULL)
			pause("CTL thr shutdown", 1);
	}
	while (softc->thresh_thread != NULL) {
		wakeup(softc->thresh_thread);
		if (softc->thresh_thread != NULL)
			pause("CTL thr shutdown", 1);
	}

	ctl_tpc_shutdown(softc);
	uma_zdestroy(softc->io_zone);
	mtx_destroy(&softc->ctl_lock);

	free(softc->ctl_luns, M_DEVBUF);
	free(softc->ctl_lun_mask, M_DEVBUF);
	free(softc->ctl_port_mask, M_DEVBUF);
	free(softc->ctl_ports, M_DEVBUF);

	sysctl_ctx_free(&softc->sysctl_ctx);

	free(softc, M_DEVBUF);
	control_softc = NULL;
	return (0);
}

static int
ctl_module_event_handler(module_t mod, int what, void *arg)
{

	switch (what) {
	case MOD_LOAD:
		return (ctl_init());
	case MOD_UNLOAD:
		return (ctl_shutdown());
	default:
		return (EOPNOTSUPP);
	}
}

/*
 * XXX KDM should we do some access checks here?  Bump a reference count to
 * prevent a CTL module from being unloaded while someone has it open?
 */
static int
ctl_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	return (0);
}

static int
ctl_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	return (0);
}

/*
 * Remove an initiator by port number and initiator ID.
 * Returns 0 for success, -1 for failure.
 */
int
ctl_remove_initiator(struct ctl_port *port, int iid)
{
	struct ctl_softc *softc = port->ctl_softc;
	int last;

	mtx_assert(&softc->ctl_lock, MA_NOTOWNED);

	if (iid > CTL_MAX_INIT_PER_PORT) {
		printf("%s: initiator ID %u > maximun %u!\n",
		       __func__, iid, CTL_MAX_INIT_PER_PORT);
		return (-1);
	}

	mtx_lock(&softc->ctl_lock);
	last = (--port->wwpn_iid[iid].in_use == 0);
	port->wwpn_iid[iid].last_use = time_uptime;
	mtx_unlock(&softc->ctl_lock);
	if (last)
		ctl_i_t_nexus_loss(softc, iid, CTL_UA_POWERON);
	ctl_isc_announce_iid(port, iid);

	return (0);
}

/*
 * Add an initiator to the initiator map.
 * Returns iid for success, < 0 for failure.
 */
int
ctl_add_initiator(struct ctl_port *port, int iid, uint64_t wwpn, char *name)
{
	struct ctl_softc *softc = port->ctl_softc;
	time_t best_time;
	int i, best;

	mtx_assert(&softc->ctl_lock, MA_NOTOWNED);

	if (iid >= CTL_MAX_INIT_PER_PORT) {
		printf("%s: WWPN %#jx initiator ID %u > maximum %u!\n",
		       __func__, wwpn, iid, CTL_MAX_INIT_PER_PORT);
		free(name, M_CTL);
		return (-1);
	}

	mtx_lock(&softc->ctl_lock);

	if (iid < 0 && (wwpn != 0 || name != NULL)) {
		for (i = 0; i < CTL_MAX_INIT_PER_PORT; i++) {
			if (wwpn != 0 && wwpn == port->wwpn_iid[i].wwpn) {
				iid = i;
				break;
			}
			if (name != NULL && port->wwpn_iid[i].name != NULL &&
			    strcmp(name, port->wwpn_iid[i].name) == 0) {
				iid = i;
				break;
			}
		}
	}

	if (iid < 0) {
		for (i = 0; i < CTL_MAX_INIT_PER_PORT; i++) {
			if (port->wwpn_iid[i].in_use == 0 &&
			    port->wwpn_iid[i].wwpn == 0 &&
			    port->wwpn_iid[i].name == NULL) {
				iid = i;
				break;
			}
		}
	}

	if (iid < 0) {
		best = -1;
		best_time = INT32_MAX;
		for (i = 0; i < CTL_MAX_INIT_PER_PORT; i++) {
			if (port->wwpn_iid[i].in_use == 0) {
				if (port->wwpn_iid[i].last_use < best_time) {
					best = i;
					best_time = port->wwpn_iid[i].last_use;
				}
			}
		}
		iid = best;
	}

	if (iid < 0) {
		mtx_unlock(&softc->ctl_lock);
		free(name, M_CTL);
		return (-2);
	}

	if (port->wwpn_iid[iid].in_use > 0 && (wwpn != 0 || name != NULL)) {
		/*
		 * This is not an error yet.
		 */
		if (wwpn != 0 && wwpn == port->wwpn_iid[iid].wwpn) {
#if 0
			printf("%s: port %d iid %u WWPN %#jx arrived"
			    " again\n", __func__, port->targ_port,
			    iid, (uintmax_t)wwpn);
#endif
			goto take;
		}
		if (name != NULL && port->wwpn_iid[iid].name != NULL &&
		    strcmp(name, port->wwpn_iid[iid].name) == 0) {
#if 0
			printf("%s: port %d iid %u name '%s' arrived"
			    " again\n", __func__, port->targ_port,
			    iid, name);
#endif
			goto take;
		}

		/*
		 * This is an error, but what do we do about it?  The
		 * driver is telling us we have a new WWPN for this
		 * initiator ID, so we pretty much need to use it.
		 */
		printf("%s: port %d iid %u WWPN %#jx '%s' arrived,"
		    " but WWPN %#jx '%s' is still at that address\n",
		    __func__, port->targ_port, iid, wwpn, name,
		    (uintmax_t)port->wwpn_iid[iid].wwpn,
		    port->wwpn_iid[iid].name);
	}
take:
	free(port->wwpn_iid[iid].name, M_CTL);
	port->wwpn_iid[iid].name = name;
	port->wwpn_iid[iid].wwpn = wwpn;
	port->wwpn_iid[iid].in_use++;
	mtx_unlock(&softc->ctl_lock);
	ctl_isc_announce_iid(port, iid);

	return (iid);
}

static int
ctl_create_iid(struct ctl_port *port, int iid, uint8_t *buf)
{
	int len;

	switch (port->port_type) {
	case CTL_PORT_FC:
	{
		struct scsi_transportid_fcp *id =
		    (struct scsi_transportid_fcp *)buf;
		if (port->wwpn_iid[iid].wwpn == 0)
			return (0);
		memset(id, 0, sizeof(*id));
		id->format_protocol = SCSI_PROTO_FC;
		scsi_u64to8b(port->wwpn_iid[iid].wwpn, id->n_port_name);
		return (sizeof(*id));
	}
	case CTL_PORT_ISCSI:
	{
		struct scsi_transportid_iscsi_port *id =
		    (struct scsi_transportid_iscsi_port *)buf;
		if (port->wwpn_iid[iid].name == NULL)
			return (0);
		memset(id, 0, 256);
		id->format_protocol = SCSI_TRN_ISCSI_FORMAT_PORT |
		    SCSI_PROTO_ISCSI;
		len = strlcpy(id->iscsi_name, port->wwpn_iid[iid].name, 252) + 1;
		len = roundup2(min(len, 252), 4);
		scsi_ulto2b(len, id->additional_length);
		return (sizeof(*id) + len);
	}
	case CTL_PORT_SAS:
	{
		struct scsi_transportid_sas *id =
		    (struct scsi_transportid_sas *)buf;
		if (port->wwpn_iid[iid].wwpn == 0)
			return (0);
		memset(id, 0, sizeof(*id));
		id->format_protocol = SCSI_PROTO_SAS;
		scsi_u64to8b(port->wwpn_iid[iid].wwpn, id->sas_address);
		return (sizeof(*id));
	}
	default:
	{
		struct scsi_transportid_spi *id =
		    (struct scsi_transportid_spi *)buf;
		memset(id, 0, sizeof(*id));
		id->format_protocol = SCSI_PROTO_SPI;
		scsi_ulto2b(iid, id->scsi_addr);
		scsi_ulto2b(port->targ_port, id->rel_trgt_port_id);
		return (sizeof(*id));
	}
	}
}

/*
 * Serialize a command that went down the "wrong" side, and so was sent to
 * this controller for execution.  The logic is a little different than the
 * standard case in ctl_scsiio_precheck().  Errors in this case need to get
 * sent back to the other side, but in the success case, we execute the
 * command on this side (XFER mode) or tell the other side to execute it
 * (SER_ONLY mode).
 */
static void
ctl_serialize_other_sc_cmd(struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_port *port = CTL_PORT(ctsio);
	union ctl_ha_msg msg_info;
	struct ctl_lun *lun;
	const struct ctl_cmd_entry *entry;
	union ctl_io *bio;
	uint32_t targ_lun;

	targ_lun = ctsio->io_hdr.nexus.targ_mapped_lun;

	/* Make sure that we know about this port. */
	if (port == NULL || (port->status & CTL_PORT_STATUS_ONLINE) == 0) {
		ctl_set_internal_failure(ctsio, /*sks_valid*/ 0,
					 /*retry_count*/ 1);
		goto badjuju;
	}

	/* Make sure that we know about this LUN. */
	mtx_lock(&softc->ctl_lock);
	if (targ_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);

		/*
		 * The other node would not send this request to us unless
		 * received announce that we are primary node for this LUN.
		 * If this LUN does not exist now, it is probably result of
		 * a race, so respond to initiator in the most opaque way.
		 */
		ctl_set_busy(ctsio);
		goto badjuju;
	}
	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);

	/*
	 * If the LUN is invalid, pretend that it doesn't exist.
	 * It will go away as soon as all pending I/Os completed.
	 */
	if (lun->flags & CTL_LUN_DISABLED) {
		mtx_unlock(&lun->lun_lock);
		ctl_set_busy(ctsio);
		goto badjuju;
	}

	entry = ctl_get_cmd_entry(ctsio, NULL);
	if (ctl_scsiio_lun_check(lun, entry, ctsio) != 0) {
		mtx_unlock(&lun->lun_lock);
		goto badjuju;
	}

	CTL_LUN(ctsio) = lun;
	CTL_BACKEND_LUN(ctsio) = lun->be_lun;

	/*
	 * Every I/O goes into the OOA queue for a
	 * particular LUN, and stays there until completion.
	 */
#ifdef CTL_TIME_IO
	if (TAILQ_EMPTY(&lun->ooa_queue))
		lun->idle_time += getsbinuptime() - lun->last_busy;
#endif
	TAILQ_INSERT_TAIL(&lun->ooa_queue, &ctsio->io_hdr, ooa_links);

	bio = (union ctl_io *)TAILQ_PREV(&ctsio->io_hdr, ctl_ooaq, ooa_links);
	switch (ctl_check_ooa(lun, (union ctl_io *)ctsio, &bio)) {
	case CTL_ACTION_BLOCK:
		ctsio->io_hdr.blocker = bio;
		TAILQ_INSERT_TAIL(&bio->io_hdr.blocked_queue, &ctsio->io_hdr,
				  blocked_links);
		mtx_unlock(&lun->lun_lock);
		break;
	case CTL_ACTION_PASS:
	case CTL_ACTION_SKIP:
		if (softc->ha_mode == CTL_HA_MODE_XFER) {
			ctsio->io_hdr.flags |= CTL_FLAG_IS_WAS_ON_RTR;
			ctl_enqueue_rtr((union ctl_io *)ctsio);
			mtx_unlock(&lun->lun_lock);
		} else {
			ctsio->io_hdr.flags &= ~CTL_FLAG_IO_ACTIVE;
			mtx_unlock(&lun->lun_lock);

			/* send msg back to other side */
			msg_info.hdr.original_sc = ctsio->io_hdr.remote_io;
			msg_info.hdr.serializing_sc = (union ctl_io *)ctsio;
			msg_info.hdr.msg_type = CTL_MSG_R2R;
			ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg_info,
			    sizeof(msg_info.hdr), M_WAITOK);
		}
		break;
	case CTL_ACTION_OVERLAP:
		TAILQ_REMOVE(&lun->ooa_queue, &ctsio->io_hdr, ooa_links);
		mtx_unlock(&lun->lun_lock);
		ctl_set_overlapped_cmd(ctsio);
		goto badjuju;
	case CTL_ACTION_OVERLAP_TAG:
		TAILQ_REMOVE(&lun->ooa_queue, &ctsio->io_hdr, ooa_links);
		mtx_unlock(&lun->lun_lock);
		ctl_set_overlapped_tag(ctsio, ctsio->tag_num);
		goto badjuju;
	case CTL_ACTION_ERROR:
	default:
		TAILQ_REMOVE(&lun->ooa_queue, &ctsio->io_hdr, ooa_links);
		mtx_unlock(&lun->lun_lock);

		ctl_set_internal_failure(ctsio, /*sks_valid*/ 0,
					 /*retry_count*/ 0);
badjuju:
		ctl_copy_sense_data_back((union ctl_io *)ctsio, &msg_info);
		msg_info.hdr.original_sc = ctsio->io_hdr.remote_io;
		msg_info.hdr.serializing_sc = NULL;
		msg_info.hdr.msg_type = CTL_MSG_BAD_JUJU;
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg_info,
		    sizeof(msg_info.scsi), M_WAITOK);
		ctl_free_io((union ctl_io *)ctsio);
		break;
	}
}

/*
 * Returns 0 for success, errno for failure.
 */
static void
ctl_ioctl_fill_ooa(struct ctl_lun *lun, uint32_t *cur_fill_num,
		   struct ctl_ooa *ooa_hdr, struct ctl_ooa_entry *kern_entries)
{
	union ctl_io *io;

	mtx_lock(&lun->lun_lock);
	for (io = (union ctl_io *)TAILQ_FIRST(&lun->ooa_queue); (io != NULL);
	     (*cur_fill_num)++, io = (union ctl_io *)TAILQ_NEXT(&io->io_hdr,
	     ooa_links)) {
		struct ctl_ooa_entry *entry;

		/*
		 * If we've got more than we can fit, just count the
		 * remaining entries.
		 */
		if (*cur_fill_num >= ooa_hdr->alloc_num)
			continue;

		entry = &kern_entries[*cur_fill_num];

		entry->tag_num = io->scsiio.tag_num;
		entry->lun_num = lun->lun;
#ifdef CTL_TIME_IO
		entry->start_bt = io->io_hdr.start_bt;
#endif
		bcopy(io->scsiio.cdb, entry->cdb, io->scsiio.cdb_len);
		entry->cdb_len = io->scsiio.cdb_len;
		if (io->io_hdr.blocker != NULL)
			entry->cmd_flags |= CTL_OOACMD_FLAG_BLOCKED;

		if (io->io_hdr.flags & CTL_FLAG_DMA_INPROG)
			entry->cmd_flags |= CTL_OOACMD_FLAG_DMA;

		if (io->io_hdr.flags & CTL_FLAG_ABORT)
			entry->cmd_flags |= CTL_OOACMD_FLAG_ABORT;

		if (io->io_hdr.flags & CTL_FLAG_IS_WAS_ON_RTR)
			entry->cmd_flags |= CTL_OOACMD_FLAG_RTR;

		if (io->io_hdr.flags & CTL_FLAG_DMA_QUEUED)
			entry->cmd_flags |= CTL_OOACMD_FLAG_DMA_QUEUED;
	}
	mtx_unlock(&lun->lun_lock);
}

/*
 * Escape characters that are illegal or not recommended in XML.
 */
int
ctl_sbuf_printf_esc(struct sbuf *sb, char *str, int size)
{
	char *end = str + size;
	int retval;

	retval = 0;

	for (; *str && str < end; str++) {
		switch (*str) {
		case '&':
			retval = sbuf_printf(sb, "&amp;");
			break;
		case '>':
			retval = sbuf_printf(sb, "&gt;");
			break;
		case '<':
			retval = sbuf_printf(sb, "&lt;");
			break;
		default:
			retval = sbuf_putc(sb, *str);
			break;
		}

		if (retval != 0)
			break;

	}

	return (retval);
}

static void
ctl_id_sbuf(struct ctl_devid *id, struct sbuf *sb)
{
	struct scsi_vpd_id_descriptor *desc;
	int i;

	if (id == NULL || id->len < 4)
		return;
	desc = (struct scsi_vpd_id_descriptor *)id->data;
	switch (desc->id_type & SVPD_ID_TYPE_MASK) {
	case SVPD_ID_TYPE_T10:
		sbuf_printf(sb, "t10.");
		break;
	case SVPD_ID_TYPE_EUI64:
		sbuf_printf(sb, "eui.");
		break;
	case SVPD_ID_TYPE_NAA:
		sbuf_printf(sb, "naa.");
		break;
	case SVPD_ID_TYPE_SCSI_NAME:
		break;
	}
	switch (desc->proto_codeset & SVPD_ID_CODESET_MASK) {
	case SVPD_ID_CODESET_BINARY:
		for (i = 0; i < desc->length; i++)
			sbuf_printf(sb, "%02x", desc->identifier[i]);
		break;
	case SVPD_ID_CODESET_ASCII:
		sbuf_printf(sb, "%.*s", (int)desc->length,
		    (char *)desc->identifier);
		break;
	case SVPD_ID_CODESET_UTF8:
		sbuf_printf(sb, "%s", (char *)desc->identifier);
		break;
	}
}

static int
ctl_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
	  struct thread *td)
{
	struct ctl_softc *softc = dev->si_drv1;
	struct ctl_port *port;
	struct ctl_lun *lun;
	int retval;

	retval = 0;

	switch (cmd) {
	case CTL_IO:
		retval = ctl_ioctl_io(dev, cmd, addr, flag, td);
		break;
	case CTL_ENABLE_PORT:
	case CTL_DISABLE_PORT:
	case CTL_SET_PORT_WWNS: {
		struct ctl_port *port;
		struct ctl_port_entry *entry;

		entry = (struct ctl_port_entry *)addr;
		
		mtx_lock(&softc->ctl_lock);
		STAILQ_FOREACH(port, &softc->port_list, links) {
			int action, done;

			if (port->targ_port < softc->port_min ||
			    port->targ_port >= softc->port_max)
				continue;

			action = 0;
			done = 0;
			if ((entry->port_type == CTL_PORT_NONE)
			 && (entry->targ_port == port->targ_port)) {
				/*
				 * If the user only wants to enable or
				 * disable or set WWNs on a specific port,
				 * do the operation and we're done.
				 */
				action = 1;
				done = 1;
			} else if (entry->port_type & port->port_type) {
				/*
				 * Compare the user's type mask with the
				 * particular frontend type to see if we
				 * have a match.
				 */
				action = 1;
				done = 0;

				/*
				 * Make sure the user isn't trying to set
				 * WWNs on multiple ports at the same time.
				 */
				if (cmd == CTL_SET_PORT_WWNS) {
					printf("%s: Can't set WWNs on "
					       "multiple ports\n", __func__);
					retval = EINVAL;
					break;
				}
			}
			if (action == 0)
				continue;

			/*
			 * XXX KDM we have to drop the lock here, because
			 * the online/offline operations can potentially
			 * block.  We need to reference count the frontends
			 * so they can't go away,
			 */
			if (cmd == CTL_ENABLE_PORT) {
				mtx_unlock(&softc->ctl_lock);
				ctl_port_online(port);
				mtx_lock(&softc->ctl_lock);
			} else if (cmd == CTL_DISABLE_PORT) {
				mtx_unlock(&softc->ctl_lock);
				ctl_port_offline(port);
				mtx_lock(&softc->ctl_lock);
			} else if (cmd == CTL_SET_PORT_WWNS) {
				ctl_port_set_wwns(port,
				    (entry->flags & CTL_PORT_WWNN_VALID) ?
				    1 : 0, entry->wwnn,
				    (entry->flags & CTL_PORT_WWPN_VALID) ?
				    1 : 0, entry->wwpn);
			}
			if (done != 0)
				break;
		}
		mtx_unlock(&softc->ctl_lock);
		break;
	}
	case CTL_GET_OOA: {
		struct ctl_ooa *ooa_hdr;
		struct ctl_ooa_entry *entries;
		uint32_t cur_fill_num;

		ooa_hdr = (struct ctl_ooa *)addr;

		if ((ooa_hdr->alloc_len == 0)
		 || (ooa_hdr->alloc_num == 0)) {
			printf("%s: CTL_GET_OOA: alloc len %u and alloc num %u "
			       "must be non-zero\n", __func__,
			       ooa_hdr->alloc_len, ooa_hdr->alloc_num);
			retval = EINVAL;
			break;
		}

		if (ooa_hdr->alloc_len != (ooa_hdr->alloc_num *
		    sizeof(struct ctl_ooa_entry))) {
			printf("%s: CTL_GET_OOA: alloc len %u must be alloc "
			       "num %d * sizeof(struct ctl_ooa_entry) %zd\n",
			       __func__, ooa_hdr->alloc_len,
			       ooa_hdr->alloc_num,sizeof(struct ctl_ooa_entry));
			retval = EINVAL;
			break;
		}

		entries = malloc(ooa_hdr->alloc_len, M_CTL, M_WAITOK | M_ZERO);
		if (entries == NULL) {
			printf("%s: could not allocate %d bytes for OOA "
			       "dump\n", __func__, ooa_hdr->alloc_len);
			retval = ENOMEM;
			break;
		}

		mtx_lock(&softc->ctl_lock);
		if ((ooa_hdr->flags & CTL_OOA_FLAG_ALL_LUNS) == 0 &&
		    (ooa_hdr->lun_num >= ctl_max_luns ||
		     softc->ctl_luns[ooa_hdr->lun_num] == NULL)) {
			mtx_unlock(&softc->ctl_lock);
			free(entries, M_CTL);
			printf("%s: CTL_GET_OOA: invalid LUN %ju\n",
			       __func__, (uintmax_t)ooa_hdr->lun_num);
			retval = EINVAL;
			break;
		}

		cur_fill_num = 0;

		if (ooa_hdr->flags & CTL_OOA_FLAG_ALL_LUNS) {
			STAILQ_FOREACH(lun, &softc->lun_list, links) {
				ctl_ioctl_fill_ooa(lun, &cur_fill_num,
				    ooa_hdr, entries);
			}
		} else {
			lun = softc->ctl_luns[ooa_hdr->lun_num];
			ctl_ioctl_fill_ooa(lun, &cur_fill_num, ooa_hdr,
			    entries);
		}
		mtx_unlock(&softc->ctl_lock);

		ooa_hdr->fill_num = min(cur_fill_num, ooa_hdr->alloc_num);
		ooa_hdr->fill_len = ooa_hdr->fill_num *
			sizeof(struct ctl_ooa_entry);
		retval = copyout(entries, ooa_hdr->entries, ooa_hdr->fill_len);
		if (retval != 0) {
			printf("%s: error copying out %d bytes for OOA dump\n", 
			       __func__, ooa_hdr->fill_len);
		}

		getbinuptime(&ooa_hdr->cur_bt);

		if (cur_fill_num > ooa_hdr->alloc_num) {
			ooa_hdr->dropped_num = cur_fill_num -ooa_hdr->alloc_num;
			ooa_hdr->status = CTL_OOA_NEED_MORE_SPACE;
		} else {
			ooa_hdr->dropped_num = 0;
			ooa_hdr->status = CTL_OOA_OK;
		}

		free(entries, M_CTL);
		break;
	}
	case CTL_DELAY_IO: {
		struct ctl_io_delay_info *delay_info;

		delay_info = (struct ctl_io_delay_info *)addr;

#ifdef CTL_IO_DELAY
		mtx_lock(&softc->ctl_lock);
		if (delay_info->lun_id >= ctl_max_luns ||
		    (lun = softc->ctl_luns[delay_info->lun_id]) == NULL) {
			mtx_unlock(&softc->ctl_lock);
			delay_info->status = CTL_DELAY_STATUS_INVALID_LUN;
			break;
		}
		mtx_lock(&lun->lun_lock);
		mtx_unlock(&softc->ctl_lock);
		delay_info->status = CTL_DELAY_STATUS_OK;
		switch (delay_info->delay_type) {
		case CTL_DELAY_TYPE_CONT:
		case CTL_DELAY_TYPE_ONESHOT:
			break;
		default:
			delay_info->status = CTL_DELAY_STATUS_INVALID_TYPE;
			break;
		}
		switch (delay_info->delay_loc) {
		case CTL_DELAY_LOC_DATAMOVE:
			lun->delay_info.datamove_type = delay_info->delay_type;
			lun->delay_info.datamove_delay = delay_info->delay_secs;
			break;
		case CTL_DELAY_LOC_DONE:
			lun->delay_info.done_type = delay_info->delay_type;
			lun->delay_info.done_delay = delay_info->delay_secs;
			break;
		default:
			delay_info->status = CTL_DELAY_STATUS_INVALID_LOC;
			break;
		}
		mtx_unlock(&lun->lun_lock);
#else
		delay_info->status = CTL_DELAY_STATUS_NOT_IMPLEMENTED;
#endif /* CTL_IO_DELAY */
		break;
	}
	case CTL_ERROR_INJECT: {
		struct ctl_error_desc *err_desc, *new_err_desc;

		err_desc = (struct ctl_error_desc *)addr;

		new_err_desc = malloc(sizeof(*new_err_desc), M_CTL,
				      M_WAITOK | M_ZERO);
		bcopy(err_desc, new_err_desc, sizeof(*new_err_desc));

		mtx_lock(&softc->ctl_lock);
		if (err_desc->lun_id >= ctl_max_luns ||
		    (lun = softc->ctl_luns[err_desc->lun_id]) == NULL) {
			mtx_unlock(&softc->ctl_lock);
			free(new_err_desc, M_CTL);
			printf("%s: CTL_ERROR_INJECT: invalid LUN %ju\n",
			       __func__, (uintmax_t)err_desc->lun_id);
			retval = EINVAL;
			break;
		}
		mtx_lock(&lun->lun_lock);
		mtx_unlock(&softc->ctl_lock);

		/*
		 * We could do some checking here to verify the validity
		 * of the request, but given the complexity of error
		 * injection requests, the checking logic would be fairly
		 * complex.
		 *
		 * For now, if the request is invalid, it just won't get
		 * executed and might get deleted.
		 */
		STAILQ_INSERT_TAIL(&lun->error_list, new_err_desc, links);

		/*
		 * XXX KDM check to make sure the serial number is unique,
		 * in case we somehow manage to wrap.  That shouldn't
		 * happen for a very long time, but it's the right thing to
		 * do.
		 */
		new_err_desc->serial = lun->error_serial;
		err_desc->serial = lun->error_serial;
		lun->error_serial++;

		mtx_unlock(&lun->lun_lock);
		break;
	}
	case CTL_ERROR_INJECT_DELETE: {
		struct ctl_error_desc *delete_desc, *desc, *desc2;
		int delete_done;

		delete_desc = (struct ctl_error_desc *)addr;
		delete_done = 0;

		mtx_lock(&softc->ctl_lock);
		if (delete_desc->lun_id >= ctl_max_luns ||
		    (lun = softc->ctl_luns[delete_desc->lun_id]) == NULL) {
			mtx_unlock(&softc->ctl_lock);
			printf("%s: CTL_ERROR_INJECT_DELETE: invalid LUN %ju\n",
			       __func__, (uintmax_t)delete_desc->lun_id);
			retval = EINVAL;
			break;
		}
		mtx_lock(&lun->lun_lock);
		mtx_unlock(&softc->ctl_lock);
		STAILQ_FOREACH_SAFE(desc, &lun->error_list, links, desc2) {
			if (desc->serial != delete_desc->serial)
				continue;

			STAILQ_REMOVE(&lun->error_list, desc, ctl_error_desc,
				      links);
			free(desc, M_CTL);
			delete_done = 1;
		}
		mtx_unlock(&lun->lun_lock);
		if (delete_done == 0) {
			printf("%s: CTL_ERROR_INJECT_DELETE: can't find "
			       "error serial %ju on LUN %u\n", __func__, 
			       delete_desc->serial, delete_desc->lun_id);
			retval = EINVAL;
			break;
		}
		break;
	}
	case CTL_DUMP_STRUCTS: {
		int j, k;
		struct ctl_port *port;
		struct ctl_frontend *fe;

		mtx_lock(&softc->ctl_lock);
		printf("CTL Persistent Reservation information start:\n");
		STAILQ_FOREACH(lun, &softc->lun_list, links) {
			mtx_lock(&lun->lun_lock);
			if ((lun->flags & CTL_LUN_DISABLED) != 0) {
				mtx_unlock(&lun->lun_lock);
				continue;
			}

			for (j = 0; j < ctl_max_ports; j++) {
				if (lun->pr_keys[j] == NULL)
					continue;
				for (k = 0; k < CTL_MAX_INIT_PER_PORT; k++){
					if (lun->pr_keys[j][k] == 0)
						continue;
					printf("  LUN %ju port %d iid %d key "
					       "%#jx\n", lun->lun, j, k,
					       (uintmax_t)lun->pr_keys[j][k]);
				}
			}
			mtx_unlock(&lun->lun_lock);
		}
		printf("CTL Persistent Reservation information end\n");
		printf("CTL Ports:\n");
		STAILQ_FOREACH(port, &softc->port_list, links) {
			printf("  Port %d '%s' Frontend '%s' Type %u pp %d vp %d WWNN "
			       "%#jx WWPN %#jx\n", port->targ_port, port->port_name,
			       port->frontend->name, port->port_type,
			       port->physical_port, port->virtual_port,
			       (uintmax_t)port->wwnn, (uintmax_t)port->wwpn);
			for (j = 0; j < CTL_MAX_INIT_PER_PORT; j++) {
				if (port->wwpn_iid[j].in_use == 0 &&
				    port->wwpn_iid[j].wwpn == 0 &&
				    port->wwpn_iid[j].name == NULL)
					continue;

				printf("    iid %u use %d WWPN %#jx '%s'\n",
				    j, port->wwpn_iid[j].in_use,
				    (uintmax_t)port->wwpn_iid[j].wwpn,
				    port->wwpn_iid[j].name);
			}
		}
		printf("CTL Port information end\n");
		mtx_unlock(&softc->ctl_lock);
		/*
		 * XXX KDM calling this without a lock.  We'd likely want
		 * to drop the lock before calling the frontend's dump
		 * routine anyway.
		 */
		printf("CTL Frontends:\n");
		STAILQ_FOREACH(fe, &softc->fe_list, links) {
			printf("  Frontend '%s'\n", fe->name);
			if (fe->fe_dump != NULL)
				fe->fe_dump();
		}
		printf("CTL Frontend information end\n");
		break;
	}
	case CTL_LUN_REQ: {
		struct ctl_lun_req *lun_req;
		struct ctl_backend_driver *backend;
		void *packed;
		nvlist_t *tmp_args_nvl;
		size_t packed_len;

		lun_req = (struct ctl_lun_req *)addr;
		tmp_args_nvl = lun_req->args_nvl;

		backend = ctl_backend_find(lun_req->backend);
		if (backend == NULL) {
			lun_req->status = CTL_LUN_ERROR;
			snprintf(lun_req->error_str,
				 sizeof(lun_req->error_str),
				 "Backend \"%s\" not found.",
				 lun_req->backend);
			break;
		}

		if (lun_req->args != NULL) {
			packed = malloc(lun_req->args_len, M_CTL, M_WAITOK);
			if (copyin(lun_req->args, packed, lun_req->args_len) != 0) {
				free(packed, M_CTL);
				lun_req->status = CTL_LUN_ERROR;
				snprintf(lun_req->error_str, sizeof(lun_req->error_str),
				    "Cannot copyin args.");
				break;
			}
			lun_req->args_nvl = nvlist_unpack(packed,
			    lun_req->args_len, 0);
			free(packed, M_CTL);

			if (lun_req->args_nvl == NULL) {
				lun_req->status = CTL_LUN_ERROR;
				snprintf(lun_req->error_str, sizeof(lun_req->error_str),
				    "Cannot unpack args nvlist.");
				break;
			}
		} else
			lun_req->args_nvl = nvlist_create(0);

		retval = backend->ioctl(dev, cmd, addr, flag, td);
		nvlist_destroy(lun_req->args_nvl);
		lun_req->args_nvl = tmp_args_nvl;

		if (lun_req->result_nvl != NULL) {
			if (lun_req->result != NULL) {
				packed = nvlist_pack(lun_req->result_nvl,
				    &packed_len);
				if (packed == NULL) {
					lun_req->status = CTL_LUN_ERROR;
					snprintf(lun_req->error_str,
					    sizeof(lun_req->error_str),
					    "Cannot pack result nvlist.");
					break;
				}

				if (packed_len > lun_req->result_len) {
					lun_req->status = CTL_LUN_ERROR;
					snprintf(lun_req->error_str,
					    sizeof(lun_req->error_str),
					    "Result nvlist too large.");
					free(packed, M_NVLIST);
					break;
				}

				if (copyout(packed, lun_req->result, packed_len)) {
					lun_req->status = CTL_LUN_ERROR;
					snprintf(lun_req->error_str,
					    sizeof(lun_req->error_str),
					    "Cannot copyout() the result.");
					free(packed, M_NVLIST);
					break;
				}

				lun_req->result_len = packed_len;
				free(packed, M_NVLIST);
			}

			nvlist_destroy(lun_req->result_nvl);
		}
		break;
	}
	case CTL_LUN_LIST: {
		struct sbuf *sb;
		struct ctl_lun_list *list;
		const char *name, *value;
		void *cookie;
		int type;

		list = (struct ctl_lun_list *)addr;

		/*
		 * Allocate a fixed length sbuf here, based on the length
		 * of the user's buffer.  We could allocate an auto-extending
		 * buffer, and then tell the user how much larger our
		 * amount of data is than his buffer, but that presents
		 * some problems:
		 *
		 * 1.  The sbuf(9) routines use a blocking malloc, and so
		 *     we can't hold a lock while calling them with an
		 *     auto-extending buffer.
 		 *
		 * 2.  There is not currently a LUN reference counting
		 *     mechanism, outside of outstanding transactions on
		 *     the LUN's OOA queue.  So a LUN could go away on us
		 *     while we're getting the LUN number, backend-specific
		 *     information, etc.  Thus, given the way things
		 *     currently work, we need to hold the CTL lock while
		 *     grabbing LUN information.
		 *
		 * So, from the user's standpoint, the best thing to do is
		 * allocate what he thinks is a reasonable buffer length,
		 * and then if he gets a CTL_LUN_LIST_NEED_MORE_SPACE error,
		 * double the buffer length and try again.  (And repeat
		 * that until he succeeds.)
		 */
		sb = sbuf_new(NULL, NULL, list->alloc_len, SBUF_FIXEDLEN);
		if (sb == NULL) {
			list->status = CTL_LUN_LIST_ERROR;
			snprintf(list->error_str, sizeof(list->error_str),
				 "Unable to allocate %d bytes for LUN list",
				 list->alloc_len);
			break;
		}

		sbuf_printf(sb, "<ctllunlist>\n");

		mtx_lock(&softc->ctl_lock);
		STAILQ_FOREACH(lun, &softc->lun_list, links) {
			mtx_lock(&lun->lun_lock);
			retval = sbuf_printf(sb, "<lun id=\"%ju\">\n",
					     (uintmax_t)lun->lun);

			/*
			 * Bail out as soon as we see that we've overfilled
			 * the buffer.
			 */
			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<backend_type>%s"
					     "</backend_type>\n",
					     (lun->backend == NULL) ?  "none" :
					     lun->backend->name);

			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<lun_type>%d</lun_type>\n",
					     lun->be_lun->lun_type);

			if (retval != 0)
				break;

			if (lun->backend == NULL) {
				retval = sbuf_printf(sb, "</lun>\n");
				if (retval != 0)
					break;
				continue;
			}

			retval = sbuf_printf(sb, "\t<size>%ju</size>\n",
					     (lun->be_lun->maxlba > 0) ?
					     lun->be_lun->maxlba + 1 : 0);

			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<blocksize>%u</blocksize>\n",
					     lun->be_lun->blocksize);

			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<serial_number>");

			if (retval != 0)
				break;

			retval = ctl_sbuf_printf_esc(sb,
			    lun->be_lun->serial_num,
			    sizeof(lun->be_lun->serial_num));

			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "</serial_number>\n");
		
			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<device_id>");

			if (retval != 0)
				break;

			retval = ctl_sbuf_printf_esc(sb,
			    lun->be_lun->device_id,
			    sizeof(lun->be_lun->device_id));

			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "</device_id>\n");

			if (retval != 0)
				break;

			if (lun->backend->lun_info != NULL) {
				retval = lun->backend->lun_info(lun->be_lun->be_lun, sb);
				if (retval != 0)
					break;
			}

			cookie = NULL;
			while ((name = nvlist_next(lun->be_lun->options, &type,
			    &cookie)) != NULL) {
				sbuf_printf(sb, "\t<%s>", name);

				if (type == NV_TYPE_STRING) {
					value = dnvlist_get_string(
					    lun->be_lun->options, name, NULL);
					if (value != NULL)
						sbuf_printf(sb, "%s", value);
				}

				sbuf_printf(sb, "</%s>\n", name);
			}

			retval = sbuf_printf(sb, "</lun>\n");

			if (retval != 0)
				break;
			mtx_unlock(&lun->lun_lock);
		}
		if (lun != NULL)
			mtx_unlock(&lun->lun_lock);
		mtx_unlock(&softc->ctl_lock);

		if ((retval != 0)
		 || ((retval = sbuf_printf(sb, "</ctllunlist>\n")) != 0)) {
			retval = 0;
			sbuf_delete(sb);
			list->status = CTL_LUN_LIST_NEED_MORE_SPACE;
			snprintf(list->error_str, sizeof(list->error_str),
				 "Out of space, %d bytes is too small",
				 list->alloc_len);
			break;
		}

		sbuf_finish(sb);

		retval = copyout(sbuf_data(sb), list->lun_xml,
				 sbuf_len(sb) + 1);

		list->fill_len = sbuf_len(sb) + 1;
		list->status = CTL_LUN_LIST_OK;
		sbuf_delete(sb);
		break;
	}
	case CTL_ISCSI: {
		struct ctl_iscsi *ci;
		struct ctl_frontend *fe;

		ci = (struct ctl_iscsi *)addr;

		fe = ctl_frontend_find("iscsi");
		if (fe == NULL) {
			ci->status = CTL_ISCSI_ERROR;
			snprintf(ci->error_str, sizeof(ci->error_str),
			    "Frontend \"iscsi\" not found.");
			break;
		}

		retval = fe->ioctl(dev, cmd, addr, flag, td);
		break;
	}
	case CTL_PORT_REQ: {
		struct ctl_req *req;
		struct ctl_frontend *fe;
		void *packed;
		nvlist_t *tmp_args_nvl;
		size_t packed_len;

		req = (struct ctl_req *)addr;
		tmp_args_nvl = req->args_nvl;

		fe = ctl_frontend_find(req->driver);
		if (fe == NULL) {
			req->status = CTL_LUN_ERROR;
			snprintf(req->error_str, sizeof(req->error_str),
			    "Frontend \"%s\" not found.", req->driver);
			break;
		}

		if (req->args != NULL) {
			packed = malloc(req->args_len, M_CTL, M_WAITOK);
			if (copyin(req->args, packed, req->args_len) != 0) {
				free(packed, M_CTL);
				req->status = CTL_LUN_ERROR;
				snprintf(req->error_str, sizeof(req->error_str),
				    "Cannot copyin args.");
				break;
			}
			req->args_nvl = nvlist_unpack(packed,
			    req->args_len, 0);
			free(packed, M_CTL);

			if (req->args_nvl == NULL) {
				req->status = CTL_LUN_ERROR;
				snprintf(req->error_str, sizeof(req->error_str),
				    "Cannot unpack args nvlist.");
				break;
			}
		} else
			req->args_nvl = nvlist_create(0);

		if (fe->ioctl)
			retval = fe->ioctl(dev, cmd, addr, flag, td);
		else
			retval = ENODEV;

		nvlist_destroy(req->args_nvl);
		req->args_nvl = tmp_args_nvl;

		if (req->result_nvl != NULL) {
			if (req->result != NULL) {
				packed = nvlist_pack(req->result_nvl,
				    &packed_len);
				if (packed == NULL) {
					req->status = CTL_LUN_ERROR;
					snprintf(req->error_str,
					    sizeof(req->error_str),
					    "Cannot pack result nvlist.");
					break;
				}

				if (packed_len > req->result_len) {
					req->status = CTL_LUN_ERROR;
					snprintf(req->error_str,
					    sizeof(req->error_str),
					    "Result nvlist too large.");
					free(packed, M_NVLIST);
					break;
				}

				if (copyout(packed, req->result, packed_len)) {
					req->status = CTL_LUN_ERROR;
					snprintf(req->error_str,
					    sizeof(req->error_str),
					    "Cannot copyout() the result.");
					free(packed, M_NVLIST);
					break;
				}

				req->result_len = packed_len;
				free(packed, M_NVLIST);
			}

			nvlist_destroy(req->result_nvl);
		}
		break;
	}
	case CTL_PORT_LIST: {
		struct sbuf *sb;
		struct ctl_port *port;
		struct ctl_lun_list *list;
		const char *name, *value;
		void *cookie;
		int j, type;
		uint32_t plun;

		list = (struct ctl_lun_list *)addr;

		sb = sbuf_new(NULL, NULL, list->alloc_len, SBUF_FIXEDLEN);
		if (sb == NULL) {
			list->status = CTL_LUN_LIST_ERROR;
			snprintf(list->error_str, sizeof(list->error_str),
				 "Unable to allocate %d bytes for LUN list",
				 list->alloc_len);
			break;
		}

		sbuf_printf(sb, "<ctlportlist>\n");

		mtx_lock(&softc->ctl_lock);
		STAILQ_FOREACH(port, &softc->port_list, links) {
			retval = sbuf_printf(sb, "<targ_port id=\"%ju\">\n",
					     (uintmax_t)port->targ_port);

			/*
			 * Bail out as soon as we see that we've overfilled
			 * the buffer.
			 */
			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<frontend_type>%s"
			    "</frontend_type>\n", port->frontend->name);
			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<port_type>%d</port_type>\n",
					     port->port_type);
			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<online>%s</online>\n",
			    (port->status & CTL_PORT_STATUS_ONLINE) ? "YES" : "NO");
			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<port_name>%s</port_name>\n",
			    port->port_name);
			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<physical_port>%d</physical_port>\n",
			    port->physical_port);
			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "\t<virtual_port>%d</virtual_port>\n",
			    port->virtual_port);
			if (retval != 0)
				break;

			if (port->target_devid != NULL) {
				sbuf_printf(sb, "\t<target>");
				ctl_id_sbuf(port->target_devid, sb);
				sbuf_printf(sb, "</target>\n");
			}

			if (port->port_devid != NULL) {
				sbuf_printf(sb, "\t<port>");
				ctl_id_sbuf(port->port_devid, sb);
				sbuf_printf(sb, "</port>\n");
			}

			if (port->port_info != NULL) {
				retval = port->port_info(port->onoff_arg, sb);
				if (retval != 0)
					break;
			}

			cookie = NULL;
			while ((name = nvlist_next(port->options, &type,
			    &cookie)) != NULL) {
				sbuf_printf(sb, "\t<%s>", name);

				if (type == NV_TYPE_STRING) {
					value = dnvlist_get_string(port->options,
					    name, NULL);
					if (value != NULL)
						sbuf_printf(sb, "%s", value);
				}

				sbuf_printf(sb, "</%s>\n", name);
			}

			if (port->lun_map != NULL) {
				sbuf_printf(sb, "\t<lun_map>on</lun_map>\n");
				for (j = 0; j < port->lun_map_size; j++) {
					plun = ctl_lun_map_from_port(port, j);
					if (plun == UINT32_MAX)
						continue;
					sbuf_printf(sb,
					    "\t<lun id=\"%u\">%u</lun>\n",
					    j, plun);
				}
			}

			for (j = 0; j < CTL_MAX_INIT_PER_PORT; j++) {
				if (port->wwpn_iid[j].in_use == 0 ||
				    (port->wwpn_iid[j].wwpn == 0 &&
				     port->wwpn_iid[j].name == NULL))
					continue;

				if (port->wwpn_iid[j].name != NULL)
					retval = sbuf_printf(sb,
					    "\t<initiator id=\"%u\">%s</initiator>\n",
					    j, port->wwpn_iid[j].name);
				else
					retval = sbuf_printf(sb,
					    "\t<initiator id=\"%u\">naa.%08jx</initiator>\n",
					    j, port->wwpn_iid[j].wwpn);
				if (retval != 0)
					break;
			}
			if (retval != 0)
				break;

			retval = sbuf_printf(sb, "</targ_port>\n");
			if (retval != 0)
				break;
		}
		mtx_unlock(&softc->ctl_lock);

		if ((retval != 0)
		 || ((retval = sbuf_printf(sb, "</ctlportlist>\n")) != 0)) {
			retval = 0;
			sbuf_delete(sb);
			list->status = CTL_LUN_LIST_NEED_MORE_SPACE;
			snprintf(list->error_str, sizeof(list->error_str),
				 "Out of space, %d bytes is too small",
				 list->alloc_len);
			break;
		}

		sbuf_finish(sb);

		retval = copyout(sbuf_data(sb), list->lun_xml,
				 sbuf_len(sb) + 1);

		list->fill_len = sbuf_len(sb) + 1;
		list->status = CTL_LUN_LIST_OK;
		sbuf_delete(sb);
		break;
	}
	case CTL_LUN_MAP: {
		struct ctl_lun_map *lm  = (struct ctl_lun_map *)addr;
		struct ctl_port *port;

		mtx_lock(&softc->ctl_lock);
		if (lm->port < softc->port_min ||
		    lm->port >= softc->port_max ||
		    (port = softc->ctl_ports[lm->port]) == NULL) {
			mtx_unlock(&softc->ctl_lock);
			return (ENXIO);
		}
		if (port->status & CTL_PORT_STATUS_ONLINE) {
			STAILQ_FOREACH(lun, &softc->lun_list, links) {
				if (ctl_lun_map_to_port(port, lun->lun) ==
				    UINT32_MAX)
					continue;
				mtx_lock(&lun->lun_lock);
				ctl_est_ua_port(lun, lm->port, -1,
				    CTL_UA_LUN_CHANGE);
				mtx_unlock(&lun->lun_lock);
			}
		}
		mtx_unlock(&softc->ctl_lock); // XXX: port_enable sleeps
		if (lm->plun != UINT32_MAX) {
			if (lm->lun == UINT32_MAX)
				retval = ctl_lun_map_unset(port, lm->plun);
			else if (lm->lun < ctl_max_luns &&
			    softc->ctl_luns[lm->lun] != NULL)
				retval = ctl_lun_map_set(port, lm->plun, lm->lun);
			else
				return (ENXIO);
		} else {
			if (lm->lun == UINT32_MAX)
				retval = ctl_lun_map_deinit(port);
			else
				retval = ctl_lun_map_init(port);
		}
		if (port->status & CTL_PORT_STATUS_ONLINE)
			ctl_isc_announce_port(port);
		break;
	}
	case CTL_GET_LUN_STATS: {
		struct ctl_get_io_stats *stats = (struct ctl_get_io_stats *)addr;
		int i;

		/*
		 * XXX KDM no locking here.  If the LUN list changes,
		 * things can blow up.
		 */
		i = 0;
		stats->status = CTL_SS_OK;
		stats->fill_len = 0;
		STAILQ_FOREACH(lun, &softc->lun_list, links) {
			if (lun->lun < stats->first_item)
				continue;
			if (stats->fill_len + sizeof(lun->stats) >
			    stats->alloc_len) {
				stats->status = CTL_SS_NEED_MORE_SPACE;
				break;
			}
			retval = copyout(&lun->stats, &stats->stats[i++],
					 sizeof(lun->stats));
			if (retval != 0)
				break;
			stats->fill_len += sizeof(lun->stats);
		}
		stats->num_items = softc->num_luns;
		stats->flags = CTL_STATS_FLAG_NONE;
#ifdef CTL_TIME_IO
		stats->flags |= CTL_STATS_FLAG_TIME_VALID;
#endif
		getnanouptime(&stats->timestamp);
		break;
	}
	case CTL_GET_PORT_STATS: {
		struct ctl_get_io_stats *stats = (struct ctl_get_io_stats *)addr;
		int i;

		/*
		 * XXX KDM no locking here.  If the LUN list changes,
		 * things can blow up.
		 */
		i = 0;
		stats->status = CTL_SS_OK;
		stats->fill_len = 0;
		STAILQ_FOREACH(port, &softc->port_list, links) {
			if (port->targ_port < stats->first_item)
				continue;
			if (stats->fill_len + sizeof(port->stats) >
			    stats->alloc_len) {
				stats->status = CTL_SS_NEED_MORE_SPACE;
				break;
			}
			retval = copyout(&port->stats, &stats->stats[i++],
					 sizeof(port->stats));
			if (retval != 0)
				break;
			stats->fill_len += sizeof(port->stats);
		}
		stats->num_items = softc->num_ports;
		stats->flags = CTL_STATS_FLAG_NONE;
#ifdef CTL_TIME_IO
		stats->flags |= CTL_STATS_FLAG_TIME_VALID;
#endif
		getnanouptime(&stats->timestamp);
		break;
	}
	default: {
		/* XXX KDM should we fix this? */
#if 0
		struct ctl_backend_driver *backend;
		unsigned int type;
		int found;

		found = 0;

		/*
		 * We encode the backend type as the ioctl type for backend
		 * ioctls.  So parse it out here, and then search for a
		 * backend of this type.
		 */
		type = _IOC_TYPE(cmd);

		STAILQ_FOREACH(backend, &softc->be_list, links) {
			if (backend->type == type) {
				found = 1;
				break;
			}
		}
		if (found == 0) {
			printf("ctl: unknown ioctl command %#lx or backend "
			       "%d\n", cmd, type);
			retval = EINVAL;
			break;
		}
		retval = backend->ioctl(dev, cmd, addr, flag, td);
#endif
		retval = ENOTTY;
		break;
	}
	}
	return (retval);
}

uint32_t
ctl_get_initindex(struct ctl_nexus *nexus)
{
	return (nexus->initid + (nexus->targ_port * CTL_MAX_INIT_PER_PORT));
}

int
ctl_lun_map_init(struct ctl_port *port)
{
	struct ctl_softc *softc = port->ctl_softc;
	struct ctl_lun *lun;
	int size = ctl_lun_map_size;
	uint32_t i;

	if (port->lun_map == NULL || port->lun_map_size < size) {
		port->lun_map_size = 0;
		free(port->lun_map, M_CTL);
		port->lun_map = malloc(size * sizeof(uint32_t),
		    M_CTL, M_NOWAIT);
	}
	if (port->lun_map == NULL)
		return (ENOMEM);
	for (i = 0; i < size; i++)
		port->lun_map[i] = UINT32_MAX;
	port->lun_map_size = size;
	if (port->status & CTL_PORT_STATUS_ONLINE) {
		if (port->lun_disable != NULL) {
			STAILQ_FOREACH(lun, &softc->lun_list, links)
				port->lun_disable(port->targ_lun_arg, lun->lun);
		}
		ctl_isc_announce_port(port);
	}
	return (0);
}

int
ctl_lun_map_deinit(struct ctl_port *port)
{
	struct ctl_softc *softc = port->ctl_softc;
	struct ctl_lun *lun;

	if (port->lun_map == NULL)
		return (0);
	port->lun_map_size = 0;
	free(port->lun_map, M_CTL);
	port->lun_map = NULL;
	if (port->status & CTL_PORT_STATUS_ONLINE) {
		if (port->lun_enable != NULL) {
			STAILQ_FOREACH(lun, &softc->lun_list, links)
				port->lun_enable(port->targ_lun_arg, lun->lun);
		}
		ctl_isc_announce_port(port);
	}
	return (0);
}

int
ctl_lun_map_set(struct ctl_port *port, uint32_t plun, uint32_t glun)
{
	int status;
	uint32_t old;

	if (port->lun_map == NULL) {
		status = ctl_lun_map_init(port);
		if (status != 0)
			return (status);
	}
	if (plun >= port->lun_map_size)
		return (EINVAL);
	old = port->lun_map[plun];
	port->lun_map[plun] = glun;
	if ((port->status & CTL_PORT_STATUS_ONLINE) && old == UINT32_MAX) {
		if (port->lun_enable != NULL)
			port->lun_enable(port->targ_lun_arg, plun);
		ctl_isc_announce_port(port);
	}
	return (0);
}

int
ctl_lun_map_unset(struct ctl_port *port, uint32_t plun)
{
	uint32_t old;

	if (port->lun_map == NULL || plun >= port->lun_map_size)
		return (0);
	old = port->lun_map[plun];
	port->lun_map[plun] = UINT32_MAX;
	if ((port->status & CTL_PORT_STATUS_ONLINE) && old != UINT32_MAX) {
		if (port->lun_disable != NULL)
			port->lun_disable(port->targ_lun_arg, plun);
		ctl_isc_announce_port(port);
	}
	return (0);
}

uint32_t
ctl_lun_map_from_port(struct ctl_port *port, uint32_t lun_id)
{

	if (port == NULL)
		return (UINT32_MAX);
	if (port->lun_map == NULL)
		return (lun_id);
	if (lun_id > port->lun_map_size)
		return (UINT32_MAX);
	return (port->lun_map[lun_id]);
}

uint32_t
ctl_lun_map_to_port(struct ctl_port *port, uint32_t lun_id)
{
	uint32_t i;

	if (port == NULL)
		return (UINT32_MAX);
	if (port->lun_map == NULL)
		return (lun_id);
	for (i = 0; i < port->lun_map_size; i++) {
		if (port->lun_map[i] == lun_id)
			return (i);
	}
	return (UINT32_MAX);
}

uint32_t
ctl_decode_lun(uint64_t encoded)
{
	uint8_t lun[8];
	uint32_t result = 0xffffffff;

	be64enc(lun, encoded);
	switch (lun[0] & RPL_LUNDATA_ATYP_MASK) {
	case RPL_LUNDATA_ATYP_PERIPH:
		if ((lun[0] & 0x3f) == 0 && lun[2] == 0 && lun[3] == 0 &&
		    lun[4] == 0 && lun[5] == 0 && lun[6] == 0 && lun[7] == 0)
			result = lun[1];
		break;
	case RPL_LUNDATA_ATYP_FLAT:
		if (lun[2] == 0 && lun[3] == 0 && lun[4] == 0 && lun[5] == 0 &&
		    lun[6] == 0 && lun[7] == 0)
			result = ((lun[0] & 0x3f) << 8) + lun[1];
		break;
	case RPL_LUNDATA_ATYP_EXTLUN:
		switch (lun[0] & RPL_LUNDATA_EXT_EAM_MASK) {
		case 0x02:
			switch (lun[0] & RPL_LUNDATA_EXT_LEN_MASK) {
			case 0x00:
				result = lun[1];
				break;
			case 0x10:
				result = (lun[1] << 16) + (lun[2] << 8) +
				    lun[3];
				break;
			case 0x20:
				if (lun[1] == 0 && lun[6] == 0 && lun[7] == 0)
					result = (lun[2] << 24) +
					    (lun[3] << 16) + (lun[4] << 8) +
					    lun[5];
				break;
			}
			break;
		case RPL_LUNDATA_EXT_EAM_NOT_SPEC:
			result = 0xffffffff;
			break;
		}
		break;
	}
	return (result);
}

uint64_t
ctl_encode_lun(uint32_t decoded)
{
	uint64_t l = decoded;

	if (l <= 0xff)
		return (((uint64_t)RPL_LUNDATA_ATYP_PERIPH << 56) | (l << 48));
	if (l <= 0x3fff)
		return (((uint64_t)RPL_LUNDATA_ATYP_FLAT << 56) | (l << 48));
	if (l <= 0xffffff)
		return (((uint64_t)(RPL_LUNDATA_ATYP_EXTLUN | 0x12) << 56) |
		    (l << 32));
	return ((((uint64_t)RPL_LUNDATA_ATYP_EXTLUN | 0x22) << 56) | (l << 16));
}

int
ctl_ffz(uint32_t *mask, uint32_t first, uint32_t last)
{
	int i;

	for (i = first; i < last; i++) {
		if ((mask[i / 32] & (1 << (i % 32))) == 0)
			return (i);
	}
	return (-1);
}

int
ctl_set_mask(uint32_t *mask, uint32_t bit)
{
	uint32_t chunk, piece;

	chunk = bit >> 5;
	piece = bit % (sizeof(uint32_t) * 8);

	if ((mask[chunk] & (1 << piece)) != 0)
		return (-1);
	else
		mask[chunk] |= (1 << piece);

	return (0);
}

int
ctl_clear_mask(uint32_t *mask, uint32_t bit)
{
	uint32_t chunk, piece;

	chunk = bit >> 5;
	piece = bit % (sizeof(uint32_t) * 8);

	if ((mask[chunk] & (1 << piece)) == 0)
		return (-1);
	else
		mask[chunk] &= ~(1 << piece);

	return (0);
}

int
ctl_is_set(uint32_t *mask, uint32_t bit)
{
	uint32_t chunk, piece;

	chunk = bit >> 5;
	piece = bit % (sizeof(uint32_t) * 8);

	if ((mask[chunk] & (1 << piece)) == 0)
		return (0);
	else
		return (1);
}

static uint64_t
ctl_get_prkey(struct ctl_lun *lun, uint32_t residx)
{
	uint64_t *t;

	t = lun->pr_keys[residx/CTL_MAX_INIT_PER_PORT];
	if (t == NULL)
		return (0);
	return (t[residx % CTL_MAX_INIT_PER_PORT]);
}

static void
ctl_clr_prkey(struct ctl_lun *lun, uint32_t residx)
{
	uint64_t *t;

	t = lun->pr_keys[residx/CTL_MAX_INIT_PER_PORT];
	if (t == NULL)
		return;
	t[residx % CTL_MAX_INIT_PER_PORT] = 0;
}

static void
ctl_alloc_prkey(struct ctl_lun *lun, uint32_t residx)
{
	uint64_t *p;
	u_int i;

	i = residx/CTL_MAX_INIT_PER_PORT;
	if (lun->pr_keys[i] != NULL)
		return;
	mtx_unlock(&lun->lun_lock);
	p = malloc(sizeof(uint64_t) * CTL_MAX_INIT_PER_PORT, M_CTL,
	    M_WAITOK | M_ZERO);
	mtx_lock(&lun->lun_lock);
	if (lun->pr_keys[i] == NULL)
		lun->pr_keys[i] = p;
	else
		free(p, M_CTL);
}

static void
ctl_set_prkey(struct ctl_lun *lun, uint32_t residx, uint64_t key)
{
	uint64_t *t;

	t = lun->pr_keys[residx/CTL_MAX_INIT_PER_PORT];
	KASSERT(t != NULL, ("prkey %d is not allocated", residx));
	t[residx % CTL_MAX_INIT_PER_PORT] = key;
}

/*
 * ctl_softc, pool_name, total_ctl_io are passed in.
 * npool is passed out.
 */
int
ctl_pool_create(struct ctl_softc *ctl_softc, const char *pool_name,
		uint32_t total_ctl_io, void **npool)
{
	struct ctl_io_pool *pool;

	pool = (struct ctl_io_pool *)malloc(sizeof(*pool), M_CTL,
					    M_NOWAIT | M_ZERO);
	if (pool == NULL)
		return (ENOMEM);

	snprintf(pool->name, sizeof(pool->name), "CTL IO %s", pool_name);
	pool->ctl_softc = ctl_softc;
#ifdef IO_POOLS
	pool->zone = uma_zsecond_create(pool->name, NULL,
	    NULL, NULL, NULL, ctl_softc->io_zone);
	/* uma_prealloc(pool->zone, total_ctl_io); */
#else
	pool->zone = ctl_softc->io_zone;
#endif

	*npool = pool;
	return (0);
}

void
ctl_pool_free(struct ctl_io_pool *pool)
{

	if (pool == NULL)
		return;

#ifdef IO_POOLS
	uma_zdestroy(pool->zone);
#endif
	free(pool, M_CTL);
}

union ctl_io *
ctl_alloc_io(void *pool_ref)
{
	struct ctl_io_pool *pool = (struct ctl_io_pool *)pool_ref;
	union ctl_io *io;

	io = uma_zalloc(pool->zone, M_WAITOK);
	if (io != NULL) {
		io->io_hdr.pool = pool_ref;
		CTL_SOFTC(io) = pool->ctl_softc;
		TAILQ_INIT(&io->io_hdr.blocked_queue);
	}
	return (io);
}

union ctl_io *
ctl_alloc_io_nowait(void *pool_ref)
{
	struct ctl_io_pool *pool = (struct ctl_io_pool *)pool_ref;
	union ctl_io *io;

	io = uma_zalloc(pool->zone, M_NOWAIT);
	if (io != NULL) {
		io->io_hdr.pool = pool_ref;
		CTL_SOFTC(io) = pool->ctl_softc;
		TAILQ_INIT(&io->io_hdr.blocked_queue);
	}
	return (io);
}

void
ctl_free_io(union ctl_io *io)
{
	struct ctl_io_pool *pool;

	if (io == NULL)
		return;

	pool = (struct ctl_io_pool *)io->io_hdr.pool;
	uma_zfree(pool->zone, io);
}

void
ctl_zero_io(union ctl_io *io)
{
	struct ctl_io_pool *pool;

	if (io == NULL)
		return;

	/*
	 * May need to preserve linked list pointers at some point too.
	 */
	pool = io->io_hdr.pool;
	memset(io, 0, sizeof(*io));
	io->io_hdr.pool = pool;
	CTL_SOFTC(io) = pool->ctl_softc;
	TAILQ_INIT(&io->io_hdr.blocked_queue);
}

int
ctl_expand_number(const char *buf, uint64_t *num)
{
	char *endptr;
	uint64_t number;
	unsigned shift;

	number = strtoq(buf, &endptr, 0);

	switch (tolower((unsigned char)*endptr)) {
	case 'e':
		shift = 60;
		break;
	case 'p':
		shift = 50;
		break;
	case 't':
		shift = 40;
		break;
	case 'g':
		shift = 30;
		break;
	case 'm':
		shift = 20;
		break;
	case 'k':
		shift = 10;
		break;
	case 'b':
	case '\0': /* No unit. */
		*num = number;
		return (0);
	default:
		/* Unrecognized unit. */
		return (-1);
	}

	if ((number << shift) >> shift != number) {
		/* Overflow */
		return (-1);
	}
	*num = number << shift;
	return (0);
}


/*
 * This routine could be used in the future to load default and/or saved
 * mode page parameters for a particuar lun.
 */
static int
ctl_init_page_index(struct ctl_lun *lun)
{
	int i, page_code;
	struct ctl_page_index *page_index;
	const char *value;
	uint64_t ival;

	memcpy(&lun->mode_pages.index, page_index_template,
	       sizeof(page_index_template));

	for (i = 0; i < CTL_NUM_MODE_PAGES; i++) {

		page_index = &lun->mode_pages.index[i];
		if (lun->be_lun->lun_type == T_DIRECT &&
		    (page_index->page_flags & CTL_PAGE_FLAG_DIRECT) == 0)
			continue;
		if (lun->be_lun->lun_type == T_PROCESSOR &&
		    (page_index->page_flags & CTL_PAGE_FLAG_PROC) == 0)
			continue;
		if (lun->be_lun->lun_type == T_CDROM &&
		    (page_index->page_flags & CTL_PAGE_FLAG_CDROM) == 0)
			continue;

		page_code = page_index->page_code & SMPH_PC_MASK;
		switch (page_code) {
		case SMS_RW_ERROR_RECOVERY_PAGE: {
			KASSERT(page_index->subpage == SMS_SUBPAGE_PAGE_0,
			    ("subpage %#x for page %#x is incorrect!",
			    page_index->subpage, page_code));
			memcpy(&lun->mode_pages.rw_er_page[CTL_PAGE_CURRENT],
			       &rw_er_page_default,
			       sizeof(rw_er_page_default));
			memcpy(&lun->mode_pages.rw_er_page[CTL_PAGE_CHANGEABLE],
			       &rw_er_page_changeable,
			       sizeof(rw_er_page_changeable));
			memcpy(&lun->mode_pages.rw_er_page[CTL_PAGE_DEFAULT],
			       &rw_er_page_default,
			       sizeof(rw_er_page_default));
			memcpy(&lun->mode_pages.rw_er_page[CTL_PAGE_SAVED],
			       &rw_er_page_default,
			       sizeof(rw_er_page_default));
			page_index->page_data =
				(uint8_t *)lun->mode_pages.rw_er_page;
			break;
		}
		case SMS_FORMAT_DEVICE_PAGE: {
			struct scsi_format_page *format_page;

			KASSERT(page_index->subpage == SMS_SUBPAGE_PAGE_0,
			    ("subpage %#x for page %#x is incorrect!",
			    page_index->subpage, page_code));

			/*
			 * Sectors per track are set above.  Bytes per
			 * sector need to be set here on a per-LUN basis.
			 */
			memcpy(&lun->mode_pages.format_page[CTL_PAGE_CURRENT],
			       &format_page_default,
			       sizeof(format_page_default));
			memcpy(&lun->mode_pages.format_page[
			       CTL_PAGE_CHANGEABLE], &format_page_changeable,
			       sizeof(format_page_changeable));
			memcpy(&lun->mode_pages.format_page[CTL_PAGE_DEFAULT],
			       &format_page_default,
			       sizeof(format_page_default));
			memcpy(&lun->mode_pages.format_page[CTL_PAGE_SAVED],
			       &format_page_default,
			       sizeof(format_page_default));

			format_page = &lun->mode_pages.format_page[
				CTL_PAGE_CURRENT];
			scsi_ulto2b(lun->be_lun->blocksize,
				    format_page->bytes_per_sector);

			format_page = &lun->mode_pages.format_page[
				CTL_PAGE_DEFAULT];
			scsi_ulto2b(lun->be_lun->blocksize,
				    format_page->bytes_per_sector);

			format_page = &lun->mode_pages.format_page[
				CTL_PAGE_SAVED];
			scsi_ulto2b(lun->be_lun->blocksize,
				    format_page->bytes_per_sector);

			page_index->page_data =
				(uint8_t *)lun->mode_pages.format_page;
			break;
		}
		case SMS_RIGID_DISK_PAGE: {
			struct scsi_rigid_disk_page *rigid_disk_page;
			uint32_t sectors_per_cylinder;
			uint64_t cylinders;
#ifndef	__XSCALE__
			int shift;
#endif /* !__XSCALE__ */

			KASSERT(page_index->subpage == SMS_SUBPAGE_PAGE_0,
			    ("subpage %#x for page %#x is incorrect!",
			    page_index->subpage, page_code));

			/*
			 * Rotation rate and sectors per track are set
			 * above.  We calculate the cylinders here based on
			 * capacity.  Due to the number of heads and
			 * sectors per track we're using, smaller arrays
			 * may turn out to have 0 cylinders.  Linux and
			 * FreeBSD don't pay attention to these mode pages
			 * to figure out capacity, but Solaris does.  It
			 * seems to deal with 0 cylinders just fine, and
			 * works out a fake geometry based on the capacity.
			 */
			memcpy(&lun->mode_pages.rigid_disk_page[
			       CTL_PAGE_DEFAULT], &rigid_disk_page_default,
			       sizeof(rigid_disk_page_default));
			memcpy(&lun->mode_pages.rigid_disk_page[
			       CTL_PAGE_CHANGEABLE],&rigid_disk_page_changeable,
			       sizeof(rigid_disk_page_changeable));

			sectors_per_cylinder = CTL_DEFAULT_SECTORS_PER_TRACK *
				CTL_DEFAULT_HEADS;

			/*
			 * The divide method here will be more accurate,
			 * probably, but results in floating point being
			 * used in the kernel on i386 (__udivdi3()).  On the
			 * XScale, though, __udivdi3() is implemented in
			 * software.
			 *
			 * The shift method for cylinder calculation is
			 * accurate if sectors_per_cylinder is a power of
			 * 2.  Otherwise it might be slightly off -- you
			 * might have a bit of a truncation problem.
			 */
#ifdef	__XSCALE__
			cylinders = (lun->be_lun->maxlba + 1) /
				sectors_per_cylinder;
#else
			for (shift = 31; shift > 0; shift--) {
				if (sectors_per_cylinder & (1 << shift))
					break;
			}
			cylinders = (lun->be_lun->maxlba + 1) >> shift;
#endif

			/*
			 * We've basically got 3 bytes, or 24 bits for the
			 * cylinder size in the mode page.  If we're over,
			 * just round down to 2^24.
			 */
			if (cylinders > 0xffffff)
				cylinders = 0xffffff;

			rigid_disk_page = &lun->mode_pages.rigid_disk_page[
				CTL_PAGE_DEFAULT];
			scsi_ulto3b(cylinders, rigid_disk_page->cylinders);

			if ((value = dnvlist_get_string(lun->be_lun->options,
			    "rpm", NULL)) != NULL) {
				scsi_ulto2b(strtol(value, NULL, 0),
				     rigid_disk_page->rotation_rate);
			}

			memcpy(&lun->mode_pages.rigid_disk_page[CTL_PAGE_CURRENT],
			       &lun->mode_pages.rigid_disk_page[CTL_PAGE_DEFAULT],
			       sizeof(rigid_disk_page_default));
			memcpy(&lun->mode_pages.rigid_disk_page[CTL_PAGE_SAVED],
			       &lun->mode_pages.rigid_disk_page[CTL_PAGE_DEFAULT],
			       sizeof(rigid_disk_page_default));

			page_index->page_data =
				(uint8_t *)lun->mode_pages.rigid_disk_page;
			break;
		}
		case SMS_VERIFY_ERROR_RECOVERY_PAGE: {
			KASSERT(page_index->subpage == SMS_SUBPAGE_PAGE_0,
			    ("subpage %#x for page %#x is incorrect!",
			    page_index->subpage, page_code));
			memcpy(&lun->mode_pages.verify_er_page[CTL_PAGE_CURRENT],
			       &verify_er_page_default,
			       sizeof(verify_er_page_default));
			memcpy(&lun->mode_pages.verify_er_page[CTL_PAGE_CHANGEABLE],
			       &verify_er_page_changeable,
			       sizeof(verify_er_page_changeable));
			memcpy(&lun->mode_pages.verify_er_page[CTL_PAGE_DEFAULT],
			       &verify_er_page_default,
			       sizeof(verify_er_page_default));
			memcpy(&lun->mode_pages.verify_er_page[CTL_PAGE_SAVED],
			       &verify_er_page_default,
			       sizeof(verify_er_page_default));
			page_index->page_data =
				(uint8_t *)lun->mode_pages.verify_er_page;
			break;
		}
		case SMS_CACHING_PAGE: {
			struct scsi_caching_page *caching_page;

			KASSERT(page_index->subpage == SMS_SUBPAGE_PAGE_0,
			    ("subpage %#x for page %#x is incorrect!",
			    page_index->subpage, page_code));
			memcpy(&lun->mode_pages.caching_page[CTL_PAGE_DEFAULT],
			       &caching_page_default,
			       sizeof(caching_page_default));
			memcpy(&lun->mode_pages.caching_page[
			       CTL_PAGE_CHANGEABLE], &caching_page_changeable,
			       sizeof(caching_page_changeable));
			memcpy(&lun->mode_pages.caching_page[CTL_PAGE_SAVED],
			       &caching_page_default,
			       sizeof(caching_page_default));
			caching_page = &lun->mode_pages.caching_page[
			    CTL_PAGE_SAVED];
			value = dnvlist_get_string(lun->be_lun->options,
			    "writecache", NULL);
			if (value != NULL && strcmp(value, "off") == 0)
				caching_page->flags1 &= ~SCP_WCE;
			value = dnvlist_get_string(lun->be_lun->options,
			    "readcache", NULL);
			if (value != NULL && strcmp(value, "off") == 0)
				caching_page->flags1 |= SCP_RCD;
			memcpy(&lun->mode_pages.caching_page[CTL_PAGE_CURRENT],
			       &lun->mode_pages.caching_page[CTL_PAGE_SAVED],
			       sizeof(caching_page_default));
			page_index->page_data =
				(uint8_t *)lun->mode_pages.caching_page;
			break;
		}
		case SMS_CONTROL_MODE_PAGE: {
			switch (page_index->subpage) {
			case SMS_SUBPAGE_PAGE_0: {
				struct scsi_control_page *control_page;

				memcpy(&lun->mode_pages.control_page[
				    CTL_PAGE_DEFAULT],
				       &control_page_default,
				       sizeof(control_page_default));
				memcpy(&lun->mode_pages.control_page[
				    CTL_PAGE_CHANGEABLE],
				       &control_page_changeable,
				       sizeof(control_page_changeable));
				memcpy(&lun->mode_pages.control_page[
				    CTL_PAGE_SAVED],
				       &control_page_default,
				       sizeof(control_page_default));
				control_page = &lun->mode_pages.control_page[
				    CTL_PAGE_SAVED];
				value = dnvlist_get_string(lun->be_lun->options,
				    "reordering", NULL);
				if (value != NULL &&
				    strcmp(value, "unrestricted") == 0) {
					control_page->queue_flags &=
					    ~SCP_QUEUE_ALG_MASK;
					control_page->queue_flags |=
					    SCP_QUEUE_ALG_UNRESTRICTED;
				}
				memcpy(&lun->mode_pages.control_page[
				    CTL_PAGE_CURRENT],
				       &lun->mode_pages.control_page[
				    CTL_PAGE_SAVED],
				       sizeof(control_page_default));
				page_index->page_data =
				    (uint8_t *)lun->mode_pages.control_page;
				break;
			}
			case 0x01:
				memcpy(&lun->mode_pages.control_ext_page[
				    CTL_PAGE_DEFAULT],
				       &control_ext_page_default,
				       sizeof(control_ext_page_default));
				memcpy(&lun->mode_pages.control_ext_page[
				    CTL_PAGE_CHANGEABLE],
				       &control_ext_page_changeable,
				       sizeof(control_ext_page_changeable));
				memcpy(&lun->mode_pages.control_ext_page[
				    CTL_PAGE_SAVED],
				       &control_ext_page_default,
				       sizeof(control_ext_page_default));
				memcpy(&lun->mode_pages.control_ext_page[
				    CTL_PAGE_CURRENT],
				       &lun->mode_pages.control_ext_page[
				    CTL_PAGE_SAVED],
				       sizeof(control_ext_page_default));
				page_index->page_data =
				    (uint8_t *)lun->mode_pages.control_ext_page;
				break;
			default:
				panic("subpage %#x for page %#x is incorrect!",
				      page_index->subpage, page_code);
			}
			break;
		}
		case SMS_INFO_EXCEPTIONS_PAGE: {
			switch (page_index->subpage) {
			case SMS_SUBPAGE_PAGE_0:
				memcpy(&lun->mode_pages.ie_page[CTL_PAGE_CURRENT],
				       &ie_page_default,
				       sizeof(ie_page_default));
				memcpy(&lun->mode_pages.ie_page[
				       CTL_PAGE_CHANGEABLE], &ie_page_changeable,
				       sizeof(ie_page_changeable));
				memcpy(&lun->mode_pages.ie_page[CTL_PAGE_DEFAULT],
				       &ie_page_default,
				       sizeof(ie_page_default));
				memcpy(&lun->mode_pages.ie_page[CTL_PAGE_SAVED],
				       &ie_page_default,
				       sizeof(ie_page_default));
				page_index->page_data =
					(uint8_t *)lun->mode_pages.ie_page;
				break;
			case 0x02: {
				struct ctl_logical_block_provisioning_page *page;

				memcpy(&lun->mode_pages.lbp_page[CTL_PAGE_DEFAULT],
				       &lbp_page_default,
				       sizeof(lbp_page_default));
				memcpy(&lun->mode_pages.lbp_page[
				       CTL_PAGE_CHANGEABLE], &lbp_page_changeable,
				       sizeof(lbp_page_changeable));
				memcpy(&lun->mode_pages.lbp_page[CTL_PAGE_SAVED],
				       &lbp_page_default,
				       sizeof(lbp_page_default));
				page = &lun->mode_pages.lbp_page[CTL_PAGE_SAVED];
				value = dnvlist_get_string(lun->be_lun->options,
				    "avail-threshold", NULL);
				if (value != NULL &&
				    ctl_expand_number(value, &ival) == 0) {
					page->descr[0].flags |= SLBPPD_ENABLED |
					    SLBPPD_ARMING_DEC;
					if (lun->be_lun->blocksize)
						ival /= lun->be_lun->blocksize;
					else
						ival /= 512;
					scsi_ulto4b(ival >> CTL_LBP_EXPONENT,
					    page->descr[0].count);
				}
				value = dnvlist_get_string(lun->be_lun->options,
				    "used-threshold", NULL);
				if (value != NULL &&
				    ctl_expand_number(value, &ival) == 0) {
					page->descr[1].flags |= SLBPPD_ENABLED |
					    SLBPPD_ARMING_INC;
					if (lun->be_lun->blocksize)
						ival /= lun->be_lun->blocksize;
					else
						ival /= 512;
					scsi_ulto4b(ival >> CTL_LBP_EXPONENT,
					    page->descr[1].count);
				}
				value = dnvlist_get_string(lun->be_lun->options,
				    "pool-avail-threshold", NULL);
				if (value != NULL &&
				    ctl_expand_number(value, &ival) == 0) {
					page->descr[2].flags |= SLBPPD_ENABLED |
					    SLBPPD_ARMING_DEC;
					if (lun->be_lun->blocksize)
						ival /= lun->be_lun->blocksize;
					else
						ival /= 512;
					scsi_ulto4b(ival >> CTL_LBP_EXPONENT,
					    page->descr[2].count);
				}
				value = dnvlist_get_string(lun->be_lun->options,
				    "pool-used-threshold", NULL);
				if (value != NULL &&
				    ctl_expand_number(value, &ival) == 0) {
					page->descr[3].flags |= SLBPPD_ENABLED |
					    SLBPPD_ARMING_INC;
					if (lun->be_lun->blocksize)
						ival /= lun->be_lun->blocksize;
					else
						ival /= 512;
					scsi_ulto4b(ival >> CTL_LBP_EXPONENT,
					    page->descr[3].count);
				}
				memcpy(&lun->mode_pages.lbp_page[CTL_PAGE_CURRENT],
				       &lun->mode_pages.lbp_page[CTL_PAGE_SAVED],
				       sizeof(lbp_page_default));
				page_index->page_data =
					(uint8_t *)lun->mode_pages.lbp_page;
				break;
			}
			default:
				panic("subpage %#x for page %#x is incorrect!",
				      page_index->subpage, page_code);
			}
			break;
		}
		case SMS_CDDVD_CAPS_PAGE:{
			KASSERT(page_index->subpage == SMS_SUBPAGE_PAGE_0,
			    ("subpage %#x for page %#x is incorrect!",
			    page_index->subpage, page_code));
			memcpy(&lun->mode_pages.cddvd_page[CTL_PAGE_DEFAULT],
			       &cddvd_page_default,
			       sizeof(cddvd_page_default));
			memcpy(&lun->mode_pages.cddvd_page[
			       CTL_PAGE_CHANGEABLE], &cddvd_page_changeable,
			       sizeof(cddvd_page_changeable));
			memcpy(&lun->mode_pages.cddvd_page[CTL_PAGE_SAVED],
			       &cddvd_page_default,
			       sizeof(cddvd_page_default));
			memcpy(&lun->mode_pages.cddvd_page[CTL_PAGE_CURRENT],
			       &lun->mode_pages.cddvd_page[CTL_PAGE_SAVED],
			       sizeof(cddvd_page_default));
			page_index->page_data =
				(uint8_t *)lun->mode_pages.cddvd_page;
			break;
		}
		default:
			panic("invalid page code value %#x", page_code);
		}
	}

	return (CTL_RETVAL_COMPLETE);
}

static int
ctl_init_log_page_index(struct ctl_lun *lun)
{
	struct ctl_page_index *page_index;
	int i, j, k, prev;

	memcpy(&lun->log_pages.index, log_page_index_template,
	       sizeof(log_page_index_template));

	prev = -1;
	for (i = 0, j = 0, k = 0; i < CTL_NUM_LOG_PAGES; i++) {

		page_index = &lun->log_pages.index[i];
		if (lun->be_lun->lun_type == T_DIRECT &&
		    (page_index->page_flags & CTL_PAGE_FLAG_DIRECT) == 0)
			continue;
		if (lun->be_lun->lun_type == T_PROCESSOR &&
		    (page_index->page_flags & CTL_PAGE_FLAG_PROC) == 0)
			continue;
		if (lun->be_lun->lun_type == T_CDROM &&
		    (page_index->page_flags & CTL_PAGE_FLAG_CDROM) == 0)
			continue;

		if (page_index->page_code == SLS_LOGICAL_BLOCK_PROVISIONING &&
		    lun->backend->lun_attr == NULL)
			continue;

		if (page_index->page_code != prev) {
			lun->log_pages.pages_page[j] = page_index->page_code;
			prev = page_index->page_code;
			j++;
		}
		lun->log_pages.subpages_page[k*2] = page_index->page_code;
		lun->log_pages.subpages_page[k*2+1] = page_index->subpage;
		k++;
	}
	lun->log_pages.index[0].page_data = &lun->log_pages.pages_page[0];
	lun->log_pages.index[0].page_len = j;
	lun->log_pages.index[1].page_data = &lun->log_pages.subpages_page[0];
	lun->log_pages.index[1].page_len = k * 2;
	lun->log_pages.index[2].page_data = &lun->log_pages.lbp_page[0];
	lun->log_pages.index[2].page_len = 12*CTL_NUM_LBP_PARAMS;
	lun->log_pages.index[3].page_data = (uint8_t *)&lun->log_pages.stat_page;
	lun->log_pages.index[3].page_len = sizeof(lun->log_pages.stat_page);
	lun->log_pages.index[4].page_data = (uint8_t *)&lun->log_pages.ie_page;
	lun->log_pages.index[4].page_len = sizeof(lun->log_pages.ie_page);

	return (CTL_RETVAL_COMPLETE);
}

static int
hex2bin(const char *str, uint8_t *buf, int buf_size)
{
	int i;
	u_char c;

	memset(buf, 0, buf_size);
	while (isspace(str[0]))
		str++;
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
		str += 2;
	buf_size *= 2;
	for (i = 0; str[i] != 0 && i < buf_size; i++) {
		while (str[i] == '-')	/* Skip dashes in UUIDs. */
			str++;
		c = str[i];
		if (isdigit(c))
			c -= '0';
		else if (isalpha(c))
			c -= isupper(c) ? 'A' - 10 : 'a' - 10;
		else
			break;
		if (c >= 16)
			break;
		if ((i & 1) == 0)
			buf[i / 2] |= (c << 4);
		else
			buf[i / 2] |= c;
	}
	return ((i + 1) / 2);
}

/*
 * LUN allocation.
 *
 * Requirements:
 * - caller allocates and zeros LUN storage, or passes in a NULL LUN if he
 *   wants us to allocate the LUN and he can block.
 * - ctl_softc is always set
 * - be_lun is set if the LUN has a backend (needed for disk LUNs)
 *
 * Returns 0 for success, non-zero (errno) for failure.
 */
static int
ctl_alloc_lun(struct ctl_softc *ctl_softc, struct ctl_lun *ctl_lun,
	      struct ctl_be_lun *const be_lun)
{
	struct ctl_lun *nlun, *lun;
	struct scsi_vpd_id_descriptor *desc;
	struct scsi_vpd_id_t10 *t10id;
	const char *eui, *naa, *scsiname, *uuid, *vendor, *value;
	int lun_number, lun_malloced;
	int devidlen, idlen1, idlen2 = 0, len;

	if (be_lun == NULL)
		return (EINVAL);

	/*
	 * We currently only support Direct Access or Processor LUN types.
	 */
	switch (be_lun->lun_type) {
	case T_DIRECT:
	case T_PROCESSOR:
	case T_CDROM:
		break;
	case T_SEQUENTIAL:
	case T_CHANGER:
	default:
		be_lun->lun_config_status(be_lun->be_lun,
					  CTL_LUN_CONFIG_FAILURE);
		break;
	}
	if (ctl_lun == NULL) {
		lun = malloc(sizeof(*lun), M_CTL, M_WAITOK);
		lun_malloced = 1;
	} else {
		lun_malloced = 0;
		lun = ctl_lun;
	}

	memset(lun, 0, sizeof(*lun));
	if (lun_malloced)
		lun->flags = CTL_LUN_MALLOCED;

	lun->pending_sense = malloc(sizeof(struct scsi_sense_data *) *
	    ctl_max_ports, M_DEVBUF, M_WAITOK | M_ZERO);
	lun->pending_ua = malloc(sizeof(ctl_ua_type *) * ctl_max_ports,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	lun->pr_keys = malloc(sizeof(uint64_t *) * ctl_max_ports,
	    M_DEVBUF, M_WAITOK | M_ZERO);

	/* Generate LUN ID. */
	devidlen = max(CTL_DEVID_MIN_LEN,
	    strnlen(be_lun->device_id, CTL_DEVID_LEN));
	idlen1 = sizeof(*t10id) + devidlen;
	len = sizeof(struct scsi_vpd_id_descriptor) + idlen1;
	scsiname = dnvlist_get_string(be_lun->options, "scsiname", NULL);
	if (scsiname != NULL) {
		idlen2 = roundup2(strlen(scsiname) + 1, 4);
		len += sizeof(struct scsi_vpd_id_descriptor) + idlen2;
	}
	eui = dnvlist_get_string(be_lun->options, "eui", NULL);
	if (eui != NULL) {
		len += sizeof(struct scsi_vpd_id_descriptor) + 16;
	}
	naa = dnvlist_get_string(be_lun->options, "naa", NULL);
	if (naa != NULL) {
		len += sizeof(struct scsi_vpd_id_descriptor) + 16;
	}
	uuid = dnvlist_get_string(be_lun->options, "uuid", NULL);
	if (uuid != NULL) {
		len += sizeof(struct scsi_vpd_id_descriptor) + 18;
	}
	lun->lun_devid = malloc(sizeof(struct ctl_devid) + len,
	    M_CTL, M_WAITOK | M_ZERO);
	desc = (struct scsi_vpd_id_descriptor *)lun->lun_devid->data;
	desc->proto_codeset = SVPD_ID_CODESET_ASCII;
	desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_LUN | SVPD_ID_TYPE_T10;
	desc->length = idlen1;
	t10id = (struct scsi_vpd_id_t10 *)&desc->identifier[0];
	memset(t10id->vendor, ' ', sizeof(t10id->vendor));
	if ((vendor = dnvlist_get_string(be_lun->options, "vendor", NULL)) == NULL) {
		strncpy((char *)t10id->vendor, CTL_VENDOR, sizeof(t10id->vendor));
	} else {
		strncpy(t10id->vendor, vendor,
		    min(sizeof(t10id->vendor), strlen(vendor)));
	}
	strncpy((char *)t10id->vendor_spec_id,
	    (char *)be_lun->device_id, devidlen);
	if (scsiname != NULL) {
		desc = (struct scsi_vpd_id_descriptor *)(&desc->identifier[0] +
		    desc->length);
		desc->proto_codeset = SVPD_ID_CODESET_UTF8;
		desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_LUN |
		    SVPD_ID_TYPE_SCSI_NAME;
		desc->length = idlen2;
		strlcpy(desc->identifier, scsiname, idlen2);
	}
	if (eui != NULL) {
		desc = (struct scsi_vpd_id_descriptor *)(&desc->identifier[0] +
		    desc->length);
		desc->proto_codeset = SVPD_ID_CODESET_BINARY;
		desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_LUN |
		    SVPD_ID_TYPE_EUI64;
		desc->length = hex2bin(eui, desc->identifier, 16);
		desc->length = desc->length > 12 ? 16 :
		    (desc->length > 8 ? 12 : 8);
		len -= 16 - desc->length;
	}
	if (naa != NULL) {
		desc = (struct scsi_vpd_id_descriptor *)(&desc->identifier[0] +
		    desc->length);
		desc->proto_codeset = SVPD_ID_CODESET_BINARY;
		desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_LUN |
		    SVPD_ID_TYPE_NAA;
		desc->length = hex2bin(naa, desc->identifier, 16);
		desc->length = desc->length > 8 ? 16 : 8;
		len -= 16 - desc->length;
	}
	if (uuid != NULL) {
		desc = (struct scsi_vpd_id_descriptor *)(&desc->identifier[0] +
		    desc->length);
		desc->proto_codeset = SVPD_ID_CODESET_BINARY;
		desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_LUN |
		    SVPD_ID_TYPE_UUID;
		desc->identifier[0] = 0x10;
		hex2bin(uuid, &desc->identifier[2], 16);
		desc->length = 18;
	}
	lun->lun_devid->len = len;

	mtx_lock(&ctl_softc->ctl_lock);
	/*
	 * See if the caller requested a particular LUN number.  If so, see
	 * if it is available.  Otherwise, allocate the first available LUN.
	 */
	if (be_lun->flags & CTL_LUN_FLAG_ID_REQ) {
		if ((be_lun->req_lun_id > (ctl_max_luns - 1))
		 || (ctl_is_set(ctl_softc->ctl_lun_mask, be_lun->req_lun_id))) {
			mtx_unlock(&ctl_softc->ctl_lock);
			if (be_lun->req_lun_id > (ctl_max_luns - 1)) {
				printf("ctl: requested LUN ID %d is higher "
				       "than ctl_max_luns - 1 (%d)\n",
				       be_lun->req_lun_id, ctl_max_luns - 1);
			} else {
				/*
				 * XXX KDM return an error, or just assign
				 * another LUN ID in this case??
				 */
				printf("ctl: requested LUN ID %d is already "
				       "in use\n", be_lun->req_lun_id);
			}
fail:
			free(lun->lun_devid, M_CTL);
			if (lun->flags & CTL_LUN_MALLOCED)
				free(lun, M_CTL);
			be_lun->lun_config_status(be_lun->be_lun,
						  CTL_LUN_CONFIG_FAILURE);
			return (ENOSPC);
		}
		lun_number = be_lun->req_lun_id;
	} else {
		lun_number = ctl_ffz(ctl_softc->ctl_lun_mask, 0, ctl_max_luns);
		if (lun_number == -1) {
			mtx_unlock(&ctl_softc->ctl_lock);
			printf("ctl: can't allocate LUN, out of LUNs\n");
			goto fail;
		}
	}
	ctl_set_mask(ctl_softc->ctl_lun_mask, lun_number);
	mtx_unlock(&ctl_softc->ctl_lock);

	mtx_init(&lun->lun_lock, "CTL LUN", NULL, MTX_DEF);
	lun->lun = lun_number;
	lun->be_lun = be_lun;
	/*
	 * The processor LUN is always enabled.  Disk LUNs come on line
	 * disabled, and must be enabled by the backend.
	 */
	lun->flags |= CTL_LUN_DISABLED;
	lun->backend = be_lun->be;
	be_lun->ctl_lun = lun;
	be_lun->lun_id = lun_number;
	atomic_add_int(&be_lun->be->num_luns, 1);
	if (be_lun->flags & CTL_LUN_FLAG_EJECTED)
		lun->flags |= CTL_LUN_EJECTED;
	if (be_lun->flags & CTL_LUN_FLAG_NO_MEDIA)
		lun->flags |= CTL_LUN_NO_MEDIA;
	if (be_lun->flags & CTL_LUN_FLAG_STOPPED)
		lun->flags |= CTL_LUN_STOPPED;

	if (be_lun->flags & CTL_LUN_FLAG_PRIMARY)
		lun->flags |= CTL_LUN_PRIMARY_SC;

	value = dnvlist_get_string(be_lun->options, "removable", NULL);
	if (value != NULL) {
		if (strcmp(value, "on") == 0)
			lun->flags |= CTL_LUN_REMOVABLE;
	} else if (be_lun->lun_type == T_CDROM)
		lun->flags |= CTL_LUN_REMOVABLE;

	lun->ctl_softc = ctl_softc;
#ifdef CTL_TIME_IO
	lun->last_busy = getsbinuptime();
#endif
	TAILQ_INIT(&lun->ooa_queue);
	STAILQ_INIT(&lun->error_list);
	lun->ie_reported = 1;
	callout_init_mtx(&lun->ie_callout, &lun->lun_lock, 0);
	ctl_tpc_lun_init(lun);
	if (lun->flags & CTL_LUN_REMOVABLE) {
		lun->prevent = malloc((CTL_MAX_INITIATORS + 31) / 32 * 4,
		    M_CTL, M_WAITOK);
	}

	/*
	 * Initialize the mode and log page index.
	 */
	ctl_init_page_index(lun);
	ctl_init_log_page_index(lun);

	/* Setup statistics gathering */
	lun->stats.item = lun_number;

	/*
	 * Now, before we insert this lun on the lun list, set the lun
	 * inventory changed UA for all other luns.
	 */
	mtx_lock(&ctl_softc->ctl_lock);
	STAILQ_FOREACH(nlun, &ctl_softc->lun_list, links) {
		mtx_lock(&nlun->lun_lock);
		ctl_est_ua_all(nlun, -1, CTL_UA_LUN_CHANGE);
		mtx_unlock(&nlun->lun_lock);
	}
	STAILQ_INSERT_TAIL(&ctl_softc->lun_list, lun, links);
	ctl_softc->ctl_luns[lun_number] = lun;
	ctl_softc->num_luns++;
	mtx_unlock(&ctl_softc->ctl_lock);

	lun->be_lun->lun_config_status(lun->be_lun->be_lun, CTL_LUN_CONFIG_OK);
	return (0);
}

/*
 * Delete a LUN.
 * Assumptions:
 * - LUN has already been marked invalid and any pending I/O has been taken
 *   care of.
 */
static int
ctl_free_lun(struct ctl_lun *lun)
{
	struct ctl_softc *softc = lun->ctl_softc;
	struct ctl_lun *nlun;
	int i;

	KASSERT(TAILQ_EMPTY(&lun->ooa_queue),
	    ("Freeing a LUN %p with outstanding I/O!\n", lun));

	mtx_lock(&softc->ctl_lock);
	STAILQ_REMOVE(&softc->lun_list, lun, ctl_lun, links);
	ctl_clear_mask(softc->ctl_lun_mask, lun->lun);
	softc->ctl_luns[lun->lun] = NULL;
	softc->num_luns--;
	STAILQ_FOREACH(nlun, &softc->lun_list, links) {
		mtx_lock(&nlun->lun_lock);
		ctl_est_ua_all(nlun, -1, CTL_UA_LUN_CHANGE);
		mtx_unlock(&nlun->lun_lock);
	}
	mtx_unlock(&softc->ctl_lock);

	/*
	 * Tell the backend to free resources, if this LUN has a backend.
	 */
	atomic_subtract_int(&lun->be_lun->be->num_luns, 1);
	lun->be_lun->lun_shutdown(lun->be_lun->be_lun);

	lun->ie_reportcnt = UINT32_MAX;
	callout_drain(&lun->ie_callout);
	ctl_tpc_lun_shutdown(lun);
	mtx_destroy(&lun->lun_lock);
	free(lun->lun_devid, M_CTL);
	for (i = 0; i < ctl_max_ports; i++)
		free(lun->pending_ua[i], M_CTL);
	free(lun->pending_ua, M_DEVBUF);
	for (i = 0; i < ctl_max_ports; i++)
		free(lun->pr_keys[i], M_CTL);
	free(lun->pr_keys, M_DEVBUF);
	free(lun->write_buffer, M_CTL);
	free(lun->prevent, M_CTL);
	if (lun->flags & CTL_LUN_MALLOCED)
		free(lun, M_CTL);

	return (0);
}

static void
ctl_create_lun(struct ctl_be_lun *be_lun)
{

	/*
	 * ctl_alloc_lun() should handle all potential failure cases.
	 */
	ctl_alloc_lun(control_softc, NULL, be_lun);
}

int
ctl_add_lun(struct ctl_be_lun *be_lun)
{
	struct ctl_softc *softc = control_softc;

	mtx_lock(&softc->ctl_lock);
	STAILQ_INSERT_TAIL(&softc->pending_lun_queue, be_lun, links);
	mtx_unlock(&softc->ctl_lock);
	wakeup(&softc->pending_lun_queue);

	return (0);
}

int
ctl_enable_lun(struct ctl_be_lun *be_lun)
{
	struct ctl_softc *softc;
	struct ctl_port *port, *nport;
	struct ctl_lun *lun;
	int retval;

	lun = (struct ctl_lun *)be_lun->ctl_lun;
	softc = lun->ctl_softc;

	mtx_lock(&softc->ctl_lock);
	mtx_lock(&lun->lun_lock);
	if ((lun->flags & CTL_LUN_DISABLED) == 0) {
		/*
		 * eh?  Why did we get called if the LUN is already
		 * enabled?
		 */
		mtx_unlock(&lun->lun_lock);
		mtx_unlock(&softc->ctl_lock);
		return (0);
	}
	lun->flags &= ~CTL_LUN_DISABLED;
	mtx_unlock(&lun->lun_lock);

	STAILQ_FOREACH_SAFE(port, &softc->port_list, links, nport) {
		if ((port->status & CTL_PORT_STATUS_ONLINE) == 0 ||
		    port->lun_map != NULL || port->lun_enable == NULL)
			continue;

		/*
		 * Drop the lock while we call the FETD's enable routine.
		 * This can lead to a callback into CTL (at least in the
		 * case of the internal initiator frontend.
		 */
		mtx_unlock(&softc->ctl_lock);
		retval = port->lun_enable(port->targ_lun_arg, lun->lun);
		mtx_lock(&softc->ctl_lock);
		if (retval != 0) {
			printf("%s: FETD %s port %d returned error "
			       "%d for lun_enable on lun %jd\n",
			       __func__, port->port_name, port->targ_port,
			       retval, (intmax_t)lun->lun);
		}
	}

	mtx_unlock(&softc->ctl_lock);
	ctl_isc_announce_lun(lun);

	return (0);
}

int
ctl_disable_lun(struct ctl_be_lun *be_lun)
{
	struct ctl_softc *softc;
	struct ctl_port *port;
	struct ctl_lun *lun;
	int retval;

	lun = (struct ctl_lun *)be_lun->ctl_lun;
	softc = lun->ctl_softc;

	mtx_lock(&softc->ctl_lock);
	mtx_lock(&lun->lun_lock);
	if (lun->flags & CTL_LUN_DISABLED) {
		mtx_unlock(&lun->lun_lock);
		mtx_unlock(&softc->ctl_lock);
		return (0);
	}
	lun->flags |= CTL_LUN_DISABLED;
	mtx_unlock(&lun->lun_lock);

	STAILQ_FOREACH(port, &softc->port_list, links) {
		if ((port->status & CTL_PORT_STATUS_ONLINE) == 0 ||
		    port->lun_map != NULL || port->lun_disable == NULL)
			continue;

		/*
		 * Drop the lock before we call the frontend's disable
		 * routine, to avoid lock order reversals.
		 *
		 * XXX KDM what happens if the frontend list changes while
		 * we're traversing it?  It's unlikely, but should be handled.
		 */
		mtx_unlock(&softc->ctl_lock);
		retval = port->lun_disable(port->targ_lun_arg, lun->lun);
		mtx_lock(&softc->ctl_lock);
		if (retval != 0) {
			printf("%s: FETD %s port %d returned error "
			       "%d for lun_disable on lun %jd\n",
			       __func__, port->port_name, port->targ_port,
			       retval, (intmax_t)lun->lun);
		}
	}

	mtx_unlock(&softc->ctl_lock);
	ctl_isc_announce_lun(lun);

	return (0);
}

int
ctl_start_lun(struct ctl_be_lun *be_lun)
{
	struct ctl_lun *lun = (struct ctl_lun *)be_lun->ctl_lun;

	mtx_lock(&lun->lun_lock);
	lun->flags &= ~CTL_LUN_STOPPED;
	mtx_unlock(&lun->lun_lock);
	return (0);
}

int
ctl_stop_lun(struct ctl_be_lun *be_lun)
{
	struct ctl_lun *lun = (struct ctl_lun *)be_lun->ctl_lun;

	mtx_lock(&lun->lun_lock);
	lun->flags |= CTL_LUN_STOPPED;
	mtx_unlock(&lun->lun_lock);
	return (0);
}

int
ctl_lun_no_media(struct ctl_be_lun *be_lun)
{
	struct ctl_lun *lun = (struct ctl_lun *)be_lun->ctl_lun;

	mtx_lock(&lun->lun_lock);
	lun->flags |= CTL_LUN_NO_MEDIA;
	mtx_unlock(&lun->lun_lock);
	return (0);
}

int
ctl_lun_has_media(struct ctl_be_lun *be_lun)
{
	struct ctl_lun *lun = (struct ctl_lun *)be_lun->ctl_lun;
	union ctl_ha_msg msg;

	mtx_lock(&lun->lun_lock);
	lun->flags &= ~(CTL_LUN_NO_MEDIA | CTL_LUN_EJECTED);
	if (lun->flags & CTL_LUN_REMOVABLE)
		ctl_est_ua_all(lun, -1, CTL_UA_MEDIUM_CHANGE);
	mtx_unlock(&lun->lun_lock);
	if ((lun->flags & CTL_LUN_REMOVABLE) &&
	    lun->ctl_softc->ha_mode == CTL_HA_MODE_XFER) {
		bzero(&msg.ua, sizeof(msg.ua));
		msg.hdr.msg_type = CTL_MSG_UA;
		msg.hdr.nexus.initid = -1;
		msg.hdr.nexus.targ_port = -1;
		msg.hdr.nexus.targ_lun = lun->lun;
		msg.hdr.nexus.targ_mapped_lun = lun->lun;
		msg.ua.ua_all = 1;
		msg.ua.ua_set = 1;
		msg.ua.ua_type = CTL_UA_MEDIUM_CHANGE;
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg, sizeof(msg.ua),
		    M_WAITOK);
	}
	return (0);
}

int
ctl_lun_ejected(struct ctl_be_lun *be_lun)
{
	struct ctl_lun *lun = (struct ctl_lun *)be_lun->ctl_lun;

	mtx_lock(&lun->lun_lock);
	lun->flags |= CTL_LUN_EJECTED;
	mtx_unlock(&lun->lun_lock);
	return (0);
}

int
ctl_lun_primary(struct ctl_be_lun *be_lun)
{
	struct ctl_lun *lun = (struct ctl_lun *)be_lun->ctl_lun;

	mtx_lock(&lun->lun_lock);
	lun->flags |= CTL_LUN_PRIMARY_SC;
	ctl_est_ua_all(lun, -1, CTL_UA_ASYM_ACC_CHANGE);
	mtx_unlock(&lun->lun_lock);
	ctl_isc_announce_lun(lun);
	return (0);
}

int
ctl_lun_secondary(struct ctl_be_lun *be_lun)
{
	struct ctl_lun *lun = (struct ctl_lun *)be_lun->ctl_lun;

	mtx_lock(&lun->lun_lock);
	lun->flags &= ~CTL_LUN_PRIMARY_SC;
	ctl_est_ua_all(lun, -1, CTL_UA_ASYM_ACC_CHANGE);
	mtx_unlock(&lun->lun_lock);
	ctl_isc_announce_lun(lun);
	return (0);
}

int
ctl_invalidate_lun(struct ctl_be_lun *be_lun)
{
	struct ctl_lun *lun;

	lun = (struct ctl_lun *)be_lun->ctl_lun;

	mtx_lock(&lun->lun_lock);

	/*
	 * The LUN needs to be disabled before it can be marked invalid.
	 */
	if ((lun->flags & CTL_LUN_DISABLED) == 0) {
		mtx_unlock(&lun->lun_lock);
		return (-1);
	}
	/*
	 * Mark the LUN invalid.
	 */
	lun->flags |= CTL_LUN_INVALID;

	/*
	 * If there is nothing in the OOA queue, go ahead and free the LUN.
	 * If we have something in the OOA queue, we'll free it when the
	 * last I/O completes.
	 */
	if (TAILQ_EMPTY(&lun->ooa_queue)) {
		mtx_unlock(&lun->lun_lock);
		ctl_free_lun(lun);
	} else
		mtx_unlock(&lun->lun_lock);

	return (0);
}

void
ctl_lun_capacity_changed(struct ctl_be_lun *be_lun)
{
	struct ctl_lun *lun = (struct ctl_lun *)be_lun->ctl_lun;
	union ctl_ha_msg msg;

	mtx_lock(&lun->lun_lock);
	ctl_est_ua_all(lun, -1, CTL_UA_CAPACITY_CHANGE);
	mtx_unlock(&lun->lun_lock);
	if (lun->ctl_softc->ha_mode == CTL_HA_MODE_XFER) {
		/* Send msg to other side. */
		bzero(&msg.ua, sizeof(msg.ua));
		msg.hdr.msg_type = CTL_MSG_UA;
		msg.hdr.nexus.initid = -1;
		msg.hdr.nexus.targ_port = -1;
		msg.hdr.nexus.targ_lun = lun->lun;
		msg.hdr.nexus.targ_mapped_lun = lun->lun;
		msg.ua.ua_all = 1;
		msg.ua.ua_set = 1;
		msg.ua.ua_type = CTL_UA_CAPACITY_CHANGE;
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg, sizeof(msg.ua),
		    M_WAITOK);
	}
}

/*
 * Backend "memory move is complete" callback for requests that never
 * make it down to say RAIDCore's configuration code.
 */
int
ctl_config_move_done(union ctl_io *io)
{
	int retval;

	CTL_DEBUG_PRINT(("ctl_config_move_done\n"));
	KASSERT(io->io_hdr.io_type == CTL_IO_SCSI,
	    ("Config I/O type isn't CTL_IO_SCSI (%d)!", io->io_hdr.io_type));

	if ((io->io_hdr.port_status != 0) &&
	    ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_STATUS_NONE ||
	     (io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS)) {
		ctl_set_internal_failure(&io->scsiio, /*sks_valid*/ 1,
		    /*retry_count*/ io->io_hdr.port_status);
	} else if (io->scsiio.kern_data_resid != 0 &&
	    (io->io_hdr.flags & CTL_FLAG_DATA_MASK) == CTL_FLAG_DATA_OUT &&
	    ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_STATUS_NONE ||
	     (io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS)) {
		ctl_set_invalid_field_ciu(&io->scsiio);
	}

	if (ctl_debug & CTL_DEBUG_CDB_DATA)
		ctl_data_print(io);
	if (((io->io_hdr.flags & CTL_FLAG_DATA_MASK) == CTL_FLAG_DATA_IN) ||
	    ((io->io_hdr.status & CTL_STATUS_MASK) != CTL_STATUS_NONE &&
	     (io->io_hdr.status & CTL_STATUS_MASK) != CTL_SUCCESS) ||
	    ((io->io_hdr.flags & CTL_FLAG_ABORT) != 0)) {
		/*
		 * XXX KDM just assuming a single pointer here, and not a
		 * S/G list.  If we start using S/G lists for config data,
		 * we'll need to know how to clean them up here as well.
		 */
		if (io->io_hdr.flags & CTL_FLAG_ALLOCATED)
			free(io->scsiio.kern_data_ptr, M_CTL);
		ctl_done(io);
		retval = CTL_RETVAL_COMPLETE;
	} else {
		/*
		 * XXX KDM now we need to continue data movement.  Some
		 * options:
		 * - call ctl_scsiio() again?  We don't do this for data
		 *   writes, because for those at least we know ahead of
		 *   time where the write will go and how long it is.  For
		 *   config writes, though, that information is largely
		 *   contained within the write itself, thus we need to
		 *   parse out the data again.
		 *
		 * - Call some other function once the data is in?
		 */

		/*
		 * XXX KDM call ctl_scsiio() again for now, and check flag
		 * bits to see whether we're allocated or not.
		 */
		retval = ctl_scsiio(&io->scsiio);
	}
	return (retval);
}

/*
 * This gets called by a backend driver when it is done with a
 * data_submit method.
 */
void
ctl_data_submit_done(union ctl_io *io)
{
	/*
	 * If the IO_CONT flag is set, we need to call the supplied
	 * function to continue processing the I/O, instead of completing
	 * the I/O just yet.
	 *
	 * If there is an error, though, we don't want to keep processing.
	 * Instead, just send status back to the initiator.
	 */
	if ((io->io_hdr.flags & CTL_FLAG_IO_CONT) &&
	    (io->io_hdr.flags & CTL_FLAG_ABORT) == 0 &&
	    ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_STATUS_NONE ||
	     (io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS)) {
		io->scsiio.io_cont(io);
		return;
	}
	ctl_done(io);
}

/*
 * This gets called by a backend driver when it is done with a
 * configuration write.
 */
void
ctl_config_write_done(union ctl_io *io)
{
	uint8_t *buf;

	/*
	 * If the IO_CONT flag is set, we need to call the supplied
	 * function to continue processing the I/O, instead of completing
	 * the I/O just yet.
	 *
	 * If there is an error, though, we don't want to keep processing.
	 * Instead, just send status back to the initiator.
	 */
	if ((io->io_hdr.flags & CTL_FLAG_IO_CONT) &&
	    (io->io_hdr.flags & CTL_FLAG_ABORT) == 0 &&
	    ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_STATUS_NONE ||
	     (io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS)) {
		io->scsiio.io_cont(io);
		return;
	}
	/*
	 * Since a configuration write can be done for commands that actually
	 * have data allocated, like write buffer, and commands that have
	 * no data, like start/stop unit, we need to check here.
	 */
	if (io->io_hdr.flags & CTL_FLAG_ALLOCATED)
		buf = io->scsiio.kern_data_ptr;
	else
		buf = NULL;
	ctl_done(io);
	if (buf)
		free(buf, M_CTL);
}

void
ctl_config_read_done(union ctl_io *io)
{
	uint8_t *buf;

	/*
	 * If there is some error -- we are done, skip data transfer.
	 */
	if ((io->io_hdr.flags & CTL_FLAG_ABORT) != 0 ||
	    ((io->io_hdr.status & CTL_STATUS_MASK) != CTL_STATUS_NONE &&
	     (io->io_hdr.status & CTL_STATUS_MASK) != CTL_SUCCESS)) {
		if (io->io_hdr.flags & CTL_FLAG_ALLOCATED)
			buf = io->scsiio.kern_data_ptr;
		else
			buf = NULL;
		ctl_done(io);
		if (buf)
			free(buf, M_CTL);
		return;
	}

	/*
	 * If the IO_CONT flag is set, we need to call the supplied
	 * function to continue processing the I/O, instead of completing
	 * the I/O just yet.
	 */
	if (io->io_hdr.flags & CTL_FLAG_IO_CONT) {
		io->scsiio.io_cont(io);
		return;
	}

	ctl_datamove(io);
}

/*
 * SCSI release command.
 */
int
ctl_scsi_release(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	uint32_t residx;

	CTL_DEBUG_PRINT(("ctl_scsi_release\n"));

	residx = ctl_get_initindex(&ctsio->io_hdr.nexus);

	/*
	 * XXX KDM right now, we only support LUN reservation.  We don't
	 * support 3rd party reservations, or extent reservations, which
	 * might actually need the parameter list.  If we've gotten this
	 * far, we've got a LUN reservation.  Anything else got kicked out
	 * above.  So, according to SPC, ignore the length.
	 */

	mtx_lock(&lun->lun_lock);

	/*
	 * According to SPC, it is not an error for an intiator to attempt
	 * to release a reservation on a LUN that isn't reserved, or that
	 * is reserved by another initiator.  The reservation can only be
	 * released, though, by the initiator who made it or by one of
	 * several reset type events.
	 */
	if ((lun->flags & CTL_LUN_RESERVED) && (lun->res_idx == residx))
			lun->flags &= ~CTL_LUN_RESERVED;

	mtx_unlock(&lun->lun_lock);

	ctl_set_success(ctsio);
	ctl_done((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_scsi_reserve(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	uint32_t residx;

	CTL_DEBUG_PRINT(("ctl_reserve\n"));

	residx = ctl_get_initindex(&ctsio->io_hdr.nexus);

	/*
	 * XXX KDM right now, we only support LUN reservation.  We don't
	 * support 3rd party reservations, or extent reservations, which
	 * might actually need the parameter list.  If we've gotten this
	 * far, we've got a LUN reservation.  Anything else got kicked out
	 * above.  So, according to SPC, ignore the length.
	 */

	mtx_lock(&lun->lun_lock);
	if ((lun->flags & CTL_LUN_RESERVED) && (lun->res_idx != residx)) {
		ctl_set_reservation_conflict(ctsio);
		goto bailout;
	}

	/* SPC-3 exceptions to SPC-2 RESERVE and RELEASE behavior. */
	if (lun->flags & CTL_LUN_PR_RESERVED) {
		ctl_set_success(ctsio);
		goto bailout;
	}

	lun->flags |= CTL_LUN_RESERVED;
	lun->res_idx = residx;
	ctl_set_success(ctsio);

bailout:
	mtx_unlock(&lun->lun_lock);
	ctl_done((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_start_stop(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_start_stop_unit *cdb;
	int retval;

	CTL_DEBUG_PRINT(("ctl_start_stop\n"));

	cdb = (struct scsi_start_stop_unit *)ctsio->cdb;

	if ((cdb->how & SSS_PC_MASK) == 0) {
		if ((lun->flags & CTL_LUN_PR_RESERVED) &&
		    (cdb->how & SSS_START) == 0) {
			uint32_t residx;

			residx = ctl_get_initindex(&ctsio->io_hdr.nexus);
			if (ctl_get_prkey(lun, residx) == 0 ||
			    (lun->pr_res_idx != residx && lun->pr_res_type < 4)) {

				ctl_set_reservation_conflict(ctsio);
				ctl_done((union ctl_io *)ctsio);
				return (CTL_RETVAL_COMPLETE);
			}
		}

		if ((cdb->how & SSS_LOEJ) &&
		    (lun->flags & CTL_LUN_REMOVABLE) == 0) {
			ctl_set_invalid_field(ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 1,
					      /*field*/ 4,
					      /*bit_valid*/ 1,
					      /*bit*/ 1);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}

		if ((cdb->how & SSS_START) == 0 && (cdb->how & SSS_LOEJ) &&
		    lun->prevent_count > 0) {
			/* "Medium removal prevented" */
			ctl_set_sense(ctsio, /*current_error*/ 1,
			    /*sense_key*/(lun->flags & CTL_LUN_NO_MEDIA) ?
			     SSD_KEY_NOT_READY : SSD_KEY_ILLEGAL_REQUEST,
			    /*asc*/ 0x53, /*ascq*/ 0x02, SSD_ELEM_NONE);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}
	}

	retval = lun->backend->config_write((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_prevent_allow(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_prevent *cdb;
	int retval;
	uint32_t initidx;

	CTL_DEBUG_PRINT(("ctl_prevent_allow\n"));

	cdb = (struct scsi_prevent *)ctsio->cdb;

	if ((lun->flags & CTL_LUN_REMOVABLE) == 0 || lun->prevent == NULL) {
		ctl_set_invalid_opcode(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	initidx = ctl_get_initindex(&ctsio->io_hdr.nexus);
	mtx_lock(&lun->lun_lock);
	if ((cdb->how & PR_PREVENT) &&
	    ctl_is_set(lun->prevent, initidx) == 0) {
		ctl_set_mask(lun->prevent, initidx);
		lun->prevent_count++;
	} else if ((cdb->how & PR_PREVENT) == 0 &&
	    ctl_is_set(lun->prevent, initidx)) {
		ctl_clear_mask(lun->prevent, initidx);
		lun->prevent_count--;
	}
	mtx_unlock(&lun->lun_lock);
	retval = lun->backend->config_write((union ctl_io *)ctsio);
	return (retval);
}

/*
 * We support the SYNCHRONIZE CACHE command (10 and 16 byte versions), but
 * we don't really do anything with the LBA and length fields if the user
 * passes them in.  Instead we'll just flush out the cache for the entire
 * LUN.
 */
int
ctl_sync_cache(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct ctl_lba_len_flags *lbalen;
	uint64_t starting_lba;
	uint32_t block_count;
	int retval;
	uint8_t byte2;

	CTL_DEBUG_PRINT(("ctl_sync_cache\n"));

	retval = 0;

	switch (ctsio->cdb[0]) {
	case SYNCHRONIZE_CACHE: {
		struct scsi_sync_cache *cdb;
		cdb = (struct scsi_sync_cache *)ctsio->cdb;

		starting_lba = scsi_4btoul(cdb->begin_lba);
		block_count = scsi_2btoul(cdb->lb_count);
		byte2 = cdb->byte2;
		break;
	}
	case SYNCHRONIZE_CACHE_16: {
		struct scsi_sync_cache_16 *cdb;
		cdb = (struct scsi_sync_cache_16 *)ctsio->cdb;

		starting_lba = scsi_8btou64(cdb->begin_lba);
		block_count = scsi_4btoul(cdb->lb_count);
		byte2 = cdb->byte2;
		break;
	}
	default:
		ctl_set_invalid_opcode(ctsio);
		ctl_done((union ctl_io *)ctsio);
		goto bailout;
		break; /* NOTREACHED */
	}

	/*
	 * We check the LBA and length, but don't do anything with them.
	 * A SYNCHRONIZE CACHE will cause the entire cache for this lun to
	 * get flushed.  This check will just help satisfy anyone who wants
	 * to see an error for an out of range LBA.
	 */
	if ((starting_lba + block_count) > (lun->be_lun->maxlba + 1)) {
		ctl_set_lba_out_of_range(ctsio,
		    MAX(starting_lba, lun->be_lun->maxlba + 1));
		ctl_done((union ctl_io *)ctsio);
		goto bailout;
	}

	lbalen = (struct ctl_lba_len_flags *)&ctsio->io_hdr.ctl_private[CTL_PRIV_LBA_LEN];
	lbalen->lba = starting_lba;
	lbalen->len = block_count;
	lbalen->flags = byte2;
	retval = lun->backend->config_write((union ctl_io *)ctsio);

bailout:
	return (retval);
}

int
ctl_format(struct ctl_scsiio *ctsio)
{
	struct scsi_format *cdb;
	int length, defect_list_len;

	CTL_DEBUG_PRINT(("ctl_format\n"));

	cdb = (struct scsi_format *)ctsio->cdb;

	length = 0;
	if (cdb->byte2 & SF_FMTDATA) {
		if (cdb->byte2 & SF_LONGLIST)
			length = sizeof(struct scsi_format_header_long);
		else
			length = sizeof(struct scsi_format_header_short);
	}

	if (((ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0)
	 && (length > 0)) {
		ctsio->kern_data_ptr = malloc(length, M_CTL, M_WAITOK);
		ctsio->kern_data_len = length;
		ctsio->kern_total_len = length;
		ctsio->kern_rel_offset = 0;
		ctsio->kern_sg_entries = 0;
		ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
		ctsio->be_move_done = ctl_config_move_done;
		ctl_datamove((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
	}

	defect_list_len = 0;

	if (cdb->byte2 & SF_FMTDATA) {
		if (cdb->byte2 & SF_LONGLIST) {
			struct scsi_format_header_long *header;

			header = (struct scsi_format_header_long *)
				ctsio->kern_data_ptr;

			defect_list_len = scsi_4btoul(header->defect_list_len);
			if (defect_list_len != 0) {
				ctl_set_invalid_field(ctsio,
						      /*sks_valid*/ 1,
						      /*command*/ 0,
						      /*field*/ 2,
						      /*bit_valid*/ 0,
						      /*bit*/ 0);
				goto bailout;
			}
		} else {
			struct scsi_format_header_short *header;

			header = (struct scsi_format_header_short *)
				ctsio->kern_data_ptr;

			defect_list_len = scsi_2btoul(header->defect_list_len);
			if (defect_list_len != 0) {
				ctl_set_invalid_field(ctsio,
						      /*sks_valid*/ 1,
						      /*command*/ 0,
						      /*field*/ 2,
						      /*bit_valid*/ 0,
						      /*bit*/ 0);
				goto bailout;
			}
		}
	}

	ctl_set_success(ctsio);
bailout:

	if (ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) {
		free(ctsio->kern_data_ptr, M_CTL);
		ctsio->io_hdr.flags &= ~CTL_FLAG_ALLOCATED;
	}

	ctl_done((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_read_buffer(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	uint64_t buffer_offset;
	uint32_t len;
	uint8_t byte2;
	static uint8_t descr[4];
	static uint8_t echo_descr[4] = { 0 };

	CTL_DEBUG_PRINT(("ctl_read_buffer\n"));

	switch (ctsio->cdb[0]) {
	case READ_BUFFER: {
		struct scsi_read_buffer *cdb;

		cdb = (struct scsi_read_buffer *)ctsio->cdb;
		buffer_offset = scsi_3btoul(cdb->offset);
		len = scsi_3btoul(cdb->length);
		byte2 = cdb->byte2;
		break;
	}
	case READ_BUFFER_16: {
		struct scsi_read_buffer_16 *cdb;

		cdb = (struct scsi_read_buffer_16 *)ctsio->cdb;
		buffer_offset = scsi_8btou64(cdb->offset);
		len = scsi_4btoul(cdb->length);
		byte2 = cdb->byte2;
		break;
	}
	default: /* This shouldn't happen. */
		ctl_set_invalid_opcode(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	if (buffer_offset > CTL_WRITE_BUFFER_SIZE ||
	    buffer_offset + len > CTL_WRITE_BUFFER_SIZE) {
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 6,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	if ((byte2 & RWB_MODE) == RWB_MODE_DESCR) {
		descr[0] = 0;
		scsi_ulto3b(CTL_WRITE_BUFFER_SIZE, &descr[1]);
		ctsio->kern_data_ptr = descr;
		len = min(len, sizeof(descr));
	} else if ((byte2 & RWB_MODE) == RWB_MODE_ECHO_DESCR) {
		ctsio->kern_data_ptr = echo_descr;
		len = min(len, sizeof(echo_descr));
	} else {
		if (lun->write_buffer == NULL) {
			lun->write_buffer = malloc(CTL_WRITE_BUFFER_SIZE,
			    M_CTL, M_WAITOK);
		}
		ctsio->kern_data_ptr = lun->write_buffer + buffer_offset;
	}
	ctsio->kern_data_len = len;
	ctsio->kern_total_len = len;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctl_set_success(ctsio);
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_write_buffer(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_write_buffer *cdb;
	int buffer_offset, len;

	CTL_DEBUG_PRINT(("ctl_write_buffer\n"));

	cdb = (struct scsi_write_buffer *)ctsio->cdb;

	len = scsi_3btoul(cdb->length);
	buffer_offset = scsi_3btoul(cdb->offset);

	if (buffer_offset + len > CTL_WRITE_BUFFER_SIZE) {
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 6,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * If we've got a kernel request that hasn't been malloced yet,
	 * malloc it and tell the caller the data buffer is here.
	 */
	if ((ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0) {
		if (lun->write_buffer == NULL) {
			lun->write_buffer = malloc(CTL_WRITE_BUFFER_SIZE,
			    M_CTL, M_WAITOK);
		}
		ctsio->kern_data_ptr = lun->write_buffer + buffer_offset;
		ctsio->kern_data_len = len;
		ctsio->kern_total_len = len;
		ctsio->kern_rel_offset = 0;
		ctsio->kern_sg_entries = 0;
		ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
		ctsio->be_move_done = ctl_config_move_done;
		ctl_datamove((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
	}

	ctl_set_success(ctsio);
	ctl_done((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_write_same(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct ctl_lba_len_flags *lbalen;
	uint64_t lba;
	uint32_t num_blocks;
	int len, retval;
	uint8_t byte2;

	CTL_DEBUG_PRINT(("ctl_write_same\n"));

	switch (ctsio->cdb[0]) {
	case WRITE_SAME_10: {
		struct scsi_write_same_10 *cdb;

		cdb = (struct scsi_write_same_10 *)ctsio->cdb;

		lba = scsi_4btoul(cdb->addr);
		num_blocks = scsi_2btoul(cdb->length);
		byte2 = cdb->byte2;
		break;
	}
	case WRITE_SAME_16: {
		struct scsi_write_same_16 *cdb;

		cdb = (struct scsi_write_same_16 *)ctsio->cdb;

		lba = scsi_8btou64(cdb->addr);
		num_blocks = scsi_4btoul(cdb->length);
		byte2 = cdb->byte2;
		break;
	}
	default:
		/*
		 * We got a command we don't support.  This shouldn't
		 * happen, commands should be filtered out above us.
		 */
		ctl_set_invalid_opcode(ctsio);
		ctl_done((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
		break; /* NOTREACHED */
	}

	/* ANCHOR flag can be used only together with UNMAP */
	if ((byte2 & SWS_UNMAP) == 0 && (byte2 & SWS_ANCHOR) != 0) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
		    /*command*/ 1, /*field*/ 1, /*bit_valid*/ 1, /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * The first check is to make sure we're in bounds, the second
	 * check is to catch wrap-around problems.  If the lba + num blocks
	 * is less than the lba, then we've wrapped around and the block
	 * range is invalid anyway.
	 */
	if (((lba + num_blocks) > (lun->be_lun->maxlba + 1))
	 || ((lba + num_blocks) < lba)) {
		ctl_set_lba_out_of_range(ctsio,
		    MAX(lba, lun->be_lun->maxlba + 1));
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/* Zero number of blocks means "to the last logical block" */
	if (num_blocks == 0) {
		if ((lun->be_lun->maxlba + 1) - lba > UINT32_MAX) {
			ctl_set_invalid_field(ctsio,
					      /*sks_valid*/ 0,
					      /*command*/ 1,
					      /*field*/ 0,
					      /*bit_valid*/ 0,
					      /*bit*/ 0);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}
		num_blocks = (lun->be_lun->maxlba + 1) - lba;
	}

	len = lun->be_lun->blocksize;

	/*
	 * If we've got a kernel request that hasn't been malloced yet,
	 * malloc it and tell the caller the data buffer is here.
	 */
	if ((byte2 & SWS_NDOB) == 0 &&
	    (ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0) {
		ctsio->kern_data_ptr = malloc(len, M_CTL, M_WAITOK);
		ctsio->kern_data_len = len;
		ctsio->kern_total_len = len;
		ctsio->kern_rel_offset = 0;
		ctsio->kern_sg_entries = 0;
		ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
		ctsio->be_move_done = ctl_config_move_done;
		ctl_datamove((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
	}

	lbalen = (struct ctl_lba_len_flags *)&ctsio->io_hdr.ctl_private[CTL_PRIV_LBA_LEN];
	lbalen->lba = lba;
	lbalen->len = num_blocks;
	lbalen->flags = byte2;
	retval = lun->backend->config_write((union ctl_io *)ctsio);

	return (retval);
}

int
ctl_unmap(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_unmap *cdb;
	struct ctl_ptr_len_flags *ptrlen;
	struct scsi_unmap_header *hdr;
	struct scsi_unmap_desc *buf, *end, *endnz, *range;
	uint64_t lba;
	uint32_t num_blocks;
	int len, retval;
	uint8_t byte2;

	CTL_DEBUG_PRINT(("ctl_unmap\n"));

	cdb = (struct scsi_unmap *)ctsio->cdb;
	len = scsi_2btoul(cdb->length);
	byte2 = cdb->byte2;

	/*
	 * If we've got a kernel request that hasn't been malloced yet,
	 * malloc it and tell the caller the data buffer is here.
	 */
	if ((ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0) {
		ctsio->kern_data_ptr = malloc(len, M_CTL, M_WAITOK);
		ctsio->kern_data_len = len;
		ctsio->kern_total_len = len;
		ctsio->kern_rel_offset = 0;
		ctsio->kern_sg_entries = 0;
		ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
		ctsio->be_move_done = ctl_config_move_done;
		ctl_datamove((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
	}

	len = ctsio->kern_total_len - ctsio->kern_data_resid;
	hdr = (struct scsi_unmap_header *)ctsio->kern_data_ptr;
	if (len < sizeof (*hdr) ||
	    len < (scsi_2btoul(hdr->length) + sizeof(hdr->length)) ||
	    len < (scsi_2btoul(hdr->desc_length) + sizeof (*hdr)) ||
	    scsi_2btoul(hdr->desc_length) % sizeof(*buf) != 0) {
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 0,
				      /*command*/ 0,
				      /*field*/ 0,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		goto done;
	}
	len = scsi_2btoul(hdr->desc_length);
	buf = (struct scsi_unmap_desc *)(hdr + 1);
	end = buf + len / sizeof(*buf);

	endnz = buf;
	for (range = buf; range < end; range++) {
		lba = scsi_8btou64(range->lba);
		num_blocks = scsi_4btoul(range->length);
		if (((lba + num_blocks) > (lun->be_lun->maxlba + 1))
		 || ((lba + num_blocks) < lba)) {
			ctl_set_lba_out_of_range(ctsio,
			    MAX(lba, lun->be_lun->maxlba + 1));
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}
		if (num_blocks != 0)
			endnz = range + 1;
	}

	/*
	 * Block backend can not handle zero last range.
	 * Filter it out and return if there is nothing left.
	 */
	len = (uint8_t *)endnz - (uint8_t *)buf;
	if (len == 0) {
		ctl_set_success(ctsio);
		goto done;
	}

	mtx_lock(&lun->lun_lock);
	ptrlen = (struct ctl_ptr_len_flags *)
	    &ctsio->io_hdr.ctl_private[CTL_PRIV_LBA_LEN];
	ptrlen->ptr = (void *)buf;
	ptrlen->len = len;
	ptrlen->flags = byte2;
	ctl_try_unblock_others(lun, (union ctl_io *)ctsio, FALSE);
	mtx_unlock(&lun->lun_lock);

	retval = lun->backend->config_write((union ctl_io *)ctsio);
	return (retval);

done:
	if (ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) {
		free(ctsio->kern_data_ptr, M_CTL);
		ctsio->io_hdr.flags &= ~CTL_FLAG_ALLOCATED;
	}
	ctl_done((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_default_page_handler(struct ctl_scsiio *ctsio,
			 struct ctl_page_index *page_index, uint8_t *page_ptr)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	uint8_t *current_cp;
	int set_ua;
	uint32_t initidx;

	initidx = ctl_get_initindex(&ctsio->io_hdr.nexus);
	set_ua = 0;

	current_cp = (page_index->page_data + (page_index->page_len *
	    CTL_PAGE_CURRENT));

	mtx_lock(&lun->lun_lock);
	if (memcmp(current_cp, page_ptr, page_index->page_len)) {
		memcpy(current_cp, page_ptr, page_index->page_len);
		set_ua = 1;
	}
	if (set_ua != 0)
		ctl_est_ua_all(lun, initidx, CTL_UA_MODE_CHANGE);
	mtx_unlock(&lun->lun_lock);
	if (set_ua) {
		ctl_isc_announce_mode(lun,
		    ctl_get_initindex(&ctsio->io_hdr.nexus),
		    page_index->page_code, page_index->subpage);
	}
	return (CTL_RETVAL_COMPLETE);
}

static void
ctl_ie_timer(void *arg)
{
	struct ctl_lun *lun = arg;
	uint64_t t;

	if (lun->ie_asc == 0)
		return;

	if (lun->MODE_IE.mrie == SIEP_MRIE_UA)
		ctl_est_ua_all(lun, -1, CTL_UA_IE);
	else
		lun->ie_reported = 0;

	if (lun->ie_reportcnt < scsi_4btoul(lun->MODE_IE.report_count)) {
		lun->ie_reportcnt++;
		t = scsi_4btoul(lun->MODE_IE.interval_timer);
		if (t == 0 || t == UINT32_MAX)
			t = 3000;  /* 5 min */
		callout_schedule(&lun->ie_callout, t * hz / 10);
	}
}

int
ctl_ie_page_handler(struct ctl_scsiio *ctsio,
			 struct ctl_page_index *page_index, uint8_t *page_ptr)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_info_exceptions_page *pg;
	uint64_t t;

	(void)ctl_default_page_handler(ctsio, page_index, page_ptr);

	pg = (struct scsi_info_exceptions_page *)page_ptr;
	mtx_lock(&lun->lun_lock);
	if (pg->info_flags & SIEP_FLAGS_TEST) {
		lun->ie_asc = 0x5d;
		lun->ie_ascq = 0xff;
		if (pg->mrie == SIEP_MRIE_UA) {
			ctl_est_ua_all(lun, -1, CTL_UA_IE);
			lun->ie_reported = 1;
		} else {
			ctl_clr_ua_all(lun, -1, CTL_UA_IE);
			lun->ie_reported = -1;
		}
		lun->ie_reportcnt = 1;
		if (lun->ie_reportcnt < scsi_4btoul(pg->report_count)) {
			lun->ie_reportcnt++;
			t = scsi_4btoul(pg->interval_timer);
			if (t == 0 || t == UINT32_MAX)
				t = 3000;  /* 5 min */
			callout_reset(&lun->ie_callout, t * hz / 10,
			    ctl_ie_timer, lun);
		}
	} else {
		lun->ie_asc = 0;
		lun->ie_ascq = 0;
		lun->ie_reported = 1;
		ctl_clr_ua_all(lun, -1, CTL_UA_IE);
		lun->ie_reportcnt = UINT32_MAX;
		callout_stop(&lun->ie_callout);
	}
	mtx_unlock(&lun->lun_lock);
	return (CTL_RETVAL_COMPLETE);
}

static int
ctl_do_mode_select(union ctl_io *io)
{
	struct ctl_lun *lun = CTL_LUN(io);
	struct scsi_mode_page_header *page_header;
	struct ctl_page_index *page_index;
	struct ctl_scsiio *ctsio;
	int page_len, page_len_offset, page_len_size;
	union ctl_modepage_info *modepage_info;
	uint16_t *len_left, *len_used;
	int retval, i;

	ctsio = &io->scsiio;
	page_index = NULL;
	page_len = 0;

	modepage_info = (union ctl_modepage_info *)
		ctsio->io_hdr.ctl_private[CTL_PRIV_MODEPAGE].bytes;
	len_left = &modepage_info->header.len_left;
	len_used = &modepage_info->header.len_used;

do_next_page:

	page_header = (struct scsi_mode_page_header *)
		(ctsio->kern_data_ptr + *len_used);

	if (*len_left == 0) {
		free(ctsio->kern_data_ptr, M_CTL);
		ctl_set_success(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	} else if (*len_left < sizeof(struct scsi_mode_page_header)) {

		free(ctsio->kern_data_ptr, M_CTL);
		ctl_set_param_len_error(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);

	} else if ((page_header->page_code & SMPH_SPF)
		&& (*len_left < sizeof(struct scsi_mode_page_header_sp))) {

		free(ctsio->kern_data_ptr, M_CTL);
		ctl_set_param_len_error(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}


	/*
	 * XXX KDM should we do something with the block descriptor?
	 */
	for (i = 0; i < CTL_NUM_MODE_PAGES; i++) {
		page_index = &lun->mode_pages.index[i];
		if (lun->be_lun->lun_type == T_DIRECT &&
		    (page_index->page_flags & CTL_PAGE_FLAG_DIRECT) == 0)
			continue;
		if (lun->be_lun->lun_type == T_PROCESSOR &&
		    (page_index->page_flags & CTL_PAGE_FLAG_PROC) == 0)
			continue;
		if (lun->be_lun->lun_type == T_CDROM &&
		    (page_index->page_flags & CTL_PAGE_FLAG_CDROM) == 0)
			continue;

		if ((page_index->page_code & SMPH_PC_MASK) !=
		    (page_header->page_code & SMPH_PC_MASK))
			continue;

		/*
		 * If neither page has a subpage code, then we've got a
		 * match.
		 */
		if (((page_index->page_code & SMPH_SPF) == 0)
		 && ((page_header->page_code & SMPH_SPF) == 0)) {
			page_len = page_header->page_length;
			break;
		}

		/*
		 * If both pages have subpages, then the subpage numbers
		 * have to match.
		 */
		if ((page_index->page_code & SMPH_SPF)
		  && (page_header->page_code & SMPH_SPF)) {
			struct scsi_mode_page_header_sp *sph;

			sph = (struct scsi_mode_page_header_sp *)page_header;
			if (page_index->subpage == sph->subpage) {
				page_len = scsi_2btoul(sph->page_length);
				break;
			}
		}
	}

	/*
	 * If we couldn't find the page, or if we don't have a mode select
	 * handler for it, send back an error to the user.
	 */
	if ((i >= CTL_NUM_MODE_PAGES)
	 || (page_index->select_handler == NULL)) {
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 0,
				      /*field*/ *len_used,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		free(ctsio->kern_data_ptr, M_CTL);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	if (page_index->page_code & SMPH_SPF) {
		page_len_offset = 2;
		page_len_size = 2;
	} else {
		page_len_size = 1;
		page_len_offset = 1;
	}

	/*
	 * If the length the initiator gives us isn't the one we specify in
	 * the mode page header, or if they didn't specify enough data in
	 * the CDB to avoid truncating this page, kick out the request.
	 */
	if (page_len != page_index->page_len - page_len_offset - page_len_size) {
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 0,
				      /*field*/ *len_used + page_len_offset,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		free(ctsio->kern_data_ptr, M_CTL);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}
	if (*len_left < page_index->page_len) {
		free(ctsio->kern_data_ptr, M_CTL);
		ctl_set_param_len_error(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * Run through the mode page, checking to make sure that the bits
	 * the user changed are actually legal for him to change.
	 */
	for (i = 0; i < page_index->page_len; i++) {
		uint8_t *user_byte, *change_mask, *current_byte;
		int bad_bit;
		int j;

		user_byte = (uint8_t *)page_header + i;
		change_mask = page_index->page_data +
			      (page_index->page_len * CTL_PAGE_CHANGEABLE) + i;
		current_byte = page_index->page_data +
			       (page_index->page_len * CTL_PAGE_CURRENT) + i;

		/*
		 * Check to see whether the user set any bits in this byte
		 * that he is not allowed to set.
		 */
		if ((*user_byte & ~(*change_mask)) ==
		    (*current_byte & ~(*change_mask)))
			continue;

		/*
		 * Go through bit by bit to determine which one is illegal.
		 */
		bad_bit = 0;
		for (j = 7; j >= 0; j--) {
			if ((((1 << i) & ~(*change_mask)) & *user_byte) !=
			    (((1 << i) & ~(*change_mask)) & *current_byte)) {
				bad_bit = i;
				break;
			}
		}
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 0,
				      /*field*/ *len_used + i,
				      /*bit_valid*/ 1,
				      /*bit*/ bad_bit);
		free(ctsio->kern_data_ptr, M_CTL);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * Decrement these before we call the page handler, since we may
	 * end up getting called back one way or another before the handler
	 * returns to this context.
	 */
	*len_left -= page_index->page_len;
	*len_used += page_index->page_len;

	retval = page_index->select_handler(ctsio, page_index,
					    (uint8_t *)page_header);

	/*
	 * If the page handler returns CTL_RETVAL_QUEUED, then we need to
	 * wait until this queued command completes to finish processing
	 * the mode page.  If it returns anything other than
	 * CTL_RETVAL_COMPLETE (e.g. CTL_RETVAL_ERROR), then it should have
	 * already set the sense information, freed the data pointer, and
	 * completed the io for us.
	 */
	if (retval != CTL_RETVAL_COMPLETE)
		goto bailout_no_done;

	/*
	 * If the initiator sent us more than one page, parse the next one.
	 */
	if (*len_left > 0)
		goto do_next_page;

	ctl_set_success(ctsio);
	free(ctsio->kern_data_ptr, M_CTL);
	ctl_done((union ctl_io *)ctsio);

bailout_no_done:

	return (CTL_RETVAL_COMPLETE);

}

int
ctl_mode_select(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	union ctl_modepage_info *modepage_info;
	int bd_len, i, header_size, param_len, rtd;
	uint32_t initidx;

	initidx = ctl_get_initindex(&ctsio->io_hdr.nexus);
	switch (ctsio->cdb[0]) {
	case MODE_SELECT_6: {
		struct scsi_mode_select_6 *cdb;

		cdb = (struct scsi_mode_select_6 *)ctsio->cdb;

		rtd = (cdb->byte2 & SMS_RTD) ? 1 : 0;
		param_len = cdb->length;
		header_size = sizeof(struct scsi_mode_header_6);
		break;
	}
	case MODE_SELECT_10: {
		struct scsi_mode_select_10 *cdb;

		cdb = (struct scsi_mode_select_10 *)ctsio->cdb;

		rtd = (cdb->byte2 & SMS_RTD) ? 1 : 0;
		param_len = scsi_2btoul(cdb->length);
		header_size = sizeof(struct scsi_mode_header_10);
		break;
	}
	default:
		ctl_set_invalid_opcode(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	if (rtd) {
		if (param_len != 0) {
			ctl_set_invalid_field(ctsio, /*sks_valid*/ 0,
			    /*command*/ 1, /*field*/ 0,
			    /*bit_valid*/ 0, /*bit*/ 0);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}

		/* Revert to defaults. */
		ctl_init_page_index(lun);
		mtx_lock(&lun->lun_lock);
		ctl_est_ua_all(lun, initidx, CTL_UA_MODE_CHANGE);
		mtx_unlock(&lun->lun_lock);
		for (i = 0; i < CTL_NUM_MODE_PAGES; i++) {
			ctl_isc_announce_mode(lun, -1,
			    lun->mode_pages.index[i].page_code & SMPH_PC_MASK,
			    lun->mode_pages.index[i].subpage);
		}
		ctl_set_success(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * From SPC-3:
	 * "A parameter list length of zero indicates that the Data-Out Buffer
	 * shall be empty. This condition shall not be considered as an error."
	 */
	if (param_len == 0) {
		ctl_set_success(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * Since we'll hit this the first time through, prior to
	 * allocation, we don't need to free a data buffer here.
	 */
	if (param_len < header_size) {
		ctl_set_param_len_error(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * Allocate the data buffer and grab the user's data.  In theory,
	 * we shouldn't have to sanity check the parameter list length here
	 * because the maximum size is 64K.  We should be able to malloc
	 * that much without too many problems.
	 */
	if ((ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0) {
		ctsio->kern_data_ptr = malloc(param_len, M_CTL, M_WAITOK);
		ctsio->kern_data_len = param_len;
		ctsio->kern_total_len = param_len;
		ctsio->kern_rel_offset = 0;
		ctsio->kern_sg_entries = 0;
		ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
		ctsio->be_move_done = ctl_config_move_done;
		ctl_datamove((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
	}

	switch (ctsio->cdb[0]) {
	case MODE_SELECT_6: {
		struct scsi_mode_header_6 *mh6;

		mh6 = (struct scsi_mode_header_6 *)ctsio->kern_data_ptr;
		bd_len = mh6->blk_desc_len;
		break;
	}
	case MODE_SELECT_10: {
		struct scsi_mode_header_10 *mh10;

		mh10 = (struct scsi_mode_header_10 *)ctsio->kern_data_ptr;
		bd_len = scsi_2btoul(mh10->blk_desc_len);
		break;
	}
	default:
		panic("%s: Invalid CDB type %#x", __func__, ctsio->cdb[0]);
	}

	if (param_len < (header_size + bd_len)) {
		free(ctsio->kern_data_ptr, M_CTL);
		ctl_set_param_len_error(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * Set the IO_CONT flag, so that if this I/O gets passed to
	 * ctl_config_write_done(), it'll get passed back to
	 * ctl_do_mode_select() for further processing, or completion if
	 * we're all done.
	 */
	ctsio->io_hdr.flags |= CTL_FLAG_IO_CONT;
	ctsio->io_cont = ctl_do_mode_select;

	modepage_info = (union ctl_modepage_info *)
		ctsio->io_hdr.ctl_private[CTL_PRIV_MODEPAGE].bytes;
	memset(modepage_info, 0, sizeof(*modepage_info));
	modepage_info->header.len_left = param_len - header_size - bd_len;
	modepage_info->header.len_used = header_size + bd_len;

	return (ctl_do_mode_select((union ctl_io *)ctsio));
}

int
ctl_mode_sense(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	int pc, page_code, dbd, subpage;
	int alloc_len, page_len, header_len, total_len;
	struct scsi_mode_block_descr *block_desc;
	struct ctl_page_index *page_index;

	dbd = 0;
	block_desc = NULL;

	CTL_DEBUG_PRINT(("ctl_mode_sense\n"));

	switch (ctsio->cdb[0]) {
	case MODE_SENSE_6: {
		struct scsi_mode_sense_6 *cdb;

		cdb = (struct scsi_mode_sense_6 *)ctsio->cdb;

		header_len = sizeof(struct scsi_mode_hdr_6);
		if (cdb->byte2 & SMS_DBD)
			dbd = 1;
		else
			header_len += sizeof(struct scsi_mode_block_descr);

		pc = (cdb->page & SMS_PAGE_CTRL_MASK) >> 6;
		page_code = cdb->page & SMS_PAGE_CODE;
		subpage = cdb->subpage;
		alloc_len = cdb->length;
		break;
	}
	case MODE_SENSE_10: {
		struct scsi_mode_sense_10 *cdb;

		cdb = (struct scsi_mode_sense_10 *)ctsio->cdb;

		header_len = sizeof(struct scsi_mode_hdr_10);

		if (cdb->byte2 & SMS_DBD)
			dbd = 1;
		else
			header_len += sizeof(struct scsi_mode_block_descr);
		pc = (cdb->page & SMS_PAGE_CTRL_MASK) >> 6;
		page_code = cdb->page & SMS_PAGE_CODE;
		subpage = cdb->subpage;
		alloc_len = scsi_2btoul(cdb->length);
		break;
	}
	default:
		ctl_set_invalid_opcode(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
		break; /* NOTREACHED */
	}

	/*
	 * We have to make a first pass through to calculate the size of
	 * the pages that match the user's query.  Then we allocate enough
	 * memory to hold it, and actually copy the data into the buffer.
	 */
	switch (page_code) {
	case SMS_ALL_PAGES_PAGE: {
		u_int i;

		page_len = 0;

		/*
		 * At the moment, values other than 0 and 0xff here are
		 * reserved according to SPC-3.
		 */
		if ((subpage != SMS_SUBPAGE_PAGE_0)
		 && (subpage != SMS_SUBPAGE_ALL)) {
			ctl_set_invalid_field(ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 1,
					      /*field*/ 3,
					      /*bit_valid*/ 0,
					      /*bit*/ 0);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}

		for (i = 0; i < CTL_NUM_MODE_PAGES; i++) {
			page_index = &lun->mode_pages.index[i];

			/* Make sure the page is supported for this dev type */
			if (lun->be_lun->lun_type == T_DIRECT &&
			    (page_index->page_flags & CTL_PAGE_FLAG_DIRECT) == 0)
				continue;
			if (lun->be_lun->lun_type == T_PROCESSOR &&
			    (page_index->page_flags & CTL_PAGE_FLAG_PROC) == 0)
				continue;
			if (lun->be_lun->lun_type == T_CDROM &&
			    (page_index->page_flags & CTL_PAGE_FLAG_CDROM) == 0)
				continue;

			/*
			 * We don't use this subpage if the user didn't
			 * request all subpages.
			 */
			if ((page_index->subpage != 0)
			 && (subpage == SMS_SUBPAGE_PAGE_0))
				continue;

			page_len += page_index->page_len;
		}
		break;
	}
	default: {
		u_int i;

		page_len = 0;

		for (i = 0; i < CTL_NUM_MODE_PAGES; i++) {
			page_index = &lun->mode_pages.index[i];

			/* Make sure the page is supported for this dev type */
			if (lun->be_lun->lun_type == T_DIRECT &&
			    (page_index->page_flags & CTL_PAGE_FLAG_DIRECT) == 0)
				continue;
			if (lun->be_lun->lun_type == T_PROCESSOR &&
			    (page_index->page_flags & CTL_PAGE_FLAG_PROC) == 0)
				continue;
			if (lun->be_lun->lun_type == T_CDROM &&
			    (page_index->page_flags & CTL_PAGE_FLAG_CDROM) == 0)
				continue;

			/* Look for the right page code */
			if ((page_index->page_code & SMPH_PC_MASK) != page_code)
				continue;

			/* Look for the right subpage or the subpage wildcard*/
			if ((page_index->subpage != subpage)
			 && (subpage != SMS_SUBPAGE_ALL))
				continue;

			page_len += page_index->page_len;
		}

		if (page_len == 0) {
			ctl_set_invalid_field(ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 1,
					      /*field*/ 2,
					      /*bit_valid*/ 1,
					      /*bit*/ 5);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}
		break;
	}
	}

	total_len = header_len + page_len;

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	switch (ctsio->cdb[0]) {
	case MODE_SENSE_6: {
		struct scsi_mode_hdr_6 *header;

		header = (struct scsi_mode_hdr_6 *)ctsio->kern_data_ptr;

		header->datalen = MIN(total_len - 1, 254);
		if (lun->be_lun->lun_type == T_DIRECT) {
			header->dev_specific = 0x10; /* DPOFUA */
			if ((lun->be_lun->flags & CTL_LUN_FLAG_READONLY) ||
			    (lun->MODE_CTRL.eca_and_aen & SCP_SWP) != 0)
				header->dev_specific |= 0x80; /* WP */
		}
		if (dbd)
			header->block_descr_len = 0;
		else
			header->block_descr_len =
				sizeof(struct scsi_mode_block_descr);
		block_desc = (struct scsi_mode_block_descr *)&header[1];
		break;
	}
	case MODE_SENSE_10: {
		struct scsi_mode_hdr_10 *header;
		int datalen;

		header = (struct scsi_mode_hdr_10 *)ctsio->kern_data_ptr;

		datalen = MIN(total_len - 2, 65533);
		scsi_ulto2b(datalen, header->datalen);
		if (lun->be_lun->lun_type == T_DIRECT) {
			header->dev_specific = 0x10; /* DPOFUA */
			if ((lun->be_lun->flags & CTL_LUN_FLAG_READONLY) ||
			    (lun->MODE_CTRL.eca_and_aen & SCP_SWP) != 0)
				header->dev_specific |= 0x80; /* WP */
		}
		if (dbd)
			scsi_ulto2b(0, header->block_descr_len);
		else
			scsi_ulto2b(sizeof(struct scsi_mode_block_descr),
				    header->block_descr_len);
		block_desc = (struct scsi_mode_block_descr *)&header[1];
		break;
	}
	default:
		panic("%s: Invalid CDB type %#x", __func__, ctsio->cdb[0]);
	}

	/*
	 * If we've got a disk, use its blocksize in the block
	 * descriptor.  Otherwise, just set it to 0.
	 */
	if (dbd == 0) {
		if (lun->be_lun->lun_type == T_DIRECT)
			scsi_ulto3b(lun->be_lun->blocksize,
				    block_desc->block_len);
		else
			scsi_ulto3b(0, block_desc->block_len);
	}

	switch (page_code) {
	case SMS_ALL_PAGES_PAGE: {
		int i, data_used;

		data_used = header_len;
		for (i = 0; i < CTL_NUM_MODE_PAGES; i++) {
			struct ctl_page_index *page_index;

			page_index = &lun->mode_pages.index[i];
			if (lun->be_lun->lun_type == T_DIRECT &&
			    (page_index->page_flags & CTL_PAGE_FLAG_DIRECT) == 0)
				continue;
			if (lun->be_lun->lun_type == T_PROCESSOR &&
			    (page_index->page_flags & CTL_PAGE_FLAG_PROC) == 0)
				continue;
			if (lun->be_lun->lun_type == T_CDROM &&
			    (page_index->page_flags & CTL_PAGE_FLAG_CDROM) == 0)
				continue;

			/*
			 * We don't use this subpage if the user didn't
			 * request all subpages.  We already checked (above)
			 * to make sure the user only specified a subpage
			 * of 0 or 0xff in the SMS_ALL_PAGES_PAGE case.
			 */
			if ((page_index->subpage != 0)
			 && (subpage == SMS_SUBPAGE_PAGE_0))
				continue;

			/*
			 * Call the handler, if it exists, to update the
			 * page to the latest values.
			 */
			if (page_index->sense_handler != NULL)
				page_index->sense_handler(ctsio, page_index,pc);

			memcpy(ctsio->kern_data_ptr + data_used,
			       page_index->page_data +
			       (page_index->page_len * pc),
			       page_index->page_len);
			data_used += page_index->page_len;
		}
		break;
	}
	default: {
		int i, data_used;

		data_used = header_len;

		for (i = 0; i < CTL_NUM_MODE_PAGES; i++) {
			struct ctl_page_index *page_index;

			page_index = &lun->mode_pages.index[i];

			/* Look for the right page code */
			if ((page_index->page_code & SMPH_PC_MASK) != page_code)
				continue;

			/* Look for the right subpage or the subpage wildcard*/
			if ((page_index->subpage != subpage)
			 && (subpage != SMS_SUBPAGE_ALL))
				continue;

			/* Make sure the page is supported for this dev type */
			if (lun->be_lun->lun_type == T_DIRECT &&
			    (page_index->page_flags & CTL_PAGE_FLAG_DIRECT) == 0)
				continue;
			if (lun->be_lun->lun_type == T_PROCESSOR &&
			    (page_index->page_flags & CTL_PAGE_FLAG_PROC) == 0)
				continue;
			if (lun->be_lun->lun_type == T_CDROM &&
			    (page_index->page_flags & CTL_PAGE_FLAG_CDROM) == 0)
				continue;

			/*
			 * Call the handler, if it exists, to update the
			 * page to the latest values.
			 */
			if (page_index->sense_handler != NULL)
				page_index->sense_handler(ctsio, page_index,pc);

			memcpy(ctsio->kern_data_ptr + data_used,
			       page_index->page_data +
			       (page_index->page_len * pc),
			       page_index->page_len);
			data_used += page_index->page_len;
		}
		break;
	}
	}

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_lbp_log_sense_handler(struct ctl_scsiio *ctsio,
			       struct ctl_page_index *page_index,
			       int pc)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_log_param_header *phdr;
	uint8_t *data;
	uint64_t val;

	data = page_index->page_data;

	if (lun->backend->lun_attr != NULL &&
	    (val = lun->backend->lun_attr(lun->be_lun->be_lun, "blocksavail"))
	     != UINT64_MAX) {
		phdr = (struct scsi_log_param_header *)data;
		scsi_ulto2b(0x0001, phdr->param_code);
		phdr->param_control = SLP_LBIN | SLP_LP;
		phdr->param_len = 8;
		data = (uint8_t *)(phdr + 1);
		scsi_ulto4b(val >> CTL_LBP_EXPONENT, data);
		data[4] = 0x02; /* per-pool */
		data += phdr->param_len;
	}

	if (lun->backend->lun_attr != NULL &&
	    (val = lun->backend->lun_attr(lun->be_lun->be_lun, "blocksused"))
	     != UINT64_MAX) {
		phdr = (struct scsi_log_param_header *)data;
		scsi_ulto2b(0x0002, phdr->param_code);
		phdr->param_control = SLP_LBIN | SLP_LP;
		phdr->param_len = 8;
		data = (uint8_t *)(phdr + 1);
		scsi_ulto4b(val >> CTL_LBP_EXPONENT, data);
		data[4] = 0x01; /* per-LUN */
		data += phdr->param_len;
	}

	if (lun->backend->lun_attr != NULL &&
	    (val = lun->backend->lun_attr(lun->be_lun->be_lun, "poolblocksavail"))
	     != UINT64_MAX) {
		phdr = (struct scsi_log_param_header *)data;
		scsi_ulto2b(0x00f1, phdr->param_code);
		phdr->param_control = SLP_LBIN | SLP_LP;
		phdr->param_len = 8;
		data = (uint8_t *)(phdr + 1);
		scsi_ulto4b(val >> CTL_LBP_EXPONENT, data);
		data[4] = 0x02; /* per-pool */
		data += phdr->param_len;
	}

	if (lun->backend->lun_attr != NULL &&
	    (val = lun->backend->lun_attr(lun->be_lun->be_lun, "poolblocksused"))
	     != UINT64_MAX) {
		phdr = (struct scsi_log_param_header *)data;
		scsi_ulto2b(0x00f2, phdr->param_code);
		phdr->param_control = SLP_LBIN | SLP_LP;
		phdr->param_len = 8;
		data = (uint8_t *)(phdr + 1);
		scsi_ulto4b(val >> CTL_LBP_EXPONENT, data);
		data[4] = 0x02; /* per-pool */
		data += phdr->param_len;
	}

	page_index->page_len = data - page_index->page_data;
	return (0);
}

int
ctl_sap_log_sense_handler(struct ctl_scsiio *ctsio,
			       struct ctl_page_index *page_index,
			       int pc)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct stat_page *data;
	struct bintime *t;

	data = (struct stat_page *)page_index->page_data;

	scsi_ulto2b(SLP_SAP, data->sap.hdr.param_code);
	data->sap.hdr.param_control = SLP_LBIN;
	data->sap.hdr.param_len = sizeof(struct scsi_log_stat_and_perf) -
	    sizeof(struct scsi_log_param_header);
	scsi_u64to8b(lun->stats.operations[CTL_STATS_READ],
	    data->sap.read_num);
	scsi_u64to8b(lun->stats.operations[CTL_STATS_WRITE],
	    data->sap.write_num);
	if (lun->be_lun->blocksize > 0) {
		scsi_u64to8b(lun->stats.bytes[CTL_STATS_WRITE] /
		    lun->be_lun->blocksize, data->sap.recvieved_lba);
		scsi_u64to8b(lun->stats.bytes[CTL_STATS_READ] /
		    lun->be_lun->blocksize, data->sap.transmitted_lba);
	}
	t = &lun->stats.time[CTL_STATS_READ];
	scsi_u64to8b((uint64_t)t->sec * 1000 + t->frac / (UINT64_MAX / 1000),
	    data->sap.read_int);
	t = &lun->stats.time[CTL_STATS_WRITE];
	scsi_u64to8b((uint64_t)t->sec * 1000 + t->frac / (UINT64_MAX / 1000),
	    data->sap.write_int);
	scsi_u64to8b(0, data->sap.weighted_num);
	scsi_u64to8b(0, data->sap.weighted_int);
	scsi_ulto2b(SLP_IT, data->it.hdr.param_code);
	data->it.hdr.param_control = SLP_LBIN;
	data->it.hdr.param_len = sizeof(struct scsi_log_idle_time) -
	    sizeof(struct scsi_log_param_header);
#ifdef CTL_TIME_IO
	scsi_u64to8b(lun->idle_time / SBT_1MS, data->it.idle_int);
#endif
	scsi_ulto2b(SLP_TI, data->ti.hdr.param_code);
	data->it.hdr.param_control = SLP_LBIN;
	data->ti.hdr.param_len = sizeof(struct scsi_log_time_interval) -
	    sizeof(struct scsi_log_param_header);
	scsi_ulto4b(3, data->ti.exponent);
	scsi_ulto4b(1, data->ti.integer);
	return (0);
}

int
ctl_ie_log_sense_handler(struct ctl_scsiio *ctsio,
			       struct ctl_page_index *page_index,
			       int pc)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_log_informational_exceptions *data;

	data = (struct scsi_log_informational_exceptions *)page_index->page_data;

	scsi_ulto2b(SLP_IE_GEN, data->hdr.param_code);
	data->hdr.param_control = SLP_LBIN;
	data->hdr.param_len = sizeof(struct scsi_log_informational_exceptions) -
	    sizeof(struct scsi_log_param_header);
	data->ie_asc = lun->ie_asc;
	data->ie_ascq = lun->ie_ascq;
	data->temperature = 0xff;
	return (0);
}

int
ctl_log_sense(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	int i, pc, page_code, subpage;
	int alloc_len, total_len;
	struct ctl_page_index *page_index;
	struct scsi_log_sense *cdb;
	struct scsi_log_header *header;

	CTL_DEBUG_PRINT(("ctl_log_sense\n"));

	cdb = (struct scsi_log_sense *)ctsio->cdb;
	pc = (cdb->page & SLS_PAGE_CTRL_MASK) >> 6;
	page_code = cdb->page & SLS_PAGE_CODE;
	subpage = cdb->subpage;
	alloc_len = scsi_2btoul(cdb->length);

	page_index = NULL;
	for (i = 0; i < CTL_NUM_LOG_PAGES; i++) {
		page_index = &lun->log_pages.index[i];

		/* Look for the right page code */
		if ((page_index->page_code & SL_PAGE_CODE) != page_code)
			continue;

		/* Look for the right subpage or the subpage wildcard*/
		if (page_index->subpage != subpage)
			continue;

		break;
	}
	if (i >= CTL_NUM_LOG_PAGES) {
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 2,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	total_len = sizeof(struct scsi_log_header) + page_index->page_len;

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	header = (struct scsi_log_header *)ctsio->kern_data_ptr;
	header->page = page_index->page_code;
	if (page_index->page_code == SLS_LOGICAL_BLOCK_PROVISIONING)
		header->page |= SL_DS;
	if (page_index->subpage) {
		header->page |= SL_SPF;
		header->subpage = page_index->subpage;
	}
	scsi_ulto2b(page_index->page_len, header->datalen);

	/*
	 * Call the handler, if it exists, to update the
	 * page to the latest values.
	 */
	if (page_index->sense_handler != NULL)
		page_index->sense_handler(ctsio, page_index, pc);

	memcpy(header + 1, page_index->page_data, page_index->page_len);

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_read_capacity(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_read_capacity *cdb;
	struct scsi_read_capacity_data *data;
	uint32_t lba;

	CTL_DEBUG_PRINT(("ctl_read_capacity\n"));

	cdb = (struct scsi_read_capacity *)ctsio->cdb;

	lba = scsi_4btoul(cdb->addr);
	if (((cdb->pmi & SRC_PMI) == 0)
	 && (lba != 0)) {
		ctl_set_invalid_field(/*ctsio*/ ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 2,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	ctsio->kern_data_ptr = malloc(sizeof(*data), M_CTL, M_WAITOK | M_ZERO);
	data = (struct scsi_read_capacity_data *)ctsio->kern_data_ptr;
	ctsio->kern_data_len = sizeof(*data);
	ctsio->kern_total_len = sizeof(*data);
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;

	/*
	 * If the maximum LBA is greater than 0xfffffffe, the user must
	 * issue a SERVICE ACTION IN (16) command, with the read capacity
	 * serivce action set.
	 */
	if (lun->be_lun->maxlba > 0xfffffffe)
		scsi_ulto4b(0xffffffff, data->addr);
	else
		scsi_ulto4b(lun->be_lun->maxlba, data->addr);

	/*
	 * XXX KDM this may not be 512 bytes...
	 */
	scsi_ulto4b(lun->be_lun->blocksize, data->length);

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_read_capacity_16(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_read_capacity_16 *cdb;
	struct scsi_read_capacity_data_long *data;
	uint64_t lba;
	uint32_t alloc_len;

	CTL_DEBUG_PRINT(("ctl_read_capacity_16\n"));

	cdb = (struct scsi_read_capacity_16 *)ctsio->cdb;

	alloc_len = scsi_4btoul(cdb->alloc_len);
	lba = scsi_8btou64(cdb->addr);

	if ((cdb->reladr & SRC16_PMI)
	 && (lba != 0)) {
		ctl_set_invalid_field(/*ctsio*/ ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 2,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	ctsio->kern_data_ptr = malloc(sizeof(*data), M_CTL, M_WAITOK | M_ZERO);
	data = (struct scsi_read_capacity_data_long *)ctsio->kern_data_ptr;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(sizeof(*data), alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	scsi_u64to8b(lun->be_lun->maxlba, data->addr);
	/* XXX KDM this may not be 512 bytes... */
	scsi_ulto4b(lun->be_lun->blocksize, data->length);
	data->prot_lbppbe = lun->be_lun->pblockexp & SRC16_LBPPBE;
	scsi_ulto2b(lun->be_lun->pblockoff & SRC16_LALBA_A, data->lalba_lbp);
	if (lun->be_lun->flags & CTL_LUN_FLAG_UNMAP)
		data->lalba_lbp[0] |= SRC16_LBPME | SRC16_LBPRZ;

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_get_lba_status(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_get_lba_status *cdb;
	struct scsi_get_lba_status_data *data;
	struct ctl_lba_len_flags *lbalen;
	uint64_t lba;
	uint32_t alloc_len, total_len;
	int retval;

	CTL_DEBUG_PRINT(("ctl_get_lba_status\n"));

	cdb = (struct scsi_get_lba_status *)ctsio->cdb;
	lba = scsi_8btou64(cdb->addr);
	alloc_len = scsi_4btoul(cdb->alloc_len);

	if (lba > lun->be_lun->maxlba) {
		ctl_set_lba_out_of_range(ctsio, lba);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	total_len = sizeof(*data) + sizeof(data->descr[0]);
	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	data = (struct scsi_get_lba_status_data *)ctsio->kern_data_ptr;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/* Fill dummy data in case backend can't tell anything. */
	scsi_ulto4b(4 + sizeof(data->descr[0]), data->length);
	scsi_u64to8b(lba, data->descr[0].addr);
	scsi_ulto4b(MIN(UINT32_MAX, lun->be_lun->maxlba + 1 - lba),
	    data->descr[0].length);
	data->descr[0].status = 0; /* Mapped or unknown. */

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;

	lbalen = (struct ctl_lba_len_flags *)&ctsio->io_hdr.ctl_private[CTL_PRIV_LBA_LEN];
	lbalen->lba = lba;
	lbalen->len = total_len;
	lbalen->flags = 0;
	retval = lun->backend->config_read((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_read_defect(struct ctl_scsiio *ctsio)
{
	struct scsi_read_defect_data_10 *ccb10;
	struct scsi_read_defect_data_12 *ccb12;
	struct scsi_read_defect_data_hdr_10 *data10;
	struct scsi_read_defect_data_hdr_12 *data12;
	uint32_t alloc_len, data_len;
	uint8_t format;

	CTL_DEBUG_PRINT(("ctl_read_defect\n"));

	if (ctsio->cdb[0] == READ_DEFECT_DATA_10) {
		ccb10 = (struct scsi_read_defect_data_10 *)&ctsio->cdb;
		format = ccb10->format;
		alloc_len = scsi_2btoul(ccb10->alloc_length);
		data_len = sizeof(*data10);
	} else {
		ccb12 = (struct scsi_read_defect_data_12 *)&ctsio->cdb;
		format = ccb12->format;
		alloc_len = scsi_4btoul(ccb12->alloc_length);
		data_len = sizeof(*data12);
	}
	if (alloc_len == 0) {
		ctl_set_success(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	if (ctsio->cdb[0] == READ_DEFECT_DATA_10) {
		data10 = (struct scsi_read_defect_data_hdr_10 *)
		    ctsio->kern_data_ptr;
		data10->format = format;
		scsi_ulto2b(0, data10->length);
	} else {
		data12 = (struct scsi_read_defect_data_hdr_12 *)
		    ctsio->kern_data_ptr;
		data12->format = format;
		scsi_ulto2b(0, data12->generation);
		scsi_ulto4b(0, data12->length);
	}

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_report_tagret_port_groups(struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_maintenance_in *cdb;
	int retval;
	int alloc_len, ext, total_len = 0, g, pc, pg, ts, os;
	int num_ha_groups, num_target_ports, shared_group;
	struct ctl_port *port;
	struct scsi_target_group_data *rtg_ptr;
	struct scsi_target_group_data_extended *rtg_ext_ptr;
	struct scsi_target_port_group_descriptor *tpg_desc;

	CTL_DEBUG_PRINT(("ctl_report_tagret_port_groups\n"));

	cdb = (struct scsi_maintenance_in *)ctsio->cdb;
	retval = CTL_RETVAL_COMPLETE;

	switch (cdb->byte2 & STG_PDF_MASK) {
	case STG_PDF_LENGTH:
		ext = 0;
		break;
	case STG_PDF_EXTENDED:
		ext = 1;
		break;
	default:
		ctl_set_invalid_field(/*ctsio*/ ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 2,
				      /*bit_valid*/ 1,
				      /*bit*/ 5);
		ctl_done((union ctl_io *)ctsio);
		return(retval);
	}

	num_target_ports = 0;
	shared_group = (softc->is_single != 0);
	mtx_lock(&softc->ctl_lock);
	STAILQ_FOREACH(port, &softc->port_list, links) {
		if ((port->status & CTL_PORT_STATUS_ONLINE) == 0)
			continue;
		if (ctl_lun_map_to_port(port, lun->lun) == UINT32_MAX)
			continue;
		num_target_ports++;
		if (port->status & CTL_PORT_STATUS_HA_SHARED)
			shared_group = 1;
	}
	mtx_unlock(&softc->ctl_lock);
	num_ha_groups = (softc->is_single) ? 0 : NUM_HA_SHELVES;

	if (ext)
		total_len = sizeof(struct scsi_target_group_data_extended);
	else
		total_len = sizeof(struct scsi_target_group_data);
	total_len += sizeof(struct scsi_target_port_group_descriptor) *
		(shared_group + num_ha_groups) +
	    sizeof(struct scsi_target_port_descriptor) * num_target_ports;

	alloc_len = scsi_4btoul(cdb->length);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	if (ext) {
		rtg_ext_ptr = (struct scsi_target_group_data_extended *)
		    ctsio->kern_data_ptr;
		scsi_ulto4b(total_len - 4, rtg_ext_ptr->length);
		rtg_ext_ptr->format_type = 0x10;
		rtg_ext_ptr->implicit_transition_time = 0;
		tpg_desc = &rtg_ext_ptr->groups[0];
	} else {
		rtg_ptr = (struct scsi_target_group_data *)
		    ctsio->kern_data_ptr;
		scsi_ulto4b(total_len - 4, rtg_ptr->length);
		tpg_desc = &rtg_ptr->groups[0];
	}

	mtx_lock(&softc->ctl_lock);
	pg = softc->port_min / softc->port_cnt;
	if (lun->flags & (CTL_LUN_PRIMARY_SC | CTL_LUN_PEER_SC_PRIMARY)) {
		/* Some shelf is known to be primary. */
		if (softc->ha_link == CTL_HA_LINK_OFFLINE)
			os = TPG_ASYMMETRIC_ACCESS_UNAVAILABLE;
		else if (softc->ha_link == CTL_HA_LINK_UNKNOWN)
			os = TPG_ASYMMETRIC_ACCESS_TRANSITIONING;
		else if (softc->ha_mode == CTL_HA_MODE_ACT_STBY)
			os = TPG_ASYMMETRIC_ACCESS_STANDBY;
		else
			os = TPG_ASYMMETRIC_ACCESS_NONOPTIMIZED;
		if (lun->flags & CTL_LUN_PRIMARY_SC) {
			ts = TPG_ASYMMETRIC_ACCESS_OPTIMIZED;
		} else {
			ts = os;
			os = TPG_ASYMMETRIC_ACCESS_OPTIMIZED;
		}
	} else {
		/* No known primary shelf. */
		if (softc->ha_link == CTL_HA_LINK_OFFLINE) {
			ts = TPG_ASYMMETRIC_ACCESS_UNAVAILABLE;
			os = TPG_ASYMMETRIC_ACCESS_OPTIMIZED;
		} else if (softc->ha_link == CTL_HA_LINK_UNKNOWN) {
			ts = TPG_ASYMMETRIC_ACCESS_TRANSITIONING;
			os = TPG_ASYMMETRIC_ACCESS_OPTIMIZED;
		} else {
			ts = os = TPG_ASYMMETRIC_ACCESS_TRANSITIONING;
		}
	}
	if (shared_group) {
		tpg_desc->pref_state = ts;
		tpg_desc->support = TPG_AO_SUP | TPG_AN_SUP | TPG_S_SUP |
		    TPG_U_SUP | TPG_T_SUP;
		scsi_ulto2b(1, tpg_desc->target_port_group);
		tpg_desc->status = TPG_IMPLICIT;
		pc = 0;
		STAILQ_FOREACH(port, &softc->port_list, links) {
			if ((port->status & CTL_PORT_STATUS_ONLINE) == 0)
				continue;
			if (!softc->is_single &&
			    (port->status & CTL_PORT_STATUS_HA_SHARED) == 0)
				continue;
			if (ctl_lun_map_to_port(port, lun->lun) == UINT32_MAX)
				continue;
			scsi_ulto2b(port->targ_port, tpg_desc->descriptors[pc].
			    relative_target_port_identifier);
			pc++;
		}
		tpg_desc->target_port_count = pc;
		tpg_desc = (struct scsi_target_port_group_descriptor *)
		    &tpg_desc->descriptors[pc];
	}
	for (g = 0; g < num_ha_groups; g++) {
		tpg_desc->pref_state = (g == pg) ? ts : os;
		tpg_desc->support = TPG_AO_SUP | TPG_AN_SUP | TPG_S_SUP |
		    TPG_U_SUP | TPG_T_SUP;
		scsi_ulto2b(2 + g, tpg_desc->target_port_group);
		tpg_desc->status = TPG_IMPLICIT;
		pc = 0;
		STAILQ_FOREACH(port, &softc->port_list, links) {
			if (port->targ_port < g * softc->port_cnt ||
			    port->targ_port >= (g + 1) * softc->port_cnt)
				continue;
			if ((port->status & CTL_PORT_STATUS_ONLINE) == 0)
				continue;
			if (port->status & CTL_PORT_STATUS_HA_SHARED)
				continue;
			if (ctl_lun_map_to_port(port, lun->lun) == UINT32_MAX)
				continue;
			scsi_ulto2b(port->targ_port, tpg_desc->descriptors[pc].
			    relative_target_port_identifier);
			pc++;
		}
		tpg_desc->target_port_count = pc;
		tpg_desc = (struct scsi_target_port_group_descriptor *)
		    &tpg_desc->descriptors[pc];
	}
	mtx_unlock(&softc->ctl_lock);

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return(retval);
}

int
ctl_report_supported_opcodes(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_report_supported_opcodes *cdb;
	const struct ctl_cmd_entry *entry, *sentry;
	struct scsi_report_supported_opcodes_all *all;
	struct scsi_report_supported_opcodes_descr *descr;
	struct scsi_report_supported_opcodes_one *one;
	int retval;
	int alloc_len, total_len;
	int opcode, service_action, i, j, num;

	CTL_DEBUG_PRINT(("ctl_report_supported_opcodes\n"));

	cdb = (struct scsi_report_supported_opcodes *)ctsio->cdb;
	retval = CTL_RETVAL_COMPLETE;

	opcode = cdb->requested_opcode;
	service_action = scsi_2btoul(cdb->requested_service_action);
	switch (cdb->options & RSO_OPTIONS_MASK) {
	case RSO_OPTIONS_ALL:
		num = 0;
		for (i = 0; i < 256; i++) {
			entry = &ctl_cmd_table[i];
			if (entry->flags & CTL_CMD_FLAG_SA5) {
				for (j = 0; j < 32; j++) {
					sentry = &((const struct ctl_cmd_entry *)
					    entry->execute)[j];
					if (ctl_cmd_applicable(
					    lun->be_lun->lun_type, sentry))
						num++;
				}
			} else {
				if (ctl_cmd_applicable(lun->be_lun->lun_type,
				    entry))
					num++;
			}
		}
		total_len = sizeof(struct scsi_report_supported_opcodes_all) +
		    num * sizeof(struct scsi_report_supported_opcodes_descr);
		break;
	case RSO_OPTIONS_OC:
		if (ctl_cmd_table[opcode].flags & CTL_CMD_FLAG_SA5) {
			ctl_set_invalid_field(/*ctsio*/ ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 1,
					      /*field*/ 2,
					      /*bit_valid*/ 1,
					      /*bit*/ 2);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}
		total_len = sizeof(struct scsi_report_supported_opcodes_one) + 32;
		break;
	case RSO_OPTIONS_OC_SA:
		if ((ctl_cmd_table[opcode].flags & CTL_CMD_FLAG_SA5) == 0 ||
		    service_action >= 32) {
			ctl_set_invalid_field(/*ctsio*/ ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 1,
					      /*field*/ 2,
					      /*bit_valid*/ 1,
					      /*bit*/ 2);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}
		/* FALLTHROUGH */
	case RSO_OPTIONS_OC_ASA:
		total_len = sizeof(struct scsi_report_supported_opcodes_one) + 32;
		break;
	default:
		ctl_set_invalid_field(/*ctsio*/ ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 2,
				      /*bit_valid*/ 1,
				      /*bit*/ 2);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	alloc_len = scsi_4btoul(cdb->length);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	switch (cdb->options & RSO_OPTIONS_MASK) {
	case RSO_OPTIONS_ALL:
		all = (struct scsi_report_supported_opcodes_all *)
		    ctsio->kern_data_ptr;
		num = 0;
		for (i = 0; i < 256; i++) {
			entry = &ctl_cmd_table[i];
			if (entry->flags & CTL_CMD_FLAG_SA5) {
				for (j = 0; j < 32; j++) {
					sentry = &((const struct ctl_cmd_entry *)
					    entry->execute)[j];
					if (!ctl_cmd_applicable(
					    lun->be_lun->lun_type, sentry))
						continue;
					descr = &all->descr[num++];
					descr->opcode = i;
					scsi_ulto2b(j, descr->service_action);
					descr->flags = RSO_SERVACTV;
					scsi_ulto2b(sentry->length,
					    descr->cdb_length);
				}
			} else {
				if (!ctl_cmd_applicable(lun->be_lun->lun_type,
				    entry))
					continue;
				descr = &all->descr[num++];
				descr->opcode = i;
				scsi_ulto2b(0, descr->service_action);
				descr->flags = 0;
				scsi_ulto2b(entry->length, descr->cdb_length);
			}
		}
		scsi_ulto4b(
		    num * sizeof(struct scsi_report_supported_opcodes_descr),
		    all->length);
		break;
	case RSO_OPTIONS_OC:
		one = (struct scsi_report_supported_opcodes_one *)
		    ctsio->kern_data_ptr;
		entry = &ctl_cmd_table[opcode];
		goto fill_one;
	case RSO_OPTIONS_OC_SA:
		one = (struct scsi_report_supported_opcodes_one *)
		    ctsio->kern_data_ptr;
		entry = &ctl_cmd_table[opcode];
		entry = &((const struct ctl_cmd_entry *)
		    entry->execute)[service_action];
fill_one:
		if (ctl_cmd_applicable(lun->be_lun->lun_type, entry)) {
			one->support = 3;
			scsi_ulto2b(entry->length, one->cdb_length);
			one->cdb_usage[0] = opcode;
			memcpy(&one->cdb_usage[1], entry->usage,
			    entry->length - 1);
		} else
			one->support = 1;
		break;
	case RSO_OPTIONS_OC_ASA:
		one = (struct scsi_report_supported_opcodes_one *)
		    ctsio->kern_data_ptr;
		entry = &ctl_cmd_table[opcode];
		if (entry->flags & CTL_CMD_FLAG_SA5) {
			entry = &((const struct ctl_cmd_entry *)
			    entry->execute)[service_action];
		} else if (service_action != 0) {
			one->support = 1;
			break;
		}
		goto fill_one;
	}

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return(retval);
}

int
ctl_report_supported_tmf(struct ctl_scsiio *ctsio)
{
	struct scsi_report_supported_tmf *cdb;
	struct scsi_report_supported_tmf_ext_data *data;
	int retval;
	int alloc_len, total_len;

	CTL_DEBUG_PRINT(("ctl_report_supported_tmf\n"));

	cdb = (struct scsi_report_supported_tmf *)ctsio->cdb;

	retval = CTL_RETVAL_COMPLETE;

	if (cdb->options & RST_REPD)
		total_len = sizeof(struct scsi_report_supported_tmf_ext_data);
	else
		total_len = sizeof(struct scsi_report_supported_tmf_data);
	alloc_len = scsi_4btoul(cdb->length);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	data = (struct scsi_report_supported_tmf_ext_data *)ctsio->kern_data_ptr;
	data->byte1 |= RST_ATS | RST_ATSS | RST_CTSS | RST_LURS | RST_QTS |
	    RST_TRS;
	data->byte2 |= RST_QAES | RST_QTSS | RST_ITNRS;
	data->length = total_len - 4;

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_report_timestamp(struct ctl_scsiio *ctsio)
{
	struct scsi_report_timestamp *cdb;
	struct scsi_report_timestamp_data *data;
	struct timeval tv;
	int64_t timestamp;
	int retval;
	int alloc_len, total_len;

	CTL_DEBUG_PRINT(("ctl_report_timestamp\n"));

	cdb = (struct scsi_report_timestamp *)ctsio->cdb;

	retval = CTL_RETVAL_COMPLETE;

	total_len = sizeof(struct scsi_report_timestamp_data);
	alloc_len = scsi_4btoul(cdb->length);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	data = (struct scsi_report_timestamp_data *)ctsio->kern_data_ptr;
	scsi_ulto2b(sizeof(*data) - 2, data->length);
	data->origin = RTS_ORIG_OUTSIDE;
	getmicrotime(&tv);
	timestamp = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
	scsi_ulto4b(timestamp >> 16, data->timestamp);
	scsi_ulto2b(timestamp & 0xffff, &data->timestamp[4]);

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_persistent_reserve_in(struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_per_res_in *cdb;
	int alloc_len, total_len = 0;
	/* struct scsi_per_res_in_rsrv in_data; */
	uint64_t key;

	CTL_DEBUG_PRINT(("ctl_persistent_reserve_in\n"));

	cdb = (struct scsi_per_res_in *)ctsio->cdb;

	alloc_len = scsi_2btoul(cdb->length);

retry:
	mtx_lock(&lun->lun_lock);
	switch (cdb->action) {
	case SPRI_RK: /* read keys */
		total_len = sizeof(struct scsi_per_res_in_keys) +
			lun->pr_key_count *
			sizeof(struct scsi_per_res_key);
		break;
	case SPRI_RR: /* read reservation */
		if (lun->flags & CTL_LUN_PR_RESERVED)
			total_len = sizeof(struct scsi_per_res_in_rsrv);
		else
			total_len = sizeof(struct scsi_per_res_in_header);
		break;
	case SPRI_RC: /* report capabilities */
		total_len = sizeof(struct scsi_per_res_cap);
		break;
	case SPRI_RS: /* read full status */
		total_len = sizeof(struct scsi_per_res_in_header) +
		    (sizeof(struct scsi_per_res_in_full_desc) + 256) *
		    lun->pr_key_count;
		break;
	default:
		panic("%s: Invalid PR type %#x", __func__, cdb->action);
	}
	mtx_unlock(&lun->lun_lock);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	mtx_lock(&lun->lun_lock);
	switch (cdb->action) {
	case SPRI_RK: { // read keys
        struct scsi_per_res_in_keys *res_keys;
		int i, key_count;

		res_keys = (struct scsi_per_res_in_keys*)ctsio->kern_data_ptr;

		/*
		 * We had to drop the lock to allocate our buffer, which
		 * leaves time for someone to come in with another
		 * persistent reservation.  (That is unlikely, though,
		 * since this should be the only persistent reservation
		 * command active right now.)
		 */
		if (total_len != (sizeof(struct scsi_per_res_in_keys) +
		    (lun->pr_key_count *
		     sizeof(struct scsi_per_res_key)))){
			mtx_unlock(&lun->lun_lock);
			free(ctsio->kern_data_ptr, M_CTL);
			printf("%s: reservation length changed, retrying\n",
			       __func__);
			goto retry;
		}

		scsi_ulto4b(lun->pr_generation, res_keys->header.generation);

		scsi_ulto4b(sizeof(struct scsi_per_res_key) *
			     lun->pr_key_count, res_keys->header.length);

		for (i = 0, key_count = 0; i < CTL_MAX_INITIATORS; i++) {
			if ((key = ctl_get_prkey(lun, i)) == 0)
				continue;

			/*
			 * We used lun->pr_key_count to calculate the
			 * size to allocate.  If it turns out the number of
			 * initiators with the registered flag set is
			 * larger than that (i.e. they haven't been kept in
			 * sync), we've got a problem.
			 */
			if (key_count >= lun->pr_key_count) {
				key_count++;
				continue;
			}
			scsi_u64to8b(key, res_keys->keys[key_count].key);
			key_count++;
		}
		break;
	}
	case SPRI_RR: { // read reservation
		struct scsi_per_res_in_rsrv *res;
		int tmp_len, header_only;

		res = (struct scsi_per_res_in_rsrv *)ctsio->kern_data_ptr;

		scsi_ulto4b(lun->pr_generation, res->header.generation);

		if (lun->flags & CTL_LUN_PR_RESERVED)
		{
			tmp_len = sizeof(struct scsi_per_res_in_rsrv);
			scsi_ulto4b(sizeof(struct scsi_per_res_in_rsrv_data),
				    res->header.length);
			header_only = 0;
		} else {
			tmp_len = sizeof(struct scsi_per_res_in_header);
			scsi_ulto4b(0, res->header.length);
			header_only = 1;
		}

		/*
		 * We had to drop the lock to allocate our buffer, which
		 * leaves time for someone to come in with another
		 * persistent reservation.  (That is unlikely, though,
		 * since this should be the only persistent reservation
		 * command active right now.)
		 */
		if (tmp_len != total_len) {
			mtx_unlock(&lun->lun_lock);
			free(ctsio->kern_data_ptr, M_CTL);
			printf("%s: reservation status changed, retrying\n",
			       __func__);
			goto retry;
		}

		/*
		 * No reservation held, so we're done.
		 */
		if (header_only != 0)
			break;

		/*
		 * If the registration is an All Registrants type, the key
		 * is 0, since it doesn't really matter.
		 */
		if (lun->pr_res_idx != CTL_PR_ALL_REGISTRANTS) {
			scsi_u64to8b(ctl_get_prkey(lun, lun->pr_res_idx),
			    res->data.reservation);
		}
		res->data.scopetype = lun->pr_res_type;
		break;
	}
	case SPRI_RC:     //report capabilities
	{
		struct scsi_per_res_cap *res_cap;
		uint16_t type_mask;

		res_cap = (struct scsi_per_res_cap *)ctsio->kern_data_ptr;
		scsi_ulto2b(sizeof(*res_cap), res_cap->length);
		res_cap->flags1 = SPRI_CRH;
		res_cap->flags2 = SPRI_TMV | SPRI_ALLOW_5;
		type_mask = SPRI_TM_WR_EX_AR |
			    SPRI_TM_EX_AC_RO |
			    SPRI_TM_WR_EX_RO |
			    SPRI_TM_EX_AC |
			    SPRI_TM_WR_EX |
			    SPRI_TM_EX_AC_AR;
		scsi_ulto2b(type_mask, res_cap->type_mask);
		break;
	}
	case SPRI_RS: { // read full status
		struct scsi_per_res_in_full *res_status;
		struct scsi_per_res_in_full_desc *res_desc;
		struct ctl_port *port;
		int i, len;

		res_status = (struct scsi_per_res_in_full*)ctsio->kern_data_ptr;

		/*
		 * We had to drop the lock to allocate our buffer, which
		 * leaves time for someone to come in with another
		 * persistent reservation.  (That is unlikely, though,
		 * since this should be the only persistent reservation
		 * command active right now.)
		 */
		if (total_len < (sizeof(struct scsi_per_res_in_header) +
		    (sizeof(struct scsi_per_res_in_full_desc) + 256) *
		     lun->pr_key_count)){
			mtx_unlock(&lun->lun_lock);
			free(ctsio->kern_data_ptr, M_CTL);
			printf("%s: reservation length changed, retrying\n",
			       __func__);
			goto retry;
		}

		scsi_ulto4b(lun->pr_generation, res_status->header.generation);

		res_desc = &res_status->desc[0];
		for (i = 0; i < CTL_MAX_INITIATORS; i++) {
			if ((key = ctl_get_prkey(lun, i)) == 0)
				continue;

			scsi_u64to8b(key, res_desc->res_key.key);
			if ((lun->flags & CTL_LUN_PR_RESERVED) &&
			    (lun->pr_res_idx == i ||
			     lun->pr_res_idx == CTL_PR_ALL_REGISTRANTS)) {
				res_desc->flags = SPRI_FULL_R_HOLDER;
				res_desc->scopetype = lun->pr_res_type;
			}
			scsi_ulto2b(i / CTL_MAX_INIT_PER_PORT,
			    res_desc->rel_trgt_port_id);
			len = 0;
			port = softc->ctl_ports[i / CTL_MAX_INIT_PER_PORT];
			if (port != NULL)
				len = ctl_create_iid(port,
				    i % CTL_MAX_INIT_PER_PORT,
				    res_desc->transport_id);
			scsi_ulto4b(len, res_desc->additional_length);
			res_desc = (struct scsi_per_res_in_full_desc *)
			    &res_desc->transport_id[len];
		}
		scsi_ulto4b((uint8_t *)res_desc - (uint8_t *)&res_status->desc[0],
		    res_status->header.length);
		break;
	}
	default:
		panic("%s: Invalid PR type %#x", __func__, cdb->action);
	}
	mtx_unlock(&lun->lun_lock);

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

/*
 * Returns 0 if ctl_persistent_reserve_out() should continue, non-zero if
 * it should return.
 */
static int
ctl_pro_preempt(struct ctl_softc *softc, struct ctl_lun *lun, uint64_t res_key,
		uint64_t sa_res_key, uint8_t type, uint32_t residx,
		struct ctl_scsiio *ctsio, struct scsi_per_res_out *cdb,
		struct scsi_per_res_out_parms* param)
{
	union ctl_ha_msg persis_io;
	int i;

	mtx_lock(&lun->lun_lock);
	if (sa_res_key == 0) {
		if (lun->pr_res_idx == CTL_PR_ALL_REGISTRANTS) {
			/* validate scope and type */
			if ((cdb->scope_type & SPR_SCOPE_MASK) !=
			     SPR_LU_SCOPE) {
				mtx_unlock(&lun->lun_lock);
				ctl_set_invalid_field(/*ctsio*/ ctsio,
						      /*sks_valid*/ 1,
						      /*command*/ 1,
						      /*field*/ 2,
						      /*bit_valid*/ 1,
						      /*bit*/ 4);
				ctl_done((union ctl_io *)ctsio);
				return (1);
			}

		        if (type>8 || type==2 || type==4 || type==0) {
				mtx_unlock(&lun->lun_lock);
				ctl_set_invalid_field(/*ctsio*/ ctsio,
       	           				      /*sks_valid*/ 1,
						      /*command*/ 1,
						      /*field*/ 2,
						      /*bit_valid*/ 1,
						      /*bit*/ 0);
				ctl_done((union ctl_io *)ctsio);
				return (1);
		        }

			/*
			 * Unregister everybody else and build UA for
			 * them
			 */
			for(i = 0; i < CTL_MAX_INITIATORS; i++) {
				if (i == residx || ctl_get_prkey(lun, i) == 0)
					continue;

				ctl_clr_prkey(lun, i);
				ctl_est_ua(lun, i, CTL_UA_REG_PREEMPT);
			}
			lun->pr_key_count = 1;
			lun->pr_res_type = type;
			if (lun->pr_res_type != SPR_TYPE_WR_EX_AR &&
			    lun->pr_res_type != SPR_TYPE_EX_AC_AR)
				lun->pr_res_idx = residx;
			lun->pr_generation++;
			mtx_unlock(&lun->lun_lock);

			/* send msg to other side */
			persis_io.hdr.nexus = ctsio->io_hdr.nexus;
			persis_io.hdr.msg_type = CTL_MSG_PERS_ACTION;
			persis_io.pr.pr_info.action = CTL_PR_PREEMPT;
			persis_io.pr.pr_info.residx = lun->pr_res_idx;
			persis_io.pr.pr_info.res_type = type;
			memcpy(persis_io.pr.pr_info.sa_res_key,
			       param->serv_act_res_key,
			       sizeof(param->serv_act_res_key));
			ctl_ha_msg_send(CTL_HA_CHAN_CTL, &persis_io,
			    sizeof(persis_io.pr), M_WAITOK);
		} else {
			/* not all registrants */
			mtx_unlock(&lun->lun_lock);
			free(ctsio->kern_data_ptr, M_CTL);
			ctl_set_invalid_field(ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 0,
					      /*field*/ 8,
					      /*bit_valid*/ 0,
					      /*bit*/ 0);
			ctl_done((union ctl_io *)ctsio);
			return (1);
		}
	} else if (lun->pr_res_idx == CTL_PR_ALL_REGISTRANTS
		|| !(lun->flags & CTL_LUN_PR_RESERVED)) {
		int found = 0;

		if (res_key == sa_res_key) {
			/* special case */
			/*
			 * The spec implies this is not good but doesn't
			 * say what to do. There are two choices either
			 * generate a res conflict or check condition
			 * with illegal field in parameter data. Since
			 * that is what is done when the sa_res_key is
			 * zero I'll take that approach since this has
			 * to do with the sa_res_key.
			 */
			mtx_unlock(&lun->lun_lock);
			free(ctsio->kern_data_ptr, M_CTL);
			ctl_set_invalid_field(ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 0,
					      /*field*/ 8,
					      /*bit_valid*/ 0,
					      /*bit*/ 0);
			ctl_done((union ctl_io *)ctsio);
			return (1);
		}

		for (i = 0; i < CTL_MAX_INITIATORS; i++) {
			if (ctl_get_prkey(lun, i) != sa_res_key)
				continue;

			found = 1;
			ctl_clr_prkey(lun, i);
			lun->pr_key_count--;
			ctl_est_ua(lun, i, CTL_UA_REG_PREEMPT);
		}
		if (!found) {
			mtx_unlock(&lun->lun_lock);
			free(ctsio->kern_data_ptr, M_CTL);
			ctl_set_reservation_conflict(ctsio);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}
		lun->pr_generation++;
		mtx_unlock(&lun->lun_lock);

		/* send msg to other side */
		persis_io.hdr.nexus = ctsio->io_hdr.nexus;
		persis_io.hdr.msg_type = CTL_MSG_PERS_ACTION;
		persis_io.pr.pr_info.action = CTL_PR_PREEMPT;
		persis_io.pr.pr_info.residx = lun->pr_res_idx;
		persis_io.pr.pr_info.res_type = type;
		memcpy(persis_io.pr.pr_info.sa_res_key,
		       param->serv_act_res_key,
		       sizeof(param->serv_act_res_key));
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &persis_io,
		    sizeof(persis_io.pr), M_WAITOK);
	} else {
		/* Reserved but not all registrants */
		/* sa_res_key is res holder */
		if (sa_res_key == ctl_get_prkey(lun, lun->pr_res_idx)) {
			/* validate scope and type */
			if ((cdb->scope_type & SPR_SCOPE_MASK) !=
			     SPR_LU_SCOPE) {
				mtx_unlock(&lun->lun_lock);
				ctl_set_invalid_field(/*ctsio*/ ctsio,
						      /*sks_valid*/ 1,
						      /*command*/ 1,
						      /*field*/ 2,
						      /*bit_valid*/ 1,
						      /*bit*/ 4);
				ctl_done((union ctl_io *)ctsio);
				return (1);
			}

			if (type>8 || type==2 || type==4 || type==0) {
				mtx_unlock(&lun->lun_lock);
				ctl_set_invalid_field(/*ctsio*/ ctsio,
						      /*sks_valid*/ 1,
						      /*command*/ 1,
						      /*field*/ 2,
						      /*bit_valid*/ 1,
						      /*bit*/ 0);
				ctl_done((union ctl_io *)ctsio);
				return (1);
			}

			/*
			 * Do the following:
			 * if sa_res_key != res_key remove all
			 * registrants w/sa_res_key and generate UA
			 * for these registrants(Registrations
			 * Preempted) if it wasn't an exclusive
			 * reservation generate UA(Reservations
			 * Preempted) for all other registered nexuses
			 * if the type has changed. Establish the new
			 * reservation and holder. If res_key and
			 * sa_res_key are the same do the above
			 * except don't unregister the res holder.
			 */

			for(i = 0; i < CTL_MAX_INITIATORS; i++) {
				if (i == residx || ctl_get_prkey(lun, i) == 0)
					continue;

				if (sa_res_key == ctl_get_prkey(lun, i)) {
					ctl_clr_prkey(lun, i);
					lun->pr_key_count--;
					ctl_est_ua(lun, i, CTL_UA_REG_PREEMPT);
				} else if (type != lun->pr_res_type &&
				    (lun->pr_res_type == SPR_TYPE_WR_EX_RO ||
				     lun->pr_res_type == SPR_TYPE_EX_AC_RO)) {
					ctl_est_ua(lun, i, CTL_UA_RES_RELEASE);
				}
			}
			lun->pr_res_type = type;
			if (lun->pr_res_type != SPR_TYPE_WR_EX_AR &&
			    lun->pr_res_type != SPR_TYPE_EX_AC_AR)
				lun->pr_res_idx = residx;
			else
				lun->pr_res_idx = CTL_PR_ALL_REGISTRANTS;
			lun->pr_generation++;
			mtx_unlock(&lun->lun_lock);

			persis_io.hdr.nexus = ctsio->io_hdr.nexus;
			persis_io.hdr.msg_type = CTL_MSG_PERS_ACTION;
			persis_io.pr.pr_info.action = CTL_PR_PREEMPT;
			persis_io.pr.pr_info.residx = lun->pr_res_idx;
			persis_io.pr.pr_info.res_type = type;
			memcpy(persis_io.pr.pr_info.sa_res_key,
			       param->serv_act_res_key,
			       sizeof(param->serv_act_res_key));
			ctl_ha_msg_send(CTL_HA_CHAN_CTL, &persis_io,
			    sizeof(persis_io.pr), M_WAITOK);
		} else {
			/*
			 * sa_res_key is not the res holder just
			 * remove registrants
			 */
			int found=0;

			for (i = 0; i < CTL_MAX_INITIATORS; i++) {
				if (sa_res_key != ctl_get_prkey(lun, i))
					continue;

				found = 1;
				ctl_clr_prkey(lun, i);
				lun->pr_key_count--;
				ctl_est_ua(lun, i, CTL_UA_REG_PREEMPT);
			}

			if (!found) {
				mtx_unlock(&lun->lun_lock);
				free(ctsio->kern_data_ptr, M_CTL);
				ctl_set_reservation_conflict(ctsio);
				ctl_done((union ctl_io *)ctsio);
		        	return (1);
			}
			lun->pr_generation++;
			mtx_unlock(&lun->lun_lock);

			persis_io.hdr.nexus = ctsio->io_hdr.nexus;
			persis_io.hdr.msg_type = CTL_MSG_PERS_ACTION;
			persis_io.pr.pr_info.action = CTL_PR_PREEMPT;
			persis_io.pr.pr_info.residx = lun->pr_res_idx;
			persis_io.pr.pr_info.res_type = type;
			memcpy(persis_io.pr.pr_info.sa_res_key,
			       param->serv_act_res_key,
			       sizeof(param->serv_act_res_key));
			ctl_ha_msg_send(CTL_HA_CHAN_CTL, &persis_io,
			    sizeof(persis_io.pr), M_WAITOK);
		}
	}
	return (0);
}

static void
ctl_pro_preempt_other(struct ctl_lun *lun, union ctl_ha_msg *msg)
{
	uint64_t sa_res_key;
	int i;

	sa_res_key = scsi_8btou64(msg->pr.pr_info.sa_res_key);

	if (lun->pr_res_idx == CTL_PR_ALL_REGISTRANTS
	 || lun->pr_res_idx == CTL_PR_NO_RESERVATION
	 || sa_res_key != ctl_get_prkey(lun, lun->pr_res_idx)) {
		if (sa_res_key == 0) {
			/*
			 * Unregister everybody else and build UA for
			 * them
			 */
			for(i = 0; i < CTL_MAX_INITIATORS; i++) {
				if (i == msg->pr.pr_info.residx ||
				    ctl_get_prkey(lun, i) == 0)
					continue;

				ctl_clr_prkey(lun, i);
				ctl_est_ua(lun, i, CTL_UA_REG_PREEMPT);
			}

			lun->pr_key_count = 1;
			lun->pr_res_type = msg->pr.pr_info.res_type;
			if (lun->pr_res_type != SPR_TYPE_WR_EX_AR &&
			    lun->pr_res_type != SPR_TYPE_EX_AC_AR)
				lun->pr_res_idx = msg->pr.pr_info.residx;
		} else {
		        for (i = 0; i < CTL_MAX_INITIATORS; i++) {
				if (sa_res_key == ctl_get_prkey(lun, i))
					continue;

				ctl_clr_prkey(lun, i);
				lun->pr_key_count--;
				ctl_est_ua(lun, i, CTL_UA_REG_PREEMPT);
			}
		}
	} else {
		for (i = 0; i < CTL_MAX_INITIATORS; i++) {
			if (i == msg->pr.pr_info.residx ||
			    ctl_get_prkey(lun, i) == 0)
				continue;

			if (sa_res_key == ctl_get_prkey(lun, i)) {
				ctl_clr_prkey(lun, i);
				lun->pr_key_count--;
				ctl_est_ua(lun, i, CTL_UA_REG_PREEMPT);
			} else if (msg->pr.pr_info.res_type != lun->pr_res_type
			    && (lun->pr_res_type == SPR_TYPE_WR_EX_RO ||
			     lun->pr_res_type == SPR_TYPE_EX_AC_RO)) {
				ctl_est_ua(lun, i, CTL_UA_RES_RELEASE);
			}
		}
		lun->pr_res_type = msg->pr.pr_info.res_type;
		if (lun->pr_res_type != SPR_TYPE_WR_EX_AR &&
		    lun->pr_res_type != SPR_TYPE_EX_AC_AR)
			lun->pr_res_idx = msg->pr.pr_info.residx;
		else
			lun->pr_res_idx = CTL_PR_ALL_REGISTRANTS;
	}
	lun->pr_generation++;

}


int
ctl_persistent_reserve_out(struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_lun *lun = CTL_LUN(ctsio);
	int retval;
	u_int32_t param_len;
	struct scsi_per_res_out *cdb;
	struct scsi_per_res_out_parms* param;
	uint32_t residx;
	uint64_t res_key, sa_res_key, key;
	uint8_t type;
	union ctl_ha_msg persis_io;
	int    i;

	CTL_DEBUG_PRINT(("ctl_persistent_reserve_out\n"));

	cdb = (struct scsi_per_res_out *)ctsio->cdb;
	retval = CTL_RETVAL_COMPLETE;

	/*
	 * We only support whole-LUN scope.  The scope & type are ignored for
	 * register, register and ignore existing key and clear.
	 * We sometimes ignore scope and type on preempts too!!
	 * Verify reservation type here as well.
	 */
	type = cdb->scope_type & SPR_TYPE_MASK;
	if ((cdb->action == SPRO_RESERVE)
	 || (cdb->action == SPRO_RELEASE)) {
		if ((cdb->scope_type & SPR_SCOPE_MASK) != SPR_LU_SCOPE) {
			ctl_set_invalid_field(/*ctsio*/ ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 1,
					      /*field*/ 2,
					      /*bit_valid*/ 1,
					      /*bit*/ 4);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}

		if (type>8 || type==2 || type==4 || type==0) {
			ctl_set_invalid_field(/*ctsio*/ ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 1,
					      /*field*/ 2,
					      /*bit_valid*/ 1,
					      /*bit*/ 0);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}
	}

	param_len = scsi_4btoul(cdb->length);

	if ((ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0) {
		ctsio->kern_data_ptr = malloc(param_len, M_CTL, M_WAITOK);
		ctsio->kern_data_len = param_len;
		ctsio->kern_total_len = param_len;
		ctsio->kern_rel_offset = 0;
		ctsio->kern_sg_entries = 0;
		ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
		ctsio->be_move_done = ctl_config_move_done;
		ctl_datamove((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
	}

	param = (struct scsi_per_res_out_parms *)ctsio->kern_data_ptr;

	residx = ctl_get_initindex(&ctsio->io_hdr.nexus);
	res_key = scsi_8btou64(param->res_key.key);
	sa_res_key = scsi_8btou64(param->serv_act_res_key);

	/*
	 * Validate the reservation key here except for SPRO_REG_IGNO
	 * This must be done for all other service actions
	 */
	if ((cdb->action & SPRO_ACTION_MASK) != SPRO_REG_IGNO) {
		mtx_lock(&lun->lun_lock);
		if ((key = ctl_get_prkey(lun, residx)) != 0) {
			if (res_key != key) {
				/*
				 * The current key passed in doesn't match
				 * the one the initiator previously
				 * registered.
				 */
				mtx_unlock(&lun->lun_lock);
				free(ctsio->kern_data_ptr, M_CTL);
				ctl_set_reservation_conflict(ctsio);
				ctl_done((union ctl_io *)ctsio);
				return (CTL_RETVAL_COMPLETE);
			}
		} else if ((cdb->action & SPRO_ACTION_MASK) != SPRO_REGISTER) {
			/*
			 * We are not registered
			 */
			mtx_unlock(&lun->lun_lock);
			free(ctsio->kern_data_ptr, M_CTL);
			ctl_set_reservation_conflict(ctsio);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		} else if (res_key != 0) {
			/*
			 * We are not registered and trying to register but
			 * the register key isn't zero.
			 */
			mtx_unlock(&lun->lun_lock);
			free(ctsio->kern_data_ptr, M_CTL);
			ctl_set_reservation_conflict(ctsio);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}
		mtx_unlock(&lun->lun_lock);
	}

	switch (cdb->action & SPRO_ACTION_MASK) {
	case SPRO_REGISTER:
	case SPRO_REG_IGNO: {

		/*
		 * We don't support any of these options, as we report in
		 * the read capabilities request (see
		 * ctl_persistent_reserve_in(), above).
		 */
		if ((param->flags & SPR_SPEC_I_PT)
		 || (param->flags & SPR_ALL_TG_PT)
		 || (param->flags & SPR_APTPL)) {
			int bit_ptr;

			if (param->flags & SPR_APTPL)
				bit_ptr = 0;
			else if (param->flags & SPR_ALL_TG_PT)
				bit_ptr = 2;
			else /* SPR_SPEC_I_PT */
				bit_ptr = 3;

			free(ctsio->kern_data_ptr, M_CTL);
			ctl_set_invalid_field(ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 0,
					      /*field*/ 20,
					      /*bit_valid*/ 1,
					      /*bit*/ bit_ptr);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}

		mtx_lock(&lun->lun_lock);

		/*
		 * The initiator wants to clear the
		 * key/unregister.
		 */
		if (sa_res_key == 0) {
			if ((res_key == 0
			  && (cdb->action & SPRO_ACTION_MASK) == SPRO_REGISTER)
			 || ((cdb->action & SPRO_ACTION_MASK) == SPRO_REG_IGNO
			  && ctl_get_prkey(lun, residx) == 0)) {
				mtx_unlock(&lun->lun_lock);
				goto done;
			}

			ctl_clr_prkey(lun, residx);
			lun->pr_key_count--;

			if (residx == lun->pr_res_idx) {
				lun->flags &= ~CTL_LUN_PR_RESERVED;
				lun->pr_res_idx = CTL_PR_NO_RESERVATION;

				if ((lun->pr_res_type == SPR_TYPE_WR_EX_RO ||
				     lun->pr_res_type == SPR_TYPE_EX_AC_RO) &&
				    lun->pr_key_count) {
					/*
					 * If the reservation is a registrants
					 * only type we need to generate a UA
					 * for other registered inits.  The
					 * sense code should be RESERVATIONS
					 * RELEASED
					 */

					for (i = softc->init_min; i < softc->init_max; i++){
						if (ctl_get_prkey(lun, i) == 0)
							continue;
						ctl_est_ua(lun, i,
						    CTL_UA_RES_RELEASE);
					}
				}
				lun->pr_res_type = 0;
			} else if (lun->pr_res_idx == CTL_PR_ALL_REGISTRANTS) {
				if (lun->pr_key_count==0) {
					lun->flags &= ~CTL_LUN_PR_RESERVED;
					lun->pr_res_type = 0;
					lun->pr_res_idx = CTL_PR_NO_RESERVATION;
				}
			}
			lun->pr_generation++;
			mtx_unlock(&lun->lun_lock);

			persis_io.hdr.nexus = ctsio->io_hdr.nexus;
			persis_io.hdr.msg_type = CTL_MSG_PERS_ACTION;
			persis_io.pr.pr_info.action = CTL_PR_UNREG_KEY;
			persis_io.pr.pr_info.residx = residx;
			ctl_ha_msg_send(CTL_HA_CHAN_CTL, &persis_io,
			    sizeof(persis_io.pr), M_WAITOK);
		} else /* sa_res_key != 0 */ {

			/*
			 * If we aren't registered currently then increment
			 * the key count and set the registered flag.
			 */
			ctl_alloc_prkey(lun, residx);
			if (ctl_get_prkey(lun, residx) == 0)
				lun->pr_key_count++;
			ctl_set_prkey(lun, residx, sa_res_key);
			lun->pr_generation++;
			mtx_unlock(&lun->lun_lock);

			persis_io.hdr.nexus = ctsio->io_hdr.nexus;
			persis_io.hdr.msg_type = CTL_MSG_PERS_ACTION;
			persis_io.pr.pr_info.action = CTL_PR_REG_KEY;
			persis_io.pr.pr_info.residx = residx;
			memcpy(persis_io.pr.pr_info.sa_res_key,
			       param->serv_act_res_key,
			       sizeof(param->serv_act_res_key));
			ctl_ha_msg_send(CTL_HA_CHAN_CTL, &persis_io,
			    sizeof(persis_io.pr), M_WAITOK);
		}

		break;
	}
	case SPRO_RESERVE:
		mtx_lock(&lun->lun_lock);
		if (lun->flags & CTL_LUN_PR_RESERVED) {
			/*
			 * if this isn't the reservation holder and it's
			 * not a "all registrants" type or if the type is
			 * different then we have a conflict
			 */
			if ((lun->pr_res_idx != residx
			  && lun->pr_res_idx != CTL_PR_ALL_REGISTRANTS)
			 || lun->pr_res_type != type) {
				mtx_unlock(&lun->lun_lock);
				free(ctsio->kern_data_ptr, M_CTL);
				ctl_set_reservation_conflict(ctsio);
				ctl_done((union ctl_io *)ctsio);
				return (CTL_RETVAL_COMPLETE);
			}
			mtx_unlock(&lun->lun_lock);
		} else /* create a reservation */ {
			/*
			 * If it's not an "all registrants" type record
			 * reservation holder
			 */
			if (type != SPR_TYPE_WR_EX_AR
			 && type != SPR_TYPE_EX_AC_AR)
				lun->pr_res_idx = residx; /* Res holder */
			else
				lun->pr_res_idx = CTL_PR_ALL_REGISTRANTS;

			lun->flags |= CTL_LUN_PR_RESERVED;
			lun->pr_res_type = type;

			mtx_unlock(&lun->lun_lock);

			/* send msg to other side */
			persis_io.hdr.nexus = ctsio->io_hdr.nexus;
			persis_io.hdr.msg_type = CTL_MSG_PERS_ACTION;
			persis_io.pr.pr_info.action = CTL_PR_RESERVE;
			persis_io.pr.pr_info.residx = lun->pr_res_idx;
			persis_io.pr.pr_info.res_type = type;
			ctl_ha_msg_send(CTL_HA_CHAN_CTL, &persis_io,
			    sizeof(persis_io.pr), M_WAITOK);
		}
		break;

	case SPRO_RELEASE:
		mtx_lock(&lun->lun_lock);
		if ((lun->flags & CTL_LUN_PR_RESERVED) == 0) {
			/* No reservation exists return good status */
			mtx_unlock(&lun->lun_lock);
			goto done;
		}
		/*
		 * Is this nexus a reservation holder?
		 */
		if (lun->pr_res_idx != residx
		 && lun->pr_res_idx != CTL_PR_ALL_REGISTRANTS) {
			/*
			 * not a res holder return good status but
			 * do nothing
			 */
			mtx_unlock(&lun->lun_lock);
			goto done;
		}

		if (lun->pr_res_type != type) {
			mtx_unlock(&lun->lun_lock);
			free(ctsio->kern_data_ptr, M_CTL);
			ctl_set_illegal_pr_release(ctsio);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}

		/* okay to release */
		lun->flags &= ~CTL_LUN_PR_RESERVED;
		lun->pr_res_idx = CTL_PR_NO_RESERVATION;
		lun->pr_res_type = 0;

		/*
		 * If this isn't an exclusive access reservation and NUAR
		 * is not set, generate UA for all other registrants.
		 */
		if (type != SPR_TYPE_EX_AC && type != SPR_TYPE_WR_EX &&
		    (lun->MODE_CTRL.queue_flags & SCP_NUAR) == 0) {
			for (i = softc->init_min; i < softc->init_max; i++) {
				if (i == residx || ctl_get_prkey(lun, i) == 0)
					continue;
				ctl_est_ua(lun, i, CTL_UA_RES_RELEASE);
			}
		}
		mtx_unlock(&lun->lun_lock);

		/* Send msg to other side */
		persis_io.hdr.nexus = ctsio->io_hdr.nexus;
		persis_io.hdr.msg_type = CTL_MSG_PERS_ACTION;
		persis_io.pr.pr_info.action = CTL_PR_RELEASE;
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &persis_io,
		     sizeof(persis_io.pr), M_WAITOK);
		break;

	case SPRO_CLEAR:
		/* send msg to other side */

		mtx_lock(&lun->lun_lock);
		lun->flags &= ~CTL_LUN_PR_RESERVED;
		lun->pr_res_type = 0;
		lun->pr_key_count = 0;
		lun->pr_res_idx = CTL_PR_NO_RESERVATION;

		ctl_clr_prkey(lun, residx);
		for (i = 0; i < CTL_MAX_INITIATORS; i++)
			if (ctl_get_prkey(lun, i) != 0) {
				ctl_clr_prkey(lun, i);
				ctl_est_ua(lun, i, CTL_UA_REG_PREEMPT);
			}
		lun->pr_generation++;
		mtx_unlock(&lun->lun_lock);

		persis_io.hdr.nexus = ctsio->io_hdr.nexus;
		persis_io.hdr.msg_type = CTL_MSG_PERS_ACTION;
		persis_io.pr.pr_info.action = CTL_PR_CLEAR;
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &persis_io,
		     sizeof(persis_io.pr), M_WAITOK);
		break;

	case SPRO_PREEMPT:
	case SPRO_PRE_ABO: {
		int nretval;

		nretval = ctl_pro_preempt(softc, lun, res_key, sa_res_key, type,
					  residx, ctsio, cdb, param);
		if (nretval != 0)
			return (CTL_RETVAL_COMPLETE);
		break;
	}
	default:
		panic("%s: Invalid PR type %#x", __func__, cdb->action);
	}

done:
	free(ctsio->kern_data_ptr, M_CTL);
	ctl_set_success(ctsio);
	ctl_done((union ctl_io *)ctsio);

	return (retval);
}

/*
 * This routine is for handling a message from the other SC pertaining to
 * persistent reserve out. All the error checking will have been done
 * so only perorming the action need be done here to keep the two
 * in sync.
 */
static void
ctl_hndl_per_res_out_on_other_sc(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	union ctl_ha_msg *msg = (union ctl_ha_msg *)&io->presio.pr_msg;
	struct ctl_lun *lun;
	int i;
	uint32_t residx, targ_lun;

	targ_lun = msg->hdr.nexus.targ_mapped_lun;
	mtx_lock(&softc->ctl_lock);
	if (targ_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		return;
	}
	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);
	if (lun->flags & CTL_LUN_DISABLED) {
		mtx_unlock(&lun->lun_lock);
		return;
	}
	residx = ctl_get_initindex(&msg->hdr.nexus);
	switch(msg->pr.pr_info.action) {
	case CTL_PR_REG_KEY:
		ctl_alloc_prkey(lun, msg->pr.pr_info.residx);
		if (ctl_get_prkey(lun, msg->pr.pr_info.residx) == 0)
			lun->pr_key_count++;
		ctl_set_prkey(lun, msg->pr.pr_info.residx,
		    scsi_8btou64(msg->pr.pr_info.sa_res_key));
		lun->pr_generation++;
		break;

	case CTL_PR_UNREG_KEY:
		ctl_clr_prkey(lun, msg->pr.pr_info.residx);
		lun->pr_key_count--;

		/* XXX Need to see if the reservation has been released */
		/* if so do we need to generate UA? */
		if (msg->pr.pr_info.residx == lun->pr_res_idx) {
			lun->flags &= ~CTL_LUN_PR_RESERVED;
			lun->pr_res_idx = CTL_PR_NO_RESERVATION;

			if ((lun->pr_res_type == SPR_TYPE_WR_EX_RO ||
			     lun->pr_res_type == SPR_TYPE_EX_AC_RO) &&
			    lun->pr_key_count) {
				/*
				 * If the reservation is a registrants
				 * only type we need to generate a UA
				 * for other registered inits.  The
				 * sense code should be RESERVATIONS
				 * RELEASED
				 */

				for (i = softc->init_min; i < softc->init_max; i++) {
					if (ctl_get_prkey(lun, i) == 0)
						continue;

					ctl_est_ua(lun, i, CTL_UA_RES_RELEASE);
				}
			}
			lun->pr_res_type = 0;
		} else if (lun->pr_res_idx == CTL_PR_ALL_REGISTRANTS) {
			if (lun->pr_key_count==0) {
				lun->flags &= ~CTL_LUN_PR_RESERVED;
				lun->pr_res_type = 0;
				lun->pr_res_idx = CTL_PR_NO_RESERVATION;
			}
		}
		lun->pr_generation++;
		break;

	case CTL_PR_RESERVE:
		lun->flags |= CTL_LUN_PR_RESERVED;
		lun->pr_res_type = msg->pr.pr_info.res_type;
		lun->pr_res_idx = msg->pr.pr_info.residx;

		break;

	case CTL_PR_RELEASE:
		/*
		 * If this isn't an exclusive access reservation and NUAR
		 * is not set, generate UA for all other registrants.
		 */
		if (lun->pr_res_type != SPR_TYPE_EX_AC &&
		    lun->pr_res_type != SPR_TYPE_WR_EX &&
		    (lun->MODE_CTRL.queue_flags & SCP_NUAR) == 0) {
			for (i = softc->init_min; i < softc->init_max; i++) {
				if (i == residx || ctl_get_prkey(lun, i) == 0)
					continue;
				ctl_est_ua(lun, i, CTL_UA_RES_RELEASE);
			}
		}

		lun->flags &= ~CTL_LUN_PR_RESERVED;
		lun->pr_res_idx = CTL_PR_NO_RESERVATION;
		lun->pr_res_type = 0;
		break;

	case CTL_PR_PREEMPT:
		ctl_pro_preempt_other(lun, msg);
		break;
	case CTL_PR_CLEAR:
		lun->flags &= ~CTL_LUN_PR_RESERVED;
		lun->pr_res_type = 0;
		lun->pr_key_count = 0;
		lun->pr_res_idx = CTL_PR_NO_RESERVATION;

		for (i=0; i < CTL_MAX_INITIATORS; i++) {
			if (ctl_get_prkey(lun, i) == 0)
				continue;
			ctl_clr_prkey(lun, i);
			ctl_est_ua(lun, i, CTL_UA_REG_PREEMPT);
		}
		lun->pr_generation++;
		break;
	}

	mtx_unlock(&lun->lun_lock);
}

int
ctl_read_write(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct ctl_lba_len_flags *lbalen;
	uint64_t lba;
	uint32_t num_blocks;
	int flags, retval;
	int isread;

	CTL_DEBUG_PRINT(("ctl_read_write: command: %#x\n", ctsio->cdb[0]));

	flags = 0;
	isread = ctsio->cdb[0] == READ_6  || ctsio->cdb[0] == READ_10
	      || ctsio->cdb[0] == READ_12 || ctsio->cdb[0] == READ_16;
	switch (ctsio->cdb[0]) {
	case READ_6:
	case WRITE_6: {
		struct scsi_rw_6 *cdb;

		cdb = (struct scsi_rw_6 *)ctsio->cdb;

		lba = scsi_3btoul(cdb->addr);
		/* only 5 bits are valid in the most significant address byte */
		lba &= 0x1fffff;
		num_blocks = cdb->length;
		/*
		 * This is correct according to SBC-2.
		 */
		if (num_blocks == 0)
			num_blocks = 256;
		break;
	}
	case READ_10:
	case WRITE_10: {
		struct scsi_rw_10 *cdb;

		cdb = (struct scsi_rw_10 *)ctsio->cdb;
		if (cdb->byte2 & SRW10_FUA)
			flags |= CTL_LLF_FUA;
		if (cdb->byte2 & SRW10_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_4btoul(cdb->addr);
		num_blocks = scsi_2btoul(cdb->length);
		break;
	}
	case WRITE_VERIFY_10: {
		struct scsi_write_verify_10 *cdb;

		cdb = (struct scsi_write_verify_10 *)ctsio->cdb;
		flags |= CTL_LLF_FUA;
		if (cdb->byte2 & SWV_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_4btoul(cdb->addr);
		num_blocks = scsi_2btoul(cdb->length);
		break;
	}
	case READ_12:
	case WRITE_12: {
		struct scsi_rw_12 *cdb;

		cdb = (struct scsi_rw_12 *)ctsio->cdb;
		if (cdb->byte2 & SRW12_FUA)
			flags |= CTL_LLF_FUA;
		if (cdb->byte2 & SRW12_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_4btoul(cdb->addr);
		num_blocks = scsi_4btoul(cdb->length);
		break;
	}
	case WRITE_VERIFY_12: {
		struct scsi_write_verify_12 *cdb;

		cdb = (struct scsi_write_verify_12 *)ctsio->cdb;
		flags |= CTL_LLF_FUA;
		if (cdb->byte2 & SWV_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_4btoul(cdb->addr);
		num_blocks = scsi_4btoul(cdb->length);
		break;
	}
	case READ_16:
	case WRITE_16: {
		struct scsi_rw_16 *cdb;

		cdb = (struct scsi_rw_16 *)ctsio->cdb;
		if (cdb->byte2 & SRW12_FUA)
			flags |= CTL_LLF_FUA;
		if (cdb->byte2 & SRW12_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_8btou64(cdb->addr);
		num_blocks = scsi_4btoul(cdb->length);
		break;
	}
	case WRITE_ATOMIC_16: {
		struct scsi_write_atomic_16 *cdb;

		if (lun->be_lun->atomicblock == 0) {
			ctl_set_invalid_opcode(ctsio);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}

		cdb = (struct scsi_write_atomic_16 *)ctsio->cdb;
		if (cdb->byte2 & SRW12_FUA)
			flags |= CTL_LLF_FUA;
		if (cdb->byte2 & SRW12_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_8btou64(cdb->addr);
		num_blocks = scsi_2btoul(cdb->length);
		if (num_blocks > lun->be_lun->atomicblock) {
			ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
			    /*command*/ 1, /*field*/ 12, /*bit_valid*/ 0,
			    /*bit*/ 0);
			ctl_done((union ctl_io *)ctsio);
			return (CTL_RETVAL_COMPLETE);
		}
		break;
	}
	case WRITE_VERIFY_16: {
		struct scsi_write_verify_16 *cdb;

		cdb = (struct scsi_write_verify_16 *)ctsio->cdb;
		flags |= CTL_LLF_FUA;
		if (cdb->byte2 & SWV_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_8btou64(cdb->addr);
		num_blocks = scsi_4btoul(cdb->length);
		break;
	}
	default:
		/*
		 * We got a command we don't support.  This shouldn't
		 * happen, commands should be filtered out above us.
		 */
		ctl_set_invalid_opcode(ctsio);
		ctl_done((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
		break; /* NOTREACHED */
	}

	/*
	 * The first check is to make sure we're in bounds, the second
	 * check is to catch wrap-around problems.  If the lba + num blocks
	 * is less than the lba, then we've wrapped around and the block
	 * range is invalid anyway.
	 */
	if (((lba + num_blocks) > (lun->be_lun->maxlba + 1))
	 || ((lba + num_blocks) < lba)) {
		ctl_set_lba_out_of_range(ctsio,
		    MAX(lba, lun->be_lun->maxlba + 1));
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * According to SBC-3, a transfer length of 0 is not an error.
	 * Note that this cannot happen with WRITE(6) or READ(6), since 0
	 * translates to 256 blocks for those commands.
	 */
	if (num_blocks == 0) {
		ctl_set_success(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/* Set FUA and/or DPO if caches are disabled. */
	if (isread) {
		if ((lun->MODE_CACHING.flags1 & SCP_RCD) != 0)
			flags |= CTL_LLF_FUA | CTL_LLF_DPO;
	} else {
		if ((lun->MODE_CACHING.flags1 & SCP_WCE) == 0)
			flags |= CTL_LLF_FUA;
	}

	lbalen = (struct ctl_lba_len_flags *)
	    &ctsio->io_hdr.ctl_private[CTL_PRIV_LBA_LEN];
	lbalen->lba = lba;
	lbalen->len = num_blocks;
	lbalen->flags = (isread ? CTL_LLF_READ : CTL_LLF_WRITE) | flags;

	ctsio->kern_total_len = num_blocks * lun->be_lun->blocksize;
	ctsio->kern_rel_offset = 0;

	CTL_DEBUG_PRINT(("ctl_read_write: calling data_submit()\n"));

	retval = lun->backend->data_submit((union ctl_io *)ctsio);
	return (retval);
}

static int
ctl_cnw_cont(union ctl_io *io)
{
	struct ctl_lun *lun = CTL_LUN(io);
	struct ctl_scsiio *ctsio;
	struct ctl_lba_len_flags *lbalen;
	int retval;

	ctsio = &io->scsiio;
	ctsio->io_hdr.status = CTL_STATUS_NONE;
	ctsio->io_hdr.flags &= ~CTL_FLAG_IO_CONT;
	lbalen = (struct ctl_lba_len_flags *)
	    &ctsio->io_hdr.ctl_private[CTL_PRIV_LBA_LEN];
	lbalen->flags &= ~CTL_LLF_COMPARE;
	lbalen->flags |= CTL_LLF_WRITE;

	CTL_DEBUG_PRINT(("ctl_cnw_cont: calling data_submit()\n"));
	retval = lun->backend->data_submit((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_cnw(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct ctl_lba_len_flags *lbalen;
	uint64_t lba;
	uint32_t num_blocks;
	int flags, retval;

	CTL_DEBUG_PRINT(("ctl_cnw: command: %#x\n", ctsio->cdb[0]));

	flags = 0;
	switch (ctsio->cdb[0]) {
	case COMPARE_AND_WRITE: {
		struct scsi_compare_and_write *cdb;

		cdb = (struct scsi_compare_and_write *)ctsio->cdb;
		if (cdb->byte2 & SRW10_FUA)
			flags |= CTL_LLF_FUA;
		if (cdb->byte2 & SRW10_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_8btou64(cdb->addr);
		num_blocks = cdb->length;
		break;
	}
	default:
		/*
		 * We got a command we don't support.  This shouldn't
		 * happen, commands should be filtered out above us.
		 */
		ctl_set_invalid_opcode(ctsio);
		ctl_done((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
		break; /* NOTREACHED */
	}

	/*
	 * The first check is to make sure we're in bounds, the second
	 * check is to catch wrap-around problems.  If the lba + num blocks
	 * is less than the lba, then we've wrapped around and the block
	 * range is invalid anyway.
	 */
	if (((lba + num_blocks) > (lun->be_lun->maxlba + 1))
	 || ((lba + num_blocks) < lba)) {
		ctl_set_lba_out_of_range(ctsio,
		    MAX(lba, lun->be_lun->maxlba + 1));
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * According to SBC-3, a transfer length of 0 is not an error.
	 */
	if (num_blocks == 0) {
		ctl_set_success(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/* Set FUA if write cache is disabled. */
	if ((lun->MODE_CACHING.flags1 & SCP_WCE) == 0)
		flags |= CTL_LLF_FUA;

	ctsio->kern_total_len = 2 * num_blocks * lun->be_lun->blocksize;
	ctsio->kern_rel_offset = 0;

	/*
	 * Set the IO_CONT flag, so that if this I/O gets passed to
	 * ctl_data_submit_done(), it'll get passed back to
	 * ctl_ctl_cnw_cont() for further processing.
	 */
	ctsio->io_hdr.flags |= CTL_FLAG_IO_CONT;
	ctsio->io_cont = ctl_cnw_cont;

	lbalen = (struct ctl_lba_len_flags *)
	    &ctsio->io_hdr.ctl_private[CTL_PRIV_LBA_LEN];
	lbalen->lba = lba;
	lbalen->len = num_blocks;
	lbalen->flags = CTL_LLF_COMPARE | flags;

	CTL_DEBUG_PRINT(("ctl_cnw: calling data_submit()\n"));
	retval = lun->backend->data_submit((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_verify(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct ctl_lba_len_flags *lbalen;
	uint64_t lba;
	uint32_t num_blocks;
	int bytchk, flags;
	int retval;

	CTL_DEBUG_PRINT(("ctl_verify: command: %#x\n", ctsio->cdb[0]));

	bytchk = 0;
	flags = CTL_LLF_FUA;
	switch (ctsio->cdb[0]) {
	case VERIFY_10: {
		struct scsi_verify_10 *cdb;

		cdb = (struct scsi_verify_10 *)ctsio->cdb;
		if (cdb->byte2 & SVFY_BYTCHK)
			bytchk = 1;
		if (cdb->byte2 & SVFY_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_4btoul(cdb->addr);
		num_blocks = scsi_2btoul(cdb->length);
		break;
	}
	case VERIFY_12: {
		struct scsi_verify_12 *cdb;

		cdb = (struct scsi_verify_12 *)ctsio->cdb;
		if (cdb->byte2 & SVFY_BYTCHK)
			bytchk = 1;
		if (cdb->byte2 & SVFY_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_4btoul(cdb->addr);
		num_blocks = scsi_4btoul(cdb->length);
		break;
	}
	case VERIFY_16: {
		struct scsi_rw_16 *cdb;

		cdb = (struct scsi_rw_16 *)ctsio->cdb;
		if (cdb->byte2 & SVFY_BYTCHK)
			bytchk = 1;
		if (cdb->byte2 & SVFY_DPO)
			flags |= CTL_LLF_DPO;
		lba = scsi_8btou64(cdb->addr);
		num_blocks = scsi_4btoul(cdb->length);
		break;
	}
	default:
		/*
		 * We got a command we don't support.  This shouldn't
		 * happen, commands should be filtered out above us.
		 */
		ctl_set_invalid_opcode(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * The first check is to make sure we're in bounds, the second
	 * check is to catch wrap-around problems.  If the lba + num blocks
	 * is less than the lba, then we've wrapped around and the block
	 * range is invalid anyway.
	 */
	if (((lba + num_blocks) > (lun->be_lun->maxlba + 1))
	 || ((lba + num_blocks) < lba)) {
		ctl_set_lba_out_of_range(ctsio,
		    MAX(lba, lun->be_lun->maxlba + 1));
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	/*
	 * According to SBC-3, a transfer length of 0 is not an error.
	 */
	if (num_blocks == 0) {
		ctl_set_success(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	lbalen = (struct ctl_lba_len_flags *)
	    &ctsio->io_hdr.ctl_private[CTL_PRIV_LBA_LEN];
	lbalen->lba = lba;
	lbalen->len = num_blocks;
	if (bytchk) {
		lbalen->flags = CTL_LLF_COMPARE | flags;
		ctsio->kern_total_len = num_blocks * lun->be_lun->blocksize;
	} else {
		lbalen->flags = CTL_LLF_VERIFY | flags;
		ctsio->kern_total_len = 0;
	}
	ctsio->kern_rel_offset = 0;

	CTL_DEBUG_PRINT(("ctl_verify: calling data_submit()\n"));
	retval = lun->backend->data_submit((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_report_luns(struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_port *port = CTL_PORT(ctsio);
	struct ctl_lun *lun, *request_lun = CTL_LUN(ctsio);
	struct scsi_report_luns *cdb;
	struct scsi_report_luns_data *lun_data;
	int num_filled, num_luns, num_port_luns, retval;
	uint32_t alloc_len, lun_datalen;
	uint32_t initidx, targ_lun_id, lun_id;

	retval = CTL_RETVAL_COMPLETE;
	cdb = (struct scsi_report_luns *)ctsio->cdb;

	CTL_DEBUG_PRINT(("ctl_report_luns\n"));

	num_luns = 0;
	num_port_luns = port->lun_map ? port->lun_map_size : ctl_max_luns;
	mtx_lock(&softc->ctl_lock);
	for (targ_lun_id = 0; targ_lun_id < num_port_luns; targ_lun_id++) {
		if (ctl_lun_map_from_port(port, targ_lun_id) != UINT32_MAX)
			num_luns++;
	}
	mtx_unlock(&softc->ctl_lock);

	switch (cdb->select_report) {
	case RPL_REPORT_DEFAULT:
	case RPL_REPORT_ALL:
	case RPL_REPORT_NONSUBSID:
		break;
	case RPL_REPORT_WELLKNOWN:
	case RPL_REPORT_ADMIN:
	case RPL_REPORT_CONGLOM:
		num_luns = 0;
		break;
	default:
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 2,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (retval);
		break; /* NOTREACHED */
	}

	alloc_len = scsi_4btoul(cdb->length);
	/*
	 * The initiator has to allocate at least 16 bytes for this request,
	 * so he can at least get the header and the first LUN.  Otherwise
	 * we reject the request (per SPC-3 rev 14, section 6.21).
	 */
	if (alloc_len < (sizeof(struct scsi_report_luns_data) +
	    sizeof(struct scsi_report_luns_lundata))) {
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 6,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (retval);
	}

	lun_datalen = sizeof(*lun_data) +
		(num_luns * sizeof(struct scsi_report_luns_lundata));

	ctsio->kern_data_ptr = malloc(lun_datalen, M_CTL, M_WAITOK | M_ZERO);
	lun_data = (struct scsi_report_luns_data *)ctsio->kern_data_ptr;
	ctsio->kern_sg_entries = 0;

	initidx = ctl_get_initindex(&ctsio->io_hdr.nexus);

	mtx_lock(&softc->ctl_lock);
	for (targ_lun_id = 0, num_filled = 0;
	    targ_lun_id < num_port_luns && num_filled < num_luns;
	    targ_lun_id++) {
		lun_id = ctl_lun_map_from_port(port, targ_lun_id);
		if (lun_id == UINT32_MAX)
			continue;
		lun = softc->ctl_luns[lun_id];
		if (lun == NULL)
			continue;

		be64enc(lun_data->luns[num_filled++].lundata,
		    ctl_encode_lun(targ_lun_id));

		/*
		 * According to SPC-3, rev 14 section 6.21:
		 *
		 * "The execution of a REPORT LUNS command to any valid and
		 * installed logical unit shall clear the REPORTED LUNS DATA
		 * HAS CHANGED unit attention condition for all logical
		 * units of that target with respect to the requesting
		 * initiator. A valid and installed logical unit is one
		 * having a PERIPHERAL QUALIFIER of 000b in the standard
		 * INQUIRY data (see 6.4.2)."
		 *
		 * If request_lun is NULL, the LUN this report luns command
		 * was issued to is either disabled or doesn't exist. In that
		 * case, we shouldn't clear any pending lun change unit
		 * attention.
		 */
		if (request_lun != NULL) {
			mtx_lock(&lun->lun_lock);
			ctl_clr_ua(lun, initidx, CTL_UA_LUN_CHANGE);
			mtx_unlock(&lun->lun_lock);
		}
	}
	mtx_unlock(&softc->ctl_lock);

	/*
	 * It's quite possible that we've returned fewer LUNs than we allocated
	 * space for.  Trim it.
	 */
	lun_datalen = sizeof(*lun_data) +
		(num_filled * sizeof(struct scsi_report_luns_lundata));
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(lun_datalen, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * We set this to the actual data length, regardless of how much
	 * space we actually have to return results.  If the user looks at
	 * this value, he'll know whether or not he allocated enough space
	 * and reissue the command if necessary.  We don't support well
	 * known logical units, so if the user asks for that, return none.
	 */
	scsi_ulto4b(lun_datalen - 8, lun_data->length);

	/*
	 * We can only return SCSI_STATUS_CHECK_COND when we can't satisfy
	 * this request.
	 */
	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_request_sense(struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_request_sense *cdb;
	struct scsi_sense_data *sense_ptr, *ps;
	uint32_t initidx;
	int have_error;
	u_int sense_len = SSD_FULL_SIZE;
	scsi_sense_data_type sense_format;
	ctl_ua_type ua_type;
	uint8_t asc = 0, ascq = 0;

	cdb = (struct scsi_request_sense *)ctsio->cdb;

	CTL_DEBUG_PRINT(("ctl_request_sense\n"));

	/*
	 * Determine which sense format the user wants.
	 */
	if (cdb->byte2 & SRS_DESC)
		sense_format = SSD_TYPE_DESC;
	else
		sense_format = SSD_TYPE_FIXED;

	ctsio->kern_data_ptr = malloc(sizeof(*sense_ptr), M_CTL, M_WAITOK);
	sense_ptr = (struct scsi_sense_data *)ctsio->kern_data_ptr;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;

	/*
	 * struct scsi_sense_data, which is currently set to 256 bytes, is
	 * larger than the largest allowed value for the length field in the
	 * REQUEST SENSE CDB, which is 252 bytes as of SPC-4.
	 */
	ctsio->kern_data_len = cdb->length;
	ctsio->kern_total_len = cdb->length;

	/*
	 * If we don't have a LUN, we don't have any pending sense.
	 */
	if (lun == NULL ||
	    ((lun->flags & CTL_LUN_PRIMARY_SC) == 0 &&
	     softc->ha_link < CTL_HA_LINK_UNKNOWN)) {
		/* "Logical unit not supported" */
		ctl_set_sense_data(sense_ptr, &sense_len, NULL, sense_format,
		    /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		    /*asc*/ 0x25,
		    /*ascq*/ 0x00,
		    SSD_ELEM_NONE);
		goto send;
	}

	have_error = 0;
	initidx = ctl_get_initindex(&ctsio->io_hdr.nexus);
	/*
	 * Check for pending sense, and then for pending unit attentions.
	 * Pending sense gets returned first, then pending unit attentions.
	 */
	mtx_lock(&lun->lun_lock);
	ps = lun->pending_sense[initidx / CTL_MAX_INIT_PER_PORT];
	if (ps != NULL)
		ps += initidx % CTL_MAX_INIT_PER_PORT;
	if (ps != NULL && ps->error_code != 0) {
		scsi_sense_data_type stored_format;

		/*
		 * Check to see which sense format was used for the stored
		 * sense data.
		 */
		stored_format = scsi_sense_type(ps);

		/*
		 * If the user requested a different sense format than the
		 * one we stored, then we need to convert it to the other
		 * format.  If we're going from descriptor to fixed format
		 * sense data, we may lose things in translation, depending
		 * on what options were used.
		 *
		 * If the stored format is SSD_TYPE_NONE (i.e. invalid),
		 * for some reason we'll just copy it out as-is.
		 */
		if ((stored_format == SSD_TYPE_FIXED)
		 && (sense_format == SSD_TYPE_DESC))
			ctl_sense_to_desc((struct scsi_sense_data_fixed *)
			    ps, (struct scsi_sense_data_desc *)sense_ptr);
		else if ((stored_format == SSD_TYPE_DESC)
		      && (sense_format == SSD_TYPE_FIXED))
			ctl_sense_to_fixed((struct scsi_sense_data_desc *)
			    ps, (struct scsi_sense_data_fixed *)sense_ptr);
		else
			memcpy(sense_ptr, ps, sizeof(*sense_ptr));

		ps->error_code = 0;
		have_error = 1;
	} else {
		ua_type = ctl_build_ua(lun, initidx, sense_ptr, &sense_len,
		    sense_format);
		if (ua_type != CTL_UA_NONE)
			have_error = 1;
	}
	if (have_error == 0) {
		/*
		 * Report informational exception if have one and allowed.
		 */
		if (lun->MODE_IE.mrie != SIEP_MRIE_NO) {
			asc = lun->ie_asc;
			ascq = lun->ie_ascq;
		}
		ctl_set_sense_data(sense_ptr, &sense_len, lun, sense_format,
		    /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_NO_SENSE,
		    /*asc*/ asc,
		    /*ascq*/ ascq,
		    SSD_ELEM_NONE);
	}
	mtx_unlock(&lun->lun_lock);

send:
	/*
	 * We report the SCSI status as OK, since the status of the command
	 * itself is OK.  We're reporting sense as parameter data.
	 */
	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_tur(struct ctl_scsiio *ctsio)
{

	CTL_DEBUG_PRINT(("ctl_tur\n"));

	ctl_set_success(ctsio);
	ctl_done((union ctl_io *)ctsio);

	return (CTL_RETVAL_COMPLETE);
}

/*
 * SCSI VPD page 0x00, the Supported VPD Pages page.
 */
static int
ctl_inquiry_evpd_supported(struct ctl_scsiio *ctsio, int alloc_len)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_vpd_supported_pages *pages;
	int sup_page_size;
	int p;

	sup_page_size = sizeof(struct scsi_vpd_supported_pages) *
	    SCSI_EVPD_NUM_SUPPORTED_PAGES;
	ctsio->kern_data_ptr = malloc(sup_page_size, M_CTL, M_WAITOK | M_ZERO);
	pages = (struct scsi_vpd_supported_pages *)ctsio->kern_data_ptr;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(sup_page_size, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * The control device is always connected.  The disk device, on the
	 * other hand, may not be online all the time.  Need to change this
	 * to figure out whether the disk device is actually online or not.
	 */
	if (lun != NULL)
		pages->device = (SID_QUAL_LU_CONNECTED << 5) |
				lun->be_lun->lun_type;
	else
		pages->device = (SID_QUAL_LU_OFFLINE << 5) | T_DIRECT;

	p = 0;
	/* Supported VPD pages */
	pages->page_list[p++] = SVPD_SUPPORTED_PAGES;
	/* Serial Number */
	pages->page_list[p++] = SVPD_UNIT_SERIAL_NUMBER;
	/* Device Identification */
	pages->page_list[p++] = SVPD_DEVICE_ID;
	/* Extended INQUIRY Data */
	pages->page_list[p++] = SVPD_EXTENDED_INQUIRY_DATA;
	/* Mode Page Policy */
	pages->page_list[p++] = SVPD_MODE_PAGE_POLICY;
	/* SCSI Ports */
	pages->page_list[p++] = SVPD_SCSI_PORTS;
	/* Third-party Copy */
	pages->page_list[p++] = SVPD_SCSI_TPC;
	if (lun != NULL && lun->be_lun->lun_type == T_DIRECT) {
		/* Block limits */
		pages->page_list[p++] = SVPD_BLOCK_LIMITS;
		/* Block Device Characteristics */
		pages->page_list[p++] = SVPD_BDC;
		/* Logical Block Provisioning */
		pages->page_list[p++] = SVPD_LBP;
	}
	pages->length = p;

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

/*
 * SCSI VPD page 0x80, the Unit Serial Number page.
 */
static int
ctl_inquiry_evpd_serial(struct ctl_scsiio *ctsio, int alloc_len)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_vpd_unit_serial_number *sn_ptr;
	int data_len;

	data_len = 4 + CTL_SN_LEN;
	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	sn_ptr = (struct scsi_vpd_unit_serial_number *)ctsio->kern_data_ptr;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * The control device is always connected.  The disk device, on the
	 * other hand, may not be online all the time.  Need to change this
	 * to figure out whether the disk device is actually online or not.
	 */
	if (lun != NULL)
		sn_ptr->device = (SID_QUAL_LU_CONNECTED << 5) |
				  lun->be_lun->lun_type;
	else
		sn_ptr->device = (SID_QUAL_LU_OFFLINE << 5) | T_DIRECT;

	sn_ptr->page_code = SVPD_UNIT_SERIAL_NUMBER;
	sn_ptr->length = CTL_SN_LEN;
	/*
	 * If we don't have a LUN, we just leave the serial number as
	 * all spaces.
	 */
	if (lun != NULL) {
		strncpy((char *)sn_ptr->serial_num,
			(char *)lun->be_lun->serial_num, CTL_SN_LEN);
	} else
		memset(sn_ptr->serial_num, 0x20, CTL_SN_LEN);

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}


/*
 * SCSI VPD page 0x86, the Extended INQUIRY Data page.
 */
static int
ctl_inquiry_evpd_eid(struct ctl_scsiio *ctsio, int alloc_len)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_vpd_extended_inquiry_data *eid_ptr;
	int data_len;

	data_len = sizeof(struct scsi_vpd_extended_inquiry_data);
	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	eid_ptr = (struct scsi_vpd_extended_inquiry_data *)ctsio->kern_data_ptr;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * The control device is always connected.  The disk device, on the
	 * other hand, may not be online all the time.
	 */
	if (lun != NULL)
		eid_ptr->device = (SID_QUAL_LU_CONNECTED << 5) |
				     lun->be_lun->lun_type;
	else
		eid_ptr->device = (SID_QUAL_LU_OFFLINE << 5) | T_DIRECT;
	eid_ptr->page_code = SVPD_EXTENDED_INQUIRY_DATA;
	scsi_ulto2b(data_len - 4, eid_ptr->page_length);
	/*
	 * We support head of queue, ordered and simple tags.
	 */
	eid_ptr->flags2 = SVPD_EID_HEADSUP | SVPD_EID_ORDSUP | SVPD_EID_SIMPSUP;
	/*
	 * Volatile cache supported.
	 */
	eid_ptr->flags3 = SVPD_EID_V_SUP;

	/*
	 * This means that we clear the REPORTED LUNS DATA HAS CHANGED unit
	 * attention for a particular IT nexus on all LUNs once we report
	 * it to that nexus once.  This bit is required as of SPC-4.
	 */
	eid_ptr->flags4 = SVPD_EID_LUICLR;

	/*
	 * We support revert to defaults (RTD) bit in MODE SELECT.
	 */
	eid_ptr->flags5 = SVPD_EID_RTD_SUP;

	/*
	 * XXX KDM in order to correctly answer this, we would need
	 * information from the SIM to determine how much sense data it
	 * can send.  So this would really be a path inquiry field, most
	 * likely.  This can be set to a maximum of 252 according to SPC-4,
	 * but the hardware may or may not be able to support that much.
	 * 0 just means that the maximum sense data length is not reported.
	 */
	eid_ptr->max_sense_length = 0;

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

static int
ctl_inquiry_evpd_mpp(struct ctl_scsiio *ctsio, int alloc_len)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_vpd_mode_page_policy *mpp_ptr;
	int data_len;

	data_len = sizeof(struct scsi_vpd_mode_page_policy) +
	    sizeof(struct scsi_vpd_mode_page_policy_descr);

	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	mpp_ptr = (struct scsi_vpd_mode_page_policy *)ctsio->kern_data_ptr;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * The control device is always connected.  The disk device, on the
	 * other hand, may not be online all the time.
	 */
	if (lun != NULL)
		mpp_ptr->device = (SID_QUAL_LU_CONNECTED << 5) |
				     lun->be_lun->lun_type;
	else
		mpp_ptr->device = (SID_QUAL_LU_OFFLINE << 5) | T_DIRECT;
	mpp_ptr->page_code = SVPD_MODE_PAGE_POLICY;
	scsi_ulto2b(data_len - 4, mpp_ptr->page_length);
	mpp_ptr->descr[0].page_code = 0x3f;
	mpp_ptr->descr[0].subpage_code = 0xff;
	mpp_ptr->descr[0].policy = SVPD_MPP_SHARED;

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

/*
 * SCSI VPD page 0x83, the Device Identification page.
 */
static int
ctl_inquiry_evpd_devid(struct ctl_scsiio *ctsio, int alloc_len)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_port *port = CTL_PORT(ctsio);
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_vpd_device_id *devid_ptr;
	struct scsi_vpd_id_descriptor *desc;
	int data_len, g;
	uint8_t proto;

	data_len = sizeof(struct scsi_vpd_device_id) +
	    sizeof(struct scsi_vpd_id_descriptor) +
		sizeof(struct scsi_vpd_id_rel_trgt_port_id) +
	    sizeof(struct scsi_vpd_id_descriptor) +
		sizeof(struct scsi_vpd_id_trgt_port_grp_id);
	if (lun && lun->lun_devid)
		data_len += lun->lun_devid->len;
	if (port && port->port_devid)
		data_len += port->port_devid->len;
	if (port && port->target_devid)
		data_len += port->target_devid->len;

	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	devid_ptr = (struct scsi_vpd_device_id *)ctsio->kern_data_ptr;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * The control device is always connected.  The disk device, on the
	 * other hand, may not be online all the time.
	 */
	if (lun != NULL)
		devid_ptr->device = (SID_QUAL_LU_CONNECTED << 5) |
				     lun->be_lun->lun_type;
	else
		devid_ptr->device = (SID_QUAL_LU_OFFLINE << 5) | T_DIRECT;
	devid_ptr->page_code = SVPD_DEVICE_ID;
	scsi_ulto2b(data_len - 4, devid_ptr->length);

	if (port && port->port_type == CTL_PORT_FC)
		proto = SCSI_PROTO_FC << 4;
	else if (port && port->port_type == CTL_PORT_SAS)
		proto = SCSI_PROTO_SAS << 4;
	else if (port && port->port_type == CTL_PORT_ISCSI)
		proto = SCSI_PROTO_ISCSI << 4;
	else
		proto = SCSI_PROTO_SPI << 4;
	desc = (struct scsi_vpd_id_descriptor *)devid_ptr->desc_list;

	/*
	 * We're using a LUN association here.  i.e., this device ID is a
	 * per-LUN identifier.
	 */
	if (lun && lun->lun_devid) {
		memcpy(desc, lun->lun_devid->data, lun->lun_devid->len);
		desc = (struct scsi_vpd_id_descriptor *)((uint8_t *)desc +
		    lun->lun_devid->len);
	}

	/*
	 * This is for the WWPN which is a port association.
	 */
	if (port && port->port_devid) {
		memcpy(desc, port->port_devid->data, port->port_devid->len);
		desc = (struct scsi_vpd_id_descriptor *)((uint8_t *)desc +
		    port->port_devid->len);
	}

	/*
	 * This is for the Relative Target Port(type 4h) identifier
	 */
	desc->proto_codeset = proto | SVPD_ID_CODESET_BINARY;
	desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_PORT |
	    SVPD_ID_TYPE_RELTARG;
	desc->length = 4;
	scsi_ulto2b(ctsio->io_hdr.nexus.targ_port, &desc->identifier[2]);
	desc = (struct scsi_vpd_id_descriptor *)(&desc->identifier[0] +
	    sizeof(struct scsi_vpd_id_rel_trgt_port_id));

	/*
	 * This is for the Target Port Group(type 5h) identifier
	 */
	desc->proto_codeset = proto | SVPD_ID_CODESET_BINARY;
	desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_PORT |
	    SVPD_ID_TYPE_TPORTGRP;
	desc->length = 4;
	if (softc->is_single ||
	    (port && port->status & CTL_PORT_STATUS_HA_SHARED))
		g = 1;
	else
		g = 2 + ctsio->io_hdr.nexus.targ_port / softc->port_cnt;
	scsi_ulto2b(g, &desc->identifier[2]);
	desc = (struct scsi_vpd_id_descriptor *)(&desc->identifier[0] +
	    sizeof(struct scsi_vpd_id_trgt_port_grp_id));

	/*
	 * This is for the Target identifier
	 */
	if (port && port->target_devid) {
		memcpy(desc, port->target_devid->data, port->target_devid->len);
	}

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

static int
ctl_inquiry_evpd_scsi_ports(struct ctl_scsiio *ctsio, int alloc_len)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_vpd_scsi_ports *sp;
	struct scsi_vpd_port_designation *pd;
	struct scsi_vpd_port_designation_cont *pdc;
	struct ctl_port *port;
	int data_len, num_target_ports, iid_len, id_len;

	num_target_ports = 0;
	iid_len = 0;
	id_len = 0;
	mtx_lock(&softc->ctl_lock);
	STAILQ_FOREACH(port, &softc->port_list, links) {
		if ((port->status & CTL_PORT_STATUS_ONLINE) == 0)
			continue;
		if (lun != NULL &&
		    ctl_lun_map_to_port(port, lun->lun) == UINT32_MAX)
			continue;
		num_target_ports++;
		if (port->init_devid)
			iid_len += port->init_devid->len;
		if (port->port_devid)
			id_len += port->port_devid->len;
	}
	mtx_unlock(&softc->ctl_lock);

	data_len = sizeof(struct scsi_vpd_scsi_ports) +
	    num_target_ports * (sizeof(struct scsi_vpd_port_designation) +
	     sizeof(struct scsi_vpd_port_designation_cont)) + iid_len + id_len;
	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	sp = (struct scsi_vpd_scsi_ports *)ctsio->kern_data_ptr;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * The control device is always connected.  The disk device, on the
	 * other hand, may not be online all the time.  Need to change this
	 * to figure out whether the disk device is actually online or not.
	 */
	if (lun != NULL)
		sp->device = (SID_QUAL_LU_CONNECTED << 5) |
				  lun->be_lun->lun_type;
	else
		sp->device = (SID_QUAL_LU_OFFLINE << 5) | T_DIRECT;

	sp->page_code = SVPD_SCSI_PORTS;
	scsi_ulto2b(data_len - sizeof(struct scsi_vpd_scsi_ports),
	    sp->page_length);
	pd = &sp->design[0];

	mtx_lock(&softc->ctl_lock);
	STAILQ_FOREACH(port, &softc->port_list, links) {
		if ((port->status & CTL_PORT_STATUS_ONLINE) == 0)
			continue;
		if (lun != NULL &&
		    ctl_lun_map_to_port(port, lun->lun) == UINT32_MAX)
			continue;
		scsi_ulto2b(port->targ_port, pd->relative_port_id);
		if (port->init_devid) {
			iid_len = port->init_devid->len;
			memcpy(pd->initiator_transportid,
			    port->init_devid->data, port->init_devid->len);
		} else
			iid_len = 0;
		scsi_ulto2b(iid_len, pd->initiator_transportid_length);
		pdc = (struct scsi_vpd_port_designation_cont *)
		    (&pd->initiator_transportid[iid_len]);
		if (port->port_devid) {
			id_len = port->port_devid->len;
			memcpy(pdc->target_port_descriptors,
			    port->port_devid->data, port->port_devid->len);
		} else
			id_len = 0;
		scsi_ulto2b(id_len, pdc->target_port_descriptors_length);
		pd = (struct scsi_vpd_port_designation *)
		    ((uint8_t *)pdc->target_port_descriptors + id_len);
	}
	mtx_unlock(&softc->ctl_lock);

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

static int
ctl_inquiry_evpd_block_limits(struct ctl_scsiio *ctsio, int alloc_len)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_vpd_block_limits *bl_ptr;
	const char *val;
	uint64_t ival;

	ctsio->kern_data_ptr = malloc(sizeof(*bl_ptr), M_CTL, M_WAITOK | M_ZERO);
	bl_ptr = (struct scsi_vpd_block_limits *)ctsio->kern_data_ptr;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(sizeof(*bl_ptr), alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * The control device is always connected.  The disk device, on the
	 * other hand, may not be online all the time.  Need to change this
	 * to figure out whether the disk device is actually online or not.
	 */
	if (lun != NULL)
		bl_ptr->device = (SID_QUAL_LU_CONNECTED << 5) |
				  lun->be_lun->lun_type;
	else
		bl_ptr->device = (SID_QUAL_LU_OFFLINE << 5) | T_DIRECT;

	bl_ptr->page_code = SVPD_BLOCK_LIMITS;
	scsi_ulto2b(sizeof(*bl_ptr) - 4, bl_ptr->page_length);
	bl_ptr->max_cmp_write_len = 0xff;
	scsi_ulto4b(0xffffffff, bl_ptr->max_txfer_len);
	if (lun != NULL) {
		scsi_ulto4b(lun->be_lun->opttxferlen, bl_ptr->opt_txfer_len);
		if (lun->be_lun->flags & CTL_LUN_FLAG_UNMAP) {
			ival = 0xffffffff;
			val = dnvlist_get_string(lun->be_lun->options,
			    "unmap_max_lba", NULL);
			if (val != NULL)
				ctl_expand_number(val, &ival);
			scsi_ulto4b(ival, bl_ptr->max_unmap_lba_cnt);
			ival = 0xffffffff;
			val = dnvlist_get_string(lun->be_lun->options,
			    "unmap_max_descr", NULL);
			if (val != NULL)
				ctl_expand_number(val, &ival);
			scsi_ulto4b(ival, bl_ptr->max_unmap_blk_cnt);
			if (lun->be_lun->ublockexp != 0) {
				scsi_ulto4b((1 << lun->be_lun->ublockexp),
				    bl_ptr->opt_unmap_grain);
				scsi_ulto4b(0x80000000 | lun->be_lun->ublockoff,
				    bl_ptr->unmap_grain_align);
			}
		}
		scsi_ulto4b(lun->be_lun->atomicblock,
		    bl_ptr->max_atomic_transfer_length);
		scsi_ulto4b(0, bl_ptr->atomic_alignment);
		scsi_ulto4b(0, bl_ptr->atomic_transfer_length_granularity);
		scsi_ulto4b(0, bl_ptr->max_atomic_transfer_length_with_atomic_boundary);
		scsi_ulto4b(0, bl_ptr->max_atomic_boundary_size);
		ival = UINT64_MAX;
		val = dnvlist_get_string(lun->be_lun->options,
		    "write_same_max_lba", NULL);
		if (val != NULL)
			ctl_expand_number(val, &ival);
		scsi_u64to8b(ival, bl_ptr->max_write_same_length);
	}

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

static int
ctl_inquiry_evpd_bdc(struct ctl_scsiio *ctsio, int alloc_len)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_vpd_block_device_characteristics *bdc_ptr;
	const char *value;
	u_int i;

	ctsio->kern_data_ptr = malloc(sizeof(*bdc_ptr), M_CTL, M_WAITOK | M_ZERO);
	bdc_ptr = (struct scsi_vpd_block_device_characteristics *)ctsio->kern_data_ptr;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(sizeof(*bdc_ptr), alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * The control device is always connected.  The disk device, on the
	 * other hand, may not be online all the time.  Need to change this
	 * to figure out whether the disk device is actually online or not.
	 */
	if (lun != NULL)
		bdc_ptr->device = (SID_QUAL_LU_CONNECTED << 5) |
				  lun->be_lun->lun_type;
	else
		bdc_ptr->device = (SID_QUAL_LU_OFFLINE << 5) | T_DIRECT;
	bdc_ptr->page_code = SVPD_BDC;
	scsi_ulto2b(sizeof(*bdc_ptr) - 4, bdc_ptr->page_length);
	if (lun != NULL &&
	    (value = dnvlist_get_string(lun->be_lun->options, "rpm", NULL)) != NULL)
		i = strtol(value, NULL, 0);
	else
		i = CTL_DEFAULT_ROTATION_RATE;
	scsi_ulto2b(i, bdc_ptr->medium_rotation_rate);
	if (lun != NULL &&
	    (value = dnvlist_get_string(lun->be_lun->options, "formfactor", NULL)) != NULL)
		i = strtol(value, NULL, 0);
	else
		i = 0;
	bdc_ptr->wab_wac_ff = (i & 0x0f);
	bdc_ptr->flags = SVPD_FUAB | SVPD_VBULS;

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

static int
ctl_inquiry_evpd_lbp(struct ctl_scsiio *ctsio, int alloc_len)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_vpd_logical_block_prov *lbp_ptr;
	const char *value;

	ctsio->kern_data_ptr = malloc(sizeof(*lbp_ptr), M_CTL, M_WAITOK | M_ZERO);
	lbp_ptr = (struct scsi_vpd_logical_block_prov *)ctsio->kern_data_ptr;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(sizeof(*lbp_ptr), alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * The control device is always connected.  The disk device, on the
	 * other hand, may not be online all the time.  Need to change this
	 * to figure out whether the disk device is actually online or not.
	 */
	if (lun != NULL)
		lbp_ptr->device = (SID_QUAL_LU_CONNECTED << 5) |
				  lun->be_lun->lun_type;
	else
		lbp_ptr->device = (SID_QUAL_LU_OFFLINE << 5) | T_DIRECT;

	lbp_ptr->page_code = SVPD_LBP;
	scsi_ulto2b(sizeof(*lbp_ptr) - 4, lbp_ptr->page_length);
	lbp_ptr->threshold_exponent = CTL_LBP_EXPONENT;
	if (lun != NULL && lun->be_lun->flags & CTL_LUN_FLAG_UNMAP) {
		lbp_ptr->flags = SVPD_LBP_UNMAP | SVPD_LBP_WS16 |
		    SVPD_LBP_WS10 | SVPD_LBP_RZ | SVPD_LBP_ANC_SUP;
		value = dnvlist_get_string(lun->be_lun->options,
		    "provisioning_type", NULL);
		if (value != NULL) {
			if (strcmp(value, "resource") == 0)
				lbp_ptr->prov_type = SVPD_LBP_RESOURCE;
			else if (strcmp(value, "thin") == 0)
				lbp_ptr->prov_type = SVPD_LBP_THIN;
		} else
			lbp_ptr->prov_type = SVPD_LBP_THIN;
	}

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

/*
 * INQUIRY with the EVPD bit set.
 */
static int
ctl_inquiry_evpd(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_inquiry *cdb;
	int alloc_len, retval;

	cdb = (struct scsi_inquiry *)ctsio->cdb;
	alloc_len = scsi_2btoul(cdb->length);

	switch (cdb->page_code) {
	case SVPD_SUPPORTED_PAGES:
		retval = ctl_inquiry_evpd_supported(ctsio, alloc_len);
		break;
	case SVPD_UNIT_SERIAL_NUMBER:
		retval = ctl_inquiry_evpd_serial(ctsio, alloc_len);
		break;
	case SVPD_DEVICE_ID:
		retval = ctl_inquiry_evpd_devid(ctsio, alloc_len);
		break;
	case SVPD_EXTENDED_INQUIRY_DATA:
		retval = ctl_inquiry_evpd_eid(ctsio, alloc_len);
		break;
	case SVPD_MODE_PAGE_POLICY:
		retval = ctl_inquiry_evpd_mpp(ctsio, alloc_len);
		break;
	case SVPD_SCSI_PORTS:
		retval = ctl_inquiry_evpd_scsi_ports(ctsio, alloc_len);
		break;
	case SVPD_SCSI_TPC:
		retval = ctl_inquiry_evpd_tpc(ctsio, alloc_len);
		break;
	case SVPD_BLOCK_LIMITS:
		if (lun == NULL || lun->be_lun->lun_type != T_DIRECT)
			goto err;
		retval = ctl_inquiry_evpd_block_limits(ctsio, alloc_len);
		break;
	case SVPD_BDC:
		if (lun == NULL || lun->be_lun->lun_type != T_DIRECT)
			goto err;
		retval = ctl_inquiry_evpd_bdc(ctsio, alloc_len);
		break;
	case SVPD_LBP:
		if (lun == NULL || lun->be_lun->lun_type != T_DIRECT)
			goto err;
		retval = ctl_inquiry_evpd_lbp(ctsio, alloc_len);
		break;
	default:
err:
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 2,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		retval = CTL_RETVAL_COMPLETE;
		break;
	}

	return (retval);
}

/*
 * Standard INQUIRY data.
 */
static int
ctl_inquiry_std(struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_port *port = CTL_PORT(ctsio);
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_inquiry_data *inq_ptr;
	struct scsi_inquiry *cdb;
	const char *val;
	uint32_t alloc_len, data_len;
	ctl_port_type port_type;

	port_type = port->port_type;
	if (port_type == CTL_PORT_IOCTL || port_type == CTL_PORT_INTERNAL)
		port_type = CTL_PORT_SCSI;

	cdb = (struct scsi_inquiry *)ctsio->cdb;
	alloc_len = scsi_2btoul(cdb->length);

	/*
	 * We malloc the full inquiry data size here and fill it
	 * in.  If the user only asks for less, we'll give him
	 * that much.
	 */
	data_len = offsetof(struct scsi_inquiry_data, vendor_specific1);
	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	inq_ptr = (struct scsi_inquiry_data *)ctsio->kern_data_ptr;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	if (lun != NULL) {
		if ((lun->flags & CTL_LUN_PRIMARY_SC) ||
		    softc->ha_link >= CTL_HA_LINK_UNKNOWN) {
			inq_ptr->device = (SID_QUAL_LU_CONNECTED << 5) |
			    lun->be_lun->lun_type;
		} else {
			inq_ptr->device = (SID_QUAL_LU_OFFLINE << 5) |
			    lun->be_lun->lun_type;
		}
		if (lun->flags & CTL_LUN_REMOVABLE)
			inq_ptr->dev_qual2 |= SID_RMB;
	} else
		inq_ptr->device = (SID_QUAL_BAD_LU << 5) | T_NODEVICE;

	/* RMB in byte 2 is 0 */
	inq_ptr->version = SCSI_REV_SPC5;

	/*
	 * According to SAM-3, even if a device only supports a single
	 * level of LUN addressing, it should still set the HISUP bit:
	 *
	 * 4.9.1 Logical unit numbers overview
	 *
	 * All logical unit number formats described in this standard are
	 * hierarchical in structure even when only a single level in that
	 * hierarchy is used. The HISUP bit shall be set to one in the
	 * standard INQUIRY data (see SPC-2) when any logical unit number
	 * format described in this standard is used.  Non-hierarchical
	 * formats are outside the scope of this standard.
	 *
	 * Therefore we set the HiSup bit here.
	 *
	 * The response format is 2, per SPC-3.
	 */
	inq_ptr->response_format = SID_HiSup | 2;

	inq_ptr->additional_length = data_len -
	    (offsetof(struct scsi_inquiry_data, additional_length) + 1);
	CTL_DEBUG_PRINT(("additional_length = %d\n",
			 inq_ptr->additional_length));

	inq_ptr->spc3_flags = SPC3_SID_3PC | SPC3_SID_TPGS_IMPLICIT;
	if (port_type == CTL_PORT_SCSI)
		inq_ptr->spc2_flags = SPC2_SID_ADDR16;
	inq_ptr->spc2_flags |= SPC2_SID_MultiP;
	inq_ptr->flags = SID_CmdQue;
	if (port_type == CTL_PORT_SCSI)
		inq_ptr->flags |= SID_WBus16 | SID_Sync;

	/*
	 * Per SPC-3, unused bytes in ASCII strings are filled with spaces.
	 * We have 8 bytes for the vendor name, and 16 bytes for the device
	 * name and 4 bytes for the revision.
	 */
	if (lun == NULL || (val = dnvlist_get_string(lun->be_lun->options,
	    "vendor", NULL)) == NULL) {
		strncpy(inq_ptr->vendor, CTL_VENDOR, sizeof(inq_ptr->vendor));
	} else {
		memset(inq_ptr->vendor, ' ', sizeof(inq_ptr->vendor));
		strncpy(inq_ptr->vendor, val,
		    min(sizeof(inq_ptr->vendor), strlen(val)));
	}
	if (lun == NULL) {
		strncpy(inq_ptr->product, CTL_DIRECT_PRODUCT,
		    sizeof(inq_ptr->product));
	} else if ((val = dnvlist_get_string(lun->be_lun->options, "product",
	    NULL)) == NULL) {
		switch (lun->be_lun->lun_type) {
		case T_DIRECT:
			strncpy(inq_ptr->product, CTL_DIRECT_PRODUCT,
			    sizeof(inq_ptr->product));
			break;
		case T_PROCESSOR:
			strncpy(inq_ptr->product, CTL_PROCESSOR_PRODUCT,
			    sizeof(inq_ptr->product));
			break;
		case T_CDROM:
			strncpy(inq_ptr->product, CTL_CDROM_PRODUCT,
			    sizeof(inq_ptr->product));
			break;
		default:
			strncpy(inq_ptr->product, CTL_UNKNOWN_PRODUCT,
			    sizeof(inq_ptr->product));
			break;
		}
	} else {
		memset(inq_ptr->product, ' ', sizeof(inq_ptr->product));
		strncpy(inq_ptr->product, val,
		    min(sizeof(inq_ptr->product), strlen(val)));
	}

	/*
	 * XXX make this a macro somewhere so it automatically gets
	 * incremented when we make changes.
	 */
	if (lun == NULL || (val = dnvlist_get_string(lun->be_lun->options,
	    "revision", NULL)) == NULL) {
		strncpy(inq_ptr->revision, "0001", sizeof(inq_ptr->revision));
	} else {
		memset(inq_ptr->revision, ' ', sizeof(inq_ptr->revision));
		strncpy(inq_ptr->revision, val,
		    min(sizeof(inq_ptr->revision), strlen(val)));
	}

	/*
	 * For parallel SCSI, we support double transition and single
	 * transition clocking.  We also support QAS (Quick Arbitration
	 * and Selection) and Information Unit transfers on both the
	 * control and array devices.
	 */
	if (port_type == CTL_PORT_SCSI)
		inq_ptr->spi3data = SID_SPI_CLOCK_DT_ST | SID_SPI_QAS |
				    SID_SPI_IUS;

	/* SAM-6 (no version claimed) */
	scsi_ulto2b(0x00C0, inq_ptr->version1);
	/* SPC-5 (no version claimed) */
	scsi_ulto2b(0x05C0, inq_ptr->version2);
	if (port_type == CTL_PORT_FC) {
		/* FCP-2 ANSI INCITS.350:2003 */
		scsi_ulto2b(0x0917, inq_ptr->version3);
	} else if (port_type == CTL_PORT_SCSI) {
		/* SPI-4 ANSI INCITS.362:200x */
		scsi_ulto2b(0x0B56, inq_ptr->version3);
	} else if (port_type == CTL_PORT_ISCSI) {
		/* iSCSI (no version claimed) */
		scsi_ulto2b(0x0960, inq_ptr->version3);
	} else if (port_type == CTL_PORT_SAS) {
		/* SAS (no version claimed) */
		scsi_ulto2b(0x0BE0, inq_ptr->version3);
	} else if (port_type == CTL_PORT_UMASS) {
		/* USB Mass Storage Class Bulk-Only Transport, Revision 1.0 */
		scsi_ulto2b(0x1730, inq_ptr->version3);
	}

	if (lun == NULL) {
		/* SBC-4 (no version claimed) */
		scsi_ulto2b(0x0600, inq_ptr->version4);
	} else {
		switch (lun->be_lun->lun_type) {
		case T_DIRECT:
			/* SBC-4 (no version claimed) */
			scsi_ulto2b(0x0600, inq_ptr->version4);
			break;
		case T_PROCESSOR:
			break;
		case T_CDROM:
			/* MMC-6 (no version claimed) */
			scsi_ulto2b(0x04E0, inq_ptr->version4);
			break;
		default:
			break;
		}
	}

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_inquiry(struct ctl_scsiio *ctsio)
{
	struct scsi_inquiry *cdb;
	int retval;

	CTL_DEBUG_PRINT(("ctl_inquiry\n"));

	cdb = (struct scsi_inquiry *)ctsio->cdb;
	if (cdb->byte2 & SI_EVPD)
		retval = ctl_inquiry_evpd(ctsio);
	else if (cdb->page_code == 0)
		retval = ctl_inquiry_std(ctsio);
	else {
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 2,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}

	return (retval);
}

int
ctl_get_config(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_get_config_header *hdr;
	struct scsi_get_config_feature *feature;
	struct scsi_get_config *cdb;
	uint32_t alloc_len, data_len;
	int rt, starting;

	cdb = (struct scsi_get_config *)ctsio->cdb;
	rt = (cdb->rt & SGC_RT_MASK);
	starting = scsi_2btoul(cdb->starting_feature);
	alloc_len = scsi_2btoul(cdb->length);

	data_len = sizeof(struct scsi_get_config_header) +
	    sizeof(struct scsi_get_config_feature) + 8 +
	    sizeof(struct scsi_get_config_feature) + 8 +
	    sizeof(struct scsi_get_config_feature) + 4 +
	    sizeof(struct scsi_get_config_feature) + 4 +
	    sizeof(struct scsi_get_config_feature) + 8 +
	    sizeof(struct scsi_get_config_feature) +
	    sizeof(struct scsi_get_config_feature) + 4 +
	    sizeof(struct scsi_get_config_feature) + 4 +
	    sizeof(struct scsi_get_config_feature) + 4 +
	    sizeof(struct scsi_get_config_feature) + 4 +
	    sizeof(struct scsi_get_config_feature) + 4 +
	    sizeof(struct scsi_get_config_feature) + 4;
	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;

	hdr = (struct scsi_get_config_header *)ctsio->kern_data_ptr;
	if (lun->flags & CTL_LUN_NO_MEDIA)
		scsi_ulto2b(0x0000, hdr->current_profile);
	else
		scsi_ulto2b(0x0010, hdr->current_profile);
	feature = (struct scsi_get_config_feature *)(hdr + 1);

	if (starting > 0x003b)
		goto done;
	if (starting > 0x003a)
		goto f3b;
	if (starting > 0x002b)
		goto f3a;
	if (starting > 0x002a)
		goto f2b;
	if (starting > 0x001f)
		goto f2a;
	if (starting > 0x001e)
		goto f1f;
	if (starting > 0x001d)
		goto f1e;
	if (starting > 0x0010)
		goto f1d;
	if (starting > 0x0003)
		goto f10;
	if (starting > 0x0002)
		goto f3;
	if (starting > 0x0001)
		goto f2;
	if (starting > 0x0000)
		goto f1;

	/* Profile List */
	scsi_ulto2b(0x0000, feature->feature_code);
	feature->flags = SGC_F_PERSISTENT | SGC_F_CURRENT;
	feature->add_length = 8;
	scsi_ulto2b(0x0008, &feature->feature_data[0]);	/* CD-ROM */
	feature->feature_data[2] = 0x00;
	scsi_ulto2b(0x0010, &feature->feature_data[4]);	/* DVD-ROM */
	feature->feature_data[6] = 0x01;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

f1:	/* Core */
	scsi_ulto2b(0x0001, feature->feature_code);
	feature->flags = 0x08 | SGC_F_PERSISTENT | SGC_F_CURRENT;
	feature->add_length = 8;
	scsi_ulto4b(0x00000000, &feature->feature_data[0]);
	feature->feature_data[4] = 0x03;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

f2:	/* Morphing */
	scsi_ulto2b(0x0002, feature->feature_code);
	feature->flags = 0x04 | SGC_F_PERSISTENT | SGC_F_CURRENT;
	feature->add_length = 4;
	feature->feature_data[0] = 0x02;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

f3:	/* Removable Medium */
	scsi_ulto2b(0x0003, feature->feature_code);
	feature->flags = 0x04 | SGC_F_PERSISTENT | SGC_F_CURRENT;
	feature->add_length = 4;
	feature->feature_data[0] = 0x39;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

	if (rt == SGC_RT_CURRENT && (lun->flags & CTL_LUN_NO_MEDIA))
		goto done;

f10:	/* Random Read */
	scsi_ulto2b(0x0010, feature->feature_code);
	feature->flags = 0x00;
	if ((lun->flags & CTL_LUN_NO_MEDIA) == 0)
		feature->flags |= SGC_F_CURRENT;
	feature->add_length = 8;
	scsi_ulto4b(lun->be_lun->blocksize, &feature->feature_data[0]);
	scsi_ulto2b(1, &feature->feature_data[4]);
	feature->feature_data[6] = 0x00;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

f1d:	/* Multi-Read */
	scsi_ulto2b(0x001D, feature->feature_code);
	feature->flags = 0x00;
	if ((lun->flags & CTL_LUN_NO_MEDIA) == 0)
		feature->flags |= SGC_F_CURRENT;
	feature->add_length = 0;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

f1e:	/* CD Read */
	scsi_ulto2b(0x001E, feature->feature_code);
	feature->flags = 0x00;
	if ((lun->flags & CTL_LUN_NO_MEDIA) == 0)
		feature->flags |= SGC_F_CURRENT;
	feature->add_length = 4;
	feature->feature_data[0] = 0x00;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

f1f:	/* DVD Read */
	scsi_ulto2b(0x001F, feature->feature_code);
	feature->flags = 0x08;
	if ((lun->flags & CTL_LUN_NO_MEDIA) == 0)
		feature->flags |= SGC_F_CURRENT;
	feature->add_length = 4;
	feature->feature_data[0] = 0x01;
	feature->feature_data[2] = 0x03;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

f2a:	/* DVD+RW */
	scsi_ulto2b(0x002A, feature->feature_code);
	feature->flags = 0x04;
	if ((lun->flags & CTL_LUN_NO_MEDIA) == 0)
		feature->flags |= SGC_F_CURRENT;
	feature->add_length = 4;
	feature->feature_data[0] = 0x00;
	feature->feature_data[1] = 0x00;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

f2b:	/* DVD+R */
	scsi_ulto2b(0x002B, feature->feature_code);
	feature->flags = 0x00;
	if ((lun->flags & CTL_LUN_NO_MEDIA) == 0)
		feature->flags |= SGC_F_CURRENT;
	feature->add_length = 4;
	feature->feature_data[0] = 0x00;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

f3a:	/* DVD+RW Dual Layer */
	scsi_ulto2b(0x003A, feature->feature_code);
	feature->flags = 0x00;
	if ((lun->flags & CTL_LUN_NO_MEDIA) == 0)
		feature->flags |= SGC_F_CURRENT;
	feature->add_length = 4;
	feature->feature_data[0] = 0x00;
	feature->feature_data[1] = 0x00;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

f3b:	/* DVD+R Dual Layer */
	scsi_ulto2b(0x003B, feature->feature_code);
	feature->flags = 0x00;
	if ((lun->flags & CTL_LUN_NO_MEDIA) == 0)
		feature->flags |= SGC_F_CURRENT;
	feature->add_length = 4;
	feature->feature_data[0] = 0x00;
	feature = (struct scsi_get_config_feature *)
	    &feature->feature_data[feature->add_length];

done:
	data_len = (uint8_t *)feature - (uint8_t *)hdr;
	if (rt == SGC_RT_SPECIFIC && data_len > 4) {
		feature = (struct scsi_get_config_feature *)(hdr + 1);
		if (scsi_2btoul(feature->feature_code) == starting)
			feature = (struct scsi_get_config_feature *)
			    &feature->feature_data[feature->add_length];
		data_len = (uint8_t *)feature - (uint8_t *)hdr;
	}
	scsi_ulto4b(data_len - 4, hdr->data_length);
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_get_event_status(struct ctl_scsiio *ctsio)
{
	struct scsi_get_event_status_header *hdr;
	struct scsi_get_event_status *cdb;
	uint32_t alloc_len, data_len;

	cdb = (struct scsi_get_event_status *)ctsio->cdb;
	if ((cdb->byte2 & SGESN_POLLED) == 0) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1, /*command*/ 1,
		    /*field*/ 1, /*bit_valid*/ 1, /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (CTL_RETVAL_COMPLETE);
	}
	alloc_len = scsi_2btoul(cdb->length);

	data_len = sizeof(struct scsi_get_event_status_header);
	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	hdr = (struct scsi_get_event_status_header *)ctsio->kern_data_ptr;
	scsi_ulto2b(0, hdr->descr_length);
	hdr->nea_class = SGESN_NEA;
	hdr->supported_class = 0;

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_mechanism_status(struct ctl_scsiio *ctsio)
{
	struct scsi_mechanism_status_header *hdr;
	struct scsi_mechanism_status *cdb;
	uint32_t alloc_len, data_len;

	cdb = (struct scsi_mechanism_status *)ctsio->cdb;
	alloc_len = scsi_2btoul(cdb->length);

	data_len = sizeof(struct scsi_mechanism_status_header);
	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	hdr = (struct scsi_mechanism_status_header *)ctsio->kern_data_ptr;
	hdr->state1 = 0x00;
	hdr->state2 = 0xe0;
	scsi_ulto3b(0, hdr->lba);
	hdr->slots_num = 0;
	scsi_ulto2b(0, hdr->slots_length);

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

static void
ctl_ultomsf(uint32_t lba, uint8_t *buf)
{

	lba += 150;
	buf[0] = 0;
	buf[1] = bin2bcd((lba / 75) / 60);
	buf[2] = bin2bcd((lba / 75) % 60);
	buf[3] = bin2bcd(lba % 75);
}

int
ctl_read_toc(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_read_toc_hdr *hdr;
	struct scsi_read_toc_type01_descr *descr;
	struct scsi_read_toc *cdb;
	uint32_t alloc_len, data_len;
	int format, msf;

	cdb = (struct scsi_read_toc *)ctsio->cdb;
	msf = (cdb->byte2 & CD_MSF) != 0;
	format = cdb->format;
	alloc_len = scsi_2btoul(cdb->data_len);

	data_len = sizeof(struct scsi_read_toc_hdr);
	if (format == 0)
		data_len += 2 * sizeof(struct scsi_read_toc_type01_descr);
	else
		data_len += sizeof(struct scsi_read_toc_type01_descr);
	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	hdr = (struct scsi_read_toc_hdr *)ctsio->kern_data_ptr;
	if (format == 0) {
		scsi_ulto2b(0x12, hdr->data_length);
		hdr->first = 1;
		hdr->last = 1;
		descr = (struct scsi_read_toc_type01_descr *)(hdr + 1);
		descr->addr_ctl = 0x14;
		descr->track_number = 1;
		if (msf)
			ctl_ultomsf(0, descr->track_start);
		else
			scsi_ulto4b(0, descr->track_start);
		descr++;
		descr->addr_ctl = 0x14;
		descr->track_number = 0xaa;
		if (msf)
			ctl_ultomsf(lun->be_lun->maxlba+1, descr->track_start);
		else
			scsi_ulto4b(lun->be_lun->maxlba+1, descr->track_start);
	} else {
		scsi_ulto2b(0x0a, hdr->data_length);
		hdr->first = 1;
		hdr->last = 1;
		descr = (struct scsi_read_toc_type01_descr *)(hdr + 1);
		descr->addr_ctl = 0x14;
		descr->track_number = 1;
		if (msf)
			ctl_ultomsf(0, descr->track_start);
		else
			scsi_ulto4b(0, descr->track_start);
	}

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

/*
 * For known CDB types, parse the LBA and length.
 */
static int
ctl_get_lba_len(union ctl_io *io, uint64_t *lba, uint64_t *len)
{
	if (io->io_hdr.io_type != CTL_IO_SCSI)
		return (1);

	switch (io->scsiio.cdb[0]) {
	case COMPARE_AND_WRITE: {
		struct scsi_compare_and_write *cdb;

		cdb = (struct scsi_compare_and_write *)io->scsiio.cdb;

		*lba = scsi_8btou64(cdb->addr);
		*len = cdb->length;
		break;
	}
	case READ_6:
	case WRITE_6: {
		struct scsi_rw_6 *cdb;

		cdb = (struct scsi_rw_6 *)io->scsiio.cdb;

		*lba = scsi_3btoul(cdb->addr);
		/* only 5 bits are valid in the most significant address byte */
		*lba &= 0x1fffff;
		*len = cdb->length;
		break;
	}
	case READ_10:
	case WRITE_10: {
		struct scsi_rw_10 *cdb;

		cdb = (struct scsi_rw_10 *)io->scsiio.cdb;

		*lba = scsi_4btoul(cdb->addr);
		*len = scsi_2btoul(cdb->length);
		break;
	}
	case WRITE_VERIFY_10: {
		struct scsi_write_verify_10 *cdb;

		cdb = (struct scsi_write_verify_10 *)io->scsiio.cdb;

		*lba = scsi_4btoul(cdb->addr);
		*len = scsi_2btoul(cdb->length);
		break;
	}
	case READ_12:
	case WRITE_12: {
		struct scsi_rw_12 *cdb;

		cdb = (struct scsi_rw_12 *)io->scsiio.cdb;

		*lba = scsi_4btoul(cdb->addr);
		*len = scsi_4btoul(cdb->length);
		break;
	}
	case WRITE_VERIFY_12: {
		struct scsi_write_verify_12 *cdb;

		cdb = (struct scsi_write_verify_12 *)io->scsiio.cdb;

		*lba = scsi_4btoul(cdb->addr);
		*len = scsi_4btoul(cdb->length);
		break;
	}
	case READ_16:
	case WRITE_16: {
		struct scsi_rw_16 *cdb;

		cdb = (struct scsi_rw_16 *)io->scsiio.cdb;

		*lba = scsi_8btou64(cdb->addr);
		*len = scsi_4btoul(cdb->length);
		break;
	}
	case WRITE_ATOMIC_16: {
		struct scsi_write_atomic_16 *cdb;

		cdb = (struct scsi_write_atomic_16 *)io->scsiio.cdb;

		*lba = scsi_8btou64(cdb->addr);
		*len = scsi_2btoul(cdb->length);
		break;
	}
	case WRITE_VERIFY_16: {
		struct scsi_write_verify_16 *cdb;

		cdb = (struct scsi_write_verify_16 *)io->scsiio.cdb;

		*lba = scsi_8btou64(cdb->addr);
		*len = scsi_4btoul(cdb->length);
		break;
	}
	case WRITE_SAME_10: {
		struct scsi_write_same_10 *cdb;

		cdb = (struct scsi_write_same_10 *)io->scsiio.cdb;

		*lba = scsi_4btoul(cdb->addr);
		*len = scsi_2btoul(cdb->length);
		break;
	}
	case WRITE_SAME_16: {
		struct scsi_write_same_16 *cdb;

		cdb = (struct scsi_write_same_16 *)io->scsiio.cdb;

		*lba = scsi_8btou64(cdb->addr);
		*len = scsi_4btoul(cdb->length);
		break;
	}
	case VERIFY_10: {
		struct scsi_verify_10 *cdb;

		cdb = (struct scsi_verify_10 *)io->scsiio.cdb;

		*lba = scsi_4btoul(cdb->addr);
		*len = scsi_2btoul(cdb->length);
		break;
	}
	case VERIFY_12: {
		struct scsi_verify_12 *cdb;

		cdb = (struct scsi_verify_12 *)io->scsiio.cdb;

		*lba = scsi_4btoul(cdb->addr);
		*len = scsi_4btoul(cdb->length);
		break;
	}
	case VERIFY_16: {
		struct scsi_verify_16 *cdb;

		cdb = (struct scsi_verify_16 *)io->scsiio.cdb;

		*lba = scsi_8btou64(cdb->addr);
		*len = scsi_4btoul(cdb->length);
		break;
	}
	case UNMAP: {
		*lba = 0;
		*len = UINT64_MAX;
		break;
	}
	case SERVICE_ACTION_IN: {	/* GET LBA STATUS */
		struct scsi_get_lba_status *cdb;

		cdb = (struct scsi_get_lba_status *)io->scsiio.cdb;
		*lba = scsi_8btou64(cdb->addr);
		*len = UINT32_MAX;
		break;
	}
	default:
		return (1);
		break; /* NOTREACHED */
	}

	return (0);
}

static ctl_action
ctl_extent_check_lba(uint64_t lba1, uint64_t len1, uint64_t lba2, uint64_t len2,
    bool seq)
{
	uint64_t endlba1, endlba2;

	endlba1 = lba1 + len1 - (seq ? 0 : 1);
	endlba2 = lba2 + len2 - 1;

	if ((endlba1 < lba2) || (endlba2 < lba1))
		return (CTL_ACTION_PASS);
	else
		return (CTL_ACTION_BLOCK);
}

static int
ctl_extent_check_unmap(union ctl_io *io, uint64_t lba2, uint64_t len2)
{
	struct ctl_ptr_len_flags *ptrlen;
	struct scsi_unmap_desc *buf, *end, *range;
	uint64_t lba;
	uint32_t len;

	/* If not UNMAP -- go other way. */
	if (io->io_hdr.io_type != CTL_IO_SCSI ||
	    io->scsiio.cdb[0] != UNMAP)
		return (CTL_ACTION_ERROR);

	/* If UNMAP without data -- block and wait for data. */
	ptrlen = (struct ctl_ptr_len_flags *)
	    &io->io_hdr.ctl_private[CTL_PRIV_LBA_LEN];
	if ((io->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0 ||
	    ptrlen->ptr == NULL)
		return (CTL_ACTION_BLOCK);

	/* UNMAP with data -- check for collision. */
	buf = (struct scsi_unmap_desc *)ptrlen->ptr;
	end = buf + ptrlen->len / sizeof(*buf);
	for (range = buf; range < end; range++) {
		lba = scsi_8btou64(range->lba);
		len = scsi_4btoul(range->length);
		if ((lba < lba2 + len2) && (lba + len > lba2))
			return (CTL_ACTION_BLOCK);
	}
	return (CTL_ACTION_PASS);
}

static ctl_action
ctl_extent_check(union ctl_io *io1, union ctl_io *io2, bool seq)
{
	uint64_t lba1, lba2;
	uint64_t len1, len2;
	int retval;

	if (ctl_get_lba_len(io2, &lba2, &len2) != 0)
		return (CTL_ACTION_ERROR);

	retval = ctl_extent_check_unmap(io1, lba2, len2);
	if (retval != CTL_ACTION_ERROR)
		return (retval);

	if (ctl_get_lba_len(io1, &lba1, &len1) != 0)
		return (CTL_ACTION_ERROR);

	if (io1->io_hdr.flags & CTL_FLAG_SERSEQ_DONE)
		seq = FALSE;
	return (ctl_extent_check_lba(lba1, len1, lba2, len2, seq));
}

static ctl_action
ctl_extent_check_seq(union ctl_io *io1, union ctl_io *io2)
{
	uint64_t lba1, lba2;
	uint64_t len1, len2;

	if (io1->io_hdr.flags & CTL_FLAG_SERSEQ_DONE)
		return (CTL_ACTION_PASS);
	if (ctl_get_lba_len(io1, &lba1, &len1) != 0)
		return (CTL_ACTION_ERROR);
	if (ctl_get_lba_len(io2, &lba2, &len2) != 0)
		return (CTL_ACTION_ERROR);

	if (lba1 + len1 == lba2)
		return (CTL_ACTION_BLOCK);
	return (CTL_ACTION_PASS);
}

static ctl_action
ctl_check_for_blockage(struct ctl_lun *lun, union ctl_io *pending_io,
    union ctl_io *ooa_io)
{
	const struct ctl_cmd_entry *pending_entry, *ooa_entry;
	const ctl_serialize_action *serialize_row;

	/*
	 * Aborted commands are not going to be executed and may even
	 * not report completion, so we don't care about their order.
	 * Let them complete ASAP to clean the OOA queue.
	 */
	if (pending_io->io_hdr.flags & CTL_FLAG_ABORT)
		return (CTL_ACTION_SKIP);

	/*
	 * The initiator attempted multiple untagged commands at the same
	 * time.  Can't do that.
	 */
	if ((pending_io->scsiio.tag_type == CTL_TAG_UNTAGGED)
	 && (ooa_io->scsiio.tag_type == CTL_TAG_UNTAGGED)
	 && ((pending_io->io_hdr.nexus.targ_port ==
	      ooa_io->io_hdr.nexus.targ_port)
	  && (pending_io->io_hdr.nexus.initid ==
	      ooa_io->io_hdr.nexus.initid))
	 && ((ooa_io->io_hdr.flags & (CTL_FLAG_ABORT |
	      CTL_FLAG_STATUS_SENT)) == 0))
		return (CTL_ACTION_OVERLAP);

	/*
	 * The initiator attempted to send multiple tagged commands with
	 * the same ID.  (It's fine if different initiators have the same
	 * tag ID.)
	 *
	 * Even if all of those conditions are true, we don't kill the I/O
	 * if the command ahead of us has been aborted.  We won't end up
	 * sending it to the FETD, and it's perfectly legal to resend a
	 * command with the same tag number as long as the previous
	 * instance of this tag number has been aborted somehow.
	 */
	if ((pending_io->scsiio.tag_type != CTL_TAG_UNTAGGED)
	 && (ooa_io->scsiio.tag_type != CTL_TAG_UNTAGGED)
	 && (pending_io->scsiio.tag_num == ooa_io->scsiio.tag_num)
	 && ((pending_io->io_hdr.nexus.targ_port ==
	      ooa_io->io_hdr.nexus.targ_port)
	  && (pending_io->io_hdr.nexus.initid ==
	      ooa_io->io_hdr.nexus.initid))
	 && ((ooa_io->io_hdr.flags & (CTL_FLAG_ABORT |
	      CTL_FLAG_STATUS_SENT)) == 0))
		return (CTL_ACTION_OVERLAP_TAG);

	/*
	 * If we get a head of queue tag, SAM-3 says that we should
	 * immediately execute it.
	 *
	 * What happens if this command would normally block for some other
	 * reason?  e.g. a request sense with a head of queue tag
	 * immediately after a write.  Normally that would block, but this
	 * will result in its getting executed immediately...
	 *
	 * We currently return "pass" instead of "skip", so we'll end up
	 * going through the rest of the queue to check for overlapped tags.
	 *
	 * XXX KDM check for other types of blockage first??
	 */
	if (pending_io->scsiio.tag_type == CTL_TAG_HEAD_OF_QUEUE)
		return (CTL_ACTION_PASS);

	/*
	 * Ordered tags have to block until all items ahead of them
	 * have completed.  If we get called with an ordered tag, we always
	 * block, if something else is ahead of us in the queue.
	 */
	if (pending_io->scsiio.tag_type == CTL_TAG_ORDERED)
		return (CTL_ACTION_BLOCK);

	/*
	 * Simple tags get blocked until all head of queue and ordered tags
	 * ahead of them have completed.  I'm lumping untagged commands in
	 * with simple tags here.  XXX KDM is that the right thing to do?
	 */
	if (((pending_io->scsiio.tag_type == CTL_TAG_UNTAGGED)
	  || (pending_io->scsiio.tag_type == CTL_TAG_SIMPLE))
	 && ((ooa_io->scsiio.tag_type == CTL_TAG_HEAD_OF_QUEUE)
	  || (ooa_io->scsiio.tag_type == CTL_TAG_ORDERED)))
		return (CTL_ACTION_BLOCK);

	pending_entry = ctl_get_cmd_entry(&pending_io->scsiio, NULL);
	KASSERT(pending_entry->seridx < CTL_SERIDX_COUNT,
	    ("%s: Invalid seridx %d for pending CDB %02x %02x @ %p",
	     __func__, pending_entry->seridx, pending_io->scsiio.cdb[0],
	     pending_io->scsiio.cdb[1], pending_io));
	ooa_entry = ctl_get_cmd_entry(&ooa_io->scsiio, NULL);
	if (ooa_entry->seridx == CTL_SERIDX_INVLD)
		return (CTL_ACTION_PASS); /* Unsupported command in OOA queue */
	KASSERT(ooa_entry->seridx < CTL_SERIDX_COUNT,
	    ("%s: Invalid seridx %d for ooa CDB %02x %02x @ %p",
	     __func__, ooa_entry->seridx, ooa_io->scsiio.cdb[0],
	     ooa_io->scsiio.cdb[1], ooa_io));

	serialize_row = ctl_serialize_table[ooa_entry->seridx];

	switch (serialize_row[pending_entry->seridx]) {
	case CTL_SER_BLOCK:
		return (CTL_ACTION_BLOCK);
	case CTL_SER_EXTENT:
		return (ctl_extent_check(ooa_io, pending_io,
		    (lun->be_lun && lun->be_lun->serseq == CTL_LUN_SERSEQ_ON)));
	case CTL_SER_EXTENTOPT:
		if ((lun->MODE_CTRL.queue_flags & SCP_QUEUE_ALG_MASK) !=
		    SCP_QUEUE_ALG_UNRESTRICTED)
			return (ctl_extent_check(ooa_io, pending_io,
			    (lun->be_lun &&
			     lun->be_lun->serseq == CTL_LUN_SERSEQ_ON)));
		return (CTL_ACTION_PASS);
	case CTL_SER_EXTENTSEQ:
		if (lun->be_lun && lun->be_lun->serseq != CTL_LUN_SERSEQ_OFF)
			return (ctl_extent_check_seq(ooa_io, pending_io));
		return (CTL_ACTION_PASS);
	case CTL_SER_PASS:
		return (CTL_ACTION_PASS);
	case CTL_SER_BLOCKOPT:
		if ((lun->MODE_CTRL.queue_flags & SCP_QUEUE_ALG_MASK) !=
		    SCP_QUEUE_ALG_UNRESTRICTED)
			return (CTL_ACTION_BLOCK);
		return (CTL_ACTION_PASS);
	case CTL_SER_SKIP:
		return (CTL_ACTION_SKIP);
	default:
		panic("%s: Invalid serialization value %d for %d => %d",
		    __func__, serialize_row[pending_entry->seridx],
		    pending_entry->seridx, ooa_entry->seridx);
	}

	return (CTL_ACTION_ERROR);
}

/*
 * Check for blockage or overlaps against the OOA (Order Of Arrival) queue.
 * Assumptions:
 * - pending_io is generally either incoming, or on the blocked queue
 * - starting I/O is the I/O we want to start the check with.
 */
static ctl_action
ctl_check_ooa(struct ctl_lun *lun, union ctl_io *pending_io,
	      union ctl_io **starting_io)
{
	union ctl_io *ooa_io;
	ctl_action action;

	mtx_assert(&lun->lun_lock, MA_OWNED);

	/*
	 * Run back along the OOA queue, starting with the current
	 * blocked I/O and going through every I/O before it on the
	 * queue.  If starting_io is NULL, we'll just end up returning
	 * CTL_ACTION_PASS.
	 */
	for (ooa_io = *starting_io; ooa_io != NULL;
	     ooa_io = (union ctl_io *)TAILQ_PREV(&ooa_io->io_hdr, ctl_ooaq,
	     ooa_links)){
		action = ctl_check_for_blockage(lun, pending_io, ooa_io);
		if (action != CTL_ACTION_PASS) {
			*starting_io = ooa_io;
			return (action);
		}
	}

	*starting_io = NULL;
	return (CTL_ACTION_PASS);
}

/*
 * Try to unblock the specified I/O.
 *
 * skip parameter allows explicitly skip present blocker of the I/O,
 * starting from the previous one on OOA queue.  It can be used when
 * we know for sure that the blocker I/O does no longer count.
 */
static void
ctl_try_unblock_io(struct ctl_lun *lun, union ctl_io *io, bool skip)
{
	struct ctl_softc *softc = lun->ctl_softc;
	union ctl_io *bio, *obio;
	const struct ctl_cmd_entry *entry;
	union ctl_ha_msg msg_info;
	ctl_action action;

	mtx_assert(&lun->lun_lock, MA_OWNED);

	if (io->io_hdr.blocker == NULL)
		return;

	obio = bio = io->io_hdr.blocker;
	if (skip)
		bio = (union ctl_io *)TAILQ_PREV(&bio->io_hdr, ctl_ooaq,
		    ooa_links);
	action = ctl_check_ooa(lun, io, &bio);
	if (action == CTL_ACTION_BLOCK) {
		/* Still blocked, but may be by different I/O now. */
		if (bio != obio) {
			TAILQ_REMOVE(&obio->io_hdr.blocked_queue,
			    &io->io_hdr, blocked_links);
			TAILQ_INSERT_TAIL(&bio->io_hdr.blocked_queue,
			    &io->io_hdr, blocked_links);
			io->io_hdr.blocker = bio;
		}
		return;
	}

	/* No longer blocked, one way or another. */
	TAILQ_REMOVE(&obio->io_hdr.blocked_queue, &io->io_hdr, blocked_links);
	io->io_hdr.blocker = NULL;

	switch (action) {
	case CTL_ACTION_OVERLAP:
		ctl_set_overlapped_cmd(&io->scsiio);
		goto error;
	case CTL_ACTION_OVERLAP_TAG:
		ctl_set_overlapped_tag(&io->scsiio,
		    io->scsiio.tag_num & 0xff);
		goto error;
	case CTL_ACTION_PASS:
	case CTL_ACTION_SKIP:

		/* Serializing commands from the other SC retire there. */
		if ((io->io_hdr.flags & CTL_FLAG_FROM_OTHER_SC) &&
		    (softc->ha_mode != CTL_HA_MODE_XFER)) {
			io->io_hdr.flags &= ~CTL_FLAG_IO_ACTIVE;
			msg_info.hdr.original_sc = io->io_hdr.remote_io;
			msg_info.hdr.serializing_sc = io;
			msg_info.hdr.msg_type = CTL_MSG_R2R;
			ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg_info,
			    sizeof(msg_info.hdr), M_NOWAIT);
			break;
		}

		/*
		 * Check this I/O for LUN state changes that may have happened
		 * while this command was blocked. The LUN state may have been
		 * changed by a command ahead of us in the queue.
		 */
		entry = ctl_get_cmd_entry(&io->scsiio, NULL);
		if (ctl_scsiio_lun_check(lun, entry, &io->scsiio) != 0) {
			ctl_done(io);
			break;
		}

		io->io_hdr.flags |= CTL_FLAG_IS_WAS_ON_RTR;
		ctl_enqueue_rtr(io);
		break;
	case CTL_ACTION_ERROR:
	default:
		ctl_set_internal_failure(&io->scsiio,
					 /*sks_valid*/ 0,
					 /*retry_count*/ 0);

error:
		/* Serializing commands from the other SC are done here. */
		if ((io->io_hdr.flags & CTL_FLAG_FROM_OTHER_SC) &&
		    (softc->ha_mode != CTL_HA_MODE_XFER)) {
			ctl_try_unblock_others(lun, io, TRUE);
			TAILQ_REMOVE(&lun->ooa_queue, &io->io_hdr, ooa_links);

			ctl_copy_sense_data_back(io, &msg_info);
			msg_info.hdr.original_sc = io->io_hdr.remote_io;
			msg_info.hdr.serializing_sc = NULL;
			msg_info.hdr.msg_type = CTL_MSG_BAD_JUJU;
			ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg_info,
			    sizeof(msg_info.scsi), M_WAITOK);
			ctl_free_io(io);
			break;
		}

		ctl_done(io);
		break;
	}
}

/*
 * Try to unblock I/Os blocked by the specified I/O.
 *
 * skip parameter allows explicitly skip the specified I/O as blocker,
 * starting from the previous one on the OOA queue.  It can be used when
 * we know for sure that the specified I/O does no longer count (done).
 * It has to be still on OOA queue though so that we know where to start.
 */
static void
ctl_try_unblock_others(struct ctl_lun *lun, union ctl_io *bio, bool skip)
{
	union ctl_io *io, *next_io;

	mtx_assert(&lun->lun_lock, MA_OWNED);

	for (io = (union ctl_io *)TAILQ_FIRST(&bio->io_hdr.blocked_queue);
	     io != NULL; io = next_io) {
		next_io = (union ctl_io *)TAILQ_NEXT(&io->io_hdr, blocked_links);

		KASSERT(io->io_hdr.blocker != NULL,
		    ("I/O %p on blocked list without blocker", io));
		ctl_try_unblock_io(lun, io, skip);
	}
	KASSERT(!skip || TAILQ_EMPTY(&bio->io_hdr.blocked_queue),
	    ("blocked_queue is not empty after skipping %p", bio));
}

/*
 * This routine (with one exception) checks LUN flags that can be set by
 * commands ahead of us in the OOA queue.  These flags have to be checked
 * when a command initially comes in, and when we pull a command off the
 * blocked queue and are preparing to execute it.  The reason we have to
 * check these flags for commands on the blocked queue is that the LUN
 * state may have been changed by a command ahead of us while we're on the
 * blocked queue.
 *
 * Ordering is somewhat important with these checks, so please pay
 * careful attention to the placement of any new checks.
 */
static int
ctl_scsiio_lun_check(struct ctl_lun *lun,
    const struct ctl_cmd_entry *entry, struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = lun->ctl_softc;
	int retval;
	uint32_t residx;

	retval = 0;

	mtx_assert(&lun->lun_lock, MA_OWNED);

	/*
	 * If this shelf is a secondary shelf controller, we may have to
	 * reject some commands disallowed by HA mode and link state.
	 */
	if ((lun->flags & CTL_LUN_PRIMARY_SC) == 0) {
		if (softc->ha_link == CTL_HA_LINK_OFFLINE &&
		    (entry->flags & CTL_CMD_FLAG_OK_ON_UNAVAIL) == 0) {
			ctl_set_lun_unavail(ctsio);
			retval = 1;
			goto bailout;
		}
		if ((lun->flags & CTL_LUN_PEER_SC_PRIMARY) == 0 &&
		    (entry->flags & CTL_CMD_FLAG_OK_ON_UNAVAIL) == 0) {
			ctl_set_lun_transit(ctsio);
			retval = 1;
			goto bailout;
		}
		if (softc->ha_mode == CTL_HA_MODE_ACT_STBY &&
		    (entry->flags & CTL_CMD_FLAG_OK_ON_STANDBY) == 0) {
			ctl_set_lun_standby(ctsio);
			retval = 1;
			goto bailout;
		}

		/* The rest of checks are only done on executing side */
		if (softc->ha_mode == CTL_HA_MODE_XFER)
			goto bailout;
	}

	if (entry->pattern & CTL_LUN_PAT_WRITE) {
		if (lun->be_lun &&
		    lun->be_lun->flags & CTL_LUN_FLAG_READONLY) {
			ctl_set_hw_write_protected(ctsio);
			retval = 1;
			goto bailout;
		}
		if ((lun->MODE_CTRL.eca_and_aen & SCP_SWP) != 0) {
			ctl_set_sense(ctsio, /*current_error*/ 1,
			    /*sense_key*/ SSD_KEY_DATA_PROTECT,
			    /*asc*/ 0x27, /*ascq*/ 0x02, SSD_ELEM_NONE);
			retval = 1;
			goto bailout;
		}
	}

	/*
	 * Check for a reservation conflict.  If this command isn't allowed
	 * even on reserved LUNs, and if this initiator isn't the one who
	 * reserved us, reject the command with a reservation conflict.
	 */
	residx = ctl_get_initindex(&ctsio->io_hdr.nexus);
	if ((lun->flags & CTL_LUN_RESERVED)
	 && ((entry->flags & CTL_CMD_FLAG_ALLOW_ON_RESV) == 0)) {
		if (lun->res_idx != residx) {
			ctl_set_reservation_conflict(ctsio);
			retval = 1;
			goto bailout;
		}
	}

	if ((lun->flags & CTL_LUN_PR_RESERVED) == 0 ||
	    (entry->flags & CTL_CMD_FLAG_ALLOW_ON_PR_RESV)) {
		/* No reservation or command is allowed. */;
	} else if ((entry->flags & CTL_CMD_FLAG_ALLOW_ON_PR_WRESV) &&
	    (lun->pr_res_type == SPR_TYPE_WR_EX ||
	     lun->pr_res_type == SPR_TYPE_WR_EX_RO ||
	     lun->pr_res_type == SPR_TYPE_WR_EX_AR)) {
		/* The command is allowed for Write Exclusive resv. */;
	} else {
		/*
		 * if we aren't registered or it's a res holder type
		 * reservation and this isn't the res holder then set a
		 * conflict.
		 */
		if (ctl_get_prkey(lun, residx) == 0 ||
		    (residx != lun->pr_res_idx && lun->pr_res_type < 4)) {
			ctl_set_reservation_conflict(ctsio);
			retval = 1;
			goto bailout;
		}
	}

	if ((entry->flags & CTL_CMD_FLAG_OK_ON_NO_MEDIA) == 0) {
		if (lun->flags & CTL_LUN_EJECTED)
			ctl_set_lun_ejected(ctsio);
		else if (lun->flags & CTL_LUN_NO_MEDIA) {
			if (lun->flags & CTL_LUN_REMOVABLE)
				ctl_set_lun_no_media(ctsio);
			else
				ctl_set_lun_int_reqd(ctsio);
		} else if (lun->flags & CTL_LUN_STOPPED)
			ctl_set_lun_stopped(ctsio);
		else
			goto bailout;
		retval = 1;
		goto bailout;
	}

bailout:
	return (retval);
}

static void
ctl_failover_io(union ctl_io *io, int have_lock)
{
	ctl_set_busy(&io->scsiio);
	ctl_done(io);
}

static void
ctl_failover_lun(union ctl_io *rio)
{
	struct ctl_softc *softc = CTL_SOFTC(rio);
	struct ctl_lun *lun;
	struct ctl_io_hdr *io, *next_io;
	uint32_t targ_lun;

	targ_lun = rio->io_hdr.nexus.targ_mapped_lun;
	CTL_DEBUG_PRINT(("FAILOVER for lun %ju\n", targ_lun));

	/* Find and lock the LUN. */
	mtx_lock(&softc->ctl_lock);
	if (targ_lun > ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		return;
	}
	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);
	if (lun->flags & CTL_LUN_DISABLED) {
		mtx_unlock(&lun->lun_lock);
		return;
	}

	if (softc->ha_mode == CTL_HA_MODE_XFER) {
		TAILQ_FOREACH_SAFE(io, &lun->ooa_queue, ooa_links, next_io) {
			/* We are master */
			if (io->flags & CTL_FLAG_FROM_OTHER_SC) {
				if (io->flags & CTL_FLAG_IO_ACTIVE) {
					io->flags |= CTL_FLAG_ABORT;
					io->flags |= CTL_FLAG_FAILOVER;
					ctl_try_unblock_io(lun,
					    (union ctl_io *)io, FALSE);
				} else { /* This can be only due to DATAMOVE */
					io->msg_type = CTL_MSG_DATAMOVE_DONE;
					io->flags &= ~CTL_FLAG_DMA_INPROG;
					io->flags |= CTL_FLAG_IO_ACTIVE;
					io->port_status = 31340;
					ctl_enqueue_isc((union ctl_io *)io);
				}
			} else
			/* We are slave */
			if (io->flags & CTL_FLAG_SENT_2OTHER_SC) {
				io->flags &= ~CTL_FLAG_SENT_2OTHER_SC;
				if (io->flags & CTL_FLAG_IO_ACTIVE) {
					io->flags |= CTL_FLAG_FAILOVER;
				} else {
					ctl_set_busy(&((union ctl_io *)io)->
					    scsiio);
					ctl_done((union ctl_io *)io);
				}
			}
		}
	} else { /* SERIALIZE modes */
		TAILQ_FOREACH_SAFE(io, &lun->ooa_queue, ooa_links, next_io) {
			/* We are master */
			if (io->flags & CTL_FLAG_FROM_OTHER_SC) {
				if (io->blocker != NULL) {
					TAILQ_REMOVE(&io->blocker->io_hdr.blocked_queue,
					    io, blocked_links);
					io->blocker = NULL;
				}
				ctl_try_unblock_others(lun, (union ctl_io *)io,
				    TRUE);
				TAILQ_REMOVE(&lun->ooa_queue, io, ooa_links);
				ctl_free_io((union ctl_io *)io);
			} else
			/* We are slave */
			if (io->flags & CTL_FLAG_SENT_2OTHER_SC) {
				io->flags &= ~CTL_FLAG_SENT_2OTHER_SC;
				if (!(io->flags & CTL_FLAG_IO_ACTIVE)) {
					ctl_set_busy(&((union ctl_io *)io)->
					    scsiio);
					ctl_done((union ctl_io *)io);
				}
			}
		}
	}
	mtx_unlock(&lun->lun_lock);
}

static int
ctl_scsiio_precheck(struct ctl_softc *softc, struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun;
	const struct ctl_cmd_entry *entry;
	union ctl_io *bio;
	uint32_t initidx, targ_lun;
	int retval = 0;

	lun = NULL;
	targ_lun = ctsio->io_hdr.nexus.targ_mapped_lun;
	if (targ_lun < ctl_max_luns)
		lun = softc->ctl_luns[targ_lun];
	if (lun) {
		/*
		 * If the LUN is invalid, pretend that it doesn't exist.
		 * It will go away as soon as all pending I/O has been
		 * completed.
		 */
		mtx_lock(&lun->lun_lock);
		if (lun->flags & CTL_LUN_DISABLED) {
			mtx_unlock(&lun->lun_lock);
			lun = NULL;
		}
	}
	CTL_LUN(ctsio) = lun;
	if (lun) {
		CTL_BACKEND_LUN(ctsio) = lun->be_lun;

		/*
		 * Every I/O goes into the OOA queue for a particular LUN,
		 * and stays there until completion.
		 */
#ifdef CTL_TIME_IO
		if (TAILQ_EMPTY(&lun->ooa_queue))
			lun->idle_time += getsbinuptime() - lun->last_busy;
#endif
		TAILQ_INSERT_TAIL(&lun->ooa_queue, &ctsio->io_hdr, ooa_links);
	}

	/* Get command entry and return error if it is unsuppotyed. */
	entry = ctl_validate_command(ctsio);
	if (entry == NULL) {
		if (lun)
			mtx_unlock(&lun->lun_lock);
		return (retval);
	}

	ctsio->io_hdr.flags &= ~CTL_FLAG_DATA_MASK;
	ctsio->io_hdr.flags |= entry->flags & CTL_FLAG_DATA_MASK;

	/*
	 * Check to see whether we can send this command to LUNs that don't
	 * exist.  This should pretty much only be the case for inquiry
	 * and request sense.  Further checks, below, really require having
	 * a LUN, so we can't really check the command anymore.  Just put
	 * it on the rtr queue.
	 */
	if (lun == NULL) {
		if (entry->flags & CTL_CMD_FLAG_OK_ON_NO_LUN) {
			ctsio->io_hdr.flags |= CTL_FLAG_IS_WAS_ON_RTR;
			ctl_enqueue_rtr((union ctl_io *)ctsio);
			return (retval);
		}

		ctl_set_unsupported_lun(ctsio);
		ctl_done((union ctl_io *)ctsio);
		CTL_DEBUG_PRINT(("ctl_scsiio_precheck: bailing out due to invalid LUN\n"));
		return (retval);
	} else {
		/*
		 * Make sure we support this particular command on this LUN.
		 * e.g., we don't support writes to the control LUN.
		 */
		if (!ctl_cmd_applicable(lun->be_lun->lun_type, entry)) {
			mtx_unlock(&lun->lun_lock);
			ctl_set_invalid_opcode(ctsio);
			ctl_done((union ctl_io *)ctsio);
			return (retval);
		}
	}

	initidx = ctl_get_initindex(&ctsio->io_hdr.nexus);

	/*
	 * If we've got a request sense, it'll clear the contingent
	 * allegiance condition.  Otherwise, if we have a CA condition for
	 * this initiator, clear it, because it sent down a command other
	 * than request sense.
	 */
	if (ctsio->cdb[0] != REQUEST_SENSE) {
		struct scsi_sense_data *ps;

		ps = lun->pending_sense[initidx / CTL_MAX_INIT_PER_PORT];
		if (ps != NULL)
			ps[initidx % CTL_MAX_INIT_PER_PORT].error_code = 0;
	}

	/*
	 * If the command has this flag set, it handles its own unit
	 * attention reporting, we shouldn't do anything.  Otherwise we
	 * check for any pending unit attentions, and send them back to the
	 * initiator.  We only do this when a command initially comes in,
	 * not when we pull it off the blocked queue.
	 *
	 * According to SAM-3, section 5.3.2, the order that things get
	 * presented back to the host is basically unit attentions caused
	 * by some sort of reset event, busy status, reservation conflicts
	 * or task set full, and finally any other status.
	 *
	 * One issue here is that some of the unit attentions we report
	 * don't fall into the "reset" category (e.g. "reported luns data
	 * has changed").  So reporting it here, before the reservation
	 * check, may be technically wrong.  I guess the only thing to do
	 * would be to check for and report the reset events here, and then
	 * check for the other unit attention types after we check for a
	 * reservation conflict.
	 *
	 * XXX KDM need to fix this
	 */
	if ((entry->flags & CTL_CMD_FLAG_NO_SENSE) == 0) {
		ctl_ua_type ua_type;
		u_int sense_len = 0;

		ua_type = ctl_build_ua(lun, initidx, &ctsio->sense_data,
		    &sense_len, SSD_TYPE_NONE);
		if (ua_type != CTL_UA_NONE) {
			mtx_unlock(&lun->lun_lock);
			ctsio->scsi_status = SCSI_STATUS_CHECK_COND;
			ctsio->io_hdr.status = CTL_SCSI_ERROR | CTL_AUTOSENSE;
			ctsio->sense_len = sense_len;
			ctl_done((union ctl_io *)ctsio);
			return (retval);
		}
	}


	if (ctl_scsiio_lun_check(lun, entry, ctsio) != 0) {
		mtx_unlock(&lun->lun_lock);
		ctl_done((union ctl_io *)ctsio);
		return (retval);
	}

	/*
	 * XXX CHD this is where we want to send IO to other side if
	 * this LUN is secondary on this SC. We will need to make a copy
	 * of the IO and flag the IO on this side as SENT_2OTHER and the flag
	 * the copy we send as FROM_OTHER.
	 * We also need to stuff the address of the original IO so we can
	 * find it easily. Something similar will need be done on the other
	 * side so when we are done we can find the copy.
	 */
	if ((lun->flags & CTL_LUN_PRIMARY_SC) == 0 &&
	    (lun->flags & CTL_LUN_PEER_SC_PRIMARY) != 0 &&
	    (entry->flags & CTL_CMD_FLAG_RUN_HERE) == 0) {
		union ctl_ha_msg msg_info;
		int isc_retval;

		ctsio->io_hdr.flags |= CTL_FLAG_SENT_2OTHER_SC;
		ctsio->io_hdr.flags &= ~CTL_FLAG_IO_ACTIVE;
		mtx_unlock(&lun->lun_lock);

		msg_info.hdr.msg_type = CTL_MSG_SERIALIZE;
		msg_info.hdr.original_sc = (union ctl_io *)ctsio;
		msg_info.hdr.serializing_sc = NULL;
		msg_info.hdr.nexus = ctsio->io_hdr.nexus;
		msg_info.scsi.tag_num = ctsio->tag_num;
		msg_info.scsi.tag_type = ctsio->tag_type;
		msg_info.scsi.cdb_len = ctsio->cdb_len;
		memcpy(msg_info.scsi.cdb, ctsio->cdb, CTL_MAX_CDBLEN);

		if ((isc_retval = ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg_info,
		    sizeof(msg_info.scsi) - sizeof(msg_info.scsi.sense_data),
		    M_WAITOK)) > CTL_HA_STATUS_SUCCESS) {
			ctl_set_busy(ctsio);
			ctl_done((union ctl_io *)ctsio);
			return (retval);
		}
		return (retval);
	}

	bio = (union ctl_io *)TAILQ_PREV(&ctsio->io_hdr, ctl_ooaq, ooa_links);
	switch (ctl_check_ooa(lun, (union ctl_io *)ctsio, &bio)) {
	case CTL_ACTION_BLOCK:
		ctsio->io_hdr.blocker = bio;
		TAILQ_INSERT_TAIL(&bio->io_hdr.blocked_queue, &ctsio->io_hdr,
				  blocked_links);
		mtx_unlock(&lun->lun_lock);
		return (retval);
	case CTL_ACTION_PASS:
	case CTL_ACTION_SKIP:
		ctsio->io_hdr.flags |= CTL_FLAG_IS_WAS_ON_RTR;
		mtx_unlock(&lun->lun_lock);
		ctl_enqueue_rtr((union ctl_io *)ctsio);
		break;
	case CTL_ACTION_OVERLAP:
		mtx_unlock(&lun->lun_lock);
		ctl_set_overlapped_cmd(ctsio);
		ctl_done((union ctl_io *)ctsio);
		break;
	case CTL_ACTION_OVERLAP_TAG:
		mtx_unlock(&lun->lun_lock);
		ctl_set_overlapped_tag(ctsio, ctsio->tag_num & 0xff);
		ctl_done((union ctl_io *)ctsio);
		break;
	case CTL_ACTION_ERROR:
	default:
		mtx_unlock(&lun->lun_lock);
		ctl_set_internal_failure(ctsio,
					 /*sks_valid*/ 0,
					 /*retry_count*/ 0);
		ctl_done((union ctl_io *)ctsio);
		break;
	}
	return (retval);
}

const struct ctl_cmd_entry *
ctl_get_cmd_entry(struct ctl_scsiio *ctsio, int *sa)
{
	const struct ctl_cmd_entry *entry;
	int service_action;

	entry = &ctl_cmd_table[ctsio->cdb[0]];
	if (sa)
		*sa = ((entry->flags & CTL_CMD_FLAG_SA5) != 0);
	if (entry->flags & CTL_CMD_FLAG_SA5) {
		service_action = ctsio->cdb[1] & SERVICE_ACTION_MASK;
		entry = &((const struct ctl_cmd_entry *)
		    entry->execute)[service_action];
	}
	return (entry);
}

const struct ctl_cmd_entry *
ctl_validate_command(struct ctl_scsiio *ctsio)
{
	const struct ctl_cmd_entry *entry;
	int i, sa;
	uint8_t diff;

	entry = ctl_get_cmd_entry(ctsio, &sa);
	if (entry->execute == NULL) {
		if (sa)
			ctl_set_invalid_field(ctsio,
					      /*sks_valid*/ 1,
					      /*command*/ 1,
					      /*field*/ 1,
					      /*bit_valid*/ 1,
					      /*bit*/ 4);
		else
			ctl_set_invalid_opcode(ctsio);
		ctl_done((union ctl_io *)ctsio);
		return (NULL);
	}
	KASSERT(entry->length > 0,
	    ("Not defined length for command 0x%02x/0x%02x",
	     ctsio->cdb[0], ctsio->cdb[1]));
	for (i = 1; i < entry->length; i++) {
		diff = ctsio->cdb[i] & ~entry->usage[i - 1];
		if (diff == 0)
			continue;
		ctl_set_invalid_field(ctsio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ i,
				      /*bit_valid*/ 1,
				      /*bit*/ fls(diff) - 1);
		ctl_done((union ctl_io *)ctsio);
		return (NULL);
	}
	return (entry);
}

static int
ctl_cmd_applicable(uint8_t lun_type, const struct ctl_cmd_entry *entry)
{

	switch (lun_type) {
	case T_DIRECT:
		if ((entry->flags & CTL_CMD_FLAG_OK_ON_DIRECT) == 0)
			return (0);
		break;
	case T_PROCESSOR:
		if ((entry->flags & CTL_CMD_FLAG_OK_ON_PROC) == 0)
			return (0);
		break;
	case T_CDROM:
		if ((entry->flags & CTL_CMD_FLAG_OK_ON_CDROM) == 0)
			return (0);
		break;
	default:
		return (0);
	}
	return (1);
}

static int
ctl_scsiio(struct ctl_scsiio *ctsio)
{
	int retval;
	const struct ctl_cmd_entry *entry;

	retval = CTL_RETVAL_COMPLETE;

	CTL_DEBUG_PRINT(("ctl_scsiio cdb[0]=%02X\n", ctsio->cdb[0]));

	entry = ctl_get_cmd_entry(ctsio, NULL);

	/*
	 * If this I/O has been aborted, just send it straight to
	 * ctl_done() without executing it.
	 */
	if (ctsio->io_hdr.flags & CTL_FLAG_ABORT) {
		ctl_done((union ctl_io *)ctsio);
		goto bailout;
	}

	/*
	 * All the checks should have been handled by ctl_scsiio_precheck().
	 * We should be clear now to just execute the I/O.
	 */
	retval = entry->execute(ctsio);

bailout:
	return (retval);
}

static int
ctl_target_reset(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_port *port = CTL_PORT(io);
	struct ctl_lun *lun;
	uint32_t initidx;
	ctl_ua_type ua_type;

	if (!(io->io_hdr.flags & CTL_FLAG_FROM_OTHER_SC)) {
		union ctl_ha_msg msg_info;

		msg_info.hdr.nexus = io->io_hdr.nexus;
		msg_info.task.task_action = io->taskio.task_action;
		msg_info.hdr.msg_type = CTL_MSG_MANAGE_TASKS;
		msg_info.hdr.original_sc = NULL;
		msg_info.hdr.serializing_sc = NULL;
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg_info,
		    sizeof(msg_info.task), M_WAITOK);
	}

	initidx = ctl_get_initindex(&io->io_hdr.nexus);
	if (io->taskio.task_action == CTL_TASK_TARGET_RESET)
		ua_type = CTL_UA_TARG_RESET;
	else
		ua_type = CTL_UA_BUS_RESET;
	mtx_lock(&softc->ctl_lock);
	STAILQ_FOREACH(lun, &softc->lun_list, links) {
		if (port != NULL &&
		    ctl_lun_map_to_port(port, lun->lun) == UINT32_MAX)
			continue;
		ctl_do_lun_reset(lun, initidx, ua_type);
	}
	mtx_unlock(&softc->ctl_lock);
	io->taskio.task_status = CTL_TASK_FUNCTION_COMPLETE;
	return (0);
}

/*
 * The LUN should always be set.  The I/O is optional, and is used to
 * distinguish between I/Os sent by this initiator, and by other
 * initiators.  We set unit attention for initiators other than this one.
 * SAM-3 is vague on this point.  It does say that a unit attention should
 * be established for other initiators when a LUN is reset (see section
 * 5.7.3), but it doesn't specifically say that the unit attention should
 * be established for this particular initiator when a LUN is reset.  Here
 * is the relevant text, from SAM-3 rev 8:
 *
 * 5.7.2 When a SCSI initiator port aborts its own tasks
 *
 * When a SCSI initiator port causes its own task(s) to be aborted, no
 * notification that the task(s) have been aborted shall be returned to
 * the SCSI initiator port other than the completion response for the
 * command or task management function action that caused the task(s) to
 * be aborted and notification(s) associated with related effects of the
 * action (e.g., a reset unit attention condition).
 *
 * XXX KDM for now, we're setting unit attention for all initiators.
 */
static void
ctl_do_lun_reset(struct ctl_lun *lun, uint32_t initidx, ctl_ua_type ua_type)
{
	union ctl_io *xio;
	int i;

	mtx_lock(&lun->lun_lock);
	/* Abort tasks. */
	for (xio = (union ctl_io *)TAILQ_FIRST(&lun->ooa_queue); xio != NULL;
	     xio = (union ctl_io *)TAILQ_NEXT(&xio->io_hdr, ooa_links)) {
		xio->io_hdr.flags |= CTL_FLAG_ABORT | CTL_FLAG_ABORT_STATUS;
		ctl_try_unblock_io(lun, xio, FALSE);
	}
	/* Clear CA. */
	for (i = 0; i < ctl_max_ports; i++) {
		free(lun->pending_sense[i], M_CTL);
		lun->pending_sense[i] = NULL;
	}
	/* Clear reservation. */
	lun->flags &= ~CTL_LUN_RESERVED;
	/* Clear prevent media removal. */
	if (lun->prevent) {
		for (i = 0; i < CTL_MAX_INITIATORS; i++)
			ctl_clear_mask(lun->prevent, i);
		lun->prevent_count = 0;
	}
	/* Clear TPC status */
	ctl_tpc_lun_clear(lun, -1);
	/* Establish UA. */
#if 0
	ctl_est_ua_all(lun, initidx, ua_type);
#else
	ctl_est_ua_all(lun, -1, ua_type);
#endif
	mtx_unlock(&lun->lun_lock);
}

static int
ctl_lun_reset(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_lun *lun;
	uint32_t targ_lun, initidx;

	targ_lun = io->io_hdr.nexus.targ_mapped_lun;
	initidx = ctl_get_initindex(&io->io_hdr.nexus);
	mtx_lock(&softc->ctl_lock);
	if (targ_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		io->taskio.task_status = CTL_TASK_LUN_DOES_NOT_EXIST;
		return (1);
	}
	ctl_do_lun_reset(lun, initidx, CTL_UA_LUN_RESET);
	mtx_unlock(&softc->ctl_lock);
	io->taskio.task_status = CTL_TASK_FUNCTION_COMPLETE;

	if ((io->io_hdr.flags & CTL_FLAG_FROM_OTHER_SC) == 0) {
		union ctl_ha_msg msg_info;

		msg_info.hdr.msg_type = CTL_MSG_MANAGE_TASKS;
		msg_info.hdr.nexus = io->io_hdr.nexus;
		msg_info.task.task_action = CTL_TASK_LUN_RESET;
		msg_info.hdr.original_sc = NULL;
		msg_info.hdr.serializing_sc = NULL;
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg_info,
		    sizeof(msg_info.task), M_WAITOK);
	}
	return (0);
}

static void
ctl_abort_tasks_lun(struct ctl_lun *lun, uint32_t targ_port, uint32_t init_id,
    int other_sc)
{
	union ctl_io *xio;

	mtx_assert(&lun->lun_lock, MA_OWNED);

	/*
	 * Run through the OOA queue and attempt to find the given I/O.
	 * The target port, initiator ID, tag type and tag number have to
	 * match the values that we got from the initiator.  If we have an
	 * untagged command to abort, simply abort the first untagged command
	 * we come to.  We only allow one untagged command at a time of course.
	 */
	for (xio = (union ctl_io *)TAILQ_FIRST(&lun->ooa_queue); xio != NULL;
	     xio = (union ctl_io *)TAILQ_NEXT(&xio->io_hdr, ooa_links)) {

		if ((targ_port == UINT32_MAX ||
		     targ_port == xio->io_hdr.nexus.targ_port) &&
		    (init_id == UINT32_MAX ||
		     init_id == xio->io_hdr.nexus.initid)) {
			if (targ_port != xio->io_hdr.nexus.targ_port ||
			    init_id != xio->io_hdr.nexus.initid)
				xio->io_hdr.flags |= CTL_FLAG_ABORT_STATUS;
			xio->io_hdr.flags |= CTL_FLAG_ABORT;
			if (!other_sc && !(lun->flags & CTL_LUN_PRIMARY_SC)) {
				union ctl_ha_msg msg_info;

				msg_info.hdr.nexus = xio->io_hdr.nexus;
				msg_info.task.task_action = CTL_TASK_ABORT_TASK;
				msg_info.task.tag_num = xio->scsiio.tag_num;
				msg_info.task.tag_type = xio->scsiio.tag_type;
				msg_info.hdr.msg_type = CTL_MSG_MANAGE_TASKS;
				msg_info.hdr.original_sc = NULL;
				msg_info.hdr.serializing_sc = NULL;
				ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg_info,
				    sizeof(msg_info.task), M_NOWAIT);
			}
			ctl_try_unblock_io(lun, xio, FALSE);
		}
	}
}

static int
ctl_abort_task_set(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_lun *lun;
	uint32_t targ_lun;

	/*
	 * Look up the LUN.
	 */
	targ_lun = io->io_hdr.nexus.targ_mapped_lun;
	mtx_lock(&softc->ctl_lock);
	if (targ_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		io->taskio.task_status = CTL_TASK_LUN_DOES_NOT_EXIST;
		return (1);
	}

	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);
	if (io->taskio.task_action == CTL_TASK_ABORT_TASK_SET) {
		ctl_abort_tasks_lun(lun, io->io_hdr.nexus.targ_port,
		    io->io_hdr.nexus.initid,
		    (io->io_hdr.flags & CTL_FLAG_FROM_OTHER_SC) != 0);
	} else { /* CTL_TASK_CLEAR_TASK_SET */
		ctl_abort_tasks_lun(lun, UINT32_MAX, UINT32_MAX,
		    (io->io_hdr.flags & CTL_FLAG_FROM_OTHER_SC) != 0);
	}
	mtx_unlock(&lun->lun_lock);
	io->taskio.task_status = CTL_TASK_FUNCTION_COMPLETE;
	return (0);
}

static void
ctl_i_t_nexus_loss(struct ctl_softc *softc, uint32_t initidx,
    ctl_ua_type ua_type)
{
	struct ctl_lun *lun;
	struct scsi_sense_data *ps;
	uint32_t p, i;

	p = initidx / CTL_MAX_INIT_PER_PORT;
	i = initidx % CTL_MAX_INIT_PER_PORT;
	mtx_lock(&softc->ctl_lock);
	STAILQ_FOREACH(lun, &softc->lun_list, links) {
		mtx_lock(&lun->lun_lock);
		/* Abort tasks. */
		ctl_abort_tasks_lun(lun, p, i, 1);
		/* Clear CA. */
		ps = lun->pending_sense[p];
		if (ps != NULL)
			ps[i].error_code = 0;
		/* Clear reservation. */
		if ((lun->flags & CTL_LUN_RESERVED) && (lun->res_idx == initidx))
			lun->flags &= ~CTL_LUN_RESERVED;
		/* Clear prevent media removal. */
		if (lun->prevent && ctl_is_set(lun->prevent, initidx)) {
			ctl_clear_mask(lun->prevent, initidx);
			lun->prevent_count--;
		}
		/* Clear TPC status */
		ctl_tpc_lun_clear(lun, initidx);
		/* Establish UA. */
		ctl_est_ua(lun, initidx, ua_type);
		mtx_unlock(&lun->lun_lock);
	}
	mtx_unlock(&softc->ctl_lock);
}

static int
ctl_i_t_nexus_reset(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	uint32_t initidx;

	if (!(io->io_hdr.flags & CTL_FLAG_FROM_OTHER_SC)) {
		union ctl_ha_msg msg_info;

		msg_info.hdr.nexus = io->io_hdr.nexus;
		msg_info.task.task_action = CTL_TASK_I_T_NEXUS_RESET;
		msg_info.hdr.msg_type = CTL_MSG_MANAGE_TASKS;
		msg_info.hdr.original_sc = NULL;
		msg_info.hdr.serializing_sc = NULL;
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg_info,
		    sizeof(msg_info.task), M_WAITOK);
	}

	initidx = ctl_get_initindex(&io->io_hdr.nexus);
	ctl_i_t_nexus_loss(softc, initidx, CTL_UA_I_T_NEXUS_LOSS);
	io->taskio.task_status = CTL_TASK_FUNCTION_COMPLETE;
	return (0);
}

static int
ctl_abort_task(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	union ctl_io *xio;
	struct ctl_lun *lun;
	uint32_t targ_lun;

	/*
	 * Look up the LUN.
	 */
	targ_lun = io->io_hdr.nexus.targ_mapped_lun;
	mtx_lock(&softc->ctl_lock);
	if (targ_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		io->taskio.task_status = CTL_TASK_LUN_DOES_NOT_EXIST;
		return (1);
	}

	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);
	/*
	 * Run through the OOA queue and attempt to find the given I/O.
	 * The target port, initiator ID, tag type and tag number have to
	 * match the values that we got from the initiator.  If we have an
	 * untagged command to abort, simply abort the first untagged command
	 * we come to.  We only allow one untagged command at a time of course.
	 */
	for (xio = (union ctl_io *)TAILQ_FIRST(&lun->ooa_queue); xio != NULL;
	     xio = (union ctl_io *)TAILQ_NEXT(&xio->io_hdr, ooa_links)) {

		if ((xio->io_hdr.nexus.targ_port != io->io_hdr.nexus.targ_port)
		 || (xio->io_hdr.nexus.initid != io->io_hdr.nexus.initid)
		 || (xio->io_hdr.flags & CTL_FLAG_ABORT))
			continue;

		/*
		 * If the abort says that the task is untagged, the
		 * task in the queue must be untagged.  Otherwise,
		 * we just check to see whether the tag numbers
		 * match.  This is because the QLogic firmware
		 * doesn't pass back the tag type in an abort
		 * request.
		 */
#if 0
		if (((xio->scsiio.tag_type == CTL_TAG_UNTAGGED)
		  && (io->taskio.tag_type == CTL_TAG_UNTAGGED))
		 || (xio->scsiio.tag_num == io->taskio.tag_num)) {
#else
		/*
		 * XXX KDM we've got problems with FC, because it
		 * doesn't send down a tag type with aborts.  So we
		 * can only really go by the tag number...
		 * This may cause problems with parallel SCSI.
		 * Need to figure that out!!
		 */
		if (xio->scsiio.tag_num == io->taskio.tag_num) {
#endif
			xio->io_hdr.flags |= CTL_FLAG_ABORT;
			if ((io->io_hdr.flags & CTL_FLAG_FROM_OTHER_SC) == 0 &&
			    !(lun->flags & CTL_LUN_PRIMARY_SC)) {
				union ctl_ha_msg msg_info;

				msg_info.hdr.nexus = io->io_hdr.nexus;
				msg_info.task.task_action = CTL_TASK_ABORT_TASK;
				msg_info.task.tag_num = io->taskio.tag_num;
				msg_info.task.tag_type = io->taskio.tag_type;
				msg_info.hdr.msg_type = CTL_MSG_MANAGE_TASKS;
				msg_info.hdr.original_sc = NULL;
				msg_info.hdr.serializing_sc = NULL;
				ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg_info,
				    sizeof(msg_info.task), M_NOWAIT);
			}
			ctl_try_unblock_io(lun, xio, FALSE);
		}
	}
	mtx_unlock(&lun->lun_lock);
	io->taskio.task_status = CTL_TASK_FUNCTION_COMPLETE;
	return (0);
}

static int
ctl_query_task(union ctl_io *io, int task_set)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	union ctl_io *xio;
	struct ctl_lun *lun;
	int found = 0;
	uint32_t targ_lun;

	targ_lun = io->io_hdr.nexus.targ_mapped_lun;
	mtx_lock(&softc->ctl_lock);
	if (targ_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		io->taskio.task_status = CTL_TASK_LUN_DOES_NOT_EXIST;
		return (1);
	}
	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);
	for (xio = (union ctl_io *)TAILQ_FIRST(&lun->ooa_queue); xio != NULL;
	     xio = (union ctl_io *)TAILQ_NEXT(&xio->io_hdr, ooa_links)) {

		if ((xio->io_hdr.nexus.targ_port != io->io_hdr.nexus.targ_port)
		 || (xio->io_hdr.nexus.initid != io->io_hdr.nexus.initid)
		 || (xio->io_hdr.flags & CTL_FLAG_ABORT))
			continue;

		if (task_set || xio->scsiio.tag_num == io->taskio.tag_num) {
			found = 1;
			break;
		}
	}
	mtx_unlock(&lun->lun_lock);
	if (found)
		io->taskio.task_status = CTL_TASK_FUNCTION_SUCCEEDED;
	else
		io->taskio.task_status = CTL_TASK_FUNCTION_COMPLETE;
	return (0);
}

static int
ctl_query_async_event(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_lun *lun;
	ctl_ua_type ua;
	uint32_t targ_lun, initidx;

	targ_lun = io->io_hdr.nexus.targ_mapped_lun;
	mtx_lock(&softc->ctl_lock);
	if (targ_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		io->taskio.task_status = CTL_TASK_LUN_DOES_NOT_EXIST;
		return (1);
	}
	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);
	initidx = ctl_get_initindex(&io->io_hdr.nexus);
	ua = ctl_build_qae(lun, initidx, io->taskio.task_resp);
	mtx_unlock(&lun->lun_lock);
	if (ua != CTL_UA_NONE)
		io->taskio.task_status = CTL_TASK_FUNCTION_SUCCEEDED;
	else
		io->taskio.task_status = CTL_TASK_FUNCTION_COMPLETE;
	return (0);
}

static void
ctl_run_task(union ctl_io *io)
{
	int retval = 1;

	CTL_DEBUG_PRINT(("ctl_run_task\n"));
	KASSERT(io->io_hdr.io_type == CTL_IO_TASK,
	    ("ctl_run_task: Unextected io_type %d\n", io->io_hdr.io_type));
	io->taskio.task_status = CTL_TASK_FUNCTION_NOT_SUPPORTED;
	bzero(io->taskio.task_resp, sizeof(io->taskio.task_resp));
	switch (io->taskio.task_action) {
	case CTL_TASK_ABORT_TASK:
		retval = ctl_abort_task(io);
		break;
	case CTL_TASK_ABORT_TASK_SET:
	case CTL_TASK_CLEAR_TASK_SET:
		retval = ctl_abort_task_set(io);
		break;
	case CTL_TASK_CLEAR_ACA:
		break;
	case CTL_TASK_I_T_NEXUS_RESET:
		retval = ctl_i_t_nexus_reset(io);
		break;
	case CTL_TASK_LUN_RESET:
		retval = ctl_lun_reset(io);
		break;
	case CTL_TASK_TARGET_RESET:
	case CTL_TASK_BUS_RESET:
		retval = ctl_target_reset(io);
		break;
	case CTL_TASK_PORT_LOGIN:
		break;
	case CTL_TASK_PORT_LOGOUT:
		break;
	case CTL_TASK_QUERY_TASK:
		retval = ctl_query_task(io, 0);
		break;
	case CTL_TASK_QUERY_TASK_SET:
		retval = ctl_query_task(io, 1);
		break;
	case CTL_TASK_QUERY_ASYNC_EVENT:
		retval = ctl_query_async_event(io);
		break;
	default:
		printf("%s: got unknown task management event %d\n",
		       __func__, io->taskio.task_action);
		break;
	}
	if (retval == 0)
		io->io_hdr.status = CTL_SUCCESS;
	else
		io->io_hdr.status = CTL_ERROR;
	ctl_done(io);
}

/*
 * For HA operation.  Handle commands that come in from the other
 * controller.
 */
static void
ctl_handle_isc(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_lun *lun;
	const struct ctl_cmd_entry *entry;
	uint32_t targ_lun;

	targ_lun = io->io_hdr.nexus.targ_mapped_lun;
	switch (io->io_hdr.msg_type) {
	case CTL_MSG_SERIALIZE:
		ctl_serialize_other_sc_cmd(&io->scsiio);
		break;
	case CTL_MSG_R2R:		/* Only used in SER_ONLY mode. */
		entry = ctl_get_cmd_entry(&io->scsiio, NULL);
		if (targ_lun >= ctl_max_luns ||
		    (lun = softc->ctl_luns[targ_lun]) == NULL) {
			ctl_done(io);
			break;
		}
		mtx_lock(&lun->lun_lock);
		if (ctl_scsiio_lun_check(lun, entry, &io->scsiio) != 0) {
			mtx_unlock(&lun->lun_lock);
			ctl_done(io);
			break;
		}
		io->io_hdr.flags |= CTL_FLAG_IS_WAS_ON_RTR;
		mtx_unlock(&lun->lun_lock);
		ctl_enqueue_rtr(io);
		break;
	case CTL_MSG_FINISH_IO:
		if (softc->ha_mode == CTL_HA_MODE_XFER) {
			ctl_done(io);
			break;
		}
		if (targ_lun >= ctl_max_luns ||
		    (lun = softc->ctl_luns[targ_lun]) == NULL) {
			ctl_free_io(io);
			break;
		}
		mtx_lock(&lun->lun_lock);
		ctl_try_unblock_others(lun, io, TRUE);
		TAILQ_REMOVE(&lun->ooa_queue, &io->io_hdr, ooa_links);
		mtx_unlock(&lun->lun_lock);
		ctl_free_io(io);
		break;
	case CTL_MSG_PERS_ACTION:
		ctl_hndl_per_res_out_on_other_sc(io);
		ctl_free_io(io);
		break;
	case CTL_MSG_BAD_JUJU:
		ctl_done(io);
		break;
	case CTL_MSG_DATAMOVE:		/* Only used in XFER mode */
		ctl_datamove_remote(io);
		break;
	case CTL_MSG_DATAMOVE_DONE:	/* Only used in XFER mode */
		io->scsiio.be_move_done(io);
		break;
	case CTL_MSG_FAILOVER:
		ctl_failover_lun(io);
		ctl_free_io(io);
		break;
	default:
		printf("%s: Invalid message type %d\n",
		       __func__, io->io_hdr.msg_type);
		ctl_free_io(io);
		break;
	}

}


/*
 * Returns the match type in the case of a match, or CTL_LUN_PAT_NONE if
 * there is no match.
 */
static ctl_lun_error_pattern
ctl_cmd_pattern_match(struct ctl_scsiio *ctsio, struct ctl_error_desc *desc)
{
	const struct ctl_cmd_entry *entry;
	ctl_lun_error_pattern filtered_pattern, pattern;

	pattern = desc->error_pattern;

	/*
	 * XXX KDM we need more data passed into this function to match a
	 * custom pattern, and we actually need to implement custom pattern
	 * matching.
	 */
	if (pattern & CTL_LUN_PAT_CMD)
		return (CTL_LUN_PAT_CMD);

	if ((pattern & CTL_LUN_PAT_MASK) == CTL_LUN_PAT_ANY)
		return (CTL_LUN_PAT_ANY);

	entry = ctl_get_cmd_entry(ctsio, NULL);

	filtered_pattern = entry->pattern & pattern;

	/*
	 * If the user requested specific flags in the pattern (e.g.
	 * CTL_LUN_PAT_RANGE), make sure the command supports all of those
	 * flags.
	 *
	 * If the user did not specify any flags, it doesn't matter whether
	 * or not the command supports the flags.
	 */
	if ((filtered_pattern & ~CTL_LUN_PAT_MASK) !=
	     (pattern & ~CTL_LUN_PAT_MASK))
		return (CTL_LUN_PAT_NONE);

	/*
	 * If the user asked for a range check, see if the requested LBA
	 * range overlaps with this command's LBA range.
	 */
	if (filtered_pattern & CTL_LUN_PAT_RANGE) {
		uint64_t lba1;
		uint64_t len1;
		ctl_action action;
		int retval;

		retval = ctl_get_lba_len((union ctl_io *)ctsio, &lba1, &len1);
		if (retval != 0)
			return (CTL_LUN_PAT_NONE);

		action = ctl_extent_check_lba(lba1, len1, desc->lba_range.lba,
					      desc->lba_range.len, FALSE);
		/*
		 * A "pass" means that the LBA ranges don't overlap, so
		 * this doesn't match the user's range criteria.
		 */
		if (action == CTL_ACTION_PASS)
			return (CTL_LUN_PAT_NONE);
	}

	return (filtered_pattern);
}

static void
ctl_inject_error(struct ctl_lun *lun, union ctl_io *io)
{
	struct ctl_error_desc *desc, *desc2;

	mtx_assert(&lun->lun_lock, MA_OWNED);

	STAILQ_FOREACH_SAFE(desc, &lun->error_list, links, desc2) {
		ctl_lun_error_pattern pattern;
		/*
		 * Check to see whether this particular command matches
		 * the pattern in the descriptor.
		 */
		pattern = ctl_cmd_pattern_match(&io->scsiio, desc);
		if ((pattern & CTL_LUN_PAT_MASK) == CTL_LUN_PAT_NONE)
			continue;

		switch (desc->lun_error & CTL_LUN_INJ_TYPE) {
		case CTL_LUN_INJ_ABORTED:
			ctl_set_aborted(&io->scsiio);
			break;
		case CTL_LUN_INJ_MEDIUM_ERR:
			ctl_set_medium_error(&io->scsiio,
			    (io->io_hdr.flags & CTL_FLAG_DATA_MASK) !=
			     CTL_FLAG_DATA_OUT);
			break;
		case CTL_LUN_INJ_UA:
			/* 29h/00h  POWER ON, RESET, OR BUS DEVICE RESET
			 * OCCURRED */
			ctl_set_ua(&io->scsiio, 0x29, 0x00);
			break;
		case CTL_LUN_INJ_CUSTOM:
			/*
			 * We're assuming the user knows what he is doing.
			 * Just copy the sense information without doing
			 * checks.
			 */
			bcopy(&desc->custom_sense, &io->scsiio.sense_data,
			      MIN(sizeof(desc->custom_sense),
				  sizeof(io->scsiio.sense_data)));
			io->scsiio.scsi_status = SCSI_STATUS_CHECK_COND;
			io->scsiio.sense_len = SSD_FULL_SIZE;
			io->io_hdr.status = CTL_SCSI_ERROR | CTL_AUTOSENSE;
			break;
		case CTL_LUN_INJ_NONE:
		default:
			/*
			 * If this is an error injection type we don't know
			 * about, clear the continuous flag (if it is set)
			 * so it will get deleted below.
			 */
			desc->lun_error &= ~CTL_LUN_INJ_CONTINUOUS;
			break;
		}
		/*
		 * By default, each error injection action is a one-shot
		 */
		if (desc->lun_error & CTL_LUN_INJ_CONTINUOUS)
			continue;

		STAILQ_REMOVE(&lun->error_list, desc, ctl_error_desc, links);

		free(desc, M_CTL);
	}
}

#ifdef CTL_IO_DELAY
static void
ctl_datamove_timer_wakeup(void *arg)
{
	union ctl_io *io;

	io = (union ctl_io *)arg;

	ctl_datamove(io);
}
#endif /* CTL_IO_DELAY */

void
ctl_datamove(union ctl_io *io)
{
	void (*fe_datamove)(union ctl_io *io);

	mtx_assert(&((struct ctl_softc *)CTL_SOFTC(io))->ctl_lock, MA_NOTOWNED);

	CTL_DEBUG_PRINT(("ctl_datamove\n"));

	/* No data transferred yet.  Frontend must update this when done. */
	io->scsiio.kern_data_resid = io->scsiio.kern_data_len;

#ifdef CTL_TIME_IO
	if ((time_uptime - io->io_hdr.start_time) > ctl_time_io_secs) {
		char str[256];
		char path_str[64];
		struct sbuf sb;

		ctl_scsi_path_string(io, path_str, sizeof(path_str));
		sbuf_new(&sb, str, sizeof(str), SBUF_FIXEDLEN);

		sbuf_cat(&sb, path_str);
		switch (io->io_hdr.io_type) {
		case CTL_IO_SCSI:
			ctl_scsi_command_string(&io->scsiio, NULL, &sb);
			sbuf_printf(&sb, "\n");
			sbuf_cat(&sb, path_str);
			sbuf_printf(&sb, "Tag: 0x%04x, type %d\n",
				    io->scsiio.tag_num, io->scsiio.tag_type);
			break;
		case CTL_IO_TASK:
			sbuf_printf(&sb, "Task I/O type: %d, Tag: 0x%04x, "
				    "Tag Type: %d\n", io->taskio.task_action,
				    io->taskio.tag_num, io->taskio.tag_type);
			break;
		default:
			panic("%s: Invalid CTL I/O type %d\n",
			    __func__, io->io_hdr.io_type);
		}
		sbuf_cat(&sb, path_str);
		sbuf_printf(&sb, "ctl_datamove: %jd seconds\n",
			    (intmax_t)time_uptime - io->io_hdr.start_time);
		sbuf_finish(&sb);
		printf("%s", sbuf_data(&sb));
	}
#endif /* CTL_TIME_IO */

#ifdef CTL_IO_DELAY
	if (io->io_hdr.flags & CTL_FLAG_DELAY_DONE) {
		io->io_hdr.flags &= ~CTL_FLAG_DELAY_DONE;
	} else {
		struct ctl_lun *lun;

		lun = CTL_LUN(io);
		if ((lun != NULL)
		 && (lun->delay_info.datamove_delay > 0)) {

			callout_init(&io->io_hdr.delay_callout, /*mpsafe*/ 1);
			io->io_hdr.flags |= CTL_FLAG_DELAY_DONE;
			callout_reset(&io->io_hdr.delay_callout,
				      lun->delay_info.datamove_delay * hz,
				      ctl_datamove_timer_wakeup, io);
			if (lun->delay_info.datamove_type ==
			    CTL_DELAY_TYPE_ONESHOT)
				lun->delay_info.datamove_delay = 0;
			return;
		}
	}
#endif

	/*
	 * This command has been aborted.  Set the port status, so we fail
	 * the data move.
	 */
	if (io->io_hdr.flags & CTL_FLAG_ABORT) {
		printf("ctl_datamove: tag 0x%04x on (%u:%u:%u) aborted\n",
		       io->scsiio.tag_num, io->io_hdr.nexus.initid,
		       io->io_hdr.nexus.targ_port,
		       io->io_hdr.nexus.targ_lun);
		io->io_hdr.port_status = 31337;
		/*
		 * Note that the backend, in this case, will get the
		 * callback in its context.  In other cases it may get
		 * called in the frontend's interrupt thread context.
		 */
		io->scsiio.be_move_done(io);
		return;
	}

	/* Don't confuse frontend with zero length data move. */
	if (io->scsiio.kern_data_len == 0) {
		io->scsiio.be_move_done(io);
		return;
	}

	fe_datamove = CTL_PORT(io)->fe_datamove;
	fe_datamove(io);
}

static void
ctl_send_datamove_done(union ctl_io *io, int have_lock)
{
	union ctl_ha_msg msg;
#ifdef CTL_TIME_IO
	struct bintime cur_bt;
#endif

	memset(&msg, 0, sizeof(msg));
	msg.hdr.msg_type = CTL_MSG_DATAMOVE_DONE;
	msg.hdr.original_sc = io;
	msg.hdr.serializing_sc = io->io_hdr.remote_io;
	msg.hdr.nexus = io->io_hdr.nexus;
	msg.hdr.status = io->io_hdr.status;
	msg.scsi.kern_data_resid = io->scsiio.kern_data_resid;
	msg.scsi.tag_num = io->scsiio.tag_num;
	msg.scsi.tag_type = io->scsiio.tag_type;
	msg.scsi.scsi_status = io->scsiio.scsi_status;
	memcpy(&msg.scsi.sense_data, &io->scsiio.sense_data,
	       io->scsiio.sense_len);
	msg.scsi.sense_len = io->scsiio.sense_len;
	msg.scsi.port_status = io->io_hdr.port_status;
	io->io_hdr.flags &= ~CTL_FLAG_IO_ACTIVE;
	if (io->io_hdr.flags & CTL_FLAG_FAILOVER) {
		ctl_failover_io(io, /*have_lock*/ have_lock);
		return;
	}
	ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg,
	    sizeof(msg.scsi) - sizeof(msg.scsi.sense_data) +
	    msg.scsi.sense_len, M_WAITOK);

#ifdef CTL_TIME_IO
	getbinuptime(&cur_bt);
	bintime_sub(&cur_bt, &io->io_hdr.dma_start_bt);
	bintime_add(&io->io_hdr.dma_bt, &cur_bt);
#endif
	io->io_hdr.num_dmas++;
}

/*
 * The DMA to the remote side is done, now we need to tell the other side
 * we're done so it can continue with its data movement.
 */
static void
ctl_datamove_remote_write_cb(struct ctl_ha_dt_req *rq)
{
	union ctl_io *io;
	uint32_t i;

	io = rq->context;

	if (rq->ret != CTL_HA_STATUS_SUCCESS) {
		printf("%s: ISC DMA write failed with error %d", __func__,
		       rq->ret);
		ctl_set_internal_failure(&io->scsiio,
					 /*sks_valid*/ 1,
					 /*retry_count*/ rq->ret);
	}

	ctl_dt_req_free(rq);

	for (i = 0; i < io->scsiio.kern_sg_entries; i++)
		free(CTL_LSGLT(io)[i].addr, M_CTL);
	free(CTL_RSGL(io), M_CTL);
	CTL_RSGL(io) = NULL;
	CTL_LSGL(io) = NULL;

	/*
	 * The data is in local and remote memory, so now we need to send
	 * status (good or back) back to the other side.
	 */
	ctl_send_datamove_done(io, /*have_lock*/ 0);
}

/*
 * We've moved the data from the host/controller into local memory.  Now we
 * need to push it over to the remote controller's memory.
 */
static int
ctl_datamove_remote_dm_write_cb(union ctl_io *io)
{
	int retval;

	retval = ctl_datamove_remote_xfer(io, CTL_HA_DT_CMD_WRITE,
					  ctl_datamove_remote_write_cb);
	return (retval);
}

static void
ctl_datamove_remote_write(union ctl_io *io)
{
	int retval;
	void (*fe_datamove)(union ctl_io *io);

	/*
	 * - Get the data from the host/HBA into local memory.
	 * - DMA memory from the local controller to the remote controller.
	 * - Send status back to the remote controller.
	 */

	retval = ctl_datamove_remote_sgl_setup(io);
	if (retval != 0)
		return;

	/* Switch the pointer over so the FETD knows what to do */
	io->scsiio.kern_data_ptr = (uint8_t *)CTL_LSGL(io);

	/*
	 * Use a custom move done callback, since we need to send completion
	 * back to the other controller, not to the backend on this side.
	 */
	io->scsiio.be_move_done = ctl_datamove_remote_dm_write_cb;

	fe_datamove = CTL_PORT(io)->fe_datamove;
	fe_datamove(io);
}

static int
ctl_datamove_remote_dm_read_cb(union ctl_io *io)
{
	uint32_t i;

	for (i = 0; i < io->scsiio.kern_sg_entries; i++)
		free(CTL_LSGLT(io)[i].addr, M_CTL);
	free(CTL_RSGL(io), M_CTL);
	CTL_RSGL(io) = NULL;
	CTL_LSGL(io) = NULL;

	/*
	 * The read is done, now we need to send status (good or bad) back
	 * to the other side.
	 */
	ctl_send_datamove_done(io, /*have_lock*/ 0);

	return (0);
}

static void
ctl_datamove_remote_read_cb(struct ctl_ha_dt_req *rq)
{
	union ctl_io *io;
	void (*fe_datamove)(union ctl_io *io);

	io = rq->context;

	if (rq->ret != CTL_HA_STATUS_SUCCESS) {
		printf("%s: ISC DMA read failed with error %d\n", __func__,
		       rq->ret);
		ctl_set_internal_failure(&io->scsiio,
					 /*sks_valid*/ 1,
					 /*retry_count*/ rq->ret);
	}

	ctl_dt_req_free(rq);

	/* Switch the pointer over so the FETD knows what to do */
	io->scsiio.kern_data_ptr = (uint8_t *)CTL_LSGL(io);

	/*
	 * Use a custom move done callback, since we need to send completion
	 * back to the other controller, not to the backend on this side.
	 */
	io->scsiio.be_move_done = ctl_datamove_remote_dm_read_cb;

	/* XXX KDM add checks like the ones in ctl_datamove? */

	fe_datamove = CTL_PORT(io)->fe_datamove;
	fe_datamove(io);
}

static int
ctl_datamove_remote_sgl_setup(union ctl_io *io)
{
	struct ctl_sg_entry *local_sglist;
	uint32_t len_to_go;
	int retval;
	int i;

	retval = 0;
	local_sglist = CTL_LSGL(io);
	len_to_go = io->scsiio.kern_data_len;

	/*
	 * The difficult thing here is that the size of the various
	 * S/G segments may be different than the size from the
	 * remote controller.  That'll make it harder when DMAing
	 * the data back to the other side.
	 */
	for (i = 0; len_to_go > 0; i++) {
		local_sglist[i].len = MIN(len_to_go, CTL_HA_DATAMOVE_SEGMENT);
		local_sglist[i].addr =
		    malloc(local_sglist[i].len, M_CTL, M_WAITOK);

		len_to_go -= local_sglist[i].len;
	}
	/*
	 * Reset the number of S/G entries accordingly.  The original
	 * number of S/G entries is available in rem_sg_entries.
	 */
	io->scsiio.kern_sg_entries = i;

	return (retval);
}

static int
ctl_datamove_remote_xfer(union ctl_io *io, unsigned command,
			 ctl_ha_dt_cb callback)
{
	struct ctl_ha_dt_req *rq;
	struct ctl_sg_entry *remote_sglist, *local_sglist;
	uint32_t local_used, remote_used, total_used;
	int i, j, isc_ret;

	rq = ctl_dt_req_alloc();

	/*
	 * If we failed to allocate the request, and if the DMA didn't fail
	 * anyway, set busy status.  This is just a resource allocation
	 * failure.
	 */
	if ((rq == NULL)
	 && ((io->io_hdr.status & CTL_STATUS_MASK) != CTL_STATUS_NONE &&
	     (io->io_hdr.status & CTL_STATUS_MASK) != CTL_SUCCESS))
		ctl_set_busy(&io->scsiio);

	if ((io->io_hdr.status & CTL_STATUS_MASK) != CTL_STATUS_NONE &&
	    (io->io_hdr.status & CTL_STATUS_MASK) != CTL_SUCCESS) {

		if (rq != NULL)
			ctl_dt_req_free(rq);

		/*
		 * The data move failed.  We need to return status back
		 * to the other controller.  No point in trying to DMA
		 * data to the remote controller.
		 */

		ctl_send_datamove_done(io, /*have_lock*/ 0);

		return (1);
	}

	local_sglist = CTL_LSGL(io);
	remote_sglist = CTL_RSGL(io);
	local_used = 0;
	remote_used = 0;
	total_used = 0;

	/*
	 * Pull/push the data over the wire from/to the other controller.
	 * This takes into account the possibility that the local and
	 * remote sglists may not be identical in terms of the size of
	 * the elements and the number of elements.
	 *
	 * One fundamental assumption here is that the length allocated for
	 * both the local and remote sglists is identical.  Otherwise, we've
	 * essentially got a coding error of some sort.
	 */
	isc_ret = CTL_HA_STATUS_SUCCESS;
	for (i = 0, j = 0; total_used < io->scsiio.kern_data_len; ) {
		uint32_t cur_len;
		uint8_t *tmp_ptr;

		rq->command = command;
		rq->context = io;

		/*
		 * Both pointers should be aligned.  But it is possible
		 * that the allocation length is not.  They should both
		 * also have enough slack left over at the end, though,
		 * to round up to the next 8 byte boundary.
		 */
		cur_len = MIN(local_sglist[i].len - local_used,
			      remote_sglist[j].len - remote_used);
		rq->size = cur_len;

		tmp_ptr = (uint8_t *)local_sglist[i].addr;
		tmp_ptr += local_used;

#if 0
		/* Use physical addresses when talking to ISC hardware */
		if ((io->io_hdr.flags & CTL_FLAG_BUS_ADDR) == 0) {
			/* XXX KDM use busdma */
			rq->local = vtophys(tmp_ptr);
		} else
			rq->local = tmp_ptr;
#else
		KASSERT((io->io_hdr.flags & CTL_FLAG_BUS_ADDR) == 0,
		    ("HA does not support BUS_ADDR"));
		rq->local = tmp_ptr;
#endif

		tmp_ptr = (uint8_t *)remote_sglist[j].addr;
		tmp_ptr += remote_used;
		rq->remote = tmp_ptr;

		rq->callback = NULL;

		local_used += cur_len;
		if (local_used >= local_sglist[i].len) {
			i++;
			local_used = 0;
		}

		remote_used += cur_len;
		if (remote_used >= remote_sglist[j].len) {
			j++;
			remote_used = 0;
		}
		total_used += cur_len;

		if (total_used >= io->scsiio.kern_data_len)
			rq->callback = callback;

		isc_ret = ctl_dt_single(rq);
		if (isc_ret > CTL_HA_STATUS_SUCCESS)
			break;
	}
	if (isc_ret != CTL_HA_STATUS_WAIT) {
		rq->ret = isc_ret;
		callback(rq);
	}

	return (0);
}

static void
ctl_datamove_remote_read(union ctl_io *io)
{
	int retval;
	uint32_t i;

	/*
	 * This will send an error to the other controller in the case of a
	 * failure.
	 */
	retval = ctl_datamove_remote_sgl_setup(io);
	if (retval != 0)
		return;

	retval = ctl_datamove_remote_xfer(io, CTL_HA_DT_CMD_READ,
					  ctl_datamove_remote_read_cb);
	if (retval != 0) {
		/*
		 * Make sure we free memory if there was an error..  The
		 * ctl_datamove_remote_xfer() function will send the
		 * datamove done message, or call the callback with an
		 * error if there is a problem.
		 */
		for (i = 0; i < io->scsiio.kern_sg_entries; i++)
			free(CTL_LSGLT(io)[i].addr, M_CTL);
		free(CTL_RSGL(io), M_CTL);
		CTL_RSGL(io) = NULL;
		CTL_LSGL(io) = NULL;
	}
}

/*
 * Process a datamove request from the other controller.  This is used for
 * XFER mode only, not SER_ONLY mode.  For writes, we DMA into local memory
 * first.  Once that is complete, the data gets DMAed into the remote
 * controller's memory.  For reads, we DMA from the remote controller's
 * memory into our memory first, and then move it out to the FETD.
 */
static void
ctl_datamove_remote(union ctl_io *io)
{

	mtx_assert(&((struct ctl_softc *)CTL_SOFTC(io))->ctl_lock, MA_NOTOWNED);

	if (io->io_hdr.flags & CTL_FLAG_FAILOVER) {
		ctl_failover_io(io, /*have_lock*/ 0);
		return;
	}

	/*
	 * Note that we look for an aborted I/O here, but don't do some of
	 * the other checks that ctl_datamove() normally does.
	 * We don't need to run the datamove delay code, since that should
	 * have been done if need be on the other controller.
	 */
	if (io->io_hdr.flags & CTL_FLAG_ABORT) {
		printf("%s: tag 0x%04x on (%u:%u:%u) aborted\n", __func__,
		       io->scsiio.tag_num, io->io_hdr.nexus.initid,
		       io->io_hdr.nexus.targ_port,
		       io->io_hdr.nexus.targ_lun);
		io->io_hdr.port_status = 31338;
		ctl_send_datamove_done(io, /*have_lock*/ 0);
		return;
	}

	if ((io->io_hdr.flags & CTL_FLAG_DATA_MASK) == CTL_FLAG_DATA_OUT)
		ctl_datamove_remote_write(io);
	else if ((io->io_hdr.flags & CTL_FLAG_DATA_MASK) == CTL_FLAG_DATA_IN)
		ctl_datamove_remote_read(io);
	else {
		io->io_hdr.port_status = 31339;
		ctl_send_datamove_done(io, /*have_lock*/ 0);
	}
}

static void
ctl_process_done(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_port *port = CTL_PORT(io);
	struct ctl_lun *lun = CTL_LUN(io);
	void (*fe_done)(union ctl_io *io);
	union ctl_ha_msg msg;

	CTL_DEBUG_PRINT(("ctl_process_done\n"));
	fe_done = port->fe_done;

#ifdef CTL_TIME_IO
	if ((time_uptime - io->io_hdr.start_time) > ctl_time_io_secs) {
		char str[256];
		char path_str[64];
		struct sbuf sb;

		ctl_scsi_path_string(io, path_str, sizeof(path_str));
		sbuf_new(&sb, str, sizeof(str), SBUF_FIXEDLEN);

		sbuf_cat(&sb, path_str);
		switch (io->io_hdr.io_type) {
		case CTL_IO_SCSI:
			ctl_scsi_command_string(&io->scsiio, NULL, &sb);
			sbuf_printf(&sb, "\n");
			sbuf_cat(&sb, path_str);
			sbuf_printf(&sb, "Tag: 0x%04x, type %d\n",
				    io->scsiio.tag_num, io->scsiio.tag_type);
			break;
		case CTL_IO_TASK:
			sbuf_printf(&sb, "Task I/O type: %d, Tag: 0x%04x, "
				    "Tag Type: %d\n", io->taskio.task_action,
				    io->taskio.tag_num, io->taskio.tag_type);
			break;
		default:
			panic("%s: Invalid CTL I/O type %d\n",
			    __func__, io->io_hdr.io_type);
		}
		sbuf_cat(&sb, path_str);
		sbuf_printf(&sb, "ctl_process_done: %jd seconds\n",
			    (intmax_t)time_uptime - io->io_hdr.start_time);
		sbuf_finish(&sb);
		printf("%s", sbuf_data(&sb));
	}
#endif /* CTL_TIME_IO */

	switch (io->io_hdr.io_type) {
	case CTL_IO_SCSI:
		break;
	case CTL_IO_TASK:
		if (ctl_debug & CTL_DEBUG_INFO)
			ctl_io_error_print(io, NULL);
		fe_done(io);
		return;
	default:
		panic("%s: Invalid CTL I/O type %d\n",
		    __func__, io->io_hdr.io_type);
	}

	if (lun == NULL) {
		CTL_DEBUG_PRINT(("NULL LUN for lun %d\n",
				 io->io_hdr.nexus.targ_mapped_lun));
		goto bailout;
	}

	mtx_lock(&lun->lun_lock);

	/*
	 * Check to see if we have any informational exception and status
	 * of this command can be modified to report it in form of either
	 * RECOVERED ERROR or NO SENSE, depending on MRIE mode page field.
	 */
	if (lun->ie_reported == 0 && lun->ie_asc != 0 &&
	    io->io_hdr.status == CTL_SUCCESS &&
	    (io->io_hdr.flags & CTL_FLAG_STATUS_SENT) == 0) {
		uint8_t mrie = lun->MODE_IE.mrie;
		uint8_t per = ((lun->MODE_RWER.byte3 & SMS_RWER_PER) ||
		    (lun->MODE_VER.byte3 & SMS_VER_PER));
		if (((mrie == SIEP_MRIE_REC_COND && per) ||
		     mrie == SIEP_MRIE_REC_UNCOND ||
		     mrie == SIEP_MRIE_NO_SENSE) &&
		    (ctl_get_cmd_entry(&io->scsiio, NULL)->flags &
		     CTL_CMD_FLAG_NO_SENSE) == 0) {
			ctl_set_sense(&io->scsiio,
			      /*current_error*/ 1,
			      /*sense_key*/ (mrie == SIEP_MRIE_NO_SENSE) ?
			        SSD_KEY_NO_SENSE : SSD_KEY_RECOVERED_ERROR,
			      /*asc*/ lun->ie_asc,
			      /*ascq*/ lun->ie_ascq,
			      SSD_ELEM_NONE);
			lun->ie_reported = 1;
		}
	} else if (lun->ie_reported < 0)
		lun->ie_reported = 0;

	/*
	 * Check to see if we have any errors to inject here.  We only
	 * inject errors for commands that don't already have errors set.
	 */
	if (!STAILQ_EMPTY(&lun->error_list) &&
	    ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) &&
	    ((io->io_hdr.flags & CTL_FLAG_STATUS_SENT) == 0))
		ctl_inject_error(lun, io);

	/*
	 * XXX KDM how do we treat commands that aren't completed
	 * successfully?
	 *
	 * XXX KDM should we also track I/O latency?
	 */
	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS &&
	    io->io_hdr.io_type == CTL_IO_SCSI) {
		int type;
#ifdef CTL_TIME_IO
		struct bintime bt;

		getbinuptime(&bt);
		bintime_sub(&bt, &io->io_hdr.start_bt);
#endif
		if ((io->io_hdr.flags & CTL_FLAG_DATA_MASK) ==
		    CTL_FLAG_DATA_IN)
			type = CTL_STATS_READ;
		else if ((io->io_hdr.flags & CTL_FLAG_DATA_MASK) ==
		    CTL_FLAG_DATA_OUT)
			type = CTL_STATS_WRITE;
		else
			type = CTL_STATS_NO_IO;

		lun->stats.bytes[type] += io->scsiio.kern_total_len;
		lun->stats.operations[type] ++;
		lun->stats.dmas[type] += io->io_hdr.num_dmas;
#ifdef CTL_TIME_IO
		bintime_add(&lun->stats.dma_time[type], &io->io_hdr.dma_bt);
		bintime_add(&lun->stats.time[type], &bt);
#endif

		mtx_lock(&port->port_lock);
		port->stats.bytes[type] += io->scsiio.kern_total_len;
		port->stats.operations[type] ++;
		port->stats.dmas[type] += io->io_hdr.num_dmas;
#ifdef CTL_TIME_IO
		bintime_add(&port->stats.dma_time[type], &io->io_hdr.dma_bt);
		bintime_add(&port->stats.time[type], &bt);
#endif
		mtx_unlock(&port->port_lock);
	}

	/*
	 * Run through the blocked queue of this I/O and see if anything
	 * can be unblocked, now that this I/O is done and will be removed.
	 * We need to do it before removal to have OOA position to start.
	 */
	ctl_try_unblock_others(lun, io, TRUE);

	/*
	 * Remove this from the OOA queue.
	 */
	TAILQ_REMOVE(&lun->ooa_queue, &io->io_hdr, ooa_links);
#ifdef CTL_TIME_IO
	if (TAILQ_EMPTY(&lun->ooa_queue))
		lun->last_busy = getsbinuptime();
#endif

	/*
	 * If the LUN has been invalidated, free it if there is nothing
	 * left on its OOA queue.
	 */
	if ((lun->flags & CTL_LUN_INVALID)
	 && TAILQ_EMPTY(&lun->ooa_queue)) {
		mtx_unlock(&lun->lun_lock);
		ctl_free_lun(lun);
	} else
		mtx_unlock(&lun->lun_lock);

bailout:

	/*
	 * If this command has been aborted, make sure we set the status
	 * properly.  The FETD is responsible for freeing the I/O and doing
	 * whatever it needs to do to clean up its state.
	 */
	if (io->io_hdr.flags & CTL_FLAG_ABORT)
		ctl_set_task_aborted(&io->scsiio);

	/*
	 * If enabled, print command error status.
	 */
	if ((io->io_hdr.status & CTL_STATUS_MASK) != CTL_SUCCESS &&
	    (ctl_debug & CTL_DEBUG_INFO) != 0)
		ctl_io_error_print(io, NULL);

	/*
	 * Tell the FETD or the other shelf controller we're done with this
	 * command.  Note that only SCSI commands get to this point.  Task
	 * management commands are completed above.
	 */
	if ((softc->ha_mode != CTL_HA_MODE_XFER) &&
	    (io->io_hdr.flags & CTL_FLAG_SENT_2OTHER_SC)) {
		memset(&msg, 0, sizeof(msg));
		msg.hdr.msg_type = CTL_MSG_FINISH_IO;
		msg.hdr.serializing_sc = io->io_hdr.remote_io;
		msg.hdr.nexus = io->io_hdr.nexus;
		ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg,
		    sizeof(msg.scsi) - sizeof(msg.scsi.sense_data),
		    M_WAITOK);
	}

	fe_done(io);
}

/*
 * Front end should call this if it doesn't do autosense.  When the request
 * sense comes back in from the initiator, we'll dequeue this and send it.
 */
int
ctl_queue_sense(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_port *port = CTL_PORT(io);
	struct ctl_lun *lun;
	struct scsi_sense_data *ps;
	uint32_t initidx, p, targ_lun;

	CTL_DEBUG_PRINT(("ctl_queue_sense\n"));

	targ_lun = ctl_lun_map_from_port(port, io->io_hdr.nexus.targ_lun);

	/*
	 * LUN lookup will likely move to the ctl_work_thread() once we
	 * have our new queueing infrastructure (that doesn't put things on
	 * a per-LUN queue initially).  That is so that we can handle
	 * things like an INQUIRY to a LUN that we don't have enabled.  We
	 * can't deal with that right now.
	 * If we don't have a LUN for this, just toss the sense information.
	 */
	mtx_lock(&softc->ctl_lock);
	if (targ_lun >= ctl_max_luns ||
	    (lun = softc->ctl_luns[targ_lun]) == NULL) {
		mtx_unlock(&softc->ctl_lock);
		goto bailout;
	}
	mtx_lock(&lun->lun_lock);
	mtx_unlock(&softc->ctl_lock);

	initidx = ctl_get_initindex(&io->io_hdr.nexus);
	p = initidx / CTL_MAX_INIT_PER_PORT;
	if (lun->pending_sense[p] == NULL) {
		lun->pending_sense[p] = malloc(sizeof(*ps) * CTL_MAX_INIT_PER_PORT,
		    M_CTL, M_NOWAIT | M_ZERO);
	}
	if ((ps = lun->pending_sense[p]) != NULL) {
		ps += initidx % CTL_MAX_INIT_PER_PORT;
		memset(ps, 0, sizeof(*ps));
		memcpy(ps, &io->scsiio.sense_data, io->scsiio.sense_len);
	}
	mtx_unlock(&lun->lun_lock);

bailout:
	ctl_free_io(io);
	return (CTL_RETVAL_COMPLETE);
}

/*
 * Primary command inlet from frontend ports.  All SCSI and task I/O
 * requests must go through this function.
 */
int
ctl_queue(union ctl_io *io)
{
	struct ctl_port *port = CTL_PORT(io);

	CTL_DEBUG_PRINT(("ctl_queue cdb[0]=%02X\n", io->scsiio.cdb[0]));

#ifdef CTL_TIME_IO
	io->io_hdr.start_time = time_uptime;
	getbinuptime(&io->io_hdr.start_bt);
#endif /* CTL_TIME_IO */

	/* Map FE-specific LUN ID into global one. */
	io->io_hdr.nexus.targ_mapped_lun =
	    ctl_lun_map_from_port(port, io->io_hdr.nexus.targ_lun);

	switch (io->io_hdr.io_type) {
	case CTL_IO_SCSI:
	case CTL_IO_TASK:
		if (ctl_debug & CTL_DEBUG_CDB)
			ctl_io_print(io);
		ctl_enqueue_incoming(io);
		break;
	default:
		printf("ctl_queue: unknown I/O type %d\n", io->io_hdr.io_type);
		return (EINVAL);
	}

	return (CTL_RETVAL_COMPLETE);
}

#ifdef CTL_IO_DELAY
static void
ctl_done_timer_wakeup(void *arg)
{
	union ctl_io *io;

	io = (union ctl_io *)arg;
	ctl_done(io);
}
#endif /* CTL_IO_DELAY */

void
ctl_serseq_done(union ctl_io *io)
{
	struct ctl_lun *lun = CTL_LUN(io);;

	if (lun->be_lun == NULL ||
	    lun->be_lun->serseq == CTL_LUN_SERSEQ_OFF)
		return;
	mtx_lock(&lun->lun_lock);
	io->io_hdr.flags |= CTL_FLAG_SERSEQ_DONE;
	ctl_try_unblock_others(lun, io, FALSE);
	mtx_unlock(&lun->lun_lock);
}

void
ctl_done(union ctl_io *io)
{

	/*
	 * Enable this to catch duplicate completion issues.
	 */
#if 0
	if (io->io_hdr.flags & CTL_FLAG_ALREADY_DONE) {
		printf("%s: type %d msg %d cdb %x iptl: "
		       "%u:%u:%u tag 0x%04x "
		       "flag %#x status %x\n",
			__func__,
			io->io_hdr.io_type,
			io->io_hdr.msg_type,
			io->scsiio.cdb[0],
			io->io_hdr.nexus.initid,
			io->io_hdr.nexus.targ_port,
			io->io_hdr.nexus.targ_lun,
			(io->io_hdr.io_type ==
			CTL_IO_TASK) ?
			io->taskio.tag_num :
			io->scsiio.tag_num,
		        io->io_hdr.flags,
			io->io_hdr.status);
	} else
		io->io_hdr.flags |= CTL_FLAG_ALREADY_DONE;
#endif

	/*
	 * This is an internal copy of an I/O, and should not go through
	 * the normal done processing logic.
	 */
	if (io->io_hdr.flags & CTL_FLAG_INT_COPY)
		return;

#ifdef CTL_IO_DELAY
	if (io->io_hdr.flags & CTL_FLAG_DELAY_DONE) {
		io->io_hdr.flags &= ~CTL_FLAG_DELAY_DONE;
	} else {
		struct ctl_lun *lun = CTL_LUN(io);

		if ((lun != NULL)
		 && (lun->delay_info.done_delay > 0)) {

			callout_init(&io->io_hdr.delay_callout, /*mpsafe*/ 1);
			io->io_hdr.flags |= CTL_FLAG_DELAY_DONE;
			callout_reset(&io->io_hdr.delay_callout,
				      lun->delay_info.done_delay * hz,
				      ctl_done_timer_wakeup, io);
			if (lun->delay_info.done_type == CTL_DELAY_TYPE_ONESHOT)
				lun->delay_info.done_delay = 0;
			return;
		}
	}
#endif /* CTL_IO_DELAY */

	ctl_enqueue_done(io);
}

static void
ctl_work_thread(void *arg)
{
	struct ctl_thread *thr = (struct ctl_thread *)arg;
	struct ctl_softc *softc = thr->ctl_softc;
	union ctl_io *io;
	int retval;

	CTL_DEBUG_PRINT(("ctl_work_thread starting\n"));
	thread_lock(curthread);
	sched_prio(curthread, PUSER - 1);
	thread_unlock(curthread);

	while (!softc->shutdown) {
		/*
		 * We handle the queues in this order:
		 * - ISC
		 * - done queue (to free up resources, unblock other commands)
		 * - incoming queue
		 * - RtR queue
		 *
		 * If those queues are empty, we break out of the loop and
		 * go to sleep.
		 */
		mtx_lock(&thr->queue_lock);
		io = (union ctl_io *)STAILQ_FIRST(&thr->isc_queue);
		if (io != NULL) {
			STAILQ_REMOVE_HEAD(&thr->isc_queue, links);
			mtx_unlock(&thr->queue_lock);
			ctl_handle_isc(io);
			continue;
		}
		io = (union ctl_io *)STAILQ_FIRST(&thr->done_queue);
		if (io != NULL) {
			STAILQ_REMOVE_HEAD(&thr->done_queue, links);
			/* clear any blocked commands, call fe_done */
			mtx_unlock(&thr->queue_lock);
			ctl_process_done(io);
			continue;
		}
		io = (union ctl_io *)STAILQ_FIRST(&thr->incoming_queue);
		if (io != NULL) {
			STAILQ_REMOVE_HEAD(&thr->incoming_queue, links);
			mtx_unlock(&thr->queue_lock);
			if (io->io_hdr.io_type == CTL_IO_TASK)
				ctl_run_task(io);
			else
				ctl_scsiio_precheck(softc, &io->scsiio);
			continue;
		}
		io = (union ctl_io *)STAILQ_FIRST(&thr->rtr_queue);
		if (io != NULL) {
			STAILQ_REMOVE_HEAD(&thr->rtr_queue, links);
			mtx_unlock(&thr->queue_lock);
			retval = ctl_scsiio(&io->scsiio);
			if (retval != CTL_RETVAL_COMPLETE)
				CTL_DEBUG_PRINT(("ctl_scsiio failed\n"));
			continue;
		}

		/* Sleep until we have something to do. */
		mtx_sleep(thr, &thr->queue_lock, PDROP, "-", 0);
	}
	thr->thread = NULL;
	kthread_exit();
}

static void
ctl_lun_thread(void *arg)
{
	struct ctl_softc *softc = (struct ctl_softc *)arg;
	struct ctl_be_lun *be_lun;

	CTL_DEBUG_PRINT(("ctl_lun_thread starting\n"));
	thread_lock(curthread);
	sched_prio(curthread, PUSER - 1);
	thread_unlock(curthread);

	while (!softc->shutdown) {
		mtx_lock(&softc->ctl_lock);
		be_lun = STAILQ_FIRST(&softc->pending_lun_queue);
		if (be_lun != NULL) {
			STAILQ_REMOVE_HEAD(&softc->pending_lun_queue, links);
			mtx_unlock(&softc->ctl_lock);
			ctl_create_lun(be_lun);
			continue;
		}

		/* Sleep until we have something to do. */
		mtx_sleep(&softc->pending_lun_queue, &softc->ctl_lock,
		    PDROP, "-", 0);
	}
	softc->lun_thread = NULL;
	kthread_exit();
}

static void
ctl_thresh_thread(void *arg)
{
	struct ctl_softc *softc = (struct ctl_softc *)arg;
	struct ctl_lun *lun;
	struct ctl_logical_block_provisioning_page *page;
	const char *attr;
	union ctl_ha_msg msg;
	uint64_t thres, val;
	int i, e, set;

	CTL_DEBUG_PRINT(("ctl_thresh_thread starting\n"));
	thread_lock(curthread);
	sched_prio(curthread, PUSER - 1);
	thread_unlock(curthread);

	while (!softc->shutdown) {
		mtx_lock(&softc->ctl_lock);
		STAILQ_FOREACH(lun, &softc->lun_list, links) {
			if ((lun->flags & CTL_LUN_DISABLED) ||
			    (lun->flags & CTL_LUN_NO_MEDIA) ||
			    lun->backend->lun_attr == NULL)
				continue;
			if ((lun->flags & CTL_LUN_PRIMARY_SC) == 0 &&
			    softc->ha_mode == CTL_HA_MODE_XFER)
				continue;
			if ((lun->MODE_RWER.byte8 & SMS_RWER_LBPERE) == 0)
				continue;
			e = 0;
			page = &lun->MODE_LBP;
			for (i = 0; i < CTL_NUM_LBP_THRESH; i++) {
				if ((page->descr[i].flags & SLBPPD_ENABLED) == 0)
					continue;
				thres = scsi_4btoul(page->descr[i].count);
				thres <<= CTL_LBP_EXPONENT;
				switch (page->descr[i].resource) {
				case 0x01:
					attr = "blocksavail";
					break;
				case 0x02:
					attr = "blocksused";
					break;
				case 0xf1:
					attr = "poolblocksavail";
					break;
				case 0xf2:
					attr = "poolblocksused";
					break;
				default:
					continue;
				}
				mtx_unlock(&softc->ctl_lock); // XXX
				val = lun->backend->lun_attr(
				    lun->be_lun->be_lun, attr);
				mtx_lock(&softc->ctl_lock);
				if (val == UINT64_MAX)
					continue;
				if ((page->descr[i].flags & SLBPPD_ARMING_MASK)
				    == SLBPPD_ARMING_INC)
					e = (val >= thres);
				else
					e = (val <= thres);
				if (e)
					break;
			}
			mtx_lock(&lun->lun_lock);
			if (e) {
				scsi_u64to8b((uint8_t *)&page->descr[i] -
				    (uint8_t *)page, lun->ua_tpt_info);
				if (lun->lasttpt == 0 ||
				    time_uptime - lun->lasttpt >= CTL_LBP_UA_PERIOD) {
					lun->lasttpt = time_uptime;
					ctl_est_ua_all(lun, -1, CTL_UA_THIN_PROV_THRES);
					set = 1;
				} else
					set = 0;
			} else {
				lun->lasttpt = 0;
				ctl_clr_ua_all(lun, -1, CTL_UA_THIN_PROV_THRES);
				set = -1;
			}
			mtx_unlock(&lun->lun_lock);
			if (set != 0 &&
			    lun->ctl_softc->ha_mode == CTL_HA_MODE_XFER) {
				/* Send msg to other side. */
				bzero(&msg.ua, sizeof(msg.ua));
				msg.hdr.msg_type = CTL_MSG_UA;
				msg.hdr.nexus.initid = -1;
				msg.hdr.nexus.targ_port = -1;
				msg.hdr.nexus.targ_lun = lun->lun;
				msg.hdr.nexus.targ_mapped_lun = lun->lun;
				msg.ua.ua_all = 1;
				msg.ua.ua_set = (set > 0);
				msg.ua.ua_type = CTL_UA_THIN_PROV_THRES;
				memcpy(msg.ua.ua_info, lun->ua_tpt_info, 8);
				mtx_unlock(&softc->ctl_lock); // XXX
				ctl_ha_msg_send(CTL_HA_CHAN_CTL, &msg,
				    sizeof(msg.ua), M_WAITOK);
				mtx_lock(&softc->ctl_lock);
			}
		}
		mtx_sleep(&softc->thresh_thread, &softc->ctl_lock,
		    PDROP, "-", CTL_LBP_PERIOD * hz);
	}
	softc->thresh_thread = NULL;
	kthread_exit();
}

static void
ctl_enqueue_incoming(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_thread *thr;
	u_int idx;

	idx = (io->io_hdr.nexus.targ_port * 127 +
	       io->io_hdr.nexus.initid) % worker_threads;
	thr = &softc->threads[idx];
	mtx_lock(&thr->queue_lock);
	STAILQ_INSERT_TAIL(&thr->incoming_queue, &io->io_hdr, links);
	mtx_unlock(&thr->queue_lock);
	wakeup(thr);
}

static void
ctl_enqueue_rtr(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_thread *thr;

	thr = &softc->threads[io->io_hdr.nexus.targ_mapped_lun % worker_threads];
	mtx_lock(&thr->queue_lock);
	STAILQ_INSERT_TAIL(&thr->rtr_queue, &io->io_hdr, links);
	mtx_unlock(&thr->queue_lock);
	wakeup(thr);
}

static void
ctl_enqueue_done(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_thread *thr;

	thr = &softc->threads[io->io_hdr.nexus.targ_mapped_lun % worker_threads];
	mtx_lock(&thr->queue_lock);
	STAILQ_INSERT_TAIL(&thr->done_queue, &io->io_hdr, links);
	mtx_unlock(&thr->queue_lock);
	wakeup(thr);
}

static void
ctl_enqueue_isc(union ctl_io *io)
{
	struct ctl_softc *softc = CTL_SOFTC(io);
	struct ctl_thread *thr;

	thr = &softc->threads[io->io_hdr.nexus.targ_mapped_lun % worker_threads];
	mtx_lock(&thr->queue_lock);
	STAILQ_INSERT_TAIL(&thr->isc_queue, &io->io_hdr, links);
	mtx_unlock(&thr->queue_lock);
	wakeup(thr);
}

/*
 *  vim: ts=8
 */

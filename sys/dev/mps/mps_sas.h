/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2015 LSI Corp.
 * Copyright (c) 2013-2015 Avago Technologies
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
 * Avago Technologies (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

struct mps_fw_event_work;

struct mpssas_lun {
	SLIST_ENTRY(mpssas_lun) lun_link;
	lun_id_t	lun_id;
	uint8_t		eedp_formatted;
	uint32_t	eedp_block_size;
};

struct mpssas_target {
	uint16_t	handle;
	uint8_t		linkrate;
	uint64_t	devname;
	uint32_t	devinfo;
	uint16_t	encl_handle;
	uint16_t	encl_slot;
	uint8_t		flags;
#define MPSSAS_TARGET_INABORT	(1 << 0)
#define MPSSAS_TARGET_INRESET	(1 << 1)
#define MPSSAS_TARGET_INDIAGRESET (1 << 2)
#define MPSSAS_TARGET_INREMOVAL	(1 << 3)
#define MPS_TARGET_FLAGS_RAID_COMPONENT (1 << 4)
#define MPS_TARGET_FLAGS_VOLUME         (1 << 5)
#define MPS_TARGET_IS_SATA_SSD	(1 << 6)
#define MPSSAS_TARGET_INRECOVERY (MPSSAS_TARGET_INABORT | \
    MPSSAS_TARGET_INRESET | MPSSAS_TARGET_INCHIPRESET)

	uint16_t	tid;
	SLIST_HEAD(, mpssas_lun) luns;
	TAILQ_HEAD(, mps_command) commands;
	struct mps_command *tm;
	TAILQ_HEAD(, mps_command) timedout_commands;
	uint16_t        exp_dev_handle;
	uint16_t        phy_num;
	uint64_t	sasaddr;
	uint16_t	parent_handle;
	uint64_t	parent_sasaddr;
	uint32_t	parent_devinfo;
	uint64_t        issued;
	uint64_t        completed;
	unsigned int    outstanding;
	unsigned int    timeouts;
	unsigned int    aborts;
	unsigned int    logical_unit_resets;
	unsigned int    target_resets;
	uint8_t		stop_at_shutdown;
	uint8_t		supports_SSU;
};

struct mpssas_softc {
	struct mps_softc	*sc;
	u_int			flags;
#define MPSSAS_IN_DISCOVERY	(1 << 0)
#define MPSSAS_IN_STARTUP	(1 << 1)
#define MPSSAS_DISCOVERY_TIMEOUT_PENDING	(1 << 2)
#define MPSSAS_QUEUE_FROZEN	(1 << 3)
#define	MPSSAS_SHUTDOWN		(1 << 4)
	u_int			maxtargets;
	struct mpssas_target	*targets;
	struct cam_devq		*devq;
	struct cam_sim		*sim;
	struct cam_path		*path;
	struct intr_config_hook	sas_ich;
	struct callout		discovery_callout;
	struct mps_event_handle	*mpssas_eh;

	u_int                   startup_refcount;
	struct proc             *sysctl_proc;

	struct taskqueue	*ev_tq;
	struct task		ev_task;
	TAILQ_HEAD(, mps_fw_event_work)	ev_queue;
};

MALLOC_DECLARE(M_MPSSAS);

/*
 * Abstracted so that the driver can be backwards and forwards compatible
 * with future versions of CAM that will provide this functionality.
 */
#define MPS_SET_LUN(lun, ccblun)	\
	mpssas_set_lun(lun, ccblun)

static __inline int
mpssas_set_lun(uint8_t *lun, u_int ccblun)
{
	uint64_t *newlun;

	newlun = (uint64_t *)lun;
	*newlun = 0;
	if (ccblun <= 0xff) {
		/* Peripheral device address method, LUN is 0 to 255 */
		lun[1] = ccblun;
	} else if (ccblun <= 0x3fff) {
		/* Flat space address method, LUN is <= 16383 */
		scsi_ulto2b(ccblun, lun);
		lun[0] |= 0x40;
	} else if (ccblun <= 0xffffff) {
		/* Extended flat space address method, LUN is <= 16777215 */
		scsi_ulto3b(ccblun, &lun[1]);
		/* Extended Flat space address method */
		lun[0] = 0xc0;
		/* Length = 1, i.e. LUN is 3 bytes long */
		lun[0] |= 0x10;
		/* Extended Address Method */
		lun[0] |= 0x02;
	} else {
		return (EINVAL);
	}

	return (0);
}

static __inline void
mpssas_set_ccbstatus(union ccb *ccb, int status)
{
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	ccb->ccb_h.status |= status;
}

static __inline int
mpssas_get_ccbstatus(union ccb *ccb)
{
	return (ccb->ccb_h.status & CAM_STATUS_MASK);
}

#define MPS_SET_SINGLE_LUN(req, lun)	\
do {					\
	bzero((req)->LUN, 8);		\
	(req)->LUN[1] = lun;		\
} while(0)

void mpssas_rescan_target(struct mps_softc *sc, struct mpssas_target *targ);
void mpssas_discovery_end(struct mpssas_softc *sassc);
void mpssas_prepare_for_tm(struct mps_softc *sc, struct mps_command *tm,
    struct mpssas_target *target, lun_id_t lun_id);
void mpssas_startup_increment(struct mpssas_softc *sassc);
void mpssas_startup_decrement(struct mpssas_softc *sassc);

void mpssas_firmware_event_work(void *arg, int pending);
int mpssas_check_id(struct mpssas_softc *sassc, int id);

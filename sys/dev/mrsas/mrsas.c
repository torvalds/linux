/*
 * Copyright (c) 2015, AVAGO Tech. All rights reserved. Author: Marian Choy
 * Copyright (c) 2014, LSI Corp. All rights reserved. Author: Marian Choy
 * Support: freebsdraid@avagotech.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer. 2. Redistributions
 * in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution. 3. Neither the name of the
 * <ORGANIZATION> nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing
 * official policies,either expressed or implied, of the FreeBSD Project.
 *
 * Send feedback to: <megaraidfbsd@avagotech.com> Mail to: AVAGO TECHNOLOGIES 1621
 * Barber Lane, Milpitas, CA 95035 ATTN: MegaRaid FreeBSD
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/mrsas/mrsas.h>
#include <dev/mrsas/mrsas_ioctl.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>

#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/sysent.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/smp.h>


/*
 * Function prototypes
 */
static d_open_t mrsas_open;
static d_close_t mrsas_close;
static d_read_t mrsas_read;
static d_write_t mrsas_write;
static d_ioctl_t mrsas_ioctl;
static d_poll_t mrsas_poll;

static void mrsas_ich_startup(void *arg);
static struct mrsas_mgmt_info mrsas_mgmt_info;
static struct mrsas_ident *mrsas_find_ident(device_t);
static int mrsas_setup_msix(struct mrsas_softc *sc);
static int mrsas_allocate_msix(struct mrsas_softc *sc);
static void mrsas_shutdown_ctlr(struct mrsas_softc *sc, u_int32_t opcode);
static void mrsas_flush_cache(struct mrsas_softc *sc);
static void mrsas_reset_reply_desc(struct mrsas_softc *sc);
static void mrsas_ocr_thread(void *arg);
static int mrsas_get_map_info(struct mrsas_softc *sc);
static int mrsas_get_ld_map_info(struct mrsas_softc *sc);
static int mrsas_sync_map_info(struct mrsas_softc *sc);
static int mrsas_get_pd_list(struct mrsas_softc *sc);
static int mrsas_get_ld_list(struct mrsas_softc *sc);
static int mrsas_setup_irq(struct mrsas_softc *sc);
static int mrsas_alloc_mem(struct mrsas_softc *sc);
static int mrsas_init_fw(struct mrsas_softc *sc);
static int mrsas_setup_raidmap(struct mrsas_softc *sc);
static void megasas_setup_jbod_map(struct mrsas_softc *sc);
static int megasas_sync_pd_seq_num(struct mrsas_softc *sc, boolean_t pend);
static int mrsas_clear_intr(struct mrsas_softc *sc);
static int mrsas_get_ctrl_info(struct mrsas_softc *sc);
static void mrsas_update_ext_vd_details(struct mrsas_softc *sc);
static int
mrsas_issue_blocked_abort_cmd(struct mrsas_softc *sc,
    struct mrsas_mfi_cmd *cmd_to_abort);
static void
mrsas_get_pd_info(struct mrsas_softc *sc, u_int16_t device_id);
static struct mrsas_softc *
mrsas_get_softc_instance(struct cdev *dev,
    u_long cmd, caddr_t arg);
u_int32_t
mrsas_read_reg_with_retries(struct mrsas_softc *sc, int offset);
u_int32_t mrsas_read_reg(struct mrsas_softc *sc, int offset);
u_int8_t
mrsas_build_mptmfi_passthru(struct mrsas_softc *sc,
    struct mrsas_mfi_cmd *mfi_cmd);
void	mrsas_complete_outstanding_ioctls(struct mrsas_softc *sc);
int	mrsas_transition_to_ready(struct mrsas_softc *sc, int ocr);
int	mrsas_init_adapter(struct mrsas_softc *sc);
int	mrsas_alloc_mpt_cmds(struct mrsas_softc *sc);
int	mrsas_alloc_ioc_cmd(struct mrsas_softc *sc);
int	mrsas_alloc_ctlr_info_cmd(struct mrsas_softc *sc);
int	mrsas_ioc_init(struct mrsas_softc *sc);
int	mrsas_bus_scan(struct mrsas_softc *sc);
int	mrsas_issue_dcmd(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd);
int	mrsas_issue_polled(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd);
int	mrsas_reset_ctrl(struct mrsas_softc *sc, u_int8_t reset_reason);
int	mrsas_wait_for_outstanding(struct mrsas_softc *sc, u_int8_t check_reason);
int mrsas_complete_cmd(struct mrsas_softc *sc, u_int32_t MSIxIndex);
int mrsas_reset_targets(struct mrsas_softc *sc);
int
mrsas_issue_blocked_cmd(struct mrsas_softc *sc,
    struct mrsas_mfi_cmd *cmd);
int
mrsas_alloc_tmp_dcmd(struct mrsas_softc *sc, struct mrsas_tmp_dcmd *tcmd,
    int size);
void	mrsas_release_mfi_cmd(struct mrsas_mfi_cmd *cmd);
void	mrsas_wakeup(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd);
void	mrsas_complete_aen(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd);
void	mrsas_complete_abort(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd);
void	mrsas_disable_intr(struct mrsas_softc *sc);
void	mrsas_enable_intr(struct mrsas_softc *sc);
void	mrsas_free_ioc_cmd(struct mrsas_softc *sc);
void	mrsas_free_mem(struct mrsas_softc *sc);
void	mrsas_free_tmp_dcmd(struct mrsas_tmp_dcmd *tmp);
void	mrsas_isr(void *arg);
void	mrsas_teardown_intr(struct mrsas_softc *sc);
void	mrsas_addr_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error);
void	mrsas_kill_hba(struct mrsas_softc *sc);
void	mrsas_aen_handler(struct mrsas_softc *sc);
void
mrsas_write_reg(struct mrsas_softc *sc, int offset,
    u_int32_t value);
void
mrsas_fire_cmd(struct mrsas_softc *sc, u_int32_t req_desc_lo,
    u_int32_t req_desc_hi);
void	mrsas_free_ctlr_info_cmd(struct mrsas_softc *sc);
void
mrsas_complete_mptmfi_passthru(struct mrsas_softc *sc,
    struct mrsas_mfi_cmd *cmd, u_int8_t status);
struct mrsas_mfi_cmd *mrsas_get_mfi_cmd(struct mrsas_softc *sc);

MRSAS_REQUEST_DESCRIPTOR_UNION *mrsas_build_mpt_cmd
        (struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd);

extern int mrsas_cam_attach(struct mrsas_softc *sc);
extern void mrsas_cam_detach(struct mrsas_softc *sc);
extern void mrsas_cmd_done(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd);
extern void mrsas_free_frame(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd);
extern int mrsas_alloc_mfi_cmds(struct mrsas_softc *sc);
extern struct mrsas_mpt_cmd *mrsas_get_mpt_cmd(struct mrsas_softc *sc);
extern int mrsas_passthru(struct mrsas_softc *sc, void *arg, u_long ioctlCmd);
extern uint8_t MR_ValidateMapInfo(struct mrsas_softc *sc);
extern u_int16_t MR_GetLDTgtId(u_int32_t ld, MR_DRV_RAID_MAP_ALL * map);
extern MR_LD_RAID *MR_LdRaidGet(u_int32_t ld, MR_DRV_RAID_MAP_ALL * map);
extern void mrsas_xpt_freeze(struct mrsas_softc *sc);
extern void mrsas_xpt_release(struct mrsas_softc *sc);
extern MRSAS_REQUEST_DESCRIPTOR_UNION *
mrsas_get_request_desc(struct mrsas_softc *sc,
    u_int16_t index);
extern int mrsas_bus_scan_sim(struct mrsas_softc *sc, struct cam_sim *sim);
static int mrsas_alloc_evt_log_info_cmd(struct mrsas_softc *sc);
static void mrsas_free_evt_log_info_cmd(struct mrsas_softc *sc);
void	mrsas_release_mpt_cmd(struct mrsas_mpt_cmd *cmd);

void mrsas_map_mpt_cmd_status(struct mrsas_mpt_cmd *cmd,
	union ccb *ccb_ptr, u_int8_t status, u_int8_t extStatus,
	u_int32_t data_length, u_int8_t *sense);
void
mrsas_write_64bit_req_desc(struct mrsas_softc *sc, u_int32_t req_desc_lo,
    u_int32_t req_desc_hi);


SYSCTL_NODE(_hw, OID_AUTO, mrsas, CTLFLAG_RD, 0, "MRSAS Driver Parameters");

/*
 * PCI device struct and table
 *
 */
typedef struct mrsas_ident {
	uint16_t vendor;
	uint16_t device;
	uint16_t subvendor;
	uint16_t subdevice;
	const char *desc;
}	MRSAS_CTLR_ID;

MRSAS_CTLR_ID device_table[] = {
	{0x1000, MRSAS_TBOLT, 0xffff, 0xffff, "AVAGO Thunderbolt SAS Controller"},
	{0x1000, MRSAS_INVADER, 0xffff, 0xffff, "AVAGO Invader SAS Controller"},
	{0x1000, MRSAS_FURY, 0xffff, 0xffff, "AVAGO Fury SAS Controller"},
	{0x1000, MRSAS_INTRUDER, 0xffff, 0xffff, "AVAGO Intruder SAS Controller"},
	{0x1000, MRSAS_INTRUDER_24, 0xffff, 0xffff, "AVAGO Intruder_24 SAS Controller"},
	{0x1000, MRSAS_CUTLASS_52, 0xffff, 0xffff, "AVAGO Cutlass_52 SAS Controller"},
	{0x1000, MRSAS_CUTLASS_53, 0xffff, 0xffff, "AVAGO Cutlass_53 SAS Controller"},
	{0x1000, MRSAS_VENTURA, 0xffff, 0xffff, "AVAGO Ventura SAS Controller"},
	{0x1000, MRSAS_CRUSADER, 0xffff, 0xffff, "AVAGO Crusader SAS Controller"},
	{0x1000, MRSAS_HARPOON, 0xffff, 0xffff, "AVAGO Harpoon SAS Controller"},
	{0x1000, MRSAS_TOMCAT, 0xffff, 0xffff, "AVAGO Tomcat SAS Controller"},
	{0x1000, MRSAS_VENTURA_4PORT, 0xffff, 0xffff, "AVAGO Ventura_4Port SAS Controller"},
	{0x1000, MRSAS_CRUSADER_4PORT, 0xffff, 0xffff, "AVAGO Crusader_4Port SAS Controller"},
	{0x1000, MRSAS_AERO_10E0, 0xffff, 0xffff, "BROADCOM AERO-10E0 SAS Controller"},
	{0x1000, MRSAS_AERO_10E1, 0xffff, 0xffff, "BROADCOM AERO-10E1 SAS Controller"},
	{0x1000, MRSAS_AERO_10E2, 0xffff, 0xffff, "BROADCOM AERO-10E2 SAS Controller"},
	{0x1000, MRSAS_AERO_10E3, 0xffff, 0xffff, "BROADCOM AERO-10E3 SAS Controller"},
	{0x1000, MRSAS_AERO_10E4, 0xffff, 0xffff, "BROADCOM AERO-10E4 SAS Controller"},
	{0x1000, MRSAS_AERO_10E5, 0xffff, 0xffff, "BROADCOM AERO-10E5 SAS Controller"},
	{0x1000, MRSAS_AERO_10E6, 0xffff, 0xffff, "BROADCOM AERO-10E6 SAS Controller"},
	{0x1000, MRSAS_AERO_10E7, 0xffff, 0xffff, "BROADCOM AERO-10E7 SAS Controller"},
	{0, 0, 0, 0, NULL}
};

/*
 * Character device entry points
 *
 */
static struct cdevsw mrsas_cdevsw = {
	.d_version = D_VERSION,
	.d_open = mrsas_open,
	.d_close = mrsas_close,
	.d_read = mrsas_read,
	.d_write = mrsas_write,
	.d_ioctl = mrsas_ioctl,
	.d_poll = mrsas_poll,
	.d_name = "mrsas",
};

MALLOC_DEFINE(M_MRSAS, "mrsasbuf", "Buffers for the MRSAS driver");

/*
 * In the cdevsw routines, we find our softc by using the si_drv1 member of
 * struct cdev.  We set this variable to point to our softc in our attach
 * routine when we create the /dev entry.
 */
int
mrsas_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct mrsas_softc *sc;

	sc = dev->si_drv1;
	return (0);
}

int
mrsas_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct mrsas_softc *sc;

	sc = dev->si_drv1;
	return (0);
}

int
mrsas_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct mrsas_softc *sc;

	sc = dev->si_drv1;
	return (0);
}
int
mrsas_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct mrsas_softc *sc;

	sc = dev->si_drv1;
	return (0);
}

u_int32_t
mrsas_read_reg_with_retries(struct mrsas_softc *sc, int offset)
{
	u_int32_t i = 0, ret_val;

	if (sc->is_aero) {
		do {
			ret_val = mrsas_read_reg(sc, offset);
			i++;
		} while(ret_val == 0 && i < 3);
	} else
		ret_val = mrsas_read_reg(sc, offset);

	return ret_val;
}

/*
 * Register Read/Write Functions
 *
 */
void
mrsas_write_reg(struct mrsas_softc *sc, int offset,
    u_int32_t value)
{
	bus_space_tag_t bus_tag = sc->bus_tag;
	bus_space_handle_t bus_handle = sc->bus_handle;

	bus_space_write_4(bus_tag, bus_handle, offset, value);
}

u_int32_t
mrsas_read_reg(struct mrsas_softc *sc, int offset)
{
	bus_space_tag_t bus_tag = sc->bus_tag;
	bus_space_handle_t bus_handle = sc->bus_handle;

	return ((u_int32_t)bus_space_read_4(bus_tag, bus_handle, offset));
}


/*
 * Interrupt Disable/Enable/Clear Functions
 *
 */
void
mrsas_disable_intr(struct mrsas_softc *sc)
{
	u_int32_t mask = 0xFFFFFFFF;
	u_int32_t status;

	sc->mask_interrupts = 1;
	mrsas_write_reg(sc, offsetof(mrsas_reg_set, outbound_intr_mask), mask);
	/* Dummy read to force pci flush */
	status = mrsas_read_reg(sc, offsetof(mrsas_reg_set, outbound_intr_mask));
}

void
mrsas_enable_intr(struct mrsas_softc *sc)
{
	u_int32_t mask = MFI_FUSION_ENABLE_INTERRUPT_MASK;
	u_int32_t status;

	sc->mask_interrupts = 0;
	mrsas_write_reg(sc, offsetof(mrsas_reg_set, outbound_intr_status), ~0);
	status = mrsas_read_reg(sc, offsetof(mrsas_reg_set, outbound_intr_status));

	mrsas_write_reg(sc, offsetof(mrsas_reg_set, outbound_intr_mask), ~mask);
	status = mrsas_read_reg(sc, offsetof(mrsas_reg_set, outbound_intr_mask));
}

static int
mrsas_clear_intr(struct mrsas_softc *sc)
{
	u_int32_t status;

	/* Read received interrupt */
	status = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set, outbound_intr_status));

	/* Not our interrupt, so just return */
	if (!(status & MFI_FUSION_ENABLE_INTERRUPT_MASK))
		return (0);

	/* We got a reply interrupt */
	return (1);
}

/*
 * PCI Support Functions
 *
 */
static struct mrsas_ident *
mrsas_find_ident(device_t dev)
{
	struct mrsas_ident *pci_device;

	for (pci_device = device_table; pci_device->vendor != 0; pci_device++) {
		if ((pci_device->vendor == pci_get_vendor(dev)) &&
		    (pci_device->device == pci_get_device(dev)) &&
		    ((pci_device->subvendor == pci_get_subvendor(dev)) ||
		    (pci_device->subvendor == 0xffff)) &&
		    ((pci_device->subdevice == pci_get_subdevice(dev)) ||
		    (pci_device->subdevice == 0xffff)))
			return (pci_device);
	}
	return (NULL);
}

static int
mrsas_probe(device_t dev)
{
	static u_int8_t first_ctrl = 1;
	struct mrsas_ident *id;

	if ((id = mrsas_find_ident(dev)) != NULL) {
		if (first_ctrl) {
			printf("AVAGO MegaRAID SAS FreeBSD mrsas driver version: %s\n",
			    MRSAS_VERSION);
			first_ctrl = 0;
		}
		device_set_desc(dev, id->desc);
		/* between BUS_PROBE_DEFAULT and BUS_PROBE_LOW_PRIORITY */
		return (-30);
	}
	return (ENXIO);
}

/*
 * mrsas_setup_sysctl:	setup sysctl values for mrsas
 * input:				Adapter instance soft state
 *
 * Setup sysctl entries for mrsas driver.
 */
static void
mrsas_setup_sysctl(struct mrsas_softc *sc)
{
	struct sysctl_ctx_list *sysctl_ctx = NULL;
	struct sysctl_oid *sysctl_tree = NULL;
	char tmpstr[80], tmpstr2[80];

	/*
	 * Setup the sysctl variable so the user can change the debug level
	 * on the fly.
	 */
	snprintf(tmpstr, sizeof(tmpstr), "MRSAS controller %d",
	    device_get_unit(sc->mrsas_dev));
	snprintf(tmpstr2, sizeof(tmpstr2), "%d", device_get_unit(sc->mrsas_dev));

	sysctl_ctx = device_get_sysctl_ctx(sc->mrsas_dev);
	if (sysctl_ctx != NULL)
		sysctl_tree = device_get_sysctl_tree(sc->mrsas_dev);

	if (sysctl_tree == NULL) {
		sysctl_ctx_init(&sc->sysctl_ctx);
		sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
		    SYSCTL_STATIC_CHILDREN(_hw_mrsas), OID_AUTO, tmpstr2,
		    CTLFLAG_RD, 0, tmpstr);
		if (sc->sysctl_tree == NULL)
			return;
		sysctl_ctx = &sc->sysctl_ctx;
		sysctl_tree = sc->sysctl_tree;
	}
	SYSCTL_ADD_UINT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "disable_ocr", CTLFLAG_RW, &sc->disableOnlineCtrlReset, 0,
	    "Disable the use of OCR");

	SYSCTL_ADD_STRING(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "driver_version", CTLFLAG_RD, MRSAS_VERSION,
	    strlen(MRSAS_VERSION), "driver version");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "reset_count", CTLFLAG_RD,
	    &sc->reset_count, 0, "number of ocr from start of the day");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "fw_outstanding", CTLFLAG_RD,
	    &sc->fw_outstanding.val_rdonly, 0, "FW outstanding commands");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "io_cmds_highwater", CTLFLAG_RD,
	    &sc->io_cmds_highwater, 0, "Max FW outstanding commands");

	SYSCTL_ADD_UINT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "mrsas_debug", CTLFLAG_RW, &sc->mrsas_debug, 0,
	    "Driver debug level");

	SYSCTL_ADD_UINT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "mrsas_io_timeout", CTLFLAG_RW, &sc->mrsas_io_timeout,
	    0, "Driver IO timeout value in mili-second.");

	SYSCTL_ADD_UINT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "mrsas_fw_fault_check_delay", CTLFLAG_RW,
	    &sc->mrsas_fw_fault_check_delay,
	    0, "FW fault check thread delay in seconds. <default is 1 sec>");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "reset_in_progress", CTLFLAG_RD,
	    &sc->reset_in_progress, 0, "ocr in progress status");

	SYSCTL_ADD_UINT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "block_sync_cache", CTLFLAG_RW,
	    &sc->block_sync_cache, 0,
	    "Block SYNC CACHE at driver. <default: 0, send it to FW>");
	SYSCTL_ADD_UINT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "stream detection", CTLFLAG_RW,
		&sc->drv_stream_detection, 0,
		"Disable/Enable Stream detection. <default: 1, Enable Stream Detection>");
	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "prp_count", CTLFLAG_RD,
	    &sc->prp_count.val_rdonly, 0, "Number of IOs for which PRPs are built");
	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "SGE holes", CTLFLAG_RD,
	    &sc->sge_holes.val_rdonly, 0, "Number of IOs with holes in SGEs");
}

/*
 * mrsas_get_tunables:	get tunable parameters.
 * input:				Adapter instance soft state
 *
 * Get tunable parameters. This will help to debug driver at boot time.
 */
static void
mrsas_get_tunables(struct mrsas_softc *sc)
{
	char tmpstr[80];

	/* XXX default to some debugging for now */
	sc->mrsas_debug =
		(MRSAS_FAULT | MRSAS_OCR | MRSAS_INFO | MRSAS_TRACE | MRSAS_AEN);
	sc->mrsas_io_timeout = MRSAS_IO_TIMEOUT;
	sc->mrsas_fw_fault_check_delay = 1;
	sc->reset_count = 0;
	sc->reset_in_progress = 0;
	sc->block_sync_cache = 0;
	sc->drv_stream_detection = 1;

	/*
	 * Grab the global variables.
	 */
	TUNABLE_INT_FETCH("hw.mrsas.debug_level", &sc->mrsas_debug);

	/*
	 * Grab the global variables.
	 */
	TUNABLE_INT_FETCH("hw.mrsas.lb_pending_cmds", &sc->lb_pending_cmds);

	/* Grab the unit-instance variables */
	snprintf(tmpstr, sizeof(tmpstr), "dev.mrsas.%d.debug_level",
	    device_get_unit(sc->mrsas_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->mrsas_debug);
}

/*
 * mrsas_alloc_evt_log_info cmd: Allocates memory to get event log information.
 * Used to get sequence number at driver load time.
 * input:		Adapter soft state
 *
 * Allocates DMAable memory for the event log info internal command.
 */
int
mrsas_alloc_evt_log_info_cmd(struct mrsas_softc *sc)
{
	int el_info_size;

	/* Allocate get event log info command */
	el_info_size = sizeof(struct mrsas_evt_log_info);
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    1, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    el_info_size,
	    1,
	    el_info_size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &sc->el_info_tag)) {
		device_printf(sc->mrsas_dev, "Cannot allocate event log info tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->el_info_tag, (void **)&sc->el_info_mem,
	    BUS_DMA_NOWAIT, &sc->el_info_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot allocate event log info cmd mem\n");
		return (ENOMEM);
	}
	if (bus_dmamap_load(sc->el_info_tag, sc->el_info_dmamap,
	    sc->el_info_mem, el_info_size, mrsas_addr_cb,
	    &sc->el_info_phys_addr, BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load event log info cmd mem\n");
		return (ENOMEM);
	}
	memset(sc->el_info_mem, 0, el_info_size);
	return (0);
}

/*
 * mrsas_free_evt_info_cmd:	Free memory for Event log info command
 * input:					Adapter soft state
 *
 * Deallocates memory for the event log info internal command.
 */
void
mrsas_free_evt_log_info_cmd(struct mrsas_softc *sc)
{
	if (sc->el_info_phys_addr)
		bus_dmamap_unload(sc->el_info_tag, sc->el_info_dmamap);
	if (sc->el_info_mem != NULL)
		bus_dmamem_free(sc->el_info_tag, sc->el_info_mem, sc->el_info_dmamap);
	if (sc->el_info_tag != NULL)
		bus_dma_tag_destroy(sc->el_info_tag);
}

/*
 *  mrsas_get_seq_num:	Get latest event sequence number
 *  @sc:				Adapter soft state
 *  @eli:				Firmware event log sequence number information.
 *
 * Firmware maintains a log of all events in a non-volatile area.
 * Driver get the sequence number using DCMD
 * "MR_DCMD_CTRL_EVENT_GET_INFO" at driver load time.
 */

static int
mrsas_get_seq_num(struct mrsas_softc *sc,
    struct mrsas_evt_log_info *eli)
{
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;
	u_int8_t do_ocr = 1, retcode = 0;

	cmd = mrsas_get_mfi_cmd(sc);

	if (!cmd) {
		device_printf(sc->mrsas_dev, "Failed to get a free cmd\n");
		return -ENOMEM;
	}
	dcmd = &cmd->frame->dcmd;

	if (mrsas_alloc_evt_log_info_cmd(sc) != SUCCESS) {
		device_printf(sc->mrsas_dev, "Cannot allocate evt log info cmd\n");
		mrsas_release_mfi_cmd(cmd);
		return -ENOMEM;
	}
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0x0;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = sizeof(struct mrsas_evt_log_info);
	dcmd->opcode = MR_DCMD_CTRL_EVENT_GET_INFO;
	dcmd->sgl.sge32[0].phys_addr = sc->el_info_phys_addr;
	dcmd->sgl.sge32[0].length = sizeof(struct mrsas_evt_log_info);

	retcode = mrsas_issue_blocked_cmd(sc, cmd);
	if (retcode == ETIMEDOUT)
		goto dcmd_timeout;

	do_ocr = 0;
	/*
	 * Copy the data back into callers buffer
	 */
	memcpy(eli, sc->el_info_mem, sizeof(struct mrsas_evt_log_info));
	mrsas_free_evt_log_info_cmd(sc);

dcmd_timeout:
	if (do_ocr)
		sc->do_timedout_reset = MFI_DCMD_TIMEOUT_OCR;
	else
		mrsas_release_mfi_cmd(cmd);

	return retcode;
}


/*
 *  mrsas_register_aen:		Register for asynchronous event notification
 *  @sc:			Adapter soft state
 *  @seq_num:			Starting sequence number
 *  @class_locale:		Class of the event
 *
 *  This function subscribes for events beyond the @seq_num
 *  and type @class_locale.
 *
 */
static int
mrsas_register_aen(struct mrsas_softc *sc, u_int32_t seq_num,
    u_int32_t class_locale_word)
{
	int ret_val;
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;
	union mrsas_evt_class_locale curr_aen;
	union mrsas_evt_class_locale prev_aen;

	/*
	 * If there an AEN pending already (aen_cmd), check if the
	 * class_locale of that pending AEN is inclusive of the new AEN
	 * request we currently have. If it is, then we don't have to do
	 * anything. In other words, whichever events the current AEN request
	 * is subscribing to, have already been subscribed to. If the old_cmd
	 * is _not_ inclusive, then we have to abort that command, form a
	 * class_locale that is superset of both old and current and re-issue
	 * to the FW
	 */

	curr_aen.word = class_locale_word;

	if (sc->aen_cmd) {

		prev_aen.word = sc->aen_cmd->frame->dcmd.mbox.w[1];

		/*
		 * A class whose enum value is smaller is inclusive of all
		 * higher values. If a PROGRESS (= -1) was previously
		 * registered, then a new registration requests for higher
		 * classes need not be sent to FW. They are automatically
		 * included. Locale numbers don't have such hierarchy. They
		 * are bitmap values
		 */
		if ((prev_aen.members.class <= curr_aen.members.class) &&
		    !((prev_aen.members.locale & curr_aen.members.locale) ^
		    curr_aen.members.locale)) {
			/*
			 * Previously issued event registration includes
			 * current request. Nothing to do.
			 */
			return 0;
		} else {
			curr_aen.members.locale |= prev_aen.members.locale;

			if (prev_aen.members.class < curr_aen.members.class)
				curr_aen.members.class = prev_aen.members.class;

			sc->aen_cmd->abort_aen = 1;
			ret_val = mrsas_issue_blocked_abort_cmd(sc,
			    sc->aen_cmd);

			if (ret_val) {
				printf("mrsas: Failed to abort previous AEN command\n");
				return ret_val;
			} else
				sc->aen_cmd = NULL;
		}
	}
	cmd = mrsas_get_mfi_cmd(sc);
	if (!cmd)
		return ENOMEM;

	dcmd = &cmd->frame->dcmd;

	memset(sc->evt_detail_mem, 0, sizeof(struct mrsas_evt_detail));

	/*
	 * Prepare DCMD for aen registration
	 */
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0x0;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = sizeof(struct mrsas_evt_detail);
	dcmd->opcode = MR_DCMD_CTRL_EVENT_WAIT;
	dcmd->mbox.w[0] = seq_num;
	sc->last_seq_num = seq_num;
	dcmd->mbox.w[1] = curr_aen.word;
	dcmd->sgl.sge32[0].phys_addr = (u_int32_t)sc->evt_detail_phys_addr;
	dcmd->sgl.sge32[0].length = sizeof(struct mrsas_evt_detail);

	if (sc->aen_cmd != NULL) {
		mrsas_release_mfi_cmd(cmd);
		return 0;
	}
	/*
	 * Store reference to the cmd used to register for AEN. When an
	 * application wants us to register for AEN, we have to abort this
	 * cmd and re-register with a new EVENT LOCALE supplied by that app
	 */
	sc->aen_cmd = cmd;

	/*
	 * Issue the aen registration frame
	 */
	if (mrsas_issue_dcmd(sc, cmd)) {
		device_printf(sc->mrsas_dev, "Cannot issue AEN DCMD command.\n");
		return (1);
	}
	return 0;
}

/*
 * mrsas_start_aen:	Subscribes to AEN during driver load time
 * @instance:		Adapter soft state
 */
static int
mrsas_start_aen(struct mrsas_softc *sc)
{
	struct mrsas_evt_log_info eli;
	union mrsas_evt_class_locale class_locale;


	/* Get the latest sequence number from FW */

	memset(&eli, 0, sizeof(eli));

	if (mrsas_get_seq_num(sc, &eli))
		return -1;

	/* Register AEN with FW for latest sequence number plus 1 */
	class_locale.members.reserved = 0;
	class_locale.members.locale = MR_EVT_LOCALE_ALL;
	class_locale.members.class = MR_EVT_CLASS_DEBUG;

	return mrsas_register_aen(sc, eli.newest_seq_num + 1,
	    class_locale.word);

}

/*
 * mrsas_setup_msix:	Allocate MSI-x vectors
 * @sc:					adapter soft state
 */
static int
mrsas_setup_msix(struct mrsas_softc *sc)
{
	int i;

	for (i = 0; i < sc->msix_vectors; i++) {
		sc->irq_context[i].sc = sc;
		sc->irq_context[i].MSIxIndex = i;
		sc->irq_id[i] = i + 1;
		sc->mrsas_irq[i] = bus_alloc_resource_any
		    (sc->mrsas_dev, SYS_RES_IRQ, &sc->irq_id[i]
		    ,RF_ACTIVE);
		if (sc->mrsas_irq[i] == NULL) {
			device_printf(sc->mrsas_dev, "Can't allocate MSI-x\n");
			goto irq_alloc_failed;
		}
		if (bus_setup_intr(sc->mrsas_dev,
		    sc->mrsas_irq[i],
		    INTR_MPSAFE | INTR_TYPE_CAM,
		    NULL, mrsas_isr, &sc->irq_context[i],
		    &sc->intr_handle[i])) {
			device_printf(sc->mrsas_dev,
			    "Cannot set up MSI-x interrupt handler\n");
			goto irq_alloc_failed;
		}
	}
	return SUCCESS;

irq_alloc_failed:
	mrsas_teardown_intr(sc);
	return (FAIL);
}

/*
 * mrsas_allocate_msix:		Setup MSI-x vectors
 * @sc:						adapter soft state
 */
static int
mrsas_allocate_msix(struct mrsas_softc *sc)
{
	if (pci_alloc_msix(sc->mrsas_dev, &sc->msix_vectors) == 0) {
		device_printf(sc->mrsas_dev, "Using MSI-X with %d number"
		    " of vectors\n", sc->msix_vectors);
	} else {
		device_printf(sc->mrsas_dev, "MSI-x setup failed\n");
		goto irq_alloc_failed;
	}
	return SUCCESS;

irq_alloc_failed:
	mrsas_teardown_intr(sc);
	return (FAIL);
}

/*
 * mrsas_attach:	PCI entry point
 * input:			pointer to device struct
 *
 * Performs setup of PCI and registers, initializes mutexes and linked lists,
 * registers interrupts and CAM, and initializes   the adapter/controller to
 * its proper state.
 */
static int
mrsas_attach(device_t dev)
{
	struct mrsas_softc *sc = device_get_softc(dev);
	uint32_t cmd, error;

	memset(sc, 0, sizeof(struct mrsas_softc));

	/* Look up our softc and initialize its fields. */
	sc->mrsas_dev = dev;
	sc->device_id = pci_get_device(dev);

	switch (sc->device_id) {
	case MRSAS_INVADER:
	case MRSAS_FURY:
	case MRSAS_INTRUDER:
	case MRSAS_INTRUDER_24:
	case MRSAS_CUTLASS_52:
	case MRSAS_CUTLASS_53:
		sc->mrsas_gen3_ctrl = 1;
		break;
	case MRSAS_VENTURA:
	case MRSAS_CRUSADER:
	case MRSAS_HARPOON:
	case MRSAS_TOMCAT:
	case MRSAS_VENTURA_4PORT:
	case MRSAS_CRUSADER_4PORT:
		sc->is_ventura = true;
		break;
	case MRSAS_AERO_10E1:
	case MRSAS_AERO_10E5:
		device_printf(dev, "Adapter is in configurable secure mode\n");
	case MRSAS_AERO_10E2:
	case MRSAS_AERO_10E6:
		sc->is_aero = true;
		break;
	case MRSAS_AERO_10E0:
	case MRSAS_AERO_10E3:
	case MRSAS_AERO_10E4:
	case MRSAS_AERO_10E7:
		device_printf(dev, "Adapter is in non-secure mode\n");
		return SUCCESS;

	}

	mrsas_get_tunables(sc);

	/*
	 * Set up PCI and registers
	 */
	cmd = pci_read_config(dev, PCIR_COMMAND, 2);
	if ((cmd & PCIM_CMD_PORTEN) == 0) {
		return (ENXIO);
	}
	/* Force the busmaster enable bit on. */
	cmd |= PCIM_CMD_BUSMASTEREN;
	pci_write_config(dev, PCIR_COMMAND, cmd, 2);

	/* For Ventura/Aero system registers are mapped to BAR0 */
	if (sc->is_ventura || sc->is_aero)
		sc->reg_res_id = PCIR_BAR(0);	/* BAR0 offset */
	else
		sc->reg_res_id = PCIR_BAR(1);	/* BAR1 offset */

	if ((sc->reg_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &(sc->reg_res_id), RF_ACTIVE))
	    == NULL) {
		device_printf(dev, "Cannot allocate PCI registers\n");
		goto attach_fail;
	}
	sc->bus_tag = rman_get_bustag(sc->reg_res);
	sc->bus_handle = rman_get_bushandle(sc->reg_res);

	/* Intialize mutexes */
	mtx_init(&sc->sim_lock, "mrsas_sim_lock", NULL, MTX_DEF);
	mtx_init(&sc->pci_lock, "mrsas_pci_lock", NULL, MTX_DEF);
	mtx_init(&sc->io_lock, "mrsas_io_lock", NULL, MTX_DEF);
	mtx_init(&sc->aen_lock, "mrsas_aen_lock", NULL, MTX_DEF);
	mtx_init(&sc->ioctl_lock, "mrsas_ioctl_lock", NULL, MTX_SPIN);
	mtx_init(&sc->mpt_cmd_pool_lock, "mrsas_mpt_cmd_pool_lock", NULL, MTX_DEF);
	mtx_init(&sc->mfi_cmd_pool_lock, "mrsas_mfi_cmd_pool_lock", NULL, MTX_DEF);
	mtx_init(&sc->raidmap_lock, "mrsas_raidmap_lock", NULL, MTX_DEF);
	mtx_init(&sc->stream_lock, "mrsas_stream_lock", NULL, MTX_DEF);

	/* Intialize linked list */
	TAILQ_INIT(&sc->mrsas_mpt_cmd_list_head);
	TAILQ_INIT(&sc->mrsas_mfi_cmd_list_head);

	mrsas_atomic_set(&sc->fw_outstanding, 0);
	mrsas_atomic_set(&sc->target_reset_outstanding, 0);
	mrsas_atomic_set(&sc->prp_count, 0);
	mrsas_atomic_set(&sc->sge_holes, 0);

	sc->io_cmds_highwater = 0;

	sc->adprecovery = MRSAS_HBA_OPERATIONAL;
	sc->UnevenSpanSupport = 0;

	sc->msix_enable = 0;

	/* Initialize Firmware */
	if (mrsas_init_fw(sc) != SUCCESS) {
		goto attach_fail_fw;
	}
	/* Register mrsas to CAM layer */
	if ((mrsas_cam_attach(sc) != SUCCESS)) {
		goto attach_fail_cam;
	}
	/* Register IRQs */
	if (mrsas_setup_irq(sc) != SUCCESS) {
		goto attach_fail_irq;
	}
	error = mrsas_kproc_create(mrsas_ocr_thread, sc,
	    &sc->ocr_thread, 0, 0, "mrsas_ocr%d",
	    device_get_unit(sc->mrsas_dev));
	if (error) {
		device_printf(sc->mrsas_dev, "Error %d starting OCR thread\n", error);
		goto attach_fail_ocr_thread;
	}
	/*
	 * After FW initialization and OCR thread creation
	 * we will defer the cdev creation, AEN setup on ICH callback
	 */
	sc->mrsas_ich.ich_func = mrsas_ich_startup;
	sc->mrsas_ich.ich_arg = sc;
	if (config_intrhook_establish(&sc->mrsas_ich) != 0) {
		device_printf(sc->mrsas_dev, "Config hook is already established\n");
	}
	mrsas_setup_sysctl(sc);
	return SUCCESS;

attach_fail_ocr_thread:
	if (sc->ocr_thread_active)
		wakeup(&sc->ocr_chan);
attach_fail_irq:
	mrsas_teardown_intr(sc);
attach_fail_cam:
	mrsas_cam_detach(sc);
attach_fail_fw:
	/* if MSIX vector is allocated and FW Init FAILED then release MSIX */
	if (sc->msix_enable == 1)
		pci_release_msi(sc->mrsas_dev);
	mrsas_free_mem(sc);
	mtx_destroy(&sc->sim_lock);
	mtx_destroy(&sc->aen_lock);
	mtx_destroy(&sc->pci_lock);
	mtx_destroy(&sc->io_lock);
	mtx_destroy(&sc->ioctl_lock);
	mtx_destroy(&sc->mpt_cmd_pool_lock);
	mtx_destroy(&sc->mfi_cmd_pool_lock);
	mtx_destroy(&sc->raidmap_lock);
	mtx_destroy(&sc->stream_lock);
attach_fail:
	if (sc->reg_res) {
		bus_release_resource(sc->mrsas_dev, SYS_RES_MEMORY,
		    sc->reg_res_id, sc->reg_res);
	}
	return (ENXIO);
}

/*
 * Interrupt config hook
 */
static void
mrsas_ich_startup(void *arg)
{
	int i = 0;
	struct mrsas_softc *sc = (struct mrsas_softc *)arg;

	/*
	 * Intialize a counting Semaphore to take care no. of concurrent IOCTLs
	 */
	sema_init(&sc->ioctl_count_sema, MRSAS_MAX_IOCTL_CMDS,
	    IOCTL_SEMA_DESCRIPTION);

	/* Create a /dev entry for mrsas controller. */
	sc->mrsas_cdev = make_dev(&mrsas_cdevsw, device_get_unit(sc->mrsas_dev), UID_ROOT,
	    GID_OPERATOR, (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP), "mrsas%u",
	    device_get_unit(sc->mrsas_dev));

	if (device_get_unit(sc->mrsas_dev) == 0) {
		make_dev_alias_p(MAKEDEV_CHECKNAME,
		    &sc->mrsas_linux_emulator_cdev, sc->mrsas_cdev,
		    "megaraid_sas_ioctl_node");
	}
	if (sc->mrsas_cdev)
		sc->mrsas_cdev->si_drv1 = sc;

	/*
	 * Add this controller to mrsas_mgmt_info structure so that it can be
	 * exported to management applications
	 */
	if (device_get_unit(sc->mrsas_dev) == 0)
		memset(&mrsas_mgmt_info, 0, sizeof(mrsas_mgmt_info));

	mrsas_mgmt_info.count++;
	mrsas_mgmt_info.sc_ptr[mrsas_mgmt_info.max_index] = sc;
	mrsas_mgmt_info.max_index++;

	/* Enable Interrupts */
	mrsas_enable_intr(sc);

	/* Call DCMD get_pd_info for all system PDs */
	for (i = 0; i < MRSAS_MAX_PD; i++) {
		if ((sc->target_list[i].target_id != 0xffff) &&
			sc->pd_info_mem)
			mrsas_get_pd_info(sc, sc->target_list[i].target_id);
	}

	/* Initiate AEN (Asynchronous Event Notification) */
	if (mrsas_start_aen(sc)) {
		device_printf(sc->mrsas_dev, "Error: AEN registration FAILED !!! "
		    "Further events from the controller will not be communicated.\n"
		    "Either there is some problem in the controller"
		    "or the controller does not support AEN.\n"
		    "Please contact to the SUPPORT TEAM if the problem persists\n");
	}
	if (sc->mrsas_ich.ich_arg != NULL) {
		device_printf(sc->mrsas_dev, "Disestablish mrsas intr hook\n");
		config_intrhook_disestablish(&sc->mrsas_ich);
		sc->mrsas_ich.ich_arg = NULL;
	}
}

/*
 * mrsas_detach:	De-allocates and teardown resources
 * input:			pointer to device struct
 *
 * This function is the entry point for device disconnect and detach.
 * It performs memory de-allocations, shutdown of the controller and various
 * teardown and destroy resource functions.
 */
static int
mrsas_detach(device_t dev)
{
	struct mrsas_softc *sc;
	int i = 0;

	sc = device_get_softc(dev);
	sc->remove_in_progress = 1;

	/* Destroy the character device so no other IOCTL will be handled */
	if ((device_get_unit(dev) == 0) && sc->mrsas_linux_emulator_cdev)
		destroy_dev(sc->mrsas_linux_emulator_cdev);
	destroy_dev(sc->mrsas_cdev);

	/*
	 * Take the instance off the instance array. Note that we will not
	 * decrement the max_index. We let this array be sparse array
	 */
	for (i = 0; i < mrsas_mgmt_info.max_index; i++) {
		if (mrsas_mgmt_info.sc_ptr[i] == sc) {
			mrsas_mgmt_info.count--;
			mrsas_mgmt_info.sc_ptr[i] = NULL;
			break;
		}
	}

	if (sc->ocr_thread_active)
		wakeup(&sc->ocr_chan);
	while (sc->reset_in_progress) {
		i++;
		if (!(i % MRSAS_RESET_NOTICE_INTERVAL)) {
			mrsas_dprint(sc, MRSAS_INFO,
			    "[%2d]waiting for OCR to be finished from %s\n", i, __func__);
		}
		pause("mr_shutdown", hz);
	}
	i = 0;
	while (sc->ocr_thread_active) {
		i++;
		if (!(i % MRSAS_RESET_NOTICE_INTERVAL)) {
			mrsas_dprint(sc, MRSAS_INFO,
			    "[%2d]waiting for "
			    "mrsas_ocr thread to quit ocr %d\n", i,
			    sc->ocr_thread_active);
		}
		pause("mr_shutdown", hz);
	}
	mrsas_flush_cache(sc);
	mrsas_shutdown_ctlr(sc, MR_DCMD_CTRL_SHUTDOWN);
	mrsas_disable_intr(sc);

	if ((sc->is_ventura || sc->is_aero) && sc->streamDetectByLD) {
		for (i = 0; i < MAX_LOGICAL_DRIVES_EXT; ++i)
			free(sc->streamDetectByLD[i], M_MRSAS);
		free(sc->streamDetectByLD, M_MRSAS);
		sc->streamDetectByLD = NULL;
	}

	mrsas_cam_detach(sc);
	mrsas_teardown_intr(sc);
	mrsas_free_mem(sc);
	mtx_destroy(&sc->sim_lock);
	mtx_destroy(&sc->aen_lock);
	mtx_destroy(&sc->pci_lock);
	mtx_destroy(&sc->io_lock);
	mtx_destroy(&sc->ioctl_lock);
	mtx_destroy(&sc->mpt_cmd_pool_lock);
	mtx_destroy(&sc->mfi_cmd_pool_lock);
	mtx_destroy(&sc->raidmap_lock);
	mtx_destroy(&sc->stream_lock);

	/* Wait for all the semaphores to be released */
	while (sema_value(&sc->ioctl_count_sema) != MRSAS_MAX_IOCTL_CMDS)
		pause("mr_shutdown", hz);

	/* Destroy the counting semaphore created for Ioctl */
	sema_destroy(&sc->ioctl_count_sema);

	if (sc->reg_res) {
		bus_release_resource(sc->mrsas_dev,
		    SYS_RES_MEMORY, sc->reg_res_id, sc->reg_res);
	}
	if (sc->sysctl_tree != NULL)
		sysctl_ctx_free(&sc->sysctl_ctx);

	return (0);
}

/*
 * mrsas_free_mem:		Frees allocated memory
 * input:				Adapter instance soft state
 *
 * This function is called from mrsas_detach() to free previously allocated
 * memory.
 */
void
mrsas_free_mem(struct mrsas_softc *sc)
{
	int i;
	u_int32_t max_fw_cmds;
	struct mrsas_mfi_cmd *mfi_cmd;
	struct mrsas_mpt_cmd *mpt_cmd;

	/*
	 * Free RAID map memory
	 */
	for (i = 0; i < 2; i++) {
		if (sc->raidmap_phys_addr[i])
			bus_dmamap_unload(sc->raidmap_tag[i], sc->raidmap_dmamap[i]);
		if (sc->raidmap_mem[i] != NULL)
			bus_dmamem_free(sc->raidmap_tag[i], sc->raidmap_mem[i], sc->raidmap_dmamap[i]);
		if (sc->raidmap_tag[i] != NULL)
			bus_dma_tag_destroy(sc->raidmap_tag[i]);

		if (sc->ld_drv_map[i] != NULL)
			free(sc->ld_drv_map[i], M_MRSAS);
	}
	for (i = 0; i < 2; i++) {
		if (sc->jbodmap_phys_addr[i])
			bus_dmamap_unload(sc->jbodmap_tag[i], sc->jbodmap_dmamap[i]);
		if (sc->jbodmap_mem[i] != NULL)
			bus_dmamem_free(sc->jbodmap_tag[i], sc->jbodmap_mem[i], sc->jbodmap_dmamap[i]);
		if (sc->jbodmap_tag[i] != NULL)
			bus_dma_tag_destroy(sc->jbodmap_tag[i]);
	}
	/*
	 * Free version buffer memory
	 */
	if (sc->verbuf_phys_addr)
		bus_dmamap_unload(sc->verbuf_tag, sc->verbuf_dmamap);
	if (sc->verbuf_mem != NULL)
		bus_dmamem_free(sc->verbuf_tag, sc->verbuf_mem, sc->verbuf_dmamap);
	if (sc->verbuf_tag != NULL)
		bus_dma_tag_destroy(sc->verbuf_tag);


	/*
	 * Free sense buffer memory
	 */
	if (sc->sense_phys_addr)
		bus_dmamap_unload(sc->sense_tag, sc->sense_dmamap);
	if (sc->sense_mem != NULL)
		bus_dmamem_free(sc->sense_tag, sc->sense_mem, sc->sense_dmamap);
	if (sc->sense_tag != NULL)
		bus_dma_tag_destroy(sc->sense_tag);

	/*
	 * Free chain frame memory
	 */
	if (sc->chain_frame_phys_addr)
		bus_dmamap_unload(sc->chain_frame_tag, sc->chain_frame_dmamap);
	if (sc->chain_frame_mem != NULL)
		bus_dmamem_free(sc->chain_frame_tag, sc->chain_frame_mem, sc->chain_frame_dmamap);
	if (sc->chain_frame_tag != NULL)
		bus_dma_tag_destroy(sc->chain_frame_tag);

	/*
	 * Free IO Request memory
	 */
	if (sc->io_request_phys_addr)
		bus_dmamap_unload(sc->io_request_tag, sc->io_request_dmamap);
	if (sc->io_request_mem != NULL)
		bus_dmamem_free(sc->io_request_tag, sc->io_request_mem, sc->io_request_dmamap);
	if (sc->io_request_tag != NULL)
		bus_dma_tag_destroy(sc->io_request_tag);

	/*
	 * Free Reply Descriptor memory
	 */
	if (sc->reply_desc_phys_addr)
		bus_dmamap_unload(sc->reply_desc_tag, sc->reply_desc_dmamap);
	if (sc->reply_desc_mem != NULL)
		bus_dmamem_free(sc->reply_desc_tag, sc->reply_desc_mem, sc->reply_desc_dmamap);
	if (sc->reply_desc_tag != NULL)
		bus_dma_tag_destroy(sc->reply_desc_tag);

	/*
	 * Free event detail memory
	 */
	if (sc->evt_detail_phys_addr)
		bus_dmamap_unload(sc->evt_detail_tag, sc->evt_detail_dmamap);
	if (sc->evt_detail_mem != NULL)
		bus_dmamem_free(sc->evt_detail_tag, sc->evt_detail_mem, sc->evt_detail_dmamap);
	if (sc->evt_detail_tag != NULL)
		bus_dma_tag_destroy(sc->evt_detail_tag);

	/*
	 * Free PD info memory
	 */
	if (sc->pd_info_phys_addr)
		bus_dmamap_unload(sc->pd_info_tag, sc->pd_info_dmamap);
	if (sc->pd_info_mem != NULL)
		bus_dmamem_free(sc->pd_info_tag, sc->pd_info_mem, sc->pd_info_dmamap);
	if (sc->pd_info_tag != NULL)
		bus_dma_tag_destroy(sc->pd_info_tag);

	/*
	 * Free MFI frames
	 */
	if (sc->mfi_cmd_list) {
		for (i = 0; i < MRSAS_MAX_MFI_CMDS; i++) {
			mfi_cmd = sc->mfi_cmd_list[i];
			mrsas_free_frame(sc, mfi_cmd);
		}
	}
	if (sc->mficmd_frame_tag != NULL)
		bus_dma_tag_destroy(sc->mficmd_frame_tag);

	/*
	 * Free MPT internal command list
	 */
	max_fw_cmds = sc->max_fw_cmds;
	if (sc->mpt_cmd_list) {
		for (i = 0; i < max_fw_cmds; i++) {
			mpt_cmd = sc->mpt_cmd_list[i];
			bus_dmamap_destroy(sc->data_tag, mpt_cmd->data_dmamap);
			free(sc->mpt_cmd_list[i], M_MRSAS);
		}
		free(sc->mpt_cmd_list, M_MRSAS);
		sc->mpt_cmd_list = NULL;
	}
	/*
	 * Free MFI internal command list
	 */

	if (sc->mfi_cmd_list) {
		for (i = 0; i < MRSAS_MAX_MFI_CMDS; i++) {
			free(sc->mfi_cmd_list[i], M_MRSAS);
		}
		free(sc->mfi_cmd_list, M_MRSAS);
		sc->mfi_cmd_list = NULL;
	}
	/*
	 * Free request descriptor memory
	 */
	free(sc->req_desc, M_MRSAS);
	sc->req_desc = NULL;

	/*
	 * Destroy parent tag
	 */
	if (sc->mrsas_parent_tag != NULL)
		bus_dma_tag_destroy(sc->mrsas_parent_tag);

	/*
	 * Free ctrl_info memory
	 */
	if (sc->ctrl_info != NULL)
		free(sc->ctrl_info, M_MRSAS);
}

/*
 * mrsas_teardown_intr:	Teardown interrupt
 * input:				Adapter instance soft state
 *
 * This function is called from mrsas_detach() to teardown and release bus
 * interrupt resourse.
 */
void
mrsas_teardown_intr(struct mrsas_softc *sc)
{
	int i;

	if (!sc->msix_enable) {
		if (sc->intr_handle[0])
			bus_teardown_intr(sc->mrsas_dev, sc->mrsas_irq[0], sc->intr_handle[0]);
		if (sc->mrsas_irq[0] != NULL)
			bus_release_resource(sc->mrsas_dev, SYS_RES_IRQ,
			    sc->irq_id[0], sc->mrsas_irq[0]);
		sc->intr_handle[0] = NULL;
	} else {
		for (i = 0; i < sc->msix_vectors; i++) {
			if (sc->intr_handle[i])
				bus_teardown_intr(sc->mrsas_dev, sc->mrsas_irq[i],
				    sc->intr_handle[i]);

			if (sc->mrsas_irq[i] != NULL)
				bus_release_resource(sc->mrsas_dev, SYS_RES_IRQ,
				    sc->irq_id[i], sc->mrsas_irq[i]);

			sc->intr_handle[i] = NULL;
		}
		pci_release_msi(sc->mrsas_dev);
	}

}

/*
 * mrsas_suspend:	Suspend entry point
 * input:			Device struct pointer
 *
 * This function is the entry point for system suspend from the OS.
 */
static int
mrsas_suspend(device_t dev)
{
	/* This will be filled when the driver will have hibernation support */
	return (0);
}

/*
 * mrsas_resume:	Resume entry point
 * input:			Device struct pointer
 *
 * This function is the entry point for system resume from the OS.
 */
static int
mrsas_resume(device_t dev)
{
	/* This will be filled when the driver will have hibernation support */
	return (0);
}

/**
 * mrsas_get_softc_instance:    Find softc instance based on cmd type
 *
 * This function will return softc instance based on cmd type.
 * In some case, application fire ioctl on required management instance and
 * do not provide host_no. Use cdev->si_drv1 to get softc instance for those
 * case, else get the softc instance from host_no provided by application in
 * user data.
 */

static struct mrsas_softc *
mrsas_get_softc_instance(struct cdev *dev, u_long cmd, caddr_t arg)
{
	struct mrsas_softc *sc = NULL;
	struct mrsas_iocpacket *user_ioc = (struct mrsas_iocpacket *)arg;

	if (cmd == MRSAS_IOC_GET_PCI_INFO) {
		sc = dev->si_drv1;
	} else {
		/*
		 * get the Host number & the softc from data sent by the
		 * Application
		 */
		sc = mrsas_mgmt_info.sc_ptr[user_ioc->host_no];
		if (sc == NULL)
			printf("There is no Controller number %d\n",
			    user_ioc->host_no);
		else if (user_ioc->host_no >= mrsas_mgmt_info.max_index)
			mrsas_dprint(sc, MRSAS_FAULT,
			    "Invalid Controller number %d\n", user_ioc->host_no);
	}

	return sc;
}

/*
 * mrsas_ioctl:	IOCtl commands entry point.
 *
 * This function is the entry point for IOCtls from the OS.  It calls the
 * appropriate function for processing depending on the command received.
 */
static int
mrsas_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int flag,
    struct thread *td)
{
	struct mrsas_softc *sc;
	int ret = 0, i = 0;
	MRSAS_DRV_PCI_INFORMATION *pciDrvInfo;

	sc = mrsas_get_softc_instance(dev, cmd, arg);
	if (!sc)
		return ENOENT;

	if (sc->remove_in_progress ||
		(sc->adprecovery == MRSAS_HW_CRITICAL_ERROR)) {
		mrsas_dprint(sc, MRSAS_INFO,
		    "Either driver remove or shutdown called or "
			"HW is in unrecoverable critical error state.\n");
		return ENOENT;
	}
	mtx_lock_spin(&sc->ioctl_lock);
	if (!sc->reset_in_progress) {
		mtx_unlock_spin(&sc->ioctl_lock);
		goto do_ioctl;
	}
	mtx_unlock_spin(&sc->ioctl_lock);
	while (sc->reset_in_progress) {
		i++;
		if (!(i % MRSAS_RESET_NOTICE_INTERVAL)) {
			mrsas_dprint(sc, MRSAS_INFO,
			    "[%2d]waiting for OCR to be finished from %s\n", i, __func__);
		}
		pause("mr_ioctl", hz);
	}

do_ioctl:
	switch (cmd) {
	case MRSAS_IOC_FIRMWARE_PASS_THROUGH64:
#ifdef COMPAT_FREEBSD32
	case MRSAS_IOC_FIRMWARE_PASS_THROUGH32:
#endif
		/*
		 * Decrement the Ioctl counting Semaphore before getting an
		 * mfi command
		 */
		sema_wait(&sc->ioctl_count_sema);

		ret = mrsas_passthru(sc, (void *)arg, cmd);

		/* Increment the Ioctl counting semaphore value */
		sema_post(&sc->ioctl_count_sema);

		break;
	case MRSAS_IOC_SCAN_BUS:
		ret = mrsas_bus_scan(sc);
		break;

	case MRSAS_IOC_GET_PCI_INFO:
		pciDrvInfo = (MRSAS_DRV_PCI_INFORMATION *) arg;
		memset(pciDrvInfo, 0, sizeof(MRSAS_DRV_PCI_INFORMATION));
		pciDrvInfo->busNumber = pci_get_bus(sc->mrsas_dev);
		pciDrvInfo->deviceNumber = pci_get_slot(sc->mrsas_dev);
		pciDrvInfo->functionNumber = pci_get_function(sc->mrsas_dev);
		pciDrvInfo->domainID = pci_get_domain(sc->mrsas_dev);
		mrsas_dprint(sc, MRSAS_INFO, "pci bus no: %d,"
		    "pci device no: %d, pci function no: %d,"
		    "pci domain ID: %d\n",
		    pciDrvInfo->busNumber, pciDrvInfo->deviceNumber,
		    pciDrvInfo->functionNumber, pciDrvInfo->domainID);
		ret = 0;
		break;

	default:
		mrsas_dprint(sc, MRSAS_TRACE, "IOCTL command 0x%lx is not handled\n", cmd);
		ret = ENOENT;
	}

	return (ret);
}

/*
 * mrsas_poll:	poll entry point for mrsas driver fd
 *
 * This function is the entry point for poll from the OS.  It waits for some AEN
 * events to be triggered from the controller and notifies back.
 */
static int
mrsas_poll(struct cdev *dev, int poll_events, struct thread *td)
{
	struct mrsas_softc *sc;
	int revents = 0;

	sc = dev->si_drv1;

	if (poll_events & (POLLIN | POLLRDNORM)) {
		if (sc->mrsas_aen_triggered) {
			revents |= poll_events & (POLLIN | POLLRDNORM);
		}
	}
	if (revents == 0) {
		if (poll_events & (POLLIN | POLLRDNORM)) {
			mtx_lock(&sc->aen_lock);
			sc->mrsas_poll_waiting = 1;
			selrecord(td, &sc->mrsas_select);
			mtx_unlock(&sc->aen_lock);
		}
	}
	return revents;
}

/*
 * mrsas_setup_irq:	Set up interrupt
 * input:			Adapter instance soft state
 *
 * This function sets up interrupts as a bus resource, with flags indicating
 * resource permitting contemporaneous sharing and for resource to activate
 * atomically.
 */
static int
mrsas_setup_irq(struct mrsas_softc *sc)
{
	if (sc->msix_enable && (mrsas_setup_msix(sc) == SUCCESS))
		device_printf(sc->mrsas_dev, "MSI-x interrupts setup success\n");

	else {
		device_printf(sc->mrsas_dev, "Fall back to legacy interrupt\n");
		sc->irq_context[0].sc = sc;
		sc->irq_context[0].MSIxIndex = 0;
		sc->irq_id[0] = 0;
		sc->mrsas_irq[0] = bus_alloc_resource_any(sc->mrsas_dev,
		    SYS_RES_IRQ, &sc->irq_id[0], RF_SHAREABLE | RF_ACTIVE);
		if (sc->mrsas_irq[0] == NULL) {
			device_printf(sc->mrsas_dev, "Cannot allocate legcay"
			    "interrupt\n");
			return (FAIL);
		}
		if (bus_setup_intr(sc->mrsas_dev, sc->mrsas_irq[0],
		    INTR_MPSAFE | INTR_TYPE_CAM, NULL, mrsas_isr,
		    &sc->irq_context[0], &sc->intr_handle[0])) {
			device_printf(sc->mrsas_dev, "Cannot set up legacy"
			    "interrupt\n");
			return (FAIL);
		}
	}
	return (0);
}

/*
 * mrsas_isr:	ISR entry point
 * input:		argument pointer
 *
 * This function is the interrupt service routine entry point.  There are two
 * types of interrupts, state change interrupt and response interrupt.  If an
 * interrupt is not ours, we just return.
 */
void
mrsas_isr(void *arg)
{
	struct mrsas_irq_context *irq_context = (struct mrsas_irq_context *)arg;
	struct mrsas_softc *sc = irq_context->sc;
	int status = 0;

	if (sc->mask_interrupts)
		return;

	if (!sc->msix_vectors) {
		status = mrsas_clear_intr(sc);
		if (!status)
			return;
	}
	/* If we are resetting, bail */
	if (mrsas_test_bit(MRSAS_FUSION_IN_RESET, &sc->reset_flags)) {
		printf(" Entered into ISR when OCR is going active. \n");
		mrsas_clear_intr(sc);
		return;
	}
	/* Process for reply request and clear response interrupt */
	if (mrsas_complete_cmd(sc, irq_context->MSIxIndex) != SUCCESS)
		mrsas_clear_intr(sc);

	return;
}

/*
 * mrsas_complete_cmd:	Process reply request
 * input:				Adapter instance soft state
 *
 * This function is called from mrsas_isr() to process reply request and clear
 * response interrupt. Processing of the reply request entails walking
 * through the reply descriptor array for the command request  pended from
 * Firmware.  We look at the Function field to determine the command type and
 * perform the appropriate action.  Before we return, we clear the response
 * interrupt.
 */
int
mrsas_complete_cmd(struct mrsas_softc *sc, u_int32_t MSIxIndex)
{
	Mpi2ReplyDescriptorsUnion_t *desc;
	MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *reply_desc;
	MRSAS_RAID_SCSI_IO_REQUEST *scsi_io_req;
	struct mrsas_mpt_cmd *cmd_mpt, *r1_cmd = NULL;
	struct mrsas_mfi_cmd *cmd_mfi;
	u_int8_t reply_descript_type, *sense;
	u_int16_t smid, num_completed;
	u_int8_t status, extStatus;
	union desc_value desc_val;
	PLD_LOAD_BALANCE_INFO lbinfo;
	u_int32_t device_id, data_length;
	int threshold_reply_count = 0;
#if TM_DEBUG
	MR_TASK_MANAGE_REQUEST *mr_tm_req;
	MPI2_SCSI_TASK_MANAGE_REQUEST *mpi_tm_req;
#endif

	/* If we have a hardware error, not need to continue */
	if (sc->adprecovery == MRSAS_HW_CRITICAL_ERROR)
		return (DONE);

	desc = sc->reply_desc_mem;
	desc += ((MSIxIndex * sc->reply_alloc_sz) / sizeof(MPI2_REPLY_DESCRIPTORS_UNION))
	    + sc->last_reply_idx[MSIxIndex];

	reply_desc = (MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *) desc;

	desc_val.word = desc->Words;
	num_completed = 0;

	reply_descript_type = reply_desc->ReplyFlags & MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;

	/* Find our reply descriptor for the command and process */
	while ((desc_val.u.low != 0xFFFFFFFF) && (desc_val.u.high != 0xFFFFFFFF)) {
		smid = reply_desc->SMID;
		cmd_mpt = sc->mpt_cmd_list[smid - 1];
		scsi_io_req = (MRSAS_RAID_SCSI_IO_REQUEST *) cmd_mpt->io_request;

		status = scsi_io_req->RaidContext.raid_context.status;
		extStatus = scsi_io_req->RaidContext.raid_context.exStatus;
		sense = cmd_mpt->sense;
		data_length = scsi_io_req->DataLength;

		switch (scsi_io_req->Function) {
		case MPI2_FUNCTION_SCSI_TASK_MGMT:
#if TM_DEBUG
			mr_tm_req = (MR_TASK_MANAGE_REQUEST *) cmd_mpt->io_request;
			mpi_tm_req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)
			    &mr_tm_req->TmRequest;
			device_printf(sc->mrsas_dev, "TM completion type 0x%X, "
			    "TaskMID: 0x%X", mpi_tm_req->TaskType, mpi_tm_req->TaskMID);
#endif
            wakeup_one((void *)&sc->ocr_chan);
            break;
		case MPI2_FUNCTION_SCSI_IO_REQUEST:	/* Fast Path IO. */
			device_id = cmd_mpt->ccb_ptr->ccb_h.target_id;
			lbinfo = &sc->load_balance_info[device_id];
			/* R1 load balancing for READ */
			if (cmd_mpt->load_balance == MRSAS_LOAD_BALANCE_FLAG) {
				mrsas_atomic_dec(&lbinfo->scsi_pending_cmds[cmd_mpt->pd_r1_lb]);
				cmd_mpt->load_balance &= ~MRSAS_LOAD_BALANCE_FLAG;
			}
			/* Fall thru and complete IO */
		case MRSAS_MPI2_FUNCTION_LD_IO_REQUEST:
			if (cmd_mpt->r1_alt_dev_handle == MR_DEVHANDLE_INVALID) {
				mrsas_map_mpt_cmd_status(cmd_mpt, cmd_mpt->ccb_ptr, status,
				    extStatus, data_length, sense);
				mrsas_cmd_done(sc, cmd_mpt);
				mrsas_atomic_dec(&sc->fw_outstanding);
			} else {
				/*
				 * If the peer  Raid  1/10 fast path failed,
				 * mark IO as failed to the scsi layer.
				 * Overwrite the current status by the failed status
				 * and make sure that if any command fails,
				 * driver returns fail status to CAM.
				 */
				cmd_mpt->cmd_completed = 1;
				r1_cmd = cmd_mpt->peer_cmd;
				if (r1_cmd->cmd_completed) {
					if (r1_cmd->io_request->RaidContext.raid_context.status != MFI_STAT_OK) {
						status = r1_cmd->io_request->RaidContext.raid_context.status;
						extStatus = r1_cmd->io_request->RaidContext.raid_context.exStatus;
						data_length = r1_cmd->io_request->DataLength;
						sense = r1_cmd->sense;
					}
					r1_cmd->ccb_ptr = NULL;
					if (r1_cmd->callout_owner) {
						callout_stop(&r1_cmd->cm_callout);
						r1_cmd->callout_owner  = false;
					}
					mrsas_release_mpt_cmd(r1_cmd);
					mrsas_atomic_dec(&sc->fw_outstanding);
					mrsas_map_mpt_cmd_status(cmd_mpt, cmd_mpt->ccb_ptr, status,
					    extStatus, data_length, sense);
					mrsas_cmd_done(sc, cmd_mpt);
					mrsas_atomic_dec(&sc->fw_outstanding);
				}
			}
			break;
		case MRSAS_MPI2_FUNCTION_PASSTHRU_IO_REQUEST:	/* MFI command */
			cmd_mfi = sc->mfi_cmd_list[cmd_mpt->sync_cmd_idx];
			/*
			 * Make sure NOT TO release the mfi command from the called
			 * function's context if it is fired with issue_polled call.
			 * And also make sure that the issue_polled call should only be
			 * used if INTERRUPT IS DISABLED.
			 */
			if (cmd_mfi->frame->hdr.flags & MFI_FRAME_DONT_POST_IN_REPLY_QUEUE)
				mrsas_release_mfi_cmd(cmd_mfi);
			else
				mrsas_complete_mptmfi_passthru(sc, cmd_mfi, status);
			break;
		}

		sc->last_reply_idx[MSIxIndex]++;
		if (sc->last_reply_idx[MSIxIndex] >= sc->reply_q_depth)
			sc->last_reply_idx[MSIxIndex] = 0;

		desc->Words = ~((uint64_t)0x00);	/* set it back to all
							 * 0xFFFFFFFFs */
		num_completed++;
		threshold_reply_count++;

		/* Get the next reply descriptor */
		if (!sc->last_reply_idx[MSIxIndex]) {
			desc = sc->reply_desc_mem;
			desc += ((MSIxIndex * sc->reply_alloc_sz) / sizeof(MPI2_REPLY_DESCRIPTORS_UNION));
		} else
			desc++;

		reply_desc = (MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *) desc;
		desc_val.word = desc->Words;

		reply_descript_type = reply_desc->ReplyFlags & MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;

		if (reply_descript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
			break;

		/*
		 * Write to reply post index after completing threshold reply
		 * count and still there are more replies in reply queue
		 * pending to be completed.
		 */
		if (threshold_reply_count >= THRESHOLD_REPLY_COUNT) {
			if (sc->msix_enable) {
				if (sc->msix_combined)
					mrsas_write_reg(sc, sc->msix_reg_offset[MSIxIndex / 8],
					    ((MSIxIndex & 0x7) << 24) |
					    sc->last_reply_idx[MSIxIndex]);
				else
					mrsas_write_reg(sc, sc->msix_reg_offset[0], (MSIxIndex << 24) |
					    sc->last_reply_idx[MSIxIndex]);
			} else
				mrsas_write_reg(sc, offsetof(mrsas_reg_set,
				    reply_post_host_index), sc->last_reply_idx[0]);

			threshold_reply_count = 0;
		}
	}

	/* No match, just return */
	if (num_completed == 0)
		return (DONE);

	/* Clear response interrupt */
	if (sc->msix_enable) {
		if (sc->msix_combined) {
			mrsas_write_reg(sc, sc->msix_reg_offset[MSIxIndex / 8],
			    ((MSIxIndex & 0x7) << 24) |
			    sc->last_reply_idx[MSIxIndex]);
		} else
			mrsas_write_reg(sc, sc->msix_reg_offset[0], (MSIxIndex << 24) |
			    sc->last_reply_idx[MSIxIndex]);
	} else
		mrsas_write_reg(sc, offsetof(mrsas_reg_set,
		    reply_post_host_index), sc->last_reply_idx[0]);

	return (0);
}

/*
 * mrsas_map_mpt_cmd_status:	Allocate DMAable memory.
 * input:						Adapter instance soft state
 *
 * This function is called from mrsas_complete_cmd(), for LD IO and FastPath IO.
 * It checks the command status and maps the appropriate CAM status for the
 * CCB.
 */
void
mrsas_map_mpt_cmd_status(struct mrsas_mpt_cmd *cmd, union ccb *ccb_ptr, u_int8_t status,
    u_int8_t extStatus, u_int32_t data_length, u_int8_t *sense)
{
	struct mrsas_softc *sc = cmd->sc;
	u_int8_t *sense_data;

	switch (status) {
	case MFI_STAT_OK:
		ccb_ptr->ccb_h.status = CAM_REQ_CMP;
		break;
	case MFI_STAT_SCSI_IO_FAILED:
	case MFI_STAT_SCSI_DONE_WITH_ERROR:
		ccb_ptr->ccb_h.status = CAM_SCSI_STATUS_ERROR;
		sense_data = (u_int8_t *)&ccb_ptr->csio.sense_data;
		if (sense_data) {
			/* For now just copy 18 bytes back */
			memcpy(sense_data, sense, 18);
			ccb_ptr->csio.sense_len = 18;
			ccb_ptr->ccb_h.status |= CAM_AUTOSNS_VALID;
		}
		break;
	case MFI_STAT_LD_OFFLINE:
	case MFI_STAT_DEVICE_NOT_FOUND:
		if (ccb_ptr->ccb_h.target_lun)
			ccb_ptr->ccb_h.status |= CAM_LUN_INVALID;
		else
			ccb_ptr->ccb_h.status |= CAM_DEV_NOT_THERE;
		break;
	case MFI_STAT_CONFIG_SEQ_MISMATCH:
		ccb_ptr->ccb_h.status |= CAM_REQUEUE_REQ;
		break;
	default:
		device_printf(sc->mrsas_dev, "FW cmd complete status %x\n", status);
		ccb_ptr->ccb_h.status = CAM_REQ_CMP_ERR;
		ccb_ptr->csio.scsi_status = status;
	}
	return;
}

/*
 * mrsas_alloc_mem:	Allocate DMAable memory
 * input:			Adapter instance soft state
 *
 * This function creates the parent DMA tag and allocates DMAable memory. DMA
 * tag describes constraints of DMA mapping. Memory allocated is mapped into
 * Kernel virtual address. Callback argument is physical memory address.
 */
static int
mrsas_alloc_mem(struct mrsas_softc *sc)
{
	u_int32_t verbuf_size, io_req_size, reply_desc_size, sense_size, chain_frame_size,
		evt_detail_size, count, pd_info_size;

	/*
	 * Allocate parent DMA tag
	 */
	if (bus_dma_tag_create(NULL,	/* parent */
	    1,				/* alignment */
	    0,				/* boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MAXPHYS,			/* maxsize */
	    sc->max_num_sge,		/* nsegments */
	    MAXPHYS,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->mrsas_parent_tag	/* tag */
	    )) {
		device_printf(sc->mrsas_dev, "Cannot allocate parent DMA tag\n");
		return (ENOMEM);
	}
	/*
	 * Allocate for version buffer
	 */
	verbuf_size = MRSAS_MAX_NAME_LENGTH * (sizeof(bus_addr_t));
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    1, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    verbuf_size,
	    1,
	    verbuf_size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &sc->verbuf_tag)) {
		device_printf(sc->mrsas_dev, "Cannot allocate verbuf DMA tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->verbuf_tag, (void **)&sc->verbuf_mem,
	    BUS_DMA_NOWAIT, &sc->verbuf_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot allocate verbuf memory\n");
		return (ENOMEM);
	}
	bzero(sc->verbuf_mem, verbuf_size);
	if (bus_dmamap_load(sc->verbuf_tag, sc->verbuf_dmamap, sc->verbuf_mem,
	    verbuf_size, mrsas_addr_cb, &sc->verbuf_phys_addr,
	    BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load verbuf DMA map\n");
		return (ENOMEM);
	}
	/*
	 * Allocate IO Request Frames
	 */
	io_req_size = sc->io_frames_alloc_sz;
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    16, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    io_req_size,
	    1,
	    io_req_size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &sc->io_request_tag)) {
		device_printf(sc->mrsas_dev, "Cannot create IO request tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->io_request_tag, (void **)&sc->io_request_mem,
	    BUS_DMA_NOWAIT, &sc->io_request_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot alloc IO request memory\n");
		return (ENOMEM);
	}
	bzero(sc->io_request_mem, io_req_size);
	if (bus_dmamap_load(sc->io_request_tag, sc->io_request_dmamap,
	    sc->io_request_mem, io_req_size, mrsas_addr_cb,
	    &sc->io_request_phys_addr, BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load IO request memory\n");
		return (ENOMEM);
	}
	/*
	 * Allocate Chain Frames
	 */
	chain_frame_size = sc->chain_frames_alloc_sz;
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    4, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    chain_frame_size,
	    1,
	    chain_frame_size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &sc->chain_frame_tag)) {
		device_printf(sc->mrsas_dev, "Cannot create chain frame tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->chain_frame_tag, (void **)&sc->chain_frame_mem,
	    BUS_DMA_NOWAIT, &sc->chain_frame_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot alloc chain frame memory\n");
		return (ENOMEM);
	}
	bzero(sc->chain_frame_mem, chain_frame_size);
	if (bus_dmamap_load(sc->chain_frame_tag, sc->chain_frame_dmamap,
	    sc->chain_frame_mem, chain_frame_size, mrsas_addr_cb,
	    &sc->chain_frame_phys_addr, BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load chain frame memory\n");
		return (ENOMEM);
	}
	count = sc->msix_vectors > 0 ? sc->msix_vectors : 1;
	/*
	 * Allocate Reply Descriptor Array
	 */
	reply_desc_size = sc->reply_alloc_sz * count;
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    16, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    reply_desc_size,
	    1,
	    reply_desc_size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &sc->reply_desc_tag)) {
		device_printf(sc->mrsas_dev, "Cannot create reply descriptor tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->reply_desc_tag, (void **)&sc->reply_desc_mem,
	    BUS_DMA_NOWAIT, &sc->reply_desc_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot alloc reply descriptor memory\n");
		return (ENOMEM);
	}
	if (bus_dmamap_load(sc->reply_desc_tag, sc->reply_desc_dmamap,
	    sc->reply_desc_mem, reply_desc_size, mrsas_addr_cb,
	    &sc->reply_desc_phys_addr, BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load reply descriptor memory\n");
		return (ENOMEM);
	}
	/*
	 * Allocate Sense Buffer Array.  Keep in lower 4GB
	 */
	sense_size = sc->max_fw_cmds * MRSAS_SENSE_LEN;
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    64, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    sense_size,
	    1,
	    sense_size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &sc->sense_tag)) {
		device_printf(sc->mrsas_dev, "Cannot allocate sense buf tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->sense_tag, (void **)&sc->sense_mem,
	    BUS_DMA_NOWAIT, &sc->sense_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot allocate sense buf memory\n");
		return (ENOMEM);
	}
	if (bus_dmamap_load(sc->sense_tag, sc->sense_dmamap,
	    sc->sense_mem, sense_size, mrsas_addr_cb, &sc->sense_phys_addr,
	    BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load sense buf memory\n");
		return (ENOMEM);
	}

	/*
	 * Allocate for Event detail structure
	 */
	evt_detail_size = sizeof(struct mrsas_evt_detail);
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    1, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    evt_detail_size,
	    1,
	    evt_detail_size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &sc->evt_detail_tag)) {
		device_printf(sc->mrsas_dev, "Cannot create Event detail tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->evt_detail_tag, (void **)&sc->evt_detail_mem,
	    BUS_DMA_NOWAIT, &sc->evt_detail_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot alloc Event detail buffer memory\n");
		return (ENOMEM);
	}
	bzero(sc->evt_detail_mem, evt_detail_size);
	if (bus_dmamap_load(sc->evt_detail_tag, sc->evt_detail_dmamap,
	    sc->evt_detail_mem, evt_detail_size, mrsas_addr_cb,
	    &sc->evt_detail_phys_addr, BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load Event detail buffer memory\n");
		return (ENOMEM);
	}

	/*
	 * Allocate for PD INFO structure
	 */
	pd_info_size = sizeof(struct mrsas_pd_info);
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    1, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    pd_info_size,
	    1,
	    pd_info_size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &sc->pd_info_tag)) {
		device_printf(sc->mrsas_dev, "Cannot create PD INFO tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->pd_info_tag, (void **)&sc->pd_info_mem,
	    BUS_DMA_NOWAIT, &sc->pd_info_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot alloc PD INFO buffer memory\n");
		return (ENOMEM);
	}
	bzero(sc->pd_info_mem, pd_info_size);
	if (bus_dmamap_load(sc->pd_info_tag, sc->pd_info_dmamap,
	    sc->pd_info_mem, pd_info_size, mrsas_addr_cb,
	    &sc->pd_info_phys_addr, BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load PD INFO buffer memory\n");
		return (ENOMEM);
	}

	/*
	 * Create a dma tag for data buffers; size will be the maximum
	 * possible I/O size (280kB).
	 */
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    1,
	    0,
	    BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    MAXPHYS,
	    sc->max_num_sge,		/* nsegments */
	    MAXPHYS,
	    BUS_DMA_ALLOCNOW,
	    busdma_lock_mutex,
	    &sc->io_lock,
	    &sc->data_tag)) {
		device_printf(sc->mrsas_dev, "Cannot create data dma tag\n");
		return (ENOMEM);
	}
	return (0);
}

/*
 * mrsas_addr_cb:	Callback function of bus_dmamap_load()
 * input:			callback argument, machine dependent type
 * 					that describes DMA segments, number of segments, error code
 *
 * This function is for the driver to receive mapping information resultant of
 * the bus_dmamap_load(). The information is actually not being used, but the
 * address is saved anyway.
 */
void
mrsas_addr_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	bus_addr_t *addr;

	addr = arg;
	*addr = segs[0].ds_addr;
}

/*
 * mrsas_setup_raidmap:	Set up RAID map.
 * input:				Adapter instance soft state
 *
 * Allocate DMA memory for the RAID maps and perform setup.
 */
static int
mrsas_setup_raidmap(struct mrsas_softc *sc)
{
	int i;

	for (i = 0; i < 2; i++) {
		sc->ld_drv_map[i] =
		    (void *)malloc(sc->drv_map_sz, M_MRSAS, M_NOWAIT);
		/* Do Error handling */
		if (!sc->ld_drv_map[i]) {
			device_printf(sc->mrsas_dev, "Could not allocate memory for local map");

			if (i == 1)
				free(sc->ld_drv_map[0], M_MRSAS);
			/* ABORT driver initialization */
			goto ABORT;
		}
	}

	for (int i = 0; i < 2; i++) {
		if (bus_dma_tag_create(sc->mrsas_parent_tag,
		    4, 0,
		    BUS_SPACE_MAXADDR_32BIT,
		    BUS_SPACE_MAXADDR,
		    NULL, NULL,
		    sc->max_map_sz,
		    1,
		    sc->max_map_sz,
		    BUS_DMA_ALLOCNOW,
		    NULL, NULL,
		    &sc->raidmap_tag[i])) {
			device_printf(sc->mrsas_dev,
			    "Cannot allocate raid map tag.\n");
			return (ENOMEM);
		}
		if (bus_dmamem_alloc(sc->raidmap_tag[i],
		    (void **)&sc->raidmap_mem[i],
		    BUS_DMA_NOWAIT, &sc->raidmap_dmamap[i])) {
			device_printf(sc->mrsas_dev,
			    "Cannot allocate raidmap memory.\n");
			return (ENOMEM);
		}
		bzero(sc->raidmap_mem[i], sc->max_map_sz);

		if (bus_dmamap_load(sc->raidmap_tag[i], sc->raidmap_dmamap[i],
		    sc->raidmap_mem[i], sc->max_map_sz,
		    mrsas_addr_cb, &sc->raidmap_phys_addr[i],
		    BUS_DMA_NOWAIT)) {
			device_printf(sc->mrsas_dev, "Cannot load raidmap memory.\n");
			return (ENOMEM);
		}
		if (!sc->raidmap_mem[i]) {
			device_printf(sc->mrsas_dev,
			    "Cannot allocate memory for raid map.\n");
			return (ENOMEM);
		}
	}

	if (!mrsas_get_map_info(sc))
		mrsas_sync_map_info(sc);

	return (0);

ABORT:
	return (1);
}

/**
 * megasas_setup_jbod_map -	setup jbod map for FP seq_number.
 * @sc:				Adapter soft state
 *
 * Return 0 on success.
 */
void
megasas_setup_jbod_map(struct mrsas_softc *sc)
{
	int i;
	uint32_t pd_seq_map_sz;

	pd_seq_map_sz = sizeof(struct MR_PD_CFG_SEQ_NUM_SYNC) +
	    (sizeof(struct MR_PD_CFG_SEQ) * (MAX_PHYSICAL_DEVICES - 1));

	if (!sc->ctrl_info->adapterOperations3.useSeqNumJbodFP) {
		sc->use_seqnum_jbod_fp = 0;
		return;
	}
	if (sc->jbodmap_mem[0])
		goto skip_alloc;

	for (i = 0; i < 2; i++) {
		if (bus_dma_tag_create(sc->mrsas_parent_tag,
		    4, 0,
		    BUS_SPACE_MAXADDR_32BIT,
		    BUS_SPACE_MAXADDR,
		    NULL, NULL,
		    pd_seq_map_sz,
		    1,
		    pd_seq_map_sz,
		    BUS_DMA_ALLOCNOW,
		    NULL, NULL,
		    &sc->jbodmap_tag[i])) {
			device_printf(sc->mrsas_dev,
			    "Cannot allocate jbod map tag.\n");
			return;
		}
		if (bus_dmamem_alloc(sc->jbodmap_tag[i],
		    (void **)&sc->jbodmap_mem[i],
		    BUS_DMA_NOWAIT, &sc->jbodmap_dmamap[i])) {
			device_printf(sc->mrsas_dev,
			    "Cannot allocate jbod map memory.\n");
			return;
		}
		bzero(sc->jbodmap_mem[i], pd_seq_map_sz);

		if (bus_dmamap_load(sc->jbodmap_tag[i], sc->jbodmap_dmamap[i],
		    sc->jbodmap_mem[i], pd_seq_map_sz,
		    mrsas_addr_cb, &sc->jbodmap_phys_addr[i],
		    BUS_DMA_NOWAIT)) {
			device_printf(sc->mrsas_dev, "Cannot load jbod map memory.\n");
			return;
		}
		if (!sc->jbodmap_mem[i]) {
			device_printf(sc->mrsas_dev,
			    "Cannot allocate memory for jbod map.\n");
			sc->use_seqnum_jbod_fp = 0;
			return;
		}
	}

skip_alloc:
	if (!megasas_sync_pd_seq_num(sc, false) &&
	    !megasas_sync_pd_seq_num(sc, true))
		sc->use_seqnum_jbod_fp = 1;
	else
		sc->use_seqnum_jbod_fp = 0;

	device_printf(sc->mrsas_dev, "Jbod map is supported\n");
}

/*
 * mrsas_init_fw:	Initialize Firmware
 * input:			Adapter soft state
 *
 * Calls transition_to_ready() to make sure Firmware is in operational state and
 * calls mrsas_init_adapter() to send IOC_INIT command to Firmware.  It
 * issues internal commands to get the controller info after the IOC_INIT
 * command response is received by Firmware.  Note:  code relating to
 * get_pdlist, get_ld_list and max_sectors are currently not being used, it
 * is left here as placeholder.
 */
static int
mrsas_init_fw(struct mrsas_softc *sc)
{

	int ret, loop, ocr = 0;
	u_int32_t max_sectors_1;
	u_int32_t max_sectors_2;
	u_int32_t tmp_sectors;
	u_int32_t scratch_pad_2, scratch_pad_3, scratch_pad_4;
	int msix_enable = 0;
	int fw_msix_count = 0;
	int i, j;

	/* Make sure Firmware is ready */
	ret = mrsas_transition_to_ready(sc, ocr);
	if (ret != SUCCESS) {
		return (ret);
	}
	if (sc->is_ventura || sc->is_aero) {
		scratch_pad_3 = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set, outbound_scratch_pad_3));
#if VD_EXT_DEBUG
		device_printf(sc->mrsas_dev, "scratch_pad_3 0x%x\n", scratch_pad_3);
#endif
		sc->maxRaidMapSize = ((scratch_pad_3 >>
		    MR_MAX_RAID_MAP_SIZE_OFFSET_SHIFT) &
		    MR_MAX_RAID_MAP_SIZE_MASK);
	}
	/* MSI-x index 0- reply post host index register */
	sc->msix_reg_offset[0] = MPI2_REPLY_POST_HOST_INDEX_OFFSET;
	/* Check if MSI-X is supported while in ready state */
	msix_enable = (mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set, outbound_scratch_pad)) & 0x4000000) >> 0x1a;

	if (msix_enable) {
		scratch_pad_2 = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
		    outbound_scratch_pad_2));

		/* Check max MSI-X vectors */
		if (sc->device_id == MRSAS_TBOLT) {
			sc->msix_vectors = (scratch_pad_2
			    & MR_MAX_REPLY_QUEUES_OFFSET) + 1;
			fw_msix_count = sc->msix_vectors;
		} else {
			/* Invader/Fury supports 96 MSI-X vectors */
			sc->msix_vectors = ((scratch_pad_2
			    & MR_MAX_REPLY_QUEUES_EXT_OFFSET)
			    >> MR_MAX_REPLY_QUEUES_EXT_OFFSET_SHIFT) + 1;
			fw_msix_count = sc->msix_vectors;

			if ((sc->mrsas_gen3_ctrl && (sc->msix_vectors > 8)) ||
				((sc->is_ventura || sc->is_aero) && (sc->msix_vectors > 16)))
				sc->msix_combined = true;
			/*
			 * Save 1-15 reply post index
			 * address to local memory Index 0
			 * is already saved from reg offset
			 * MPI2_REPLY_POST_HOST_INDEX_OFFSET
			 */
			for (loop = 1; loop < MR_MAX_MSIX_REG_ARRAY;
			    loop++) {
				sc->msix_reg_offset[loop] =
				    MPI2_SUP_REPLY_POST_HOST_INDEX_OFFSET +
				    (loop * 0x10);
			}
		}

		/* Don't bother allocating more MSI-X vectors than cpus */
		sc->msix_vectors = min(sc->msix_vectors,
		    mp_ncpus);

		/* Allocate MSI-x vectors */
		if (mrsas_allocate_msix(sc) == SUCCESS)
			sc->msix_enable = 1;
		else
			sc->msix_enable = 0;

		device_printf(sc->mrsas_dev, "FW supports <%d> MSIX vector,"
		    "Online CPU %d Current MSIX <%d>\n",
		    fw_msix_count, mp_ncpus, sc->msix_vectors);
	}
	/*
     * MSI-X host index 0 is common for all adapter.
     * It is used for all MPT based Adapters.
	 */
	if (sc->msix_combined) {
		sc->msix_reg_offset[0] =
		    MPI2_SUP_REPLY_POST_HOST_INDEX_OFFSET;
	}
	if (mrsas_init_adapter(sc) != SUCCESS) {
		device_printf(sc->mrsas_dev, "Adapter initialize Fail.\n");
		return (1);
	}

	if (sc->is_ventura || sc->is_aero) {
		scratch_pad_4 = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
		    outbound_scratch_pad_4));
		if ((scratch_pad_4 & MR_NVME_PAGE_SIZE_MASK) >= MR_DEFAULT_NVME_PAGE_SHIFT)
			sc->nvme_page_size = 1 << (scratch_pad_4 & MR_NVME_PAGE_SIZE_MASK);

		device_printf(sc->mrsas_dev, "NVME page size\t: (%d)\n", sc->nvme_page_size);
	}

	/* Allocate internal commands for pass-thru */
	if (mrsas_alloc_mfi_cmds(sc) != SUCCESS) {
		device_printf(sc->mrsas_dev, "Allocate MFI cmd failed.\n");
		return (1);
	}
	sc->ctrl_info = malloc(sizeof(struct mrsas_ctrl_info), M_MRSAS, M_NOWAIT);
	if (!sc->ctrl_info) {
		device_printf(sc->mrsas_dev, "Malloc for ctrl_info failed.\n");
		return (1);
	}
	/*
	 * Get the controller info from FW, so that the MAX VD support
	 * availability can be decided.
	 */
	if (mrsas_get_ctrl_info(sc)) {
		device_printf(sc->mrsas_dev, "Unable to get FW ctrl_info.\n");
		return (1);
	}
	sc->secure_jbod_support =
	    (u_int8_t)sc->ctrl_info->adapterOperations3.supportSecurityonJBOD;

	if (sc->secure_jbod_support)
		device_printf(sc->mrsas_dev, "FW supports SED \n");

	if (sc->use_seqnum_jbod_fp)
		device_printf(sc->mrsas_dev, "FW supports JBOD Map \n");

	if (sc->support_morethan256jbod)
		device_printf(sc->mrsas_dev, "FW supports JBOD Map Ext \n");

	if (mrsas_setup_raidmap(sc) != SUCCESS) {
		device_printf(sc->mrsas_dev, "Error: RAID map setup FAILED !!! "
		    "There seems to be some problem in the controller\n"
		    "Please contact to the SUPPORT TEAM if the problem persists\n");
	}
	megasas_setup_jbod_map(sc);


	memset(sc->target_list, 0,
		MRSAS_MAX_TM_TARGETS * sizeof(struct mrsas_target));
	for (i = 0; i < MRSAS_MAX_TM_TARGETS; i++)
		sc->target_list[i].target_id = 0xffff;

	/* For pass-thru, get PD/LD list and controller info */
	memset(sc->pd_list, 0,
	    MRSAS_MAX_PD * sizeof(struct mrsas_pd_list));
	if (mrsas_get_pd_list(sc) != SUCCESS) {
		device_printf(sc->mrsas_dev, "Get PD list failed.\n");
		return (1);
	}
	memset(sc->ld_ids, 0xff, MRSAS_MAX_LD_IDS);
	if (mrsas_get_ld_list(sc) != SUCCESS) {
		device_printf(sc->mrsas_dev, "Get LD lsit failed.\n");
		return (1);
	}

	if ((sc->is_ventura || sc->is_aero) && sc->drv_stream_detection) {
		sc->streamDetectByLD = malloc(sizeof(PTR_LD_STREAM_DETECT) *
						MAX_LOGICAL_DRIVES_EXT, M_MRSAS, M_NOWAIT);
		if (!sc->streamDetectByLD) {
			device_printf(sc->mrsas_dev,
				"unable to allocate stream detection for pool of LDs\n");
			return (1);
		}
		for (i = 0; i < MAX_LOGICAL_DRIVES_EXT; ++i) {
			sc->streamDetectByLD[i] = malloc(sizeof(LD_STREAM_DETECT), M_MRSAS, M_NOWAIT);
			if (!sc->streamDetectByLD[i]) {
				device_printf(sc->mrsas_dev, "unable to allocate stream detect by LD\n");
				for (j = 0; j < i; ++j)
					free(sc->streamDetectByLD[j], M_MRSAS);
				free(sc->streamDetectByLD, M_MRSAS);
				sc->streamDetectByLD = NULL;
				return (1);
			}
			memset(sc->streamDetectByLD[i], 0, sizeof(LD_STREAM_DETECT));
			sc->streamDetectByLD[i]->mruBitMap = MR_STREAM_BITMAP;
		}
	}

	/*
	 * Compute the max allowed sectors per IO: The controller info has
	 * two limits on max sectors. Driver should use the minimum of these
	 * two.
	 *
	 * 1 << stripe_sz_ops.min = max sectors per strip
	 *
	 * Note that older firmwares ( < FW ver 30) didn't report information to
	 * calculate max_sectors_1. So the number ended up as zero always.
	 */
	tmp_sectors = 0;
	max_sectors_1 = (1 << sc->ctrl_info->stripe_sz_ops.min) *
	    sc->ctrl_info->max_strips_per_io;
	max_sectors_2 = sc->ctrl_info->max_request_size;
	tmp_sectors = min(max_sectors_1, max_sectors_2);
	sc->max_sectors_per_req = sc->max_num_sge * MRSAS_PAGE_SIZE / 512;

	if (tmp_sectors && (sc->max_sectors_per_req > tmp_sectors))
		sc->max_sectors_per_req = tmp_sectors;

	sc->disableOnlineCtrlReset =
	    sc->ctrl_info->properties.OnOffProperties.disableOnlineCtrlReset;
	sc->UnevenSpanSupport =
	    sc->ctrl_info->adapterOperations2.supportUnevenSpans;
	if (sc->UnevenSpanSupport) {
		device_printf(sc->mrsas_dev, "FW supports: UnevenSpanSupport=%x\n\n",
		    sc->UnevenSpanSupport);

		if (MR_ValidateMapInfo(sc))
			sc->fast_path_io = 1;
		else
			sc->fast_path_io = 0;
	}
		
	device_printf(sc->mrsas_dev, "max_fw_cmds: %u  max_scsi_cmds: %u\n",
		sc->max_fw_cmds, sc->max_scsi_cmds);
	return (0);
}

/*
 * mrsas_init_adapter:	Initializes the adapter/controller
 * input:				Adapter soft state
 *
 * Prepares for the issuing of the IOC Init cmd to FW for initializing the
 * ROC/controller.  The FW register is read to determined the number of
 * commands that is supported.  All memory allocations for IO is based on
 * max_cmd.  Appropriate calculations are performed in this function.
 */
int
mrsas_init_adapter(struct mrsas_softc *sc)
{
	uint32_t status;
	u_int32_t scratch_pad_2;
	int ret;
	int i = 0;

	/* Read FW status register */
	status = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set, outbound_scratch_pad));

	sc->max_fw_cmds = status & MRSAS_FWSTATE_MAXCMD_MASK;

	/* Decrement the max supported by 1, to correlate with FW */
	sc->max_fw_cmds = sc->max_fw_cmds - 1;
	sc->max_scsi_cmds = sc->max_fw_cmds - MRSAS_MAX_MFI_CMDS;

	/* Determine allocation size of command frames */
	sc->reply_q_depth = ((sc->max_fw_cmds + 1 + 15) / 16 * 16) * 2;
	sc->request_alloc_sz = sizeof(MRSAS_REQUEST_DESCRIPTOR_UNION) * sc->max_fw_cmds;
	sc->reply_alloc_sz = sizeof(MPI2_REPLY_DESCRIPTORS_UNION) * (sc->reply_q_depth);
	sc->io_frames_alloc_sz = MRSAS_MPI2_RAID_DEFAULT_IO_FRAME_SIZE +
	    (MRSAS_MPI2_RAID_DEFAULT_IO_FRAME_SIZE * (sc->max_fw_cmds + 1));
	scratch_pad_2 = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
	    outbound_scratch_pad_2));
	/*
	 * If scratch_pad_2 & MEGASAS_MAX_CHAIN_SIZE_UNITS_MASK is set,
	 * Firmware support extended IO chain frame which is 4 time more
	 * than legacy Firmware. Legacy Firmware - Frame size is (8 * 128) =
	 * 1K 1M IO Firmware  - Frame size is (8 * 128 * 4)  = 4K
	 */
	if (scratch_pad_2 & MEGASAS_MAX_CHAIN_SIZE_UNITS_MASK)
		sc->max_chain_frame_sz =
		    ((scratch_pad_2 & MEGASAS_MAX_CHAIN_SIZE_MASK) >> 5)
		    * MEGASAS_1MB_IO;
	else
		sc->max_chain_frame_sz =
		    ((scratch_pad_2 & MEGASAS_MAX_CHAIN_SIZE_MASK) >> 5)
		    * MEGASAS_256K_IO;

	sc->chain_frames_alloc_sz = sc->max_chain_frame_sz * sc->max_fw_cmds;
	sc->max_sge_in_main_msg = (MRSAS_MPI2_RAID_DEFAULT_IO_FRAME_SIZE -
	    offsetof(MRSAS_RAID_SCSI_IO_REQUEST, SGL)) / 16;

	sc->max_sge_in_chain = sc->max_chain_frame_sz / sizeof(MPI2_SGE_IO_UNION);
	sc->max_num_sge = sc->max_sge_in_main_msg + sc->max_sge_in_chain - 2;

	mrsas_dprint(sc, MRSAS_INFO,
	    "max sge: 0x%x, max chain frame size: 0x%x, "
	    "max fw cmd: 0x%x\n", sc->max_num_sge,
	    sc->max_chain_frame_sz, sc->max_fw_cmds);

	/* Used for pass thru MFI frame (DCMD) */
	sc->chain_offset_mfi_pthru = offsetof(MRSAS_RAID_SCSI_IO_REQUEST, SGL) / 16;

	sc->chain_offset_io_request = (MRSAS_MPI2_RAID_DEFAULT_IO_FRAME_SIZE -
	    sizeof(MPI2_SGE_IO_UNION)) / 16;

	int count = sc->msix_vectors > 0 ? sc->msix_vectors : 1;

	for (i = 0; i < count; i++)
		sc->last_reply_idx[i] = 0;

	ret = mrsas_alloc_mem(sc);
	if (ret != SUCCESS)
		return (ret);

	ret = mrsas_alloc_mpt_cmds(sc);
	if (ret != SUCCESS)
		return (ret);

	ret = mrsas_ioc_init(sc);
	if (ret != SUCCESS)
		return (ret);

	return (0);
}

/*
 * mrsas_alloc_ioc_cmd:	Allocates memory for IOC Init command
 * input:				Adapter soft state
 *
 * Allocates for the IOC Init cmd to FW to initialize the ROC/controller.
 */
int
mrsas_alloc_ioc_cmd(struct mrsas_softc *sc)
{
	int ioc_init_size;

	/* Allocate IOC INIT command */
	ioc_init_size = 1024 + sizeof(MPI2_IOC_INIT_REQUEST);
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    1, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    ioc_init_size,
	    1,
	    ioc_init_size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &sc->ioc_init_tag)) {
		device_printf(sc->mrsas_dev, "Cannot allocate ioc init tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->ioc_init_tag, (void **)&sc->ioc_init_mem,
	    BUS_DMA_NOWAIT, &sc->ioc_init_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot allocate ioc init cmd mem\n");
		return (ENOMEM);
	}
	bzero(sc->ioc_init_mem, ioc_init_size);
	if (bus_dmamap_load(sc->ioc_init_tag, sc->ioc_init_dmamap,
	    sc->ioc_init_mem, ioc_init_size, mrsas_addr_cb,
	    &sc->ioc_init_phys_mem, BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load ioc init cmd mem\n");
		return (ENOMEM);
	}
	return (0);
}

/*
 * mrsas_free_ioc_cmd:	Allocates memory for IOC Init command
 * input:				Adapter soft state
 *
 * Deallocates memory of the IOC Init cmd.
 */
void
mrsas_free_ioc_cmd(struct mrsas_softc *sc)
{
	if (sc->ioc_init_phys_mem)
		bus_dmamap_unload(sc->ioc_init_tag, sc->ioc_init_dmamap);
	if (sc->ioc_init_mem != NULL)
		bus_dmamem_free(sc->ioc_init_tag, sc->ioc_init_mem, sc->ioc_init_dmamap);
	if (sc->ioc_init_tag != NULL)
		bus_dma_tag_destroy(sc->ioc_init_tag);
}

/*
 * mrsas_ioc_init:	Sends IOC Init command to FW
 * input:			Adapter soft state
 *
 * Issues the IOC Init cmd to FW to initialize the ROC/controller.
 */
int
mrsas_ioc_init(struct mrsas_softc *sc)
{
	struct mrsas_init_frame *init_frame;
	pMpi2IOCInitRequest_t IOCInitMsg;
	MRSAS_REQUEST_DESCRIPTOR_UNION req_desc;
	u_int8_t max_wait = MRSAS_INTERNAL_CMD_WAIT_TIME;
	bus_addr_t phys_addr;
	int i, retcode = 0;
	u_int32_t scratch_pad_2;

	/* Allocate memory for the IOC INIT command */
	if (mrsas_alloc_ioc_cmd(sc)) {
		device_printf(sc->mrsas_dev, "Cannot allocate IOC command.\n");
		return (1);
	}

	if (!sc->block_sync_cache) {
		scratch_pad_2 = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
		    outbound_scratch_pad_2));
		sc->fw_sync_cache_support = (scratch_pad_2 &
		    MR_CAN_HANDLE_SYNC_CACHE_OFFSET) ? 1 : 0;
	}

	IOCInitMsg = (pMpi2IOCInitRequest_t)(((char *)sc->ioc_init_mem) + 1024);
	IOCInitMsg->Function = MPI2_FUNCTION_IOC_INIT;
	IOCInitMsg->WhoInit = MPI2_WHOINIT_HOST_DRIVER;
	IOCInitMsg->MsgVersion = MPI2_VERSION;
	IOCInitMsg->HeaderVersion = MPI2_HEADER_VERSION;
	IOCInitMsg->SystemRequestFrameSize = MRSAS_MPI2_RAID_DEFAULT_IO_FRAME_SIZE / 4;
	IOCInitMsg->ReplyDescriptorPostQueueDepth = sc->reply_q_depth;
	IOCInitMsg->ReplyDescriptorPostQueueAddress = sc->reply_desc_phys_addr;
	IOCInitMsg->SystemRequestFrameBaseAddress = sc->io_request_phys_addr;
	IOCInitMsg->HostMSIxVectors = (sc->msix_vectors > 0 ? sc->msix_vectors : 0);
	IOCInitMsg->HostPageSize = MR_DEFAULT_NVME_PAGE_SHIFT;

	init_frame = (struct mrsas_init_frame *)sc->ioc_init_mem;
	init_frame->cmd = MFI_CMD_INIT;
	init_frame->cmd_status = 0xFF;
	init_frame->flags |= MFI_FRAME_DONT_POST_IN_REPLY_QUEUE;

	/* driver support Extended MSIX */
	if (sc->mrsas_gen3_ctrl || sc->is_ventura || sc->is_aero) {
		init_frame->driver_operations.
		    mfi_capabilities.support_additional_msix = 1;
	}
	if (sc->verbuf_mem) {
		snprintf((char *)sc->verbuf_mem, strlen(MRSAS_VERSION) + 2, "%s\n",
		    MRSAS_VERSION);
		init_frame->driver_ver_lo = (bus_addr_t)sc->verbuf_phys_addr;
		init_frame->driver_ver_hi = 0;
	}
	init_frame->driver_operations.mfi_capabilities.support_ndrive_r1_lb = 1;
	init_frame->driver_operations.mfi_capabilities.support_max_255lds = 1;
	init_frame->driver_operations.mfi_capabilities.security_protocol_cmds_fw = 1;
	if (sc->max_chain_frame_sz > MEGASAS_CHAIN_FRAME_SZ_MIN)
		init_frame->driver_operations.mfi_capabilities.support_ext_io_size = 1;
	phys_addr = (bus_addr_t)sc->ioc_init_phys_mem + 1024;
	init_frame->queue_info_new_phys_addr_lo = phys_addr;
	init_frame->data_xfer_len = sizeof(Mpi2IOCInitRequest_t);

	req_desc.addr.Words = (bus_addr_t)sc->ioc_init_phys_mem;
	req_desc.MFAIo.RequestFlags =
	    (MRSAS_REQ_DESCRIPT_FLAGS_MFA << MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

	mrsas_disable_intr(sc);
	mrsas_dprint(sc, MRSAS_OCR, "Issuing IOC INIT command to FW.\n");
	mrsas_write_64bit_req_desc(sc, req_desc.addr.u.low, req_desc.addr.u.high);

	/*
	 * Poll response timer to wait for Firmware response.  While this
	 * timer with the DELAY call could block CPU, the time interval for
	 * this is only 1 millisecond.
	 */
	if (init_frame->cmd_status == 0xFF) {
		for (i = 0; i < (max_wait * 1000); i++) {
			if (init_frame->cmd_status == 0xFF)
				DELAY(1000);
			else
				break;
		}
	}
	if (init_frame->cmd_status == 0)
		mrsas_dprint(sc, MRSAS_OCR,
		    "IOC INIT response received from FW.\n");
	else {
		if (init_frame->cmd_status == 0xFF)
			device_printf(sc->mrsas_dev, "IOC Init timed out after %d seconds.\n", max_wait);
		else
			device_printf(sc->mrsas_dev, "IOC Init failed, status = 0x%x\n", init_frame->cmd_status);
		retcode = 1;
	}

	if (sc->is_aero) {
		scratch_pad_2 = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
		    outbound_scratch_pad_2));
		sc->atomic_desc_support = (scratch_pad_2 &
			MR_ATOMIC_DESCRIPTOR_SUPPORT_OFFSET) ? 1 : 0;
		device_printf(sc->mrsas_dev, "FW supports atomic descriptor: %s\n",
			sc->atomic_desc_support ? "Yes" : "No");
	}

	mrsas_free_ioc_cmd(sc);
	return (retcode);
}

/*
 * mrsas_alloc_mpt_cmds:	Allocates the command packets
 * input:					Adapter instance soft state
 *
 * This function allocates the internal commands for IOs. Each command that is
 * issued to FW is wrapped in a local data structure called mrsas_mpt_cmd. An
 * array is allocated with mrsas_mpt_cmd context.  The free commands are
 * maintained in a linked list (cmd pool). SMID value range is from 1 to
 * max_fw_cmds.
 */
int
mrsas_alloc_mpt_cmds(struct mrsas_softc *sc)
{
	int i, j;
	u_int32_t max_fw_cmds, count;
	struct mrsas_mpt_cmd *cmd;
	pMpi2ReplyDescriptorsUnion_t reply_desc;
	u_int32_t offset, chain_offset, sense_offset;
	bus_addr_t io_req_base_phys, chain_frame_base_phys, sense_base_phys;
	u_int8_t *io_req_base, *chain_frame_base, *sense_base;

	max_fw_cmds = sc->max_fw_cmds;

	sc->req_desc = malloc(sc->request_alloc_sz, M_MRSAS, M_NOWAIT);
	if (!sc->req_desc) {
		device_printf(sc->mrsas_dev, "Out of memory, cannot alloc req desc\n");
		return (ENOMEM);
	}
	memset(sc->req_desc, 0, sc->request_alloc_sz);

	/*
	 * sc->mpt_cmd_list is an array of struct mrsas_mpt_cmd pointers.
	 * Allocate the dynamic array first and then allocate individual
	 * commands.
	 */
	sc->mpt_cmd_list = malloc(sizeof(struct mrsas_mpt_cmd *) * max_fw_cmds,
	    M_MRSAS, M_NOWAIT);
	if (!sc->mpt_cmd_list) {
		device_printf(sc->mrsas_dev, "Cannot alloc memory for mpt_cmd_list.\n");
		return (ENOMEM);
	}
	memset(sc->mpt_cmd_list, 0, sizeof(struct mrsas_mpt_cmd *) * max_fw_cmds);
	for (i = 0; i < max_fw_cmds; i++) {
		sc->mpt_cmd_list[i] = malloc(sizeof(struct mrsas_mpt_cmd),
		    M_MRSAS, M_NOWAIT);
		if (!sc->mpt_cmd_list[i]) {
			for (j = 0; j < i; j++)
				free(sc->mpt_cmd_list[j], M_MRSAS);
			free(sc->mpt_cmd_list, M_MRSAS);
			sc->mpt_cmd_list = NULL;
			return (ENOMEM);
		}
	}

	io_req_base = (u_int8_t *)sc->io_request_mem + MRSAS_MPI2_RAID_DEFAULT_IO_FRAME_SIZE;
	io_req_base_phys = (bus_addr_t)sc->io_request_phys_addr + MRSAS_MPI2_RAID_DEFAULT_IO_FRAME_SIZE;
	chain_frame_base = (u_int8_t *)sc->chain_frame_mem;
	chain_frame_base_phys = (bus_addr_t)sc->chain_frame_phys_addr;
	sense_base = (u_int8_t *)sc->sense_mem;
	sense_base_phys = (bus_addr_t)sc->sense_phys_addr;
	for (i = 0; i < max_fw_cmds; i++) {
		cmd = sc->mpt_cmd_list[i];
		offset = MRSAS_MPI2_RAID_DEFAULT_IO_FRAME_SIZE * i;
		chain_offset = sc->max_chain_frame_sz * i;
		sense_offset = MRSAS_SENSE_LEN * i;
		memset(cmd, 0, sizeof(struct mrsas_mpt_cmd));
		cmd->index = i + 1;
		cmd->ccb_ptr = NULL;
		cmd->r1_alt_dev_handle = MR_DEVHANDLE_INVALID;
		callout_init_mtx(&cmd->cm_callout, &sc->sim_lock, 0);
		cmd->sync_cmd_idx = (u_int32_t)MRSAS_ULONG_MAX;
		cmd->sc = sc;
		cmd->io_request = (MRSAS_RAID_SCSI_IO_REQUEST *) (io_req_base + offset);
		memset(cmd->io_request, 0, sizeof(MRSAS_RAID_SCSI_IO_REQUEST));
		cmd->io_request_phys_addr = io_req_base_phys + offset;
		cmd->chain_frame = (MPI2_SGE_IO_UNION *) (chain_frame_base + chain_offset);
		cmd->chain_frame_phys_addr = chain_frame_base_phys + chain_offset;
		cmd->sense = sense_base + sense_offset;
		cmd->sense_phys_addr = sense_base_phys + sense_offset;
		if (bus_dmamap_create(sc->data_tag, 0, &cmd->data_dmamap)) {
			return (FAIL);
		}
		TAILQ_INSERT_TAIL(&(sc->mrsas_mpt_cmd_list_head), cmd, next);
	}

	/* Initialize reply descriptor array to 0xFFFFFFFF */
	reply_desc = sc->reply_desc_mem;
	count = sc->msix_vectors > 0 ? sc->msix_vectors : 1;
	for (i = 0; i < sc->reply_q_depth * count; i++, reply_desc++) {
		reply_desc->Words = MRSAS_ULONG_MAX;
	}
	return (0);
}

/*
 * mrsas_write_64bit_req_dsc:	Writes 64 bit request descriptor to FW
 * input:			Adapter softstate
 * 				request descriptor address low
 * 				request descriptor address high
 */
void
mrsas_write_64bit_req_desc(struct mrsas_softc *sc, u_int32_t req_desc_lo,
    u_int32_t req_desc_hi)
{
	mtx_lock(&sc->pci_lock);
	mrsas_write_reg(sc, offsetof(mrsas_reg_set, inbound_low_queue_port),
	    req_desc_lo);
	mrsas_write_reg(sc, offsetof(mrsas_reg_set, inbound_high_queue_port),
	    req_desc_hi);
	mtx_unlock(&sc->pci_lock);
}

/*
 * mrsas_fire_cmd:	Sends command to FW
 * input:		Adapter softstate
 * 			request descriptor address low
 * 			request descriptor address high
 *
 * This functions fires the command to Firmware by writing to the
 * inbound_low_queue_port and inbound_high_queue_port.
 */
void
mrsas_fire_cmd(struct mrsas_softc *sc, u_int32_t req_desc_lo,
    u_int32_t req_desc_hi)
{
	if (sc->atomic_desc_support)
		mrsas_write_reg(sc, offsetof(mrsas_reg_set, inbound_single_queue_port),
		    req_desc_lo);
	else
		mrsas_write_64bit_req_desc(sc, req_desc_lo, req_desc_hi);
}

/*
 * mrsas_transition_to_ready:  Move FW to Ready state input:
 * Adapter instance soft state
 *
 * During the initialization, FW passes can potentially be in any one of several
 * possible states. If the FW in operational, waiting-for-handshake states,
 * driver must take steps to bring it to ready state. Otherwise, it has to
 * wait for the ready state.
 */
int
mrsas_transition_to_ready(struct mrsas_softc *sc, int ocr)
{
	int i;
	u_int8_t max_wait;
	u_int32_t val, fw_state;
	u_int32_t cur_state;
	u_int32_t abs_state, curr_abs_state;

	val = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set, outbound_scratch_pad));
	fw_state = val & MFI_STATE_MASK;
	max_wait = MRSAS_RESET_WAIT_TIME;

	if (fw_state != MFI_STATE_READY)
		device_printf(sc->mrsas_dev, "Waiting for FW to come to ready state\n");

	while (fw_state != MFI_STATE_READY) {
		abs_state = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set, outbound_scratch_pad));
		switch (fw_state) {
		case MFI_STATE_FAULT:
			device_printf(sc->mrsas_dev, "FW is in FAULT state!!\n");
			if (ocr) {
				cur_state = MFI_STATE_FAULT;
				break;
			} else
				return -ENODEV;
		case MFI_STATE_WAIT_HANDSHAKE:
			/* Set the CLR bit in inbound doorbell */
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, doorbell),
			    MFI_INIT_CLEAR_HANDSHAKE | MFI_INIT_HOTPLUG);
			cur_state = MFI_STATE_WAIT_HANDSHAKE;
			break;
		case MFI_STATE_BOOT_MESSAGE_PENDING:
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, doorbell),
			    MFI_INIT_HOTPLUG);
			cur_state = MFI_STATE_BOOT_MESSAGE_PENDING;
			break;
		case MFI_STATE_OPERATIONAL:
			/*
			 * Bring it to READY state; assuming max wait 10
			 * secs
			 */
			mrsas_disable_intr(sc);
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, doorbell), MFI_RESET_FLAGS);
			for (i = 0; i < max_wait * 1000; i++) {
				if (mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set, doorbell)) & 1)
					DELAY(1000);
				else
					break;
			}
			cur_state = MFI_STATE_OPERATIONAL;
			break;
		case MFI_STATE_UNDEFINED:
			/*
			 * This state should not last for more than 2
			 * seconds
			 */
			cur_state = MFI_STATE_UNDEFINED;
			break;
		case MFI_STATE_BB_INIT:
			cur_state = MFI_STATE_BB_INIT;
			break;
		case MFI_STATE_FW_INIT:
			cur_state = MFI_STATE_FW_INIT;
			break;
		case MFI_STATE_FW_INIT_2:
			cur_state = MFI_STATE_FW_INIT_2;
			break;
		case MFI_STATE_DEVICE_SCAN:
			cur_state = MFI_STATE_DEVICE_SCAN;
			break;
		case MFI_STATE_FLUSH_CACHE:
			cur_state = MFI_STATE_FLUSH_CACHE;
			break;
		default:
			device_printf(sc->mrsas_dev, "Unknown state 0x%x\n", fw_state);
			return -ENODEV;
		}

		/*
		 * The cur_state should not last for more than max_wait secs
		 */
		for (i = 0; i < (max_wait * 1000); i++) {
			fw_state = (mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
			    outbound_scratch_pad)) & MFI_STATE_MASK);
			curr_abs_state = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
			    outbound_scratch_pad));
			if (abs_state == curr_abs_state)
				DELAY(1000);
			else
				break;
		}

		/*
		 * Return error if fw_state hasn't changed after max_wait
		 */
		if (curr_abs_state == abs_state) {
			device_printf(sc->mrsas_dev, "FW state [%d] hasn't changed "
			    "in %d secs\n", fw_state, max_wait);
			return -ENODEV;
		}
	}
	mrsas_dprint(sc, MRSAS_OCR, "FW now in Ready state\n");
	return 0;
}

/*
 * mrsas_get_mfi_cmd:	Get a cmd from free command pool
 * input:				Adapter soft state
 *
 * This function removes an MFI command from the command list.
 */
struct mrsas_mfi_cmd *
mrsas_get_mfi_cmd(struct mrsas_softc *sc)
{
	struct mrsas_mfi_cmd *cmd = NULL;

	mtx_lock(&sc->mfi_cmd_pool_lock);
	if (!TAILQ_EMPTY(&sc->mrsas_mfi_cmd_list_head)) {
		cmd = TAILQ_FIRST(&sc->mrsas_mfi_cmd_list_head);
		TAILQ_REMOVE(&sc->mrsas_mfi_cmd_list_head, cmd, next);
	}
	mtx_unlock(&sc->mfi_cmd_pool_lock);

	return cmd;
}

/*
 * mrsas_ocr_thread:	Thread to handle OCR/Kill Adapter.
 * input:				Adapter Context.
 *
 * This function will check FW status register and flag do_timeout_reset flag.
 * It will do OCR/Kill adapter if FW is in fault state or IO timed out has
 * trigger reset.
 */
static void
mrsas_ocr_thread(void *arg)
{
	struct mrsas_softc *sc;
	u_int32_t fw_status, fw_state;
	u_int8_t tm_target_reset_failed = 0;

	sc = (struct mrsas_softc *)arg;

	mrsas_dprint(sc, MRSAS_TRACE, "%s\n", __func__);

	sc->ocr_thread_active = 1;
	mtx_lock(&sc->sim_lock);
	for (;;) {
		/* Sleep for 1 second and check the queue status */
		msleep(&sc->ocr_chan, &sc->sim_lock, PRIBIO,
		    "mrsas_ocr", sc->mrsas_fw_fault_check_delay * hz);
		if (sc->remove_in_progress ||
		    sc->adprecovery == MRSAS_HW_CRITICAL_ERROR) {
			mrsas_dprint(sc, MRSAS_OCR,
			    "Exit due to %s from %s\n",
			    sc->remove_in_progress ? "Shutdown" :
			    "Hardware critical error", __func__);
			break;
		}
		fw_status = mrsas_read_reg_with_retries(sc,
		    offsetof(mrsas_reg_set, outbound_scratch_pad));
		fw_state = fw_status & MFI_STATE_MASK;
		if (fw_state == MFI_STATE_FAULT || sc->do_timedout_reset ||
			mrsas_atomic_read(&sc->target_reset_outstanding)) {

			/* First, freeze further IOs to come to the SIM */
			mrsas_xpt_freeze(sc);

			/* If this is an IO timeout then go for target reset */
			if (mrsas_atomic_read(&sc->target_reset_outstanding)) {
				device_printf(sc->mrsas_dev, "Initiating Target RESET "
				    "because of SCSI IO timeout!\n");

				/* Let the remaining IOs to complete */
				msleep(&sc->ocr_chan, &sc->sim_lock, PRIBIO,
				      "mrsas_reset_targets", 5 * hz);

				/* Try to reset the target device */
				if (mrsas_reset_targets(sc) == FAIL)
					tm_target_reset_failed = 1;
			}

			/* If this is a DCMD timeout or FW fault,
			 * then go for controller reset
			 */
			if (fw_state == MFI_STATE_FAULT || tm_target_reset_failed ||
			    (sc->do_timedout_reset == MFI_DCMD_TIMEOUT_OCR)) {
				if (tm_target_reset_failed)
					device_printf(sc->mrsas_dev, "Initiaiting OCR because of "
					    "TM FAILURE!\n");
				else
					device_printf(sc->mrsas_dev, "Initiaiting OCR "
						"because of %s!\n", sc->do_timedout_reset ?
						"DCMD IO Timeout" : "FW fault");

				mtx_lock_spin(&sc->ioctl_lock);
				sc->reset_in_progress = 1;
				mtx_unlock_spin(&sc->ioctl_lock);
				sc->reset_count++;
				
				/*
				 * Wait for the AEN task to be completed if it is running.
				 */
				mtx_unlock(&sc->sim_lock);
				taskqueue_drain(sc->ev_tq, &sc->ev_task);
				mtx_lock(&sc->sim_lock);

				taskqueue_block(sc->ev_tq);
				/* Try to reset the controller */
				mrsas_reset_ctrl(sc, sc->do_timedout_reset);

				sc->do_timedout_reset = 0;
				sc->reset_in_progress = 0;
				tm_target_reset_failed = 0;
				mrsas_atomic_set(&sc->target_reset_outstanding, 0);
				memset(sc->target_reset_pool, 0,
				    sizeof(sc->target_reset_pool));
				taskqueue_unblock(sc->ev_tq);
			}

			/* Now allow IOs to come to the SIM */
			 mrsas_xpt_release(sc);
		}
	}
	mtx_unlock(&sc->sim_lock);
	sc->ocr_thread_active = 0;
	mrsas_kproc_exit(0);
}

/*
 * mrsas_reset_reply_desc:	Reset Reply descriptor as part of OCR.
 * input:					Adapter Context.
 *
 * This function will clear reply descriptor so that post OCR driver and FW will
 * lost old history.
 */
void
mrsas_reset_reply_desc(struct mrsas_softc *sc)
{
	int i, count;
	pMpi2ReplyDescriptorsUnion_t reply_desc;

	count = sc->msix_vectors > 0 ? sc->msix_vectors : 1;
	for (i = 0; i < count; i++)
		sc->last_reply_idx[i] = 0;

	reply_desc = sc->reply_desc_mem;
	for (i = 0; i < sc->reply_q_depth; i++, reply_desc++) {
		reply_desc->Words = MRSAS_ULONG_MAX;
	}
}

/*
 * mrsas_reset_ctrl:	Core function to OCR/Kill adapter.
 * input:				Adapter Context.
 *
 * This function will run from thread context so that it can sleep. 1. Do not
 * handle OCR if FW is in HW critical error. 2. Wait for outstanding command
 * to complete for 180 seconds. 3. If #2 does not find any outstanding
 * command Controller is in working state, so skip OCR. Otherwise, do
 * OCR/kill Adapter based on flag disableOnlineCtrlReset. 4. Start of the
 * OCR, return all SCSI command back to CAM layer which has ccb_ptr. 5. Post
 * OCR, Re-fire Management command and move Controller to Operation state.
 */
int
mrsas_reset_ctrl(struct mrsas_softc *sc, u_int8_t reset_reason)
{
	int retval = SUCCESS, i, j, retry = 0;
	u_int32_t host_diag, abs_state, status_reg, reset_adapter;
	union ccb *ccb;
	struct mrsas_mfi_cmd *mfi_cmd;
	struct mrsas_mpt_cmd *mpt_cmd;
	union mrsas_evt_class_locale class_locale;
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc;

	if (sc->adprecovery == MRSAS_HW_CRITICAL_ERROR) {
		device_printf(sc->mrsas_dev,
		    "mrsas: Hardware critical error, returning FAIL.\n");
		return FAIL;
	}
	mrsas_set_bit(MRSAS_FUSION_IN_RESET, &sc->reset_flags);
	sc->adprecovery = MRSAS_ADPRESET_SM_INFAULT;
	mrsas_disable_intr(sc);
	msleep(&sc->ocr_chan, &sc->sim_lock, PRIBIO, "mrsas_ocr",
	    sc->mrsas_fw_fault_check_delay * hz);

	/* First try waiting for commands to complete */
	if (mrsas_wait_for_outstanding(sc, reset_reason)) {
		mrsas_dprint(sc, MRSAS_OCR,
		    "resetting adapter from %s.\n",
		    __func__);
		/* Now return commands back to the CAM layer */
		mtx_unlock(&sc->sim_lock);
		for (i = 0; i < sc->max_fw_cmds; i++) {
			mpt_cmd = sc->mpt_cmd_list[i];

			if (mpt_cmd->peer_cmd) {
				mrsas_dprint(sc, MRSAS_OCR,
				    "R1 FP command [%d] - (mpt_cmd) %p, (peer_cmd) %p\n",
				    i, mpt_cmd, mpt_cmd->peer_cmd);
			}

			if (mpt_cmd->ccb_ptr) {
				if (mpt_cmd->callout_owner) {
					ccb = (union ccb *)(mpt_cmd->ccb_ptr);
					ccb->ccb_h.status = CAM_SCSI_BUS_RESET;
					mrsas_cmd_done(sc, mpt_cmd);
				} else {
					mpt_cmd->ccb_ptr = NULL;
					mrsas_release_mpt_cmd(mpt_cmd);
				}
			}
		}

		mrsas_atomic_set(&sc->fw_outstanding, 0);

		mtx_lock(&sc->sim_lock);

		status_reg = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
		    outbound_scratch_pad));
		abs_state = status_reg & MFI_STATE_MASK;
		reset_adapter = status_reg & MFI_RESET_ADAPTER;
		if (sc->disableOnlineCtrlReset ||
		    (abs_state == MFI_STATE_FAULT && !reset_adapter)) {
			/* Reset not supported, kill adapter */
			mrsas_dprint(sc, MRSAS_OCR, "Reset not supported, killing adapter.\n");
			mrsas_kill_hba(sc);
			retval = FAIL;
			goto out;
		}
		/* Now try to reset the chip */
		for (i = 0; i < MRSAS_FUSION_MAX_RESET_TRIES; i++) {
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, fusion_seq_offset),
			    MPI2_WRSEQ_FLUSH_KEY_VALUE);
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, fusion_seq_offset),
			    MPI2_WRSEQ_1ST_KEY_VALUE);
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, fusion_seq_offset),
			    MPI2_WRSEQ_2ND_KEY_VALUE);
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, fusion_seq_offset),
			    MPI2_WRSEQ_3RD_KEY_VALUE);
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, fusion_seq_offset),
			    MPI2_WRSEQ_4TH_KEY_VALUE);
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, fusion_seq_offset),
			    MPI2_WRSEQ_5TH_KEY_VALUE);
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, fusion_seq_offset),
			    MPI2_WRSEQ_6TH_KEY_VALUE);

			/* Check that the diag write enable (DRWE) bit is on */
			host_diag = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
			    fusion_host_diag));
			retry = 0;
			while (!(host_diag & HOST_DIAG_WRITE_ENABLE)) {
				DELAY(100 * 1000);
				host_diag = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
				    fusion_host_diag));
				if (retry++ == 100) {
					mrsas_dprint(sc, MRSAS_OCR,
					    "Host diag unlock failed!\n");
					break;
				}
			}
			if (!(host_diag & HOST_DIAG_WRITE_ENABLE))
				continue;

			/* Send chip reset command */
			mrsas_write_reg(sc, offsetof(mrsas_reg_set, fusion_host_diag),
			    host_diag | HOST_DIAG_RESET_ADAPTER);
			DELAY(3000 * 1000);

			/* Make sure reset adapter bit is cleared */
			host_diag = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
			    fusion_host_diag));
			retry = 0;
			while (host_diag & HOST_DIAG_RESET_ADAPTER) {
				DELAY(100 * 1000);
				host_diag = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
				    fusion_host_diag));
				if (retry++ == 1000) {
					mrsas_dprint(sc, MRSAS_OCR,
					    "Diag reset adapter never cleared!\n");
					break;
				}
			}
			if (host_diag & HOST_DIAG_RESET_ADAPTER)
				continue;

			abs_state = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
			    outbound_scratch_pad)) & MFI_STATE_MASK;
			retry = 0;

			while ((abs_state <= MFI_STATE_FW_INIT) && (retry++ < 1000)) {
				DELAY(100 * 1000);
				abs_state = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
				    outbound_scratch_pad)) & MFI_STATE_MASK;
			}
			if (abs_state <= MFI_STATE_FW_INIT) {
				mrsas_dprint(sc, MRSAS_OCR, "firmware state < MFI_STATE_FW_INIT,"
				    " state = 0x%x\n", abs_state);
				continue;
			}
			/* Wait for FW to become ready */
			if (mrsas_transition_to_ready(sc, 1)) {
				mrsas_dprint(sc, MRSAS_OCR,
				    "mrsas: Failed to transition controller to ready.\n");
				continue;
			}
			mrsas_reset_reply_desc(sc);
			if (mrsas_ioc_init(sc)) {
				mrsas_dprint(sc, MRSAS_OCR, "mrsas_ioc_init() failed!\n");
				continue;
			}
			for (j = 0; j < sc->max_fw_cmds; j++) {
				mpt_cmd = sc->mpt_cmd_list[j];
				if (mpt_cmd->sync_cmd_idx != (u_int32_t)MRSAS_ULONG_MAX) {
					mfi_cmd = sc->mfi_cmd_list[mpt_cmd->sync_cmd_idx];
					/* If not an IOCTL then release the command else re-fire */
					if (!mfi_cmd->sync_cmd) {
						mrsas_release_mfi_cmd(mfi_cmd);
					} else {
						req_desc = mrsas_get_request_desc(sc,
						    mfi_cmd->cmd_id.context.smid - 1);
						mrsas_dprint(sc, MRSAS_OCR,
						    "Re-fire command DCMD opcode 0x%x index %d\n ",
						    mfi_cmd->frame->dcmd.opcode, j);
						if (!req_desc)
							device_printf(sc->mrsas_dev, 
							    "Cannot build MPT cmd.\n");
						else
							mrsas_fire_cmd(sc, req_desc->addr.u.low,
							    req_desc->addr.u.high);
					}
				}
			}

			/* Reset load balance info */
			memset(sc->load_balance_info, 0,
			    sizeof(LD_LOAD_BALANCE_INFO) * MAX_LOGICAL_DRIVES_EXT);

			if (mrsas_get_ctrl_info(sc)) {
				mrsas_kill_hba(sc);
				retval = FAIL;
				goto out;
			}
			if (!mrsas_get_map_info(sc))
				mrsas_sync_map_info(sc);

			megasas_setup_jbod_map(sc);

			if ((sc->is_ventura || sc->is_aero) && sc->streamDetectByLD) {
				for (j = 0; j < MAX_LOGICAL_DRIVES_EXT; ++j) {
					memset(sc->streamDetectByLD[i], 0, sizeof(LD_STREAM_DETECT));
					sc->streamDetectByLD[i]->mruBitMap = MR_STREAM_BITMAP;
				}
			}

			mrsas_clear_bit(MRSAS_FUSION_IN_RESET, &sc->reset_flags);
			mrsas_enable_intr(sc);
			sc->adprecovery = MRSAS_HBA_OPERATIONAL;

			/* Register AEN with FW for last sequence number */
			class_locale.members.reserved = 0;
			class_locale.members.locale = MR_EVT_LOCALE_ALL;
			class_locale.members.class = MR_EVT_CLASS_DEBUG;

			mtx_unlock(&sc->sim_lock);
			if (mrsas_register_aen(sc, sc->last_seq_num,
			    class_locale.word)) {
				device_printf(sc->mrsas_dev,
				    "ERROR: AEN registration FAILED from OCR !!! "
				    "Further events from the controller cannot be notified."
				    "Either there is some problem in the controller"
				    "or the controller does not support AEN.\n"
				    "Please contact to the SUPPORT TEAM if the problem persists\n");
			}
			mtx_lock(&sc->sim_lock);

			/* Adapter reset completed successfully */
			device_printf(sc->mrsas_dev, "Reset successful\n");
			retval = SUCCESS;
			goto out;
		}
		/* Reset failed, kill the adapter */
		device_printf(sc->mrsas_dev, "Reset failed, killing adapter.\n");
		mrsas_kill_hba(sc);
		retval = FAIL;
	} else {
		mrsas_clear_bit(MRSAS_FUSION_IN_RESET, &sc->reset_flags);
		mrsas_enable_intr(sc);
		sc->adprecovery = MRSAS_HBA_OPERATIONAL;
	}
out:
	mrsas_clear_bit(MRSAS_FUSION_IN_RESET, &sc->reset_flags);
	mrsas_dprint(sc, MRSAS_OCR,
	    "Reset Exit with %d.\n", retval);
	return retval;
}

/*
 * mrsas_kill_hba:	Kill HBA when OCR is not supported
 * input:			Adapter Context.
 *
 * This function will kill HBA when OCR is not supported.
 */
void
mrsas_kill_hba(struct mrsas_softc *sc)
{
	sc->adprecovery = MRSAS_HW_CRITICAL_ERROR;
	DELAY(1000 * 1000);
	mrsas_dprint(sc, MRSAS_OCR, "%s\n", __func__);
	mrsas_write_reg(sc, offsetof(mrsas_reg_set, doorbell),
	    MFI_STOP_ADP);
	/* Flush */
	mrsas_read_reg(sc, offsetof(mrsas_reg_set, doorbell));
	mrsas_complete_outstanding_ioctls(sc);
}

/**
 * mrsas_complete_outstanding_ioctls	Complete pending IOCTLS after kill_hba
 * input:			Controller softc
 *
 * Returns void
 */
void 
mrsas_complete_outstanding_ioctls(struct mrsas_softc *sc)
{
	int i;
	struct mrsas_mpt_cmd *cmd_mpt;
	struct mrsas_mfi_cmd *cmd_mfi;
	u_int32_t count, MSIxIndex;

	count = sc->msix_vectors > 0 ? sc->msix_vectors : 1;
	for (i = 0; i < sc->max_fw_cmds; i++) {
		cmd_mpt = sc->mpt_cmd_list[i];

		if (cmd_mpt->sync_cmd_idx != (u_int32_t)MRSAS_ULONG_MAX) {
			cmd_mfi = sc->mfi_cmd_list[cmd_mpt->sync_cmd_idx];
			if (cmd_mfi->sync_cmd && cmd_mfi->frame->hdr.cmd != MFI_CMD_ABORT) {
				for (MSIxIndex = 0; MSIxIndex < count; MSIxIndex++)
					mrsas_complete_mptmfi_passthru(sc, cmd_mfi,
					    cmd_mpt->io_request->RaidContext.raid_context.status);
			}
		}
	}
}

/*
 * mrsas_wait_for_outstanding:	Wait for outstanding commands
 * input:						Adapter Context.
 *
 * This function will wait for 180 seconds for outstanding commands to be
 * completed.
 */
int
mrsas_wait_for_outstanding(struct mrsas_softc *sc, u_int8_t check_reason)
{
	int i, outstanding, retval = 0;
	u_int32_t fw_state, count, MSIxIndex;


	for (i = 0; i < MRSAS_RESET_WAIT_TIME; i++) {
		if (sc->remove_in_progress) {
			mrsas_dprint(sc, MRSAS_OCR,
			    "Driver remove or shutdown called.\n");
			retval = 1;
			goto out;
		}
		/* Check if firmware is in fault state */
		fw_state = mrsas_read_reg_with_retries(sc, offsetof(mrsas_reg_set,
		    outbound_scratch_pad)) & MFI_STATE_MASK;
		if (fw_state == MFI_STATE_FAULT) {
			mrsas_dprint(sc, MRSAS_OCR,
			    "Found FW in FAULT state, will reset adapter.\n");
			count = sc->msix_vectors > 0 ? sc->msix_vectors : 1;
			mtx_unlock(&sc->sim_lock);
			for (MSIxIndex = 0; MSIxIndex < count; MSIxIndex++)
				mrsas_complete_cmd(sc, MSIxIndex);
			mtx_lock(&sc->sim_lock);
			retval = 1;
			goto out;
		}
		if (check_reason == MFI_DCMD_TIMEOUT_OCR) {
			mrsas_dprint(sc, MRSAS_OCR,
			    "DCMD IO TIMEOUT detected, will reset adapter.\n");
			retval = 1;
			goto out;
		}
		outstanding = mrsas_atomic_read(&sc->fw_outstanding);
		if (!outstanding)
			goto out;

		if (!(i % MRSAS_RESET_NOTICE_INTERVAL)) {
			mrsas_dprint(sc, MRSAS_OCR, "[%2d]waiting for %d "
			    "commands to complete\n", i, outstanding);
			count = sc->msix_vectors > 0 ? sc->msix_vectors : 1;
			mtx_unlock(&sc->sim_lock);
			for (MSIxIndex = 0; MSIxIndex < count; MSIxIndex++)
				mrsas_complete_cmd(sc, MSIxIndex);
			mtx_lock(&sc->sim_lock);
		}
		DELAY(1000 * 1000);
	}

	if (mrsas_atomic_read(&sc->fw_outstanding)) {
		mrsas_dprint(sc, MRSAS_OCR,
		    " pending commands remain after waiting,"
		    " will reset adapter.\n");
		retval = 1;
	}
out:
	return retval;
}

/*
 * mrsas_release_mfi_cmd:	Return a cmd to free command pool
 * input:					Command packet for return to free cmd pool
 *
 * This function returns the MFI & MPT command to the command list.
 */
void
mrsas_release_mfi_cmd(struct mrsas_mfi_cmd *cmd_mfi)
{
	struct mrsas_softc *sc = cmd_mfi->sc;
	struct mrsas_mpt_cmd *cmd_mpt;


	mtx_lock(&sc->mfi_cmd_pool_lock);
	/*
	 * Release the mpt command (if at all it is allocated
	 * associated with the mfi command
	 */
	if (cmd_mfi->cmd_id.context.smid) {
		mtx_lock(&sc->mpt_cmd_pool_lock);
		/* Get the mpt cmd from mfi cmd frame's smid value */
		cmd_mpt = sc->mpt_cmd_list[cmd_mfi->cmd_id.context.smid-1];
		cmd_mpt->flags = 0;
		cmd_mpt->sync_cmd_idx = (u_int32_t)MRSAS_ULONG_MAX;
		TAILQ_INSERT_HEAD(&(sc->mrsas_mpt_cmd_list_head), cmd_mpt, next);
		mtx_unlock(&sc->mpt_cmd_pool_lock);
	}
	/* Release the mfi command */
	cmd_mfi->ccb_ptr = NULL;
	cmd_mfi->cmd_id.frame_count = 0;
	TAILQ_INSERT_HEAD(&(sc->mrsas_mfi_cmd_list_head), cmd_mfi, next);
	mtx_unlock(&sc->mfi_cmd_pool_lock);

	return;
}

/*
 * mrsas_get_controller_info:	Returns FW's controller structure
 * input:						Adapter soft state
 * 								Controller information structure
 *
 * Issues an internal command (DCMD) to get the FW's controller structure. This
 * information is mainly used to find out the maximum IO transfer per command
 * supported by the FW.
 */
static int
mrsas_get_ctrl_info(struct mrsas_softc *sc)
{
	int retcode = 0;
	u_int8_t do_ocr = 1;
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;

	cmd = mrsas_get_mfi_cmd(sc);

	if (!cmd) {
		device_printf(sc->mrsas_dev, "Failed to get a free cmd\n");
		return -ENOMEM;
	}
	dcmd = &cmd->frame->dcmd;

	if (mrsas_alloc_ctlr_info_cmd(sc) != SUCCESS) {
		device_printf(sc->mrsas_dev, "Cannot allocate get ctlr info cmd\n");
		mrsas_release_mfi_cmd(cmd);
		return -ENOMEM;
	}
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = sizeof(struct mrsas_ctrl_info);
	dcmd->opcode = MR_DCMD_CTRL_GET_INFO;
	dcmd->sgl.sge32[0].phys_addr = sc->ctlr_info_phys_addr;
	dcmd->sgl.sge32[0].length = sizeof(struct mrsas_ctrl_info);

	if (!sc->mask_interrupts)
		retcode = mrsas_issue_blocked_cmd(sc, cmd);
	else
		retcode = mrsas_issue_polled(sc, cmd);

	if (retcode == ETIMEDOUT)
		goto dcmd_timeout;
	else
		memcpy(sc->ctrl_info, sc->ctlr_info_mem, sizeof(struct mrsas_ctrl_info));

	do_ocr = 0;
	mrsas_update_ext_vd_details(sc);

	sc->use_seqnum_jbod_fp =
	    sc->ctrl_info->adapterOperations3.useSeqNumJbodFP;
	sc->support_morethan256jbod =
		sc->ctrl_info->adapterOperations4.supportPdMapTargetId;

	sc->disableOnlineCtrlReset =
	    sc->ctrl_info->properties.OnOffProperties.disableOnlineCtrlReset;

dcmd_timeout:
	mrsas_free_ctlr_info_cmd(sc);

	if (do_ocr)
		sc->do_timedout_reset = MFI_DCMD_TIMEOUT_OCR;

	if (!sc->mask_interrupts)
		mrsas_release_mfi_cmd(cmd);

	return (retcode);
}

/*
 * mrsas_update_ext_vd_details : Update details w.r.t Extended VD
 * input:
 *	sc - Controller's softc
*/
static void 
mrsas_update_ext_vd_details(struct mrsas_softc *sc)
{
	u_int32_t ventura_map_sz = 0;
	sc->max256vdSupport =
		sc->ctrl_info->adapterOperations3.supportMaxExtLDs;

	/* Below is additional check to address future FW enhancement */
	if (sc->ctrl_info->max_lds > 64)
		sc->max256vdSupport = 1;

	sc->drv_supported_vd_count = MRSAS_MAX_LD_CHANNELS
	    * MRSAS_MAX_DEV_PER_CHANNEL;
	sc->drv_supported_pd_count = MRSAS_MAX_PD_CHANNELS
	    * MRSAS_MAX_DEV_PER_CHANNEL;
	if (sc->max256vdSupport) {
		sc->fw_supported_vd_count = MAX_LOGICAL_DRIVES_EXT;
		sc->fw_supported_pd_count = MAX_PHYSICAL_DEVICES;
	} else {
		sc->fw_supported_vd_count = MAX_LOGICAL_DRIVES;
		sc->fw_supported_pd_count = MAX_PHYSICAL_DEVICES;
	}

	if (sc->maxRaidMapSize) {
		ventura_map_sz = sc->maxRaidMapSize *
		    MR_MIN_MAP_SIZE;
		sc->current_map_sz = ventura_map_sz;
		sc->max_map_sz = ventura_map_sz;
	} else {
		sc->old_map_sz = sizeof(MR_FW_RAID_MAP) +
		    (sizeof(MR_LD_SPAN_MAP) * (sc->fw_supported_vd_count - 1));
		sc->new_map_sz = sizeof(MR_FW_RAID_MAP_EXT);
		sc->max_map_sz = max(sc->old_map_sz, sc->new_map_sz);
		if (sc->max256vdSupport)
			sc->current_map_sz = sc->new_map_sz;
		else
			sc->current_map_sz = sc->old_map_sz;
	}

	sc->drv_map_sz = sizeof(MR_DRV_RAID_MAP_ALL);
#if VD_EXT_DEBUG
	device_printf(sc->mrsas_dev, "sc->maxRaidMapSize 0x%x \n",
	    sc->maxRaidMapSize);
	device_printf(sc->mrsas_dev,
	    "new_map_sz = 0x%x, old_map_sz = 0x%x, "
	    "ventura_map_sz = 0x%x, current_map_sz = 0x%x "
	    "fusion->drv_map_sz =0x%x, size of driver raid map 0x%lx \n",
	    sc->new_map_sz, sc->old_map_sz, ventura_map_sz,
	    sc->current_map_sz, sc->drv_map_sz, sizeof(MR_DRV_RAID_MAP_ALL));
#endif
}

/*
 * mrsas_alloc_ctlr_info_cmd:	Allocates memory for controller info command
 * input:						Adapter soft state
 *
 * Allocates DMAable memory for the controller info internal command.
 */
int
mrsas_alloc_ctlr_info_cmd(struct mrsas_softc *sc)
{
	int ctlr_info_size;

	/* Allocate get controller info command */
	ctlr_info_size = sizeof(struct mrsas_ctrl_info);
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    1, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    ctlr_info_size,
	    1,
	    ctlr_info_size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &sc->ctlr_info_tag)) {
		device_printf(sc->mrsas_dev, "Cannot allocate ctlr info tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->ctlr_info_tag, (void **)&sc->ctlr_info_mem,
	    BUS_DMA_NOWAIT, &sc->ctlr_info_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot allocate ctlr info cmd mem\n");
		return (ENOMEM);
	}
	if (bus_dmamap_load(sc->ctlr_info_tag, sc->ctlr_info_dmamap,
	    sc->ctlr_info_mem, ctlr_info_size, mrsas_addr_cb,
	    &sc->ctlr_info_phys_addr, BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load ctlr info cmd mem\n");
		return (ENOMEM);
	}
	memset(sc->ctlr_info_mem, 0, ctlr_info_size);
	return (0);
}

/*
 * mrsas_free_ctlr_info_cmd:	Free memory for controller info command
 * input:						Adapter soft state
 *
 * Deallocates memory of the get controller info cmd.
 */
void
mrsas_free_ctlr_info_cmd(struct mrsas_softc *sc)
{
	if (sc->ctlr_info_phys_addr)
		bus_dmamap_unload(sc->ctlr_info_tag, sc->ctlr_info_dmamap);
	if (sc->ctlr_info_mem != NULL)
		bus_dmamem_free(sc->ctlr_info_tag, sc->ctlr_info_mem, sc->ctlr_info_dmamap);
	if (sc->ctlr_info_tag != NULL)
		bus_dma_tag_destroy(sc->ctlr_info_tag);
}

/*
 * mrsas_issue_polled:	Issues a polling command
 * inputs:				Adapter soft state
 * 						Command packet to be issued
 *
 * This function is for posting of internal commands to Firmware.  MFI requires
 * the cmd_status to be set to 0xFF before posting.  The maximun wait time of
 * the poll response timer is 180 seconds.
 */
int
mrsas_issue_polled(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd)
{
	struct mrsas_header *frame_hdr = &cmd->frame->hdr;
	u_int8_t max_wait = MRSAS_INTERNAL_CMD_WAIT_TIME;
	int i, retcode = SUCCESS;

	frame_hdr->cmd_status = 0xFF;
	frame_hdr->flags |= MFI_FRAME_DONT_POST_IN_REPLY_QUEUE;

	/* Issue the frame using inbound queue port */
	if (mrsas_issue_dcmd(sc, cmd)) {
		device_printf(sc->mrsas_dev, "Cannot issue DCMD internal command.\n");
		return (1);
	}
	/*
	 * Poll response timer to wait for Firmware response.  While this
	 * timer with the DELAY call could block CPU, the time interval for
	 * this is only 1 millisecond.
	 */
	if (frame_hdr->cmd_status == 0xFF) {
		for (i = 0; i < (max_wait * 1000); i++) {
			if (frame_hdr->cmd_status == 0xFF)
				DELAY(1000);
			else
				break;
		}
	}
	if (frame_hdr->cmd_status == 0xFF) {
		device_printf(sc->mrsas_dev, "DCMD timed out after %d "
		    "seconds from %s\n", max_wait, __func__);
		device_printf(sc->mrsas_dev, "DCMD opcode 0x%X\n",
		    cmd->frame->dcmd.opcode);
		retcode = ETIMEDOUT;
	}
	return (retcode);
}

/*
 * mrsas_issue_dcmd:	Issues a MFI Pass thru cmd
 * input:				Adapter soft state mfi cmd pointer
 *
 * This function is called by mrsas_issued_blocked_cmd() and
 * mrsas_issued_polled(), to build the MPT command and then fire the command
 * to Firmware.
 */
int
mrsas_issue_dcmd(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd)
{
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc;

	req_desc = mrsas_build_mpt_cmd(sc, cmd);
	if (!req_desc) {
		device_printf(sc->mrsas_dev, "Cannot build MPT cmd.\n");
		return (1);
	}
	mrsas_fire_cmd(sc, req_desc->addr.u.low, req_desc->addr.u.high);

	return (0);
}

/*
 * mrsas_build_mpt_cmd:	Calls helper function to build Passthru cmd
 * input:				Adapter soft state mfi cmd to build
 *
 * This function is called by mrsas_issue_cmd() to build the MPT-MFI passthru
 * command and prepares the MPT command to send to Firmware.
 */
MRSAS_REQUEST_DESCRIPTOR_UNION *
mrsas_build_mpt_cmd(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd)
{
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	u_int16_t index;

	if (mrsas_build_mptmfi_passthru(sc, cmd)) {
		device_printf(sc->mrsas_dev, "Cannot build MPT-MFI passthru cmd.\n");
		return NULL;
	}
	index = cmd->cmd_id.context.smid;

	req_desc = mrsas_get_request_desc(sc, index - 1);
	if (!req_desc)
		return NULL;

	req_desc->addr.Words = 0;
	req_desc->SCSIIO.RequestFlags = (MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO << MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

	req_desc->SCSIIO.SMID = index;

	return (req_desc);
}

/*
 * mrsas_build_mptmfi_passthru:	Builds a MPT MFI Passthru command
 * input:						Adapter soft state mfi cmd pointer
 *
 * The MPT command and the io_request are setup as a passthru command. The SGE
 * chain address is set to frame_phys_addr of the MFI command.
 */
u_int8_t
mrsas_build_mptmfi_passthru(struct mrsas_softc *sc, struct mrsas_mfi_cmd *mfi_cmd)
{
	MPI25_IEEE_SGE_CHAIN64 *mpi25_ieee_chain;
	PTR_MRSAS_RAID_SCSI_IO_REQUEST io_req;
	struct mrsas_mpt_cmd *mpt_cmd;
	struct mrsas_header *frame_hdr = &mfi_cmd->frame->hdr;

	mpt_cmd = mrsas_get_mpt_cmd(sc);
	if (!mpt_cmd)
		return (1);

	/* Save the smid. To be used for returning the cmd */
	mfi_cmd->cmd_id.context.smid = mpt_cmd->index;

	mpt_cmd->sync_cmd_idx = mfi_cmd->index;

	/*
	 * For cmds where the flag is set, store the flag and check on
	 * completion. For cmds with this flag, don't call
	 * mrsas_complete_cmd.
	 */

	if (frame_hdr->flags & MFI_FRAME_DONT_POST_IN_REPLY_QUEUE)
		mpt_cmd->flags = MFI_FRAME_DONT_POST_IN_REPLY_QUEUE;

	io_req = mpt_cmd->io_request;

	if (sc->mrsas_gen3_ctrl || sc->is_ventura || sc->is_aero) {
		pMpi25IeeeSgeChain64_t sgl_ptr_end = (pMpi25IeeeSgeChain64_t)&io_req->SGL;

		sgl_ptr_end += sc->max_sge_in_main_msg - 1;
		sgl_ptr_end->Flags = 0;
	}
	mpi25_ieee_chain = (MPI25_IEEE_SGE_CHAIN64 *) & io_req->SGL.IeeeChain;

	io_req->Function = MRSAS_MPI2_FUNCTION_PASSTHRU_IO_REQUEST;
	io_req->SGLOffset0 = offsetof(MRSAS_RAID_SCSI_IO_REQUEST, SGL) / 4;
	io_req->ChainOffset = sc->chain_offset_mfi_pthru;

	mpi25_ieee_chain->Address = mfi_cmd->frame_phys_addr;

	mpi25_ieee_chain->Flags = IEEE_SGE_FLAGS_CHAIN_ELEMENT |
	    MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR;

	mpi25_ieee_chain->Length = sc->max_chain_frame_sz;

	return (0);
}

/*
 * mrsas_issue_blocked_cmd:	Synchronous wrapper around regular FW cmds
 * input:					Adapter soft state Command to be issued
 *
 * This function waits on an event for the command to be returned from the ISR.
 * Max wait time is MRSAS_INTERNAL_CMD_WAIT_TIME secs. Used for issuing
 * internal and ioctl commands.
 */
int
mrsas_issue_blocked_cmd(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd)
{
	u_int8_t max_wait = MRSAS_INTERNAL_CMD_WAIT_TIME;
	unsigned long total_time = 0;
	int retcode = SUCCESS;

	/* Initialize cmd_status */
	cmd->cmd_status = 0xFF;

	/* Build MPT-MFI command for issue to FW */
	if (mrsas_issue_dcmd(sc, cmd)) {
		device_printf(sc->mrsas_dev, "Cannot issue DCMD internal command.\n");
		return (1);
	}
	sc->chan = (void *)&cmd;

	while (1) {
		if (cmd->cmd_status == 0xFF) {
			tsleep((void *)&sc->chan, 0, "mrsas_sleep", hz);
		} else
			break;

		if (!cmd->sync_cmd) {	/* cmd->sync will be set for an IOCTL
					 * command */
			total_time++;
			if (total_time >= max_wait) {
				device_printf(sc->mrsas_dev,
				    "Internal command timed out after %d seconds.\n", max_wait);
				retcode = 1;
				break;
			}
		}
	}

	if (cmd->cmd_status == 0xFF) {
		device_printf(sc->mrsas_dev, "DCMD timed out after %d "
		    "seconds from %s\n", max_wait, __func__);
		device_printf(sc->mrsas_dev, "DCMD opcode 0x%X\n",
		    cmd->frame->dcmd.opcode);
		retcode = ETIMEDOUT;
	}
	return (retcode);
}

/*
 * mrsas_complete_mptmfi_passthru:	Completes a command
 * input:	@sc:					Adapter soft state
 * 			@cmd:					Command to be completed
 * 			@status:				cmd completion status
 *
 * This function is called from mrsas_complete_cmd() after an interrupt is
 * received from Firmware, and io_request->Function is
 * MRSAS_MPI2_FUNCTION_PASSTHRU_IO_REQUEST.
 */
void
mrsas_complete_mptmfi_passthru(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd,
    u_int8_t status)
{
	struct mrsas_header *hdr = &cmd->frame->hdr;
	u_int8_t cmd_status = cmd->frame->hdr.cmd_status;

	/* Reset the retry counter for future re-tries */
	cmd->retry_for_fw_reset = 0;

	if (cmd->ccb_ptr)
		cmd->ccb_ptr = NULL;

	switch (hdr->cmd) {
	case MFI_CMD_INVALID:
		device_printf(sc->mrsas_dev, "MFI_CMD_INVALID command.\n");
		break;
	case MFI_CMD_PD_SCSI_IO:
	case MFI_CMD_LD_SCSI_IO:
		/*
		 * MFI_CMD_PD_SCSI_IO and MFI_CMD_LD_SCSI_IO could have been
		 * issued either through an IO path or an IOCTL path. If it
		 * was via IOCTL, we will send it to internal completion.
		 */
		if (cmd->sync_cmd) {
			cmd->sync_cmd = 0;
			mrsas_wakeup(sc, cmd);
			break;
		}
	case MFI_CMD_SMP:
	case MFI_CMD_STP:
	case MFI_CMD_DCMD:
		/* Check for LD map update */
		if ((cmd->frame->dcmd.opcode == MR_DCMD_LD_MAP_GET_INFO) &&
		    (cmd->frame->dcmd.mbox.b[1] == 1)) {
			sc->fast_path_io = 0;
			mtx_lock(&sc->raidmap_lock);
			sc->map_update_cmd = NULL;
			if (cmd_status != 0) {
				if (cmd_status != MFI_STAT_NOT_FOUND)
					device_printf(sc->mrsas_dev, "map sync failed, status=%x\n", cmd_status);
				else {
					mrsas_release_mfi_cmd(cmd);
					mtx_unlock(&sc->raidmap_lock);
					break;
				}
			} else
				sc->map_id++;
			mrsas_release_mfi_cmd(cmd);
			if (MR_ValidateMapInfo(sc))
				sc->fast_path_io = 0;
			else
				sc->fast_path_io = 1;
			mrsas_sync_map_info(sc);
			mtx_unlock(&sc->raidmap_lock);
			break;
		}
		if (cmd->frame->dcmd.opcode == MR_DCMD_CTRL_EVENT_GET_INFO ||
		    cmd->frame->dcmd.opcode == MR_DCMD_CTRL_EVENT_GET) {
			sc->mrsas_aen_triggered = 0;
		}
		/* FW has an updated PD sequence */
		if ((cmd->frame->dcmd.opcode ==
		    MR_DCMD_SYSTEM_PD_MAP_GET_INFO) &&
		    (cmd->frame->dcmd.mbox.b[0] == 1)) {

			mtx_lock(&sc->raidmap_lock);
			sc->jbod_seq_cmd = NULL;
			mrsas_release_mfi_cmd(cmd);

			if (cmd_status == MFI_STAT_OK) {
				sc->pd_seq_map_id++;
				/* Re-register a pd sync seq num cmd */
				if (megasas_sync_pd_seq_num(sc, true))
					sc->use_seqnum_jbod_fp = 0;
			} else {
				sc->use_seqnum_jbod_fp = 0;
				device_printf(sc->mrsas_dev,
				    "Jbod map sync failed, status=%x\n", cmd_status);
			}
			mtx_unlock(&sc->raidmap_lock);
			break;
		}
		/* See if got an event notification */
		if (cmd->frame->dcmd.opcode == MR_DCMD_CTRL_EVENT_WAIT)
			mrsas_complete_aen(sc, cmd);
		else
			mrsas_wakeup(sc, cmd);
		break;
	case MFI_CMD_ABORT:
		/* Command issued to abort another cmd return */
		mrsas_complete_abort(sc, cmd);
		break;
	default:
		device_printf(sc->mrsas_dev, "Unknown command completed! [0x%X]\n", hdr->cmd);
		break;
	}
}

/*
 * mrsas_wakeup:	Completes an internal command
 * input:			Adapter soft state
 * 					Command to be completed
 *
 * In mrsas_issue_blocked_cmd(), after a command is issued to Firmware, a wait
 * timer is started.  This function is called from
 * mrsas_complete_mptmfi_passthru() as it completes the command, to wake up
 * from the command wait.
 */
void
mrsas_wakeup(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd)
{
	cmd->cmd_status = cmd->frame->io.cmd_status;

	if (cmd->cmd_status == 0xFF)
		cmd->cmd_status = 0;

	sc->chan = (void *)&cmd;
	wakeup_one((void *)&sc->chan);
	return;
}

/*
 * mrsas_shutdown_ctlr:       Instructs FW to shutdown the controller input:
 * Adapter soft state Shutdown/Hibernate
 *
 * This function issues a DCMD internal command to Firmware to initiate shutdown
 * of the controller.
 */
static void
mrsas_shutdown_ctlr(struct mrsas_softc *sc, u_int32_t opcode)
{
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;

	if (sc->adprecovery == MRSAS_HW_CRITICAL_ERROR)
		return;

	cmd = mrsas_get_mfi_cmd(sc);
	if (!cmd) {
		device_printf(sc->mrsas_dev, "Cannot allocate for shutdown cmd.\n");
		return;
	}
	if (sc->aen_cmd)
		mrsas_issue_blocked_abort_cmd(sc, sc->aen_cmd);
	if (sc->map_update_cmd)
		mrsas_issue_blocked_abort_cmd(sc, sc->map_update_cmd);
	if (sc->jbod_seq_cmd)
		mrsas_issue_blocked_abort_cmd(sc, sc->jbod_seq_cmd);

	dcmd = &cmd->frame->dcmd;
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0x0;
	dcmd->sge_count = 0;
	dcmd->flags = MFI_FRAME_DIR_NONE;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = 0;
	dcmd->opcode = opcode;

	device_printf(sc->mrsas_dev, "Preparing to shut down controller.\n");

	mrsas_issue_blocked_cmd(sc, cmd);
	mrsas_release_mfi_cmd(cmd);

	return;
}

/*
 * mrsas_flush_cache:         Requests FW to flush all its caches input:
 * Adapter soft state
 *
 * This function is issues a DCMD internal command to Firmware to initiate
 * flushing of all caches.
 */
static void
mrsas_flush_cache(struct mrsas_softc *sc)
{
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;

	if (sc->adprecovery == MRSAS_HW_CRITICAL_ERROR)
		return;

	cmd = mrsas_get_mfi_cmd(sc);
	if (!cmd) {
		device_printf(sc->mrsas_dev, "Cannot allocate for flush cache cmd.\n");
		return;
	}
	dcmd = &cmd->frame->dcmd;
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0x0;
	dcmd->sge_count = 0;
	dcmd->flags = MFI_FRAME_DIR_NONE;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = 0;
	dcmd->opcode = MR_DCMD_CTRL_CACHE_FLUSH;
	dcmd->mbox.b[0] = MR_FLUSH_CTRL_CACHE | MR_FLUSH_DISK_CACHE;

	mrsas_issue_blocked_cmd(sc, cmd);
	mrsas_release_mfi_cmd(cmd);

	return;
}

int
megasas_sync_pd_seq_num(struct mrsas_softc *sc, boolean_t pend)
{
	int retcode = 0;
	u_int8_t do_ocr = 1;
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;
	uint32_t pd_seq_map_sz;
	struct MR_PD_CFG_SEQ_NUM_SYNC *pd_sync;
	bus_addr_t pd_seq_h;

	pd_seq_map_sz = sizeof(struct MR_PD_CFG_SEQ_NUM_SYNC) +
	    (sizeof(struct MR_PD_CFG_SEQ) *
	    (MAX_PHYSICAL_DEVICES - 1));

	cmd = mrsas_get_mfi_cmd(sc);
	if (!cmd) {
		device_printf(sc->mrsas_dev,
		    "Cannot alloc for ld map info cmd.\n");
		return 1;
	}
	dcmd = &cmd->frame->dcmd;

	pd_sync = (void *)sc->jbodmap_mem[(sc->pd_seq_map_id & 1)];
	pd_seq_h = sc->jbodmap_phys_addr[(sc->pd_seq_map_id & 1)];
	if (!pd_sync) {
		device_printf(sc->mrsas_dev,
		    "Failed to alloc mem for jbod map info.\n");
		mrsas_release_mfi_cmd(cmd);
		return (ENOMEM);
	}
	memset(pd_sync, 0, pd_seq_map_sz);
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = (pd_seq_map_sz);
	dcmd->opcode = (MR_DCMD_SYSTEM_PD_MAP_GET_INFO);
	dcmd->sgl.sge32[0].phys_addr = (pd_seq_h);
	dcmd->sgl.sge32[0].length = (pd_seq_map_sz);

	if (pend) {
		dcmd->mbox.b[0] = MRSAS_DCMD_MBOX_PEND_FLAG;
		dcmd->flags = (MFI_FRAME_DIR_WRITE);
		sc->jbod_seq_cmd = cmd;
		if (mrsas_issue_dcmd(sc, cmd)) {
			device_printf(sc->mrsas_dev,
			    "Fail to send sync map info command.\n");
			return 1;
		} else
			return 0;
	} else
		dcmd->flags = MFI_FRAME_DIR_READ;

	retcode = mrsas_issue_polled(sc, cmd);
	if (retcode == ETIMEDOUT)
		goto dcmd_timeout;

	if (pd_sync->count > MAX_PHYSICAL_DEVICES) {
		device_printf(sc->mrsas_dev,
		    "driver supports max %d JBOD, but FW reports %d\n",
		    MAX_PHYSICAL_DEVICES, pd_sync->count);
		retcode = -EINVAL;
	}
	if (!retcode)
		sc->pd_seq_map_id++;
	do_ocr = 0;

dcmd_timeout:
	if (do_ocr)
		sc->do_timedout_reset = MFI_DCMD_TIMEOUT_OCR;

	return (retcode);
}

/*
 * mrsas_get_map_info:        Load and validate RAID map input:
 * Adapter instance soft state
 *
 * This function calls mrsas_get_ld_map_info() and MR_ValidateMapInfo() to load
 * and validate RAID map.  It returns 0 if successful, 1 other- wise.
 */
static int
mrsas_get_map_info(struct mrsas_softc *sc)
{
	uint8_t retcode = 0;

	sc->fast_path_io = 0;
	if (!mrsas_get_ld_map_info(sc)) {
		retcode = MR_ValidateMapInfo(sc);
		if (retcode == 0) {
			sc->fast_path_io = 1;
			return 0;
		}
	}
	return 1;
}

/*
 * mrsas_get_ld_map_info:      Get FW's ld_map structure input:
 * Adapter instance soft state
 *
 * Issues an internal command (DCMD) to get the FW's controller PD list
 * structure.
 */
static int
mrsas_get_ld_map_info(struct mrsas_softc *sc)
{
	int retcode = 0;
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;
	void *map;
	bus_addr_t map_phys_addr = 0;

	cmd = mrsas_get_mfi_cmd(sc);
	if (!cmd) {
		device_printf(sc->mrsas_dev,
		    "Cannot alloc for ld map info cmd.\n");
		return 1;
	}
	dcmd = &cmd->frame->dcmd;

	map = (void *)sc->raidmap_mem[(sc->map_id & 1)];
	map_phys_addr = sc->raidmap_phys_addr[(sc->map_id & 1)];
	if (!map) {
		device_printf(sc->mrsas_dev,
		    "Failed to alloc mem for ld map info.\n");
		mrsas_release_mfi_cmd(cmd);
		return (ENOMEM);
	}
	memset(map, 0, sizeof(sc->max_map_sz));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = sc->current_map_sz;
	dcmd->opcode = MR_DCMD_LD_MAP_GET_INFO;
	dcmd->sgl.sge32[0].phys_addr = map_phys_addr;
	dcmd->sgl.sge32[0].length = sc->current_map_sz;

	retcode = mrsas_issue_polled(sc, cmd);
	if (retcode == ETIMEDOUT)
		sc->do_timedout_reset = MFI_DCMD_TIMEOUT_OCR;

	return (retcode);
}

/*
 * mrsas_sync_map_info:        Get FW's ld_map structure input:
 * Adapter instance soft state
 *
 * Issues an internal command (DCMD) to get the FW's controller PD list
 * structure.
 */
static int
mrsas_sync_map_info(struct mrsas_softc *sc)
{
	int retcode = 0, i;
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;
	uint32_t size_sync_info, num_lds;
	MR_LD_TARGET_SYNC *target_map = NULL;
	MR_DRV_RAID_MAP_ALL *map;
	MR_LD_RAID *raid;
	MR_LD_TARGET_SYNC *ld_sync;
	bus_addr_t map_phys_addr = 0;

	cmd = mrsas_get_mfi_cmd(sc);
	if (!cmd) {
		device_printf(sc->mrsas_dev, "Cannot alloc for sync map info cmd\n");
		return ENOMEM;
	}
	map = sc->ld_drv_map[sc->map_id & 1];
	num_lds = map->raidMap.ldCount;

	dcmd = &cmd->frame->dcmd;
	size_sync_info = sizeof(MR_LD_TARGET_SYNC) * num_lds;
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	target_map = (MR_LD_TARGET_SYNC *) sc->raidmap_mem[(sc->map_id - 1) & 1];
	memset(target_map, 0, sc->max_map_sz);

	map_phys_addr = sc->raidmap_phys_addr[(sc->map_id - 1) & 1];

	ld_sync = (MR_LD_TARGET_SYNC *) target_map;

	for (i = 0; i < num_lds; i++, ld_sync++) {
		raid = MR_LdRaidGet(i, map);
		ld_sync->targetId = MR_GetLDTgtId(i, map);
		ld_sync->seqNum = raid->seqNum;
	}

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_WRITE;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = sc->current_map_sz;
	dcmd->mbox.b[0] = num_lds;
	dcmd->mbox.b[1] = MRSAS_DCMD_MBOX_PEND_FLAG;
	dcmd->opcode = MR_DCMD_LD_MAP_GET_INFO;
	dcmd->sgl.sge32[0].phys_addr = map_phys_addr;
	dcmd->sgl.sge32[0].length = sc->current_map_sz;

	sc->map_update_cmd = cmd;
	if (mrsas_issue_dcmd(sc, cmd)) {
		device_printf(sc->mrsas_dev,
		    "Fail to send sync map info command.\n");
		return (1);
	}
	return (retcode);
}

/* Input:	dcmd.opcode		- MR_DCMD_PD_GET_INFO
  *		dcmd.mbox.s[0]		- deviceId for this physical drive
  *		dcmd.sge IN		- ptr to returned MR_PD_INFO structure
  * Desc:	Firmware return the physical drive info structure
  *
  */
static void
mrsas_get_pd_info(struct mrsas_softc *sc, u_int16_t device_id)
{
	int retcode;
	u_int8_t do_ocr = 1;
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;

	cmd = mrsas_get_mfi_cmd(sc);

	if (!cmd) {
		device_printf(sc->mrsas_dev,
		    "Cannot alloc for get PD info cmd\n");
		return;
	}
	dcmd = &cmd->frame->dcmd;

	memset(sc->pd_info_mem, 0, sizeof(struct mrsas_pd_info));
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->mbox.s[0] = device_id;
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = sizeof(struct mrsas_pd_info);
	dcmd->opcode = MR_DCMD_PD_GET_INFO;
	dcmd->sgl.sge32[0].phys_addr = (u_int32_t)sc->pd_info_phys_addr;
	dcmd->sgl.sge32[0].length = sizeof(struct mrsas_pd_info);

	if (!sc->mask_interrupts)
		retcode = mrsas_issue_blocked_cmd(sc, cmd);
	else
		retcode = mrsas_issue_polled(sc, cmd);

	if (retcode == ETIMEDOUT)
		goto dcmd_timeout;

	sc->target_list[device_id].interface_type =
		sc->pd_info_mem->state.ddf.pdType.intf;

	do_ocr = 0;

dcmd_timeout:

	if (do_ocr)
		sc->do_timedout_reset = MFI_DCMD_TIMEOUT_OCR;

	if (!sc->mask_interrupts)
		mrsas_release_mfi_cmd(cmd);
}

/*
 * mrsas_add_target:				Add target ID of system PD/VD to driver's data structure.
 * sc:						Adapter's soft state
 * target_id:					Unique target id per controller(managed by driver)
 *						for system PDs- target ID ranges from 0 to (MRSAS_MAX_PD - 1)
 *						for VDs- target ID ranges from MRSAS_MAX_PD to MRSAS_MAX_TM_TARGETS
 * return:					void
 * Descripton:					This function will be called whenever system PD or VD is created.
 */
static void mrsas_add_target(struct mrsas_softc *sc,
	u_int16_t target_id)
{
	sc->target_list[target_id].target_id = target_id;

	device_printf(sc->mrsas_dev,
		"%s created target ID: 0x%x\n",
		(target_id < MRSAS_MAX_PD ? "System PD" : "VD"),
		(target_id < MRSAS_MAX_PD ? target_id : (target_id - MRSAS_MAX_PD)));
	/*
	 * If interrupts are enabled, then only fire DCMD to get pd_info
	 * for system PDs
	 */
	if (!sc->mask_interrupts && sc->pd_info_mem &&
		(target_id < MRSAS_MAX_PD))
		mrsas_get_pd_info(sc, target_id);

}

/*
 * mrsas_remove_target:			Remove target ID of system PD/VD from driver's data structure.
 * sc:						Adapter's soft state
 * target_id:					Unique target id per controller(managed by driver)
 *						for system PDs- target ID ranges from 0 to (MRSAS_MAX_PD - 1)
 *						for VDs- target ID ranges from MRSAS_MAX_PD to MRSAS_MAX_TM_TARGETS
 * return:					void
 * Descripton:					This function will be called whenever system PD or VD is deleted
 */
static void mrsas_remove_target(struct mrsas_softc *sc,
	u_int16_t target_id)
{
	sc->target_list[target_id].target_id = 0xffff;
	device_printf(sc->mrsas_dev,
		"%s deleted target ID: 0x%x\n",
		(target_id < MRSAS_MAX_PD ? "System PD" : "VD"),
		(target_id < MRSAS_MAX_PD ? target_id : (target_id - MRSAS_MAX_PD)));
}

/*
 * mrsas_get_pd_list:           Returns FW's PD list structure input:
 * Adapter soft state
 *
 * Issues an internal command (DCMD) to get the FW's controller PD list
 * structure.  This information is mainly used to find out about system
 * supported by Firmware.
 */
static int
mrsas_get_pd_list(struct mrsas_softc *sc)
{
	int retcode = 0, pd_index = 0, pd_count = 0, pd_list_size;
	u_int8_t do_ocr = 1;
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;
	struct MR_PD_LIST *pd_list_mem;
	struct MR_PD_ADDRESS *pd_addr;
	bus_addr_t pd_list_phys_addr = 0;
	struct mrsas_tmp_dcmd *tcmd;

	cmd = mrsas_get_mfi_cmd(sc);
	if (!cmd) {
		device_printf(sc->mrsas_dev,
		    "Cannot alloc for get PD list cmd\n");
		return 1;
	}
	dcmd = &cmd->frame->dcmd;

	tcmd = malloc(sizeof(struct mrsas_tmp_dcmd), M_MRSAS, M_NOWAIT);
	pd_list_size = MRSAS_MAX_PD * sizeof(struct MR_PD_LIST);
	if (mrsas_alloc_tmp_dcmd(sc, tcmd, pd_list_size) != SUCCESS) {
		device_printf(sc->mrsas_dev,
		    "Cannot alloc dmamap for get PD list cmd\n");
		mrsas_release_mfi_cmd(cmd);
		mrsas_free_tmp_dcmd(tcmd);
		free(tcmd, M_MRSAS);
		return (ENOMEM);
	} else {
		pd_list_mem = tcmd->tmp_dcmd_mem;
		pd_list_phys_addr = tcmd->tmp_dcmd_phys_addr;
	}
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	dcmd->mbox.b[0] = MR_PD_QUERY_TYPE_EXPOSED_TO_HOST;
	dcmd->mbox.b[1] = 0;
	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = MRSAS_MAX_PD * sizeof(struct MR_PD_LIST);
	dcmd->opcode = MR_DCMD_PD_LIST_QUERY;
	dcmd->sgl.sge32[0].phys_addr = pd_list_phys_addr;
	dcmd->sgl.sge32[0].length = MRSAS_MAX_PD * sizeof(struct MR_PD_LIST);

	if (!sc->mask_interrupts)
		retcode = mrsas_issue_blocked_cmd(sc, cmd);
	else
		retcode = mrsas_issue_polled(sc, cmd);

	if (retcode == ETIMEDOUT)
		goto dcmd_timeout;

	/* Get the instance PD list */
	pd_count = MRSAS_MAX_PD;
	pd_addr = pd_list_mem->addr;
	if (pd_list_mem->count < pd_count) {
		memset(sc->local_pd_list, 0,
		    MRSAS_MAX_PD * sizeof(struct mrsas_pd_list));
		for (pd_index = 0; pd_index < pd_list_mem->count; pd_index++) {
			sc->local_pd_list[pd_addr->deviceId].tid = pd_addr->deviceId;
			sc->local_pd_list[pd_addr->deviceId].driveType =
			    pd_addr->scsiDevType;
			sc->local_pd_list[pd_addr->deviceId].driveState =
			    MR_PD_STATE_SYSTEM;
			if (sc->target_list[pd_addr->deviceId].target_id == 0xffff)
				mrsas_add_target(sc, pd_addr->deviceId);
			pd_addr++;
		}
		for (pd_index = 0; pd_index < MRSAS_MAX_PD; pd_index++) {
			if ((sc->local_pd_list[pd_index].driveState !=
				MR_PD_STATE_SYSTEM) &&
				(sc->target_list[pd_index].target_id !=
				0xffff)) {
				mrsas_remove_target(sc, pd_index);
			}
		}
		/*
		 * Use mutext/spinlock if pd_list component size increase more than
		 * 32 bit.
		 */
		memcpy(sc->pd_list, sc->local_pd_list, sizeof(sc->local_pd_list));
		do_ocr = 0;
	}
dcmd_timeout:
	mrsas_free_tmp_dcmd(tcmd);
	free(tcmd, M_MRSAS);

	if (do_ocr)
		sc->do_timedout_reset = MFI_DCMD_TIMEOUT_OCR;

	if (!sc->mask_interrupts)
		mrsas_release_mfi_cmd(cmd);

	return (retcode);
}

/*
 * mrsas_get_ld_list:           Returns FW's LD list structure input:
 * Adapter soft state
 *
 * Issues an internal command (DCMD) to get the FW's controller PD list
 * structure.  This information is mainly used to find out about supported by
 * the FW.
 */
static int
mrsas_get_ld_list(struct mrsas_softc *sc)
{
	int ld_list_size, retcode = 0, ld_index = 0, ids = 0, drv_tgt_id;
	u_int8_t do_ocr = 1;
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_dcmd_frame *dcmd;
	struct MR_LD_LIST *ld_list_mem;
	bus_addr_t ld_list_phys_addr = 0;
	struct mrsas_tmp_dcmd *tcmd;

	cmd = mrsas_get_mfi_cmd(sc);
	if (!cmd) {
		device_printf(sc->mrsas_dev,
		    "Cannot alloc for get LD list cmd\n");
		return 1;
	}
	dcmd = &cmd->frame->dcmd;

	tcmd = malloc(sizeof(struct mrsas_tmp_dcmd), M_MRSAS, M_NOWAIT);
	ld_list_size = sizeof(struct MR_LD_LIST);
	if (mrsas_alloc_tmp_dcmd(sc, tcmd, ld_list_size) != SUCCESS) {
		device_printf(sc->mrsas_dev,
		    "Cannot alloc dmamap for get LD list cmd\n");
		mrsas_release_mfi_cmd(cmd);
		mrsas_free_tmp_dcmd(tcmd);
		free(tcmd, M_MRSAS);
		return (ENOMEM);
	} else {
		ld_list_mem = tcmd->tmp_dcmd_mem;
		ld_list_phys_addr = tcmd->tmp_dcmd_phys_addr;
	}
	memset(dcmd->mbox.b, 0, MFI_MBOX_SIZE);

	if (sc->max256vdSupport)
		dcmd->mbox.b[0] = 1;

	dcmd->cmd = MFI_CMD_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->data_xfer_len = sizeof(struct MR_LD_LIST);
	dcmd->opcode = MR_DCMD_LD_GET_LIST;
	dcmd->sgl.sge32[0].phys_addr = ld_list_phys_addr;
	dcmd->sgl.sge32[0].length = sizeof(struct MR_LD_LIST);
	dcmd->pad_0 = 0;

	if (!sc->mask_interrupts)
		retcode = mrsas_issue_blocked_cmd(sc, cmd);
	else
		retcode = mrsas_issue_polled(sc, cmd);

	if (retcode == ETIMEDOUT)
		goto dcmd_timeout;

#if VD_EXT_DEBUG
	printf("Number of LDs %d\n", ld_list_mem->ldCount);
#endif

	/* Get the instance LD list */
	if (ld_list_mem->ldCount <= sc->fw_supported_vd_count) {
		sc->CurLdCount = ld_list_mem->ldCount;
		memset(sc->ld_ids, 0xff, MAX_LOGICAL_DRIVES_EXT);
		for (ld_index = 0; ld_index < ld_list_mem->ldCount; ld_index++) {
			ids = ld_list_mem->ldList[ld_index].ref.ld_context.targetId;
			drv_tgt_id = ids + MRSAS_MAX_PD;
			if (ld_list_mem->ldList[ld_index].state != 0) {
				sc->ld_ids[ids] = ld_list_mem->ldList[ld_index].ref.ld_context.targetId;
				if (sc->target_list[drv_tgt_id].target_id ==
					0xffff)
					mrsas_add_target(sc, drv_tgt_id);
			} else {
				if (sc->target_list[drv_tgt_id].target_id !=
					0xffff)
					mrsas_remove_target(sc,
						drv_tgt_id);
			}
		}

		do_ocr = 0;
	}
dcmd_timeout:
	mrsas_free_tmp_dcmd(tcmd);
	free(tcmd, M_MRSAS);

	if (do_ocr)
		sc->do_timedout_reset = MFI_DCMD_TIMEOUT_OCR;
	if (!sc->mask_interrupts)
		mrsas_release_mfi_cmd(cmd);

	return (retcode);
}

/*
 * mrsas_alloc_tmp_dcmd:       Allocates memory for temporary command input:
 * Adapter soft state Temp command Size of alloction
 *
 * Allocates DMAable memory for a temporary internal command. The allocated
 * memory is initialized to all zeros upon successful loading of the dma
 * mapped memory.
 */
int
mrsas_alloc_tmp_dcmd(struct mrsas_softc *sc,
    struct mrsas_tmp_dcmd *tcmd, int size)
{
	if (bus_dma_tag_create(sc->mrsas_parent_tag,
	    1, 0,
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR,
	    NULL, NULL,
	    size,
	    1,
	    size,
	    BUS_DMA_ALLOCNOW,
	    NULL, NULL,
	    &tcmd->tmp_dcmd_tag)) {
		device_printf(sc->mrsas_dev, "Cannot allocate tmp dcmd tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(tcmd->tmp_dcmd_tag, (void **)&tcmd->tmp_dcmd_mem,
	    BUS_DMA_NOWAIT, &tcmd->tmp_dcmd_dmamap)) {
		device_printf(sc->mrsas_dev, "Cannot allocate tmp dcmd mem\n");
		return (ENOMEM);
	}
	if (bus_dmamap_load(tcmd->tmp_dcmd_tag, tcmd->tmp_dcmd_dmamap,
	    tcmd->tmp_dcmd_mem, size, mrsas_addr_cb,
	    &tcmd->tmp_dcmd_phys_addr, BUS_DMA_NOWAIT)) {
		device_printf(sc->mrsas_dev, "Cannot load tmp dcmd mem\n");
		return (ENOMEM);
	}
	memset(tcmd->tmp_dcmd_mem, 0, size);
	return (0);
}

/*
 * mrsas_free_tmp_dcmd:      Free memory for temporary command input:
 * temporary dcmd pointer
 *
 * Deallocates memory of the temporary command for use in the construction of
 * the internal DCMD.
 */
void
mrsas_free_tmp_dcmd(struct mrsas_tmp_dcmd *tmp)
{
	if (tmp->tmp_dcmd_phys_addr)
		bus_dmamap_unload(tmp->tmp_dcmd_tag, tmp->tmp_dcmd_dmamap);
	if (tmp->tmp_dcmd_mem != NULL)
		bus_dmamem_free(tmp->tmp_dcmd_tag, tmp->tmp_dcmd_mem, tmp->tmp_dcmd_dmamap);
	if (tmp->tmp_dcmd_tag != NULL)
		bus_dma_tag_destroy(tmp->tmp_dcmd_tag);
}

/*
 * mrsas_issue_blocked_abort_cmd:       Aborts previously issued cmd input:
 * Adapter soft state Previously issued cmd to be aborted
 *
 * This function is used to abort previously issued commands, such as AEN and
 * RAID map sync map commands.  The abort command is sent as a DCMD internal
 * command and subsequently the driver will wait for a return status.  The
 * max wait time is MRSAS_INTERNAL_CMD_WAIT_TIME seconds.
 */
static int
mrsas_issue_blocked_abort_cmd(struct mrsas_softc *sc,
    struct mrsas_mfi_cmd *cmd_to_abort)
{
	struct mrsas_mfi_cmd *cmd;
	struct mrsas_abort_frame *abort_fr;
	u_int8_t retcode = 0;
	unsigned long total_time = 0;
	u_int8_t max_wait = MRSAS_INTERNAL_CMD_WAIT_TIME;

	cmd = mrsas_get_mfi_cmd(sc);
	if (!cmd) {
		device_printf(sc->mrsas_dev, "Cannot alloc for abort cmd\n");
		return (1);
	}
	abort_fr = &cmd->frame->abort;

	/* Prepare and issue the abort frame */
	abort_fr->cmd = MFI_CMD_ABORT;
	abort_fr->cmd_status = 0xFF;
	abort_fr->flags = 0;
	abort_fr->abort_context = cmd_to_abort->index;
	abort_fr->abort_mfi_phys_addr_lo = cmd_to_abort->frame_phys_addr;
	abort_fr->abort_mfi_phys_addr_hi = 0;

	cmd->sync_cmd = 1;
	cmd->cmd_status = 0xFF;

	if (mrsas_issue_dcmd(sc, cmd)) {
		device_printf(sc->mrsas_dev, "Fail to send abort command.\n");
		return (1);
	}
	/* Wait for this cmd to complete */
	sc->chan = (void *)&cmd;
	while (1) {
		if (cmd->cmd_status == 0xFF) {
			tsleep((void *)&sc->chan, 0, "mrsas_sleep", hz);
		} else
			break;
		total_time++;
		if (total_time >= max_wait) {
			device_printf(sc->mrsas_dev, "Abort cmd timed out after %d sec.\n", max_wait);
			retcode = 1;
			break;
		}
	}

	cmd->sync_cmd = 0;
	mrsas_release_mfi_cmd(cmd);
	return (retcode);
}

/*
 * mrsas_complete_abort:      Completes aborting a command input:
 * Adapter soft state Cmd that was issued to abort another cmd
 *
 * The mrsas_issue_blocked_abort_cmd() function waits for the command status to
 * change after sending the command.  This function is called from
 * mrsas_complete_mptmfi_passthru() to wake up the sleep thread associated.
 */
void
mrsas_complete_abort(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd)
{
	if (cmd->sync_cmd) {
		cmd->sync_cmd = 0;
		cmd->cmd_status = 0;
		sc->chan = (void *)&cmd;
		wakeup_one((void *)&sc->chan);
	}
	return;
}

/*
 * mrsas_aen_handler:	AEN processing callback function from thread context
 * input:				Adapter soft state
 *
 * Asynchronous event handler
 */
void
mrsas_aen_handler(struct mrsas_softc *sc)
{
	union mrsas_evt_class_locale class_locale;
	int doscan = 0;
	u_int32_t seq_num;
 	int error, fail_aen = 0;

	if (sc == NULL) {
		printf("invalid instance!\n");
		return;
	}
	if (sc->remove_in_progress || sc->reset_in_progress) {
		device_printf(sc->mrsas_dev, "Returning from %s, line no %d\n",
			__func__, __LINE__);
		return;
	}
	if (sc->evt_detail_mem) {
		switch (sc->evt_detail_mem->code) {
		case MR_EVT_PD_INSERTED:
			fail_aen = mrsas_get_pd_list(sc);
			if (!fail_aen)
				mrsas_bus_scan_sim(sc, sc->sim_1);
			else
				goto skip_register_aen;
			break;
		case MR_EVT_PD_REMOVED:
			fail_aen = mrsas_get_pd_list(sc);
			if (!fail_aen)
				mrsas_bus_scan_sim(sc, sc->sim_1);
			else
				goto skip_register_aen;
			break;
		case MR_EVT_LD_OFFLINE:
		case MR_EVT_CFG_CLEARED:
		case MR_EVT_LD_DELETED:
			mrsas_bus_scan_sim(sc, sc->sim_0);
			break;
		case MR_EVT_LD_CREATED:
			fail_aen = mrsas_get_ld_list(sc);
			if (!fail_aen)
				mrsas_bus_scan_sim(sc, sc->sim_0);
			else
				goto skip_register_aen;
			break;
		case MR_EVT_CTRL_HOST_BUS_SCAN_REQUESTED:
		case MR_EVT_FOREIGN_CFG_IMPORTED:
		case MR_EVT_LD_STATE_CHANGE:
			doscan = 1;
			break;
		case MR_EVT_CTRL_PROP_CHANGED:
			fail_aen = mrsas_get_ctrl_info(sc);
			if (fail_aen)
				goto skip_register_aen;
			break;
		default:
			break;
		}
	} else {
		device_printf(sc->mrsas_dev, "invalid evt_detail\n");
		return;
	}
	if (doscan) {
		fail_aen = mrsas_get_pd_list(sc);
		if (!fail_aen) {
			mrsas_dprint(sc, MRSAS_AEN, "scanning ...sim 1\n");
			mrsas_bus_scan_sim(sc, sc->sim_1);
		} else
			goto skip_register_aen;

		fail_aen = mrsas_get_ld_list(sc);
		if (!fail_aen) {
			mrsas_dprint(sc, MRSAS_AEN, "scanning ...sim 0\n");
			mrsas_bus_scan_sim(sc, sc->sim_0);
		} else
			goto skip_register_aen;
	}
	seq_num = sc->evt_detail_mem->seq_num + 1;

	/* Register AEN with FW for latest sequence number plus 1 */
	class_locale.members.reserved = 0;
	class_locale.members.locale = MR_EVT_LOCALE_ALL;
	class_locale.members.class = MR_EVT_CLASS_DEBUG;

	if (sc->aen_cmd != NULL)
		return;

	mtx_lock(&sc->aen_lock);
	error = mrsas_register_aen(sc, seq_num,
	    class_locale.word);
	mtx_unlock(&sc->aen_lock);

	if (error)
		device_printf(sc->mrsas_dev, "register aen failed error %x\n", error);

skip_register_aen:
	return;

}


/*
 * mrsas_complete_aen:	Completes AEN command
 * input:				Adapter soft state
 * 						Cmd that was issued to abort another cmd
 *
 * This function will be called from ISR and will continue event processing from
 * thread context by enqueuing task in ev_tq (callback function
 * "mrsas_aen_handler").
 */
void
mrsas_complete_aen(struct mrsas_softc *sc, struct mrsas_mfi_cmd *cmd)
{
	/*
	 * Don't signal app if it is just an aborted previously registered
	 * aen
	 */
	if ((!cmd->abort_aen) && (sc->remove_in_progress == 0)) {
		sc->mrsas_aen_triggered = 1;
		mtx_lock(&sc->aen_lock);
		if (sc->mrsas_poll_waiting) {
			sc->mrsas_poll_waiting = 0;
			selwakeup(&sc->mrsas_select);
		}
		mtx_unlock(&sc->aen_lock);
	} else
		cmd->abort_aen = 0;

	sc->aen_cmd = NULL;
	mrsas_release_mfi_cmd(cmd);

	taskqueue_enqueue(sc->ev_tq, &sc->ev_task);

	return;
}

static device_method_t mrsas_methods[] = {
	DEVMETHOD(device_probe, mrsas_probe),
	DEVMETHOD(device_attach, mrsas_attach),
	DEVMETHOD(device_detach, mrsas_detach),
	DEVMETHOD(device_suspend, mrsas_suspend),
	DEVMETHOD(device_resume, mrsas_resume),
	DEVMETHOD(bus_print_child, bus_generic_print_child),
	DEVMETHOD(bus_driver_added, bus_generic_driver_added),
	{0, 0}
};

static driver_t mrsas_driver = {
	"mrsas",
	mrsas_methods,
	sizeof(struct mrsas_softc)
};

static devclass_t mrsas_devclass;

DRIVER_MODULE(mrsas, pci, mrsas_driver, mrsas_devclass, 0, 0);
MODULE_DEPEND(mrsas, cam, 1, 1, 1);

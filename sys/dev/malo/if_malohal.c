/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Marvell Semiconductor, Inc.
 * Copyright (c) 2007 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Weongyo Jeong <weongyo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
#ifdef __FreeBSD__
__FBSDID("$FreeBSD$");
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/firmware.h>
#include <sys/socket.h>

#include <machine/bus.h>
#include <sys/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>

#include <dev/malo/if_malo.h>

#define MALO_WAITOK				1
#define MALO_NOWAIT				0

#define	_CMD_SETUP(pCmd, _type, _cmd) do {				\
	pCmd = (_type *)&mh->mh_cmdbuf[0];				\
	memset(pCmd, 0, sizeof(_type));					\
	pCmd->cmdhdr.cmd = htole16(_cmd);				\
	pCmd->cmdhdr.length = htole16(sizeof(_type));			\
} while (0)

static __inline uint32_t
malo_hal_read4(struct malo_hal *mh, bus_size_t off)
{
	return bus_space_read_4(mh->mh_iot, mh->mh_ioh, off);
}

static __inline void
malo_hal_write4(struct malo_hal *mh, bus_size_t off, uint32_t val)
{
	bus_space_write_4(mh->mh_iot, mh->mh_ioh, off, val);
}

static void
malo_hal_load_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	bus_addr_t *paddr = (bus_addr_t*) arg;

	KASSERT(error == 0, ("error %u on bus_dma callback", error));
	*paddr = segs->ds_addr;
}

/*
 * Setup for communication with the device.  We allocate
 * a command buffer and map it for bus dma use.  The pci
 * device id is used to identify whether the device has
 * SRAM on it (in which case f/w download must include a
 * memory controller reset).  All bus i/o operations happen
 * in BAR 1; the driver passes in the tag and handle we need.
 */
struct malo_hal *
malo_hal_attach(device_t dev, uint16_t devid,
    bus_space_handle_t ioh, bus_space_tag_t iot, bus_dma_tag_t tag)
{
	int error;
	struct malo_hal *mh;

	mh = malloc(sizeof(struct malo_hal), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mh == NULL)
		return NULL;

	mh->mh_dev = dev;
	mh->mh_ioh = ioh;
	mh->mh_iot = iot;

	snprintf(mh->mh_mtxname, sizeof(mh->mh_mtxname),
	    "%s_hal", device_get_nameunit(dev));
	mtx_init(&mh->mh_mtx, mh->mh_mtxname, NULL, MTX_DEF);

	/*
	 * Allocate the command buffer and map into the address
	 * space of the h/w.  We request "coherent" memory which
	 * will be uncached on some architectures.
	 */
	error = bus_dma_tag_create(tag,		/* parent */
		       PAGE_SIZE, 0,		/* alignment, bounds */
		       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		       BUS_SPACE_MAXADDR,	/* highaddr */
		       NULL, NULL,		/* filter, filterarg */
		       MALO_CMDBUF_SIZE,	/* maxsize */
		       1,			/* nsegments */
		       MALO_CMDBUF_SIZE,	/* maxsegsize */
		       BUS_DMA_ALLOCNOW,	/* flags */
		       NULL,			/* lockfunc */
		       NULL,			/* lockarg */
		       &mh->mh_dmat);
	if (error != 0) {
		device_printf(dev, "unable to allocate memory for cmd tag, "
			"error %u\n", error);
		goto fail;
	}

	/* allocate descriptors */
	error = bus_dmamem_alloc(mh->mh_dmat, (void**) &mh->mh_cmdbuf,
				 BUS_DMA_NOWAIT | BUS_DMA_COHERENT, 
				 &mh->mh_dmamap);
	if (error != 0) {
		device_printf(dev, "unable to allocate memory for cmd buffer, "
			"error %u\n", error);
		goto fail;
	}

	error = bus_dmamap_load(mh->mh_dmat, mh->mh_dmamap,
				mh->mh_cmdbuf, MALO_CMDBUF_SIZE,
				malo_hal_load_cb, &mh->mh_cmdaddr,
				BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(dev, "unable to load cmd buffer, error %u\n",
			error);
		goto fail;
	}

	return (mh);

fail:
	if (mh->mh_cmdbuf != NULL)
		bus_dmamem_free(mh->mh_dmat, mh->mh_cmdbuf,
		    mh->mh_dmamap);
	if (mh->mh_dmat)
		bus_dma_tag_destroy(mh->mh_dmat);
	free(mh, M_DEVBUF);

	return (NULL);
}

/*
 * Low level firmware cmd block handshake support.
 */

static void
malo_hal_send_cmd(struct malo_hal *mh)
{
	uint32_t dummy;

	bus_dmamap_sync(mh->mh_dmat, mh->mh_dmamap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	malo_hal_write4(mh, MALO_REG_GEN_PTR, mh->mh_cmdaddr);
	dummy = malo_hal_read4(mh, MALO_REG_INT_CODE);

	malo_hal_write4(mh, MALO_REG_H2A_INTERRUPT_EVENTS,
	    MALO_H2ARIC_BIT_DOOR_BELL);
}

static int
malo_hal_waitforcmd(struct malo_hal *mh, uint16_t cmd)
{
#define MAX_WAIT_FW_COMPLETE_ITERATIONS 10000
	int i;

	for (i = 0; i < MAX_WAIT_FW_COMPLETE_ITERATIONS; i++) {
		if (mh->mh_cmdbuf[0] == le16toh(cmd))
			return 1;

		DELAY(1 * 1000);
	}

	return 0;
#undef MAX_WAIT_FW_COMPLETE_ITERATIONS
}

static int
malo_hal_execute_cmd(struct malo_hal *mh, unsigned short cmd)
{
	MALO_HAL_LOCK_ASSERT(mh);

	if ((mh->mh_flags & MHF_FWHANG) &&
	    (mh->mh_debug & MALO_HAL_DEBUG_IGNHANG) == 0) {
		device_printf(mh->mh_dev, "firmware hung, skipping cmd 0x%x\n",
			cmd);
		return ENXIO;
	}

	if (malo_hal_read4(mh, MALO_REG_INT_CODE) == 0xffffffff) {
		device_printf(mh->mh_dev, "%s: device not present!\n",
		    __func__);
		return EIO;
	}

	malo_hal_send_cmd(mh);
	if (!malo_hal_waitforcmd(mh, cmd | 0x8000)) {
		device_printf(mh->mh_dev,
		    "timeout waiting for f/w cmd 0x%x\n", cmd);
		mh->mh_flags |= MHF_FWHANG;
		return ETIMEDOUT;
	}

	bus_dmamap_sync(mh->mh_dmat, mh->mh_dmamap,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	return 0;
}

static int
malo_hal_get_cal_table(struct malo_hal *mh, uint8_t annex, uint8_t index)
{
	struct malo_cmd_caltable *cmd;
	int ret;

	MALO_HAL_LOCK_ASSERT(mh);

	_CMD_SETUP(cmd, struct malo_cmd_caltable, MALO_HOSTCMD_GET_CALTABLE);
	cmd->annex = annex;
	cmd->index = index;

	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_GET_CALTABLE);
	if (ret == 0 && cmd->caltbl[0] != annex && annex != 0 && annex != 255)
		ret = EIO;
	return ret;
}							  

static int
malo_hal_get_pwrcal_table(struct malo_hal *mh, struct malo_hal_caldata *cal)
{
	const uint8_t *data;
	int len;

	MALO_HAL_LOCK(mh);
	/* NB: we hold the lock so it's ok to use cmdbuf */
	data = ((const struct malo_cmd_caltable *) mh->mh_cmdbuf)->caltbl;
	if (malo_hal_get_cal_table(mh, 33, 0) == 0) {
		len = (data[2] | (data[3] << 8)) - 12;
		/* XXX validate len */
		memcpy(cal->pt_ratetable_20m, &data[12], len);	
	}
	mh->mh_flags |= MHF_CALDATA;
	MALO_HAL_UNLOCK(mh);

	return 0;
}

/*
 * Reset internal state after a firmware download.
 */
static int
malo_hal_resetstate(struct malo_hal *mh)
{
	/*
	 * Fetch cal data for later use.
	 * XXX may want to fetch other stuff too.
	 */
	if ((mh->mh_flags & MHF_CALDATA) == 0)
		malo_hal_get_pwrcal_table(mh, &mh->mh_caldata);
	return 0;
}

static void
malo_hal_fw_reset(struct malo_hal *mh)
{

	if (malo_hal_read4(mh,  MALO_REG_INT_CODE) == 0xffffffff) {
		device_printf(mh->mh_dev, "%s: device not present!\n",
		    __func__);
		return;
	}

	malo_hal_write4(mh, MALO_REG_H2A_INTERRUPT_EVENTS, MALO_ISR_RESET);
	mh->mh_flags &= ~MHF_FWHANG;
}

static void
malo_hal_trigger_pcicmd(struct malo_hal *mh)
{
	uint32_t dummy;

	bus_dmamap_sync(mh->mh_dmat, mh->mh_dmamap, BUS_DMASYNC_PREWRITE);

	malo_hal_write4(mh, MALO_REG_GEN_PTR, mh->mh_cmdaddr);
	dummy = malo_hal_read4(mh, MALO_REG_INT_CODE);

	malo_hal_write4(mh, MALO_REG_INT_CODE, 0x00);
	dummy = malo_hal_read4(mh, MALO_REG_INT_CODE);

	malo_hal_write4(mh, MALO_REG_H2A_INTERRUPT_EVENTS,
	    MALO_H2ARIC_BIT_DOOR_BELL);
	dummy = malo_hal_read4(mh, MALO_REG_INT_CODE);
}

static int
malo_hal_waitfor(struct malo_hal *mh, uint32_t val)
{
	int i;

	for (i = 0; i < MALO_FW_MAX_NUM_CHECKS; i++) {
		DELAY(MALO_FW_CHECK_USECS);
		if (malo_hal_read4(mh, MALO_REG_INT_CODE) == val)
			return 0;
	}

	return -1;
}

/*
 * Firmware block xmit when talking to the boot-rom.
 */
static int
malo_hal_send_helper(struct malo_hal *mh, int bsize,
    const void *data, size_t dsize, int waitfor)
{
	mh->mh_cmdbuf[0] = htole16(MALO_HOSTCMD_CODE_DNLD);
	mh->mh_cmdbuf[1] = htole16(bsize);
	memcpy(&mh->mh_cmdbuf[4], data , dsize);

	malo_hal_trigger_pcicmd(mh);

	if (waitfor == MALO_NOWAIT)
		goto pass;

	/* XXX 2000 vs 200 */
	if (malo_hal_waitfor(mh, MALO_INT_CODE_CMD_FINISHED) != 0) {
		device_printf(mh->mh_dev,
		    "%s: timeout waiting for CMD_FINISHED, INT_CODE 0x%x\n",
		    __func__, malo_hal_read4(mh, MALO_REG_INT_CODE));
		
		return ETIMEDOUT;
	}

pass:
	malo_hal_write4(mh, MALO_REG_INT_CODE, 0);

	return (0);
}

static int
malo_hal_fwload_helper(struct malo_hal *mh, char *helper)
{
	const struct firmware *fw;
	int error;

	fw = firmware_get(helper);
	if (fw == NULL) {
		device_printf(mh->mh_dev, "could not read microcode %s!\n",
		    helper);
		return (EIO);
	}

	device_printf(mh->mh_dev, "load %s firmware image (%zu bytes)\n",
	    helper, fw->datasize);

	error = malo_hal_send_helper(mh, fw->datasize, fw->data, fw->datasize,
		MALO_WAITOK);
	if (error != 0)
		goto fail;

	/* tell the card we're done and... */
	error = malo_hal_send_helper(mh, 0, NULL, 0, MALO_NOWAIT);

fail:
	firmware_put(fw, FIRMWARE_UNLOAD);

	return (error);
}

/*
 * Firmware block xmit when talking to the 1st-stage loader.
 */
static int
malo_hal_send_main(struct malo_hal *mh, const void *data, size_t dsize,
    uint16_t seqnum, int waitfor)
{
	mh->mh_cmdbuf[0] = htole16(MALO_HOSTCMD_CODE_DNLD);
	mh->mh_cmdbuf[1] = htole16(dsize);
	mh->mh_cmdbuf[2] = htole16(seqnum);
	mh->mh_cmdbuf[3] = 0;
	memcpy(&mh->mh_cmdbuf[4], data, dsize);

	malo_hal_trigger_pcicmd(mh);

	if (waitfor == MALO_NOWAIT)
		goto pass;

	if (malo_hal_waitfor(mh, MALO_INT_CODE_CMD_FINISHED) != 0) {
		device_printf(mh->mh_dev,
		    "%s: timeout waiting for CMD_FINISHED, INT_CODE 0x%x\n",
		    __func__, malo_hal_read4(mh, MALO_REG_INT_CODE));

		return ETIMEDOUT;
	}

pass:
	malo_hal_write4(mh, MALO_REG_INT_CODE, 0);

	return 0;
}

static int
malo_hal_fwload_main(struct malo_hal *mh, char *firmware)
{
	const struct firmware *fw;
	const uint8_t *fp;
	int error;
	size_t count;
	uint16_t seqnum;
	uint32_t blocksize;

	error = 0;

	fw = firmware_get(firmware);
	if (fw == NULL) {
		device_printf(mh->mh_dev, "could not read firmware %s!\n",
		    firmware);
		return (EIO);
	}

	device_printf(mh->mh_dev, "load %s firmware image (%zu bytes)\n",
	    firmware, fw->datasize);

	seqnum = 1;
	for (count = 0; count < fw->datasize; count += blocksize) {
		blocksize = MIN(256, fw->datasize - count);
		fp = (const uint8_t *)fw->data + count;

		error = malo_hal_send_main(mh, fp, blocksize, seqnum++,
		    MALO_NOWAIT);
		if (error != 0)
			goto fail;
		DELAY(500);
	}
	
	/*
	 * send a command with size 0 to tell that the firmware has been
	 * uploaded
	 */
	error = malo_hal_send_main(mh, NULL, 0, seqnum++, MALO_NOWAIT);
	DELAY(100);

fail:
	firmware_put(fw, FIRMWARE_UNLOAD);

	return (error);
}

int
malo_hal_fwload(struct malo_hal *mh, char *helper, char *firmware)
{
	int error, i;
	uint32_t fwreadysig, opmode;

	/*
	 * NB: now malo(4) supports only STA mode.  It will be better if it
	 * supports AP mode.
	 */
	fwreadysig = MALO_HOSTCMD_STA_FWRDY_SIGNATURE;
	opmode = MALO_HOSTCMD_STA_MODE;

	malo_hal_fw_reset(mh);

	malo_hal_write4(mh, MALO_REG_A2H_INTERRUPT_CLEAR_SEL,
	    MALO_A2HRIC_BIT_MASK);
	malo_hal_write4(mh, MALO_REG_A2H_INTERRUPT_CAUSE, 0x00);
	malo_hal_write4(mh, MALO_REG_A2H_INTERRUPT_MASK, 0x00);
	malo_hal_write4(mh, MALO_REG_A2H_INTERRUPT_STATUS_MASK,
	    MALO_A2HRIC_BIT_MASK);

	error = malo_hal_fwload_helper(mh, helper);
	if (error != 0) {
		device_printf(mh->mh_dev, "failed to load bootrom loader.\n");
		goto fail;
	}

	DELAY(200 * MALO_FW_CHECK_USECS);

	error = malo_hal_fwload_main(mh, firmware);
	if (error != 0) {
		device_printf(mh->mh_dev, "failed to load firmware.\n");
		goto fail;
	}

	/*
	 * Wait for firmware to startup; we monitor the INT_CODE register
	 * waiting for a signature to written back indicating it's ready to go.
	 */
	mh->mh_cmdbuf[1] = 0;

	if (opmode != MALO_HOSTCMD_STA_MODE)
		malo_hal_trigger_pcicmd(mh);
	
	for (i = 0; i < MALO_FW_MAX_NUM_CHECKS; i++) {
		malo_hal_write4(mh, MALO_REG_GEN_PTR, opmode);
		DELAY(MALO_FW_CHECK_USECS);
		if (malo_hal_read4(mh, MALO_REG_INT_CODE) == fwreadysig) {
			malo_hal_write4(mh, MALO_REG_INT_CODE, 0x00);
			return malo_hal_resetstate(mh);
		}
	}

	return ETIMEDOUT;
fail:
	malo_hal_fw_reset(mh);

	return (error);
}

/*
 * Return "hw specs".  Note this must be the first cmd MUST be done after
 * a firmware download or the f/w will lockup.
 */
int
malo_hal_gethwspecs(struct malo_hal *mh, struct malo_hal_hwspec *hw)
{
	struct malo_cmd_get_hwspec *cmd;
	int ret;

	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_get_hwspec, MALO_HOSTCMD_GET_HW_SPEC);
	memset(&cmd->permaddr[0], 0xff, IEEE80211_ADDR_LEN);
	cmd->ul_fw_awakecookie = htole32((unsigned int)mh->mh_cmdaddr + 2048);

	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_GET_HW_SPEC);
	if (ret == 0) {
		IEEE80211_ADDR_COPY(hw->macaddr, cmd->permaddr);
		hw->wcbbase[0] = le32toh(cmd->wcbbase0) & 0x0000ffff;
		hw->wcbbase[1] = le32toh(cmd->wcbbase1) & 0x0000ffff;
		hw->wcbbase[2] = le32toh(cmd->wcbbase2) & 0x0000ffff;
		hw->wcbbase[3] = le32toh(cmd->wcbbase3) & 0x0000ffff;
		hw->rxdesc_read = le32toh(cmd->rxpdrd_ptr)& 0x0000ffff;
		hw->rxdesc_write = le32toh(cmd->rxpdwr_ptr)& 0x0000ffff;
		hw->regioncode = le16toh(cmd->regioncode) & 0x00ff;
		hw->fw_releasenum = le32toh(cmd->fw_releasenum);
		hw->maxnum_wcb = le16toh(cmd->num_wcb);
		hw->maxnum_mcaddr = le16toh(cmd->num_mcastaddr);
		hw->num_antenna = le16toh(cmd->num_antenna);
		hw->hwversion = cmd->version;
		hw->hostinterface = cmd->hostif;
	}

	MALO_HAL_UNLOCK(mh);

	return ret;
}

void
malo_hal_detach(struct malo_hal *mh)
{

	bus_dmamem_free(mh->mh_dmat, mh->mh_cmdbuf, mh->mh_dmamap);
	bus_dma_tag_destroy(mh->mh_dmat);
	mtx_destroy(&mh->mh_mtx);
	free(mh, M_DEVBUF);
}

/*
 * Configure antenna use.  Takes effect immediately.
 *
 * XXX tx antenna setting ignored
 * XXX rx antenna setting should always be 3 (for now)
 */
int
malo_hal_setantenna(struct malo_hal *mh, enum malo_hal_antenna dirset, int ant)
{
	struct malo_cmd_rf_antenna *cmd;
	int ret;

	if (!(dirset == MHA_ANTENNATYPE_RX || dirset == MHA_ANTENNATYPE_TX))
		return EINVAL;

	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_rf_antenna,
	    MALO_HOSTCMD_802_11_RF_ANTENNA);
	cmd->action = htole16(dirset);
	if (ant == 0) {			/* default to all/both antennae */
		/* XXX never reach now.  */
		ant = 3;
	}
	cmd->mode = htole16(ant);

	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_802_11_RF_ANTENNA);

	MALO_HAL_UNLOCK(mh);

	return ret;
}

/*
 * Configure radio.  Takes effect immediately.
 *
 * XXX preamble installed after set fixed rate cmd
 */
int
malo_hal_setradio(struct malo_hal *mh, int onoff,
    enum malo_hal_preamble preamble)
{
	struct malo_cmd_radio_control *cmd;
	int ret;

	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_radio_control,
	    MALO_HOSTCMD_802_11_RADIO_CONTROL);
	cmd->action = htole16(MALO_HOSTCMD_ACT_GEN_SET);
	if (onoff == 0)
		cmd->control = 0;
	else
		cmd->control = htole16(preamble);
	cmd->radio_on = htole16(onoff);

	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_802_11_RADIO_CONTROL);

	MALO_HAL_UNLOCK(mh);

	return ret;
}

/*
 * Set the interrupt mask.
 */
void
malo_hal_intrset(struct malo_hal *mh, uint32_t mask)
{

	malo_hal_write4(mh, MALO_REG_A2H_INTERRUPT_MASK, 0);
	(void)malo_hal_read4(mh, MALO_REG_INT_CODE);

	mh->mh_imask = mask;
	malo_hal_write4(mh, MALO_REG_A2H_INTERRUPT_MASK, mask);
	(void)malo_hal_read4(mh, MALO_REG_INT_CODE);
}

int
malo_hal_setchannel(struct malo_hal *mh, const struct malo_hal_channel *chan)
{
	struct malo_cmd_fw_set_rf_channel *cmd;
	int ret;

	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_fw_set_rf_channel,
	    MALO_HOSTCMD_SET_RF_CHANNEL);
	cmd->action = htole16(MALO_HOSTCMD_ACT_GEN_SET);
	cmd->cur_channel = chan->channel;

	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_SET_RF_CHANNEL);

	MALO_HAL_UNLOCK(mh);

	return ret;
}

int
malo_hal_settxpower(struct malo_hal *mh, const struct malo_hal_channel *c)
{
	struct malo_cmd_rf_tx_power *cmd;
	const struct malo_hal_caldata *cal = &mh->mh_caldata;
	uint8_t chan = c->channel;
	uint16_t pow;
	int i, idx, ret;
	
	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_rf_tx_power,
	    MALO_HOSTCMD_802_11_RF_TX_POWER);
	cmd->action = htole16(MALO_HOSTCMD_ACT_GEN_SET_LIST);
	for (i = 0; i < 4; i++) {
		idx = (chan - 1) * 4 + i;
		pow = cal->pt_ratetable_20m[idx];
		cmd->power_levellist[i] = htole16(pow);
	}
	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_802_11_RF_TX_POWER);

	MALO_HAL_UNLOCK(mh);

	return ret;
}

int
malo_hal_setpromisc(struct malo_hal *mh, int enable)
{
	/* XXX need host cmd */
	return 0;
}

int
malo_hal_setassocid(struct malo_hal *mh,
    const uint8_t bssid[IEEE80211_ADDR_LEN], uint16_t associd)
{
	struct malo_cmd_fw_set_aid *cmd;
	int ret;

	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_fw_set_aid,
	    MALO_HOSTCMD_SET_AID);
	cmd->cmdhdr.seqnum = 1;
	cmd->associd = htole16(associd);
	IEEE80211_ADDR_COPY(&cmd->macaddr[0], bssid);
	
	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_SET_AID);
	MALO_HAL_UNLOCK(mh);
	return ret;
}

/*
 * Kick the firmware to tell it there are new tx descriptors
 * for processing.  The driver says what h/w q has work in
 * case the f/w ever gets smarter.
 */
void
malo_hal_txstart(struct malo_hal *mh, int qnum)
{
	bus_space_write_4(mh->mh_iot, mh->mh_ioh,
	    MALO_REG_H2A_INTERRUPT_EVENTS, MALO_H2ARIC_BIT_PPA_READY);
	(void) bus_space_read_4(mh->mh_iot, mh->mh_ioh, MALO_REG_INT_CODE);
}

/*
 * Return the current ISR setting and clear the cause.
 */
void
malo_hal_getisr(struct malo_hal *mh, uint32_t *status)
{
	uint32_t cause;

	cause = bus_space_read_4(mh->mh_iot, mh->mh_ioh,
	    MALO_REG_A2H_INTERRUPT_CAUSE);
	if (cause == 0xffffffff) {	/* card removed */
		cause = 0;
	} else if (cause != 0) {
		/* clear cause bits */
		bus_space_write_4(mh->mh_iot, mh->mh_ioh,
		    MALO_REG_A2H_INTERRUPT_CAUSE, cause &~ mh->mh_imask);
		(void) bus_space_read_4(mh->mh_iot, mh->mh_ioh,
		    MALO_REG_INT_CODE);
		cause &= mh->mh_imask;
	}

	*status = cause;
}

/*
 * Callback from the driver on a cmd done interrupt.  Nothing to do right
 * now as we spin waiting for cmd completion.
 */
void
malo_hal_cmddone(struct malo_hal *mh)
{
	/* NB : do nothing.  */
}

int
malo_hal_prescan(struct malo_hal *mh)
{
	struct malo_cmd_prescan *cmd;
	int ret;

	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_prescan, MALO_HOSTCMD_SET_PRE_SCAN);
	cmd->cmdhdr.seqnum = 1;
	
	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_SET_PRE_SCAN);

	MALO_HAL_UNLOCK(mh);

	return ret;
}

int
malo_hal_postscan(struct malo_hal *mh, uint8_t *macaddr, uint8_t ibsson)
{
	struct malo_cmd_postscan *cmd;
	int ret;

	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_postscan, MALO_HOSTCMD_SET_POST_SCAN);
	cmd->cmdhdr.seqnum = 1;
	cmd->isibss = htole32(ibsson);
	IEEE80211_ADDR_COPY(&cmd->bssid[0], macaddr);

	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_SET_POST_SCAN);

	MALO_HAL_UNLOCK(mh);

	return ret;
}

int
malo_hal_set_slot(struct malo_hal *mh, int is_short)
{
	int ret;
	struct malo_cmd_fw_setslot *cmd;

	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_fw_setslot, MALO_HOSTCMD_SET_SLOT);
	cmd->action = htole16(MALO_HOSTCMD_ACT_GEN_SET);
	cmd->slot = (is_short == 1 ? 1 : 0);

	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_SET_SLOT);

	MALO_HAL_UNLOCK(mh);

	return ret;
}

int
malo_hal_set_rate(struct malo_hal *mh, uint16_t curmode, uint8_t rate)
{
	int i, ret;
	struct malo_cmd_set_rate *cmd;

	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_set_rate, MALO_HOSTCMD_SET_RATE);
	cmd->aprates[0] = 2;
	cmd->aprates[1] = 4;
	cmd->aprates[2] = 11;
	cmd->aprates[3] = 22;
	if (curmode == IEEE80211_MODE_11G) {
		cmd->aprates[4] = 0;		/* XXX reserved?  */
		cmd->aprates[5] = 12;
		cmd->aprates[6] = 18;
		cmd->aprates[7] = 24;
		cmd->aprates[8] = 36;
		cmd->aprates[9] = 48;
		cmd->aprates[10] = 72;
		cmd->aprates[11] = 96;
		cmd->aprates[12] = 108;
	}

	if (rate != 0) {
		/* fixed rate */
		for (i = 0; i < 13; i++) {
			if (cmd->aprates[i] == rate) {
				cmd->rateindex = i;
				cmd->dataratetype = 1;
				break;
			}
		}
	}

	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_SET_RATE);

	MALO_HAL_UNLOCK(mh);

	return ret;
}

int
malo_hal_setmcast(struct malo_hal *mh, int nmc, const uint8_t macs[])
{
	struct malo_cmd_mcast *cmd;
	int ret;

	if (nmc > MALO_HAL_MCAST_MAX)
		return EINVAL;

	MALO_HAL_LOCK(mh);

	_CMD_SETUP(cmd, struct malo_cmd_mcast, MALO_HOSTCMD_MAC_MULTICAST_ADR);
	memcpy(cmd->maclist, macs, nmc * IEEE80211_ADDR_LEN);
	cmd->numaddr = htole16(nmc);
	cmd->action = htole16(0xffff);

	ret = malo_hal_execute_cmd(mh, MALO_HOSTCMD_MAC_MULTICAST_ADR);

	MALO_HAL_UNLOCK(mh);

	return ret;
}

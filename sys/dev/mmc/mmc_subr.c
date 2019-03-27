/*-
 * Copyright (c) 2006 Bernd Walter.  All rights reserved.
 * Copyright (c) 2006 M. Warner Losh.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Portions of this software may have been developed with reference to
 * the SD Simplified Specification.  The following disclaimer may apply:
 *
 * The following conditions apply to the release of the simplified
 * specification ("Simplified Specification") by the SD Card Association and
 * the SD Group. The Simplified Specification is a subset of the complete SD
 * Specification which is owned by the SD Card Association and the SD
 * Group. This Simplified Specification is provided on a non-confidential
 * basis subject to the disclaimers below. Any implementation of the
 * Simplified Specification may require a license from the SD Card
 * Association, SD Group, SD-3C LLC or other third parties.
 *
 * Disclaimers:
 *
 * The information contained in the Simplified Specification is presented only
 * as a standard specification for SD Cards and SD Host/Ancillary products and
 * is provided "AS-IS" without any representations or warranties of any
 * kind. No responsibility is assumed by the SD Group, SD-3C LLC or the SD
 * Card Association for any damages, any infringements of patents or other
 * right of the SD Group, SD-3C LLC, the SD Card Association or any third
 * parties, which may result from its use. No license is granted by
 * implication, estoppel or otherwise under any patent or other rights of the
 * SD Group, SD-3C LLC, the SD Card Association or any third party. Nothing
 * herein shall be construed as an obligation by the SD Group, the SD-3C LLC
 * or the SD Card Association to disclose or distribute any technical
 * information, know-how or other confidential information to any third party.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/time.h>

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmc_private.h>
#include <dev/mmc/mmc_subr.h>
#include <dev/mmc/mmcreg.h>
#include <dev/mmc/mmcbrvar.h>

#include "mmcbus_if.h"

#define	CMD_RETRIES	3
#define	LOG_PPS		5 /* Log no more than 5 errors per second. */

int
mmc_wait_for_cmd(device_t busdev, device_t dev, struct mmc_command *cmd,
    int retries)
{
	struct mmc_request mreq;
	struct mmc_softc *sc;
	int err;

	do {
		memset(&mreq, 0, sizeof(mreq));
		memset(cmd->resp, 0, sizeof(cmd->resp));
		cmd->retries = 0; /* Retries done here, not in hardware. */
		cmd->mrq = &mreq;
		if (cmd->data != NULL)
			cmd->data->mrq = &mreq;
		mreq.cmd = cmd;
		if (MMCBUS_WAIT_FOR_REQUEST(busdev, dev, &mreq) != 0)
			err = MMC_ERR_FAILED;
		else
			err = cmd->error;
	} while (err != MMC_ERR_NONE && retries-- > 0);

	if (err != MMC_ERR_NONE && busdev == dev) {
		sc = device_get_softc(busdev);
		if (sc->squelched == 0 && ppsratecheck(&sc->log_time,
		    &sc->log_count, LOG_PPS)) {
			device_printf(sc->dev, "CMD%d failed, RESULT: %d\n",
			    cmd->opcode, err);
		}
	}

	return (err);
}

int
mmc_wait_for_app_cmd(device_t busdev, device_t dev, uint16_t rca,
    struct mmc_command *cmd, int retries)
{
	struct mmc_command appcmd;
	struct mmc_softc *sc;
	int err;

	sc = device_get_softc(busdev);

	/* Squelch error reporting at lower levels, we report below. */
	sc->squelched++;
	do {
		memset(&appcmd, 0, sizeof(appcmd));
		appcmd.opcode = MMC_APP_CMD;
		appcmd.arg = (uint32_t)rca << 16;
		appcmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
		if (mmc_wait_for_cmd(busdev, dev, &appcmd, 0) != 0)
			err = MMC_ERR_FAILED;
		else
			err = appcmd.error;
		if (err == MMC_ERR_NONE) {
			if (!(appcmd.resp[0] & R1_APP_CMD))
				err = MMC_ERR_FAILED;
			else if (mmc_wait_for_cmd(busdev, dev, cmd, 0) != 0)
				err = MMC_ERR_FAILED;
			else
				err = cmd->error;
		}
	} while (err != MMC_ERR_NONE && retries-- > 0);
	sc->squelched--;

	if (err != MMC_ERR_NONE && busdev == dev) {
		if (sc->squelched == 0 && ppsratecheck(&sc->log_time,
		    &sc->log_count, LOG_PPS)) {
			device_printf(sc->dev, "ACMD%d failed, RESULT: %d\n",
			    cmd->opcode, err);
		}
	}

	return (err);
}

int
mmc_switch(device_t busdev, device_t dev, uint16_t rca, uint8_t set,
    uint8_t index, uint8_t value, u_int timeout, bool status)
{
	struct mmc_command cmd;
	struct mmc_softc *sc;
	int err;

	KASSERT(timeout != 0, ("%s: no timeout", __func__));

	sc = device_get_softc(busdev);

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = MMC_SWITCH_FUNC;
	cmd.arg = (MMC_SWITCH_FUNC_WR << 24) | (index << 16) | (value << 8) |
	    set;
	/*
	 * If the hardware supports busy detection but the switch timeout
	 * exceeds the maximum host timeout, use a R1 instead of a R1B
	 * response in order to keep the hardware from timing out.
	 */
	if (mmcbr_get_caps(busdev) & MMC_CAP_WAIT_WHILE_BUSY &&
	    timeout > mmcbr_get_max_busy_timeout(busdev))
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	else
		cmd.flags = MMC_RSP_R1B | MMC_CMD_AC;
	/*
	 * Pause re-tuning so it won't interfere with the busy state and also
	 * so that the result of CMD13 will always refer to switching rather
	 * than to a tuning command that may have snuck in between.
	 */
	sc->retune_paused++;
	err = mmc_wait_for_cmd(busdev, dev, &cmd, CMD_RETRIES);
	if (err != MMC_ERR_NONE || status == false)
		goto out;
	err = mmc_switch_status(busdev, dev, rca, timeout);
out:
	sc->retune_paused--;
	return (err);
}

int
mmc_switch_status(device_t busdev, device_t dev, uint16_t rca, u_int timeout)
{
	struct timeval cur, end;
	int err;
	uint32_t status;

	KASSERT(timeout != 0, ("%s: no timeout", __func__));

	/*
	 * Note that when using a R1B response in mmc_switch(), bridges of
	 * type MMC_CAP_WAIT_WHILE_BUSY will issue mmc_send_status() only
	 * once and then exit the loop.
	 */
	end.tv_sec = end.tv_usec = 0;
	for (;;) {
		err = mmc_send_status(busdev, dev, rca, &status);
		if (err != MMC_ERR_NONE)
			break;
		if (R1_CURRENT_STATE(status) == R1_STATE_TRAN)
			break;
		getmicrouptime(&cur);
		if (end.tv_sec == 0 && end.tv_usec == 0) {
			end.tv_usec = timeout;
			timevaladd(&end, &cur);
		}
		if (timevalcmp(&cur, &end, >)) {
			err = MMC_ERR_TIMEOUT;
			break;
		}
	}
	if (err == MMC_ERR_NONE && (status & R1_SWITCH_ERROR) != 0)
		return (MMC_ERR_FAILED);
	return (err);
}

int
mmc_send_ext_csd(device_t busdev, device_t dev, uint8_t *rawextcsd)
{
	struct mmc_command cmd;
	struct mmc_data data;
	int err;

	memset(&cmd, 0, sizeof(cmd));
	memset(&data, 0, sizeof(data));

	memset(rawextcsd, 0, MMC_EXTCSD_SIZE);
	cmd.opcode = MMC_SEND_EXT_CSD;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
	cmd.data = &data;

	data.data = rawextcsd;
	data.len = MMC_EXTCSD_SIZE;
	data.flags = MMC_DATA_READ;

	err = mmc_wait_for_cmd(busdev, dev, &cmd, CMD_RETRIES);
	return (err);
}

int
mmc_send_status(device_t busdev, device_t dev, uint16_t rca, uint32_t *status)
{
	struct mmc_command cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = MMC_SEND_STATUS;
	cmd.arg = (uint32_t)rca << 16;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	err = mmc_wait_for_cmd(busdev, dev, &cmd, CMD_RETRIES);
	*status = cmd.resp[0];
	return (err);
}

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * This software was developed in part by Philip Paeps under contract for
 * Solarflare Communications, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/syslog.h>
#include <sys/taskqueue.h>
#include <sys/malloc.h>

#include "common/efx.h"
#include "common/efx_mcdi.h"
#include "common/efx_regs_mcdi.h"

#include "sfxge.h"

#if EFSYS_OPT_MCDI_LOGGING
#include <dev/pci/pcivar.h>
#endif

#define	SFXGE_MCDI_POLL_INTERVAL_MIN 10		/* 10us in 1us units */
#define	SFXGE_MCDI_POLL_INTERVAL_MAX 100000	/* 100ms in 1us units */

static void
sfxge_mcdi_timeout(struct sfxge_softc *sc)
{
	device_t dev = sc->dev;

	log(LOG_WARNING, "[%s%d] MC_TIMEOUT", device_get_name(dev),
		device_get_unit(dev));

	EFSYS_PROBE(mcdi_timeout);
	sfxge_schedule_reset(sc);
}

static void
sfxge_mcdi_poll(struct sfxge_softc *sc, uint32_t timeout_us)
{
	efx_nic_t *enp;
	clock_t delay_total;
	clock_t delay_us;
	boolean_t aborted;

	delay_total = 0;
	delay_us = SFXGE_MCDI_POLL_INTERVAL_MIN;
	enp = sc->enp;

	do {
		if (efx_mcdi_request_poll(enp)) {
			EFSYS_PROBE1(mcdi_delay, clock_t, delay_total);
			return;
		}

		if (delay_total > timeout_us) {
			aborted = efx_mcdi_request_abort(enp);
			KASSERT(aborted, ("abort failed"));
			sfxge_mcdi_timeout(sc);
			return;
		}

		/* Spin or block depending on delay interval. */
		if (delay_us < 1000000)
			DELAY(delay_us);
		else
			pause("mcdi wait", delay_us * hz / 1000000);

		delay_total += delay_us;

		/* Exponentially back off the poll frequency. */
		delay_us = delay_us * 2;
		if (delay_us > SFXGE_MCDI_POLL_INTERVAL_MAX)
			delay_us = SFXGE_MCDI_POLL_INTERVAL_MAX;

	} while (1);
}

static void
sfxge_mcdi_execute(void *arg, efx_mcdi_req_t *emrp)
{
	struct sfxge_softc *sc;
	struct sfxge_mcdi *mcdi;
	uint32_t timeout_us = 0;

	sc = (struct sfxge_softc *)arg;
	mcdi = &sc->mcdi;

	SFXGE_MCDI_LOCK(mcdi);

	KASSERT(mcdi->state == SFXGE_MCDI_INITIALIZED,
	    ("MCDI not initialized"));

	/* Issue request and poll for completion. */
	efx_mcdi_get_timeout(sc->enp, emrp, &timeout_us);
	KASSERT(timeout_us > 0, ("MCDI timeout not initialized"));

	efx_mcdi_request_start(sc->enp, emrp, B_FALSE);
	sfxge_mcdi_poll(sc, timeout_us);

	SFXGE_MCDI_UNLOCK(mcdi);
}

static void
sfxge_mcdi_ev_cpl(void *arg)
{
	struct sfxge_softc *sc;
	struct sfxge_mcdi *mcdi;

	sc = (struct sfxge_softc *)arg;
	mcdi = &sc->mcdi;

	KASSERT(mcdi->state == SFXGE_MCDI_INITIALIZED,
	    ("MCDI not initialized"));

	/* We do not use MCDI completion, MCDI is simply polled */
}

static void
sfxge_mcdi_exception(void *arg, efx_mcdi_exception_t eme)
{
	struct sfxge_softc *sc;
	device_t dev;

	sc = (struct sfxge_softc *)arg;
	dev = sc->dev;

	log(LOG_WARNING, "[%s%d] MC_%s", device_get_name(dev),
	    device_get_unit(dev),
	    (eme == EFX_MCDI_EXCEPTION_MC_REBOOT)
	    ? "REBOOT"
	    : (eme == EFX_MCDI_EXCEPTION_MC_BADASSERT)
	    ? "BADASSERT" : "UNKNOWN");

	EFSYS_PROBE(mcdi_exception);

	sfxge_schedule_reset(sc);
}

#if EFSYS_OPT_MCDI_LOGGING

#define SFXGE_MCDI_LOG_BUF_SIZE 128

static size_t
sfxge_mcdi_do_log(char *buffer, void *data, size_t data_size,
		  size_t pfxsize, size_t position)
{
	uint32_t *words = data;
	size_t i;

	for (i = 0; i < data_size; i += sizeof(*words)) {
		if (position + 2 * sizeof(*words) + 1 >= SFXGE_MCDI_LOG_BUF_SIZE) {
			buffer[position] = '\0';
			printf("%s \\\n", buffer);
			position = pfxsize;
		}
		snprintf(buffer + position, SFXGE_MCDI_LOG_BUF_SIZE - position,
			 " %08x", *words);
		words++;
		position += 2 * sizeof(uint32_t) + 1;
	}
	return (position);
}

static void
sfxge_mcdi_logger(void *arg, efx_log_msg_t type,
		  void *header, size_t header_size,
		  void *data, size_t data_size)
{
	struct sfxge_softc *sc = (struct sfxge_softc *)arg;
	char buffer[SFXGE_MCDI_LOG_BUF_SIZE];
	size_t pfxsize;
	size_t start;

	if (!sc->mcdi_logging)
		return;

	pfxsize = snprintf(buffer, sizeof(buffer),
			   "sfc %04x:%02x:%02x.%02x %s MCDI RPC %s:",
			   pci_get_domain(sc->dev),
			   pci_get_bus(sc->dev),
			   pci_get_slot(sc->dev),
			   pci_get_function(sc->dev),
			   device_get_nameunit(sc->dev),
			   type == EFX_LOG_MCDI_REQUEST ? "REQ" :
			   type == EFX_LOG_MCDI_RESPONSE ? "RESP" : "???");
	start = sfxge_mcdi_do_log(buffer, header, header_size,
				  pfxsize, pfxsize);
	start = sfxge_mcdi_do_log(buffer, data, data_size, pfxsize, start);
	if (start != pfxsize) {
		buffer[start] = '\0';
		printf("%s\n", buffer);
	}
}

#endif

int
sfxge_mcdi_ioctl(struct sfxge_softc *sc, sfxge_ioc_t *ip)
{
	const efx_nic_cfg_t *encp = efx_nic_cfg_get(sc->enp);
	struct sfxge_mcdi *mp = &(sc->mcdi);
	efx_mcdi_req_t emr;
	uint8_t *mcdibuf;
	int rc;

	if (mp->state == SFXGE_MCDI_UNINITIALIZED) {
		rc = ENODEV;
		goto fail1;
	}

	if (!(encp->enc_features & EFX_FEATURE_MCDI)) {
		rc = ENOTSUP;
		goto fail2;
	}

	if (ip->u.mcdi.len > SFXGE_MCDI_MAX_PAYLOAD) {
		rc = EINVAL;
		goto fail3;
	}

	mcdibuf = malloc(SFXGE_MCDI_MAX_PAYLOAD, M_TEMP, M_WAITOK | M_ZERO);
	if ((rc = copyin(ip->u.mcdi.payload, mcdibuf, ip->u.mcdi.len)) != 0) {
		goto fail5;
	}

	emr.emr_cmd = ip->u.mcdi.cmd;
	emr.emr_in_buf = mcdibuf;
	emr.emr_in_length = ip->u.mcdi.len;

	emr.emr_out_buf = mcdibuf;
	emr.emr_out_length = SFXGE_MCDI_MAX_PAYLOAD;

	sfxge_mcdi_execute(sc, &emr);

	ip->u.mcdi.rc = emr.emr_rc;
	ip->u.mcdi.cmd = emr.emr_cmd;
	ip->u.mcdi.len = emr.emr_out_length_used;
	if ((rc = copyout(mcdibuf, ip->u.mcdi.payload, ip->u.mcdi.len)) != 0) {
		goto fail6;
	}

	/*
	 * Helpfully trigger a device reset in response to an MCDI_CMD_REBOOT
	 * Both ports will see ->emt_exception callbacks on the next MCDI poll
	 */
	if (ip->u.mcdi.cmd == MC_CMD_REBOOT) {

		EFSYS_PROBE(mcdi_ioctl_mc_reboot);
		/* sfxge_t->s_state_lock held */
		(void) sfxge_schedule_reset(sc);
	}

	free(mcdibuf, M_TEMP);

	return (0);

fail6:
fail5:
	free(mcdibuf, M_TEMP);
fail3:
fail2:
fail1:
	return (rc);
}


int
sfxge_mcdi_init(struct sfxge_softc *sc)
{
	efx_nic_t *enp;
	struct sfxge_mcdi *mcdi;
	efx_mcdi_transport_t *emtp;
	efsys_mem_t *esmp;
	int max_msg_size;
	int rc;

	enp = sc->enp;
	mcdi = &sc->mcdi;
	emtp = &mcdi->transport;
	esmp = &mcdi->mem;
	max_msg_size = sizeof (uint32_t) + MCDI_CTL_SDU_LEN_MAX_V2;

	KASSERT(mcdi->state == SFXGE_MCDI_UNINITIALIZED,
	    ("MCDI already initialized"));

	SFXGE_MCDI_LOCK_INIT(mcdi, device_get_nameunit(sc->dev));

	mcdi->state = SFXGE_MCDI_INITIALIZED;

	if ((rc = sfxge_dma_alloc(sc, max_msg_size, esmp)) != 0)
		goto fail;

	emtp->emt_context = sc;
	emtp->emt_dma_mem = esmp;
	emtp->emt_execute = sfxge_mcdi_execute;
	emtp->emt_ev_cpl = sfxge_mcdi_ev_cpl;
	emtp->emt_exception = sfxge_mcdi_exception;
#if EFSYS_OPT_MCDI_LOGGING
	emtp->emt_logger = sfxge_mcdi_logger;
	SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->dev),
		       SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)),
		       OID_AUTO, "mcdi_logging", CTLFLAG_RW,
		       &sc->mcdi_logging, 0,
		       "MCDI logging");
#endif

	if ((rc = efx_mcdi_init(enp, emtp)) != 0)
		goto fail;

	return (0);

fail:
	SFXGE_MCDI_LOCK_DESTROY(mcdi);
	mcdi->state = SFXGE_MCDI_UNINITIALIZED;
	return (rc);
}

void
sfxge_mcdi_fini(struct sfxge_softc *sc)
{
	struct sfxge_mcdi *mcdi;
	efx_nic_t *enp;
	efx_mcdi_transport_t *emtp;
	efsys_mem_t *esmp;

	enp = sc->enp;
	mcdi = &sc->mcdi;
	emtp = &mcdi->transport;
	esmp = &mcdi->mem;

	SFXGE_MCDI_LOCK(mcdi);
	KASSERT(mcdi->state == SFXGE_MCDI_INITIALIZED,
	    ("MCDI not initialized"));

	efx_mcdi_fini(enp);
	bzero(emtp, sizeof(*emtp));

	SFXGE_MCDI_UNLOCK(mcdi);

	sfxge_dma_free(esmp);

	SFXGE_MCDI_LOCK_DESTROY(mcdi);
}

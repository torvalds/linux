/*	$OpenBSD: psp.c,v 1.7 2025/06/04 08:21:29 bluhm Exp $	*/

/*
 * Copyright (c) 2023-2025 Hans-Joerg Hoexer <hshoexer@genua.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <dev/ic/pspvar.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "vmd.h"

extern struct vmd	*env;

/* Guest policy */
#define GPOL_NODBG	(1ULL << 0)	/* no debuggin */
#define GPOL_NOKS	(1ULL << 1)	/* no key sharing */
#define GPOL_ES		(1ULL << 2)	/* SEV-ES required */
#define GPOL_NOSEND	(1ULL << 3)	/* no guest migration */
#define GPOL_DOMAIN	(1ULL << 4)	/* no migration to other domain */
#define GPOL_SEV	(1ULL << 5)	/* no migration to non-SEV platform */


/*
 * Retrieve platform state.
 */
int
psp_get_pstate(uint16_t *state, uint8_t *major, uint8_t *minor,
    uint8_t *build, uint8_t *seves)
{
	struct psp_platform_status pst;

	if (ioctl(env->vmd_psp_fd, PSP_IOC_GET_PSTATUS, &pst) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	if (state)
		*state = pst.state;
	if (major)
		*major = pst.api_major;
	if (minor)
		*minor = pst.api_minor;
	if (build)
		*build = (pst.cfges_build >> 24) & 0xff;
	if (seves)
		*seves = pst.cfges_build & 0x1;

	return (0);
}


/*
 * Flush data fabrics of all cores.
 *
 * This ensures all data of a SEV enabled guest is committed to
 * memory.  This needs to be done before an ASID is assigend to
 * guest using psp_activate().
 */
int
psp_df_flush(void)
{
	if (ioctl(env->vmd_psp_fd, PSP_IOC_DF_FLUSH) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	return (0);
}


/*
 * Retrieve guest state.
 */
int
psp_get_gstate(uint32_t handle, uint32_t *policy, uint32_t *asid,
    uint8_t *state)
{
	struct psp_guest_status gst;

	memset(&gst, 0, sizeof(gst));
	gst.handle = handle;

	if (ioctl(env->vmd_psp_fd, PSP_IOC_GET_GSTATUS, &gst) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	if (policy)
		*policy = gst.policy;
	if (asid)
		*asid = gst.asid;
	if (state)
		*state = gst.state;

	return (0);
}


/*
 * Start the launch sequence of a guest.
 */
int
psp_launch_start(uint32_t *handle, int seves)
{
	struct psp_launch_start ls;

	memset(&ls, 0, sizeof(ls));

	/* Set guest policy. */
	ls.policy = (GPOL_NODBG | GPOL_NOKS | GPOL_NOSEND | GPOL_DOMAIN |
	    GPOL_SEV);
	/* Add encrypted state. */
	if (seves)
		ls.policy |= GPOL_ES;

	if (ioctl(env->vmd_psp_fd, PSP_IOC_LAUNCH_START, &ls) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	if (handle)
		*handle = ls.handle;

	return (0);
}


/*
 * Encrypt and measure a memory range.
 */
int
psp_launch_update(uint32_t handle, vaddr_t v, size_t len)
{
	struct psp_launch_update_data lud;

	memset(&lud, 0, sizeof(lud));
	lud.handle = handle;
	lud.paddr = v;			/* will be converted to paddr */
	lud.length = len;

	if (ioctl(env->vmd_psp_fd, PSP_IOC_LAUNCH_UPDATE_DATA, &lud) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	return (0);
}


/*
 * Finalize and return memory measurement.
 *
 * We ask the PSP to provide a measurement (HMAC) over the encrypted
 * memory.  As we do not yet negotiate a shared integrity key with
 * the PSP, the measurement is not really meaningful.  Thus we just
 * log it for now.
 */
int
psp_encrypt_state(uint32_t handle, uint32_t asid, uint32_t vmid,
    uint32_t vcpuid)
{
	struct psp_encrypt_state es;

	memset(&es, 0, sizeof(es));
	es.handle = handle;
	es.asid = asid;
	es.vmid = vmid;
	es.vcpuid = vcpuid;

	if (ioctl(env->vmd_psp_fd, PSP_IOC_ENCRYPT_STATE, &es) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	return (0);
}


int
psp_launch_measure(uint32_t handle)
{
	struct psp_launch_measure lm;
	char *p, buf[256];
	size_t len;
	unsigned int i;

	memset(&lm, 0, sizeof(lm));
	lm.handle = handle;
	lm.measure_len = sizeof(lm.psp_measure);
	memset(lm.measure, 0, sizeof(lm.measure));
	memset(lm.measure_nonce, 0, sizeof(lm.measure_nonce));

	if (ioctl(env->vmd_psp_fd, PSP_IOC_LAUNCH_MEASURE, &lm) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	/*
	 * We can not verify the measurement, yet. Therefore just
	 * log it.
	 */
	len = sizeof(buf);
	memset(buf, 0, len);
	p = buf;
	for (i = 0; i < sizeof(lm.measure) && len >= 2;
	    i++, p += 2, len -= 2) {
		snprintf(p, len, "%02x", lm.measure[i]);
	}
	log_info("%s: measurement 0x%s", __func__, buf);

	len = sizeof(buf);
	memset(buf, 0, len);
	p = buf;
	for (i = 0; i < sizeof(lm.measure_nonce) && len >= 2;
	    i++, p += 2, len -= 2) {
		snprintf(p, len, "%02x", lm.measure_nonce[i]);
	}
	log_info("%s: nonce 0x%s", __func__, buf);

	return (0);
}


/*
 * Finalize launch sequence.
 */
int
psp_launch_finish(uint32_t handle)
{
	struct psp_launch_finish lf;

	lf.handle = handle;

	if (ioctl(env->vmd_psp_fd, PSP_IOC_LAUNCH_FINISH, &lf) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	return (0);
}


/*
 * Activate a guest.
 *
 * This associates the guest's ASID with the handle used to identify
 * crypto contexts managed by the PSP.
 */
int
psp_activate(uint32_t handle, uint32_t asid)
{
	struct psp_activate act;

	act.handle = handle;
	act.asid = asid;

	if (ioctl(env->vmd_psp_fd, PSP_IOC_ACTIVATE, &act) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	return (0);
}


/*
 * Deactivate and decommission a guest.
 *
 * This deassociates the guest's ASID from the crypto contexts in
 * the PSP.  Then the PSP releases the crypto contexts (i.e. deletes
 * keys).
 */
int
psp_guest_shutdown(uint32_t handle)
{
	struct psp_guest_shutdown gshutdown;

	gshutdown.handle = handle;

	if (ioctl(env->vmd_psp_fd, PSP_IOC_GUEST_SHUTDOWN, &gshutdown) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	return (0);
}

/*
 * Initialize PSP.
 */
static int
psp_init(void)
{
	if (ioctl(env->vmd_psp_fd, PSP_IOC_INIT) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	return (0);
}

/*
 * Shutdown PSP.
 */
static int
psp_shutdown(void)
{
	if (ioctl(env->vmd_psp_fd, PSP_IOC_SHUTDOWN) < 0) {
		log_warn("%s: ioctl", __func__);
		return (-1);
	}

	return (0);
}

/*
 * Reset PSP.
 *
 * Shut PSP down, then re-initialize it.  This clears and resets
 * all active contexts.
 */
static int
psp_reset(void)
{
	int	ret;

	if ((ret = psp_shutdown()) < 0 || (ret = psp_init()) < 0)
		return (ret);

	return (0);
}

void
psp_setup(void)
{
	uint8_t	major, minor, build;

	env->vmd_psp_fd = open(PSP_NODE, O_RDWR);
	if (env->vmd_psp_fd == -1) {
		if (errno != ENXIO)
			log_debug("%s: failed to open %s", __func__, PSP_NODE);
		return;
	}

	if (psp_reset() < 0)
		fatalx("%s: failed to reset PSP", __func__);
	if (psp_get_pstate(NULL, &major, &minor, &build, NULL) < 0)
		fatalx("%s: failed to get platform state", __func__);
	log_info("PSP api %hhu.%hhu, build %hhu", major, minor, build);
}

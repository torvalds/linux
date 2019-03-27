/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/random.h>
#include <sys/rman.h>
#include <sys/uio.h>
#include <sys/kobj.h>

#include <dev/pci/pcivar.h>

#include <opencrypto/cryptodev.h>

#include "cryptodev_if.h"

#include <vm/vm.h>
#include <vm/pmap.h>

#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/xlp.h>
#include <mips/nlm/hal/sys.h>
#include <mips/nlm/hal/fmn.h>
#include <mips/nlm/hal/nlmsaelib.h>
#include <mips/nlm/dev/sec/rsa_ucode.h>
#include <mips/nlm/hal/cop2.h>
#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/msgring.h>
#include <mips/nlm/dev/sec/nlmrsalib.h>

#ifdef NLM_RSA_DEBUG
static	void print_krp_params(struct cryptkop *krp);
#endif

static	int xlp_rsa_init(struct xlp_rsa_softc *sc, int node);
static	int xlp_rsa_newsession(device_t , crypto_session_t, struct cryptoini *);
static	int xlp_rsa_kprocess(device_t , struct cryptkop *, int);
static	int xlp_get_rsa_opsize(struct xlp_rsa_command *cmd, unsigned int bits);
static	void xlp_free_cmd_params(struct xlp_rsa_command *cmd);
static	int xlp_rsa_inp2hwformat(uint8_t *src, uint8_t *dst,
    uint32_t paramsize, uint8_t result);

static	int xlp_rsa_probe(device_t);
static	int xlp_rsa_attach(device_t);
static	int xlp_rsa_detach(device_t);

static device_method_t xlp_rsa_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe, xlp_rsa_probe),
	DEVMETHOD(device_attach, xlp_rsa_attach),
	DEVMETHOD(device_detach, xlp_rsa_detach),

	/* bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),
	DEVMETHOD(bus_driver_added, bus_generic_driver_added),

	/* crypto device methods */
	DEVMETHOD(cryptodev_newsession, xlp_rsa_newsession),
	DEVMETHOD(cryptodev_kprocess,   xlp_rsa_kprocess),

	DEVMETHOD_END
};

static driver_t xlp_rsa_driver = {
	"nlmrsa",
	xlp_rsa_methods,
	sizeof(struct xlp_rsa_softc)
};
static devclass_t xlp_rsa_devclass;

DRIVER_MODULE(nlmrsa, pci, xlp_rsa_driver, xlp_rsa_devclass, 0, 0);
MODULE_DEPEND(nlmrsa, crypto, 1, 1, 1);

#ifdef NLM_RSA_DEBUG
static void
print_krp_params(struct cryptkop *krp)
{
	int i;

	printf("krp->krp_op	:%d\n", krp->krp_op);
	printf("krp->krp_status	:%d\n", krp->krp_status);
	printf("krp->krp_iparams:%d\n", krp->krp_iparams);
	printf("krp->krp_oparams:%d\n", krp->krp_oparams);
	for (i = 0; i < krp->krp_iparams + krp->krp_oparams; i++) {
		printf("krp->krp_param[%d].crp_p	:0x%llx\n", i,
		    (unsigned long long)krp->krp_param[i].crp_p);
		printf("krp->krp_param[%d].crp_nbits	:%d\n", i,
		    krp->krp_param[i].crp_nbits);
		printf("krp->krp_param[%d].crp_nbytes	:%d\n", i,
		    howmany(krp->krp_param[i].crp_nbits, 8));
	}
}
#endif

static int
xlp_rsa_init(struct xlp_rsa_softc *sc, int node)
{
	struct xlp_rsa_command *cmd = NULL;
	uint32_t fbvc, dstvc, endsel, regval;
	struct nlm_fmn_msg m;
	int err, ret, i;
	uint64_t base;

	/* Register interrupt handler for the RSA/ECC CMS messages */
	if (register_msgring_handler(sc->rsaecc_vc_start,
	    sc->rsaecc_vc_end, nlm_xlprsaecc_msgring_handler, sc) != 0) {
		err = -1;
		printf("Couldn't register rsa/ecc msgring handler\n");
		goto errout;
	}
	fbvc = nlm_cpuid() * 4 + XLPGE_FB_VC;
	/* Do the CMS credit initialization */
	/* Currently it is configured by default to 50 when kernel comes up */

#if BYTE_ORDER == LITTLE_ENDIAN
	for (i = 0; i < nitems(nlm_rsa_ucode_data); i++)
		nlm_rsa_ucode_data[i] = htobe64(nlm_rsa_ucode_data[i]);
#endif
	for (dstvc = sc->rsaecc_vc_start; dstvc <= sc->rsaecc_vc_end; dstvc++) {
		cmd = malloc(sizeof(struct xlp_rsa_command), M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		KASSERT(cmd != NULL, ("%s:cmd is NULL\n", __func__));
		cmd->rsasrc = contigmalloc(sizeof(nlm_rsa_ucode_data),
		    M_DEVBUF,
		    (M_WAITOK | M_ZERO),
		    0UL /* low address */, -1UL /* high address */,
		    XLP_L2L3_CACHELINE_SIZE /* alignment */,
		    0UL /* boundary */);
		KASSERT(cmd->rsasrc != NULL,
		    ("%s:cmd->rsasrc is NULL\n", __func__));
		memcpy(cmd->rsasrc, nlm_rsa_ucode_data,
		    sizeof(nlm_rsa_ucode_data));
		m.msg[0] = nlm_crypto_form_rsa_ecc_fmn_entry0(1, 0x70, 0,
		    vtophys(cmd->rsasrc));
		m.msg[1] = nlm_crypto_form_rsa_ecc_fmn_entry1(0, 1, fbvc,
		    vtophys(cmd->rsasrc));
		/* Software scratch pad */
		m.msg[2] = (uintptr_t)cmd;
		m.msg[3] = 0;

		ret = nlm_fmn_msgsend(dstvc, 3, FMN_SWCODE_RSA, &m);
		if (ret != 0) {
			err = -1;
			printf("%s: msgsnd failed (%x)\n", __func__, ret);
			goto errout;
		}
	}
	/* Configure so that all VCs send request to all RSA pipes */
	base = nlm_get_rsa_regbase(node);
	if (nlm_is_xlp3xx()) {
		endsel = 1;
		regval = 0xFFFF;
	} else {
		endsel = 3;
		regval = 0x07FFFFFF;
	}
	for (i = 0; i < endsel; i++)
		nlm_write_rsa_reg(base, RSA_ENG_SEL_0 + i, regval);
	return (0);
errout:
	xlp_free_cmd_params(cmd);
	return (err);
}

/* This function is called from an interrupt handler */
void
nlm_xlprsaecc_msgring_handler(int vc, int size, int code, int src_id,
    struct nlm_fmn_msg *msg, void *data)
{
	struct xlp_rsa_command *cmd;
	struct xlp_rsa_softc *sc;
	struct crparam *outparam;
	int ostart;

	KASSERT(code == FMN_SWCODE_RSA,
	    ("%s: bad code = %d, expected code = %d\n", __func__, code,
	    FMN_SWCODE_RSA));

	sc = data;
	KASSERT(src_id >= sc->rsaecc_vc_start && src_id <= sc->rsaecc_vc_end,
	    ("%s: bad src_id = %d, expect %d - %d\n", __func__,
	    src_id, sc->rsaecc_vc_start, sc->rsaecc_vc_end));

	cmd = (struct xlp_rsa_command *)(uintptr_t)msg->msg[1];
	KASSERT(cmd != NULL, ("%s:cmd not received properly\n", __func__));

	if (RSA_ERROR(msg->msg[0]) != 0) {
		printf("%s: Message rcv msg0 %llx msg1 %llx err %x \n",
		    __func__, (unsigned long long)msg->msg[0],
		    (unsigned long long)msg->msg[1],
		    (int)RSA_ERROR(msg->msg[0]));
		cmd->krp->krp_status = EBADMSG;
	}

	if (cmd->krp != NULL) {
		ostart = cmd->krp->krp_iparams;
		outparam = &cmd->krp->krp_param[ostart];
		xlp_rsa_inp2hwformat(cmd->rsasrc + cmd->rsaopsize * ostart,
		    outparam->crp_p,
		    howmany(outparam->crp_nbits, 8),
		    1);
		crypto_kdone(cmd->krp);
	}

	xlp_free_cmd_params(cmd);
}

static int
xlp_rsa_probe(device_t dev)
{
	struct xlp_rsa_softc *sc;

	if (pci_get_vendor(dev) == PCI_VENDOR_NETLOGIC &&
	    pci_get_device(dev) == PCI_DEVICE_ID_NLM_RSA) {
		sc = device_get_softc(dev);
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

/*
 * Attach an interface that successfully probed.
 */
static int
xlp_rsa_attach(device_t dev)
{
	struct xlp_rsa_softc *sc = device_get_softc(dev);
	uint64_t base;
	int qstart, qnum;
	int freq, node;

	sc->sc_dev = dev;

	node = nlm_get_device_node(pci_get_slot(dev));
	freq = nlm_set_device_frequency(node, DFS_DEVICE_RSA, 250);
	if (bootverbose)
		device_printf(dev, "RSA Freq: %dMHz\n", freq);
	if (pci_get_device(dev) == PCI_DEVICE_ID_NLM_RSA) {
		device_set_desc(dev, "XLP RSA/ECC Accelerator");
		sc->sc_cid = crypto_get_driverid(dev,
		    sizeof(struct xlp_rsa_session), CRYPTOCAP_F_HARDWARE);
		if (sc->sc_cid < 0) {
			printf("xlp_rsaecc-err:couldn't get the driver id\n");
			goto error_exit;
		}
		if (crypto_kregister(sc->sc_cid, CRK_MOD_EXP, 0) != 0)
			goto error_exit;

		base = nlm_get_rsa_pcibase(node);
		qstart = nlm_qidstart(base);
		qnum = nlm_qnum(base);
		sc->rsaecc_vc_start = qstart;
		sc->rsaecc_vc_end = qstart + qnum - 1;
	}
	if (xlp_rsa_init(sc, node) != 0)
		goto error_exit;
	device_printf(dev, "RSA Initialization complete!\n");
	return (0);

error_exit:
	return (ENXIO);
}

/*
 * Detach an interface that successfully probed.
 */
static int
xlp_rsa_detach(device_t dev)
{
	return (0);
}

/*
 * Allocate a new 'session' (unused).
 */
static int
xlp_rsa_newsession(device_t dev, crypto_session_t cses, struct cryptoini *cri)
{
	struct xlp_rsa_softc *sc = device_get_softc(dev);

	if (cri == NULL || sc == NULL)
		return (EINVAL);

	return (0);
}

/*
 * XXX freesession should run a zero'd mac/encrypt key into context ram.
 * XXX to blow away any keys already stored there.
 */

static void
xlp_free_cmd_params(struct xlp_rsa_command *cmd)
{

	if (cmd == NULL)
		return;
	if (cmd->rsasrc != NULL) {
		if (cmd->krp == NULL) /* Micro code load */
			contigfree(cmd->rsasrc, sizeof(nlm_rsa_ucode_data),
			    M_DEVBUF);
		else
			free(cmd->rsasrc, M_DEVBUF);
	}
	free(cmd, M_DEVBUF);
}

static int
xlp_get_rsa_opsize(struct xlp_rsa_command *cmd, unsigned int bits)
{

	if (bits == 0 || bits > 8192)
		return (-1);
	/* XLP hardware expects always a fixed size with unused bytes
	 * zeroed out in the input data */
	if (bits <= 512) {
		cmd->rsatype = 0x40;
		cmd->rsaopsize = 64;
	} else if (bits <= 1024) {
		cmd->rsatype = 0x41;
		cmd->rsaopsize = 128;
	} else if (bits <= 2048) {
		cmd->rsatype = 0x42;
		cmd->rsaopsize = 256;
	} else if (bits <= 4096) {
		cmd->rsatype = 0x43;
		cmd->rsaopsize = 512;
	} else if (bits <= 8192) {
		cmd->rsatype = 0x44;
		cmd->rsaopsize = 1024;
	}
	return (0);
}

static int
xlp_rsa_inp2hwformat(uint8_t *src, uint8_t *dst, uint32_t paramsize,
    uint8_t result)
{
	uint32_t pdwords, pbytes;
	int i, j, k;

	pdwords = paramsize / 8;
	pbytes = paramsize % 8;

	for (i = 0, k = 0; i < pdwords; i++) {
		/* copy dwords of inp/hw to hw/out format */
		for (j = 7; j >= 0; j--, k++)
			dst[i * 8 + j] = src[k];
	}
	if (pbytes) {
		if (result == 0) {
			/* copy rem bytes of input data to hw format */
			for (j = 7; k < paramsize; j--, k++)
				dst[i * 8 + j] = src[k];
		} else {
			/* copy rem bytes of hw data to exp output format */
			for (j = 7; k < paramsize; j--, k++)
				dst[k] = src[i * 8 + j];
		}
	}

	return (0);
}

static int
nlm_crypto_complete_rsa_request(struct xlp_rsa_softc *sc,
    struct xlp_rsa_command *cmd)
{
	unsigned int fbvc;
	struct nlm_fmn_msg m;
	int ret;

	fbvc = nlm_cpuid() * 4 + XLPGE_FB_VC;

	m.msg[0] = nlm_crypto_form_rsa_ecc_fmn_entry0(1, cmd->rsatype,
	    cmd->rsafn, vtophys(cmd->rsasrc));
	m.msg[1] = nlm_crypto_form_rsa_ecc_fmn_entry1(0, 1, fbvc,
	    vtophys(cmd->rsasrc + cmd->rsaopsize * cmd->krp->krp_iparams));
	/* Software scratch pad */
	m.msg[2] = (uintptr_t)cmd;
	m.msg[3] = 0;

	/* Send the message to rsa engine vc */
	ret = nlm_fmn_msgsend(sc->rsaecc_vc_start, 3, FMN_SWCODE_RSA, &m);
        if (ret != 0) {
#ifdef NLM_SEC_DEBUG
                printf("%s: msgsnd failed (%x)\n", __func__, ret);
#endif
		return (ERESTART);
        }
	return (0);
}

static int
xlp_rsa_kprocess(device_t dev, struct cryptkop *krp, int hint)
{
	struct xlp_rsa_softc *sc = device_get_softc(dev);
	struct xlp_rsa_command *cmd;
	struct crparam *kp;
	int err, i;

	if (krp == NULL || krp->krp_callback == NULL)
		return (EINVAL);

	cmd = malloc(sizeof(struct xlp_rsa_command), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	KASSERT(cmd != NULL, ("%s:cmd is NULL\n", __func__));
	cmd->krp = krp;

#ifdef NLM_RSA_DEBUG
	print_krp_params(krp);
#endif
	err = EOPNOTSUPP;
	switch (krp->krp_op) {
	case CRK_MOD_EXP:
		if (krp->krp_iparams == 3 && krp->krp_oparams == 1)
			break;
		goto errout;
	default:
		device_printf(dev, "Op:%d not yet supported\n", krp->krp_op);
		goto errout;
	}

	err = xlp_get_rsa_opsize(cmd,
	    krp->krp_param[krp->krp_iparams - 1].crp_nbits);
	if (err != 0) {
		err = EINVAL;
		goto errout;
	}
	cmd->rsafn = 0; /* Mod Exp */
	cmd->rsasrc = malloc(
	    cmd->rsaopsize * (krp->krp_iparams + krp->krp_oparams),
	    M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (cmd->rsasrc == NULL) {
		err = ENOMEM;
		goto errout;
	}

	for (i = 0, kp = krp->krp_param; i < krp->krp_iparams; i++, kp++) {
		KASSERT(kp->crp_nbits != 0,
		    ("%s: parameter[%d]'s length is zero\n", __func__, i));
		xlp_rsa_inp2hwformat(kp->crp_p,
		    cmd->rsasrc + i * cmd->rsaopsize,
		    howmany(kp->crp_nbits, 8), 0);
	}
	err = nlm_crypto_complete_rsa_request(sc, cmd);
	if (err != 0)
		goto errout;

	return (0);
errout:
	xlp_free_cmd_params(cmd);
	krp->krp_status = err;
	crypto_kdone(krp);
	return (err);
}

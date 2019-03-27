/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Nahanni Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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
 */

/*
 * virtio entropy device emulation.
 * Randomness is sourced from /dev/random which does not block
 * once it has been seeded at bootup.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#ifndef WITHOUT_CAPSICUM
#include <sys/capsicum.h>
#endif
#include <sys/linker_set.h>
#include <sys/uio.h>

#ifndef WITHOUT_CAPSICUM
#include <capsicum_helpers.h>
#endif
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <sysexits.h>

#include "bhyverun.h"
#include "pci_emul.h"
#include "virtio.h"

#define VTRND_RINGSZ	64


static int pci_vtrnd_debug;
#define DPRINTF(params) if (pci_vtrnd_debug) printf params
#define WPRINTF(params) printf params

/*
 * Per-device softc
 */
struct pci_vtrnd_softc {
	struct virtio_softc vrsc_vs;
	struct vqueue_info  vrsc_vq;
	pthread_mutex_t     vrsc_mtx;
	uint64_t            vrsc_cfg;
	int                 vrsc_fd;
};

static void pci_vtrnd_reset(void *);
static void pci_vtrnd_notify(void *, struct vqueue_info *);

static struct virtio_consts vtrnd_vi_consts = {
	"vtrnd",		/* our name */
	1,			/* we support 1 virtqueue */
	0,			/* config reg size */
	pci_vtrnd_reset,	/* reset */
	pci_vtrnd_notify,	/* device-wide qnotify */
	NULL,			/* read virtio config */
	NULL,			/* write virtio config */
	NULL,			/* apply negotiated features */
	0,			/* our capabilities */
};


static void
pci_vtrnd_reset(void *vsc)
{
	struct pci_vtrnd_softc *sc;

	sc = vsc;

	DPRINTF(("vtrnd: device reset requested !\n"));
	vi_reset_dev(&sc->vrsc_vs);
}


static void
pci_vtrnd_notify(void *vsc, struct vqueue_info *vq)
{
	struct iovec iov;
	struct pci_vtrnd_softc *sc;
	int len;
	uint16_t idx;

	sc = vsc;

	if (sc->vrsc_fd < 0) {
		vq_endchains(vq, 0);
		return;
	}

	while (vq_has_descs(vq)) {
		vq_getchain(vq, &idx, &iov, 1, NULL);

		len = read(sc->vrsc_fd, iov.iov_base, iov.iov_len);

		DPRINTF(("vtrnd: vtrnd_notify(): %d\r\n", len));

		/* Catastrophe if unable to read from /dev/random */
		assert(len > 0);

		/*
		 * Release this chain and handle more
		 */
		vq_relchain(vq, idx, len);
	}
	vq_endchains(vq, 1);	/* Generate interrupt if appropriate. */
}


static int
pci_vtrnd_init(struct vmctx *ctx, struct pci_devinst *pi, char *opts)
{
	struct pci_vtrnd_softc *sc;
	int fd;
	int len;
	uint8_t v;
#ifndef WITHOUT_CAPSICUM
	cap_rights_t rights;
#endif

	/*
	 * Should always be able to open /dev/random.
	 */
	fd = open("/dev/random", O_RDONLY | O_NONBLOCK);

	assert(fd >= 0);

#ifndef WITHOUT_CAPSICUM
	cap_rights_init(&rights, CAP_READ);
	if (caph_rights_limit(fd, &rights) == -1)
		errx(EX_OSERR, "Unable to apply rights for sandbox");
#endif

	/*
	 * Check that device is seeded and non-blocking.
	 */
	len = read(fd, &v, sizeof(v));
	if (len <= 0) {
		WPRINTF(("vtrnd: /dev/random not ready, read(): %d", len));
		close(fd);
		return (1);
	}

	sc = calloc(1, sizeof(struct pci_vtrnd_softc));

	vi_softc_linkup(&sc->vrsc_vs, &vtrnd_vi_consts, sc, pi, &sc->vrsc_vq);
	sc->vrsc_vs.vs_mtx = &sc->vrsc_mtx;

	sc->vrsc_vq.vq_qsize = VTRND_RINGSZ;

	/* keep /dev/random opened while emulating */
	sc->vrsc_fd = fd;

	/* initialize config space */
	pci_set_cfgdata16(pi, PCIR_DEVICE, VIRTIO_DEV_RANDOM);
	pci_set_cfgdata16(pi, PCIR_VENDOR, VIRTIO_VENDOR);
	pci_set_cfgdata8(pi, PCIR_CLASS, PCIC_CRYPTO);
	pci_set_cfgdata16(pi, PCIR_SUBDEV_0, VIRTIO_TYPE_ENTROPY);
	pci_set_cfgdata16(pi, PCIR_SUBVEND_0, VIRTIO_VENDOR);

	if (vi_intr_init(&sc->vrsc_vs, 1, fbsdrun_virtio_msix()))
		return (1);
	vi_set_io_bar(&sc->vrsc_vs, 0);

	return (0);
}


struct pci_devemu pci_de_vrnd = {
	.pe_emu =	"virtio-rnd",
	.pe_init =	pci_vtrnd_init,
	.pe_barwrite =	vi_pci_write,
	.pe_barread =	vi_pci_read
};
PCI_EMUL_SET(pci_de_vrnd);

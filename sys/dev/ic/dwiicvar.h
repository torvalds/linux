/* $OpenBSD: dwiicvar.h,v 1.6 2023/08/29 12:09:40 kettenis Exp $ */
/*
 * Synopsys DesignWare I2C controller
 *
 * Copyright (c) 2015, 2016 joshua stein <jcs@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#ifdef __HAVE_ACPI
#include "acpi.h"
#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>
#endif

#include <dev/pci/pcivar.h>

#include <dev/i2c/i2cvar.h>

#include <dev/ic/dwiicreg.h>

/* #define DWIIC_DEBUG */

#ifdef DWIIC_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

struct dwiic_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;
	char			sc_hid[16];
	void			*sc_ih;

	struct pci_attach_args	sc_paa;

	struct i2cbus_attach_args sc_iba;
	struct device		*sc_iic;

	u_int32_t		sc_caps;
	int			sc_poll;
	int			sc_poll_ihidev;
	int			sc_busy;
	int			sc_readwait;
	int			sc_writewait;

	uint32_t		master_cfg;
	uint16_t		ss_hcnt, ss_lcnt, fs_hcnt, fs_lcnt;
	uint32_t		sda_hold_time;
	int			tx_fifo_depth;
	int			rx_fifo_depth;

	struct i2c_controller	sc_i2c_tag;
	struct rwlock		sc_i2c_lock;
	struct {
		i2c_op_t	op;
		void		*buf;
		size_t		len;
		int		flags;
		volatile int	error;
	} sc_i2c_xfer;
};

int		dwiic_activate(struct device *, int);
int		dwiic_init(struct dwiic_softc *);
void		dwiic_enable(struct dwiic_softc *, int);
int		dwiic_intr(void *);

void *		dwiic_i2c_intr_establish(void *, void *, int,
		    int (*)(void *), void *, const char *);
void		dwiic_i2c_intr_disestablish(void *, void *);
const char *	dwiic_i2c_intr_string(void *, void *);
int		dwiic_i2c_print(void *, const char *);

int		dwiic_i2c_acquire_bus(void *, int);
void		dwiic_i2c_release_bus(void *, int);
uint32_t	dwiic_read(struct dwiic_softc *, int);
void		dwiic_write(struct dwiic_softc *, int, uint32_t);
int		dwiic_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *,
		    size_t, void *, size_t, int);

#if NACPI > 0
int		dwiic_acpi_found_hid(struct aml_node *, void *);
void		dwiic_acpi_get_params(struct dwiic_softc *, char *, uint16_t *,
		    uint16_t *, uint32_t *);
#endif

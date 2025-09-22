/*	$OpenBSD: pucvar.h,v 1.18 2024/05/24 04:36:26 jsg Exp $	*/
/*	$NetBSD: pucvar.h,v 1.2 1999/02/06 06:29:54 cgd Exp $	*/

/*
 * Copyright (c) 1998, 1999 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 */

/*
 * Exported (or conveniently located) PCI "universal" communications card
 * software structures.
 *
 * Author: Christopher G. Demetriou, May 14, 1998.
 */

#include <dev/ic/comreg.h>

#define	PUC_MAX_PORTS		16

struct puc_device_description {
	u_int16_t	rval[4];
	u_int16_t	rmask[4];
	struct {
		u_char	type;
		u_char	bar;
		u_short	offset;
	}			ports[PUC_MAX_PORTS];
};

struct puc_port_type {
	enum {
		PUC_PORT_LPT = 1,
		PUC_PORT_COM,
		PUC_PORT_COM_MUL4,
		PUC_PORT_COM_MUL8,
		PUC_PORT_COM_MUL10,
		PUC_PORT_COM_MUL128,
		PUC_PORT_COM_XR17V35X,
	} type;
	u_int32_t	freq;
};

static const struct puc_port_type puc_port_types[] = {
	{ PUC_PORT_LPT,		0		},
	{ PUC_PORT_COM,		COM_FREQ	},
	{ PUC_PORT_COM_MUL4,	COM_FREQ * 4	},
	{ PUC_PORT_COM_MUL8,	COM_FREQ * 8	},
	{ PUC_PORT_COM_MUL10,	COM_FREQ * 10	},
	{ PUC_PORT_COM_MUL128,	COM_FREQ * 128	},
	{ PUC_PORT_COM_XR17V35X, 125000000	},
};

#define PUC_IS_LPT(type)	((type) == PUC_PORT_LPT)
#define PUC_IS_COM(type)	((type) != PUC_PORT_LPT)

#define PUC_PORT_BAR_INDEX(bar)	(((bar) - PCI_MAPREG_START) / 4)

struct puc_attach_args {
	int			port;
	int			type;
	void			*puc;

	bus_addr_t		a;
	bus_space_tag_t		t;
	bus_space_handle_t	h;

	const char *(*intr_string)(struct puc_attach_args *);
	void *(*intr_establish)(struct puc_attach_args *, int, int (*)(void *),
	    void *, char *);
};

extern const struct puc_device_description puc_devs[];
extern int puc_ndevs;

#define	PUC_NBARS	6
struct puc_softc {
	struct device		sc_dev;

	/* static configuration data */
	const struct puc_device_description *sc_desc;

	/* card-global dynamic data */
	struct {
		int		mapped;
		u_long		type;
		bus_addr_t	a;
		bus_size_t	s;
		bus_space_tag_t	t;
		bus_space_handle_t h;
	} sc_bar_mappings[PUC_NBARS];

	/* per-port dynamic data */
	struct {
		struct device   *dev;
		/* filled in by port attachments */
		void	*intrhand;
		int	(*real_intrhand)(void *);
		void	*real_intrhand_arg;
	} sc_ports[PUC_MAX_PORTS];

	int			sc_xr17v35x;
};

const struct puc_device_description *
    puc_find_description(u_int16_t, u_int16_t, u_int16_t, u_int16_t);
void	puc_print_ports(const struct puc_device_description *);
void	puc_common_attach(struct puc_softc *, struct puc_attach_args *);
int	puc_print(void *, const char *);
int	puc_submatch(struct device *, void *, void *);

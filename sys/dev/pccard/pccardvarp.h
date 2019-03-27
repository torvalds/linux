/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005, M. Warner Losh
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * $FreeBSD$
 */

#ifndef _PCCARD_PCCARDVARP_H
#define _PCCARD_PCCARDVARP_H

/* pccard itself */

#define PCCARD_MEM_PAGE_SIZE		1024

#define PCCARD_CFE_MWAIT_REQUIRED	0x0001
#define PCCARD_CFE_RDYBSY_ACTIVE	0x0002
#define PCCARD_CFE_WP_ACTIVE		0x0004
#define PCCARD_CFE_BVD_ACTIVE		0x0008
#define PCCARD_CFE_IO8			0x0010
#define PCCARD_CFE_IO16			0x0020
#define PCCARD_CFE_IRQSHARE		0x0040
#define PCCARD_CFE_IRQPULSE		0x0080
#define PCCARD_CFE_IRQLEVEL		0x0100
#define PCCARD_CFE_POWERDOWN		0x0200
#define PCCARD_CFE_READONLY		0x0400
#define PCCARD_CFE_AUDIO		0x0800

struct pccard_ce_iospace {
	rman_res_t	length;
	rman_res_t	start;
};

struct pccard_ce_memspace {
	rman_res_t	length;
	rman_res_t	cardaddr;
	rman_res_t	hostaddr;
};

struct pccard_config_entry {
	int		number;
	uint32_t	flags;
	int		iftype;
	int		num_iospace;
	/*
	 * The card will only decode this mask in any case, so we can
	 * do dynamic allocation with this in mind, in case the suggestions
	 * below are no good.
	 */
	u_long		iomask;
	struct pccard_ce_iospace iospace[4]; /* XXX up to 16 */
	uint16_t	irqmask;
	int		num_memspace;
	struct pccard_ce_memspace memspace[2];	/* XXX up to 8 */
	int		maxtwins;
	STAILQ_ENTRY(pccard_config_entry) cfe_list;
};

struct pccard_funce_disk {
	uint8_t pfd_interface;
	uint8_t pfd_power;
};

struct pccard_funce_lan {
	int pfl_nidlen;
	uint8_t pfl_nid[8];
};

union pccard_funce {
	struct pccard_funce_disk pfv_disk;
	struct pccard_funce_lan pfv_lan;
};

struct pccard_function {
	/* read off the card */
	int		number;
	int		function;
	int		last_config_index;
	uint32_t	ccr_base;	/* Offset with card's memory */
	uint32_t	ccr_mask;
	struct resource *ccr_res;
	int		ccr_rid;
	STAILQ_HEAD(, pccard_config_entry) cfe_head;
	STAILQ_ENTRY(pccard_function) pf_list;
	/* run-time state */
	struct pccard_softc *sc;
	struct pccard_config_entry *cfe;
	struct pccard_mem_handle pf_pcmh;
	device_t	dev;
#define	pf_ccrt		pf_pcmh.memt
#define	pf_ccrh		pf_pcmh.memh
#define	pf_ccr_realsize	pf_pcmh.realsize
	uint32_t	pf_ccr_offset;	/* Offset from ccr_base of CIS */
	int		pf_ccr_window;
	bus_addr_t	pf_mfc_iobase;
	bus_addr_t	pf_mfc_iomax;
	int		pf_flags;
	driver_filter_t	*intr_filter;
	driver_intr_t	*intr_handler;
	void		*intr_handler_arg;
	void		*intr_handler_cookie;

	union pccard_funce pf_funce; /* CISTPL_FUNCE */
#define pf_funce_disk_interface pf_funce.pfv_disk.pfd_interface
#define pf_funce_disk_power pf_funce.pfv_disk.pfd_power
#define pf_funce_lan_nid pf_funce.pfv_lan.pfl_nid
#define pf_funce_lan_nidlen pf_funce.pfv_lan.pfl_nidlen
};

/* pf_flags */
#define	PFF_ENABLED	0x0001		/* function is enabled */

struct pccard_card {
	int		cis1_major;
	int		cis1_minor;
	/* XXX waste of space? */
	char		cis1_info_buf[256];
	char		*cis1_info[4];
	/*
	 * Use int32_t for manufacturer and product so that they can
	 * hold the id value found in card CIS and special value that
	 * indicates no id was found.
	 */
	int32_t		manufacturer;
#define	PCMCIA_VENDOR_INVALID	-1
	int32_t		product;
#define	PCMCIA_PRODUCT_INVALID		-1
	int16_t		prodext;
	uint16_t	error;
#define	PCMCIA_CIS_INVALID		{ NULL, NULL, NULL, NULL }
	STAILQ_HEAD(, pccard_function) pf_head;
};

/* More later? */
struct pccard_ivar {
	struct resource_list resources;
	struct pccard_function *pf;
};

struct cis_buffer
{
	size_t	len;			/* Actual length of the CIS */
	uint8_t buffer[2040];		/* small enough to be 2k */
};

struct pccard_softc {
	device_t		dev;
	/* this stuff is for the socket */

	/* this stuff is for the card */
	struct pccard_card card;
	int		sc_enabled_count;	/* num functions enabled */
	struct cdev *cisdev;
	int	cis_open;
	struct cis_buffer *cis;
};

struct pccard_cis_quirk {
	int32_t manufacturer;
	int32_t product;
	char *cis1_info[4];
	struct pccard_function *pf;
	struct pccard_config_entry *cfe;
};

void	pccard_read_cis(struct pccard_softc *);
void	pccard_check_cis_quirks(device_t);
void	pccard_print_cis(device_t);
int	pccard_scan_cis(device_t, device_t, pccard_scan_t, void *);

int	pccard_device_create(struct pccard_softc *);
int	pccard_device_destroy(struct pccard_softc *);

#define PCCARD_SOFTC(d) (struct pccard_softc *) device_get_softc(d)
#define PCCARD_IVAR(d) (struct pccard_ivar *) device_get_ivars(d)

#endif /* _PCCARD_PCCARDVARP_H */

/*	$OpenBSD: uhavar.h,v 1.5 2020/07/22 13:16:04 krw Exp $	*/
/*	$NetBSD: uhavar.h,v 1.3 1996/10/21 22:34:43 thorpej Exp $	*/

/*
 * Copyright (c) 1994, 1996 Charles M. Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#define UHA_MSCP_MAX	32	/* store up to 32 MSCPs at one time */
#define	MSCP_HASH_SIZE	32	/* hash table size for phystokv */
#define	MSCP_HASH_SHIFT	9
#define MSCP_HASH(x)	((((long)(x))>>MSCP_HASH_SHIFT) & (MSCP_HASH_SIZE - 1))

struct uha_softc {
	struct device sc_dev;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;

	int sc_irq, sc_drq;
	void *sc_ih;

	void (*start_mbox)(struct uha_softc *, struct uha_mscp *);
	int (*poll)(struct uha_softc *, struct scsi_xfer *, int);
	void (*init)(struct uha_softc *);

	struct uha_mscp *sc_mscphash[MSCP_HASH_SIZE];
	SLIST_HEAD(, uha_mscp) sc_free_mscp;

	struct mutex		sc_mscp_mtx;
	struct scsi_iopool	sc_iopool;

	int sc_nummscps;
	int sc_scsi_dev;		/* our scsi id */
};

void	uha_attach(struct uha_softc *);
void	uha_timeout(void *arg);
struct	uha_mscp *uha_mscp_phys_kv(struct uha_softc *, u_long);
void	uha_done(struct uha_softc *, struct uha_mscp *);

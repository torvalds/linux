/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2007 Daniel Roethlisberger <daniel@roe.ch>
 * All rights reserved.
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

/*#define	CMX_DEBUG*/
/*#define	CMX_INTR*/

#define	CMX_MIN_RDLEN	10		/* min read length */
#define	CMX_MIN_WRLEN	5		/* min write length */
#define	CMX_BUFSZ	512		/* I/O block size */

struct cmx_softc {
	device_t dev;			/* pccard device */
	struct cdev *cdev;		/* character device */

	struct resource *ioport;	/* io port resource descriptor */
	int ioport_rid;			/* io port resource identification */

	bus_space_tag_t bst;		/* bus space tag */
	bus_space_handle_t bsh;		/* bus space handle */

#ifdef CMX_INTR
	struct resource* irq;		/* irq resource descriptor */
	int irq_rid;			/* irq resource identification */
	void *ih;			/* intr handle */
#endif

	struct mtx mtx;			/* per-unit lock */
	struct callout ch;		/* callout handle */
	struct selinfo sel;		/* select/poll queue handle */

	int open;			/* is chardev open? */
	int polling;			/* are we polling? */
	int dying;			/* are we detaching? */

	unsigned long timeout;		/* response timeout */

	uint8_t buf[CMX_BUFSZ];		/* read/write buffer */
};

extern devclass_t cmx_devclass;

void	cmx_init_softc(device_t);
int	cmx_alloc_resources(device_t);
void	cmx_release_resources(device_t);
int	cmx_attach(device_t);
int	cmx_detach(device_t);

#define	CMX_READ_1(sc, off)						\
	(bus_space_read_1((sc)->bst, (sc)->bsh, off))
#define	CMX_WRITE_1(sc, off, val)					\
	(bus_space_write_1((sc)->bst, (sc)->bsh, off, val))

#define	cmx_read_BSR(sc)						\
	CMX_READ_1(sc, REG_OFFSET_BSR)
#define	cmx_write_BSR(sc, val)						\
	CMX_WRITE_1(sc, REG_OFFSET_BSR, val)
#define	cmx_read_SCR(sc)						\
	CMX_READ_1(sc, REG_OFFSET_SCR)
#define	cmx_write_SCR(sc, val)						\
	CMX_WRITE_1(sc, REG_OFFSET_SCR, val)
#define	cmx_read_DTR(sc)						\
	CMX_READ_1(sc, REG_OFFSET_DTR)
#define	cmx_write_DTR(sc, val)						\
	CMX_WRITE_1(sc, REG_OFFSET_DTR, val)

#define	cmx_test(byte, flags, test)					\
	(((byte) & (flags)) == ((test) ? (flags) : 0))

#define	cmx_test_BSR(sc, flags, test)					\
	cmx_test(cmx_read_BSR(sc), flags, test)

#define	CMX_LOCK(sc)			mtx_lock(&(sc)->mtx)
#define	CMX_UNLOCK(sc)			mtx_unlock(&(sc)->mtx)
#define	CMX_LOCK_ASSERT(sc, what)	mtx_assert(&(sc)->mtx, (what))

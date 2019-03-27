/*-
 * Copyright (c) 1997-2001 Granch, Ltd. All rights reserved.
 * Author: Denis I.Timofeev <timofeev@granch.ru>
 *
 * Redistributon and use in source and binary forms, with or without
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
 * LIABILITY, OR TORT (INCLUDING NEIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Device driver for Granch SBNI12 leased line adapters
 *
 * Revision 2.0.0  1997/08/06
 * Initial revision by Alexey Zverev
 *
 * Revision 2.0.1 1997/08/11
 * Additional internal statistics support (tx statistics)
 *
 * Revision 2.0.2 1997/11/05
 * if_bpf bug has been fixed
 *
 * Revision 2.0.3 1998/12/20
 * Memory leakage has been eliminated in
 * the sbni_st and sbni_timeout routines.
 *
 * Revision 3.0 2000/08/10 by Yaroslav Polyakov
 * Support for PCI cards. 4.1 modification.
 *
 * Revision 3.1 2000/09/12
 * Removed extra #defines around bpf functions
 *
 * Revision 4.0 2000/11/23 by Denis Timofeev
 * Completely redesigned the buffer management
 *
 * Revision 4.1 2001/01/21
 * Support for PCI Dual cards and new SBNI12D-10, -11 Dual/ISA cards
 *
 * Written with reference to NE2000 driver developed by David Greenman.
 */
 

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/callout.h>
#include <sys/syslog.h>
#include <sys/random.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/ethernet.h>
#include <net/bpf.h>
#include <net/if_types.h>

#include <dev/sbni/if_sbnireg.h>
#include <dev/sbni/if_sbnivar.h>

static void	sbni_init(void *);
static void	sbni_init_locked(struct sbni_softc *);
static void	sbni_start(struct ifnet *);
static void	sbni_start_locked(struct ifnet *);
static int	sbni_ioctl(struct ifnet *, u_long, caddr_t);
static void	sbni_stop(struct sbni_softc *);
static void	handle_channel(struct sbni_softc *);

static void	card_start(struct sbni_softc *);
static int	recv_frame(struct sbni_softc *);
static void	send_frame(struct sbni_softc *);
static int	upload_data(struct sbni_softc *, u_int, u_int, u_int, u_int32_t);
static int	skip_tail(struct sbni_softc *, u_int, u_int32_t);
static void	interpret_ack(struct sbni_softc *, u_int);
static void	download_data(struct sbni_softc *, u_int32_t *);
static void	prepare_to_send(struct sbni_softc *);
static void	drop_xmit_queue(struct sbni_softc *);
static int	get_rx_buf(struct sbni_softc *);
static void	indicate_pkt(struct sbni_softc *);
static void	change_level(struct sbni_softc *);
static int	check_fhdr(struct sbni_softc *, u_int *, u_int *,
			   u_int *, u_int *, u_int32_t *); 
static int	append_frame_to_pkt(struct sbni_softc *, u_int, u_int32_t);
static void	timeout_change_level(struct sbni_softc *);
static void	send_frame_header(struct sbni_softc *, u_int32_t *);
static void	set_initial_values(struct sbni_softc *, struct sbni_flags);

static u_int32_t	calc_crc32(u_int32_t, caddr_t, u_int);
static timeout_t	sbni_timeout;

static __inline u_char	sbni_inb(struct sbni_softc *, enum sbni_reg);
static __inline void	sbni_outb(struct sbni_softc *, enum sbni_reg, u_char);
static __inline void	sbni_insb(struct sbni_softc *, u_char *, u_int);
static __inline void	sbni_outsb(struct sbni_softc *, u_char *, u_int);

static u_int32_t crc32tab[];

#ifdef SBNI_DUAL_COMPOUND
static struct mtx headlist_lock;
MTX_SYSINIT(headlist_lock, &headlist_lock, "sbni headlist", MTX_DEF);
static struct sbni_softc *sbni_headlist;
#endif

/* -------------------------------------------------------------------------- */

static __inline u_char
sbni_inb(struct sbni_softc *sc, enum sbni_reg reg)
{
	return bus_space_read_1(
	    rman_get_bustag(sc->io_res),
	    rman_get_bushandle(sc->io_res),
	    sc->io_off + reg);
}

static __inline void
sbni_outb(struct sbni_softc *sc, enum sbni_reg reg, u_char value)
{
	bus_space_write_1(
	    rman_get_bustag(sc->io_res),
	    rman_get_bushandle(sc->io_res),
	    sc->io_off + reg, value);
}

static __inline void
sbni_insb(struct sbni_softc *sc, u_char *to, u_int len)
{
	bus_space_read_multi_1(
	    rman_get_bustag(sc->io_res),
	    rman_get_bushandle(sc->io_res),
	    sc->io_off + DAT, to, len);
}

static __inline void
sbni_outsb(struct sbni_softc *sc, u_char *from, u_int len)
{
	bus_space_write_multi_1(
	    rman_get_bustag(sc->io_res),
	    rman_get_bushandle(sc->io_res),
	    sc->io_off + DAT, from, len);
}


/*
	Valid combinations in CSR0 (for probing):

	VALID_DECODER	0000,0011,1011,1010

				    	; 0   ; -
				TR_REQ	; 1   ; +
			TR_RDY	    	; 2   ; -
			TR_RDY	TR_REQ	; 3   ; +
		BU_EMP		    	; 4   ; +
		BU_EMP	     	TR_REQ	; 5   ; +
		BU_EMP	TR_RDY	    	; 6   ; -
		BU_EMP	TR_RDY	TR_REQ	; 7   ; +
	RC_RDY 		     		; 8   ; +
	RC_RDY			TR_REQ	; 9   ; +
	RC_RDY		TR_RDY		; 10  ; -
	RC_RDY		TR_RDY	TR_REQ	; 11  ; -
	RC_RDY	BU_EMP			; 12  ; -
	RC_RDY	BU_EMP		TR_REQ	; 13  ; -
	RC_RDY	BU_EMP	TR_RDY		; 14  ; -
	RC_RDY	BU_EMP	TR_RDY	TR_REQ	; 15  ; -
*/

#define VALID_DECODER	(2 + 8 + 0x10 + 0x20 + 0x80 + 0x100 + 0x200)


int
sbni_probe(struct sbni_softc *sc)
{
	u_char csr0;

	csr0 = sbni_inb(sc, CSR0);
	if (csr0 != 0xff && csr0 != 0x00) {
		csr0 &= ~EN_INT;
		if (csr0 & BU_EMP)
			csr0 |= EN_INT;
      
		if (VALID_DECODER & (1 << (csr0 >> 4)))
			return (0);
	}
   
	return (ENXIO);
}


/*
 * Install interface into kernel networking data structures
 */
int
sbni_attach(struct sbni_softc *sc, int unit, struct sbni_flags flags)
{
	struct ifnet *ifp;
	u_char csr0;
   
	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL)
		return (ENOMEM);
	sbni_outb(sc, CSR0, 0);
	set_initial_values(sc, flags);

	/* Initialize ifnet structure */
	ifp->if_softc	= sc;
	if_initname(ifp, "sbni", unit);
	ifp->if_init	= sbni_init;
	ifp->if_start	= sbni_start;
	ifp->if_ioctl	= sbni_ioctl;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);

	/* report real baud rate */
	csr0 = sbni_inb(sc, CSR0);
	ifp->if_baudrate =
		(csr0 & 0x01 ? 500000 : 2000000) / (1 << flags.rate);

	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

	mtx_init(&sc->lock, ifp->if_xname, MTX_NETWORK_LOCK, MTX_DEF);
	callout_init_mtx(&sc->wch, &sc->lock, 0);
	ether_ifattach(ifp, sc->enaddr);
	/* device attach does transition from UNCONFIGURED to IDLE state */

	if_printf(ifp, "speed %ju, rxl ", (uintmax_t)ifp->if_baudrate);
	if (sc->delta_rxl)
		printf("auto\n");
	else
		printf("%d (fixed)\n", sc->cur_rxl_index);
	return (0);
}

void
sbni_detach(struct sbni_softc *sc)
{

	SBNI_LOCK(sc);
	sbni_stop(sc);
	SBNI_UNLOCK(sc);
	callout_drain(&sc->wch);
	ether_ifdetach(sc->ifp);
	if (sc->irq_handle)
		bus_teardown_intr(sc->dev, sc->irq_res, sc->irq_handle);
	mtx_destroy(&sc->lock);
	if_free(sc->ifp);
}

void
sbni_release_resources(struct sbni_softc *sc)
{

	if (sc->irq_res)
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
	if (sc->io_res && sc->io_off == 0)
		bus_release_resource(sc->dev, SYS_RES_IOPORT, sc->io_rid,
		    sc->io_res);
}

/* -------------------------------------------------------------------------- */

static void
sbni_init(void *xsc)
{
	struct sbni_softc *sc;

	sc = (struct sbni_softc *)xsc;
	SBNI_LOCK(sc);
	sbni_init_locked(sc);
	SBNI_UNLOCK(sc);
}

static void
sbni_init_locked(struct sbni_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->ifp;

	/*
	 * kludge to avoid multiple initialization when more than once
	 * protocols configured
	 */
	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	card_start(sc);
	callout_reset(&sc->wch, hz/SBNI_HZ, sbni_timeout, sc);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	/* attempt to start output */
	sbni_start_locked(ifp);
}

static void
sbni_start(struct ifnet *ifp)
{
	struct sbni_softc *sc = ifp->if_softc;

	SBNI_LOCK(sc);
	sbni_start_locked(ifp);
	SBNI_UNLOCK(sc);
}

static void
sbni_start_locked(struct ifnet *ifp)
{
	struct sbni_softc *sc = ifp->if_softc;

	if (sc->tx_frameno == 0)
		prepare_to_send(sc);
}


static void
sbni_stop(struct sbni_softc *sc)
{
	sbni_outb(sc, CSR0, 0);
	drop_xmit_queue(sc);

	if (sc->rx_buf_p) {
		m_freem(sc->rx_buf_p);
		sc->rx_buf_p = NULL;
	}

	callout_stop(&sc->wch);
	sc->ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
}

/* -------------------------------------------------------------------------- */

/* interrupt handler */

/*
 * 	SBNI12D-10, -11/ISA boards within "common interrupt" mode could not
 * be looked as two independent single-channel devices. Every channel seems
 * as Ethernet interface but interrupt handler must be common. Really, first
 * channel ("master") driver only registers the handler. In it's struct softc
 * it has got pointer to "slave" channel's struct softc and handles that's
 * interrupts too.
 *	softc of successfully attached ISA SBNI boards is linked to list.
 * While next board driver is initialized, it scans this list. If one
 * has found softc with same irq and ioaddr different by 4 then it assumes
 * this board to be "master".
 */ 

void
sbni_intr(void *arg)
{
	struct sbni_softc *sc;
	int repeat;

	sc = (struct sbni_softc *)arg;

	do {
		repeat = 0;
		SBNI_LOCK(sc);
		if (sbni_inb(sc, CSR0) & (RC_RDY | TR_RDY)) {
			handle_channel(sc);
			repeat = 1;
		}
		SBNI_UNLOCK(sc);
		if (sc->slave_sc) {
			/* second channel present */
			SBNI_LOCK(sc->slave_sc);
			if (sbni_inb(sc->slave_sc, CSR0) & (RC_RDY | TR_RDY)) {
				handle_channel(sc->slave_sc);
				repeat = 1;
			}
			SBNI_UNLOCK(sc->slave_sc);
		}
	} while (repeat);
}


static void
handle_channel(struct sbni_softc *sc)
{
	int req_ans;
	u_char csr0;

	sbni_outb(sc, CSR0, (sbni_inb(sc, CSR0) & ~EN_INT) | TR_REQ);

	sc->timer_ticks = CHANGE_LEVEL_START_TICKS;
	for (;;) {
		csr0 = sbni_inb(sc, CSR0);
		if ((csr0 & (RC_RDY | TR_RDY)) == 0)
			break;

		req_ans = !(sc->state & FL_PREV_OK);

		if (csr0 & RC_RDY)
			req_ans = recv_frame(sc);

		/*
		 * TR_RDY always equals 1 here because we have owned the marker,
		 * and we set TR_REQ when disabled interrupts
		 */
		csr0 = sbni_inb(sc, CSR0);
		if ((csr0 & TR_RDY) == 0 || (csr0 & RC_RDY) != 0)
			if_printf(sc->ifp, "internal error!\n");

		/* if state & FL_NEED_RESEND != 0 then tx_frameno != 0 */
		if (req_ans || sc->tx_frameno != 0)
			send_frame(sc);
		else {
			/* send the marker without any data */
			sbni_outb(sc, CSR0, sbni_inb(sc, CSR0) & ~TR_REQ);
		}
	}

	sbni_outb(sc, CSR0, sbni_inb(sc, CSR0) | EN_INT);
}


/*
 * Routine returns 1 if it need to acknoweledge received frame.
 * Empty frame received without errors won't be acknoweledged.
 */

static int
recv_frame(struct sbni_softc *sc)
{
	u_int32_t crc;
	u_int framelen, frameno, ack;
	u_int is_first, frame_ok;

	crc = CRC32_INITIAL;
	if (check_fhdr(sc, &framelen, &frameno, &ack, &is_first, &crc)) {
		frame_ok = framelen > 4 ?
		    upload_data(sc, framelen, frameno, is_first, crc) :
		    skip_tail(sc, framelen, crc);
		if (frame_ok)
			interpret_ack(sc, ack);
	} else {
		framelen = 0;
		frame_ok = 0;
	}

	sbni_outb(sc, CSR0, sbni_inb(sc, CSR0) ^ CT_ZER);
	if (frame_ok) {
		sc->state |= FL_PREV_OK;
		if (framelen > 4)
			sc->in_stats.all_rx_number++;
	} else {
		sc->state &= ~FL_PREV_OK;
		change_level(sc);
		sc->in_stats.all_rx_number++;
		sc->in_stats.bad_rx_number++;
	}

	return (!frame_ok || framelen > 4);
}


static void
send_frame(struct sbni_softc *sc)
{
	u_int32_t crc;
	u_char csr0;

	crc = CRC32_INITIAL;
	if (sc->state & FL_NEED_RESEND) {

		/* if frame was sended but not ACK'ed - resend it */
		if (sc->trans_errors) {
			sc->trans_errors--;
			if (sc->framelen != 0)
				sc->in_stats.resend_tx_number++;
		} else {
			/* cannot xmit with many attempts */
			drop_xmit_queue(sc);
			goto do_send;
		}
	} else
		sc->trans_errors = TR_ERROR_COUNT;

	send_frame_header(sc, &crc);
	sc->state |= FL_NEED_RESEND;
	/*
	 * FL_NEED_RESEND will be cleared after ACK, but if empty
	 * frame sended then in prepare_to_send next frame
	 */


	if (sc->framelen) {
		download_data(sc, &crc);
		sc->in_stats.all_tx_number++;
		sc->state |= FL_WAIT_ACK;
	}

	sbni_outsb(sc, (u_char *)&crc, sizeof crc);

do_send:
	csr0 = sbni_inb(sc, CSR0);
	sbni_outb(sc, CSR0, csr0 & ~TR_REQ);

	if (sc->tx_frameno) {
		/* next frame exists - request to send */
		sbni_outb(sc, CSR0, csr0 | TR_REQ);
	}
}


static void
download_data(struct sbni_softc *sc, u_int32_t *crc_p)
{
	struct mbuf *m;
	caddr_t	data_p;
	u_int data_len, pos, slice;

	data_p = NULL;		/* initialized to avoid warn */
	pos = 0;

	for (m = sc->tx_buf_p;  m != NULL && pos < sc->pktlen;  m = m->m_next) {
		if (pos + m->m_len > sc->outpos) {
			data_len = m->m_len - (sc->outpos - pos);
			data_p = mtod(m, caddr_t) + (sc->outpos - pos);

			goto do_copy;
		} else
			pos += m->m_len;
	}

	data_len = 0;

do_copy:
	pos = 0;
	do {
		if (data_len) {
			slice = min(data_len, sc->framelen - pos);
			sbni_outsb(sc, data_p, slice);
			*crc_p = calc_crc32(*crc_p, data_p, slice);

			pos += slice;
			if (data_len -= slice)
				data_p += slice;
			else {
				do {
					m = m->m_next;
				} while (m != NULL && m->m_len == 0);

				if (m) {
					data_len = m->m_len;
					data_p = mtod(m, caddr_t);
				}
			}
		} else {
			/* frame too short - zero padding */

			pos = sc->framelen - pos;
			while (pos--) {
				sbni_outb(sc, DAT, 0);
				*crc_p = CRC32(0, *crc_p);
			}
			return;
		}
	} while (pos < sc->framelen);
}


static int
upload_data(struct sbni_softc *sc, u_int framelen, u_int frameno,
	    u_int is_first, u_int32_t crc)
{
	int frame_ok;

	if (is_first) {
		sc->wait_frameno = frameno;
		sc->inppos = 0;
	}

	if (sc->wait_frameno == frameno) {

		if (sc->inppos + framelen  <=  ETHER_MAX_LEN) {
			frame_ok = append_frame_to_pkt(sc, framelen, crc);

		/*
		 * if CRC is right but framelen incorrect then transmitter
		 * error was occurred... drop entire packet
		 */
		} else if ((frame_ok = skip_tail(sc, framelen, crc)) != 0) {
			sc->wait_frameno = 0;
			sc->inppos = 0;
			if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
			/* now skip all frames until is_first != 0 */
		}
	} else
		frame_ok = skip_tail(sc, framelen, crc);

	if (is_first && !frame_ok) {
		/*
		 * Frame has been violated, but we have stored
		 * is_first already... Drop entire packet.
		 */
		sc->wait_frameno = 0;
		if_inc_counter(sc->ifp, IFCOUNTER_IERRORS, 1);
	}

	return (frame_ok);
}


static __inline void	send_complete(struct sbni_softc *);

static __inline void
send_complete(struct sbni_softc *sc)
{
	m_freem(sc->tx_buf_p);
	sc->tx_buf_p = NULL;
	if_inc_counter(sc->ifp, IFCOUNTER_OPACKETS, 1);
}


static void
interpret_ack(struct sbni_softc *sc, u_int ack)
{
	if (ack == FRAME_SENT_OK) {
		sc->state &= ~FL_NEED_RESEND;

		if (sc->state & FL_WAIT_ACK) {
			sc->outpos += sc->framelen;

			if (--sc->tx_frameno) {
				sc->framelen = min(
				    sc->maxframe, sc->pktlen - sc->outpos);
			} else {
				send_complete(sc);
				prepare_to_send(sc);
			}
		}
	}

	sc->state &= ~FL_WAIT_ACK;
}


/*
 * Glue received frame with previous fragments of packet.
 * Indicate packet when last frame would be accepted.
 */

static int
append_frame_to_pkt(struct sbni_softc *sc, u_int framelen, u_int32_t crc)
{
	caddr_t p;

	if (sc->inppos + framelen > ETHER_MAX_LEN)
		return (0);

	if (!sc->rx_buf_p && !get_rx_buf(sc))
		return (0);

	p = sc->rx_buf_p->m_data + sc->inppos;
	sbni_insb(sc, p, framelen);
	if (calc_crc32(crc, p, framelen) != CRC32_REMAINDER)
		return (0);

	sc->inppos += framelen - 4;
	if (--sc->wait_frameno == 0) {		/* last frame received */
		indicate_pkt(sc);
		if_inc_counter(sc->ifp, IFCOUNTER_IPACKETS, 1);
	}

	return (1);
}


/*
 * Prepare to start output on adapter. Current priority must be set to splimp
 * before this routine is called.
 * Transmitter will be actually activated when marker has been accepted.
 */

static void
prepare_to_send(struct sbni_softc *sc)
{
	struct mbuf *m;
	u_int len;

	/* sc->tx_buf_p == NULL here! */
	if (sc->tx_buf_p)
		printf("sbni: memory leak!\n");

	sc->outpos = 0;
	sc->state &= ~(FL_WAIT_ACK | FL_NEED_RESEND);

	for (;;) {
		IF_DEQUEUE(&sc->ifp->if_snd, sc->tx_buf_p);
		if (!sc->tx_buf_p) {
			/* nothing to transmit... */
			sc->pktlen     = 0;
			sc->tx_frameno = 0;
			sc->framelen   = 0;
			sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
			return;
		}

		for (len = 0, m = sc->tx_buf_p;  m;  m = m->m_next)
			len += m->m_len;

		if (len != 0)
			break;
		m_freem(sc->tx_buf_p);
	}

	if (len < SBNI_MIN_LEN)
		len = SBNI_MIN_LEN;

	sc->pktlen	= len;
	sc->tx_frameno	= howmany(len, sc->maxframe);
	sc->framelen	= min(len, sc->maxframe);

	sbni_outb(sc, CSR0, sbni_inb(sc, CSR0) | TR_REQ);
	sc->ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	BPF_MTAP(sc->ifp, sc->tx_buf_p);
}


static void
drop_xmit_queue(struct sbni_softc *sc)
{
	struct mbuf *m;

	if (sc->tx_buf_p) {
		m_freem(sc->tx_buf_p);
		sc->tx_buf_p = NULL;
		if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, 1);
	}

	for (;;) {
		IF_DEQUEUE(&sc->ifp->if_snd, m);
		if (m == NULL)
			break;
		m_freem(m);
		if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, 1);
	}

	sc->tx_frameno	= 0;
	sc->framelen	= 0;
	sc->outpos	= 0;
	sc->state &= ~(FL_WAIT_ACK | FL_NEED_RESEND);
	sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}


static void
send_frame_header(struct sbni_softc *sc, u_int32_t *crc_p)
{
	u_int32_t crc;
	u_int len_field;
	u_char value;

	crc = *crc_p;
	len_field = sc->framelen + 6;	/* CRC + frameno + reserved */

	if (sc->state & FL_NEED_RESEND)
		len_field |= FRAME_RETRY;	/* non-first attempt... */

	if (sc->outpos == 0)
		len_field |= FRAME_FIRST;

	len_field |= (sc->state & FL_PREV_OK) ? FRAME_SENT_OK : FRAME_SENT_BAD;
	sbni_outb(sc, DAT, SBNI_SIG);

	value = (u_char)len_field;
	sbni_outb(sc, DAT, value);
	crc = CRC32(value, crc);
	value = (u_char)(len_field >> 8);
	sbni_outb(sc, DAT, value);
	crc = CRC32(value, crc);

	sbni_outb(sc, DAT, sc->tx_frameno);
	crc = CRC32(sc->tx_frameno, crc);
	sbni_outb(sc, DAT, 0);
	crc = CRC32(0, crc);
	*crc_p = crc;
}


/*
 * if frame tail not needed (incorrect number or received twice),
 * it won't store, but CRC will be calculated
 */

static int
skip_tail(struct sbni_softc *sc, u_int tail_len, u_int32_t crc)
{
	while (tail_len--)
		crc = CRC32(sbni_inb(sc, DAT), crc);

	return (crc == CRC32_REMAINDER);
}


static int
check_fhdr(struct sbni_softc *sc, u_int *framelen, u_int *frameno,
	   u_int *ack, u_int *is_first, u_int32_t *crc_p)
{
	u_int32_t crc;
	u_char value;

	crc = *crc_p;
	if (sbni_inb(sc, DAT) != SBNI_SIG)
		return (0);

	value = sbni_inb(sc, DAT);
	*framelen = (u_int)value;
	crc = CRC32(value, crc);
	value = sbni_inb(sc, DAT);
	*framelen |= ((u_int)value) << 8;
	crc = CRC32(value, crc);

	*ack = *framelen & FRAME_ACK_MASK;
	*is_first = (*framelen & FRAME_FIRST) != 0;

	if ((*framelen &= FRAME_LEN_MASK) < 6 || *framelen > SBNI_MAX_FRAME - 3)
		return (0);

	value = sbni_inb(sc, DAT);
	*frameno = (u_int)value;
	crc = CRC32(value, crc);

	crc = CRC32(sbni_inb(sc, DAT), crc);		/* reserved byte */
	*framelen -= 2;

	*crc_p = crc;
	return (1);
}


static int
get_rx_buf(struct sbni_softc *sc)
{
	struct mbuf *m;

	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		if_printf(sc->ifp, "cannot allocate header mbuf\n");
		return (0);
	}

	/*
	 * We always put the received packet in a single buffer -
	 * either with just an mbuf header or in a cluster attached
	 * to the header. The +2 is to compensate for the alignment
	 * fixup below.
	 */
	if (ETHER_MAX_LEN + 2 > MHLEN) {
		/* Attach an mbuf cluster */
		if (!(MCLGET(m, M_NOWAIT))) {
			m_freem(m);
			return (0);
		}
	}
	m->m_pkthdr.len = m->m_len = ETHER_MAX_LEN + 2;

	/*
	 * The +2 is to longword align the start of the real packet.
	 * (sizeof ether_header == 14)
	 * This is important for NFS.
	 */
	m_adj(m, 2);
	sc->rx_buf_p = m;
	return (1);
}


static void
indicate_pkt(struct sbni_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	struct mbuf *m;

	m = sc->rx_buf_p;
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len   = m->m_len = sc->inppos;
	sc->rx_buf_p = NULL;

	SBNI_UNLOCK(sc);
	(*ifp->if_input)(ifp, m);
	SBNI_LOCK(sc);
}

/* -------------------------------------------------------------------------- */

/*
 * Routine checks periodically wire activity and regenerates marker if
 * connect was inactive for a long time.
 */

static void
sbni_timeout(void *xsc)
{
	struct sbni_softc *sc;
	u_char csr0;

	sc = (struct sbni_softc *)xsc;
	SBNI_ASSERT_LOCKED(sc);

	csr0 = sbni_inb(sc, CSR0);
	if (csr0 & RC_CHK) {

		if (sc->timer_ticks) {
			if (csr0 & (RC_RDY | BU_EMP))
				/* receiving not active */
				sc->timer_ticks--;
		} else {
			sc->in_stats.timeout_number++;
			if (sc->delta_rxl)
				timeout_change_level(sc);

			sbni_outb(sc, CSR1, *(u_char *)&sc->csr1 | PR_RES);
			csr0 = sbni_inb(sc, CSR0);
		}
	}

	sbni_outb(sc, CSR0, csr0 | RC_CHK);
	callout_reset(&sc->wch, hz/SBNI_HZ, sbni_timeout, sc);
}

/* -------------------------------------------------------------------------- */

static void
card_start(struct sbni_softc *sc)
{
	sc->timer_ticks = CHANGE_LEVEL_START_TICKS;
	sc->state &= ~(FL_WAIT_ACK | FL_NEED_RESEND);
	sc->state |= FL_PREV_OK;

	sc->inppos = 0;
	sc->wait_frameno = 0;

	sbni_outb(sc, CSR1, *(u_char *)&sc->csr1 | PR_RES);
	sbni_outb(sc, CSR0, EN_INT);
}

/* -------------------------------------------------------------------------- */

static u_char rxl_tab[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08,
	0x0a, 0x0c, 0x0f, 0x16, 0x18, 0x1a, 0x1c, 0x1f
};

#define SIZE_OF_TIMEOUT_RXL_TAB 4
static u_char timeout_rxl_tab[] = {
	0x03, 0x05, 0x08, 0x0b
};

static void
set_initial_values(struct sbni_softc *sc, struct sbni_flags flags)
{
	if (flags.fixed_rxl) {
		sc->delta_rxl = 0; /* disable receive level autodetection */
		sc->cur_rxl_index = flags.rxl;
	} else {
		sc->delta_rxl = DEF_RXL_DELTA;
		sc->cur_rxl_index = DEF_RXL;
	}
   
	sc->csr1.rate = flags.fixed_rate ? flags.rate : DEFAULT_RATE;
	sc->csr1.rxl  = rxl_tab[sc->cur_rxl_index];
	sc->maxframe  = DEFAULT_FRAME_LEN;
   
	/*
	 * generate Ethernet address (0x00ff01xxxxxx)
	 */
	*(u_int16_t *) sc->enaddr = htons(0x00ff);
	if (flags.mac_addr) {
		*(u_int32_t *) (sc->enaddr + 2) =
		    htonl(flags.mac_addr | 0x01000000);
	} else {
		*(u_char *) (sc->enaddr + 2) = 0x01;
		read_random(sc->enaddr + 3, 3);
	}
}


#ifdef SBNI_DUAL_COMPOUND
void
sbni_add(struct sbni_softc *sc)
{

	mtx_lock(&headlist_lock);
	sc->link = sbni_headlist;
	sbni_headlist = sc;
	mtx_unlock(&headlist_lock);
}

struct sbni_softc *
connect_to_master(struct sbni_softc *sc)
{
	struct sbni_softc *p, *p_prev;

	mtx_lock(&headlist_lock);
	for (p = sbni_headlist, p_prev = NULL; p; p_prev = p, p = p->link) {
		if (rman_get_start(p->io_res) == rman_get_start(sc->io_res) + 4 ||
		    rman_get_start(p->io_res) == rman_get_start(sc->io_res) - 4) {
			p->slave_sc = sc;
			if (p_prev)
				p_prev->link = p->link;
			else
				sbni_headlist = p->link;
			mtx_unlock(&headlist_lock);
			return p;
		}
	}
	mtx_unlock(&headlist_lock);

	return (NULL);
}

#endif	/* SBNI_DUAL_COMPOUND */


/* Receive level auto-selection */

static void
change_level(struct sbni_softc *sc)
{
	if (sc->delta_rxl == 0)		/* do not auto-negotiate RxL */
		return;

	if (sc->cur_rxl_index == 0)
		sc->delta_rxl = 1;
	else if (sc->cur_rxl_index == 15)
		sc->delta_rxl = -1;
	else if (sc->cur_rxl_rcvd < sc->prev_rxl_rcvd)
		sc->delta_rxl = -sc->delta_rxl;

	sc->csr1.rxl = rxl_tab[sc->cur_rxl_index += sc->delta_rxl];
	sbni_inb(sc, CSR0);	/* it needed for PCI cards */
	sbni_outb(sc, CSR1, *(u_char *)&sc->csr1);

	sc->prev_rxl_rcvd = sc->cur_rxl_rcvd;
	sc->cur_rxl_rcvd  = 0;
}


static void
timeout_change_level(struct sbni_softc *sc)
{
	sc->cur_rxl_index = timeout_rxl_tab[sc->timeout_rxl];
	if (++sc->timeout_rxl >= 4)
		sc->timeout_rxl = 0;

	sc->csr1.rxl = rxl_tab[sc->cur_rxl_index];
	sbni_inb(sc, CSR0);
	sbni_outb(sc, CSR1, *(u_char *)&sc->csr1);

	sc->prev_rxl_rcvd = sc->cur_rxl_rcvd;
	sc->cur_rxl_rcvd  = 0;
}

/* -------------------------------------------------------------------------- */

/*
 * Process an ioctl request. This code needs some work - it looks
 *	pretty ugly.
 */

static int
sbni_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct sbni_softc *sc;
	struct ifreq *ifr;
	struct thread *td;
	struct sbni_in_stats *in_stats;
	struct sbni_flags flags;
	int error;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	td = curthread;
	error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		/*
		 * If the interface is marked up and stopped, then start it.
		 * If it is marked down and running, then stop it.
		 */
		SBNI_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
				sbni_init_locked(sc);
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				sbni_stop(sc);
			}
		}
		SBNI_UNLOCK(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/*
		 * Multicast list has changed; set the hardware filter
		 * accordingly.
		 */
		error = 0;
		/* if (ifr == NULL)
			error = EAFNOSUPPORT; */
		break;

		/*
		 * SBNI specific ioctl
		 */
	case SIOCGHWFLAGS:	/* get flags */
		SBNI_LOCK(sc);
		bcopy((caddr_t)IF_LLADDR(sc->ifp)+3, (caddr_t) &flags, 3);
		flags.rxl = sc->cur_rxl_index;
		flags.rate = sc->csr1.rate;
		flags.fixed_rxl = (sc->delta_rxl == 0);
		flags.fixed_rate = 1;
		SBNI_UNLOCK(sc);
		bcopy(&flags, &ifr->ifr_ifru, sizeof(flags));
		break;

	case SIOCGINSTATS:
		in_stats = malloc(sizeof(struct sbni_in_stats), M_DEVBUF,
		    M_WAITOK);
		SBNI_LOCK(sc);
		bcopy(&sc->in_stats, in_stats, sizeof(struct sbni_in_stats));
		SBNI_UNLOCK(sc);
		error = copyout(in_stats, ifr_data_get_ptr(ifr),
		    sizeof(struct sbni_in_stats));
		free(in_stats, M_DEVBUF);
		break;

	case SIOCSHWFLAGS:	/* set flags */
		/* root only */
		error = priv_check(td, PRIV_DRIVER);
		if (error)
			break;
		bcopy(&ifr->ifr_ifru, &flags, sizeof(flags));
		SBNI_LOCK(sc);
		if (flags.fixed_rxl) {
			sc->delta_rxl = 0;
			sc->cur_rxl_index = flags.rxl;
		} else {
			sc->delta_rxl = DEF_RXL_DELTA;
			sc->cur_rxl_index = DEF_RXL;
		}
		sc->csr1.rxl = rxl_tab[sc->cur_rxl_index];
		sc->csr1.rate = flags.fixed_rate ? flags.rate : DEFAULT_RATE;
		if (flags.mac_addr)
			bcopy((caddr_t) &flags,
			      (caddr_t) IF_LLADDR(sc->ifp)+3, 3);

		/* Don't be afraid... */
		sbni_outb(sc, CSR1, *(char*)(&sc->csr1) | PR_RES);
		SBNI_UNLOCK(sc);
		break;

	case SIOCRINSTATS:
		SBNI_LOCK(sc);
		if (!(error = priv_check(td, PRIV_DRIVER)))	/* root only */
			bzero(&sc->in_stats, sizeof(struct sbni_in_stats));
		SBNI_UNLOCK(sc);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

/* -------------------------------------------------------------------------- */

static u_int32_t
calc_crc32(u_int32_t crc, caddr_t p, u_int len)
{
	while (len--)
		crc = CRC32(*p++, crc);

	return (crc);
}

static u_int32_t crc32tab[] __aligned(8) = {
	0xD202EF8D,  0xA505DF1B,  0x3C0C8EA1,  0x4B0BBE37,
	0xD56F2B94,  0xA2681B02,  0x3B614AB8,  0x4C667A2E,
	0xDCD967BF,  0xABDE5729,  0x32D70693,  0x45D03605,
	0xDBB4A3A6,  0xACB39330,  0x35BAC28A,  0x42BDF21C,
	0xCFB5FFE9,  0xB8B2CF7F,  0x21BB9EC5,  0x56BCAE53,
	0xC8D83BF0,  0xBFDF0B66,  0x26D65ADC,  0x51D16A4A,
	0xC16E77DB,  0xB669474D,  0x2F6016F7,  0x58672661,
	0xC603B3C2,  0xB1048354,  0x280DD2EE,  0x5F0AE278,
	0xE96CCF45,  0x9E6BFFD3,  0x0762AE69,  0x70659EFF,
	0xEE010B5C,  0x99063BCA,  0x000F6A70,  0x77085AE6,
	0xE7B74777,  0x90B077E1,  0x09B9265B,  0x7EBE16CD,
	0xE0DA836E,  0x97DDB3F8,  0x0ED4E242,  0x79D3D2D4,
	0xF4DBDF21,  0x83DCEFB7,  0x1AD5BE0D,  0x6DD28E9B,
	0xF3B61B38,  0x84B12BAE,  0x1DB87A14,  0x6ABF4A82,
	0xFA005713,  0x8D076785,  0x140E363F,  0x630906A9,
	0xFD6D930A,  0x8A6AA39C,  0x1363F226,  0x6464C2B0,
	0xA4DEAE1D,  0xD3D99E8B,  0x4AD0CF31,  0x3DD7FFA7,
	0xA3B36A04,  0xD4B45A92,  0x4DBD0B28,  0x3ABA3BBE,
	0xAA05262F,  0xDD0216B9,  0x440B4703,  0x330C7795,
	0xAD68E236,  0xDA6FD2A0,  0x4366831A,  0x3461B38C,
	0xB969BE79,  0xCE6E8EEF,  0x5767DF55,  0x2060EFC3,
	0xBE047A60,  0xC9034AF6,  0x500A1B4C,  0x270D2BDA,
	0xB7B2364B,  0xC0B506DD,  0x59BC5767,  0x2EBB67F1,
	0xB0DFF252,  0xC7D8C2C4,  0x5ED1937E,  0x29D6A3E8,
	0x9FB08ED5,  0xE8B7BE43,  0x71BEEFF9,  0x06B9DF6F,
	0x98DD4ACC,  0xEFDA7A5A,  0x76D32BE0,  0x01D41B76,
	0x916B06E7,  0xE66C3671,  0x7F6567CB,  0x0862575D,
	0x9606C2FE,  0xE101F268,  0x7808A3D2,  0x0F0F9344,
	0x82079EB1,  0xF500AE27,  0x6C09FF9D,  0x1B0ECF0B,
	0x856A5AA8,  0xF26D6A3E,  0x6B643B84,  0x1C630B12,
	0x8CDC1683,  0xFBDB2615,  0x62D277AF,  0x15D54739,
	0x8BB1D29A,  0xFCB6E20C,  0x65BFB3B6,  0x12B88320,
	0x3FBA6CAD,  0x48BD5C3B,  0xD1B40D81,  0xA6B33D17,
	0x38D7A8B4,  0x4FD09822,  0xD6D9C998,  0xA1DEF90E,
	0x3161E49F,  0x4666D409,  0xDF6F85B3,  0xA868B525,
	0x360C2086,  0x410B1010,  0xD80241AA,  0xAF05713C,
	0x220D7CC9,  0x550A4C5F,  0xCC031DE5,  0xBB042D73,
	0x2560B8D0,  0x52678846,  0xCB6ED9FC,  0xBC69E96A,
	0x2CD6F4FB,  0x5BD1C46D,  0xC2D895D7,  0xB5DFA541,
	0x2BBB30E2,  0x5CBC0074,  0xC5B551CE,  0xB2B26158,
	0x04D44C65,  0x73D37CF3,  0xEADA2D49,  0x9DDD1DDF,
	0x03B9887C,  0x74BEB8EA,  0xEDB7E950,  0x9AB0D9C6,
	0x0A0FC457,  0x7D08F4C1,  0xE401A57B,  0x930695ED,
	0x0D62004E,  0x7A6530D8,  0xE36C6162,  0x946B51F4,
	0x19635C01,  0x6E646C97,  0xF76D3D2D,  0x806A0DBB,
	0x1E0E9818,  0x6909A88E,  0xF000F934,  0x8707C9A2,
	0x17B8D433,  0x60BFE4A5,  0xF9B6B51F,  0x8EB18589,
	0x10D5102A,  0x67D220BC,  0xFEDB7106,  0x89DC4190,
	0x49662D3D,  0x3E611DAB,  0xA7684C11,  0xD06F7C87,
	0x4E0BE924,  0x390CD9B2,  0xA0058808,  0xD702B89E,
	0x47BDA50F,  0x30BA9599,  0xA9B3C423,  0xDEB4F4B5,
	0x40D06116,  0x37D75180,  0xAEDE003A,  0xD9D930AC,
	0x54D13D59,  0x23D60DCF,  0xBADF5C75,  0xCDD86CE3,
	0x53BCF940,  0x24BBC9D6,  0xBDB2986C,  0xCAB5A8FA,
	0x5A0AB56B,  0x2D0D85FD,  0xB404D447,  0xC303E4D1,
	0x5D677172,  0x2A6041E4,  0xB369105E,  0xC46E20C8,
	0x72080DF5,  0x050F3D63,  0x9C066CD9,  0xEB015C4F,
	0x7565C9EC,  0x0262F97A,  0x9B6BA8C0,  0xEC6C9856,
	0x7CD385C7,  0x0BD4B551,  0x92DDE4EB,  0xE5DAD47D,
	0x7BBE41DE,  0x0CB97148,  0x95B020F2,  0xE2B71064,
	0x6FBF1D91,  0x18B82D07,  0x81B17CBD,  0xF6B64C2B,
	0x68D2D988,  0x1FD5E91E,  0x86DCB8A4,  0xF1DB8832,
	0x616495A3,  0x1663A535,  0x8F6AF48F,  0xF86DC419,
	0x660951BA,  0x110E612C,  0x88073096,  0xFF000000
};

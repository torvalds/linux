/* $OpenBSD: ns8250.c,v 1.41 2025/06/12 21:04:37 dv Exp $ */
/*
 * Copyright (c) 2016 Mike Larkin <mlarkin@openbsd.org>
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
#include <sys/ttycom.h>

#include <dev/ic/comreg.h>
#include <dev/vmm/vmm.h>

#include <errno.h>
#include <event.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "atomicio.h"
#include "ns8250.h"
#include "vmd.h"

extern char *__progname;
struct ns8250_dev com1_dev;

static struct vm_dev_pipe dev_pipe;

static void com_rcv_event(int, short, void *);
static void com_rcv(struct ns8250_dev *, uint32_t, uint32_t);

/*
 * ns8250_pipe_dispatch
 *
 * Reads a message off the pipe, expecting a reguest to reset events after a
 * zero-byte read from the com device.
 */
static void
ns8250_pipe_dispatch(int fd, short event, void *arg)
{
	enum pipe_msg_type msg;

	msg = vm_pipe_recv(&dev_pipe);
	switch(msg) {
	case NS8250_RATELIMIT:
		evtimer_add(&com1_dev.rate, &com1_dev.rate_tv);
		break;
	default:
		fatalx("%s: unexpected pipe message %d", __func__, msg);
	}
}

/*
 * ratelimit
 *
 * Timeout callback function used when we have to slow down the output rate
 * from the emulated serial port.
 *
 * Parameters:
 *  fd: unused
 *  type: unused
 *  arg: unused
 */
static void
ratelimit(int fd, short type, void *arg)
{
	/* Set TXRDY and clear "no pending interrupt" */
	mutex_lock(&com1_dev.mutex);
	com1_dev.regs.iir |= IIR_TXRDY;
	com1_dev.regs.iir &= ~IIR_NOPEND;

	vcpu_assert_irq(com1_dev.vmid, 0, com1_dev.irq);
	mutex_unlock(&com1_dev.mutex);
}

void
ns8250_init(int fd, uint32_t vmid)
{
	int ret;

	memset(&com1_dev, 0, sizeof(com1_dev));
	ret = pthread_mutex_init(&com1_dev.mutex, NULL);
	if (ret) {
		errno = ret;
		fatal("could not initialize com1 mutex");
	}

	com1_dev.fd = fd;
	com1_dev.irq = 4;
	com1_dev.portid = NS8250_COM1;
	com1_dev.vmid = vmid;
	com1_dev.byte_out = 0;
	com1_dev.regs.divlo = 1;
	com1_dev.baudrate = 115200;

	/*
	 * Our serial port is essentially instantaneous, with infinite
	 * baudrate capability. To adjust for the selected baudrate,
	 * we calculate how many characters could be transmitted in a 10ms
	 * period (pause_ct) and then delay 10ms after each pause_ct sized
	 * group of characters have been transmitted. Since it takes nearly
	 * zero time to send the actual characters, the total amount of time
	 * spent is roughly equal to what it would be on real hardware.
	 *
	 * To make things simple, we don't adjust for different sized bytes
	 * (and parity, stop bits, etc) and simply assume each character
	 * output is 8 bits.
	 */
	com1_dev.pause_ct = (com1_dev.baudrate / 8) / 1000 * 10;

	event_set(&com1_dev.event, com1_dev.fd, EV_READ | EV_PERSIST,
	    com_rcv_event, (void *)(intptr_t)vmid);

	/*
	 * Whenever fd is writable implies that the pty slave is connected.
	 * Until then, avoid waiting for read events since EOF would constantly
	 * be reached.
	 */
	event_set(&com1_dev.wake, com1_dev.fd, EV_WRITE,
	    com_rcv_event, (void *)(intptr_t)vmid);
	event_add(&com1_dev.wake, NULL);

	/* Rate limiter for simulating baud rate */
	timerclear(&com1_dev.rate_tv);
	com1_dev.rate_tv.tv_usec = 10000;
	evtimer_set(&com1_dev.rate, ratelimit, NULL);

	vm_pipe_init(&dev_pipe, ns8250_pipe_dispatch);
	event_add(&dev_pipe.read_ev, NULL);
}

static void
com_rcv_event(int fd, short kind, void *arg)
{
	mutex_lock(&com1_dev.mutex);

	if (kind == EV_WRITE) {
		event_add(&com1_dev.event, NULL);
		mutex_unlock(&com1_dev.mutex);
		return;
	}

	if ((com1_dev.regs.lsr & LSR_RXRDY) == 0)
		com_rcv(&com1_dev, (uintptr_t)arg, 0);

	/* If pending interrupt, inject */
	if ((com1_dev.regs.iir & IIR_NOPEND) == 0) {
		/* XXX: vcpu_id */
		vcpu_assert_irq((uintptr_t)arg, 0, com1_dev.irq);
	}

	mutex_unlock(&com1_dev.mutex);
}

/*
 * com_rcv_handle_break
 *
 * Set/clear break detected condition based on received TIOCUCNTL_{S,C}BRK.
 */
static int
com_rcv_handle_break(struct ns8250_dev *com, uint8_t cmd)
{
	switch (cmd) {
	case 0: /* DATA */
		return 0;
	case TIOCUCNTL_SBRK:
		com->regs.lsr |= LSR_BI;
		break;
	case TIOCUCNTL_CBRK:
		com->regs.lsr &= ~LSR_BI;
		break;
	default:
		log_warnx("unexpected UCNTL ioctl: %d", cmd);
	}

	return 1;
}

/*
 * com_rcv
 *
 * Move received byte into com data register.
 * Must be called with the mutex of the com device acquired
 */
static void
com_rcv(struct ns8250_dev *com, uint32_t vm_id, uint32_t vcpu_id)
{
	char buf[2];
	ssize_t sz;

	/*
	 * Is there a new character available on com1?
	 * If so, consume the character, buffer it into the com1 data register
	 * assert IRQ4, and set the line status register RXRDY bit.
	 */
	sz = read(com->fd, buf, sizeof(buf));
	if (sz == -1) {
		/*
		 * If we get EAGAIN, we'll retry and get the character later.
		 * This error can happen when typing two characters at once
		 * at the keyboard, for example.
		 */
		if (errno != EAGAIN)
			log_warn("unexpected read error on com device");
	} else if (sz == 0) {
		/* Zero read typically occurs on a disconnect */
		event_del(&com->event);
		event_add(&com->wake, NULL);
		return;
	} else if (sz != 1 && sz != 2)
		log_warnx("unexpected read return value %zd on com device", sz);
	else {
		if (com_rcv_handle_break(com, buf[0]))
			buf[1] = 0;

		com->regs.lsr |= LSR_RXRDY;
		com->regs.data = buf[1];

		if (com->regs.ier & IER_ERXRDY) {
			com->regs.iir |= IIR_RXRDY;
			com->regs.iir &= ~IIR_NOPEND;
		}
	}
}

/*
 * vcpu_process_com_data
 *
 * Emulate in/out instructions to the com1 (ns8250) UART data register
 *
 * Parameters:
 *  vei: vm exit information from vmm(4) containing information on the in/out
 *      instruction being performed
 *
 * Return value:
 *  interrupt to inject, or 0xFF if nothing to inject
 */
uint8_t
vcpu_process_com_data(struct vm_exit *vei, uint32_t vm_id, uint32_t vcpu_id)
{
	/*
	 * vei_dir == VEI_DIR_OUT : out instruction
	 *
	 * The guest wrote to the data register. Since we are emulating a
	 * no-fifo chip, write the character immediately to the pty and
	 * assert TXRDY in IIR (if the guest has requested TXRDY interrupt
	 * reporting)
	 */
	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		if (com1_dev.regs.lcr & LCR_DLAB) {
			com1_dev.regs.divlo = vei->vei.vei_data;
			return 0xFF;
		}

		write(com1_dev.fd, &vei->vei.vei_data, 1);
		com1_dev.byte_out++;

		if (com1_dev.regs.ier & IER_ETXRDY) {
			/* Limit output rate if needed */
			if (com1_dev.pause_ct > 0 &&
			    com1_dev.byte_out % com1_dev.pause_ct == 0) {
				vm_pipe_send(&dev_pipe, NS8250_RATELIMIT);
			} else {
				/* Set TXRDY and clear "no pending interrupt" */
				com1_dev.regs.iir |= IIR_TXRDY;
				com1_dev.regs.iir &= ~IIR_NOPEND;
			}
		}
	} else {
		if (com1_dev.regs.lcr & LCR_DLAB) {
			set_return_data(vei, com1_dev.regs.divlo);
			return 0xFF;
		}
		/*
		 * vei_dir == VEI_DIR_IN : in instruction
		 *
		 * The guest read from the data register. Check to see if
		 * there is data available (RXRDY) and if so, consume the
		 * input data and return to the guest. Also clear the
		 * interrupt info register regardless.
		 */
		if (com1_dev.regs.lsr & LSR_RXRDY) {
			set_return_data(vei, com1_dev.regs.data);
			com1_dev.regs.data = 0x0;
			com1_dev.regs.lsr &= ~LSR_RXRDY;
		} else {
			set_return_data(vei, com1_dev.regs.data);
			log_debug("%s: guest reading com1 when not ready",
			    __func__);
		}

		/* Reading the data register always clears RXRDY from IIR */
		com1_dev.regs.iir &= ~IIR_RXRDY;

		/*
		 * Clear "interrupt pending" by setting IIR low bit to 1
		 * if no interrupt are pending
		 */
		if (com1_dev.regs.iir == 0x0)
			com1_dev.regs.iir = 0x1;
	}

	/* If pending interrupt, make sure it gets injected */
	if ((com1_dev.regs.iir & IIR_NOPEND) == 0)
		return (com1_dev.irq);

	return (0xFF);
}

/*
 * vcpu_process_com_lcr
 *
 * Emulate in/out instructions to the com1 (ns8250) UART line control register
 *
 * Parameters:
 *  vei: vm exit information from vmm(4) containing information on the in/out
 *      instruction being performed
 */
void
vcpu_process_com_lcr(struct vm_exit *vei)
{
	uint8_t data = (uint8_t)vei->vei.vei_data;
	uint16_t divisor;

	/*
	 * vei_dir == VEI_DIR_OUT : out instruction
	 *
	 * Write content to line control register
	 */
	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		if (com1_dev.regs.lcr & LCR_DLAB) {
			if (!(data & LCR_DLAB)) {
				if (com1_dev.regs.divlo == 0 &&
				    com1_dev.regs.divhi == 0) {
					log_warnx("%s: ignoring invalid "
					    "baudrate", __func__);
				} else {
					divisor = com1_dev.regs.divlo |
					     com1_dev.regs.divhi << 8;
					com1_dev.baudrate = 115200 / divisor;
					com1_dev.pause_ct =
					    (com1_dev.baudrate / 8) / 1000 * 10;
				}

				log_debug("%s: set baudrate = %d", __func__,
				    com1_dev.baudrate);
			}
		}
		com1_dev.regs.lcr = (uint8_t)vei->vei.vei_data;
	} else {
		/*
		 * vei_dir == VEI_DIR_IN : in instruction
		 *
		 * Read line control register
		 */
		set_return_data(vei, com1_dev.regs.lcr);
	}
}

/*
 * vcpu_process_com_iir
 *
 * Emulate in/out instructions to the com1 (ns8250) UART interrupt information
 * register. Note that writes to this register actually are to a different
 * register, the FCR (FIFO control register) that we don't emulate but still
 * consume the data provided.
 *
 * Parameters:
 *  vei: vm exit information from vmm(4) containing information on the in/out
 *      instruction being performed
 */
void
vcpu_process_com_iir(struct vm_exit *vei)
{
	/*
	 * vei_dir == VEI_DIR_OUT : out instruction
	 *
	 * Write to FCR
	 */
	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		com1_dev.regs.fcr = vei->vei.vei_data;
	} else {
		/*
		 * vei_dir == VEI_DIR_IN : in instruction
		 *
		 * Read IIR. Reading the IIR resets the TXRDY bit in the IIR
		 * after the data is read.
		 */
		set_return_data(vei, com1_dev.regs.iir);
		com1_dev.regs.iir &= ~IIR_TXRDY;

		/*
		 * Clear "interrupt pending" by setting IIR low bit to 1
		 * if no interrupts are pending
		 */
		if (com1_dev.regs.iir == 0x0)
			com1_dev.regs.iir = 0x1;
	}
}

/*
 * vcpu_process_com_mcr
 *
 * Emulate in/out instructions to the com1 (ns8250) UART modem control
 * register.
 *
 * Parameters:
 *  vei: vm exit information from vmm(4) containing information on the in/out
 *      instruction being performed
 */
void
vcpu_process_com_mcr(struct vm_exit *vei)
{
	/*
	 * vei_dir == VEI_DIR_OUT : out instruction
	 *
	 * Write to MCR
	 */
	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		com1_dev.regs.mcr = vei->vei.vei_data;
	} else {
		/*
		 * vei_dir == VEI_DIR_IN : in instruction
		 *
		 * Read from MCR
		 */
		set_return_data(vei, com1_dev.regs.mcr);
	}
}

/*
 * vcpu_process_com_lsr
 *
 * Emulate in/out instructions to the com1 (ns8250) UART line status register.
 *
 * Parameters:
 *  vei: vm exit information from vmm(4) containing information on the in/out
 *      instruction being performed
 */
void
vcpu_process_com_lsr(struct vm_exit *vei)
{
	/*
	 * vei_dir == VEI_DIR_OUT : out instruction
	 *
	 * Write to LSR. This is an illegal operation, so we just log it and
	 * continue.
	 */
	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		log_warnx("%s: LSR UART write 0x%x unsupported",
		    __progname, vei->vei.vei_data);
	} else {
		/*
		 * vei_dir == VEI_DIR_IN : in instruction
		 *
		 * Read from LSR. We always report TXRDY and TSRE since we
		 * can process output characters immediately (at any time).
		 */
		set_return_data(vei, com1_dev.regs.lsr | LSR_TSRE | LSR_TXRDY);
	}
}

/*
 * vcpu_process_com_msr
 *
 * Emulate in/out instructions to the com1 (ns8250) UART modem status register.
 *
 * Parameters:
 *  vei: vm exit information from vmm(4) containing information on the in/out
 *      instruction being performed
 */
void
vcpu_process_com_msr(struct vm_exit *vei)
{
	/*
	 * vei_dir == VEI_DIR_OUT : out instruction
	 *
	 * Write to MSR. This is an illegal operation, so we just log it and
	 * continue.
	 */
	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		log_warnx("%s: MSR UART write 0x%x unsupported",
		    __progname, vei->vei.vei_data);
	} else {
		/*
		 * vei_dir == VEI_DIR_IN : in instruction
		 *
		 * Read from MSR. We always report DCD, DSR, and CTS.
		 */
		set_return_data(vei, com1_dev.regs.lsr | MSR_DCD | MSR_DSR |
		    MSR_CTS);
	}
}

/*
 * vcpu_process_com_scr
 *
 * Emulate in/out instructions to the com1 (ns8250) UART scratch register.
 *
 * Parameters:
 *  vei: vm exit information from vmm(4) containing information on the in/out
 *      instruction being performed
 */
void
vcpu_process_com_scr(struct vm_exit *vei)
{
	/*
	 * vei_dir == VEI_DIR_OUT : out instruction
	 *
	 * The 8250 does not have a scratch register.
	 */
	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		com1_dev.regs.scr = 0xFF;
	} else {
		/*
		 * vei_dir == VEI_DIR_IN : in instruction
		 *
		 * Read from SCR
		 */
		set_return_data(vei, com1_dev.regs.scr);
	}
}

/*
 * vcpu_process_com_ier
 *
 * Emulate in/out instructions to the com1 (ns8250) UART interrupt enable
 * register.
 *
 * Parameters:
 *  vei: vm exit information from vmm(4) containing information on the in/out
 *      instruction being performed
 */
void
vcpu_process_com_ier(struct vm_exit *vei)
{
	/*
	 * vei_dir == VEI_DIR_OUT : out instruction
	 *
	 * Write to IER
	 */
	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		if (com1_dev.regs.lcr & LCR_DLAB) {
			com1_dev.regs.divhi = vei->vei.vei_data;
			return;
		}
		com1_dev.regs.ier = vei->vei.vei_data;
		if (com1_dev.regs.ier & IER_ETXRDY)
			com1_dev.regs.iir |= IIR_TXRDY;
	} else {
		if (com1_dev.regs.lcr & LCR_DLAB) {
			set_return_data(vei, com1_dev.regs.divhi);
			return;
		}
		/*
		 * vei_dir == VEI_DIR_IN : in instruction
		 *
		 * Read from IER
		 */
		set_return_data(vei, com1_dev.regs.ier);
	}
}

/*
 * vcpu_exit_com
 *
 * Process com1 (ns8250) UART exits. vmd handles most basic 8250
 * features
 *
 * Parameters:
 *  vrp: vcpu run parameters containing guest state for this exit
 *
 * Return value:
 *  Interrupt to inject to the guest VM, or 0xFF if no interrupt should
 *      be injected.
 */
uint8_t
vcpu_exit_com(struct vm_run_params *vrp)
{
	uint8_t intr = 0xFF;
	struct vm_exit *vei = vrp->vrp_exit;

	mutex_lock(&com1_dev.mutex);

	switch (vei->vei.vei_port) {
	case COM1_LCR:
		vcpu_process_com_lcr(vei);
		break;
	case COM1_IER:
		vcpu_process_com_ier(vei);
		break;
	case COM1_IIR:
		vcpu_process_com_iir(vei);
		break;
	case COM1_MCR:
		vcpu_process_com_mcr(vei);
		break;
	case COM1_LSR:
		vcpu_process_com_lsr(vei);
		break;
	case COM1_MSR:
		vcpu_process_com_msr(vei);
		break;
	case COM1_SCR:
		vcpu_process_com_scr(vei);
		break;
	case COM1_DATA:
		intr = vcpu_process_com_data(vei, vrp->vrp_vm_id,
		    vrp->vrp_vcpu_id);
		break;
	}

	mutex_unlock(&com1_dev.mutex);

	return (intr);
}

void
ns8250_stop(void)
{
	if(event_del(&com1_dev.event))
		log_warn("could not delete ns8250 event handler");
	event_del(&dev_pipe.read_ev);
	evtimer_del(&com1_dev.rate);
}

void
ns8250_start(void)
{
	event_add(&com1_dev.event, NULL);
	event_add(&com1_dev.wake, NULL);
	event_add(&dev_pipe.read_ev, NULL);
	evtimer_add(&com1_dev.rate, &com1_dev.rate_tv);
}

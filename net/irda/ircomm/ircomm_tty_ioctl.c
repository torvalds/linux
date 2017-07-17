/*********************************************************************
 *
 * Filename:      ircomm_tty_ioctl.c
 * Version:
 * Description:
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Jun 10 14:39:09 1999
 * Modified at:   Wed Jan  5 14:45:43 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1999-2000 Dag Brattli, All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 ********************************************************************/

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/serial.h>

#include <linux/uaccess.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>

#include <net/irda/ircomm_core.h>
#include <net/irda/ircomm_param.h>
#include <net/irda/ircomm_tty_attach.h>
#include <net/irda/ircomm_tty.h>

#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

/*
 * Function ircomm_tty_change_speed (driver)
 *
 *    Change speed of the driver. If the remote device is a DCE, then this
 *    should make it change the speed of its serial port
 */
static void ircomm_tty_change_speed(struct ircomm_tty_cb *self,
		struct tty_struct *tty)
{
	unsigned int cflag, cval;
	int baud;

	if (!self->ircomm)
		return;

	cflag = tty->termios.c_cflag;

	/*  byte size and parity */
	switch (cflag & CSIZE) {
	case CS5: cval = IRCOMM_WSIZE_5; break;
	case CS6: cval = IRCOMM_WSIZE_6; break;
	case CS7: cval = IRCOMM_WSIZE_7; break;
	case CS8: cval = IRCOMM_WSIZE_8; break;
	default:  cval = IRCOMM_WSIZE_5; break;
	}
	if (cflag & CSTOPB)
		cval |= IRCOMM_2_STOP_BIT;

	if (cflag & PARENB)
		cval |= IRCOMM_PARITY_ENABLE;
	if (!(cflag & PARODD))
		cval |= IRCOMM_PARITY_EVEN;

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);
	if (!baud)
		baud = 9600;	/* B0 transition handled in rs_set_termios */

	self->settings.data_rate = baud;
	ircomm_param_request(self, IRCOMM_DATA_RATE, FALSE);

	/* CTS flow control flag and modem status interrupts */
	tty_port_set_cts_flow(&self->port, cflag & CRTSCTS);
	if (cflag & CRTSCTS) {
		self->settings.flow_control |= IRCOMM_RTS_CTS_IN;
		/* This got me. Bummer. Jean II */
		if (self->service_type == IRCOMM_3_WIRE_RAW)
			net_warn_ratelimited("%s(), enabling RTS/CTS on link that doesn't support it (3-wire-raw)\n",
					     __func__);
	} else {
		self->settings.flow_control &= ~IRCOMM_RTS_CTS_IN;
	}
	tty_port_set_check_carrier(&self->port, ~cflag & CLOCAL);

	self->settings.data_format = cval;

	ircomm_param_request(self, IRCOMM_DATA_FORMAT, FALSE);
	ircomm_param_request(self, IRCOMM_FLOW_CONTROL, TRUE);
}

/*
 * Function ircomm_tty_set_termios (tty, old_termios)
 *
 *    This routine allows the tty driver to be notified when device's
 *    termios settings have changed.  Note that a well-designed tty driver
 *    should be prepared to accept the case where old == NULL, and try to
 *    do something rational.
 */
void ircomm_tty_set_termios(struct tty_struct *tty,
			    struct ktermios *old_termios)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	unsigned int cflag = tty->termios.c_cflag;

	if ((cflag == old_termios->c_cflag) &&
	    (RELEVANT_IFLAG(tty->termios.c_iflag) ==
	     RELEVANT_IFLAG(old_termios->c_iflag)))
	{
		return;
	}

	ircomm_tty_change_speed(self, tty);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) && !(cflag & CBAUD)) {
		self->settings.dte &= ~(IRCOMM_DTR|IRCOMM_RTS);
		ircomm_param_request(self, IRCOMM_DTE, TRUE);
	}

	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) && (cflag & CBAUD)) {
		self->settings.dte |= IRCOMM_DTR;
		if (!C_CRTSCTS(tty) || !tty_throttled(tty))
			self->settings.dte |= IRCOMM_RTS;
		ircomm_param_request(self, IRCOMM_DTE, TRUE);
	}

	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) && !C_CRTSCTS(tty))
	{
		tty->hw_stopped = 0;
		ircomm_tty_start(tty);
	}
}

/*
 * Function ircomm_tty_tiocmget (tty)
 *
 *
 *
 */
int ircomm_tty_tiocmget(struct tty_struct *tty)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	unsigned int result;

	if (tty_io_error(tty))
		return -EIO;

	result =  ((self->settings.dte & IRCOMM_RTS) ? TIOCM_RTS : 0)
		| ((self->settings.dte & IRCOMM_DTR) ? TIOCM_DTR : 0)
		| ((self->settings.dce & IRCOMM_CD)  ? TIOCM_CAR : 0)
		| ((self->settings.dce & IRCOMM_RI)  ? TIOCM_RNG : 0)
		| ((self->settings.dce & IRCOMM_DSR) ? TIOCM_DSR : 0)
		| ((self->settings.dce & IRCOMM_CTS) ? TIOCM_CTS : 0);
	return result;
}

/*
 * Function ircomm_tty_tiocmset (tty, set, clear)
 *
 *
 *
 */
int ircomm_tty_tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;

	if (tty_io_error(tty))
		return -EIO;

	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRCOMM_TTY_MAGIC, return -1;);

	if (set & TIOCM_RTS)
		self->settings.dte |= IRCOMM_RTS;
	if (set & TIOCM_DTR)
		self->settings.dte |= IRCOMM_DTR;

	if (clear & TIOCM_RTS)
		self->settings.dte &= ~IRCOMM_RTS;
	if (clear & TIOCM_DTR)
		self->settings.dte &= ~IRCOMM_DTR;

	if ((set|clear) & TIOCM_RTS)
		self->settings.dte |= IRCOMM_DELTA_RTS;
	if ((set|clear) & TIOCM_DTR)
		self->settings.dte |= IRCOMM_DELTA_DTR;

	ircomm_param_request(self, IRCOMM_DTE, TRUE);

	return 0;
}

/*
 * Function get_serial_info (driver, retinfo)
 *
 *
 *
 */
static int ircomm_tty_get_serial_info(struct ircomm_tty_cb *self,
				      struct serial_struct __user *retinfo)
{
	struct serial_struct info;

	memset(&info, 0, sizeof(info));
	info.line = self->line;
	info.flags = self->port.flags;
	info.baud_base = self->settings.data_rate;
	info.close_delay = self->port.close_delay;
	info.closing_wait = self->port.closing_wait;

	/* For compatibility  */
	info.type = PORT_16550A;

	if (copy_to_user(retinfo, &info, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}

/*
 * Function set_serial_info (driver, new_info)
 *
 *
 *
 */
static int ircomm_tty_set_serial_info(struct ircomm_tty_cb *self,
				      struct serial_struct __user *new_info)
{
	return 0;
}

/*
 * Function ircomm_tty_ioctl (tty, cmd, arg)
 *
 *
 *
 */
int ircomm_tty_ioctl(struct tty_struct *tty,
		     unsigned int cmd, unsigned long arg)
{
	struct ircomm_tty_cb *self = (struct ircomm_tty_cb *) tty->driver_data;
	int ret = 0;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty_io_error(tty))
		    return -EIO;
	}

	switch (cmd) {
	case TIOCGSERIAL:
		ret = ircomm_tty_get_serial_info(self, (struct serial_struct __user *) arg);
		break;
	case TIOCSSERIAL:
		ret = ircomm_tty_set_serial_info(self, (struct serial_struct __user *) arg);
		break;
	case TIOCMIWAIT:
		pr_debug("(), TIOCMIWAIT, not impl!\n");
		break;

	case TIOCGICOUNT:
		pr_debug("%s(), TIOCGICOUNT not impl!\n", __func__);
		return 0;
	default:
		ret = -ENOIOCTLCMD;  /* ioctls which we must ignore */
	}
	return ret;
}




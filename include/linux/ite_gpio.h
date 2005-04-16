/*
 * FILE NAME ite_gpio.h
 *
 * BRIEF MODULE DESCRIPTION
 *	Generic gpio.
 *
 *  Author: MontaVista Software, Inc.  <source@mvista.com>
 *          Hai-Pao Fan <haipao@mvista.com>
 *
 * Copyright 2001 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ITE_GPIO_H
#define __ITE_GPIO_H

#include <linux/ioctl.h>

struct ite_gpio_ioctl_data {
	__u32 device;
	__u32 mask;
	__u32 data;
};

#define ITE_GPIO_IOCTL_BASE	'Z'

#define ITE_GPIO_IN		_IOWR(ITE_GPIO_IOCTL_BASE, 0, struct ite_gpio_ioctl_data)
#define ITE_GPIO_OUT		_IOW (ITE_GPIO_IOCTL_BASE, 1, struct ite_gpio_ioctl_data)
#define	ITE_GPIO_INT_CTRL	_IOW (ITE_GPIO_IOCTL_BASE, 2, struct ite_gpio_ioctl_data)
#define	ITE_GPIO_IN_STATUS	_IOW (ITE_GPIO_IOCTL_BASE, 3, struct ite_gpio_ioctl_data)
#define	ITE_GPIO_OUT_STATUS	_IOW (ITE_GPIO_IOCTL_BASE, 4, struct ite_gpio_ioctl_data)
#define ITE_GPIO_GEN_CTRL	_IOW (ITE_GPIO_IOCTL_BASE, 5, struct ite_gpio_ioctl_data)
#define ITE_GPIO_INT_WAIT	_IOW (ITE_GPIO_IOCTL_BASE, 6, struct ite_gpio_ioctl_data)

#define	ITE_GPIO_PORTA	0x01
#define	ITE_GPIO_PORTB	0x02
#define	ITE_GPIO_PORTC	0x04

extern int ite_gpio_in(__u32 device, __u32 mask, volatile __u32 *data);
extern int ite_gpio_out(__u32 device, __u32 mask, __u32 data);
extern int ite_gpio_int_ctrl(__u32 device, __u32 mask, __u32 data);
extern int ite_gpio_in_status(__u32 device, __u32 mask, volatile __u32 *data);
extern int ite_gpio_out_status(__u32 device, __u32 mask, __u32 data);
extern int ite_gpio_gen_ctrl(__u32 device, __u32 mask, __u32 data);
extern int ite_gpio_int_wait(__u32 device, __u32 mask, __u32 data);

#endif

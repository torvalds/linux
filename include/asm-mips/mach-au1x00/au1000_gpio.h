/*
 * FILE NAME au1000_gpio.h
 *
 * BRIEF MODULE DESCRIPTION
 *	API to Alchemy Au1000 GPIO device.
 *
 *  Author: MontaVista Software, Inc.  <source@mvista.com>
 *          Steve Longerbeam <stevel@mvista.com>
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

#ifndef __AU1000_GPIO_H
#define __AU1000_GPIO_H

#include <linux/ioctl.h>

#define AU1000GPIO_IOC_MAGIC 'A'

#define AU1000GPIO_IN		_IOR (AU1000GPIO_IOC_MAGIC, 0, int)
#define AU1000GPIO_SET		_IOW (AU1000GPIO_IOC_MAGIC, 1, int)
#define AU1000GPIO_CLEAR	_IOW (AU1000GPIO_IOC_MAGIC, 2, int)
#define AU1000GPIO_OUT		_IOW (AU1000GPIO_IOC_MAGIC, 3, int)
#define AU1000GPIO_TRISTATE	_IOW (AU1000GPIO_IOC_MAGIC, 4, int)
#define AU1000GPIO_AVAIL_MASK	_IOR (AU1000GPIO_IOC_MAGIC, 5, int)

#ifdef __KERNEL__
extern u32 get_au1000_avail_gpio_mask(void);
extern int au1000gpio_tristate(u32 data);
extern int au1000gpio_in(u32 *data);
extern int au1000gpio_set(u32 data);
extern int au1000gpio_clear(u32 data);
extern int au1000gpio_out(u32 data);
#endif

#endif

/*
 *  DaVinci Power & Sleep Controller (PSC) defines
 *
 *  Copyright (C) 2006 Texas Instruments.
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
 *
 */
#ifndef __ASM_ARCH_PSC_H
#define __ASM_ARCH_PSC_H

/* Power and Sleep Controller (PSC) Domains */
#define DAVINCI_GPSC_ARMDOMAIN      0
#define DAVINCI_GPSC_DSPDOMAIN      1

#define DAVINCI_LPSC_VPSSMSTR       0
#define DAVINCI_LPSC_VPSSSLV        1
#define DAVINCI_LPSC_TPCC           2
#define DAVINCI_LPSC_TPTC0          3
#define DAVINCI_LPSC_TPTC1          4
#define DAVINCI_LPSC_EMAC           5
#define DAVINCI_LPSC_EMAC_WRAPPER   6
#define DAVINCI_LPSC_MDIO           7
#define DAVINCI_LPSC_IEEE1394       8
#define DAVINCI_LPSC_USB            9
#define DAVINCI_LPSC_ATA            10
#define DAVINCI_LPSC_VLYNQ          11
#define DAVINCI_LPSC_UHPI           12
#define DAVINCI_LPSC_DDR_EMIF       13
#define DAVINCI_LPSC_AEMIF          14
#define DAVINCI_LPSC_MMC_SD         15
#define DAVINCI_LPSC_MEMSTICK       16
#define DAVINCI_LPSC_McBSP          17
#define DAVINCI_LPSC_I2C            18
#define DAVINCI_LPSC_UART0          19
#define DAVINCI_LPSC_UART1          20
#define DAVINCI_LPSC_UART2          21
#define DAVINCI_LPSC_SPI            22
#define DAVINCI_LPSC_PWM0           23
#define DAVINCI_LPSC_PWM1           24
#define DAVINCI_LPSC_PWM2           25
#define DAVINCI_LPSC_GPIO           26
#define DAVINCI_LPSC_TIMER0         27
#define DAVINCI_LPSC_TIMER1         28
#define DAVINCI_LPSC_TIMER2         29
#define DAVINCI_LPSC_SYSTEM_SUBSYS  30
#define DAVINCI_LPSC_ARM            31
#define DAVINCI_LPSC_SCR2           32
#define DAVINCI_LPSC_SCR3           33
#define DAVINCI_LPSC_SCR4           34
#define DAVINCI_LPSC_CROSSBAR       35
#define DAVINCI_LPSC_CFG27          36
#define DAVINCI_LPSC_CFG3           37
#define DAVINCI_LPSC_CFG5           38
#define DAVINCI_LPSC_GEM            39
#define DAVINCI_LPSC_IMCOP          40

#endif /* __ASM_ARCH_PSC_H */

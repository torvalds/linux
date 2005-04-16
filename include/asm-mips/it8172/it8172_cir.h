/*
 *
 * BRIEF MODULE DESCRIPTION
 *	IT8172 Consumer IR port defines.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
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

#define NUM_CIR_PORTS 2

/* Master Control Register */
#define CIR_RESET              0x1
#define CIR_FIFO_CLEAR         0x2
#define CIR_SET_FIFO_TL(x)     (((x)&0x3)<<2)
#define CIR_ILE                0x10
#define CIR_ILSEL              0x20

/* Interrupt Enable Register */
#define CIR_TLDLIE             0x1
#define CIR_RDAIE              0x2
#define CIR_RFOIE              0x4
#define CIR_IEC                0x80

/* Interrupt Identification Register */
#define CIR_TLDLI              0x1
#define CIR_RDAI               0x2
#define CIR_RFOI               0x4
#define CIR_NIP                0x80

/* Carrier Frequency Register */
#define CIR_SET_CF(x)          ((x)&0x1f)
  #define CFQ_38_480           0xB       /* 38 KHz low, 480 KHz high */
#define CIR_HCFS               0x20
  #define CIR_SET_HS(x)        (((x)&0x1)<<5)


/* Receiver Control Register */
#define CIR_SET_RXDCR(x)       ((x)&0x7)
#define CIR_RXACT              0x8
#define CIR_RXEND              0x10
#define CIR_RDWOS              0x20
  #define CIR_SET_RDWOS(x)     (((x)&0x1)<<5)
#define CIR_RXEN               0x80

/* Transmitter Control Register */
#define CIR_SET_TXMPW(x)       ((x)&0x7)
#define CIR_SET_TXMPM(x)       (((x)&0x3)<<3)
#define CIR_TXENDF             0x20
#define CIR_TXRLE              0x40

/* Receiver FIFO Status Register */
#define CIR_RXFBC_MASK         0x3f
#define CIR_RXFTO              0x80

/* Wakeup Code Length Register */
#define CIR_SET_WCL            ((x)&0x3f)
#define CIR_WCL_MASK(x)        ((x)&0x3f)

/* Wakeup Power Control/Status Register */
#define CIR_BTMON              0x2
#define CIR_CIRON              0x4
#define CIR_RCRST              0x10
#define CIR_WCRST              0x20

struct cir_port {
	int port;
	unsigned short baud_rate;
	unsigned char fifo_tl;
	unsigned char cfq;
	unsigned char hcfs;
	unsigned char rdwos;
	unsigned char rxdcr;
};

struct it8172_cir_regs {
	unsigned char dr;       /* data                        */
	char pad;
	unsigned char mstcr;    /* master control              */
	char pad1;
	unsigned char ier;      /* interrupt enable            */
	char pad2;
	unsigned char iir;      /* interrupt identification    */
	char pad3;
	unsigned char cfr;      /* carrier frequency           */
	char pad4;
	unsigned char rcr;      /* receiver control            */
	char pad5;
	unsigned char tcr;      /* transmitter control         */
	char pad6;
	char pad7;
	char pad8;
	unsigned char bdlr;     /* baud rate divisor low byte  */
	char pad9;
	unsigned char bdhr;     /* baud rate divisor high byte */
	char pad10;
	unsigned char tfsr;     /* tx fifo byte count          */
	char pad11;
	unsigned char rfsr;     /* rx fifo status              */
	char pad12;
	unsigned char wcl;      /* wakeup code length          */
	char pad13;
	unsigned char wcr;      /* wakeup code read/write      */
	char pad14;
	unsigned char wps;      /* wakeup power control/status */
};

int cir_port_init(struct cir_port *cir);
extern void clear_fifo(struct cir_port *cir);
extern void enable_receiver(struct cir_port *cir);
extern void disable_receiver(struct cir_port *cir);
extern void enable_rx_demodulation(struct cir_port *cir);
extern void disable_rx_demodulation(struct cir_port *cir);
extern void set_rx_active(struct cir_port *cir);
extern void int_enable(struct cir_port *cir);
extern void rx_int_enable(struct cir_port *cir);
extern char get_int_status(struct cir_port *cir);
extern int cir_get_rx_count(struct cir_port *cir);
extern char cir_read_data(struct cir_port *cir);

/*
 *
 * BRIEF MODULE DESCRIPTION
 *	IT8172 system controller specific pci defines.
 *
 * Copyright 2000 MontaVista Software Inc.
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

#ifndef _8172PCI_H_
#define _8172PCI_H_

// PCI configuration space Type0
#define	PCI_IDREG	0x00
#define	PCI_CMDSTSREG	0x04
#define	PCI_CLASSREG	0x08
#define	PCI_BHLCREG	0x0C
#define	PCI_BASE1REG	0x10
#define	PCI_BASE2REG	0x14
#define	PCI_BASE3REG	0x18
#define	PCI_BASE4REG	0x1C
#define	PCI_BASE5REG	0x20
#define	PCI_BASE6REG	0x24
#define	PCI_ROMBASEREG	0x30
#define	PCI_INTRREG	0x3C

// PCI configuration space Type1
#define	PCI_BUSNOREG	0x18

#define	IT_PCI_VENDORID(x)	((x) & 0xFFFF)
#define	IT_PCI_DEVICEID(x)	(((x)>>16) & 0xFFFF)

// Command register
#define	PCI_CMD_IOEN		0x00000001
#define	PCI_CMD_MEMEN		0x00000002
#define	PCI_CMD_BUSMASTER	0x00000004
#define	PCI_CMD_SPCYCLE		0x00000008
#define	PCI_CMD_WRINV		0x00000010
#define	PCI_CMD_VGASNOOP	0x00000020
#define	PCI_CMD_PERR		0x00000040
#define	PCI_CMD_WAITCTRL	0x00000080
#define	PCI_CMD_SERR		0x00000100
#define	PCI_CMD_FAST_BACKTOBACK	0x00000200

// Status register
#define	PCI_STS_66MHZ		0x00200000
#define	PCI_STS_SUPPORT_UDF	0x00400000
#define	PCI_STS_FAST_BACKTOBACK	0x00800000
#define	PCI_STS_DATA_PERR	0x01000000
#define	PCI_STS_DEVSEL0		0x02000000
#define	PCI_STS_DEVSEL1		0x04000000
#define	PCI_STS_SIG_TGTABORT	0x08000000
#define	PCI_STS_RCV_TGTABORT	0x10000000
#define	PCI_STS_RCV_MSTABORT	0x20000000
#define	PCI_STS_SYSERR		0x40000000
#define	PCI_STS_DETCT_PERR	0x80000000

#define	IT_PCI_CLASS(x)		(((x)>>24) & 0xFF)
#define	IT_PCI_SUBCLASS(x)		(((x)>>16) & 0xFF)
#define	IT_PCI_INTERFACE(x)	(((x)>>8) & 0xFF)
#define	IT_PCI_REVISION(x)		((x) & 0xFF)

// PCI class code
#define	PCI_CLASS_BRIDGE			0x06

// bridge subclass
#define	PCI_SUBCLASS_BRIDGE_HOST		0x00
#define	PCI_SUBCLASS_BRIDGE_PCI			0x04

// BHLCREG
#define	IT_PCI_BIST(x)		(((x)>>24) & 0xFF)
#define	IT_PCI_HEADERTYPE(x)	(((x)>>16) & 0xFF)
#define	IT_PCI_LATENCYTIMER(x)	(((x)>>8) & 0xFF)
#define	IT_PCI_CACHELINESIZE(x)	((x) & 0xFF)

#define	PCI_MULTIFUNC	0x80

// INTRREG
#define	IT_PCI_MAXLAT(x)		(((x)>>24) & 0xFF)
#define	IT_PCI_MINGNT(x)		(((x)>>16) & 0xFF)
#define	IT_PCI_INTRPIN(x)		(((x)>>8) & 0xFF)
#define	IT_PCI_INTRLINE(x)		((x) & 0xFF)

#define	PCI_VENDOR_NEC	0x1033
#define PCI_VENDOR_DEC	0x1101

#endif // _8172PCI_H_

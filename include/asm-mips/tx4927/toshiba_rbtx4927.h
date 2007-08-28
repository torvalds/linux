/*
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * Copyright 2001-2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __ASM_TX4927_TOSHIBA_RBTX4927_H
#define __ASM_TX4927_TOSHIBA_RBTX4927_H

#include <asm/tx4927/tx4927.h>
#include <asm/tx4927/tx4927_mips.h>
#ifdef CONFIG_PCI
#include <asm/tx4927/tx4927_pci.h>
#endif

#define TOSHIBA_RBTX4927_WR08(a,b) do { TX4927_WR08(a,b); wbflush(); } while ( 0 )


#ifdef CONFIG_PCI
#define TBTX4927_ISA_IO_OFFSET TX4927_PCIIO
#else
#define TBTX4927_ISA_IO_OFFSET 0
#endif

#define RBTX4927_SW_RESET_DO         0xbc00f000
#define RBTX4927_SW_RESET_DO_SET                0x01

#define RBTX4927_SW_RESET_ENABLE     0xbc00f002
#define RBTX4927_SW_RESET_ENABLE_SET            0x01


#define RBTX4927_RTL_8019_BASE (0x1c020280-TBTX4927_ISA_IO_OFFSET)
#define RBTX4927_RTL_8019_IRQ  (TX4927_IRQ_PIC_BEG + 5)

int toshiba_rbtx4927_irq_nested(int sw_irq);

#endif /* __ASM_TX4927_TOSHIBA_RBTX4927_H */

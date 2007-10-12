/*
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * Copyright 2001-2006 MontaVista Software Inc.
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
#ifndef __ASM_TX4927_TX4927_H
#define __ASM_TX4927_TX4927_H

#include <asm/txx9irq.h>

#define TX4927_IRQ_CP0_BEG  MIPS_CPU_IRQ_BASE
#define TX4927_IRQ_CP0_END  (MIPS_CPU_IRQ_BASE + 8 - 1)

#define TX4927_IRQ_PIC_BEG  TXX9_IRQ_BASE
#define TX4927_IRQ_PIC_END  (TXX9_IRQ_BASE + TXx9_MAX_IR - 1)


#define TX4927_IRQ_USER0            (TX4927_IRQ_CP0_BEG+0)
#define TX4927_IRQ_USER1            (TX4927_IRQ_CP0_BEG+1)
#define TX4927_IRQ_NEST_PIC_ON_CP0  (TX4927_IRQ_CP0_BEG+2)
#define TX4927_IRQ_CPU_TIMER        (TX4927_IRQ_CP0_BEG+7)

#define TX4927_IRQ_NEST_EXT_ON_PIC  (TX4927_IRQ_PIC_BEG+3)

#endif /* __ASM_TX4927_TX4927_H */

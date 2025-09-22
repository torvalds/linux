/*	$OpenBSD: asioreg.h,v 1.3 2003/06/02 18:32:41 jason Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#define	ASIO_CSR		0	/* bus space offset */
/*
 * As a feature, different board revisions 's' and 'sj' define the
 * interrupt enables differently.
 */
#define	ASIO_CSR_SBUS_INT7	0x80	/* sbus interrupt 7 */
#define	ASIO_CSR_SBUS_INT6	0x40	/* sbus interrupt 6 */
#define	ASIO_CSR_SBUS_INT5	0x20	/* sbus interrupt 5 */
#define	ASIO_CSR_S_PAR_INTEN	0x08	/* parallel interrupt enable */
#define	ASIO_CSR_SJ_UART0_INTEN	0x08	/* sj: uart0 interrupt enable */
#define	ASIO_CSR_UART1_INTEN	0x04	/* uart1 interrupt enable */
#define	ASIO_CSR_S_UART0_INTEN	0x02	/* s: uart0 interrupt enable */
#define	ASIO_CSR_SJ_PAR_INTEN	0x02	/* sj: parallel interrupt enable */
#define	ASIO_CSR_LPTOE		0x01

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Nicolas Souchu
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __PPBIO_H
#define __PPBIO_H

/*
 * Set of ppbus i/o routines callable from ppbus device drivers
 */

#define ppb_outsb_epp(dev,buf,cnt)					    \
		(PPBUS_IO(device_get_parent(dev), PPB_OUTSB_EPP, buf, cnt, 0))
#define ppb_outsw_epp(dev,buf,cnt)					    \
		(PPBUS_IO(device_get_parent(dev), PPB_OUTSW_EPP, buf, cnt, 0))
#define ppb_outsl_epp(dev,buf,cnt)					    \
		(PPBUS_IO(device_get_parent(dev), PPB_OUTSL_EPP, buf, cnt, 0))

#define ppb_insb_epp(dev,buf,cnt)					    \
		(PPBUS_IO(device_get_parent(dev), PPB_INSB_EPP, buf, cnt, 0))
#define ppb_insw_epp(dev,buf,cnt)					    \
		(PPBUS_IO(device_get_parent(dev), PPB_INSW_EPP, buf, cnt, 0))
#define ppb_insl_epp(dev,buf,cnt)					    \
		(PPBUS_IO(device_get_parent(dev), PPB_INSL_EPP, buf, cnt, 0))

#define ppb_repp_A(dev) 						    \
		(PPBUS_IO(device_get_parent(dev), PPB_REPP_A, 0, 0, 0))
#define ppb_repp_D(dev) 						    \
		(PPBUS_IO(device_get_parent(dev), PPB_REPP_D, 0, 0, 0))
#define ppb_recr(dev)						    	    \
		(PPBUS_IO(device_get_parent(dev), PPB_RECR, 0, 0, 0))
#define ppb_rfifo(dev)							    \
		(PPBUS_IO(device_get_parent(dev), PPB_RFIFO, 0, 0, 0))

#define ppb_wepp_A(dev,byte)						    \
		(PPBUS_IO(device_get_parent(dev), PPB_WEPP_A, 0, 0, byte))
#define ppb_wepp_D(dev,byte)						    \
		(PPBUS_IO(device_get_parent(dev), PPB_WEPP_D, 0, 0, byte))
#define ppb_wecr(dev,byte)						    \
		(PPBUS_IO(device_get_parent(dev), PPB_WECR, 0, 0, byte))
#define ppb_wfifo(dev,byte)						    \
		(PPBUS_IO(device_get_parent(dev), PPB_WFIFO, 0, 0, byte))

#define ppb_rdtr(dev)							    \
		(PPBUS_IO(device_get_parent(dev), PPB_RDTR, 0, 0, 0))
#define ppb_rstr(dev)							    \
		(PPBUS_IO(device_get_parent(dev), PPB_RSTR, 0, 0, 0))
#define ppb_rctr(dev)							    \
		(PPBUS_IO(device_get_parent(dev), PPB_RCTR, 0, 0, 0))

#define ppb_wdtr(dev,byte)						    \
		(PPBUS_IO(device_get_parent(dev), PPB_WDTR, 0, 0, byte))
#define ppb_wstr(dev,byte)						    \
		(PPBUS_IO(device_get_parent(dev), PPB_WSTR, 0, 0, byte))
#define ppb_wctr(dev,byte)						    \
		(PPBUS_IO(device_get_parent(dev), PPB_WCTR, 0, 0, byte))

#endif

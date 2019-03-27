/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Authors: Joe Kloss; Ravi Pokala (rpokala@freebsd.org)
 *
 * Copyright (c) 2017-2018 Panasas
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
 */

#ifndef _DEV__IMCSMB__IMCSMB_REG_H_
#define _DEV__IMCSMB__IMCSMB_REG_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/syslog.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/atomic.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/smbus/smbconf.h>

/* Intel (Sandy,Ivy)bridge and (Has,Broad)well CPUs have integrated memory
 * controllers (iMCs), each of which having up to two SMBus controllers. They
 * are programmed via sets of registers in the same PCI device, which are
 * identical other than the register numbers.
 *
 * The full documentation for these registers can be found in volume two of the
 * datasheets for the CPUs. Refer to the links in imcsmb_pci.c
 */

#define	IMCSMB_REG_STATUS0			0x0180
#define	IMCSMB_REG_STATUS1			0x0190
#define		IMCSMB_STATUS_BUSY_BIT		0x10000000
#define		IMCSMB_STATUS_BUS_ERROR_BIT	0x20000000
#define		IMCSMB_STATUS_WRITE_DATA_DONE	0x40000000
#define		IMCSMB_STATUS_READ_DATA_VALID	0x80000000

#define	IMCSMB_REG_COMMAND0			0x0184
#define	IMCSMB_REG_COMMAND1			0x0194
#define		IMCSMB_CMD_WORD_ACCESS		0x20000000
#define		IMCSMB_CMD_WRITE_BIT		0x08000000
#define		IMCSMB_CMD_TRIGGER_BIT		0x80000000

#define	IMCSMB_REG_CONTROL0			0x0188
#define	IMCSMB_REG_CONTROL1			0x0198
#define		IMCSMB_CNTL_POLL_EN		0x00000100
#define		IMCSMB_CNTL_CLK_OVERRIDE	0x08000000
#define		IMCSMB_CNTL_DTI_MASK		0xf0000000
#define		IMCSMB_CNTL_WRITE_DISABLE_BIT	0x04000000

#endif /* _DEV__IMCSMB__IMCSMB_REG_H_ */

/* vi: set ts=8 sw=4 sts=8 noet: */

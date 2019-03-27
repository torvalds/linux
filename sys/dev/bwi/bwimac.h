/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/netif/bwi/bwimac.h,v 1.2 2008/02/15 11:15:38 sephe Exp $
 * $FreeBSD$
 */

#ifndef _BWI_MAC_H
#define _BWI_MAC_H

int		bwi_mac_attach(struct bwi_softc *, int, uint8_t);
int		bwi_mac_lateattach(struct bwi_mac *);
void		bwi_mac_detach(struct bwi_mac *);
int		bwi_mac_init(struct bwi_mac *);
void		bwi_mac_reset(struct bwi_mac *, int);
int		bwi_mac_start(struct bwi_mac *);
int		bwi_mac_stop(struct bwi_mac *);
void		bwi_mac_shutdown(struct bwi_mac *);
void		bwi_mac_updateslot(struct bwi_mac *, int);
void		bwi_mac_set_promisc(struct bwi_mac *, int);

void		bwi_mac_calibrate_txpower(struct bwi_mac *,
					  enum bwi_txpwrcb_type);
void		bwi_mac_set_tpctl_11bg(struct bwi_mac *,
				       const struct bwi_tpctl *);
void		bwi_mac_init_tpctl_11bg(struct bwi_mac *);
void		bwi_mac_dummy_xmit(struct bwi_mac *);
void		bwi_mac_reset_hwkeys(struct bwi_mac *);
int		bwi_mac_config_ps(struct bwi_mac *);
int		bwi_mac_fw_alloc(struct bwi_mac *);

uint16_t	bwi_memobj_read_2(struct bwi_mac *, uint16_t, uint16_t);
uint32_t	bwi_memobj_read_4(struct bwi_mac *, uint16_t, uint16_t);
void		bwi_memobj_write_2(struct bwi_mac *, uint16_t, uint16_t,
				   uint16_t);
void		bwi_memobj_write_4(struct bwi_mac *, uint16_t, uint16_t,
				   uint32_t);
void		bwi_tmplt_write_4(struct bwi_mac *, uint32_t, uint32_t);
void		bwi_hostflags_write(struct bwi_mac *, uint64_t);
uint64_t	bwi_hostflags_read(struct bwi_mac *);

#define MOBJ_WRITE_2(mac, objid, ofs, val)	\
	bwi_memobj_write_2((mac), (objid), (ofs), (val))
#define MOBJ_WRITE_4(mac, objid, ofs, val)	\
	bwi_memobj_write_4((mac), (objid), (ofs), (val))
#define MOBJ_READ_2(mac, objid, ofs)		\
	bwi_memobj_read_2((mac), (objid), (ofs))
#define MOBJ_READ_4(mac, objid, ofs)		\
	bwi_memobj_read_4((mac), (objid), (ofs))

#define MOBJ_SETBITS_4(mac, objid, ofs, bits)	\
	MOBJ_WRITE_4((mac), (objid), (ofs),	\
		     MOBJ_READ_4((mac), (objid), (ofs)) | (bits))
#define MOBJ_CLRBITS_4(mac, objid, ofs, bits)	\
	MOBJ_WRITE_4((mac), (objid), (ofs),	\
		     MOBJ_READ_4((mac), (objid), (ofs)) & ~(bits))

#define MOBJ_FILT_SETBITS_2(mac, objid, ofs, filt, bits) \
	MOBJ_WRITE_2((mac), (objid), (ofs),	\
		     (MOBJ_READ_2((mac), (objid), (ofs)) & (filt)) | (bits))

#define TMPLT_WRITE_4(mac, ofs, val)	bwi_tmplt_write_4((mac), (ofs), (val))

#define HFLAGS_WRITE(mac, flags)	bwi_hostflags_write((mac), (flags))
#define HFLAGS_READ(mac)		bwi_hostflags_read((mac))
#define HFLAGS_CLRBITS(mac, bits)	\
	HFLAGS_WRITE((mac), HFLAGS_READ((mac)) | (bits))
#define HFLAGS_SETBITS(mac, bits)	\
	HFLAGS_WRITE((mac), HFLAGS_READ((mac)) & ~(bits))

#endif	/* !_BWI_MAC_H */

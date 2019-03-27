/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2003
 * 	Hidetoshi Shimokawa. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

/*
 * IEEE 1394a
 * Figure 5B - 1
 */
struct phyreg_base {
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t	phy_id:6,
		r:1,
		cps:1;
	uint8_t	rhb:1,
		ibr:1,
		gap_count:6;
	uint8_t	extended:3,
		num_ports:5;
	uint8_t	phy_speed:3,
		:1,
		delay:4;
	uint8_t	lctrl:1,
		c:1,
		jitter:3,
		pwr_class:3;
	uint8_t	wdie:1,
		isbr:1,
		ctoi:1,
		cpsi:1,
		stoi:1,
		pei:1,
		eaa:1,
		emc:1;
	uint8_t	legacy_spd:3,
		blink:1,
		bridge:2,
		:2;
	uint8_t	page_select:3,
		:1,
		port_select:4;
#else
	uint8_t	cps:1,
		r:1,
		phy_id:6;
	uint8_t	gap_count:6,
		ibr:1,
		rhb:1;
	uint8_t	num_ports:5,
		extended:3;
	uint8_t	delay:4,
		:1,
		phy_speed:3;
	uint8_t	pwr_class:3,
		jitter:3,
		c:1,
		lctrl:1;
	uint8_t	emc:1,
		eaa:1,
		pei:1,
		stoi:1,
		cpsi:1,
		ctoi:1,
		isbr:1,
		wdie:1;
	uint8_t	:2,
		bridge:2,
		blink:1,
		legacy_spd:3;
	uint8_t	port_select:4,
		:1,
		page_select:3;
#endif
};

/*
 * IEEE 1394a
 * Figure 5B - 2
 */
struct phyreg_page0 {
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t	astat:2,
		bstat:2,
		ch:1,
		con:1,
		rxok:1,
		dis:1;
	uint8_t	negotiated_speed:3,
		pie:1,
		fault:1,
		stanby_fault:1,
		disscrm:1,
		b_only:1;
	uint8_t	dc_connected:1,
		max_port_speed:3,
		lpp:1,
		cable_speed:3;
	uint8_t	connection_unreliable:1,
		:3,
		beta_mode:1,
		:3;
	uint8_t	port_error;
	uint8_t	:5,
		loop_disable:1,
		in_standby:1,
		hard_disable:1;
	uint8_t	:8;
	uint8_t	:8;
#else
	uint8_t	dis:1,
		rxok:1,
		con:1,
		ch:1,
		bstat:2,
		astat:2;
	uint8_t	b_only:1,
		disscrm:1,
		stanby_fault:1,
		fault:1,
		pie:1,
		negotiated_speed:3;
	uint8_t	cable_speed:3,
		lpp:1,
		max_port_speed:3,
		dc_connected:1;
	uint8_t	:3,
		beta_mode:1,
		:3,
		connection_unreliable:1;
	uint8_t	port_error;
	uint8_t	hard_disable:1,
		in_standby:1,
		loop_disable:1,
		:5;
	uint8_t	:8;
	uint8_t	:8;
#endif
};

/* 
 * IEEE 1394a
 * Figure 5B - 3
 */
struct phyreg_page1 {
	uint8_t	compliance;
	uint8_t	:8;
	uint8_t	vendor_id[3];
	uint8_t	product_id[3];
};

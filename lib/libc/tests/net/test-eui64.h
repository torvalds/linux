/*
 * Copyright 2004 The Aerospace Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions, and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions, and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  The name of The Aerospace Corporation may not be used to endorse or
 *     promote products derived from this software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AEROSPACE CORPORATION "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AEROSPACE CORPORATION BE LIABLE
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
#ifndef _TEST_EUI64_H
#define _TEST_EUI64_H

struct eui64	test_eui64_id = {{0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77}};
struct eui64	test_eui64_eui48 = {{0x00,0x11,0x22,0xFF,0xFE,0x33,0x44,0x55}};
struct eui64	test_eui64_mac48 = {{0x00,0x11,0x22,0xFF,0xFF,0x33,0x44,0x55}};

#define test_eui64_id_ascii		"00-11-22-33-44-55-66-77"
#define test_eui64_id_colon_ascii	"00:11:22:33:44:55:66:77"
#define test_eui64_hex_ascii		"0x0011223344556677"
#define test_eui64_eui48_ascii		"00-11-22-ff-fe-33-44-55"
#define test_eui64_mac48_ascii		"00-11-22-ff-fe-33-44-55"
#define test_eui64_mac_ascii		"00-11-22-33-44-55"
#define test_eui64_mac_colon_ascii	"00:11:22:33:44:55"
#define test_eui64_id_host		"id"
#define test_eui64_eui48_host		"eui-48"
#define test_eui64_mac48_host		"mac-48"

#define	test_eui64_line_id		"00-11-22-33-44-55-66-77 id"
#define	test_eui64_line_id_colon	"00:11:22:33:44:55:66:77  id"
#define	test_eui64_line_eui48		"00-11-22-FF-fe-33-44-55	eui-48"
#define	test_eui64_line_mac48		"00-11-22-FF-ff-33-44-55	 mac-48"
#define	test_eui64_line_eui48_6byte	"00-11-22-33-44-55 eui-48"
#define	test_eui64_line_eui48_6byte_c	"00:11:22:33:44:55 eui-48"

#endif /* !_TEST_EUI64_H */

/*	$OpenBSD: dhctoken.h,v 1.2 2004/02/04 12:16:56 henning Exp $	*/

/* Tokens for config file lexer and parser. */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995, 1996, 1997, 1998, 1999
 * The Internet Software Consortium.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 *
 * $FreeBSD$
 */

#define SEMI ';'
#define DOT '.'
#define COLON ':'
#define COMMA ','
#define SLASH '/'
#define LBRACE '{'
#define RBRACE '}'

#define FIRST_TOKEN	HOST
#define HOST		256
#define HARDWARE	257
#define FILENAME	258
#define FIXED_ADDR	259
#define OPTION		260
#define ETHERNET	261
#define STRING		262
#define NUMBER		263
#define NUMBER_OR_NAME	264
#define NAME		265
#define TIMESTAMP	266
#define STARTS		267
#define ENDS		268
#define UID		269
#define CLASS		270
#define LEASE		271
#define RANGE		272
#define PACKET		273
#define CIADDR		274
#define YIADDR		275
#define SIADDR		276
#define GIADDR		277
#define SUBNET		278
#define NETMASK		279
#define DEFAULT_LEASE_TIME 280
#define MAX_LEASE_TIME	281
#define VENDOR_CLASS	282
#define USER_CLASS	283
#define SHARED_NETWORK	284
#define SERVER_NAME	285
#define DYNAMIC_BOOTP	286
#define SERVER_IDENTIFIER 287
#define DYNAMIC_BOOTP_LEASE_CUTOFF 288
#define DYNAMIC_BOOTP_LEASE_LENGTH 289
#define BOOT_UNKNOWN_CLIENTS 290
#define NEXT_SERVER	291
#define TOKEN_RING	292
#define GROUP		293
#define ONE_LEASE_PER_CLIENT 294
#define GET_LEASE_HOSTNAMES 295
#define USE_HOST_DECL_NAMES 296
#define SEND		297
#define CLIENT_IDENTIFIER 298
#define REQUEST		299
#define REQUIRE		300
#define TIMEOUT		301
#define RETRY		302
#define SELECT_TIMEOUT	303
#define SCRIPT		304
#define INTERFACE	305
#define RENEW		306
#define	REBIND		307
#define EXPIRE		308
#define UNKNOWN_CLIENTS	309
#define	ALLOW		310
#define BOOTP		311
#define DENY		312
#define BOOTING		313
#define DEFAULT		314
#define MEDIA		315
#define MEDIUM		316
#define ALIAS		317
#define REBOOT		318
#define ABANDONED	319
#define	BACKOFF_CUTOFF	320
#define	INITIAL_INTERVAL 321
#define NAMESERVER	322
#define	DOMAIN		323
#define SEARCH		324
#define SUPERSEDE	325
#define APPEND		326
#define PREPEND		327
#define HOSTNAME	328
#define CLIENT_HOSTNAME	329
#define REJECT		330
#define FDDI		331
#define USE_LEASE_ADDR_FOR_DEFAULT_ROUTE 332
#define AUTHORITATIVE	333
#define TOKEN_NOT	334
#define ALWAYS_REPLY_RFC1048 335

#define is_identifier(x)	((x) >= FIRST_TOKEN &&	\
				 (x) != STRING &&	\
				 (x) != NUMBER &&	\
				 (x) != EOF)

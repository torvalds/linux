/*	$OpenBSD: dhctoken.h,v 1.8 2017/04/24 14:58:36 krw Exp $	*/

/* Tokens for config file lexer and parser. */

/*
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
 */

#define TOK_FIRST_TOKEN	TOK_HOST
#define TOK_HOST			256
#define TOK_HARDWARE			257
#define TOK_FILENAME			258
#define TOK_FIXED_ADDR			259
#define TOK_OPTION			260
#define TOK_ETHERNET			261
#define TOK_STRING			262
#define TOK_NUMBER			263
#define TOK_NUMBER_OR_NAME		264
#define TOK_NAME			265
#define TOK_TIMESTAMP			266
#define TOK_STARTS			267
#define TOK_ENDS			268
#define TOK_UID				269
#define TOK_CLASS			270
#define TOK_LEASE			271
#define TOK_RANGE			272
#define TOK_SUBNET			278
#define TOK_NETMASK			279
#define TOK_DEFAULT_LEASE_TIME		280
#define TOK_MAX_LEASE_TIME		281
#define TOK_VENDOR_CLASS		282
#define TOK_USER_CLASS			283
#define TOK_SHARED_NETWORK		284
#define TOK_SERVER_NAME			285
#define TOK_DYNAMIC_BOOTP		286
#define TOK_SERVER_IDENTIFIER		287
#define TOK_DYNAMIC_BOOTP_LEASE_CUTOFF	288
#define TOK_DYNAMIC_BOOTP_LEASE_LENGTH	289
#define TOK_BOOT_UNKNOWN_CLIENTS	290
#define TOK_NEXT_SERVER			291
#define TOK_GROUP			293
#define TOK_GET_LEASE_HOSTNAMES		295
#define TOK_USE_HOST_DECL_NAMES		296
#define TOK_SEND			297
#define TOK_TIMEOUT			301
#define TOK_UNKNOWN_CLIENTS		309
#define	TOK_ALLOW			310
#define TOK_BOOTP			311
#define TOK_DENY			312
#define TOK_BOOTING			313
#define TOK_ABANDONED			319
#define TOK_DOMAIN			323
#define TOK_HOSTNAME			328
#define TOK_CLIENT_HOSTNAME		329
#define TOK_USE_LEASE_ADDR_FOR_DEFAULT_ROUTE	332
#define TOK_AUTHORITATIVE		333
#define TOK_TOKEN_NOT			334
#define TOK_ALWAYS_REPLY_RFC1048	335
#define TOK_IPSEC_TUNNEL		336
#define TOK_ECHO_CLIENT_ID		337

#define is_identifier(x)	((x) >= TOK_FIRST_TOKEN &&	\
				 (x) != TOK_STRING &&	\
				 (x) != TOK_NUMBER &&	\
				 (x) != EOF)

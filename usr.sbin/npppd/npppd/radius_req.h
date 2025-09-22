/*	$OpenBSD: radius_req.h,v 1.8 2024/02/26 08:47:28 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 */
#ifndef	RADIUS_REQ_H
#define	RADIUS_REQ_H 1

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <radius.h>

/** maximum number of length for RADIUS shared secret */
#define MAX_RADIUS_SECRET				128

/** maximum number of RADIUS server */
#define MAX_RADIUS_SERVERS				16

/** RADIUS request failed */
#define	RADIUS_REQUEST_ERROR				0x0001

/** RADIUS request timed out */
#define	RADIUS_REQUEST_TIMEOUT				0x0002

/** response has valid authenticator */
#define	RADIUS_REQUEST_CHECK_AUTHENTICATOR_OK		0x0010

/** authenticator is not checked */
#define	RADIUS_REQUEST_CHECK_AUTHENTICATOR_NO_CHECK	0x0020

/** no message authenticator */
#define	RADIUS_REQUEST_CHECK_NO_MSG_AUTHENTICATOR	0x0040

/** has valid message authenticator */
#define	RADIUS_REQUEST_CHECK_MSG_AUTHENTICATOR_OK	0x0080

/** message authenticator is not checked*/
#define	RADIUS_REQUEST_CHECK_MSG_AUTHENTICATOR_NO_CHECK	0x0100

/** type for context to handle RADIUS request / response */
typedef void * RADIUS_REQUEST_CTX;

/** type for callback function to receive the RADIUS response */
typedef void (radius_response)(void *context, RADIUS_PACKET *pkt, int flags, RADIUS_REQUEST_CTX reqctx);

/** type for setting of RADIUS request */
typedef struct _radius_req_setting
{
	/** RADIUS Servers */
	struct {
		/** Server's address */
		union {
			struct sockaddr_in6	sin6;
			struct sockaddr_in	sin4;
		} peer;
		/** Our address */
		union {
			struct sockaddr_in6	sin6;
			struct sockaddr_in	sin4;
		} sock;
		char	secret[MAX_RADIUS_SECRET];
		int	enabled;
	} server[MAX_RADIUS_SERVERS];
	/** Index of current server */
	int curr_server;
	/** request timeout(in second) */
	int timeout;
	/** The maximum number of RADIUS request transmission */
	int max_tries;
	/** The maximum number of RADIUS request failover */
	int max_failovers;

	/** references by radius request */
	int refcnt;
	/** destroy is requested */
	int destroyed;

} radius_req_setting;

#ifdef __cplusplus
extern "C" {
#endif

void                radius_request (RADIUS_REQUEST_CTX, RADIUS_PACKET *);
int                 radius_prepare_nas_address (radius_req_setting *, RADIUS_PACKET *);
int                 radius_request_can_failover (RADIUS_REQUEST_CTX);
int                 radius_request_failover (RADIUS_REQUEST_CTX);
int                 radius_prepare (radius_req_setting *, void *, RADIUS_REQUEST_CTX *, radius_response);
void                radius_cancel_request (RADIUS_REQUEST_CTX);
const char          *radius_get_server_secret (RADIUS_REQUEST_CTX);
struct sockaddr     *radius_get_server_address (RADIUS_REQUEST_CTX);
radius_req_setting  *radius_req_setting_create (void);
int                 radius_req_setting_has_server(radius_req_setting *);
void                radius_req_setting_destroy (radius_req_setting *);

#ifdef __cplusplus
}
#endif

#endif

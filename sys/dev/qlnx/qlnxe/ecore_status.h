/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __ECORE_STATUS_H__
#define __ECORE_STATUS_H__

enum _ecore_status_t {
	ECORE_CONN_REFUSED = -14,
	ECORE_CONN_RESET = -13,
	ECORE_UNKNOWN_ERROR  = -12,
	ECORE_NORESOURCES	 = -11,
	ECORE_NODEV   = -10,
	ECORE_ABORTED = -9,
	ECORE_AGAIN   = -8,
	ECORE_NOTIMPL = -7,
	ECORE_EXISTS  = -6,
	ECORE_IO      = -5,
	ECORE_TIMEOUT = -4,
	ECORE_INVAL   = -3,
	ECORE_BUSY    = -2,
	ECORE_NOMEM   = -1,
	ECORE_SUCCESS = 0,
	/* PENDING is not an error and should be positive */
	ECORE_PENDING = 1,
};

#endif /* __ECORE_STATUS_H__ */


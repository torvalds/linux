/*
 * Copyright (c) 2016-2017, Marie Helene Kvello-Aune
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * thislist of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#pragma once

#include "libifconfig.h"


struct errstate {
	/**
	 * Type of error.
	 */
	ifconfig_errtype errtype;

	/**
	 * The error occured in this ioctl() request.
	 * Populated if errtype = IOCTL
	 */
	unsigned long ioctl_request;

	/**
	 * The value of the global errno variable when the error occured.
	 */
	int errcode;
};

struct ifconfig_handle {
	struct errstate error;
	int sockets[AF_MAX + 1];
	/** Cached output of getifaddrs */
	struct ifaddrs *ifap;
};

/* Fetch the list of interface addrs, if it hasn't already been fetched */
int ifconfig_getifaddrs(ifconfig_handle_t *h);

/**
 * Retrieves socket for address family <paramref name="addressfamily"> from
 * cache, or creates it if it doesn't already exist.
 * @param addressfamily The address family of the socket to retrieve
 * @param s The retrieved socket.
 * @return 0 on success, -1 on failure.
 * {@example
 * This example shows how to retrieve a socket from the cache.
 * {@code
 * static void myfunc() \{
 *    int s;
 *    if (ifconfig_socket(AF_LOCAL, &s) != 0) \{
 *        // Handle error state here
 *    \}
 *    // user code here
 * \}
 * }
 * }
 */
int ifconfig_socket(ifconfig_handle_t *h, const int addressfamily, int *s);

/** Function to wrap ioctl() and automatically populate ifconfig_errstate when appropriate.*/
int ifconfig_ioctlwrap(ifconfig_handle_t *h, const int addressfamily,
    unsigned long request, void *data);

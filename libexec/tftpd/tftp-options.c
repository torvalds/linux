/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008 Edwin Groothuis. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/tftp.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "tftp-utils.h"
#include "tftp-io.h"
#include "tftp-options.h"

/*
 * Option handlers
 */

struct options options[] = {
	{ "tsize",	NULL, NULL, NULL /* option_tsize */, 1 },
	{ "timeout",	NULL, NULL, option_timeout, 1 },
	{ "blksize",	NULL, NULL, option_blksize, 1 },
	{ "blksize2",	NULL, NULL, option_blksize2, 0 },
	{ "rollover",	NULL, NULL, option_rollover, 0 },
	{ NULL,		NULL, NULL, NULL, 0 }
};

/* By default allow them */
int options_rfc_enabled = 1;
int options_extra_enabled = 1;

/*
 * Rules for the option handlers:
 * - If there is no o_request, there will be no processing.
 *
 * For servers
 * - Logging is done as warnings.
 * - The handler exit()s if there is a serious problem with the
 *   values submitted in the option.
 *
 * For clients
 * - Logging is done as errors. After all, the server shouldn't
 *   return rubbish.
 * - The handler returns if there is a serious problem with the
 *   values submitted in the option.
 * - Sending the EBADOP packets is done by the handler.
 */

int
option_tsize(int peer __unused, struct tftphdr *tp __unused, int mode,
    struct stat *stbuf)
{

	if (options[OPT_TSIZE].o_request == NULL)
		return (0);

	if (mode == RRQ) 
		asprintf(&options[OPT_TSIZE].o_reply,
			"%ju", stbuf->st_size);
	else
		/* XXX Allows writes of all sizes. */
		options[OPT_TSIZE].o_reply =
			strdup(options[OPT_TSIZE].o_request);
	return (0);
}

int
option_timeout(int peer)
{
	int to;

	if (options[OPT_TIMEOUT].o_request == NULL)
		return (0);

	to = atoi(options[OPT_TIMEOUT].o_request);
	if (to < TIMEOUT_MIN || to > TIMEOUT_MAX) {
		tftp_log(acting_as_client ? LOG_ERR : LOG_WARNING,
		    "Received bad value for timeout. "
		    "Should be between %d and %d, received %d",
		    TIMEOUT_MIN, TIMEOUT_MAX, to);
		send_error(peer, EBADOP);
		if (acting_as_client)
			return (1);
		exit(1);
	} else {
		timeoutpacket = to;
		options[OPT_TIMEOUT].o_reply =
			strdup(options[OPT_TIMEOUT].o_request);
	}
	settimeouts(timeoutpacket, timeoutnetwork, maxtimeouts);

	if (debug&DEBUG_OPTIONS)
		tftp_log(LOG_DEBUG, "Setting timeout to '%s'",
			options[OPT_TIMEOUT].o_reply);

	return (0);
}

int
option_rollover(int peer)
{

	if (options[OPT_ROLLOVER].o_request == NULL)
		return (0);

	if (strcmp(options[OPT_ROLLOVER].o_request, "0") != 0
	 && strcmp(options[OPT_ROLLOVER].o_request, "1") != 0) {
		tftp_log(acting_as_client ? LOG_ERR : LOG_WARNING,
		    "Bad value for rollover, "
		    "should be either 0 or 1, received '%s', "
		    "ignoring request",
		    options[OPT_ROLLOVER].o_request);
		if (acting_as_client) {
			send_error(peer, EBADOP);
			return (1);
		}
		return (0);
	}
	options[OPT_ROLLOVER].o_reply =
		strdup(options[OPT_ROLLOVER].o_request);

	if (debug&DEBUG_OPTIONS)
		tftp_log(LOG_DEBUG, "Setting rollover to '%s'",
			options[OPT_ROLLOVER].o_reply);

	return (0);
}

int
option_blksize(int peer)
{
	u_long maxdgram;
	size_t len;

	if (options[OPT_BLKSIZE].o_request == NULL)
		return (0);

	/* maximum size of an UDP packet according to the system */
	len = sizeof(maxdgram);
	if (sysctlbyname("net.inet.udp.maxdgram",
	    &maxdgram, &len, NULL, 0) < 0) {
		tftp_log(LOG_ERR, "sysctl: net.inet.udp.maxdgram");
		return (acting_as_client ? 1 : 0);
	}

	int size = atoi(options[OPT_BLKSIZE].o_request);
	if (size < BLKSIZE_MIN || size > BLKSIZE_MAX) {
		if (acting_as_client) {
			tftp_log(LOG_ERR,
			    "Invalid blocksize (%d bytes), aborting",
			    size);
			send_error(peer, EBADOP);
			return (1);
		} else {
			tftp_log(LOG_WARNING,
			    "Invalid blocksize (%d bytes), ignoring request",
			    size);
			return (0);
		}
	}

	if (size > (int)maxdgram) {
		if (acting_as_client) {
			tftp_log(LOG_ERR,
			    "Invalid blocksize (%d bytes), "
			    "net.inet.udp.maxdgram sysctl limits it to "
			    "%ld bytes.\n", size, maxdgram);
			send_error(peer, EBADOP);
			return (1);
		} else {
			tftp_log(LOG_WARNING,
			    "Invalid blocksize (%d bytes), "
			    "net.inet.udp.maxdgram sysctl limits it to "
			    "%ld bytes.\n", size, maxdgram);
			size = maxdgram;
			/* No reason to return */
		}
	}

	asprintf(&options[OPT_BLKSIZE].o_reply, "%d", size);
	segsize = size;
	pktsize = size + 4;
	if (debug&DEBUG_OPTIONS)
		tftp_log(LOG_DEBUG, "Setting blksize to '%s'",
		    options[OPT_BLKSIZE].o_reply);

	return (0);
}

int
option_blksize2(int peer __unused)
{
	u_long	maxdgram;
	int	size, i;
	size_t	len;

	int sizes[] = {
		8, 16, 32, 64, 128, 256, 512, 1024,
		2048, 4096, 8192, 16384, 32768, 0
	};

	if (options[OPT_BLKSIZE2].o_request == NULL)
		return (0);

	/* maximum size of an UDP packet according to the system */
	len = sizeof(maxdgram);
	if (sysctlbyname("net.inet.udp.maxdgram",
	    &maxdgram, &len, NULL, 0) < 0) {
		tftp_log(LOG_ERR, "sysctl: net.inet.udp.maxdgram");
		return (acting_as_client ? 1 : 0);
	}

	size = atoi(options[OPT_BLKSIZE2].o_request);
	for (i = 0; sizes[i] != 0; i++) {
		if (size == sizes[i]) break;
	}
	if (sizes[i] == 0) {
		tftp_log(LOG_INFO,
		    "Invalid blocksize2 (%d bytes), ignoring request", size);
		return (acting_as_client ? 1 : 0);
	}

	if (size > (int)maxdgram) {
		for (i = 0; sizes[i+1] != 0; i++) {
			if ((int)maxdgram < sizes[i+1]) break;
		}
		tftp_log(LOG_INFO,
		    "Invalid blocksize2 (%d bytes), net.inet.udp.maxdgram "
		    "sysctl limits it to %ld bytes.\n", size, maxdgram);
		size = sizes[i];
		/* No need to return */
	}

	asprintf(&options[OPT_BLKSIZE2].o_reply, "%d", size);
	segsize = size;
	pktsize = size + 4;
	if (debug&DEBUG_OPTIONS)
		tftp_log(LOG_DEBUG, "Setting blksize2 to '%s'",
		    options[OPT_BLKSIZE2].o_reply);

	return (0);
}

/*
 * Append the available options to the header
 */
uint16_t
make_options(int peer __unused, char *buffer, uint16_t size) {
	int	i;
	char	*value;
	const char *option;
	uint16_t length;
	uint16_t returnsize = 0;

	if (!options_rfc_enabled) return (0);

	for (i = 0; options[i].o_type != NULL; i++) {
		if (options[i].rfc == 0 && !options_extra_enabled)
			continue;

		option = options[i].o_type;
		if (acting_as_client)
			value = options[i].o_request;
		else
			value = options[i].o_reply;
		if (value == NULL)
			continue;

		length = strlen(value) + strlen(option) + 2;
		if (size <= length) {
			tftp_log(LOG_ERR,
			    "Running out of option space for "
			    "option '%s' with value '%s': "
			    "needed %d bytes, got %d bytes",
			    option, value, size, length);
			continue;
		}

		sprintf(buffer, "%s%c%s%c", option, '\000', value, '\000');
		size -= length;
		buffer += length;
		returnsize += length;
	}

	return (returnsize);
}

/*
 * Parse the received options in the header
 */
int
parse_options(int peer, char *buffer, uint16_t size)
{
	int	i, options_failed;
	char	*c, *cp, *option, *value;

	if (!options_rfc_enabled) return (0);

	/* Parse the options */
	cp = buffer;
	options_failed = 0;	
	while (size > 0) {
		option = cp;
		i = get_field(peer, cp, size);
		cp += i;

		value = cp;
		i = get_field(peer, cp, size);
		cp += i;

		/* We are at the end */
		if (*option == '\0') break;

		if (debug&DEBUG_OPTIONS)
			tftp_log(LOG_DEBUG,
			    "option: '%s' value: '%s'", option, value);

		for (c = option; *c; c++)
			if (isupper(*c))
				*c = tolower(*c);
		for (i = 0; options[i].o_type != NULL; i++) {
			if (strcmp(option, options[i].o_type) == 0) {
				if (!acting_as_client)
					options[i].o_request = value;
				if (!options_extra_enabled && !options[i].rfc) {
					tftp_log(LOG_INFO,
					    "Option '%s' with value '%s' found "
					    "but it is not an RFC option",
					    option, value);
					continue;
				}
				if (options[i].o_handler)
					options_failed +=
					    (options[i].o_handler)(peer);
				break;
			}
		}
		if (options[i].o_type == NULL)
			tftp_log(LOG_WARNING,
			    "Unknown option: '%s'", option);

		size -= strlen(option) + strlen(value) + 2;
	}

	return (options_failed);
}

/*
 * Set some default values in the options
 */
void
init_options(void)
{

	options[OPT_ROLLOVER].o_request = strdup("0");
}

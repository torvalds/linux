/*	$OpenBSD: options.c,v 1.35 2017/02/13 22:33:39 krw Exp $	*/

/* DHCP options parsing and reassembly. */

/*
 * Copyright (c) 1995, 1996, 1997, 1998 The Internet Software Consortium.
 * All rights reserved.
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

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>

#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"

int bad_options = 0;
int bad_options_max = 5;

void	parse_options(struct packet *);
void	parse_option_buffer(struct packet *, unsigned char *, int);
void	create_priority_list(unsigned char *, unsigned char *, int);
int	store_option_fragment(unsigned char *, int, unsigned char,
	    int, unsigned char *);
int	store_options(unsigned char *, int, struct tree_cache **,
	    unsigned char *, int, int);


/*
 * Parse all available options out of the specified packet.
 */
void
parse_options(struct packet *packet)
{
	/* Initially, zero all option pointers. */
	memset(packet->options, 0, sizeof(packet->options));

	/* If we don't see the magic cookie, there's nothing to parse. */
	if (memcmp(packet->raw->options, DHCP_OPTIONS_COOKIE, 4)) {
		packet->options_valid = 0;
		return;
	}

	/*
	 * Go through the options field, up to the end of the packet or
	 * the End field.
	 */
	parse_option_buffer(packet, &packet->raw->options[4],
	    packet->packet_length - DHCP_FIXED_NON_UDP - 4);

	/*
	 * If we parsed a DHCP Option Overload option, parse more
	 * options out of the buffer(s) containing them.
	 */
	if (packet->options_valid &&
	    packet->options[DHO_DHCP_OPTION_OVERLOAD].data) {
		if (packet->options[DHO_DHCP_OPTION_OVERLOAD].data[0] & 1)
			parse_option_buffer(packet,
			    (unsigned char *)packet->raw->file,
			    sizeof(packet->raw->file));
		if (packet->options[DHO_DHCP_OPTION_OVERLOAD].data[0] & 2)
			parse_option_buffer(packet,
			    (unsigned char *)packet->raw->sname,
			    sizeof(packet->raw->sname));
	}
}

/*
 * Parse options out of the specified buffer, storing addresses of
 * option values in packet->options and setting packet->options_valid if
 * no errors are encountered.
 */
void
parse_option_buffer(struct packet *packet,
    unsigned char *buffer, int length)
{
	unsigned char *s, *t;
	unsigned char *end = buffer + length;
	int len;
	int code;

	for (s = buffer; *s != DHO_END && s < end; ) {
		code = s[0];

		/* Pad options don't have a length - just skip them. */
		if (code == DHO_PAD) {
			s++;
			continue;
		}
		if (s + 2 > end) {
			len = 65536;
			goto bogus;
		}

		/*
		 * All other fields (except end, see above) have a
		 * one-byte length.
		 */
		len = s[1];

		/*
		 * If the length is outrageous, silently skip the rest,
		 * and mark the packet bad. Unfortunately some crappy
		 * dhcp servers always seem to give us garbage on the
		 * end of a packet. so rather than keep refusing, give
		 * up and try to take one after seeing a few without
		 * anything good.
		 */
		if (s + len + 2 > end) {
		    bogus:
			bad_options++;
			log_warnx("option %s (%d) %s.",
			    dhcp_options[code].name, len,
			    "larger than buffer");
			if (bad_options == bad_options_max) {
				packet->options_valid = 1;
				bad_options = 0;
				log_warnx("Many bogus options seen in "
				    "offers.");
				log_warnx("Taking this offer in spite of "
				    "bogus");
				log_warnx("options - hope for the best!");
			} else {
				log_warnx("rejecting bogus offer.");
				packet->options_valid = 0;
			}
			return;
		}
		/*
		 * If we haven't seen this option before, just make
		 * space for it and copy it there.
		 */
		if (!packet->options[code].data) {
			t = calloc(1, len + 1);
			if (!t)
				fatalx("Can't allocate storage for option %s.",
				    dhcp_options[code].name);
			/*
			 * Copy and NUL-terminate the option (in case
			 * it's an ASCII string).
			 */
			memcpy(t, &s[2], len);
			t[len] = 0;
			packet->options[code].len = len;
			packet->options[code].data = t;
		} else {
			/*
			 * If it's a repeat, concatenate it to whatever
			 * we last saw.   This is really only required
			 * for clients, but what the heck...
			 */
			t = calloc(1, len + packet->options[code].len + 1);
			if (!t)
				fatalx("Can't expand storage for option %s.",
				    dhcp_options[code].name);
			memcpy(t, packet->options[code].data,
				packet->options[code].len);
			memcpy(t + packet->options[code].len,
				&s[2], len);
			packet->options[code].len += len;
			t[packet->options[code].len] = 0;
			free(packet->options[code].data);
			packet->options[code].data = t;
		}
		s += len + 2;
	}
	packet->options_valid = 1;
}

/*
 * Fill priority_list with a complete list of DHCP options sorted by
 * priority. i.e.
 *     1) Mandatory options.
 *     2) Options from prl that are not already present.
 *     3) Options from the default list that are not already present.
 */
void
create_priority_list(unsigned char *priority_list, unsigned char *prl,
    int prl_len)
{
	unsigned char stored_list[256];
	int i, priority_len = 0;

	/* clear stored_list, priority_list should be cleared before */
	memset(&stored_list, 0, sizeof(stored_list));

	/* Some options we don't want on the priority list. */
	stored_list[DHO_PAD] = 1;
	stored_list[DHO_END] = 1;

	/* Mandatory options. */
	for(i = 0; dhcp_option_default_priority_list[i] != DHO_END; i++) {
		priority_list[priority_len++] =
		    dhcp_option_default_priority_list[i];
		stored_list[dhcp_option_default_priority_list[i]] = 1;
	}

	/* Supplied priority list. */
	if (!prl)
		prl_len = 0;
	for(i = 0; i < prl_len; i++) {
		/* CLASSLESS routes always have priority, sayeth RFC 3442. */
		if (prl[i] == DHO_CLASSLESS_STATIC_ROUTES ||
		    prl[i] == DHO_CLASSLESS_MS_STATIC_ROUTES) {
			priority_list[priority_len++] = prl[i];
			stored_list[prl[i]] = 1;
		}
	}
	for(i = 0; i < prl_len; i++) {
		if (stored_list[prl[i]])
			continue;
		priority_list[priority_len++] = prl[i];
		stored_list[prl[i]] = 1;
	}

	/* Default priority list. */
	prl = dhcp_option_default_priority_list;
	for(i = 0; i < 256; i++) {
		if (stored_list[prl[i]])
			continue;
		priority_list[priority_len++] = prl[i];
		stored_list[prl[i]] = 1;
	}
}
/*
 * cons options into a big buffer, and then split them out into the
 * three separate buffers if needed.  This allows us to cons up a set of
 * vendor options using the same routine.
 */
int
cons_options(struct packet *inpacket, struct dhcp_packet *outpacket,
    int mms, struct tree_cache **options,
    int overload, /* Overload flags that may be set. */
    int terminate, int bootpp, u_int8_t *prl, int prl_len)
{
	unsigned char priority_list[256];
	unsigned char buffer[4096];	/* Really big buffer... */
	int bufix, main_buffer_size, option_size;

	/*
	 * If the client has provided a maximum DHCP message size, use
	 * that; otherwise, if it's BOOTP, only 64 bytes; otherwise use
	 * up to the minimum IP MTU size (576 bytes).
	 *
	 * XXX if a BOOTP client specifies a max message size, we will
	 * honor it.
	 */
	if (!mms &&
	    inpacket &&
	    inpacket->options[DHO_DHCP_MAX_MESSAGE_SIZE].data &&
	    (inpacket->options[DHO_DHCP_MAX_MESSAGE_SIZE].len >=
	    sizeof(u_int16_t))) {
		mms = getUShort(
		    inpacket->options[DHO_DHCP_MAX_MESSAGE_SIZE].data);
	}

	if (mms) {
		if (mms < 576)
			mms = 576;	/* mms must be >= minimum IP MTU */
		main_buffer_size = mms - DHCP_FIXED_LEN;
	} else if (bootpp)
		main_buffer_size = 64;
	else
		main_buffer_size = 576 - DHCP_FIXED_LEN;

	if (main_buffer_size > sizeof(outpacket->options))
		main_buffer_size = sizeof(outpacket->options);

	/*
	 * Initialize the available buffers, some or all of which may not be
	 * used.
	 */
	memset(outpacket->options, DHO_PAD, sizeof(outpacket->options));
	if (overload & 1)
		memset(outpacket->file, DHO_PAD, DHCP_FILE_LEN);
	if (overload & 2)
		memset(outpacket->sname, DHO_PAD, DHCP_SNAME_LEN);
	if (bootpp)
		overload = 0; /* Don't use overload buffers for bootp! */

	/*
	 * Get complete list of possible options in priority order. Use the
	 * list provided in the options. Lacking that use the list provided by
	 * prl. If that is not available just use the default list.
	 */
	memset(&priority_list, 0, sizeof(priority_list));
	if (inpacket &&
	    inpacket->options[DHO_DHCP_PARAMETER_REQUEST_LIST].data)
		create_priority_list(priority_list,
		    inpacket->options[DHO_DHCP_PARAMETER_REQUEST_LIST].data,
		    inpacket->options[DHO_DHCP_PARAMETER_REQUEST_LIST].len);
	else if (prl)
		create_priority_list(priority_list, prl, prl_len);
	else
		create_priority_list(priority_list, NULL, 0);

	/*
	 * Copy the options into the big buffer, including leading cookie and
	 * DHCP_OVERLOAD_OPTION, and DHO_END if it fits. All unused space will
	 * be set to DHO_PAD
	 */
	option_size = store_options(buffer, main_buffer_size, options,
	    priority_list, overload, terminate);
	if (option_size == 0)
		return (DHCP_FIXED_NON_UDP);

	/* Copy the main buffer. */
	memcpy(&outpacket->options[0], buffer, main_buffer_size);
	if (option_size <= main_buffer_size)
		return (DHCP_FIXED_NON_UDP + option_size);

	/* Copy the overflow buffers. */
	bufix = main_buffer_size;
	if (overload & 1) {
		memcpy(outpacket->file, &buffer[bufix], DHCP_FILE_LEN);
		bufix += DHCP_FILE_LEN;
	}
	if (overload & 2)
		memcpy(outpacket->sname, &buffer[bufix], DHCP_SNAME_LEN);

	return (DHCP_FIXED_NON_UDP + main_buffer_size);
}

/*
 * Store a <code><length><data> fragment in buffer. Return the number of
 * characters used. Return 0 if no data could be stored.
 */
int
store_option_fragment(unsigned char *buffer, int buffer_size,
    unsigned char code, int length, unsigned char *data)
{
	buffer_size -= 2; /* Space for option code and option length. */

	if (buffer_size < 1)
		return (0);

	if (buffer_size > 255)
		buffer_size = 255;
	if (length > buffer_size)
		length = buffer_size;

	buffer[0] = code;
	buffer[1] = length;

	memcpy(&buffer[2], data, length);

	return (length + 2);
}

/*
 * Store all the requested options into the requested buffer. Insert the
 * required cookie, DHO_DHCP_OPTION_OVERLOAD options and append a DHO_END if
 * if fits. Ensure all buffer space is set to DHO_PAD if unused.
 */
int
store_options(unsigned char *buffer, int main_buffer_size,
    struct tree_cache **options, unsigned char *priority_list, int overload,
    int terminate)
{
	int buflen, code, cutoff, i, incr, ix, length, optstart, overflow;
	int second_cutoff;
	int bufix = 0;
	int stored_classless = 0;

	overload &= 3; /* Only consider valid bits. */

	cutoff = main_buffer_size;
	second_cutoff = cutoff + ((overload & 1) ? DHCP_FILE_LEN : 0);
	buflen = second_cutoff + ((overload & 2) ? DHCP_SNAME_LEN : 0);

	memset(buffer, DHO_PAD, buflen);
	memcpy(buffer, DHCP_OPTIONS_COOKIE, 4);

	if (overload)
		bufix = 7; /* Reserve space for DHO_DHCP_OPTION_OVERLOAD. */
	else
		bufix = 4;

	/*
	 * Store options in the order they appear in the priority list.
	 */
	for (i = 0; i < 256; i++) {
		/* Code for next option to try to store. */
		code = priority_list[i];
		if (code == DHO_PAD || code == DHO_END)
			continue;

		if (!options[code] || !tree_evaluate(options[code]))
			continue;

		/*
		 * RFC 3442 says:
		 *
		 * When a DHCP client requests the Classless Static
		 * Routes option and also requests either or both of the
		 * Router option and the Static Routes option, and the
		 * DHCP server is sending Classless Static Routes options
		 * to that client, the server SHOULD NOT include the
		 * Router or Static Routes options.
		 */
		if ((code == DHO_ROUTERS || code == DHO_STATIC_ROUTES) &&
		    stored_classless)
			continue;

		/* We should now have a constant length for the option. */
		length = options[code]->len;

		/* Try to store the option. */
		optstart = bufix;
		ix = 0;
		while (length) {
			incr = store_option_fragment(&buffer[bufix],
			    cutoff - bufix, code, length,
			    options[code]->value + ix);

			if (incr > 0) {
				bufix += incr;
				length -= incr - 2;
				ix += incr - 2;
				continue;
			}

			/*
			 * No fragment could be stored in the space before the
			 * cutoff. Fill the unusable space with DHO_PAD and
			 * move cutoff for another attempt.
			 */
			memset(&buffer[bufix], DHO_PAD, cutoff - bufix);
			bufix = cutoff;
			if (cutoff < second_cutoff)
				cutoff = second_cutoff;
			else if (cutoff < buflen)
				cutoff = buflen;
			else
				break;
		}

		if (length > 0) {
zapfrags:
			memset(&buffer[optstart], DHO_PAD, buflen - optstart);
			bufix = optstart;
		} else if (terminate && dhcp_options[code].format[0] == 't') {
			if (bufix < cutoff)
				buffer[bufix++] = '\0';
			else
				goto zapfrags;
		}
		if (code == DHO_CLASSLESS_STATIC_ROUTES ||
		    code == DHO_CLASSLESS_MS_STATIC_ROUTES)
			stored_classless = 1;
	}

	if (bufix == (4 + (overload ? 3 : 0)))
		/* Didn't manage to store any options. */
		return (0);

	if (bufix < buflen)
		buffer[bufix++] = DHO_END;

	/* Fill in overload option value based on space used for options. */
	if (overload) {
		overflow = bufix - main_buffer_size;
		if (overflow > 0) {
			buffer[4] = DHO_DHCP_OPTION_OVERLOAD;
			buffer[5] = 1;
			if (overload & 1) {
				buffer[6] |= 1;
				overflow -= DHCP_FILE_LEN;
			}
			if ((overload & 2) && overflow > 0)
				buffer[6] |= 2;
		} else {
			/*
			 * Compact buffer to eliminate the unused
			 * DHO_DHCP_OPTION_OVERLOAD option. Some clients
			 * choke on DHO_PAD options there.
			 */
			memmove(&buffer[4], &buffer[7], buflen - 7);
			bufix -= 3;
			memset(&buffer[bufix], DHO_PAD, 3);
		}
	}

	return (bufix);
}

void
do_packet(struct interface_info *interface, struct dhcp_packet *packet,
    int len, unsigned int from_port, struct iaddr from, struct hardware *hfrom)
{
	struct packet tp;
	int i;

	if (packet->hlen > sizeof(packet->chaddr)) {
		log_info("Discarding packet with invalid hlen.");
		return;
	}

	memset(&tp, 0, sizeof(tp));
	tp.raw = packet;
	tp.packet_length = len;
	tp.client_port = from_port;
	tp.client_addr = from;
	tp.interface = interface;
	tp.haddr = hfrom;

	parse_options(&tp);
	if (tp.options_valid &&
	    tp.options[DHO_DHCP_MESSAGE_TYPE].data)
		tp.packet_type = tp.options[DHO_DHCP_MESSAGE_TYPE].data[0];

	if (tp.packet_type)
		dhcp(&tp, interface->is_udpsock);
	else
		bootp(&tp);

	/* Free the data associated with the options. */
	for (i = 0; i < 256; i++)
		free(tp.options[i].data);
}

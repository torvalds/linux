/* $OpenBSD: esc.c,v 1.6 2021/06/14 17:58:15 eric Exp $	 */

/*
 * Copyright (c) 2014 Gilles Chehade <gilles@poolp.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/tree.h>

#include <limits.h>
#include <stdio.h>

#include "smtpd-defines.h"
#include "smtpd-api.h"

static struct escode {
	enum enhanced_status_code	code;
	const char		       *description;
} esc[] = {
	/* 0.0 */
	{ ESC_OTHER_STATUS,				"Other/Undefined" },

	/* 1.x */
	{ ESC_OTHER_ADDRESS_STATUS,			"Other/Undefined address status" },
	{ ESC_BAD_DESTINATION_MAILBOX_ADDRESS,		"Bad destination mailbox address" },
	{ ESC_BAD_DESTINATION_SYSTEM_ADDRESS,		"Bad destination system address" },
	{ ESC_BAD_DESTINATION_MAILBOX_ADDRESS_SYNTAX,	"Bad destination mailbox address syntax" },
	{ ESC_DESTINATION_MAILBOX_ADDRESS_AMBIGUOUS,	"Destination mailbox address ambiguous" },
	{ ESC_DESTINATION_ADDRESS_VALID,		"Destination address valid" },
	{ ESC_DESTINATION_MAILBOX_HAS_MOVED,		"Destination mailbox has moved, No forwarding address" },
	{ ESC_BAD_SENDER_MAILBOX_ADDRESS_SYNTAX,	"Bad sender's mailbox address syntax" },
	{ ESC_BAD_SENDER_SYSTEM_ADDRESS,		"Bad sender's system address syntax" },

	/* 2.x */
	{ ESC_OTHER_MAILBOX_STATUS,			"Other/Undefined mailbox status" },
	{ ESC_MAILBOX_DISABLED,				"Mailbox disabled, not accepting messages" },
	{ ESC_MAILBOX_FULL,				"Mailbox full" },
	{ ESC_MESSAGE_LENGTH_TOO_LARGE,			"Message length exceeds administrative limit" },
	{ ESC_MAILING_LIST_EXPANSION_PROBLEM,		"Mailing list expansion problem" },

	/* 3.x */
	{ ESC_OTHER_MAIL_SYSTEM_STATUS,			"Other/Undefined mail system status" },
	{ ESC_MAIL_SYSTEM_FULL,				"Mail system full" },
	{ ESC_SYSTEM_NOT_ACCEPTING_MESSAGES,		"System not accepting network messages" },
	{ ESC_SYSTEM_NOT_CAPABLE_OF_SELECTED_FEATURES,	"System not capable of selected features" },
	{ ESC_MESSAGE_TOO_BIG_FOR_SYSTEM,		"Message too big for system" },
	{ ESC_SYSTEM_INCORRECTLY_CONFIGURED,		"System incorrectly configured" },

	/* 4.x */
	{ ESC_OTHER_NETWORK_ROUTING_STATUS,		"Other/Undefined network or routing status" },
	{ ESC_NO_ANSWER_FROM_HOST,			"No answer from host" },
	{ ESC_BAD_CONNECTION,				"Bad connection" },
	{ ESC_DIRECTORY_SERVER_FAILURE,			"Directory server failure" },
	{ ESC_UNABLE_TO_ROUTE,				"Unable to route" },
	{ ESC_MAIL_SYSTEM_CONGESTION,			"Mail system congestion" },
	{ ESC_ROUTING_LOOP_DETECTED,			"Routing loop detected" },
	{ ESC_DELIVERY_TIME_EXPIRED,			"Delivery time expired" },

	/* 5.x */
	{ ESC_INVALID_RECIPIENT,			"Invalid recipient" },
	{ ESC_INVALID_COMMAND,				"Invalid command" },
	{ ESC_SYNTAX_ERROR,				"Syntax error" },
	{ ESC_TOO_MANY_RECIPIENTS,			"Too many recipients" },
	{ ESC_INVALID_COMMAND_ARGUMENTS,		"Invalid command arguments" },
	{ ESC_WRONG_PROTOCOL_VERSION,			"Wrong protocol version" },

	/* 6.x */
	{ ESC_OTHER_MEDIA_ERROR,			"Other/Undefined media error" },
	{ ESC_MEDIA_NOT_SUPPORTED,			"Media not supported" },
	{ ESC_CONVERSION_REQUIRED_AND_PROHIBITED,	"Conversion required and prohibited" },
	{ ESC_CONVERSION_REQUIRED_BUT_NOT_SUPPORTED,	"Conversion required but not supported" },
	{ ESC_CONVERSION_WITH_LOSS_PERFORMED,		"Conversion with loss performed" },
	{ ESC_CONVERSION_FAILED,			"Conversion failed" },

	/* 7.x */
	{ ESC_OTHER_SECURITY_STATUS,			"Other/Undefined security status" },
	{ ESC_DELIVERY_NOT_AUTHORIZED_MESSAGE_REFUSED,	"Delivery not authorized, message refused" },
	{ ESC_MAILING_LIST_EXPANSION_PROHIBITED,       	"Mailing list expansion prohibited" },
	{ ESC_SECURITY_CONVERSION_REQUIRED_NOT_POSSIBLE,"Security conversion required but not possible" },
	{ ESC_SECURITY_FEATURES_NOT_SUPPORTED,		"Security features not supported" },
	{ ESC_CRYPTOGRAPHIC_FAILURE,			"Cryptographic failure" },
	{ ESC_CRYPTOGRAPHIC_ALGORITHM_NOT_SUPPORTED,	"Cryptographic algorithm not supported" },
	{ ESC_MESSAGE_TOO_BIG_FOR_SYSTEM,		"Message integrity failure" },
};

const char *
esc_code(enum enhanced_status_class class, enum enhanced_status_code code)
{
	static char buffer[6];

	(void)snprintf(buffer, sizeof buffer, "%d.%d.%d", class, code / 10, code % 10);
	return buffer;

}

const char *
esc_description(enum enhanced_status_code code)
{
	uint32_t	i;

	for (i = 0; i < nitems(esc); ++i)
		if (code == esc[i].code)
			return esc[i].description;
	return "Other/Undefined";
}

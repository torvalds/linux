/*	$OpenBSD: http.h,v 1.1 2022/06/28 16:11:30 claudio Exp $	*/

/*
 * Copyright (c) 2012 - 2015 Reyk Floeter <reyk@openbsd.org>
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

struct http_error {
	int			 error_code;
	const char		*error_name;
};

/*
 * HTTP status codes based on IANA assignments (2014-06-11 version):
 * https://www.iana.org/assignments/http-status-codes/http-status-codes.xhtml
 * plus legacy (306) and non-standard (420).
 */
#define HTTP_ERRORS		{			\
	{ 100,	"Continue" },				\
	{ 101,	"Switching Protocols" },		\
	{ 102,	"Processing" },				\
	/* 103-199 unassigned */			\
	{ 200,	"OK" },					\
	{ 201,	"Created" },				\
	{ 202,	"Accepted" },				\
	{ 203,	"Non-Authoritative Information" },	\
	{ 204,	"No Content" },				\
	{ 205,	"Reset Content" },			\
	{ 206,	"Partial Content" },			\
	{ 207,	"Multi-Status" },			\
	{ 208,	"Already Reported" },			\
	/* 209-225 unassigned */			\
	{ 226,	"IM Used" },				\
	/* 227-299 unassigned */			\
	{ 300,	"Multiple Choices" },			\
	{ 301,	"Moved Permanently" },			\
	{ 302,	"Found" },				\
	{ 303,	"See Other" },				\
	{ 304,	"Not Modified" },			\
	{ 305,	"Use Proxy" },				\
	{ 306,	"Switch Proxy" },			\
	{ 307,	"Temporary Redirect" },			\
	{ 308,	"Permanent Redirect" },			\
	/* 309-399 unassigned */			\
	{ 400,	"Bad Request" },			\
	{ 401,	"Unauthorized" },			\
	{ 402,	"Payment Required" },			\
	{ 403,	"Forbidden" },				\
	{ 404,	"Not Found" },				\
	{ 405,	"Method Not Allowed" },			\
	{ 406,	"Not Acceptable" },			\
	{ 407,	"Proxy Authentication Required" },	\
	{ 408,	"Request Timeout" },			\
	{ 409,	"Conflict" },				\
	{ 410,	"Gone" },				\
	{ 411,	"Length Required" },			\
	{ 412,	"Precondition Failed" },		\
	{ 413,	"Payload Too Large" },			\
	{ 414,	"URI Too Long" },			\
	{ 415,	"Unsupported Media Type" },		\
	{ 416,	"Range Not Satisfiable" },		\
	{ 417,	"Expectation Failed" },			\
	{ 418,	"I'm a teapot" },			\
	/* 419-421 unassigned */			\
	{ 420,	"Enhance Your Calm" },			\
	{ 422,	"Unprocessable Entity" },		\
	{ 423,	"Locked" },				\
	{ 424,	"Failed Dependency" },			\
	/* 425 unassigned */				\
	{ 426,	"Upgrade Required" },			\
	/* 427 unassigned */				\
	{ 428,	"Precondition Required" },		\
	{ 429,	"Too Many Requests" },			\
	/* 430 unassigned */				\
	{ 431,	"Request Header Fields Too Large" },	\
	/* 432-450 unassigned */			\
	{ 451,	"Unavailable For Legal Reasons" },	\
	/* 452-499 unassigned */			\
	{ 500,	"Internal Server Error" },		\
	{ 501,	"Not Implemented" },			\
	{ 502,	"Bad Gateway" },			\
	{ 503,	"Service Unavailable" },		\
	{ 504,	"Gateway Timeout" },			\
	{ 505,	"HTTP Version Not Supported" },		\
	{ 506,	"Variant Also Negotiates" },		\
	{ 507,	"Insufficient Storage" },		\
	{ 508,	"Loop Detected" },			\
	/* 509 unassigned */				\
	{ 510,	"Not Extended" },			\
	{ 511,	"Network Authentication Required" },	\
	/* 512-599 unassigned */			\
	{ 0,	NULL }					\
}

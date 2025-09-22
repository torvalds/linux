/*	$OpenBSD: libscsi.c,v 1.11 2016/01/28 17:26:10 gsoares Exp $	*/

/* Copyright (c) 1994 HD Associates
 * (contact: dufault@hda.com)
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * This product includes software developed by HD Associates
 * 4. Neither the name of the HD Associaates nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY HD ASSOCIATES``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL HD ASSOCIATES OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: scsi.c,v 1.6 1995/05/30 05:47:26 rgrimes Exp $
 */
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/scsiio.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>

#include "libscsi.h"

static struct {
	FILE	*db_f;
	int	db_level;
	int	db_trunc;
} behave;

/* scsireq_reset: Reset a scsireq structure.
 */
scsireq_t *
scsireq_reset(scsireq_t *scsireq)
{
	if (scsireq == 0)
		return scsireq;

	scsireq->flags = 0;		/* info about the request status and type */
	scsireq->timeout = 2000;	/* 2 seconds */
	bzero(scsireq->cmd, sizeof(scsireq->cmd));
	scsireq->cmdlen = 0;
	/* Leave scsireq->databuf alone */
	/* Leave scsireq->datalen alone */
	scsireq->datalen_used = 0;
	bzero(scsireq->sense, sizeof(scsireq->sense));
	scsireq->senselen = sizeof(scsireq->sense);
	scsireq->senselen_used = 0;
	scsireq->status = 0;
	scsireq->retsts = 0;
	scsireq->error = 0;

	return scsireq;
}

/* scsireq_new: Allocate and initialize a new scsireq.
 */
scsireq_t *
scsireq_new(void)
{
	scsireq_t *p = malloc(sizeof(scsireq_t));

	if (p)
		scsireq_reset(p);

	return p;
}

/*
 * Decode: Decode the data section of a scsireq.  This decodes
 * trivial grammar:
 *
 * fields : field fields
 *        ;
 *
 * field : field_specifier
 *       | control
 *       ;
 *
 * control : 's' seek_value
 *       | 's' '+' seek_value
 *       ;
 *
 * seek_value : DECIMAL_NUMBER
 *       | 'v'				// For indirect seek, i.e., value from the arg list
 *       ;
 *
 * field_specifier : type_specifier field_width
 *       | '{' NAME '}' type_specifier field_width
 *       ;
 *
 * field_width : DECIMAL_NUMBER
 *       ;
 *
 * type_specifier : 'i'	// Integral types (i1, i2, i3, i4)
 *       | 'b'				// Bits
 *       | 't'				// Bits
 *       | 'c'				// Character arrays
 *       | 'z'				// Character arrays with zeroed trailing spaces
 *       ;
 *
 * Notes:
 * 1. Integral types are swapped into host order.
 * 2. Bit fields are allocated MSB to LSB to match the SCSI spec documentation.
 * 3. 's' permits "seeking" in the string.  "s+DECIMAL" seeks relative to
 *    DECIMAL; "sDECIMAL" seeks absolute to decimal.
 * 4. 's' permits an indirect reference.  "sv" or "s+v" will get the
 *    next integer value from the arg array.
 * 5. Field names can be anything between the braces
 *
 * BUGS:
 * i and b types are promoted to ints.
 *
 */
static int
do_buff_decode(u_char *databuf, size_t len,
    void (*arg_put)(void *, int , void *, int, char *),
    void *puthook, char *fmt, va_list ap)
{
	int assigned = 0;
	int width;
	int suppress;
	int plus;
	int done = 0;
	static u_char mask[] = {0, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};
	int value;
	u_char *base = databuf;
	char letter;
	char field_name[80];

#	define ARG_PUT(ARG) \
	do \
	{ \
		if (!suppress) { \
			if (arg_put) \
				(*arg_put)(puthook, (letter == 't' ? 'b' : letter), \
				    (void *)((long)(ARG)), 1, field_name); \
			else \
				*(va_arg(ap, int *)) = (ARG); \
			assigned++; \
		} \
		field_name[0] = 0; \
		suppress = 0; \
	} while (0)

	u_char bits = 0;	/* For bit fields */
	int shift = 0;		/* Bits already shifted out */
	suppress = 0;
	field_name[0] = 0;

	while (!done) {
		switch (letter = *fmt) {
		case ' ':	/* White space */
		case '\t':
		case '\r':
		case '\n':
		case '\f':
			fmt++;
			break;

		case '#':	/* Comment */
			while (*fmt && (*fmt != '\n'))
				fmt++;
			if (fmt)
				fmt++;	/* Skip '\n' */
			break;

		case '*':	/* Suppress assignment */
			fmt++;
			suppress = 1;
			break;

		case '{':	/* Field Name */
			{
				int i = 0;
				fmt++;	/* Skip '{' */
				while (*fmt && (*fmt != '}')) {
					if (i < sizeof(field_name)-1)
						field_name[i++] = *fmt;

					fmt++;
				}
				if (fmt)
					fmt++;	/* Skip '}' */
				field_name[i] = 0;
			}
			break;

		case 't':	/* Bit (field) */
		case 'b':	/* Bits */
			fmt++;
			width = strtol(fmt, &fmt, 10);
			if (width > 8)
				done = 1;
			else {
				if (shift <= 0) {
					bits = *databuf++;
					shift = 8;
				}
				value = (bits >> (shift - width)) & mask[width];

#if 0
				printf("shift %2d bits %02x value %02x width %2d mask %02x\n",
				    shift, bits, value, width, mask[width]);
#endif

				ARG_PUT(value);

				shift -= width;
			}

			break;

		case 'i':	/* Integral values */
			shift = 0;
			fmt++;
			width = strtol(fmt, &fmt, 10);
			switch (width) {
			case 1:
				ARG_PUT(*databuf);
				databuf++;
				break;

			case 2:
				ARG_PUT((*databuf) << 8 | *(databuf + 1));
				databuf += 2;
				break;

			case 3:
				ARG_PUT(
				    (*databuf) << 16 |
				    (*(databuf + 1)) << 8 |
				    *(databuf + 2));
				databuf += 3;
				break;

			case 4:
				ARG_PUT(
				    (*databuf) << 24 |
				    (*(databuf + 1)) << 16 |
				    (*(databuf + 2)) << 8 |
				    *(databuf + 3));
				databuf += 4;
				break;

				default:
				done = 1;
			}

			break;

		case 'c':	/* Characters (i.e., not swapped) */
		case 'z':	/* Characters with zeroed trailing spaces  */
			shift = 0;
			fmt++;
			width = strtol(fmt, &fmt, 10);
			if (!suppress) {
				if (arg_put)
					(*arg_put)(puthook, (letter == 't' ? 'b' : letter),
					    databuf, width, field_name);
				else {
					char *dest;
					dest = va_arg(ap, char *);
					bcopy(databuf, dest, width);
					if (letter == 'z') {
						char *p;
						for (p = dest + width - 1;
						(p >= (char *)dest) && (*p == ' '); p--)
							*p = 0;
					}
				}
				assigned++;
			}
			databuf += width;
			field_name[0] = 0;
			suppress = 0;
			break;

		case 's':	/* Seek */
			shift = 0;
			fmt++;
			if (*fmt == '+') {
				plus = 1;
				fmt++;
			} else
				plus = 0;

			if (tolower((unsigned char)*fmt) == 'v') {
				/* You can't suppress a seek value.  You also
				 * can't have a variable seek when you are using
				 * "arg_put".
				 */
				width = (arg_put) ? 0 : va_arg(ap, int);
				fmt++;
			} else
				width = strtol(fmt, &fmt, 10);

			if (plus)
				databuf += width;	/* Relative seek */
			else
				databuf = base + width;	/* Absolute seek */

			break;

		case 0:
			done = 1;
			break;

		default:
			fprintf(stderr, "Unknown letter in format: %c\n", letter);
			fmt++;
		}
	}

	return assigned;
}

int
scsireq_decode_visit(scsireq_t *scsireq, char *fmt,
    void (*arg_put)(void *, int , void *, int, char *), void *puthook)
{
	va_list ap;
	int ret;

	ret = do_buff_decode(scsireq->databuf, (size_t)scsireq->datalen,
	    arg_put, puthook, fmt, ap);
	va_end (ap);
	return (ret);
}

int
scsireq_buff_decode_visit(u_char *buff, size_t len, char *fmt,
    void (*arg_put)(void *, int, void *, int, char *), void *puthook)
{
	va_list ap;

	/* XXX */
	return do_buff_decode(buff, len, arg_put, puthook, fmt, ap);
}

/* next_field: Return the next field in a command specifier.  This
 * builds up a SCSI command using this trivial grammar:
 *
 * fields : field fields
 *        ;
 *
 * field : value
 *       | value ':' field_width
 *       ;
 *
 * field_width : digit
 *       | 'i' digit		// i2 = 2 byte integer, i3 = 3 byte integer etc.
 *       ;
 *
 * value : HEX_NUMBER
 *       | 'v'				// For indirection.
 *       ;
 *
 * Notes:
 *  Bit fields are specified MSB first to match the SCSI spec.
 *
 * Examples:
 *  TUR: "0 0 0 0 0 0"
 *  WRITE BUFFER: "38 v:3 0:2 0:3 v v:i3 v:i3 0", mode, buffer_id, list_length
 *
 * The function returns the value:
 *  0: For reached end, with error_p set if an error was found
 *  1: For valid stuff setup
 *  2: For "v" was entered as the value (implies use varargs)
 *
 */
static int
next_field(char **pp, char *fmt, int *width_p, int *value_p, char *name,
    int n_name, int *error_p, int *suppress_p)
{
	char *p = *pp;

	int something = 0;

	enum { BETWEEN_FIELDS, START_FIELD, GET_FIELD, DONE } state;

	int value = 0;
	int field_size;		/* Default to byte field type... */
	int field_width;	/* 1 byte wide */
	int is_error = 0;
	int suppress = 0;

	field_size = 8;		/* Default to byte field type... */
	*fmt = 'i';
	field_width = 1;	/* 1 byte wide */
	if (name)
		*name = 0;

	state = BETWEEN_FIELDS;

	while (state != DONE)
		switch (state)
		{
		case BETWEEN_FIELDS:
			if (*p == 0)
				state = DONE;
			else if (isspace((unsigned char)*p))
				p++;
			else if (*p == '#') {
				while (*p && *p != '\n')
					p++;

				if (p)
					p++;
				} else if (*p == '{') {
					int i = 0;

					p++;

					while (*p && *p != '}') {
						if (name && i < n_name) {
							name[i] = *p;
							i++;
						}
						p++;
					}

					if (name && i < n_name)
						name[i] = 0;

					if (*p == '}')
						p++;
				} else if (*p == '*') {
					p++;
					suppress = 1;
				} else if (isxdigit((unsigned char)*p)) {
					something = 1;
					value = strtol(p, &p, 16);
					state = START_FIELD;
				} else if (tolower((unsigned char)*p) == 'v') {
					p++;
					something = 2;
					value = *value_p;
					state = START_FIELD;
				}
				/* try to work without the 'v' */
				else if (tolower((unsigned char)*p) == 'i') {
					something = 2;
					value = *value_p;
					p++;

					*fmt = 'i';
					field_size = 8;
					field_width = strtol(p, &p, 10);
					state = DONE;
				} else if (tolower((unsigned char)*p) == 't') {
					/* XXX: B can't work: Sees the 'b'
					 * as a hex digit in "isxdigit".
					 * try "t" for bit field.
					 */
					something = 2;
					value = *value_p;
					p++;

					*fmt = 'b';
					field_size = 1;
					field_width = strtol(p, &p, 10);
					state = DONE;
				} else if (tolower((unsigned char)*p) == 's') { /* Seek */
					*fmt = 's';
					p++;
					if (tolower((unsigned char)*p) == 'v') {
						p++;
						something = 2;
						value = *value_p;
					} else {
						something = 1;
						value = strtol(p, &p, 0);
					}
					state = DONE;
				} else {
					fprintf(stderr, "Invalid starting character: %c\n", *p);
					is_error = 1;
					state = DONE;
				}
			break;

		case START_FIELD:
			if (*p == ':') {
				p++;
				field_size = 1;		/* Default to bits when specified */
				state = GET_FIELD;
			} else
				state = DONE;
			break;

		case GET_FIELD:
			if (isdigit((unsigned char)*p)) {
				*fmt = 'b';
				field_size = 1;
				field_width = strtol(p, &p, 10);
				state = DONE;
			} else if (*p == 'i') {	/* Integral (bytes) */
				p++;

				*fmt = 'i';
				field_size = 8;
				field_width = strtol(p, &p, 10);
				state = DONE;
			} else if (*p == 'b') {	/* Bits */
				p++;

				*fmt = 'b';
				field_size = 1;
				field_width = strtol(p, &p, 10);
				state = DONE;
			} else {
				fprintf(stderr, "Invalid startfield %c (%02x)\n",
				    *p, *p);
				is_error = 1;
				state = DONE;
			}
			break;

		case DONE:
			break;
		}

	if (is_error) {
		*error_p = 1;
		return 0;
	}

	*error_p = 0;
	*pp = p;
	*width_p = field_width * field_size;
	*value_p = value;
	*suppress_p = suppress;

	return something;
}

static int
do_encode(u_char *buff, size_t vec_max, size_t *used,
    int (*arg_get)(void *, char *),
    void *gethook, char *fmt, va_list ap)
{
	int ind;
	int shift;
	u_char val;
	int ret;
	int width, value, error, suppress;
	char c;
	int encoded = 0;
	char field_name[80];

	ind = 0;
	shift = 0;
	val = 0;

 	while ((ret = next_field(&fmt, &c, &width, &value, field_name,
	    sizeof(field_name), &error, &suppress)))
	{
		encoded++;

		if (ret == 2) {
			if (suppress)
				value = 0;
			else
				value = arg_get ? (*arg_get)(gethook, field_name) : va_arg(ap, int);
		}

#if 0
		printf(
		    "do_encode: ret %d fmt %c width %d value %d name \"%s\""
		    "error %d suppress %d\n",
		    ret, c, width, value, field_name, error, suppress);
#endif

		if (c == 's')	/* Absolute seek */
		{
			ind = value;
			continue;
		}

		if (width < 8)	/* A width of < 8 is a bit field. */
		{

			/* This is a bit field.  We start with the high bits
			 * so it reads the same as the SCSI spec.
			 */

			shift += width;

			val |= (value << (8 - shift));

			if (shift == 8) {
				if (ind < vec_max) {
					buff[ind++] = val;
					val = 0;
				}
				shift = 0;
			}
		} else {
			if (shift) {
				if (ind < vec_max) {
					buff[ind++] = val;
					val = 0;
				}
				shift = 0;
			}
			switch (width)
			{
			case 8:		/* 1 byte integer */
				if (ind < vec_max)
					buff[ind++] = value;
				break;

			case 16:	/* 2 byte integer */
				if (ind < vec_max - 2 + 1) {
					buff[ind++] = value >> 8;
					buff[ind++] = value;
				}
				break;

			case 24:	/* 3 byte integer */
				if (ind < vec_max - 3 + 1) {
					buff[ind++] = value >> 16;
					buff[ind++] = value >> 8;
					buff[ind++] = value;
				}
				break;

			case 32:	/* 4 byte integer */
				if (ind < vec_max - 4 + 1) {
					buff[ind++] = value >> 24;
					buff[ind++] = value >> 16;
					buff[ind++] = value >> 8;
					buff[ind++] = value;
				}
				break;

			default:
				fprintf(stderr, "do_encode: Illegal width\n");
				break;
			}
		}
	}

	/* Flush out any remaining bits */
	if (shift && ind < vec_max) {
		buff[ind++] = val;
		val = 0;
	}


	if (used)
		*used = ind;

	if (error)
		return -1;

	return encoded;
}

scsireq_t *
scsireq_build(scsireq_t *scsireq, u_long datalen, caddr_t databuf,
    u_long flags, char *cmd_spec, ...)
{
	size_t cmdlen;
	va_list ap;

	if (scsireq == 0)
		return 0;

	scsireq_reset(scsireq);

	if (databuf) {
		scsireq->databuf = databuf;
		scsireq->datalen = datalen;
		scsireq->flags = flags;
	}
	else if (datalen) {
		/* XXX: Good way to get a memory leak.  Perhaps this should be
		 * removed.
		 */
		if ( (scsireq->databuf = malloc(datalen)) == NULL)
			return 0;

		scsireq->datalen = datalen;
		scsireq->flags = flags;
	}

 	va_start(ap, cmd_spec);

 	if (do_encode(scsireq->cmd, CMDBUFLEN, &cmdlen, 0, 0, cmd_spec, ap) == -1)
 		return 0;
	va_end (ap);

	scsireq->cmdlen = cmdlen;
	return scsireq;
}

scsireq_t
*scsireq_build_visit(scsireq_t *scsireq, u_long datalen, caddr_t databuf,
    u_long flags, char *cmd_spec,
    int (*arg_get)(void *hook, char *field_name), void *gethook)
{
	size_t cmdlen;
	va_list ap;

	if (scsireq == 0)
		return 0;

	scsireq_reset(scsireq);

	if (databuf) {
		scsireq->databuf = databuf;
		scsireq->datalen = datalen;
		scsireq->flags = flags;
	} else if (datalen) {
		/* XXX: Good way to get a memory leak.  Perhaps this should be
		 * removed.
		 */
		if ( (scsireq->databuf = malloc(datalen)) == NULL)
			return 0;

		scsireq->datalen = datalen;
		scsireq->flags = flags;
	}

 	if (do_encode(scsireq->cmd, CMDBUFLEN, &cmdlen, arg_get, gethook,
	    cmd_spec, ap) == -1)
 		return 0;

	scsireq->cmdlen = cmdlen;

	return scsireq;
}

int
scsireq_buff_encode_visit(u_char *buff, size_t len, char *fmt,
	int (*arg_get)(void *hook, char *field_name), void *gethook)
{
	va_list ap;
	return do_encode(buff, len, 0, arg_get, gethook, fmt, ap);
}

int
scsireq_encode_visit(scsireq_t *scsireq, char *fmt,
    int (*arg_get)(void *hook, char *field_name), void *gethook)
{
	va_list ap;
	return do_encode(scsireq->databuf, scsireq->datalen, 0,
	    arg_get, gethook, fmt, ap);
}

FILE *
scsi_debug_output(char *s)
{
	if (s == 0)
		behave.db_f = 0;
	else {
		behave.db_f = fopen(s, "w");

		if (behave.db_f == 0)
			behave.db_f = stderr;
	}

	return behave.db_f;
}

#define SCSI_TRUNCATE -1

typedef struct scsi_assoc {
	int code;
	char *text;
} scsi_assoc_t;

static scsi_assoc_t retsts[] =
{
	{ SCCMD_OK, "No error" },
	{ SCCMD_TIMEOUT, "Command Timeout" },
	{ SCCMD_BUSY, "Busy" },
	{ SCCMD_SENSE, "Sense Returned" },
	{ SCCMD_UNKNOWN, "Unknown return status" },

	{ 0, 0 }
};

static char *
scsi_assoc_text(int code, scsi_assoc_t *tab)
{
	while (tab->text) {
		if (tab->code == code)
			return tab->text;

		tab++;
	}

	return "Unknown code";
}

void
scsi_dump(FILE *f, char *text, u_char *p, int req, int got, int dump_print)
{
	int i;
	int trunc = 0;

	if (f == 0 || req == 0)
		return;

	fprintf(f, "%s (%d of %d):\n", text, got, req);

	if (behave.db_trunc != -1 && got > behave.db_trunc) {
		trunc = 1;
		got = behave.db_trunc;
	}

	for (i = 0; i < got; i++) {
		fprintf(f, "%02x", p[i]);

		putc(' ', f);

		if ((i % 16) == 15 || i == got - 1) {
			int j;
			if (dump_print) {
				fprintf(f, " # ");
				for (j = i - 15; j <= i; j++)
					putc((isprint(p[j]) ? p[j] : '.'), f);

				putc('\n', f);
			} else
				putc('\n', f);
		}
	}

	fprintf(f, "%s", (trunc) ? "(truncated)...\n" : "\n");
}

/* XXX: sense_7x_dump and scsi_sense dump was just sort of
 * grabbed out of the old ds
 * library and not really merged in carefully.  It should use the
 * new buffer decoding stuff.
 */

/* Get unsigned long.
 */
static u_long
g_u_long(u_char *s)
{
	return (s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3];
}

/* In the old software you could patch in a special error table:
 */
static scsi_assoc_t *error_table = 0;

static void
sense_7x_dump(FILE *f, scsireq_t *scsireq)
{
	int code;
	u_char *s = (u_char *)scsireq->sense;
	int valid = (*s) & 0x80;
	u_long val;

	static scsi_assoc_t sense[] = {
		{ 0, "No sense" },
		{ 1, "Recovered error" },
		{ 2, "Not Ready" },
		{ 3, "Medium error" },
		{ 4, "Hardware error" },
		{ 5, "Illegal request" },
		{ 6, "Unit attention" },
		{ 7, "Data protect" },
		{ 8, "Blank check" },
		{ 9, "Vendor specific" },
		{ 0xa, "Copy aborted" },
		{ 0xb, "Aborted Command" },
		{ 0xc, "Equal" },
		{ 0xd, "Volume overflow" },
		{ 0xe, "Miscompare" },
		{ 0, 0 },
	};

	static scsi_assoc_t code_tab[] = {
		{0x70, "current errors"},
		{0x71, "deferred errors"},
	};

	fprintf(f, "Error code is \"%s\"\n", scsi_assoc_text(s[0]&0x7F, code_tab));
	fprintf(f, "Segment number is %02x\n", s[1]);

	if (s[2] & 0x20)
		fprintf(f, "Incorrect Length Indicator is set.\n");

	fprintf(f, "Sense key is \"%s\"\n", scsi_assoc_text(s[2] & 0x7, sense));

	val = g_u_long(s + 3);
	fprintf(f, "The Information field is%s %08lx (%ld).\n",
	    valid ? "" : " not valid but contains", (long)val, (long)val);

	val = g_u_long(s + 8);
	fprintf(f, "The Command Specific Information field is %08lx (%ld).\n",
	    (long)val, (long)val);

	fprintf(f, "Additional sense code: %02x\n", s[12]);
	fprintf(f, "Additional sense code qualifier: %02x\n", s[13]);

	code = (s[12] << 8) | s[13];

	if (error_table)
		fprintf(f, "%s\n", scsi_assoc_text(code, error_table));

	if (s[15] & 0x80) {
		if ((s[2] & 0x7) == 0x05)	/* Illegal request */
		{
			int byte;
			u_char value, bit;
			int bad_par = ((s[15] & 0x40) == 0);
			fprintf(f, "Illegal value in the %s.\n",
			    (bad_par ? "parameter list" : "command descriptor block"));
			byte = ((s[16] << 8) | s[17]);
			value = bad_par ? (u_char)scsireq->databuf[byte] : (u_char)scsireq->cmd[byte];
			bit = s[15] & 0x7;
			if (s[15] & 0x08)
				fprintf(f, "Bit %d of byte %d (value %02x) is illegal.\n",
				    bit, byte, value);
			else
				fprintf(f, "Byte %d (value %02x) is illegal.\n", byte, value);
		} else 	{
			fprintf(f, "Sense Key Specific (valid but not illegal request):\n");
			fprintf(f, "%02x %02x %02x\n", s[15] & 0x7f, s[16], s[17]);
		}
	}
}

/* scsi_sense_dump: Dump the sense portion of the scsireq structure.
 */
static void
scsi_sense_dump(FILE *f, scsireq_t *scsireq)
{
	u_char *s = (u_char *)scsireq->sense;
	int code = (*s) & 0x7f;

	if (scsireq->senselen_used == 0) {
		fprintf(f, "No sense sent.\n");
		return;
	}

#if 0
	if (!valid)
		fprintf(f, "The sense data is not valid.\n");
#endif

	switch (code) {
	case 0x70:
	case 0x71:
		sense_7x_dump(f, scsireq);
		break;

	default:
		fprintf(f, "No sense dump for error code %02x.\n", code);
	}
	scsi_dump(f, "sense", s, scsireq->senselen, scsireq->senselen_used, 0);
}

static void
scsi_retsts_dump(FILE *f, scsireq_t *scsireq)
{
	if (scsireq->retsts == 0)
		return;

	fprintf(f, "return status %d (%s)",
	    scsireq->retsts, scsi_assoc_text(scsireq->retsts, retsts));

	switch (scsireq->retsts) {
	case SCCMD_TIMEOUT:
		fprintf(f, " after %ld ms", scsireq->timeout);
		break;

	default:
		break;
	}
}

int
scsi_debug(FILE *f, int ret, scsireq_t *scsireq)
{
	char *d;
	if (f == 0)
		return 0;

	fprintf(f, "SCIOCCOMMAND ioctl");

	if (ret == 0)
		fprintf(f, ": Command accepted.");
	else {
		if (ret != -1)
			fprintf(f, ", return value %d?", ret);

		if (errno) {
			fprintf(f, ": %s", strerror(errno));
			errno = 0;
		}
	}

	fputc('\n', f);

	if (ret == 0 && (scsireq->status || scsireq->retsts || behave.db_level))
	{
		scsi_retsts_dump(f, scsireq);

		if (scsireq->status)
			fprintf(f, " host adapter status %d\n", scsireq->status);

		if (scsireq->flags & SCCMD_READ)
			d = "Data in";
		else if (scsireq->flags & SCCMD_WRITE)
			d = "Data out";
		else
			d = "No data transfer?";

		if (scsireq->cmdlen == 0)
			fprintf(f, "Zero length command????\n");

		scsi_dump(f, "Command out",
		    (u_char *)scsireq->cmd, scsireq->cmdlen, scsireq->cmdlen, 0);
		scsi_dump(f, d,
		    (u_char *)scsireq->databuf, scsireq->datalen,
	 	scsireq->datalen_used, 1);
		scsi_sense_dump(f, scsireq);
	}

	fflush(f);

	return ret;
}

static char *debug_output;

int
scsi_open(const char *path, int flags)
{
	int fd = open(path, flags);

	if (fd != -1) {
		char *p;
		debug_output = getenv("SU_DEBUG_OUTPUT");
		(void)scsi_debug_output(debug_output);

		if ((p = getenv("SU_DEBUG_LEVEL")))
			sscanf(p, "%d", &behave.db_level);

		if ((p = getenv("SU_DEBUG_TRUNCATE")))
			sscanf(p, "%d", &behave.db_trunc);
		else
			behave.db_trunc = SCSI_TRUNCATE;
	}

	return fd;
}

int
scsireq_enter(int fid, scsireq_t *scsireq)
{
	int ret;

	ret = ioctl(fid, SCIOCCOMMAND, (void *)scsireq);

	if (behave.db_f)
		scsi_debug(behave.db_f, ret, scsireq);

	return ret;
}

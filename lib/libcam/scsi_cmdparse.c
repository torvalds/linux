/*
 * Taken from the original FreeBSD user SCSI library.
 */
/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994 HD Associates
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
 * From: scsi.c,v 1.8 1997/02/22 15:07:54 peter Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/errno.h>
#include <stdarg.h>
#include <fcntl.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_message.h>
#include "camlib.h"

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
do_buff_decode(u_int8_t *buff, size_t len,
	       void (*arg_put)(void *, int , void *, int, char *),
	       void *puthook, const char *fmt, va_list *ap)
{
	int ind = 0;
	int assigned = 0;
	int width;
	int suppress;
	int plus;
	int done = 0;
	static u_char mask[] = {0, 0x01, 0x03, 0x07, 0x0f,
				   0x1f, 0x3f, 0x7f, 0xff};
	int value;
	char *intendp;
	char letter;
	char field_name[80];

#define ARG_PUT(ARG) \
	do { \
		if (!suppress) { \
			if (arg_put) \
				(*arg_put)(puthook, (letter == 't' ? 'b' : \
				    letter), (void *)((long)(ARG)), width, \
				    field_name); \
			else \
				*(va_arg(*ap, int *)) = (ARG); \
			assigned++; \
		} \
		field_name[0] = '\0'; \
		suppress = 0; \
	} while (0)

	u_char bits = 0;	/* For bit fields */
	int shift = 0;		/* Bits already shifted out */
	suppress = 0;
	field_name[0] = '\0';

	while (!done) {
		switch(letter = *fmt) {
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
				if (i < sizeof(field_name))
					field_name[i++] = *fmt;

				fmt++;
			}
			if (*fmt != '\0')
				fmt++;	/* Skip '}' */
			field_name[i] = '\0';
			break;
		}

		case 't':	/* Bit (field) */
		case 'b':	/* Bits */
			fmt++;
			width = strtol(fmt, &intendp, 10);
			fmt = intendp;
			if (width > 8)
				done = 1;
			else {
				if (shift <= 0) {
					if (ind >= len) {
						done = 1;
						break;
					}
					bits = buff[ind++];
					shift = 8;
				}
				value = (bits >> (shift - width)) &
					 mask[width];

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
			width = strtol(fmt, &intendp, 10);
			fmt = intendp;
			if (ind + width > len) {
				done = 1;
				break;
			}
			switch(width) {
			case 1:
				ARG_PUT(buff[ind]);
				ind++;
				break;

			case 2:
				ARG_PUT(buff[ind] << 8 | buff[ind + 1]);
				ind += 2;
				break;

			case 3:
				ARG_PUT(buff[ind] << 16 |
					buff[ind + 1] << 8 | buff[ind + 2]);
				ind += 3;
				break;

			case 4:
				ARG_PUT(buff[ind] << 24 | buff[ind + 1] << 16 |
					buff[ind + 2] << 8 | buff[ind + 3]);
				ind += 4;
				break;

			default:
				done = 1;
				break;
			}

			break;

		case 'c':	/* Characters (i.e., not swapped) */
		case 'z':	/* Characters with zeroed trailing spaces */
			shift = 0;
			fmt++;
			width = strtol(fmt, &intendp, 10);
			fmt = intendp;
			if (ind + width > len) {
				done = 1;
				break;
			}
			if (!suppress) {
				if (arg_put != NULL)
					(*arg_put)(puthook,
					    (letter == 't' ? 'b' : letter),
					    &buff[ind], width, field_name);
				else {
					char *dest;
					dest = va_arg(*ap, char *);
					bcopy(&buff[ind], dest, width);
					if (letter == 'z') {
						char *p;
						for (p = dest + width - 1;
						    p >= dest && *p == ' ';
						    p--)
							*p = '\0';
					}
				}
				assigned++;
			}
			ind += width;
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

			if (tolower(*fmt) == 'v') {
				/*
				 * You can't suppress a seek value.  You also
				 * can't have a variable seek when you are using
				 * "arg_put".
				 */
				width = (arg_put) ? 0 : va_arg(*ap, int);
				fmt++;
			} else {
				width = strtol(fmt, &intendp, 10);
				fmt = intendp;
			}

			if (plus)
				ind += width;	/* Relative seek */
			else
				ind = width;	/* Absolute seek */

			break;

		case 0:
			done = 1;
			break;

		default:
			fprintf(stderr, "Unknown letter in format: %c\n",
				letter);
			fmt++;
			break;
		}
	}

	return (assigned);
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
next_field(const char **pp, char *fmt, int *width_p, int *value_p, char *name,
	   int n_name, int *error_p, int *suppress_p)
{
	const char *p = *pp;
	char *intendp;

	int something = 0;

	enum {
		BETWEEN_FIELDS,
		START_FIELD,
		GET_FIELD,
		DONE,
	} state;

	int value = 0;
	int field_size;		/* Default to byte field type... */
	int field_width;	/* 1 byte wide */
	int is_error = 0;
	int suppress = 0;

	field_size = 8;		/* Default to byte field type... */
	*fmt = 'i';
	field_width = 1;	/* 1 byte wide */
	if (name != NULL)
		*name = '\0';

	state = BETWEEN_FIELDS;

	while (state != DONE) {
		switch(state) {
		case BETWEEN_FIELDS:
			if (*p == '\0')
				state = DONE;
			else if (isspace(*p))
				p++;
			else if (*p == '#') {
				while (*p && *p != '\n')
					p++;
				if (*p != '\0')
					p++;
			} else if (*p == '{') {
				int i = 0;

				p++;

				while (*p && *p != '}') {
					if(name && i < n_name) {
						name[i] = *p;
						i++;
					}
					p++;
				}

				if(name && i < n_name)
					name[i] = '\0';

				if (*p == '}')
					p++;
			} else if (*p == '*') {
				p++;
				suppress = 1;
			} else if (isxdigit(*p)) {
				something = 1;
				value = strtol(p, &intendp, 16);
				p = intendp;
				state = START_FIELD;
			} else if (tolower(*p) == 'v') {
				p++;
				something = 2;
				value = *value_p;
				state = START_FIELD;
			} else if (tolower(*p) == 'i') {
				/*
				 * Try to work without the "v".
				 */
				something = 2;
				value = *value_p;
				p++;

				*fmt = 'i';
				field_size = 8;
				field_width = strtol(p, &intendp, 10);
				p = intendp;
				state = DONE;

			} else if (tolower(*p) == 't') {
				/*
				 * XXX: B can't work: Sees the 'b' as a
				 * hex digit in "isxdigit".  try "t" for
				 * bit field.
				 */
				something = 2;
				value = *value_p;
				p++;

				*fmt = 'b';
				field_size = 1;
				field_width = strtol(p, &intendp, 10);
				p = intendp;
				state = DONE;
			} else if (tolower(*p) == 's') {
				/* Seek */
				*fmt = 's';
				p++;
				if (tolower(*p) == 'v') {
					p++;
					something = 2;
					value = *value_p;
				} else {
					something = 1;
					value = strtol(p, &intendp, 0);
					p = intendp;
				}
				state = DONE;
			} else {
				fprintf(stderr, "Invalid starting "
					"character: %c\n", *p);
				is_error = 1;
				state = DONE;
			}
			break;

		case START_FIELD:
			if (*p == ':') {
				p++;
				field_size = 1;		/* Default to bits
							   when specified */
				state = GET_FIELD;
			} else
				state = DONE;
			break;

		case GET_FIELD:
			if (isdigit(*p)) {
				*fmt = 'b';
				field_size = 1;
				field_width = strtol(p, &intendp, 10);
				p = intendp;
				state = DONE;
			} else if (*p == 'i') {

				/* Integral (bytes) */
				p++;

				*fmt = 'i';
				field_size = 8;
				field_width = strtol(p, &intendp, 10);
				p = intendp;
				state = DONE;
			} else if (*p == 'b') {

				/* Bits */
				p++;

				*fmt = 'b';
				field_size = 1;
				field_width = strtol(p, &intendp, 10);
				p = intendp;
				state = DONE;
			} else {
				fprintf(stderr, "Invalid startfield %c "
					"(%02x)\n", *p, *p);
				is_error = 1;
				state = DONE;
			}
			break;

		case DONE:
			break;
		}
	}

	if (is_error) {
		*error_p = 1;
		return (0);
	}

	*error_p = 0;
	*pp = p;
	*width_p = field_width * field_size;
	*value_p = value;
	*suppress_p = suppress;

	return (something);
}

static int
do_encode(u_char *buff, size_t vec_max, size_t *used,
	  int (*arg_get)(void *, char *), void *gethook, const char *fmt,
	  va_list *ap)
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
				 sizeof(field_name), &error, &suppress))) {
		encoded++;

		if (ret == 2) {
			if (suppress)
				value = 0;
			else
				value = arg_get != NULL ?
					(*arg_get)(gethook, field_name) :
					va_arg(*ap, int);
		}

#if 0
		printf(
"do_encode: ret %d fmt %c width %d value %d name \"%s\" error %d suppress %d\n",
		ret, c, width, value, field_name, error, suppress);
#endif
		/* Absolute seek */
		if (c == 's') {
			ind = value;
			continue;
		}

		/* A width of < 8 is a bit field. */
		if (width < 8) {

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
			switch(width) {
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

	/* Flush out any remaining bits
	 */
	if (shift && ind < vec_max) {
		buff[ind++] = val;
		val = 0;
	}


	if (used)
		*used = ind;

	if (error)
		return (-1);

	return (encoded);
}

int
csio_decode(struct ccb_scsiio *csio, const char *fmt, ...)
{
	va_list ap;
	int retval;

	va_start(ap, fmt);

	retval = do_buff_decode(csio->data_ptr, (size_t)csio->dxfer_len,
	    NULL, NULL, fmt, &ap);

	va_end(ap);

	return (retval);
}

int
csio_decode_visit(struct ccb_scsiio *csio, const char *fmt,
		  void (*arg_put)(void *, int, void *, int, char *),
		  void *puthook)
{

	/*
	 * We need some way to output things; we can't do it without
	 * the arg_put function.
	 */
	if (arg_put == NULL)
		return (-1);

	return (do_buff_decode(csio->data_ptr, (size_t)csio->dxfer_len,
		    arg_put, puthook, fmt, NULL));
}

int
buff_decode(u_int8_t *buff, size_t len, const char *fmt, ...)
{
	va_list ap;
	int retval;

	va_start(ap, fmt);

	retval = do_buff_decode(buff, len, NULL, NULL, fmt, &ap);

	va_end(ap);

	return (retval);
}

int
buff_decode_visit(u_int8_t *buff, size_t len, const char *fmt,
		  void (*arg_put)(void *, int, void *, int, char *),
		  void *puthook)
{

	/*
	 * We need some way to output things; we can't do it without
	 * the arg_put function.
	 */
	if (arg_put == NULL)
		return (-1);

	return (do_buff_decode(buff, len, arg_put, puthook, fmt, NULL));
}

/*
 * Build a SCSI CCB, given the command and data pointers and a format
 * string describing the 
 */
int
csio_build(struct ccb_scsiio *csio, u_int8_t *data_ptr, u_int32_t dxfer_len,
	   u_int32_t flags, int retry_count, int timeout, const char *cmd_spec,
	   ...)
{
	size_t cmdlen;
	int retval;
	va_list ap;

	if (csio == NULL)
		return (0);

	bzero(csio, sizeof(struct ccb_scsiio));

	va_start(ap, cmd_spec);

	if ((retval = do_encode(csio->cdb_io.cdb_bytes, SCSI_MAX_CDBLEN,
				&cmdlen, NULL, NULL, cmd_spec, &ap)) == -1)
		goto done;

	cam_fill_csio(csio,
		      /* retries */ retry_count,
		      /* cbfcnp */ NULL,
		      /* flags */ flags,
		      /* tag_action */ MSG_SIMPLE_Q_TAG,
		      /* data_ptr */ data_ptr,
		      /* dxfer_len */ dxfer_len,
		      /* sense_len */ SSD_FULL_SIZE,
		      /* cdb_len */ cmdlen,
		      /* timeout */ timeout ? timeout : 5000);

done:
	va_end(ap);

	return (retval);
}

int
csio_build_visit(struct ccb_scsiio *csio, u_int8_t *data_ptr,
		 u_int32_t dxfer_len, u_int32_t flags, int retry_count,
		 int timeout, const char *cmd_spec,
		 int (*arg_get)(void *hook, char *field_name), void *gethook)
{
	size_t cmdlen;
	int retval;

	if (csio == NULL)
		return (0);

	/*
	 * We need something to encode, but we can't get it without the
	 * arg_get function.
	 */
	if (arg_get == NULL)
		return (-1);

	bzero(csio, sizeof(struct ccb_scsiio));

	if ((retval = do_encode(csio->cdb_io.cdb_bytes, SCSI_MAX_CDBLEN,
				&cmdlen, arg_get, gethook, cmd_spec, NULL)) == -1)
		return (retval);

	cam_fill_csio(csio,
		      /* retries */ retry_count,
		      /* cbfcnp */ NULL,
		      /* flags */ flags,
		      /* tag_action */ MSG_SIMPLE_Q_TAG,
		      /* data_ptr */ data_ptr,
		      /* dxfer_len */ dxfer_len,
		      /* sense_len */ SSD_FULL_SIZE,
		      /* cdb_len */ cmdlen,
		      /* timeout */ timeout ? timeout : 5000);

	return (retval);
}

int
csio_encode(struct ccb_scsiio *csio, const char *fmt, ...)
{
	va_list ap;
	int retval;

	if (csio == NULL)
		return (0);

	va_start(ap, fmt);

	retval = do_encode(csio->data_ptr, csio->dxfer_len, NULL, NULL, NULL,
	    fmt, &ap);

	va_end(ap);

	return (retval);
}

int
buff_encode_visit(u_int8_t *buff, size_t len, const char *fmt,
		  int (*arg_get)(void *hook, char *field_name), void *gethook)
{

	/*
	 * We need something to encode, but we can't get it without the
	 * arg_get function.
	 */
	if (arg_get == NULL)
		return (-1);

	return (do_encode(buff, len, NULL, arg_get, gethook, fmt, NULL));
}

int
csio_encode_visit(struct ccb_scsiio *csio, const char *fmt,
		  int (*arg_get)(void *hook, char *field_name), void *gethook)
{

	/*
	 * We need something to encode, but we can't get it without the
	 * arg_get function.
	 */
	if (arg_get == NULL)
		return (-1);

	return (do_encode(csio->data_ptr, csio->dxfer_len, NULL, arg_get,
			 gethook, fmt, NULL));
}

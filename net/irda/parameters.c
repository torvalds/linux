/*********************************************************************
 *
 * Filename:      parameters.c
 * Version:       1.0
 * Description:   A more general way to handle (pi,pl,pv) parameters
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Jun  7 10:25:11 1999
 * Modified at:   Sun Jan 30 14:08:39 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1999-2000 Dag Brattli, All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *     MA 02111-1307 USA
 *
 ********************************************************************/

#include <linux/types.h>
#include <linux/module.h>

#include <asm/unaligned.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/parameters.h>

static int irda_extract_integer(void *self, __u8 *buf, int len, __u8 pi,
				PV_TYPE type, PI_HANDLER func);
static int irda_extract_string(void *self, __u8 *buf, int len, __u8 pi,
			       PV_TYPE type, PI_HANDLER func);
static int irda_extract_octseq(void *self, __u8 *buf, int len, __u8 pi,
			       PV_TYPE type, PI_HANDLER func);
static int irda_extract_no_value(void *self, __u8 *buf, int len, __u8 pi,
				 PV_TYPE type, PI_HANDLER func);

static int irda_insert_integer(void *self, __u8 *buf, int len, __u8 pi,
			       PV_TYPE type, PI_HANDLER func);
static int irda_insert_no_value(void *self, __u8 *buf, int len, __u8 pi,
				PV_TYPE type, PI_HANDLER func);

static int irda_param_unpack(__u8 *buf, char *fmt, ...);

/* Parameter value call table. Must match PV_TYPE */
static PV_HANDLER pv_extract_table[] = {
	irda_extract_integer, /* Handler for any length integers */
	irda_extract_integer, /* Handler for 8  bits integers */
	irda_extract_integer, /* Handler for 16 bits integers */
	irda_extract_string,  /* Handler for strings */
	irda_extract_integer, /* Handler for 32 bits integers */
	irda_extract_octseq,  /* Handler for octet sequences */
	irda_extract_no_value /* Handler for no value parameters */
};

static PV_HANDLER pv_insert_table[] = {
	irda_insert_integer, /* Handler for any length integers */
	irda_insert_integer, /* Handler for 8  bits integers */
	irda_insert_integer, /* Handler for 16 bits integers */
	NULL,                /* Handler for strings */
	irda_insert_integer, /* Handler for 32 bits integers */
	NULL,                /* Handler for octet sequences */
	irda_insert_no_value /* Handler for no value parameters */
};

/*
 * Function irda_insert_no_value (self, buf, len, pi, type, func)
 */
static int irda_insert_no_value(void *self, __u8 *buf, int len, __u8 pi,
				PV_TYPE type, PI_HANDLER func)
{
	irda_param_t p;
	int ret;

	p.pi = pi;
	p.pl = 0;

	/* Call handler for this parameter */
	ret = (*func)(self, &p, PV_GET);

	/* Extract values anyway, since handler may need them */
	irda_param_pack(buf, "bb", p.pi, p.pl);

	if (ret < 0)
		return ret;

	return 2; /* Inserted pl+2 bytes */
}

/*
 * Function irda_extract_no_value (self, buf, len, type, func)
 *
 *    Extracts a parameter without a pv field (pl=0)
 *
 */
static int irda_extract_no_value(void *self, __u8 *buf, int len, __u8 pi,
				 PV_TYPE type, PI_HANDLER func)
{
	irda_param_t p;
	int ret;

	/* Extract values anyway, since handler may need them */
	irda_param_unpack(buf, "bb", &p.pi, &p.pl);

	/* Call handler for this parameter */
	ret = (*func)(self, &p, PV_PUT);

	if (ret < 0)
		return ret;

	return 2; /* Extracted pl+2 bytes */
}

/*
 * Function irda_insert_integer (self, buf, len, pi, type, func)
 */
static int irda_insert_integer(void *self, __u8 *buf, int len, __u8 pi,
			       PV_TYPE type, PI_HANDLER func)
{
	irda_param_t p;
	int n = 0;
	int err;

	p.pi = pi;             /* In case handler needs to know */
	p.pl = type & PV_MASK; /* The integer type codes the lenght as well */
	p.pv.i = 0;            /* Clear value */

	/* Call handler for this parameter */
	err = (*func)(self, &p, PV_GET);
	if (err < 0)
		return err;

	/*
	 * If parameter lenght is still 0, then (1) this is an any length
	 * integer, and (2) the handler function does not care which length
	 * we choose to use, so we pick the one the gives the fewest bytes.
	 */
	if (p.pl == 0) {
		if (p.pv.i < 0xff) {
			IRDA_DEBUG(2, "%s(), using 1 byte\n", __FUNCTION__);
			p.pl = 1;
		} else if (p.pv.i < 0xffff) {
			IRDA_DEBUG(2, "%s(), using 2 bytes\n", __FUNCTION__);
			p.pl = 2;
		} else {
			IRDA_DEBUG(2, "%s(), using 4 bytes\n", __FUNCTION__);
			p.pl = 4; /* Default length */
		}
	}
	/* Check if buffer is long enough for insertion */
	if (len < (2+p.pl)) {
		IRDA_WARNING("%s: buffer to short for insertion!\n",
			     __FUNCTION__);
		return -1;
	}
	IRDA_DEBUG(2, "%s(), pi=%#x, pl=%d, pi=%d\n", __FUNCTION__,
		   p.pi, p.pl, p.pv.i);
	switch (p.pl) {
	case 1:
		n += irda_param_pack(buf, "bbb", p.pi, p.pl, (__u8) p.pv.i);
		break;
	case 2:
		if (type & PV_BIG_ENDIAN)
			p.pv.i = cpu_to_be16((__u16) p.pv.i);
		else
			p.pv.i = cpu_to_le16((__u16) p.pv.i);
		n += irda_param_pack(buf, "bbs", p.pi, p.pl, (__u16) p.pv.i);
		break;
	case 4:
		if (type & PV_BIG_ENDIAN)
			cpu_to_be32s(&p.pv.i);
		else
			cpu_to_le32s(&p.pv.i);
		n += irda_param_pack(buf, "bbi", p.pi, p.pl, p.pv.i);

		break;
	default:
		IRDA_WARNING("%s: length %d not supported\n",
			     __FUNCTION__, p.pl);
		/* Skip parameter */
		return -1;
	}

	return p.pl+2; /* Inserted pl+2 bytes */
}

/*
 * Function irda_extract integer (self, buf, len, pi, type, func)
 *
 *    Extract a possibly variable length integer from buffer, and call
 *    handler for processing of the parameter
 */
static int irda_extract_integer(void *self, __u8 *buf, int len, __u8 pi,
				PV_TYPE type, PI_HANDLER func)
{
	irda_param_t p;
	int n = 0;
	int extract_len;	/* Real lenght we extract */
	int err;

	p.pi = pi;     /* In case handler needs to know */
	p.pl = buf[1]; /* Extract lenght of value */
	p.pv.i = 0;    /* Clear value */
	extract_len = p.pl;	/* Default : extract all */

	/* Check if buffer is long enough for parsing */
	if (len < (2+p.pl)) {
		IRDA_WARNING("%s: buffer to short for parsing! "
			     "Need %d bytes, but len is only %d\n",
			     __FUNCTION__, p.pl, len);
		return -1;
	}

	/*
	 * Check that the integer length is what we expect it to be. If the
	 * handler want a 16 bits integer then a 32 bits is not good enough
	 * PV_INTEGER means that the handler is flexible.
	 */
	if (((type & PV_MASK) != PV_INTEGER) && ((type & PV_MASK) != p.pl)) {
		IRDA_ERROR("%s: invalid parameter length! "
			   "Expected %d bytes, but value had %d bytes!\n",
			   __FUNCTION__, type & PV_MASK, p.pl);

		/* Most parameters are bit/byte fields or little endian,
		 * so it's ok to only extract a subset of it (the subset
		 * that the handler expect). This is necessary, as some
		 * broken implementations seems to add extra undefined bits.
		 * If the parameter is shorter than we expect or is big
		 * endian, we can't play those tricks. Jean II */
		if((p.pl < (type & PV_MASK)) || (type & PV_BIG_ENDIAN)) {
			/* Skip parameter */
			return p.pl+2;
		} else {
			/* Extract subset of it, fallthrough */
			extract_len = type & PV_MASK;
		}
	}


	switch (extract_len) {
	case 1:
		n += irda_param_unpack(buf+2, "b", &p.pv.i);
		break;
	case 2:
		n += irda_param_unpack(buf+2, "s", &p.pv.i);
		if (type & PV_BIG_ENDIAN)
			p.pv.i = be16_to_cpu((__u16) p.pv.i);
		else
			p.pv.i = le16_to_cpu((__u16) p.pv.i);
		break;
	case 4:
		n += irda_param_unpack(buf+2, "i", &p.pv.i);
		if (type & PV_BIG_ENDIAN)
			be32_to_cpus(&p.pv.i);
		else
			le32_to_cpus(&p.pv.i);
		break;
	default:
		IRDA_WARNING("%s: length %d not supported\n",
			     __FUNCTION__, p.pl);

		/* Skip parameter */
		return p.pl+2;
	}

	IRDA_DEBUG(2, "%s(), pi=%#x, pl=%d, pi=%d\n", __FUNCTION__,
		   p.pi, p.pl, p.pv.i);
	/* Call handler for this parameter */
	err = (*func)(self, &p, PV_PUT);
	if (err < 0)
		return err;

	return p.pl+2; /* Extracted pl+2 bytes */
}

/*
 * Function irda_extract_string (self, buf, len, type, func)
 */
static int irda_extract_string(void *self, __u8 *buf, int len, __u8 pi,
			       PV_TYPE type, PI_HANDLER func)
{
	char str[33];
	irda_param_t p;
	int err;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	p.pi = pi;     /* In case handler needs to know */
	p.pl = buf[1]; /* Extract lenght of value */

	IRDA_DEBUG(2, "%s(), pi=%#x, pl=%d\n", __FUNCTION__,
		   p.pi, p.pl);

	/* Check if buffer is long enough for parsing */
	if (len < (2+p.pl)) {
		IRDA_WARNING("%s: buffer to short for parsing! "
			     "Need %d bytes, but len is only %d\n",
			     __FUNCTION__, p.pl, len);
		return -1;
	}

	/* Should be safe to copy string like this since we have already
	 * checked that the buffer is long enough */
	strncpy(str, buf+2, p.pl);

	IRDA_DEBUG(2, "%s(), str=0x%02x 0x%02x\n", __FUNCTION__,
		   (__u8) str[0], (__u8) str[1]);

	/* Null terminate string */
	str[p.pl+1] = '\0';

	p.pv.c = str; /* Handler will need to take a copy */

	/* Call handler for this parameter */
	err = (*func)(self, &p, PV_PUT);
	if (err < 0)
		return err;

	return p.pl+2; /* Extracted pl+2 bytes */
}

/*
 * Function irda_extract_octseq (self, buf, len, type, func)
 */
static int irda_extract_octseq(void *self, __u8 *buf, int len, __u8 pi,
			       PV_TYPE type, PI_HANDLER func)
{
	irda_param_t p;

	p.pi = pi;     /* In case handler needs to know */
	p.pl = buf[1]; /* Extract lenght of value */

	/* Check if buffer is long enough for parsing */
	if (len < (2+p.pl)) {
		IRDA_WARNING("%s: buffer to short for parsing! "
			     "Need %d bytes, but len is only %d\n",
			     __FUNCTION__, p.pl, len);
		return -1;
	}

	IRDA_DEBUG(0, "%s(), not impl\n", __FUNCTION__);

	return p.pl+2; /* Extracted pl+2 bytes */
}

/*
 * Function irda_param_pack (skb, fmt, ...)
 *
 *    Format:
 *        'i' = 32 bits integer
 *        's' = string
 *
 */
int irda_param_pack(__u8 *buf, char *fmt, ...)
{
	irda_pv_t arg;
	va_list args;
	char *p;
	int n = 0;

	va_start(args, fmt);

	for (p = fmt; *p != '\0'; p++) {
		switch (*p) {
		case 'b':  /* 8 bits unsigned byte */
			buf[n++] = (__u8)va_arg(args, int);
			break;
		case 's':  /* 16 bits unsigned short */
			arg.i = (__u16)va_arg(args, int);
			put_unaligned((__u16)arg.i, (__u16 *)(buf+n)); n+=2;
			break;
		case 'i':  /* 32 bits unsigned integer */
			arg.i = va_arg(args, __u32);
			put_unaligned(arg.i, (__u32 *)(buf+n)); n+=4;
			break;
#if 0
		case 'c': /* \0 terminated string */
			arg.c = va_arg(args, char *);
			strcpy(buf+n, arg.c);
			n += strlen(arg.c) + 1;
			break;
#endif
		default:
			va_end(args);
			return -1;
		}
	}
	va_end(args);

	return 0;
}
EXPORT_SYMBOL(irda_param_pack);

/*
 * Function irda_param_unpack (skb, fmt, ...)
 */
static int irda_param_unpack(__u8 *buf, char *fmt, ...)
{
	irda_pv_t arg;
	va_list args;
	char *p;
	int n = 0;

	va_start(args, fmt);

	for (p = fmt; *p != '\0'; p++) {
		switch (*p) {
		case 'b':  /* 8 bits byte */
			arg.ip = va_arg(args, __u32 *);
			*arg.ip = buf[n++];
			break;
		case 's':  /* 16 bits short */
			arg.ip = va_arg(args, __u32 *);
			*arg.ip = get_unaligned((__u16 *)(buf+n)); n+=2;
			break;
		case 'i':  /* 32 bits unsigned integer */
			arg.ip = va_arg(args, __u32 *);
			*arg.ip = get_unaligned((__u32 *)(buf+n)); n+=4;
			break;
#if 0
		case 'c':   /* \0 terminated string */
			arg.c = va_arg(args, char *);
			strcpy(arg.c, buf+n);
			n += strlen(arg.c) + 1;
			break;
#endif
		default:
			va_end(args);
			return -1;
		}

	}
	va_end(args);

	return 0;
}

/*
 * Function irda_param_insert (self, pi, buf, len, info)
 *
 *    Insert the specified parameter (pi) into buffer. Returns number of
 *    bytes inserted
 */
int irda_param_insert(void *self, __u8 pi, __u8 *buf, int len,
		      pi_param_info_t *info)
{
	pi_minor_info_t *pi_minor_info;
	__u8 pi_minor;
	__u8 pi_major;
	int type;
	int ret = -1;
	int n = 0;

	IRDA_ASSERT(buf != NULL, return ret;);
	IRDA_ASSERT(info != 0, return ret;);

	pi_minor = pi & info->pi_mask;
	pi_major = pi >> info->pi_major_offset;

	/* Check if the identifier value (pi) is valid */
	if ((pi_major > info->len-1) ||
	    (pi_minor > info->tables[pi_major].len-1))
	{
		IRDA_DEBUG(0, "%s(), no handler for parameter=0x%02x\n",
			   __FUNCTION__, pi);

		/* Skip this parameter */
		return -1;
	}

	/* Lookup the info on how to parse this parameter */
	pi_minor_info = &info->tables[pi_major].pi_minor_call_table[pi_minor];

	/* Find expected data type for this parameter identifier (pi)*/
	type = pi_minor_info->type;

	/*  Check if handler has been implemented */
	if (!pi_minor_info->func) {
		IRDA_MESSAGE("%s: no handler for pi=%#x\n", __FUNCTION__, pi);
		/* Skip this parameter */
		return -1;
	}

	/* Insert parameter value */
	ret = (*pv_insert_table[type & PV_MASK])(self, buf+n, len, pi, type,
						 pi_minor_info->func);
	return ret;
}
EXPORT_SYMBOL(irda_param_insert);

/*
 * Function irda_param_extract (self, buf, len, info)
 *
 *    Parse all parameters. If len is correct, then everything should be
 *    safe. Returns the number of bytes that was parsed
 *
 */
static int irda_param_extract(void *self, __u8 *buf, int len,
			      pi_param_info_t *info)
{
	pi_minor_info_t *pi_minor_info;
	__u8 pi_minor;
	__u8 pi_major;
	int type;
	int ret = -1;
	int n = 0;

	IRDA_ASSERT(buf != NULL, return ret;);
	IRDA_ASSERT(info != 0, return ret;);

	pi_minor = buf[n] & info->pi_mask;
	pi_major = buf[n] >> info->pi_major_offset;

	/* Check if the identifier value (pi) is valid */
	if ((pi_major > info->len-1) ||
	    (pi_minor > info->tables[pi_major].len-1))
	{
		IRDA_DEBUG(0, "%s(), no handler for parameter=0x%02x\n",
			   __FUNCTION__, buf[0]);

		/* Skip this parameter */
		return 2 + buf[n + 1];  /* Continue */
	}

	/* Lookup the info on how to parse this parameter */
	pi_minor_info = &info->tables[pi_major].pi_minor_call_table[pi_minor];

	/* Find expected data type for this parameter identifier (pi)*/
	type = pi_minor_info->type;

	IRDA_DEBUG(3, "%s(), pi=[%d,%d], type=%d\n", __FUNCTION__,
		   pi_major, pi_minor, type);

	/*  Check if handler has been implemented */
	if (!pi_minor_info->func) {
		IRDA_MESSAGE("%s: no handler for pi=%#x\n",
			     __FUNCTION__, buf[n]);
		/* Skip this parameter */
		return 2 + buf[n + 1]; /* Continue */
	}

	/* Parse parameter value */
	ret = (*pv_extract_table[type & PV_MASK])(self, buf+n, len, buf[n],
						  type, pi_minor_info->func);
	return ret;
}

/*
 * Function irda_param_extract_all (self, buf, len, info)
 *
 *    Parse all parameters. If len is correct, then everything should be
 *    safe. Returns the number of bytes that was parsed
 *
 */
int irda_param_extract_all(void *self, __u8 *buf, int len, 
			   pi_param_info_t *info)
{
	int ret = -1;
	int n = 0;

	IRDA_ASSERT(buf != NULL, return ret;);
	IRDA_ASSERT(info != 0, return ret;);

	/*
	 * Parse all parameters. Each parameter must be at least two bytes
	 * long or else there is no point in trying to parse it
	 */
	while (len > 2) {
		ret = irda_param_extract(self, buf+n, len, info);
		if (ret < 0)
			return ret;

		n += ret;
		len -= ret;
	}
	return n;
}
EXPORT_SYMBOL(irda_param_extract_all);

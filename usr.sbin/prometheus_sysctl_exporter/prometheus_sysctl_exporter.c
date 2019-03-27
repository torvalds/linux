/*-
 * Copyright (c) 2016-2017 Nuxi, https://nuxi.nl/
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

/*
 * Cursor for iterating over all of the system's sysctl OIDs.
 */
struct oid {
	int	id[CTL_MAXNAME];
	size_t	len;
};

/* Initializes the cursor to point to start of the tree. */
static void
oid_get_root(struct oid *o)
{

	o->id[0] = 1;
	o->len = 1;
}

/* Obtains the OID for a sysctl by name. */
static void
oid_get_by_name(struct oid *o, const char *name)
{

	o->len = nitems(o->id);
	if (sysctlnametomib(name, o->id, &o->len) != 0)
		err(1, "sysctl(%s)", name);
}

/* Returns whether an OID is placed below another OID. */
static bool
oid_is_beneath(struct oid *oa, struct oid *ob)
{

	return (oa->len >= ob->len &&
	    memcmp(oa->id, ob->id, ob->len * sizeof(oa->id[0])) == 0);
}

/* Advances the cursor to the next OID. */
static bool
oid_get_next(const struct oid *cur, struct oid *next)
{
	int lookup[CTL_MAXNAME + 2];
	size_t nextsize;

	lookup[0] = 0;
	lookup[1] = 2;
	memcpy(lookup + 2, cur->id, cur->len * sizeof(lookup[0]));
	nextsize = sizeof(next->id);
	if (sysctl(lookup, 2 + cur->len, &next->id, &nextsize, 0, 0) != 0) {
		if (errno == ENOENT)
			return (false);
		err(1, "sysctl(next)");
	}
	next->len = nextsize / sizeof(next->id[0]);
	return (true);
}

/*
 * OID formatting metadata.
 */
struct oidformat {
	unsigned int	kind;
	char		format[BUFSIZ];
};

/* Returns whether the OID represents a temperature value. */
static bool
oidformat_is_temperature(const struct oidformat *of)
{

	return (of->format[0] == 'I' && of->format[1] == 'K');
}

/* Returns whether the OID represents a timeval structure. */
static bool
oidformat_is_timeval(const struct oidformat *of)
{

	return (strcmp(of->format, "S,timeval") == 0);
}

/* Fetches the formatting metadata for an OID. */
static bool
oid_get_format(const struct oid *o, struct oidformat *of)
{
	int lookup[CTL_MAXNAME + 2];
	size_t oflen;

	lookup[0] = 0;
	lookup[1] = 4;
	memcpy(lookup + 2, o->id, o->len * sizeof(lookup[0]));
	oflen = sizeof(*of);
	if (sysctl(lookup, 2 + o->len, of, &oflen, 0, 0) != 0) {
		if (errno == ENOENT)
			return (false);
		err(1, "sysctl(oidfmt)");
	}
	return (true);
}

/*
 * Container for holding the value of an OID.
 */
struct oidvalue {
	enum { SIGNED, UNSIGNED, FLOAT } type;
	union {
		intmax_t	s;
		uintmax_t	u;
		double		f;
	} value;
};

/* Extracts the value of an OID, converting it to a floating-point number. */
static double
oidvalue_get_float(const struct oidvalue *ov)
{

	switch (ov->type) {
	case SIGNED:
		return (ov->value.s);
	case UNSIGNED:
		return (ov->value.u);
	case FLOAT:
		return (ov->value.f);
	default:
		assert(0 && "Unknown value type");
	}
}

/* Sets the value of an OID as a signed integer. */
static void
oidvalue_set_signed(struct oidvalue *ov, intmax_t s)
{

	ov->type = SIGNED;
	ov->value.s = s;
}

/* Sets the value of an OID as an unsigned integer. */
static void
oidvalue_set_unsigned(struct oidvalue *ov, uintmax_t u)
{

	ov->type = UNSIGNED;
	ov->value.u = u;
}

/* Sets the value of an OID as a floating-point number. */
static void
oidvalue_set_float(struct oidvalue *ov, double f)
{

	ov->type = FLOAT;
	ov->value.f = f;
}

/* Prints the value of an OID to a file stream. */
static void
oidvalue_print(const struct oidvalue *ov, FILE *fp)
{

	switch (ov->type) {
	case SIGNED:
		fprintf(fp, "%jd", ov->value.s);
		break;
	case UNSIGNED:
		fprintf(fp, "%ju", ov->value.u);
		break;
	case FLOAT:
		switch (fpclassify(ov->value.f)) {
		case FP_INFINITE:
			if (signbit(ov->value.f))
				fprintf(fp, "-Inf");
			else
				fprintf(fp, "+Inf");
			break;
		case FP_NAN:
			fprintf(fp, "Nan");
			break;
		default:
			fprintf(fp, "%.6f", ov->value.f);
			break;
		}
		break;
	}
}

/* Fetches the value of an OID. */
static bool
oid_get_value(const struct oid *o, const struct oidformat *of,
    struct oidvalue *ov)
{

	switch (of->kind & CTLTYPE) {
#define	GET_VALUE(ctltype, type) \
	case (ctltype): {						\
		type value;						\
		size_t valuesize;					\
									\
		valuesize = sizeof(value);				\
		if (sysctl(o->id, o->len, &value, &valuesize, 0, 0) != 0) \
			return (false);					\
		if ((type)-1 > 0)					\
			oidvalue_set_unsigned(ov, value);		\
		else							\
			oidvalue_set_signed(ov, value);			\
		break;							\
	}
	GET_VALUE(CTLTYPE_INT, int);
	GET_VALUE(CTLTYPE_UINT, unsigned int);
	GET_VALUE(CTLTYPE_LONG, long);
	GET_VALUE(CTLTYPE_ULONG, unsigned long);
	GET_VALUE(CTLTYPE_S8, int8_t);
	GET_VALUE(CTLTYPE_U8, uint8_t);
	GET_VALUE(CTLTYPE_S16, int16_t);
	GET_VALUE(CTLTYPE_U16, uint16_t);
	GET_VALUE(CTLTYPE_S32, int32_t);
	GET_VALUE(CTLTYPE_U32, uint32_t);
	GET_VALUE(CTLTYPE_S64, int64_t);
	GET_VALUE(CTLTYPE_U64, uint64_t);
#undef GET_VALUE
	case CTLTYPE_OPAQUE:
		if (oidformat_is_timeval(of)) {
			struct timeval tv;
			size_t tvsize;

			tvsize = sizeof(tv);
			if (sysctl(o->id, o->len, &tv, &tvsize, 0, 0) != 0)
				return (false);
			oidvalue_set_float(ov,
			    (double)tv.tv_sec + (double)tv.tv_usec / 1000000);
			return (true);
		} else if (strcmp(of->format, "S,loadavg") == 0) {
			struct loadavg la;
			size_t lasize;

			/*
			 * Only return the one minute load average, as
			 * the others can be inferred using avg_over_time().
			 */
			lasize = sizeof(la);
			if (sysctl(o->id, o->len, &la, &lasize, 0, 0) != 0)
				return (false);
			oidvalue_set_float(ov,
			    (double)la.ldavg[0] / (double)la.fscale);
			return (true);
		}
		return (false);
	default:
		return (false);
	}

	/* Convert temperatures from decikelvin to degrees Celcius. */
	if (oidformat_is_temperature(of)) {
		double v;
		int e;

		v = oidvalue_get_float(ov);
		if (v < 0) {
			oidvalue_set_float(ov, NAN);
		} else {
			e = of->format[2] >= '0' && of->format[2] <= '9' ?
			    of->format[2] - '0' : 1;
			oidvalue_set_float(ov, v / pow(10, e) - 273.15);
		}
	}
	return (true);
}

/*
 * The full name of an OID, stored as a series of components.
 */
struct oidname {
	struct oid	oid;
	char		names[BUFSIZ];
	char		labels[BUFSIZ];
};

/*
 * Initializes the OID name object with an empty value.
 */
static void
oidname_init(struct oidname *on)
{

	on->oid.len = 0;
}

/* Fetches the name and labels of an OID, reusing the previous results. */
static void
oid_get_name(const struct oid *o, struct oidname *on)
{
	int lookup[CTL_MAXNAME + 2];
	char *c, *label;
	size_t i, len;

	/* Fetch the name and split it up in separate components. */
	lookup[0] = 0;
	lookup[1] = 1;
	memcpy(lookup + 2, o->id, o->len * sizeof(lookup[0]));
	len = sizeof(on->names);
	if (sysctl(lookup, 2 + o->len, on->names, &len, 0, 0) != 0)
		err(1, "sysctl(name)");
	for (c = strchr(on->names, '.'); c != NULL; c = strchr(c + 1, '.'))
		*c = '\0';

	/* No need to fetch labels for components that we already have. */
	label = on->labels;
	for (i = 0; i < o->len && i < on->oid.len && o->id[i] == on->oid.id[i];
	    ++i)
		label += strlen(label) + 1;

	/* Fetch the remaining labels. */
	lookup[1] = 6;
	for (; i < o->len; ++i) {
		len = on->labels + sizeof(on->labels) - label;
		if (sysctl(lookup, 2 + i + 1, label, &len, 0, 0) == 0) {
			label += len;
		} else if (errno == ENOENT) {
			*label++ = '\0';
		} else {
			err(1, "sysctl(oidlabel)");
		}
	}
	on->oid = *o;
}

/* Prints the name and labels of an OID to a file stream. */
static void
oidname_print(const struct oidname *on, const struct oidformat *of,
    FILE *fp)
{
	const char *name, *label;
	size_t i;
	char separator;

	/* Print the name of the metric. */
	fprintf(fp, "sysctl");
	name = on->names;
	label = on->labels;
	for (i = 0; i < on->oid.len; ++i) {
		if (*label == '\0') {
			fputc('_', fp);
			while (*name != '\0') {
				/* Map unsupported characters to underscores. */
				fputc(isalnum(*name) ? *name : '_', fp);
				++name;
			}
		}
		name += strlen(name) + 1;
		label += strlen(label) + 1;
	}
	if (oidformat_is_temperature(of))
		fprintf(fp, "_celcius");
	else if (oidformat_is_timeval(of))
		fprintf(fp, "_seconds");

	/* Print the labels of the metric. */
	name = on->names;
	label = on->labels;
	separator = '{';
	for (i = 0; i < on->oid.len; ++i) {
		if (*label != '\0') {
			assert(label[strspn(label,
			    "abcdefghijklmnopqrstuvwxyz"
			    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			    "0123456789_")] == '\0');
			fprintf(fp, "%c%s=\"", separator, label);
			while (*name != '\0') {
				/* Escape backslashes and double quotes. */
				if (*name == '\\' || *name == '"')
					fputc('\\', fp);
				fputc(*name++, fp);
			}
			fputc('"', fp);
			separator = ',';
		}
		name += strlen(name) + 1;
		label += strlen(label) + 1;
	}
	if (separator != '{')
		fputc('}', fp);
}

/* Returns whether the OID name has any labels associated to it. */
static bool
oidname_has_labels(const struct oidname *on)
{
	size_t i;

	for (i = 0; i < on->oid.len; ++i)
		if (on->labels[i] != 0)
			return (true);
	return (false);
}

/*
 * The description of an OID.
 */
struct oiddescription {
	char description[BUFSIZ];
};

/*
 * Fetches the description of an OID.
 */
static bool
oid_get_description(const struct oid *o, struct oiddescription *od)
{
	int lookup[CTL_MAXNAME + 2];
	char *newline;
	size_t odlen;

	lookup[0] = 0;
	lookup[1] = 5;
	memcpy(lookup + 2, o->id, o->len * sizeof(lookup[0]));
	odlen = sizeof(od->description);
	if (sysctl(lookup, 2 + o->len, &od->description, &odlen, 0, 0) != 0) {
		if (errno == ENOENT)
			return (false);
		err(1, "sysctl(oiddescr)");
	}

	newline = strchr(od->description, '\n');
	if (newline != NULL)
		*newline = '\0';

	return (*od->description != '\0');
}

/* Prints the description of an OID to a file stream. */
static void
oiddescription_print(const struct oiddescription *od, FILE *fp)
{

	fprintf(fp, "%s", od->description);
}

static void
oid_print(const struct oid *o, struct oidname *on, bool print_description,
    FILE *fp)
{
	struct oidformat of;
	struct oidvalue ov;
	struct oiddescription od;

	if (!oid_get_format(o, &of) || !oid_get_value(o, &of, &ov))
		return;
	oid_get_name(o, on);

	/*
	 * Print the line with the description. Prometheus expects a
	 * single unique description for every metric, which cannot be
	 * guaranteed by sysctl if labels are present. Omit the
	 * description if labels are present.
	 */
	if (print_description && !oidname_has_labels(on) &&
	    oid_get_description(o, &od)) {
		fprintf(fp, "# HELP ");
		oidname_print(on, &of, fp);
		fputc(' ', fp);
		oiddescription_print(&od, fp);
		fputc('\n', fp);
	}

	/* Print the line with the value. */
	oidname_print(on, &of, fp);
	fputc(' ', fp);
	oidvalue_print(&ov, fp);
	fputc('\n', fp);
}

/* Gzip compresses a buffer of memory. */
static bool
buf_gzip(const char *in, size_t inlen, char *out, size_t *outlen)
{
	z_stream stream = {
	    .next_in	= __DECONST(unsigned char *, in),
	    .avail_in	= inlen,
	    .next_out	= (unsigned char *)out,
	    .avail_out	= *outlen,
	};

	if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
	    MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK ||
	    deflate(&stream, Z_FINISH) != Z_STREAM_END) {
		return (false);
	}
	*outlen = stream.total_out;
	return (deflateEnd(&stream) == Z_OK);
}

static void
usage(void)
{

	fprintf(stderr,
	    "usage: prometheus_sysctl_exporter [-dgh] [prefix ...]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct oidname on;
	char *http_buf;
	FILE *fp;
	size_t http_buflen;
	int ch;
	bool gzip_mode, http_mode, print_descriptions;

	/* Parse command line flags. */
	gzip_mode = http_mode = print_descriptions = false;
	while ((ch = getopt(argc, argv, "dgh")) != -1) {
		switch (ch) {
		case 'd':
			print_descriptions = true;
			break;
		case 'g':
			gzip_mode = true;
			break;
		case 'h':
			http_mode = true;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* HTTP output: cache metrics in buffer. */
	if (http_mode) {
		fp = open_memstream(&http_buf, &http_buflen);
		if (fp == NULL)
			err(1, "open_memstream");
	} else {
		fp = stdout;
	}

	oidname_init(&on);
	if (argc == 0) {
		struct oid o;

		/* Print all OIDs. */
		oid_get_root(&o);
		do {
			oid_print(&o, &on, print_descriptions, fp);
		} while (oid_get_next(&o, &o));
	} else {
		int i;

		/* Print only trees provided as arguments. */
		for (i = 0; i < argc; ++i) {
			struct oid o, root;

			oid_get_by_name(&root, argv[i]);
			o = root;
			do {
				oid_print(&o, &on, print_descriptions, fp);
			} while (oid_get_next(&o, &o) &&
			    oid_is_beneath(&o, &root));
		}
	}

	if (http_mode) {
		const char *content_encoding = "";

		if (ferror(fp) || fclose(fp) != 0)
			err(1, "Cannot generate output");

		/* Gzip compress the output. */
		if (gzip_mode) {
			char *buf;
			size_t buflen;

			buflen = http_buflen;
			buf = malloc(buflen);
			if (buf == NULL)
				err(1, "Cannot allocate compression buffer");
			if (buf_gzip(http_buf, http_buflen, buf, &buflen)) {
				content_encoding = "Content-Encoding: gzip\r\n";
				free(http_buf);
				http_buf = buf;
				http_buflen = buflen;
			} else {
				free(buf);
			}
		}

		/* Print HTTP header and metrics. */
		dprintf(STDOUT_FILENO,
		    "HTTP/1.1 200 OK\r\n"
		    "Connection: close\r\n"
		    "%s"
		    "Content-Length: %zu\r\n"
		    "Content-Type: text/plain; version=0.0.4\r\n"
		    "\r\n",
		    content_encoding, http_buflen);
		write(STDOUT_FILENO, http_buf, http_buflen);
		free(http_buf);

		/* Drain output. */
		if (shutdown(STDIN_FILENO, SHUT_WR) == 0) {
			char buf[1024];

			while (read(STDIN_FILENO, buf, sizeof(buf)) > 0) {
			}
		}
	}
	return (0);
}

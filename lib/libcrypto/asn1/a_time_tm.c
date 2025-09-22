/* $OpenBSD: a_time_tm.c,v 1.43 2025/05/10 05:54:38 tb Exp $ */
/*
 * Copyright (c) 2015 Bob Beck <beck@openbsd.org>
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

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <openssl/asn1t.h>

#include "asn1_local.h"
#include "bytestring.h"
#include "err_local.h"

#define RFC5280 0
#define GENTIME_LENGTH 15
#define UTCTIME_LENGTH 13

int
ASN1_time_tm_cmp(struct tm *tm1, struct tm *tm2)
{
	if (tm1->tm_year < tm2->tm_year)
		return -1;
	if (tm1->tm_year > tm2->tm_year)
		return 1;
	if (tm1->tm_mon < tm2->tm_mon)
		return -1;
	if (tm1->tm_mon > tm2->tm_mon)
		return 1;
	if (tm1->tm_mday < tm2->tm_mday)
		return -1;
	if (tm1->tm_mday > tm2->tm_mday)
		return 1;
	if (tm1->tm_hour < tm2->tm_hour)
		return -1;
	if (tm1->tm_hour > tm2->tm_hour)
		return 1;
	if (tm1->tm_min < tm2->tm_min)
		return -1;
	if (tm1->tm_min > tm2->tm_min)
		return 1;
	if (tm1->tm_sec < tm2->tm_sec)
		return -1;
	if (tm1->tm_sec > tm2->tm_sec)
		return 1;
	return 0;
}

int
ASN1_time_tm_clamp_notafter(struct tm *tm)
{
#ifdef SMALL_TIME_T
	struct tm broken_os_epoch_tm;
	time_t broken_os_epoch_time = INT_MAX;

	if (!asn1_time_time_t_to_tm(&broken_os_epoch_time, &broken_os_epoch_tm))
		return 0;

	if (ASN1_time_tm_cmp(tm, &broken_os_epoch_tm) == 1)
		memcpy(tm, &broken_os_epoch_tm, sizeof(*tm));
#endif
	return 1;
}

/* Convert time to GeneralizedTime, X.690, 11.7. */
static int
tm_to_gentime(struct tm *tm, ASN1_TIME *atime)
{
	char *time_str = NULL;

	if (tm->tm_year < -1900 || tm->tm_year > 9999 - 1900) {
		ASN1error(ASN1_R_ILLEGAL_TIME_VALUE);
		return 0;
	}

	if (asprintf(&time_str, "%04u%02u%02u%02u%02u%02uZ", tm->tm_year + 1900,
	    tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min,
	    tm->tm_sec) == -1) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	free(atime->data);
	atime->data = time_str;
	atime->length = GENTIME_LENGTH;
	atime->type = V_ASN1_GENERALIZEDTIME;

	return 1;
}

/* Convert time to UTCTime, X.690, 11.8. */
static int
tm_to_utctime(struct tm *tm, ASN1_TIME *atime)
{
	char *time_str = NULL;

	if (tm->tm_year >= 150 || tm->tm_year < 50) {
		ASN1error(ASN1_R_ILLEGAL_TIME_VALUE);
		return 0;
	}

	if (asprintf(&time_str, "%02u%02u%02u%02u%02u%02uZ",
	    tm->tm_year % 100,  tm->tm_mon + 1, tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec) == -1) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	free(atime->data);
	atime->data = time_str;
	atime->length = UTCTIME_LENGTH;
	atime->type = V_ASN1_UTCTIME;

	return 1;
}

static int
tm_to_rfc5280_time(struct tm *tm, ASN1_TIME *atime)
{
	if (tm->tm_year >= 50 && tm->tm_year < 150)
		return tm_to_utctime(tm, atime);

	return tm_to_gentime(tm, atime);
}


static int
cbs_get_two_digit_value(CBS *cbs, int *out)
{
	uint8_t first_digit, second_digit;

	if (!CBS_get_u8(cbs, &first_digit))
		return 0;
	if (!isdigit(first_digit))
		return 0;
	if (!CBS_get_u8(cbs, &second_digit))
		return 0;
	if (!isdigit(second_digit))
		return 0;

	*out = (first_digit - '0') * 10 + (second_digit - '0');

	return 1;
}

static int
is_valid_day(int year, int month, int day)
{
	if (day < 1)
		return 0;
	switch (month) {
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
		return day <= 31;
	case 4:
	case 6:
	case 9:
	case 11:
		return day <= 30;
	case 2:
		if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
			return day <= 29;
		 else
			return day <= 28;
	default:
		return 0;
	}
}

/*
 * asn1_time_parse_cbs returns one if |cbs| is a valid DER-encoded, ASN.1 Time
 * body within the limitations imposed by RFC 5280, or zero otherwise. The time
 * is expected to parse as a Generalized Time if is_gentime is true, and as a
 * UTC Time otherwise. If |out_tm| is non-NULL, |*out_tm| will be zeroed, and
 * then set to the corresponding time in UTC. This function does not compute
 * |out_tm->tm_wday| or |out_tm->tm_yday|. |cbs| is not consumed.
 */
int
asn1_time_parse_cbs(const CBS *cbs, int is_gentime, struct tm *out_tm)
{
	int year, month, day, hour, min, sec, val;
	CBS copy;
	uint8_t tz;

	CBS_dup(cbs, &copy);

	if (is_gentime) {
		if (!cbs_get_two_digit_value(&copy, &val))
			return 0;
		year = val * 100;
		if (!cbs_get_two_digit_value(&copy, &val))
			return 0;
		year += val;
	} else {
		year = 1900;
		if (!cbs_get_two_digit_value(&copy, &val))
			return 0;
		year += val;
		if (year < 1950)
			year += 100;
		if (year >= 2050)
			return 0;  /* A Generalized time must be used. */
	}

	if (!cbs_get_two_digit_value(&copy, &month))
		return 0;
	if (month < 1 || month > 12)
		return 0; /* Reject invalid months. */

	if (!cbs_get_two_digit_value(&copy, &day))
		return 0;
	if (!is_valid_day(year, month, day))
		return 0; /* Reject invalid days. */

	if (!cbs_get_two_digit_value(&copy, &hour))
		return 0;
	if (hour > 23)
		return 0; /* Reject invalid hours. */

	if (!cbs_get_two_digit_value(&copy, &min))
		return 0;
	if (min > 59)
		return 0; /* Reject invalid minutes. */

	if (!cbs_get_two_digit_value(&copy, &sec))
		return 0;
	if (sec > 59)
		return 0; /* Reject invalid seconds. Leap seconds are invalid. */

	if (!CBS_get_u8(&copy, &tz))
		return 0;
	if (tz != 'Z')
		return 0; /* Reject anything but Z on the end. */

	if (CBS_len(&copy) != 0)
		return 0;  /* Reject invalid lengths. */

	if (out_tm != NULL) {
		memset(out_tm, 0, sizeof(*out_tm));
		/* Fill in the tm fields corresponding to what we validated. */
		out_tm->tm_year = year - 1900;
		out_tm->tm_mon = month - 1;
		out_tm->tm_mday = day;
		out_tm->tm_hour = hour;
		out_tm->tm_min = min;
		out_tm->tm_sec = sec;
	}

	return 1;
}

/*
 * Parse an RFC 5280 format ASN.1 time string.
 *
 * mode must be:
 * 0 if we expect to parse a time as specified in RFC 5280 for an X509 object.
 * V_ASN1_UTCTIME if we wish to parse an RFC5280 format UTC time.
 * V_ASN1_GENERALIZEDTIME if we wish to parse an RFC5280 format Generalized time.
 *
 * Returns:
 * -1 if the string was invalid.
 * V_ASN1_UTCTIME if the string validated as a UTC time string.
 * V_ASN1_GENERALIZEDTIME if the string validated as a Generalized time string.
 *
 * Fills in *tm with the corresponding time if tm is non NULL.
 */
int
ASN1_time_parse(const char *bytes, size_t len, struct tm *tm, int mode)
{
	int type = 0;
	CBS cbs;

	if (bytes == NULL)
		return -1;

	CBS_init(&cbs, bytes, len);

	if (CBS_len(&cbs) == UTCTIME_LENGTH)
		type = V_ASN1_UTCTIME;
	if (CBS_len(&cbs) == GENTIME_LENGTH)
		type = V_ASN1_GENERALIZEDTIME;
	if (asn1_time_parse_cbs(&cbs, type == V_ASN1_GENERALIZEDTIME, tm)) {
		if (mode != 0 && mode != type)
			return -1;
		return type;
	}

	return -1;
}

/*
 * ASN1_TIME generic functions.
 */

static int
ASN1_TIME_set_string_internal(ASN1_TIME *s, const char *str, int mode)
{
	struct tm tm;

	if (ASN1_time_parse(str, strlen(str), &tm, mode) == -1)
		return 0;

	/* Only check str's format, as documented. */
	if (s == NULL)
		return 1;

	switch (mode) {
	case V_ASN1_UTCTIME:
		return tm_to_utctime(&tm, s);
	case V_ASN1_GENERALIZEDTIME:
		return tm_to_gentime(&tm, s);
	case RFC5280:
		return tm_to_rfc5280_time(&tm, s);
	default:
		return 0;
	}
}

static ASN1_TIME *
ASN1_TIME_adj_internal(ASN1_TIME *s, time_t t, int offset_day, long offset_sec,
    int mode)
{
	ASN1_TIME *atime = s;
	struct tm tm;

	if (!asn1_time_time_t_to_tm(&t, &tm))
		goto err;

	if (offset_day != 0 || offset_sec != 0) {
		if (!OPENSSL_gmtime_adj(&tm, offset_day, offset_sec))
			goto err;
	}

	if (atime == NULL)
		atime = ASN1_TIME_new();
	if (atime == NULL)
		goto err;

	switch (mode) {
	case V_ASN1_UTCTIME:
		if (!tm_to_utctime(&tm, atime))
			goto err;
		break;
	case V_ASN1_GENERALIZEDTIME:
		if (!tm_to_gentime(&tm, atime))
			goto err;
		break;
	case RFC5280:
		if (!tm_to_rfc5280_time(&tm, atime))
			goto err;
		break;
	default:
		goto err;
	}

	return atime;

 err:
	if (atime != s)
		ASN1_TIME_free(atime);

	return NULL;
}

ASN1_TIME *
ASN1_TIME_set(ASN1_TIME *s, time_t t)
{
	return ASN1_TIME_adj(s, t, 0, 0);
}
LCRYPTO_ALIAS(ASN1_TIME_set);

ASN1_TIME *
ASN1_TIME_adj(ASN1_TIME *s, time_t t, int offset_day, long offset_sec)
{
	return ASN1_TIME_adj_internal(s, t, offset_day, offset_sec, RFC5280);
}
LCRYPTO_ALIAS(ASN1_TIME_adj);

int
ASN1_TIME_check(const ASN1_TIME *t)
{
	if (t->type != V_ASN1_GENERALIZEDTIME && t->type != V_ASN1_UTCTIME)
		return 0;
	return t->type == ASN1_time_parse(t->data, t->length, NULL, t->type);
}
LCRYPTO_ALIAS(ASN1_TIME_check);

ASN1_GENERALIZEDTIME *
ASN1_TIME_to_generalizedtime(const ASN1_TIME *t, ASN1_GENERALIZEDTIME **out)
{
	ASN1_GENERALIZEDTIME *agt = NULL;
	struct tm tm;

	if (t->type != V_ASN1_GENERALIZEDTIME && t->type != V_ASN1_UTCTIME)
		goto err;

	if (t->type != ASN1_time_parse(t->data, t->length, &tm, t->type))
		goto err;

	if (out == NULL || (agt = *out) == NULL)
		agt = ASN1_TIME_new();
	if (agt == NULL)
		goto err;

	if (!tm_to_gentime(&tm, agt))
		goto err;

	if (out != NULL)
		*out = agt;

	return agt;

 err:
	if (out == NULL || *out != agt)
		ASN1_TIME_free(agt);

	return NULL;
}
LCRYPTO_ALIAS(ASN1_TIME_to_generalizedtime);

int
ASN1_TIME_set_string(ASN1_TIME *s, const char *str)
{
	return ASN1_TIME_set_string_internal(s, str, RFC5280);
}
LCRYPTO_ALIAS(ASN1_TIME_set_string);

static int
ASN1_TIME_cmp_time_t_internal(const ASN1_TIME *s, time_t t2, int mode)
{
	struct tm tm1, tm2;

	/*
	 * This function has never handled failure conditions properly
	 * The OpenSSL version used to simply follow NULL pointers on failure.
	 * BoringSSL and OpenSSL now make it return -2 on failure.
	 *
	 * The danger is that users of this function will not differentiate the
	 * -2 failure case from s < t2. Callers must be careful. Sadly this is
	 * one of those pervasive things from OpenSSL we must continue with.
	 */

	if (ASN1_time_parse(s->data, s->length, &tm1, mode) == -1)
		return -2;

	if (!asn1_time_time_t_to_tm(&t2, &tm2))
		return -2;

	return ASN1_time_tm_cmp(&tm1, &tm2);
}

int
ASN1_TIME_compare(const ASN1_TIME *t1, const ASN1_TIME *t2)
{
	struct tm tm1, tm2;

	if (t1->type != V_ASN1_UTCTIME && t1->type != V_ASN1_GENERALIZEDTIME)
		return -2;

	if (t2->type != V_ASN1_UTCTIME && t2->type != V_ASN1_GENERALIZEDTIME)
		return -2;

	if (ASN1_time_parse(t1->data, t1->length, &tm1, t1->type) == -1)
		return -2;

	if (ASN1_time_parse(t2->data, t2->length, &tm2, t2->type) == -1)
		return -2;

	return ASN1_time_tm_cmp(&tm1, &tm2);
}
LCRYPTO_ALIAS(ASN1_TIME_compare);

int
ASN1_TIME_cmp_time_t(const ASN1_TIME *s, time_t t)
{
	if (s->type == V_ASN1_UTCTIME)
		return ASN1_TIME_cmp_time_t_internal(s, t, V_ASN1_UTCTIME);
	if (s->type == V_ASN1_GENERALIZEDTIME)
		return ASN1_TIME_cmp_time_t_internal(s, t,
		    V_ASN1_GENERALIZEDTIME);
	return -2;
}
LCRYPTO_ALIAS(ASN1_TIME_cmp_time_t);

/*
 * ASN1_UTCTIME wrappers
 */

int
ASN1_UTCTIME_check(const ASN1_UTCTIME *d)
{
	if (d->type != V_ASN1_UTCTIME)
		return 0;
	return d->type == ASN1_time_parse(d->data, d->length, NULL, d->type);
}
LCRYPTO_ALIAS(ASN1_UTCTIME_check);

int
ASN1_UTCTIME_set_string(ASN1_UTCTIME *s, const char *str)
{
	if (s != NULL && s->type != V_ASN1_UTCTIME)
		return 0;
	return ASN1_TIME_set_string_internal(s, str, V_ASN1_UTCTIME);
}
LCRYPTO_ALIAS(ASN1_UTCTIME_set_string);

ASN1_UTCTIME *
ASN1_UTCTIME_set(ASN1_UTCTIME *s, time_t t)
{
	return ASN1_UTCTIME_adj(s, t, 0, 0);
}
LCRYPTO_ALIAS(ASN1_UTCTIME_set);

ASN1_UTCTIME *
ASN1_UTCTIME_adj(ASN1_UTCTIME *s, time_t t, int offset_day, long offset_sec)
{
	return ASN1_TIME_adj_internal(s, t, offset_day, offset_sec,
	    V_ASN1_UTCTIME);
}
LCRYPTO_ALIAS(ASN1_UTCTIME_adj);

int
ASN1_UTCTIME_cmp_time_t(const ASN1_UTCTIME *s, time_t t)
{
	if (s->type == V_ASN1_UTCTIME)
		return ASN1_TIME_cmp_time_t_internal(s, t, V_ASN1_UTCTIME);
	return -2;
}
LCRYPTO_ALIAS(ASN1_UTCTIME_cmp_time_t);

/*
 * ASN1_GENERALIZEDTIME wrappers
 */

int
ASN1_GENERALIZEDTIME_check(const ASN1_GENERALIZEDTIME *d)
{
	if (d->type != V_ASN1_GENERALIZEDTIME)
		return 0;
	return d->type == ASN1_time_parse(d->data, d->length, NULL, d->type);
}
LCRYPTO_ALIAS(ASN1_GENERALIZEDTIME_check);

int
ASN1_GENERALIZEDTIME_set_string(ASN1_GENERALIZEDTIME *s, const char *str)
{
	if (s != NULL && s->type != V_ASN1_GENERALIZEDTIME)
		return 0;
	return ASN1_TIME_set_string_internal(s, str, V_ASN1_GENERALIZEDTIME);
}
LCRYPTO_ALIAS(ASN1_GENERALIZEDTIME_set_string);

ASN1_GENERALIZEDTIME *
ASN1_GENERALIZEDTIME_set(ASN1_GENERALIZEDTIME *s, time_t t)
{
	return ASN1_GENERALIZEDTIME_adj(s, t, 0, 0);
}
LCRYPTO_ALIAS(ASN1_GENERALIZEDTIME_set);

ASN1_GENERALIZEDTIME *
ASN1_GENERALIZEDTIME_adj(ASN1_GENERALIZEDTIME *s, time_t t, int offset_day,
    long offset_sec)
{
	return ASN1_TIME_adj_internal(s, t, offset_day, offset_sec,
	    V_ASN1_GENERALIZEDTIME);
}
LCRYPTO_ALIAS(ASN1_GENERALIZEDTIME_adj);

int
ASN1_TIME_normalize(ASN1_TIME *t)
{
	struct tm tm;

	if (t == NULL)
		return 0;
	if (!ASN1_TIME_to_tm(t, &tm))
		return 0;
	return tm_to_rfc5280_time(&tm, t);
}
LCRYPTO_ALIAS(ASN1_TIME_normalize);

int
ASN1_TIME_set_string_X509(ASN1_TIME *s, const char *str)
{
	return ASN1_TIME_set_string_internal(s, str, RFC5280);
}
LCRYPTO_ALIAS(ASN1_TIME_set_string_X509);

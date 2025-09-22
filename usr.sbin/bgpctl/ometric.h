/*	$OpenBSD: ometric.h,v 1.6 2023/01/06 13:26:57 tb Exp $ */

/*
 * Copyright (c) 2022 Claudio Jeker <claudio@openbsd.org>
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

enum ometric_type {
	OMT_UNKNOWN,
	OMT_GAUGE,
	OMT_COUNTER,
	OMT_STATESET,
	OMT_HISTOGRAM,
	OMT_SUMMARY,
	OMT_INFO,
};

struct ometric;
struct olabels;

struct ometric	*ometric_new(enum ometric_type, const char *, const char *);
struct ometric	*ometric_new_state(const char * const *, size_t, const char *,
		    const char *);
void		 ometric_free_all(void);
struct olabels	*olabels_new(const char * const *, const char **);
void		 olabels_free(struct olabels *);

int		 ometric_output_all(FILE *);

/* functions to set gauge and counter metrics */
void	ometric_set_int(struct ometric *, uint64_t, struct olabels *);
void	ometric_set_float(struct ometric *, double, struct olabels *);
void	ometric_set_timespec(struct ometric *, const struct timespec *,
	    struct olabels *);
void	ometric_set_info(struct ometric *, const char **, const char **,
	    struct olabels *);
void	ometric_set_state(struct ometric *, const char *, struct olabels *);
void	ometric_set_int_with_labels(struct ometric *, uint64_t, const char **,
	    const char **, struct olabels *);
void	ometric_set_timespec_with_labels(struct ometric *, struct timespec *,
	    const char **, const char **, struct olabels *);
#define OKV(...)		(const char *[]){ __VA_ARGS__, NULL }

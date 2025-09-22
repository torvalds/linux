/*	$OpenBSD: asr.h,v 1.2 2019/10/24 05:57:41 otto Exp $	*/
/*
 * Copyright (c) 2012-2014 Eric Faurot <eric@openbsd.org>
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

/*
 * Expected fd conditions
 */
#define ASR_WANT_READ	1
#define ASR_WANT_WRITE	2

/*
 * Structure through which asynchronous query results are returned when
 * calling asr_run().
 */
struct asr_result {
	/* Fields set if the query is not done yet (asr_run returns 0) */
	int	 ar_cond;	/* ASR_WANT_READ or ASR_WANT_WRITE */
	int	 ar_fd;		/* the fd waiting for io condition */
	int	 ar_timeout;	/* time to wait for in milliseconds */

	/* Error fields.  Depends on the query type. */
	int	 ar_errno;
	int	 ar_h_errno;
	int	 ar_gai_errno;
	int	 ar_rrset_errno;

	/* Result for res_*_async() calls */
	int	 ar_count;	/* number of answers in the dns reply */
	int	 ar_rcode;	/* response code in the dns reply */
	void	*ar_data;	/* raw reply packet (must be freed) */
	int	 ar_datalen;	/* reply packet length */
	struct sockaddr_storage ar_ns; /* nameserver that responded */

	/* Result for other calls. Must be freed properly. */
	struct addrinfo	 *ar_addrinfo;
	struct rrsetinfo *ar_rrsetinfo;
	struct hostent	 *ar_hostent;
	struct netent	 *ar_netent;
};

/*
 * Asynchronous query management.
 */

/* Forward declaration. The API uses opaque pointers as query handles. */
struct asr_query;

void *asr_resolver_from_string(const char *);
void asr_resolver_free(void *);
int asr_run(struct asr_query *, struct asr_result *);
int asr_run_sync(struct asr_query *, struct asr_result *);
void asr_abort(struct asr_query *);

/*
 * Asynchronous version of the resolver functions. Similar prototypes, with
 * an extra context parameter at the end which must currently be set to NULL.
 * All functions return a handle suitable for use with the management functions
 * above.
 */
struct asr_query *res_send_async(const unsigned char *, int, void *);
struct asr_query *res_query_async(const char *, int, int, void *);
struct asr_query *res_search_async(const char *, int, int, void *);

struct asr_query *getrrsetbyname_async(const char *, unsigned int, unsigned int,
    unsigned int, void *);

struct asr_query *gethostbyname_async(const char *, void *);
struct asr_query *gethostbyname2_async(const char *, int, void *);
struct asr_query *gethostbyaddr_async(const void *, socklen_t, int, void *);

struct asr_query *getnetbyname_async(const char *, void *);
struct asr_query *getnetbyaddr_async(in_addr_t, int, void *);

struct asr_query *getaddrinfo_async(const char *, const char *,
    const struct addrinfo *, void *);
struct asr_query *getnameinfo_async(const struct sockaddr *, socklen_t, char *,
    size_t, char *, size_t, int, void *);

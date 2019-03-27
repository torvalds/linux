/*
 * Copyright (c) 2016 Adrian Chadd <adrian@FreeBSD.org>
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/cpuset.h>
#include <sys/sysctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <netinet/in.h>

#include "librss.h"

int
rss_sock_set_bindmulti(int fd, int af, int val)
{
	int opt;
	socklen_t optlen;
	int retval;

	/* Set bindmulti */
	opt = val;
	optlen = sizeof(opt);
	retval = setsockopt(fd,
	    af == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
	    af == AF_INET ? IP_BINDMULTI : IPV6_BINDMULTI,
	    &opt,
	    optlen);
	if (retval < 0) {
		warn("%s: setsockopt(IP_BINDMULTI)", __func__);
		return (-1);
	}
	return (0);
}

int
rss_sock_set_rss_bucket(int fd, int af, int rss_bucket)
{
	int opt;
	socklen_t optlen;
	int retval;
	int f, p;

	switch (af) {
	case AF_INET:
		p = IPPROTO_IP;
		f = IP_RSS_LISTEN_BUCKET;
		break;
	case AF_INET6:
		p = IPPROTO_IPV6;
		f = IPV6_RSS_LISTEN_BUCKET;
		break;
	default:
		return (-1);
	}

	/* Set RSS bucket */
	opt = rss_bucket;
	optlen = sizeof(opt);
	retval = setsockopt(fd, p, f, &opt, optlen);
	if (retval < 0) {
		warn("%s: setsockopt(IP_RSS_LISTEN_BUCKET)", __func__);
		return (-1);
	}
	return (0);
}

int
rss_sock_set_recvrss(int fd, int af, int val)
{
	int opt, retval;
	socklen_t optlen;
	int f1, f2, p;

	switch (af) {
	case AF_INET:
		p = IPPROTO_IP;
		f1 = IP_RECVFLOWID;
		f2 = IP_RECVRSSBUCKETID;
		break;
	case AF_INET6:
		p = IPPROTO_IPV6;
		f1 = IPV6_RECVFLOWID;
		f2 = IPV6_RECVRSSBUCKETID;
		break;
	default:
		return (-1);
	}

	/* Enable/disable flowid */
	opt = val;
	optlen = sizeof(opt);
	retval = setsockopt(fd, p, f1, &opt, optlen);
	if (retval < 0) {
		warn("%s: setsockopt(IP_RECVFLOWID)", __func__);
		return (-1);
	}

	/* Enable/disable RSS bucket reception */
	opt = val;
	optlen = sizeof(opt);
	retval = setsockopt(fd, p, f2, &opt, optlen);
	if (retval < 0) {
		warn("%s: setsockopt(IP_RECVRSSBUCKETID)", __func__);
		return (-1);
	}

	return (0);
}

static int
rss_getsysctlint(const char *s)
{
	int val, retval;
	size_t rlen;

	rlen = sizeof(int);
	retval = sysctlbyname(s, &val, &rlen, NULL, 0);
	if (retval < 0) {
		warn("sysctlbyname (%s)", s);
		return (-1);
	}

	return (val);
}

static int
rss_getbucketmap(int *bucket_map, int nbuckets)
{
	/* XXX I'm lazy; so static string it is */
	char bstr[2048];
	int retval;
	size_t rlen;
	char *s, *ss;
	int r, b, c;

	/* Paranoia */
	memset(bstr, '\0', sizeof(bstr));

	rlen = sizeof(bstr) - 1;
	retval = sysctlbyname("net.inet.rss.bucket_mapping", bstr, &rlen, NULL, 0);
	if (retval < 0) {
		warn("sysctlbyname (net.inet.rss.bucket_mapping)");
		return (-1);
	}

	ss = bstr;
	while ((s = strsep(&ss, " ")) != NULL) {
		r = sscanf(s, "%d:%d", &b, &c);
		if (r != 2) {
			fprintf(stderr, "%s: string (%s) not parsable\n",
			    __func__,
			    s);
			return (-1);
		}
		if (b > nbuckets) {
			fprintf(stderr, "%s: bucket %d > nbuckets %d\n",
			    __func__,
			    b,
			    nbuckets);
			return (-1);
		}
		/* XXX no maxcpu check */
		bucket_map[b] = c;
	}
	return (0);
}

struct rss_config *
rss_config_get(void)
{
	struct rss_config *rc = NULL;

	rc = calloc(1, sizeof(*rc));
	if (rc == NULL) {
		warn("%s: calloc", __func__);
		goto error;
	}

	rc->rss_ncpus = rss_getsysctlint("net.inet.rss.ncpus");
	if (rc->rss_ncpus < 0) {
		fprintf(stderr, "%s: couldn't fetch net.inet.rss.ncpus\n", __func__);
		goto error;
	}

	rc->rss_nbuckets = rss_getsysctlint("net.inet.rss.buckets");
	if (rc->rss_nbuckets < 0) {
		fprintf(stderr, "%s: couldn't fetch net.inet.rss.nbuckets\n", __func__);
		goto error;
	}

	rc->rss_basecpu = rss_getsysctlint("net.inet.rss.basecpu");
	if (rc->rss_basecpu< 0) {
		fprintf(stderr, "%s: couldn't fetch net.inet.rss.basecpu\n", __func__);
		goto error;
	}

	rc->rss_bucket_map = calloc(rc->rss_nbuckets, sizeof(int));
	if (rc->rss_bucket_map == NULL) {
		warn("%s: calloc (rss buckets; %d entries)", __func__, rc->rss_nbuckets);
		goto error;
	}

	if (rss_getbucketmap(rc->rss_bucket_map, rc->rss_nbuckets) != 0) {
		fprintf(stderr, "%s: rss_getbucketmap failed\n", __func__);
		goto error;
	}

	return (rc);

error:
	if (rc != NULL) {
		free(rc->rss_bucket_map);
		free(rc);
	}
	return (NULL);
}

void
rss_config_free(struct rss_config *rc)
{

	if ((rc != NULL) && rc->rss_bucket_map)
		free(rc->rss_bucket_map);
	if (rc != NULL)
		free(rc);
}

int
rss_config_get_bucket_count(struct rss_config *rc)
{

	if (rc == NULL)
		return (-1);
	return (rc->rss_nbuckets);
}

int
rss_get_bucket_cpuset(struct rss_config *rc, rss_bucket_type_t btype,
    int bucket, cpuset_t *cs)
{

	if (bucket < 0 || bucket >= rc->rss_nbuckets) {
		errno = EINVAL;
		return (-1);
	}

	/*
	 * For now all buckets are the same, but eventually we'll want
	 * to allow administrators to set separate RSS cpusets for
	 * {kernel,user} {tx, rx} combinations.
	 */
	if (btype <= RSS_BUCKET_TYPE_NONE || btype > RSS_BUCKET_TYPE_MAX) {
		errno = ENOTSUP;
		return (-1);
	}

	CPU_ZERO(cs);
	CPU_SET(rc->rss_bucket_map[bucket], cs);

	return (0);
}

int
rss_set_bucket_rebalance_cb(rss_bucket_rebalance_cb_t *cb, void *cbdata)
{

	(void) cb;
	(void) cbdata;

	/*
	 * For now there's no rebalance callback, so
	 * just return 0 and ignore it.
	 */
	return (0);
}

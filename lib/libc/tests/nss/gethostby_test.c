/*-
 * Copyright (c) 2006 Michael Bushkov <bushman@freebsd.org>
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stringlist.h>
#include <unistd.h>

#include <atf-c.h>

#include "freebsd_test_suite/macros.h"
#include "testutil.h"

enum test_methods {
	TEST_GETHOSTBYNAME2,
	TEST_GETHOSTBYADDR,
	TEST_GETHOSTBYNAME2_GETADDRINFO,
	TEST_GETHOSTBYADDR_GETNAMEINFO,
	TEST_BUILD_SNAPSHOT,
	TEST_BUILD_ADDR_SNAPSHOT
};

static int ipnode_flags = 0;
static int af_type = AF_INET;
static bool use_ipnode_functions;

DECLARE_TEST_DATA(hostent)
DECLARE_TEST_FILE_SNAPSHOT(hostent)
DECLARE_1PASS_TEST(hostent)
DECLARE_2PASS_TEST(hostent)

/* These stubs will use gethostby***() or getipnodeby***() functions,
 * depending on the use_ipnode_functions global variable value */
static struct hostent *__gethostbyname2(const char *, int);
static struct hostent *__gethostbyaddr(const void *, socklen_t, int);
static void __freehostent(struct hostent *);

static void clone_hostent(struct hostent *, struct hostent const *);
static int compare_hostent(struct hostent *, struct hostent *, void *);
static void dump_hostent(struct hostent *);
static void free_hostent(struct hostent *);

static int is_hostent_equal(struct hostent *, struct addrinfo *);

static void sdump_hostent(struct hostent *, char *, size_t);
static int hostent_read_hostlist_func(struct hostent *, char *);
static int hostent_read_snapshot_addr(char *, unsigned char *, size_t);
static int hostent_read_snapshot_func(struct hostent *, char *);

static int hostent_test_correctness(struct hostent *, void *);
static int hostent_test_gethostbyaddr(struct hostent *, void *);
static int hostent_test_getaddrinfo_eq(struct hostent *, void *);
static int hostent_test_getnameinfo_eq(struct hostent *, void *);

IMPLEMENT_TEST_DATA(hostent)
IMPLEMENT_TEST_FILE_SNAPSHOT(hostent)
IMPLEMENT_1PASS_TEST(hostent)
IMPLEMENT_2PASS_TEST(hostent)

static struct hostent *
__gethostbyname2(const char *name, int af)
{
	struct hostent *he;
	int error;

	if (use_ipnode_functions) {
		error = 0;
		he = getipnodebyname(name, af, ipnode_flags, &error);
		if (he == NULL)
			errno = error;
	} else
		he = gethostbyname2(name, af);

	return (he);
}

static struct hostent *
__gethostbyaddr(const void *addr, socklen_t len, int af)
{
	struct hostent *he;
	int error;

	if (use_ipnode_functions) {
		error = 0;
		he = getipnodebyaddr(addr, len, af, &error);
		if (he == NULL)
			errno = error;
	} else
		he = gethostbyaddr(addr, len, af);

	return (he);
}

static void
__freehostent(struct hostent *he)
{

	/* NOTE: checking for he != NULL - just in case */
	if (use_ipnode_functions && he != NULL)
		freehostent(he);
}

static void
clone_hostent(struct hostent *dest, struct hostent const *src)
{
	ATF_REQUIRE(dest != NULL);
	ATF_REQUIRE(src != NULL);

	char **cp;
	int aliases_num;
	int addrs_num;
	size_t offset;

	memset(dest, 0, sizeof(struct hostent));

	if (src->h_name != NULL) {
		dest->h_name = strdup(src->h_name);
		ATF_REQUIRE(dest->h_name != NULL);
	}

	dest->h_addrtype = src->h_addrtype;
	dest->h_length = src->h_length;

	if (src->h_aliases != NULL) {
		aliases_num = 0;
		for (cp = src->h_aliases; *cp; ++cp)
			++aliases_num;

		dest->h_aliases = calloc(aliases_num + 1, sizeof(char *));
		ATF_REQUIRE(dest->h_aliases != NULL);

		for (cp = src->h_aliases; *cp; ++cp) {
			dest->h_aliases[cp - src->h_aliases] = strdup(*cp);
			ATF_REQUIRE(dest->h_aliases[cp - src->h_aliases] != NULL);
		}
	}

	if (src->h_addr_list != NULL) {
		addrs_num = 0;
		for (cp = src->h_addr_list; *cp; ++cp)
			++addrs_num;

		dest->h_addr_list = calloc(addrs_num + 1, sizeof(char *));
		ATF_REQUIRE(dest->h_addr_list != NULL);

		for (cp = src->h_addr_list; *cp; ++cp) {
			offset = cp - src->h_addr_list;
			dest->h_addr_list[offset] = malloc(src->h_length);
			ATF_REQUIRE(dest->h_addr_list[offset] != NULL);
			memcpy(dest->h_addr_list[offset],
			    src->h_addr_list[offset], src->h_length);
		}
	}
}

static void
free_hostent(struct hostent *ht)
{
	char **cp;

	ATF_REQUIRE(ht != NULL);

	free(ht->h_name);

	if (ht->h_aliases != NULL) {
		for (cp = ht->h_aliases; *cp; ++cp)
			free(*cp);
		free(ht->h_aliases);
	}

	if  (ht->h_addr_list != NULL) {
		for (cp = ht->h_addr_list; *cp; ++cp)
			free(*cp);
		free(ht->h_addr_list);
	}
}

static  int
compare_hostent(struct hostent *ht1, struct hostent *ht2, void *mdata)
{
	char **c1, **c2, **ct, **cb;
	int b;

	if (ht1 == ht2)
		return 0;

	if (ht1 == NULL || ht2 == NULL)
		goto errfin;

	if (ht1->h_name == NULL || ht2->h_name == NULL)
		goto errfin;

	if (ht1->h_addrtype != ht2->h_addrtype ||
	    ht1->h_length != ht2->h_length ||
	    strcmp(ht1->h_name, ht2->h_name) != 0)
		goto errfin;

	c1 = ht1->h_aliases;
	c2 = ht2->h_aliases;

	if ((ht1->h_aliases == NULL || ht2->h_aliases == NULL) &&
	    ht1->h_aliases != ht2->h_aliases)
		goto errfin;

	if (c1 != NULL && c2 != NULL) {
		cb = c1;
		for (;*c1; ++c1) {
			b = 0;
			for (ct = c2; *ct; ++ct) {
				if (strcmp(*c1, *ct) == 0) {
					b = 1;
					break;
				}
			}
			if (b == 0) {
				printf("h1 aliases item can't be found in h2 "
				    "aliases\n");
				goto errfin;
			}
		}

		c1 = cb;
		for (;*c2; ++c2) {
			b = 0;
			for (ct = c1; *ct; ++ct) {
				if (strcmp(*c2, *ct) == 0) {
					b = 1;
					break;
				}
			}
			if (b == 0) {
				printf("h2 aliases item can't be found in h1 "
				    "aliases\n");
				goto errfin;
			}
		}
	}

	c1 = ht1->h_addr_list;
	c2 = ht2->h_addr_list;

	if ((ht1->h_addr_list == NULL || ht2->h_addr_list== NULL) &&
	    ht1->h_addr_list != ht2->h_addr_list)
		goto errfin;

	if (c1 != NULL && c2 != NULL) {
		cb = c1;
		for (; *c1; ++c1) {
			b = 0;
			for (ct = c2; *ct; ++ct) {
				if (memcmp(*c1, *ct, ht1->h_length) == 0) {
					b = 1;
					break;
				}
			}
			if (b == 0) {
				printf("h1 addresses item can't be found in "
				    "h2 addresses\n");
				goto errfin;
			}
		}

		c1 = cb;
		for (; *c2; ++c2) {
			b = 0;
			for (ct = c1; *ct; ++ct) {
				if (memcmp(*c2, *ct, ht1->h_length) == 0) {
					b = 1;
					break;
				}
			}
			if (b == 0) {
				printf("h2 addresses item can't be found in "
				    "h1 addresses\n");
				goto errfin;
			}
		}
	}

	return 0;

errfin:
	if (mdata == NULL) {
		printf("following structures are not equal:\n");
		dump_hostent(ht1);
		dump_hostent(ht2);
	}

	return (-1);
}

static int
check_addrinfo_for_name(struct addrinfo *ai, char const *name)
{
	struct addrinfo *ai2;

	for (ai2 = ai; ai2 != NULL; ai2 = ai2->ai_next) {
		if (strcmp(ai2->ai_canonname, name) == 0)
			return (0);
	}

	return (-1);
}

static int
check_addrinfo_for_addr(struct addrinfo *ai, char const *addr,
	socklen_t addrlen, int af)
{
	struct addrinfo *ai2;

	for (ai2 = ai; ai2 != NULL; ai2 = ai2->ai_next) {
		if (af != ai2->ai_family)
			continue;

		switch (af) {
		case AF_INET:
			if (memcmp(addr,
			    (void *)&((struct sockaddr_in *)ai2->ai_addr)->sin_addr,
			    MIN(addrlen, ai2->ai_addrlen)) == 0)
				return (0);
			break;
		case AF_INET6:
			if (memcmp(addr,
			    (void *)&((struct sockaddr_in6 *)ai2->ai_addr)->sin6_addr,
			    MIN(addrlen, ai2->ai_addrlen)) == 0)
				return (0);
			break;
		default:
			break;
		}
	}

	return (-1);
}

static int
is_hostent_equal(struct hostent *he, struct addrinfo *ai)
{
	char **cp;
	int rv;

#ifdef DEBUG
	printf("checking equality of he and ai\n");
#endif

	rv = check_addrinfo_for_name(ai, he->h_name);
	if (rv != 0) {
		printf("not equal - he->h_name couldn't be found\n");
		return (rv);
	}

	for (cp = he->h_addr_list; *cp; ++cp) {
		rv = check_addrinfo_for_addr(ai, *cp, he->h_length,
			he->h_addrtype);
		if (rv != 0) {
			printf("not equal - one of he->h_addr_list couldn't be found\n");
			return (rv);
		}
	}

#ifdef DEBUG
	printf("equal\n");
#endif

	return (0);
}

static void
sdump_hostent(struct hostent *ht, char *buffer, size_t buflen)
{
	char **cp;
	size_t i;
	int written;

	written = snprintf(buffer, buflen, "%s %d %d",
		ht->h_name, ht->h_addrtype, ht->h_length);
	buffer += written;
	if (written > (int)buflen)
		return;
	buflen -= written;

	if (ht->h_aliases != NULL) {
		if (*(ht->h_aliases) != NULL) {
			for (cp = ht->h_aliases; *cp; ++cp) {
				written = snprintf(buffer, buflen, " %s",*cp);
				buffer += written;
				if (written > (int)buflen)
					return;
				buflen -= written;

				if (buflen == 0)
					return;
			}
		} else {
			written = snprintf(buffer, buflen, " noaliases");
			buffer += written;
			if (written > (int)buflen)
				return;
			buflen -= written;
		}
	} else {
		written = snprintf(buffer, buflen, " (null)");
		buffer += written;
		if (written > (int)buflen)
			return;
		buflen -= written;
	}

	written = snprintf(buffer, buflen, " : ");
	buffer += written;
	if (written > (int)buflen)
		return;
	buflen -= written;

	if (ht->h_addr_list != NULL) {
		if (*(ht->h_addr_list) != NULL) {
			for (cp = ht->h_addr_list; *cp; ++cp) {
				for (i = 0; i < (size_t)ht->h_length; ++i) {
					written = snprintf(buffer, buflen,
					    i + 1 != (size_t)ht->h_length ?
					        "%d." : "%d",
					    (unsigned char)(*cp)[i]);
					buffer += written;
					if (written > (int)buflen)
						return;
					buflen -= written;

					if (buflen == 0)
						return;
				}

				if (*(cp + 1)) {
					written = snprintf(buffer, buflen,
					    " ");
					buffer += written;
					if (written > (int)buflen)
						return;
					buflen -= written;
				}
			}
		} else {
			written = snprintf(buffer, buflen, " noaddrs");
			buffer += written;
			if (written > (int)buflen)
				return;
			buflen -= written;
		}
	} else {
		written = snprintf(buffer, buflen, " (null)");
		buffer += written;
		if (written > (int)buflen)
			return;
		buflen -= written;
	}
}

static int
hostent_read_hostlist_func(struct hostent *he, char *line)
{
	struct hostent *result;
	int rv;

#ifdef DEBUG
	printf("resolving %s: ", line);
#endif
	result = __gethostbyname2(line, af_type);
	if (result != NULL) {
#ifdef DEBUG
		printf("found\n");
#endif

		rv = hostent_test_correctness(result, NULL);
		if (rv != 0) {
			__freehostent(result);
			return (rv);
		}

		clone_hostent(he, result);
		__freehostent(result);
	} else {
#ifdef DEBUG
		printf("not found\n");
#endif
 		memset(he, 0, sizeof(struct hostent));
		he->h_name = strdup(line);
		ATF_REQUIRE(he->h_name != NULL);
	}
	return (0);
}

static int
hostent_read_snapshot_addr(char *addr, unsigned char *result, size_t len)
{
	char *s, *ps, *ts;

	ps = addr;
	while ( (s = strsep(&ps, ".")) != NULL) {
		if (len == 0)
			return (-1);

		*result = (unsigned char)strtol(s, &ts, 10);
		++result;
		if (*ts != '\0')
			return (-1);

		--len;
	}
	if (len != 0)
		return (-1);
	else
		return (0);
}

static int
hostent_read_snapshot_func(struct hostent *ht, char *line)
{
	StringList *sl1, *sl2;
	char *s, *ps, *ts;
	int i, rv;

#ifdef DEBUG
	printf("1 line read from snapshot:\n%s\n", line);
#endif

	rv = 0;
	i = 0;
	sl1 = sl2 = NULL;
	ps = line;
	memset(ht, 0, sizeof(struct hostent));
	while ((s = strsep(&ps, " ")) != NULL) {
		switch (i) {
		case 0:
			ht->h_name = strdup(s);
			ATF_REQUIRE(ht->h_name != NULL);
			break;

		case 1:
			ht->h_addrtype = (int)strtol(s, &ts, 10);
			if (*ts != '\0')
				goto fin;
			break;

		case 2:
			ht->h_length = (int)strtol(s, &ts, 10);
			if (*ts != '\0')
				goto fin;
			break;

		case 3:
			if (sl1 == NULL) {
				if (strcmp(s, "(null)") == 0)
					return (0);

				sl1 = sl_init();
				ATF_REQUIRE(sl1 != NULL);

				if (strcmp(s, "noaliases") != 0) {
					ts = strdup(s);
					ATF_REQUIRE(ts != NULL);
					sl_add(sl1, ts);
				}
			} else {
				if (strcmp(s, ":") == 0)
					++i;
				else {
					ts = strdup(s);
					ATF_REQUIRE(ts != NULL);
					sl_add(sl1, ts);
				}
			}
			break;

		case 4:
			if (sl2 == NULL) {
				if (strcmp(s, "(null)") == 0)
					return (0);

				sl2 = sl_init();
				ATF_REQUIRE(sl2 != NULL);

				if (strcmp(s, "noaddrs") != 0) {
					ts = calloc(1, ht->h_length);
					ATF_REQUIRE(ts != NULL);
					rv = hostent_read_snapshot_addr(s,
					    (unsigned char *)ts,
					    ht->h_length);
					sl_add(sl2, ts);
					if (rv != 0)
						goto fin;
				}
			} else {
				ts = calloc(1, ht->h_length);
				ATF_REQUIRE(ts != NULL);
				rv = hostent_read_snapshot_addr(s,
				    (unsigned char *)ts, ht->h_length);
				sl_add(sl2, ts);
				if (rv != 0)
					goto fin;
			}
			break;
		default:
			break;
		}

		if (i != 3 && i != 4)
			++i;
	}

fin:
	if (sl1 != NULL) {
		sl_add(sl1, NULL);
		ht->h_aliases = sl1->sl_str;
	}
	if (sl2 != NULL) {
		sl_add(sl2, NULL);
		ht->h_addr_list = sl2->sl_str;
	}

	if ((i != 4) || (rv != 0)) {
		free_hostent(ht);
		memset(ht, 0, sizeof(struct hostent));
		return (-1);
	}

	/* NOTE: is it a dirty hack or not? */
	free(sl1);
	free(sl2);
	return (0);
}

static void
dump_hostent(struct hostent *result)
{
	if (result != NULL) {
		char buffer[1024];
		sdump_hostent(result, buffer, sizeof(buffer));
		printf("%s\n", buffer);
	} else
		printf("(null)\n");
}

static int
hostent_test_correctness(struct hostent *ht, void *mdata __unused)
{

#ifdef DEBUG
	printf("testing correctness with the following data:\n");
	dump_hostent(ht);
#endif

	if (ht == NULL)
		goto errfin;

	if (ht->h_name == NULL)
		goto errfin;

	if (!((ht->h_addrtype >= 0) && (ht->h_addrtype < AF_MAX)))
		goto errfin;

	if ((ht->h_length != sizeof(struct in_addr)) &&
		(ht->h_length != sizeof(struct in6_addr)))
		goto errfin;

	if (ht->h_aliases == NULL)
		goto errfin;

	if (ht->h_addr_list == NULL)
		goto errfin;

#ifdef DEBUG
	printf("correct\n");
#endif

	return (0);
errfin:
	printf("incorrect\n");

	return (-1);
}

static int
hostent_test_gethostbyaddr(struct hostent *he, void *mdata)
{
	struct hostent *result;
	struct hostent_test_data *addr_test_data;
	int rv;

	addr_test_data = (struct hostent_test_data *)mdata;

	/* We should omit unresolved hostents */
	if (he->h_addr_list != NULL) {
		char **cp;
		for (cp = he->h_addr_list; *cp; ++cp) {
#ifdef DEBUG
			printf("doing reverse lookup for %s\n", he->h_name);
#endif

			result = __gethostbyaddr(*cp, he->h_length,
			    he->h_addrtype);
			if (result == NULL) {
#ifdef DEBUG
				printf("%s: warning: reverse lookup failed "
				    "for %s: %s\n", __func__, he->h_name,
				    strerror(errno));
#endif
				continue;
			}
			rv = hostent_test_correctness(result, NULL);
			if (rv != 0) {
				__freehostent(result);
				return (rv);
			}

			if (addr_test_data != NULL)
				TEST_DATA_APPEND(hostent, addr_test_data,
				    result);

			__freehostent(result);
		}
	}

	return (0);
}

static int
hostent_test_getaddrinfo_eq(struct hostent *he, void *mdata __unused)
{
	struct addrinfo *ai, hints;
	int rv;

	ai = NULL;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = af_type;
	hints.ai_flags = AI_CANONNAME;

	printf("using getaddrinfo() to resolve %s\n", he->h_name);

	/* struct hostent *he was not resolved */
	if (he->h_addr_list == NULL) {
		/* We can be sure that he->h_name is not NULL */
		rv = getaddrinfo(he->h_name, NULL, &hints, &ai);
		if (rv == 0) {
			printf("not ok - shouldn't have been resolved\n");
			rv = -1;
		} else
			rv = 0;
	} else {
		rv = getaddrinfo(he->h_name, NULL, &hints, &ai);
		if (rv != 0) {
			printf("not ok - should have been resolved\n");
			rv = -1;
			goto done;
		}
		rv = is_hostent_equal(he, ai);
		if (rv != 0) {
			printf("not ok - addrinfo and hostent are not equal\n");
			rv = -1;
		}
	}
done:
	if (ai != NULL)
		freeaddrinfo(ai);
	return (rv);
}

static int
hostent_test_getnameinfo_eq(struct hostent *he, void *mdata __unused)
{
	char **cp;
	char buffer[NI_MAXHOST];
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	struct sockaddr *saddr;
	struct hostent *result;
	int i, rv;

	if (he->h_addr_list == NULL)
		return (0);

	for (cp = he->h_addr_list; *cp; ++cp) {
#ifdef DEBUG
		printf("doing reverse lookup for %s\n", he->h_name);
#endif
		result = __gethostbyaddr(*cp, he->h_length,
		    he->h_addrtype);
		if (result != NULL) {
			rv = hostent_test_correctness(result, NULL);
			if (rv != 0) {
				__freehostent(result);
				return (rv);
			}
		} else
			printf("%s: warning: reverse lookup failed "
			    "for %s: %s\n", __func__, he->h_name,
			    strerror(errno));

		switch (he->h_addrtype) {
		case AF_INET:
			memset(&sin, 0, sizeof(struct sockaddr_in));
			sin.sin_len = sizeof(struct sockaddr_in);
			sin.sin_family = AF_INET;
			memcpy(&sin.sin_addr, *cp, he->h_length);

			saddr = (struct sockaddr *)&sin;
			break;
		case AF_INET6:
			memset(&sin6, 0, sizeof(struct sockaddr_in6));
			sin6.sin6_len = sizeof(struct sockaddr_in6);
			sin6.sin6_family = AF_INET6;
			memcpy(&sin6.sin6_addr, *cp, he->h_length);

			saddr = (struct sockaddr *)&sin6;
			break;
		default:
			printf("warning: %d family is unsupported\n",
			    he->h_addrtype);
			continue;
		}

		ATF_REQUIRE(saddr != NULL);
		rv = getnameinfo(saddr, saddr->sa_len, buffer,
			sizeof(buffer), NULL, 0, NI_NAMEREQD);

		if (rv != 0 && result != NULL) {
			printf("getnameinfo() didn't make the reverse "
			    "lookup, when it should have (%s)\n",
			    gai_strerror(rv));
			return (rv);
		}

		if (rv == 0 && result == NULL) {
			printf("getnameinfo() made the "
			    "reverse lookup, when it shouldn't have\n");
			return (rv);
		}

		if (rv != 0 && result == NULL) {
#ifdef DEBUG
			printf("both getnameinfo() and ***byaddr() failed as "
			    "expected\n");
#endif
			continue;
		}

#ifdef DEBUG
		printf("comparing %s with %s\n", result->h_name,
		    buffer);
#endif

		/*
		 * An address might reverse resolve to hostname alias or the
		 * official hostname, e.g. moon.vub.ac.be.
		 */
		bool found_a_match = false;

		if (strcmp(result->h_name, buffer) == 0) {
			found_a_match = true;
#ifdef DEBUG
			printf("matched official hostname\n");
#endif
		} else {
			for (i = 0; result->h_aliases[i] != NULL; i++) {
				printf("[%d] resolved: %s\n", i,
				    result->h_aliases[i]);
				if (strcmp(result->h_aliases[i],
				    buffer) == 0) {
					printf("matched hostname alias\n");
					found_a_match = true;
					break;
				}
			}
		}
		__freehostent(result);

		if (found_a_match) {
#ifdef DEBUG
			printf("getnameinfo() and ***byaddr() results are "
			    "equal\n");
#endif
		} else {
			printf("getnameinfo() and ***byaddr() results are not "
			    "equal for %s\n", he->h_name);
			return (-1);
		}
	}

	return (0);
}

static int
run_tests(const char *hostlist_file, const char *snapshot_file, int _af_type,
    enum test_methods method, bool use_ipv6_mapping)
{
	char *snapshot_file_copy;
	struct hostent_test_data td, td_addr, td_snap;
	res_state statp;
	int rv = -2;

	if (snapshot_file == NULL)
		snapshot_file_copy = NULL;
	else {
		snapshot_file_copy = strdup(snapshot_file);
		ATF_REQUIRE(snapshot_file_copy != NULL);
	}
	snapshot_file = snapshot_file_copy;

	switch (_af_type) {
	case AF_INET:
		ATF_REQUIRE_FEATURE("inet");
		ATF_REQUIRE(!use_ipv6_mapping);
		break;
	case AF_INET6:
		ATF_REQUIRE_FEATURE("inet6");
		break;
	default:
		atf_tc_fail("unhandled address family: %d", _af_type);
		break;
	}

	if (!use_ipnode_functions) {
		statp = __res_state();
		if (statp == NULL || ((statp->options & RES_INIT) == 0 &&
		    res_ninit(statp) == -1)) {
			printf("error: can't init res_state\n");
			rv = -1;
			goto fin2;
		}

		if (use_ipv6_mapping)
			statp->options |= RES_USE_INET6;
		else
			statp->options &= ~RES_USE_INET6;
	}

	TEST_DATA_INIT(hostent, &td, clone_hostent, free_hostent);
	TEST_DATA_INIT(hostent, &td_addr, clone_hostent, free_hostent);
	TEST_DATA_INIT(hostent, &td_snap, clone_hostent, free_hostent);

	if (access(hostlist_file, R_OK) != 0) {
		printf("can't access the hostlist file %s\n", hostlist_file);
		rv = -1;
		goto fin;
	}

#ifdef DEBUG
	printf("building host lists from %s\n", hostlist_file);
#endif

	rv = TEST_SNAPSHOT_FILE_READ(hostent, hostlist_file, &td,
		hostent_read_hostlist_func);
	if (rv != 0) {
		printf("failed to read the host list file: %s\n",
		    hostlist_file);
		goto fin;
	}

	if (snapshot_file != NULL) {
		if (access(snapshot_file, W_OK | R_OK) != 0) {
			if (errno == ENOENT) {
				if (method != TEST_GETHOSTBYADDR)
					method = TEST_BUILD_SNAPSHOT;
				else
					method = TEST_BUILD_ADDR_SNAPSHOT;
			} else {
				printf("can't access the snapshot file %s\n",
				    snapshot_file);
				rv = -1;
				goto fin;
			}
		} else {
			rv = TEST_SNAPSHOT_FILE_READ(hostent, snapshot_file,
				&td_snap, hostent_read_snapshot_func);
			if (rv != 0) {
				printf("error reading snapshot file\n");
				goto fin;
			}
		}
	}

	switch (method) {
	case TEST_GETHOSTBYNAME2:
		if (snapshot_file != NULL)
			rv = DO_2PASS_TEST(hostent, &td, &td_snap,
			    compare_hostent, NULL);
		break;
	case TEST_GETHOSTBYADDR:
		rv = DO_1PASS_TEST(hostent, &td,
			hostent_test_gethostbyaddr, (void *)&td_addr);
		if (rv != 0)
			goto fin;

		if (snapshot_file != NULL)
			rv = DO_2PASS_TEST(hostent, &td_addr, &td_snap,
			    compare_hostent, NULL);
		break;
	case TEST_GETHOSTBYNAME2_GETADDRINFO:
		rv = DO_1PASS_TEST(hostent, &td,
			hostent_test_getaddrinfo_eq, NULL);
		break;
	case TEST_GETHOSTBYADDR_GETNAMEINFO:
		rv = DO_1PASS_TEST(hostent, &td,
			hostent_test_getnameinfo_eq, NULL);
		break;
	case TEST_BUILD_SNAPSHOT:
		if (snapshot_file != NULL) {
			rv = TEST_SNAPSHOT_FILE_WRITE(hostent, snapshot_file,
			    &td, sdump_hostent);
		}
		break;
	case TEST_BUILD_ADDR_SNAPSHOT:
		if (snapshot_file != NULL) {
			rv = DO_1PASS_TEST(hostent, &td,
			    hostent_test_gethostbyaddr, (void *)&td_addr);
			if (rv != 0)
				goto fin;
			rv = TEST_SNAPSHOT_FILE_WRITE(hostent, snapshot_file,
			    &td_addr, sdump_hostent);
		}
		break;
	default:
		rv = 0;
		break;
	}

fin:
	TEST_DATA_DESTROY(hostent, &td_snap);
	TEST_DATA_DESTROY(hostent, &td_addr);
	TEST_DATA_DESTROY(hostent, &td);

fin2:
	free(snapshot_file_copy);

	return (rv);
}

#define	HOSTLIST_FILE	"mach"

#define	_RUN_TESTS(tc, snapshot_file, af_type, method, use_ipv6_mapping) \
do {									\
	char *_hostlist_file;						\
	ATF_REQUIRE(0 < asprintf(&_hostlist_file, "%s/%s",		\
	    atf_tc_get_config_var(tc, "srcdir"), HOSTLIST_FILE));	\
	ATF_REQUIRE(run_tests(_hostlist_file, snapshot_file, af_type,	\
	    method, use_ipv6_mapping) == 0);				\
	free(_hostlist_file);						\
} while (0)

#define	RUN_HOST_TESTS(tc, snapshot_file, af_type, method, use_ipv6_mapping) \
do {									\
	use_ipnode_functions = false; 					\
	_RUN_TESTS(tc, snapshot_file, af_type, method, use_ipv6_mapping); \
} while (0)

#define	RUN_IPNODE_TESTS(tc, snapshot_file, af_type, method, use_ipv6_mapping) \
do {									\
	use_ipnode_functions = true; 					\
	_RUN_TESTS(tc, snapshot_file, af_type, method, use_ipv6_mapping); \
} while (0)

ATF_TC_WITHOUT_HEAD(gethostbyaddr_ipv4);
ATF_TC_BODY(gethostbyaddr_ipv4, tc)
{

	RUN_HOST_TESTS(tc, NULL, AF_INET, TEST_GETHOSTBYADDR, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyaddr_ipv4_with_snapshot);
ATF_TC_BODY(gethostbyaddr_ipv4_with_snapshot, tc)
{

	RUN_HOST_TESTS(tc, "snapshot_htaddr4", AF_INET, TEST_GETHOSTBYADDR, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyaddr_ipv6);
ATF_TC_BODY(gethostbyaddr_ipv6, tc)
{

	RUN_HOST_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYADDR, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyaddr_ipv6_AI_V4MAPPED);
ATF_TC_BODY(gethostbyaddr_ipv6_AI_V4MAPPED, tc)
{

	ipnode_flags = AI_V4MAPPED;
	RUN_HOST_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYADDR, true);
}

ATF_TC_WITHOUT_HEAD(gethostbyaddr_ipv6_with_snapshot);
ATF_TC_BODY(gethostbyaddr_ipv6_with_snapshot, tc)
{

	RUN_HOST_TESTS(tc, "snapshot_htaddr6", AF_INET6, TEST_GETHOSTBYADDR, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyaddr_ipv6_with_snapshot_AI_V4MAPPED);
ATF_TC_BODY(gethostbyaddr_ipv6_with_snapshot_AI_V4MAPPED, tc)
{

	ipnode_flags = AI_V4MAPPED;
	RUN_HOST_TESTS(tc, "snapshot_htaddr6map", AF_INET6, TEST_GETHOSTBYADDR, true);
}

ATF_TC_WITHOUT_HEAD(gethostbyname2_getaddrinfo_ipv4);
ATF_TC_BODY(gethostbyname2_getaddrinfo_ipv4, tc)
{

	RUN_HOST_TESTS(tc, NULL, AF_INET, TEST_GETHOSTBYNAME2_GETADDRINFO, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyname2_getaddrinfo_ipv6);
ATF_TC_BODY(gethostbyname2_getaddrinfo_ipv6, tc)
{

	RUN_HOST_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYNAME2_GETADDRINFO, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyaddr_getnameinfo_ipv4);
ATF_TC_BODY(gethostbyaddr_getnameinfo_ipv4, tc)
{

	RUN_HOST_TESTS(tc, NULL, AF_INET, TEST_GETHOSTBYADDR_GETNAMEINFO, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyaddr_getnameinfo_ipv6);
ATF_TC_BODY(gethostbyaddr_getnameinfo_ipv6, tc)
{

	RUN_HOST_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYADDR_GETNAMEINFO, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyname2_ipv4);
ATF_TC_BODY(gethostbyname2_ipv4, tc)
{

	RUN_HOST_TESTS(tc, NULL, AF_INET, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyname2_ipv4_with_snapshot);
ATF_TC_BODY(gethostbyname2_ipv4_with_snapshot, tc)
{

	RUN_HOST_TESTS(tc, "snapshot_htname4", AF_INET, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyname2_ipv6);
ATF_TC_BODY(gethostbyname2_ipv6, tc)
{

	RUN_HOST_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyname2_ipv6_AI_V4MAPPED);
ATF_TC_BODY(gethostbyname2_ipv6_AI_V4MAPPED, tc)
{

	ipnode_flags = AI_V4MAPPED;
	RUN_HOST_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYNAME2, true);
}

ATF_TC_WITHOUT_HEAD(gethostbyname2_ipv6_with_snapshot);
ATF_TC_BODY(gethostbyname2_ipv6_with_snapshot, tc)
{

	RUN_HOST_TESTS(tc, "snapshot_htname6", AF_INET6, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(gethostbyname2_ipv6_with_snapshot_AI_V4MAPPED);
ATF_TC_BODY(gethostbyname2_ipv6_with_snapshot_AI_V4MAPPED, tc)
{

	ipnode_flags = AI_V4MAPPED;
	RUN_HOST_TESTS(tc, "snapshot_htname6map", AF_INET6, TEST_GETHOSTBYNAME2, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_ipv4);
ATF_TC_BODY(getipnodebyaddr_ipv4, tc)
{

	RUN_IPNODE_TESTS(tc, NULL, AF_INET, TEST_GETHOSTBYADDR, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_ipv4_with_snapshot);
ATF_TC_BODY(getipnodebyaddr_ipv4_with_snapshot, tc)
{

	RUN_IPNODE_TESTS(tc, "snapshot_ipnodeaddr4", AF_INET, TEST_GETHOSTBYADDR, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_getnameinfo_ipv4);
ATF_TC_BODY(getipnodebyaddr_getnameinfo_ipv4, tc)
{

	RUN_IPNODE_TESTS(tc, NULL, AF_INET, TEST_GETHOSTBYADDR_GETNAMEINFO, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_ipv6);
ATF_TC_BODY(getipnodebyaddr_ipv6, tc)
{

	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYADDR, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_ipv6_AI_V4MAPPED);
ATF_TC_BODY(getipnodebyaddr_ipv6_AI_V4MAPPED, tc)
{

	ipnode_flags = AI_V4MAPPED;
	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYADDR, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_ipv6_AI_V4MAPPED_CFG);
ATF_TC_BODY(getipnodebyaddr_ipv6_AI_V4MAPPED_CFG, tc)
{

	ipnode_flags = AI_V4MAPPED_CFG;
	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYADDR, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_ipv6_AI_V4MAPPED_CFG_AI_ALL);
ATF_TC_BODY(getipnodebyaddr_ipv6_AI_V4MAPPED_CFG_AI_ALL, tc)
{

	ipnode_flags = AI_V4MAPPED_CFG | AI_ALL;
	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYADDR, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_ipv6_with_snapshot);
ATF_TC_BODY(getipnodebyaddr_ipv6_with_snapshot, tc)
{

	RUN_IPNODE_TESTS(tc, "snapshot_ipnodeaddr6", AF_INET6, TEST_GETHOSTBYADDR, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_ipv6_with_snapshot_AI_V4MAPPED);
ATF_TC_BODY(getipnodebyaddr_ipv6_with_snapshot_AI_V4MAPPED, tc)
{

	ipnode_flags = AI_V4MAPPED;
	RUN_IPNODE_TESTS(tc,
	    "snapshot_ipnodeaddr6_AI_V4MAPPED", AF_INET6,
	    TEST_GETHOSTBYADDR, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_ipv6_with_snapshot_AI_V4MAPPED_CFG);
ATF_TC_BODY(getipnodebyaddr_ipv6_with_snapshot_AI_V4MAPPED_CFG, tc)
{

	ipnode_flags = AI_V4MAPPED_CFG;
	RUN_IPNODE_TESTS(tc,
	    "snapshot_ipnodeaddr6_AI_V4MAPPED_CFG", AF_INET6,
	    TEST_GETHOSTBYADDR, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_ipv6_with_snapshot_AI_V4MAPPED_CFG_AI_ALL);
ATF_TC_BODY(getipnodebyaddr_ipv6_with_snapshot_AI_V4MAPPED_CFG_AI_ALL, tc)
{

	ipnode_flags = AI_V4MAPPED_CFG | AI_ALL;
	RUN_IPNODE_TESTS(tc,
	    "snapshot_ipnodeaddr6_AI_V4MAPPED_CFG_AI_ALL", AF_INET6,
	    TEST_GETHOSTBYADDR, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyaddr_getnameinfo_ipv6);
ATF_TC_BODY(getipnodebyaddr_getnameinfo_ipv6, tc)
{

	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYADDR_GETNAMEINFO, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv4);
ATF_TC_BODY(getipnodebyname_ipv4, tc)
{

	RUN_IPNODE_TESTS(tc, NULL, AF_INET, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv4_with_snapshot);
ATF_TC_BODY(getipnodebyname_ipv4_with_snapshot, tc)
{

	RUN_IPNODE_TESTS(tc, "snapshot_ipnodename4", AF_INET, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv4_AI_ADDRCONFIG);
ATF_TC_BODY(getipnodebyname_ipv4_AI_ADDRCONFIG, tc)
{

	ipnode_flags = AI_ADDRCONFIG;
	RUN_IPNODE_TESTS(tc, NULL, AF_INET, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv4_with_snapshot_AI_ADDRCONFIG);
ATF_TC_BODY(getipnodebyname_ipv4_with_snapshot_AI_ADDRCONFIG, tc)
{

	ipnode_flags = AI_ADDRCONFIG;
	RUN_IPNODE_TESTS(tc, "snapshot_ipnodename4_AI_ADDRCONFIG", AF_INET,
	    TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_getaddrinfo_ipv4);
ATF_TC_BODY(getipnodebyname_getaddrinfo_ipv4, tc)
{

	RUN_IPNODE_TESTS(tc, NULL, AF_INET, TEST_GETHOSTBYNAME2_GETADDRINFO, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6);
ATF_TC_BODY(getipnodebyname_ipv6, tc)
{

	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_with_snapshot);
ATF_TC_BODY(getipnodebyname_ipv6_with_snapshot, tc)
{

	RUN_IPNODE_TESTS(tc, "snapshot_ipnodename6", AF_INET6, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_AI_ADDRCONFIG);
ATF_TC_BODY(getipnodebyname_ipv6_AI_ADDRCONFIG, tc)
{

	ipnode_flags = AI_ADDRCONFIG;
	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_AI_V4MAPPED);
ATF_TC_BODY(getipnodebyname_ipv6_AI_V4MAPPED, tc)
{

	ipnode_flags = AI_V4MAPPED;
	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYNAME2, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_AI_V4MAPPED_CFG);
ATF_TC_BODY(getipnodebyname_ipv6_AI_V4MAPPED_CFG, tc)
{

	ipnode_flags = AI_V4MAPPED_CFG;
	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYNAME2, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_AI_V4MAPPED_CFG_AI_ADDRCONFIG);
ATF_TC_BODY(getipnodebyname_ipv6_AI_V4MAPPED_CFG_AI_ADDRCONFIG, tc)
{

	ipnode_flags = AI_V4MAPPED_CFG | AI_ADDRCONFIG;
	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_AI_V4MAPPED_CFG_AI_ALL);
ATF_TC_BODY(getipnodebyname_ipv6_AI_V4MAPPED_CFG_AI_ALL, tc)
{

	ipnode_flags = AI_V4MAPPED_CFG | AI_ALL;
	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYNAME2, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED);
ATF_TC_BODY(getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED, tc)
{

	ipnode_flags = AI_V4MAPPED;
	RUN_IPNODE_TESTS(tc,
	    "snapshot_ipnodename6_AI_V4MAPPED", AF_INET6,
	    TEST_GETHOSTBYNAME2, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED_CFG);
ATF_TC_BODY(getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED_CFG, tc)
{

	ipnode_flags = AI_V4MAPPED_CFG;
	RUN_IPNODE_TESTS(tc,
	    "snapshot_ipnodename6_AI_V4MAPPED_CFG", AF_INET6,
	    TEST_GETHOSTBYNAME2, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED_CFG_AI_ADDRCONFIG);
ATF_TC_BODY(getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED_CFG_AI_ADDRCONFIG, tc)
{

	ipnode_flags = AI_V4MAPPED_CFG | AI_ADDRCONFIG;
	RUN_IPNODE_TESTS(tc,
	    "snapshot_ipnodename6_AI_V4MAPPED_CFG_AI_ADDRCONFIG", AF_INET6,
	    TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED_CFG_AI_ALL);
ATF_TC_BODY(getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED_CFG_AI_ALL, tc)
{

	ipnode_flags = AI_V4MAPPED_CFG | AI_ALL;
	RUN_IPNODE_TESTS(tc,
	    "snapshot_ipnodename6_AI_V4MAPPED_CFG_AI_ALL", AF_INET6,
	    TEST_GETHOSTBYNAME2, true);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_ipv6_with_snapshot_AI_ADDRCONFIG);
ATF_TC_BODY(getipnodebyname_ipv6_with_snapshot_AI_ADDRCONFIG, tc)
{

	ipnode_flags = AI_ADDRCONFIG;
	RUN_IPNODE_TESTS(tc, "snapshot_ipnodename6_AI_ADDRCONFIG", AF_INET6,
	    TEST_GETHOSTBYNAME2, false);
}

ATF_TC_WITHOUT_HEAD(getipnodebyname_getaddrinfo_ipv6);
ATF_TC_BODY(getipnodebyname_getaddrinfo_ipv6, tc)
{

	RUN_IPNODE_TESTS(tc, NULL, AF_INET6, TEST_GETHOSTBYNAME2_GETADDRINFO, false);
}

ATF_TP_ADD_TCS(tp)
{

	/* gethostbyaddr */
	ATF_TP_ADD_TC(tp, gethostbyaddr_ipv4);
	ATF_TP_ADD_TC(tp, gethostbyaddr_ipv4_with_snapshot);
	ATF_TP_ADD_TC(tp, gethostbyaddr_ipv6);
	ATF_TP_ADD_TC(tp, gethostbyaddr_ipv6_AI_V4MAPPED); /* XXX */
	ATF_TP_ADD_TC(tp, gethostbyaddr_ipv6_with_snapshot);
	ATF_TP_ADD_TC(tp, gethostbyaddr_ipv6_with_snapshot_AI_V4MAPPED);
	ATF_TP_ADD_TC(tp, gethostbyaddr_getnameinfo_ipv4);
	ATF_TP_ADD_TC(tp, gethostbyaddr_getnameinfo_ipv6);

	/* gethostbyname2 */
	ATF_TP_ADD_TC(tp, gethostbyname2_getaddrinfo_ipv4);
	ATF_TP_ADD_TC(tp, gethostbyname2_getaddrinfo_ipv6);
	ATF_TP_ADD_TC(tp, gethostbyname2_ipv4);
	ATF_TP_ADD_TC(tp, gethostbyname2_ipv4_with_snapshot);
	ATF_TP_ADD_TC(tp, gethostbyname2_ipv6);
	ATF_TP_ADD_TC(tp, gethostbyname2_ipv6_AI_V4MAPPED);
	ATF_TP_ADD_TC(tp, gethostbyname2_ipv6_with_snapshot);
	ATF_TP_ADD_TC(tp, gethostbyname2_ipv6_with_snapshot_AI_V4MAPPED);

	/* getipnodebyaddr */
	ATF_TP_ADD_TC(tp, getipnodebyaddr_ipv4);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_ipv4_with_snapshot);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_getnameinfo_ipv4);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_ipv6);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_ipv6_AI_V4MAPPED);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_ipv6_AI_V4MAPPED_CFG);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_ipv6_AI_V4MAPPED_CFG_AI_ALL);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_ipv6_with_snapshot);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_ipv6_with_snapshot_AI_V4MAPPED);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_ipv6_with_snapshot_AI_V4MAPPED_CFG);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_ipv6_with_snapshot_AI_V4MAPPED_CFG_AI_ALL);
	ATF_TP_ADD_TC(tp, getipnodebyaddr_getnameinfo_ipv6);

	/* getipnodebyname */
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv4);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv4_with_snapshot);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv4_AI_ADDRCONFIG);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv4_with_snapshot_AI_ADDRCONFIG);
	ATF_TP_ADD_TC(tp, getipnodebyname_getaddrinfo_ipv4);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_with_snapshot);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_AI_ADDRCONFIG);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_AI_V4MAPPED);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_AI_V4MAPPED_CFG);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_AI_V4MAPPED_CFG_AI_ADDRCONFIG);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_AI_V4MAPPED_CFG_AI_ALL);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED_CFG);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED_CFG_AI_ADDRCONFIG);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_with_snapshot_AI_V4MAPPED_CFG_AI_ALL);
	ATF_TP_ADD_TC(tp, getipnodebyname_ipv6_with_snapshot_AI_ADDRCONFIG);
	ATF_TP_ADD_TC(tp, getipnodebyname_getaddrinfo_ipv6);

	return (atf_no_error());
}

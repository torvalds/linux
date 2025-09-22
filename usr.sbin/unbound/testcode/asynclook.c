/*
 * testcode/asynclook.c - debug program perform async libunbound queries.
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This program shows the results from several background lookups,
 * while printing time in the foreground.
 */

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include "libunbound/unbound.h"
#include "libunbound/context.h"
#include "util/locks.h"
#include "util/log.h"
#include "sldns/rrdef.h"
#ifdef UNBOUND_ALLOC_LITE
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup
#endif
#ifdef HAVE_SSL
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#endif /* HAVE_SSL */


/** keeping track of the async ids */
struct track_id {
	/** the id to pass to libunbound to cancel */
	int id;
	/** true if cancelled */
	int cancel;
	/** a lock on this structure for thread safety */
	lock_basic_type lock;
};

/**
 * result list for the lookups
 */
struct lookinfo {
	/** name to look up */
	char* name;
	/** tracking number that can be used to cancel the query */
	int async_id;
	/** error code from libunbound */
	int err;
	/** result from lookup */
	struct ub_result* result;
};

/** global variable to see how many queries we have left */
static int num_wait = 0;

/** usage information for asynclook */
static void usage(char* argv[])
{
	printf("usage: %s [options] name ...\n", argv[0]);
	printf("names are looked up at the same time, asynchronously.\n");
	printf("	-b : use blocking requests\n");
	printf("	-c : cancel the requests\n");
	printf("	-d : enable debug output\n");
	printf("	-f addr : use addr, forward to that server\n");
	printf("	-h : this help message\n");
	printf("	-H fname : read hosts from fname\n");
	printf("	-r fname : read resolv.conf from fname\n");
	printf("	-t : use a resolver thread instead of forking a process\n");
	printf("	-x : perform extended threaded test\n");
	exit(1);
}

/** print result from lookup nicely */
static void
print_result(struct lookinfo* info)
{
	char buf[100];
	if(info->err) /* error (from libunbound) */
		printf("%s: error %s\n", info->name,
			ub_strerror(info->err));
	else if(!info->result)
		printf("%s: cancelled\n", info->name);
	else if(info->result->havedata)
		printf("%s: %s\n", info->name,
			inet_ntop(AF_INET, info->result->data[0],
			buf, (socklen_t)sizeof(buf)));
	else {
		/* there is no data, why that? */
		if(info->result->rcode == 0 /*noerror*/ ||
			info->result->nxdomain)
			printf("%s: no data %s\n", info->name,
			info->result->nxdomain?"(no such host)":
			"(no IP4 address)");
		else	/* some error (from the server) */
			printf("%s: DNS error %d\n", info->name,
				info->result->rcode);
	}
}

/** this is a function of type ub_callback_t */
static void 
lookup_is_done(void* mydata, int err, struct ub_result* result)
{
	/* cast mydata back to the correct type */
	struct lookinfo* info = (struct lookinfo*)mydata;
	fprintf(stderr, "name %s resolved\n", info->name);
	info->err = err;
	info->result = result;
	/* one less to wait for */
	num_wait--;
}

/** check error, if bad, exit with error message */
static void 
checkerr(const char* desc, int err)
{
	if(err != 0) {
		printf("%s error: %s\n", desc, ub_strerror(err));
		exit(1);
	}
}

#ifdef THREADS_DISABLED
/** only one process can communicate with async worker */
#define NUMTHR 1
#else /* have threads */
/** number of threads to make in extended test */
#define NUMTHR 10
#endif

/** struct for extended thread info */
struct ext_thr_info {
	/** thread num for debug */
	int thread_num;
	/** thread id */
	ub_thread_type tid;
	/** context */
	struct ub_ctx* ctx;
	/** size of array to query */
	int argc;
	/** array of names to query */
	char** argv;
	/** number of queries to do */
	int numq;
	/** list of ids to free once threads are done */
	struct track_id* id_list;
};

/** if true, we are testing against 'localhost' and extra checking is done */
static int q_is_localhost = 0;

/** check result structure for the 'correct' answer */
static void
ext_check_result(const char* desc, int err, struct ub_result* result)
{
	checkerr(desc, err);
	if(result == NULL) {
		printf("%s: error result is NULL.\n", desc);
		exit(1);
	}
	if(q_is_localhost) {
		if(strcmp(result->qname, "localhost") != 0) {
			printf("%s: error result has wrong qname.\n", desc);
			exit(1);
		}
		if(result->qtype != LDNS_RR_TYPE_A) {
			printf("%s: error result has wrong qtype.\n", desc);
			exit(1);
		}
		if(result->qclass != LDNS_RR_CLASS_IN) {
			printf("%s: error result has wrong qclass.\n", desc);
			exit(1);
		}
		if(result->data == NULL) {
			printf("%s: error result->data is NULL.\n", desc);
			exit(1);
		}
		if(result->len == NULL) {
			printf("%s: error result->len is NULL.\n", desc);
			exit(1);
		}
		if(result->rcode != 0) {
			printf("%s: error result->rcode is set.\n", desc);
			exit(1);
		}
		if(result->havedata == 0) {
			printf("%s: error result->havedata is unset.\n", desc);
			exit(1);
		}
		if(result->nxdomain != 0) {
			printf("%s: error result->nxdomain is set.\n", desc);
			exit(1);
		}
		if(result->secure || result->bogus) {
			printf("%s: error result->secure or bogus is set.\n", 
				desc);
			exit(1);
		}
		if(result->data[0] == NULL) {
			printf("%s: error result->data[0] is NULL.\n", desc);
			exit(1);
		}
		if(result->len[0] != 4) {
			printf("%s: error result->len[0] is wrong.\n", desc);
			exit(1);
		}
		if(result->len[1] != 0 || result->data[1] != NULL) {
			printf("%s: error result->data[1] or len[1] is "
				"wrong.\n", desc);
			exit(1);
		}
		if(result->answer_packet == NULL) {
			printf("%s: error result->answer_packet is NULL.\n", 
				desc);
			exit(1);
		}
		if(result->answer_len != 54) {
			printf("%s: error result->answer_len is wrong.\n", 
				desc);
			exit(1);
		}
	}
}

/** extended bg result callback, this function is ub_callback_t */
static void 
ext_callback(void* mydata, int err, struct ub_result* result)
{
	struct track_id* my_id = (struct track_id*)mydata;
	int doprint = 0;
	if(my_id) {
		/* I have an id, make sure we are not cancelled */
		lock_basic_lock(&my_id->lock);
		if(doprint) 
			printf("cb %d: ", my_id->id);
		if(my_id->cancel) {
			printf("error: query id=%d returned, but was cancelled\n",
				my_id->id);
			abort();
			exit(1);
		}
		lock_basic_unlock(&my_id->lock);
	}
	ext_check_result("ext_callback", err, result);
	log_assert(result);
	if(doprint) {
		struct lookinfo pi;
		pi.name = result?result->qname:"noname";
		pi.result = result;
		pi.err = 0;
		print_result(&pi);
	}
	ub_resolve_free(result);
}

/** extended thread worker */
static void*
ext_thread(void* arg)
{
	struct ext_thr_info* inf = (struct ext_thr_info*)arg;
	int i, r;
	struct ub_result* result;
	struct track_id* async_ids = NULL;
	log_thread_set(&inf->thread_num);
	if(inf->thread_num > NUMTHR*2/3) {
		async_ids = (struct track_id*)calloc((size_t)inf->numq, sizeof(struct track_id));
		if(!async_ids) {
			printf("out of memory\n");
			exit(1);
		}
		for(i=0; i<inf->numq; i++) {
			lock_basic_init(&async_ids[i].lock);
		}
		inf->id_list = async_ids;
	}
	for(i=0; i<inf->numq; i++) {
		if(async_ids) {
			r = ub_resolve_async(inf->ctx, 
				inf->argv[i%inf->argc], LDNS_RR_TYPE_A, 
				LDNS_RR_CLASS_IN, &async_ids[i], ext_callback, 
				&async_ids[i].id);
			checkerr("ub_resolve_async", r);
			if(i > 100) {
				lock_basic_lock(&async_ids[i-100].lock);
				r = ub_cancel(inf->ctx, async_ids[i-100].id);
				if(r != UB_NOID)
					async_ids[i-100].cancel=1;
				lock_basic_unlock(&async_ids[i-100].lock);
				if(r != UB_NOID) 
					checkerr("ub_cancel", r);
			}
		} else if(inf->thread_num > NUMTHR/2) {
			/* async */
			r = ub_resolve_async(inf->ctx, 
				inf->argv[i%inf->argc], LDNS_RR_TYPE_A, 
				LDNS_RR_CLASS_IN, NULL, ext_callback, NULL);
			checkerr("ub_resolve_async", r);
		} else  {
			/* blocking */
			r = ub_resolve(inf->ctx, inf->argv[i%inf->argc], 
				LDNS_RR_TYPE_A, LDNS_RR_CLASS_IN, &result);
			ext_check_result("ub_resolve", r, result);
			ub_resolve_free(result);
		}
	}
	if(inf->thread_num > NUMTHR/2) {
		r = ub_wait(inf->ctx);
		checkerr("ub_ctx_wait", r);
	}
	/* if these locks are destroyed, or if the async_ids is freed, then
	   a use-after-free happens in another thread.
	   The allocation is only part of this test, though. */
	
	return NULL;
}

/** perform extended threaded test */
static int
ext_test(struct ub_ctx* ctx, int argc, char** argv)
{
	struct ext_thr_info inf[NUMTHR];
	int i;
	if(argc == 1 && strcmp(argv[0], "localhost") == 0)
		q_is_localhost = 1;
	printf("extended test start (%d threads)\n", NUMTHR);
	for(i=0; i<NUMTHR; i++) {
		/* 0 = this, 1 = library bg worker */
		inf[i].thread_num = i+2;
		inf[i].ctx = ctx;
		inf[i].argc = argc;
		inf[i].argv = argv;
		inf[i].numq = 100;
		inf[i].id_list = NULL;
		ub_thread_create(&inf[i].tid, ext_thread, &inf[i]);
	}
	/* the work happens here */
	for(i=0; i<NUMTHR; i++) {
		ub_thread_join(inf[i].tid);
	}
	printf("extended test end\n");
	/* free the id lists */
	for(i=0; i<NUMTHR; i++) {
		if(inf[i].id_list) {
			int j;
			for(j=0; j<inf[i].numq; j++) {
				lock_basic_destroy(&inf[i].id_list[j].lock);
			}
			free(inf[i].id_list);
		}
	}
	ub_ctx_delete(ctx);
	checklock_stop();
	return 0;
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** main program for asynclook */
int main(int argc, char** argv) 
{
	int c;
	struct ub_ctx* ctx;
	struct lookinfo* lookups;
	int i, r, cancel=0, blocking=0, ext=0;

	checklock_start();
	/* init log now because solaris thr_key_create() is not threadsafe */
	log_init(0,0,0);
	/* lock debug start (if any) */

	/* create context */
	ctx = ub_ctx_create();
	if(!ctx) {
		printf("could not create context, %s\n", strerror(errno));
		return 1;
	}

	/* command line options */
	if(argc == 1) {
		usage(argv);
	}
	while( (c=getopt(argc, argv, "bcdf:hH:r:tx")) != -1) {
		switch(c) {
			case 'd':
				r = ub_ctx_debuglevel(ctx, 3);
				checkerr("ub_ctx_debuglevel", r);
				break;
			case 't':
				r = ub_ctx_async(ctx, 1);
				checkerr("ub_ctx_async", r);
				break;
			case 'c':
				cancel = 1;
				break;
			case 'b':
				blocking = 1;
				break;
			case 'r':
				r = ub_ctx_resolvconf(ctx, optarg);
				if(r != 0) {
					printf("ub_ctx_resolvconf "
						"error: %s : %s\n",
						ub_strerror(r), 
						strerror(errno));
					return 1;
				}
				break;
			case 'H':
				r = ub_ctx_hosts(ctx, optarg);
				if(r != 0) {
					printf("ub_ctx_hosts "
						"error: %s : %s\n",
						ub_strerror(r), 
						strerror(errno));
					return 1;
				}
				break;
			case 'f':
				r = ub_ctx_set_fwd(ctx, optarg);
				checkerr("ub_ctx_set_fwd", r);
				break;
			case 'x':
				ext = 1;
				break;
			case 'h':
			case '?':
			default:
				usage(argv);
		}
	}
	argc -= optind;
	argv += optind;

#ifdef HAVE_SSL
#ifdef HAVE_ERR_LOAD_CRYPTO_STRINGS
	ERR_load_crypto_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	ERR_load_SSL_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_CRYPTO)
#  ifndef S_SPLINT_S
	OpenSSL_add_all_algorithms();
#  endif
#else
	OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS
		| OPENSSL_INIT_ADD_ALL_DIGESTS
		| OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	(void)SSL_library_init();
#else
	(void)OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
#endif
#endif /* HAVE_SSL */

	if(ext)
		return ext_test(ctx, argc, argv);

	/* allocate array for results. */
	lookups = (struct lookinfo*)calloc((size_t)argc, 
		sizeof(struct lookinfo));
	if(!lookups) {
		printf("out of memory\n");
		return 1;
	}

	/* perform asynchronous calls */
	num_wait = argc;
	for(i=0; i<argc; i++) {
		lookups[i].name = argv[i];
		if(blocking) {
			fprintf(stderr, "lookup %s\n", argv[i]);
			r = ub_resolve(ctx, argv[i], LDNS_RR_TYPE_A,
				LDNS_RR_CLASS_IN, &lookups[i].result);
			checkerr("ub_resolve", r);
		} else {
			fprintf(stderr, "start async lookup %s\n", argv[i]);
			r = ub_resolve_async(ctx, argv[i], LDNS_RR_TYPE_A,
				LDNS_RR_CLASS_IN, &lookups[i], &lookup_is_done, 
				&lookups[i].async_id);
			checkerr("ub_resolve_async", r);
		}
	}
	if(blocking)
		num_wait = 0;
	else if(cancel) {
		for(i=0; i<argc; i++) {
			fprintf(stderr, "cancel %s\n", argv[i]);
			r = ub_cancel(ctx, lookups[i].async_id);
			if(r != UB_NOID) 
				checkerr("ub_cancel", r);
		}
		num_wait = 0;
	}

	/* wait while the hostnames are looked up. Do something useful here */
	if(num_wait > 0)
	    for(i=0; i<1000; i++) {
		usleep(100000);
		fprintf(stderr, "%g seconds passed\n", 0.1*(double)i);
		r = ub_process(ctx);
		checkerr("ub_process", r);
		if(num_wait == 0)
			break;
	}
	if(i>=999) {
		printf("timed out\n");
		return 0;
	}
	printf("lookup complete\n");

	/* print lookup results */
	for(i=0; i<argc; i++) {
		print_result(&lookups[i]);
		ub_resolve_free(lookups[i].result);
	}

	ub_ctx_delete(ctx);
	free(lookups);
	checklock_stop();
	return 0;
}

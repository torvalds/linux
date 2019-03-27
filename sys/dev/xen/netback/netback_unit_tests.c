/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2011 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Justin T. Gibbs     (Spectra Logic Corporation)
 *          Alan Somers         (Spectra Logic Corporation)
 *          John Suykerbuyk     (Spectra Logic Corporation)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * \file netback_unit_tests.c
 *
 * \brief Unit tests for the Xen netback driver.
 *
 * Due to the driver's use of static functions, these tests cannot be compiled
 * standalone; they must be #include'd from the driver's .c file.
 */


/** Helper macro used to snprintf to a buffer and update the buffer pointer */
#define	SNCATF(buffer, buflen, ...) do {				\
	size_t new_chars = snprintf(buffer, buflen, __VA_ARGS__);	\
	buffer += new_chars;						\
	/* be careful; snprintf's return value can be  > buflen */	\
	buflen -= MIN(buflen, new_chars);				\
} while (0)

/* STRINGIFY and TOSTRING are used only to help turn __LINE__ into a string */
#define	STRINGIFY(x) #x
#define	TOSTRING(x) STRINGIFY(x)

/**
 * Writes an error message to buffer if cond is false
 * Note the implied parameters buffer and
 * buflen
 */
#define	XNB_ASSERT(cond) ({						\
	int passed = (cond);						\
	char *_buffer = (buffer);					\
	size_t _buflen = (buflen);					\
	if (! passed) {							\
		strlcat(_buffer, __func__, _buflen);			\
		strlcat(_buffer, ":" TOSTRING(__LINE__) 		\
		  " Assertion Error: " #cond "\n", _buflen);		\
	}								\
	})


/**
 * The signature used by all testcases.  If the test writes anything
 * to buffer, then it will be considered a failure
 * \param buffer	Return storage for error messages
 * \param buflen	The space available in the buffer
 */
typedef void testcase_t(char *buffer, size_t buflen);

/**
 * Signature used by setup functions
 * \return nonzero on error
 */
typedef int setup_t(void);

typedef void teardown_t(void);

/** A simple test fixture comprising setup, teardown, and test */
struct test_fixture {
	/** Will be run before the test to allocate and initialize variables */
	setup_t *setup;

	/** Will be run if setup succeeds */
	testcase_t *test;

	/** Cleans up test data whether or not the setup succeeded */
	teardown_t *teardown;
};

typedef struct test_fixture test_fixture_t;

static int	xnb_get1pkt(struct xnb_pkt *pkt, size_t size, uint16_t flags);
static int	xnb_unit_test_runner(test_fixture_t const tests[], int ntests,
				     char *buffer, size_t buflen);

static int __unused
null_setup(void) { return 0; }

static void __unused
null_teardown(void) { }

static setup_t setup_pvt_data;
static teardown_t teardown_pvt_data;
static testcase_t xnb_ring2pkt_emptyring;
static testcase_t xnb_ring2pkt_1req;
static testcase_t xnb_ring2pkt_2req;
static testcase_t xnb_ring2pkt_3req;
static testcase_t xnb_ring2pkt_extra;
static testcase_t xnb_ring2pkt_partial;
static testcase_t xnb_ring2pkt_wraps;
static testcase_t xnb_txpkt2rsp_emptypkt;
static testcase_t xnb_txpkt2rsp_1req;
static testcase_t xnb_txpkt2rsp_extra;
static testcase_t xnb_txpkt2rsp_long;
static testcase_t xnb_txpkt2rsp_invalid;
static testcase_t xnb_txpkt2rsp_error;
static testcase_t xnb_txpkt2rsp_wraps;
static testcase_t xnb_pkt2mbufc_empty;
static testcase_t xnb_pkt2mbufc_short;
static testcase_t xnb_pkt2mbufc_csum;
static testcase_t xnb_pkt2mbufc_1cluster;
static testcase_t xnb_pkt2mbufc_largecluster;
static testcase_t xnb_pkt2mbufc_2cluster;
static testcase_t xnb_txpkt2gnttab_empty;
static testcase_t xnb_txpkt2gnttab_short;
static testcase_t xnb_txpkt2gnttab_2req;
static testcase_t xnb_txpkt2gnttab_2cluster;
static testcase_t xnb_update_mbufc_short;
static testcase_t xnb_update_mbufc_2req;
static testcase_t xnb_update_mbufc_2cluster;
static testcase_t xnb_mbufc2pkt_empty;
static testcase_t xnb_mbufc2pkt_short;
static testcase_t xnb_mbufc2pkt_1cluster;
static testcase_t xnb_mbufc2pkt_2short;
static testcase_t xnb_mbufc2pkt_long;
static testcase_t xnb_mbufc2pkt_extra;
static testcase_t xnb_mbufc2pkt_nospace;
static testcase_t xnb_rxpkt2gnttab_empty;
static testcase_t xnb_rxpkt2gnttab_short;
static testcase_t xnb_rxpkt2gnttab_2req;
static testcase_t xnb_rxpkt2rsp_empty;
static testcase_t xnb_rxpkt2rsp_short;
static testcase_t xnb_rxpkt2rsp_extra;
static testcase_t xnb_rxpkt2rsp_2short;
static testcase_t xnb_rxpkt2rsp_2slots;
static testcase_t xnb_rxpkt2rsp_copyerror;
static testcase_t xnb_sscanf_llu;
static testcase_t xnb_sscanf_lld;
static testcase_t xnb_sscanf_hhu;
static testcase_t xnb_sscanf_hhd;
static testcase_t xnb_sscanf_hhn;

#if defined(INET) || defined(INET6)
/* TODO: add test cases for xnb_add_mbuf_cksum for IPV6 tcp and udp */
static testcase_t xnb_add_mbuf_cksum_arp;
static testcase_t xnb_add_mbuf_cksum_tcp;
static testcase_t xnb_add_mbuf_cksum_udp;
static testcase_t xnb_add_mbuf_cksum_icmp;
static testcase_t xnb_add_mbuf_cksum_tcp_swcksum;
static void	xnb_fill_eh_and_ip(struct mbuf *m, uint16_t ip_len,
				   uint16_t ip_id, uint16_t ip_p,
				   uint16_t ip_off, uint16_t ip_sum);
static void	xnb_fill_tcp(struct mbuf *m);
#endif /* INET || INET6 */

/** Private data used by unit tests */
static struct {
	gnttab_copy_table 	gnttab;
	netif_rx_back_ring_t	rxb;
	netif_rx_front_ring_t	rxf;
	netif_tx_back_ring_t	txb;
	netif_tx_front_ring_t	txf;
	struct ifnet*		ifp;
	netif_rx_sring_t*	rxs;
	netif_tx_sring_t*	txs;
} xnb_unit_pvt;

static inline void safe_m_freem(struct mbuf **ppMbuf) {
	if (*ppMbuf != NULL) {
		m_freem(*ppMbuf);
		*ppMbuf = NULL;
	}
}

/**
 * The unit test runner.  It will run every supplied test and return an
 * output message as a string
 * \param tests		An array of tests.  Every test will be attempted.
 * \param ntests	The length of tests
 * \param buffer	Return storage for the result string
 * \param buflen	The length of buffer
 * \return		The number of tests that failed
 */
static int
xnb_unit_test_runner(test_fixture_t const tests[], int ntests, char *buffer,
    		     size_t buflen)
{
	int i;
	int n_passes;
	int n_failures = 0;

	for (i = 0; i < ntests; i++) {
		int error = tests[i].setup();
		if (error != 0) {
			SNCATF(buffer, buflen,
			    "Setup failed for test idx %d\n", i);
			n_failures++;
		} else {
			size_t new_chars;

			tests[i].test(buffer, buflen);
			new_chars = strnlen(buffer, buflen);
			buffer += new_chars;
			buflen -= new_chars;

			if (new_chars > 0) {
				n_failures++;
			}
		}
		tests[i].teardown();
	}

	n_passes = ntests - n_failures;
	if (n_passes > 0) {
		SNCATF(buffer, buflen, "%d Tests Passed\n", n_passes);
	}
	if (n_failures > 0) {
		SNCATF(buffer, buflen, "%d Tests FAILED\n", n_failures);
	}

	return n_failures;
}

/** Number of unit tests.  Must match the length of the tests array below */
#define	TOTAL_TESTS	(53)
/**
 * Max memory available for returning results.  400 chars/test should give
 * enough space for a five line error message for every test
 */
#define	TOTAL_BUFLEN	(400 * TOTAL_TESTS + 2)

/**
 * Called from userspace by a sysctl.  Runs all internal unit tests, and
 * returns the results to userspace as a string
 * \param oidp	unused
 * \param arg1	pointer to an xnb_softc for a specific xnb device
 * \param arg2	unused
 * \param req	sysctl access structure
 * \return a string via the special SYSCTL_OUT macro.
 */

static int
xnb_unit_test_main(SYSCTL_HANDLER_ARGS) {
	test_fixture_t const tests[TOTAL_TESTS] = {
		{setup_pvt_data, xnb_ring2pkt_emptyring, teardown_pvt_data},
		{setup_pvt_data, xnb_ring2pkt_1req, teardown_pvt_data},
		{setup_pvt_data, xnb_ring2pkt_2req, teardown_pvt_data},
		{setup_pvt_data, xnb_ring2pkt_3req, teardown_pvt_data},
		{setup_pvt_data, xnb_ring2pkt_extra, teardown_pvt_data},
		{setup_pvt_data, xnb_ring2pkt_partial, teardown_pvt_data},
		{setup_pvt_data, xnb_ring2pkt_wraps, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2rsp_emptypkt, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2rsp_1req, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2rsp_extra, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2rsp_long, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2rsp_invalid, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2rsp_error, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2rsp_wraps, teardown_pvt_data},
		{setup_pvt_data, xnb_pkt2mbufc_empty, teardown_pvt_data},
		{setup_pvt_data, xnb_pkt2mbufc_short, teardown_pvt_data},
		{setup_pvt_data, xnb_pkt2mbufc_csum, teardown_pvt_data},
		{setup_pvt_data, xnb_pkt2mbufc_1cluster, teardown_pvt_data},
		{setup_pvt_data, xnb_pkt2mbufc_largecluster, teardown_pvt_data},
		{setup_pvt_data, xnb_pkt2mbufc_2cluster, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2gnttab_empty, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2gnttab_short, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2gnttab_2req, teardown_pvt_data},
		{setup_pvt_data, xnb_txpkt2gnttab_2cluster, teardown_pvt_data},
		{setup_pvt_data, xnb_update_mbufc_short, teardown_pvt_data},
		{setup_pvt_data, xnb_update_mbufc_2req, teardown_pvt_data},
		{setup_pvt_data, xnb_update_mbufc_2cluster, teardown_pvt_data},
		{setup_pvt_data, xnb_mbufc2pkt_empty, teardown_pvt_data},
		{setup_pvt_data, xnb_mbufc2pkt_short, teardown_pvt_data},
		{setup_pvt_data, xnb_mbufc2pkt_1cluster, teardown_pvt_data},
		{setup_pvt_data, xnb_mbufc2pkt_2short, teardown_pvt_data},
		{setup_pvt_data, xnb_mbufc2pkt_long, teardown_pvt_data},
		{setup_pvt_data, xnb_mbufc2pkt_extra, teardown_pvt_data},
		{setup_pvt_data, xnb_mbufc2pkt_nospace, teardown_pvt_data},
		{setup_pvt_data, xnb_rxpkt2gnttab_empty, teardown_pvt_data},
		{setup_pvt_data, xnb_rxpkt2gnttab_short, teardown_pvt_data},
		{setup_pvt_data, xnb_rxpkt2gnttab_2req, teardown_pvt_data},
		{setup_pvt_data, xnb_rxpkt2rsp_empty, teardown_pvt_data},
		{setup_pvt_data, xnb_rxpkt2rsp_short, teardown_pvt_data},
		{setup_pvt_data, xnb_rxpkt2rsp_extra, teardown_pvt_data},
		{setup_pvt_data, xnb_rxpkt2rsp_2short, teardown_pvt_data},
		{setup_pvt_data, xnb_rxpkt2rsp_2slots, teardown_pvt_data},
		{setup_pvt_data, xnb_rxpkt2rsp_copyerror, teardown_pvt_data},
#if defined(INET) || defined(INET6)
		{null_setup, xnb_add_mbuf_cksum_arp, null_teardown},
		{null_setup, xnb_add_mbuf_cksum_icmp, null_teardown},
		{null_setup, xnb_add_mbuf_cksum_tcp, null_teardown},
		{null_setup, xnb_add_mbuf_cksum_tcp_swcksum, null_teardown},
		{null_setup, xnb_add_mbuf_cksum_udp, null_teardown},
#endif
		{null_setup, xnb_sscanf_hhd, null_teardown},
		{null_setup, xnb_sscanf_hhu, null_teardown},
		{null_setup, xnb_sscanf_lld, null_teardown},
		{null_setup, xnb_sscanf_llu, null_teardown},
		{null_setup, xnb_sscanf_hhn, null_teardown},
	};
	/**
	 * results is static so that the data will persist after this function
	 * returns.  The sysctl code expects us to return a constant string.
	 * \todo: the static variable is not thread safe.  Put a mutex around
	 * it.
	 */
	static char results[TOTAL_BUFLEN];

	/* empty the result strings */
	results[0] = 0;
	xnb_unit_test_runner(tests, TOTAL_TESTS, results, TOTAL_BUFLEN);

	return (SYSCTL_OUT(req, results, strnlen(results, TOTAL_BUFLEN)));
}

static int
setup_pvt_data(void)
{
	int error = 0;

	bzero(xnb_unit_pvt.gnttab, sizeof(xnb_unit_pvt.gnttab));

	xnb_unit_pvt.txs = malloc(PAGE_SIZE, M_XENNETBACK, M_WAITOK|M_ZERO);
	if (xnb_unit_pvt.txs != NULL) {
		SHARED_RING_INIT(xnb_unit_pvt.txs);
		BACK_RING_INIT(&xnb_unit_pvt.txb, xnb_unit_pvt.txs, PAGE_SIZE);
		FRONT_RING_INIT(&xnb_unit_pvt.txf, xnb_unit_pvt.txs, PAGE_SIZE);
	} else {
		error = 1;
	}

	xnb_unit_pvt.ifp = if_alloc(IFT_ETHER);
	if (xnb_unit_pvt.ifp == NULL) {
		error = 1;
	}

	xnb_unit_pvt.rxs = malloc(PAGE_SIZE, M_XENNETBACK, M_WAITOK|M_ZERO);
	if (xnb_unit_pvt.rxs != NULL) {
		SHARED_RING_INIT(xnb_unit_pvt.rxs);
		BACK_RING_INIT(&xnb_unit_pvt.rxb, xnb_unit_pvt.rxs, PAGE_SIZE);
		FRONT_RING_INIT(&xnb_unit_pvt.rxf, xnb_unit_pvt.rxs, PAGE_SIZE);
	} else {
		error = 1;
	}

	return error;
}

static void
teardown_pvt_data(void)
{
	if (xnb_unit_pvt.txs != NULL) {
		free(xnb_unit_pvt.txs, M_XENNETBACK);
	}
	if (xnb_unit_pvt.rxs != NULL) {
		free(xnb_unit_pvt.rxs, M_XENNETBACK);
	}
	if (xnb_unit_pvt.ifp != NULL) {
		if_free(xnb_unit_pvt.ifp);
	}
}

/**
 * Verify that xnb_ring2pkt will not consume any requests from an empty ring
 */
static void
xnb_ring2pkt_emptyring(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int num_consumed;

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	XNB_ASSERT(num_consumed == 0);
}

/**
 * Verify that xnb_ring2pkt can convert a single request packet correctly
 */
static void
xnb_ring2pkt_1req(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int num_consumed;
	struct netif_tx_request *req;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);

	req->flags = 0;
	req->size = 69;	/* arbitrary number for test */
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	XNB_ASSERT(num_consumed == 1);
	XNB_ASSERT(pkt.size == 69);
	XNB_ASSERT(pkt.car_size == 69);
	XNB_ASSERT(pkt.flags == 0);
	XNB_ASSERT(xnb_pkt_is_valid(&pkt));
	XNB_ASSERT(pkt.list_len == 1);
	XNB_ASSERT(pkt.car == 0);
}

/**
 * Verify that xnb_ring2pkt can convert a two request packet correctly.
 * This tests handling of the MORE_DATA flag and cdr
 */
static void
xnb_ring2pkt_2req(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int num_consumed;
	struct netif_tx_request *req;
	RING_IDX start_idx = xnb_unit_pvt.txf.req_prod_pvt;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_more_data;
	req->size = 100;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = 40;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	XNB_ASSERT(num_consumed == 2);
	XNB_ASSERT(pkt.size == 100);
	XNB_ASSERT(pkt.car_size == 60);
	XNB_ASSERT(pkt.flags == 0);
	XNB_ASSERT(xnb_pkt_is_valid(&pkt));
	XNB_ASSERT(pkt.list_len == 2);
	XNB_ASSERT(pkt.car == start_idx);
	XNB_ASSERT(pkt.cdr == start_idx + 1);
}

/**
 * Verify that xnb_ring2pkt can convert a three request packet correctly
 */
static void
xnb_ring2pkt_3req(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int num_consumed;
	struct netif_tx_request *req;
	RING_IDX start_idx = xnb_unit_pvt.txf.req_prod_pvt;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_more_data;
	req->size = 200;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_more_data;
	req->size = 40;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = 50;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	XNB_ASSERT(num_consumed == 3);
	XNB_ASSERT(pkt.size == 200);
	XNB_ASSERT(pkt.car_size == 110);
	XNB_ASSERT(pkt.flags == 0);
	XNB_ASSERT(xnb_pkt_is_valid(&pkt));
	XNB_ASSERT(pkt.list_len == 3);
	XNB_ASSERT(pkt.car == start_idx);
	XNB_ASSERT(pkt.cdr == start_idx + 1);
	XNB_ASSERT(RING_GET_REQUEST(&xnb_unit_pvt.txb, pkt.cdr + 1) == req);
}

/**
 * Verify that xnb_ring2pkt can read extra inf
 */
static void
xnb_ring2pkt_extra(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int num_consumed;
	struct netif_tx_request *req;
	struct netif_extra_info *ext;
	RING_IDX start_idx = xnb_unit_pvt.txf.req_prod_pvt;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_extra_info | NETTXF_more_data;
	req->size = 150;
	xnb_unit_pvt.txf.req_prod_pvt++;

	ext = (struct netif_extra_info*) RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	ext->flags = 0;
	ext->type = XEN_NETIF_EXTRA_TYPE_GSO;
	ext->u.gso.size = 250;
	ext->u.gso.type = XEN_NETIF_GSO_TYPE_TCPV4;
	ext->u.gso.features = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = 50;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	XNB_ASSERT(num_consumed == 3);
	XNB_ASSERT(pkt.extra.flags == 0);
	XNB_ASSERT(pkt.extra.type == XEN_NETIF_EXTRA_TYPE_GSO);
	XNB_ASSERT(pkt.extra.u.gso.size == 250);
	XNB_ASSERT(pkt.extra.u.gso.type = XEN_NETIF_GSO_TYPE_TCPV4);
	XNB_ASSERT(pkt.size == 150);
	XNB_ASSERT(pkt.car_size == 100);
	XNB_ASSERT(pkt.flags == NETTXF_extra_info);
	XNB_ASSERT(xnb_pkt_is_valid(&pkt));
	XNB_ASSERT(pkt.list_len == 2);
	XNB_ASSERT(pkt.car == start_idx);
	XNB_ASSERT(pkt.cdr == start_idx + 2);
	XNB_ASSERT(RING_GET_REQUEST(&xnb_unit_pvt.txb, pkt.cdr) == req);
}

/**
 * Verify that xnb_ring2pkt will consume no requests if the entire packet is
 * not yet in the ring
 */
static void
xnb_ring2pkt_partial(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int num_consumed;
	struct netif_tx_request *req;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_more_data;
	req->size = 150;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	XNB_ASSERT(num_consumed == 0);
	XNB_ASSERT(! xnb_pkt_is_valid(&pkt));
}

/**
 * Verity that xnb_ring2pkt can read a packet whose requests wrap around
 * the end of the ring
 */
static void
xnb_ring2pkt_wraps(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int num_consumed;
	struct netif_tx_request *req;
	unsigned int rsize;

	/*
	 * Manually tweak the ring indices to create a ring with no responses
	 * and the next request slot at position 2 from the end
	 */
	rsize = RING_SIZE(&xnb_unit_pvt.txf);
	xnb_unit_pvt.txf.req_prod_pvt = rsize - 2;
	xnb_unit_pvt.txf.rsp_cons = rsize - 2;
	xnb_unit_pvt.txs->req_prod = rsize - 2;
	xnb_unit_pvt.txs->req_event = rsize - 1;
	xnb_unit_pvt.txs->rsp_prod = rsize - 2;
	xnb_unit_pvt.txs->rsp_event = rsize - 1;
	xnb_unit_pvt.txb.rsp_prod_pvt = rsize - 2;
	xnb_unit_pvt.txb.req_cons = rsize - 2;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_more_data;
	req->size = 550;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_more_data;
	req->size = 100;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = 50;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	XNB_ASSERT(num_consumed == 3);
	XNB_ASSERT(xnb_pkt_is_valid(&pkt));
	XNB_ASSERT(pkt.list_len == 3);
	XNB_ASSERT(RING_GET_REQUEST(&xnb_unit_pvt.txb, pkt.cdr + 1) == req);
}


/**
 * xnb_txpkt2rsp should do nothing for an empty packet
 */
static void
xnb_txpkt2rsp_emptypkt(char *buffer, size_t buflen)
{
	int num_consumed;
	struct xnb_pkt pkt;
	netif_tx_back_ring_t txb_backup = xnb_unit_pvt.txb;
	netif_tx_sring_t txs_backup = *xnb_unit_pvt.txs;
	pkt.list_len = 0;

	/* must call xnb_ring2pkt just to intialize pkt */
	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	xnb_txpkt2rsp(&pkt, &xnb_unit_pvt.txb, 0);
	XNB_ASSERT(
	    memcmp(&txb_backup, &xnb_unit_pvt.txb, sizeof(txb_backup)) == 0);
	XNB_ASSERT(
	    memcmp(&txs_backup, xnb_unit_pvt.txs, sizeof(txs_backup)) == 0);
}

/**
 * xnb_txpkt2rsp responding to one request
 */
static void
xnb_txpkt2rsp_1req(char *buffer, size_t buflen)
{
	uint16_t num_consumed;
	struct xnb_pkt pkt;
	struct netif_tx_request *req;
	struct netif_tx_response *rsp;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->size = 1000;
	req->flags = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	xnb_unit_pvt.txb.req_cons += num_consumed;

	xnb_txpkt2rsp(&pkt, &xnb_unit_pvt.txb, 0);
	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb, xnb_unit_pvt.txf.rsp_cons);

	XNB_ASSERT(
	    xnb_unit_pvt.txb.rsp_prod_pvt == xnb_unit_pvt.txs->req_prod);
	XNB_ASSERT(rsp->id == req->id);
	XNB_ASSERT(rsp->status == NETIF_RSP_OKAY);
};

/**
 * xnb_txpkt2rsp responding to 1 data request and 1 extra info
 */
static void
xnb_txpkt2rsp_extra(char *buffer, size_t buflen)
{
	uint16_t num_consumed;
	struct xnb_pkt pkt;
	struct netif_tx_request *req;
	netif_extra_info_t *ext;
	struct netif_tx_response *rsp;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->size = 1000;
	req->flags = NETTXF_extra_info;
	req->id = 69;
	xnb_unit_pvt.txf.req_prod_pvt++;

	ext = (netif_extra_info_t*) RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	ext->type = XEN_NETIF_EXTRA_TYPE_GSO;
	ext->flags = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	xnb_unit_pvt.txb.req_cons += num_consumed;

	xnb_txpkt2rsp(&pkt, &xnb_unit_pvt.txb, 0);

	XNB_ASSERT(
	    xnb_unit_pvt.txb.rsp_prod_pvt == xnb_unit_pvt.txs->req_prod);

	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb, xnb_unit_pvt.txf.rsp_cons);
	XNB_ASSERT(rsp->id == req->id);
	XNB_ASSERT(rsp->status == NETIF_RSP_OKAY);

	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb,
	    xnb_unit_pvt.txf.rsp_cons + 1);
	XNB_ASSERT(rsp->status == NETIF_RSP_NULL);
};

/**
 * xnb_pkg2rsp responding to 3 data requests and 1 extra info
 */
static void
xnb_txpkt2rsp_long(char *buffer, size_t buflen)
{
	uint16_t num_consumed;
	struct xnb_pkt pkt;
	struct netif_tx_request *req;
	netif_extra_info_t *ext;
	struct netif_tx_response *rsp;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->size = 1000;
	req->flags = NETTXF_extra_info | NETTXF_more_data;
	req->id = 254;
	xnb_unit_pvt.txf.req_prod_pvt++;

	ext = (netif_extra_info_t*) RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	ext->type = XEN_NETIF_EXTRA_TYPE_GSO;
	ext->flags = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->size = 300;
	req->flags = NETTXF_more_data;
	req->id = 1034;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->size = 400;
	req->flags = 0;
	req->id = 34;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	xnb_unit_pvt.txb.req_cons += num_consumed;

	xnb_txpkt2rsp(&pkt, &xnb_unit_pvt.txb, 0);

	XNB_ASSERT(
	    xnb_unit_pvt.txb.rsp_prod_pvt == xnb_unit_pvt.txs->req_prod);

	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb, xnb_unit_pvt.txf.rsp_cons);
	XNB_ASSERT(rsp->id ==
	    RING_GET_REQUEST(&xnb_unit_pvt.txf, 0)->id);
	XNB_ASSERT(rsp->status == NETIF_RSP_OKAY);

	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb,
	    xnb_unit_pvt.txf.rsp_cons + 1);
	XNB_ASSERT(rsp->status == NETIF_RSP_NULL);

	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb,
	    xnb_unit_pvt.txf.rsp_cons + 2);
	XNB_ASSERT(rsp->id ==
	    RING_GET_REQUEST(&xnb_unit_pvt.txf, 2)->id);
	XNB_ASSERT(rsp->status == NETIF_RSP_OKAY);

	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb,
	    xnb_unit_pvt.txf.rsp_cons + 3);
	XNB_ASSERT(rsp->id ==
	    RING_GET_REQUEST(&xnb_unit_pvt.txf, 3)->id);
	XNB_ASSERT(rsp->status == NETIF_RSP_OKAY);
}

/**
 * xnb_txpkt2rsp responding to an invalid packet.
 * Note: this test will result in an error message being printed to the console
 * such as:
 * xnb(xnb_ring2pkt:1306): Unknown extra info type 255.  Discarding packet
 */
static void
xnb_txpkt2rsp_invalid(char *buffer, size_t buflen)
{
	uint16_t num_consumed;
	struct xnb_pkt pkt;
	struct netif_tx_request *req;
	netif_extra_info_t *ext;
	struct netif_tx_response *rsp;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->size = 1000;
	req->flags = NETTXF_extra_info;
	req->id = 69;
	xnb_unit_pvt.txf.req_prod_pvt++;

	ext = (netif_extra_info_t*) RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	ext->type = 0xFF;	/* Invalid extra type */
	ext->flags = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	xnb_unit_pvt.txb.req_cons += num_consumed;
	XNB_ASSERT(! xnb_pkt_is_valid(&pkt));

	xnb_txpkt2rsp(&pkt, &xnb_unit_pvt.txb, 0);

	XNB_ASSERT(
	    xnb_unit_pvt.txb.rsp_prod_pvt == xnb_unit_pvt.txs->req_prod);

	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb, xnb_unit_pvt.txf.rsp_cons);
	XNB_ASSERT(rsp->id == req->id);
	XNB_ASSERT(rsp->status == NETIF_RSP_ERROR);

	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb,
	    xnb_unit_pvt.txf.rsp_cons + 1);
	XNB_ASSERT(rsp->status == NETIF_RSP_NULL);
};

/**
 * xnb_txpkt2rsp responding to one request which caused an error
 */
static void
xnb_txpkt2rsp_error(char *buffer, size_t buflen)
{
	uint16_t num_consumed;
	struct xnb_pkt pkt;
	struct netif_tx_request *req;
	struct netif_tx_response *rsp;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->size = 1000;
	req->flags = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	xnb_unit_pvt.txb.req_cons += num_consumed;

	xnb_txpkt2rsp(&pkt, &xnb_unit_pvt.txb, 1);
	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb, xnb_unit_pvt.txf.rsp_cons);

	XNB_ASSERT(
	    xnb_unit_pvt.txb.rsp_prod_pvt == xnb_unit_pvt.txs->req_prod);
	XNB_ASSERT(rsp->id == req->id);
	XNB_ASSERT(rsp->status == NETIF_RSP_ERROR);
};

/**
 * xnb_txpkt2rsp's responses wrap around the end of the ring
 */
static void
xnb_txpkt2rsp_wraps(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int num_consumed;
	struct netif_tx_request *req;
	struct netif_tx_response *rsp;
	unsigned int rsize;

	/*
	 * Manually tweak the ring indices to create a ring with no responses
	 * and the next request slot at position 2 from the end
	 */
	rsize = RING_SIZE(&xnb_unit_pvt.txf);
	xnb_unit_pvt.txf.req_prod_pvt = rsize - 2;
	xnb_unit_pvt.txf.rsp_cons = rsize - 2;
	xnb_unit_pvt.txs->req_prod = rsize - 2;
	xnb_unit_pvt.txs->req_event = rsize - 1;
	xnb_unit_pvt.txs->rsp_prod = rsize - 2;
	xnb_unit_pvt.txs->rsp_event = rsize - 1;
	xnb_unit_pvt.txb.rsp_prod_pvt = rsize - 2;
	xnb_unit_pvt.txb.req_cons = rsize - 2;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_more_data;
	req->size = 550;
	req->id = 1;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_more_data;
	req->size = 100;
	req->id = 2;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = 50;
	req->id = 3;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);

	xnb_txpkt2rsp(&pkt, &xnb_unit_pvt.txb, 0);

	XNB_ASSERT(
	    xnb_unit_pvt.txb.rsp_prod_pvt == xnb_unit_pvt.txs->req_prod);
	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.txb,
	    xnb_unit_pvt.txf.rsp_cons + 2);
	XNB_ASSERT(rsp->id == req->id);
	XNB_ASSERT(rsp->status == NETIF_RSP_OKAY);
}


/**
 * Helper function used to setup pkt2mbufc tests
 * \param size     size in bytes of the single request to push to the ring
 * \param flags		optional flags to put in the netif request
 * \param[out] pkt the returned packet object
 * \return number of requests consumed from the ring
 */
static int
xnb_get1pkt(struct xnb_pkt *pkt, size_t size, uint16_t flags)
{
	struct netif_tx_request *req;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = flags;
	req->size = size;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	return xnb_ring2pkt(pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
}

/**
 * xnb_pkt2mbufc on an empty packet
 */
static void
xnb_pkt2mbufc_empty(char *buffer, size_t buflen)
{
	int num_consumed;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;
	pkt.list_len = 0;

	/* must call xnb_ring2pkt just to intialize pkt */
	num_consumed = xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb,
	                            xnb_unit_pvt.txb.req_cons);
	pkt.size = 0;
	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	safe_m_freem(&pMbuf);
}

/**
 * xnb_pkt2mbufc on short packet that can fit in an mbuf internal buffer
 */
static void
xnb_pkt2mbufc_short(char *buffer, size_t buflen)
{
	const size_t size = MINCLSIZE - 1;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;

	xnb_get1pkt(&pkt, size, 0);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	XNB_ASSERT(M_TRAILINGSPACE(pMbuf) >= size);
	safe_m_freem(&pMbuf);
}

/**
 * xnb_pkt2mbufc on short packet whose checksum was validated by the netfron
 */
static void
xnb_pkt2mbufc_csum(char *buffer, size_t buflen)
{
	const size_t size = MINCLSIZE - 1;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;

	xnb_get1pkt(&pkt, size, NETTXF_data_validated);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	XNB_ASSERT(M_TRAILINGSPACE(pMbuf) >= size);
	XNB_ASSERT(pMbuf->m_pkthdr.csum_flags & CSUM_IP_CHECKED);
	XNB_ASSERT(pMbuf->m_pkthdr.csum_flags & CSUM_IP_VALID);
	XNB_ASSERT(pMbuf->m_pkthdr.csum_flags & CSUM_DATA_VALID);
	XNB_ASSERT(pMbuf->m_pkthdr.csum_flags & CSUM_PSEUDO_HDR);
	safe_m_freem(&pMbuf);
}

/**
 * xnb_pkt2mbufc on packet that can fit in one cluster
 */
static void
xnb_pkt2mbufc_1cluster(char *buffer, size_t buflen)
{
	const size_t size = MINCLSIZE;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;

	xnb_get1pkt(&pkt, size, 0);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	XNB_ASSERT(M_TRAILINGSPACE(pMbuf) >= size);
	safe_m_freem(&pMbuf);
}

/**
 * xnb_pkt2mbufc on packet that cannot fit in one regular cluster
 */
static void
xnb_pkt2mbufc_largecluster(char *buffer, size_t buflen)
{
	const size_t size = MCLBYTES + 1;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;

	xnb_get1pkt(&pkt, size, 0);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	XNB_ASSERT(M_TRAILINGSPACE(pMbuf) >= size);
	safe_m_freem(&pMbuf);
}

/**
 * xnb_pkt2mbufc on packet that cannot fit in one clusters
 */
static void
xnb_pkt2mbufc_2cluster(char *buffer, size_t buflen)
{
	const size_t size = 2 * MCLBYTES + 1;
	size_t space = 0;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;
	struct mbuf *m;

	xnb_get1pkt(&pkt, size, 0);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);

	for (m = pMbuf; m != NULL; m = m->m_next) {
		space += M_TRAILINGSPACE(m);
	}
	XNB_ASSERT(space >= size);
	safe_m_freem(&pMbuf);
}

/**
 * xnb_txpkt2gnttab on an empty packet.  Should return empty gnttab
 */
static void
xnb_txpkt2gnttab_empty(char *buffer, size_t buflen)
{
	int n_entries;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;
	pkt.list_len = 0;

	/* must call xnb_ring2pkt just to intialize pkt */
	xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb, xnb_unit_pvt.txb.req_cons);
	pkt.size = 0;
	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	n_entries = xnb_txpkt2gnttab(&pkt, pMbuf, xnb_unit_pvt.gnttab,
	    &xnb_unit_pvt.txb, DOMID_FIRST_RESERVED);
	XNB_ASSERT(n_entries == 0);
	safe_m_freem(&pMbuf);
}

/**
 * xnb_txpkt2gnttab on a short packet, that can fit in one mbuf internal buffer
 * and has one request
 */
static void
xnb_txpkt2gnttab_short(char *buffer, size_t buflen)
{
	const size_t size = MINCLSIZE - 1;
	int n_entries;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;

	struct netif_tx_request *req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = size;
	req->gref = 7;
	req->offset = 17;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb, xnb_unit_pvt.txb.req_cons);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	n_entries = xnb_txpkt2gnttab(&pkt, pMbuf, xnb_unit_pvt.gnttab,
	    &xnb_unit_pvt.txb, DOMID_FIRST_RESERVED);
	XNB_ASSERT(n_entries == 1);
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].len == size);
	/* flags should indicate gref's for source */
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].flags & GNTCOPY_source_gref);
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].source.offset == req->offset);
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].source.domid == DOMID_SELF);
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].dest.offset == virt_to_offset(
	      mtod(pMbuf, vm_offset_t)));
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].dest.u.gmfn ==
		virt_to_mfn(mtod(pMbuf, vm_offset_t)));
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].dest.domid == DOMID_FIRST_RESERVED);
	safe_m_freem(&pMbuf);
}

/**
 * xnb_txpkt2gnttab on a packet with two requests, that can fit into a single
 * mbuf cluster
 */
static void
xnb_txpkt2gnttab_2req(char *buffer, size_t buflen)
{
	int n_entries;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;

	struct netif_tx_request *req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_more_data;
	req->size = 1900;
	req->gref = 7;
	req->offset = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = 500;
	req->gref = 8;
	req->offset = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb, xnb_unit_pvt.txb.req_cons);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	n_entries = xnb_txpkt2gnttab(&pkt, pMbuf, xnb_unit_pvt.gnttab,
	    &xnb_unit_pvt.txb, DOMID_FIRST_RESERVED);

	XNB_ASSERT(n_entries == 2);
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].len == 1400);
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].dest.offset == virt_to_offset(
	      mtod(pMbuf, vm_offset_t)));

	XNB_ASSERT(xnb_unit_pvt.gnttab[1].len == 500);
	XNB_ASSERT(xnb_unit_pvt.gnttab[1].dest.offset == virt_to_offset(
	      mtod(pMbuf, vm_offset_t) + 1400));
	safe_m_freem(&pMbuf);
}

/**
 * xnb_txpkt2gnttab on a single request that spans two mbuf clusters
 */
static void
xnb_txpkt2gnttab_2cluster(char *buffer, size_t buflen)
{
	int n_entries;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;
	const uint16_t data_this_transaction = (MCLBYTES*2) + 1;

	struct netif_tx_request *req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = data_this_transaction;
	req->gref = 8;
	req->offset = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);
	xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb, xnb_unit_pvt.txb.req_cons);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	XNB_ASSERT(pMbuf != NULL);
	if (pMbuf == NULL)
		return;

	n_entries = xnb_txpkt2gnttab(&pkt, pMbuf, xnb_unit_pvt.gnttab,
	    &xnb_unit_pvt.txb, DOMID_FIRST_RESERVED);

	if (M_TRAILINGSPACE(pMbuf) == MCLBYTES) {
		/* there should be three mbufs and three gnttab entries */
		XNB_ASSERT(n_entries == 3);
		XNB_ASSERT(xnb_unit_pvt.gnttab[0].len == MCLBYTES);
		XNB_ASSERT(
		    xnb_unit_pvt.gnttab[0].dest.offset == virt_to_offset(
		      mtod(pMbuf, vm_offset_t)));
		XNB_ASSERT(xnb_unit_pvt.gnttab[0].source.offset == 0);

		XNB_ASSERT(xnb_unit_pvt.gnttab[1].len == MCLBYTES);
		XNB_ASSERT(
		    xnb_unit_pvt.gnttab[1].dest.offset == virt_to_offset(
		      mtod(pMbuf->m_next, vm_offset_t)));
		XNB_ASSERT(xnb_unit_pvt.gnttab[1].source.offset == MCLBYTES);

		XNB_ASSERT(xnb_unit_pvt.gnttab[2].len == 1);
		XNB_ASSERT(
		    xnb_unit_pvt.gnttab[2].dest.offset == virt_to_offset(
		      mtod(pMbuf->m_next, vm_offset_t)));
		XNB_ASSERT(xnb_unit_pvt.gnttab[2].source.offset == 2 *
			    MCLBYTES);
	} else if (M_TRAILINGSPACE(pMbuf) == 2 * MCLBYTES) {
		/* there should be two mbufs and two gnttab entries */
		XNB_ASSERT(n_entries == 2);
		XNB_ASSERT(xnb_unit_pvt.gnttab[0].len == 2 * MCLBYTES);
		XNB_ASSERT(
		    xnb_unit_pvt.gnttab[0].dest.offset == virt_to_offset(
		      mtod(pMbuf, vm_offset_t)));
		XNB_ASSERT(xnb_unit_pvt.gnttab[0].source.offset == 0);

		XNB_ASSERT(xnb_unit_pvt.gnttab[1].len == 1);
		XNB_ASSERT(
		    xnb_unit_pvt.gnttab[1].dest.offset == virt_to_offset(
		      mtod(pMbuf->m_next, vm_offset_t)));
		XNB_ASSERT(
		    xnb_unit_pvt.gnttab[1].source.offset == 2 * MCLBYTES);

	} else {
		/* should never get here */
		XNB_ASSERT(0);
	}
	m_freem(pMbuf);
}


/**
 * xnb_update_mbufc on a short packet that only has one gnttab entry
 */
static void
xnb_update_mbufc_short(char *buffer, size_t buflen)
{
	const size_t size = MINCLSIZE - 1;
	int n_entries;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;

	struct netif_tx_request *req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = size;
	req->gref = 7;
	req->offset = 17;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb, xnb_unit_pvt.txb.req_cons);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	n_entries = xnb_txpkt2gnttab(&pkt, pMbuf, xnb_unit_pvt.gnttab,
	    &xnb_unit_pvt.txb, DOMID_FIRST_RESERVED);

	/* Update grant table's status fields as the hypervisor call would */
	xnb_unit_pvt.gnttab[0].status = GNTST_okay;

	xnb_update_mbufc(pMbuf, xnb_unit_pvt.gnttab, n_entries);
	XNB_ASSERT(pMbuf->m_len == size);
	XNB_ASSERT(pMbuf->m_pkthdr.len == size);
	safe_m_freem(&pMbuf);
}

/**
 * xnb_update_mbufc on a packet with two requests, that can fit into a single
 * mbuf cluster
 */
static void
xnb_update_mbufc_2req(char *buffer, size_t buflen)
{
	int n_entries;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;

	struct netif_tx_request *req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = NETTXF_more_data;
	req->size = 1900;
	req->gref = 7;
	req->offset = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = 500;
	req->gref = 8;
	req->offset = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);

	xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb, xnb_unit_pvt.txb.req_cons);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	n_entries = xnb_txpkt2gnttab(&pkt, pMbuf, xnb_unit_pvt.gnttab,
	    &xnb_unit_pvt.txb, DOMID_FIRST_RESERVED);

	/* Update grant table's status fields as the hypervisor call would */
	xnb_unit_pvt.gnttab[0].status = GNTST_okay;
	xnb_unit_pvt.gnttab[1].status = GNTST_okay;

	xnb_update_mbufc(pMbuf, xnb_unit_pvt.gnttab, n_entries);
	XNB_ASSERT(n_entries == 2);
	XNB_ASSERT(pMbuf->m_pkthdr.len == 1900);
	XNB_ASSERT(pMbuf->m_len == 1900);

	safe_m_freem(&pMbuf);
}

/**
 * xnb_update_mbufc on a single request that spans two mbuf clusters
 */
static void
xnb_update_mbufc_2cluster(char *buffer, size_t buflen)
{
	int i;
	int n_entries;
	struct xnb_pkt pkt;
	struct mbuf *pMbuf;
	const uint16_t data_this_transaction = (MCLBYTES*2) + 1;

	struct netif_tx_request *req = RING_GET_REQUEST(&xnb_unit_pvt.txf,
	    xnb_unit_pvt.txf.req_prod_pvt);
	req->flags = 0;
	req->size = data_this_transaction;
	req->gref = 8;
	req->offset = 0;
	xnb_unit_pvt.txf.req_prod_pvt++;

	RING_PUSH_REQUESTS(&xnb_unit_pvt.txf);
	xnb_ring2pkt(&pkt, &xnb_unit_pvt.txb, xnb_unit_pvt.txb.req_cons);

	pMbuf = xnb_pkt2mbufc(&pkt, xnb_unit_pvt.ifp);
	n_entries = xnb_txpkt2gnttab(&pkt, pMbuf, xnb_unit_pvt.gnttab,
	    &xnb_unit_pvt.txb, DOMID_FIRST_RESERVED);

	/* Update grant table's status fields */
	for (i = 0; i < n_entries; i++) {
		xnb_unit_pvt.gnttab[0].status = GNTST_okay;
	}
	xnb_update_mbufc(pMbuf, xnb_unit_pvt.gnttab, n_entries);

	if (n_entries == 3) {
		/* there should be three mbufs and three gnttab entries */
		XNB_ASSERT(pMbuf->m_pkthdr.len == data_this_transaction);
		XNB_ASSERT(pMbuf->m_len == MCLBYTES);
		XNB_ASSERT(pMbuf->m_next->m_len == MCLBYTES);
		XNB_ASSERT(pMbuf->m_next->m_next->m_len == 1);
	} else if (n_entries == 2) {
		/* there should be two mbufs and two gnttab entries */
		XNB_ASSERT(n_entries == 2);
		XNB_ASSERT(pMbuf->m_pkthdr.len == data_this_transaction);
		XNB_ASSERT(pMbuf->m_len == 2 * MCLBYTES);
		XNB_ASSERT(pMbuf->m_next->m_len == 1);
	} else {
		/* should never get here */
		XNB_ASSERT(0);
	}
	safe_m_freem(&pMbuf);
}

/** xnb_mbufc2pkt on an empty mbufc */
static void
xnb_mbufc2pkt_empty(char *buffer, size_t buflen) {
	struct xnb_pkt pkt;
	int free_slots = 64;
	struct mbuf *mbuf;

	mbuf = m_get(M_WAITOK, MT_DATA);
	/*
	 * note: it is illegal to set M_PKTHDR on a mbuf with no data.  Doing so
	 * will cause m_freem to segfault
	 */
	XNB_ASSERT(mbuf->m_len == 0);

	xnb_mbufc2pkt(mbuf, &pkt, 0, free_slots);
	XNB_ASSERT(! xnb_pkt_is_valid(&pkt));

	safe_m_freem(&mbuf);
}

/** xnb_mbufc2pkt on a short mbufc */
static void
xnb_mbufc2pkt_short(char *buffer, size_t buflen) {
	struct xnb_pkt pkt;
	size_t size = 128;
	int free_slots = 64;
	RING_IDX start = 9;
	struct mbuf *mbuf;

	mbuf = m_getm(NULL, size, M_WAITOK, MT_DATA);
	mbuf->m_flags |= M_PKTHDR;
	mbuf->m_pkthdr.len = size;
	mbuf->m_len = size;

	xnb_mbufc2pkt(mbuf, &pkt, start, free_slots);
	XNB_ASSERT(xnb_pkt_is_valid(&pkt));
	XNB_ASSERT(pkt.size == size);
	XNB_ASSERT(pkt.car_size == size);
	XNB_ASSERT(! (pkt.flags &
	      (NETRXF_more_data | NETRXF_extra_info)));
	XNB_ASSERT(pkt.list_len == 1);
	XNB_ASSERT(pkt.car == start);

	safe_m_freem(&mbuf);
}

/** xnb_mbufc2pkt on a single mbuf with an mbuf cluster */
static void
xnb_mbufc2pkt_1cluster(char *buffer, size_t buflen) {
	struct xnb_pkt pkt;
	size_t size = MCLBYTES;
	int free_slots = 32;
	RING_IDX start = 12;
	struct mbuf *mbuf;

	mbuf = m_getm(NULL, size, M_WAITOK, MT_DATA);
	mbuf->m_flags |= M_PKTHDR;
	mbuf->m_pkthdr.len = size;
	mbuf->m_len = size;

	xnb_mbufc2pkt(mbuf, &pkt, start, free_slots);
	XNB_ASSERT(xnb_pkt_is_valid(&pkt));
	XNB_ASSERT(pkt.size == size);
	XNB_ASSERT(pkt.car_size == size);
	XNB_ASSERT(! (pkt.flags &
	      (NETRXF_more_data | NETRXF_extra_info)));
	XNB_ASSERT(pkt.list_len == 1);
	XNB_ASSERT(pkt.car == start);

	safe_m_freem(&mbuf);
}

/** xnb_mbufc2pkt on a two-mbuf chain with short data regions */
static void
xnb_mbufc2pkt_2short(char *buffer, size_t buflen) {
	struct xnb_pkt pkt;
	size_t size1 = MHLEN - 5;
	size_t size2 = MHLEN - 15;
	int free_slots = 32;
	RING_IDX start = 14;
	struct mbuf *mbufc, *mbufc2;

	mbufc = m_getm(NULL, size1, M_WAITOK, MT_DATA);
	XNB_ASSERT(mbufc != NULL);
	if (mbufc == NULL)
		return;
	mbufc->m_flags |= M_PKTHDR;

	mbufc2 = m_getm(mbufc, size2, M_WAITOK, MT_DATA);
	XNB_ASSERT(mbufc2 != NULL);
	if (mbufc2 == NULL) {
		safe_m_freem(&mbufc);
		return;
	}
	mbufc2->m_pkthdr.len = size1 + size2;
	mbufc2->m_len = size1;

	xnb_mbufc2pkt(mbufc2, &pkt, start, free_slots);
	XNB_ASSERT(xnb_pkt_is_valid(&pkt));
	XNB_ASSERT(pkt.size == size1 + size2);
	XNB_ASSERT(pkt.car == start);
	/*
	 * The second m_getm may allocate a new mbuf and append
	 * it to the chain, or it may simply extend the first mbuf.
	 */
	if (mbufc2->m_next != NULL) {
		XNB_ASSERT(pkt.car_size == size1);
		XNB_ASSERT(pkt.list_len == 1);
		XNB_ASSERT(pkt.cdr == start + 1);
	}

	safe_m_freem(&mbufc2);
}

/** xnb_mbufc2pkt on a mbuf chain with >1 mbuf cluster */
static void
xnb_mbufc2pkt_long(char *buffer, size_t buflen) {
	struct xnb_pkt pkt;
	size_t size = 14 * MCLBYTES / 3;
	size_t size_remaining;
	int free_slots = 15;
	RING_IDX start = 3;
	struct mbuf *mbufc, *m;

	mbufc = m_getm(NULL, size, M_WAITOK, MT_DATA);
	XNB_ASSERT(mbufc != NULL);
	if (mbufc == NULL)
		return;
	mbufc->m_flags |= M_PKTHDR;

	mbufc->m_pkthdr.len = size;
	size_remaining = size;
	for (m = mbufc; m != NULL; m = m->m_next) {
		m->m_len = MAX(M_TRAILINGSPACE(m), size_remaining);
		size_remaining -= m->m_len;
	}

	xnb_mbufc2pkt(mbufc, &pkt, start, free_slots);
	XNB_ASSERT(xnb_pkt_is_valid(&pkt));
	XNB_ASSERT(pkt.size == size);
	XNB_ASSERT(pkt.car == start);
	XNB_ASSERT(pkt.car_size = mbufc->m_len);
	/*
	 * There should be >1 response in the packet, and there is no
	 * extra info.
	 */
	XNB_ASSERT(! (pkt.flags & NETRXF_extra_info));
	XNB_ASSERT(pkt.cdr == pkt.car + 1);

	safe_m_freem(&mbufc);
}

/** xnb_mbufc2pkt on a mbuf chain with >1 mbuf cluster and extra info */
static void
xnb_mbufc2pkt_extra(char *buffer, size_t buflen) {
	struct xnb_pkt pkt;
	size_t size = 14 * MCLBYTES / 3;
	size_t size_remaining;
	int free_slots = 15;
	RING_IDX start = 3;
	struct mbuf *mbufc, *m;

	mbufc = m_getm(NULL, size, M_WAITOK, MT_DATA);
	XNB_ASSERT(mbufc != NULL);
	if (mbufc == NULL)
		return;

	mbufc->m_flags |= M_PKTHDR;
	mbufc->m_pkthdr.len = size;
	mbufc->m_pkthdr.csum_flags |= CSUM_TSO;
	mbufc->m_pkthdr.tso_segsz = TCP_MSS - 40;
	size_remaining = size;
	for (m = mbufc; m != NULL; m = m->m_next) {
		m->m_len = MAX(M_TRAILINGSPACE(m), size_remaining);
		size_remaining -= m->m_len;
	}

	xnb_mbufc2pkt(mbufc, &pkt, start, free_slots);
	XNB_ASSERT(xnb_pkt_is_valid(&pkt));
	XNB_ASSERT(pkt.size == size);
	XNB_ASSERT(pkt.car == start);
	XNB_ASSERT(pkt.car_size = mbufc->m_len);
	/* There should be >1 response in the packet, there is extra info */
	XNB_ASSERT(pkt.flags & NETRXF_extra_info);
	XNB_ASSERT(pkt.flags & NETRXF_data_validated);
	XNB_ASSERT(pkt.cdr == pkt.car + 2);
	XNB_ASSERT(pkt.extra.u.gso.size = mbufc->m_pkthdr.tso_segsz);
	XNB_ASSERT(pkt.extra.type == XEN_NETIF_EXTRA_TYPE_GSO);
	XNB_ASSERT(! (pkt.extra.flags & XEN_NETIF_EXTRA_FLAG_MORE));

	safe_m_freem(&mbufc);
}

/** xnb_mbufc2pkt with insufficient space in the ring */
static void
xnb_mbufc2pkt_nospace(char *buffer, size_t buflen) {
	struct xnb_pkt pkt;
	size_t size = 14 * MCLBYTES / 3;
	size_t size_remaining;
	int free_slots = 2;
	RING_IDX start = 3;
	struct mbuf *mbufc, *m;
	int error;

	mbufc = m_getm(NULL, size, M_WAITOK, MT_DATA);
	XNB_ASSERT(mbufc != NULL);
	if (mbufc == NULL)
		return;
	mbufc->m_flags |= M_PKTHDR;

	mbufc->m_pkthdr.len = size;
	size_remaining = size;
	for (m = mbufc; m != NULL; m = m->m_next) {
		m->m_len = MAX(M_TRAILINGSPACE(m), size_remaining);
		size_remaining -= m->m_len;
	}

	error = xnb_mbufc2pkt(mbufc, &pkt, start, free_slots);
	XNB_ASSERT(error == EAGAIN);
	XNB_ASSERT(! xnb_pkt_is_valid(&pkt));

	safe_m_freem(&mbufc);
}

/**
 * xnb_rxpkt2gnttab on an empty packet.  Should return empty gnttab
 */
static void
xnb_rxpkt2gnttab_empty(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int nr_entries;
	int free_slots = 60;
	struct mbuf *mbuf;

	mbuf = m_get(M_WAITOK, MT_DATA);

	xnb_mbufc2pkt(mbuf, &pkt, 0, free_slots);
	nr_entries = xnb_rxpkt2gnttab(&pkt, mbuf, xnb_unit_pvt.gnttab,
			&xnb_unit_pvt.rxb, DOMID_FIRST_RESERVED);

	XNB_ASSERT(nr_entries == 0);

	safe_m_freem(&mbuf);
}

/** xnb_rxpkt2gnttab on a short packet without extra data */
static void
xnb_rxpkt2gnttab_short(char *buffer, size_t buflen) {
	struct xnb_pkt pkt;
	int nr_entries;
	size_t size = 128;
	int free_slots = 60;
	RING_IDX start = 9;
	struct netif_rx_request *req;
	struct mbuf *mbuf;

	mbuf = m_getm(NULL, size, M_WAITOK, MT_DATA);
	mbuf->m_flags |= M_PKTHDR;
	mbuf->m_pkthdr.len = size;
	mbuf->m_len = size;

	xnb_mbufc2pkt(mbuf, &pkt, start, free_slots);
	req = RING_GET_REQUEST(&xnb_unit_pvt.rxf,
			       xnb_unit_pvt.txf.req_prod_pvt);
	req->gref = 7;

	nr_entries = xnb_rxpkt2gnttab(&pkt, mbuf, xnb_unit_pvt.gnttab,
				      &xnb_unit_pvt.rxb, DOMID_FIRST_RESERVED);

	XNB_ASSERT(nr_entries == 1);
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].len == size);
	/* flags should indicate gref's for dest */
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].flags & GNTCOPY_dest_gref);
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].dest.offset == 0);
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].source.domid == DOMID_SELF);
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].source.offset == virt_to_offset(
		   mtod(mbuf, vm_offset_t)));
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].source.u.gmfn ==
		   virt_to_mfn(mtod(mbuf, vm_offset_t)));
	XNB_ASSERT(xnb_unit_pvt.gnttab[0].dest.domid == DOMID_FIRST_RESERVED);

	safe_m_freem(&mbuf);
}

/**
 * xnb_rxpkt2gnttab on a packet with two different mbufs in a single chai
 */
static void
xnb_rxpkt2gnttab_2req(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int nr_entries;
	int i, num_mbufs;
	size_t total_granted_size = 0;
	size_t size = MJUMPAGESIZE + 1;
	int free_slots = 60;
	RING_IDX start = 11;
	struct netif_rx_request *req;
	struct mbuf *mbuf, *m;

	mbuf = m_getm(NULL, size, M_WAITOK, MT_DATA);
	mbuf->m_flags |= M_PKTHDR;
	mbuf->m_pkthdr.len = size;
	mbuf->m_len = size;

	xnb_mbufc2pkt(mbuf, &pkt, start, free_slots);

	for (i = 0, m=mbuf; m != NULL; i++, m = m->m_next) {
		req = RING_GET_REQUEST(&xnb_unit_pvt.rxf,
		    xnb_unit_pvt.txf.req_prod_pvt);
		req->gref = i;
		req->id = 5;
	}
	num_mbufs = i;

	nr_entries = xnb_rxpkt2gnttab(&pkt, mbuf, xnb_unit_pvt.gnttab,
			&xnb_unit_pvt.rxb, DOMID_FIRST_RESERVED);

	XNB_ASSERT(nr_entries >= num_mbufs);
	for (i = 0; i < nr_entries; i++) {
		int end_offset = xnb_unit_pvt.gnttab[i].len +
			xnb_unit_pvt.gnttab[i].dest.offset;
		XNB_ASSERT(end_offset <= PAGE_SIZE);
		total_granted_size += xnb_unit_pvt.gnttab[i].len;
	}
	XNB_ASSERT(total_granted_size == size);
}

/**
 * xnb_rxpkt2rsp on an empty packet.  Shouldn't make any response
 */
static void
xnb_rxpkt2rsp_empty(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int nr_entries;
	int nr_reqs;
	int free_slots = 60;
	netif_rx_back_ring_t rxb_backup = xnb_unit_pvt.rxb;
	netif_rx_sring_t rxs_backup = *xnb_unit_pvt.rxs;
	struct mbuf *mbuf;

	mbuf = m_get(M_WAITOK, MT_DATA);

	xnb_mbufc2pkt(mbuf, &pkt, 0, free_slots);
	nr_entries = xnb_rxpkt2gnttab(&pkt, mbuf, xnb_unit_pvt.gnttab,
			&xnb_unit_pvt.rxb, DOMID_FIRST_RESERVED);

	nr_reqs = xnb_rxpkt2rsp(&pkt, xnb_unit_pvt.gnttab, nr_entries,
	    &xnb_unit_pvt.rxb);
	XNB_ASSERT(nr_reqs == 0);
	XNB_ASSERT(
	    memcmp(&rxb_backup, &xnb_unit_pvt.rxb, sizeof(rxb_backup)) == 0);
	XNB_ASSERT(
	    memcmp(&rxs_backup, xnb_unit_pvt.rxs, sizeof(rxs_backup)) == 0);

	safe_m_freem(&mbuf);
}

/**
 * xnb_rxpkt2rsp on a short packet with no extras
 */
static void
xnb_rxpkt2rsp_short(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int nr_entries, nr_reqs;
	size_t size = 128;
	int free_slots = 60;
	RING_IDX start = 5;
	struct netif_rx_request *req;
	struct netif_rx_response *rsp;
	struct mbuf *mbuf;

	mbuf = m_getm(NULL, size, M_WAITOK, MT_DATA);
	mbuf->m_flags |= M_PKTHDR;
	mbuf->m_pkthdr.len = size;
	mbuf->m_len = size;

	xnb_mbufc2pkt(mbuf, &pkt, start, free_slots);
	req = RING_GET_REQUEST(&xnb_unit_pvt.rxf, start);
	req->gref = 7;
	xnb_unit_pvt.rxb.req_cons = start;
	xnb_unit_pvt.rxb.rsp_prod_pvt = start;
	xnb_unit_pvt.rxs->req_prod = start + 1;
	xnb_unit_pvt.rxs->rsp_prod = start;

	nr_entries = xnb_rxpkt2gnttab(&pkt, mbuf, xnb_unit_pvt.gnttab,
			&xnb_unit_pvt.rxb, DOMID_FIRST_RESERVED);

	nr_reqs = xnb_rxpkt2rsp(&pkt, xnb_unit_pvt.gnttab, nr_entries,
	    &xnb_unit_pvt.rxb);

	XNB_ASSERT(nr_reqs == 1);
	XNB_ASSERT(xnb_unit_pvt.rxb.rsp_prod_pvt == start + 1);
	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.rxb, start);
	XNB_ASSERT(rsp->id == req->id);
	XNB_ASSERT(rsp->offset == 0);
	XNB_ASSERT((rsp->flags & (NETRXF_more_data | NETRXF_extra_info)) == 0);
	XNB_ASSERT(rsp->status == size);

	safe_m_freem(&mbuf);
}

/**
 * xnb_rxpkt2rsp with extra data
 */
static void
xnb_rxpkt2rsp_extra(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int nr_entries, nr_reqs;
	size_t size = 14;
	int free_slots = 15;
	RING_IDX start = 3;
	uint16_t id = 49;
	uint16_t gref = 65;
	uint16_t mss = TCP_MSS - 40;
	struct mbuf *mbufc;
	struct netif_rx_request *req;
	struct netif_rx_response *rsp;
	struct netif_extra_info *ext;

	mbufc = m_getm(NULL, size, M_WAITOK, MT_DATA);
	XNB_ASSERT(mbufc != NULL);
	if (mbufc == NULL)
		return;

	mbufc->m_flags |= M_PKTHDR;
	mbufc->m_pkthdr.len = size;
	mbufc->m_pkthdr.csum_flags |= CSUM_TSO;
	mbufc->m_pkthdr.tso_segsz = mss;
	mbufc->m_len = size;

	xnb_mbufc2pkt(mbufc, &pkt, start, free_slots);
	req = RING_GET_REQUEST(&xnb_unit_pvt.rxf, start);
	req->id = id;
	req->gref = gref;
	req = RING_GET_REQUEST(&xnb_unit_pvt.rxf, start + 1);
	req->id = id + 1;
	req->gref = gref + 1;
	xnb_unit_pvt.rxb.req_cons = start;
	xnb_unit_pvt.rxb.rsp_prod_pvt = start;
	xnb_unit_pvt.rxs->req_prod = start + 2;
	xnb_unit_pvt.rxs->rsp_prod = start;

	nr_entries = xnb_rxpkt2gnttab(&pkt, mbufc, xnb_unit_pvt.gnttab,
			&xnb_unit_pvt.rxb, DOMID_FIRST_RESERVED);

	nr_reqs = xnb_rxpkt2rsp(&pkt, xnb_unit_pvt.gnttab, nr_entries,
	    &xnb_unit_pvt.rxb);

	XNB_ASSERT(nr_reqs == 2);
	XNB_ASSERT(xnb_unit_pvt.rxb.rsp_prod_pvt == start + 2);
	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.rxb, start);
	XNB_ASSERT(rsp->id == id);
	XNB_ASSERT((rsp->flags & NETRXF_more_data) == 0);
	XNB_ASSERT((rsp->flags & NETRXF_extra_info));
	XNB_ASSERT((rsp->flags & NETRXF_data_validated));
	XNB_ASSERT((rsp->flags & NETRXF_csum_blank));
	XNB_ASSERT(rsp->status == size);

	ext = (struct netif_extra_info*)
		RING_GET_RESPONSE(&xnb_unit_pvt.rxb, start + 1);
	XNB_ASSERT(ext->type == XEN_NETIF_EXTRA_TYPE_GSO);
	XNB_ASSERT(! (ext->flags & XEN_NETIF_EXTRA_FLAG_MORE));
	XNB_ASSERT(ext->u.gso.size == mss);
	XNB_ASSERT(ext->u.gso.type == XEN_NETIF_EXTRA_TYPE_GSO);

	safe_m_freem(&mbufc);
}

/**
 * xnb_rxpkt2rsp on a packet with more than a pages's worth of data.  It should
 * generate two response slot
 */
static void
xnb_rxpkt2rsp_2slots(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int nr_entries, nr_reqs;
	size_t size = PAGE_SIZE + 100;
	int free_slots = 3;
	uint16_t id1 = 17;
	uint16_t id2 = 37;
	uint16_t gref1 = 24;
	uint16_t gref2 = 34;
	RING_IDX start = 15;
	struct netif_rx_request *req;
	struct netif_rx_response *rsp;
	struct mbuf *mbuf;

	mbuf = m_getm(NULL, size, M_WAITOK, MT_DATA);
	mbuf->m_flags |= M_PKTHDR;
	mbuf->m_pkthdr.len = size;
	if (mbuf->m_next != NULL) {
		size_t first_len = MIN(M_TRAILINGSPACE(mbuf), size);
		mbuf->m_len = first_len;
		mbuf->m_next->m_len = size - first_len;

	} else {
		mbuf->m_len = size;
	}

	xnb_mbufc2pkt(mbuf, &pkt, start, free_slots);
	req = RING_GET_REQUEST(&xnb_unit_pvt.rxf, start);
	req->gref = gref1;
	req->id = id1;
	req = RING_GET_REQUEST(&xnb_unit_pvt.rxf, start + 1);
	req->gref = gref2;
	req->id = id2;
	xnb_unit_pvt.rxb.req_cons = start;
	xnb_unit_pvt.rxb.rsp_prod_pvt = start;
	xnb_unit_pvt.rxs->req_prod = start + 2;
	xnb_unit_pvt.rxs->rsp_prod = start;

	nr_entries = xnb_rxpkt2gnttab(&pkt, mbuf, xnb_unit_pvt.gnttab,
			&xnb_unit_pvt.rxb, DOMID_FIRST_RESERVED);

	nr_reqs = xnb_rxpkt2rsp(&pkt, xnb_unit_pvt.gnttab, nr_entries,
	    &xnb_unit_pvt.rxb);

	XNB_ASSERT(nr_reqs == 2);
	XNB_ASSERT(xnb_unit_pvt.rxb.rsp_prod_pvt == start + 2);
	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.rxb, start);
	XNB_ASSERT(rsp->id == id1);
	XNB_ASSERT(rsp->offset == 0);
	XNB_ASSERT((rsp->flags & NETRXF_extra_info) == 0);
	XNB_ASSERT(rsp->flags & NETRXF_more_data);
	XNB_ASSERT(rsp->status == PAGE_SIZE);

	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.rxb, start + 1);
	XNB_ASSERT(rsp->id == id2);
	XNB_ASSERT(rsp->offset == 0);
	XNB_ASSERT((rsp->flags & NETRXF_extra_info) == 0);
	XNB_ASSERT(! (rsp->flags & NETRXF_more_data));
	XNB_ASSERT(rsp->status == size - PAGE_SIZE);

	safe_m_freem(&mbuf);
}

/** xnb_rxpkt2rsp on a grant table with two sub-page entries */
static void
xnb_rxpkt2rsp_2short(char *buffer, size_t buflen) {
	struct xnb_pkt pkt;
	int nr_reqs, nr_entries;
	size_t size1 = MHLEN - 5;
	size_t size2 = MHLEN - 15;
	int free_slots = 32;
	RING_IDX start = 14;
	uint16_t id = 47;
	uint16_t gref = 54;
	struct netif_rx_request *req;
	struct netif_rx_response *rsp;
	struct mbuf *mbufc;

	mbufc = m_getm(NULL, size1, M_WAITOK, MT_DATA);
	XNB_ASSERT(mbufc != NULL);
	if (mbufc == NULL)
		return;
	mbufc->m_flags |= M_PKTHDR;

	m_getm(mbufc, size2, M_WAITOK, MT_DATA);
	XNB_ASSERT(mbufc->m_next != NULL);
	mbufc->m_pkthdr.len = size1 + size2;
	mbufc->m_len = size1;
	mbufc->m_next->m_len = size2;

	xnb_mbufc2pkt(mbufc, &pkt, start, free_slots);

	req = RING_GET_REQUEST(&xnb_unit_pvt.rxf, start);
	req->gref = gref;
	req->id = id;
	xnb_unit_pvt.rxb.req_cons = start;
	xnb_unit_pvt.rxb.rsp_prod_pvt = start;
	xnb_unit_pvt.rxs->req_prod = start + 1;
	xnb_unit_pvt.rxs->rsp_prod = start;

	nr_entries = xnb_rxpkt2gnttab(&pkt, mbufc, xnb_unit_pvt.gnttab,
			&xnb_unit_pvt.rxb, DOMID_FIRST_RESERVED);

	nr_reqs = xnb_rxpkt2rsp(&pkt, xnb_unit_pvt.gnttab, nr_entries,
	    &xnb_unit_pvt.rxb);

	XNB_ASSERT(nr_entries == 2);
	XNB_ASSERT(nr_reqs == 1);
	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.rxb, start);
	XNB_ASSERT(rsp->id == id);
	XNB_ASSERT(rsp->status == size1 + size2);
	XNB_ASSERT(rsp->offset == 0);
	XNB_ASSERT(! (rsp->flags & (NETRXF_more_data | NETRXF_extra_info)));

	safe_m_freem(&mbufc);
}

/**
 * xnb_rxpkt2rsp on a long packet with a hypervisor gnttab_copy error
 * Note: this test will result in an error message being printed to the console
 * such as:
 * xnb(xnb_rxpkt2rsp:1720): Got error -1 for hypervisor gnttab_copy status
 */
static void
xnb_rxpkt2rsp_copyerror(char *buffer, size_t buflen)
{
	struct xnb_pkt pkt;
	int nr_entries, nr_reqs;
	int id = 7;
	int gref = 42;
	uint16_t canary = 6859;
	size_t size = 7 * MCLBYTES;
	int free_slots = 9;
	RING_IDX start = 2;
	struct netif_rx_request *req;
	struct netif_rx_response *rsp;
	struct mbuf *mbuf;
	
	mbuf = m_getm(NULL, size, M_WAITOK, MT_DATA);
	mbuf->m_flags |= M_PKTHDR;
	mbuf->m_pkthdr.len = size;
	mbuf->m_len = size;

	xnb_mbufc2pkt(mbuf, &pkt, start, free_slots);
	req = RING_GET_REQUEST(&xnb_unit_pvt.rxf, start);
	req->gref = gref;
	req->id = id;
	xnb_unit_pvt.rxb.req_cons = start;
	xnb_unit_pvt.rxb.rsp_prod_pvt = start;
	xnb_unit_pvt.rxs->req_prod = start + 1;
	xnb_unit_pvt.rxs->rsp_prod = start;
	req = RING_GET_REQUEST(&xnb_unit_pvt.rxf, start + 1);
	req->gref = canary;
	req->id = canary;

	nr_entries = xnb_rxpkt2gnttab(&pkt, mbuf, xnb_unit_pvt.gnttab,
			&xnb_unit_pvt.rxb, DOMID_FIRST_RESERVED);
	/* Inject the error*/
	xnb_unit_pvt.gnttab[2].status = GNTST_general_error;

	nr_reqs = xnb_rxpkt2rsp(&pkt, xnb_unit_pvt.gnttab, nr_entries,
	    &xnb_unit_pvt.rxb);

	XNB_ASSERT(nr_reqs == 1);
	XNB_ASSERT(xnb_unit_pvt.rxb.rsp_prod_pvt == start + 1);
	rsp = RING_GET_RESPONSE(&xnb_unit_pvt.rxb, start);
	XNB_ASSERT(rsp->id == id);
	XNB_ASSERT(rsp->status == NETIF_RSP_ERROR);
	req = RING_GET_REQUEST(&xnb_unit_pvt.rxf, start + 1);
	XNB_ASSERT(req->gref == canary);
	XNB_ASSERT(req->id == canary);

	safe_m_freem(&mbuf);
}

#if defined(INET) || defined(INET6)
/**
 * xnb_add_mbuf_cksum on an ARP request packet
 */
static void
xnb_add_mbuf_cksum_arp(char *buffer, size_t buflen)
{
	const size_t pkt_len = sizeof(struct ether_header) +
		sizeof(struct ether_arp);
	struct mbuf *mbufc;
	struct ether_header *eh;
	struct ether_arp *ep;
	unsigned char pkt_orig[pkt_len];

	mbufc = m_getm(NULL, pkt_len, M_WAITOK, MT_DATA);
	/* Fill in an example arp request */
	eh = mtod(mbufc, struct ether_header*);
	eh->ether_dhost[0] = 0xff;
	eh->ether_dhost[1] = 0xff;
	eh->ether_dhost[2] = 0xff;
	eh->ether_dhost[3] = 0xff;
	eh->ether_dhost[4] = 0xff;
	eh->ether_dhost[5] = 0xff;
	eh->ether_shost[0] = 0x00;
	eh->ether_shost[1] = 0x15;
	eh->ether_shost[2] = 0x17;
	eh->ether_shost[3] = 0xe9;
	eh->ether_shost[4] = 0x30;
	eh->ether_shost[5] = 0x68;
	eh->ether_type = htons(ETHERTYPE_ARP);
	ep = (struct ether_arp*)(eh + 1);
	ep->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
	ep->ea_hdr.ar_pro = htons(ETHERTYPE_IP);
	ep->ea_hdr.ar_hln = 6;
	ep->ea_hdr.ar_pln = 4;
	ep->ea_hdr.ar_op = htons(ARPOP_REQUEST);
	ep->arp_sha[0] = 0x00;
	ep->arp_sha[1] = 0x15;
	ep->arp_sha[2] = 0x17;
	ep->arp_sha[3] = 0xe9;
	ep->arp_sha[4] = 0x30;
	ep->arp_sha[5] = 0x68;
	ep->arp_spa[0] = 0xc0;
	ep->arp_spa[1] = 0xa8;
	ep->arp_spa[2] = 0x0a;
	ep->arp_spa[3] = 0x04;
	bzero(&(ep->arp_tha), ETHER_ADDR_LEN);
	ep->arp_tpa[0] = 0xc0;
	ep->arp_tpa[1] = 0xa8;
	ep->arp_tpa[2] = 0x0a;
	ep->arp_tpa[3] = 0x06;

	/* fill in the length field */
	mbufc->m_len = pkt_len;
	mbufc->m_pkthdr.len = pkt_len;
	/* indicate that the netfront uses hw-assisted checksums */
	mbufc->m_pkthdr.csum_flags = CSUM_IP_CHECKED | CSUM_IP_VALID   |
				CSUM_DATA_VALID | CSUM_PSEUDO_HDR;

	/* Make a backup copy of the packet */
	bcopy(mtod(mbufc, const void*), pkt_orig, pkt_len);

	/* Function under test */
	xnb_add_mbuf_cksum(mbufc);

	/* Verify that the packet's data did not change */
	XNB_ASSERT(bcmp(mtod(mbufc, const void*), pkt_orig, pkt_len) == 0);
	m_freem(mbufc);
}

/**
 * Helper function that populates the ethernet header and IP header used by
 * some of the xnb_add_mbuf_cksum unit tests.  m must already be allocated
 * and must be large enough
 */
static void
xnb_fill_eh_and_ip(struct mbuf *m, uint16_t ip_len, uint16_t ip_id,
		   uint16_t ip_p, uint16_t ip_off, uint16_t ip_sum)
{
	struct ether_header *eh;
	struct ip *iph;

	eh = mtod(m, struct ether_header*);
	eh->ether_dhost[0] = 0x00;
	eh->ether_dhost[1] = 0x16;
	eh->ether_dhost[2] = 0x3e;
	eh->ether_dhost[3] = 0x23;
	eh->ether_dhost[4] = 0x50;
	eh->ether_dhost[5] = 0x0b;
	eh->ether_shost[0] = 0x00;
	eh->ether_shost[1] = 0x16;
	eh->ether_shost[2] = 0x30;
	eh->ether_shost[3] = 0x00;
	eh->ether_shost[4] = 0x00;
	eh->ether_shost[5] = 0x00;
	eh->ether_type = htons(ETHERTYPE_IP);
	iph = (struct ip*)(eh + 1);
	iph->ip_hl = 0x5;	/* 5 dwords == 20 bytes */
	iph->ip_v = 4;		/* IP v4 */
	iph->ip_tos = 0;
	iph->ip_len = htons(ip_len);
	iph->ip_id = htons(ip_id);
	iph->ip_off = htons(ip_off);
	iph->ip_ttl = 64;
	iph->ip_p = ip_p;
	iph->ip_sum = htons(ip_sum);
	iph->ip_src.s_addr = htonl(0xc0a80a04);
	iph->ip_dst.s_addr = htonl(0xc0a80a05);
}

/**
 * xnb_add_mbuf_cksum on an ICMP packet, based on a tcpdump of an actual
 * ICMP packet
 */
static void
xnb_add_mbuf_cksum_icmp(char *buffer, size_t buflen)
{
	const size_t icmp_len = 64;	/* set by ping(1) */
	const size_t pkt_len = sizeof(struct ether_header) +
		sizeof(struct ip) + icmp_len;
	struct mbuf *mbufc;
	struct ether_header *eh;
	struct ip *iph;
	struct icmp *icmph;
	unsigned char pkt_orig[icmp_len];
	uint32_t *tv_field;
	uint8_t *data_payload;
	int i;
	const uint16_t ICMP_CSUM = 0xaed7;
	const uint16_t IP_CSUM = 0xe533;

	mbufc = m_getm(NULL, pkt_len, M_WAITOK, MT_DATA);
	/* Fill in an example ICMP ping request */
	eh = mtod(mbufc, struct ether_header*);
	xnb_fill_eh_and_ip(mbufc, 84, 28, IPPROTO_ICMP, 0, 0);
	iph = (struct ip*)(eh + 1);
	icmph = (struct icmp*)(iph + 1);
	icmph->icmp_type = ICMP_ECHO;
	icmph->icmp_code = 0;
	icmph->icmp_cksum = htons(ICMP_CSUM);
	icmph->icmp_id = htons(31492);
	icmph->icmp_seq = htons(0);
	/*
	 * ping(1) uses bcopy to insert a native-endian timeval after icmp_seq.
	 * For this test, we will set the bytes individually for portability.
	 */
	tv_field = (uint32_t*)(&(icmph->icmp_hun));
	tv_field[0] = 0x4f02cfac;
	tv_field[1] = 0x0007c46a;
	/*
	 * Remainder of packet is an incrmenting 8 bit integer, starting with 8
	 */
	data_payload = (uint8_t*)(&tv_field[2]);
	for (i = 8; i < 37; i++) {
		*data_payload++ = i;
	}

	/* fill in the length field */
	mbufc->m_len = pkt_len;
	mbufc->m_pkthdr.len = pkt_len;
	/* indicate that the netfront uses hw-assisted checksums */
	mbufc->m_pkthdr.csum_flags = CSUM_IP_CHECKED | CSUM_IP_VALID   |
				CSUM_DATA_VALID | CSUM_PSEUDO_HDR;

	bcopy(mtod(mbufc, const void*), pkt_orig, icmp_len);
	/* Function under test */
	xnb_add_mbuf_cksum(mbufc);

	/* Check the IP checksum */
	XNB_ASSERT(iph->ip_sum == htons(IP_CSUM));

	/* Check that the ICMP packet did not change */
	XNB_ASSERT(bcmp(icmph, pkt_orig, icmp_len));
	m_freem(mbufc);
}

/**
 * xnb_add_mbuf_cksum on a UDP packet, based on a tcpdump of an actual
 * UDP packet
 */
static void
xnb_add_mbuf_cksum_udp(char *buffer, size_t buflen)
{
	const size_t udp_len = 16;
	const size_t pkt_len = sizeof(struct ether_header) +
		sizeof(struct ip) + udp_len;
	struct mbuf *mbufc;
	struct ether_header *eh;
	struct ip *iph;
	struct udphdr *udp;
	uint8_t *data_payload;
	const uint16_t IP_CSUM = 0xe56b;
	const uint16_t UDP_CSUM = 0xdde2;

	mbufc = m_getm(NULL, pkt_len, M_WAITOK, MT_DATA);
	/* Fill in an example UDP packet made by 'uname | nc -u <host> 2222 */
	eh = mtod(mbufc, struct ether_header*);
	xnb_fill_eh_and_ip(mbufc, 36, 4, IPPROTO_UDP, 0, 0xbaad);
	iph = (struct ip*)(eh + 1);
	udp = (struct udphdr*)(iph + 1);
	udp->uh_sport = htons(0x51ae);
	udp->uh_dport = htons(0x08ae);
	udp->uh_ulen = htons(udp_len);
	udp->uh_sum = htons(0xbaad);  /* xnb_add_mbuf_cksum will fill this in */
	data_payload = (uint8_t*)(udp + 1);
	data_payload[0] = 'F';
	data_payload[1] = 'r';
	data_payload[2] = 'e';
	data_payload[3] = 'e';
	data_payload[4] = 'B';
	data_payload[5] = 'S';
	data_payload[6] = 'D';
	data_payload[7] = '\n';

	/* fill in the length field */
	mbufc->m_len = pkt_len;
	mbufc->m_pkthdr.len = pkt_len;
	/* indicate that the netfront uses hw-assisted checksums */
	mbufc->m_pkthdr.csum_flags = CSUM_IP_CHECKED | CSUM_IP_VALID   |
				CSUM_DATA_VALID | CSUM_PSEUDO_HDR;

	/* Function under test */
	xnb_add_mbuf_cksum(mbufc);

	/* Check the checksums */
	XNB_ASSERT(iph->ip_sum == htons(IP_CSUM));
	XNB_ASSERT(udp->uh_sum == htons(UDP_CSUM));

	m_freem(mbufc);
}

/**
 * Helper function that populates a TCP packet used by all of the
 * xnb_add_mbuf_cksum tcp unit tests.  m must already be allocated and must be
 * large enough
 */
static void
xnb_fill_tcp(struct mbuf *m)
{
	struct ether_header *eh;
	struct ip *iph;
	struct tcphdr *tcp;
	uint32_t *options;
	uint8_t *data_payload;

	/* Fill in an example TCP packet made by 'uname | nc <host> 2222' */
	eh = mtod(m, struct ether_header*);
	xnb_fill_eh_and_ip(m, 60, 8, IPPROTO_TCP, IP_DF, 0);
	iph = (struct ip*)(eh + 1);
	tcp = (struct tcphdr*)(iph + 1);
	tcp->th_sport = htons(0x9cd9);
	tcp->th_dport = htons(2222);
	tcp->th_seq = htonl(0x00f72b10);
	tcp->th_ack = htonl(0x7f37ba6c);
	tcp->th_x2 = 0;
	tcp->th_off = 8;
	tcp->th_flags = 0x18;
	tcp->th_win = htons(0x410);
	/* th_sum is incorrect; will be inserted by function under test */
	tcp->th_sum = htons(0xbaad);
	tcp->th_urp = htons(0);
	/*
	 * The following 12 bytes of options encode:
	 * [nop, nop, TS val 33247 ecr 3457687679]
	 */
	options = (uint32_t*)(tcp + 1);
	options[0] = htonl(0x0101080a);
	options[1] = htonl(0x000081df);
	options[2] = htonl(0xce18207f);
	data_payload = (uint8_t*)(&options[3]);
	data_payload[0] = 'F';
	data_payload[1] = 'r';
	data_payload[2] = 'e';
	data_payload[3] = 'e';
	data_payload[4] = 'B';
	data_payload[5] = 'S';
	data_payload[6] = 'D';
	data_payload[7] = '\n';
}

/**
 * xnb_add_mbuf_cksum on a TCP packet, based on a tcpdump of an actual TCP
 * packet
 */
static void
xnb_add_mbuf_cksum_tcp(char *buffer, size_t buflen)
{
	const size_t payload_len = 8;
	const size_t tcp_options_len = 12;
	const size_t pkt_len = sizeof(struct ether_header) + sizeof(struct ip) +
	    sizeof(struct tcphdr) + tcp_options_len + payload_len;
	struct mbuf *mbufc;
	struct ether_header *eh;
	struct ip *iph;
	struct tcphdr *tcp;
	const uint16_t IP_CSUM = 0xa55a;
	const uint16_t TCP_CSUM = 0x2f64;

	mbufc = m_getm(NULL, pkt_len, M_WAITOK, MT_DATA);
	/* Fill in an example TCP packet made by 'uname | nc <host> 2222' */
	xnb_fill_tcp(mbufc);
	eh = mtod(mbufc, struct ether_header*);
	iph = (struct ip*)(eh + 1);
	tcp = (struct tcphdr*)(iph + 1);

	/* fill in the length field */
	mbufc->m_len = pkt_len;
	mbufc->m_pkthdr.len = pkt_len;
	/* indicate that the netfront uses hw-assisted checksums */
	mbufc->m_pkthdr.csum_flags = CSUM_IP_CHECKED | CSUM_IP_VALID   |
				CSUM_DATA_VALID | CSUM_PSEUDO_HDR;

	/* Function under test */
	xnb_add_mbuf_cksum(mbufc);

	/* Check the checksums */
	XNB_ASSERT(iph->ip_sum == htons(IP_CSUM));
	XNB_ASSERT(tcp->th_sum == htons(TCP_CSUM));

	m_freem(mbufc);
}

/**
 * xnb_add_mbuf_cksum on a TCP packet that does not use HW assisted checksums
 */
static void
xnb_add_mbuf_cksum_tcp_swcksum(char *buffer, size_t buflen)
{
	const size_t payload_len = 8;
	const size_t tcp_options_len = 12;
	const size_t pkt_len = sizeof(struct ether_header) + sizeof(struct ip) +
	    sizeof(struct tcphdr) + tcp_options_len + payload_len;
	struct mbuf *mbufc;
	struct ether_header *eh;
	struct ip *iph;
	struct tcphdr *tcp;
	/* Use deliberately bad checksums, and verify that they don't get */
	/* corrected by xnb_add_mbuf_cksum */
	const uint16_t IP_CSUM = 0xdead;
	const uint16_t TCP_CSUM = 0xbeef;

	mbufc = m_getm(NULL, pkt_len, M_WAITOK, MT_DATA);
	/* Fill in an example TCP packet made by 'uname | nc <host> 2222' */
	xnb_fill_tcp(mbufc);
	eh = mtod(mbufc, struct ether_header*);
	iph = (struct ip*)(eh + 1);
	iph->ip_sum = htons(IP_CSUM);
	tcp = (struct tcphdr*)(iph + 1);
	tcp->th_sum = htons(TCP_CSUM);

	/* fill in the length field */
	mbufc->m_len = pkt_len;
	mbufc->m_pkthdr.len = pkt_len;
	/* indicate that the netfront does not use hw-assisted checksums */
	mbufc->m_pkthdr.csum_flags = 0;

	/* Function under test */
	xnb_add_mbuf_cksum(mbufc);

	/* Check that the checksums didn't change */
	XNB_ASSERT(iph->ip_sum == htons(IP_CSUM));
	XNB_ASSERT(tcp->th_sum == htons(TCP_CSUM));

	m_freem(mbufc);
}
#endif /* INET || INET6 */

/**
 * sscanf on unsigned chars
 */
static void
xnb_sscanf_hhu(char *buffer, size_t buflen)
{
	const char mystr[] = "137";
	uint8_t dest[12];
	int i;

	for (i = 0; i < 12; i++)
		dest[i] = 'X';

	XNB_ASSERT(sscanf(mystr, "%hhu", &dest[4]) == 1);
	for (i = 0; i < 12; i++)
		XNB_ASSERT(dest[i] == (i == 4 ? 137 : 'X'));
}

/**
 * sscanf on signed chars
 */
static void
xnb_sscanf_hhd(char *buffer, size_t buflen)
{
	const char mystr[] = "-27";
	int8_t dest[12];
	int i;

	for (i = 0; i < 12; i++)
		dest[i] = 'X';

	XNB_ASSERT(sscanf(mystr, "%hhd", &dest[4]) == 1);
	for (i = 0; i < 12; i++)
		XNB_ASSERT(dest[i] == (i == 4 ? -27 : 'X'));
}

/**
 * sscanf on signed long longs
 */
static void
xnb_sscanf_lld(char *buffer, size_t buflen)
{
	const char mystr[] = "-123456789012345";	/* about -2**47 */
	long long dest[3];
	int i;

	for (i = 0; i < 3; i++)
		dest[i] = (long long)0xdeadbeefdeadbeef;

	XNB_ASSERT(sscanf(mystr, "%lld", &dest[1]) == 1);
	for (i = 0; i < 3; i++)
		XNB_ASSERT(dest[i] == (i != 1 ? (long long)0xdeadbeefdeadbeef :
		    -123456789012345));
}

/**
 * sscanf on unsigned long longs
 */
static void
xnb_sscanf_llu(char *buffer, size_t buflen)
{
	const char mystr[] = "12802747070103273189";
	unsigned long long dest[3];
	int i;

	for (i = 0; i < 3; i++)
		dest[i] = (long long)0xdeadbeefdeadbeef;

	XNB_ASSERT(sscanf(mystr, "%llu", &dest[1]) == 1);
	for (i = 0; i < 3; i++)
		XNB_ASSERT(dest[i] == (i != 1 ? (long long)0xdeadbeefdeadbeef :
		    12802747070103273189ull));
}

/**
 * sscanf on unsigned short short n's
 */
static void
xnb_sscanf_hhn(char *buffer, size_t buflen)
{
	const char mystr[] =
	    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
	    "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
	    "404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f";
	unsigned char dest[12];
	int i;

	for (i = 0; i < 12; i++)
		dest[i] = (unsigned char)'X';

	XNB_ASSERT(sscanf(mystr,
	    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
	    "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
	    "404142434445464748494a4b4c4d4e4f%hhn", &dest[4]) == 0);
	for (i = 0; i < 12; i++)
		XNB_ASSERT(dest[i] == (i == 4 ? 160 : 'X'));
}

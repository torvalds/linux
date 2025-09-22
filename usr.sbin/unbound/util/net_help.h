/*
 * util/net_help.h - network help functions 
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
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
 * This file contains functions to perform network related tasks.
 */

#ifndef NET_HELP_H
#define NET_HELP_H
#include "util/log.h"
#include "util/random.h"
struct sock_list;
struct regional;
struct config_strlist;

/** DNS constants for uint16_t style flag manipulation. host byteorder. 
 *                                1  1  1  1  1  1
 *  0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |QR|   Opcode  |AA|TC|RD|RA| Z|AD|CD|   RCODE   |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 */ 
/** CD flag */
#define BIT_CD 0x0010
/** AD flag */
#define BIT_AD 0x0020
/** Z flag */
#define BIT_Z  0x0040
/** RA flag */
#define BIT_RA 0x0080
/** RD flag */
#define BIT_RD 0x0100
/** TC flag */
#define BIT_TC 0x0200
/** AA flag */
#define BIT_AA 0x0400
/** QR flag */
#define BIT_QR 0x8000
/** get RCODE bits from uint16 flags */
#define FLAGS_GET_RCODE(f) ((f) & 0xf)
/** set RCODE bits in uint16 flags */
#define FLAGS_SET_RCODE(f, r) (f = (((f) & 0xfff0) | (r)))

/** timeout in milliseconds for UDP queries to auth servers. */
#define UDP_AUTH_QUERY_TIMEOUT 3000
/** Advertised version of EDNS capabilities */
#define EDNS_ADVERTISED_VERSION         0
/** Advertised size of EDNS capabilities */
extern uint16_t EDNS_ADVERTISED_SIZE;
/** bits for EDNS bitfield */
#define EDNS_DO 0x8000 /* Dnssec Ok */
/** byte size of ip4 address */
#define INET_SIZE 4
/** byte size of ip6 address */
#define INET6_SIZE 16

/** DNSKEY zone sign key flag */
#define DNSKEY_BIT_ZSK 0x0100
/** DNSKEY secure entry point, KSK flag */
#define DNSKEY_BIT_SEP 0x0001

/** return a random 16-bit number given a random source */
#define GET_RANDOM_ID(rnd) (((unsigned)ub_random(rnd)>>8) & 0xffff)

/** define MSG_DONTWAIT for unsupported platforms */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

/** minimal responses when positive answer */
extern int MINIMAL_RESPONSES;

/** rrset order roundrobin */
extern int RRSET_ROUNDROBIN;

/** log tag queries with name instead of 'info' for filtering */
extern int LOG_TAG_QUERYREPLY;

/**
 * See if string is ip4 or ip6.
 * @param str: IP specification.
 * @return: true if string addr is an ip6 specced address.
 */
int str_is_ip6(const char* str);

/**
 * Set fd nonblocking.
 * @param s: file descriptor.
 * @return: 0 on error (error is printed to log).
 */
int fd_set_nonblock(int s); 

/**
 * Set fd (back to) blocking.
 * @param s: file descriptor.
 * @return: 0 on error (error is printed to log).
 */
int fd_set_block(int s); 

/**
 * See if number is a power of 2.
 * @param num: the value.
 * @return: true if the number is a power of 2.
 */
int is_pow2(size_t num);

/**
 * Allocate memory and copy over contents.
 * @param data: what to copy over.
 * @param len: length of data.
 * @return: NULL on malloc failure, or newly malloced data.
 */
void* memdup(void* data, size_t len);

/**
 * Prints the sockaddr in readable format with log_info. Debug helper.
 * @param v: at what verbosity level to print this.
 * @param str: descriptive string printed with it.
 * @param addr: the sockaddr to print. Can be ip4 or ip6.
 * @param addrlen: length of addr.
 */
void log_addr(enum verbosity_value v, const char* str, 
	struct sockaddr_storage* addr, socklen_t addrlen);

/**
 * Prints zone name and sockaddr in readable format with log_info. Debug.
 * @param v: at what verbosity level to print this.
 * @param str: descriptive string printed with it.
 * @param zone: DNS domain name, uncompressed wireformat.
 * @param addr: the sockaddr to print. Can be ip4 or ip6.
 * @param addrlen: length of addr.
 */
void log_name_addr(enum verbosity_value v, const char* str, uint8_t* zone, 
	struct sockaddr_storage* addr, socklen_t addrlen);

/**
 * Log errno and addr.
 * @param str: descriptive string printed with it.
 * @param err: errno string to print, i.e. strerror(errno).
 * @param addr: the sockaddr to print. Can be ip4 or ip6.
 * @param addrlen: length of addr.
 */
void log_err_addr(const char* str, const char* err,
	struct sockaddr_storage* addr, socklen_t addrlen);

/**
 * Convert address string, with "@port" appendix, to sockaddr.
 * Uses DNS port by default.
 * @param str: the string
 * @param addr: where to store sockaddr.
 * @param addrlen: length of stored sockaddr is returned.
 * @param port: default port.
 * @return 0 on error.
 */
int extstrtoaddr(const char* str, struct sockaddr_storage* addr, 
	socklen_t* addrlen, int port);

/**
 * Convert ip address string and port to sockaddr.
 * @param ip: ip4 or ip6 address string.
 * @param port: port number, host format.
 * @param addr: where to store sockaddr.
 * @param addrlen: length of stored sockaddr is returned.
 * @return 0 on error.
 */
int ipstrtoaddr(const char* ip, int port, struct sockaddr_storage* addr,
	socklen_t* addrlen);

/**
 * Convert ip netblock (ip/netsize) string and port to sockaddr.
 * performs a copy internally to avoid writing over 'ip' string.
 * @param ip: ip4 or ip6 address string.
 * @param port: port number, host format.
 * @param addr: where to store sockaddr.
 * @param addrlen: length of stored sockaddr is returned.
 * @param net: netblock size is returned.
 * @return 0 on error.
 */
int netblockstrtoaddr(const char* ip, int port, struct sockaddr_storage* addr,
	socklen_t* addrlen, int* net);

/**
 * Convert address string, with "@port" appendix, to sockaddr.
 * It can also have an "#tls-auth-name" appendix (after the port).
 * The returned auth_name string is a pointer into the input string.
 * Uses DNS port by default; TLS port when a "#tls-auth-name" is configured.
 * @param str: the string
 * @param addr: where to store sockaddr.
 * @param addrlen: length of stored sockaddr is returned.
 * @param auth_name: returned pointer to tls_auth_name, or NULL if none.
 * @return 0 on error.
 */
int authextstrtoaddr(char* str, struct sockaddr_storage* addr,
	socklen_t* addrlen, char** auth_name);

/**
 * Convert domain string, with "@port" appendix, to dname.
 * It can also have an "#tls-auth-name" appendix (after the port).
 * The return port is the parsed port.
 * Uses DNS port by default; TLS port when a "#tls-auth-name" is configured.
 * The returned auth_name string is a pointer into the input string.
 * @param str: the string
 * @param port: pointer to be assigned the parsed port value.
 * @param auth_name: returned pointer to tls_auth_name, or NULL if none.
 * @return pointer to the dname.
 */
uint8_t* authextstrtodname(char* str, int* port, char** auth_name);

/**
 * Store port number into sockaddr structure
 * @param addr: sockaddr structure, ip4 or ip6.
 * @param addrlen: length of addr.
 * @param port: port number to put into the addr.
 */
void sockaddr_store_port(struct sockaddr_storage* addr, socklen_t addrlen,
	int port);

/**
 * Print string with neat domain name, type and class.
 * @param v: at what verbosity level to print this.
 * @param str: string of message.
 * @param name: domain name uncompressed wireformat.
 * @param type: host format RR type.
 * @param dclass: host format RR class.
 */
void log_nametypeclass(enum verbosity_value v, const char* str, 
	uint8_t* name, uint16_t type, uint16_t dclass);

/**
 * Like log_nametypeclass, but logs with log_query for query logging
 */
void log_query_in(const char* str, uint8_t* name, uint16_t type,
	uint16_t dclass);

/**
 * Compare two sockaddrs. Imposes an ordering on the addresses.
 * Compares address and port.
 * @param addr1: address 1.
 * @param len1: lengths of addr1.
 * @param addr2: address 2.
 * @param len2: lengths of addr2.
 * @return: 0 if addr1 == addr2. -1 if addr1 is smaller, +1 if larger.
 */
int sockaddr_cmp(struct sockaddr_storage* addr1, socklen_t len1, 
	struct sockaddr_storage* addr2, socklen_t len2);

/**
 * Compare two sockaddrs. Compares address, not the port.
 * @param addr1: address 1.
 * @param len1: lengths of addr1.
 * @param addr2: address 2.
 * @param len2: lengths of addr2.
 * @return: 0 if addr1 == addr2. -1 if addr1 is smaller, +1 if larger.
 */
int sockaddr_cmp_addr(struct sockaddr_storage* addr1, socklen_t len1, 
	struct sockaddr_storage* addr2, socklen_t len2);

/**
 * Compare two sockaddrs. Imposes an ordering on the addresses.
 * Compares address and port. It also compares scope_id for ip6.
 * @param addr1: address 1.
 * @param len1: lengths of addr1.
 * @param addr2: address 2.
 * @param len2: lengths of addr2.
 * @return: 0 if addr1 == addr2. -1 if addr1 is smaller, +1 if larger.
 */
int sockaddr_cmp_scopeid(struct sockaddr_storage* addr1, socklen_t len1,
	struct sockaddr_storage* addr2, socklen_t len2);

/**
 * Checkout address family.
 * @param addr: the sockaddr to examine.
 * @param len: the length of addr.
 * @return: true if sockaddr is ip6.
 */
int addr_is_ip6(struct sockaddr_storage* addr, socklen_t len);

/**
 * Make sure the sockaddr ends in zeroes. For tree insertion and subsequent
 * comparison.
 * @param addr: the ip4 or ip6 addr.
 * @param len: length of addr.
 * @param net: number of bits to leave untouched, the rest of the netblock
 * 	address is zeroed.
 */
void addr_mask(struct sockaddr_storage* addr, socklen_t len, int net);

/**
 * See how many bits are shared, equal, between two addrs.
 * @param addr1: first addr.
 * @param net1: netblock size of first addr.
 * @param addr2: second addr.
 * @param net2: netblock size of second addr.
 * @param addrlen: length of first addr and of second addr.
 * 	They must be of the same length (i.e. same type IP4, IP6).
 * @return: number of bits the same.
 */
int addr_in_common(struct sockaddr_storage* addr1, int net1,
	struct sockaddr_storage* addr2, int net2, socklen_t addrlen);

/**
 * Put address into string, works for IPv4 and IPv6.
 * @param addr: address
 * @param addrlen: length of address
 * @param buf: result string stored here
 * @param len: length of buf.
 * On failure a string with "error" is stored inside.
 */
void addr_to_str(struct sockaddr_storage* addr, socklen_t addrlen,
	char* buf, size_t len);

/**
 * Check if the prefix network length is one of the allowed 32, 40, 48, 56, 64,
 * or 96.
 * @param prefixnet: prefix network length to check.
 * @return 1 on success, 0 on failure.
 */
int prefixnet_is_nat64(int prefixnet);

/**
 * Create a NAT64 address from a given address (needs to be IPv4) and a given
 * NAT64 prefix. The NAT64 prefix net needs to be one of 32, 40, 48, 56, 64, 96.
 * @param addr: IPv4 address.
 * @param nat64_prefix: NAT64 prefix.
 * @param nat64_prefixlen: NAT64 prefix len.
 * @param nat64_prefixnet: NAT64 prefix mask.
 * @param nat64_addr: the resulting NAT64 address.
 * @param nat64_addrlen: the resulting NAT64 address length.
 */
void addr_to_nat64(const struct sockaddr_storage* addr,
	const struct sockaddr_storage* nat64_prefix,
	socklen_t nat64_prefixlen, int nat64_prefixnet,
	struct sockaddr_storage* nat64_addr, socklen_t* nat64_addrlen);

/**
 * See if sockaddr is an ipv6 mapped ipv4 address, "::ffff:0.0.0.0"
 * @param addr: address
 * @param addrlen: length of address
 * @return true if so
 */
int addr_is_ip4mapped(struct sockaddr_storage* addr, socklen_t addrlen);

/**
 * See if sockaddr is an ipv6 fe80::/10 link local address.
 * @param addr: address
 * @param addrlen: length of address
 * @return true if so
 */
int addr_is_ip6linklocal(struct sockaddr_storage* addr, socklen_t addrlen);

/**
 * See if sockaddr is 255.255.255.255.
 * @param addr: address
 * @param addrlen: length of address
 * @return true if so
 */
int addr_is_broadcast(struct sockaddr_storage* addr, socklen_t addrlen);

/**
 * See if sockaddr is 0.0.0.0 or ::0.
 * @param addr: address
 * @param addrlen: length of address
 * @return true if so
 */
int addr_is_any(struct sockaddr_storage* addr, socklen_t addrlen);

/**
 * Insert new socket list item. If fails logs error.
 * @param list: pointer to pointer to first item.
 * @param addr: address or NULL if 'cache'.
 * @param len: length of addr, or 0 if 'cache'.
 * @param region: where to allocate
 */
void sock_list_insert(struct sock_list** list, struct sockaddr_storage* addr,
	socklen_t len, struct regional* region);

/**
 * Append one list to another.  Must both be from same qstate(regional).
 * @param list: pointer to result list that is modified.
 * @param add: item(s) to add.  They are prepended to list.
 */
void sock_list_prepend(struct sock_list** list, struct sock_list* add);

/**
 * Find addr in list.
 * @param list: to search in
 * @param addr: address to look for.
 * @param len: length. Can be 0, look for 'cache entry'.
 * @return true if found.
 */
int sock_list_find(struct sock_list* list, struct sockaddr_storage* addr,
        socklen_t len);

/**
 * Merge socklist into another socket list.  Allocates the new entries
 * freshly and copies them over, so also performs a region switchover.
 * Allocation failures are logged.
 * @param list: the destination list (checked for duplicates)
 * @param region: where to allocate
 * @param add: the list of entries to add.
 */
void sock_list_merge(struct sock_list** list, struct regional* region,
	struct sock_list* add);

/**
 * Log libcrypto error with descriptive string. Calls log_err().
 * @param str: what failed.
 */
void log_crypto_err(const char* str);

/**
 * Log libcrypto error from errcode with descriptive string, calls log_err.
 * @param str: what failed.
 * @param err: error code from ERR_get_error.
 */
void log_crypto_err_code(const char* str, unsigned long err);

/**
 * Log an error from libcrypto that came from SSL_write and so on, with
 * a value from SSL_get_error, calls log_err. If that fails it logs with
 * log_crypto_err.
 * @param str: what failed
 * @param r: output of SSL_get_error on the I/O operation result.
 */
void log_crypto_err_io(const char* str, int r);

/**
 * Log an error from libcrypt that came from an I/O routine with the
 * errcode from ERR_get_error. Calls log_err() and log_crypto_err_code.
 * @param str: what failed
 * @param r: output of SSL_get_error on the I/O operation result.
 * @param err: error code from ERR_get_error
 */
void log_crypto_err_io_code(const char* str, int r, unsigned long err);

/**
 * Log certificate details verbosity, string, of X509 cert
 * @param level: verbosity level
 * @param str: string to prefix on output
 * @param cert: X509* structure.
 */
void log_cert(unsigned level, const char* str, void* cert);

/**
 * Set SSL_OP_NOxxx options on SSL context to disable bad crypto
 * @param ctxt: SSL_CTX*
 * @return false on failure.
 */
int listen_sslctx_setup(void* ctxt);

/**
 * Further setup of listening SSL context, after keys loaded.
 * @param ctxt: SSL_CTX*
 */
void listen_sslctx_setup_2(void* ctxt);

/**
 * create SSL listen context
 * @param key: private key file.
 * @param pem: public key cert.
 * @param verifypem: if nonNULL, verifylocation file.
 * @param tls_ciphers: if non empty string, tls ciphers to use.
 * @param tls_ciphersuites: if non empty string, tls ciphersuites to use.
 * @param set_ticket_keys_cb: if the callback for configured ticket keys needs
 *	to be set.
 * @param is_dot: if the TLS connection is for DoT to set the appropriate ALPN.
 * @param is_doh: if the TLS connection is for DoH to set the appropriate ALPN.
 * return SSL_CTX* or NULL on failure (logged).
 */
void* listen_sslctx_create(const char* key, const char* pem,
	const char* verifypem, const char* tls_ciphers,
	const char* tls_ciphersuites, int set_ticket_keys_cb,
	int is_dot, int is_doh);

/**
 * create SSL connect context
 * @param key: if nonNULL (also pem nonNULL), the client private key.
 * @param pem: client public key (or NULL if key is NULL).
 * @param verifypem: if nonNULL used for verifylocation file.
 * @param wincert: add system certificate store to ctx (add to verifypem ca
 * 	certs).
 * @return SSL_CTX* or NULL on failure (logged).
 */
void* connect_sslctx_create(char* key, char* pem, char* verifypem, int wincert);

/**
 * accept a new fd and wrap it in a BIO in SSL
 * @param sslctx: the SSL_CTX to use (from listen_sslctx_create()).
 * @param fd: from accept, nonblocking.
 * @return SSL or NULL on alloc failure.
 */
void* incoming_ssl_fd(void* sslctx, int fd);

/**
 * connect a new fd and wrap it in a BIO in SSL
 * @param sslctx: the SSL_CTX to use (from connect_sslctx_create())
 * @param fd: from connect.
 * @return SSL or NULL on alloc failure
 */
void* outgoing_ssl_fd(void* sslctx, int fd);

/**
 * check if authname SSL functionality is available, false if not
 * @param auth_name: the name for the remote server, used for error print.
 * @return false if SSL functionality to check the SSL name is not available.
 */
int check_auth_name_for_ssl(char* auth_name);

/**
 * set auth name on SSL for verification
 * @param ssl: SSL* to set
 * @param auth_name: if NULL nothing happens, otherwise the name to check.
 * @param use_sni: if SNI will be used.
 * @return 1 on success or NULL auth_name, 0 on failure.
 */
int set_auth_name_on_ssl(void* ssl, char* auth_name, int use_sni);

/**
 * Initialize openssl locking for thread safety
 * @return false on failure (alloc failure).
 */
int ub_openssl_lock_init(void);

/**
 * De-init the allocated openssl locks
 */
void ub_openssl_lock_delete(void);

/**
 * setup TLS session ticket
 * @param tls_session_ticket_keys: TLS ticket secret filenames
 * @return false on failure (alloc failure).
 */
int listen_sslctx_setup_ticket_keys(struct config_strlist* tls_session_ticket_keys);

/** Free memory used for TLS session ticket keys */
void listen_sslctx_delete_ticket_keys(void);

/**
 * RPZ format netblock to network byte order address and netblock
 * example RPZ netblock format dnames:
 *  - 24.10.100.51.198.rpz-ip -> 198.51.100.10/24
 *  - 32.10.zz.db8.2001.rpz-ip -> 2001:db8:0:0:0:0:0:10/32
 * @param dname: the dname containing RPZ format netblock
 * @param dnamelen: length of dname
 * @param addr: where to store sockaddr.
 * @param addrlen: length of stored sockaddr is returned.
 * @param net: where to store netmask
 * @param af: where to store address family.
 * @return 0 on error.
 */
int netblockdnametoaddr(uint8_t* dname, size_t dnamelen,
	struct sockaddr_storage* addr, socklen_t* addrlen, int* net, int* af);

/** Return strerror or wsastrerror for socket error printout */
char* sock_strerror(int errn);
/** close the socket with close, or wsa closesocket */
void sock_close(int socket);

/**
 * Convert binary data to a string of hexadecimal characters.
 */
ssize_t hex_ntop(uint8_t const *src, size_t srclength, char *target,
		 size_t targsize);

/** Convert hexadecimal data to binary. */
ssize_t hex_pton(const char* src, uint8_t* target, size_t targsize);

#endif /* NET_HELP_H */

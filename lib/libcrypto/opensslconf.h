/*	$OpenBSD: opensslconf.h,v 1.4 2025/08/29 18:29:42 tb Exp $ */
/*
 * Public domain.
 */

#include <openssl/opensslfeatures.h>

#ifndef OPENSSL_FILE
#ifdef OPENSSL_NO_FILENAMES
#define OPENSSL_FILE ""
#define OPENSSL_LINE 0
#else
#define OPENSSL_FILE __FILE__
#define OPENSSL_LINE __LINE__
#endif
#endif

/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (c) 2023, Oracle and/or its affiliates.
 *
 * TLS Protocol definitions
 *
 * From https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml
 */

#ifndef _TLS_PROT_H
#define _TLS_PROT_H

/*
 * TLS Record protocol: ContentType
 */
enum {
	TLS_RECORD_TYPE_CHANGE_CIPHER_SPEC = 20,
	TLS_RECORD_TYPE_ALERT = 21,
	TLS_RECORD_TYPE_HANDSHAKE = 22,
	TLS_RECORD_TYPE_DATA = 23,
	TLS_RECORD_TYPE_HEARTBEAT = 24,
	TLS_RECORD_TYPE_TLS12_CID = 25,
	TLS_RECORD_TYPE_ACK = 26,
};

#endif /* _TLS_PROT_H */

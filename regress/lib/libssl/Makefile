#	$OpenBSD: Makefile,v 1.58 2025/02/01 12:27:11 jsing Exp $

SUBDIR += api
SUBDIR += asn1
SUBDIR += buffer
SUBDIR += bytestring
SUBDIR += ciphers
SUBDIR += client
SUBDIR += dtls
SUBDIR += exporter
SUBDIR += handshake
SUBDIR += pqueue
SUBDIR += quic
SUBDIR += record
SUBDIR += record_layer
SUBDIR += renegotiation
SUBDIR += server
SUBDIR += shutdown
SUBDIR += ssl
SUBDIR += symbols
SUBDIR += tls
SUBDIR += tlsext
SUBDIR += tlslegacy
SUBDIR += key_schedule
SUBDIR += unit
SUBDIR += verify

# Things that take a long time should go below here. 
SUBDIR += openssl-ruby
SUBDIR += rust-openssl
SUBDIR += tlsfuzzer
SUBDIR += interop

install:

.include <bsd.subdir.mk>

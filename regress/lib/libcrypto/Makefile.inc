#	$OpenBSD: Makefile.inc,v 1.2 2018/07/17 17:06:49 tb Exp $
# Use this variable when the test needs internal symbols from libcrypto
CRYPTO_INT=  -Wl,-Bstatic -lcrypto -Wl,-Bdynamic

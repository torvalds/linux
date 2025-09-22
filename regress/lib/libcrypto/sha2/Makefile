#	$OpenBSD: Makefile,v 1.3 2015/09/29 01:07:56 doug Exp $

REGRESS_TARGETS=regress-sha2
OPENSSL=/usr/bin/openssl

HASHES= \
	sha224 \
		abc \
		23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7 \
	sha224 \
		abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq \
		75388b16512776cc5dba5da1fd890150b0c6455cb4f58b1952522525 \
	sha256 \
		abc \
		ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad \
	sha256 \
		abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq \
		248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1 \
	sha384 \
		abc \
		cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7 \
	sha384 \
		abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu \
		09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712fcc7c71a557e2db966c3e9fa91746039 \
	sha512 \
		abc \
		ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f \
	sha512 \
		abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu \
		8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909

regress-sha2:
.for hash input output in ${HASHES}
	@(echo -n ${input} | ${OPENSSL} ${hash} | (read a b; \
	  test $$b == ${output} || \
	  (echo wrong ${hash} hash of \"${input}\"; \
	   echo expected: ${output}; \
	   echo computed: $$b; \
	   false)))
.endfor

.include <bsd.regress.mk>

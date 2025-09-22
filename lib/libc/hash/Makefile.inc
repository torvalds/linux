#	$OpenBSD: Makefile.inc,v 1.25 2019/08/30 22:20:43 deraadt Exp $

# hash functions
.PATH: ${LIBCSRCDIR}/hash

HELPER=	md5hl.c rmd160hl.c sha1hl.c sha224hl.c sha256hl.c sha384hl.c sha512hl.c sha512_256hl.c
SRCS+=	md5.c rmd160.c sha1.c sha2.c ${HELPER} siphash.c
MAN+=	MD5Init.3 RMD160Init.3 SHA1Init.3 SHA256Init.3 SipHash24_Init.3

CLEANFILES+= ${HELPER}

md5hl.c:	helper.c
	sed -e 's/hashinc/md5.h/g' -e 's/HASH/MD5/g' $> > $@

rmd160hl.c:	helper.c
	sed -e 's/hashinc/rmd160.h/g' -e 's/HASH/RMD160/g' $> > $@

sha1hl.c:	helper.c
	sed -e 's/hashinc/sha1.h/g' -e 's/HASH/SHA1/g' $> > $@

sha224hl.c:	helper.c
	sed -e 's/hashinc/sha2.h/g' \
	    -e 's/HASH/SHA224/g' \
	    -e 's/SHA[0-9][0-9][0-9]_CTX/SHA2_CTX/g' $> > $@

sha256hl.c:	helper.c
	sed -e 's/hashinc/sha2.h/g' \
	    -e 's/HASH/SHA256/g' \
	    -e 's/SHA[0-9][0-9][0-9]_CTX/SHA2_CTX/g' $> > $@

sha384hl.c:	helper.c
	sed -e 's/hashinc/sha2.h/g' \
	    -e 's/HASH/SHA384/g' \
	    -e 's/SHA[0-9][0-9][0-9]_CTX/SHA2_CTX/g' $> > $@

sha512hl.c:	helper.c
	sed -e 's/hashinc/sha2.h/g' \
	    -e 's/HASH/SHA512/g' \
	    -e 's/SHA[0-9][0-9][0-9]_CTX/SHA2_CTX/g' $> > $@

sha512_256hl.c:	helper.c
	sed -e 's/hashinc/sha2.h/g' \
	    -e 's/HASH/SHA512_256/g' \
	    -e 's/SHA512_256_CTX/SHA2_CTX/g' $> > $@

beforedepend: md5hl.c rmd160hl.c sha1hl.c sha256hl.c sha384hl.c sha512hl.c sha512_256hl.c

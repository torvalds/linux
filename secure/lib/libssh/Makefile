# $FreeBSD$

.include <src.opts.mk>

LIB=	ssh
PRIVATELIB=	true
SHLIB_MAJOR=	5
SRCS=	ssh_api.c ssherr.c sshbuf.c sshkey.c sshbuf-getput-basic.c \
	sshbuf-misc.c sshbuf-getput-crypto.c krl.c bitmap.c
SRCS+=	authfd.c authfile.c \
	canohost.c channels.c cipher.c cipher-aes.c cipher-aesctr.c \
	cipher-ctr.c cleanup.c \
	compat.c crc32.c fatal.c hostfile.c \
	log.c match.c moduli.c nchan.c packet.c opacket.c \
	readpass.c ttymodes.c xmalloc.c addrmatch.c \
	atomicio.c dispatch.c mac.c uuencode.c misc.c utf8.c \
	monitor_fdpass.c rijndael.c ssh-dss.c ssh-ecdsa.c ssh-rsa.c dh.c \
	msg.c progressmeter.c dns.c entropy.c umac.c umac128.c \
	ssh-pkcs11.c smult_curve25519_ref.c \
	poly1305.c chacha.c cipher-chachapoly.c \
	ssh-ed25519.c digest-openssl.c digest-libc.c hmac.c \
	sc25519.c ge25519.c fe25519.c ed25519.c verify.c hash.c \
	kex.c kexdh.c kexgex.c kexecdh.c kexc25519.c \
	kexdhc.c kexgexc.c kexecdhc.c kexc25519c.c \
	kexdhs.c kexgexs.c kexecdhs.c kexc25519s.c \
	platform-pledge.c platform-tracing.c platform-misc.c
PACKAGE=	ssh

# gss-genr.c should be in $SRCS but causes linking problems, so it is
# compiled directly into sshd instead.

# Portability layer
SRCS+=	bcrypt_pbkdf.c blowfish.c bsd-misc.c bsd-signal.c explicit_bzero.c \
	fmt_scaled.c freezero.c glob.c \
	libressl-api-compat.c \
	openssl-compat.c port-net.c \
	realpath.c recallocarray.c strtonum.c timingsafe_bcmp.c vis.c xcrypt.c

.if ${MK_LDNS} == "no"
SRCS+=	getrrsetbyname.c
.else
LDNSDIR=	${SRCTOP}/contrib/ldns
CFLAGS+=	-DHAVE_LDNS=1 -I${LDNSDIR}
SRCS+=	getrrsetbyname-ldns.c
LIBADD+=	ldns
.endif

CFLAGS+= -I${SSHDIR} -include ssh_namespace.h
SRCS+=	 ssh_namespace.h

.if ${MK_GSSAPI} != "no" && ${MK_KERBEROS_SUPPORT} != "no"
CFLAGS+= -include krb5_config.h
SRCS+=	 krb5_config.h
.endif

NO_LINT=

LIBADD+=	crypto crypt z

.include <bsd.lib.mk>

.PATH: ${SSHDIR} ${SSHDIR}/openbsd-compat

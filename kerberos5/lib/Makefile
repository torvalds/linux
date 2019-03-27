# $FreeBSD$

.include <src.opts.mk>

SUBDIR=	libasn1 libhdb \
	libheimntlm libhx509 libkadm5clnt libkadm5srv libkrb5 \
	libroken libsl libvers libkdc libwind libheimbase libheimipcc libheimipcs

SUBDIR+= libkafs5  # requires krb_err.h from libkrb5
SUBDIR_DEPEND_libkafs5=	libkrb5

.if ${MK_GSSAPI} != "no"
SUBDIR+=	libgssapi_krb5
SUBDIR+=	libgssapi_ntlm
SUBDIR+=	libgssapi_spnego
.endif

.include <bsd.subdir.mk>

# $FreeBSD$

# Consider this file an example.
#
# For Junos this is how we obtain trust anchor .pems
# the signing server (http://www.crufty.net/sjg/blog/signing-server.htm)
# for each key will provide the appropriate certificate chain on request

# force these for Junos
#MANIFEST_SKIP_ALWAYS= boot
VE_HASH_LIST= \
	SHA1 \
	SHA256 \
	SHA384 \
	SHA512

VE_SIGNATURE_LIST= \
	ECDSA \
	RSA

VE_SIGNATURE_EXT_LIST= \
	esig \
	rsig

VE_SELF_TESTS= yes

.if ${MACHINE} == "host" && ${.CURDIR:T} == "tests"

VE_SIGNATURE_LIST+= \
	DEPRECATED_RSA_SHA1

VE_SIGNATURE_EXT_LIST+= \
	sig
.endif

SIGNER ?= ${SB_TOOLS_PATH:U/volume/buildtools/bin}/sign.py

.if exists(${SIGNER})
SIGN_HOST ?= ${SB_SITE:Usvl}-junos-signer.juniper.net
ECDSA_PORT:= ${133%y:L:gmtime}
SIGN_ECDSA= ${PYTHON} ${SIGNER} -u ${SIGN_HOST}:${ECDSA_PORT} -h sha256
RSA2_PORT:= ${163%y:L:gmtime}
SIGN_RSA2=   ${PYTHON} ${SIGNER} -u ${SIGN_HOST}:${RSA2_PORT} -h sha256

.if !empty(OPENPGP_SIGN_URL)
VE_SIGNATURE_LIST+= OPENPGP
VE_SIGNATURE_EXT_LIST+= asc

SIGN_OPENPGP= ${PYTHON} ${SIGNER:H}/openpgp-sign.py -a -u ${OPENPGP_SIGN_URL}

ta_openpgp.asc:
	${SIGN_OPENPGP} -C ${.TARGET}

ta.h: ta_openpgp.asc

.if ${VE_SELF_TESTS} != "no"
# for self test
vc_openpgp.asc: ta_openpgp.asc
	${SIGN_OPENPGP} ${.ALLSRC:M*.asc}
	mv ta_openpgp.asc.asc ${.TARGET}

ta.h: vc_openpgp.asc
.endif
.endif

rcerts.pem:
	${SIGN_RSA2} -C ${.TARGET}

ecerts.pem:
	${SIGN_ECDSA} -C ${.TARGET}

.if ${VE_SIGNATURE_LIST:tu:MECDSA} != ""
# the last cert in the chain is the one we want
ta_ec.pem: ecerts.pem _LAST_PEM_USE

.if ${VE_SELF_TESTS} != "no"
# these are for verification self test
vc_ec.pem: ecerts.pem _2ndLAST_PEM_USE
.endif
.endif

.if ${VE_SIGNATURE_LIST:tu:MRSA} != ""
ta_rsa.pem: rcerts.pem _LAST_PEM_USE
.if ${VE_SELF_TESTS} != "no"
vc_rsa.pem: rcerts.pem _2ndLAST_PEM_USE
.endif
.endif

# we take the mtime of this as our baseline time
#BUILD_UTC_FILE= ecerts.pem
#VE_DEBUG_LEVEL=3
#VE_VERBOSE_DEFAULT=1

.else
# you need to provide t*.pem or t*.asc files for each trust anchor
.if empty(TRUST_ANCHORS)
TRUST_ANCHORS!= cd ${.CURDIR} && 'ls' -1 *.pem t*.asc 2> /dev/null
.endif
.if empty(TRUST_ANCHORS) && ${MK_LOADER_EFI_SECUREBOOT} != "yes"
.error Need TRUST_ANCHORS see ${.CURDIR}/README.rst
.endif
.if ${TRUST_ANCHORS:T:Mt*.pem} != ""
ta.h: ${TRUST_ANCHORS:M*.pem}
.endif
.if ${TRUST_ANCHORS:T:Mt*.asc} != ""
VE_SIGNATURE_LIST+= OPENPGP
VE_SIGNATURE_EXT_LIST+= asc
ta_asc.h: ${TRUST_ANCHORS:M*.asc}
.endif
# we take the mtime of this as our baseline time
BUILD_UTC_FILE?= ${TRUST_ANCHORS:[1]}
.endif


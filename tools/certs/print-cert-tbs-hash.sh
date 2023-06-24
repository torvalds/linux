#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright © 2020, Microsoft Corporation. All rights reserved.
#
# Author: Mickaël Salaün <mic@linux.microsoft.com>
#
# Compute and print the To Be Signed (TBS) hash of a certificate.  This is used
# as description of keys in the blacklist keyring to identify certificates.
# This output should be redirected, without newline, in a file (hash0.txt) and
# signed to create a PKCS#7 file (hash0.p7s).  Both of these files can then be
# loaded in the kernel with.
#
# Exemple on a workstation:
# ./print-cert-tbs-hash.sh certificate-to-invalidate.pem > hash0.txt
# openssl smime -sign -in hash0.txt -inkey builtin-private-key.pem \
#               -signer builtin-certificate.pem -certfile certificate-chain.pem \
#               -noattr -binary -outform DER -out hash0.p7s
#
# Exemple on a managed system:
# keyctl padd blacklist "$(< hash0.txt)" %:.blacklist < hash0.p7s

set -u -e -o pipefail

CERT="${1:-}"
BASENAME="$(basename -- "${BASH_SOURCE[0]}")"

if [ $# -ne 1 ] || [ ! -f "${CERT}" ]; then
	echo "usage: ${BASENAME} <certificate>" >&2
	exit 1
fi

# Checks that it is indeed a certificate (PEM or DER encoded) and exclude the
# optional PEM text header.
if ! PEM="$(openssl x509 -inform DER -in "${CERT}" 2>/dev/null || openssl x509 -in "${CERT}")"; then
	echo "ERROR: Failed to parse certificate" >&2
	exit 1
fi

# TBSCertificate starts at the second entry.
# Cf. https://tools.ietf.org/html/rfc3280#section-4.1
#
# Exemple of first lines printed by openssl asn1parse:
#    0:d=0  hl=4 l= 763 cons: SEQUENCE
#    4:d=1  hl=4 l= 483 cons: SEQUENCE
#    8:d=2  hl=2 l=   3 cons: cont [ 0 ]
#   10:d=3  hl=2 l=   1 prim: INTEGER           :02
#   13:d=2  hl=2 l=  20 prim: INTEGER           :3CEB2CB8818D968AC00EEFE195F0DF9665328B7B
#   35:d=2  hl=2 l=  13 cons: SEQUENCE
#   37:d=3  hl=2 l=   9 prim: OBJECT            :sha256WithRSAEncryption
RANGE_AND_DIGEST_RE='
2s/^\s*\([0-9]\+\):d=\s*[0-9]\+\s\+hl=\s*[0-9]\+\s\+l=\s*\([0-9]\+\)\s\+cons:\s*SEQUENCE\s*$/\1 \2/p;
7s/^\s*[0-9]\+:d=\s*[0-9]\+\s\+hl=\s*[0-9]\+\s\+l=\s*[0-9]\+\s\+prim:\s*OBJECT\s*:\(.*\)$/\1/p;
'

RANGE_AND_DIGEST=($(echo "${PEM}" | \
	openssl asn1parse -in - | \
	sed -n -e "${RANGE_AND_DIGEST_RE}"))

if [ "${#RANGE_AND_DIGEST[@]}" != 3 ]; then
	echo "ERROR: Failed to parse TBSCertificate." >&2
	exit 1
fi

OFFSET="${RANGE_AND_DIGEST[0]}"
END="$(( OFFSET + RANGE_AND_DIGEST[1] ))"
DIGEST="${RANGE_AND_DIGEST[2]}"

# The signature hash algorithm is used by Linux to blacklist certificates.
# Cf. crypto/asymmetric_keys/x509_cert_parser.c:x509_note_pkey_algo()
DIGEST_MATCH=""
while read -r DIGEST_ITEM; do
	if [ -z "${DIGEST_ITEM}" ]; then
		break
	fi
	if echo "${DIGEST}" | grep -qiF "${DIGEST_ITEM}"; then
		DIGEST_MATCH="${DIGEST_ITEM}"
		break
	fi
done < <(openssl list -digest-commands | tr ' ' '\n' | sort -ur)

if [ -z "${DIGEST_MATCH}" ]; then
	echo "ERROR: Unknown digest algorithm: ${DIGEST}" >&2
	exit 1
fi

echo "${PEM}" | \
	openssl x509 -in - -outform DER | \
	dd "bs=1" "skip=${OFFSET}" "count=${END}" "status=none" | \
	openssl dgst "-${DIGEST_MATCH}" - | \
	awk '{printf "tbs:" $2}'

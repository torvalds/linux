#!/bin/ksh

#
# Copyright (c) 2020, 2021 Joel Sing <jsing@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
# 

set -e
set -u
set -x

readonly SUBJECT="/CN=LibreSSL Test"

readonly TMPDIR=$(mktemp -d)

cleanup() {
        rm -rf "${TMPDIR}"
}

trap cleanup EXIT INT

reset() {
	echo '100001' > ${TMPDIR}/certserial
	cat /dev/null > ${TMPDIR}/certindex
}

setup() {
	reset

	cat > ${TMPDIR}/openssl.cnf <<EOF
[ca]
default_ca = test_ca

[test_ca]
new_certs_dir = ${TMPDIR}/
database = ${TMPDIR}/certindex
default_days = 365
default_md = sha256
policy = test_policy
serial = ${TMPDIR}/certserial

[test_policy]
countryName             = optional
stateOrProvinceName     = optional
localityName            = optional
organizationName        = optional
organizationalUnitName  = optional
commonName              = supplied
emailAddress            = optional

[v3_ca_root]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true
keyUsage = critical, cRLSign, keyCertSign

[v3_ca_int]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true
keyUsage = critical, cRLSign, keyCertSign

[v3_other]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature

[req]
distinguished_name      = req_distinguished_name

[ req_distinguished_name ]
EOF
}

key_type_to_args() {
	local key_type=$1

	alg=${key_type%:*}
	param=${key_type#*:}

	if [[ "${alg}" == "rsa" ]]; then
		echo "-newkey ${key_type}";
	elif [[ "${alg}" == "ec" ]]; then
		echo "-newkey $alg -pkeyopt ec_paramgen_curve:${param}"
	else
		echo "Unknown key type ${key_type}" >&2
		exit 1
	fi
}

create_root() {
	local name=$1 file=$2 key_type=$3

	key_args=$(key_type_to_args "${key_type}")

	openssl req -new -days 3650 -nodes ${key_args} -sha256 -x509 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -config ${TMPDIR}/openssl.cnf -extensions v3_ca_root \
	    -out "${TMPDIR}/${file}.crt"
}

create_intermediate() {
	local name=$1 file=$2 issuer_file=$3 key_type=$4

	key_args=$(key_type_to_args "${key_type}")

	openssl req -new -days 3650 -nodes ${key_args} -sha256 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -out "${TMPDIR}/${file}.csr"
	openssl x509 -req -days 3650 -CA "${TMPDIR}/${issuer_file}.crt" \
	    -CAkey "${TMPDIR}/${issuer_file}.key" -CAcreateserial \
	    -extfile ${TMPDIR}/openssl.cnf -extensions v3_ca_int \
	    -in "${TMPDIR}/${file}.csr" -out "${TMPDIR}/${file}.crt"
}

create_leaf() {
	local name=$1 file=$2 issuer_file=$3 key_type=$4

	key_args=$(key_type_to_args "${key_type}")

	openssl req -new -days 3650 -nodes ${key_args} -sha256 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -out "${TMPDIR}/${file}.csr"
	openssl x509 -req -days 3650 -CA "${TMPDIR}/${issuer_file}.crt" \
	    -CAkey "${TMPDIR}/${issuer_file}.key" -CAcreateserial -sha256 \
	    -extfile ${TMPDIR}/openssl.cnf -extensions v3_other \
	    -in "${TMPDIR}/${file}.csr" -out "${TMPDIR}/${file}.crt"
}

create_expired_leaf() {
	local name=$1 file=$2 issuer_file=$3 key_type=$4

	key_args=$(key_type_to_args "${key_type}")

	openssl req -new -days 3650 -nodes ${key_args} -sha256 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -out "${TMPDIR}/${file}.csr"
	openssl ca -batch -notext -cert "${TMPDIR}/${issuer_file}.crt" \
	    -keyfile "${TMPDIR}/${issuer_file}.key" \
	    -config ${TMPDIR}/openssl.cnf -extensions v3_other \
	    -startdate 20100101000000Z -enddate 20200101000000Z \
	    -in "${TMPDIR}/${file}.csr" -out "${TMPDIR}/${file}.crt"
}

create_revoked_leaf() {
	local name=$1 file=$2 issuer_file=$3 key_type=$4

	key_args=$(key_type_to_args "${key_type}")

	openssl req -new -days 3650 -nodes ${key_args} -sha256 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -out "${TMPDIR}/${file}.csr"
	openssl x509 -req -days 3650 -CA "${TMPDIR}/${issuer_file}.crt" \
	    -CAkey "${TMPDIR}/${issuer_file}.key" -CAcreateserial \
	    -extfile ${TMPDIR}/openssl.cnf -extensions v3_other \
	    -in "${TMPDIR}/${file}.csr" -out "${TMPDIR}/${file}.crt"
	openssl ca -cert "${TMPDIR}/${issuer_file}.crt" \
	    -keyfile "${TMPDIR}/${issuer_file}.key" \
	    -config "${TMPDIR}/openssl.cnf" -extensions v3_other \
	    -revoke "${TMPDIR}/${file}.crt"
	openssl ca -gencrl -cert "${TMPDIR}/${issuer_file}.crt" \
	    -keyfile "${TMPDIR}/${issuer_file}.key" \
	    -config "${TMPDIR}/openssl.cnf" -extensions v3_other \
	    -crldays 30 -out "${TMPDIR}/${issuer_file}.crl"
}

create_bundle() {
	local bundle_file=$1
	shift

	mkdir -p $(dirname ${bundle_file})
	cat /dev/null > ${bundle_file}

	for _cert_file in $@; do
		openssl x509 -nameopt oneline -subject -issuer \
		    -in "${TMPDIR}/${_cert_file}.crt" >> ${bundle_file}
	done
}

create_bundle_with_key() {
	local bundle_file=$1
	shift

	mkdir -p $(dirname ${bundle_file})
	cat /dev/null > ${bundle_file}

	for _cert_file in $@; do
		openssl x509 -nameopt oneline -subject -issuer -noout \
		    -in "${TMPDIR}/${_cert_file}.crt" >> ${bundle_file}
	done
	for _cert_file in $@; do
		cat "${TMPDIR}/${_cert_file}.crt" >> ${bundle_file}
	done
	for _key_file in $@; do
		cat "${TMPDIR}/${_key_file}.key" >> ${bundle_file}
	done
}

setup

reset
create_root "Root CA RSA" "ca-root-rsa" "rsa:2048"
create_intermediate "Intermediate CA RSA" "ca-int-rsa" "ca-root-rsa" "rsa:2048"
create_leaf "Server 1 RSA" "server-1-rsa" "ca-int-rsa" "rsa:2048"
create_expired_leaf "Server 2 RSA" "server-2-rsa" "ca-int-rsa" "rsa:2048"
create_revoked_leaf "Server 3 RSA" "server-3-rsa" "ca-int-rsa" "rsa:2048"
create_leaf "Client 1 RSA" "client-1-rsa" "ca-int-rsa" "rsa:2048"
create_expired_leaf "Client 2 RSA" "client-2-rsa" "ca-int-rsa" "rsa:2048"
create_revoked_leaf "Client 3 RSA" "client-3-rsa" "ca-int-rsa" "rsa:2048"

create_bundle "./ca-root-rsa.pem" "ca-root-rsa"
create_bundle "./ca-int-rsa.pem" "ca-int-rsa"
cp "${TMPDIR}/ca-int-rsa.crl" "./ca-int-rsa.crl"
create_bundle_with_key "./server1-rsa.pem" "server-1-rsa"
create_bundle "./server1-rsa-chain.pem" "server-1-rsa" "ca-int-rsa"
create_bundle_with_key "./server2-rsa.pem" "server-2-rsa"
create_bundle "./server2-rsa-chain.pem" "server-2-rsa" "ca-int-rsa"
create_bundle_with_key "./server3-rsa.pem" "server-3-rsa"
create_bundle "./server3-rsa-chain.pem" "server-3-rsa" "ca-int-rsa"
create_bundle_with_key "./client1-rsa.pem" "client-1-rsa"
create_bundle "./client1-rsa-chain.pem" "client-1-rsa" "ca-int-rsa"
create_bundle_with_key "./client2-rsa.pem" "client-2-rsa"
create_bundle "./client2-rsa-chain.pem" "client-2-rsa" "ca-int-rsa"
create_bundle_with_key "./client3-rsa.pem" "client-3-rsa"
create_bundle "./client3-rsa-chain.pem" "client-3-rsa" "ca-int-rsa"

reset
create_root "Root CA ECDSA" "ca-root-ecdsa" "ec:prime256v1"
create_intermediate "Intermediate CA ECDSA" "ca-int-ecdsa" "ca-root-ecdsa" "ec:prime256v1"
create_leaf "Server 1 ECDSA" "server-1-ecdsa" "ca-int-ecdsa" "ec:prime256v1"
create_expired_leaf "Server 2 ECDSA" "server-2-ecdsa" "ca-int-ecdsa" "ec:prime256v1"
create_revoked_leaf "Server 3 ECDSA" "server-3-ecdsa" "ca-int-ecdsa" "ec:prime256v1"
create_leaf "Client 1 ECDSA" "client-1-ecdsa" "ca-int-ecdsa" "ec:prime256v1"
create_expired_leaf "Client 2 ECDSA" "client-2-ecdsa" "ca-int-ecdsa" "ec:prime256v1"
create_revoked_leaf "Client 3 ECDSA" "client-3-ecdsa" "ca-int-ecdsa" "ec:prime256v1"

create_bundle "./ca-root-ecdsa.pem" "ca-root-ecdsa"
create_bundle "./ca-int-ecdsa.pem" "ca-int-ecdsa"
cp "${TMPDIR}/ca-int-ecdsa.crl" "./ca-int-ecdsa.crl"
create_bundle_with_key "./server1-ecdsa.pem" "server-1-ecdsa"
create_bundle "./server1-ecdsa-chain.pem" "server-1-ecdsa" "ca-int-ecdsa"
create_bundle_with_key "./server2-ecdsa.pem" "server-2-ecdsa"
create_bundle "./server2-ecdsa-chain.pem" "server-2-ecdsa" "ca-int-ecdsa"
create_bundle_with_key "./server3-ecdsa.pem" "server-3-ecdsa"
create_bundle "./server3-ecdsa-chain.pem" "server-3-ecdsa" "ca-int-ecdsa"
create_bundle_with_key "./client1-ecdsa.pem" "client-1-ecdsa"
create_bundle "./client1-ecdsa-chain.pem" "client-1-ecdsa" "ca-int-ecdsa"
create_bundle_with_key "./client2-ecdsa.pem" "client-2-ecdsa"
create_bundle "./client2-ecdsa-chain.pem" "client-2-ecdsa" "ca-int-ecdsa"
create_bundle_with_key "./client3-ecdsa.pem" "client-3-ecdsa"
create_bundle "./client3-ecdsa-chain.pem" "client-3-ecdsa" "ca-int-ecdsa"

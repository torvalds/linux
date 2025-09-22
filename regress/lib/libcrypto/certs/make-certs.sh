#!/bin/ksh

#
# Copyright (c) 2020 Joel Sing <jsing@openbsd.org>
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

create_root() {
	local name=$1 file=$2

	openssl req -new -days 3650 -nodes -newkey rsa:2048 -sha256 -x509 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -config ${TMPDIR}/openssl.cnf -extensions v3_ca_root \
	    -out "${TMPDIR}/${file}.crt"
}

create_expired_root() {
	local name=$1 file=$2

	openssl req -new -days 3650 -nodes -newkey rsa:2048 -sha256 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -config ${TMPDIR}/openssl.cnf -extensions v3_ca_root \
	    -out "${TMPDIR}/${file}.csr"
	openssl ca -batch -notext -selfsign \
	    -keyfile "${TMPDIR}/${file}.key" \
	    -startdate 20100101000000Z -enddate 20200101000000Z \
	    -config ${TMPDIR}/openssl.cnf -extensions v3_ca_root \
	    -in "${TMPDIR}/${file}.csr" -out "${TMPDIR}/${file}.crt"
}

create_intermediate() {
	local name=$1 file=$2 issuer_file=$3

	openssl req -new -days 3650 -nodes -newkey rsa:2048 -sha256 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -out "${TMPDIR}/${file}.csr"
	openssl x509 -req -days 3650 -CA "${TMPDIR}/${issuer_file}.crt" \
	    -CAkey "${TMPDIR}/${issuer_file}.key" -CAcreateserial \
	    -extfile ${TMPDIR}/openssl.cnf -extensions v3_ca_int \
	    -in "${TMPDIR}/${file}.csr" -out "${TMPDIR}/${file}.crt"
}

create_expired_intermediate() {
	local name=$1 file=$2 issuer_file=$3

	openssl req -new -days 3650 -nodes -newkey rsa:2048 -sha256 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -out "${TMPDIR}/${file}.csr"
	openssl ca -batch -notext -cert "${TMPDIR}/${issuer_file}.crt" \
	    -keyfile "${TMPDIR}/${issuer_file}.key" \
	    -startdate 20100101000000Z -enddate 20200101000000Z \
	    -config ${TMPDIR}/openssl.cnf -extensions v3_ca_int \
	    -in "${TMPDIR}/${file}.csr" -out "${TMPDIR}/${file}.crt"
}

create_leaf() {
	local name=$1 file=$2 issuer_file=$3

	openssl req -new -days 3650 -nodes -newkey rsa:2048 -sha256 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -out "${TMPDIR}/${file}.csr"
	openssl x509 -req -days 3650 -CA "${TMPDIR}/${issuer_file}.crt" \
	    -CAkey "${TMPDIR}/${issuer_file}.key" -CAcreateserial \
	    -extfile ${TMPDIR}/openssl.cnf -extensions v3_other \
	    -in "${TMPDIR}/${file}.csr" -out "${TMPDIR}/${file}.crt"
}

create_expired_leaf() {
	local name=$1 file=$2 issuer_file=$3

	openssl req -new -days 3650 -nodes -newkey rsa:2048 -sha256 \
	    -subj "${SUBJECT} ${name}" -keyout "${TMPDIR}/${file}.key" \
	    -out "${TMPDIR}/${file}.csr"
	openssl ca -batch -notext -cert "${TMPDIR}/${issuer_file}.crt" \
	    -keyfile "${TMPDIR}/${issuer_file}.key" \
	    -config ${TMPDIR}/openssl.cnf -extensions v3_other \
	    -startdate 20100101000000Z -enddate 20200101000000Z \
	    -in "${TMPDIR}/${file}.csr" -out "${TMPDIR}/${file}.crt"
}

create_cross_signed() {
	local file=$1 csr_file=$2 issuer_file=$3

	openssl x509 -req -days 3650 -CA "${TMPDIR}/${issuer_file}.crt" \
	    -CAkey "${TMPDIR}/${issuer_file}.key" -CAcreateserial \
	    -extfile ${TMPDIR}/openssl.cnf -extensions v3_ca_int \
	    -in "${TMPDIR}/${csr_file}.csr" -out "${TMPDIR}/${file}.crt"
}

create_expired_cross_signed() {
	local file=$1 csr_file=$2 issuer_file=$3

	openssl ca -batch -notext -cert "${TMPDIR}/${issuer_file}.crt" \
	    -keyfile "${TMPDIR}/${issuer_file}.key" \
	    -startdate 20100101000000Z -enddate 20200101000000Z \
	    -config ${TMPDIR}/openssl.cnf -extensions v3_ca_int \
	    -in "${TMPDIR}/${csr_file}.csr" -out "${TMPDIR}/${file}.crt"
}

create_bundle() {
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
}

create_root_bundle() {
	local bundle_file=$1
	shift

	mkdir -p $(dirname ${bundle_file})
	cat /dev/null > ${bundle_file}

	for _cert_file in $@; do
		openssl x509 -nameopt oneline -subject -issuer \
		    -in "${TMPDIR}/${_cert_file}.crt" >> ${bundle_file}
	done
}

setup

# Scenario 1a.
reset
create_root "Root CA 1" "ca-root"
create_leaf "Server 1" "server-1" "ca-root"
create_root_bundle "./1a/roots.pem" "ca-root"
create_bundle "./1a/bundle.pem" "server-1"

# Scenarios 2a and 2b.
reset
create_root "Root CA 1" "ca-root"
create_intermediate "Intermediate CA 1" "ca-int-1" "ca-root"
create_leaf "Server 1" "server-1" "ca-int-1"
create_root_bundle "./2a/roots.pem" "ca-root"
create_bundle "./2a/bundle.pem" "server-1" "ca-int-1"
create_root_bundle "./2b/roots.pem" "ca-root"
create_bundle "./2b/bundle.pem" "server-1"
create_root_bundle "./2c/roots.pem" "ca-root"
create_bundle "./2c/bundle.pem" "server-1" "ca-root" "ca-int-1"

# Scenarios 3a, 3b, 3c, 3d and 3e.
reset
create_root "Root CA 1" "ca-root"
create_intermediate "Intermediate CA 1" "ca-int-1" "ca-root"
create_intermediate "Intermediate CA 2" "ca-int-2" "ca-int-1"
create_intermediate "Intermediate CA 3" "ca-int-3" "ca-int-2"
create_leaf "Server 1" "server-1" "ca-int-3"
create_root_bundle "./3a/roots.pem" "ca-root"
create_bundle "./3a/bundle.pem" "server-1" "ca-int-3" "ca-int-2" "ca-int-1"
create_root_bundle "./3b/roots.pem" "ca-root"
create_bundle "./3b/bundle.pem" "server-1" "ca-int-3" "ca-int-2"
create_root_bundle "./3c/roots.pem" "ca-root"
create_bundle "./3c/bundle.pem" "server-1" "ca-int-3" "ca-int-1"
create_root_bundle "./3d/roots.pem" "ca-root"
create_bundle "./3d/bundle.pem" "server-1" "ca-int-2" "ca-int-1"
create_root_bundle "./3e/roots.pem" "ca-root"
create_bundle "./3e/bundle.pem" "server-1" "ca-int-1" "ca-int-2" "ca-int-3"

# Scenarios 4a, 4b, 4c, 4d, 4e, 4f, 4g and 4h.
reset
create_root "Root CA 1" "ca-root-1"
create_root "Root CA 2" "ca-root-2"
create_intermediate "Intermediate CA 1" "ca-int-1" "ca-root-1"
create_cross_signed "ca-int-1-xs" "ca-int-1" "ca-root-2"
create_leaf "Server 1" "server-1" "ca-int-1"
create_root_bundle "./4a/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./4a/bundle.pem" "server-1" "ca-int-1" "ca-int-1-xs"
create_root_bundle "./4b/roots.pem" "ca-root-1"
create_bundle "./4b/bundle.pem" "server-1" "ca-int-1" "ca-int-1-xs"
create_root_bundle "./4c/roots.pem" "ca-root-2"
create_bundle "./4c/bundle.pem" "server-1" "ca-int-1" "ca-int-1-xs"
create_root_bundle "./4d/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./4d/bundle.pem" "server-1" "ca-int-1"
create_root_bundle "./4e/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./4e/bundle.pem" "server-1" "ca-int-1-xs"
create_root_bundle "./4f/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./4f/bundle.pem" "server-1" "ca-int-1-xs" "ca-int-1"
create_root_bundle "./4g/roots.pem" "ca-root-1"
create_bundle "./4g/bundle.pem" "server-1" "ca-int-1-xs" "ca-int-1"
create_root_bundle "./4h/roots.pem" "ca-root-2"
create_bundle "./4h/bundle.pem" "server-1" "ca-int-1-xs" "ca-int-1"

# Scenario 5a, 5b, 5c, 5d, 5e, 5f, 5g, 5h and 5i.
reset
create_root "Root CA 1" "ca-root-1"
create_intermediate "Intermediate CA 1" "ca-int-1" "ca-root-1"
create_intermediate "Intermediate CA 2" "ca-int-2" "ca-int-1"
create_root "Root CA 2" "ca-root-2"
create_cross_signed "ca-int-2-xs" "ca-int-2" "ca-root-2"
create_leaf "Server 1" "server-1" "ca-int-2"
create_root_bundle "./5a/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./5a/bundle.pem" "server-1" "ca-int-2" "ca-int-2-xs" "ca-int-1"
create_root_bundle "./5b/roots.pem" "ca-root-2"
create_bundle "./5b/bundle.pem" "server-1" "ca-int-2" "ca-int-2-xs" "ca-int-1"
create_root_bundle "./5c/roots.pem" "ca-root-1"
create_bundle "./5c/bundle.pem" "server-1" "ca-int-2" "ca-int-2-xs" "ca-int-1"
create_root_bundle "./5d/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./5d/bundle.pem" "server-1" "ca-int-2-xs" "ca-int-1"
create_root_bundle "./5e/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./5e/bundle.pem" "server-1" "ca-int-2" "ca-int-2-xs"
create_root_bundle "./5f/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./5f/bundle.pem" "server-1" "ca-int-2" "ca-int-1"
create_root_bundle "./5g/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./5g/bundle.pem" "server-1" "ca-int-2-xs" "ca-int-2" "ca-int-1"
create_root_bundle "./5h/roots.pem" "ca-root-2"
create_bundle "./5h/bundle.pem" "server-1" "ca-int-2-xs" "ca-int-2" "ca-int-1"
create_root_bundle "./5i/roots.pem" "ca-root-1"
create_bundle "./5i/bundle.pem" "server-1" "ca-int-2-xs" "ca-int-2" "ca-int-1"

# Scenarios 6a and 6b.
reset
create_root "Root CA 1" "ca-root-1"
create_intermediate "Intermediate CA 1" "ca-int-1" "ca-root-1"
create_intermediate "Intermediate CA 2" "ca-int-2" "ca-int-1"
create_expired_root "Root CA 2" "ca-root-2"
create_cross_signed "ca-int-2-xs" "ca-int-2" "ca-root-2"
create_leaf "Server 1" "server-1" "ca-int-2"
create_root_bundle "./6a/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./6a/bundle.pem" "server-1" "ca-int-2" "ca-int-2-xs" "ca-int-1"
create_root_bundle "./6b/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./6b/bundle.pem" "server-1" "ca-int-2-xs" "ca-int-2" "ca-int-1"

# Scenarios 7a and 7b.
reset
create_expired_root "Root CA 1" "ca-root-1"
create_intermediate "Intermediate CA 1" "ca-int-1" "ca-root-1"
create_intermediate "Intermediate CA 2" "ca-int-2" "ca-int-1"
create_root "Root CA 2" "ca-root-2"
create_cross_signed "ca-int-2-xs" "ca-int-2" "ca-root-2"
create_leaf "Server 1" "server-1" "ca-int-2"
create_root_bundle "./7a/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./7a/bundle.pem" "server-1" "ca-int-2" "ca-int-2-xs" "ca-int-1"
create_root_bundle "./7b/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./7b/bundle.pem" "server-1" "ca-int-2-xs" "ca-int-2" "ca-int-1"

# Scenario 8a.
reset
create_root "Root CA 1" "ca-root"
create_intermediate "Intermediate CA 1" "ca-int-1" "ca-root"
create_expired_leaf "Server 1" "server-1" "ca-int-1"
create_root_bundle "./8a/roots.pem" "ca-root"
create_bundle "./8a/bundle.pem" "server-1" "ca-int-1"

# Scenario 9a.
reset
create_root "Root CA 1" "ca-root"
create_expired_intermediate "Intermediate CA 1" "ca-int-1" "ca-root"
create_leaf "Server 1" "server-1" "ca-int-1"
create_root_bundle "./9a/roots.pem" "ca-root"
create_bundle "./9a/bundle.pem" "server-1" "ca-int-1"

# Scenarios 10a and 10b.
reset
create_root "Root CA 1" "ca-root-1"
create_root "Root CA 2" "ca-root-2"
create_expired_intermediate "Intermediate CA 1" "ca-int-1" "ca-root-1"
create_cross_signed "ca-int-1-xs" "ca-int-1" "ca-root-2"
create_leaf "Server 1" "server-1" "ca-int-1"
create_root_bundle "./10a/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./10a/bundle.pem" "server-1" "ca-int-1" "ca-int-1-xs"
create_root_bundle "./10b/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./10b/bundle.pem" "server-1" "ca-int-1-xs" "ca-int-1"

# Scenarios 11a and 11b.
reset
create_root "Root CA 1" "ca-root-1"
create_expired_intermediate "Intermediate CA 1" "ca-int-1" "ca-root-1"
create_intermediate "Intermediate CA 2" "ca-int-2" "ca-int-1"
create_root "Root CA 2" "ca-root-2"
create_cross_signed "ca-int-2-xs" "ca-int-2" "ca-root-2"
create_leaf "Server 1" "server-1" "ca-int-2"
create_root_bundle "./11a/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./11a/bundle.pem" "server-1" "ca-int-2" "ca-int-2-xs" "ca-int-1"
create_root_bundle "./11b/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./11b/bundle.pem" "server-1" "ca-int-2-xs" "ca-int-2" "ca-int-1"

# Scenario 12a.
reset
create_root "Root CA 1" "ca-root-1"
create_expired_intermediate "Intermediate CA 1" "ca-int-1" "ca-root-1"
create_cross_signed "ca-root-2" "ca-int-1" "ca-root-1"
create_intermediate "Intermediate CA 2" "ca-int-2" "ca-int-1"
create_leaf "Server 1" "server-1" "ca-int-2"
create_root_bundle "./12a/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./12a/bundle.pem" "server-1" "ca-int-2" "ca-int-1"

# Scenario 13a.
reset
create_root "Root CA 1" "ca-root-1"
create_intermediate "Intermediate CA 1" "ca-int-1" "ca-root-1"
create_expired_cross_signed "ca-root-2" "ca-int-1" "ca-root-1"
create_intermediate "Intermediate CA 2" "ca-int-2" "ca-int-1"
create_leaf "Server 1" "server-1" "ca-int-2"
create_root_bundle "./13a/roots.pem" "ca-root-1" "ca-root-2"
create_bundle "./13a/bundle.pem" "server-1" "ca-int-2" "ca-int-1"

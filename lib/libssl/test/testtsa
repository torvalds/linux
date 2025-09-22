#!/bin/sh

#
# A few very basic tests for the 'ts' time stamping authority command.
#

SH="/bin/sh"
if test "$OSTYPE" = msdosdjgpp; then
    PATH="../apps\;$PATH"
else
    PATH="../apps:$PATH"
fi
export SH PATH

OPENSSL_CONF="../CAtsa.cnf"
export OPENSSL_CONF
# Because that's what ../apps/CA.sh really looks at
SSLEAY_CONFIG="-config $OPENSSL_CONF"
export SSLEAY_CONFIG

OPENSSL="`pwd`/../util/opensslwrap.sh"
export OPENSSL

error () {

    echo "TSA test failed!" >&2
    exit 1
}

setup_dir () {

    rm -rf tsa 2>/dev/null
    mkdir tsa
    cd ./tsa
}

clean_up_dir () {

    cd ..
    rm -rf tsa
}

create_ca () {

    echo "Creating a new CA for the TSA tests..."
    TSDNSECT=ts_ca_dn
    export TSDNSECT   
    ../../util/shlib_wrap.sh ../../apps/openssl req -new -x509 -nodes \
	-out tsaca.pem -keyout tsacakey.pem
    test $? != 0 && error
}

create_tsa_cert () {

    INDEX=$1
    export INDEX
    EXT=$2
    TSDNSECT=ts_cert_dn
    export TSDNSECT   

    ../../util/shlib_wrap.sh ../../apps/openssl req -new \
	-out tsa_req${INDEX}.pem -keyout tsa_key${INDEX}.pem
    test $? != 0 && error
echo Using extension $EXT
    ../../util/shlib_wrap.sh ../../apps/openssl x509 -req \
	-in tsa_req${INDEX}.pem -out tsa_cert${INDEX}.pem \
	-CA tsaca.pem -CAkey tsacakey.pem -CAcreateserial \
	-extfile $OPENSSL_CONF -extensions $EXT
    test $? != 0 && error
}

print_request () {

    ../../util/shlib_wrap.sh ../../apps/openssl ts -query -in $1 -text
}

create_time_stamp_request1 () {

    ../../util/shlib_wrap.sh ../../apps/openssl ts -query -data ../testtsa -policy tsa_policy1 -cert -out req1.tsq
    test $? != 0 && error
}

create_time_stamp_request2 () {

    ../../util/shlib_wrap.sh ../../apps/openssl ts -query -data ../testtsa -policy tsa_policy2 -no_nonce \
	-out req2.tsq
    test $? != 0 && error
}

create_time_stamp_request3 () {

    ../../util/shlib_wrap.sh ../../apps/openssl ts -query -data ../CAtsa.cnf -no_nonce -out req3.tsq
    test $? != 0 && error
}

print_response () {

    ../../util/shlib_wrap.sh ../../apps/openssl ts -reply -in $1 -text
    test $? != 0 && error
}

create_time_stamp_response () {

    ../../util/shlib_wrap.sh ../../apps/openssl ts -reply -section $3 -queryfile $1 -out $2
    test $? != 0 && error
}

time_stamp_response_token_test () {

    RESPONSE2=$2.copy.tsr
    TOKEN_DER=$2.token.der
    ../../util/shlib_wrap.sh ../../apps/openssl ts -reply -in $2 -out $TOKEN_DER -token_out
    test $? != 0 && error
    ../../util/shlib_wrap.sh ../../apps/openssl ts -reply -in $TOKEN_DER -token_in -out $RESPONSE2
    test $? != 0 && error
    cmp $RESPONSE2 $2
    test $? != 0 && error
    ../../util/shlib_wrap.sh ../../apps/openssl ts -reply -in $2 -text -token_out
    test $? != 0 && error
    ../../util/shlib_wrap.sh ../../apps/openssl ts -reply -in $TOKEN_DER -token_in -text -token_out
    test $? != 0 && error
    ../../util/shlib_wrap.sh ../../apps/openssl ts -reply -queryfile $1 -text -token_out
    test $? != 0 && error
}

verify_time_stamp_response () {

    ../../util/shlib_wrap.sh ../../apps/openssl ts -verify -queryfile $1 -in $2 -CAfile tsaca.pem \
	-untrusted tsa_cert1.pem
    test $? != 0 && error
    ../../util/shlib_wrap.sh ../../apps/openssl ts -verify -data $3 -in $2 -CAfile tsaca.pem \
	-untrusted tsa_cert1.pem
    test $? != 0 && error
}

verify_time_stamp_token () {

    # create the token from the response first
    ../../util/shlib_wrap.sh ../../apps/openssl ts -reply -in $2 -out $2.token -token_out
    test $? != 0 && error
    ../../util/shlib_wrap.sh ../../apps/openssl ts -verify -queryfile $1 -in $2.token -token_in \
	-CAfile tsaca.pem -untrusted tsa_cert1.pem
    test $? != 0 && error
    ../../util/shlib_wrap.sh ../../apps/openssl ts -verify -data $3 -in $2.token -token_in \
	-CAfile tsaca.pem -untrusted tsa_cert1.pem
    test $? != 0 && error
}

verify_time_stamp_response_fail () {

    ../../util/shlib_wrap.sh ../../apps/openssl ts -verify -queryfile $1 -in $2 -CAfile tsaca.pem \
	-untrusted tsa_cert1.pem
    # Checks if the verification failed, as it should have.
    test $? = 0 && error
    echo Ok
}

# main functions

echo "Setting up TSA test directory..."
setup_dir

echo "Creating CA for TSA tests..."
create_ca

echo "Creating tsa_cert1.pem TSA server cert..."
create_tsa_cert 1 tsa_cert

echo "Creating tsa_cert2.pem non-TSA server cert..."
create_tsa_cert 2 non_tsa_cert

echo "Creating req1.req time stamp request for file testtsa..."
create_time_stamp_request1

echo "Printing req1.req..."
print_request req1.tsq

echo "Generating valid response for req1.req..."
create_time_stamp_response req1.tsq resp1.tsr tsa_config1

echo "Printing response..."
print_response resp1.tsr

echo "Verifying valid response..."
verify_time_stamp_response req1.tsq resp1.tsr ../testtsa

echo "Verifying valid token..."
verify_time_stamp_token req1.tsq resp1.tsr ../testtsa

# The tests below are commented out, because invalid signer certificates
# can no longer be specified in the config file.

# echo "Generating _invalid_ response for req1.req..."
# create_time_stamp_response req1.tsq resp1_bad.tsr tsa_config2

# echo "Printing response..."
# print_response resp1_bad.tsr

# echo "Verifying invalid response, it should fail..."
# verify_time_stamp_response_fail req1.tsq resp1_bad.tsr

echo "Creating req2.req time stamp request for file testtsa..."
create_time_stamp_request2

echo "Printing req2.req..."
print_request req2.tsq

echo "Generating valid response for req2.req..."
create_time_stamp_response req2.tsq resp2.tsr tsa_config1

echo "Checking '-token_in' and '-token_out' options with '-reply'..."
time_stamp_response_token_test req2.tsq resp2.tsr

echo "Printing response..."
print_response resp2.tsr

echo "Verifying valid response..."
verify_time_stamp_response req2.tsq resp2.tsr ../testtsa

echo "Verifying response against wrong request, it should fail..."
verify_time_stamp_response_fail req1.tsq resp2.tsr

echo "Verifying response against wrong request, it should fail..."
verify_time_stamp_response_fail req2.tsq resp1.tsr

echo "Creating req3.req time stamp request for file CAtsa.cnf..."
create_time_stamp_request3

echo "Printing req3.req..."
print_request req3.tsq

echo "Verifying response against wrong request, it should fail..."
verify_time_stamp_response_fail req3.tsq resp1.tsr

echo "Cleaning up..."
clean_up_dir

exit 0

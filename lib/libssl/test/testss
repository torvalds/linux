#!/bin/sh

digest='-sha1'
reqcmd="../util/shlib_wrap.sh ../apps/openssl req"
x509cmd="../util/shlib_wrap.sh ../apps/openssl x509 $digest"
verifycmd="../util/shlib_wrap.sh ../apps/openssl verify"
dummycnf="../apps/openssl.cnf"

CAkey="keyCA.ss"
CAcert="certCA.ss"
CAreq="reqCA.ss"
CAconf="CAss.cnf"
CAreq2="req2CA.ss"	# temp

Uconf="Uss.cnf"
Ukey="keyU.ss"
Ureq="reqU.ss"
Ucert="certU.ss"

P1conf="P1ss.cnf"
P1key="keyP1.ss"
P1req="reqP1.ss"
P1cert="certP1.ss"
P1intermediate="tmp_intP1.ss"

P2conf="P2ss.cnf"
P2key="keyP2.ss"
P2req="reqP2.ss"
P2cert="certP2.ss"
P2intermediate="tmp_intP2.ss"

echo
echo "make a certificate request using 'req'"

echo "string to make the random number generator think it has entropy" >> ./.rnd

if ../util/shlib_wrap.sh ../apps/openssl no-rsa; then
  req_new='-newkey dsa:../apps/dsa512.pem'
else
  req_new='-new'
fi

$reqcmd -config $CAconf -out $CAreq -keyout $CAkey $req_new #>err.ss
if [ $? != 0 ]; then
	echo "error using 'req' to generate a certificate request"
	exit 1
fi
echo
echo "convert the certificate request into a self signed certificate using 'x509'"
$x509cmd -CAcreateserial -in $CAreq -days 30 -req -out $CAcert -signkey $CAkey -extfile $CAconf -extensions v3_ca >err.ss
if [ $? != 0 ]; then
	echo "error using 'x509' to self sign a certificate request"
	exit 1
fi

echo
echo "convert a certificate into a certificate request using 'x509'"
$x509cmd -in $CAcert -x509toreq -signkey $CAkey -out $CAreq2 >err.ss
if [ $? != 0 ]; then
	echo "error using 'x509' convert a certificate to a certificate request"
	exit 1
fi

$reqcmd -config $dummycnf -verify -in $CAreq -noout
if [ $? != 0 ]; then
	echo first generated request is invalid
	exit 1
fi

$reqcmd -config $dummycnf -verify -in $CAreq2 -noout
if [ $? != 0 ]; then
	echo second generated request is invalid
	exit 1
fi

$verifycmd -CAfile $CAcert $CAcert
if [ $? != 0 ]; then
	echo first generated cert is invalid
	exit 1
fi

echo
echo "make a user certificate request using 'req'"
$reqcmd -config $Uconf -out $Ureq -keyout $Ukey $req_new >err.ss
if [ $? != 0 ]; then
	echo "error using 'req' to generate a user certificate request"
	exit 1
fi

echo
echo "sign user certificate request with the just created CA via 'x509'"
$x509cmd -CAcreateserial -in $Ureq -days 30 -req -out $Ucert -CA $CAcert -CAkey $CAkey -extfile $Uconf -extensions v3_ee >err.ss
if [ $? != 0 ]; then
	echo "error using 'x509' to sign a user certificate request"
	exit 1
fi

$verifycmd -CAfile $CAcert $Ucert
echo
echo "Certificate details"
$x509cmd -subject -issuer -startdate -enddate -noout -in $Ucert

echo
echo "make a proxy certificate request using 'req'"
$reqcmd -config $P1conf -out $P1req -keyout $P1key $req_new >err.ss
if [ $? != 0 ]; then
	echo "error using 'req' to generate a proxy certificate request"
	exit 1
fi

echo
echo "sign proxy certificate request with the just created user certificate via 'x509'"
$x509cmd -CAcreateserial -in $P1req -days 30 -req -out $P1cert -CA $Ucert -CAkey $Ukey -extfile $P1conf -extensions v3_proxy >err.ss
if [ $? != 0 ]; then
	echo "error using 'x509' to sign a proxy certificate request"
	exit 1
fi

cat $Ucert > $P1intermediate
$verifycmd -CAfile $CAcert -untrusted $P1intermediate $P1cert
echo
echo "Certificate details"
$x509cmd -subject -issuer -startdate -enddate -noout -in $P1cert

echo
echo "make another proxy certificate request using 'req'"
$reqcmd -config $P2conf -out $P2req -keyout $P2key $req_new >err.ss
if [ $? != 0 ]; then
	echo "error using 'req' to generate another proxy certificate request"
	exit 1
fi

echo
echo "sign second proxy certificate request with the first proxy certificate via 'x509'"
$x509cmd -CAcreateserial -in $P2req -days 30 -req -out $P2cert -CA $P1cert -CAkey $P1key -extfile $P2conf -extensions v3_proxy >err.ss
if [ $? != 0 ]; then
	echo "error using 'x509' to sign a second proxy certificate request"
	exit 1
fi

cat $Ucert $P1cert > $P2intermediate
$verifycmd -CAfile $CAcert -untrusted $P2intermediate $P2cert
echo
echo "Certificate details"
$x509cmd -subject -issuer -startdate -enddate -noout -in $P2cert

echo
echo The generated CA certificate is $CAcert
echo The generated CA private key is $CAkey

echo The generated user certificate is $Ucert
echo The generated user private key is $Ukey

echo The first generated proxy certificate is $P1cert
echo The first generated proxy private key is $P1key

echo The second generated proxy certificate is $P2cert
echo The second generated proxy private key is $P2key

/bin/rm err.ss
#/bin/rm $P1intermediate
#/bin/rm $P2intermediate
exit 0

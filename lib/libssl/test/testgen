#!/bin/sh

T=testcert
KEY=512
CA=../certs/testca.pem

/bin/rm -f $T.1 $T.2 $T.key

if test "$OSTYPE" = msdosdjgpp; then
    PATH=../apps\;$PATH;
else
    PATH=../apps:$PATH;
fi
export PATH

echo "generating certificate request"

echo "string to make the random number generator think it has entropy" >> ./.rnd

if ../util/shlib_wrap.sh ../apps/openssl no-rsa; then
  req_new='-newkey dsa:../apps/dsa512.pem'
else
  req_new='-new'
  echo "There should be a 2 sequences of .'s and some +'s."
  echo "There should not be more that at most 80 per line"
fi

echo "This could take some time."

rm -f testkey.pem testreq.pem

../util/shlib_wrap.sh ../apps/openssl req -config test.cnf $req_new -out testreq.pem
if [ $? != 0 ]; then
echo problems creating request
exit 1
fi

../util/shlib_wrap.sh ../apps/openssl req -config test.cnf -verify -in testreq.pem -noout
if [ $? != 0 ]; then
echo signature on req is wrong
exit 1
fi

exit 0



function testit ()
{
 printf "%-30s: " $1
 ./rt-tester.py $1 | grep Pass
}

testit t2-l1-2rt-sameprio.tst
testit t2-l1-pi.tst
testit t2-l1-signal.tst
#testit t2-l2-2rt-deadlock.tst
testit t3-l1-pi-1rt.tst
testit t3-l1-pi-2rt.tst
testit t3-l1-pi-3rt.tst
testit t3-l1-pi-signal.tst
testit t3-l1-pi-steal.tst
testit t3-l2-pi.tst
testit t4-l2-pi-deboost.tst
testit t5-l4-pi-boost-deboost.tst
testit t5-l4-pi-boost-deboost-setsched.tst

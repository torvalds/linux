#!/bin/sh

bindir=$WORKAREA/host/.output/$ATH_PLATFORM/image

length=1530
length2=1522

randping()
{

flags="-q -v --random"

$bindir/mboxping $flags -i $NETIF -t 0 -r 0 -s $length  -p 3  -d $1  &
$bindir/mboxping $flags -i $NETIF -t 1 -r 1 -s $length  -p 3 -d $1  &
$bindir/mboxping $flags -i $NETIF -t 2 -r 2 -s $length  -p 3 -d $1  &
$bindir/mboxping $flags -i $NETIF -t 3 -r 3 -s $length  -p 3 -d $1  &

}

perfping() 
{
flags="-s 1522 -p 16 -d $1"
$bindir/mboxping -q -i $NETIF  -t 0  -r 0 $flags
$bindir/mboxping -q -i $NETIF  -t 1  -r 1 $flags
$bindir/mboxping -q -i $NETIF  -t 2  -r 2 $flags
$bindir/mboxping -q -i $NETIF  -t 3  -r 3 $flags

}

perfrxsingle()
{
$bindir/mboxping --rxperf -q $4 $5 -i $NETIF  -t $1  -r $1 -s $2  -p 16 -d $3 --rxburst=8
}

perftxsingle()
{
$bindir/mboxping --txperf -q $4 -i $NETIF  -t $1  -r $1 -s $2  -p 16 -d $3
}

perfrxverf()
{
perfrxsingle 0 $length2  4 -v $1
perfrxsingle 1 $length2  4 -v $1
perfrxsingle 2 $length2  4 -v $1
perfrxsingle 3 $length2  4 -v $1
}

perfrxrand()
{
perfrxverf --random
}

perfrx()
{
perfrxsingle 0 $length2 4
perfrxsingle 1 $length2 4
perfrxsingle 2 $length2 4
perfrxsingle 3 $length2 4
}

perftx()
{
perftxsingle 0 $length2  4 
perftxsingle 1 $length2  4
perftxsingle 2 $length2  4
perftxsingle 3 $length2  4
}

perftxqos()
{
perftxsingle 0 $length2  4 &
perftxsingle 1 $length2  4 &
perftxsingle 2 $length2  4 &
perftxsingle 3 $length2  4 &
sleep 6
}

perftxrand()
{
perftxsingle 0 $length2  8 --random
perftxsingle 1 $length2  8 --random
perftxsingle 2 $length2  8 --random
perftxsingle 3 $length2  8 --random
}

pingsimple()
{
$bindir/mboxping --delay -v -i  $NETIF  -t $1  -r $2  -s $3  -c 4
}

testheader()
{
echo " "
echo "========================================================================"
echo "**** $1 "
echo " "

}

testfooter()
{
echo "========================================================================"
echo " "
}

#wait for first test to finish
testheader "waiting $1 seconds for randomized packet test to complete"
sleep $1
sleep 4
testfooter

testheader "randomized no-delay testing....."
randping 10
sleep 14
testfooter

#make sure interface is still running at speed
testheader "testing high perf again...."
perfping 4
testfooter

sleep 1
testheader "testing continous rx - verify"
perfrxverf
testfooter

testheader "testing continous rx - random lengths"
perfrxrand
testfooter

testheader "testing continous rx performance"
perfrx
testfooter

testheader "testing continous tx - random lengths (packet loss expected) "
perftxrand
testfooter

testheader "testing continous tx performance (packet loss expected)"
perftx
testfooter

testheader "testing continous tx qos (packet loss expected)"
perftxqos
testfooter

#make sure interface is still running
testheader "testing with final pings ...."
pingsimple 0 0 1500
pingsimple 0 0 200
pingsimple 1 1 1500 
pingsimple 2 2 1500
pingsimple 3 3 1500
testfooter
 
echo "*************************************"
echo "****** HTC Regression Test Done *****"
echo "*************************************"







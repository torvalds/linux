#	$OpenBSD: regress.sh,v 1.7 2017/03/10 17:23:48 eric Exp $

. regress.subr

test_res_mkquery()
{
	for i in $@; do
		regress res_mkquery $i
	done
}

test_res_query()
{
	for i in $@; do
		regress res_query $i
		regress res_query -q $i
	done
}

test_getrrsetbyname()
{
	for i in $@; do
		regress getrrsetbyname $i
		regress getrrsetbyname -t MX $i
		regress getrrsetbyname -t AAAA $i
	done
}

test_gethostbyname()
{
	for i in $@; do
		regress gethostnamadr $i
		regress gethostnamadr -4 $i
		regress gethostnamadr -6 $i
	done
}

test_gethostbyaddr()
{
	for i in $@; do
		regress gethostnamadr -a $i
	done
}

test_getaddrinfo()
{
	for i in $@; do
		regress getaddrinfo $i
		regress getaddrinfo -C $i
		regress getaddrinfo -F $i
		regress getaddrinfo -CF $i
		regress getaddrinfo -P $i
		regress getaddrinfo -PF $i
		regress getaddrinfo -PC $i
		regress getaddrinfo -H $i
		regress getaddrinfo -p tcp $i
		regress getaddrinfo -p udp $i
		regress getaddrinfo -s www $i
		regress getaddrinfo -s bad $i
		regress getaddrinfo -S -s 8081 $i
		regress getaddrinfo -S -s bad $i
		regress getaddrinfo -P -s syslog $i
		regress getaddrinfo -P -s syslog -p tcp $i
		regress getaddrinfo -P -s syslog -p udp $i
	done
}

test_getaddrinfo2()
{
	for i in $@; do
		regress getaddrinfo -f inet6 -t raw -p icmpv6 $i
	done
}

test_getnameinfo()
{
	for i in $@; do
		regress getnameinfo $i
		regress getnameinfo -D $i
		regress getnameinfo -F $i
		regress getnameinfo -H $i
		regress getnameinfo -N $i
		regress getnameinfo -S $i
		regress getnameinfo -p 80 $i
		regress getnameinfo -p 514 $i
		regress getnameinfo -p 514 -D $i
		regress getnameinfo -p 5566 $i
	done
}

WEIRD="EMPTY . .. ..."
BASIC="localhost $(hostname -s) $(hostname)"
EXTRA="undeadly.org www.openbsd.org cvs.openbsd.org www.google.com www.bing.com"

ADDRS="0.0.0.0 :: 127.0.0.1 ::1 212.227.193.194"

for e in file bind local; do
	regress_setenv $e

	test_res_mkquery $WEIRD $BASIC
	test_res_query $WEIRD $BASIC $EXTRA
	test_getrrsetbyname $WEIRD $BASIC $EXTRA
	test_gethostbyname $WEIRD $BASIC $EXTRA
	test_gethostbyaddr $ADDRS
	test_getaddrinfo NULL $WEIRD $BASIC $EXTRA
	test_getaddrinfo2 undeadly.org www.kame.net
	test_getnameinfo $ADDRS
 	test_gethostbyname $ADDRS
done

regress_digest

#!/usr/bin/env bash
. testdata/common.sh
quiet=0
if test "$1" = "-q"; then
	quiet=1
	tdirarg="-q"
	shift
fi

NEED_SPLINT='00-lint.tdir'
NEED_DOXYGEN='01-doc.tdir'
NEED_XXD='fwd_compress_c00c.tdir fwd_zero.tdir'
NEED_NC='fwd_compress_c00c.tdir fwd_zero.tdir'
NEED_CURL='06-ianaports.tdir root_anchor.tdir'
NEED_WHOAMI='07-confroot.tdir'
NEED_IPV6='fwd_ancil.tdir fwd_tcp_tc6.tdir stub_udp6.tdir edns_cache.tdir'
NEED_NOMINGW='tcp_sigpipe.tdir 07-confroot.tdir 08-host-lib.tdir fwd_ancil.tdir'
NEED_DNSCRYPT_PROXY='dnscrypt_queries.tdir dnscrypt_queries_chacha.tdir'
NEED_UNSHARE='acl_interface.tdir proxy_protocol.tdir'
NEED_REDIS_SERVER='redis_replica.tdir'

# test if dig and ldns-testns are available.
test_tool_avail "dig"
test_tool_avail "ldns-testns"

# test for ipv6, uses streamtcp peculiarity.
if ./streamtcp -f ::1 2>&1 | grep "not supported" >/dev/null 2>&1; then
	HAVE_IPV6=no
else
	HAVE_IPV6=yes
fi

# test mingw. no signals and so on.
if uname | grep MINGW >/dev/null; then
	HAVE_MINGW=yes
else
	HAVE_MINGW=no
fi

# stop tests from notifying systemd, if that is compiled in.
export -n NOTIFY_SOCKET

cd testdata;
sh ../testcode/mini_tdir.sh $tdirarg clean
rm -f .perfstats.txt
for test in `ls -d *.tdir`; do
	SKIP=0
	skip_if_in_list $test "$NEED_SPLINT" "splint"
	skip_if_in_list $test "$NEED_DOXYGEN" "doxygen"
	skip_if_in_list $test "$NEED_CURL" "curl"
	skip_if_in_list $test "$NEED_XXD" "xxd"
	skip_if_in_list $test "$NEED_NC" "nc"
	skip_if_in_list $test "$NEED_WHOAMI" "whoami"
	skip_if_in_list $test "$NEED_DNSCRYPT_PROXY" "dnscrypt-proxy"
	skip_if_in_list $test "$NEED_UNSHARE" "unshare"
	skip_if_in_list $test "$NEED_REDIS_SERVER" "redis-server"

	if echo $NEED_IPV6 | grep $test >/dev/null; then
		if test "$HAVE_IPV6" = no; then
			SKIP=1;
		fi
	fi
	if echo $NEED_NOMINGW | grep $test >/dev/null; then
		if test "$HAVE_MINGW" = yes; then
			SKIP=1;
		fi
	fi
	if test $SKIP -eq 0; then
		echo $test
		sh ../testcode/mini_tdir.sh -a ../.. $tdirarg exe $test
	else
		echo "skip $test"
	fi
done
sh ../testcode/mini_tdir.sh $tdirarg report
cat .perfstats.txt

#!/usr/local/bin/bash
# run tdir tests from within a VM.  Looks for loopback addr.
# if run not from within a VM, runs the tests as usual.
# with one argument: run that tdir, otherwise, run all tdirs.

get_lo0_ip4() {
        if test -x /sbin/ifconfig
        then
                LO0_IP4=`/sbin/ifconfig lo0 | grep '[^0-9]127\.' | sed -e 's/^[^1]*\(127\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)[^0-9]*.*$/\1/g'`
                if ( echo $LO0_IP4 | grep '^127\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$' > /dev/null )
                then
                        return
                fi
        fi
        LO0_IP4=127.0.0.1
}
get_lo0_ip4
export LO0_IP4
if test "x$LO0_IP4" = "x127.0.0.1"
then
        ALT_LOOPBACK=false
else
        ALT_LOOPBACK=true
fi
cd testdata
TPKG=../testcode/mini_tdir.sh
#RUNLIST=`(ls -1d *.tdir|grep -v '^0[016]')`
RUNLIST=`(ls -1d *.tdir)`
if test "$#" = "1"; then
	RUNLIST="$1";
	if echo "$RUNLIST" | grep '/$' >/dev/null; then
		RUNLIST=`echo "$RUNLIST" | sed -e 's?/$??'`
	fi
fi

# fix up tdir that was edited on keyboard interrupt.
cleanup() {
	echo cleanup
	if test -f "$t.bak"; then rm -fr "${t}"; mv "$t.bak" "$t"; fi
	exit 0
}
trap cleanup INT
# stop tests from notifying systemd, if that is compiled in.
export -n NOTIFY_SOCKET

for t in $RUNLIST
do
	if ! $ALT_LOOPBACK
	then
		$TPKG exe $t
		continue
	fi
	# We have alternative 127.0.0.1 number
	if ( echo $t | grep '6\.tdir$' ) # skip IPv6 tests
	then
		continue
       	elif test "$t" = "edns_cache.tdir" # This one is IPv6 too!
	then
		continue
	fi
	cp -ap "$t" "$t.bak"
	find "${t}" -type f \
		-exec grep -q -e '127\.0\.0\.1' -e '@localhost' {} \; -print | {
		while read f
		do
			sed "s/127\.0\.0\.1/${LO0_IP4}/g" "$f" > "$f._"
			mv "$f._" "$f"
			sed "s/@localhost/@${LO0_IP4}/g" "$f" > "$f._"
			mv "$f._" "$f"
		done
	}
	find "${t}" -type d -name "127.0.0.1" -print | {
		while read d
		do
			mv -v "$d" "${d%127.0.0.1}${LO0_IP4}"
		done
	}
	$TPKG exe $t
	rm -fr "${t}"
	mv "$t.bak" "$t"
done
# get out of testdata/
cd ..

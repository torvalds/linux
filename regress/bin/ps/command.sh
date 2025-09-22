#!/bin/sh
#
# Copyright (c) 2015, 2018 Ingo Schwarze <schwarze@openbsd.org>
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

test_ps()
{
	args=$1
	ps_vars=$2
	ps_args=$3
	expected=$4
	if [ "X$args" = "X${args%=*}" ]; then
		if [ -n "$args" ]; then
			./shortsleep "$args" &
		else
			./shortsleep &
		fi
	else
		env -i $args ./shortsleep &
	fi
	pid=$!

	# Give the forked process some time to set up its process name.
	until ps -p $pid -o wchan | grep -q nanoslp; do :; done

	result=`env $ps_vars ps -p $pid $ps_args | tail -n +2`
	kill $pid
	if [ "$result" != "$expected" ]; then
		echo "$args & $ps_vars ps $ps_args"
		echo "expected: >$expected<"
		echo "result:   >$result<"
		exit 1;
	fi
}

unset LC_ALL

mypid=`printf %5d $$`

# not in the last column, limited width
test_ps "" "" "-o command,ppid" "./shortsleep     $mypid"
test_ps "" "" "-c -o command,ppid" "shortsleep       $mypid"
test_ps "E=1" "" "-eo command,ppid" "E=1 ./shortsleep $mypid"
test_ps "E=1" "" "-ceo command,ppid" "E=1 shortsleep   $mypid"
test_ps "long_argument" "" "-o command,ppid" "./shortsleep lon $mypid"
test_ps "long_argument" "" "-co command,ppid" "shortsleep       $mypid"
test_ps "E=long" "" "-eo command,ppid" "E=long ./shortsl $mypid"
test_ps "E=long" "" "-ceo command,ppid" "E=long shortslee $mypid"
test_ps "E=1 L=very_long_var" "" "-eo command,ppid" "E=1 L=very_long_ $mypid"
test_ps "E=1 L=very_long_var" "" "-ceo command,ppid" "E=1 L=very_long_ $mypid"

# not in the last column, unlimited width
test_ps "" "" "-wwo command,ppid" "./shortsleep     $mypid"
test_ps "" "" "-cwwo command,ppid" "shortsleep       $mypid"
test_ps "E=1" "" "-ewwo command,ppid" "E=1 ./shortsleep $mypid"
test_ps "E=1" "" "-cewwo command,ppid" "E=1 shortsleep   $mypid"
test_ps "long_argument" "" "-wwo command,ppid" "./shortsleep lon $mypid"
test_ps "long_argument" "" "-cwwo command,ppid" "shortsleep       $mypid"
test_ps "E=long" "" "-ewwo command,ppid" "E=long ./shortsl $mypid"
test_ps "E=long" "" "-cewwo command,ppid" "E=long shortslee $mypid"
test_ps "E=1 L=very_long_var" "" "-ewwo command,ppid" \
	"E=1 L=very_long_ $mypid"
test_ps "E=1 L=very_long_var" "" "-cewwo command,ppid" \
	"E=1 L=very_long_ $mypid"

# UTF-8
#width 1
test_ps "ñﾝ𝛁" \
	"LC_CTYPE=en_US.UTF-8" "-wwo command,ppid" \
	"./shortsleep ñﾝ𝛁 $mypid"
# width 0 (combining)
test_ps "x̀" "LC_CTYPE=en_US.UTF-8" "-wwo command,ppid" \
	"./shortsleep x̀   $mypid"
# width 2 (east asian)
test_ps "쿿" "LC_CTYPE=en_US.UTF-8" "-wwo command,ppid" \
	"./shortsleep 쿿  $mypid"
# non-printable
test_ps "͸" "LC_CTYPE=en_US.UTF-8" "-wwo command,ppid" \
	"./shortsleep �   $mypid"

# UTF-8 in the C locale
test_ps "E=ñ" "LC_CTYPE=C" "-ewwo command,ppid" \
	"E=\M-C\M-1 ./sho $mypid"
test_ps "E=ﾝ" "LC_CTYPE=C" "-ewwo command,ppid" \
	"E=\M-o\M->\M^] . $mypid"
test_ps "E=x̀" "LC_CTYPE=C" "-ewwo command,ppid" \
	"E=x\M-L\M^@ ./sh $mypid"
test_ps "E=쿿" "LC_CTYPE=C" "-ewwo command,ppid" \
	"E=\M-l\M-?\M-? . $mypid"

# invalid 8-bit bytes
test_ps "E=x�x" "" "-ewwo command,ppid" "E=x\M^?x ./short $mypid"
test_ps "E=x�x" "" "-ewwo command,ppid" "E=x\M-1x ./short $mypid"
test_ps "E=x�x" "" "-ewwo command,ppid" "E=x\M-Cx ./short $mypid"
test_ps "E=x�x" "" "-ewwo command,ppid" "E=x\M-o\M->x ./s $mypid"

# in the last column, limited width
test_ps "" "" "-o command" "./shortsleep"
test_ps "" "" "-co command" "shortsleep"
test_ps "" "COLUMNS=4" "-o command" "./sh"
test_ps "" "COLUMNS=4" "-co command" "shor"
test_ps "" "COLUMNS=10" "-o ppid,command" "$mypid ./sh"
test_ps "" "COLUMNS=10" "-co ppid,command" "$mypid shor"
test_ps "" "COLUMNS=4" "-o ppid,command" "$mypid ./shortsleep"
test_ps "" "COLUMNS=4" "-co ppid,command" "$mypid shortsleep"
test_ps "long_arg" "COLUMNS=4" "-o ppid,command" "$mypid ./shortsleep lon"
test_ps "long_arg" "COLUMNS=4" "-co ppid,command" "$mypid shortsleep"
test_ps "E=1" "" "-eo command" "E=1 ./shortsleep"
test_ps "E=1" "" "-ceo command" "E=1 shortsleep"
test_ps "E=1" "COLUMNS=7" "-eo command" "E=1 ./s"
test_ps "E=1" "COLUMNS=6" "-eo command" "E=1 ./"
test_ps "E=1" "COLUMNS=5" "-eo command" "E=1 ."
test_ps "E=1" "COLUMNS=4" "-eo command" "E=1 "
test_ps "E=1" "COLUMNS=3" "-eo command" "E=1"
test_ps "E=1" "COLUMNS=2" "-eo command" "E="
test_ps "E=1" "COLUMNS=5" "-ceo command" "E=1 s"
test_ps "E=1" "COLUMNS=4" "-ceo command" "E=1 "
test_ps "E=1" "COLUMNS=3" "-ceo command" "E=1"
test_ps "E=1" "COLUMNS=2" "-ceo command" "E="

# in the last column, unlimited width
test_ps "" "" "-wwo command" "./shortsleep"
test_ps "" "" "-cwwo command" "shortsleep"
test_ps "long_argument" "" "-wwo command" "./shortsleep long_argument"
test_ps "long_argument" "" "-cwwo command" "shortsleep"
test_ps "E=1" "" "-ewwo command" "E=1 ./shortsleep"
test_ps "E=1" "" "-cewwo command" "E=1 shortsleep"
test_ps "E=1 L=very_long_var" "" "-ewwo command" \
	"E=1 L=very_long_var ./shortsleep"
test_ps "E=1 L=very_long_var" "" "-cewwo command" \
	"E=1 L=very_long_var shortsleep"

# test vis(3)ing
test_ps "" "" "-o command" "./shortsleep \\^A\\^B\\^C"
test_ps "" "" "-o command" "./shortsleep \\^D\\^E\\^F\\a"
test_ps "	x
" "" "-o command" "./shortsleep \\b\\tx\\n\\v"
test_ps "" "" "-o command" "./shortsleep \\f\\r\\^N\\^O"
test_ps "" "" "-o command" "./shortsleep \\^P\\^Q\\^R\\^S"
test_ps "" "" "-o command" "./shortsleep \\^T\\^U\\^V\\^W"
test_ps "" "" "-o command" "./shortsleep \\^X\\^Y\\^Z\\^["
test_ps "" "" "-o command" "./shortsleep \\^\\\\^]\\^^\\^_"
test_ps "x x" "" "-o command" "./shortsleep x x"
test_ps "" "" "-o command" "./shortsleep \\^?"

exit 0

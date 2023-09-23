#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Check that audit logs generated for nft commands are as expected.

SKIP_RC=4
RC=0

nft --version >/dev/null 2>&1 || {
	echo "SKIP: missing nft tool"
	exit $SKIP_RC
}

logfile=$(mktemp)
echo "logging into $logfile"
./audit_logread >"$logfile" &
logread_pid=$!
trap 'kill $logread_pid; rm -f $logfile' EXIT
exec 3<"$logfile"

do_test() { # (cmd, log)
	echo -n "testing for cmd: $1 ... "
	cat <&3 >/dev/null
	$1 >/dev/null || exit 1
	sleep 0.1
	res=$(diff -a -u <(echo "$2") - <&3)
	[ $? -eq 0 ] && { echo "OK"; return; }
	echo "FAIL"
	echo "$res"
	((RC++))
}

nft flush ruleset

for table in t1 t2; do
	do_test "nft add table $table" \
	"table=$table family=2 entries=1 op=nft_register_table"

	do_test "nft add chain $table c1" \
	"table=$table family=2 entries=1 op=nft_register_chain"

	do_test "nft add chain $table c2; add chain $table c3" \
	"table=$table family=2 entries=2 op=nft_register_chain"

	cmd="add rule $table c1 counter"

	do_test "nft $cmd" \
	"table=$table family=2 entries=1 op=nft_register_rule"

	do_test "nft $cmd; $cmd" \
	"table=$table family=2 entries=2 op=nft_register_rule"

	cmd=""
	sep=""
	for chain in c2 c3; do
		for i in {1..3}; do
			cmd+="$sep add rule $table $chain counter"
			sep=";"
		done
	done
	do_test "nft $cmd" \
	"table=$table family=2 entries=6 op=nft_register_rule"
done

do_test 'nft reset rules t1 c2' \
'table=t1 family=2 entries=3 op=nft_reset_rule'

do_test 'nft reset rules table t1' \
'table=t1 family=2 entries=3 op=nft_reset_rule
table=t1 family=2 entries=3 op=nft_reset_rule
table=t1 family=2 entries=3 op=nft_reset_rule'

do_test 'nft reset rules' \
'table=t1 family=2 entries=3 op=nft_reset_rule
table=t1 family=2 entries=3 op=nft_reset_rule
table=t1 family=2 entries=3 op=nft_reset_rule
table=t2 family=2 entries=3 op=nft_reset_rule
table=t2 family=2 entries=3 op=nft_reset_rule
table=t2 family=2 entries=3 op=nft_reset_rule'

for ((i = 0; i < 500; i++)); do
	echo "add rule t2 c3 counter accept comment \"rule $i\""
done | do_test 'nft -f -' \
'table=t2 family=2 entries=500 op=nft_register_rule'

do_test 'nft reset rules t2 c3' \
'table=t2 family=2 entries=189 op=nft_reset_rule
table=t2 family=2 entries=188 op=nft_reset_rule
table=t2 family=2 entries=126 op=nft_reset_rule'

do_test 'nft reset rules t2' \
'table=t2 family=2 entries=3 op=nft_reset_rule
table=t2 family=2 entries=3 op=nft_reset_rule
table=t2 family=2 entries=186 op=nft_reset_rule
table=t2 family=2 entries=188 op=nft_reset_rule
table=t2 family=2 entries=129 op=nft_reset_rule'

do_test 'nft reset rules' \
'table=t1 family=2 entries=3 op=nft_reset_rule
table=t1 family=2 entries=3 op=nft_reset_rule
table=t1 family=2 entries=3 op=nft_reset_rule
table=t2 family=2 entries=3 op=nft_reset_rule
table=t2 family=2 entries=3 op=nft_reset_rule
table=t2 family=2 entries=180 op=nft_reset_rule
table=t2 family=2 entries=188 op=nft_reset_rule
table=t2 family=2 entries=135 op=nft_reset_rule'

exit $RC

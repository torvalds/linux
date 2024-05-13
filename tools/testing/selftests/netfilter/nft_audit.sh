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

# Run everything in a separate network namespace
[ "${1}" != "run" ] && { unshare -n "${0}" run; exit $?; }

# give other scripts a chance to finish - audit_logread sees all activity
sleep 1

logfile=$(mktemp)
rulefile=$(mktemp)
echo "logging into $logfile"
./audit_logread >"$logfile" &
logread_pid=$!
trap 'kill $logread_pid; rm -f $logfile $rulefile' EXIT
exec 3<"$logfile"

do_test() { # (cmd, log)
	echo -n "testing for cmd: $1 ... "
	cat <&3 >/dev/null
	$1 >/dev/null || exit 1
	sleep 0.1
	res=$(diff -a -u <(echo "$2") - <&3)
	[ $? -eq 0 ] && { echo "OK"; return; }
	echo "FAIL"
	grep -v '^\(---\|+++\|@@\)' <<< "$res"
	((RC--))
}

nft flush ruleset

# adding tables, chains and rules

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

for ((i = 0; i < 500; i++)); do
	echo "add rule t2 c3 counter accept comment \"rule $i\""
done >$rulefile
do_test "nft -f $rulefile" \
'table=t2 family=2 entries=500 op=nft_register_rule'

# adding sets and elements

settype='type inet_service; counter'
setelem='{ 22, 80, 443 }'
setblock="{ $settype; elements = $setelem; }"
do_test "nft add set t1 s $setblock" \
"table=t1 family=2 entries=4 op=nft_register_set"

do_test "nft add set t1 s2 $setblock; add set t1 s3 { $settype; }" \
"table=t1 family=2 entries=5 op=nft_register_set"

do_test "nft add element t1 s3 $setelem" \
"table=t1 family=2 entries=3 op=nft_register_setelem"

# adding counters

do_test 'nft add counter t1 c1' \
'table=t1 family=2 entries=1 op=nft_register_obj'

do_test 'nft add counter t2 c1; add counter t2 c2' \
'table=t2 family=2 entries=2 op=nft_register_obj'

for ((i = 3; i <= 500; i++)); do
	echo "add counter t2 c$i"
done >$rulefile
do_test "nft -f $rulefile" \
'table=t2 family=2 entries=498 op=nft_register_obj'

# adding/updating quotas

do_test 'nft add quota t1 q1 { 10 bytes }' \
'table=t1 family=2 entries=1 op=nft_register_obj'

do_test 'nft add quota t2 q1 { 10 bytes }; add quota t2 q2 { 10 bytes }' \
'table=t2 family=2 entries=2 op=nft_register_obj'

for ((i = 3; i <= 500; i++)); do
	echo "add quota t2 q$i { 10 bytes }"
done >$rulefile
do_test "nft -f $rulefile" \
'table=t2 family=2 entries=498 op=nft_register_obj'

# changing the quota value triggers obj update path
do_test 'nft add quota t1 q1 { 20 bytes }' \
'table=t1 family=2 entries=1 op=nft_register_obj'

# resetting rules

do_test 'nft reset rules t1 c2' \
'table=t1 family=2 entries=3 op=nft_reset_rule'

do_test 'nft reset rules table t1' \
'table=t1 family=2 entries=3 op=nft_reset_rule
table=t1 family=2 entries=3 op=nft_reset_rule
table=t1 family=2 entries=3 op=nft_reset_rule'

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

# resetting sets and elements

elem=(22 ,80 ,443)
relem=""
for i in {1..3}; do
	relem+="${elem[((i - 1))]}"
	do_test "nft reset element t1 s { $relem }" \
	"table=t1 family=2 entries=$i op=nft_reset_setelem"
done

do_test 'nft reset set t1 s' \
'table=t1 family=2 entries=3 op=nft_reset_setelem'

# resetting counters

do_test 'nft reset counter t1 c1' \
'table=t1 family=2 entries=1 op=nft_reset_obj'

do_test 'nft reset counters t1' \
'table=t1 family=2 entries=1 op=nft_reset_obj'

do_test 'nft reset counters t2' \
'table=t2 family=2 entries=342 op=nft_reset_obj
table=t2 family=2 entries=158 op=nft_reset_obj'

do_test 'nft reset counters' \
'table=t1 family=2 entries=1 op=nft_reset_obj
table=t2 family=2 entries=341 op=nft_reset_obj
table=t2 family=2 entries=159 op=nft_reset_obj'

# resetting quotas

do_test 'nft reset quota t1 q1' \
'table=t1 family=2 entries=1 op=nft_reset_obj'

do_test 'nft reset quotas t1' \
'table=t1 family=2 entries=1 op=nft_reset_obj'

do_test 'nft reset quotas t2' \
'table=t2 family=2 entries=315 op=nft_reset_obj
table=t2 family=2 entries=185 op=nft_reset_obj'

do_test 'nft reset quotas' \
'table=t1 family=2 entries=1 op=nft_reset_obj
table=t2 family=2 entries=314 op=nft_reset_obj
table=t2 family=2 entries=186 op=nft_reset_obj'

# deleting rules

readarray -t handles < <(nft -a list chain t1 c1 | \
			 sed -n 's/.*counter.* handle \(.*\)$/\1/p')

do_test "nft delete rule t1 c1 handle ${handles[0]}" \
'table=t1 family=2 entries=1 op=nft_unregister_rule'

cmd='delete rule t1 c1 handle'
do_test "nft $cmd ${handles[1]}; $cmd ${handles[2]}" \
'table=t1 family=2 entries=2 op=nft_unregister_rule'

do_test 'nft flush chain t1 c2' \
'table=t1 family=2 entries=3 op=nft_unregister_rule'

do_test 'nft flush table t2' \
'table=t2 family=2 entries=509 op=nft_unregister_rule'

# deleting chains

do_test 'nft delete chain t2 c2' \
'table=t2 family=2 entries=1 op=nft_unregister_chain'

# deleting sets and elements

do_test 'nft delete element t1 s { 22 }' \
'table=t1 family=2 entries=1 op=nft_unregister_setelem'

do_test 'nft delete element t1 s { 80, 443 }' \
'table=t1 family=2 entries=2 op=nft_unregister_setelem'

do_test 'nft flush set t1 s2' \
'table=t1 family=2 entries=3 op=nft_unregister_setelem'

do_test 'nft delete set t1 s2' \
'table=t1 family=2 entries=1 op=nft_unregister_set'

do_test 'nft delete set t1 s3' \
'table=t1 family=2 entries=1 op=nft_unregister_set'

exit $RC

#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

echo "Boot config test script"

if [ -d "$1" ]; then
  TESTDIR=$1
else
  TESTDIR=.
fi
BOOTCONF=${TESTDIR}/bootconfig
ALIGN=4

INITRD=`mktemp ${TESTDIR}/initrd-XXXX`
TEMPCONF=`mktemp ${TESTDIR}/temp-XXXX.bconf`
OUTFILE=`mktemp ${TESTDIR}/tempout-XXXX`
NG=0

cleanup() {
  rm -f $INITRD $TEMPCONF $OUTFILE
  exit $NG
}

trap cleanup EXIT TERM

NO=1

xpass() { # pass test command
  echo "test case $NO ($*)... "
  if ! ($@ && printf "\t\t[OK]\n"); then
     printf "\t\t[NG]\n"; NG=$((NG + 1))
  fi
  NO=$((NO + 1))
}

xfail() { # fail test command
  echo "test case $NO ($*)... "
  if ! (! $@ && printf "\t\t[OK]\n"); then
     printf "\t\t[NG]\n"; NG=$((NG + 1))
  fi
  NO=$((NO + 1))
}

echo "Basic command test"
xpass $BOOTCONF $INITRD

echo "Delete command should success without bootconfig"
xpass $BOOTCONF -d $INITRD

dd if=/dev/zero of=$INITRD bs=4096 count=1
printf "key = value;" > $TEMPCONF
bconf_size=$(wc -c < $TEMPCONF)
initrd_size=$(wc -c < $INITRD)

echo "Apply command test"
xpass $BOOTCONF -a $TEMPCONF $INITRD
new_size=$(wc -c < $INITRD)

echo "Show command test"
xpass $BOOTCONF $INITRD

echo "File size check"
total_size=$(expr $bconf_size + $initrd_size + 9 + 12 + $ALIGN - 1 )
total_size=$(expr $total_size / $ALIGN)
total_size=$(expr $total_size \* $ALIGN)
xpass test $new_size -eq $total_size

echo "Apply command repeat test"
xpass $BOOTCONF -a $TEMPCONF $INITRD

echo "File size check"
xpass test $new_size -eq $(wc -c < $INITRD)

echo "Delete command check"
xpass $BOOTCONF -d $INITRD

echo "File size check"
new_size=$(wc -c < $INITRD)
xpass test $new_size -eq $initrd_size

echo "No error messge while applying"
dd if=/dev/zero of=$INITRD bs=4096 count=1
printf " \0\0\0 \0\0\0" >> $INITRD
$BOOTCONF -a $TEMPCONF $INITRD > $OUTFILE 2>&1
xfail grep -i "failed" $OUTFILE
xfail grep -i "error" $OUTFILE

echo "Max node number check"

awk '
BEGIN {
  for (i = 0; i < 26; i += 1)
      printf("%c\n", 65 + i % 26)
  for (i = 26; i < 8192; i += 1)
      printf("%c%c%c\n", 65 + i % 26, 65 + (i / 26) % 26, 65 + (i / 26 / 26))
}
' > $TEMPCONF
xpass $BOOTCONF -a $TEMPCONF $INITRD

printf "badnode\n" >> $TEMPCONF
xfail $BOOTCONF -a $TEMPCONF $INITRD

echo "Max key depth check"

printf 'a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p = "value"\n' > $TEMPCONF
xpass $BOOTCONF -a $TEMPCONF $INITRD
$BOOTCONF -l $INITRD > $OUTFILE
xpass cmp $TEMPCONF $OUTFILE

printf 'a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p.q = "value"\n' > $TEMPCONF
xfail $BOOTCONF -a $TEMPCONF $INITRD

echo "Max filesize check"

# Max size is 32767 (including terminal byte)
printf "data = \"" > $TEMPCONF
dd if=/dev/urandom bs=768 count=32 | base64 -w0 >> $TEMPCONF
printf "\"\n" >> $TEMPCONF
xfail $BOOTCONF -a $TEMPCONF $INITRD

dd if=$TEMPCONF of=$OUTFILE bs=1 count=32765
cp $OUTFILE $TEMPCONF
printf "\"\n" >> $TEMPCONF	# 32767 bytes + terminal ('\0') = 32768 (> max)
xfail $BOOTCONF -a $TEMPCONF $INITRD

dd if=$TEMPCONF of=$OUTFILE bs=1 count=32764
cp $OUTFILE $TEMPCONF
printf "\"\n" >> $TEMPCONF	# add 2 bytes + terminal ('\"\n\0') = 32767
# Test max size across all 4 alignment paddings (0..3 bytes)
for sz in 4096 4097 4098 4099; do
  dd if=/dev/zero of=$INITRD bs=1 count=$sz 2>/dev/null
  xpass $BOOTCONF -a $TEMPCONF $INITRD
  $BOOTCONF -l $INITRD > $OUTFILE
  xpass cmp $TEMPCONF $OUTFILE
  xpass $BOOTCONF -d $INITRD
  xpass test $(wc -c < $INITRD) -eq $sz
done

echo "Adding same-key values"
cat > $TEMPCONF << EOF
key = bar, baz
key += qux
EOF
echo > $INITRD

xpass $BOOTCONF -a $TEMPCONF $INITRD
$BOOTCONF $INITRD > $OUTFILE
xpass grep -q "bar" $OUTFILE
xpass grep -q "baz" $OUTFILE
xpass grep -q "qux" $OUTFILE

echo "Override same-key values"
cat > $TEMPCONF << EOF
key = bar, baz
key := qux
EOF
echo > $INITRD

xpass $BOOTCONF -a $TEMPCONF $INITRD
$BOOTCONF $INITRD > $OUTFILE
xfail grep -q "bar" $OUTFILE
xfail grep -q "baz" $OUTFILE
xpass grep -q "qux" $OUTFILE

echo "Double/single quotes test"
printf "key = '\"string\"';" > $TEMPCONF
$BOOTCONF -a $TEMPCONF $INITRD
$BOOTCONF $INITRD > $TEMPCONF
cat $TEMPCONF
xpass grep \'\"string\"\' $TEMPCONF

echo "Repeat same-key tree"
cat > $TEMPCONF << EOF
foo
bar
foo { buz }
EOF
echo > $INITRD

xpass $BOOTCONF -a $TEMPCONF $INITRD
$BOOTCONF $INITRD > $OUTFILE
xpass grep -q "bar" $OUTFILE


echo "Remove/keep tailing spaces"
cat > $TEMPCONF << EOF
foo = val     # comment
bar = "val2 " # comment
EOF
echo > $INITRD

xpass $BOOTCONF -a $TEMPCONF $INITRD
$BOOTCONF $INITRD > $OUTFILE
xfail grep -q 'val[[:space:]]' $OUTFILE
xpass grep -q 'val2[[:space:]]' $OUTFILE

echo "Remove unquoted trailing whitespace at end of file"
printf 'foo = val \t' > $TEMPCONF
$BOOTCONF -l $TEMPCONF > $OUTFILE
printf 'foo = "val"\n' > $TEMPCONF
xpass cmp $TEMPCONF $OUTFILE

echo "Remove trailing whitespace from the last array value at end of file"
printf 'foo = first, last \t' > $TEMPCONF
$BOOTCONF -l $TEMPCONF > $OUTFILE
printf 'foo = "first", "last"\n' > $TEMPCONF
xpass cmp $TEMPCONF $OUTFILE

echo "Keep quoted trailing whitespace at end of file"
printf 'foo = "val "' > $TEMPCONF
$BOOTCONF -l $TEMPCONF > $OUTFILE
printf 'foo = "val "\n' > $TEMPCONF
xpass cmp $TEMPCONF $OUTFILE

echo "Showing correct line:column of no closing brace"
cat > $TEMPCONF << EOF
foo {
bar {
}
EOF
$BOOTCONF -a $TEMPCONF $INITRD 2> $OUTFILE
xpass grep -q "1:1" $OUTFILE

echo "Intermediate null character test"
printf "key = value\n\0extra = data\n" > $TEMPCONF
xfail $BOOTCONF -a $TEMPCONF $INITRD
$BOOTCONF -a $TEMPCONF $INITRD 2> $OUTFILE
xpass grep -q "Unexpected" $OUTFILE

echo "Trailing null character test"
printf "key = value\n\0" > $TEMPCONF
xpass $BOOTCONF -a $TEMPCONF $INITRD
$BOOTCONF $INITRD > $OUTFILE
xpass grep -q "value" $OUTFILE

echo "Unclosed brace error position"
cat > $TEMPCONF << EOF
a {
  b = 1
}
c {
  d = 2
EOF
echo > $INITRD

xfail $BOOTCONF -a $TEMPCONF $INITRD
$BOOTCONF -a $TEMPCONF $INITRD > $OUTFILE 2>&1
xpass grep -q "Brace.is.not.closed.at.4:1" $OUTFILE
$BOOTCONF -a samples/bad-tree.bconf $INITRD > $OUTFILE 2>&1
xpass grep -q "Brace.is.not.closed.at.2:1" $OUTFILE

echo "Mixed value and subkey depth/length limits"
cat > $TEMPCONF << EOF
k1 = v1
k1.k2.k3.k4.k5.k6.k7.k8.k9.k10.k11.k12.k13.k14.k15.k16.k17 = v2
EOF
xfail $BOOTCONF -a $TEMPCONF $INITRD

awk '
BEGIN {
  printf("root = v\nroot.")
  for (i = 0; i < 255; i += 1)
      printf("a")
  printf(" = v2\n")
}
' > $TEMPCONF
xfail $BOOTCONF -a $TEMPCONF $INITRD

echo "=== expected failure cases ==="
for i in samples/bad-* ; do
  xfail $BOOTCONF -a $i $INITRD
done

echo "=== expected success cases ==="
for i in samples/good-* ; do
  xpass $BOOTCONF -a $i $INITRD
  x="samples/exp-"`basename $i`
  $BOOTCONF $i > $TEMPCONF
  xpass diff $x $TEMPCONF
done


echo
echo "=== Summary ==="
echo "# of Passed: $(expr $NO - $NG - 1)"
echo "# of Failed: $NG"

echo
if [ $NG -eq 0 ]; then
	echo "All tests passed"
else
	echo "$NG tests failed"
	exit 1
fi

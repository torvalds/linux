# $OpenBSD: t5.sh,v 1.1 2006/01/25 17:42:08 markus Exp $
# append without a file list should not corrupt a tar file
#
OBJ=$2
cd ${OBJ}
fail () {
	rm -f a b foo.tar
	echo "$*"
	exit 1
}
echo a > a
echo b > b
echo a  | pax -w -f foo.tar
tar tf foo.tar | grep -q a || fail missing file a
# append without a file list
echo -n | pax -w -a -f foo.tar
tar tf foo.tar > /dev/null || fail not a tar file
# again
echo    | pax -w -a -f foo.tar
tar tf foo.tar > /dev/null || fail not a tar file
# append file
echo b  | pax -w -a -f foo.tar
for i in a b; do
	tar tf foo.tar | grep -q $i || fail missing file $i
done
rm -f a b foo.tar

#!/bin/sh

in="$1"
arch="$2"

syscall_macro() {
    nr="$1"
    name="$2"

    echo "	[$nr] = \"$name\","
}

emit() {
    nr="$1"
    entry="$2"

    syscall_macro "$nr" "$entry"
}

echo "static const char *syscalltbl_${arch}[] = {"

sorted_table=$(mktemp /tmp/syscalltbl.XXXXXX)
grep '^[0-9]' "$in" | sort -n > $sorted_table

max_nr=0
while read nr abi name entry compat; do
    if [ $nr -ge 512 ] ; then # discard compat sycalls
        break
    fi

    emit "$nr" "$name"
    max_nr=$nr
done < $sorted_table

rm -f $sorted_table

echo "};"

echo "#define SYSCALLTBL_${arch}_MAX_ID ${max_nr}"

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

TOOL=$(dirname $(realpath $0))/ynl-gen-c.py

force=

while [ ! -z "$1" ]; do
  case "$1" in
    -f ) force=yes; shift ;;
    * )  echo "Unrecognized option '$1'"; exit 1 ;;
  esac
done

KDIR=$(dirname $(dirname $(dirname $(dirname $(realpath $0)))))

files=$(git grep --files-with-matches '^/\* YNL-GEN \(kernel\|uapi\|user\)')
for f in $files; do
    # params:     0       1      2     3
    #         $YAML YNL-GEN kernel $mode
    params=( $(git grep -B1 -h '/\* YNL-GEN' $f | sed 's@/\*\(.*\)\*/@\1@') )
    args=$(sed -n 's@/\* YNL-ARG \(.*\) \*/@\1@p' $f)

    if [ $f -nt ${params[0]} -a -z "$force" ]; then
	echo -e "\tSKIP $f"
	continue
    fi

    echo -e "\tGEN ${params[2]}\t$f"
    $TOOL --mode ${params[2]} --${params[3]} --spec $KDIR/${params[0]} \
	  $args -o $f
done

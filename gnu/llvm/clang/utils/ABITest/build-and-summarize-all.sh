#!/bin/sh

set -eu

if [ $# != 1 ]; then
    echo "usage: $0 <num-tests>"
    exit 1
fi

for bits in 32 64; do
    for kind in return-types single-args; do
        echo "-- $kind-$bits --"
        (cd $kind-$bits && ../build-and-summarize.sh $1)
    done
done

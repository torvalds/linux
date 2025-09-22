#!/bin/sh

set -eu

if [ $# != 1 ]; then
    echo "usage: $0 <num-tests>"
    exit 1
fi

for i in $(seq 0 $1); do 
    if (! make test.$i.report &> /dev/null); then 
        echo "FAIL: $i";
    fi; 
done


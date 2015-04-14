#!/bin/bash

# This is deliberately minimal.  IMO kselftests should provide a standard
# script here.
./sigreturn_32 || exit 1

if [[ "$uname -p" -eq "x86_64" ]]; then
    ./sigreturn_64 || exit 1
fi

exit 0

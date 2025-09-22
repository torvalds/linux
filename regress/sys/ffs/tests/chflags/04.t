#!/bin/sh

desc="chflags returns ENOENT if path or file does not exist"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect ENOENT chflags ${n0}/${n1}/test UF_IMMUTABLE
expect ENOENT chflags ${n0}/${n1} UF_IMMUTABLE
expect 0 rmdir ${n0}

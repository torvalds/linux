#!/usr/bin/env ksh93
script=$(realpath $0)
export STF_BIN=$(dirname ${script})
export STF_SUITE=$(dirname ${STF_BIN})

# $FreeBSD$

env ENV=${STF_SUITE}/include/testenv.kshlib ksh93 -E -l

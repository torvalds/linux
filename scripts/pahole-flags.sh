#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

extra_paholeopt=

if ! [ -x "$(command -v ${PAHOLE})" ]; then
	exit 0
fi

pahole_ver=$(${PAHOLE} --version | sed -E 's/v([0-9]+)\.([0-9]+)/\1\2/')

if [ "${pahole_ver}" -ge "118" ] && [ "${pahole_ver}" -le "121" ]; then
	# pahole 1.18 through 1.21 can't handle zero-sized per-CPU vars
	extra_paholeopt="${extra_paholeopt} --skip_encoding_btf_vars"
fi
if [ "${pahole_ver}" -ge "121" ]; then
	extra_paholeopt="${extra_paholeopt} --btf_gen_floats"
fi

if [ "${pahole_ver}" -ge "124" ]; then
	extra_paholeopt="${extra_paholeopt} --skip_encoding_btf_enum64"
fi

echo ${extra_paholeopt}

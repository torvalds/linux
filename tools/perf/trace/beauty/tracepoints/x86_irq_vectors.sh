#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1
# (C) 2019, Arnaldo Carvalho de Melo <acme@redhat.com>

if [ $# -ne 1 ] ; then
	beauty_arch_asm_dir=tools/perf/trace/beauty/arch/x86/include/asm/
else
	beauty_arch_asm_dir=$1
fi

x86_irq_vectors=${beauty_arch_asm_dir}/irq_vectors.h

# FIRST_EXTERNAL_VECTOR is not that useful, find what is its number
# and then replace whatever is using it and that is useful, which at
# the time of writing of this script was: 0x20.

first_external_regex='^#define[[:space:]]+FIRST_EXTERNAL_VECTOR[[:space:]]+(0x[[:xdigit:]]+)$'
first_external_vector=$(grep -E ${first_external_regex} ${x86_irq_vectors} | sed -r "s/${first_external_regex}/\1/g")

printf "static const char *x86_irq_vectors[] = {\n"
regex='^#define[[:space:]]+([[:alnum:]_]+)_VECTOR[[:space:]]+(0x[[:xdigit:]]+)$'
sed -r "s/FIRST_EXTERNAL_VECTOR/${first_external_vector}/g" ${x86_irq_vectors} | \
grep -E ${regex} | \
	sed -r "s/${regex}/\2 \1/g" | sort -n | \
	xargs printf "\t[%s] = \"%s\",\n"
printf "};\n\n"


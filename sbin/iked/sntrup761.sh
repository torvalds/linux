#!/bin/sh
#       $OpenBSD: sntrup761.sh,v 1.1 2021/05/28 18:01:39 tobhe Exp $
#       Placed in the Public Domain.
#
AUTHOR="supercop-20201130/crypto_kem/sntrup761/ref/implementors"
FILES="
	supercop-20201130/crypto_sort/int32/portable4/int32_minmax.inc
	supercop-20201130/crypto_sort/int32/portable4/sort.c
	supercop-20201130/crypto_sort/uint32/useint32/sort.c
	supercop-20201130/crypto_kem/sntrup761/ref/uint32.c
	supercop-20201130/crypto_kem/sntrup761/ref/int32.c
	supercop-20201130/crypto_kem/sntrup761/ref/paramsmenu.h
	supercop-20201130/crypto_kem/sntrup761/ref/params.h
	supercop-20201130/crypto_kem/sntrup761/ref/Decode.h
	supercop-20201130/crypto_kem/sntrup761/ref/Decode.c
	supercop-20201130/crypto_kem/sntrup761/ref/Encode.h
	supercop-20201130/crypto_kem/sntrup761/ref/Encode.c
	supercop-20201130/crypto_kem/sntrup761/ref/kem.c
"
###

set -e
cd $1
echo -n '/*  $'
echo 'OpenBSD: $ */'
echo
echo '/*'
echo ' * Public Domain, Authors:'
sed -e '/Alphabetical order:/d' -e 's/^/ * - /' < $AUTHOR
echo ' */'
echo
echo '#include <string.h>'
echo '#include "crypto_api.h"'
echo
# Map the types used in this code to the ones in crypto_api.h.  We use #define
# instead of typedef since some systems have existing intXX types and do not
# permit multiple typedefs even if they do not conflict.
for t in int8 uint8 int16 uint16 int32 uint32 int64 uint64; do
	echo "#define $t crypto_${t}"
done
echo
for i in $FILES; do
	echo "/* from $i */"
	# Changes to all files:
	#  - remove all includes, we inline everything required.
	#  - make functions not required elsewhere static.
	#  - rename the functions we do use.
	#  - remove unneccesary defines and externs.
	sed -e "/#include/d" \
	    -e "s/crypto_kem_/crypto_kem_sntrup761_/g" \
	    -e "s/^void /static void /g" \
	    -e "s/^int16 /static int16 /g" \
	    -e "s/^uint16 /static uint16 /g" \
	    -e "/^extern /d" \
	    -e '/CRYPTO_NAMESPACE/d' \
	    -e "/^#define int32 crypto_int32/d" \
	    $i | \
	case "$i" in
	# Use int64_t for intermediate values in int32_MINMAX to prevent signed
	# 32-bit integer overflow when called by crypto_sort_uint32.
	*/int32_minmax.inc)
	    sed -e "s/int32 ab = b ^ a/int64_t ab = (int64_t)b ^ (int64_t)a/" \
	        -e "s/int32 c = b - a/int64_t c = (int64_t)b - (int64_t)a/"
	    ;;
	*/int32/portable4/sort.c)
	    sed -e "s/void crypto_sort/void crypto_sort_int32/g"
	    ;;
	*/uint32/useint32/sort.c)
	    sed -e "s/void crypto_sort/void crypto_sort_uint32/g"
	    ;;
	# Remove unused function to prevent warning.
	*/crypto_kem/sntrup761/ref/int32.c)
	    sed -e '/ int32_div_uint14/,/^}$/d'
	    ;;
	# Remove unused function to prevent warning.
	*/crypto_kem/sntrup761/ref/uint32.c)
	    sed -e '/ uint32_div_uint14/,/^}$/d'
	    ;;
	# Default: pass through.
	*)
	    cat
	    ;;
	esac
	echo
done

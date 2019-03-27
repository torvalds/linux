#!/bin/sh
# $FreeBSD$

class="eli"
base=$(atf_get ident)
MAX_SECSIZE=8192

attach_md()
{
	local test_md

	test_md=$(mdconfig -a "$@") || atf_fail "failed to allocate md(4)"
	echo $test_md >> $TEST_MDS_FILE || exit
	echo $test_md
}

# Execute `func` for each combination of cipher, sectorsize, and hmac algo
# `func` usage should be:
# func <cipher> <aalgo> <secsize>
for_each_geli_config() {
	func=$1
	backing_filename=$2

	# Double the sector size to allow for the HMACs' storage space.
	osecsize=$(( $MAX_SECSIZE * 2 ))
	# geli needs 512B for the label.
	bytes=`expr $osecsize \* $sectors + 512`b

	if [ -n "$backing_filename" ]; then
		# Use a file-backed md(4) device, so we can deliberatly corrupt
		# it without detaching the geli device first.
		truncate -s $bytes backing_file
		md=$(attach_md -t vnode -f backing_file)
	else
		md=$(attach_md -t malloc -s $bytes)
	fi

	for cipher in aes-xts:128 aes-xts:256 \
	    aes-cbc:128 aes-cbc:192 aes-cbc:256 \
	    3des-cbc:192 \
	    blowfish-cbc:128 blowfish-cbc:160 blowfish-cbc:192 \
	    blowfish-cbc:224 blowfish-cbc:256 blowfish-cbc:288 \
	    blowfish-cbc:320 blowfish-cbc:352 blowfish-cbc:384 \
	    blowfish-cbc:416 blowfish-cbc:448 \
	    camellia-cbc:128 camellia-cbc:192 camellia-cbc:256; do
		ealgo=${cipher%%:*}
		keylen=${cipher##*:}
		for aalgo in hmac/md5 hmac/sha1 hmac/ripemd160 hmac/sha256 \
		    hmac/sha384 hmac/sha512; do
			for secsize in 512 1024 2048 4096 $MAX_SECSIZE; do
				${func} $cipher $aalgo $secsize
				geli detach ${md} 2>/dev/null
			done
		done
	done
}

# Execute `func` for each combination of cipher, and sectorsize, with no hmac
# `func` usage should be:
# func <cipher> <secsize>
for_each_geli_config_nointegrity() {
	func=$1

	# geli needs 512B for the label.
	bytes=`expr $MAX_SECSIZE \* $sectors + 512`b
	md=$(attach_md -t malloc -s $bytes)
	for cipher in aes-xts:128 aes-xts:256 \
	    aes-cbc:128 aes-cbc:192 aes-cbc:256 \
	    3des-cbc:192 \
	    blowfish-cbc:128 blowfish-cbc:160 blowfish-cbc:192 \
	    blowfish-cbc:224 blowfish-cbc:256 blowfish-cbc:288 \
	    blowfish-cbc:320 blowfish-cbc:352 blowfish-cbc:384 \
	    blowfish-cbc:416 blowfish-cbc:448 \
	    camellia-cbc:128 camellia-cbc:192 camellia-cbc:256; do
		ealgo=${cipher%%:*}
		keylen=${cipher##*:}
		for secsize in 512 1024 2048 4096 $MAX_SECSIZE; do
			${func} $cipher $secsize
			geli detach ${md} 2>/dev/null
		done
	done
}

geli_test_cleanup()
{
	if [ -f "$TEST_MDS_FILE" ]; then
		while read md; do
			[ -c /dev/${md}.eli ] && \
				geli detach $md.eli 2>/dev/null
			mdconfig -d -u $md 2>/dev/null
		done < $TEST_MDS_FILE
	fi
	true
}

geli_test_setup()
{
	geom_atf_test_setup
}

ATF_TEST=true
. `dirname $0`/../geom_subr.sh

#!/bin/sh
# $FreeBSD$
#
# IPv6 IPsec test based on ipsec.t, in this same directory, which tests
# IPsec by setting up a set of tunnels and then sending ICMPv6 packets,   
# aka those generated with ping6(8), across the tunnel.
#
# This test should ONLY be used as a smoke test to verify that nothing
# drastic has been broken, it is insufficient for true protocol conformance
# testing.
#
# Expected Output: No failures.

netif="lo0"
spi="10000"

echo "1..414"

#sysctl net.inet.ipsec.crypto_support=1 >/dev/null 2>&1

ifconfig $netif inet6 alias 1::1
ifconfig $netif inet6 alias 2::1

i=1

for ecipher in \
    des-cbc:12345678 \
    3des-cbc:012345678901234567890123 \
    blowfish-cbc:0123456789012345 \
    blowfish-cbc:01234567890123456789 \
    blowfish-cbc:012345678901234567890123 \
    blowfish-cbc:0123456789012345678901234567 \
    blowfish-cbc:01234567890123456789012345678901 \
    blowfish-cbc:012345678901234567890123456789012345 \
    blowfish-cbc:0123456789012345678901234567890123456789 \
    blowfish-cbc:01234567890123456789012345678901234567890123 \
    blowfish-cbc:012345678901234567890123456789012345678901234567 \
    blowfish-cbc:0123456789012345678901234567890123456789012345678901 \
    blowfish-cbc:01234567890123456789012345678901234567890123456789012345 \
    cast128-cbc:0123456789012345 \
    aes-ctr:01234567890123456789\
    aes-ctr:0123456789012345678901234567\
    aes-ctr:012345678901234567890123456789012345\
    camellia-cbc:0123456789012345\
    camellia-cbc:012345678901234567890123\
    camellia-cbc:01234567890123456789012345678901\
    rijndael-cbc:0123456789012345 \
    rijndael-cbc:012345678901234567890123 \
    rijndael-cbc:01234567890123456789012345678901; do

	ealgo=${ecipher%%:*}
	ekey=${ecipher##*:}

	for acipher in \
	    hmac-md5:0123456789012345 \
	    hmac-sha1:01234567890123456789 \
	    hmac-ripemd160:01234567890123456789 \
	    hmac-sha2-256:01234567890123456789012345678901 \
	    hmac-sha2-384:012345678901234567890123456789012345678901234567 \
	    hmac-sha2-512:0123456789012345678901234567890123456789012345678901234567890123; do

		aalgo=${acipher%%:*}
		akey=${acipher##*:}

		setkey -F
		setkey -FP

		(echo "add -6 1::1 2::1 esp $spi            -m transport -E $ealgo \"${ekey}\" -A $aalgo \"${akey}\" ;"
		 echo "add -6 2::1 1::1 esp `expr $spi + 1` -m transport -E $ealgo \"${ekey}\" -A $aalgo \"${akey}\" ;"

		 echo "spdadd -6 1::1 2::1 any -P out ipsec esp/transport//require;"
		 echo "spdadd -6 2::1 1::1 any -P in  ipsec esp/transport//require;"
		 echo "spdadd -6 1::1 2::1 any -P in  ipsec esp/transport//require;"
		 echo "spdadd -6 2::1 1::1 any -P out ipsec esp/transport//require;"
		) | setkey -c >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			echo "ok $i - setkey ${ealgo} ${ekey} ${aalgo} ${akey}"
		else
			echo "not ok $i - setkey ${ealgo} ${ekey} ${aalgo} ${akey}"
		fi
		i=$((i+1))

		ping6 -c 1 -i 1 -S 1::1 2::1 >/dev/null
		if [ $? -eq 0 ]; then
			echo "ok $i - test 1 ${ealgo} ${ekey} ${aalgo} ${akey}"
		else
			echo "not ok $i - test 1 ${ealgo} ${ekey} ${aalgo} ${akey}"
		fi
		i=$((i+1))
		ping6 -c 1 -i 1 -S 2::1 1::1 >/dev/null
		if [ $? -eq 0 ]; then
			echo "ok $i - test 2 ${ealgo} ${ekey} ${aalgo} ${akey}"
		else
			echo "not ok $i - test 2 ${ealgo} ${ekey} ${aalgo} ${akey}"
		fi
		i=$((i+1))
	done
done

setkey -F
setkey -FP

ifconfig $netif inet6 1::1 delete
ifconfig $netif inet6 2::1 delete

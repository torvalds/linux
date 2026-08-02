#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Script that generates known-good data used in the AEAD tests.
#
# Requires that python-cryptography be installed.
#
# Copyright 2026 Google LLC

import hashlib
import sys
import cryptography.hazmat.primitives.ciphers.aead


# Deterministically generate 'length' random bytes.
def rand_bytes(length):
    seed = length
    out = []
    for _ in range(length):
        seed = (seed * 25214903917 + 11) % 2**48
        out.append((seed >> 16) % 256)
    return bytes(out)


# Deterministically generate many different AEAD inputs using exactly the same
# method that the test uses; encrypt them using an independent implementation of
# the algorithm; compute the checksum of all the resulting (ciphertext, authtag)
# pairs concatenated to each other; and print the checksum as a C struct.
def gen_monte_carlo_checksum(alg):
    blake2s = hashlib.blake2s()
    for data_len in range(1025):
        ad_len = data_len % 293
        pt = rand_bytes(data_len)
        ad = rand_bytes(ad_len)
        if alg == "aes-ccm":
            key_len = [16, 24, 32][data_len % 3]
            key = rand_bytes(key_len)
            nonce = rand_bytes([7, 8, 9, 10, 11, 12, 13][data_len % 7])
            tag_len = [4, 6, 8, 10, 12, 14, 16][data_len % 7]
            ccm = cryptography.hazmat.primitives.ciphers.aead.AESCCM(
                key, tag_length=tag_len
            )
            ct_and_tag = ccm.encrypt(nonce, pt, ad)
        elif alg == "aes-gcm":
            key_len = [16, 24, 32][data_len % 3]
            key = rand_bytes(key_len)
            nonce = rand_bytes(12)
            tag_len = [4, 8, 12, 13, 14, 15, 16][data_len % 7]
            gcm = cryptography.hazmat.primitives.ciphers.aead.AESGCM(key)
            # python-cryptography supports only 16-byte GCM tags.  However, in
            # GCM, shorter tags are simply truncated.  Do that below.
            ct_and_tag = gcm.encrypt(nonce, pt, ad)[: data_len + tag_len]

        blake2s.update(ct_and_tag)

    name = f"{alg.replace('-', '_')}_monte_carlo_checksum"
    value = blake2s.digest()
    print(f"static const u8 {name}[BLAKE2S_HASH_SIZE] = {{")
    for i in range(0, len(value), 11):
        line = "\t" + "".join(f"0x{b:02x}, " for b in value[i : i + 11])
        print(f"{line.rstrip()}")
    print("};")


if len(sys.argv) != 2 or sys.argv[1] not in ("aes-ccm", "aes-gcm"):
    sys.stderr.write("Usage: gen-aead-testvecs.py [aes-ccm|aes-gcm]\n")
    sys.exit(1)

gen_monte_carlo_checksum(sys.argv[1])

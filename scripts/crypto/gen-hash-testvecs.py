#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Script that generates test vectors for the given cryptographic hash function.
#
# Copyright 2025 Google LLC

import hashlib
import hmac
import sys

DATA_LENS = [0, 1, 2, 3, 16, 32, 48, 49, 63, 64, 65, 127, 128, 129, 256, 511,
             513, 1000, 3333, 4096, 4128, 4160, 4224, 16384]

# Generate the given number of random bytes, using the length itself as the seed
# for a simple linear congruential generator (LCG).  The C test code uses the
# same LCG with the same seeding strategy to reconstruct the data, ensuring
# reproducibility without explicitly storing the data in the test vectors.
def rand_bytes(length):
    seed = length
    out = []
    for _ in range(length):
        seed = (seed * 25214903917 + 11) % 2**48
        out.append((seed >> 16) % 256)
    return bytes(out)

POLY1305_KEY_SIZE = 32

# A straightforward, unoptimized implementation of Poly1305.
# Reference: https://cr.yp.to/mac/poly1305-20050329.pdf
class Poly1305:
    def __init__(self, key):
        assert len(key) == POLY1305_KEY_SIZE
        self.h = 0
        rclamp = 0x0ffffffc0ffffffc0ffffffc0fffffff
        self.r = int.from_bytes(key[:16], byteorder='little') & rclamp
        self.s = int.from_bytes(key[16:], byteorder='little')

    # Note: this supports partial blocks only at the end.
    def update(self, data):
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            c = int.from_bytes(chunk, byteorder='little') + 2**(8 * len(chunk))
            self.h = ((self.h + c) * self.r) % (2**130 - 5)
        return self

    # Note: gen_additional_poly1305_testvecs() relies on this being
    # nondestructive, i.e. not changing any field of self.
    def digest(self):
        m = (self.h + self.s) % 2**128
        return m.to_bytes(16, byteorder='little')

def hash_init(alg):
    if alg == 'poly1305':
        # Use a fixed random key here, to present Poly1305 as an unkeyed hash.
        # This allows all the test cases for unkeyed hashes to work on Poly1305.
        return Poly1305(rand_bytes(POLY1305_KEY_SIZE))
    return hashlib.new(alg)

def hash_update(ctx, data):
    ctx.update(data)

def hash_final(ctx):
    return ctx.digest()

def compute_hash(alg, data):
    ctx = hash_init(alg)
    hash_update(ctx, data)
    return hash_final(ctx)

def print_bytes(prefix, value, bytes_per_line):
    for i in range(0, len(value), bytes_per_line):
        line = prefix + ''.join(f'0x{b:02x}, ' for b in value[i:i+bytes_per_line])
        print(f'{line.rstrip()}')

def print_static_u8_array_definition(name, value):
    print('')
    print(f'static const u8 {name} = {{')
    print_bytes('\t', value, 8)
    print('};')

def print_c_struct_u8_array_field(name, value):
    print(f'\t\t.{name} = {{')
    print_bytes('\t\t\t', value, 8)
    print('\t\t},')

def alg_digest_size_const(alg):
    if alg == 'blake2s':
        return 'BLAKE2S_HASH_SIZE'
    return f'{alg.upper()}_DIGEST_SIZE'

def gen_unkeyed_testvecs(alg):
    print('')
    print('static const struct {')
    print('\tsize_t data_len;')
    print(f'\tu8 digest[{alg_digest_size_const(alg)}];')
    print('} hash_testvecs[] = {')
    for data_len in DATA_LENS:
        data = rand_bytes(data_len)
        print('\t{')
        print(f'\t\t.data_len = {data_len},')
        print_c_struct_u8_array_field('digest', compute_hash(alg, data))
        print('\t},')
    print('};')

    data = rand_bytes(4096)
    ctx = hash_init(alg)
    for data_len in range(len(data) + 1):
        hash_update(ctx, compute_hash(alg, data[:data_len]))
    print_static_u8_array_definition(
            f'hash_testvec_consolidated[{alg_digest_size_const(alg)}]',
            hash_final(ctx))

def gen_hmac_testvecs(alg):
    ctx = hmac.new(rand_bytes(32), digestmod=alg)
    data = rand_bytes(4096)
    for data_len in range(len(data) + 1):
        ctx.update(data[:data_len])
        key_len = data_len % 293
        key = rand_bytes(key_len)
        mac = hmac.digest(key, data[:data_len], alg)
        ctx.update(mac)
    print_static_u8_array_definition(
            f'hmac_testvec_consolidated[{alg.upper()}_DIGEST_SIZE]',
            ctx.digest())

BLAKE2S_KEY_SIZE = 32
BLAKE2S_HASH_SIZE = 32

def gen_additional_blake2s_testvecs():
    hashes = b''
    for key_len in range(BLAKE2S_KEY_SIZE + 1):
        for out_len in range(1, BLAKE2S_HASH_SIZE + 1):
            h = hashlib.blake2s(digest_size=out_len, key=rand_bytes(key_len))
            h.update(rand_bytes(100))
            hashes += h.digest()
    print_static_u8_array_definition(
            'blake2s_keyed_testvec_consolidated[BLAKE2S_HASH_SIZE]',
            compute_hash('blake2s', hashes))

def gen_additional_poly1305_testvecs():
    key = b'\xff' * POLY1305_KEY_SIZE
    data = b''
    ctx = Poly1305(key)
    for _ in range(32):
        for j in range(0, 4097, 16):
            ctx.update(b'\xff' * j)
            data += ctx.digest()
    print_static_u8_array_definition(
            'poly1305_allones_macofmacs[POLY1305_DIGEST_SIZE]',
            Poly1305(key).update(data).digest())

if len(sys.argv) != 2:
    sys.stderr.write('Usage: gen-hash-testvecs.py ALGORITHM\n')
    sys.stderr.write('ALGORITHM may be any supported by Python hashlib, or poly1305.\n')
    sys.stderr.write('Example: gen-hash-testvecs.py sha512\n')
    sys.exit(1)

alg = sys.argv[1]
print('/* SPDX-License-Identifier: GPL-2.0-or-later */')
print(f'/* This file was generated by: {sys.argv[0]} {" ".join(sys.argv[1:])} */')
gen_unkeyed_testvecs(alg)
if alg == 'blake2s':
    gen_additional_blake2s_testvecs()
elif alg == 'poly1305':
    gen_additional_poly1305_testvecs()
else:
    gen_hmac_testvecs(alg)

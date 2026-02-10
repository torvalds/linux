#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Script that generates test vectors for the given hash function.
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

POLYVAL_POLY = sum((1 << i) for i in [128, 127, 126, 121, 0])
POLYVAL_BLOCK_SIZE = 16

# A straightforward, unoptimized implementation of POLYVAL.
# Reference: https://datatracker.ietf.org/doc/html/rfc8452
class Polyval:
    def __init__(self, key):
        assert len(key) == 16
        self.h = int.from_bytes(key, byteorder='little')
        self.acc = 0

    # Note: this supports partial blocks only at the end.
    def update(self, data):
        for i in range(0, len(data), 16):
            # acc += block
            self.acc ^= int.from_bytes(data[i:i+16], byteorder='little')
            # acc = (acc * h * x^-128) mod POLYVAL_POLY
            product = 0
            for j in range(128):
                if (self.h & (1 << j)) != 0:
                    product ^= self.acc << j
                if (product & (1 << j)) != 0:
                    product ^= POLYVAL_POLY << j
            self.acc = product >> 128
        return self

    def digest(self):
        return self.acc.to_bytes(16, byteorder='little')

def hash_init(alg):
    if alg == 'poly1305':
        # Use a fixed random key here, to present Poly1305 as an unkeyed hash.
        # This allows all the test cases for unkeyed hashes to work on Poly1305.
        return Poly1305(rand_bytes(POLY1305_KEY_SIZE))
    if alg == 'polyval':
        return Polyval(rand_bytes(POLYVAL_BLOCK_SIZE))
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
    if alg.startswith('blake2'):
        return f'{alg.upper()}_HASH_SIZE'
    return f"{alg.upper().replace('-', '_')}_DIGEST_SIZE"

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

def gen_additional_sha3_testvecs():
    max_len = 4096
    in_data = rand_bytes(max_len)
    for alg in ['shake128', 'shake256']:
        ctx = hashlib.new('sha3-256')
        for in_len in range(max_len + 1):
            out_len = (in_len * 293) % (max_len + 1)
            out = hashlib.new(alg, data=in_data[:in_len]).digest(out_len)
            ctx.update(out)
        print_static_u8_array_definition(f'{alg}_testvec_consolidated[SHA3_256_DIGEST_SIZE]',
                                         ctx.digest())

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

def gen_additional_blake2_testvecs(alg):
    if alg == 'blake2s':
        (max_key_size, max_hash_size) = (32, 32)
    elif alg == 'blake2b':
        (max_key_size, max_hash_size) = (64, 64)
    else:
        raise ValueError(f'Unsupported alg: {alg}')
    hashes = b''
    for key_len in range(max_key_size + 1):
        for out_len in range(1, max_hash_size + 1):
            h = hashlib.new(alg, digest_size=out_len, key=rand_bytes(key_len))
            h.update(rand_bytes(100))
            hashes += h.digest()
    print_static_u8_array_definition(
            f'{alg}_keyed_testvec_consolidated[{alg_digest_size_const(alg)}]',
            compute_hash(alg, hashes))

def nh_extract_int(bytestr, pos, length):
    assert pos % 8 == 0 and length % 8 == 0
    return int.from_bytes(bytestr[pos//8 : pos//8 + length//8], byteorder='little')

# The NH "almost-universal hash function" used in Adiantum.  This is a
# straightforward translation of the pseudocode from Section 6.3 of the Adiantum
# paper (https://eprint.iacr.org/2018/720.pdf), except the outer loop is omitted
# because we assume len(msg) <= 1024.  (The kernel's nh() function is only
# expected to handle up to 1024 bytes; it's just called repeatedly as needed.)
def nh(key, msg):
    (w, s, r, u) = (32, 2, 4, 8192)
    l = 8 * len(msg)
    assert l <= u
    assert l % (2*s*w) == 0
    h = bytes()
    for i in range(0, 2*s*w*r, 2*s*w):
        p = 0
        for j in range(0, l, 2*s*w):
            for k in range(0, w*s, w):
                a0 = nh_extract_int(key, i + j + k, w)
                a1 = nh_extract_int(key, i + j + k + s*w, w)
                b0 = nh_extract_int(msg, j + k, w)
                b1 = nh_extract_int(msg, j + k + s*w, w)
                p += ((a0 + b0) % 2**w) * ((a1 + b1) % 2**w)
        h += (p % 2**64).to_bytes(8, byteorder='little')
    return h

def gen_nh_testvecs():
    NH_KEY_BYTES = 1072
    NH_MESSAGE_BYTES = 1024
    key = rand_bytes(NH_KEY_BYTES)
    msg = rand_bytes(NH_MESSAGE_BYTES)
    print_static_u8_array_definition('nh_test_key[NH_KEY_BYTES]', key)
    print_static_u8_array_definition('nh_test_msg[NH_MESSAGE_BYTES]', msg)
    for length in [16, 96, 256, 1024]:
        print_static_u8_array_definition(f'nh_test_val{length}[NH_HASH_BYTES]',
                                         nh(key, msg[:length]))

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

def gen_additional_polyval_testvecs():
    key = b'\xff' * POLYVAL_BLOCK_SIZE
    hashes = b''
    for data_len in range(0, 4097, 16):
        hashes += Polyval(key).update(b'\xff' * data_len).digest()
    print_static_u8_array_definition(
            'polyval_allones_hashofhashes[POLYVAL_DIGEST_SIZE]',
            Polyval(key).update(hashes).digest())

if len(sys.argv) != 2:
    sys.stderr.write('Usage: gen-hash-testvecs.py ALGORITHM\n')
    sys.stderr.write('ALGORITHM may be any supported by Python hashlib; or poly1305, polyval, or sha3.\n')
    sys.stderr.write('Example: gen-hash-testvecs.py sha512\n')
    sys.exit(1)

alg = sys.argv[1]
print('/* SPDX-License-Identifier: GPL-2.0-or-later */')
print(f'/* This file was generated by: {sys.argv[0]} {" ".join(sys.argv[1:])} */')
if alg.startswith('blake2'):
    gen_unkeyed_testvecs(alg)
    gen_additional_blake2_testvecs(alg)
elif alg == 'nh':
    gen_nh_testvecs()
elif alg == 'poly1305':
    gen_unkeyed_testvecs(alg)
    gen_additional_poly1305_testvecs()
elif alg == 'polyval':
    gen_unkeyed_testvecs(alg)
    gen_additional_polyval_testvecs()
elif alg == 'sha3':
    print()
    print('/* SHA3-256 test vectors */')
    gen_unkeyed_testvecs('sha3-256')
    print()
    print('/* SHAKE test vectors */')
    gen_additional_sha3_testvecs()
else:
    gen_unkeyed_testvecs(alg)
    gen_hmac_testvecs(alg)

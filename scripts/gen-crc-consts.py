#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Script that generates constants for computing the given CRC variant(s).
#
# Copyright 2025 Google LLC
#
# Author: Eric Biggers <ebiggers@google.com>

import sys

# XOR (add) an iterable of polynomials.
def xor(iterable):
    res = 0
    for val in iterable:
        res ^= val
    return res

# Multiply two polynomials.
def clmul(a, b):
    return xor(a << i for i in range(b.bit_length()) if (b & (1 << i)) != 0)

# Polynomial division floor(a / b).
def div(a, b):
    q = 0
    while a.bit_length() >= b.bit_length():
        q ^= 1 << (a.bit_length() - b.bit_length())
        a ^= b << (a.bit_length() - b.bit_length())
    return q

# Reduce the polynomial 'a' modulo the polynomial 'b'.
def reduce(a, b):
    return a ^ clmul(div(a, b), b)

# Reflect the bits of a polynomial.
def bitreflect(poly, num_bits):
    assert poly.bit_length() <= num_bits
    return xor(((poly >> i) & 1) << (num_bits - 1 - i) for i in range(num_bits))

# Format a polynomial as hex.  Bit-reflect it if the CRC is lsb-first.
def fmt_poly(variant, poly, num_bits):
    if variant.lsb:
        poly = bitreflect(poly, num_bits)
    return f'0x{poly:0{2*num_bits//8}x}'

# Print a pair of 64-bit polynomial multipliers.  They are always passed in the
# order [HI64_TERMS, LO64_TERMS] but will be printed in the appropriate order.
def print_mult_pair(variant, mults):
    mults = list(mults if variant.lsb else reversed(mults))
    terms = ['HI64_TERMS', 'LO64_TERMS'] if variant.lsb else ['LO64_TERMS', 'HI64_TERMS']
    for i in range(2):
        print(f'\t\t{fmt_poly(variant, mults[i]["val"], 64)},\t/* {terms[i]}: {mults[i]["desc"]} */')

# Pretty-print a polynomial.
def pprint_poly(prefix, poly):
    terms = [f'x^{i}' for i in reversed(range(poly.bit_length()))
             if (poly & (1 << i)) != 0]
    j = 0
    while j < len(terms):
        s = prefix + terms[j] + (' +' if j < len(terms) - 1 else '')
        j += 1
        while j < len(terms) and len(s) < 73:
            s += ' ' + terms[j] + (' +' if j < len(terms) - 1 else '')
            j += 1
        print(s)
        prefix = ' * ' + (' ' * (len(prefix) - 3))

# Print a comment describing constants generated for the given CRC variant.
def print_header(variant, what):
    print('/*')
    s = f'{"least" if variant.lsb else "most"}-significant-bit-first CRC-{variant.bits}'
    print(f' * {what} generated for {s} using')
    pprint_poly(' * G(x) = ', variant.G)
    print(' */')

class CrcVariant:
    def __init__(self, bits, generator_poly, bit_order):
        self.bits = bits
        if bit_order not in ['lsb', 'msb']:
            raise ValueError('Invalid value for bit_order')
        self.lsb = bit_order == 'lsb'
        self.name = f'crc{bits}_{bit_order}_0x{generator_poly:0{(2*bits+7)//8}x}'
        if self.lsb:
            generator_poly = bitreflect(generator_poly, bits)
        self.G = generator_poly ^ (1 << bits)

# Generate tables for CRC computation using the "slice-by-N" method.
# N=1 corresponds to the traditional byte-at-a-time table.
def gen_slicebyN_tables(variants, n):
    for v in variants:
        print('')
        print_header(v, f'Slice-by-{n} CRC table')
        print(f'static const u{v.bits} __maybe_unused {v.name}_table[{256*n}] = {{')
        s = ''
        for i in range(256 * n):
            # The i'th table entry is the CRC of the message consisting of byte
            # i % 256 followed by i // 256 zero bytes.
            poly = (bitreflect(i % 256, 8) if v.lsb else i % 256) << (v.bits + 8*(i//256))
            next_entry = fmt_poly(v, reduce(poly, v.G), v.bits) + ','
            if len(s + next_entry) > 71:
                print(f'\t{s}')
                s = ''
            s += (' ' if s else '') + next_entry
        if s:
            print(f'\t{s}')
        print('};')

def print_riscv_const(v, bits_per_long, name, val, desc):
    print(f'\t.{name} = {fmt_poly(v, val, bits_per_long)}, /* {desc} */')

def do_gen_riscv_clmul_consts(v, bits_per_long):
    (G, n, lsb) = (v.G, v.bits, v.lsb)

    pow_of_x = 3 * bits_per_long - (1 if lsb else 0)
    print_riscv_const(v, bits_per_long, 'fold_across_2_longs_const_hi',
                      reduce(1 << pow_of_x, G), f'x^{pow_of_x} mod G')
    pow_of_x = 2 * bits_per_long - (1 if lsb else 0)
    print_riscv_const(v, bits_per_long, 'fold_across_2_longs_const_lo',
                      reduce(1 << pow_of_x, G), f'x^{pow_of_x} mod G')

    pow_of_x = bits_per_long - 1 + n
    print_riscv_const(v, bits_per_long, 'barrett_reduction_const_1',
                      div(1 << pow_of_x, G), f'floor(x^{pow_of_x} / G)')

    val = G - (1 << n)
    desc = f'G - x^{n}'
    if lsb:
        val <<= bits_per_long - n
        desc = f'({desc}) * x^{bits_per_long - n}'
    print_riscv_const(v, bits_per_long, 'barrett_reduction_const_2', val, desc)

def gen_riscv_clmul_consts(variants):
    print('')
    print('struct crc_clmul_consts {');
    print('\tunsigned long fold_across_2_longs_const_hi;');
    print('\tunsigned long fold_across_2_longs_const_lo;');
    print('\tunsigned long barrett_reduction_const_1;');
    print('\tunsigned long barrett_reduction_const_2;');
    print('};');
    for v in variants:
        print('');
        if v.bits > 32:
            print_header(v, 'Constants')
            print('#ifdef CONFIG_64BIT')
            print(f'static const struct crc_clmul_consts {v.name}_consts __maybe_unused = {{')
            do_gen_riscv_clmul_consts(v, 64)
            print('};')
            print('#endif')
        else:
            print_header(v, 'Constants')
            print(f'static const struct crc_clmul_consts {v.name}_consts __maybe_unused = {{')
            print('#ifdef CONFIG_64BIT')
            do_gen_riscv_clmul_consts(v, 64)
            print('#else')
            do_gen_riscv_clmul_consts(v, 32)
            print('#endif')
            print('};')

# Generate constants for carryless multiplication based CRC computation.
def gen_x86_pclmul_consts(variants):
    # These are the distances, in bits, to generate folding constants for.
    FOLD_DISTANCES = [2048, 1024, 512, 256, 128]

    for v in variants:
        (G, n, lsb) = (v.G, v.bits, v.lsb)
        print('')
        print_header(v, 'CRC folding constants')
        print('static const struct {')
        if not lsb:
            print('\tu8 bswap_mask[16];')
        for i in FOLD_DISTANCES:
            print(f'\tu64 fold_across_{i}_bits_consts[2];')
        print('\tu8 shuf_table[48];')
        print('\tu64 barrett_reduction_consts[2];')
        print(f'}} {v.name}_consts ____cacheline_aligned __maybe_unused = {{')

        # Byte-reflection mask, needed for msb-first CRCs
        if not lsb:
            print('\t.bswap_mask = {' + ', '.join(str(i) for i in reversed(range(16))) + '},')

        # Fold constants for all distances down to 128 bits
        for i in FOLD_DISTANCES:
            print(f'\t.fold_across_{i}_bits_consts = {{')
            # Given 64x64 => 128 bit carryless multiplication instructions, two
            # 64-bit fold constants are needed per "fold distance" i: one for
            # HI64_TERMS that is basically x^(i+64) mod G and one for LO64_TERMS
            # that is basically x^i mod G.  The exact values however undergo a
            # couple adjustments, described below.
            mults = []
            for j in [64, 0]:
                pow_of_x = i + j
                if lsb:
                    # Each 64x64 => 128 bit carryless multiplication instruction
                    # actually generates a 127-bit product in physical bits 0
                    # through 126, which in the lsb-first case represent the
                    # coefficients of x^1 through x^127, not x^0 through x^126.
                    # Thus in the lsb-first case, each such instruction
                    # implicitly adds an extra factor of x.  The below removes a
                    # factor of x from each constant to compensate for this.
                    # For n < 64 the x could be removed from either the reduced
                    # part or unreduced part, but for n == 64 the reduced part
                    # is the only option.  Just always use the reduced part.
                    pow_of_x -= 1
                # Make a factor of x^(64-n) be applied unreduced rather than
                # reduced, to cause the product to use only the x^(64-n) and
                # higher terms and always be zero in the lower terms.  Usually
                # this makes no difference as it does not affect the product's
                # congruence class mod G and the constant remains 64-bit, but
                # part of the final reduction from 128 bits does rely on this
                # property when it reuses one of the constants.
                pow_of_x -= 64 - n
                mults.append({ 'val': reduce(1 << pow_of_x, G) << (64 - n),
                               'desc': f'(x^{pow_of_x} mod G) * x^{64-n}' })
            print_mult_pair(v, mults)
            print('\t},')

        # Shuffle table for handling 1..15 bytes at end
        print('\t.shuf_table = {')
        print('\t\t' + (16*'-1, ').rstrip())
        print('\t\t' + ''.join(f'{i:2}, ' for i in range(16)).rstrip())
        print('\t\t' + (16*'-1, ').rstrip())
        print('\t},')

        # Barrett reduction constants for reducing 128 bits to the final CRC
        print('\t.barrett_reduction_consts = {')
        mults = []

        val = div(1 << (63+n), G)
        desc = f'floor(x^{63+n} / G)'
        if not lsb:
            val = (val << 1) - (1 << 64)
            desc = f'({desc} * x) - x^64'
        mults.append({ 'val': val, 'desc': desc })

        val = G - (1 << n)
        desc = f'G - x^{n}'
        if lsb and n == 64:
            assert (val & 1) != 0  # The x^0 term should always be nonzero.
            val >>= 1
            desc = f'({desc} - x^0) / x'
        else:
            pow_of_x = 64 - n - (1 if lsb else 0)
            val <<= pow_of_x
            desc = f'({desc}) * x^{pow_of_x}'
        mults.append({ 'val': val, 'desc': desc })

        print_mult_pair(v, mults)
        print('\t},')

        print('};')

def parse_crc_variants(vars_string):
    variants = []
    for var_string in vars_string.split(','):
        bits, bit_order, generator_poly = var_string.split('_')
        assert bits.startswith('crc')
        bits = int(bits.removeprefix('crc'))
        assert generator_poly.startswith('0x')
        generator_poly = generator_poly.removeprefix('0x')
        assert len(generator_poly) % 2 == 0
        generator_poly = int(generator_poly, 16)
        variants.append(CrcVariant(bits, generator_poly, bit_order))
    return variants

if len(sys.argv) != 3:
    sys.stderr.write(f'Usage: {sys.argv[0]} CONSTS_TYPE[,CONSTS_TYPE]... CRC_VARIANT[,CRC_VARIANT]...\n')
    sys.stderr.write('  CONSTS_TYPE can be sliceby[1-8], riscv_clmul, or x86_pclmul\n')
    sys.stderr.write('  CRC_VARIANT is crc${num_bits}_${bit_order}_${generator_poly_as_hex}\n')
    sys.stderr.write('     E.g. crc16_msb_0x8bb7 or crc32_lsb_0xedb88320\n')
    sys.stderr.write('     Polynomial must use the given bit_order and exclude x^{num_bits}\n')
    sys.exit(1)

print('/* SPDX-License-Identifier: GPL-2.0-or-later */')
print('/*')
print(' * CRC constants generated by:')
print(' *')
print(f' *\t{sys.argv[0]} {" ".join(sys.argv[1:])}')
print(' *')
print(' * Do not edit manually.')
print(' */')
consts_types = sys.argv[1].split(',')
variants = parse_crc_variants(sys.argv[2])
for consts_type in consts_types:
    if consts_type.startswith('sliceby'):
        gen_slicebyN_tables(variants, int(consts_type.removeprefix('sliceby')))
    elif consts_type == 'riscv_clmul':
        gen_riscv_clmul_consts(variants)
    elif consts_type == 'x86_pclmul':
        gen_x86_pclmul_consts(variants)
    else:
        raise ValueError(f'Unknown consts_type: {consts_type}')

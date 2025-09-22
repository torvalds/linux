#!/usr/bin/env python3

# This script was committed on 20/11/2019 and it would probably make sense to remove
# it after the next release branches.

# This script is pipe based and converts an arm_neon.td (or arm_fp16.td) file
# using the old single-char type modifiers to an equivalent new-style form where
# each modifier is orthogonal and they can be composed.
#
# It was used to directly generate the .td files on main, so if you have any
# local additions I would suggest implementing any modifiers here, and running
# it over your entire pre-merge .td files rather than trying to resolve any
# conflicts manually.

import re, sys

MOD_MAP = {
    "v": "v",
    "x": "S",
    "u": "U",
    "d": ".",
    "g": "q",
    "j": "Q",
    "w": ">Q",
    "n": ">",
    "h": "<",
    "q": "<Q",
    "e": "<U",
    "m": "<q",
    "i": "I",
    "l": "IU>",
    "s": "1",
    "z": "1<",
    "r": "1>",
    "b": "1U",
    "$": "1S",
    "k": "Q",
    "2": "2",
    "3": "3",
    "4": "4",
    "B": "2Q",
    "C": "3Q",
    "D": "4Q",
    "p": "*",
    "c": "c*",
    "7": "<<q",
    "8": "<<",
    "9": "<<Q",
    "t": "p",
}


def typespec_elt_size(typespec):
    if "c" in typespec:
        return 8
    elif "s" in typespec or "h" in typespec:
        return 16
    elif "i" in typespec or "f" in typespec:
        return 32
    elif "l" in typespec or "d" in typespec:
        return 64
    elif "k" in typespec:
        return 128


def get_resize(cur, desired):
    res = ""
    while cur < desired:
        res += ">"
        cur *= 2
    while cur > desired:
        res += "<"
        cur /= 2
    return res


def remap_protocol(proto, typespec, name):
    key_type = 0

    # Conversions like to see the integer type so they know signedness.
    if (
        "vcvt" in name
        and "_f" in name
        and name != "vcvt_f32_f64"
        and name != "vcvt_f64_f32"
    ):
        key_type = 1
    default_width = typespec_elt_size(typespec)
    inconsistent_width = False
    for elt in typespec:
        new_width = typespec_elt_size(elt)
        if new_width and new_width != default_width:
            inconsistent_width = True

    res = ""
    for i, c in enumerate(proto):
        # void and pointers make for bad discriminators in CGBuiltin.cpp.
        if c in "vcp":
            key_type += 1

        if c in MOD_MAP:
            cur_mod = MOD_MAP[c]
        elif inconsistent_width:
            # Otherwise it's a fixed output width modifier.
            sys.stderr.write(
                f"warning: {name} uses fixed output size but has inconsistent input widths: {proto} {typespec}\n"
            )

        if c == "Y":
            # y: scalar of half float
            resize = get_resize(default_width, 16)
            cur_mod = f"1F{resize}"
        elif c == "y":
            # y: scalar of float
            resize = get_resize(default_width, 32)
            cur_mod = f"1F{resize}"
        elif c == "o":
            # o: scalar of double
            resize = get_resize(default_width, 64)
            cur_mod = f"1F{resize}"
        elif c == "I":
            # I: scalar of 32-bit signed
            resize = get_resize(default_width, 32)
            cur_mod = f"1S{resize}"
        elif c == "L":
            # L: scalar of 64-bit signed
            resize = get_resize(default_width, 64)
            cur_mod = f"1S{resize}"
        elif c == "U":
            # I: scalar of 32-bit unsigned
            resize = get_resize(default_width, 32)
            cur_mod = f"1U{resize}"
        elif c == "O":
            # O: scalar of 64-bit unsigned
            resize = get_resize(default_width, 64)
            cur_mod = f"1U{resize}"
        elif c == "f":
            # f: float (int args)
            resize = get_resize(default_width, 32)
            cur_mod = f"F{resize}"
        elif c == "F":
            # F: double (int args)
            resize = get_resize(default_width, 64)
            cur_mod = f"F{resize}"
        elif c == "H":
            # H: half (int args)
            resize = get_resize(default_width, 16)
            cur_mod = f"F{resize}"
        elif c == "0":
            # 0: half (int args), ignore 'Q' size modifier.
            resize = get_resize(default_width, 16)
            cur_mod = f"Fq{resize}"
        elif c == "1":
            # 1: half (int args), force 'Q' size modifier.
            resize = get_resize(default_width, 16)
            cur_mod = f"FQ{resize}"

        if len(cur_mod) == 0:
            raise Exception(f"WTF: {c} in {name}")

        if key_type != 0 and key_type == i:
            cur_mod += "!"

        if len(cur_mod) == 1:
            res += cur_mod
        else:
            res += "(" + cur_mod + ")"

    return res


def replace_insts(m):
    start, end = m.span("proto")
    start -= m.start()
    end -= m.start()
    new_proto = remap_protocol(m["proto"], m["kinds"], m["name"])
    return m.group()[:start] + new_proto + m.group()[end:]


INST = re.compile(r'Inst<"(?P<name>.*?)",\s*"(?P<proto>.*?)",\s*"(?P<kinds>.*?)"')

new_td = INST.sub(replace_insts, sys.stdin.read())
sys.stdout.write(new_td)

#!/usr/bin/env python

from __future__ import print_function


def analyze_match_table(path):
    # Extract the instruction table.
    data = open(path).read()
    start = data.index("static const MatchEntry MatchTable")
    end = data.index("\n};\n", start)
    lines = data[start:end].split("\n")[1:]

    # Parse the instructions.
    insns = []
    for ln in lines:
        ln = ln.split("{", 1)[1]
        ln = ln.rsplit("}", 1)[0]
        a, bc = ln.split("{", 1)
        b, c = bc.split("}", 1)
        code, string, converter, _ = [s.strip() for s in a.split(",")]
        items = [s.strip() for s in b.split(",")]
        _, features = [s.strip() for s in c.split(",")]
        assert string[0] == string[-1] == '"'
        string = string[1:-1]
        insns.append((code, string, converter, items, features))

    # For every mnemonic, compute whether or not it can have a carry setting
    # operand and whether or not it can have a predication code.
    mnemonic_flags = {}
    for insn in insns:
        mnemonic = insn[1]
        items = insn[3]
        flags = mnemonic_flags[mnemonic] = mnemonic_flags.get(mnemonic, set())
        flags.update(items)

    mnemonics = set(mnemonic_flags)
    ccout_mnemonics = set(m for m in mnemonics if "MCK_CCOut" in mnemonic_flags[m])
    condcode_mnemonics = set(
        m for m in mnemonics if "MCK_CondCode" in mnemonic_flags[m]
    )
    noncondcode_mnemonics = mnemonics - condcode_mnemonics
    print(" || ".join('Mnemonic == "%s"' % m for m in ccout_mnemonics))
    print(" || ".join('Mnemonic == "%s"' % m for m in noncondcode_mnemonics))


def main():
    import sys

    if len(sys.argv) == 1:
        import os
        from lit.Util import capture

        llvm_obj_root = capture(["llvm-config", "--obj-root"])
        file = os.path.join(llvm_obj_root, "lib/Target/ARM/ARMGenAsmMatcher.inc")
    elif len(sys.argv) == 2:
        file = sys.argv[1]
    else:
        raise NotImplementedError

    analyze_match_table(file)


if __name__ == "__main__":
    main()

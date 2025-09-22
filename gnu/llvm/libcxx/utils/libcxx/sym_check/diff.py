# -*- Python -*- vim: set syntax=python tabstop=4 expandtab cc=80:
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""
diff - A set of functions for diff-ing two symbol lists.
"""

from libcxx.sym_check import util


def _symbol_difference(lhs, rhs):
    lhs_names = set(((n["name"], n["type"]) for n in lhs))
    rhs_names = set(((n["name"], n["type"]) for n in rhs))
    diff_names = lhs_names - rhs_names
    return [n for n in lhs if (n["name"], n["type"]) in diff_names]


def _find_by_key(sym_list, k):
    for sym in sym_list:
        if sym["name"] == k:
            return sym
    return None


def added_symbols(old, new):
    return _symbol_difference(new, old)


def removed_symbols(old, new):
    return _symbol_difference(old, new)


def changed_symbols(old, new):
    changed = []
    for old_sym in old:
        if old_sym in new:
            continue
        new_sym = _find_by_key(new, old_sym["name"])
        if new_sym is not None and not new_sym in old and old_sym != new_sym:
            changed += [(old_sym, new_sym)]
    return changed


def diff(old, new):
    added = added_symbols(old, new)
    removed = removed_symbols(old, new)
    changed = changed_symbols(old, new)
    return added, removed, changed


def report_diff(
    added_syms, removed_syms, changed_syms, names_only=False, demangle=True
):
    def maybe_demangle(name):
        return util.demangle_symbol(name) if demangle else name

    report = ""
    for sym in added_syms:
        report += "Symbol added: %s\n" % maybe_demangle(sym["name"])
        if not names_only:
            report += "    %s\n\n" % sym
    if added_syms and names_only:
        report += "\n"
    for sym in removed_syms:
        report += "SYMBOL REMOVED: %s\n" % maybe_demangle(sym["name"])
        if not names_only:
            report += "    %s\n\n" % sym
    if removed_syms and names_only:
        report += "\n"
    if not names_only:
        for sym_pair in changed_syms:
            old_sym, new_sym = sym_pair
            old_str = "\n    OLD SYMBOL: %s" % old_sym
            new_str = "\n    NEW SYMBOL: %s" % new_sym
            report += "SYMBOL CHANGED: %s%s%s\n\n" % (
                maybe_demangle(old_sym["name"]),
                old_str,
                new_str,
            )

    added = bool(len(added_syms) != 0)
    abi_break = bool(len(removed_syms))
    if not names_only:
        abi_break = abi_break or len(changed_syms)
    if added or abi_break:
        report += "Summary\n"
        report += "    Added:   %d\n" % len(added_syms)
        report += "    Removed: %d\n" % len(removed_syms)
        if not names_only:
            report += "    Changed: %d\n" % len(changed_syms)
        if not abi_break:
            report += "Symbols added."
        else:
            report += "ABI BREAKAGE: SYMBOLS ADDED OR REMOVED!"
    else:
        report += "Symbols match."
    is_different = abi_break or bool(len(added_syms)) or bool(len(changed_syms))
    return report, abi_break, is_different

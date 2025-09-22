#!/usr/bin/env python3
import re, sys


def fix_string(s):
    TYPE = re.compile(
        '\s*(i[0-9]+|float|double|x86_fp80|fp128|ppc_fp128|\[\[.*?\]\]|\[2 x \[\[[A-Z_0-9]+\]\]\]|<.*?>|{.*?}|\[[0-9]+ x .*?\]|%["a-z:A-Z0-9._]+({{.*?}})?|%{{.*?}}|{{.*?}}|\[\[.*?\]\])(\s*(\*|addrspace\(.*?\)|dereferenceable\(.*?\)|byval\(.*?\)|sret|zeroext|inreg|returned|signext|nocapture|align \d+|swiftself|swifterror|readonly|noalias|inalloca|nocapture))*\s*'
    )

    counter = 0
    if "i32{{.*}}" in s:
        counter = 1

    at_pos = s.find("@")
    if at_pos == -1:
        at_pos = 0

    annoying_pos = s.find("{{[^(]+}}")
    if annoying_pos != -1:
        at_pos = annoying_pos + 9

    paren_pos = s.find("(", at_pos)
    if paren_pos == -1:
        return s

    res = s[: paren_pos + 1]
    s = s[paren_pos + 1 :]

    m = TYPE.match(s)
    while m:
        res += m.group()
        s = s[m.end() :]
        if s.startswith(",") or s.startswith(")"):
            res += f" %{counter}"
            counter += 1

        next_arg = s.find(",")
        if next_arg == -1:
            break

        res += s[: next_arg + 1]
        s = s[next_arg + 1 :]
        m = TYPE.match(s)

    return res + s


def process_file(contents):
    PREFIX = re.compile(r"check-prefix(es)?(=|\s+)([a-zA-Z0-9,]+)")
    check_prefixes = ["CHECK"]
    result = ""
    for line in contents.split("\n"):
        if "FileCheck" in line:
            m = PREFIX.search(line)
            if m:
                check_prefixes.extend(m.group(3).split(","))

        found_check = False
        for prefix in check_prefixes:
            if prefix in line:
                found_check = True
                break

        if not found_check or "define" not in line:
            result += line + "\n"
            continue

        # We have a check for a function definition. Number the args.
        line = fix_string(line)
        result += line + "\n"
    return result


def main():
    print(f"Processing {sys.argv[1]}")
    f = open(sys.argv[1])
    content = f.read()
    f.close()

    content = process_file(content)

    f = open(sys.argv[1], "w")
    f.write(content)
    f.close()


if __name__ == "__main__":
    main()

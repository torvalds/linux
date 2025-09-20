#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Test that truncation of bprm->buf doesn't cause unexpected execs paths, along
# with various other pathological cases.
import os, subprocess

# Relevant commits
#
# b5372fe5dc84 ("exec: load_script: Do not exec truncated interpreter path")
# 6eb3c3d0a52d ("exec: increase BINPRM_BUF_SIZE to 256")

# BINPRM_BUF_SIZE
SIZE=256

NAME_MAX=int(subprocess.check_output(["getconf", "NAME_MAX", "."]))

test_num=0
pass_num=0
fail_num=0

code='''#!/usr/bin/perl
print "Executed interpreter! Args:\n";
print "0 : '$0'\n";
$counter = 1;
foreach my $a (@ARGV) {
    print "$counter : '$a'\n";
    $counter++;
}
'''

##
# test - produce a binfmt_script hashbang line for testing
#
# @size:     bytes for bprm->buf line, including hashbang but not newline
# @good:     whether this script is expected to execute correctly
# @hashbang: the special 2 bytes for running binfmt_script
# @leading:  any leading whitespace before the executable path
# @root:     start of executable pathname
# @target:   end of executable pathname
# @arg:      bytes following the executable pathname
# @fill:     character to fill between @root and @target to reach @size bytes
# @newline:  character to use as newline, not counted towards @size
# ...
def test(name, size, good=True, leading="", root="./", target="/perl",
                     fill="A", arg="", newline="\n", hashbang="#!"):
    global test_num, pass_num, fail_num, tests, NAME_MAX
    test_num += 1
    if test_num > tests:
        raise ValueError("more binfmt_script tests than expected! (want %d, expected %d)"
                         % (test_num, tests))

    middle = ""
    remaining = size - len(hashbang) - len(leading) - len(root) - len(target) - len(arg)
    # The middle of the pathname must not exceed NAME_MAX
    while remaining >= NAME_MAX:
        middle += fill * (NAME_MAX - 1)
        middle += '/'
        remaining -= NAME_MAX
    middle += fill * remaining

    dirpath = root + middle
    binary = dirpath + target
    if len(target):
        os.makedirs(dirpath, mode=0o755, exist_ok=True)
        open(binary, "w").write(code)
        os.chmod(binary, 0o755)

    buf=hashbang + leading + root + middle + target + arg + newline
    if len(newline) > 0:
        buf += 'echo this is not really perl\n'

    script = "binfmt_script-%s" % (name)
    open(script, "w").write(buf)
    os.chmod(script, 0o755)

    proc = subprocess.Popen(["./%s" % (script)], shell=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    stdout = proc.communicate()[0]

    if proc.returncode == 0 and b'Executed interpreter' in stdout:
        if good:
            print("ok %d - binfmt_script %s (successful good exec)"
                  % (test_num, name))
            pass_num += 1
        else:
            print("not ok %d - binfmt_script %s succeeded when it should have failed"
                  % (test_num, name))
            fail_num = 1
    else:
        if good:
            print("not ok %d - binfmt_script %s failed when it should have succeeded (rc:%d)"
                  % (test_num, name, proc.returncode))
            fail_num = 1
        else:
            print("ok %d - binfmt_script %s (correctly failed bad exec)"
                  % (test_num, name))
            pass_num += 1

    # Clean up crazy binaries
    os.unlink(script)
    if len(target):
        elements = binary.split('/')
        os.unlink(binary)
        elements.pop()
        while len(elements) > 1:
            os.rmdir("/".join(elements))
            elements.pop()

tests=27
print("TAP version 1.3")
print("1..%d" % (tests))

### FAIL (8 tests)

# Entire path is well past the BINFMT_BUF_SIZE.
test(name="too-big",        size=SIZE+80, good=False)
# Path is right at max size, making it impossible to tell if it was truncated.
test(name="exact",          size=SIZE,    good=False)
# Same as above, but with leading whitespace.
test(name="exact-space",    size=SIZE,    good=False, leading=" ")
# Huge buffer of only whitespace.
test(name="whitespace-too-big", size=SIZE+71, good=False, root="",
                                              fill=" ", target="")
# A good path, but it gets truncated due to leading whitespace.
test(name="truncated",      size=SIZE+17, good=False, leading=" " * 19)
# Entirely empty except for #!
test(name="empty",          size=2,       good=False, root="",
                                          fill="", target="", newline="")
# Within size, but entirely spaces
test(name="spaces",         size=SIZE-1,  good=False, root="", fill=" ",
                                          target="", newline="")
# Newline before binary.
test(name="newline-prefix", size=SIZE-1,  good=False, leading="\n",
                                          root="", fill=" ", target="")

### ok (19 tests)

# The original test case that was broken by commit:
# 8099b047ecc4 ("exec: load_script: don't blindly truncate shebang string")
test(name="test.pl",        size=439, leading=" ",
     root="./nix/store/bwav8kz8b3y471wjsybgzw84mrh4js9-perl-5.28.1/bin",
     arg=" -I/nix/store/x6yyav38jgr924nkna62q3pkp0dgmzlx-perl5.28.1-File-Slurp-9999.25/lib/perl5/site_perl -I/nix/store/ha8v67sl8dac92r9z07vzr4gv1y9nwqz-perl5.28.1-Net-DBus-1.1.0/lib/perl5/site_perl -I/nix/store/dcrkvnjmwh69ljsvpbdjjdnqgwx90a9d-perl5.28.1-XML-Parser-2.44/lib/perl5/site_perl -I/nix/store/rmji88k2zz7h4zg97385bygcydrf2q8h-perl5.28.1-XML-Twig-3.52/lib/perl5/site_perl")
# One byte under size, leaving newline visible.
test(name="one-under",           size=SIZE-1)
# Two bytes under size, leaving newline visible.
test(name="two-under",           size=SIZE-2)
# Exact size, but trailing whitespace visible instead of newline
test(name="exact-trunc-whitespace", size=SIZE, arg=" ")
# Exact size, but trailing space and first arg char visible instead of newline.
test(name="exact-trunc-arg",     size=SIZE, arg=" f")
# One bute under, with confirmed non-truncated arg since newline now visible.
test(name="one-under-full-arg",  size=SIZE-1, arg=" f")
# Short read buffer by one byte.
test(name="one-under-no-nl",     size=SIZE-1, newline="")
# Short read buffer by half buffer size.
test(name="half-under-no-nl",    size=int(SIZE/2), newline="")
# One byte under with whitespace arg. leaving wenline visible.
test(name="one-under-trunc-arg", size=SIZE-1, arg=" ")
# One byte under with whitespace leading. leaving wenline visible.
test(name="one-under-leading",   size=SIZE-1, leading=" ")
# One byte under with whitespace leading and as arg. leaving newline visible.
test(name="one-under-leading-trunc-arg",  size=SIZE-1, leading=" ", arg=" ")
# Same as above, but with 2 bytes under
test(name="two-under-no-nl",     size=SIZE-2, newline="")
test(name="two-under-trunc-arg", size=SIZE-2, arg=" ")
test(name="two-under-leading",   size=SIZE-2, leading=" ")
test(name="two-under-leading-trunc-arg",   size=SIZE-2, leading=" ", arg=" ")
# Same as above, but with buffer half filled
test(name="two-under-no-nl",     size=int(SIZE/2), newline="")
test(name="two-under-trunc-arg", size=int(SIZE/2), arg=" ")
test(name="two-under-leading",   size=int(SIZE/2), leading=" ")
test(name="two-under-lead-trunc-arg", size=int(SIZE/2), leading=" ", arg=" ")

print("# Totals: pass:%d fail:%d xfail:0 xpass:0 skip:0 error:0" % (pass_num, fail_num))

if test_num != tests:
    raise ValueError("fewer binfmt_script tests than expected! (ran %d, expected %d"
                     % (test_num, tests))

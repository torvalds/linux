#!/usr/bin/env python

from __future__ import absolute_import, division, print_function
import os
import re
import subprocess
import sys
import tempfile

###


class DeltaAlgorithm(object):
    def __init__(self):
        self.cache = set()

    def test(self, changes):
        abstract

    ###

    def getTestResult(self, changes):
        # There is no reason to cache successful tests because we will
        # always reduce the changeset when we see one.

        changeset = frozenset(changes)
        if changeset in self.cache:
            return False
        elif not self.test(changes):
            self.cache.add(changeset)
            return False
        else:
            return True

    def run(self, changes, force=False):
        # Make sure the initial test passes, if not then (a) either
        # the user doesn't expect monotonicity, and we may end up
        # doing O(N^2) tests, or (b) the test is wrong. Avoid the
        # O(N^2) case unless user requests it.
        if not force:
            if not self.getTestResult(changes):
                raise ValueError("Initial test passed to delta fails.")

        # Check empty set first to quickly find poor test functions.
        if self.getTestResult(set()):
            return set()
        else:
            return self.delta(changes, self.split(changes))

    def split(self, S):
        """split(set) -> [sets]

        Partition a set into one or two pieces.
        """

        # There are many ways to split, we could do a better job with more
        # context information (but then the API becomes grosser).
        L = list(S)
        mid = len(L) // 2
        if mid == 0:
            return (L,)
        else:
            return L[:mid], L[mid:]

    def delta(self, c, sets):
        # assert(reduce(set.union, sets, set()) == c)

        # If there is nothing left we can remove, we are done.
        if len(sets) <= 1:
            return c

        # Look for a passing subset.
        res = self.search(c, sets)
        if res is not None:
            return res

        # Otherwise, partition sets if possible; if not we are done.
        refined = sum(map(list, map(self.split, sets)), [])
        if len(refined) == len(sets):
            return c

        return self.delta(c, refined)

    def search(self, c, sets):
        for i, S in enumerate(sets):
            # If test passes on this subset alone, recurse.
            if self.getTestResult(S):
                return self.delta(S, self.split(S))

            # Otherwise if we have more than two sets, see if test
            # pases without this subset.
            if len(sets) > 2:
                complement = sum(sets[:i] + sets[i + 1 :], [])
                if self.getTestResult(complement):
                    return self.delta(complement, sets[:i] + sets[i + 1 :])


###


class Token(object):
    def __init__(self, type, data, flags, file, line, column):
        self.type = type
        self.data = data
        self.flags = flags
        self.file = file
        self.line = line
        self.column = column


kTokenRE = re.compile(
    r"""([a-z_]+) '(.*)'\t(.*)\tLoc=<(.*):(.*):(.*)>""", re.DOTALL | re.MULTILINE
)


def getTokens(path):
    p = subprocess.Popen(
        ["clang", "-dump-raw-tokens", path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    out, err = p.communicate()

    tokens = []
    collect = None
    for ln in err.split("\n"):
        # Silly programmers refuse to print in simple machine readable
        # formats. Whatever.
        if collect is None:
            collect = ln
        else:
            collect = collect + "\n" + ln
        if "Loc=<" in ln and ln.endswith(">"):
            ln, collect = collect, None
            tokens.append(Token(*kTokenRE.match(ln).groups()))

    return tokens


###


class TMBDDelta(DeltaAlgorithm):
    def __init__(self, testProgram, tokenLists, log):
        def patchName(name, suffix):
            base, ext = os.path.splitext(name)
            return base + "." + suffix + ext

        super(TMBDDelta, self).__init__()
        self.testProgram = testProgram
        self.tokenLists = tokenLists
        self.tempFiles = [patchName(f, "tmp") for f, _ in self.tokenLists]
        self.targetFiles = [patchName(f, "ok") for f, _ in self.tokenLists]
        self.log = log
        self.numTests = 0

    def writeFiles(self, changes, fileNames):
        assert len(fileNames) == len(self.tokenLists)
        byFile = [[] for i in self.tokenLists]
        for i, j in changes:
            byFile[i].append(j)

        for i, (file, tokens) in enumerate(self.tokenLists):
            f = open(fileNames[i], "w")
            for j in byFile[i]:
                f.write(tokens[j])
            f.close()

        return byFile

    def test(self, changes):
        self.numTests += 1

        byFile = self.writeFiles(changes, self.tempFiles)

        if self.log:
            print("TEST - ", end=" ", file=sys.stderr)
            if self.log > 1:
                for i, (file, _) in enumerate(self.tokenLists):
                    indices = byFile[i]
                    if i:
                        sys.stderr.write("\n      ")
                    sys.stderr.write("%s:%d tokens: [" % (file, len(byFile[i])))
                    prev = None
                    for j in byFile[i]:
                        if prev is None or j != prev + 1:
                            if prev:
                                sys.stderr.write("%d][" % prev)
                            sys.stderr.write(str(j))
                            sys.stderr.write(":")
                        prev = j
                    if byFile[i]:
                        sys.stderr.write(str(byFile[i][-1]))
                    sys.stderr.write("] ")
            else:
                print(
                    ", ".join(
                        [
                            "%s:%d tokens" % (file, len(byFile[i]))
                            for i, (file, _) in enumerate(self.tokenLists)
                        ]
                    ),
                    end=" ",
                    file=sys.stderr,
                )

        p = subprocess.Popen([self.testProgram] + self.tempFiles)
        res = p.wait() == 0

        if res:
            self.writeFiles(changes, self.targetFiles)

        if self.log:
            print("=> %s" % res, file=sys.stderr)
        else:
            if res:
                print("\nSUCCESS (%d tokens)" % len(changes))
            else:
                sys.stderr.write(".")

        return res

    def run(self):
        res = super(TMBDDelta, self).run(
            [
                (i, j)
                for i, (file, tokens) in enumerate(self.tokenLists)
                for j in range(len(tokens))
            ]
        )
        self.writeFiles(res, self.targetFiles)
        if not self.log:
            print(file=sys.stderr)
        return res


def tokenBasedMultiDelta(program, files, log):
    # Read in the lists of tokens.
    tokenLists = [(file, [t.data for t in getTokens(file)]) for file in files]

    numTokens = sum([len(tokens) for _, tokens in tokenLists])
    print("Delta on %s with %d tokens." % (", ".join(files), numTokens))

    tbmd = TMBDDelta(program, tokenLists, log)

    res = tbmd.run()

    print(
        "Finished %s with %d tokens (in %d tests)."
        % (", ".join(tbmd.targetFiles), len(res), tbmd.numTests)
    )


def main():
    from optparse import OptionParser, OptionGroup

    parser = OptionParser("%prog <test program> {files+}")
    parser.add_option(
        "",
        "--debug",
        dest="debugLevel",
        help="set debug level [default %default]",
        action="store",
        type=int,
        default=0,
    )
    (opts, args) = parser.parse_args()

    if len(args) <= 1:
        parser.error("Invalid number of arguments.")

    program, files = args[0], args[1:]

    md = tokenBasedMultiDelta(program, files, log=opts.debugLevel)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("Interrupted.", file=sys.stderr)
        os._exit(1)  # Avoid freeing our giant cache.

"""Utilities for enumeration of finite and countably infinite sets.
"""
from __future__ import absolute_import, division, print_function

###
# Countable iteration

# Simplifies some calculations
class Aleph0(int):
    _singleton = None

    def __new__(type):
        if type._singleton is None:
            type._singleton = int.__new__(type)
        return type._singleton

    def __repr__(self):
        return "<aleph0>"

    def __str__(self):
        return "inf"

    def __cmp__(self, b):
        return 1

    def __sub__(self, b):
        raise ValueError("Cannot subtract aleph0")

    __rsub__ = __sub__

    def __add__(self, b):
        return self

    __radd__ = __add__

    def __mul__(self, b):
        if b == 0:
            return b
        return self

    __rmul__ = __mul__

    def __floordiv__(self, b):
        if b == 0:
            raise ZeroDivisionError
        return self

    __rfloordiv__ = __floordiv__
    __truediv__ = __floordiv__
    __rtuediv__ = __floordiv__
    __div__ = __floordiv__
    __rdiv__ = __floordiv__

    def __pow__(self, b):
        if b == 0:
            return 1
        return self


aleph0 = Aleph0()


def base(line):
    return line * (line + 1) // 2


def pairToN(pair):
    x, y = pair
    line, index = x + y, y
    return base(line) + index


def getNthPairInfo(N):
    # Avoid various singularities
    if N == 0:
        return (0, 0)

    # Gallop to find bounds for line
    line = 1
    next = 2
    while base(next) <= N:
        line = next
        next = line << 1

    # Binary search for starting line
    lo = line
    hi = line << 1
    while lo + 1 != hi:
        # assert base(lo) <= N < base(hi)
        mid = (lo + hi) >> 1
        if base(mid) <= N:
            lo = mid
        else:
            hi = mid

    line = lo
    return line, N - base(line)


def getNthPair(N):
    line, index = getNthPairInfo(N)
    return (line - index, index)


def getNthPairBounded(N, W=aleph0, H=aleph0, useDivmod=False):
    """getNthPairBounded(N, W, H) -> (x, y)

    Return the N-th pair such that 0 <= x < W and 0 <= y < H."""

    if W <= 0 or H <= 0:
        raise ValueError("Invalid bounds")
    elif N >= W * H:
        raise ValueError("Invalid input (out of bounds)")

    # Simple case...
    if W is aleph0 and H is aleph0:
        return getNthPair(N)

    # Otherwise simplify by assuming W < H
    if H < W:
        x, y = getNthPairBounded(N, H, W, useDivmod=useDivmod)
        return y, x

    if useDivmod:
        return N % W, N // W
    else:
        # Conceptually we want to slide a diagonal line across a
        # rectangle. This gives more interesting results for large
        # bounds than using divmod.

        # If in lower left, just return as usual
        cornerSize = base(W)
        if N < cornerSize:
            return getNthPair(N)

        # Otherwise if in upper right, subtract from corner
        if H is not aleph0:
            M = W * H - N - 1
            if M < cornerSize:
                x, y = getNthPair(M)
                return (W - 1 - x, H - 1 - y)

        # Otherwise, compile line and index from number of times we
        # wrap.
        N = N - cornerSize
        index, offset = N % W, N // W
        # p = (W-1, 1+offset) + (-1,1)*index
        return (W - 1 - index, 1 + offset + index)


def getNthPairBoundedChecked(
    N, W=aleph0, H=aleph0, useDivmod=False, GNP=getNthPairBounded
):
    x, y = GNP(N, W, H, useDivmod)
    assert 0 <= x < W and 0 <= y < H
    return x, y


def getNthNTuple(N, W, H=aleph0, useLeftToRight=False):
    """getNthNTuple(N, W, H) -> (x_0, x_1, ..., x_W)

    Return the N-th W-tuple, where for 0 <= x_i < H."""

    if useLeftToRight:
        elts = [None] * W
        for i in range(W):
            elts[i], N = getNthPairBounded(N, H)
        return tuple(elts)
    else:
        if W == 0:
            return ()
        elif W == 1:
            return (N,)
        elif W == 2:
            return getNthPairBounded(N, H, H)
        else:
            LW, RW = W // 2, W - (W // 2)
            L, R = getNthPairBounded(N, H**LW, H**RW)
            return getNthNTuple(
                L, LW, H=H, useLeftToRight=useLeftToRight
            ) + getNthNTuple(R, RW, H=H, useLeftToRight=useLeftToRight)


def getNthNTupleChecked(N, W, H=aleph0, useLeftToRight=False, GNT=getNthNTuple):
    t = GNT(N, W, H, useLeftToRight)
    assert len(t) == W
    for i in t:
        assert i < H
    return t


def getNthTuple(
    N, maxSize=aleph0, maxElement=aleph0, useDivmod=False, useLeftToRight=False
):
    """getNthTuple(N, maxSize, maxElement) -> x

    Return the N-th tuple where len(x) < maxSize and for y in x, 0 <=
    y < maxElement."""

    # All zero sized tuples are isomorphic, don't ya know.
    if N == 0:
        return ()
    N -= 1
    if maxElement is not aleph0:
        if maxSize is aleph0:
            raise NotImplementedError("Max element size without max size unhandled")
        bounds = [maxElement**i for i in range(1, maxSize + 1)]
        S, M = getNthPairVariableBounds(N, bounds)
    else:
        S, M = getNthPairBounded(N, maxSize, useDivmod=useDivmod)
    return getNthNTuple(M, S + 1, maxElement, useLeftToRight=useLeftToRight)


def getNthTupleChecked(
    N,
    maxSize=aleph0,
    maxElement=aleph0,
    useDivmod=False,
    useLeftToRight=False,
    GNT=getNthTuple,
):
    # FIXME: maxsize is inclusive
    t = GNT(N, maxSize, maxElement, useDivmod, useLeftToRight)
    assert len(t) <= maxSize
    for i in t:
        assert i < maxElement
    return t


def getNthPairVariableBounds(N, bounds):
    """getNthPairVariableBounds(N, bounds) -> (x, y)

    Given a finite list of bounds (which may be finite or aleph0),
    return the N-th pair such that 0 <= x < len(bounds) and 0 <= y <
    bounds[x]."""

    if not bounds:
        raise ValueError("Invalid bounds")
    if not (0 <= N < sum(bounds)):
        raise ValueError("Invalid input (out of bounds)")

    level = 0
    active = list(range(len(bounds)))
    active.sort(key=lambda i: bounds[i])
    prevLevel = 0
    for i, index in enumerate(active):
        level = bounds[index]
        W = len(active) - i
        if level is aleph0:
            H = aleph0
        else:
            H = level - prevLevel
        levelSize = W * H
        if N < levelSize:  # Found the level
            idelta, delta = getNthPairBounded(N, W, H)
            return active[i + idelta], prevLevel + delta
        else:
            N -= levelSize
            prevLevel = level
    else:
        raise RuntimError("Unexpected loop completion")


def getNthPairVariableBoundsChecked(N, bounds, GNVP=getNthPairVariableBounds):
    x, y = GNVP(N, bounds)
    assert 0 <= x < len(bounds) and 0 <= y < bounds[x]
    return (x, y)


###


def testPairs():
    W = 3
    H = 6
    a = [["  " for x in range(10)] for y in range(10)]
    b = [["  " for x in range(10)] for y in range(10)]
    for i in range(min(W * H, 40)):
        x, y = getNthPairBounded(i, W, H)
        x2, y2 = getNthPairBounded(i, W, H, useDivmod=True)
        print(i, (x, y), (x2, y2))
        a[y][x] = "%2d" % i
        b[y2][x2] = "%2d" % i

    print("-- a --")
    for ln in a[::-1]:
        if "".join(ln).strip():
            print("  ".join(ln))
    print("-- b --")
    for ln in b[::-1]:
        if "".join(ln).strip():
            print("  ".join(ln))


def testPairsVB():
    bounds = [2, 2, 4, aleph0, 5, aleph0]
    a = [["  " for x in range(15)] for y in range(15)]
    b = [["  " for x in range(15)] for y in range(15)]
    for i in range(min(sum(bounds), 40)):
        x, y = getNthPairVariableBounds(i, bounds)
        print(i, (x, y))
        a[y][x] = "%2d" % i

    print("-- a --")
    for ln in a[::-1]:
        if "".join(ln).strip():
            print("  ".join(ln))


###

# Toggle to use checked versions of enumeration routines.
if False:
    getNthPairVariableBounds = getNthPairVariableBoundsChecked
    getNthPairBounded = getNthPairBoundedChecked
    getNthNTuple = getNthNTupleChecked
    getNthTuple = getNthTupleChecked

if __name__ == "__main__":
    testPairs()

    testPairsVB()

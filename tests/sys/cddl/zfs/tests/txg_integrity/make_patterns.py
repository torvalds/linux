#! /usr/bin/env python

# $FreeBSD$

# Generate random IO patterns for the txg_integrity test
# We do this statically and embed the results into the code so that the
# Testing will be more repeatable compared to generating the tables at runtime

import random

CLUSTERSIZE = (1 << 16)
NUM_CHUNKS = 64


def rand_partition():
    partitions = []
    while len(partitions) != NUM_CHUNKS:
        # We don't want any duplicates, so we make a set and then check that
        # its length is correct
        partitions = sorted(
            list(
                set(
                    [random.randrange(0,
                                      2**31,
                                      (2**31) * 8 / (NUM_CHUNKS * CLUSTERSIZE))
                        for i in range(NUM_CHUNKS - 1)] + [2**31])))
    return partitions


def rand_permutation():
    perm = range(NUM_CHUNKS)
    random.shuffle(perm)
    return perm


def rand_follower_bitmap():
    bmp = 0
    chunks = random.sample(range(NUM_CHUNKS), NUM_CHUNKS / 2)
    for chunk in chunks:
        bmp |= (1 << chunk)
    return bmp


def print_pattern(n):
    print "const pattern_t pat%d = {" % n
    print "  {",
    for p in rand_partition():
        print "%#x, " % p,
    print "  },"
    print "  {",
    for p in rand_permutation():
        print "%d, " % p,
    print "  },"
    print "  %#x" % rand_follower_bitmap()
    print "};"


for n in range(32):
    print_pattern(n)

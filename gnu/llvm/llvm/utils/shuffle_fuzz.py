#!/usr/bin/env python

"""A shuffle vector fuzz tester.

This is a python program to fuzz test the LLVM shufflevector instruction. It
generates a function with a random sequnece of shufflevectors, maintaining the
element mapping accumulated across the function. It then generates a main
function which calls it with a different value in each element and checks that
the result matches the expected mapping.

Take the output IR printed to stdout, compile it to an executable using whatever
set of transforms you want to test, and run the program. If it crashes, it found
a bug.
"""

from __future__ import print_function

import argparse
import itertools
import random
import sys
import uuid


def main():
    element_types = ["i8", "i16", "i32", "i64", "f32", "f64"]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Show verbose output"
    )
    parser.add_argument(
        "--seed", default=str(uuid.uuid4()), help="A string used to seed the RNG"
    )
    parser.add_argument(
        "--max-shuffle-height",
        type=int,
        default=16,
        help="Specify a fixed height of shuffle tree to test",
    )
    parser.add_argument(
        "--no-blends",
        dest="blends",
        action="store_false",
        help="Include blends of two input vectors",
    )
    parser.add_argument(
        "--fixed-bit-width",
        type=int,
        choices=[128, 256],
        help="Specify a fixed bit width of vector to test",
    )
    parser.add_argument(
        "--fixed-element-type",
        choices=element_types,
        help="Specify a fixed element type to test",
    )
    parser.add_argument("--triple", help="Specify a triple string to include in the IR")
    args = parser.parse_args()

    random.seed(args.seed)

    if args.fixed_element_type is not None:
        element_types = [args.fixed_element_type]

    if args.fixed_bit_width is not None:
        if args.fixed_bit_width == 128:
            width_map = {"i64": 2, "i32": 4, "i16": 8, "i8": 16, "f64": 2, "f32": 4}
            (width, element_type) = random.choice(
                [(width_map[t], t) for t in element_types]
            )
        elif args.fixed_bit_width == 256:
            width_map = {"i64": 4, "i32": 8, "i16": 16, "i8": 32, "f64": 4, "f32": 8}
            (width, element_type) = random.choice(
                [(width_map[t], t) for t in element_types]
            )
        else:
            sys.exit(1)  # Checked above by argument parsing.
    else:
        width = random.choice([2, 4, 8, 16, 32, 64])
        element_type = random.choice(element_types)

    element_modulus = {
        "i8": 1 << 8,
        "i16": 1 << 16,
        "i32": 1 << 32,
        "i64": 1 << 64,
        "f32": 1 << 32,
        "f64": 1 << 64,
    }[element_type]

    shuffle_range = (2 * width) if args.blends else width

    # Because undef (-1) saturates and is indistinguishable when testing the
    # correctness of a shuffle, we want to bias our fuzz toward having a decent
    # mixture of non-undef lanes in the end. With a deep shuffle tree, the
    # probabilies aren't good so we need to bias things. The math here is that if
    # we uniformly select between -1 and the other inputs, each element of the
    # result will have the following probability of being undef:
    #
    #   1 - (shuffle_range/(shuffle_range+1))^max_shuffle_height
    #
    # More generally, for any probability P of selecting a defined element in
    # a single shuffle, the end result is:
    #
    #   1 - P^max_shuffle_height
    #
    # The power of the shuffle height is the real problem, as we want:
    #
    #   1 - shuffle_range/(shuffle_range+1)
    #
    # So we bias the selection of undef at any given node based on the tree
    # height. Below, let 'A' be 'len(shuffle_range)', 'C' be 'max_shuffle_height',
    # and 'B' be the bias we use to compensate for
    # C '((A+1)*A^(1/C))/(A*(A+1)^(1/C))':
    #
    #   1 - (B * A)/(A + 1)^C = 1 - A/(A + 1)
    #
    # So at each node we use:
    #
    #   1 - (B * A)/(A + 1)
    # = 1 - ((A + 1) * A * A^(1/C))/(A * (A + 1) * (A + 1)^(1/C))
    # = 1 - ((A + 1) * A^((C + 1)/C))/(A * (A + 1)^((C + 1)/C))
    #
    # This is the formula we use to select undef lanes in the shuffle.
    A = float(shuffle_range)
    C = float(args.max_shuffle_height)
    undef_prob = 1.0 - (
        ((A + 1.0) * pow(A, (C + 1.0) / C)) / (A * pow(A + 1.0, (C + 1.0) / C))
    )

    shuffle_tree = [
        [
            [
                -1
                if random.random() <= undef_prob
                else random.choice(range(shuffle_range))
                for _ in itertools.repeat(None, width)
            ]
            for _ in itertools.repeat(None, args.max_shuffle_height - i)
        ]
        for i in range(args.max_shuffle_height)
    ]

    if args.verbose:
        # Print out the shuffle sequence in a compact form.
        print(
            (
                'Testing shuffle sequence "%s" (v%d%s):'
                % (args.seed, width, element_type)
            ),
            file=sys.stderr,
        )
        for i, shuffles in enumerate(shuffle_tree):
            print("  tree level %d:" % (i,), file=sys.stderr)
            for j, s in enumerate(shuffles):
                print("    shuffle %d: %s" % (j, s), file=sys.stderr)
        print("", file=sys.stderr)

    # Symbolically evaluate the shuffle tree.
    inputs = [
        [int(j % element_modulus) for j in range(i * width + 1, (i + 1) * width + 1)]
        for i in range(args.max_shuffle_height + 1)
    ]
    results = inputs
    for shuffles in shuffle_tree:
        results = [
            [
                (
                    (results[i] if j < width else results[i + 1])[j % width]
                    if j != -1
                    else -1
                )
                for j in s
            ]
            for i, s in enumerate(shuffles)
        ]
    if len(results) != 1:
        print("ERROR: Bad results: %s" % (results,), file=sys.stderr)
        sys.exit(1)
    result = results[0]

    if args.verbose:
        print("Which transforms:", file=sys.stderr)
        print("  from: %s" % (inputs,), file=sys.stderr)
        print("  into: %s" % (result,), file=sys.stderr)
        print("", file=sys.stderr)

    # The IR uses silly names for floating point types. We also need a same-size
    # integer type.
    integral_element_type = element_type
    if element_type == "f32":
        integral_element_type = "i32"
        element_type = "float"
    elif element_type == "f64":
        integral_element_type = "i64"
        element_type = "double"

    # Now we need to generate IR for the shuffle function.
    subst = {"N": width, "T": element_type, "IT": integral_element_type}
    print(
        """
define internal fastcc <%(N)d x %(T)s> @test(%(arguments)s) noinline nounwind {
entry:"""
        % dict(
            subst,
            arguments=", ".join(
                [
                    "<%(N)d x %(T)s> %%s.0.%(i)d" % dict(subst, i=i)
                    for i in range(args.max_shuffle_height + 1)
                ]
            ),
        )
    )

    for i, shuffles in enumerate(shuffle_tree):
        for j, s in enumerate(shuffles):
            print(
                """
  %%s.%(next_i)d.%(j)d = shufflevector <%(N)d x %(T)s> %%s.%(i)d.%(j)d, <%(N)d x %(T)s> %%s.%(i)d.%(next_j)d, <%(N)d x i32> <%(S)s>
""".strip(
                    "\n"
                )
                % dict(
                    subst,
                    i=i,
                    next_i=i + 1,
                    j=j,
                    next_j=j + 1,
                    S=", ".join(
                        ["i32 " + (str(si) if si != -1 else "undef") for si in s]
                    ),
                )
            )

    print(
        """
  ret <%(N)d x %(T)s> %%s.%(i)d.0
}
"""
        % dict(subst, i=len(shuffle_tree))
    )

    # Generate some string constants that we can use to report errors.
    for i, r in enumerate(result):
        if r != -1:
            s = (
                "FAIL(%(seed)s): lane %(lane)d, expected %(result)d, found %%d\n\\0A"
                % {"seed": args.seed, "lane": i, "result": r}
            )
            s += "".join(["\\00" for _ in itertools.repeat(None, 128 - len(s) + 2)])
            print(
                """
@error.%(i)d = private unnamed_addr global [128 x i8] c"%(s)s"
""".strip()
                % {"i": i, "s": s}
            )

    # Define a wrapper function which is marked 'optnone' to prevent
    # interprocedural optimizations from deleting the test.
    print(
        """
define internal fastcc <%(N)d x %(T)s> @test_wrapper(%(arguments)s) optnone noinline {
  %%result = call fastcc <%(N)d x %(T)s> @test(%(arguments)s)
  ret <%(N)d x %(T)s> %%result
}
"""
        % dict(
            subst,
            arguments=", ".join(
                [
                    "<%(N)d x %(T)s> %%s.%(i)d" % dict(subst, i=i)
                    for i in range(args.max_shuffle_height + 1)
                ]
            ),
        )
    )

    # Finally, generate a main function which will trap if any lanes are mapped
    # incorrectly (in an observable way).
    print(
        """
define i32 @main() {
entry:
  ; Create a scratch space to print error messages.
  %%str = alloca [128 x i8]
  %%str.ptr = getelementptr inbounds [128 x i8], [128 x i8]* %%str, i32 0, i32 0

  ; Build the input vector and call the test function.
  %%v = call fastcc <%(N)d x %(T)s> @test_wrapper(%(inputs)s)
  ; We need to cast this back to an integer type vector to easily check the
  ; result.
  %%v.cast = bitcast <%(N)d x %(T)s> %%v to <%(N)d x %(IT)s>
  br label %%test.0
"""
        % dict(
            subst,
            inputs=", ".join(
                [
                    (
                        "<%(N)d x %(T)s> bitcast "
                        "(<%(N)d x %(IT)s> <%(input)s> to <%(N)d x %(T)s>)"
                        % dict(
                            subst,
                            input=", ".join(
                                ["%(IT)s %(i)d" % dict(subst, i=i) for i in input]
                            ),
                        )
                    )
                    for input in inputs
                ]
            ),
        )
    )

    # Test that each non-undef result lane contains the expected value.
    for i, r in enumerate(result):
        if r == -1:
            print(
                """
test.%(i)d:
  ; Skip this lane, its value is undef.
  br label %%test.%(next_i)d
"""
                % dict(subst, i=i, next_i=i + 1)
            )
        else:
            print(
                """
test.%(i)d:
  %%v.%(i)d = extractelement <%(N)d x %(IT)s> %%v.cast, i32 %(i)d
  %%cmp.%(i)d = icmp ne %(IT)s %%v.%(i)d, %(r)d
  br i1 %%cmp.%(i)d, label %%die.%(i)d, label %%test.%(next_i)d

die.%(i)d:
  ; Capture the actual value and print an error message.
  %%tmp.%(i)d = zext %(IT)s %%v.%(i)d to i2048
  %%bad.%(i)d = trunc i2048 %%tmp.%(i)d to i32
  call i32 (i8*, i8*, ...) @sprintf(i8* %%str.ptr, i8* getelementptr inbounds ([128 x i8], [128 x i8]* @error.%(i)d, i32 0, i32 0), i32 %%bad.%(i)d)
  %%length.%(i)d = call i32 @strlen(i8* %%str.ptr)
  call i32 @write(i32 2, i8* %%str.ptr, i32 %%length.%(i)d)
  call void @llvm.trap()
  unreachable
"""
                % dict(subst, i=i, next_i=i + 1, r=r)
            )

    print(
        """
test.%d:
  ret i32 0
}

declare i32 @strlen(i8*)
declare i32 @write(i32, i8*, i32)
declare i32 @sprintf(i8*, i8*, ...)
declare void @llvm.trap() noreturn nounwind
"""
        % (len(result),)
    )


if __name__ == "__main__":
    main()

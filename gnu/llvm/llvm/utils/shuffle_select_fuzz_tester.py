#!/usr/bin/env python

"""A shuffle-select vector fuzz tester.

This is a python program to fuzz test the LLVM shufflevector and select
instructions. It generates a function with a random sequnece of shufflevectors
while optionally attaching it with a select instruction (regular or zero merge),
maintaining the element mapping accumulated across the function. It then
generates a main function which calls it with a different value in each element
and checks that the result matches the expected mapping.

Take the output IR printed to stdout, compile it to an executable using whatever
set of transforms you want to test, and run the program. If it crashes, it found
a bug (an error message with the expected and actual result is printed).
"""
from __future__ import print_function

import random
import uuid
import argparse

# Possibility of one undef index in generated mask for shufflevector instruction
SHUF_UNDEF_POS = 0.15

# Possibility of one undef index in generated mask for select instruction
SEL_UNDEF_POS = 0.15

# Possibility of adding a select instruction to the result of a shufflevector
ADD_SEL_POS = 0.4

# If we are adding a select instruction, this is the possibility of a
# merge-select instruction (1 - MERGE_SEL_POS = possibility of zero-merge-select
# instruction.
MERGE_SEL_POS = 0.5


test_template = r"""
define internal fastcc {ty} @test({inputs}) noinline nounwind {{
entry:
{instructions}
  ret {ty} {last_name}
}}
"""

error_template = r'''@error.{lane} = private unnamed_addr global [64 x i8] c"FAIL: lane {lane}, expected {exp}, found %d\0A{padding}"'''

main_template = r"""
define i32 @main() {{
entry:
  ; Create a scratch space to print error messages.
  %str = alloca [64 x i8]
  %str.ptr = getelementptr inbounds [64 x i8], [64 x i8]* %str, i32 0, i32 0

  ; Build the input vector and call the test function.
  %v = call fastcc {ty} @test({inputs})
  br label %test.0

  {check_die}
}}

declare i32 @strlen(i8*)
declare i32 @write(i32, i8*, i32)
declare i32 @sprintf(i8*, i8*, ...)
declare void @llvm.trap() noreturn nounwind
"""

check_template = r"""
test.{lane}:
  %v.{lane} = extractelement {ty} %v, i32 {lane}
  %cmp.{lane} = {i_f}cmp {ordered}ne {scalar_ty} %v.{lane}, {exp}
  br i1 %cmp.{lane}, label %die.{lane}, label %test.{n_lane}
"""

undef_check_template = r"""
test.{lane}:
; Skip this lane, its value is undef.
  br label %test.{n_lane}
"""

die_template = r"""
die.{lane}:
; Capture the actual value and print an error message.
  call i32 (i8*, i8*, ...) @sprintf(i8* %str.ptr, i8* getelementptr inbounds ([64 x i8], [64 x i8]* @error.{lane}, i32 0, i32 0), {scalar_ty} %v.{lane})
  %length.{lane} = call i32 @strlen(i8* %str.ptr)
  call i32 @write(i32 2, i8* %str.ptr, i32 %length.{lane})
  call void @llvm.trap()
  unreachable
"""


class Type:
    def __init__(self, is_float, elt_width, elt_num):
        self.is_float = is_float  # Boolean
        self.elt_width = elt_width  # Integer
        self.elt_num = elt_num  # Integer

    def dump(self):
        if self.is_float:
            str_elt = "float" if self.elt_width == 32 else "double"
        else:
            str_elt = "i" + str(self.elt_width)

        if self.elt_num == 1:
            return str_elt
        else:
            return "<" + str(self.elt_num) + " x " + str_elt + ">"

    def get_scalar_type(self):
        return Type(self.is_float, self.elt_width, 1)


# Class to represent any value (variable) that can be used.
class Value:
    def __init__(self, name, ty, value=None):
        self.ty = ty  # Type
        self.name = name  # String
        self.value = value  # list of integers or floating points


# Class to represent an IR instruction (shuffle/select).
class Instruction(Value):
    def __init__(self, name, ty, op0, op1, mask):
        Value.__init__(self, name, ty)
        self.op0 = op0  # Value
        self.op1 = op1  # Value
        self.mask = mask  # list of integers

    def dump(self):
        pass

    def calc_value(self):
        pass


# Class to represent an IR shuffle instruction
class ShufInstr(Instruction):

    shuf_template = (
        "  {name} = shufflevector {ty} {op0}, {ty} {op1}, <{num} x i32> {mask}\n"
    )

    def __init__(self, name, ty, op0, op1, mask):
        Instruction.__init__(self, "%shuf" + name, ty, op0, op1, mask)

    def dump(self):
        str_mask = [
            ("i32 " + str(idx)) if idx != -1 else "i32 undef" for idx in self.mask
        ]
        str_mask = "<" + (", ").join(str_mask) + ">"
        return self.shuf_template.format(
            name=self.name,
            ty=self.ty.dump(),
            op0=self.op0.name,
            op1=self.op1.name,
            num=self.ty.elt_num,
            mask=str_mask,
        )

    def calc_value(self):
        if self.value != None:
            print("Trying to calculate the value of a shuffle instruction twice")
            exit(1)

        result = []
        for i in range(len(self.mask)):
            index = self.mask[i]

            if index < self.ty.elt_num and index >= 0:
                result.append(self.op0.value[index])
            elif index >= self.ty.elt_num:
                index = index % self.ty.elt_num
                result.append(self.op1.value[index])
            else:  # -1 => undef
                result.append(-1)

        self.value = result


# Class to represent an IR select instruction
class SelectInstr(Instruction):

    sel_template = "  {name} = select <{num} x i1> {mask}, {ty} {op0}, {ty} {op1}\n"

    def __init__(self, name, ty, op0, op1, mask):
        Instruction.__init__(self, "%sel" + name, ty, op0, op1, mask)

    def dump(self):
        str_mask = [
            ("i1 " + str(idx)) if idx != -1 else "i1 undef" for idx in self.mask
        ]
        str_mask = "<" + (", ").join(str_mask) + ">"
        return self.sel_template.format(
            name=self.name,
            ty=self.ty.dump(),
            op0=self.op0.name,
            op1=self.op1.name,
            num=self.ty.elt_num,
            mask=str_mask,
        )

    def calc_value(self):
        if self.value != None:
            print("Trying to calculate the value of a select instruction twice")
            exit(1)

        result = []
        for i in range(len(self.mask)):
            index = self.mask[i]

            if index == 1:
                result.append(self.op0.value[i])
            elif index == 0:
                result.append(self.op1.value[i])
            else:  # -1 => undef
                result.append(-1)

        self.value = result


# Returns a list of Values initialized with actual numbers according to the
# provided type
def gen_inputs(ty, num):
    inputs = []
    for i in range(num):
        inp = []
        for j in range(ty.elt_num):
            if ty.is_float:
                inp.append(float(i * ty.elt_num + j))
            else:
                inp.append((i * ty.elt_num + j) % (1 << ty.elt_width))
        inputs.append(Value("%inp" + str(i), ty, inp))

    return inputs


# Returns a random vector type to be tested
# In case one of the dimensions (scalar type/number of elements) is provided,
# fill the blank dimension and return appropriate Type object.
def get_random_type(ty, num_elts):
    if ty != None:
        if ty == "i8":
            is_float = False
            width = 8
        elif ty == "i16":
            is_float = False
            width = 16
        elif ty == "i32":
            is_float = False
            width = 32
        elif ty == "i64":
            is_float = False
            width = 64
        elif ty == "f32":
            is_float = True
            width = 32
        elif ty == "f64":
            is_float = True
            width = 64

    int_elt_widths = [8, 16, 32, 64]
    float_elt_widths = [32, 64]

    if num_elts == None:
        num_elts = random.choice(range(2, 65))

    if ty == None:
        # 1 for integer type, 0 for floating-point
        if random.randint(0, 1):
            is_float = False
            width = random.choice(int_elt_widths)
        else:
            is_float = True
            width = random.choice(float_elt_widths)

    return Type(is_float, width, num_elts)


# Generate mask for shufflevector IR instruction, with SHUF_UNDEF_POS possibility
# of one undef index.
def gen_shuf_mask(ty):
    mask = []
    for i in range(ty.elt_num):
        if SHUF_UNDEF_POS / ty.elt_num > random.random():
            mask.append(-1)
        else:
            mask.append(random.randint(0, ty.elt_num * 2 - 1))

    return mask


# Generate mask for select IR instruction, with SEL_UNDEF_POS possibility
# of one undef index.
def gen_sel_mask(ty):
    mask = []
    for i in range(ty.elt_num):
        if SEL_UNDEF_POS / ty.elt_num > random.random():
            mask.append(-1)
        else:
            mask.append(random.randint(0, 1))

    return mask


# Generate shuffle instructions with optional select instruction after.
def gen_insts(inputs, ty):
    int_zero_init = Value("zeroinitializer", ty, [0] * ty.elt_num)
    float_zero_init = Value("zeroinitializer", ty, [0.0] * ty.elt_num)

    insts = []
    name_idx = 0
    while len(inputs) > 1:
        # Choose 2 available Values - remove them from inputs list.
        [idx0, idx1] = sorted(random.sample(range(len(inputs)), 2))
        op0 = inputs[idx0]
        op1 = inputs[idx1]

        # Create the shuffle instruction.
        shuf_mask = gen_shuf_mask(ty)
        shuf_inst = ShufInstr(str(name_idx), ty, op0, op1, shuf_mask)
        shuf_inst.calc_value()

        # Add the new shuffle instruction to the list of instructions.
        insts.append(shuf_inst)

        # Optionally, add select instruction with the result of the previous shuffle.
        if random.random() < ADD_SEL_POS:
            #  Either blending with a random Value or with an all-zero vector.
            if random.random() < MERGE_SEL_POS:
                op2 = random.choice(inputs)
            else:
                op2 = float_zero_init if ty.is_float else int_zero_init

            select_mask = gen_sel_mask(ty)
            select_inst = SelectInstr(str(name_idx), ty, shuf_inst, op2, select_mask)
            select_inst.calc_value()

            # Add the select instructions to the list of instructions and to the available Values.
            insts.append(select_inst)
            inputs.append(select_inst)
        else:
            # If the shuffle instruction is not followed by select, add it to the available Values.
            inputs.append(shuf_inst)

        del inputs[idx1]
        del inputs[idx0]
        name_idx += 1

    return insts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--seed", default=str(uuid.uuid4()), help="A string used to seed the RNG"
    )
    parser.add_argument(
        "--max-num-inputs",
        type=int,
        default=20,
        help="Specify the maximum number of vector inputs for the test. (default: 20)",
    )
    parser.add_argument(
        "--min-num-inputs",
        type=int,
        default=10,
        help="Specify the minimum number of vector inputs for the test. (default: 10)",
    )
    parser.add_argument(
        "--type",
        default=None,
        help="""
                          Choose specific type to be tested.
                          i8, i16, i32, i64, f32 or f64.
                          (default: random)""",
    )
    parser.add_argument(
        "--num-elts",
        default=None,
        type=int,
        help="Choose specific number of vector elements to be tested. (default: random)",
    )
    args = parser.parse_args()

    print("; The seed used for this test is " + args.seed)

    assert (
        args.min_num_inputs < args.max_num_inputs
    ), "Minimum value greater than maximum."
    assert args.type in [None, "i8", "i16", "i32", "i64", "f32", "f64"], "Illegal type."
    assert (
        args.num_elts == None or args.num_elts > 0
    ), "num_elts must be a positive integer."

    random.seed(args.seed)
    ty = get_random_type(args.type, args.num_elts)
    inputs = gen_inputs(ty, random.randint(args.min_num_inputs, args.max_num_inputs))
    inputs_str = (", ").join([inp.ty.dump() + " " + inp.name for inp in inputs])
    inputs_values = [inp.value for inp in inputs]

    insts = gen_insts(inputs, ty)

    assert len(inputs) == 1, "Only one value should be left after generating phase"
    res = inputs[0]

    # print the actual test function by dumping the generated instructions.
    insts_str = "".join([inst.dump() for inst in insts])
    print(
        test_template.format(
            ty=ty.dump(), inputs=inputs_str, instructions=insts_str, last_name=res.name
        )
    )

    # Print the error message templates as global strings
    for i in range(len(res.value)):
        pad = "".join(["\\00"] * (31 - len(str(i)) - len(str(res.value[i]))))
        print(error_template.format(lane=str(i), exp=str(res.value[i]), padding=pad))

    # Prepare the runtime checks and failure handlers.
    scalar_ty = ty.get_scalar_type()
    check_die = ""
    i_f = "f" if ty.is_float else "i"
    ordered = "o" if ty.is_float else ""
    for i in range(len(res.value)):
        if res.value[i] != -1:
            # Emit runtime check for each non-undef expected value.
            check_die += check_template.format(
                lane=str(i),
                n_lane=str(i + 1),
                ty=ty.dump(),
                i_f=i_f,
                scalar_ty=scalar_ty.dump(),
                exp=str(res.value[i]),
                ordered=ordered,
            )
            # Emit failure handler for each runtime check with proper error message
            check_die += die_template.format(lane=str(i), scalar_ty=scalar_ty.dump())
        else:
            # Ignore lanes with undef result
            check_die += undef_check_template.format(lane=str(i), n_lane=str(i + 1))

    check_die += "\ntest." + str(len(res.value)) + ":\n"
    check_die += "  ret i32 0"

    # Prepare the input values passed to the test function.
    inputs_values = [
        ", ".join([scalar_ty.dump() + " " + str(i) for i in inp])
        for inp in inputs_values
    ]
    inputs = ", ".join([ty.dump() + " <" + inp + ">" for inp in inputs_values])

    print(main_template.format(ty=ty.dump(), inputs=inputs, check_die=check_die))


if __name__ == "__main__":
    main()

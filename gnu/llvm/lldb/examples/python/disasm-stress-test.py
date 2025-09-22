#!/usr/bin/env python

import argparse
import datetime
import re
import subprocess
import sys
import time

parser = argparse.ArgumentParser(
    description="Run an exhaustive test of the LLDB disassembler for a specific architecture."
)

parser.add_argument(
    "--arch",
    required=True,
    action="store",
    help="The architecture whose disassembler is to be tested",
)
parser.add_argument(
    "--bytes",
    required=True,
    action="store",
    type=int,
    help="The byte width of instructions for that architecture",
)
parser.add_argument(
    "--random",
    required=False,
    action="store_true",
    help="Enables non-sequential testing",
)
parser.add_argument(
    "--start",
    required=False,
    action="store",
    type=int,
    help="The first instruction value to test",
)
parser.add_argument(
    "--skip",
    required=False,
    action="store",
    type=int,
    help="The interval between instructions to test",
)
parser.add_argument(
    "--log",
    required=False,
    action="store",
    help="A log file to write the most recent instruction being tested",
)
parser.add_argument(
    "--time",
    required=False,
    action="store_true",
    help="Every 100,000 instructions, print an ETA to standard out",
)
parser.add_argument(
    "--lldb",
    required=False,
    action="store",
    help="The path to LLDB.framework, if LLDB should be overridden",
)

arguments = sys.argv[1:]

arg_ns = parser.parse_args(arguments)


def AddLLDBToSysPathOnMacOSX():
    def GetLLDBFrameworkPath():
        lldb_path = subprocess.check_output(["xcrun", "-find", "lldb"])
        re_result = re.match("(.*)/Developer/usr/bin/lldb", lldb_path)
        if re_result is None:
            return None
        xcode_contents_path = re_result.group(1)
        return xcode_contents_path + "/SharedFrameworks/LLDB.framework"

    lldb_framework_path = GetLLDBFrameworkPath()

    if lldb_framework_path is None:
        print("Couldn't find LLDB.framework")
        sys.exit(-1)

    sys.path.append(lldb_framework_path + "/Resources/Python")


if arg_ns.lldb is None:
    AddLLDBToSysPathOnMacOSX()
else:
    sys.path.append(arg_ns.lldb + "/Resources/Python")

import lldb

debugger = lldb.SBDebugger.Create()

if not debugger.IsValid():
    print("Couldn't create an SBDebugger")
    sys.exit(-1)

target = debugger.CreateTargetWithFileAndArch(None, arg_ns.arch)

if not target.IsValid():
    print("Couldn't create an SBTarget for architecture " + arg_ns.arch)
    sys.exit(-1)


def ResetLogFile(log_file):
    if log_file != sys.stdout:
        log_file.seek(0)


def PrintByteArray(log_file, byte_array):
    for byte in byte_array:
        print(hex(byte) + " ", end=" ", file=log_file)
    print(file=log_file)


class SequentialInstructionProvider:
    def __init__(self, byte_width, log_file, start=0, skip=1):
        self.m_byte_width = byte_width
        self.m_log_file = log_file
        self.m_start = start
        self.m_skip = skip
        self.m_value = start
        self.m_last = (1 << (byte_width * 8)) - 1

    def PrintCurrentState(self, ret):
        ResetLogFile(self.m_log_file)
        print(self.m_value, file=self.m_log_file)
        PrintByteArray(self.m_log_file, ret)

    def GetNextInstruction(self):
        if self.m_value > self.m_last:
            return None
        ret = bytearray(self.m_byte_width)
        for i in range(self.m_byte_width):
            ret[self.m_byte_width - (i + 1)] = (self.m_value >> (i * 8)) & 255
        self.PrintCurrentState(ret)
        self.m_value += self.m_skip
        return ret

    def GetNumInstructions(self):
        return (self.m_last - self.m_start) / self.m_skip

    def __iter__(self):
        return self

    def next(self):
        ret = self.GetNextInstruction()
        if ret is None:
            raise StopIteration
        return ret


class RandomInstructionProvider:
    def __init__(self, byte_width, log_file):
        self.m_byte_width = byte_width
        self.m_log_file = log_file
        self.m_random_file = open("/dev/random", "r")

    def PrintCurrentState(self, ret):
        ResetLogFile(self.m_log_file)
        PrintByteArray(self.m_log_file, ret)

    def GetNextInstruction(self):
        ret = bytearray(self.m_byte_width)
        for i in range(self.m_byte_width):
            ret[i] = self.m_random_file.read(1)
        self.PrintCurrentState(ret)
        return ret

    def __iter__(self):
        return self

    def next(self):
        ret = self.GetNextInstruction()
        if ret is None:
            raise StopIteration
        return ret


log_file = None


def GetProviderWithArguments(args):
    global log_file
    if args.log is not None:
        log_file = open(args.log, "w")
    else:
        log_file = sys.stdout
    instruction_provider = None
    if args.random:
        instruction_provider = RandomInstructionProvider(args.bytes, log_file)
    else:
        start = 0
        skip = 1
        if args.start is not None:
            start = args.start
        if args.skip is not None:
            skip = args.skip
        instruction_provider = SequentialInstructionProvider(
            args.bytes, log_file, start, skip
        )
    return instruction_provider


instruction_provider = GetProviderWithArguments(arg_ns)

fake_address = lldb.SBAddress()

actually_time = arg_ns.time and not arg_ns.random

if actually_time:
    num_instructions_logged = 0
    total_num_instructions = instruction_provider.GetNumInstructions()
    start_time = time.time()

for inst_bytes in instruction_provider:
    if actually_time:
        if (num_instructions_logged != 0) and (num_instructions_logged % 100000 == 0):
            curr_time = time.time()
            elapsed_time = curr_time - start_time
            remaining_time = float(total_num_instructions - num_instructions_logged) * (
                float(elapsed_time) / float(num_instructions_logged)
            )
            print(str(datetime.timedelta(seconds=remaining_time)))
        num_instructions_logged = num_instructions_logged + 1
    inst_list = target.GetInstructions(fake_address, inst_bytes)
    if not inst_list.IsValid():
        print("Invalid instruction list", file=log_file)
        continue
    inst = inst_list.GetInstructionAtIndex(0)
    if not inst.IsValid():
        print("Invalid instruction", file=log_file)
        continue
    instr_output_stream = lldb.SBStream()
    inst.GetDescription(instr_output_stream)
    print(instr_output_stream.GetData(), file=log_file)

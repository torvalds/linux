"""Utility for testing InteractiveModelRunner.

Use it from pass-specific tests by providing a main .py which calls this library's
`run_interactive` with an appropriate callback to provide advice.

From .ll tests, just call the above-mentioned main as a prefix to the opt/llc
invocation (with the appropriate flags enabling the interactive mode)

Examples:
test/Transforms/Inline/ML/interactive-mode.ll
test/CodeGen/MLRegAlloc/interactive-mode.ll
"""

import ctypes
import log_reader
import io
import math
import os
import subprocess
from typing import Callable, List, Union


def send(f: io.BufferedWriter, value: Union[int, float], spec: log_reader.TensorSpec):
    """Send the `value` - currently just a scalar - formatted as per `spec`."""

    # just int64 for now
    assert spec.element_type == ctypes.c_int64
    to_send = ctypes.c_int64(int(value))
    assert f.write(bytes(to_send)) == ctypes.sizeof(spec.element_type) * math.prod(
        spec.shape
    )
    f.flush()


def run_interactive(
    temp_rootname: str,
    make_response: Callable[[List[log_reader.TensorValue]], Union[int, float]],
    process_and_args: List[str],
):
    """Host the compiler.
    Args:
      temp_rootname: the base file name from which to construct the 2 pipes for
      communicating with the compiler.
      make_response: a function that, given the current tensor values, provides a
      response.
      process_and_args: the full commandline for the compiler. It it assumed it
      contains a flag poiting to `temp_rootname` so that the InteractiveModeRunner
      would attempt communication on the same pair as this function opens.

    This function sets up the communication with the compiler - via 2 files named
    `temp_rootname`.in and `temp_rootname`.out - prints out the received features,
    and sends back to the compiler an advice (which it gets from `make_response`).
    It's used for testing, and also to showcase how to set up communication in an
    interactive ML ("gym") environment.
    """
    to_compiler = temp_rootname + ".in"
    from_compiler = temp_rootname + ".out"
    try:
        os.mkfifo(to_compiler, 0o666)
        os.mkfifo(from_compiler, 0o666)
        compiler_proc = subprocess.Popen(
            process_and_args, stderr=subprocess.PIPE, stdout=subprocess.DEVNULL
        )
        with io.BufferedWriter(io.FileIO(to_compiler, "wb")) as tc:
            with io.BufferedReader(io.FileIO(from_compiler, "rb")) as fc:
                tensor_specs, _, advice_spec = log_reader.read_header(fc)
                context = None
                while compiler_proc.poll() is None:
                    next_event = fc.readline()
                    if not next_event:
                        break
                    (
                        last_context,
                        observation_id,
                        features,
                        _,
                    ) = log_reader.read_one_observation(
                        context, next_event, fc, tensor_specs, None
                    )
                    if last_context != context:
                        print(f"context: {last_context}")
                    context = last_context
                    print(f"observation: {observation_id}")
                    tensor_values = []
                    for fv in features:
                        log_reader.pretty_print_tensor_value(fv)
                        tensor_values.append(fv)
                    send(tc, make_response(tensor_values), advice_spec)
        _, err = compiler_proc.communicate()
        print(err.decode("utf-8"))
        compiler_proc.wait()

    finally:
        os.unlink(to_compiler)
        os.unlink(from_compiler)

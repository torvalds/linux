import binascii
import shlex
import subprocess


def get_command_output(command):
    try:
        return subprocess.check_output(
            command, shell=True, universal_newlines=True
        ).rstrip()
    except subprocess.CalledProcessError as e:
        return e.output


def bitcast_to_string(b: bytes) -> str:
    """
    Take a bytes object and return a string. The returned string contains the
    exact same bytes as the input object. (latin1 <-> unicode transformation is
    an identity operation for the first 256 code points).
    """
    return b.decode("latin1")


def bitcast_to_bytes(s: str) -> bytes:
    """
    Take a string and return a bytes object. The returned object contains the
    exact same bytes as the input string. (latin1 <-> unicode transformation isi
    an identity operation for the first 256 code points).
    """
    return s.encode("latin1")


def unhexlify(hexstr):
    """Hex-decode a string. The result is always a string."""
    return bitcast_to_string(binascii.unhexlify(hexstr))


def hexlify(data):
    """Hex-encode string data. The result if always a string."""
    return bitcast_to_string(binascii.hexlify(bitcast_to_bytes(data)))


# TODO: Replace this with `shlex.join` when minimum Python version is >= 3.8
def join_for_shell(split_command):
    return " ".join([shlex.quote(part) for part in split_command])

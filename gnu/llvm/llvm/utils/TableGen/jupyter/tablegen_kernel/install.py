# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import os
import json
import sys
import argparse
from tempfile import TemporaryDirectory
from jupyter_client.kernelspec import KernelSpecManager


def install_my_kernel_spec(user=True, prefix=None):
    """Install the kernel spec for user in given prefix."""
    print("Installing llvm-tblgen IPython kernel spec")

    kernel_json = {
        "argv": [
            sys.executable, "-m", "tablegen_kernel", "-f", "{connection_file}"
        ],
        "display_name": "LLVM TableGen",
        "language": "tablegen",
        "language_info": {
            "name": "tablegen",
            "codemirror_mode": "tablegen",
            "mimetype": "text/x-tablegen",
            "file_extension": ".td",
            "pygments_lexer": "text"
        }
    }

    with TemporaryDirectory() as tmpdir:
      json_path = os.path.join(tmpdir, "kernel.json")
      with open(json_path, 'w') as json_file:
        json.dump(kernel_json, json_file)
      KernelSpecManager().install_kernel_spec(
          tmpdir, "tablegen", user=user, prefix=prefix
      )


def _is_root():
    """Returns whether the current user is root."""
    try:
        return os.geteuid() == 0
    except AttributeError:
        # Return false wherever unknown.
        return False


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Install KernelSpec for LLVM TableGen Kernel"
    )
    prefix_locations = parser.add_mutually_exclusive_group()

    prefix_locations.add_argument(
        "--user", help="Install in user home directory", action="store_true"
    )
    prefix_locations.add_argument(
        "--prefix", help="Install directory prefix", default=None
    )

    args = parser.parse_args(argv)

    user = args.user or not _is_root()
    prefix = args.prefix

    install_my_kernel_spec(user=user, prefix=prefix)


if __name__ == "__main__":
    main()

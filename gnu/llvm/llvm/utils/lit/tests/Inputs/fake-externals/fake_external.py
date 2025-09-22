import os
import sys


def execute(fileName):
    sys.stderr.write(
        "error: external '{}' command called unexpectedly\n".format(
            os.path.basename(fileName)
        )
    )
    sys.exit(1)

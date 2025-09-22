# ALLOW_RETRIES: 5

# RUN: "%python" "%s" "%counter"

import sys
import os

counter_file = sys.argv[1]

# The first time the test is run, initialize the counter to 1.
if not os.path.exists(counter_file):
    with open(counter_file, "w") as counter:
        counter.write("1")

# Succeed if this is the fourth time we're being run.
with open(counter_file, "r") as counter:
    num = int(counter.read())
    if num == 4:
        sys.exit(0)

# Otherwise, increment the counter and fail
with open(counter_file, "w") as counter:
    counter.write(str(num + 1))
    sys.exit(1)

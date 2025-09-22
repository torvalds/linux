# 'sleep 60' in Python because Windows does not have a native sleep command.
#
# RUN: %{python} %s

import time

time.sleep(60)

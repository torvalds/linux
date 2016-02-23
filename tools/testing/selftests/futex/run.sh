#!/bin/sh

###############################################################################
#
#   Copyright © International Business Machines  Corp., 2009
#
#   This program is free software;  you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
# DESCRIPTION
#      Run all tests under the functional, performance, and stress directories.
#      Format and summarize the results.
#
# AUTHOR
#      Darren Hart <dvhart@linux.intel.com>
#
# HISTORY
#      2009-Nov-9: Initial version by Darren Hart <dvhart@linux.intel.com>
#
###############################################################################

# Test for a color capable shell and pass the result to the subdir scripts
USE_COLOR=0
tput setf 7
if [ $? -eq 0 ]; then
    USE_COLOR=1
    tput sgr0
fi
export USE_COLOR

(cd functional; ./run.sh)

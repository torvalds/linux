#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

TID="generic_13"
ERR_CODE=0

_prep_test "null" "check that feature list is complete"

if ${UBLK_PROG} features | grep -q unknown; then
        echo "# unknown feature detected!"
        echo "# did you add a feature and forget to update feat_map in kublk?"
        echo "# this failure is expected if running an older test suite against"
        echo "# a newer kernel with new features added"
        ERR_CODE=255
fi

_cleanup_test "null"
_show_result $TID $ERR_CODE

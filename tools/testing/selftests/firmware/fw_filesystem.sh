#!/bin/sh
# This validates that the kernel will load firmware out of its list of
# firmware locations on disk. Since the user helper does similar work,
# we reset the custom load directory to a location the user helper doesn't
# know so we can be sure we're not accidentally testing the user helper.
set -e

modprobe test_firmware

DIR=/sys/devices/virtual/misc/test_firmware

OLD_TIMEOUT=$(cat /sys/class/firmware/timeout)
OLD_FWPATH=$(cat /sys/module/firmware_class/parameters/path)

FWPATH=$(mktemp -d)
FW="$FWPATH/test-firmware.bin"

test_finish()
{
	echo "$OLD_TIMEOUT" >/sys/class/firmware/timeout
	echo -n "$OLD_PATH" >/sys/module/firmware_class/parameters/path
	rm -f "$FW"
	rmdir "$FWPATH"
}

trap "test_finish" EXIT

# Turn down the timeout so failures don't take so long.
echo 1 >/sys/class/firmware/timeout
# Set the kernel search path.
echo -n "$FWPATH" >/sys/module/firmware_class/parameters/path

# This is an unlikely real-world firmware content. :)
echo "ABCD0123" >"$FW"

NAME=$(basename "$FW")

# Request a firmware that doesn't exist, it should fail.
echo -n "nope-$NAME" >"$DIR"/trigger_request
if diff -q "$FW" /dev/test_firmware >/dev/null ; then
	echo "$0: firmware was not expected to match" >&2
	exit 1
else
	echo "$0: timeout works"
fi

# This should succeed via kernel load or will fail after 1 second after
# being handed over to the user helper, which won't find the fw either.
if ! echo -n "$NAME" >"$DIR"/trigger_request ; then
	echo "$0: could not trigger request" >&2
	exit 1
fi

# Verify the contents are what we expect.
if ! diff -q "$FW" /dev/test_firmware >/dev/null ; then
	echo "$0: firmware was not loaded" >&2
	exit 1
else
	echo "$0: filesystem loading works"
fi

exit 0

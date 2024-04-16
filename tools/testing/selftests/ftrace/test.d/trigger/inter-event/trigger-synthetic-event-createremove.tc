#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: event trigger - test synthetic event create remove
# requires: set_event synthetic_events

fail() { #msg
    echo $1
    exit_fail
}

echo "Test create synthetic event"

echo 'wakeup_latency  u64 lat pid_t pid char comm[16]' > synthetic_events
if [ ! -d events/synthetic/wakeup_latency ]; then
    fail "Failed to create wakeup_latency synthetic event"
fi

reset_trigger

echo "Test remove synthetic event"
echo '!wakeup_latency  u64 lat pid_t pid char comm[16]' >> synthetic_events
if [ -d events/synthetic/wakeup_latency ]; then
    fail "Failed to delete wakeup_latency synthetic event"
fi

reset_trigger

echo "Test create synthetic event with an error"
echo 'wakeup_latency  u64 lat pid_t pid char' > synthetic_events > /dev/null
if [ -d events/synthetic/wakeup_latency ]; then
    fail "Created wakeup_latency synthetic event with an invalid format"
fi

exit 0

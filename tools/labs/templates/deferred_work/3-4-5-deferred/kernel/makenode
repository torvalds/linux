#!/bin/sh

device=deferred
type=c
major=42
minor=0

rm -f /dev/${device}
mknod /dev/${device} $type $major $minor && ls -al /dev/${device}

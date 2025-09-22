#!/bin/sh
umask 022
exec mkdir -p "$@"

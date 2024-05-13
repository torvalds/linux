#!/bin/bash

TC="$1"; shift
ETH="$1"; shift

# The taprio architecture changes the admin schedule from a hrtimer and not
# from process context, so we need to wait in order to make sure that any
# schedule change actually took place.
while :; do
	has_admin="$($TC -j qdisc show dev $ETH root | jq '.[].options | has("admin")')"
	if [ "$has_admin" = "false" ]; then
		break;
	fi

	sleep 1
done

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source "$(dirname "$(realpath "$0")")/ynl_nsim_lib.sh"
nsim_setup
"$(dirname "$(realpath "$0")")/rt-route"

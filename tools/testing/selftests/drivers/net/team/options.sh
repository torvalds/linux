#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# These tests verify basic set and get functionality of the team
# driver options over netlink.

# Run in private netns.
test_dir="$(dirname "$0")"
if [[ $# -eq 0 ]]; then
        "${test_dir}"/../../../net/in_netns.sh "$0" __subprocess
        exit $?
fi

ALL_TESTS="
        team_test_options
"

source "${test_dir}/../../../net/lib.sh"

TEAM_PORT="team0"
MEMBER_PORT="dummy0"

setup()
{
        ip link add name "${MEMBER_PORT}" type dummy
        ip link add name "${TEAM_PORT}" type team
}

get_and_check_value()
{
        local option_name="$1"
        local expected_value="$2"
        local port_flag="$3"

        local value_from_get

        if ! value_from_get=$(teamnl "${TEAM_PORT}" getoption "${option_name}" \
                        "${port_flag}"); then
                echo "Could not get option '${option_name}'" >&2
                return 1
        fi

        if [[ "${value_from_get}" != "${expected_value}" ]]; then
                echo "Incorrect value for option '${option_name}'" >&2
                echo "get (${value_from_get}) != set (${expected_value})" >&2
                return 1
        fi
}

set_and_check_get()
{
        local option_name="$1"
        local option_value="$2"
        local port_flag="$3"

        local value_from_get

        if ! teamnl "${TEAM_PORT}" setoption "${option_name}" \
                        "${option_value}" "${port_flag}"; then
                echo "'setoption ${option_name} ${option_value}' failed" >&2
                return 1
        fi

        get_and_check_value "${option_name}" "${option_value}" "${port_flag}"
        return $?
}

# Get a "port flag" to pass to the `teamnl` command.
# E.g. $1="dummy0" -> "port=dummy0",
#      $1=""       -> ""
get_port_flag()
{
        local port_name="$1"

        if [[ -n "${port_name}" ]]; then
                echo "--port=${port_name}"
        fi
}

attach_port_if_specified()
{
        local port_name="$1"

        if [[ -n "${port_name}" ]]; then
                ip link set dev "${port_name}" master "${TEAM_PORT}"
                return $?
        fi
}

detach_port_if_specified()
{
        local port_name="$1"

        if [[ -n "${port_name}" ]]; then
                ip link set dev "${port_name}" nomaster
                return $?
        fi
}

# Test that an option's get value matches its set value.
# Globals:
#   RET - Used by testing infra like `check_err`.
#   EXIT_STATUS - Used by `log_test` for whole script exit value.
# Arguments:
#   option_name - The name of the option.
#   value_1 - The first value to try setting.
#   value_2 - The second value to try setting.
#   port_name - The (optional) name of the attached port.
team_test_option()
{
        local option_name="$1"
        local value_1="$2"
        local value_2="$3"
        local possible_values="$2 $3 $2"
        local port_name="$4"
        local port_flag

        RET=0

        echo "Setting '${option_name}' to '${value_1}' and '${value_2}'"

        attach_port_if_specified "${port_name}"
        check_err $? "Couldn't attach ${port_name} to master"
        port_flag=$(get_port_flag "${port_name}")

        # Set and get both possible values.
        for value in ${possible_values}; do
                set_and_check_get "${option_name}" "${value}" "${port_flag}"
                check_err $? "Failed to set '${option_name}' to '${value}'"
        done

        detach_port_if_specified "${port_name}"
        check_err $? "Couldn't detach ${port_name} from its master"

        log_test "Set + Get '${option_name}' test"
}

# Test that getting a non-existant option fails.
# Globals:
#   RET - Used by testing infra like `check_err`.
#   EXIT_STATUS - Used by `log_test` for whole script exit value.
# Arguments:
#   option_name - The name of the option.
#   port_name - The (optional) name of the attached port.
team_test_get_option_fails()
{
        local option_name="$1"
        local port_name="$2"
        local port_flag

        RET=0

        attach_port_if_specified "${port_name}"
        check_err $? "Couldn't attach ${port_name} to master"
        port_flag=$(get_port_flag "${port_name}")

        # Just confirm that getting the value fails.
        teamnl "${TEAM_PORT}" getoption "${option_name}" "${port_flag}"
        check_fail $? "Shouldn't be able to get option '${option_name}'"

        detach_port_if_specified "${port_name}"

        log_test "Get '${option_name}' fails"
}

team_test_options()
{
        # Wrong option name behavior.
        team_test_get_option_fails fake_option1
        team_test_get_option_fails fake_option2 "${MEMBER_PORT}"

        # Correct set and get behavior.
        team_test_option mode activebackup loadbalance
        team_test_option notify_peers_count 0 5
        team_test_option notify_peers_interval 0 5
        team_test_option mcast_rejoin_count 0 5
        team_test_option mcast_rejoin_interval 0 5
        team_test_option enabled true false "${MEMBER_PORT}"
        team_test_option user_linkup true false "${MEMBER_PORT}"
        team_test_option user_linkup_enabled true false "${MEMBER_PORT}"
        team_test_option priority 10 20 "${MEMBER_PORT}"
        team_test_option queue_id 0 1 "${MEMBER_PORT}"
}

require_command teamnl
setup
tests_run
exit "${EXIT_STATUS}"

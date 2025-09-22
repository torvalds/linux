#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

# Bash functions for managing the names of emulator system images.

# Parse the image name and set variables: API, TYPE, and ARCH.
__parse_emu_img() {
    if [[ "${1}" =~ ([0-9]+)-(def|goog|play)-(arm|arm64|x86|x86_64)$ ]]; then
        API=${BASH_REMATCH[1]}
        case ${BASH_REMATCH[2]} in
            def) TYPE=default ;;
            goog) TYPE=google_apis ;;
            play) TYPE=google_apis_playstore ;;
        esac
        ARCH=${BASH_REMATCH[3]}
        return 0
    else
        return 1
    fi
}

# Check that the emulator image name has valid syntax.
validate_emu_img_syntax() {
    local EMU_IMG="${1}"
    local API TYPE ARCH
    if ! __parse_emu_img "${EMU_IMG}"; then
        echo "\
error: invalid emulator image name: ${EMU_IMG}
  expected \"\${API}-\${TYPE}-\${ARCH}\" where API is a number, TYPE is one of
  (def|goog|play), and ARCH is one of arm, arm64, x86, or x86_64." >&2
        return 1
    fi
}

docker_image_of_emu_img() {
    echo "android-emulator-${1}"
}

# Check that the emulator image name has valid syntax and that the Docker image
# is present. On failure, writes an error to stderr and exits the script.
validate_emu_img() {
    local EMU_IMG="${1}"
    if ! validate_emu_img_syntax "${EMU_IMG}"; then
        return 1
    fi
    # Make sure Docker is working before trusting other Docker commands.
    # Temporarily suppress command echoing so we only show 'docker info' output
    # on failure, and only once.
    if (set +x; !(docker info &>/dev/null || docker info)); then
        echo "error: Docker is required for emulator usage but 'docker info' failed" >&2
        return 1
    fi
    local DOCKER_IMAGE=$(docker_image_of_emu_img ${EMU_IMG})
    if ! docker image inspect ${DOCKER_IMAGE} &>/dev/null; then
        echo "error: emulator Docker image (${DOCKER_IMAGE}) is not installed" >&2
        return 1
    fi
}

api_of_emu_img() {
    local API TYPE ARCH
    __parse_emu_img "${1}"
    echo ${API}
}

type_of_emu_img() {
    local API TYPE ARCH
    __parse_emu_img "${1}"
    echo ${TYPE}
}

arch_of_emu_img() {
    local API TYPE ARCH
    __parse_emu_img "${1}"
    echo ${ARCH}
}

# Expand the short emu_img string into the full SDK package string identifying
# the system image.
sdk_package_of_emu_img() {
    local API TYPE ARCH
    __parse_emu_img "${1}"
    echo "system-images;android-${API};${TYPE};$(abi_of_arch ${ARCH})"
}

# Return the Android ABI string for an architecture.
abi_of_arch() {
    case "${1}" in
        arm) echo armeabi-v7a ;;
        arm64) echo aarch64-v8a ;;
        x86) echo x86 ;;
        x86_64) echo x86_64 ;;
        *) echo "error: unhandled arch ${1}" >&2; exit 1 ;;
    esac
}

triple_of_arch() {
    case "${1}" in
        arm) echo armv7a-linux-androideabi ;;
        arm64) echo aarch64-linux-android ;;
        x86) echo i686-linux-android ;;
        x86_64) echo x86_64-linux-android ;;
        *) echo "error: unhandled arch ${1}" >&2; exit 1 ;;
    esac
}

#!/usr/bin/env bash
#
# Script to rename DLL name within side deck.
#

# Stops execution if a command or pipeline has an error.
set -e

sidedeck=$1
old_dll_name=$2
new_dll_name=$3

function error() {
  printf "ERROR: %s\n" "$*"
  exit 1
}

function usage() {
cat <<EOF
Usage: $(basename $0) <side deck file> <old dll name> <new dll name>:
          [-h|--help] Display this help and exit.
EOF
}

rename_dll_name_inside_side_deck() {

if [[ -z "$sidedeck" || -z "$old_dll_name" || -z "$new_dll_name" ]]; then
  usage
  error "All 3 parameters must be specified."
fi

[[ -f "$sidedeck" ]] || error "The '$sidedeck' file must exists."

old_len=${#old_dll_name}
new_len=${#new_dll_name}

if (( $new_len > $old_len )); then
  error "New DLL name $new_dll_name must have $old_len characters or less."
fi

if ((padding_len=$old_len-$new_len )); then
  pad=$(printf "%*s" $padding_len "")
fi

# Touch the temp. file and set the tag to 1047 first so the redirecting statement
# will write in 1047 and not 819 encoding.
touch $sidedeck.tmp; chtag -tc1047 $sidedeck.tmp
sed "/ IMPORT /s/'$old_dll_name/$pad'$new_dll_name/g" $sidedeck > $sidedeck.tmp
mv $sidedeck.tmp $sidedeck
}

function main() {
  rename_dll_name_inside_side_deck
}

main "$@"


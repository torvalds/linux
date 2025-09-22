#!/usr/bin/env bash

# The lines that we're looking to symbolize look like this:
  #0 ./a.out(_foo+0x3e6) [0x55a52e64c696]
# ... which come from the backtrace_symbols() symbolisation function used by
# default in Scudo's implementation of GWP-ASan.

while read -r line; do
  # Check that this line needs symbolization.
  should_symbolize="$(echo $line |\
     grep -E '^[ ]*\#.*\(.*\+0x[0-9a-f]+\) \[0x[0-9a-f]+\]$')"

  if [ -z "$should_symbolize" ]; then
    echo "$line"
    continue
  fi

  # Carve up the input line into sections.
  binary_name="$(echo $line | grep -oE ' .*\(' | rev | cut -c2- | rev |\
      cut -c2-)"
  function_name="$(echo $line | grep -oE '\([^+]*' | cut -c2-)"
  function_offset="$(echo $line | grep -oE '\(.*\)' | grep -oE '\+.*\)' |\
      cut -c2- | rev | cut -c2- | rev)"
  frame_number="$(echo $line | grep -oE '\#[0-9]+ ')"

  if [ -z "$function_name" ]; then
    # If the offset is binary-relative, just resolve that.
    symbolized="$(echo $function_offset | addr2line -ie $binary_name)"
  else
    # Otherwise, the offset is function-relative. Get the address of the
    # function, and add it to the offset, then symbolize.
    function_addr="0x$(echo $function_offset |\
       nm --defined-only $binary_name 2> /dev/null |\
       grep -E " $function_name$" | cut -d' ' -f1)"

    # Check that we could get the function address from nm.
    if [ -z "$function_addr" ]; then
      echo "$line"
      continue
    fi

    # Add the function address and offset to get the offset into the binary.
    binary_offset="$(printf "0x%X" "$((function_addr+function_offset))")"
    symbolized="$(echo $binary_offset | addr2line -ie $binary_name)"
  fi

  # Check that it symbolized properly. If it didn't, output the old line.
  echo $symbolized | grep -E ".*\?.*:" > /dev/null
  if [ "$?" -eq "0" ]; then
    echo "$line"
    continue
  else
    echo "${frame_number}${symbolized}"
  fi
done 2> >(grep -v "addr2line: DWARF error: could not find variable specification")

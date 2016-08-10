#!/bin/bash
 i=0
file=/dev/media$1
 while :; do
  echo $file
  i=$((i+1))
  R=$(./media_device_open -d $file);
 # clear
  echo -e "Loop $i\n$R"
 done

#!/bin/bash -e

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)
hijack_script=lkl-hijack.sh
export PATH=${script_dir}/../bin/:${PATH}

echo "== ip addr test=="
${hijack_script} ip addr
echo "== ip route test=="
${hijack_script} ip route
echo "== ping test=="
sudo ${script_dir}/../bin/${hijack_script} ping 127.0.0.1 -c 2
echo "== ping6 test=="
sudo ${script_dir}/../bin/${hijack_script} ping6 ::1 -c 2

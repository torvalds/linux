#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

set -e

if [[ $(id -u) -ne 0 ]]; then
  echo "This test must be run as root. Skipping..."
  exit $ksft_skip
fi

fault_limit_file=limit_in_bytes
reservation_limit_file=rsvd.limit_in_bytes
fault_usage_file=usage_in_bytes
reservation_usage_file=rsvd.usage_in_bytes

if [[ "$1" == "-cgroup-v2" ]]; then
  cgroup2=1
  fault_limit_file=max
  reservation_limit_file=rsvd.max
  fault_usage_file=current
  reservation_usage_file=rsvd.current
fi

if [[ $cgroup2 ]]; then
  cgroup_path=$(mount -t cgroup2 | head -1 | awk -e '{print $3}')
  if [[ -z "$cgroup_path" ]]; then
    cgroup_path=/dev/cgroup/memory
    mount -t cgroup2 none $cgroup_path
    do_umount=1
  fi
  echo "+hugetlb" >$cgroup_path/cgroup.subtree_control
else
  cgroup_path=$(mount -t cgroup | grep ",hugetlb" | awk -e '{print $3}')
  if [[ -z "$cgroup_path" ]]; then
    cgroup_path=/dev/cgroup/memory
    mount -t cgroup memory,hugetlb $cgroup_path
    do_umount=1
  fi
fi
export cgroup_path

function cleanup() {
  if [[ $cgroup2 ]]; then
    echo $$ >$cgroup_path/cgroup.procs
  else
    echo $$ >$cgroup_path/tasks
  fi

  if [[ -e /mnt/huge ]]; then
    rm -rf /mnt/huge/*
    umount /mnt/huge || echo error
    rmdir /mnt/huge
  fi
  if [[ -e $cgroup_path/hugetlb_cgroup_test ]]; then
    rmdir $cgroup_path/hugetlb_cgroup_test
  fi
  if [[ -e $cgroup_path/hugetlb_cgroup_test1 ]]; then
    rmdir $cgroup_path/hugetlb_cgroup_test1
  fi
  if [[ -e $cgroup_path/hugetlb_cgroup_test2 ]]; then
    rmdir $cgroup_path/hugetlb_cgroup_test2
  fi
  echo 0 >/proc/sys/vm/nr_hugepages
  echo CLEANUP DONE
}

function expect_equal() {
  local expected="$1"
  local actual="$2"
  local error="$3"

  if [[ "$expected" != "$actual" ]]; then
    echo "expected ($expected) != actual ($actual): $3"
    cleanup
    exit 1
  fi
}

function get_machine_hugepage_size() {
  hpz=$(grep -i hugepagesize /proc/meminfo)
  kb=${hpz:14:-3}
  mb=$(($kb / 1024))
  echo $mb
}

MB=$(get_machine_hugepage_size)

function setup_cgroup() {
  local name="$1"
  local cgroup_limit="$2"
  local reservation_limit="$3"

  mkdir $cgroup_path/$name

  echo writing cgroup limit: "$cgroup_limit"
  echo "$cgroup_limit" >$cgroup_path/$name/hugetlb.${MB}MB.$fault_limit_file

  echo writing reseravation limit: "$reservation_limit"
  echo "$reservation_limit" > \
    $cgroup_path/$name/hugetlb.${MB}MB.$reservation_limit_file

  if [ -e "$cgroup_path/$name/cpuset.cpus" ]; then
    echo 0 >$cgroup_path/$name/cpuset.cpus
  fi
  if [ -e "$cgroup_path/$name/cpuset.mems" ]; then
    echo 0 >$cgroup_path/$name/cpuset.mems
  fi
}

function wait_for_hugetlb_memory_to_get_depleted() {
  local cgroup="$1"
  local path="$cgroup_path/$cgroup/hugetlb.${MB}MB.$reservation_usage_file"
  # Wait for hugetlbfs memory to get depleted.
  while [ $(cat $path) != 0 ]; do
    echo Waiting for hugetlb memory to get depleted.
    cat $path
    sleep 0.5
  done
}

function wait_for_hugetlb_memory_to_get_reserved() {
  local cgroup="$1"
  local size="$2"

  local path="$cgroup_path/$cgroup/hugetlb.${MB}MB.$reservation_usage_file"
  # Wait for hugetlbfs memory to get written.
  while [ $(cat $path) != $size ]; do
    echo Waiting for hugetlb memory reservation to reach size $size.
    cat $path
    sleep 0.5
  done
}

function wait_for_hugetlb_memory_to_get_written() {
  local cgroup="$1"
  local size="$2"

  local path="$cgroup_path/$cgroup/hugetlb.${MB}MB.$fault_usage_file"
  # Wait for hugetlbfs memory to get written.
  while [ $(cat $path) != $size ]; do
    echo Waiting for hugetlb memory to reach size $size.
    cat $path
    sleep 0.5
  done
}

function write_hugetlbfs_and_get_usage() {
  local cgroup="$1"
  local size="$2"
  local populate="$3"
  local write="$4"
  local path="$5"
  local method="$6"
  local private="$7"
  local expect_failure="$8"
  local reserve="$9"

  # Function return values.
  reservation_failed=0
  oom_killed=0
  hugetlb_difference=0
  reserved_difference=0

  local hugetlb_usage=$cgroup_path/$cgroup/hugetlb.${MB}MB.$fault_usage_file
  local reserved_usage=$cgroup_path/$cgroup/hugetlb.${MB}MB.$reservation_usage_file

  local hugetlb_before=$(cat $hugetlb_usage)
  local reserved_before=$(cat $reserved_usage)

  echo
  echo Starting:
  echo hugetlb_usage="$hugetlb_before"
  echo reserved_usage="$reserved_before"
  echo expect_failure is "$expect_failure"

  output=$(mktemp)
  set +e
  if [[ "$method" == "1" ]] || [[ "$method" == 2 ]] ||
    [[ "$private" == "-r" ]] && [[ "$expect_failure" != 1 ]]; then

    bash write_hugetlb_memory.sh "$size" "$populate" "$write" \
      "$cgroup" "$path" "$method" "$private" "-l" "$reserve" 2>&1 | tee $output &

    local write_result=$?
    local write_pid=$!

    until grep -q -i "DONE" $output; do
      echo waiting for DONE signal.
      if ! ps $write_pid > /dev/null
      then
        echo "FAIL: The write died"
        cleanup
        exit 1
      fi
      sleep 0.5
    done

    echo ================= write_hugetlb_memory.sh output is:
    cat $output
    echo ================= end output.

    if [[ "$populate" == "-o" ]] || [[ "$write" == "-w" ]]; then
      wait_for_hugetlb_memory_to_get_written "$cgroup" "$size"
    elif [[ "$reserve" != "-n" ]]; then
      wait_for_hugetlb_memory_to_get_reserved "$cgroup" "$size"
    else
      # This case doesn't produce visible effects, but we still have
      # to wait for the async process to start and execute...
      sleep 0.5
    fi

    echo write_result is $write_result
  else
    bash write_hugetlb_memory.sh "$size" "$populate" "$write" \
      "$cgroup" "$path" "$method" "$private" "$reserve"
    local write_result=$?

    if [[ "$reserve" != "-n" ]]; then
      wait_for_hugetlb_memory_to_get_reserved "$cgroup" "$size"
    fi
  fi
  set -e

  if [[ "$write_result" == 1 ]]; then
    reservation_failed=1
  fi

  # On linus/master, the above process gets SIGBUS'd on oomkill, with
  # return code 135. On earlier kernels, it gets actual oomkill, with return
  # code 137, so just check for both conditions in case we're testing
  # against an earlier kernel.
  if [[ "$write_result" == 135 ]] || [[ "$write_result" == 137 ]]; then
    oom_killed=1
  fi

  local hugetlb_after=$(cat $hugetlb_usage)
  local reserved_after=$(cat $reserved_usage)

  echo After write:
  echo hugetlb_usage="$hugetlb_after"
  echo reserved_usage="$reserved_after"

  hugetlb_difference=$(($hugetlb_after - $hugetlb_before))
  reserved_difference=$(($reserved_after - $reserved_before))
}

function cleanup_hugetlb_memory() {
  set +e
  local cgroup="$1"
  if [[ "$(pgrep -f write_to_hugetlbfs)" != "" ]]; then
    echo killing write_to_hugetlbfs
    killall -2 write_to_hugetlbfs
    wait_for_hugetlb_memory_to_get_depleted $cgroup
  fi
  set -e

  if [[ -e /mnt/huge ]]; then
    rm -rf /mnt/huge/*
    umount /mnt/huge
    rmdir /mnt/huge
  fi
}

function run_test() {
  local size=$(($1 * ${MB} * 1024 * 1024))
  local populate="$2"
  local write="$3"
  local cgroup_limit=$(($4 * ${MB} * 1024 * 1024))
  local reservation_limit=$(($5 * ${MB} * 1024 * 1024))
  local nr_hugepages="$6"
  local method="$7"
  local private="$8"
  local expect_failure="$9"
  local reserve="${10}"

  # Function return values.
  hugetlb_difference=0
  reserved_difference=0
  reservation_failed=0
  oom_killed=0

  echo nr hugepages = "$nr_hugepages"
  echo "$nr_hugepages" >/proc/sys/vm/nr_hugepages

  setup_cgroup "hugetlb_cgroup_test" "$cgroup_limit" "$reservation_limit"

  mkdir -p /mnt/huge
  mount -t hugetlbfs -o pagesize=${MB}M,size=256M none /mnt/huge

  write_hugetlbfs_and_get_usage "hugetlb_cgroup_test" "$size" "$populate" \
    "$write" "/mnt/huge/test" "$method" "$private" "$expect_failure" \
    "$reserve"

  cleanup_hugetlb_memory "hugetlb_cgroup_test"

  local final_hugetlb=$(cat $cgroup_path/hugetlb_cgroup_test/hugetlb.${MB}MB.$fault_usage_file)
  local final_reservation=$(cat $cgroup_path/hugetlb_cgroup_test/hugetlb.${MB}MB.$reservation_usage_file)

  echo $hugetlb_difference
  echo $reserved_difference
  expect_equal "0" "$final_hugetlb" "final hugetlb is not zero"
  expect_equal "0" "$final_reservation" "final reservation is not zero"
}

function run_multiple_cgroup_test() {
  local size1="$1"
  local populate1="$2"
  local write1="$3"
  local cgroup_limit1="$4"
  local reservation_limit1="$5"

  local size2="$6"
  local populate2="$7"
  local write2="$8"
  local cgroup_limit2="$9"
  local reservation_limit2="${10}"

  local nr_hugepages="${11}"
  local method="${12}"
  local private="${13}"
  local expect_failure="${14}"
  local reserve="${15}"

  # Function return values.
  hugetlb_difference1=0
  reserved_difference1=0
  reservation_failed1=0
  oom_killed1=0

  hugetlb_difference2=0
  reserved_difference2=0
  reservation_failed2=0
  oom_killed2=0

  echo nr hugepages = "$nr_hugepages"
  echo "$nr_hugepages" >/proc/sys/vm/nr_hugepages

  setup_cgroup "hugetlb_cgroup_test1" "$cgroup_limit1" "$reservation_limit1"
  setup_cgroup "hugetlb_cgroup_test2" "$cgroup_limit2" "$reservation_limit2"

  mkdir -p /mnt/huge
  mount -t hugetlbfs -o pagesize=${MB}M,size=256M none /mnt/huge

  write_hugetlbfs_and_get_usage "hugetlb_cgroup_test1" "$size1" \
    "$populate1" "$write1" "/mnt/huge/test1" "$method" "$private" \
    "$expect_failure" "$reserve"

  hugetlb_difference1=$hugetlb_difference
  reserved_difference1=$reserved_difference
  reservation_failed1=$reservation_failed
  oom_killed1=$oom_killed

  local cgroup1_hugetlb_usage=$cgroup_path/hugetlb_cgroup_test1/hugetlb.${MB}MB.$fault_usage_file
  local cgroup1_reservation_usage=$cgroup_path/hugetlb_cgroup_test1/hugetlb.${MB}MB.$reservation_usage_file
  local cgroup2_hugetlb_usage=$cgroup_path/hugetlb_cgroup_test2/hugetlb.${MB}MB.$fault_usage_file
  local cgroup2_reservation_usage=$cgroup_path/hugetlb_cgroup_test2/hugetlb.${MB}MB.$reservation_usage_file

  local usage_before_second_write=$(cat $cgroup1_hugetlb_usage)
  local reservation_usage_before_second_write=$(cat $cgroup1_reservation_usage)

  write_hugetlbfs_and_get_usage "hugetlb_cgroup_test2" "$size2" \
    "$populate2" "$write2" "/mnt/huge/test2" "$method" "$private" \
    "$expect_failure" "$reserve"

  hugetlb_difference2=$hugetlb_difference
  reserved_difference2=$reserved_difference
  reservation_failed2=$reservation_failed
  oom_killed2=$oom_killed

  expect_equal "$usage_before_second_write" \
    "$(cat $cgroup1_hugetlb_usage)" "Usage changed."
  expect_equal "$reservation_usage_before_second_write" \
    "$(cat $cgroup1_reservation_usage)" "Reservation usage changed."

  cleanup_hugetlb_memory

  local final_hugetlb=$(cat $cgroup1_hugetlb_usage)
  local final_reservation=$(cat $cgroup1_reservation_usage)

  expect_equal "0" "$final_hugetlb" \
    "hugetlbt_cgroup_test1 final hugetlb is not zero"
  expect_equal "0" "$final_reservation" \
    "hugetlbt_cgroup_test1 final reservation is not zero"

  local final_hugetlb=$(cat $cgroup2_hugetlb_usage)
  local final_reservation=$(cat $cgroup2_reservation_usage)

  expect_equal "0" "$final_hugetlb" \
    "hugetlb_cgroup_test2 final hugetlb is not zero"
  expect_equal "0" "$final_reservation" \
    "hugetlb_cgroup_test2 final reservation is not zero"
}

cleanup

for populate in "" "-o"; do
  for method in 0 1 2; do
    for private in "" "-r"; do
      for reserve in "" "-n"; do

        # Skip mmap(MAP_HUGETLB | MAP_SHARED). Doesn't seem to be supported.
        if [[ "$method" == 1 ]] && [[ "$private" == "" ]]; then
          continue
        fi

        # Skip populated shmem tests. Doesn't seem to be supported.
        if [[ "$method" == 2"" ]] && [[ "$populate" == "-o" ]]; then
          continue
        fi

        if [[ "$method" == 2"" ]] && [[ "$reserve" == "-n" ]]; then
          continue
        fi

        cleanup
        echo
        echo
        echo
        echo Test normal case.
        echo private=$private, populate=$populate, method=$method, reserve=$reserve
        run_test 5 "$populate" "" 10 10 10 "$method" "$private" "0" "$reserve"

        echo Memory charged to hugtlb=$hugetlb_difference
        echo Memory charged to reservation=$reserved_difference

        if [[ "$populate" == "-o" ]]; then
          expect_equal "$((5 * $MB * 1024 * 1024))" "$hugetlb_difference" \
            "Reserved memory charged to hugetlb cgroup."
        else
          expect_equal "0" "$hugetlb_difference" \
            "Reserved memory charged to hugetlb cgroup."
        fi

        if [[ "$reserve" != "-n" ]] || [[ "$populate" == "-o" ]]; then
          expect_equal "$((5 * $MB * 1024 * 1024))" "$reserved_difference" \
            "Reserved memory not charged to reservation usage."
        else
          expect_equal "0" "$reserved_difference" \
            "Reserved memory not charged to reservation usage."
        fi

        echo 'PASS'

        cleanup
        echo
        echo
        echo
        echo Test normal case with write.
        echo private=$private, populate=$populate, method=$method, reserve=$reserve
        run_test 5 "$populate" '-w' 5 5 10 "$method" "$private" "0" "$reserve"

        echo Memory charged to hugtlb=$hugetlb_difference
        echo Memory charged to reservation=$reserved_difference

        expect_equal "$((5 * $MB * 1024 * 1024))" "$hugetlb_difference" \
          "Reserved memory charged to hugetlb cgroup."

        expect_equal "$((5 * $MB * 1024 * 1024))" "$reserved_difference" \
          "Reserved memory not charged to reservation usage."

        echo 'PASS'

        cleanup
        continue
        echo
        echo
        echo
        echo Test more than reservation case.
        echo private=$private, populate=$populate, method=$method, reserve=$reserve

        if [ "$reserve" != "-n" ]; then
          run_test "5" "$populate" '' "10" "2" "10" "$method" "$private" "1" \
            "$reserve"

          expect_equal "1" "$reservation_failed" "Reservation succeeded."
        fi

        echo 'PASS'

        cleanup

        echo
        echo
        echo
        echo Test more than cgroup limit case.
        echo private=$private, populate=$populate, method=$method, reserve=$reserve

        # Not sure if shm memory can be cleaned up when the process gets sigbus'd.
        if [[ "$method" != 2 ]]; then
          run_test 5 "$populate" "-w" 2 10 10 "$method" "$private" "1" "$reserve"

          expect_equal "1" "$oom_killed" "Not oom killed."
        fi
        echo 'PASS'

        cleanup

        echo
        echo
        echo
        echo Test normal case, multiple cgroups.
        echo private=$private, populate=$populate, method=$method, reserve=$reserve
        run_multiple_cgroup_test "3" "$populate" "" "10" "10" "5" \
          "$populate" "" "10" "10" "10" \
          "$method" "$private" "0" "$reserve"

        echo Memory charged to hugtlb1=$hugetlb_difference1
        echo Memory charged to reservation1=$reserved_difference1
        echo Memory charged to hugtlb2=$hugetlb_difference2
        echo Memory charged to reservation2=$reserved_difference2

        if [[ "$reserve" != "-n" ]] || [[ "$populate" == "-o" ]]; then
          expect_equal "3" "$reserved_difference1" \
            "Incorrect reservations charged to cgroup 1."

          expect_equal "5" "$reserved_difference2" \
            "Incorrect reservation charged to cgroup 2."

        else
          expect_equal "0" "$reserved_difference1" \
            "Incorrect reservations charged to cgroup 1."

          expect_equal "0" "$reserved_difference2" \
            "Incorrect reservation charged to cgroup 2."
        fi

        if [[ "$populate" == "-o" ]]; then
          expect_equal "3" "$hugetlb_difference1" \
            "Incorrect hugetlb charged to cgroup 1."

          expect_equal "5" "$hugetlb_difference2" \
            "Incorrect hugetlb charged to cgroup 2."

        else
          expect_equal "0" "$hugetlb_difference1" \
            "Incorrect hugetlb charged to cgroup 1."

          expect_equal "0" "$hugetlb_difference2" \
            "Incorrect hugetlb charged to cgroup 2."
        fi
        echo 'PASS'

        cleanup
        echo
        echo
        echo
        echo Test normal case with write, multiple cgroups.
        echo private=$private, populate=$populate, method=$method, reserve=$reserve
        run_multiple_cgroup_test "3" "$populate" "-w" "10" "10" "5" \
          "$populate" "-w" "10" "10" "10" \
          "$method" "$private" "0" "$reserve"

        echo Memory charged to hugtlb1=$hugetlb_difference1
        echo Memory charged to reservation1=$reserved_difference1
        echo Memory charged to hugtlb2=$hugetlb_difference2
        echo Memory charged to reservation2=$reserved_difference2

        expect_equal "3" "$hugetlb_difference1" \
          "Incorrect hugetlb charged to cgroup 1."

        expect_equal "3" "$reserved_difference1" \
          "Incorrect reservation charged to cgroup 1."

        expect_equal "5" "$hugetlb_difference2" \
          "Incorrect hugetlb charged to cgroup 2."

        expect_equal "5" "$reserved_difference2" \
          "Incorrected reservation charged to cgroup 2."
        echo 'PASS'

        cleanup

      done # reserve
    done   # private
  done     # populate
done       # method

if [[ $do_umount ]]; then
  umount $cgroup_path
  rmdir $cgroup_path
fi

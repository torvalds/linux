#Test a bunch of positive cases to verify basic functionality
for prog in "--txmsg_redir --txmsg_ingress" "--txmsg" "--txmsg_redir" "--txmsg_redir --txmsg_ingress" "--txmsg_drop"; do
for t in "sendmsg" "sendpage"; do
for r in 1 10 100; do
	for i in 1 10 100; do
		for l in 1 10 100; do
			TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
			echo $TEST
			$TEST
			sleep 2
		done
	done
done
done
done

#Test max iov
t="sendmsg"
r=1
i=1024
l=1
prog="--txmsg"

TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
echo $TEST
$TEST
sleep 2
prog="--txmsg_redir"
TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
echo $TEST
$TEST

# Test max iov with 1k send

t="sendmsg"
r=1
i=1024
l=1024
prog="--txmsg"

TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
echo $TEST
$TEST
sleep 2
prog="--txmsg_redir"
TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
echo $TEST
$TEST
sleep 2

# Test apply with 1B
r=1
i=1024
l=1024
prog="--txmsg_apply 1"

for t in "sendmsg" "sendpage"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test apply with larger value than send
r=1
i=8
l=1024
prog="--txmsg_apply 2048"

for t in "sendmsg" "sendpage"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test apply with apply that never reaches limit
r=1024
i=1
l=1
prog="--txmsg_apply 2048"

for t in "sendmsg" "sendpage"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test apply and redirect with 1B
r=1
i=1024
l=1024
prog="--txmsg_redir --txmsg_apply 1"

for t in "sendmsg" "sendpage"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

prog="--txmsg_redir --txmsg_apply 1 --txmsg_ingress"

for t in "sendmsg" "sendpage"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done


# Test apply and redirect with larger value than send
r=1
i=8
l=1024
prog="--txmsg_redir --txmsg_apply 2048"

for t in "sendmsg" "sendpage"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

prog="--txmsg_redir --txmsg_apply 2048 --txmsg_ingress"

for t in "sendmsg" "sendpage"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done


# Test apply and redirect with apply that never reaches limit
r=1024
i=1
l=1
prog="--txmsg_apply 2048"

for t in "sendmsg" "sendpage"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test cork with 1B not really useful but test it anyways
r=1
i=1024
l=1024
prog="--txmsg_cork 1"

for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test cork with a more reasonable 100B
r=1
i=1000
l=1000
prog="--txmsg_cork 100"

for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test cork with larger value than send
r=1
i=8
l=1024
prog="--txmsg_cork 2048"

for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test cork with cork that never reaches limit
r=1024
i=1
l=1
prog="--txmsg_cork 2048"

for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

r=1
i=1024
l=1024
prog="--txmsg_redir --txmsg_cork 1"

for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test cork with a more reasonable 100B
r=1
i=1000
l=1000
prog="--txmsg_redir --txmsg_cork 100"

for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test cork with larger value than send
r=1
i=8
l=1024
prog="--txmsg_redir --txmsg_cork 2048"

for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test cork with cork that never reaches limit
r=1024
i=1
l=1
prog="--txmsg_cork 2048"

for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done


# mix and match cork and apply not really useful but valid programs

# Test apply < cork
r=100
i=1
l=5
prog="--txmsg_apply 10 --txmsg_cork 100"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Try again with larger sizes so we hit overflow case
r=100
i=1000
l=2048
prog="--txmsg_apply 4096 --txmsg_cork 8096"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test apply > cork
r=100
i=1
l=5
prog="--txmsg_apply 100 --txmsg_cork 10"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Again with larger sizes so we hit overflow cases
r=100
i=1000
l=2048
prog="--txmsg_apply 8096 --txmsg_cork 4096"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done


# Test apply = cork
r=100
i=1
l=5
prog="--txmsg_apply 10 --txmsg_cork 10"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

r=100
i=1000
l=2048
prog="--txmsg_apply 4096 --txmsg_cork 4096"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test apply < cork
r=100
i=1
l=5
prog="--txmsg_redir --txmsg_apply 10 --txmsg_cork 100"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Try again with larger sizes so we hit overflow case
r=100
i=1000
l=2048
prog="--txmsg_redir --txmsg_apply 4096 --txmsg_cork 8096"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Test apply > cork
r=100
i=1
l=5
prog="--txmsg_redir --txmsg_apply 100 --txmsg_cork 10"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Again with larger sizes so we hit overflow cases
r=100
i=1000
l=2048
prog="--txmsg_redir --txmsg_apply 8096 --txmsg_cork 4096"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done


# Test apply = cork
r=100
i=1
l=5
prog="--txmsg_redir --txmsg_apply 10 --txmsg_cork 10"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

r=100
i=1000
l=2048
prog="--txmsg_redir --txmsg_apply 4096 --txmsg_cork 4096"
for t in "sendpage" "sendmsg"; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog"
	echo $TEST
	$TEST
	sleep 2
done

# Tests for bpf_msg_pull_data()
for i in `seq 99 100 1600`; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t sendpage -r 16 -i 1 -l 100 \
		--txmsg --txmsg_start 0 --txmsg_end $i --txmsg_cork 1600"
	echo $TEST
	$TEST
	sleep 2
done

for i in `seq 199 100 1600`; do
	TEST="./sockmap --cgroup /mnt/cgroup2/ -t sendpage -r 16 -i 1 -l 100 \
		--txmsg --txmsg_start 100 --txmsg_end $i --txmsg_cork 1600"
	echo $TEST
	$TEST
	sleep 2
done

TEST="./sockmap --cgroup /mnt/cgroup2/ -t sendpage -r 16 -i 1 -l 100 \
	--txmsg --txmsg_start 1500 --txmsg_end 1600 --txmsg_cork 1600"
echo $TEST
$TEST
sleep 2

TEST="./sockmap --cgroup /mnt/cgroup2/ -t sendpage -r 16 -i 1 -l 100 \
	--txmsg --txmsg_start 1111 --txmsg_end 1112 --txmsg_cork 1600"
echo $TEST
$TEST
sleep 2

TEST="./sockmap --cgroup /mnt/cgroup2/ -t sendpage -r 16 -i 1 -l 100 \
	--txmsg --txmsg_start 1111 --txmsg_end 0 --txmsg_cork 1600"
echo $TEST
$TEST
sleep 2

TEST="./sockmap --cgroup /mnt/cgroup2/ -t sendpage -r 16 -i 1 -l 100 \
	--txmsg --txmsg_start 0 --txmsg_end 1601 --txmsg_cork 1600"
echo $TEST
$TEST
sleep 2

TEST="./sockmap --cgroup /mnt/cgroup2/ -t sendpage -r 16 -i 1 -l 100 \
	--txmsg --txmsg_start 0 --txmsg_end 1601 --txmsg_cork 1602"
echo $TEST
$TEST
sleep 2

# Run through gamut again with start and end
for prog in "--txmsg" "--txmsg_redir" "--txmsg_drop"; do
for t in "sendmsg" "sendpage"; do
for r in 1 10 100; do
	for i in 1 10 100; do
		for l in 1 10 100; do
			TEST="./sockmap --cgroup /mnt/cgroup2/ -t $t -r $r -i $i -l $l $prog --txmsg_start 1 --txmsg_end 2"
			echo $TEST
			$TEST
			sleep 2
		done
	done
done
done
done

# Some specific tests to cover specific code paths
./sockmap --cgroup /mnt/cgroup2/ -t sendpage \
	-r 5 -i 1 -l 1 --txmsg_redir --txmsg_cork 5 --txmsg_apply 3
./sockmap --cgroup /mnt/cgroup2/ -t sendmsg \
	-r 5 -i 1 -l 1 --txmsg_redir --txmsg_cork 5 --txmsg_apply 3
./sockmap --cgroup /mnt/cgroup2/ -t sendpage \
	-r 5 -i 1 -l 1 --txmsg_redir --txmsg_cork 5 --txmsg_apply 5
./sockmap --cgroup /mnt/cgroup2/ -t sendmsg \
	-r 5 -i 1 -l 1 --txmsg_redir --txmsg_cork 5 --txmsg_apply 5

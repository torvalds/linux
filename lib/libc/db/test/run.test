#!/bin/sh -
#
#	@(#)run.test	8.10 (Berkeley) 7/26/94
#

# db regression tests
main()
{

	PROG=./dbtest
	TMP1=t1
	TMP2=t2
	TMP3=t3

	if [ -f /usr/share/dict/words ]; then
		DICT=/usr/share/dict/words
	elif [ -f /usr/dict/words ]; then
		DICT=/usr/dict/words
	else
		echo 'run.test: no dictionary'
		exit 1
	fi
	
	if [ $# -eq 0 ]; then
		for t in 1 2 3 4 5 6 7 8 9 10 11 12 13 20; do
			test$t
		done
	else
		while [ $# -gt 0 ]
			do case "$1" in
			test*)
				$1;;
			[0-9]*)
				test$1;;
			btree)
				for t in 1 2 3 7 8 9 10 12 13; do
					test$t
				done;;
			hash)
				for t in 1 2 3 8 13 20; do
					test$t
				done;;
			recno)
				for t in 1 2 3 4 5 6 7 10 11; do
					test$t
				done;;
			*)
				echo "run.test: unknown test $1"
				echo "usage: run.test test# | type"
				exit 1
			esac
			shift
		done
	fi
	rm -f $TMP1 $TMP2 $TMP3
	exit 0
}

# Take the first hundred entries in the dictionary, and make them
# be key/data pairs.
test1()
{
	echo "Test 1: btree, hash: small key, small data pairs"
	sed 200q $DICT > $TMP1
	for type in btree hash; do
		rm -f $TMP2 $TMP3
		for i in `sed 200q $DICT`; do
			echo p
			echo k$i
			echo d$i
			echo g
			echo k$i
		done > $TMP2
		$PROG -o $TMP3 $type $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test1: type $type: failed"
			exit 1
		fi
	done
	echo "Test 1: recno: small key, small data pairs"
	rm -f $TMP2 $TMP3
	sed 200q $DICT |
	awk '{ 
		++i;
		printf("p\nk%d\nd%s\ng\nk%d\n", i, $0, i);
	}' > $TMP2
	$PROG -o $TMP3 recno $TMP2
	if (cmp -s $TMP1 $TMP3) ; then :
	else
		echo "test1: type recno: failed"
		exit 1
	fi
}

# Take the first 200 entries in the dictionary, and give them
# each a medium size data entry.
test2()
{
	echo "Test 2: btree, hash: small key, medium data pairs"
	mdata=abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz
	echo $mdata |
	awk '{ for (i = 1; i < 201; ++i) print $0 }' > $TMP1
	for type in hash btree; do
		rm -f $TMP2 $TMP3
		for i in `sed 200q $DICT`; do
			echo p
			echo k$i
			echo d$mdata
			echo g
			echo k$i
		done > $TMP2
		$PROG -o $TMP3 $type $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test2: type $type: failed"
			exit 1
		fi
	done
	echo "Test 2: recno: small key, medium data pairs"
	rm -f $TMP2 $TMP3
	echo $mdata | 
	awk '{  for (i = 1; i < 201; ++i)
		printf("p\nk%d\nd%s\ng\nk%d\n", i, $0, i);
	}' > $TMP2
	$PROG -o $TMP3 recno $TMP2
	if (cmp -s $TMP1 $TMP3) ; then :
	else
		echo "test2: type recno: failed"
		exit 1
	fi
}

# Insert the programs in /bin with their paths as their keys.
test3()
{
	echo "Test 3: hash: small key, big data pairs"
	rm -f $TMP1
	(find /bin -type f -print | xargs cat) > $TMP1
	for type in hash; do
		rm -f $TMP2 $TMP3
		for i in `find /bin -type f -print`; do
			echo p
			echo k$i
			echo D$i
			echo g
			echo k$i
		done > $TMP2
		$PROG -o $TMP3 $type $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test3: $type: failed"
			exit 1
		fi
	done
	echo "Test 3: btree: small key, big data pairs"
	for psize in 512 16384 65536; do
		echo "    page size $psize"
		for type in btree; do
			rm -f $TMP2 $TMP3
			for i in `find /bin -type f -print`; do
				echo p
				echo k$i
				echo D$i
				echo g
				echo k$i
			done > $TMP2
			$PROG -i psize=$psize -o $TMP3 $type $TMP2
			if (cmp -s $TMP1 $TMP3) ; then :
			else
				echo "test3: $type: page size $psize: failed"
				exit 1
			fi
		done
	done
	echo "Test 3: recno: big data pairs"
	rm -f $TMP2 $TMP3
	find /bin -type f -print | 
	awk '{
		++i;
		printf("p\nk%d\nD%s\ng\nk%d\n", i, $0, i);
	}' > $TMP2
	for psize in 512 16384 65536; do
		echo "    page size $psize"
		$PROG -i psize=$psize -o $TMP3 recno $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test3: recno: page size $psize: failed"
			exit 1
		fi
	done
}

# Do random recno entries.
test4()
{
	echo "Test 4: recno: random entries"
	echo "abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg" |
	awk '{
		for (i = 37; i <= 37 + 88 * 17; i += 17) {
			if (i % 41)
				s = substr($0, 1, i % 41);
			else
				s = substr($0, 1);
			printf("input key %d: %s\n", i, s);
		}
		for (i = 1; i <= 15; ++i) {
			if (i % 41)
				s = substr($0, 1, i % 41);
			else
				s = substr($0, 1);
			printf("input key %d: %s\n", i, s);
		}
		for (i = 19234; i <= 19234 + 61 * 27; i += 27) {
			if (i % 41)
				s = substr($0, 1, i % 41);
			else
				s = substr($0, 1);
			printf("input key %d: %s\n", i, s);
		}
		exit
	}' > $TMP1
	rm -f $TMP2 $TMP3
	cat $TMP1 |
	awk 'BEGIN {
			i = 37;
			incr = 17;
		}
		{
			printf("p\nk%d\nd%s\n", i, $0);
			if (i == 19234 + 61 * 27)
				exit;
			if (i == 37 + 88 * 17) {
				i = 1;
				incr = 1;
			} else if (i == 15) {
				i = 19234;
				incr = 27;
			} else
				i += incr;
		}
		END {
			for (i = 37; i <= 37 + 88 * 17; i += 17)
				printf("g\nk%d\n", i);
			for (i = 1; i <= 15; ++i)
				printf("g\nk%d\n", i);
			for (i = 19234; i <= 19234 + 61 * 27; i += 27)
				printf("g\nk%d\n", i);
		}' > $TMP2
	$PROG -o $TMP3 recno $TMP2
	if (cmp -s $TMP1 $TMP3) ; then :
	else
		echo "test4: type recno: failed"
		exit 1
	fi
}

# Do reverse order recno entries.
test5()
{
	echo "Test 5: recno: reverse order entries"
	echo "abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg" |
	awk ' {
		for (i = 1500; i; --i) {
			if (i % 34)
				s = substr($0, 1, i % 34);
			else
				s = substr($0, 1);
			printf("input key %d: %s\n", i, s);
		}
		exit;
	}' > $TMP1
	rm -f $TMP2 $TMP3
	cat $TMP1 |
	awk 'BEGIN {
			i = 1500;
		}
		{
			printf("p\nk%d\nd%s\n", i, $0);
			--i;
		}
		END {
			for (i = 1500; i; --i) 
				printf("g\nk%d\n", i);
		}' > $TMP2
	$PROG -o $TMP3 recno $TMP2
	if (cmp -s $TMP1 $TMP3) ; then :
	else
		echo "test5: type recno: failed"
		exit 1
	fi
}
		
# Do alternating order recno entries.
test6()
{
	echo "Test 6: recno: alternating order entries"
	echo "abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg" |
	awk ' {
		for (i = 1; i < 1200; i += 2) {
			if (i % 34)
				s = substr($0, 1, i % 34);
			else
				s = substr($0, 1);
			printf("input key %d: %s\n", i, s);
		}
		for (i = 2; i < 1200; i += 2) {
			if (i % 34)
				s = substr($0, 1, i % 34);
			else
				s = substr($0, 1);
			printf("input key %d: %s\n", i, s);
		}
		exit;
	}' > $TMP1
	rm -f $TMP2 $TMP3
	cat $TMP1 |
	awk 'BEGIN {
			i = 1;
			even = 0;
		}
		{
			printf("p\nk%d\nd%s\n", i, $0);
			i += 2;
			if (i >= 1200) {
				if (even == 1)
					exit;
				even = 1;
				i = 2;
			}
		}
		END {
			for (i = 1; i < 1200; ++i) 
				printf("g\nk%d\n", i);
		}' > $TMP2
	$PROG -o $TMP3 recno $TMP2
	sort -o $TMP1 $TMP1
	sort -o $TMP3 $TMP3
	if (cmp -s $TMP1 $TMP3) ; then :
	else
		echo "test6: type recno: failed"
		exit 1
	fi
}

# Delete cursor record
test7()
{
	echo "Test 7: btree, recno: delete cursor record"
	echo "abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg" |
	awk '{
		for (i = 1; i <= 120; ++i)
			printf("%05d: input key %d: %s\n", i, i, $0);
		printf("%05d: input key %d: %s\n", 120, 120, $0);
		printf("seq failed, no such key\n");
		printf("%05d: input key %d: %s\n", 1, 1, $0);
		printf("%05d: input key %d: %s\n", 2, 2, $0);
		exit;
	}' > $TMP1
	rm -f $TMP2 $TMP3

	for type in btree recno; do
		cat $TMP1 |
		awk '{
			if (i == 120)
				exit;
			printf("p\nk%d\nd%s\n", ++i, $0);
		}
		END {
			printf("fR_NEXT\n");
			for (i = 1; i <= 120; ++i)
				printf("s\n");
			printf("fR_CURSOR\ns\nk120\n");
			printf("r\n");
			printf("fR_NEXT\ns\n");
			printf("fR_CURSOR\ns\nk1\n");
			printf("r\n");
			printf("fR_FIRST\ns\n");
		}' > $TMP2
		$PROG -o $TMP3 recno $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test7: type $type: failed"
			exit 1
		fi
	done
}

# Make sure that overflow pages are reused.
test8()
{
	echo "Test 8: btree, hash: repeated small key, big data pairs"
	rm -f $TMP1
	echo "" | 
	awk 'BEGIN {
		for (i = 1; i <= 10; ++i) {
			printf("p\nkkey1\nD/bin/sh\n");
			printf("p\nkkey2\nD/bin/csh\n");
			if (i % 8 == 0) {
				printf("c\nkkey2\nD/bin/csh\n");
				printf("c\nkkey1\nD/bin/sh\n");
				printf("e\t%d of 10 (comparison)\n", i);
			} else
				printf("e\t%d of 10             \n", i);
			printf("r\nkkey1\nr\nkkey2\n");
		}
	}' > $TMP1
	$PROG btree $TMP1
#	$PROG hash $TMP1
	# No explicit test for success.
}

# Test btree duplicate keys
test9()
{
	echo "Test 9: btree: duplicate keys"
	echo "abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg" |
	awk '{
		for (i = 1; i <= 543; ++i)
			printf("%05d: input key %d: %s\n", i, i, $0);
		exit;
	}' > $TMP1
	rm -f $TMP2 $TMP3

	for type in btree; do
		cat $TMP1 | 
		awk '{
			if (i++ % 2)
				printf("p\nkduplicatekey\nd%s\n", $0);
			else
				printf("p\nkunique%dkey\nd%s\n", i, $0);
		}
		END {
				printf("o\n");
		}' > $TMP2
		$PROG -iflags=1 -o $TMP3 $type $TMP2
		sort -o $TMP3 $TMP3
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test9: type $type: failed"
			exit 1
		fi
	done
}

# Test use of cursor flags without initialization
test10()
{
	echo "Test 10: btree, recno: test cursor flag use"
	echo "abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg" |
	awk '{
		for (i = 1; i <= 20; ++i)
			printf("%05d: input key %d: %s\n", i, i, $0);
		exit;
	}' > $TMP1
	rm -f $TMP2 $TMP3

	# Test that R_CURSOR doesn't succeed before cursor initialized
	for type in btree recno; do
		cat $TMP1 |
		awk '{
			if (i == 10)
				exit;
			printf("p\nk%d\nd%s\n", ++i, $0);
		}
		END {
			printf("fR_CURSOR\nr\n");
			printf("eR_CURSOR SHOULD HAVE FAILED\n");
		}' > $TMP2
		$PROG -o $TMP3 $type $TMP2 > /dev/null 2>&1
		if [ -s $TMP3 ] ; then
			echo "Test 10: delete: R_CURSOR SHOULD HAVE FAILED"
			exit 1
		fi
	done
	for type in btree recno; do
		cat $TMP1 |
		awk '{
			if (i == 10)
				exit;
			printf("p\nk%d\nd%s\n", ++i, $0);
		}
		END {
			printf("fR_CURSOR\np\nk1\ndsome data\n");
			printf("eR_CURSOR SHOULD HAVE FAILED\n");
		}' > $TMP2
		$PROG -o $TMP3 $type $TMP2 > /dev/null 2>&1
		if [ -s $TMP3 ] ; then
			echo "Test 10: put: R_CURSOR SHOULD HAVE FAILED"
			exit 1
		fi
	done
}

# Test insert in reverse order.
test11()
{
	echo "Test 11: recno: reverse order insert"
	echo "abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg" |
	awk '{
		for (i = 1; i <= 779; ++i)
			printf("%05d: input key %d: %s\n", i, i, $0);
		exit;
	}' > $TMP1
	rm -f $TMP2 $TMP3

	for type in recno; do
		cat $TMP1 |
		awk '{
			if (i == 0) {
				i = 1;
				printf("p\nk1\nd%s\n", $0);
				printf("%s\n", "fR_IBEFORE");
			} else
				printf("p\nk1\nd%s\n", $0);
		}
		END {
				printf("or\n");
		}' > $TMP2
		$PROG -o $TMP3 $type $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test11: type $type: failed"
			exit 1
		fi
	done
}

# Take the first 20000 entries in the dictionary, reverse them, and give
# them each a small size data entry.  Use a small page size to make sure
# the btree split code gets hammered.
test12()
{
	echo "Test 12: btree: lots of keys, small page size"
	mdata=abcdefghijklmnopqrstuvwxy
	echo $mdata |
	awk '{ for (i = 1; i < 20001; ++i) print $0 }' > $TMP1
	for type in btree; do
		rm -f $TMP2 $TMP3
		for i in `sed 20000q $DICT | rev`; do
			echo p
			echo k$i
			echo d$mdata
			echo g
			echo k$i
		done > $TMP2
		$PROG -i psize=512 -o $TMP3 $type $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test12: type $type: failed"
			exit 1
		fi
	done
}

# Test different byte orders.
test13()
{
	echo "Test 13: btree, hash: differing byte orders"
	sed 50q $DICT > $TMP1
	for order in 1234 4321; do
		for type in btree hash; do
			rm -f byte.file $TMP2 $TMP3
			for i in `sed 50q $DICT`; do
				echo p
				echo k$i
				echo d$i
				echo g
				echo k$i
			done > $TMP2
			$PROG -ilorder=$order -f byte.file -o $TMP3 $type $TMP2
			if (cmp -s $TMP1 $TMP3) ; then :
			else
				echo "test13: $type/$order put failed"
				exit 1
			fi
			for i in `sed 50q $DICT`; do
				echo g
				echo k$i
			done > $TMP2
			$PROG -s \
			    -ilorder=$order -f byte.file -o $TMP3 $type $TMP2
			if (cmp -s $TMP1 $TMP3) ; then :
			else
				echo "test13: $type/$order get failed"
				exit 1
			fi
		done
	done
	rm -f byte.file
}

# Try a variety of bucketsizes and fill factors for hashing
test20()
{
	echo\
    "Test 20: hash: bucketsize, fill factor; nelem 25000 cachesize 65536"
	echo "abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg" |
	awk '{
		for (i = 1; i <= 10000; ++i) {
			if (i % 34)
				s = substr($0, 1, i % 34);
			else
				s = substr($0, 1);
			printf("%s\n", s);
		}
		exit;
	}' > $TMP1
	sed 10000q $DICT |
	awk 'BEGIN {
		ds="abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg abcdefg"
	}
	{
		if (++i % 34)
			s = substr(ds, 1, i % 34);
		else
			s = substr(ds, 1);
		printf("p\nk%s\nd%s\n", $0, s);
	}' > $TMP2
	sed 10000q $DICT |
	awk '{
		++i;
		printf("g\nk%s\n", $0);
	}' >> $TMP2
	bsize=256
	for ffactor in 11 14 21; do
		echo "    bucketsize $bsize, fill factor $ffactor"
		$PROG -o$TMP3 \
		    -ibsize=$bsize,ffactor=$ffactor,nelem=25000,cachesize=65536\
		    hash $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test20: type hash:\
bsize=$bsize ffactor=$ffactor nelem=25000 cachesize=65536 failed"
			exit 1
		fi
	done
	bsize=512
	for ffactor in 21 28 43; do
		echo "    bucketsize $bsize, fill factor $ffactor"
		$PROG -o$TMP3 \
		    -ibsize=$bsize,ffactor=$ffactor,nelem=25000,cachesize=65536\
		    hash $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test20: type hash:\
bsize=$bsize ffactor=$ffactor nelem=25000 cachesize=65536 failed"
			exit 1
		fi
	done
	bsize=1024
	for ffactor in 43 57 85; do
		echo "    bucketsize $bsize, fill factor $ffactor"
		$PROG -o$TMP3 \
		    -ibsize=$bsize,ffactor=$ffactor,nelem=25000,cachesize=65536\
		    hash $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test20: type hash:\
bsize=$bsize ffactor=$ffactor nelem=25000 cachesize=65536 failed"
			exit 1
		fi
	done
	bsize=2048
	for ffactor in 85 114 171; do
		echo "    bucketsize $bsize, fill factor $ffactor"
		$PROG -o$TMP3 \
		    -ibsize=$bsize,ffactor=$ffactor,nelem=25000,cachesize=65536\
		    hash $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test20: type hash:\
bsize=$bsize ffactor=$ffactor nelem=25000 cachesize=65536 failed"
			exit 1
		fi
	done
	bsize=4096
	for ffactor in 171 228 341; do
		echo "    bucketsize $bsize, fill factor $ffactor"
		$PROG -o$TMP3 \
		    -ibsize=$bsize,ffactor=$ffactor,nelem=25000,cachesize=65536\
		    hash $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test20: type hash:\
bsize=$bsize ffactor=$ffactor nelem=25000 cachesize=65536 failed"
			exit 1
		fi
	done
	bsize=8192
	for ffactor in 341 455 683; do
		echo "    bucketsize $bsize, fill factor $ffactor"
		$PROG -o$TMP3 \
		    -ibsize=$bsize,ffactor=$ffactor,nelem=25000,cachesize=65536\
		    hash $TMP2
		if (cmp -s $TMP1 $TMP3) ; then :
		else
			echo "test20: type hash:\
bsize=$bsize ffactor=$ffactor nelem=25000 cachesize=65536 failed"
			exit 1
		fi
	done
}

main $*

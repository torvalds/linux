all:
	gcc -O2 -lrt mq_open_tests.c -o mq_open_tests
	gcc -O2 -lrt -lpthread -lpopt -o mq_perf_tests mq_perf_tests.c

run_tests:
	./mq_open_tests /test1
	./mq_perf_tests

clean:
	rm -f mq_open_tests mq_perf_tests

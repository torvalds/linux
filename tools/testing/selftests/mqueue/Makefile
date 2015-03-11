all:
	gcc -O2 mq_open_tests.c -o mq_open_tests -lrt
	gcc -O2 -o mq_perf_tests mq_perf_tests.c -lrt -lpthread -lpopt

include ../lib.mk

override define RUN_TESTS
	@./mq_open_tests /test1 || echo "selftests: mq_open_tests [FAIL]"
	@./mq_perf_tests || echo "selftests: mq_perf_tests [FAIL]"
endef

clean:
	rm -f mq_open_tests mq_perf_tests

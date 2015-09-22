CFLAGS += -O2
LDLIBS = -lrt -lpthread -lpopt
TEST_PROGS := mq_open_tests mq_perf_tests

all: $(TEST_PROGS)

include ../lib.mk

override define RUN_TESTS
	@./mq_open_tests /test1 || echo "selftests: mq_open_tests [FAIL]"
	@./mq_perf_tests || echo "selftests: mq_perf_tests [FAIL]"
endef

override define EMIT_TESTS
	echo "./mq_open_tests /test1 || echo \"selftests: mq_open_tests [FAIL]\""
	echo "./mq_perf_tests || echo \"selftests: mq_perf_tests [FAIL]\""
endef

clean:
	rm -f mq_open_tests mq_perf_tests

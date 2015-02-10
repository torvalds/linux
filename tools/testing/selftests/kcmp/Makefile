CC := $(CROSS_COMPILE)$(CC)
CFLAGS += -I../../../../usr/include/

all: kcmp_test

run_tests: all
	@./kcmp_test || echo "kcmp_test: [FAIL]"

clean:
	$(RM) kcmp_test kcmp-test-file

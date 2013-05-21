CFLAGS += -iquote../../../../include/uapi -Wall
soft-dirty: soft-dirty.c

all: soft-dirty

clean:
	rm -f soft-dirty

run_tests: all
	@./soft-dirty || echo "soft-dirty selftests: [FAIL]"

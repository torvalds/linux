CFLAGS += -iquote../../../../include/uapi -Wall
peeksiginfo: peeksiginfo.c

all: peeksiginfo

clean:
	rm -f peeksiginfo

run_tests: all
	@./peeksiginfo || echo "peeksiginfo selftests: [FAIL]"

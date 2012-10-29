# Makefile for epoll selftests

all: test_epoll
%: %.c
	gcc -pthread -g -o $@ $^

run_tests: all
	./test_epoll

clean:
	$(RM) test_epoll

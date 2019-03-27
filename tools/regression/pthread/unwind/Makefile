# $FreeBSD$
all: main_thread_exit thread_normal_exit sem_wait_cancel \
     cond_wait_cancel cond_wait_cancel2 catch_pthread_exit

.cpp:
	c++ -o $@ $< -lpthread
	
main_thread_exit: main_thread_exit.cpp
thread_normal_exit: thread_normal_exit.cpp
sem_wait_cancel: sem_wait_cancel.cpp
cond_wait_cancel: cond_wait_cancel.cpp
cond_wait_cancel2: cond_wait_cancel2.cpp
catch_pthread_exit: catch_pthread_exit.cpp

clean: .PHONY
	rm -rf main_thread_exit thread_normal_exit sem_wait_cancel \
	cond_wait_cancel cond_wait_cancel2 catch_pthread_exit

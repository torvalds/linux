CC=arm-linux-gnueabihf-gcc
#CFLAGS=-Wall

all:
	$(CC) $(CFLAGS) stp_uart_launcher.c -lpthread -o stp_uart_launcher
	$(CC) $(CFLAGS) wmt_loopback.c -o wmt_loopback
	$(CC) $(CFLAGS) wmt_loader.c -o wmt_loader
	
.PHONY:clean
clean:
	rm -rf stp_uart_launcher wmt_loopback wmt_loader *.o



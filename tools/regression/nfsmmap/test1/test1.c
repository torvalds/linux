#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    int fd, fd2;
    caddr_t addr;
    char zeros[4096];
    char ones[200];

    memset(zeros, 0, sizeof zeros);
    memset(ones, 1, sizeof ones);
#if 0
    unlink("test1.data");
    fd = open("test1.data", O_RDWR|O_CREAT, 0666);
    if (fd < 0)
	err(1, "creating file");
    if (write(fd, zeros, sizeof zeros) < 0)
	err(1, "writing zeros");
    close(fd);
#endif

    fd = open("test1.data", O_RDWR);
    if (fd < 0)
	err(1, "opening file");
    if (lseek(fd, 600, SEEK_SET) < 0)
	err(1, "seeking");
	
    if (write(fd, ones, sizeof ones) < 0)
	err(1, "writing ones");

    fsync(fd);

    addr = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED)
	err(1, "mapping");
    unlink("test1.scratch");
    fd2 = open("test1.scratch", O_RDWR|O_CREAT, 0666);
    if (fd2 < 0)
	err(1, "creating scratch");
    
    if (write(fd2, addr, 4096) < 0)
	err(1, "writing scratch");
}

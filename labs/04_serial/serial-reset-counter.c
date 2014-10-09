#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#define SERIAL_RESET_COUNTER 0
#define SERIAL_GET_COUNTER 1

int main(void)
{
	int fd, ret;

	fd = open("/dev/feserial-481a8000", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Unable to open /dev/feserial-481a8000\n");
		exit(1);
	}
	ret = ioctl(fd, SERIAL_RESET_COUNTER);
	if (ret < 0) {
		fprintf(stderr, "Unable to get counter from /dev/feserial-481a8000\n");
		exit(1);
	}
	close(fd);
	printf("Counter reset /dev/feserial-481a8000\n");

	fd = open("/dev/feserial-48024000", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Unable to open /dev/feserial-48024000\n");
		exit(1);
	}
	ret = ioctl(fd, SERIAL_RESET_COUNTER);
	if (ret < 0) {
		fprintf(stderr, "Unable to get counter from /dev/feserial-48024000\n");
		exit(1);
	}
	close(fd);
	printf("Counter reset /dev/feserial-48024000\n");

	return 0;
}

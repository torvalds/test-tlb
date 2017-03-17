#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <math.h>

#define PAGE_SIZE 4096

#define FREQ 3.9

static void die(const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	exit(1);
}

static volatile int stop = 0;

void alarm_handler(int sig)
{
	stop = 1;
}

static void do_test(void *map, unsigned long size)
{
	unsigned long count = 0, offset = 0, cycles;
	signal(SIGALRM, alarm_handler);
	alarm(1);

	do {
		count++;
		offset += *(unsigned int *)(map + offset);
		offset &= size-1;
	} while (!stop);
	cycles = lrint(10*(double)FREQ*1000*1000*1000 / count);
	printf("%lu.%06lu iterations (~%lu.%lu cycles) in one second, got to %lu\n",
		count / 1000000,
		count % 1000000,
		cycles / 10,
		cycles % 10,
		offset);
}

static unsigned long get_num(const char *str)
{
	char *end, c;
	unsigned long val;

	if (!str)
		return 0;
	val = strtoul(str, &end, 0);
	if (!val || val == ULONG_MAX)
		return 0;
	while ((c = *end++) != 0) {
		switch (c) {
		case 'k':
			val <<= 10;
			break;
		case 'M':
			val <<= 20;
			break;
		case 'G':
			val <<= 30;
			break;
		default:
			return 0;
		}
	}
	return val;
}

static void *create_mmap(int fd, unsigned long size, unsigned long stride)
{
	unsigned long off;
	void *base = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED)
		die("base mmap failed");
	for (off = 0; off < size; off += PAGE_SIZE) {
		void *page = mmap(base + off, PAGE_SIZE, PROT_READ, MAP_FIXED | MAP_SHARED | MAP_FILE, fd, 0);
		if (page != base + off)
			die("file mmap failed at offset %ld", off);
	}
	return base;
}

int main(int argc, char **argv)
{
	int fd, i;
	unsigned long stride, size;

	size = get_num(argv[1]);
	stride = get_num(argv[2]);
	if (!stride || stride & 3 || size < stride)
		die("bad arguments: test-tlb <size> <stride>");
	fd = open(".", O_RDWR | O_TMPFILE | O_EXCL, 0);
	if (fd < 0)
		die("tempfile creation failed");
	for (i = 0; i < PAGE_SIZE / 4; i++) {
		unsigned int data = stride;
		if (write(fd, &data, 4) != 4)
			die("stride write failed");
	}

	do_test(create_mmap(fd, size, stride), size);
	return 0;
}

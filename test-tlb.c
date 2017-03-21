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
#include <time.h>

#define PAGE_SIZE 4096

#define FREQ 3.9

static int test_hugepage = 0;
static int random_list = 0;

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
		offset = *(unsigned int *)(map + offset);
		offset &= size-1;
	} while (!stop);
	cycles = lrint(10*(double)FREQ*1000*1000*1000 / count);
	printf("%.5fns %lu.%06lu iterations (~%lu.%lu cycles) in one second, got to %lu\n",
		1000000000.0 / count,
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

static void *create_map(unsigned long size, unsigned long stride)
{
	int fd, i;
	unsigned long off;

	fd = open(".", O_RDWR | O_TMPFILE | O_EXCL, 0);
	if (fd < 0)
		die("tempfile creation failed");
	for (i = 0; i < PAGE_SIZE / 4; i++) {
		unsigned int data = stride;
		if (write(fd, &data, 4) != 4)
			die("stride write failed");
	}


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

// Hugepage size
#define HUGEPAGE (2*1024*1024)

static void *create_random_map(unsigned long size, unsigned long stride)
{
	unsigned int *lastpos;
	unsigned int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	unsigned long off, mapsize;
	unsigned int *rnd;
	void *map;
	int n;

	rnd = calloc(size / stride + 1, sizeof(unsigned int));
	if (!rnd)
		die("out of memory");
	for (n = 0, off = 0; off < size; n++, off += stride)
		rnd[n] = off;
	for (n = 0, off = 0; off < size; n++, off += stride) {
		unsigned int m = (unsigned long)random() % (size / stride);
		unsigned int tmp = rnd[n];
		rnd[n] = rnd[m];
		rnd[m] = tmp;
	}

	mapsize = size;
	if (test_hugepage)
		mapsize += 2*HUGEPAGE;

	map = mmap(NULL, mapsize, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (map == MAP_FAILED)
		die("mmap failed");

	if (test_hugepage) {
		unsigned long mapstart = (unsigned long) map;
		mapstart += HUGEPAGE-1;
		mapstart &= ~(HUGEPAGE-1);
		map = (void *)mapstart;

		mapsize = size + HUGEPAGE-1;
		mapsize &= ~(HUGEPAGE-1);

		madvise(map, mapsize, MADV_HUGEPAGE);
	}

	/* Create a random mapping */
	srandom(time(NULL));

	/* Create a circular list of offsets */
	lastpos = map;
	for (n = 0, off = 0; off < size; n++, off += stride) {
		lastpos = map + rnd[n];
		*lastpos = rnd[n+1];
	}
	*lastpos = rnd[0];

	free(rnd);

	return map;
}

int main(int argc, char **argv)
{
	unsigned long stride, size;
	const char *arg;
	void *map;

	while ((arg = argv[1]) != NULL) {
		if (*arg != '-')
			break;
		for (;;) {
			switch (*++arg) {
			case 0:
				break;
			case 'H':
				test_hugepage = 1;
			/* fall-through: hugepage implies random-list */
			case 'r':
				random_list = 1;
				break;
			default:
				die("Unknown flag '%s'", arg);
			}
			break;
		}
		argv++;
	}

	size = get_num(argv[1]);
	stride = get_num(argv[2]);
	if (!stride || stride & 3 || size < stride)
		die("bad arguments: test-tlb [-H] <size> <stride>");

	if (random_list)
		map = create_random_map(size, stride);
	else
		map = create_map(size, stride);

	do_test(map, size);
	return 0;
}

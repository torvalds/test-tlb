#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
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

static unsigned long do_test(void *map)
{
	unsigned long count = 0, offset = 0, ns;
	struct timeval start, end;
	double cycles;

	signal(SIGALRM, alarm_handler);
	alarm(1);

	gettimeofday(&start, NULL);
	do {
		count++;
		offset = *(unsigned int *)(map + offset);
	} while (!stop);
	gettimeofday(&end, NULL);
	ns = (end.tv_sec - start.tv_sec)*1000000;
	ns += end.tv_usec - start.tv_usec;
	ns *= 1000;

	cycles = (double) ns / count;
	printf("%6.2fns (~%.1f cycles)\n",
		cycles, cycles*FREQ);
	return offset;
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

static void randomize_map(void *map, unsigned long size, unsigned long stride)
{
	unsigned long off;
	unsigned int *lastpos, *rnd;
	int n;

	rnd = calloc(size / stride + 1, sizeof(unsigned int));
	if (!rnd)
		die("out of memory");

	/* Create sorted list of offsets */
	for (n = 0, off = 0; off < size; n++, off += stride)
		rnd[n] = off;

	/* Randomize the offsets */
	for (n = 0, off = 0; off < size; n++, off += stride) {
		unsigned int m = (unsigned long)random() % (size / stride);
		unsigned int tmp = rnd[n];
		rnd[n] = rnd[m];
		rnd[m] = tmp;
	}

	/* Create a circular list from the random offsets */
	lastpos = map;
	for (n = 0, off = 0; off < size; n++, off += stride) {
		lastpos = map + rnd[n];
		*lastpos = rnd[n+1];
	}
	*lastpos = rnd[0];

	free(rnd);
}

// Hugepage size
#define HUGEPAGE (2*1024*1024)

static void *create_map(unsigned long size, unsigned long stride)
{
	unsigned int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	unsigned long off, mapsize;
	unsigned int *lastpos;
	void *map;

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

	lastpos = map;
	for (off = 0; off < size; off += stride) {
		lastpos = map + off;
		*lastpos = off + stride;
	}
	*lastpos = 0;

	return map;
}

int main(int argc, char **argv)
{
	unsigned long stride, size;
	const char *arg;
	void *map;

	srandom(time(NULL));

	while ((arg = argv[1]) != NULL) {
		if (*arg != '-')
			break;
		for (;;) {
			switch (*++arg) {
			case 0:
				break;
			case 'H':
				test_hugepage = 1;
				continue;
			case 'r':
				random_list = 1;
				continue;
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

	map = create_map(size, stride);

	if (random_list)
		randomize_map(map, size, stride);

	stop = do_test(map);
	return 0;
}

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

static unsigned long usec_diff(const struct timeval *restrict a, const struct timeval *restrict b)
{
	unsigned long usec;

	usec = (b->tv_sec - a->tv_sec) * 1000000 + b->tv_usec - a->tv_usec;
	return usec;
}

/*
 * Warmup run.
 *
 * This is mainly to make sure that we can go around the
 * map without timing any writeback activity from the cache
 * from creating the map.
 */
static unsigned long warmup(volatile unsigned int *offset)
{
	struct timeval start, end;

	gettimeofday(&start, NULL);
	do
	{
		*offset = *(volatile unsigned int *)(offset + *offset);
	} while (*offset);
	gettimeofday(&end, NULL);

	return usec_diff(&start, &end);
}

static unsigned long get_num(const char *str)
{
	static const unsigned long sizes[] = {1, 1 << 10, 1 << 20, 1 << 30};
	static const char suffixes[] = {' ', 'k', 'M', 'G'};
	unsigned long size = 0;
	char *end;

	if (!str)
	{
		return 0;
	}
	size = strtoul(str, &end, 0);
	if (*end && end[1])
	{
		const char *suffix = strchr(suffixes, *end);
		if (suffix)
		{
			size *= sizes[suffix - suffixes];
		}
		else
		{
			return 0;
		}
	}

	return size;
}

static void randomize_map(volatile unsigned int *map, unsigned long size, unsigned long stride)
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
	for (n = 0, off = 0; off < size; n++, off += stride)
	{
		unsigned int m = (unsigned long)random() % (size / stride);
		unsigned int tmp = rnd[n];
		rnd[n] = rnd[m];
		rnd[m] = tmp;
	}

	/* Create a circular list from the random offsets */
	lastpos = map;
	for (n = 0, off = 0; off < size; n++, off += stride)
	{
		lastpos = map + rnd[n];
		*lastpos = rnd[n + 1];
	}
	*lastpos = rnd[0];

	free(rnd);
}

// Hugepage size
#define HUGEPAGE (2 * 1024 * 1024)

static unsigned int *create_map(unsigned long size, unsigned long stride)
{
	unsigned int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	unsigned long off, mapsize;
	unsigned int *lastpos, *map;

	/*
	 * If we're using hugepages, we will just re-use any existing
	 * hugepage map - the issues with different physical page
	 * allocations for cache associativity testing just isn't worth
	 * it with large pages.
	 *
	 * With regular pages, just mmap over the old allocation to
	 * force new page allocations. Hopefully this will then make
	 * the virtual mapping different enough to matter for timings.
	 */
	if (test_hugepage)
	{
		flags |= MAP_HUGETLB;
		mapsize = size + HUGEPAGE;
	}
	else
	{
		mapsize = size;
	}

	map = aligned_alloc(PAGE_SIZE, mapsize);
	if (!map)
		die("aligned_alloc failed");

	if (test_hugepage)
	{
		map += PAGE_SIZE;
		mapsize -= PAGE_SIZE
	}

	memset(map, 0, mapsize);

	if (random_list)
	{
		randomize_map(map, size, stride);
	}
	else
	{
		/* Create a circular list */
		lastpos = map;
		for (off = 0; off < size - stride; off += stride)
		{
			lastpos = map + off;
			*lastpos = off + stride;
		}
		*lastpos = 0;
	}

	return map;
}

int main(int argc, char **argv)
{
	unsigned int *map, *curpos;
	unsigned long size, stride, count, totalcount, i, j;
	unsigned long usec, minusec, maxusec, avgusec;
	struct sigaction act;
	struct timeval start, end;

	if (argc != 4 && argc != 5)
	{
		printf("usage: %s size stride count [r]\n", argv[0]);
		exit(1);
	}

	size = get_num(argv[1]);
	stride = get_num(argv[2]);
	count = get_num(argv[3]);

	if (argc == 5 && argv[4][0] == 'r')
	{
		random_list = 1;
	}

	if (!size || !stride || !count)
	{
		die("invalid argument");
	}

	if ((errno = posix_memalign((void **)&curpos, PAGE_SIZE, sizeof(*curpos))))
	{
		perror("posix_memalign");
		exit(1);
	}

	map = create_map(size, stride);

	act.sa_handler = alarm_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGALRM, &act, NULL);

	minusec = ULONG_MAX;
	maxusec = 0;
	totalcount = 0;

	if ((usec = warmup(map)))
	{
		printf("warmup took %lu usec\n", usec);
	}

	gettimeofday(&start, NULL);

	for (i = 0; !stop && i < count; i++)
	{
		curpos = map + (unsigned long)random() % (size / stride);
		alarm(1);
		j = FREQ;
		do
		{
			*curpos = *(volatile unsigned int *)(curpos + *curpos);
			j--;
		} while (j);
		usec = alarm(0);
		if (usec >= 1000)
		{
			totalcount++;
			if (usec < minusec)
				minusec = usec;
			if (usec > maxusec)
				maxusec = usec;
		}
	}

	gettimeofday(&end, NULL);
	avgusec = usec_diff(&start, &end) / count;

	printf("min %lu max %lu avg %lu total %lu\n",
		   minusec, maxusec, avgusec, totalcount);

	return 0;
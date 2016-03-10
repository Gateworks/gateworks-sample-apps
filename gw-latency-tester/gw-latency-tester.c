/**
 * Filename: gw-latency-tester.c
 * Description: Application to measure latency synchronously
 * Author: Pushpal Sidhu <psidhu@gateworks.com>
 * Created: Fri Aug 21 13:21:28 2015 (-0700)
 * Last-Updated: Fri Sep  4 17:36:22 2015 (-0700)
 *           By: Pushpal Sidhu
 *     Update #: 144
 * Compatibility: Gateworks imx6 based product
 *
 * Compile: ${CC} gw-latency-tester.c -o gw-latency-tester
 */

/**
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gw-latency-tester.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Code: */
#include <stdio.h>		/* standard io */
#include <stdlib.h>		/* system() */
#include <time.h>		/* struct timesec */
#include <string.h>		/* strlen() */
#include <unistd.h>		/* read(), write() */
#include <sys/types.h>		/* open(), close() */
#include <sys/stat.h>		/* open(), close() */
#include <fcntl.h>		/* open(), close() */
#include <signal.h>		/* SIGINT */

#define NUM_DIO 4 /* Number of DIO's req'd */
enum {pwr=0, emit=1, led=2, recv=3};
struct dio_grp {
	int dio;
	int value_fd;
	int direction_fd;
};

struct dio_grp g_dio_grp[NUM_DIO];

static double ts_to_double(struct timespec ts)
{
	char buf[1024];

	snprintf(buf, sizeof(buf), "%ld.%.9ld", ts.tv_sec, ts.tv_nsec);
	return atof(buf);
}

static double diff_ts(struct timespec start, struct timespec end)
{
	return (ts_to_double(end) - ts_to_double(start));
}

static void  change_dir_dio(struct dio_grp g, char *dir)
{
	lseek(g.direction_fd, 0, SEEK_SET);
	if (write(g.direction_fd, dir, strlen(dir)) < 0) {
		perror("write direction");
		exit(1);
	}
}

static void change_val_dio(struct dio_grp g, char *val)
{
	lseek(g.value_fd, 0, SEEK_SET);
	if (write(g.value_fd, val, strlen(val)) < 0) {
		perror("write value");
		exit(1);
	}
}

static char get_val_dio(struct dio_grp g)
{
	char ret = '0';

	lseek(g.value_fd, 0, SEEK_SET);
	if (read(g.value_fd, &ret, sizeof(ret)) < 0)
		perror("read value");

	return ret;
}

static void open_dio(int dio, struct dio_grp g)
{
	char buf[1024];

	snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/value", g.dio);
	g_dio_grp[dio].value_fd = open(buf, O_RDWR | O_SYNC);
	if (g_dio_grp[dio].value_fd < 0) {
		perror("open value");
		exit(1);
	}

	snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/direction", g.dio);
	g_dio_grp[dio].direction_fd = open(buf, O_RDWR | O_SYNC);
	if (g_dio_grp[dio].direction_fd < 0) {
		perror("open direction");
		exit(1);
	}
}

static void release_all_dio(int sig)
{
	int i = 0;
	char buf[1024];

	for (; i < NUM_DIO; i++) {
		/* Configure to output and drive low */
		change_dir_dio(g_dio_grp[i], "out");
		change_val_dio(g_dio_grp[i], "0");

		close(g_dio_grp[i].value_fd);
		close(g_dio_grp[i].direction_fd);

		snprintf(buf, sizeof(buf),
			 "echo %d > /sys/class/gpio/unexport", g_dio_grp[i].dio);
		system(buf);
	}

	if (sig != 0)
		exit(sig);
}

/**
 * Configure DIO's for use
 */
static void setup_dio()
{
	int i;
	char buf[1024];

	/* Attempt to export dio's, don't check ret */
	for (i = 0; i < NUM_DIO; i++) {
		snprintf(buf, sizeof(buf),
			 "echo %d > /sys/class/gpio/export", g_dio_grp[i].dio);
		system(buf);
	}

	/* Attempt to open them all, drive low */
	for (i = 0; i < NUM_DIO; i++) {
		open_dio(i, g_dio_grp[i]);
		change_dir_dio(g_dio_grp[i], "out");
		change_val_dio(g_dio_grp[i], "0");
	}

	/* Set recv to input */
	change_dir_dio(g_dio_grp[recv], "in");

	/* Configure handler to release DIO's on certain signals */
	signal(SIGINT, release_all_dio);
	signal(SIGQUIT, release_all_dio);
	signal(SIGTERM, release_all_dio);
}

static void do_latency_test(unsigned int loop_count, unsigned int udelay,
			    double alpha)
{
	struct timespec s_ts, e_ts, res;
	unsigned int count = loop_count;
	double diff;
	double min = 9999, max = 0;
	double ewma = 0;

	setup_dio();

	clock_getres(CLOCK_MONOTONIC_RAW, &res);
	printf("System Clock Resolution: %.9fs\n", ts_to_double(res));

	puts("=== Starting Test ===");
	change_val_dio(g_dio_grp[led], "1"); /* Status LED */

	/* Make sure recv is on */
	change_val_dio(g_dio_grp[pwr], "0");
	/* Sleep 1s to get recv in known state */
	sleep(1);
	while (count--) {
		/* Let system normalize */
		while (get_val_dio(g_dio_grp[recv]) == '0')
			/* Spin */;

		/* Start Timer */
		clock_gettime(CLOCK_MONOTONIC_RAW, &s_ts);

		/* Drive emitter high */
		change_val_dio(g_dio_grp[emit], "1");

		/* Hardpoll the recv; a '0' means light is present on sensor */
		while (get_val_dio(g_dio_grp[recv]) != '0')
			/* Spin */;

		/* End Timer */
		clock_gettime(CLOCK_MONOTONIC_RAW, &e_ts);

		/* Drive emitter Low */
		change_val_dio(g_dio_grp[emit], "0");

		diff = diff_ts(s_ts, e_ts);
		printf("Running time difference: %.9fs\n",  diff);

	        ewma = (alpha * diff) + (1.0 - alpha) * ewma;
		if (min > diff)
			min = diff;
		if (max < diff)
			max = diff;

		usleep(udelay);
	}

	puts("=== Summary ===");
	printf("Ran %d time%s\n", loop_count, (loop_count==1) ? "" : "s");
	printf("Max Latency: %.9fs\n", max);
	printf("Min Latency: %.9fs\n", min);
	printf("Max Jitter : %.9fs\n", max - min);
	printf("Exponential Moving Average (alpha=%.1f): %.9fs\n", alpha, ewma);

	change_val_dio(g_dio_grp[led], "0"); /* Status LED */

	release_all_dio(0);
}

int main(int argc, char *argv[])
{
	unsigned int loop_count = 1, udelay = 500000;
	double alpha = .1;
	const char *usage =
		"gw-latency-tester <pwr_dio emit_dio led_dio recv_dio>"
		" [<count>] [<udelay>] [<alpha>]\n"
		"\n"
		"   DIO:  0  1  2  3\n"
		"         ----------\n"
		"GW54xx:  9 19 41 42\n"
		"GW53xx: 16 19 17 20\n"
		"GW52xx: 16 19 17 20\n"
		"GW51xx: 16 19 17 18\n"
		"GW552x: 16 19 17 20\n"
		"GW551x (with GW16111 on J12): 224 225 226 227\n";

	/* Parse cmdline */
	if (argc < 5) {
		puts(usage);
		exit(1);
	}

	g_dio_grp[pwr].dio  = atoi(argv[1]);
	g_dio_grp[emit].dio = atoi(argv[2]);
	g_dio_grp[led].dio  = atoi(argv[3]);
	g_dio_grp[recv].dio = atoi(argv[4]);

	/* Optional Arguments */
	if (argc > 5)
		loop_count = atoi(argv[5]);
	if (argc > 6)
		udelay = atoi(argv[6]);
	if (argc > 7)
		alpha = atof(argv[7]);

	do_latency_test(loop_count, udelay, alpha);

	return 0;
}

/* gw-latency-tester.c ends here */

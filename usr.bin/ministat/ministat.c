/*-
 * SPDX-License-Identifier: Beerware
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/capsicum.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/ttycom.h>

#include <capsicum_helpers.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NSTUDENT 100
#define NCONF 6
static double const studentpct[] = { 80, 90, 95, 98, 99, 99.5 };
static double student[NSTUDENT + 1][NCONF] = {
/* inf */	{	1.282,	1.645,	1.960,	2.326,	2.576,	3.090  },
/* 1. */	{	3.078,	6.314,	12.706,	31.821,	63.657,	318.313  },
/* 2. */	{	1.886,	2.920,	4.303,	6.965,	9.925,	22.327  },
/* 3. */	{	1.638,	2.353,	3.182,	4.541,	5.841,	10.215  },
/* 4. */	{	1.533,	2.132,	2.776,	3.747,	4.604,	7.173  },
/* 5. */	{	1.476,	2.015,	2.571,	3.365,	4.032,	5.893  },
/* 6. */	{	1.440,	1.943,	2.447,	3.143,	3.707,	5.208  },
/* 7. */	{	1.415,	1.895,	2.365,	2.998,	3.499,	4.782  },
/* 8. */	{	1.397,	1.860,	2.306,	2.896,	3.355,	4.499  },
/* 9. */	{	1.383,	1.833,	2.262,	2.821,	3.250,	4.296  },
/* 10. */	{	1.372,	1.812,	2.228,	2.764,	3.169,	4.143  },
/* 11. */	{	1.363,	1.796,	2.201,	2.718,	3.106,	4.024  },
/* 12. */	{	1.356,	1.782,	2.179,	2.681,	3.055,	3.929  },
/* 13. */	{	1.350,	1.771,	2.160,	2.650,	3.012,	3.852  },
/* 14. */	{	1.345,	1.761,	2.145,	2.624,	2.977,	3.787  },
/* 15. */	{	1.341,	1.753,	2.131,	2.602,	2.947,	3.733  },
/* 16. */	{	1.337,	1.746,	2.120,	2.583,	2.921,	3.686  },
/* 17. */	{	1.333,	1.740,	2.110,	2.567,	2.898,	3.646  },
/* 18. */	{	1.330,	1.734,	2.101,	2.552,	2.878,	3.610  },
/* 19. */	{	1.328,	1.729,	2.093,	2.539,	2.861,	3.579  },
/* 20. */	{	1.325,	1.725,	2.086,	2.528,	2.845,	3.552  },
/* 21. */	{	1.323,	1.721,	2.080,	2.518,	2.831,	3.527  },
/* 22. */	{	1.321,	1.717,	2.074,	2.508,	2.819,	3.505  },
/* 23. */	{	1.319,	1.714,	2.069,	2.500,	2.807,	3.485  },
/* 24. */	{	1.318,	1.711,	2.064,	2.492,	2.797,	3.467  },
/* 25. */	{	1.316,	1.708,	2.060,	2.485,	2.787,	3.450  },
/* 26. */	{	1.315,	1.706,	2.056,	2.479,	2.779,	3.435  },
/* 27. */	{	1.314,	1.703,	2.052,	2.473,	2.771,	3.421  },
/* 28. */	{	1.313,	1.701,	2.048,	2.467,	2.763,	3.408  },
/* 29. */	{	1.311,	1.699,	2.045,	2.462,	2.756,	3.396  },
/* 30. */	{	1.310,	1.697,	2.042,	2.457,	2.750,	3.385  },
/* 31. */	{	1.309,	1.696,	2.040,	2.453,	2.744,	3.375  },
/* 32. */	{	1.309,	1.694,	2.037,	2.449,	2.738,	3.365  },
/* 33. */	{	1.308,	1.692,	2.035,	2.445,	2.733,	3.356  },
/* 34. */	{	1.307,	1.691,	2.032,	2.441,	2.728,	3.348  },
/* 35. */	{	1.306,	1.690,	2.030,	2.438,	2.724,	3.340  },
/* 36. */	{	1.306,	1.688,	2.028,	2.434,	2.719,	3.333  },
/* 37. */	{	1.305,	1.687,	2.026,	2.431,	2.715,	3.326  },
/* 38. */	{	1.304,	1.686,	2.024,	2.429,	2.712,	3.319  },
/* 39. */	{	1.304,	1.685,	2.023,	2.426,	2.708,	3.313  },
/* 40. */	{	1.303,	1.684,	2.021,	2.423,	2.704,	3.307  },
/* 41. */	{	1.303,	1.683,	2.020,	2.421,	2.701,	3.301  },
/* 42. */	{	1.302,	1.682,	2.018,	2.418,	2.698,	3.296  },
/* 43. */	{	1.302,	1.681,	2.017,	2.416,	2.695,	3.291  },
/* 44. */	{	1.301,	1.680,	2.015,	2.414,	2.692,	3.286  },
/* 45. */	{	1.301,	1.679,	2.014,	2.412,	2.690,	3.281  },
/* 46. */	{	1.300,	1.679,	2.013,	2.410,	2.687,	3.277  },
/* 47. */	{	1.300,	1.678,	2.012,	2.408,	2.685,	3.273  },
/* 48. */	{	1.299,	1.677,	2.011,	2.407,	2.682,	3.269  },
/* 49. */	{	1.299,	1.677,	2.010,	2.405,	2.680,	3.265  },
/* 50. */	{	1.299,	1.676,	2.009,	2.403,	2.678,	3.261  },
/* 51. */	{	1.298,	1.675,	2.008,	2.402,	2.676,	3.258  },
/* 52. */	{	1.298,	1.675,	2.007,	2.400,	2.674,	3.255  },
/* 53. */	{	1.298,	1.674,	2.006,	2.399,	2.672,	3.251  },
/* 54. */	{	1.297,	1.674,	2.005,	2.397,	2.670,	3.248  },
/* 55. */	{	1.297,	1.673,	2.004,	2.396,	2.668,	3.245  },
/* 56. */	{	1.297,	1.673,	2.003,	2.395,	2.667,	3.242  },
/* 57. */	{	1.297,	1.672,	2.002,	2.394,	2.665,	3.239  },
/* 58. */	{	1.296,	1.672,	2.002,	2.392,	2.663,	3.237  },
/* 59. */	{	1.296,	1.671,	2.001,	2.391,	2.662,	3.234  },
/* 60. */	{	1.296,	1.671,	2.000,	2.390,	2.660,	3.232  },
/* 61. */	{	1.296,	1.670,	2.000,	2.389,	2.659,	3.229  },
/* 62. */	{	1.295,	1.670,	1.999,	2.388,	2.657,	3.227  },
/* 63. */	{	1.295,	1.669,	1.998,	2.387,	2.656,	3.225  },
/* 64. */	{	1.295,	1.669,	1.998,	2.386,	2.655,	3.223  },
/* 65. */	{	1.295,	1.669,	1.997,	2.385,	2.654,	3.220  },
/* 66. */	{	1.295,	1.668,	1.997,	2.384,	2.652,	3.218  },
/* 67. */	{	1.294,	1.668,	1.996,	2.383,	2.651,	3.216  },
/* 68. */	{	1.294,	1.668,	1.995,	2.382,	2.650,	3.214  },
/* 69. */	{	1.294,	1.667,	1.995,	2.382,	2.649,	3.213  },
/* 70. */	{	1.294,	1.667,	1.994,	2.381,	2.648,	3.211  },
/* 71. */	{	1.294,	1.667,	1.994,	2.380,	2.647,	3.209  },
/* 72. */	{	1.293,	1.666,	1.993,	2.379,	2.646,	3.207  },
/* 73. */	{	1.293,	1.666,	1.993,	2.379,	2.645,	3.206  },
/* 74. */	{	1.293,	1.666,	1.993,	2.378,	2.644,	3.204  },
/* 75. */	{	1.293,	1.665,	1.992,	2.377,	2.643,	3.202  },
/* 76. */	{	1.293,	1.665,	1.992,	2.376,	2.642,	3.201  },
/* 77. */	{	1.293,	1.665,	1.991,	2.376,	2.641,	3.199  },
/* 78. */	{	1.292,	1.665,	1.991,	2.375,	2.640,	3.198  },
/* 79. */	{	1.292,	1.664,	1.990,	2.374,	2.640,	3.197  },
/* 80. */	{	1.292,	1.664,	1.990,	2.374,	2.639,	3.195  },
/* 81. */	{	1.292,	1.664,	1.990,	2.373,	2.638,	3.194  },
/* 82. */	{	1.292,	1.664,	1.989,	2.373,	2.637,	3.193  },
/* 83. */	{	1.292,	1.663,	1.989,	2.372,	2.636,	3.191  },
/* 84. */	{	1.292,	1.663,	1.989,	2.372,	2.636,	3.190  },
/* 85. */	{	1.292,	1.663,	1.988,	2.371,	2.635,	3.189  },
/* 86. */	{	1.291,	1.663,	1.988,	2.370,	2.634,	3.188  },
/* 87. */	{	1.291,	1.663,	1.988,	2.370,	2.634,	3.187  },
/* 88. */	{	1.291,	1.662,	1.987,	2.369,	2.633,	3.185  },
/* 89. */	{	1.291,	1.662,	1.987,	2.369,	2.632,	3.184  },
/* 90. */	{	1.291,	1.662,	1.987,	2.368,	2.632,	3.183  },
/* 91. */	{	1.291,	1.662,	1.986,	2.368,	2.631,	3.182  },
/* 92. */	{	1.291,	1.662,	1.986,	2.368,	2.630,	3.181  },
/* 93. */	{	1.291,	1.661,	1.986,	2.367,	2.630,	3.180  },
/* 94. */	{	1.291,	1.661,	1.986,	2.367,	2.629,	3.179  },
/* 95. */	{	1.291,	1.661,	1.985,	2.366,	2.629,	3.178  },
/* 96. */	{	1.290,	1.661,	1.985,	2.366,	2.628,	3.177  },
/* 97. */	{	1.290,	1.661,	1.985,	2.365,	2.627,	3.176  },
/* 98. */	{	1.290,	1.661,	1.984,	2.365,	2.627,	3.175  },
/* 99. */	{	1.290,	1.660,	1.984,	2.365,	2.626,	3.175  },
/* 100. */	{	1.290,	1.660,	1.984,	2.364,	2.626,	3.174  }
};

#define	MAX_DS	8
static char symbol[MAX_DS] = { ' ', 'x', '+', '*', '%', '#', '@', 'O' };

struct dataset {
	char *name;
	double	*points;
	unsigned lpoints;
	double sy, syy;
	unsigned n;
};

static struct dataset *
NewSet(void)
{
	struct dataset *ds;

	ds = calloc(1, sizeof *ds);
	ds->lpoints = 100000;
	ds->points = calloc(sizeof *ds->points, ds->lpoints);
	return(ds);
}

static void
AddPoint(struct dataset *ds, double a)
{
	double *dp;

	if (ds->n >= ds->lpoints) {
		dp = ds->points;
		ds->lpoints *= 4;
		ds->points = calloc(sizeof *ds->points, ds->lpoints);
		memcpy(ds->points, dp, sizeof *dp * ds->n);
		free(dp);
	}
	ds->points[ds->n++] = a;
	ds->sy += a;
	ds->syy += a * a;
}

static double
Min(struct dataset *ds)
{

	return (ds->points[0]);
}

static double
Max(struct dataset *ds)
{

	return (ds->points[ds->n -1]);
}

static double
Avg(struct dataset *ds)
{

	return(ds->sy / ds->n);
}

static double
Median(struct dataset *ds)
{
	if ((ds->n % 2) == 0)
		return ((ds->points[ds->n / 2] + (ds->points[(ds->n / 2) - 1])) / 2);
    	else
		return (ds->points[ds->n / 2]);
}

static double
Var(struct dataset *ds)
{

	/*
	 * Due to limited precision it is possible that sy^2/n > syy,
	 * but variance cannot actually be negative.
	 */
	if (ds->syy <= ds->sy * ds->sy / ds->n)
		return (0);
	return (ds->syy - ds->sy * ds->sy / ds->n) / (ds->n - 1.0);
}

static double
Stddev(struct dataset *ds)
{

	return sqrt(Var(ds));
}

static void
VitalsHead(void)
{

	printf("    N           Min           Max        Median           Avg        Stddev\n");
}

static void
Vitals(struct dataset *ds, int flag)
{

	printf("%c %3d %13.8g %13.8g %13.8g %13.8g %13.8g", symbol[flag],
	    ds->n, Min(ds), Max(ds), Median(ds), Avg(ds), Stddev(ds));
	printf("\n");
}

static void
Relative(struct dataset *ds, struct dataset *rs, int confidx)
{
	double spool, s, d, e, t;
	double re;
	int i;

	i = ds->n + rs->n - 2;
	if (i > NSTUDENT)
		t = student[0][confidx];
	else
		t = student[i][confidx];
	spool = (ds->n - 1) * Var(ds) + (rs->n - 1) * Var(rs);
	spool /= ds->n + rs->n - 2;
	spool = sqrt(spool);
	s = spool * sqrt(1.0 / ds->n + 1.0 / rs->n);
	d = Avg(ds) - Avg(rs);
	e = t * s;

	re = (ds->n - 1) * Var(ds) + (rs->n - 1) * Var(rs) *
	    (Avg(ds) * Avg(ds)) / (Avg(rs) * Avg(rs));
	re *= (ds->n + rs->n) / (ds->n * rs->n * (ds->n + rs->n - 2.0));
	re = t * sqrt(re);

	if (fabs(d) > e) {
	
		printf("Difference at %.1f%% confidence\n", studentpct[confidx]);
		printf("	%g +/- %g\n", d, e);
		printf("	%g%% +/- %g%%\n", d * 100 / Avg(rs), re * 100 / Avg(rs));
		printf("	(Student's t, pooled s = %g)\n", spool);
	} else {
		printf("No difference proven at %.1f%% confidence\n",
		    studentpct[confidx]);
	}
}

struct plot {
	double		min;
	double		max;
	double		span;
	int		width;

	double		x0, dx;
	int		height;
	char		*data;
	char		**bar;
	int		separate_bars;
	int		num_datasets;
};

static struct plot plot;

static void
SetupPlot(int width, int separate, int num_datasets)
{
	struct plot *pl;

	pl = &plot;
	pl->width = width;
	pl->height = 0;
	pl->data = NULL;
	pl->bar = NULL;
	pl->separate_bars = separate;
	pl->num_datasets = num_datasets;
	pl->min = 999e99;
	pl->max = -999e99;
}

static void
AdjPlot(double a)
{
	struct plot *pl;

	pl = &plot;
	if (a < pl->min)
		pl->min = a;
	if (a > pl->max)
		pl->max = a;
	pl->span = pl->max - pl->min;
	pl->dx = pl->span / (pl->width - 1.0);
	pl->x0 = pl->min - .5 * pl->dx;
}

static void
DimPlot(struct dataset *ds)
{
	AdjPlot(Min(ds));
	AdjPlot(Max(ds));
	AdjPlot(Avg(ds) - Stddev(ds));
	AdjPlot(Avg(ds) + Stddev(ds));
}

static void
PlotSet(struct dataset *ds, int val)
{
	struct plot *pl;
	int i, j, m, x;
	unsigned n;
	int bar;

	pl = &plot;
	if (pl->span == 0)
		return;

	if (pl->separate_bars)
		bar = val-1;
	else
		bar = 0;

	if (pl->bar == NULL)
		pl->bar = calloc(sizeof(char *), pl->num_datasets);
	if (pl->bar[bar] == NULL) {
		pl->bar[bar] = malloc(pl->width);
		memset(pl->bar[bar], 0, pl->width);
	}
	
	m = 1;
	i = -1;
	j = 0;
	for (n = 0; n < ds->n; n++) {
		x = (ds->points[n] - pl->x0) / pl->dx;
		if (x == i) {
			j++;
			if (j > m)
				m = j;
		} else {
			j = 1;
			i = x;
		}
	}
	m += 1;
	if (m > pl->height) {
		pl->data = realloc(pl->data, pl->width * m);
		memset(pl->data + pl->height * pl->width, 0,
		    (m - pl->height) * pl->width);
	}
	pl->height = m;
	i = -1;
	for (n = 0; n < ds->n; n++) {
		x = (ds->points[n] - pl->x0) / pl->dx;
		if (x == i) {
			j++;
		} else {
			j = 1;
			i = x;
		}
		pl->data[j * pl->width + x] |= val;
	}
	if (!isnan(Stddev(ds))) {
		x = ((Avg(ds) - Stddev(ds)) - pl->x0) / pl->dx;
		m = ((Avg(ds) + Stddev(ds)) - pl->x0) / pl->dx;
		pl->bar[bar][m] = '|';
		pl->bar[bar][x] = '|';
		for (i = x + 1; i < m; i++)
			if (pl->bar[bar][i] == 0)
				pl->bar[bar][i] = '_';
	}
	x = (Median(ds) - pl->x0) / pl->dx;
	pl->bar[bar][x] = 'M';
	x = (Avg(ds) - pl->x0) / pl->dx;
	pl->bar[bar][x] = 'A';
}

static void
DumpPlot(void)
{
	struct plot *pl;
	int i, j, k;

	pl = &plot;
	if (pl->span == 0) {
		printf("[no plot, span is zero width]\n");
		return;
	}

	putchar('+');
	for (i = 0; i < pl->width; i++)
		putchar('-');
	putchar('+');
	putchar('\n');
	for (i = 1; i < pl->height; i++) {
		putchar('|');
		for (j = 0; j < pl->width; j++) {
			k = pl->data[(pl->height - i) * pl->width + j];
			if (k >= 0 && k < MAX_DS)
				putchar(symbol[k]);
			else
				printf("[%02x]", k);
		}
		putchar('|');
		putchar('\n');
	}
	for (i = 0; i < pl->num_datasets; i++) {
		if (pl->bar[i] == NULL)
			continue;
		putchar('|');
		for (j = 0; j < pl->width; j++) {
			k = pl->bar[i][j];
			if (k == 0)
				k = ' ';
			putchar(k);
		}
		putchar('|');
		putchar('\n');
	}
	putchar('+');
	for (i = 0; i < pl->width; i++)
		putchar('-');
	putchar('+');
	putchar('\n');
}

static int
dbl_cmp(const void *a, const void *b)
{
	const double *aa = a;
	const double *bb = b;

	if (*aa < *bb)
		return (-1);
	else if (*aa > *bb)
		return (1);
	else
		return (0);
}

static struct dataset *
ReadSet(FILE *f, const char *n, int column, const char *delim)
{
	char buf[BUFSIZ], *p, *t;
	struct dataset *s;
	double d;
	int line;
	int i;

	s = NewSet();
	s->name = strdup(n);
	line = 0;
	while (fgets(buf, sizeof buf, f) != NULL) {
		line++;

		i = strlen(buf);
		while (i > 0 && isspace(buf[i - 1]))
			buf[--i] = '\0';
		for (i = 1, t = strtok(buf, delim);
		     t != NULL && *t != '#';
		     i++, t = strtok(NULL, delim)) {
			if (i == column)
				break;
		}
		if (t == NULL || *t == '#')
			continue;

		d = strtod(t, &p);
		if (p != NULL && *p != '\0')
			errx(2, "Invalid data on line %d in %s", line, n);
		if (*buf != '\0')
			AddPoint(s, d);
	}
	if (s->n < 3) {
		fprintf(stderr,
		    "Dataset %s must contain at least 3 data points\n", n);
		exit (2);
	}
	qsort(s->points, s->n, sizeof *s->points, dbl_cmp);
	return (s);
}

static void
usage(char const *whine)
{
	int i;

	fprintf(stderr, "%s\n", whine);
	fprintf(stderr,
	    "Usage: ministat [-C column] [-c confidence] [-d delimiter(s)] [-Ans] [-w width] [file [file ...]]\n");
	fprintf(stderr, "\tconfidence = {");
	for (i = 0; i < NCONF; i++) {
		fprintf(stderr, "%s%g%%",
		    i ? ", " : "",
		    studentpct[i]);
	}
	fprintf(stderr, "}\n");
	fprintf(stderr, "\t-A : print statistics only. suppress the graph.\n");
	fprintf(stderr, "\t-C : column number to extract (starts and defaults to 1)\n");
	fprintf(stderr, "\t-d : delimiter(s) string, default to \" \\t\"\n");
	fprintf(stderr, "\t-n : print summary statistics only, no graph/test\n");
	fprintf(stderr, "\t-s : print avg/median/stddev bars on separate lines\n");
	fprintf(stderr, "\t-w : width of graph/test output (default 74 or terminal width)\n");
	exit (2);
}

int
main(int argc, char **argv)
{
	const char *setfilenames[MAX_DS - 1];
	struct dataset *ds[MAX_DS - 1];
	FILE *setfiles[MAX_DS - 1];
	int nds;
	double a;
	const char *delim = " \t";
	char *p;
	int c, i, ci;
	int column = 1;
	int flag_s = 0;
	int flag_n = 0;
	int termwidth = 74;
	int suppress_plot = 0;

	if (isatty(STDOUT_FILENO)) {
		struct winsize wsz;

		if ((p = getenv("COLUMNS")) != NULL && *p != '\0')
			termwidth = atoi(p);
		else if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &wsz) != -1 &&
			 wsz.ws_col > 0)
			termwidth = wsz.ws_col - 2;
	}

	ci = -1;
	while ((c = getopt(argc, argv, "AC:c:d:snw:")) != -1)
		switch (c) {
		case 'A':
			suppress_plot = 1;
			break;
		case 'C':
			column = strtol(optarg, &p, 10);
			if (p != NULL && *p != '\0')
				usage("Invalid column number.");
			if (column <= 0)
				usage("Column number should be positive.");
			break;
		case 'c':
			a = strtod(optarg, &p);
			if (p != NULL && *p != '\0')
				usage("Not a floating point number");
			for (i = 0; i < NCONF; i++)
				if (a == studentpct[i])
					ci = i;
			if (ci == -1)
				usage("No support for confidence level");
			break;
		case 'd':
			if (*optarg == '\0')
				usage("Can't use empty delimiter string");
			delim = optarg;
			break;
		case 'n':
			flag_n = 1;
			break;
		case 's':
			flag_s = 1;
			break;
		case 'w':
			termwidth = strtol(optarg, &p, 10);
			if (p != NULL && *p != '\0')
				usage("Invalid width, not a number.");
			if (termwidth < 0)
				usage("Unable to move beyond left margin.");
			break;
		default:
			usage("Unknown option");
			break;
		}
	if (ci == -1)
		ci = 2;
	argc -= optind;
	argv += optind;

	if (argc == 0) {
		setfilenames[0] = "<stdin>";
		setfiles[0] = stdin;
		nds = 1;
	} else {
		if (argc > (MAX_DS - 1))
			usage("Too many datasets.");
		nds = argc;
		for (i = 0; i < nds; i++) {
			setfilenames[i] = argv[i];
			setfiles[i] = fopen(argv[i], "r");
			if (setfiles[i] == NULL)
				err(2, "Cannot open %s", argv[i]);
		}
	}

	if (caph_limit_stdio() < 0)
		err(2, "capsicum");

	for (i = 0; i < nds; i++)
		if (caph_limit_stream(fileno(setfiles[i]), CAPH_READ) < 0)
			err(2, "unable to limit rights for %s",
			    setfilenames[i]);

	/* Enter Capsicum sandbox. */
	if (caph_enter() < 0)
		err(2, "unable to enter capability mode");

	for (i = 0; i < nds; i++) {
		ds[i] = ReadSet(setfiles[i], setfilenames[i], column, delim);
		fclose(setfiles[i]);
	}

	for (i = 0; i < nds; i++) 
		printf("%c %s\n", symbol[i+1], ds[i]->name);

	if (!flag_n && !suppress_plot) {
		SetupPlot(termwidth, flag_s, nds);
		for (i = 0; i < nds; i++)
			DimPlot(ds[i]);
		for (i = 0; i < nds; i++)
			PlotSet(ds[i], i + 1);
		DumpPlot();
	}
	VitalsHead();
	Vitals(ds[0], 1);
	for (i = 1; i < nds; i++) {
		Vitals(ds[i], i + 1);
		if (!flag_n)
			Relative(ds[i], ds[0], ci);
	}
	exit(0);
}

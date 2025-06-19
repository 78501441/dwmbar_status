#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sys_stat_helpers.h"

int
loads(char s_line[HELPER_LINE_LEN], struct sysload *cputimes)
{
	int ret;
	size_t cr;
	const char *spc;
	FILE *f = fopen("/proc/stat", "r");

	if (!f)
		return -1;

	cr = fread(s_line, 1, 255, f);
	s_line[cr] = '\0';
	spc = strchr(s_line, ' ');
	if (!spc) {
		fclose(f);
		return -1;
	}
	while (isspace(*spc))
		spc += 1;
	ret = sscanf(spc, "%i %i %i %i", &cputimes->user, &cputimes->nice,
		&cputimes->sys, &cputimes->idle);
	fclose(f);
#ifdef _DEBUG
	printf("user: %i, idle: %i\n", cputimes->user, cputimes->idle);
#endif
	return ret;
}

double
calc_load_perc(struct sysload *prev_measure, struct sysload *current_measure)
{	
	const int du = current_measure->user - prev_measure->user;
	const int dn = current_measure->nice - prev_measure->nice;
	const int ds = current_measure->sys - prev_measure->sys;
	const int di = current_measure->idle - prev_measure->idle;
	const int iter_total = du + dn + ds + di;
	double iter_busy;
	if (iter_total == 0)
		return 0;
	iter_busy = ((double)(du + dn + ds)) / (double)iter_total;
#ifdef _DEBUG
	printf("iter_busy=%.2f\n", iter_busy);
#endif
	return iter_busy * 100.0f;
}

double
mem_free_percent(void)
{
	char my_buf[128];
	size_t cr;
	double total_mem;
	double free_mem;
	const char *iter;
	
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f)
		return -1;
	cr = fread(my_buf, 1, sizeof(my_buf) - 1, f);
	fclose(f);
	my_buf[cr] = '\0';
	iter = strchr(my_buf, ':');
	if (!iter)
		return -1;
	iter += 1;
	while (*iter == ' ')
		iter += 1;
	if (sscanf(iter, "%lf ", &total_mem) != 1)
		return -1;
	iter = strchr(iter, '\n');
	if (!iter)
		return -1;
	if ((iter = strchr(iter, ':')) == NULL)
		return -1;
	iter += 1;
	while (*iter == ' ')
		iter += 1;
	if (sscanf(iter, "%lf ", &free_mem) != 1)
		return 1;
	return (free_mem / total_mem) * 100.0f;
}

struct canon_time
timenow(void)
{
	time_t t;
	struct tm tspec;
	struct canon_time ret;

	t = time(NULL);
	localtime_r(&t, &tspec);
	ret.day = tspec.tm_mday;
	ret.month = tspec.tm_mon + 1;
	ret.hour = tspec.tm_hour;
	ret.minute = tspec.tm_min;
	return ret;
}

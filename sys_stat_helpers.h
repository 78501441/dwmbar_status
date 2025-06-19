#define HELPER_LINE_LEN (256)

struct sysload {
	int user;
	int nice;
	int sys;
	int idle;
};

struct canon_time {
	int day;
	int month;
	int hour;
	int minute;
};

#define ALLOCED_SZ_PTR char *

int
loads(char s_line[HELPER_LINE_LEN], struct sysload *cputimes);

double
calc_load_perc(struct sysload *prev_measure, struct sysload *current_measure);

double
mem_free_percent(void);

struct canon_time
timenow(void);

ALLOCED_SZ_PTR
report_ifaces(void);

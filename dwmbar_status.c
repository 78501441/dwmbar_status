#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/XKBlib.h>

#define STATIC_MAX_LAYOUTS_COUNT     (12)
#define POLL_INTERVAL                (630)
/* collect stats from procfs upon this signal arrive */
#define SIG_CHKPROC                  SIGUSR1

static volatile sig_atomic_t sigint_fired;
static volatile sig_atomic_t stats_alarmed;

struct xkb_initparams {
	int v_maj;
	int v_min;
	int xkb_event_type;
	int xkb_err;
	int xkb_result;
};

struct canon_time {
	int day;
	int month;
	int hour;
	int minute;
};

struct xkb_layout_state {
	char **names;
	int count;
	int active_index;
	char *priv_static_names[STATIC_MAX_LAYOUTS_COUNT];
};

struct sysload {
	int user;
	int nice;
	int sys;
	int idle;
};

static void
sighandler(int signo, siginfo_t *info, void *context)
{
	if (signo == SIGINT)
		sigint_fired = 1;
	else if (signo == SIG_CHKPROC)
		stats_alarmed = 1;
	(void)info;
	(void)context;
}

static char **
retrieve_kbd_info(Display *disp, struct xkb_layout_state *kbdinfo)
{
	int n_groups = 0;
	XkbStateRec kbd_state;

	XkbDescPtr kbd_desc = XkbGetKeyboard(disp,
		XkbAllComponentsMask, XkbUseCoreKbd);
	if (!kbd_desc)
		return NULL;

	if (XkbGetControls(disp, XkbAllComponentsMask, kbd_desc) != Success) {
		fputs("[-] failed to get kbd controls\n", stderr);
		return NULL;
	}
	n_groups = kbd_desc->ctrls->num_groups;
	kbdinfo->names = n_groups <= STATIC_MAX_LAYOUTS_COUNT ?
		kbdinfo->priv_static_names :
		calloc(n_groups, sizeof(char *));
	XGetAtomNames(disp, kbd_desc->names->groups, n_groups, kbdinfo->names);
	if (XkbGetState(disp, XkbUseCoreKbd, &kbd_state) == Success) {
		kbdinfo->active_index = kbd_state.group;
#ifdef _DEBUG
		printf("active kbd layout is %s\n", kbdinfo->names[kbdinfo->active_index]);
#endif
	}
	
	XkbFreeKeyboard(kbd_desc, XkbAllComponentsMask, True);
	kbdinfo->count = n_groups;
	return kbdinfo->names;
}

static int
x11poll_fd(int x11fd)
{
	struct pollfd x[1];
	int ret;
	int msec_remain = POLL_INTERVAL;
	int e = errno;
	struct timespec tv1, tv2;

	clock_gettime(CLOCK_REALTIME, &tv1);

	x[0].fd = x11fd;
	x[0].events = POLLIN;
	do {
		ret = poll(x, 1, msec_remain);
		if (ret == -1) {
			if (errno == EINTR) {
				clock_gettime(CLOCK_REALTIME, &tv2);
				msec_remain -=
				(tv2.tv_sec * 1000 + tv2.tv_nsec / 1000000)
				- (tv1.tv_sec * 1000 + tv1.tv_nsec / 1000000);
				if (msec_remain <= 0) {
					int rp = poll(x, 1, 0);
					if (rp == -1)
						perror("[warning]"
						" poll failure");
					return rp;
				}
				tv1 = tv2;
#ifdef _DEBUG
				fprintf(stderr, "poll: EINTR recieved, %i ms remains\n", msec_remain);
#endif
				continue;
			}
			perror("warning, poll failure");
			break;
		}
	} while (ret < 0);
	errno = e;
	return ret;
}

static int
xkb_open_default_display(struct xkb_initparams *initparams, Display **out_dpy)
{

	*out_dpy = XkbOpenDisplay(NULL, &initparams->xkb_event_type,
		&initparams->xkb_err, &initparams->v_maj, &initparams->v_min,
		&initparams->xkb_result);
	return initparams->xkb_result;
}

static struct canon_time
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

static int
setup_timers(timer_t *out_timer_id)
{
	timer_t timer_id;
	struct sigevent sigq;
	struct sigaction sa;
	struct itimerspec its;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = sighandler;
	if (sigaction(SIG_CHKPROC, &sa, NULL) == -1) {
		perror("[-] setup_timers(), sigaction");
		return -1;
	}
	sigq.sigev_notify = SIGEV_SIGNAL;
	sigq.sigev_signo = SIG_CHKPROC;
	sigq.sigev_value.sival_ptr = NULL;
	if (timer_create(CLOCK_MONOTONIC, &sigq, &timer_id) == -1) {
		signal(SIG_CHKPROC, SIG_DFL);
		perror("[-] setup_timers(), timer_create");
		return -1;
	}
	its.it_value.tv_sec = 2;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = 2;
	its.it_interval.tv_nsec = 0;
	if (timer_settime(timer_id, 0, &its, 0) == -1) {
		perror("[-] setup_timers(), timer_settime");
		signal(SIG_CHKPROC, SIG_DFL);
		timer_delete(timer_id);
		return -1;
	}
	*out_timer_id = timer_id;
	return 0;
}

static int
loads(char s_line[256], struct sysload *cputimes)
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

static double
calc_load_perc(struct sysload *prev_measure, struct sysload *current_measure)
{
	int du, dn, ds, di;
	int iter_total;
	double iter_busy;
	du = current_measure->user - prev_measure->user;
	dn = current_measure->nice - prev_measure->nice;
	ds = current_measure->sys - prev_measure->sys;
	di = current_measure->idle - prev_measure->idle;
	iter_total = du + dn + ds + di;
	if (iter_total == 0)
		return 0;
	iter_busy = ((double)(du + dn + ds)) / (double)iter_total;
#ifdef _DEBUG
	printf("iter_busy=%.2f\n", iter_busy);
#endif
	return iter_busy * 100.0f;
}

#ifdef _DEBUG
static int
xkbrules_layouts(Display *dpy, char out_langs[12][24], int *out_langcount)
{
	int fmt;
	unsigned long nitems, bytes_after;
	unsigned char *data;
	char *iter, *iter_end;
	Atom actual_type;
	int idx, i;
	Atom rules =
		XInternAtom(dpy, "_XKB_RULES_NAMES", True);
	if (rules == None)
		return -1;
	/* taken from libxkbfile */
	if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), rules, 0L, 1024,
		False, ((Atom)31), &actual_type, &fmt, &nitems, &bytes_after,
		&data) != Success)

		return -1;
	
	iter_end = ((char *)data) + nitems;
	for (iter = (char *)data; iter < iter_end; iter = (strchr(iter, '\0') + 1))
		printf("[debug] _XKB_RULES_NAMES => \"%s\"\n", iter);
	iter = strchr(strchr((const char *)data, '\0') + 1, '\0') + 1;
	for (idx = 0; *iter; idx += 1) {
		i = 0;
		while (*iter != ',' && *iter) {
			out_langs[idx][i] = *iter;
			i += 1;
			iter += 1;
		}
		out_langs[idx][i] = '\0';
		iter += 1;
	}
	*out_langcount = idx;
	XFree(data);
	return 0;
}
#endif

static double
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

int
main(void)
{
	int update_flag = 0;
	double cpu_load_percentage = 0.0f;

	timer_t timer_id;

	char *orig_rootwin_name;
	Display *dpy;

	struct sigaction sa;
	struct canon_time past, now;
	XEvent xev;
	struct sysload loadpast, loadnow;
	struct xkb_initparams initparams;
	struct xkb_layout_state kbd_layout_state;

	char s1[256];
	char err_buf[1536];

	initparams.v_maj = XkbMajorVersion;
	initparams.v_min = XkbMinorVersion;

	sigint_fired = 0;
	stats_alarmed = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_sigaction = sighandler;
	if (sigaction(SIGINT, &sa, NULL) == -1)
		perror("warning: unable to set SIGINT handler");

	xkb_open_default_display(&initparams, &dpy);
	if (dpy == NULL) {
		fprintf(stderr, "[-] XkbOpenDisplay failure: %i\n",
			initparams.xkb_result);
		return 3;
	}
	XGetErrorText(dpy, initparams.xkb_result, err_buf, sizeof(err_buf));
#ifdef _DEBUG
	printf("xkb reporting:\n"
		"base event code: %i\n"
		"base error code: %i\n"
		"major server version: %i\n"
		"minor server version: %i\n"
		"error hint: %s\n"
		"X11 server socket fd: %i\n",
		initparams.xkb_event_type, initparams.xkb_err, initparams.v_maj,
		initparams.v_min, err_buf, ConnectionNumber(dpy));
#endif
	if (!retrieve_kbd_info(dpy, &kbd_layout_state))
		goto _failed;
		
	XFlush(dpy);
	update_flag = 1;
	past = timenow();
	loads(s1, &loadpast);

	if (setup_timers(&timer_id) == -1)
		goto _failed;

	if (!XFetchName(dpy, RootWindow(dpy, DefaultScreen(dpy)),
		&orig_rootwin_name)) {

		fputs("[warning] failed to get current root window name\n",
			stderr);
		orig_rootwin_name = NULL;

	}

	/* track change in current group id */
	XkbSelectEventDetails(dpy, XkbUseCoreKbd,
		XkbStateNotify, XkbAllEventsMask, XkbGroupStateMask);

	for (;;) {
		if (sigint_fired) {
			fputs("SIGINT recieved, exiting...\n", stderr);
			XStoreName(dpy, RootWindow(dpy, DefaultScreen(dpy)),
				orig_rootwin_name ?
				orig_rootwin_name :
				"dwm-6.5");
			XFlush(dpy);
			if (orig_rootwin_name)
				XFree(orig_rootwin_name);
			break;
		}
		if (x11poll_fd(ConnectionNumber(dpy)) > 0) {
#ifdef _DEBUG
			fputs("[debug] before XNextEvent()\n", stderr);
#endif
			XNextEvent(dpy, &xev);
			if (xev.type == initparams.xkb_event_type) {
				XkbEvent *kbd_ev = (XkbEvent*)&xev;
				const int xkbev_subtype = kbd_ev->any.xkb_type;
				if (xkbev_subtype == XkbStateNotify) {
#ifdef _DEBUG
					char lngs[12][24];
					int r;
					printf("Group kbd now is: %s\n", kbd_layout_state.names[kbd_ev->state.group]);
					if (xkbrules_layouts(dpy, lngs, &r)
						!= -1)
						
						printf("[debug] rules: %s\n", lngs[kbd_ev->state.group]);
#endif
					kbd_layout_state.active_index =
						kbd_ev->state.group;
					update_flag = 1;

				}
			}
		} else if (stats_alarmed) {
			loads(s1, &loadnow);
			cpu_load_percentage =
				calc_load_perc(&loadpast, &loadnow);
			loadpast = loadnow;
			update_flag = 1;
			stats_alarmed = 0;
		} else {
			now = timenow();
			if (now.minute != past.minute ||
				now.hour != past.hour) {

				past = now;
				update_flag = 1;

			}
		}

		if (update_flag) {
			snprintf(err_buf, sizeof(err_buf),
				"%s cpu:%.2f%% memfree:%.02f%% "
				"%02i/%02i [%02i:%02i]",
				kbd_layout_state.names
				[kbd_layout_state.active_index],
				cpu_load_percentage, mem_free_percent(),
				now.day, now.month, now.hour, now.minute);
			XStoreName(dpy, RootWindow(dpy, DefaultScreen(dpy)),
				err_buf);
			XFlush(dpy);
			update_flag = 0;
		}
	} /* for */

	if (kbd_layout_state.names) {
		int i;
		for (i = 0; i < kbd_layout_state.count; i += 1)
			XFree(kbd_layout_state.names[i]);
		if (kbd_layout_state.names !=
			kbd_layout_state.priv_static_names)

			free(kbd_layout_state.names);
	}
	timer_delete(timer_id);
_failed:
	XCloseDisplay(dpy);
	return 0;
}

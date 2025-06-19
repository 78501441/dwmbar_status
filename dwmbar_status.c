#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <X11/XKBlib.h>

#include "sys_stat_helpers.h"
#include "xkb_helpers.h"

#define STATIC_MAX_LAYOUTS_COUNT	(12)
/* X11 server socket polling interval in milliseconds */
#define POLL_INTERVAL	(630)
/* collect stats from procfs upon this signal arrive */
#define SIG_CHKPROC	(SIGUSR1)

static volatile sig_atomic_t sigint_fired;
static volatile sig_atomic_t stats_alarmed;

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

static int
ms_delta(struct timespec *past, struct timespec *current)
{
	const int now = current->tv_sec * 1000 + current->tv_nsec / 1000000;
	const int prev = past->tv_sec * 1000 + past->tv_nsec / 1000000;
	return now - prev;
}

static int
x11poll_fd(int x11fd)
{
	struct pollfd x[1];
	int ret;
	int msec_remain = POLL_INTERVAL;
	int e = errno;
	struct timespec tv1, tv2;

	clock_gettime(CLOCK_MONOTONIC, &tv1);

	x[0].fd = x11fd;
	x[0].events = POLLIN;
	while (1) {
		ret = poll(x, 1, msec_remain);
		if (ret != -1) break;
		if (errno != EINTR) {
			perror("warning, poll failure");
			return -1;
		}
		clock_gettime(CLOCK_MONOTONIC, &tv2);
		msec_remain -= ms_delta(&tv1, &tv2);
		if (msec_remain <= 0) {
			int rp = poll(x, 1, 0);
			if (rp == -1)
				perror("[warning] poll failure");
			return rp;
		}
		tv1 = tv2;
	}
	errno = e;
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

int
main(void)
{
	int update_flag = 1;
	int retval = 3;
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

	xkb_open_default_display(&initparams, (void **)&dpy);
	if (dpy == NULL) {
		fprintf(stderr, "[-] XkbOpenDisplay failure: %i\n",
			initparams.xkb_result);
		return retval;
	}

	if (retrieve_kbd_info(dpy, &kbd_layout_state) == -1
		|| setup_timers(&timer_id) == -1)
		
		goto _failed;

	past = timenow();
	loads(s1, &loadpast);

	XFlush(dpy);
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
			XNextEvent(dpy, &xev);
			if (xev.type == initparams.xkb_event_type) {
				XkbEvent *kbd_ev = (XkbEvent*)&xev;
				const int xkbev_subtype = kbd_ev->any.xkb_type;
				if (xkbev_subtype == XkbStateNotify) {
					retrieve_kbd_info(dpy,
						&kbd_layout_state);
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
		if (!update_flag)
			continue;

		snprintf(err_buf, sizeof(err_buf),
			"%s cpu:%.2f%% memfree:%.02f%% "
			"%02i/%02i [%02i:%02i]",
			kbd_layout_state.kb_names
			[kbd_layout_state.active_index],
			cpu_load_percentage, mem_free_percent(),
			now.day, now.month, now.hour, now.minute);
		XStoreName(dpy, RootWindow(dpy, DefaultScreen(dpy)),
			err_buf);
		XFlush(dpy);
		update_flag = 0;

	} /* for */
	timer_delete(timer_id);
	retval = 0;
_failed:
	XCloseDisplay(dpy);
	return retval;
}

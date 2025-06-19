#include <stdio.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>

#include "xkb_helpers.h"

int
xkb_open_default_display(struct xkb_initparams *initparams, void **out_dpy)
{

	*out_dpy = XkbOpenDisplay(NULL, &initparams->xkb_event_type,
		&initparams->xkb_err, &initparams->v_maj, &initparams->v_min,
		&initparams->xkb_result);
	return initparams->xkb_result;
}

int
xkbrules_layouts(void *d, struct xkb_layout_state *st)
{
	Display *dpy = (Display *)d;
	int fmt;
	unsigned long nitems, bytes_after;
	unsigned char *data;
	char *iter;
	Atom actual_type;
	int idx, i;
	Atom rules = XInternAtom(dpy, SELF_NAMES_PROP_ATOM, True);
	if (rules == None)
		return -1;
	/* taken from libxkbfile */
	if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), rules, 0L,
		SELF_NAMES_PROP_MAXLEN, False, XA_STRING, &actual_type, &fmt,
		&nitems, &bytes_after, &data) != Success)

		return -1;

	iter = memchr(data, '\0', nitems);
	if (iter == NULL)
		return -1;
	if ((iter = memchr(iter + 1, '\0', nitems)) == NULL)
		return -1;
	iter += 1;

	for (idx = 0; *iter && idx < KB_DIMS; idx += 1) {
		i = 0;
		while (*iter != ',' && *iter && i < (KB_DIMS - 1)) {
			st->kb_names[idx][i] = *iter;
			i += 1;
			iter += 1;
		}
		st->kb_names[idx][i] = '\0';
		iter += 1;
	}
	st->count = idx;
	XFree(data);
	return 0;
}

int
retrieve_kbd_info(void *d, struct xkb_layout_state *kbdinfo)
{
	Display *dpy = (Display *)d;
	int res = -1;
	XkbDescPtr kbd_desc = XkbGetKeyboard(dpy,
		XkbAllComponentsMask, XkbUseCoreKbd);
	if (!kbd_desc)
		return res;

	if (XkbGetControls(dpy, XkbAllComponentsMask, kbd_desc) != Success)
		fputs("[-] failed to get kbd controls\n", stderr);
	else
		res = xkbrules_layouts(dpy, kbdinfo);
	XkbFreeKeyboard(kbd_desc, XkbAllComponentsMask, True);
	return res;
}

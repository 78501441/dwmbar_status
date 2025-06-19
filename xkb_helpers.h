#define KB_DIMS	(8)
#define SELF_NAMES_PROP_ATOM	("_XKB_RULES_NAMES")
#define SELF_NAMES_PROP_MAXLEN	(1024)

struct xkb_initparams {
	int v_maj;
	int v_min;
	int xkb_event_type;
	int xkb_err;
	int xkb_result;
};

struct xkb_layout_state {
	int count;
	int active_index;
	char kb_names[KB_DIMS][KB_DIMS];
};

int
xkb_open_default_display(struct xkb_initparams *initparams, void **out_dpy);

int
xkbrules_layouts(void *d, struct xkb_layout_state *st);

int
retrieve_kbd_info(void *d, struct xkb_layout_state *kbdinfo);

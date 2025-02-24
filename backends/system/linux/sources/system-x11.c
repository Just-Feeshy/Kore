#include <kore3/backend/x11.h>

#include <kore3/input/keyboard.h>
#include <kore3/input/mouse.h>
#include <kore3/input/pen.h>

#include <X11/Xlib.h>

#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Button6 6
#define Button7 7

struct kore_x11_procs xlib = {0};
struct x11_context x11_ctx = {0};

static size_t clipboardStringSize = 1024;
static char *clipboardString = NULL;

char buffer[1024];

int kore_x11_error_handler(Display *display, XErrorEvent *error_event) {
	xlib.XGetErrorText(display, error_event->error_code, buffer, 1024);
	kore_log(KORE_LOG_LEVEL_ERROR, "X Error: %s", buffer);
	kore_debug_break();
	return 0;
}

struct kore_x11_window *window_from_window(Window window) {
	for (int i = 0; i < MAXIMUM_WINDOWS; i++) {
		if (x11_ctx.windows[i].window == window) {
			return &x11_ctx.windows[i];
		}
	}

	return NULL;
}

void kore_internal_resize(int window_index, int width, int height);
static void init_pen_device(XDeviceInfo *info, struct x11_pen_device *pen, bool eraser);

static void load_lib(void **lib, const char *name);

bool kore_x11_init() {

#undef LOAD_LIB
#undef LOAD_FUN
#define LOAD_LIB(name)                                                                                                                                         \
	{                                                                                                                                                          \
		load_lib(&x11_ctx.libs.name, #name);                                                                                                                   \
                                                                                                                                                               \
		if (x11_ctx.libs.name == NULL) {                                                                                                                       \
			kore_log(KORE_LOG_LEVEL_ERROR, "Failed to load lib%s.so", #name);                                                                                  \
			return false;                                                                                                                                      \
		}                                                                                                                                                      \
	}                                                                                                                                                          \
	// manually check for libX11, and return false if not present
	// only error for further libs

	load_lib(&x11_ctx.libs.X11, "X11");
	if (x11_ctx.libs.X11 == NULL) {
		return false;
	}

	LOAD_LIB(Xi);
	LOAD_LIB(Xcursor);
	LOAD_LIB(Xinerama);
	LOAD_LIB(Xrandr);
#define LOAD_FUN(lib, symbol)                                                                                                                                  \
	xlib.symbol = dlsym(x11_ctx.libs.lib, #symbol);                                                                                                            \
	if (xlib.symbol == NULL) {                                                                                                                                 \
		kore_log(KORE_LOG_LEVEL_ERROR, "Did not find symbol %s in library %s.", #symbol, #lib);                                                                \
	}
	LOAD_FUN(X11, XOpenDisplay)
	LOAD_FUN(X11, XCloseDisplay)
	LOAD_FUN(X11, XSetErrorHandler)
	LOAD_FUN(X11, XGetErrorText)
	LOAD_FUN(X11, XInternAtoms)
	LOAD_FUN(X11, XPending)
	LOAD_FUN(X11, XFlush)
	LOAD_FUN(X11, XNextEvent)
	LOAD_FUN(X11, XRefreshKeyboardMapping)
	LOAD_FUN(X11, XwcLookupString)
	LOAD_FUN(X11, XFilterEvent)
	LOAD_FUN(X11, XConvertSelection)
	LOAD_FUN(X11, XSetSelectionOwner)
	LOAD_FUN(X11, XLookupString)
	LOAD_FUN(X11, XkbKeycodeToKeysym)
	LOAD_FUN(X11, XSendEvent)
	LOAD_FUN(X11, XGetWindowProperty)
	LOAD_FUN(X11, XFree)
	LOAD_FUN(X11, XChangeProperty)
	LOAD_FUN(X11, XDefineCursor)
	LOAD_FUN(X11, XUndefineCursor)
	LOAD_FUN(X11, XCreateBitmapFromData)
	LOAD_FUN(X11, XCreatePixmapCursor)
	LOAD_FUN(X11, XFreePixmap)
	LOAD_FUN(Xcursor, XcursorLibraryLoadCursor)
	LOAD_FUN(X11, XWarpPointer)
	LOAD_FUN(X11, XQueryPointer)
	LOAD_FUN(X11, XCreateColormap)
	LOAD_FUN(X11, XCreateWindow)
	LOAD_FUN(X11, XMoveWindow)
	LOAD_FUN(X11, XResizeWindow)
	LOAD_FUN(X11, XDestroyWindow)
	LOAD_FUN(X11, XSetClassHint)
	LOAD_FUN(X11, XSetLocaleModifiers)
	LOAD_FUN(X11, XOpenIM)
	LOAD_FUN(X11, XCloseIM)
	LOAD_FUN(X11, XCreateIC)
	LOAD_FUN(X11, XDestroyIC)
	LOAD_FUN(X11, XSetICFocus)
	LOAD_FUN(X11, XMapWindow)
	LOAD_FUN(X11, XUnmapWindow)
	LOAD_FUN(X11, XSetWMProtocols)
	LOAD_FUN(X11, XPeekEvent)

	LOAD_FUN(Xi, XListInputDevices)
	LOAD_FUN(Xi, XFreeDeviceList)
	LOAD_FUN(Xi, XOpenDevice)
	LOAD_FUN(Xi, XCloseDevice)
	LOAD_FUN(Xi, XSelectExtensionEvent)

	LOAD_FUN(Xinerama, XineramaQueryExtension)
	LOAD_FUN(Xinerama, XineramaIsActive)
	LOAD_FUN(Xinerama, XineramaQueryScreens)
	LOAD_FUN(Xrandr, XRRGetScreenResourcesCurrent)
	LOAD_FUN(Xrandr, XRRGetOutputPrimary)
	LOAD_FUN(Xrandr, XRRGetOutputInfo)
	LOAD_FUN(Xrandr, XRRFreeOutputInfo)
	LOAD_FUN(Xrandr, XRRGetCrtcInfo)
	LOAD_FUN(Xrandr, XRRFreeCrtcInfo)
	LOAD_FUN(Xrandr, XRRFreeScreenResources)

#undef LOAD_FUN
#undef LOAD_LIB

	x11_ctx.display = xlib.XOpenDisplay(NULL);
	if (!x11_ctx.display) {
		return false;
	}

	xlib.XSetErrorHandler(kore_x11_error_handler);

	// this should be kept in sync with the x11_atoms struct
	static char *atom_names[] = {
	    "XdndAware",
	    "XdndDrop",
	    "XdndEnter",
	    "text/uri-list",
	    "XdndStatus",
	    "XdndActionCopy",
	    "XdndSelection",
	    "CLIPBOARD",
	    "UTF8_STRING",
	    "XSEL_DATA",
	    "TARGETS",
	    "MULTIPLE",
	    "text/plain;charset=utf-8",
	    "WM_DELETE_WINDOW",
	    "_MOTIF_WM_HINTS",
	    "_NET_WM_NAME",
	    "_NET_WM_ICON_NAME",
	    "_NET_WM_STATE",
	    "_NET_WM_STATE_FULLSCREEN",
	    XI_MOUSE,
	    XI_TABLET,
	    XI_KEYBOARD,
	    XI_TOUCHSCREEN,
	    XI_TOUCHPAD,
	    XI_BUTTONBOX,
	    XI_BARCODE,
	    XI_TRACKBALL,
	    XI_QUADRATURE,
	    XI_ID_MODULE,
	    XI_ONE_KNOB,
	    XI_NINE_KNOB,
	    XI_KNOB_BOX,
	    XI_SPACEBALL,
	    XI_DATAGLOVE,
	    XI_EYETRACKER,
	    XI_CURSORKEYS,
	    XI_FOOTMOUSE,
	    XI_JOYSTICK,
	};

	assert((sizeof atom_names / sizeof atom_names[0]) == (sizeof(struct kore_x11_atoms) / sizeof(Atom)));
	xlib.XInternAtoms(x11_ctx.display, atom_names, sizeof atom_names / sizeof atom_names[0], False, (Atom *)&x11_ctx.atoms);
	clipboardString = (char *)malloc(clipboardStringSize);

	x11_ctx.pen.id = -1;
	x11_ctx.eraser.id = -1;

	int count;
	XDeviceInfoPtr devices = (XDeviceInfoPtr)xlib.XListInputDevices(x11_ctx.display, &count);
	for (int i = 0; i < count; i++) {
		strncpy(buffer, devices[i].name, 1023);
		buffer[1023] = 0;
		for (int j = 0; buffer[j]; j++) {
			buffer[j] = tolower(buffer[j]);
		}

		if (strstr(buffer, "stylus") || strstr(buffer, "pen") || strstr(buffer, "wacom")) {
			init_pen_device(&devices[i], &x11_ctx.pen, false);
		}
		if (strstr(buffer, "eraser")) {
			init_pen_device(&devices[i], &x11_ctx.eraser, true);
		}
	}

	if (devices != NULL) {
		xlib.XFreeDeviceList(devices);
	}

	return true;
}

void kore_x11_shutdown() {
	free(clipboardString);
	xlib.XCloseDisplay(x11_ctx.display);
}

static void init_pen_device(XDeviceInfo *info, struct x11_pen_device *pen, bool eraser) {
	XDevice *device = xlib.XOpenDevice(x11_ctx.display, info->id);
	XAnyClassPtr c = info->inputclassinfo;
	for (int j = 0; j < device->num_classes; j++) {
		if (c->class == ValuatorClass) {
			XValuatorInfo *valuator_info = (XValuatorInfo *)c;
			if (valuator_info->num_axes > 2) {
				pen->maxPressure = valuator_info->axes[2].max_value;
			}
			pen->id = info->id;
			DeviceMotionNotify(device, pen->motionEvent, pen->motionClass);
			if (eraser) {
				pen->press = kore_internal_eraser_trigger_press;
				pen->move = kore_internal_eraser_trigger_move;
				pen->release = kore_internal_eraser_trigger_release;
			}
			else {
				pen->press = kore_internal_pen_trigger_press;
				pen->move = kore_internal_pen_trigger_move;
				pen->release = kore_internal_pen_trigger_release;
			}
			return;
		}
		c = (XAnyClassPtr)((uint8_t *)c + c->length);
	}
	xlib.XCloseDevice(x11_ctx.display, device);
}

static void check_pen_device(struct kore_x11_window *window, XEvent *event, struct x11_pen_device *pen) {
	if (event->type == pen->motionEvent) {
		XDeviceMotionEvent *motion = (XDeviceMotionEvent *)(event);
		if (motion->deviceid == pen->id) {
			float p = (float)motion->axis_data[2] / (float)pen->maxPressure;
			if (p > 0 && x11_ctx.pen.current_pressure == 0) {
				pen->press(window->window_index, motion->x, motion->y, p);
			}
			else if (p == 0 && pen->current_pressure > 0) {
				pen->release(window->window_index, motion->x, motion->y, p);
			}
			else if (p > 0) {
				pen->move(window->window_index, motion->x, motion->y, p);
			}
			pen->current_pressure = p;
		}
	}
}

bool kore_x11_handle_messages() {
	static bool controlDown = false;
	static int ignoreKeycode = 0;
	static bool preventNextKeyDownEvent = false;

	while (xlib.XPending(x11_ctx.display)) {
		XEvent event;
		xlib.XNextEvent(x11_ctx.display, &event);
		Window window = event.xclient.window;
		struct kore_x11_window *k_window = window_from_window(window);
		if (k_window == NULL) {
			continue;
		}
		check_pen_device(k_window, &event, &x11_ctx.pen);
		check_pen_device(k_window, &event, &x11_ctx.eraser);
		switch (event.type) {
		case MappingNotify: {
			xlib.XRefreshKeyboardMapping(&event.xmapping);
		} break;
		case KeyPress: {

			XKeyEvent *key = (XKeyEvent *)&event;
			KeySym keysym;

			wchar_t wchar;

			bool wcConverted = xlib.XwcLookupString(k_window->xInputContext, key, &wchar, 1, &keysym, NULL);

			bool isIgnoredKeySym = keysym == XK_Escape || keysym == XK_BackSpace || keysym == XK_Delete;
			if (!controlDown && !xlib.XFilterEvent(&event, window) && !isIgnoredKeySym) {

				if (wcConverted) {
					kore_internal_keyboard_trigger_key_press(wchar);
				}
			}

			if (preventNextKeyDownEvent) {
				// this keypress is a repeated keystroke and should not lead to a keydown-event
				preventNextKeyDownEvent = false;
				continue;
			}

#define KEY(xkey, korekey)                                                                                                                                     \
	case xkey:                                                                                                                                                 \
		kore_internal_keyboard_trigger_key_down(korekey);                                                                                                      \
		break;

			KeySym ksKey = xlib.XkbKeycodeToKeysym(x11_ctx.display, event.xkey.keycode, 0, 0);

			if (ksKey == XK_Control_L || ksKey == XK_Control_R) {
				controlDown = true;
			}
			else if (controlDown && (ksKey == XK_v || ksKey == XK_V)) {
				xlib.XConvertSelection(x11_ctx.display, x11_ctx.atoms.CLIPBOARD, x11_ctx.atoms.UTF8_STRING, x11_ctx.atoms.XSEL_DATA, window, CurrentTime);
			}
			else if (controlDown && (ksKey == XK_c || ksKey == XK_C)) {
				xlib.XSetSelectionOwner(x11_ctx.display, x11_ctx.atoms.CLIPBOARD, window, CurrentTime);
				char *text = kore_internal_copy_callback();
				if (text != NULL)
					kore_x11_copy_to_clipboard(text);
			}
			else if (controlDown && (ksKey == XK_x || ksKey == XK_X)) {
				xlib.XSetSelectionOwner(x11_ctx.display, x11_ctx.atoms.CLIPBOARD, window, CurrentTime);
				char *text = kore_internal_cut_callback();
				if (text != NULL)
					kore_x11_copy_to_clipboard(text);
			}

			if (event.xkey.keycode == ignoreKeycode) {
				break;
			}
			else {
				ignoreKeycode = event.xkey.keycode;
			}

			if (ksKey < 97 || ksKey > 122) {
				ksKey = keysym;
			}

			switch (ksKey) {
				KEY(XK_Right, KORE_KEY_RIGHT)
				KEY(XK_Left, KORE_KEY_LEFT)
				KEY(XK_Up, KORE_KEY_UP)
				KEY(XK_Down, KORE_KEY_DOWN)
				KEY(XK_space, KORE_KEY_SPACE)
				KEY(XK_BackSpace, KORE_KEY_BACKSPACE)
				KEY(XK_Tab, KORE_KEY_TAB)
				KEY(XK_Return, KORE_KEY_RETURN)
				KEY(XK_Shift_L, KORE_KEY_SHIFT)
				KEY(XK_Shift_R, KORE_KEY_SHIFT)
				KEY(XK_Control_L, KORE_KEY_CONTROL)
				KEY(XK_Control_R, KORE_KEY_CONTROL)
				KEY(XK_Alt_L, KORE_KEY_ALT)
				KEY(XK_Alt_R, KORE_KEY_ALT)
				KEY(XK_Delete, KORE_KEY_DELETE)
				KEY(XK_comma, KORE_KEY_COMMA)
				KEY(XK_period, KORE_KEY_PERIOD)
				KEY(XK_bracketleft, KORE_KEY_OPEN_BRACKET)
				KEY(XK_bracketright, KORE_KEY_CLOSE_BRACKET)
				KEY(XK_braceleft, KORE_KEY_OPEN_CURLY_BRACKET)
				KEY(XK_braceright, KORE_KEY_CLOSE_CURLY_BRACKET)
				KEY(XK_parenleft, KORE_KEY_OPEN_PAREN)
				KEY(XK_parenright, KORE_KEY_CLOSE_PAREN)
				KEY(XK_backslash, KORE_KEY_BACK_SLASH)
				KEY(XK_apostrophe, KORE_KEY_QUOTE)
				KEY(XK_colon, KORE_KEY_COLON)
				KEY(XK_semicolon, KORE_KEY_SEMICOLON)
				KEY(XK_minus, KORE_KEY_HYPHEN_MINUS)
				KEY(XK_underscore, KORE_KEY_UNDERSCORE)
				KEY(XK_slash, KORE_KEY_SLASH)
				KEY(XK_bar, KORE_KEY_PIPE)
				KEY(XK_question, KORE_KEY_QUESTIONMARK)
				KEY(XK_less, KORE_KEY_LESS_THAN)
				KEY(XK_greater, KORE_KEY_GREATER_THAN)
				KEY(XK_asterisk, KORE_KEY_ASTERISK)
				KEY(XK_ampersand, KORE_KEY_AMPERSAND)
				KEY(XK_asciicircum, KORE_KEY_CIRCUMFLEX)
				KEY(XK_percent, KORE_KEY_PERCENT)
				KEY(XK_dollar, KORE_KEY_DOLLAR)
				KEY(XK_numbersign, KORE_KEY_HASH)
				KEY(XK_at, KORE_KEY_AT)
				KEY(XK_exclam, KORE_KEY_EXCLAMATION)
				KEY(XK_equal, KORE_KEY_EQUALS)
				KEY(XK_plus, KORE_KEY_ADD)
				KEY(XK_quoteleft, KORE_KEY_BACK_QUOTE)
				KEY(XK_quotedbl, KORE_KEY_DOUBLE_QUOTE)
				KEY(XK_asciitilde, KORE_KEY_TILDE)
				KEY(XK_Pause, KORE_KEY_PAUSE)
				KEY(XK_Scroll_Lock, KORE_KEY_SCROLL_LOCK)
				KEY(XK_Home, KORE_KEY_HOME)
				KEY(XK_Page_Up, KORE_KEY_PAGE_UP)
				KEY(XK_Page_Down, KORE_KEY_PAGE_DOWN)
				KEY(XK_End, KORE_KEY_END)
				KEY(XK_Insert, KORE_KEY_INSERT)
				KEY(XK_KP_Enter, KORE_KEY_RETURN)
				KEY(XK_KP_Multiply, KORE_KEY_MULTIPLY)
				KEY(XK_KP_Add, KORE_KEY_ADD)
				KEY(XK_KP_Subtract, KORE_KEY_SUBTRACT)
				KEY(XK_KP_Decimal, KORE_KEY_DECIMAL)
				KEY(XK_KP_Divide, KORE_KEY_DIVIDE)
				KEY(XK_KP_0, KORE_KEY_NUMPAD_0)
				KEY(XK_KP_1, KORE_KEY_NUMPAD_1)
				KEY(XK_KP_2, KORE_KEY_NUMPAD_2)
				KEY(XK_KP_3, KORE_KEY_NUMPAD_3)
				KEY(XK_KP_4, KORE_KEY_NUMPAD_4)
				KEY(XK_KP_5, KORE_KEY_NUMPAD_5)
				KEY(XK_KP_6, KORE_KEY_NUMPAD_6)
				KEY(XK_KP_7, KORE_KEY_NUMPAD_7)
				KEY(XK_KP_8, KORE_KEY_NUMPAD_8)
				KEY(XK_KP_9, KORE_KEY_NUMPAD_9)
				KEY(XK_KP_Insert, KORE_KEY_INSERT)
				KEY(XK_KP_Delete, KORE_KEY_DELETE)
				KEY(XK_KP_End, KORE_KEY_END)
				KEY(XK_KP_Home, KORE_KEY_HOME)
				KEY(XK_KP_Left, KORE_KEY_LEFT)
				KEY(XK_KP_Up, KORE_KEY_UP)
				KEY(XK_KP_Right, KORE_KEY_RIGHT)
				KEY(XK_KP_Down, KORE_KEY_DOWN)
				KEY(XK_KP_Page_Up, KORE_KEY_PAGE_UP)
				KEY(XK_KP_Page_Down, KORE_KEY_PAGE_DOWN)
				KEY(XK_Menu, KORE_KEY_CONTEXT_MENU)
				KEY(XK_a, KORE_KEY_A)
				KEY(XK_b, KORE_KEY_B)
				KEY(XK_c, KORE_KEY_C)
				KEY(XK_d, KORE_KEY_D)
				KEY(XK_e, KORE_KEY_E)
				KEY(XK_f, KORE_KEY_F)
				KEY(XK_g, KORE_KEY_G)
				KEY(XK_h, KORE_KEY_H)
				KEY(XK_i, KORE_KEY_I)
				KEY(XK_j, KORE_KEY_J)
				KEY(XK_k, KORE_KEY_K)
				KEY(XK_l, KORE_KEY_L)
				KEY(XK_m, KORE_KEY_M)
				KEY(XK_n, KORE_KEY_N)
				KEY(XK_o, KORE_KEY_O)
				KEY(XK_p, KORE_KEY_P)
				KEY(XK_q, KORE_KEY_Q)
				KEY(XK_r, KORE_KEY_R)
				KEY(XK_s, KORE_KEY_S)
				KEY(XK_t, KORE_KEY_T)
				KEY(XK_u, KORE_KEY_U)
				KEY(XK_v, KORE_KEY_V)
				KEY(XK_w, KORE_KEY_W)
				KEY(XK_x, KORE_KEY_X)
				KEY(XK_y, KORE_KEY_Y)
				KEY(XK_z, KORE_KEY_Z)
				KEY(XK_1, KORE_KEY_1)
				KEY(XK_2, KORE_KEY_2)
				KEY(XK_3, KORE_KEY_3)
				KEY(XK_4, KORE_KEY_4)
				KEY(XK_5, KORE_KEY_5)
				KEY(XK_6, KORE_KEY_6)
				KEY(XK_7, KORE_KEY_7)
				KEY(XK_8, KORE_KEY_8)
				KEY(XK_9, KORE_KEY_9)
				KEY(XK_0, KORE_KEY_0)
				KEY(XK_Escape, KORE_KEY_ESCAPE)
				KEY(XK_F1, KORE_KEY_F1)
				KEY(XK_F2, KORE_KEY_F2)
				KEY(XK_F3, KORE_KEY_F3)
				KEY(XK_F4, KORE_KEY_F4)
				KEY(XK_F5, KORE_KEY_F5)
				KEY(XK_F6, KORE_KEY_F6)
				KEY(XK_F7, KORE_KEY_F7)
				KEY(XK_F8, KORE_KEY_F8)
				KEY(XK_F9, KORE_KEY_F9)
				KEY(XK_F10, KORE_KEY_F10)
				KEY(XK_F11, KORE_KEY_F11)
				KEY(XK_F12, KORE_KEY_F12)
			}
			break;
#undef KEY
		}
		case KeyRelease: {
			XKeyEvent *key = (XKeyEvent *)&event;

			// peek next-event to determine if this a repeated-keystroke
			XEvent nev;
			if (xlib.XPending(x11_ctx.display)) {
				xlib.XPeekEvent(x11_ctx.display, &nev);

				if (nev.type == KeyPress && nev.xkey.time == event.xkey.time && nev.xkey.keycode == event.xkey.keycode) {
					// repeated keystroke! prevent this keyup-event and next keydown-event from being fired
					preventNextKeyDownEvent = true;
					continue;
				}
			}
			KeySym keysym;

			char c;
			xlib.XLookupString(key, &c, 1, &keysym, NULL);

#define KEY(xkey, korekey)                                                                                                                                     \
	case xkey:                                                                                                                                                 \
		kore_internal_keyboard_trigger_key_up(korekey);                                                                                                        \
		break;

			KeySym ksKey = xlib.XkbKeycodeToKeysym(x11_ctx.display, event.xkey.keycode, 0, 0);

			if (ksKey == XK_Control_L || ksKey == XK_Control_R) {
				controlDown = false;
			}

			if (event.xkey.keycode == ignoreKeycode) {
				ignoreKeycode = 0;
			}

			if (ksKey < 97 || ksKey > 122) {
				ksKey = keysym;
			}

			switch (ksKey) {
				KEY(XK_Right, KORE_KEY_RIGHT)
				KEY(XK_Left, KORE_KEY_LEFT)
				KEY(XK_Up, KORE_KEY_UP)
				KEY(XK_Down, KORE_KEY_DOWN)
				KEY(XK_space, KORE_KEY_SPACE)
				KEY(XK_BackSpace, KORE_KEY_BACKSPACE)
				KEY(XK_Tab, KORE_KEY_TAB)
				KEY(XK_Return, KORE_KEY_RETURN)
				KEY(XK_Shift_L, KORE_KEY_SHIFT)
				KEY(XK_Shift_R, KORE_KEY_SHIFT)
				KEY(XK_Control_L, KORE_KEY_CONTROL)
				KEY(XK_Control_R, KORE_KEY_CONTROL)
				KEY(XK_Alt_L, KORE_KEY_ALT)
				KEY(XK_Alt_R, KORE_KEY_ALT)
				KEY(XK_Delete, KORE_KEY_DELETE)
				KEY(XK_comma, KORE_KEY_COMMA)
				KEY(XK_period, KORE_KEY_PERIOD)
				KEY(XK_bracketleft, KORE_KEY_OPEN_BRACKET)
				KEY(XK_bracketright, KORE_KEY_CLOSE_BRACKET)
				KEY(XK_braceleft, KORE_KEY_OPEN_CURLY_BRACKET)
				KEY(XK_braceright, KORE_KEY_CLOSE_CURLY_BRACKET)
				KEY(XK_parenleft, KORE_KEY_OPEN_PAREN)
				KEY(XK_parenright, KORE_KEY_CLOSE_PAREN)
				KEY(XK_backslash, KORE_KEY_BACK_SLASH)
				KEY(XK_apostrophe, KORE_KEY_QUOTE)
				KEY(XK_colon, KORE_KEY_COLON)
				KEY(XK_semicolon, KORE_KEY_SEMICOLON)
				KEY(XK_minus, KORE_KEY_HYPHEN_MINUS)
				KEY(XK_underscore, KORE_KEY_UNDERSCORE)
				KEY(XK_slash, KORE_KEY_SLASH)
				KEY(XK_bar, KORE_KEY_PIPE)
				KEY(XK_question, KORE_KEY_QUESTIONMARK)
				KEY(XK_less, KORE_KEY_LESS_THAN)
				KEY(XK_greater, KORE_KEY_GREATER_THAN)
				KEY(XK_asterisk, KORE_KEY_ASTERISK)
				KEY(XK_ampersand, KORE_KEY_AMPERSAND)
				KEY(XK_asciicircum, KORE_KEY_CIRCUMFLEX)
				KEY(XK_percent, KORE_KEY_PERCENT)
				KEY(XK_dollar, KORE_KEY_DOLLAR)
				KEY(XK_numbersign, KORE_KEY_HASH)
				KEY(XK_at, KORE_KEY_AT)
				KEY(XK_exclam, KORE_KEY_EXCLAMATION)
				KEY(XK_equal, KORE_KEY_EQUALS)
				KEY(XK_plus, KORE_KEY_ADD)
				KEY(XK_quoteleft, KORE_KEY_BACK_QUOTE)
				KEY(XK_quotedbl, KORE_KEY_DOUBLE_QUOTE)
				KEY(XK_asciitilde, KORE_KEY_TILDE)
				KEY(XK_Pause, KORE_KEY_PAUSE)
				KEY(XK_Scroll_Lock, KORE_KEY_SCROLL_LOCK)
				KEY(XK_Home, KORE_KEY_HOME)
				KEY(XK_Page_Up, KORE_KEY_PAGE_UP)
				KEY(XK_Page_Down, KORE_KEY_PAGE_DOWN)
				KEY(XK_End, KORE_KEY_END)
				KEY(XK_Insert, KORE_KEY_INSERT)
				KEY(XK_KP_Enter, KORE_KEY_RETURN)
				KEY(XK_KP_Multiply, KORE_KEY_MULTIPLY)
				KEY(XK_KP_Add, KORE_KEY_ADD)
				KEY(XK_KP_Subtract, KORE_KEY_SUBTRACT)
				KEY(XK_KP_Decimal, KORE_KEY_DECIMAL)
				KEY(XK_KP_Divide, KORE_KEY_DIVIDE)
				KEY(XK_KP_0, KORE_KEY_NUMPAD_0)
				KEY(XK_KP_1, KORE_KEY_NUMPAD_1)
				KEY(XK_KP_2, KORE_KEY_NUMPAD_2)
				KEY(XK_KP_3, KORE_KEY_NUMPAD_3)
				KEY(XK_KP_4, KORE_KEY_NUMPAD_4)
				KEY(XK_KP_5, KORE_KEY_NUMPAD_5)
				KEY(XK_KP_6, KORE_KEY_NUMPAD_6)
				KEY(XK_KP_7, KORE_KEY_NUMPAD_7)
				KEY(XK_KP_8, KORE_KEY_NUMPAD_8)
				KEY(XK_KP_9, KORE_KEY_NUMPAD_9)
				KEY(XK_KP_Insert, KORE_KEY_INSERT)
				KEY(XK_KP_Delete, KORE_KEY_DELETE)
				KEY(XK_KP_End, KORE_KEY_END)
				KEY(XK_KP_Home, KORE_KEY_HOME)
				KEY(XK_KP_Left, KORE_KEY_LEFT)
				KEY(XK_KP_Up, KORE_KEY_UP)
				KEY(XK_KP_Right, KORE_KEY_RIGHT)
				KEY(XK_KP_Down, KORE_KEY_DOWN)
				KEY(XK_KP_Page_Up, KORE_KEY_PAGE_UP)
				KEY(XK_KP_Page_Down, KORE_KEY_PAGE_DOWN)
				KEY(XK_Menu, KORE_KEY_CONTEXT_MENU)
				KEY(XK_a, KORE_KEY_A)
				KEY(XK_b, KORE_KEY_B)
				KEY(XK_c, KORE_KEY_C)
				KEY(XK_d, KORE_KEY_D)
				KEY(XK_e, KORE_KEY_E)
				KEY(XK_f, KORE_KEY_F)
				KEY(XK_g, KORE_KEY_G)
				KEY(XK_h, KORE_KEY_H)
				KEY(XK_i, KORE_KEY_I)
				KEY(XK_j, KORE_KEY_J)
				KEY(XK_k, KORE_KEY_K)
				KEY(XK_l, KORE_KEY_L)
				KEY(XK_m, KORE_KEY_M)
				KEY(XK_n, KORE_KEY_N)
				KEY(XK_o, KORE_KEY_O)
				KEY(XK_p, KORE_KEY_P)
				KEY(XK_q, KORE_KEY_Q)
				KEY(XK_r, KORE_KEY_R)
				KEY(XK_s, KORE_KEY_S)
				KEY(XK_t, KORE_KEY_T)
				KEY(XK_u, KORE_KEY_U)
				KEY(XK_v, KORE_KEY_V)
				KEY(XK_w, KORE_KEY_W)
				KEY(XK_x, KORE_KEY_X)
				KEY(XK_y, KORE_KEY_Y)
				KEY(XK_z, KORE_KEY_Z)
				KEY(XK_1, KORE_KEY_1)
				KEY(XK_2, KORE_KEY_2)
				KEY(XK_3, KORE_KEY_3)
				KEY(XK_4, KORE_KEY_4)
				KEY(XK_5, KORE_KEY_5)
				KEY(XK_6, KORE_KEY_6)
				KEY(XK_7, KORE_KEY_7)
				KEY(XK_8, KORE_KEY_8)
				KEY(XK_9, KORE_KEY_9)
				KEY(XK_0, KORE_KEY_0)
				KEY(XK_Escape, KORE_KEY_ESCAPE)
				KEY(XK_F1, KORE_KEY_F1)
				KEY(XK_F2, KORE_KEY_F2)
				KEY(XK_F3, KORE_KEY_F3)
				KEY(XK_F4, KORE_KEY_F4)
				KEY(XK_F5, KORE_KEY_F5)
				KEY(XK_F6, KORE_KEY_F6)
				KEY(XK_F7, KORE_KEY_F7)
				KEY(XK_F8, KORE_KEY_F8)
				KEY(XK_F9, KORE_KEY_F9)
				KEY(XK_F10, KORE_KEY_F10)
				KEY(XK_F11, KORE_KEY_F11)
				KEY(XK_F12, KORE_KEY_F12)
			}
			break;
#undef KEY
		}
		case ButtonPress: {
			XButtonEvent *button = (XButtonEvent *)&event;
			int window_index = k_window->window_index;

			switch (button->button) {
			case Button1:
				kore_internal_mouse_trigger_press(window_index, 0, button->x, button->y);
				break;
			case Button2:
				kore_internal_mouse_trigger_press(window_index, 2, button->x, button->y);
				break;
			case Button3:
				kore_internal_mouse_trigger_press(window_index, 1, button->x, button->y);
				break;
			// buttons 4-7 are for mouse wheel events because why not
			case Button4:
			case Button5:
			case Button6:
			case Button7:
				break;
			default:
				kore_internal_mouse_trigger_press(window_index, button->button - Button1 - 4, button->x, button->y);
				break;
			}
			break;
		}
		case ButtonRelease: {
			XButtonEvent *button = (XButtonEvent *)&event;
			int window_index = k_window->window_index;

			switch (button->button) {
			case Button1:
				kore_internal_mouse_trigger_release(window_index, 0, button->x, button->y);
				break;
			case Button2:
				kore_internal_mouse_trigger_release(window_index, 2, button->x, button->y);
				break;
			case Button3:
				kore_internal_mouse_trigger_release(window_index, 1, button->x, button->y);
				break;
			// Button4 and Button5 provide mouse wheel events because why not
			case Button4:
				kore_internal_mouse_trigger_scroll(window_index, -1);
				break;
			case Button5:
				kore_internal_mouse_trigger_scroll(window_index, 1);
				break;
			// button 6 and 7 seem to be horizontal scrolling, which is not exposed in Kinc's api at the moment
			case Button6:
			case Button7:
				break;
			default:
				kore_internal_mouse_trigger_release(window_index, button->button - Button1 - 4, button->x, button->y);
				break;
			}
			break;
		}
		case MotionNotify: {
			XMotionEvent *motion = (XMotionEvent *)&event;
			kore_internal_mouse_trigger_move(k_window->window_index, motion->x, motion->y);
			break;
		}
		case ConfigureNotify: {
			if (event.xconfigure.width != k_window->width || event.xconfigure.height != k_window->height) {
				k_window->width = event.xconfigure.width;
				k_window->height = event.xconfigure.height;
				// kore_internal_resize(k_window->window_index, event.xconfigure.width, event.xconfigure.height); // TODO
				kore_internal_call_resize_callback(k_window->window_index, event.xconfigure.width, event.xconfigure.height);
			}
			break;
		}
		case ClientMessage: {
			if (event.xclient.message_type == x11_ctx.atoms.XdndEnter) {
				Window source_window = event.xclient.data.l[0];
				XEvent m;
				memset(&m, 0, sizeof(m));
				m.type = ClientMessage;
				m.xclient.window = event.xclient.data.l[0];
				m.xclient.message_type = x11_ctx.atoms.XdndStatus;
				m.xclient.format = 32;
				m.xclient.data.l[0] = window;
				m.xclient.data.l[2] = 0;
				m.xclient.data.l[3] = 0;
				m.xclient.data.l[1] = 1;
				m.xclient.data.l[4] = x11_ctx.atoms.XdndActionCopy;
				xlib.XSendEvent(x11_ctx.display, source_window, false, NoEventMask, (XEvent *)&m);
				xlib.XFlush(x11_ctx.display);
			}
			else if (event.xclient.message_type == x11_ctx.atoms.XdndDrop) {
				xlib.XConvertSelection(x11_ctx.display, x11_ctx.atoms.XdndSelection, x11_ctx.atoms.XdndTextUriList, x11_ctx.atoms.XdndSelection, window,
				                       event.xclient.data.l[2]);
			}
			else if (event.xclient.data.l[0] == x11_ctx.atoms.WM_DELETE_WINDOW) {
				if (kore_internal_call_close_callback(k_window->window_index)) {
					kore_window_destroy(k_window->window_index);
					if (x11_ctx.num_windows <= 0) {
						// no windows left, stop
						kore_stop();
					}
				}
			}
		}; break;
		case SelectionNotify: {
			if (event.xselection.selection == x11_ctx.atoms.CLIPBOARD) {
				char *result;
				unsigned long ressize, restail;
				int resbits;
				xlib.XGetWindowProperty(x11_ctx.display, window, x11_ctx.atoms.XSEL_DATA, 0, LONG_MAX / 4, False, AnyPropertyType, &x11_ctx.atoms.UTF8_STRING,
				                        &resbits, &ressize, &restail, (unsigned char **)&result);
				kore_internal_paste_callback(result);
				xlib.XFree(result);
			}
			else if (event.xselection.property == x11_ctx.atoms.XdndSelection) {
				Atom type;
				int format;
				unsigned long numItems;
				unsigned long bytesAfter = 1;
				unsigned char *data = 0;
				xlib.XGetWindowProperty(x11_ctx.display, event.xselection.requestor, event.xselection.property, 0, LONG_MAX, False, event.xselection.target,
				                        &type, &format, &numItems, &bytesAfter, &data);
				size_t pos = 0;
				size_t len = 0;
				while (pos < numItems) {
					if (data[pos] == '\r') { // Found a file
						wchar_t filePath[len + 1];
						mbstowcs(filePath, buffer, len);
						filePath[len] = 0;
						kore_internal_drop_files_callback(filePath + 7); // Strip file://
						pos += 2;                                        // Avoid \n
						len = 0;
					}
					buffer[len++] = data[pos++];
				}
				xlib.XFree(data);
			}
			break;
		}
		case SelectionRequest: {
			if (event.xselectionrequest.target == x11_ctx.atoms.TARGETS) {
				XEvent send;
				send.xselection.type = SelectionNotify;
				send.xselection.requestor = event.xselectionrequest.requestor;
				send.xselection.selection = event.xselectionrequest.selection;
				send.xselection.target = event.xselectionrequest.target;
				send.xselection.property = event.xselectionrequest.property;
				send.xselection.time = event.xselectionrequest.time;
				Atom available[] = {x11_ctx.atoms.TARGETS, x11_ctx.atoms.MULTIPLE, x11_ctx.atoms.TEXT_PLAIN, x11_ctx.atoms.UTF8_STRING};
				xlib.XChangeProperty(x11_ctx.display, send.xselection.requestor, send.xselection.property, XA_ATOM, 32, PropModeReplace,
				                     (unsigned char *)&available[0], 4);
				xlib.XSendEvent(x11_ctx.display, send.xselection.requestor, True, 0, &send);
			}
			if (event.xselectionrequest.target == x11_ctx.atoms.TEXT_PLAIN || event.xselectionrequest.target == x11_ctx.atoms.UTF8_STRING) {
				XEvent send;
				send.xselection.type = SelectionNotify;
				send.xselection.requestor = event.xselectionrequest.requestor;
				send.xselection.selection = event.xselectionrequest.selection;
				send.xselection.target = event.xselectionrequest.target;
				send.xselection.property = event.xselectionrequest.property;
				send.xselection.time = event.xselectionrequest.time;
				xlib.XChangeProperty(x11_ctx.display, send.xselection.requestor, send.xselection.property, send.xselection.target, 8, PropModeReplace,
				                     (const unsigned char *)clipboardString, strlen(clipboardString));
				xlib.XSendEvent(x11_ctx.display, send.xselection.requestor, True, 0, &send);
			}
			break;
		}
		case Expose:
			break;
		case FocusIn: {
			kore_internal_foreground_callback();
			break;
		}
		case FocusOut: {
			controlDown = false;
			ignoreKeycode = 0;
			kore_internal_background_callback();
			break;
		}
		case LeaveNotify:
			kore_internal_mouse_trigger_leave_window(k_window->window_index);
			break;
		case EnterNotify:
			x11_ctx.mouse.current_window = k_window->window_index;
			kore_internal_mouse_trigger_enter_window(k_window->window_index);
			break;
		}
	}

	return true;
}

void kore_x11_copy_to_clipboard(const char *text) {
	size_t textLength = strlen(text);
	if (textLength >= clipboardStringSize) {
		free(clipboardString);
		clipboardStringSize = textLength + 1;
		clipboardString = (char *)malloc(clipboardStringSize);
	}
	strcpy(clipboardString, text);
}

#ifdef KORE_EGL
EGLDisplay kore_x11_egl_get_display() {
	return eglGetDisplay(x11_ctx.display);
}

EGLNativeWindowType kore_x11_egl_get_native_window(EGLDisplay display, EGLConfig config, int window_index) {
	return (EGLNativeWindowType)x11_ctx.windows[window_index].window;
}
#endif

#ifdef KORE_VULKAN
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>
VkResult kore_x11_vulkan_create_surface(VkInstance instance, int window_index, VkSurfaceKHR *surface) {
	VkXlibSurfaceCreateInfoKHR info = {0};
	info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
	info.pNext = NULL;
	info.flags = 0;
	info.dpy = x11_ctx.display;
	info.window = x11_ctx.windows[window_index].window;
	return vkCreateXlibSurfaceKHR(instance, &info, NULL, surface);
}

#include <assert.h>

void kore_x11_vulkan_get_instance_extensions(const char **names, int *index, int max) {
	assert(*index + 1 < max);
	names[(*index)++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
}

VkBool32 kore_x11_vulkan_get_physical_device_presentation_support(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) {
	return vkGetPhysicalDeviceXlibPresentationSupportKHR(physicalDevice, queueFamilyIndex, x11_ctx.display,
	                                                     DefaultVisual(x11_ctx.display, DefaultScreen(x11_ctx.display))->visualid);
}
#endif

void kore_x11_mouse_lock(int window) {
	kore_mouse_hide();
	int width = kore_window_width(window);
	int height = kore_window_height(window);

	int x, y;
	kore_mouse_get_position(window, &x, &y);

	// Guess the new position of X and Y
	int newX = x;
	int newY = y;

	// Correct the position of the X coordinate
	// if the mouse is out the window
	if (x < 0) {
		newX -= x;
	}
	else if (x > width) {
		newX -= x - width;
	}

	// Correct the position of the Y coordinate
	// if the mouse is out the window
	if (y < 0) {
		newY -= y;
	}
	else if (y > height) {
		newY -= y - height;
	}

	// Force the mouse to stay inside the window
	kore_mouse_set_position(window, newX, newY);
}

void kore_x11_mouse_unlock(void) {
	kore_mouse_show();
}

bool kore_x11_mouse_can_lock(void) {
	return true;
}

static bool mouseHidden = false;

void kore_x11_mouse_show() {
	struct kore_x11_window *window = &x11_ctx.windows[x11_ctx.mouse.current_window];
	if (mouseHidden) {
		xlib.XUndefineCursor(x11_ctx.display, window->window);
		mouseHidden = false;
	}
}

void kore_x11_mouse_hide() {
	struct kore_x11_window *window = &x11_ctx.windows[x11_ctx.mouse.current_window];
	if (!mouseHidden) {
		XColor col;
		col.pixel = 0;
		col.red = 0;
		col.green = 0;
		col.blue = 0;
		col.flags = DoRed | DoGreen | DoBlue;
		col.pad = 0;
		char data[1] = {'\0'};
		Pixmap blank = xlib.XCreateBitmapFromData(x11_ctx.display, window->window, data, 1, 1);
		Cursor cursor = xlib.XCreatePixmapCursor(x11_ctx.display, blank, blank, &col, &col, 0, 0);
		xlib.XDefineCursor(x11_ctx.display, window->window, cursor);
		xlib.XFreePixmap(x11_ctx.display, blank);
		mouseHidden = true;
	}
}

void kore_x11_mouse_set_cursor(int cursorIndex) {
	struct kore_x11_window *window = &x11_ctx.windows[x11_ctx.mouse.current_window];
	if (!mouseHidden) {
		Cursor cursor;
		switch (cursorIndex) {
		case 0: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "arrow");
			break;
		}
		case 1: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "hand1");
			break;
		}
		case 2: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "xterm");
			break;
		}
		case 3: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "sb_h_double_arrow");
			break;
		}
		case 4: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "sb_v_double_arrow");
			break;
		}
		case 5: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "top_right_corner");
			break;
		}
		case 6: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "bottom_right_corner");
			break;
		}
		case 7: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "top_left_corner");
			break;
		}
		case 8: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "bottom_left_corner");
			break;
		}
		case 9: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "grab");
			break;
		}
		case 10: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "grabbing");
			break;
		}
		case 11: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "not-allowed");
			break;
		}
		case 12: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "watch");
			break;
		}
		case 13: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "crosshair");
			break;
		}
		default: {
			cursor = xlib.XcursorLibraryLoadCursor(x11_ctx.display, "arrow");
			break;
		}
		}
		xlib.XDefineCursor(x11_ctx.display, window->window, cursor);
	}
}

void kore_x11_mouse_set_position(int window_index, int x, int y) {
	struct kore_x11_window *window = &x11_ctx.windows[window_index];

	xlib.XWarpPointer(x11_ctx.display, None, window->window, 0, 0, 0, 0, x, y);
	xlib.XFlush(x11_ctx.display); // Flushes the output buffer, therefore updates the cursor's position.
}

void kore_x11_mouse_get_position(int window_index, int *x, int *y) {
	struct kore_x11_window *window = &x11_ctx.windows[window_index];
	Window inwin;
	Window inchildwin;
	int rootx, rooty;
	unsigned int mask;

	xlib.XQueryPointer(x11_ctx.display, window->window, &inwin, &inchildwin, &rootx, &rooty, x, y, &mask);
}

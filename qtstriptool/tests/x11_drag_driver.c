/* Drive a real, unmodified QtEDM mouse gesture in an isolated X11 test session. */
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void pauseMs(long ms) {
  const struct timespec delay = {ms / 1000, (ms % 1000) * 1000000};
  nanosleep(&delay, NULL);
}
static Window findSource(Display *display, Window parent) {
  char *name = NULL;
  if (XFetchName(display, parent, &name) && name) {
    const int matches = strstr(name, "pv_drag_source.adl") != NULL;
    XFree(name);
    if (matches) return parent;
  }
  Window root, ancestor, *children = NULL;
  unsigned int count = 0;
  if (!XQueryTree(display, parent, &root, &ancestor, &children, &count)) return None;
  Window found = None;
  for (unsigned int i = 0; i < count && !found; ++i)
    found = findSource(display, children[i]);
  if (children) XFree(children);
  return found;
}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  Display *display = XOpenDisplay(NULL);
  if (!display) return 2;
  Window root = DefaultRootWindow(display);
  Window source = None;
  for (int i = 0; i < 50 && !source; ++i) {
    source = findSource(display, root);
    if (!source) pauseMs(100);
  }
  if (!source) { fprintf(stderr, "QtEDM source window not found\n"); return 2; }
  XMoveWindow(display, source, 0, 0);
  XRaiseWindow(display, source);
  XSync(display, False);
  pauseMs(200);
  Window destination = strtoul(argv[1], NULL, 10), child;
  int startX, startY, dropX, dropY;
  XTranslateCoordinates(display, source, root, 100, 80, &startX, &startY, &child);
  XTranslateCoordinates(display, destination, root, 300, 200, &dropX, &dropY, &child);
  XTestFakeMotionEvent(display, -1, startX, startY, CurrentTime);
  XTestFakeButtonEvent(display, 2, True, CurrentTime);
  XFlush(display);
  pauseMs(200);
  XTestFakeMotionEvent(display, -1, startX + 20, startY, CurrentTime);
  XFlush(display);
  pauseMs(200);
  XTestFakeMotionEvent(display, -1, dropX - 20, dropY, CurrentTime);
  XFlush(display);
  pauseMs(300);
  XTestFakeMotionEvent(display, -1, dropX, dropY, CurrentTime);
  XFlush(display);
  pauseMs(300);
  XTestFakeButtonEvent(display, 2, False, CurrentTime);
  XSync(display, False);
  XCloseDisplay(display);
  return 0;
}

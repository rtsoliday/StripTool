/* Exercise the same XmDragStart/COMPOUND_TEXT path as legacy MEDM. */
#include <Xm/Xm.h>
#include <Xm/DragDrop.h>
#include <Xm/PushB.h>
#include <X11/extensions/XTest.h>
#include <stdlib.h>
#include <string.h>

static Widget source;
static const char *pv;
static int dropX, dropY;

static Boolean convert(Widget widget, Atom *selection, Atom *target,
                       Atom *type, XtPointer *value, unsigned long *length,
                       int *format, unsigned long *maxLength,
                       XtPointer data, XtRequestId *request) {
  (void)selection; (void)maxLength; (void)data; (void)request;
  if (*target != XInternAtom(XtDisplay(widget), "COMPOUND_TEXT", False)) return False;
  *type = *target;
  *value = XtNewString(pv);
  *length = strlen(pv);
  *format = 8;
  return True;
}
static void finished(Widget widget, XtPointer data, XtPointer call) {
  (void)widget; (void)data;
  XmDropFinishCallbackStruct *result = call;
  exit(result->completionStatus == XmDROP_SUCCESS ? 0 : 1);
}
static void start(Widget widget, XtPointer data, XEvent *event, Boolean *dispatch) {
  (void)data; (void)dispatch;
  if (event->type != ButtonPress || event->xbutton.button != 2) return;
  Atom target = XInternAtom(XtDisplay(widget), "COMPOUND_TEXT", False);
  Arg args[4];
  int n = 0;
  XtSetArg(args[n], XmNexportTargets, &target); ++n;
  XtSetArg(args[n], XmNnumExportTargets, 1); ++n;
  XtSetArg(args[n], XmNdragOperations, XmDROP_COPY); ++n;
  XtSetArg(args[n], XmNconvertProc, convert); ++n;
  Widget drag = XmDragStart(widget, event, args, n);
  XtAddCallback(drag, XmNdropFinishCallback, finished, NULL);
}
static void release(XtPointer data, XtIntervalId *id) {
  (void)data; (void)id;
  XTestFakeButtonEvent(XtDisplay(source), 2, False, CurrentTime);
  XFlush(XtDisplay(source));
}
static void move(XtPointer data, XtIntervalId *id) {
  (void)data; (void)id;
  XTestFakeMotionEvent(XtDisplay(source), -1, dropX, dropY, CurrentTime);
  XFlush(XtDisplay(source));
  XtAppAddTimeOut(XtWidgetToApplicationContext(source), 500, release, NULL);
}
static void press(XtPointer data, XtIntervalId *id) {
  (void)data; (void)id;
  XTestFakeMotionEvent(XtDisplay(source), -1, 30, 30, CurrentTime);
  XTestFakeButtonEvent(XtDisplay(source), 2, True, CurrentTime);
  XFlush(XtDisplay(source));
  XtAppAddTimeOut(XtWidgetToApplicationContext(source), 500, move, NULL);
}
static void expired(XtPointer data, XtIntervalId *id) {
  (void)data; (void)id;
  exit(2);
}
int main(int argc, char **argv) {
  if (argc != 3) return 2;
  Window destination = strtoul(argv[1], NULL, 10);
  pv = argv[2];
  XtAppContext app;
  Widget shell = XtVaAppInitialize(&app, "MotifPvSource", NULL, 0,
                                   &argc, argv, NULL, XtNx, 0, XtNy, 0, NULL);
  source = XtVaCreateManagedWidget("source", xmPushButtonWidgetClass, shell,
                                   XmNwidth, 100, XmNheight, 80, NULL);
  XtAddEventHandler(source, ButtonPressMask, False, start, NULL);
  XtRealizeWidget(shell);
  Window child;
  XTranslateCoordinates(XtDisplay(source), destination,
                         DefaultRootWindow(XtDisplay(source)),
                         300, 200, &dropX, &dropY, &child);
  XtAppAddTimeOut(app, 200, press, NULL);
  XtAppAddTimeOut(app, 8000, expired, NULL);
  XtAppMainLoop(app);
  return 2;
}

#include "events.h"
#include "misc/extern.h"
#include "windows.h"
#include "border.h"
#include "misc/window.h"
#include <time.h>

extern struct table g_windows;
extern pid_t g_pid;

#ifdef DEBUG
static void dump_event(void* data, size_t data_length) {
  for (int i = 0; i < data_length; i++) {
    printf("%02x ", *((unsigned char*)data + i));
  }
  printf("\n");
}

static void event_watcher(uint32_t event, void* data, size_t data_length, void* context) {
  static int count = 0;
  printf("(%d) Event: %d; Payload:\n", ++count, event);
  dump_event(data, data_length);
}
#endif

struct window_spawn_data {
  uint64_t sid;
  uint32_t wid;
};

static bool is_own_window(int cid, uint32_t wid) {
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);
  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  return pid == g_pid;
}

static void window_spawn_handler(uint32_t event, struct window_spawn_data* data, size_t _, int cid) {
  struct table* windows = &g_windows;
  uint32_t wid = data->wid;
  uint64_t sid = data->sid;

  if (!wid || !sid || is_own_window(cid, wid)) return;

  if (event == EVENT_WINDOW_CREATE) {
    if (windows_window_create(windows, wid, sid)) {
      debug("Window Created: %d %d\n", wid, sid);
      windows_determine_and_focus_active_window(windows);
    }
  } else if (event == EVENT_WINDOW_DESTROY) {
    if (windows_window_destroy(windows, wid, sid)) {
      debug("Window Destroyed: %d %d\n", wid, sid);
    }
    windows_determine_and_focus_active_window(windows);
  }
}

// Keep the first requested check and the latest settling deadline in each burst.
// All timer state is owned by the main queue.
static dispatch_source_t focus_timer;
static uint64_t next_focus_check;
static uint64_t last_focus_check;

static void arm_focus_timer(uint64_t deadline, uint64_t now) {
  next_focus_check = deadline;
  int64_t delay = deadline > now ? (int64_t)(deadline - now) : 0;
  dispatch_source_set_timer(focus_timer, dispatch_time(DISPATCH_TIME_NOW, delay),
                            DISPATCH_TIME_FOREVER, 0);
}

static void request_focus_check(unsigned delay_ms) {
  uint64_t deadline = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + delay_ms * NSEC_PER_MSEC;
  dispatch_async(dispatch_get_main_queue(), ^{
    if (deadline > last_focus_check) last_focus_check = deadline;
    if (!next_focus_check || deadline < next_focus_check) {
      arm_focus_timer(deadline, clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
    }
  });
}

static void focus_timer_fired(void) {
  if (!next_focus_check) return;
  uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  if (now < next_focus_check) {
    arm_focus_timer(next_focus_check, now);
    return;
  }

  if (last_focus_check > now) {
    arm_focus_timer(last_focus_check, now);
  } else {
    next_focus_check = 0;
    last_focus_check = 0;
  }
  windows_determine_and_focus_active_window(&g_windows);
}

static void window_modify_handler(uint32_t event, uint32_t* window_id, size_t _, int cid) {
  uint32_t wid = *window_id;
  struct table* windows = &g_windows;

  if (is_own_window(cid, wid)) return;

  if (event == EVENT_WINDOW_MOVE) {
    debug("Window Move: %d\n", wid);
    windows_window_move(windows, wid);
  } else if (event == EVENT_WINDOW_RESIZE) {
    debug("Window Resize: %d\n", wid);
    windows_window_update(windows, wid);
  } else if (event == EVENT_WINDOW_REORDER) {
    debug("Window Reorder (and focus): %d\n", wid);
    windows_window_update(windows, wid);
    request_focus_check(10);
  } else if (event == EVENT_WINDOW_LEVEL) {
    debug("Window Level: %d\n", wid);
    windows_window_update(windows, wid);
  } else if (event == EVENT_WINDOW_TITLE || event == EVENT_WINDOW_UPDATE) {
    debug("Window Focus\n");
    request_focus_check(50);
  } else if (event == EVENT_WINDOW_UNHIDE) {
    debug("Window Unhide: %d\n", wid);
    windows_window_unhide(windows, wid);
  } else if (event == EVENT_WINDOW_HIDE) {
    debug("Window Hide: %d\n", wid);
    windows_window_hide(windows, wid);
  } else if (event == EVENT_WINDOW_CLOSE) {
    debug("Window Close: %d\n", wid);
    windows_window_destroy(windows, wid, 0);
  }
}

static void front_app_handler() {
  debug("Window Focus\n");
  request_focus_check(50);
}

static void space_handler() {
  // Not all native-fullscreen windows have yet updated their space id...
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    windows_draw_borders_on_current_spaces(&g_windows);
  });
}

void events_register(int cid) {
  focus_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
  dispatch_source_set_event_handler(focus_timer, ^{ focus_timer_fired(); });
  dispatch_resume(focus_timer);

  void* cid_ctx = (void*)(intptr_t)cid;

  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_CLOSE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_MOVE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_RESIZE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_LEVEL, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_UNHIDE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_HIDE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_TITLE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_REORDER, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_UPDATE, cid_ctx);
  SLSRegisterNotifyProc(window_spawn_handler, EVENT_WINDOW_CREATE, cid_ctx);
  SLSRegisterNotifyProc(window_spawn_handler, EVENT_WINDOW_DESTROY, cid_ctx);

  SLSRegisterNotifyProc(space_handler, EVENT_SPACE_CHANGE, cid_ctx);

  SLSRegisterNotifyProc(front_app_handler, EVENT_FRONT_CHANGE, cid_ctx);

#ifdef DEBUG
  for (int i = 0; i < 2000; i++) {
    SLSRegisterNotifyProc(event_watcher, i, NULL);
  }
#endif
}

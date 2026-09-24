#include <pebble.h>

// 24-hour one-hand watchface for Pebble Time 2 (emery, 200x228).
// Port of SingleHanded #2 by fsargent
// (https://github.com/fsargent/pebble-slow-24h, branch one-hand-24h-design-2).

// ---- Persistent storage keys ------------------------------------------------
#define PKEY_SUNRISE          1
#define PKEY_SUNSET           2
#define PKEY_USE_12H          3
#define PKEY_RAIN_HOURS       4
#define PKEY_SHOW_RAIN        5

#define DEFAULT_SUNRISE_MIN  330    // 5:30 AM
#define DEFAULT_SUNSET_MIN   1155   // 7:15 PM
#define DEFAULT_RAIN_HOURS   0

static Window   *s_window;
static Layer    *s_canvas_layer;
static int32_t   s_sunrise_min      = DEFAULT_SUNRISE_MIN;
static int32_t   s_sunset_min       = DEFAULT_SUNSET_MIN;
static int32_t   s_sunrise_angle    = 0;
static int32_t   s_sunset_angle     = 0;
static bool      s_use_12h          = true;
static bool      s_show_rain        = true;
static uint32_t  s_rain_hours       = DEFAULT_RAIN_HOURS;

// ---- Angle helpers ----------------------------------------------------------
// Noon (12:00 local) = top = 0; midnight = bottom = TRIG_MAX_ANGLE/2
static int32_t minutes_to_angle(int local_min) {
  int adj = (local_min + 12 * 60) % (24 * 60);
  return (int32_t)((int64_t)TRIG_MAX_ANGLE * adj / (24 * 60));
}

static int32_t hour_to_angle(int h) {
  return (int32_t)((int64_t)TRIG_MAX_ANGLE * ((h + 12) % 24) / 24);
}

static bool angle_in_arc(int32_t angle, int32_t start, int32_t end) {
  angle = ((angle % TRIG_MAX_ANGLE) + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
  start = ((start % TRIG_MAX_ANGLE) + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
  end   = ((end   % TRIG_MAX_ANGLE) + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
  return (start <= end)
    ? (angle >= start && angle <= end)
    : (angle >= start || angle <= end);
}

static void update_sun_angles(void) {
  s_sunrise_angle = minutes_to_angle(s_sunrise_min);
  s_sunset_angle  = minutes_to_angle(s_sunset_min);
}

static GPoint polar(GPoint center, int r, int32_t angle) {
  return GPoint(
    center.x + (r * sin_lookup(angle)) / TRIG_MAX_RATIO,
    center.y - (r * cos_lookup(angle)) / TRIG_MAX_RATIO
  );
}

// Fill an arc of the given ring width, handling wrap past 0.
static void fill_arc(GContext *ctx, GRect rect, int width, int32_t a0, int32_t a1) {
  if (a0 <= a1) {
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, width, a0, a1);
  } else {
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, width, a0, TRIG_MAX_ANGLE);
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, width, 0, a1);
  }
}

// Tapered hand: pointed tip, widest at the center, short tail.
static void draw_hand(GContext *ctx, GPoint center, int32_t angle,
                      int len, int tail_len, int base_half) {
  int32_t perp = angle + TRIG_MAX_ANGLE / 4;
  GPoint tip    = polar(center, len, angle);
  GPoint base_l = polar(center, base_half, perp);
  GPoint base_r = polar(center, -base_half, perp);
  GPoint tail   = polar(center, -tail_len, angle);

  GPoint pts[] = { tip, base_l, tail, base_r };
  GPath *path = gpath_create(&(GPathInfo){ .num_points = 4, .points = pts });
  graphics_context_set_fill_color(ctx, GColorBlack);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, center, tip);
}

// ---- Drawing ----------------------------------------------------------------
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect  bounds = layer_get_bounds(layer);
  int    size   = bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h;
  int    radius = size / 2;
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  // Largest circle that fits the (rectangular) screen, centered.
  GRect  dial   = GRect(center.x - radius, center.y - radius, size, size);
  int    ring_width = 38;

  int32_t sunrise_angle = s_sunrise_angle;
  int32_t sunset_angle  = s_sunset_angle;

  time_t     now   = time(NULL);
  struct tm *local = localtime(&now);

  // White face (whole screen, so the bands above/below the dial match).
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Night arc, outer ring only.
  graphics_context_set_fill_color(ctx, GColorBlack);
  fill_arc(ctx, dial, ring_width, sunset_angle, sunrise_angle);

  // Numerals along the outer edge.
  GFont num_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  for (int i = 0; i < 24; i++) {
    int32_t angle = hour_to_angle(i);
    GPoint  pos   = polar(center, radius - 8, angle);

    bool   is_day  = angle_in_arc(angle, sunrise_angle, sunset_angle);
    GColor fg      = is_day ? GColorBlack : GColorWhite;
    GColor outline = is_day ? GColorWhite : GColorBlack;

    int display_h = s_use_12h ? (i % 12 == 0 ? 12 : i % 12) : i;
    char num_str[4];
    snprintf(num_str, sizeof(num_str), "%d", display_h);
    GRect text_rect = GRect(pos.x - 10, pos.y - 8, 20, 16);

    graphics_context_set_text_color(ctx, outline);
    for (int dx = -1; dx <= 1; dx++) {
      for (int dy = -1; dy <= 1; dy++) {
        if (dx == 0 && dy == 0) continue;
        GRect r = GRect(text_rect.origin.x + dx, text_rect.origin.y + dy,
                        text_rect.size.w, text_rect.size.h);
        graphics_draw_text(ctx, num_str, num_font, r,
                           GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
      }
    }
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, num_str, num_font, text_rect,
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

  // Tick marks inside the numerals, every 15 minutes (96 ticks per day).
  int tick_base = radius - 18;
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < 96; i++) {
    int32_t angle    = minutes_to_angle(i * 15);
    bool    on_hour  = (i % 4 == 0);
    bool    on_half  = (i % 2 == 0);
    int     tick_len = on_hour ? 14 : on_half ? 10 : 5;

    bool is_day = angle_in_arc(angle, sunrise_angle, sunset_angle);
    graphics_context_set_stroke_color(ctx, is_day ? GColorBlack : GColorWhite);
    graphics_draw_line(ctx, polar(center, tick_base, angle),
                            polar(center, tick_base - tick_len, angle));
  }

  // Rain overlay: 10px ring inside the tick marks.
  if (s_show_rain) {
    int   rain_width  = 10;
    GRect rain_bounds = grect_inset(dial, GEdgeInsets(radius - tick_base + 14));
    graphics_context_set_fill_color(ctx, GColorPictonBlue);
    for (int rh = 0; rh < 24; rh++) {
      if (!(s_rain_hours & (1 << rh))) continue;
      int run_start = rh;
      while (rh < 23 && (s_rain_hours & (1 << (rh + 1)))) rh++;
      fill_arc(ctx, rain_bounds, rain_width, hour_to_angle(run_start), hour_to_angle(rh + 1));
    }
  }

  // AM / PM labels.
  GFont label_font   = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  int   label_y      = center.y - 7;
  int   label_offset = (radius - ring_width) * 2 / 3;
  GRect am_rect = GRect(center.x - label_offset - 12, label_y, 24, 16);
  GRect pm_rect = GRect(center.x + label_offset - 12, label_y, 24, 16);

  bool left_day  = angle_in_arc(TRIG_MAX_ANGLE * 3 / 4, sunrise_angle, sunset_angle);
  bool right_day = angle_in_arc(TRIG_MAX_ANGLE / 4,     sunrise_angle, sunset_angle);

  graphics_context_set_text_color(ctx, left_day ? GColorDarkGray : GColorLightGray);
  graphics_draw_text(ctx, "AM", label_font, am_rect,
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, right_day ? GColorDarkGray : GColorLightGray);
  graphics_draw_text(ctx, "PM", label_font, pm_rect,
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  // Single 24h hand, one rotation per day, tip at the tick ring.
  draw_hand(ctx, center, minutes_to_angle(local->tm_hour * 60 + local->tm_min),
            tick_base, 15, 4);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 5);
}

// ---- AppMessage -------------------------------------------------------------
static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  bool sun_changed = false;

  if ((t = dict_find(iter, MESSAGE_KEY_SUNRISE))) {
    s_sunrise_min = t->value->int32;
    persist_write_int(PKEY_SUNRISE, s_sunrise_min);
    sun_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SUNSET))) {
    s_sunset_min = t->value->int32;
    persist_write_int(PKEY_SUNSET, s_sunset_min);
    sun_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_USE_12H))) {
    s_use_12h = (t->value->int32 != 0);
    persist_write_bool(PKEY_USE_12H, s_use_12h);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_RAIN))) {
    s_show_rain = (t->value->int32 != 0);
    persist_write_bool(PKEY_SHOW_RAIN, s_show_rain);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_RAIN_HOURS))) {
    s_rain_hours = (uint32_t)t->value->int32;
    persist_write_int(PKEY_RAIN_HOURS, (int32_t)s_rain_hours);
  }

  if (sun_changed) update_sun_angles();
  layer_mark_dirty(s_canvas_layer);
}

// ---- Tick -------------------------------------------------------------------
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(s_canvas_layer);
}

// ---- Window -----------------------------------------------------------------
static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root, s_canvas_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
}

// ---- Init -------------------------------------------------------------------
static void prv_init(void) {
  if (persist_exists(PKEY_SUNRISE))          s_sunrise_min      = persist_read_int(PKEY_SUNRISE);
  if (persist_exists(PKEY_SUNSET))           s_sunset_min       = persist_read_int(PKEY_SUNSET);
  if (persist_exists(PKEY_USE_12H))          s_use_12h          = persist_read_bool(PKEY_USE_12H);
  if (persist_exists(PKEY_SHOW_RAIN))        s_show_rain        = persist_read_bool(PKEY_SHOW_RAIN);
  if (persist_exists(PKEY_RAIN_HOURS))       s_rain_hours       = (uint32_t)persist_read_int(PKEY_RAIN_HOURS);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  update_sun_angles();

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 128);
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

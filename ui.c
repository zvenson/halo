/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). HALO, see LICENSE. */
/* ui.c – the HALO display (480 x 320) and the lights. Design 0.2 (12.11.2026, Sven: "nicht sooo fresh – Schrift klarer"):
   the logo's colours (cyan, violet, mint on a deep blue-black), labels in semibold and light, values big and white, the
   four values as cards, no small grey hint lines. Top: the halo glyph + HALO, the area, status pills, the input, LEVEL /
   SPACE (encoders A, B). Second line: the sub-pages as tabs, the program on the right. Middle: the page. Then the four
   value cards (encoders 1-4), the soft keys F1-F6.
   A POPUP asks for the microphone while the input is off (HALO is nothing without it) – RETURN or OK closes it.
   A QUIET display (as VALVE, 9.11.2026): the panel's SPI crackles into a microphone's analog path, so nothing moves with
   the sound at frame rate – every live value is sampled 4x a second in coarse steps; GLOBAL METERS OFF = a still picture. */
#include "halo.h"

#define BG 0x07090E                   /* deep blue-black */
#define SURF 0x11151F                 /* cards */
#define SURF2 0x1A2030                /* raised */
#define LINE 0x252C3D
#define LABEL 0xA9B3C8                /* labels: light, never grey-on-grey */
#define FG 0xF3F6FC
#define MUTED 0x68728C
#define INK 0x0A0D14                  /* text on a lit fill */
#define ACC 0x8BE9FF                  /* cyan – the logo's letters */
#define VIOLET 0xB9A2FF               /* the logo's ring */
#define MINT 0x74F0B4                 /* the logo's wave */
#define AMBER 0xFFC857
#define CORAL 0xFF7A6B
#define BLUE 0x6FA8FF
#define RED 0xFF5A4D
#define CW 472
#define MID 240
#define TOP 74                        /* the page starts under the tabs */
#define FO_POPUP 330
#define FO_ENG 340                    /* clicks: an engine card */
#define FO_KEY 360                    /* … a note of the circle = the key */
#define FO_RESP 380                   /* … the RESPOND card */
#define FO_WPAD 400                   /* … a WANDER cell = that program */

static const char *const PAGE_N[P_COUNT] = { "LISTEN", "ENGINE", "TONE", "SPACE", "MOTION", "WANDER", "PRESETS", "GLOBAL" };
static const uint32_t KEY_C[6] = { MINT, ACC, AMBER, BLUE, VIOLET, CORAL };
static const char *const NOTE_N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static float q(float v, int steps) { return (float)(int)(v * steps + 0.5f) / steps; }
static float lvl01(float db) { return vd_clamp(1 + db / 72, 0, 1); }

static struct { float in, floor, thr, act, pres, swell, bright, out, heard, fill; int clip, guard, onsets; } D;
static void sample_live(void) {
  static double last; static int last_on; double now = v_ms();
  if (now - last < 250 && now >= last) return;
  last = now;
  if (V.meters < 0.5f) { D.in = D.act = D.pres = D.swell = D.out = 0; D.clip = 0; D.heard = 0; }
  else {
    D.in = q(lvl01(V.in_db), 24); D.floor = q(lvl01(V.floor_db), 48); D.thr = q(lvl01(V.floor_db + V.thr_db), 48);
    D.act = q(V.act, 10); D.pres = q(V.presence, 10); D.swell = q(V.swell, 10); D.bright = q(V.bright, 5);
    D.out = q(lvl01(lin2db(V.out_peak)), 12); D.clip = now < V.clip_until;
    D.heard = V.now < V.heard_until ? (float)(int)(V.heard + 0.5f) : 0;
  }
  D.fill = q(V.cloud_fill, 20); D.guard = V.now < V.guard_until;
  D.onsets = V.onsets - last_on; last_on = V.onsets;
}
static char *cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; return d; }
static uint32_t page_colour(void) { return V.page < 6 ? KEY_C[V.page] : ACC; }
static void note_name(float m, char *b) { int n = (int)(m + 0.5f); char o[6]; b[0] = 0; cat(b, NOTE_N[((n % 12) + 12) % 12]); cat(b, vg_itoa(n / 12 - 1, o)); }

/* the logo's ring: a flat ellipse */
static void ring(float cx, float cy, float rx, float ry, float w, uint32_t c) {
  float px = cx + rx, py = cy;
  for (int i = 1; i <= 28; i++) { float t = i / 28.0f, x = cx + rx * vd_sin(t + 0.25f), y = cy + ry * vd_sin(t); vg_line(px, py, x, y, w, c); px = x; py = y; }
}
/* a pill with text */
static int pill(int x, int y, const char *t, uint32_t fill, uint32_t ink) {
  int w = vg_text_width(t, VG_BOLD) + 18; vg_round(x, y, w, 20, 10, fill); vg_text(t, x + w / 2, y + 3, VG_BOLD, ink, VG_CENTER); return w;
}
/* a level bar with round ends */
static void meter_bar(int x, int y, int w, int h, float v, uint32_t c) {
  vg_round(x, y, w, h, h / 2, SURF2);
  int f = (int)(w * vd_clamp(v, 0, 1)); if (f >= h) vg_round(x, y, f, h, h / 2, c);
}

static void header(void) {
  ring(31, 9, 19, 4, 1.6f, VIOLET);
  vg_text("HALO", 12, 13, VG_MED, ACC, VG_LEFT);
  vg_circle(66, 21, 3, page_colour());
  vg_text(PAGE_N[V.page], 75, 13, VG_MED, FG, VG_LEFT);
  vf_key(VB_K1 + (V.page < 6 ? (V.page + 1) % 6 : 0), 8, 4, 150, 30);
  int x = 168;
  if (V.freeze) x += pill(x, 12, "FREEZE", ACC, INK) + 5;
  if (D.guard) x += pill(x, 12, "GUARD", CORAL, INK) + 5;
  if (halo_hush()) x += pill(x, 12, "HUSH", AMBER, INK) + 5;
  /* the input: a small meter, or a red OFF */
  int ix = 276;
  if (V.input_on) { static const char *const SRC_N[] = { "IN 1", "IN 2", "IN 1+2" };
    vg_text(SRC_N[(int)(V.src + 0.5f) % 3], ix, 15, VG_BOLD, LABEL, VG_LEFT);
    meter_bar(ix + 36, 19, 44, 6, D.in, D.in > 0.95f ? RED : D.in > D.thr ? MINT : MUTED); }
  else pill(ix, 12, "MIC OFF", RED, INK);
  { char b[12], n[8]; b[0] = 0; cat(b, vg_itoa((int)(V.s.level * 100 + 0.5f), n));
    vg_text("LVL", 396, 15, VG_BOLD, MUTED, VG_RIGHT); vg_text(b, 418, 15, VG_BOLD, FG, VG_RIGHT);
    b[0] = 0; cat(b, vg_itoa((int)(V.s.mix * 100 + 0.5f), n));
    vg_text("SPC", 448, 15, VG_BOLD, MUTED, VG_RIGHT); vg_text(b, 470, 15, VG_BOLD, FG, VG_RIGHT);
    vf_encoder(VE_A, 366, 8, 56, 26); vf_encoder(VE_B, 424, 8, 52, 26); }
}

/* the sub-pages as tabs (clickable), the program on the right */
static void tabs(void) {
  uint32_t col = page_colour();
  int n = page_subs(V.page), cx = 8;
  if (n > 1) {
    const char *const *names = page_sub_names(V.page);
    for (int k = 0; k < n; k++) {
      int w = vg_text_width(names[k], VG_BOLD) + 26, on = k == V.sub;
      vg_round(cx, 42, w, 22, 11, on ? col : SURF); vf_button(FO_CHIP + k, cx, 42, w, 22);
      vg_text(names[k], cx + w / 2, 46, VG_BOLD, on ? INK : LABEL, VG_CENTER);
      cx += w + 6;
    }
  }
  if (V.page >= 6) return;
  char b[40] = "", t[8]; cat(b, vg_itoa(V.preset + 1, t)); cat(b, "  "); cat(b, prog_name(V.preset));
  int w = vg_text_width(b, VG_BOLD) + 30, x = CW - w;
  vg_round(x, 42, w, 22, 11, SURF); vg_circle(x + 12, 53, 4, prog_colour(V.preset));
  vg_text(b, x + 21, 46, VG_BOLD, FG, VG_LEFT);
}

static void softkeys(void) {
  static const char *const F[6] = { "FREEZE", "PRESETS", "PRG -", "PRG +", "SAVE", "GLOBAL" };
  int on[6] = { V.freeze, V.page == P_PRESETS, 0, 0, V.save_held, V.page == P_GLOBAL };
  for (int i = 0; i < 6; i++) {
    const char *l = V.shift && i == 4 ? "FACTORY" : F[i];
    int x = 8 + i * 78;
    vg_round(x, 286, 74, 28, 6, on[i] ? ACC : SURF); vf_key(VB_F1 + i, x, 286, 74, 28);
    vg_text(l, x + 37, 292, VG_BOLD, on[i] ? INK : FG, VG_CENTER);
  }
}

/* the four values as cards: label, the value big, a bar */
static void params_row(void) {
  v_param p[4]; params_get(p);
  uint32_t c = page_colour();
  for (int i = 0; i < 4; i++) {
    int x = 8 + i * 117, y = 202;
    vg_round(x, y, 112, 76, 8, SURF);
    if (!p[i].label) continue;
    vf_encoder(VE_1 + i, x, y, 112, 76);
    char n[2] = { (char)('1' + i), 0 };
    vg_text(n, x + 10, y + 8, VG_BOLD, c, VG_LEFT);
    vg_text(p[i].label, x + 56, y + 8, VG_BOLD, LABEL, VG_CENTER);
    vg_text(p[i].text, x + 56, y + 29, VG_BIG, FG, VG_CENTER);
    meter_bar(x + 10, y + 62, 92, 5, p[i].value, c);
  }
}

static void toast_draw(void) {
  if (!V.toast[0] || v_ms() > V.toast_until) return;
  int w = vg_text_width(V.toast, VG_MED) + 40;
  vg_round(240 - w / 2, 150, w, 34, 17, ACC);
  vg_text(V.toast, 240, 158, VG_MED, INK, VG_CENTER);
}

/* the microphone popup: HALO is nothing without it */
static int popup_shown(void) { return !V.input_on && !V.popup_off && v_ms() > 1200; }
static void popup(void) {
  vg_rect_a(0, 0, VIBE_SCREEN_W, VIBE_SCREEN_H, BG, 0.82f);
  int x = 40, y = 44, w = 400, h = 228;
  vg_round(x - 2, y - 2, w + 4, h + 4, 16, VIOLET);
  vg_round(x, y, w, h, 14, SURF);
  ring(MID, y + 30, 54, 10, 3, VIOLET);
  /* a microphone */
  vg_round(MID - 9, y + 36, 18, 30, 9, ACC);
  vg_line(MID - 15, y + 54, MID - 15, y + 60, 2.5f, ACC); vg_line(MID + 15, y + 54, MID + 15, y + 60, 2.5f, ACC);
  vg_line(MID - 15, y + 60, MID, y + 72, 2.5f, ACC); vg_line(MID + 15, y + 60, MID, y + 72, 2.5f, ACC);
  vg_line(MID, y + 72, MID, y + 80, 2.5f, ACC);
  vg_text("TURN ON THE MICROPHONE", MID, y + 92, VG_BIG, FG, VG_CENTER);
  vg_text("HALO is played by the room \xE2\x80\x93 it has to hear it.", MID, y + 124, VG_MED, LABEL, VG_CENTER);
  vg_text("Browser: click  AUDIO INPUT  at the top", MID, y + 150, VG_BOLD, ACC, VG_CENTER);
  vg_text("VIBE: press ENTER \xC2\xB7 no mic: the keys sing", MID, y + 170, VG_BOLD, MUTED, VG_CENTER);
  vg_round(MID - 50, y + 192, 100, 26, 13, ACC); vf_button(FO_POPUP, MID - 50, y + 192, 100, 26);
  vg_text("OK", MID, y + 197, VG_BOLD, INK, VG_CENTER);
}
int ui_popup_press(int id) {                            /* the display's own clicks (SDK 18) */
  if (id == FO_POPUP) { V.popup_off = 1; vibe_request(VIBE_REQ_AUDIO_INPUT); return 1; }
  if (id >= FO_ENG && id < FO_ENG + EN_COUNT) { V.s.engine = (float)(id - FO_ENG); return 1; }
  if (id >= FO_KEY && id < FO_KEY + 12) { V.s.key = (float)(id - FO_KEY); return 1; }
  if (id == FO_RESP) { V.s.respond = (float)(((int)(V.s.respond + 0.5f) + 1) % RS_COUNT); return 1; }
  if (id >= FO_WPAD && id < FO_WPAD + N_PRESETS) { ui_go_program(id - FO_WPAD); return 1; }
  return 0;
}

/* ---------------------------------------------------------------- pages ----------------------------------------- */
static void bar_row(int y, const char *label, float v, uint32_t col) {
  vg_text(label, 16, y, VG_BOLD, LABEL, VG_LEFT);
  meter_bar(104, y + 3, 186, 10, v, col);
  char b[8], t[8]; b[0] = 0; cat(b, vg_itoa((int)(v * 100 + 0.5f), t)); vg_text(b, 318, y, VG_BOLD, FG, VG_RIGHT);
}
static void page_listen(void) {
  vg_round(8, TOP, 316, 120, 10, SURF);
  if (!V.input_on) {
    vg_text("MIC OFF", 166, TOP + 34, VG_BIG, RED, VG_CENTER);
    vg_text("ENTER or AUDIO INPUT", 166, TOP + 68, VG_BOLD, LABEL, VG_CENTER);
  } else {
    /* the room: input against the floor (white) and the threshold (amber) */
    int x0 = 104, w = 210, y = TOP + 12;
    vg_text("ROOM", 16, y + 2, VG_BOLD, LABEL, VG_LEFT);
    meter_bar(x0, y + 2, w, 14, D.in, D.in > D.thr ? MINT : MUTED);
    vg_rect(x0 + (int)(w * D.floor) - 1, y - 2, 3, 22, FG);
    vg_rect(x0 + (int)(w * D.thr) - 1, y - 2, 3, 22, AMBER);
    bar_row(TOP + 44, "PRESENCE", D.pres, MINT);
    bar_row(TOP + 68, "DRONE", D.swell, ACC);
    bar_row(TOP + 92, "BRIGHT", D.bright, AMBER);
  }
  /* what it heard: the note, big */
  vg_round(332, TOP, 140, 120, 10, SURF);
  vg_text("HEARD", 402, TOP + 10, VG_BOLD, LABEL, VG_CENTER);
  if (D.heard > 0) { char n[8]; note_name(D.heard, n); vg_text(n, 402, TOP + 32, VG_HUGE, FG, VG_CENTER); }
  else vg_text("-", 402, TOP + 32, VG_HUGE, LINE, VG_CENTER);
  if (D.guard) pill(372, TOP + 94, "GUARD", CORAL, INK);
  else if (D.onsets > 0) pill(372, TOP + 94, "ONSET", MINT, INK);
}

static const char *const ENGINE_T[EN_COUNT] = {
  "Strings, set ringing by the room",
  "Grains of what it heard, sung in key",
  "A choir that follows your hum",
  "Every sound strikes a bell" };
static void engine_pic(int e, int x, int y, int w, int h, uint32_t c) {      /* a little picture of each engine */
  switch (e) {
    case EN_BLOOM: for (int k = 0; k < 5; k++) { float yy = y + 6 + k * (h - 12) / 4.0f; vg_line(x + 10, yy, x + w - 10, yy + (k & 1 ? 2 : -2), 1.6f, c); } break;
    case EN_CLOUD: for (int k = 0; k < 16; k++) { uint32_t s = (uint32_t)(k + 3) * 2654435761u; vg_circle(x + 10 + (int)(s % (uint32_t)(w - 20)), y + 6 + (int)((s >> 11) % (uint32_t)(h - 12)), 1.5f + (k % 3), c); } break;
    case EN_CHOIR: for (int k = 0; k < 5; k++) { int bh = 8 + (k * 9 + 5) % 22, bx = x + 14 + k * (w - 28) / 4; vg_round(bx - 5, y + h - 4 - bh, 10, bh, 5, c); } break;
    case EN_CHIMES: for (int k = 0; k < 5; k++) { int bx = x + 16 + k * (w - 32) / 4, len = 6 + (k * 7) % 16; vg_rect(bx, y + 2, 1, len, c); vg_round(bx - 4, y + 2 + len, 9, 12, 4, c); } break;
  }
}
static void page_engine(void) {
  int e = (int)(V.s.engine + 0.5f);
  for (int i = 0; i < EN_COUNT; i++) {
    int x = 8 + i * 117, y = TOP, w = 112, h = 92, on = e == i;
    if (on) { vg_round(x, y, w, h, 10, ACC); vg_round(x + 2, y + 2, w - 4, h - 4, 9, SURF2); }
    else vg_round(x, y, w, h, 10, SURF);
    vf_button(FO_ENG + i, x, y, w, h);
    engine_pic(i, x + 4, y + 8, w - 8, 44, on ? ACC : MUTED);
    vg_text(ENGINE_N[i], x + w / 2, y + 62, VG_MED, on ? FG : LABEL, VG_CENTER);
  }
  vg_text(ENGINE_T[e], MID, TOP + 102, VG_MED, FG, VG_CENTER);
}

/* TONE: the twelve notes in a circle of fifths – the scale lit, the chord amber, the root cyan, what was heard ringed */
static void page_tone(void) {
  vg_round(8, TOP, 190, 120, 10, SURF);
  int cx = 103, cy = TOP + 60, R = 46;
  int key = (int)(V.s.key + 0.5f), sc = (int)(V.s.scale + 0.5f);
  int chord[12] = { 0 }; for (int j = 0; j < N_HARM; j++) chord[(((int)(V.harm[j] + 0.5f)) % 12 + 12) % 12] = 1;
  int heard = D.heard > 0 ? (((int)D.heard) % 12 + 12) % 12 : -1;
  for (int i = 0; i < 12; i++) {
    int pc = (i * 7) % 12;
    float ph = i / 12.0f, x = cx + vd_sin(ph) * R, y = cy - vd_sin(ph + 0.25f) * R;
    int in = scale_has(sc, pc - key), root = pc == V.chord_pc;
    if (heard == pc) vg_circle(x, y, 13, MINT);
    vf_button(FO_KEY + pc, (int)x - 12, (int)y - 12, 24, 24);
    vg_circle(x, y, 10.5f, root ? ACC : chord[pc] ? AMBER : in ? SURF2 : SURF);
    vg_text(NOTE_N[pc], (int)x, (int)y - 7, VG_BOLD, root || chord[pc] ? INK : in ? FG : MUTED, VG_CENTER);
  }
  vg_text(NOTE_N[V.chord_pc], cx, cy - 12, VG_BIG, ACC, VG_CENTER);
  /* the chord as notes */
  vg_round(206, TOP, 266, 120, 10, SURF);
  vg_text("CHORD", 220, TOP + 10, VG_BOLD, LABEL, VG_LEFT);
  { char b[24] = ""; cat(b, NOTE_N[key]); cat(b, " "); cat(b, scale_name(sc)); vg_text(b, 460, TOP + 10, VG_BOLD, AMBER, VG_RIGHT); }
  for (int j = 0; j < N_HARM; j++) { char n[8]; note_name(V.harm[j], n); vg_text(n, 220 + (j % 3) * 82, TOP + 34 + (j / 3) * 30, VG_BIG, j == 1 ? ACC : FG, VG_LEFT); }
  char b[48] = "", t[8];
  if (V.s.follow > 0.01f) { cat(b, "FOLLOW "); cat(b, vg_itoa((int)(V.s.follow * 100 + 0.5f), t)); cat(b, "%"); } else cat(b, "FOLLOW OFF");
  if (V.s.move > 0.01f) { cat(b, "  \xC2\xB7  MOVE "); cat(b, vg_itoa((int)(V.s.move * 100 + 0.5f), t)); cat(b, "%"); }
  vg_text(b, 220, TOP + 98, VG_BOLD, LABEL, VG_LEFT);
}

static void page_space(void) {
  int x0 = 8, y0 = TOP, w = CW - 8, h = 120;
  vg_round(x0, y0, w, h, 10, SURF);
  float t60 = 2 + V.s.decay * V.s.decay * 58, px = x0 + 14, py = y0 + h - 12;
  for (int i = 1; i <= 64; i++) {
    float tt = i / 64.0f * 30, lv = vd_exp2(-tt / t60 * 9.97f / 3.0f);
    float x = x0 + 14 + (w - 28) * i / 64.0f, y = y0 + h - 12 - (h - 40) * lv * (0.25f + 0.75f * V.s.mix);
    vg_line(px, py, x, y, 2.5f, BLUE); px = x; py = y;
  }
  if (V.s.shimmer > 0.01f) for (int i = 0; i < 6; i++) vg_circle(x0 + 60 + i * 64, y0 + h - 34 - i * 9 * V.s.shimmer, 2 + V.s.shimmer * 3, VIOLET);
  { char b[24] = "", t[8]; cat(b, t60 < 10 ? vg_ftoa(t60, 1, t) : vg_itoa((int)t60, t)); cat(b, " s");
    vg_text("DECAY", w - 6, y0 + 10, VG_BOLD, LABEL, VG_RIGHT); vg_text(b, w - 6, y0 + 28, VG_BIG, FG, VG_RIGHT); }
  if (V.s.shimmer > 0.01f) vg_text("SHIMMER", 22, y0 + 10, VG_BOLD, VIOLET, VG_LEFT);
}

static void page_motion(void) {
  int x0 = 8, y0 = TOP, w = 230, h = 120;
  vg_round(x0, y0, w, h, 10, SURF);
  int rs = (int)(V.s.respond + 0.5f); float idle = V.s.idle;
  float y_a = rs == RS_SWELL ? idle : 1, y_b = rs == RS_CALM ? idle : 1;
  int gx = x0 + 16, gw = w - 32, gy = y0 + 14, gh = h - 44;
  vg_line(gx, gy + gh - gh * y_a, gx + gw, gy + gh - gh * y_b, 3, VIOLET);
  vg_circle(gx + gw * D.pres, gy + gh - gh * (y_a + (y_b - y_a) * D.pres), 6, FG);
  vg_text("QUIET", gx, y0 + h - 22, VG_BOLD, MUTED, VG_LEFT); vg_text("BUSY", gx + gw, y0 + h - 22, VG_BOLD, MUTED, VG_RIGHT);
  static const char *const T[RS_COUNT] = { "busier = fuller", "busier = softer", "always there" };
  static const char *const N[RS_COUNT] = { "SWELL", "CALM", "STEADY" };
  vg_round(246, y0, CW - 246, h, 10, SURF); vf_button(FO_RESP, 246, y0, CW - 246, h);
  vg_text(N[rs], 262, y0 + 12, VG_BIG, VIOLET, VG_LEFT);
  vg_text(T[rs], 262, y0 + 42, VG_MED, FG, VG_LEFT);
  char b[40] = "RISE ", t[8]; cat(b, vg_ftoa(V.s.rise, 1, t)); cat(b, " s  \xC2\xB7  FALL "); cat(b, vg_itoa((int)(V.s.fall + 0.5f), t)); cat(b, " s");
  vg_text(b, 262, y0 + 74, VG_BOLD, LABEL, VG_LEFT);
  b[0] = 0; cat(b, "IDLE "); cat(b, vg_itoa((int)(idle * 100 + 0.5f), t)); cat(b, " %");
  vg_text(b, 262, y0 + 94, VG_BOLD, LABEL, VG_LEFT);
}

static void page_wander(void) {
  int a = (int)(V.from + 0.5f), b = (int)(V.to + 0.5f); if (a > b) { int t = a; a = b; b = t; }
  for (int i = 0; i < N_PRESETS; i++) {
    int x = 8 + (i % 8) * 58, y = TOP + (i / 8) * 34, in = i >= a && i <= b, on = V.preset == i;
    vg_round(x, y, 54, 30, 8, on ? prog_colour(i) : in ? SURF2 : SURF); vf_button(FO_WPAD + i, x, y, 54, 30);
    char t[4]; vg_text(vg_itoa(i + 1, t), x + 27, y + 7, VG_BOLD, on ? INK : in ? FG : MUTED, VG_CENTER);
  }
  char s[64] = "";
  if (V.wander > 0.5f) {
    double left = V.every * 60000.0 - (v_ms() - V.wander_t); int m = (int)(left / 60000.0) + 1; char t[8];
    cat(s, "NEXT IN "); cat(s, vg_itoa(m < 1 ? 1 : m, t)); cat(s, " MIN");
  } else cat(s, "WANDER OFF");
  vg_text(s, MID, TOP + 84, VG_BIG, V.wander > 0.5f ? CORAL : MUTED, VG_CENTER);
}

static void page_presets(void) {
  for (int i = 0; i < N_PRESETS; i++) {
    int x = 8 + (i % 4) * 117, y = 44 + (i / 4) * 38, on = V.preset == i;
    uint32_t c = prog_colour(i);
    vg_round(x, y, 112, 32, 8, on ? c : SURF); vf_button(FO_PRESET + i, x, y, 112, 32);
    if (!on) vg_circle(x + 12, y + 16, 4, c);
    vg_text(prog_name(i), x + 60, y + 9, VG_BOLD, on ? INK : FG, VG_CENTER);
    if (V.prog_user & (1 << i)) vg_circle(x + 104, y + 7, 2.5f, on ? INK : AMBER);
  }
}

static void page_global(void) {
  vg_round(8, 44, CW - 8, 150, 10, SURF);
  if (V.sub) {                                           /* the SAVER sub-page (F6 again) */
    static const char *const S[] = { "SAVER", "after AFTER minutes untouched: a flight", "", "through rooms that hear the mic",
                                     "REACT", "how much the sound moves it", "FPS", "fewer pictures = less display noise" };
    for (int i = 0; i < 4; i++) { vg_text(S[i * 2], 24, 60 + i * 32, VG_BOLD, ACC, VG_LEFT); vg_text(S[i * 2 + 1], 150, 60 + i * 32, VG_MED, FG, VG_LEFT); }
    return;
  }
  static const char *const L[] = { "STOP \xC2\xB7 PLAY", "hush (fade out) \xC2\xB7 back", "PEDAL", "freeze \xC2\xB7 expression = level",
                                   "METERS OFF", "a still picture (no display noise)", "GUARD", "ducks the mic when a howl starts" };
  for (int i = 0; i < 4; i++) { vg_text(L[i * 2], 24, 60 + i * 32, VG_BOLD, ACC, VG_LEFT); vg_text(L[i * 2 + 1], 150, 60 + i * 32, VG_MED, FG, VG_LEFT); }
}

void ui_draw(void) {
  sample_live();
  vg_clear(BG);
  vf_begin();
  if (popup_shown()) {                                   /* the popup takes the clicks */
    header(); popup(); vf_end(ACC); return;
  }
  header();
  if (V.page < 6) tabs();
  switch (V.page) {
    case P_LISTEN: page_listen(); break;
    case P_ENGINE: page_engine(); break;
    case P_TONE: page_tone(); break;
    case P_SPACE: page_space(); break;
    case P_MOTION: page_motion(); break;
    case P_WANDER: page_wander(); break;
    case P_PRESETS: page_presets(); break;
    case P_GLOBAL: page_global(); break;
  }
  params_row();
  toast_draw();
  softkeys();
  vf_end(ACC);
}

void ui_lights(void) {
  for (int i = 0; i < 6; i++) vibe_led_button(VB_K1 + i, KEY_C[i], V.page == i ? 1.0f : 0.16f);
  vibe_led_button(VB_F1, ACC, V.freeze ? 1 : 0.15f);
  vibe_led_button(VB_F2, ACC, V.page == P_PRESETS ? 1 : 0.15f);
  vibe_led_button(VB_F3, ACC, 0.15f); vibe_led_button(VB_F4, ACC, 0.15f);
  vibe_led_button(VB_F5, RED, V.save_held ? 1 : 0.15f);
  vibe_led_button(VB_F6, ACC, V.page == P_GLOBAL ? 1 : 0.15f);
  vibe_led_button(VB_SHIFT, ACC, V.shift ? 1 : 0);
  vibe_led_button(VB_REC, ACC, V.freeze ? 1 : 0.1f);
  vibe_led_button(VB_STOP, AMBER, halo_hush() ? 1 : 0.1f);
  vibe_led_button(VB_PLAY, MINT, halo_hush() ? 0.3f : 0.8f);
  vibe_led_button(VB_ENTER, V.input_on ? MINT : RED, V.input_on ? (V.page == P_LISTEN ? 1 : 0.15f) : ((int)(v_ms() / 400) & 1));
  for (int i = 0; i < N_PRESETS; i++) vibe_led_pad(i, prog_colour(i), V.preset == i ? 1.0f : 0.12f);
}

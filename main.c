/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). HALO, see LICENSE. */
/* main.c – HALO: events, pages and their encoders, programs on the pads, storage, the audio chain.
   Colour keys K1-K6 = the areas LISTEN ENGINE TONE SPACE MOTION WANDER; the area's key AGAIN = its next sub-page (chips in
   the second header line). Never more than four values, always on encoders 1-4, no SHIFT layer (as VALVE).
   F1 FREEZE (tap = on/off, hold = only while held) · F2 PRESETS · F3 PRG − · F4 PRG + · F5 SAVE (hold + pad = save there,
   SHIFT: factory sound) · F6 GLOBAL. A = LEVEL and B = SPACE (mix) on every page, DATA = the program.
   STOP = HUSH (everything fades out, for the shop's closing time), PLAY = back. REC = FREEZE as well.
   Pads 1-16 = programs (tap = keep, hold = only while held); a program change MORPHS (GLOBAL: 0.1 .. 30 s).
   Keys / MIDI notes = a test voice INTO the input (no microphone at hand? sing with the keys). ENTER = switch the input on.
   Two flows (Protokoll 6.2): the audio runs in its own thread on the VIBE; the control flow writes single floats (the
   sound, the globals, the test voice's note), the audio writes what the ears found. */
#include "halo.h"

VIBE_PARALLEL_AUDIO
halo_state V;
static int hush;
double v_ms(void) { return V.now / V.sr * 1000.0; }
void toast(const char *t) { int i = 0; for (; t[i] && i < 39; i++) V.toast[i] = t[i]; V.toast[i] = 0; V.toast_until = v_ms() + 1600; }
static char *cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; return d; }

/* ---------------------------------------------------------------- programs -------------------------------------- */
static const char *const PRESET_N[N_PRESETS] = {
  "SYMPATHY", "TANPURA", "GLASS", "DEEP", "MURMUR", "FLOCK", "TIDE", "STATION",
  "CATHEDRAL", "BREATH", "AURORA", "CHANT", "WIND CHIMES", "TEMPLE", "RAIN", "NIGHT" };
static const uint32_t PRESET_C[N_PRESETS] = {
  0x7FD8C9, 0xF5C93F, 0x9FE3FF, 0x3F6FD0, 0xB38CFF, 0xD7A6FF, 0x5EA8FF, 0x8E9BB5,
  0xFFD9A0, 0x9FE3D0, 0x6EE7A8, 0xFFB347, 0xE6EAF2, 0xFF8A3D, 0x7FB8FF, 0x6B78C9 };
#define MAGIC 0x48414C31u                                             /* "HAL1" */
typedef struct { uint32_t magic, size, user; halo_sound s[N_PRESETS]; int8_t src[N_PRESETS]; } prg_file;
typedef struct { uint32_t magic, size; float src, in_mode, gain_db, sense, adapt, guard, out_db, pad_mode, meters, morph, keys_level, wander, every, from, to; int32_t last;
                 float saver, saver_after, saver_react, saver_fps; } glb_file;   /* (0.3: + the saver; 0.2 files still load) */
#define GLB_V02 ((uint32_t)(sizeof(glb_file) - 4 * sizeof(float)))
static prg_file P;

static void neutral(halo_sound *s) {
  *s = (halo_sound){ 0 };
  for (int e = 0; e < EN_COUNT; e++) for (int i = 0; i < 4; i++) s->ep[e][i] = EPARAM[e][i].def;
  s->engine = EN_BLOOM; s->level = 0.8f; s->drift = 0.3f;
  s->key = 2; s->scale = 0; s->octave = 1; s->follow = 0.5f; s->glide = 4; s->voicing = VC_OPEN; s->move = 0.3f;
  s->mix = 0.55f; s->decay = 0.6f; s->damp = 0.4f; s->shimmer = 0; s->size = 0.6f; s->mod = 0.4f; s->mic = 0.15f; s->width = 0.7f;
  s->respond = RS_SWELL; s->rise = 2; s->fall = 12; s->idle = 0.3f;
}
#define EP(e, a, b, c, d) do { s->ep[e][0] = a; s->ep[e][1] = b; s->ep[e][2] = c; s->ep[e][3] = d; } while (0)
/* the factory sounds: four per engine, each with its own answer to the room */
static void factory(int i, halo_sound *s) {
  neutral(s);
  switch (i) {
    case 0: s->engine = EN_BLOOM; s->key = 2; s->scale = 1; EP(EN_BLOOM, 10, 0.5f, 6, 0.3f); break;          /* SYMPATHY: D lydian strings that answer the room */
    case 1: s->engine = EN_BLOOM; s->key = 0; s->scale = 2; s->voicing = VC_FIFTHS; EP(EN_BLOOM, 22, 0.7f, 6, 0.15f);   /* TANPURA: always there, hollow fifths */
            s->follow = 0; s->move = 0; s->respond = RS_STEADY; s->idle = 0.6f; s->mix = 0.45f; break;
    case 2: s->engine = EN_BLOOM; s->key = 4; s->scale = 1; s->octave = 2; s->voicing = VC_CLOSE; EP(EN_BLOOM, 14, 0.92f, 6, 0.4f);   /* GLASS: high, bright, shimmering */
            s->shimmer = 0.5f; s->mix = 0.65f; s->decay = 0.75f; break;
    case 3: s->engine = EN_BLOOM; s->key = 9; s->scale = 4; s->octave = 0; EP(EN_BLOOM, 26, 0.18f, 6, 0.5f);   /* DEEP: low and dark, a long hall */
            s->decay = 0.9f; s->damp = 0.75f; s->size = 0.9f; s->rise = 4; s->fall = 25; break;
    case 4: s->engine = EN_CLOUD; s->key = 5; s->scale = 0; EP(EN_CLOUD, 500, 0.45f, 0.85f, 0.5f); s->mic = 0.25f; break;   /* MURMUR: voices become a choir in key */
    case 5: s->engine = EN_CLOUD; s->key = 7; s->scale = 3; EP(EN_CLOUD, 180, 0.85f, 0.5f, 0.9f); s->mix = 0.5f; s->rise = 0.8f; s->fall = 8; break;   /* FLOCK: many short grains, wide */
    case 6: s->engine = EN_CLOUD; s->key = 3; s->scale = 6; EP(EN_CLOUD, 1300, 0.25f, 0.9f, 0.3f);      /* TIDE: slow swells, softer when it is busy */
            s->respond = RS_CALM; s->rise = 3; s->fall = 30; s->idle = 0.3f; s->decay = 0.85f; s->mix = 0.7f; break;
    case 7: s->engine = EN_CLOUD; s->key = 11; s->scale = 2; EP(EN_CLOUD, 700, 0.35f, 0.75f, 0.6f);    /* STATION: for a busy place – calm, wide, the voices far away */
            s->respond = RS_CALM; s->idle = 0.35f; s->mix = 0.72f; s->mic = 0.1f; s->rise = 5; s->fall = 40; s->move = 0.5f; break;
    case 8: s->engine = EN_CHOIR; s->key = 2; s->scale = 3; EP(EN_CHOIR, 0.55f, 0.35f, 0.35f, 0.15f);   /* CATHEDRAL: a choir in a huge room, follows what it hears */
            s->decay = 0.95f; s->size = 1; s->mix = 0.7f; s->follow = 0.7f; break;
    case 9: s->engine = EN_CHOIR; s->key = 9; s->scale = 4; EP(EN_CHOIR, 0.05f, 0.2f, 0.3f, 0.5f);     /* BREATH: hummed "oo", sinks back when people talk */
            s->respond = RS_CALM; s->idle = 0.25f; s->fall = 20; break;
    case 10: s->engine = EN_CHOIR; s->key = 5; s->scale = 1; s->octave = 2; s->voicing = VC_CLOSE; EP(EN_CHOIR, 0.8f, 0.55f, 0.45f, 0.2f);   /* AURORA: bright, rising shimmer */
             s->shimmer = 0.55f; s->mix = 0.65f; s->decay = 0.8f; break;
    case 11: s->engine = EN_CHOIR; s->key = 0; s->scale = 2; s->voicing = VC_FIFTHS; EP(EN_CHOIR, 0.3f, 0.3f, 0.25f, 0.1f);   /* CHANT: hum a note and it moves there */
             s->follow = 0.9f; s->glide = 1.5f; s->move = 0; break;
    case 12: s->engine = EN_CHIMES; s->key = 7; s->scale = 6; s->octave = 2; EP(EN_CHIMES, 0.55f, 6, 0.55f, 0.25f); break;   /* WIND CHIMES */
    case 13: s->engine = EN_CHIMES; s->key = 2; s->scale = 8; s->octave = 0; EP(EN_CHIMES, 0.15f, 11, 0.8f, 0.6f); s->decay = 0.85f; break;   /* TEMPLE: low bells, a drone under */
    case 14: s->engine = EN_CHIMES; s->key = 4; s->scale = 7; s->octave = 2; EP(EN_CHIMES, 0.9f, 1.5f, 0.15f, 0.3f); s->mix = 0.5f; break;   /* RAIN: many soft mallets */
    case 15: s->engine = EN_CHIMES; s->key = 9; s->scale = 7; s->octave = 1; EP(EN_CHIMES, 0.2f, 8, 0.45f, 0.7f);   /* NIGHT: sparse, a pad always there */
             s->respond = RS_STEADY; s->idle = 0.4f; s->mix = 0.75f; break;
  }
}
static void write_programs(void) { P.magic = MAGIC; P.size = sizeof P; if (vibe_store_write("programs", &P, sizeof P)) toast("SAVE FAILED"); }
const halo_sound *prog_get(int i) { return &P.s[i < 0 || i >= N_PRESETS ? 0 : i]; }
const char *prog_name(int i) { int s = P.src[i]; return s >= 0 && s < N_PRESETS ? PRESET_N[s] : "USER"; }
uint32_t prog_colour(int i) { int s = P.src[i]; return s >= 0 && s < N_PRESETS ? PRESET_C[s] : 0xE6EAF2; }
static void prog_store(int i, const halo_sound *s, int src) { P.s[i] = *s; P.src[i] = (int8_t)(src >= 0 ? P.src[src] : -1); P.user |= 1u << i; V.prog_user = (int)P.user; write_programs(); }
static void prog_reset(int i) { factory(i, &P.s[i]); P.src[i] = (int8_t)i; P.user &= ~(1u << i); V.prog_user = (int)P.user; write_programs(); }
static void global_save(void) {
  glb_file g = { MAGIC, sizeof g, V.src, V.in_mode, V.gain_db, V.sense, V.adapt, V.guard, V.out_db, V.pad_mode, V.meters, V.morph, V.keys_level, V.wander, V.every, V.from, V.to, V.preset,
                 V.saver, V.saver_after, V.saver_react, V.saver_fps };
  vibe_store_write("global", &g, sizeof g);
}
static void prog_init(void) {
  if (vibe_store_read("programs", &P, sizeof P) != (int)sizeof P || P.magic != MAGIC || P.size != sizeof P) {
    for (int i = 0; i < N_PRESETS; i++) { factory(i, &P.s[i]); P.src[i] = (int8_t)i; }
    P.user = 0;
  }
  V.prog_user = (int)P.user;
  V.s = P.s[0]; V.preset = 0;
  glb_file g = { 0 };
  int got = vibe_store_read("global", &g, sizeof g);
  if (g.magic == MAGIC && ((got == (int)sizeof g && g.size == sizeof g) || (got == (int)GLB_V02 && g.size == GLB_V02))) {
    V.src = g.src; V.in_mode = g.in_mode; V.gain_db = g.gain_db; V.sense = g.sense; V.adapt = g.adapt; V.guard = g.guard; V.out_db = g.out_db;
    V.pad_mode = g.pad_mode; V.meters = g.meters; V.morph = g.morph; V.keys_level = g.keys_level;
    V.wander = g.wander; V.every = g.every; V.from = g.from; V.to = g.to;
    if (g.size == sizeof g) { V.saver = g.saver; V.saver_after = g.saver_after; V.saver_react = g.saver_react; V.saver_fps = g.saver_fps; }
    if (g.last >= 0 && g.last < N_PRESETS) { V.s = P.s[g.last]; V.preset = g.last; }
  }
}

/* ---------------------------------------------------------------- parameters ------------------------------------ */
static const char *const MODE_N[] = { "LINE", "MIC", "HI-Z" };
static const char *const SRC_N[] = { "IN 1", "IN 2", "IN 1+2" };
static const char *const ONOFF_N[] = { "OFF", "ON" };
static const char *const PADM_N[] = { "HOLD", "TOGGLE" };
static const char *const FPS_N[] = { "FULL", "24", "12" };
static const char *const RESP_N[] = { "SWELL", "CALM", "STEADY" };
static const char *const VOIC_N[] = { "OPEN", "FIFTHS", "CLOSE", "CLUSTER" };
static const char *const NOTE_N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const OCT_N[] = { "LOW", "MID", "HIGH", "TOP" };
enum { F_DB, F_PCT, F_NAME, F_SEC, F_MIN, F_PAD, F_INT, F_MS, F_PRG, F_SCALE, F_AMT, F_ENGINE };
typedef struct { const char *label; float *v; float min, max, step; int fmt; const char *const *names; } pdef;
static float prg_sel;

static const char *const SUBN_LISTEN[] = { "INPUT", "ROOM" }, *const SUBN_ENGINE[] = { "ENGINE", "MORE" },
                         *const SUBN_TONE[] = { "KEY", "CHORD" }, *const SUBN_SPACE[] = { "REVERB", "SIZE" },
                         *const SUBN_GLOBAL[] = { "GLOBAL", "SAVER" };
int page_subs(int page) { return page == P_LISTEN || page == P_ENGINE || page == P_TONE || page == P_SPACE || page == P_GLOBAL ? 2 : 1; }
const char *const *page_sub_names(int page) {
  switch (page) { case P_LISTEN: return SUBN_LISTEN; case P_ENGINE: return SUBN_ENGINE; case P_TONE: return SUBN_TONE; case P_SPACE: return SUBN_SPACE; case P_GLOBAL: return SUBN_GLOBAL; }
  return 0;
}
static pdef eng_def(int k) {                              /* the open engine's k-th value */
  int e = (int)(V.s.engine + 0.5f); const eparam *p = &EPARAM[e][k];
  static const int FM[] = { F_SEC, F_PCT, F_INT, F_MS, F_PCT };
  return (pdef){ p->label, &V.s.ep[e][k], p->min, p->max, p->step, FM[p->fmt], 0 };
}
static int page_defs(int page, int sub, pdef d[4]) {
  static const pdef none = { 0 };
  for (int i = 0; i < 4; i++) d[i] = none;
  switch (page) {
    case P_LISTEN:
      if (sub) {
        d[0] = (pdef){ "ADAPT", &V.adapt, 5, 600, 5, F_SEC, 0 };
        d[1] = (pdef){ "GUARD", &V.guard, 0, 1, 1, F_NAME, ONOFF_N };
        d[2] = (pdef){ "MIC", &V.s.mic, 0, 1, 0.02f, F_AMT, 0 };
        d[3] = (pdef){ "KEYS", &V.keys_level, 0, 1, 0.02f, F_AMT, 0 };
      } else {
        d[0] = (pdef){ "SOURCE", &V.src, 0, SRC_COUNT - 1, 1, F_NAME, SRC_N };
        d[1] = (pdef){ "MODE", &V.in_mode, 0, 2, 1, F_NAME, MODE_N };
        d[2] = (pdef){ "GAIN", &V.gain_db, -12, 40, 1, F_DB, 0 };
        d[3] = (pdef){ "SENSE", &V.sense, 0, 1, 0.02f, F_PCT, 0 };
      }
      return 1;
    case P_ENGINE:
      if (sub) {
        d[0] = eng_def(3);
        d[1] = (pdef){ "LEVEL", &V.s.level, 0, 1, 0.02f, F_PCT, 0 };
        d[2] = (pdef){ "DRIFT", &V.s.drift, 0, 1, 0.02f, F_AMT, 0 };
      } else {
        d[0] = (pdef){ "ENGINE", &V.s.engine, 0, EN_COUNT - 1, 1, F_ENGINE, 0 };
        d[1] = eng_def(0); d[2] = eng_def(1); d[3] = eng_def(2);
      }
      return 1;
    case P_TONE:
      if (sub) {
        d[0] = (pdef){ "VOICING", &V.s.voicing, 0, VC_COUNT - 1, 1, F_NAME, VOIC_N };
        d[1] = (pdef){ "GLIDE", &V.s.glide, 0.1f, 20, 0.1f, F_SEC, 0 };
        d[2] = (pdef){ "MOVE", &V.s.move, 0, 1, 0.02f, F_AMT, 0 };
      } else {
        d[0] = (pdef){ "KEY", &V.s.key, 0, 11, 1, F_NAME, NOTE_N };
        d[1] = (pdef){ "SCALE", &V.s.scale, 0, N_SCALES - 1, 1, F_SCALE, 0 };
        d[2] = (pdef){ "OCTAVE", &V.s.octave, 0, 3, 1, F_NAME, OCT_N };
        d[3] = (pdef){ "FOLLOW", &V.s.follow, 0, 1, 0.02f, F_AMT, 0 };
      }
      return 1;
    case P_SPACE:
      if (sub) {
        d[0] = (pdef){ "SIZE", &V.s.size, 0, 1, 0.02f, F_PCT, 0 };
        d[1] = (pdef){ "MOD", &V.s.mod, 0, 1, 0.02f, F_AMT, 0 };
        d[2] = (pdef){ "WIDTH", &V.s.width, 0, 1, 0.02f, F_PCT, 0 };
      } else {
        d[0] = (pdef){ "MIX", &V.s.mix, 0, 1, 0.02f, F_PCT, 0 };
        d[1] = (pdef){ "DECAY", &V.s.decay, 0, 1, 0.02f, F_PCT, 0 };
        d[2] = (pdef){ "DAMP", &V.s.damp, 0, 1, 0.02f, F_PCT, 0 };
        d[3] = (pdef){ "SHIMMER", &V.s.shimmer, 0, 1, 0.02f, F_AMT, 0 };
      }
      return 1;
    case P_MOTION:
      d[0] = (pdef){ "RESPOND", &V.s.respond, 0, RS_COUNT - 1, 1, F_NAME, RESP_N };
      d[1] = (pdef){ "RISE", &V.s.rise, 0.1f, 30, 0.1f, F_SEC, 0 };
      d[2] = (pdef){ "FALL", &V.s.fall, 0.5f, 120, 0.5f, F_SEC, 0 };
      d[3] = (pdef){ "IDLE", &V.s.idle, 0, 1, 0.02f, F_AMT, 0 };
      return 1;
    case P_WANDER:
      d[0] = (pdef){ "WANDER", &V.wander, 0, 1, 1, F_NAME, ONOFF_N };
      d[1] = (pdef){ "EVERY", &V.every, 1, 120, 1, F_MIN, 0 };
      d[2] = (pdef){ "FROM", &V.from, 0, N_PRESETS - 1, 1, F_PAD, 0 };
      d[3] = (pdef){ "TO", &V.to, 0, N_PRESETS - 1, 1, F_PAD, 0 };
      return 1;
    case P_PRESETS:
      d[0] = (pdef){ "PROGRAM", &prg_sel, 0, N_PRESETS - 1, 1, F_PRG, 0 };
      return 1;
    case P_GLOBAL:
      if (sub) {
        d[0] = (pdef){ "SAVER", &V.saver, 0, 1, 1, F_NAME, ONOFF_N };
        d[1] = (pdef){ "AFTER", &V.saver_after, 1, 30, 1, F_MIN, 0 };
        d[2] = (pdef){ "REACT", &V.saver_react, 0, 1, 0.02f, F_AMT, 0 };
        d[3] = (pdef){ "FPS", &V.saver_fps, 0, 2, 1, F_NAME, FPS_N };
        return 1;
      }
      d[0] = (pdef){ "OUTPUT", &V.out_db, -40, 6, 1, F_DB, 0 };
      d[1] = (pdef){ "PADS", &V.pad_mode, 0, 1, 1, F_NAME, PADM_N };
      d[2] = (pdef){ "MORPH", &V.morph, 0.1f, 30, 0.1f, F_SEC, 0 };
      d[3] = (pdef){ "METERS", &V.meters, 0, 1, 1, F_NAME, ONOFF_N };
      return 1;
  }
  return 0;
}
static void fmt(const pdef *d, char *out) {
  char b[16]; float v = *d->v; out[0] = 0;
  switch (d->fmt) {
    case F_NAME: cat(out, d->names[(int)(v + 0.5f)]); break;
    case F_ENGINE: cat(out, ENGINE_N[(int)(v + 0.5f)]); break;
    case F_SCALE: cat(out, scale_name((int)(v + 0.5f))); break;
    case F_PRG: case F_PAD: cat(out, vg_itoa((int)(v + 0.5f) + 1, b)); break;
    case F_INT: cat(out, vg_itoa((int)(v + 0.5f), b)); break;
    case F_MS: cat(out, vg_itoa((int)(v + 0.5f), b)); cat(out, " ms"); break;
    case F_MIN: cat(out, vg_itoa((int)(v + 0.5f), b)); cat(out, " min"); break;
    case F_SEC: cat(out, v < 10 ? vg_ftoa(v, 1, b) : vg_itoa((int)(v + 0.5f), b)); cat(out, " s"); break;
    case F_AMT: if (v < 0.001f) { cat(out, "OFF"); break; } /* fall through */
    case F_PCT: cat(out, vg_itoa((int)(v * 100 + 0.5f), b)); cat(out, " %"); break;
    case F_DB: { int i = (int)(v < 0 ? v - 0.5f : v + 0.5f); if (i > 0) cat(out, "+"); cat(out, vg_itoa(i, b)); cat(out, " dB"); break; }
  }
}
void params_get(v_param p[4]) {
  pdef d[4]; page_defs(V.page, V.sub, d);
  for (int i = 0; i < 4; i++) {
    p[i].label = d[i].label; p[i].bipolar = 0; p[i].text[0] = 0; p[i].value = 0;
    if (!d[i].label) continue;
    fmt(&d[i], p[i].text);
    p[i].value = (*d[i].v - d[i].min) / (d[i].max - d[i].min);
  }
}
int freeze_on(void) { return V.freeze; }

/* ---------------------------------------------------------------- pads, encoders ------------------------------- */
static void prog_load(int i) { V.s = *prog_get(i); V.preset = i; prg_sel = (float)i; VIBE_RELEASE(); V.prog_seq++; }
static void prog_save(int i) {
  prog_store(i, &V.s, V.preset); V.preset = i; prg_sel = (float)i; global_save();
  char b[40] = "SAVED TO PAD "; char t[8]; cat(b, vg_itoa(i + 1, t)); toast(b);
}
static void prg_go(int i) { prog_load(i); char b[40] = ""; char t[8]; cat(b, vg_itoa(i + 1, t)); cat(b, " "); cat(b, prog_name(i)); toast(b); }
void ui_go_program(int i) { prg_go(i); }
static void prg_step(int d) { prg_go(V.preset < 0 ? (d > 0 ? 0 : N_PRESETS - 1) : (V.preset + d + N_PRESETS) % N_PRESETS); }

static halo_sound snap; static int snap_preset, pad_held = -1; static double pad_t;
static void pad(int i, int down) {
  if (i < 0 || i >= N_PRESETS) return;
  if (down) {
    if (V.save_held) { V.save_used = 1; prog_save(i); return; }
    snap = V.s; snap_preset = V.preset; pad_held = i; pad_t = v_ms();
    prg_go(i);
  } else if (i == pad_held) {
    pad_held = -1;
    if ((int)V.pad_mode == 0 && v_ms() - pad_t > 450) { V.s = snap; V.preset = snap_preset; if (snap_preset >= 0) prg_sel = (float)snap_preset; V.prog_seq++; }
  }
}

static int help;
static const char *const HELP[] = {
  "K1 - K6", "the six areas", "K AGAIN", "next sub-page",
  "ENCODER 1 - 4", "the page's values", "A \xC2\xB7 B", "level \xC2\xB7 space",
  "DATA \xC2\xB7 \xE2\x97\x80 \xE2\x96\xB6", "program", "PADS 1 - 16", "programs (morph)",
  "F1 \xC2\xB7 REC", "freeze (hold: held)", "F2 \xC2\xB7 F6", "presets \xC2\xB7 global",
  "F3 \xC2\xB7 F4", "program - / +", "F5", "save (+ pad: there)",
  "STOP \xC2\xB7 PLAY", "hush \xC2\xB7 back", "KEYS \xC2\xB7 OCT", "test voice \xC2\xB7 octave",
  "ENTER", "input on \xC2\xB7 LISTEN" };
static float acc[4];
static void param_turn(int i, float detents) {
  pdef d[4]; if (!page_defs(V.page, V.sub, d) || !d[i].label) return;
  float v = *d[i].v;
  if (d[i].step >= 1) {
    acc[i] += detents;
    while (acc[i] >= 1) { v += d[i].step; acc[i] -= 1; }
    while (acc[i] <= -1) { v -= d[i].step; acc[i] += 1; }
  } else {
    float st = d[i].step;
    if (d[i].fmt == F_SEC && v >= 10) st *= 5;           /* (seconds: coarser past 10 s) */
    v += detents * st * (V.shift ? 0.25f : 1);
  }
  *d[i].v = vd_clamp(v, d[i].min, d[i].max);
  if (d[i].v == &V.in_mode) { vibe_input_mode(0, (int)V.in_mode); vibe_input_mode(1, (int)V.in_mode); }
  if (d[i].v == &prg_sel && (int)prg_sel != V.preset) prog_load((int)prg_sel);
  if (d[i].v == &V.wander) V.wander_t = v_ms();
}
static void set_page(int p) {
  if ((V.page == P_GLOBAL || V.page == P_LISTEN || V.page == P_WANDER) && p != V.page) global_save();
  V.sub = p == V.page ? (V.sub + 1) % page_subs(p) : 0;
  V.page = p; acc[0] = acc[1] = acc[2] = acc[3] = 0;
  if (!V.input_on) vibe_request(VIBE_REQ_AUDIO_INPUT);
  if (p == P_PRESETS && V.preset >= 0) prg_sel = (float)V.preset;
}

/* ---------------------------------------------------------------- the DAW (SDK 8) ------------------------------- */
static const char *const EN_C[4] = { "Bloom", "Cloud", "Choir", "Chimes" }, *const RS_C[3] = { "Swell", "Calm", "Steady" };
static const vibe_param VP[] = {
  { "engine", "Engine", 0, 3, 0, "", 4, EN_C },          { "level", "Level", 0, 1, 0.8f, "", 0, 0 },
  { "e1", "Engine 1", 0, 1500, 0, "", 0, 0 },            { "e2", "Engine 2", 0, 1500, 0, "", 0, 0 },
  { "e3", "Engine 3", 0, 1500, 0, "", 0, 0 },            { "e4", "Engine 4", 0, 1500, 0, "", 0, 0 },
  { "key", "Key", 0, 11, 2, "", 12, NOTE_N },            { "scale", "Scale", 0, 9, 0, "", 10, 0 },
  { "follow", "Follow", 0, 1, 0.5f, "", 0, 0 },          { "glide", "Glide", 0.1f, 20, 4, "s", 0, 0 },
  { "move", "Move", 0, 1, 0.3f, "", 0, 0 },              { "mix", "Space Mix", 0, 1, 0.55f, "", 0, 0 },
  { "decay", "Space Decay", 0, 1, 0.6f, "", 0, 0 },      { "shimmer", "Shimmer", 0, 1, 0, "", 0, 0 },
  { "respond", "Respond", 0, 2, 0, "", 3, RS_C },        { "rise", "Rise", 0.1f, 30, 2, "s", 0, 0 },
  { "fall", "Fall", 0.5f, 120, 12, "s", 0, 0 },          { "idle", "Idle", 0, 1, 0.3f, "", 0, 0 },
  { "mic", "Mic Send", 0, 1, 0.15f, "", 0, 0 },
};
#define NVP ((int)(sizeof VP / sizeof VP[0]))
static const char *const VG[] = { "ENGINE", "ENGINE", "ENGINE", "ENGINE", "ENGINE", "ENGINE", "TONE", "TONE", "TONE", "TONE", "TONE",
                                  "SPACE", "SPACE", "SPACE", "MOTION", "MOTION", "MOTION", "MOTION", "SPACE" };
static float *vp_val(int i) {
  int e = (int)(V.s.engine + 0.5f);
  float *const Pp[NVP] = { &V.s.engine, &V.s.level, &V.s.ep[e][0], &V.s.ep[e][1], &V.s.ep[e][2], &V.s.ep[e][3], &V.s.key, &V.s.scale,
                           &V.s.follow, &V.s.glide, &V.s.move, &V.s.mix, &V.s.decay, &V.s.shimmer, &V.s.respond, &V.s.rise, &V.s.fall, &V.s.idle, &V.s.mic };
  return i >= 0 && i < NVP ? Pp[i] : 0;
}
static void daw_sync(void) { for (int i = 0; i < NVP; i++) { float v = *vp_val(i); if (vibe_param_get(i) != v) vibe_param_set(i, v); } }

/* ---------------------------------------------------------------- init, events ---------------------------------- */
void vibe_on_init(float sample_rate) {
  vf_mode(VF_ON_HOLD); vf_clicks(1);
  V.sr = sample_rate;
  V.src = SRC_IN1; V.in_mode = VIBE_IN_MIC; V.gain_db = 12; V.sense = 0.5f; V.adapt = 60; V.guard = 1; V.out_db = 0; V.pad_mode = 0;
  V.meters = 1; V.morph = 8; V.keys_level = 0.8f; V.wander = 0; V.every = 15; V.from = 0; V.to = 15;
  V.saver = 1; V.saver_after = 1; V.saver_react = 0.8f; V.saver_fps = 1;
  V.floor_db = -60;
  prog_init();
  vibe_params(VP, NVP); vibe_param_groups(VG); daw_sync();
  vibe_areas(6);
  listen_init(sample_rate); engines_init(sample_rate); space_init(sample_rate);
  vibe_input_mode(0, (int)V.in_mode); vibe_input_mode(1, (int)V.in_mode);
  static const char *const K[6] = { "LISTEN", "ENGINE", "TONE", "SPACE", "MOTION", "WANDER" };
  static const char *const F[6] = { "FREEZE", "PRESETS", "PRG -", "PRG +", "SAVE", "GLOBAL" };
  for (int i = 0; i < 6; i++) { vibe_label(VB_K1 + i, K[i]); vibe_label(VB_F1 + i, F[i]); }
  V.page = P_ENGINE; prg_sel = (float)V.preset;
  vibe_request(VIBE_REQ_AUDIO_INPUT);
  vibe_log("HALO 0.3 started");
}

static double frz_t; static int frz_keep;
static void freeze_switch(int down) {                 /* tap = on/off, hold = only while held */
  if (down) { frz_t = v_ms(); V.freeze_held = 1; V.freeze = 1; return; }
  V.freeze_held = 0;
  if (v_ms() - frz_t < 450) { frz_keep = !frz_keep; toast(frz_keep ? "FREEZE" : "LISTENING"); }
  V.freeze = frz_keep;
}
static void button(int b, int down) {
  if (b == VB_SHIFT) { V.shift = down; return; }
  if ((b == VB_F1 && !V.shift) || b == VB_REC) { freeze_switch(down); return; }
  if (b == VB_F5) {
    if (down) { V.save_held = 1; V.save_used = 0; return; }
    V.save_held = 0;
    if (V.save_used) return;
    if (V.shift) { if (V.preset >= 0) { prog_reset(V.preset); prog_load(V.preset); toast("FACTORY SOUND RESTORED"); } return; }
    if (V.preset >= 0) prog_save(V.preset); else { global_save(); toast("HOLD SAVE + PAD = SAVE THERE"); }
    return;
  }
  if (!down) return;
  if (b >= VB_K1 && b <= VB_K6) { set_page(P_LISTEN + b - VB_K1); return; }
  switch (b) {
    case VB_F2: set_page(P_PRESETS); break;
    case VB_F3: case VB_LEFT: prg_step(-1); break;
    case VB_F4: case VB_RIGHT: prg_step(1); break;
    case VB_F6: set_page(P_GLOBAL); break;
    case VB_STOP: hush = 1; toast("HUSH"); break;
    case VB_PLAY: if (hush) { hush = 0; toast("BACK"); } break;
    case VB_ENTER: case VB_DATA_PUSH:
      if (!V.input_on) { vibe_request(VIBE_REQ_AUDIO_INPUT); toast("SWITCHING THE INPUT ON"); }
      else set_page(P_LISTEN);
      break;
    case VB_HOME: help = !help; break;
    case VB_RETURN: help = 0; V.popup_off = 1; break;
  }
}

static int key_oct;
static void note(int n, int on) {
  if (on) { V.key_note = n; V.key_gate = 1; }
  else if (n == V.key_note) V.key_gate = 0;
}
/* a touch wakes the display: the touch that ends the saver does nothing else (nor does its release) */
static int is_touch(const vibe_event *e) {
  switch (e->type) {
    case VIBE_EV_BUTTON: case VIBE_EV_PAD: case VIBE_EV_KEY: case VIBE_EV_ENCODER: case VIBE_EV_TOUCH:
    case VIBE_EV_AREA: case VIBE_EV_FADER: case VIBE_EV_STAGE: return 1;
  }
  return 0;
}
static int wake_type = -1, wake_id = -1;
static int wake(const vibe_event *e) {
  if (!is_touch(e)) return 0;
  V.touch_ms = v_ms();
  if (wake_type == e->type && wake_id == e->id && !e->down && e->type != VIBE_EV_ENCODER) { wake_type = wake_id = -1; return 1; }
  if (!V.saver_on) return 0;
  V.saver_on = 0;
  if (e->type != VIBE_EV_ENCODER && e->down) { wake_type = e->type; wake_id = e->id; }
  return 1;
}
void vibe_on_event(const vibe_event *e) {
  if (wake(e)) return;
  { int id = -1, r = vf_event(e, &id);
    if (r == VF_PRESS && ui_popup_press(id)) return;
    if (r == VF_PRESS && id >= FO_PRESET && id < FO_PRESET + N_PRESETS) prg_go(id - FO_PRESET);
    if (r == VF_PRESS && id >= FO_CHIP && id < FO_CHIP + 4) { V.sub = id - FO_CHIP; acc[0] = acc[1] = acc[2] = acc[3] = 0; }
    if (r != VF_NO) return; }
  switch (e->type) {
    case VIBE_EV_BUTTON:
      if (e->down && e->id == VB_OCT_DOWN) { if (key_oct > -2) key_oct--; break; }
      if (e->down && e->id == VB_OCT_UP) { if (key_oct < 2) key_oct++; break; }
      button(e->id, e->down); break;
    case VIBE_EV_PAD: pad(e->id, e->down); break;
    case VIBE_EV_AREA: {
      int a = V.page < 6 ? V.page : P_ENGINE, d = e->value > 0.5f ? 1 : e->value < -0.5f ? -1 : 0;
      set_page(d ? (a + d + 6) % 6 : a); break; }
    case VIBE_EV_PEDAL:
      if (e->id == VIBE_PEDAL_SWITCH) freeze_switch(e->down);
      else V.s.level = e->value;
      break;
    case VIBE_EV_ENCODER:
      if (e->id <= VE_4) param_turn(e->id, e->value);
      else if (e->id == VE_A) { V.s.level = vd_clamp(V.s.level + e->value * 0.02f, 0, 1); char t[24] = "LEVEL ", n[8]; cat(t, vg_itoa((int)(V.s.level * 100 + 0.5f), n)); cat(t, " %"); toast(t); }
      else if (e->id == VE_B) { V.s.mix = vd_clamp(V.s.mix + e->value * 0.02f, 0, 1); char t[24] = "SPACE ", n[8]; cat(t, vg_itoa((int)(V.s.mix * 100 + 0.5f), n)); cat(t, " %"); toast(t); }
      else if (e->id == VE_DATA) { static float a; a += e->value; while (a >= 1) { a -= 1; prg_step(1); } while (a <= -1) { a += 1; prg_step(-1); } }
      break;
    case VIBE_EV_KEY: note(48 + e->id + 12 * key_oct, e->down); break;
    case VIBE_EV_MIDI: {
      int st = e->midi[0] & 0xF0, ch = e->midi[0] & 15, d1 = e->midi[1], d2 = e->midi[2];
      if (ch == 9) { if (st == 0x90 && d2 && d1 >= 36 && d1 < 52) pad(d1 - 36, 1); else if ((st == 0x80 || st == 0x90) && d1 >= 36 && d1 < 52) pad(d1 - 36, 0); break; }
      if (st == 0x90 && d2) note(d1, 1); else if (st == 0x80 || st == 0x90) note(d1, 0);
      else if (st == 0xB0 && d1 == 7) V.out_db = -40 + 46 * d2 / 127.0f;
      break; }
    case VIBE_EV_PARAM: { float *v = vp_val(e->id); if (v) *v = vibe_param_get(e->id); break; }
    case VIBE_EV_SAVE: global_save(); break;
  }
}

/* ---------------------------------------------------------------- audio ----------------------------------------- */
void vibe_on_audio(const float *in, float *l, float *r, int n) {
  static float raw[VIBE_MAX_BLOCK], exc[VIBE_MAX_BLOCK], mic[VIBE_MAX_BLOCK];
  static halo_sound S; static int seq = -1; static float morph_left, hush_g = 1;
  if (in) V.input_on = 1;
  int src = (int)(V.src + 0.5f);
  const float *a = in, *b = vibe_input(1);
  for (int i = 0; i < n; i++) {
    float x = a ? a[i] : 0, y = b ? b[i] : 0;
    raw[i] = src == SRC_IN2 ? y : src == SRC_BOTH ? 0.5f * (x + y) : x;
  }
  /* the scope for the screensaver: every 4th raw sample into the ring (one writer; the reader never blocks us) */
  { static int ph; int h = SCOPE.head;
    for (int i = ph; i < n; i += 4) SCOPE.w[h++ & (SCOPE_N - 1)] = raw[i];
    ph = (ph - n) & 3; VIBE_RELEASE(); SCOPE.head = h; }
  /* the sound glides to what the control flow set – after a program change over MORPH seconds */
  if (seq != V.prog_seq) { VIBE_ACQUIRE(); if (seq < 0) S = V.s; else morph_left = V.morph; seq = V.prog_seq; }
  float k = 1 - vd_coef((morph_left > 0.05f ? V.morph : 0.05f) / 3, V.sr / n);
  morph_left -= n / V.sr;
  float *ps = (float *)&S; const float *pt = (const float *)&V.s;
  for (int j = 0; j < (int)(sizeof S / sizeof(float)); j++) ps[j] += (pt[j] - ps[j]) * k;
  S.engine = V.s.engine; S.key = V.s.key; S.scale = V.s.scale; S.octave = V.s.octave; S.voicing = V.s.voicing; S.respond = V.s.respond;
  listen_block(raw, exc, mic, n, &S);
  engines_run(exc, l, r, n, &S);
  space_run(l, r, mic, n, &S);
  /* HUSH: a slow fade to silence and back */
  float ht = hush ? 0.0f : 1.0f, hk = 1 - vd_coef(hush ? 1.5f : 0.8f, V.sr / n), h0 = hush_g;
  hush_g += (ht - hush_g) * hk;
  for (int i = 0; i < n; i++) { float g = h0 + (hush_g - h0) * i / n; l[i] *= g; r[i] *= g; }
  master_run(l, r, n);
  V.now += n;
}

static void wander_tick(void) {
  if (V.wander < 0.5f || hush) { V.wander_t = v_ms(); return; }
  if (v_ms() - V.wander_t < V.every * 60000.0) return;
  V.wander_t = v_ms();
  int a = (int)(V.from + 0.5f), b = (int)(V.to + 0.5f); if (a > b) { int t = a; a = b; b = t; }
  int span = b - a + 1; if (span < 2) return;
  int p = V.preset; for (int t = 0; t < 8 && p == V.preset; t++) p = a + (int)(vd_rand() % (uint32_t)span);
  prg_go(p);
}
int halo_hush(void) { return hush; }
void vibe_on_draw(uint16_t *fb) { daw_sync(); wander_tick();
  if (V.saver > 0.5f && !V.saver_on && !help && v_ms() - V.touch_ms > V.saver_after * 60000.0) { V.saver_on = 1; saver_reset(); }
  if (V.saver < 0.5f) V.saver_on = 0;
  if (V.saver_on) { saver_draw(fb); ui_lights(); return; }
  ui_draw(); if (help) vg_controls("HALO", HELP, sizeof HELP / sizeof HELP[0] / 2); ui_lights(); }

/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). HALO, see LICENSE. */
/* halo.h – HALO, a listening drone for the VIBE (concept: KONZEPT.md).
   A microphone (or any sound at IN 1 / IN 2) listens to the room; what it hears drives a slow, wide drone:
   how much is going on (PRESENCE), single events (ONSETS), the pitch someone hums or a bell rings (HEARD), the brightness.
   Four engines, each with its own relation to the sound:
     BLOOM   sympathetic strings – the room's sound itself excites six strings tuned to a chord
     CLOUD   a cloud of grains made from what the mic heard, retuned into the chord
     CHOIR   a choir whose chord follows the pitch it hears and that swells with the room
     CHIMES  every sound in the room strikes a bell from the chord, a soft pad under it
   Then a big space (8-line FDN reverb with shimmer), a limiter. The harmony stays in a key and scale; FOLLOW lets a heard
   pitch move the chord to that degree, MOVE lets it wander by itself, WANDER walks the programs for an installation.
   Files: main.c = events, pages, parameters, programs, the audio chain · listen.c = the ears (levels, adaptive floor,
          onsets, pitch, the harmony, the feedback guard, the test voice on the keys) · engines.c = the four engines ·
          space.c = reverb, shimmer, master · ui.c = the display */
#ifndef HALO_H
#define HALO_H
#include "vibe.h"
#include "vibe_gfx.h"
#include "vibe_dsp.h"

enum { P_LISTEN, P_ENGINE, P_TONE, P_SPACE, P_MOTION, P_WANDER,      /* the areas on the colour keys K1-K6 */
       P_PRESETS, P_GLOBAL, P_COUNT };
enum { EN_BLOOM, EN_CLOUD, EN_CHOIR, EN_CHIMES, EN_COUNT };
enum { RS_SWELL, RS_CALM, RS_STEADY, RS_COUNT };                      /* how the level answers the room */
enum { VC_OPEN, VC_FIFTHS, VC_CLOSE, VC_CLUSTER, VC_COUNT };          /* the chord's voicing */
enum { SRC_IN1, SRC_IN2, SRC_BOTH, SRC_COUNT };
#define N_SCALES 10
#define N_PRESETS 16
#define N_HARM 6                       /* the chord: six notes */
#define FO_PRESET 200                  /* (SDK 10 FOCUS ids: the PRESETS page's cells) */
#define FO_CHIP 300                    /* … the sub-page chips */

/* what a program sets */
typedef struct {
  float engine, ep[EN_COUNT][4], level, drift;            /* ENGINE: which, its four values (each engine keeps its own) */
  float key, scale, octave, follow, glide, voicing, move; /* TONE */
  float mix, decay, damp, shimmer, size, mod, mic, width; /* SPACE */
  float respond, rise, fall, idle;                         /* MOTION */
} halo_sound;

typedef struct {
  float sr; double now;
  int page, sub, shift, input_on, freeze, freeze_held, popup_off;
  halo_sound s;                                            /* the sound (control flow writes, audio glides to it) */
  int prog_seq;                                            /* +1 on every program change: the audio morphs */
  /* global – the place, not the sound */
  float src, in_mode, gain_db, sense, adapt, guard, out_db, pad_mode, meters, morph, keys_level;
  float wander, every, from, to;
  int preset, prog_user, save_held, save_used;
  int key_note, key_gate;                                  /* the test voice (keys, MIDI): control writes, audio reads */
  /* what the ears found – audio writes, the display reads */
  float in_db, floor_db, thr_db, act, presence, swell, bright, heard, out_peak, clip_until, cloud_fill;
  float harm[N_HARM]; int chord_deg, chord_pc, onsets, guard_hits; double guard_until, heard_until;
  char toast[40]; double toast_until;
  double wander_t;
} halo_state;
extern halo_state V;
double v_ms(void);
void toast(const char *t);

/* listen.c – audio flow */
typedef struct {                                          /* what the engines get, once a block */
  float swell, act, presence, bright, gate, heard;        /* heard: MIDI note of a pitch just heard, 0 = none */
  int onsets; float onset_vel;                            /* this block */
  float harm[N_HARM];                                     /* the chord now (MIDI, gliding) */
  int freeze;
} halo_ears;
extern halo_ears E;
void listen_init(float sr);
void listen_block(const float *raw, float *exc, float *mic, int n, const halo_sound *s);   /* raw in → excitation, mic send */
int  scale_count(int scale);
int  scale_has(int scale, int pc);
const char *scale_name(int scale);

/* engines.c */
void engines_init(float sr);
void engines_run(const float *exc, float *l, float *r, int n, const halo_sound *s);
extern const char *const ENGINE_N[EN_COUNT];
typedef struct { const char *label; float min, max, step, def; int fmt; } eparam;
extern const eparam EPARAM[EN_COUNT][4];

/* space.c */
void space_init(float sr);
void space_run(float *l, float *r, const float *mic, int n, const halo_sound *s);
void master_run(float *l, float *r, int n);

/* main.c */
typedef struct { const char *label; char text[16]; float value; int bipolar; } v_param;
void params_get(v_param p[4]);
int  page_subs(int page);
const char *const *page_sub_names(int page);
const halo_sound *prog_get(int i);
const char *prog_name(int i);
uint32_t prog_colour(int i);
int freeze_on(void);
int halo_hush(void);

/* ui.c */
void ui_draw(void);
void ui_lights(void);
int  ui_popup_press(int id);                            /* the microphone popup's OK */

/* small helpers */
static inline float db2lin(float db) { return vd_exp2(db * 0.16609640f); }
static inline float lin2db(float x) { return x < 1e-9f ? -180.0f : 6.0205999f * vd_log2(x); }
#endif

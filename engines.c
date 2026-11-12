/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). HALO, see LICENSE. */
/* engines.c – HALO's four engines. Audio flow only. Each adds stereo into l / r, scaled by the room's swell.
   BLOOM   six Karplus strings (delay + loop low-pass), tuned to the chord, gliding with it. Their input is the room's
           sound (levelled and gated) plus a breath of noise – a sitar's sympathetic strings, a piano with the pedal down.
           DECAY (T60), COLOR (the loop's low-pass), STRINGS (3..6), DETUNE (slow beating between the strings).
   CLOUD   12 s of what the mic heard (only while something is heard: when the room is empty the cloud keeps its last
           voices), up to 24 grains read from it. TUNE retunes a grain from the pitch heard at its place to the nearest
           chord note – people talking become a choir in key. SIZE, DENSITY, TUNE, SPREAD (place, stereo, octaves).
           Under it a breath: noise through two narrow band-passes on the chord.
   CHOIR   the chord as six voices of three detuned saws, a low-pass, two formant filters (oo .. ee). The room's
           brightness moves the vowel. VOWEL, BRIGHT, DETUNE, BREATH.
   CHIMES  every onset (and, with DENSITY, the room's presence at random) strikes an FM bell from the chord – near the
           pitch just heard, if there was one. A soft sine pad of root and fifth under it. DENSITY, DECAY, METAL, PAD.
   An engine change crossfades over 2.5 s (both run meanwhile). DRIFT: slow random detune of everything. */
#include "halo.h"

const char *const ENGINE_N[EN_COUNT] = { "BLOOM", "CLOUD", "CHOIR", "CHIMES" };
enum { EF_SEC, EF_PCT, EF_INT, EF_MS, EF_PERS };
const eparam EPARAM[EN_COUNT][4] = {
  { { "DECAY", 1, 30, 0.5f, 8, EF_SEC }, { "COLOR", 0, 1, 0.02f, 0.5f, EF_PCT }, { "STRINGS", 3, 6, 1, 6, EF_INT }, { "DETUNE", 0, 1, 0.02f, 0.3f, EF_PCT } },
  { { "SIZE", 60, 1500, 20, 400, EF_MS }, { "DENSITY", 0, 1, 0.02f, 0.4f, EF_PCT }, { "TUNE", 0, 1, 0.02f, 0.7f, EF_PCT }, { "SPREAD", 0, 1, 0.02f, 0.5f, EF_PCT } },
  { { "VOWEL", 0, 1, 0.02f, 0.4f, EF_PCT }, { "BRIGHT", 0, 1, 0.02f, 0.4f, EF_PCT }, { "DETUNE", 0, 1, 0.02f, 0.4f, EF_PCT }, { "BREATH", 0, 1, 0.02f, 0.2f, EF_PCT } },
  { { "DENSITY", 0, 1, 0.02f, 0.3f, EF_PCT }, { "DECAY", 0.5f, 12, 0.25f, 5, EF_SEC }, { "METAL", 0, 1, 0.02f, 0.5f, EF_PCT }, { "PAD", 0, 1, 0.02f, 0.4f, EF_PCT } } };

static float sr; static vd_noise nz;
static inline float rnd(void) { return vd_noise_next(&nz); }                 /* -1..1 */
static inline float rnd01(void) { return 0.5f + 0.5f * vd_noise_next(&nz); }
static float drift_ph[8], drift_v[8];
static void drift_tick(int n, float amount) {             /* eight slow random walks, -1..1, once a block */
  for (int i = 0; i < 8; i++) {
    drift_ph[i] += n / sr * (0.05f + 0.03f * i);
    if (drift_ph[i] >= 1) drift_ph[i] -= 1;
    drift_v[i] = amount * (0.6f * vd_sin(drift_ph[i]) + 0.4f * vd_sin(drift_ph[i] * 2.7f + 0.3f * i));
  }
}

/* ---------------------------------------------------------------- BLOOM ----------------------------------------- */
#define BL_N 6
#define BL_MAX 4096
static float bl_buf[BL_N][BL_MAX], bl_lp[BL_N], bl_hp; static int bl_w;
static const float BL_DET[BL_N] = { -0.6f, 0.5f, -0.25f, 0.8f, -0.45f, 0.3f };
static const float BL_PAN[BL_N] = { 0.5f, 0.3f, 0.7f, 0.15f, 0.85f, 0.4f };
static void bloom(const float *exc, float *l, float *r, int n, float gain, const float *p) {
  int ns = (int)(p[2] + 0.5f); if (ns < 3) ns = 3; if (ns > BL_N) ns = BL_N;
  float t60 = vd_clamp(p[0], 0.5f, 40), fc = 700 + p[1] * p[1] * 11000;
  float c = 1 - vd_coef(1.0f / (VD_TAU * fc), sr);
  float d[BL_N], g[BL_N], ins[BL_N], gl[BL_N], gr[BL_N];
  for (int k = 0; k < ns; k++) {
    float note = E.harm[k] + (BL_DET[k] * p[3] * 0.18f) + drift_v[k] * 0.12f;
    float dd = sr / vd_mtof(note) - (1 - c) / c;            /* (the loop's low-pass delays a little: take it off) */
    d[k] = vd_clamp(dd, 2, BL_MAX - 2);
    g[k] = vd_exp2(-9.9658f * d[k] / (sr * t60));
    ins[k] = vd_sqrt(1 - g[k] * g[k]) * 0.9f * (k == 0 ? 0.7f : 1.0f);
    gl[k] = vd_sqrt(1 - BL_PAN[k]) * gain; gr[k] = vd_sqrt(BL_PAN[k]) * gain;
  }
  float a_hp = 1 - vd_coef(1.0f / (VD_TAU * 160), sr);
  for (int i = 0; i < n; i++) {
    float x = exc[i] * 0.7f; bl_hp += (x - bl_hp) * a_hp; x -= bl_hp;
    x += rnd() * 0.32f;                                       /* the breath that keeps them humming */
    float ol = 0, orr = 0;
    for (int k = 0; k < ns; k++) {
      float rp = (float)bl_w - d[k]; if (rp < 0) rp += BL_MAX;
      int i0 = (int)rp; float fr = rp - (float)i0; int i1 = i0 + 1; if (i1 >= BL_MAX) i1 = 0;
      float y = bl_buf[k][i0] + (bl_buf[k][i1] - bl_buf[k][i0]) * fr;
      bl_lp[k] += (y - bl_lp[k]) * c;
      float w = x * ins[k] + bl_lp[k] * g[k];
      bl_buf[k][bl_w] = w > 2 ? 2 : w < -2 ? -2 : w;
      ol += y * gl[k]; orr += y * gr[k];
    }
    for (int k = ns; k < BL_N; k++) { bl_buf[k][bl_w] *= 0.999f; }
    if (++bl_w >= BL_MAX) bl_w = 0;
    l[i] += vd_tanh(ol * 0.8f) * 0.7f; r[i] += vd_tanh(orr * 0.8f) * 0.7f;
  }
}

/* ---------------------------------------------------------------- CLOUD ----------------------------------------- */
#define CL_LEN (48000 * 12)
#define CL_CH 1024                                            /* the pitch is kept per chunk of 1024 samples */
#define CL_G 24
#define HN 1024
static float cl_buf[CL_LEN], cl_pitch[CL_LEN / CL_CH + 1], hann[HN + 1];
static uint32_t cl_w, cl_fill;
typedef struct { float pos, inc, amp, gl, gr; int age, len; } grain;
static grain gr_[CL_G]; static int gr_n; static float cl_next;
static vd_svf cl_f1, cl_f2;
static float near_chord(float src, float shift) {            /* the chord note nearest src (any octave) */
  float best = src, bd = 99;
  for (int j = 1; j < N_HARM; j++) {
    float t = E.harm[j]; while (t - src > 6) t -= 12; while (src - t > 6) t += 12;
    if (vd_absf(t - src) < bd) { bd = vd_absf(t - src); best = t; }
  }
  return best + shift;
}
static void cloud_spawn(const float *p, float dens) {
  if (gr_n >= CL_G || cl_fill < (uint32_t)(sr * 0.4f)) return;
  int len = (int)(p[0] * 0.001f * sr * (0.75f + 0.5f * rnd01())); if (len < 64) len = 64;
  float maxback = (float)(cl_fill < CL_LEN - 1 ? cl_fill : CL_LEN - 1);
  float region = sr * (1 + p[3] * 9); if (region > maxback - len * 4.0f) region = maxback - len * 4.0f;
  if (region < 0) { if (maxback < len * 4.0f) return; region = 0; }
  float back = len * 4.0f + rnd01() * region;
  float pos = (float)cl_w - back; while (pos < 0) pos += CL_LEN;
  float src = cl_pitch[((int)pos) / CL_CH], semis;
  if (src > 0 && rnd01() < p[2]) {                          /* TUNE: into the chord */
    float sh = rnd01() < 0.25f * p[3] ? 12 : rnd01() < 0.2f ? -12 : 0;
    semis = near_chord(src, sh) - src;
  } else {
    float u = rnd01(); semis = u < 0.55f ? 0 : u < 0.55f + 0.25f * (0.4f + p[3]) ? -12 : 12;
    semis += rnd() * p[3] * 0.15f;
  }
  semis = vd_clamp(semis, -24, 24);
  grain *g = &gr_[gr_n++];
  g->pos = pos; g->inc = vd_exp2(semis / 12); g->age = 0; g->len = len;
  float pan = 0.5f + rnd() * 0.5f * (0.25f + 0.75f * p[3]);
  g->gl = vd_sqrt(1 - pan); g->gr = vd_sqrt(pan);
  g->amp = 0.9f / vd_sqrt(1 + dens * len / sr);
}
static void cloud(const float *exc, float *l, float *r, int n, float gain, const float *p) {
  float dens = 1.5f + p[1] * 22;
  float rate = dens * (0.55f + 0.45f * E.presence);
  /* write what is heard (only while something is: an empty room keeps the last voices) */
  int writing = E.gate > 0.2f && !E.freeze;
  float hp = E.heard;
  vd_svf_c c1, c2; vd_svf_set(&c1, vd_mtof(E.harm[2] + 12), 40, sr); vd_svf_set(&c2, vd_mtof(E.harm[3] + 12), 40, sr);
  for (int i = 0; i < n; i++) {
    if (writing) {
      cl_buf[cl_w] = exc[i];
      if (cl_w % CL_CH == 0) cl_pitch[cl_w / CL_CH] = hp;
      if (++cl_w >= CL_LEN) cl_w = 0;
      if (cl_fill < CL_LEN) cl_fill++;
    }
    cl_next -= 1;
    if (cl_next <= 0) { cloud_spawn(p, dens); cl_next = sr / rate * (0.4f + 1.2f * rnd01()); }
    float ol = 0, orr = 0;
    for (int k = 0; k < gr_n; k++) {
      grain *g = &gr_[k];
      float w = hann[(g->age * HN) / g->len];
      int i0 = (int)g->pos; float fr = g->pos - (float)i0; int i1 = i0 + 1; if (i1 >= CL_LEN) i1 = 0;
      float s = (cl_buf[i0] + (cl_buf[i1] - cl_buf[i0]) * fr) * w * g->amp;
      ol += s * g->gl; orr += s * g->gr;
      g->pos += g->inc; if (g->pos >= CL_LEN) g->pos -= CL_LEN;
      if (++g->age >= g->len) { gr_[k] = gr_[--gr_n]; k--; }
    }
    float b = rnd() * 0.05f;                                 /* the breath under it, on the chord */
    float br = vd_svf_tick(&cl_f1, &c1, b).bp + vd_svf_tick(&cl_f2, &c2, b).bp;
    l[i] += (ol * 2.2f + br * 0.6f) * gain; r[i] += (orr * 2.2f + br * 0.6f) * gain;
  }
  V.cloud_fill = (float)cl_fill / CL_LEN;
}

/* ---------------------------------------------------------------- CHOIR ----------------------------------------- */
static float ch_ph[N_HARM][3];
static vd_svf ch_lp[2], ch_f1[2], ch_f2[2];
static const float VOWEL_F[5][2] = { { 300, 870 }, { 400, 800 }, { 730, 1090 }, { 530, 1840 }, { 270, 2290 } };   /* oo oh ah eh ee */
static const float CH_PAN[N_HARM] = { 0.5f, 0.25f, 0.75f, 0.4f, 0.6f, 0.1f };
static void choir(float *l, float *r, int n, float gain, const float *p) {
  float vw = vd_clamp(p[0] + (E.bright - 0.35f) * 0.6f, 0, 1) * 4;
  int vi = (int)vw; if (vi > 3) vi = 3; float vf = vw - vi;
  float f1 = VOWEL_F[vi][0] + (VOWEL_F[vi + 1][0] - VOWEL_F[vi][0]) * vf, f2 = VOWEL_F[vi][1] + (VOWEL_F[vi + 1][1] - VOWEL_F[vi][1]) * vf;
  float cut = 250 + p[1] * p[1] * 7000 * (0.7f + 0.6f * E.bright);
  vd_svf_c clp, cf1, cf2; vd_svf_set(&clp, cut, 0.7f, sr); vd_svf_set(&cf1, f1, 5, sr); vd_svf_set(&cf2, f2, 7, sr);
  float dt[N_HARM][3], vgain[N_HARM];
  for (int k = 0; k < N_HARM; k++) {
    float hz = vd_mtof(E.harm[k] + drift_v[k] * 0.1f);
    for (int j = 0; j < 3; j++) { float det = (j - 1) * p[2] * 0.22f + drift_v[(k + j) & 7] * 0.05f; dt[k][j] = hz * vd_exp2(det / 12) / sr; }
    vgain[k] = (k == 0 ? 0.5f : 0.33f) / (1 + (E.harm[k] - 48) * 0.012f);
  }
  float br = p[3] * 0.25f;
  for (int i = 0; i < n; i++) {
    float sl = 0, sr_ = 0;
    for (int k = 0; k < N_HARM; k++) {
      float v = 0;
      for (int j = 0; j < 3; j++) { float *ph = &ch_ph[k][j]; *ph += dt[k][j]; if (*ph >= 1) *ph -= 1; v += vd_saw(*ph, dt[k][j]); }
      v *= vgain[k];
      sl += v * (1 - CH_PAN[k]); sr_ += v * CH_PAN[k];
    }
    float nl = rnd() * br, nr = rnd() * br;
    float al = vd_svf_tick(&ch_lp[0], &clp, sl).lp, ar = vd_svf_tick(&ch_lp[1], &clp, sr_).lp;
    float fl = vd_svf_tick(&ch_f1[0], &cf1, sl + nl).bp + 0.7f * vd_svf_tick(&ch_f2[0], &cf2, sl + nl).bp;
    float fr = vd_svf_tick(&ch_f1[1], &cf1, sr_ + nr).bp + 0.7f * vd_svf_tick(&ch_f2[1], &cf2, sr_ + nr).bp;
    l[i] += (al * 0.07f + fl * 0.14f) * gain; r[i] += (ar * 0.07f + fr * 0.14f) * gain;
  }
}

/* ---------------------------------------------------------------- CHIMES ---------------------------------------- */
#define CM_N 14
typedef struct { float cph, mph, cdt, mdt, idx, idx_c, amp, amp_c, gl, gr; } bell;
static bell bells[CM_N]; static int bell_rr;
static float pad_ph[3], pad_lfo, cm_rand;
static void strike(const float *p, float vel) {
  int j = 1 + (int)(rnd01() * (N_HARM - 1.001f));
  float note = E.harm[j] + 12;
  if (E.heard > 0 && rnd01() < 0.7f) note = near_chord(E.heard, 0) + (E.heard < 60 ? 12 : 0);
  if (rnd01() < 0.25f) note += 12;
  while (note > 96) note -= 12;
  int v = -1; for (int k = 0; k < CM_N; k++) if (bells[k].amp < 0.0005f) { v = k; break; }
  if (v < 0) { v = bell_rr; bell_rr = (bell_rr + 1) % CM_N; }
  bell *b = &bells[v];
  float metal = p[2], hz = vd_mtof(note);
  float ratio = metal < 0.5f ? 1 + metal * 2 : 3.5f + (metal - 0.5f) * 1.6f;   /* harmonic mallet → bell → odd metal */
  b->cdt = hz / sr; b->mdt = hz * ratio / sr; b->cph = 0; b->mph = 0;
  b->idx = 0.3f + metal * 3.2f * (0.5f + 0.5f * vel);
  float dec = p[1] * (note > 84 ? 0.5f : note > 72 ? 0.75f : 1.0f);
  b->amp = 0.22f * (0.35f + 0.65f * vel); b->amp_c = vd_coef(dec / 6.9f, sr); b->idx_c = vd_coef(dec / 18, sr);
  float pan = 0.5f + rnd() * 0.42f; b->gl = vd_sqrt(1 - pan); b->gr = vd_sqrt(pan);
}
static void chimes(float *l, float *r, int n, float gain, const float *p) {
  if (!E.freeze) {
    for (int k = 0; k < E.onsets; k++) strike(p, E.onset_vel);
    cm_rand += n / sr * p[0] * (0.15f + 2.5f * E.presence) * (0.5f + rnd01());   /* the room's presence, at random */
    while (cm_rand >= 1) { cm_rand -= 1; strike(p, 0.2f + 0.5f * rnd01()); }
  }
  float pdt[3] = { vd_mtof(E.harm[0]) / sr, vd_mtof(E.harm[2]) / sr, vd_mtof(E.harm[1] + drift_v[1] * 0.08f) / sr };
  float pg = p[3] * 0.09f;
  pad_lfo += n / sr * 0.07f; if (pad_lfo >= 1) pad_lfo -= 1;
  float breath = 0.7f + 0.3f * vd_sin(pad_lfo);
  for (int i = 0; i < n; i++) {
    float ol = 0, orr = 0;
    for (int k = 0; k < CM_N; k++) {
      bell *b = &bells[k]; if (b->amp < 0.0005f) continue;
      b->mph += b->mdt; if (b->mph >= 1) b->mph -= 1;
      b->cph += b->cdt; if (b->cph >= 1) b->cph -= 1;
      float s = vd_sin(b->cph + b->idx * vd_sin(b->mph) * 0.16f) * b->amp;
      b->amp *= b->amp_c; b->idx *= b->idx_c;
      ol += s * b->gl; orr += s * b->gr;
    }
    float pad = 0;
    for (int j = 0; j < 3; j++) { pad_ph[j] += pdt[j]; if (pad_ph[j] >= 1) pad_ph[j] -= 1; pad += vd_sin(pad_ph[j]) * (j == 0 ? 1.0f : 0.6f); }
    pad *= pg * breath;
    l[i] += (ol * 2.5f + pad * 1.6f) * gain; r[i] += (orr * 2.5f + pad * 1.6f) * gain;
  }
}

/* ---------------------------------------------------------------- the switch ------------------------------------ */
static int cur = -1, prev = -1; static float xf = 1, gain_s;
void engines_init(float rate) {
  sr = rate; nz.s = 0xC0FFEE11u;
  for (int i = 0; i <= HN; i++) hann[i] = 0.5f - 0.5f * vd_sin(0.25f + (float)i / HN);
}
static void run(int e, const float *exc, float *l, float *r, int n, float g, const halo_sound *s) {
  const float *p = s->ep[e];
  switch (e) {
    case EN_BLOOM: bloom(exc, l, r, n, g, p); break;
    case EN_CLOUD: cloud(exc, l, r, n, g, p); break;
    case EN_CHOIR: choir(l, r, n, g, p); break;
    case EN_CHIMES: chimes(l, r, n, g, p); break;
  }
}
void engines_run(const float *exc, float *l, float *r, int n, const halo_sound *s) {
  int e = (int)(s->engine + 0.5f); if (e < 0 || e >= EN_COUNT) e = 0;
  if (cur < 0) cur = e;
  if (e != cur) { prev = cur; cur = e; xf = 0; }
  drift_tick(n, s->drift);
  float tgt = s->level * E.swell;
  float g0 = gain_s, g1 = gain_s + (tgt - gain_s) * (1 - vd_coef(0.08f, sr / n));
  gain_s = g1;
  float g = (g0 + g1) * 0.5f;
  for (int i = 0; i < n; i++) l[i] = r[i] = 0;
  if (xf < 1 && prev >= 0) {
    xf += n / (sr * 2.5f); if (xf > 1) xf = 1;
    run(prev, exc, l, r, n, g * vd_sin(0.25f * (1 - xf)), s);
    run(cur, exc, l, r, n, g * vd_sin(0.25f * xf), s);
  } else run(cur, exc, l, r, n, g, s);
}

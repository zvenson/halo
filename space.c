/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). HALO, see LICENSE. */
/* space.c – HALO's space and master. Audio flow only.
   SPACE: an 8-line feedback delay network – Hadamard mixing, a low-pass in every line (DAMP), each line's read point
   swaying slowly (MOD) so the tail never rings metallic, T60 = DECAY (2 .. 60 s), line lengths × SIZE. SHIMMER: the
   tail's sum through a two-head pitch shifter an octave up, fed back into the network – the tail rises. MIC: the room's
   own sound (levelled, gated, ducked by the guard) sent into the space – the people's voices come back as a halo.
   MIX: dry / wet (equal power), WIDTH: mid / side.
   MASTER: DC block, OUTPUT, a peak limiter at -1 dB (1 ms attack, 250 ms release), soft clip. */
#include "halo.h"

#define FL 8
#define FMAX 8192
static float sr;
static float fd[FL][FMAX], fdl[FL], flfo[FL]; static int fw;
static const float FLEN[FL] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
static const float FRATE[FL] = { 0.11f, 0.13f, 0.17f, 0.19f, 0.23f, 0.07f, 0.29f, 0.31f };
#define SH_N 4096
static float sh_buf[SH_N], sh_ph, sh_hp, sh_lp, sh_fb; static int sh_w;
static float pre_hp_l, pre_hp_r;

void space_init(float rate) {
  sr = rate;
  for (int i = 0; i < FL; i++) flfo[i] = i * 0.137f;
}

static inline float rd(const float *b, float pos) {
  if (pos < 0) pos += FMAX;
  int i0 = (int)pos; float f = pos - (float)i0; int i1 = i0 + 1; if (i1 >= FMAX) i1 = 0;
  return b[i0] + (b[i1] - b[i0]) * f;
}

void space_run(float *l, float *r, const float *mic, int n, const halo_sound *s) {
  float scale = (0.55f + s->size * 2.6f) * sr / 48000.0f;
  float t60 = 2 + s->decay * s->decay * 58;
  float len[FL], g[FL];
  for (int i = 0; i < FL; i++) {
    len[i] = vd_clamp(FLEN[i] * scale, 64, FMAX - 64);
    g[i] = vd_exp2(-9.9658f * len[i] / (sr * t60));
  }
  float dc = 1 - vd_coef(1.0f / (VD_TAU * (1500 + (1 - s->damp) * (1 - s->damp) * 12000)), sr);
  float mod = s->mod * 14 * sr / 48000.0f;
  float wet = vd_sin(0.25f * vd_clamp(s->mix, 0, 1)), dry = vd_sin(0.25f * (1 - vd_clamp(s->mix, 0, 1)));
  float shim = vd_clamp(s->shimmer, 0, 1) * 0.42f, micg = vd_clamp(s->mic, 0, 1) * 0.8f;
  float lfo_inc[FL]; for (int i = 0; i < FL; i++) lfo_inc[i] = FRATE[i] / sr;
  float a_hp = 1 - vd_coef(1.0f / (VD_TAU * 120), sr);
  float sh_hpa = 1 - vd_coef(1.0f / (VD_TAU * 300), sr), sh_lpa = 1 - vd_coef(1.0f / (VD_TAU * 6000), sr);
  const float W = 2048, H = 0.35355339f;                     /* the shifter's window; 1/sqrt(8) */
  float wd = vd_clamp(s->width, 0, 1) * 1.4f;
  for (int i = 0; i < n; i++) {
    float il = l[i], ir = r[i], m = mic[i] * micg;
    pre_hp_l += (il - pre_hp_l) * a_hp; pre_hp_r += (ir - pre_hp_r) * a_hp;
    float sl = il - pre_hp_l * 0.7f + m + sh_fb * shim, sr_ = ir - pre_hp_r * 0.7f + m * 0.9f + sh_fb * shim;
    /* read, damp */
    float y[FL];
    for (int k = 0; k < FL; k++) {
      flfo[k] += lfo_inc[k]; if (flfo[k] >= 1) flfo[k] -= 1;
      float p = (float)fw - len[k] - mod * (1 + vd_sin(flfo[k]));
      float v = rd(fd[k], p);
      fdl[k] += (v - fdl[k]) * dc; y[k] = fdl[k];
    }
    float outl = y[0] + y[2] + y[4] + y[6] - (y[1] + y[5]) * 0.3f, outr = y[1] + y[3] + y[5] + y[7] - (y[0] + y[4]) * 0.3f;
    /* Hadamard 8 */
    for (int h = 1; h < FL; h <<= 1)
      for (int a = 0; a < FL; a += h << 1)
        for (int b = a; b < a + h; b++) { float u = y[b], w = y[b + h]; y[b] = u + w; y[b + h] = u - w; }
    for (int k = 0; k < FL; k++) {
      float in = (k & 1) ? sr_ : sl;
      float v = y[k] * H * g[k] + in * ((k & 2) ? 0.35f : -0.35f);
      fd[k][fw] = v > 4 ? 4 : v < -4 ? -4 : v;
    }
    if (++fw >= FMAX) fw = 0;
    outl *= 0.42f; outr *= 0.42f;
    /* the shimmer: the tail an octave up (two heads, crossfaded), band-limited, into the network again */
    if (shim > 0.0001f) {
      float t = (outl + outr) * 0.5f;
      sh_hp += (t - sh_hp) * sh_hpa; t -= sh_hp;
      sh_buf[sh_w] = t;
      sh_ph += 1.0f / W; if (sh_ph >= 1) sh_ph -= 1;
      float o = 0;
      for (int h = 0; h < 2; h++) {
        float ph = sh_ph + h * 0.5f; if (ph >= 1) ph -= 1;
        float d = (1 - ph) * W + 4, rp = (float)sh_w - d; if (rp < 0) rp += SH_N;
        int i0 = (int)rp; float f = rp - (float)i0; int i1 = (i0 + 1) & (SH_N - 1);
        float v = sh_buf[i0] + (sh_buf[i1] - sh_buf[i0]) * f;
        o += v * vd_sin(ph * 0.5f);                          /* (a sine window: 0 at the jump) */
      }
      sh_w = (sh_w + 1) & (SH_N - 1);
      sh_lp += (o - sh_lp) * sh_lpa;
      sh_fb = vd_tanh(sh_lp * 1.2f);
    } else sh_fb = 0;
    float ol = il * dry + outl * wet, orr = ir * dry + outr * wet;
    float mid = (ol + orr) * 0.5f, side = (ol - orr) * 0.5f * wd;
    l[i] = mid + side; r[i] = mid - side;
  }
}

static float dc_l, dc_r, lim_env = 0, fade;
void master_run(float *l, float *r, int n) {
  float out = db2lin(V.out_db), ceil = 0.89f;
  float att = 1 - vd_coef(0.001f, sr), rel = 1 - vd_coef(0.25f, sr), fd_ = 1 - vd_coef(1.2f, sr);
  float peak = 0;
  for (int i = 0; i < n; i++) {
    float a = l[i], b = r[i];
    dc_l += (a - dc_l) * 0.0004f; dc_r += (b - dc_r) * 0.0004f; a -= dc_l; b -= dc_r;
    fade += (1 - fade) * fd_;
    a *= out * fade; b *= out * fade;
    float m = vd_maxf(vd_absf(a), vd_absf(b));
    lim_env += (m - lim_env) * (m > lim_env ? att : rel);
    float gr = lim_env > ceil ? ceil / lim_env : 1;
    a = vd_tanh(a * gr * 1.05f) * 0.95f; b = vd_tanh(b * gr * 1.05f) * 0.95f;
    l[i] = a; r[i] = b;
    float p = vd_maxf(vd_absf(a), vd_absf(b)); if (p > peak) peak = p;
  }
  V.out_peak = peak;
  if (peak > 0.94f) V.clip_until = (float)(v_ms() + 800);
}

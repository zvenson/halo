/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). HALO, see LICENSE. */
/* listen.c – HALO's ears. Audio flow only.
   IN → gain → high-pass 90 Hz → three envelopes (fast 2/60 ms, mid 30/300 ms, the brightness above 2.5 kHz).
   FLOOR: the room's own level, learnt all the time – it rises slowly (ADAPT, default 60 s: a busy station becomes the new
   quiet) and falls fast (2 s). Everything is measured ABOVE the floor: the threshold is SENSE dB over it, PRESENCE is how
   far above it the room is (0..1), smoothed with RISE / FALL into the drone's swell. So the drone hearing itself in the
   room raises the floor and calms down instead of feeding itself.
   ONSETS: the fast envelope jumping over the mid one (x2) above the threshold, 120 ms apart.
   PITCH: YIN at sr/4 (60 Hz - 1 kHz) every ~21 ms. A pitch held steady long enough (FOLLOW: 0.25 .. 3.75 s) moves the
   chord's root to that scale degree. MOVE: the chord walks to another degree by itself every 20 .. 120 s.
   GUARD: a feedback howl is a RIGID sine – one pitch within 8 cents for 1.5 s (no voice does that: vibrato, jitter):
   the mic's feed drops 18 dB for 8 s.
   EXCITATION: the input levelled (AGC, at most +18 dB, with GUARD +6) and gated by the threshold – what BLOOM's strings
   and CLOUD's buffer get, and the MIC send into the space.
   The keys play a test voice (a sung "ah") into the input – so all of this can be tried without a microphone. */
#include "halo.h"

halo_ears E;
static float sr;

/* ---------------------------------------------------------------- scales and the chord --------------------------- */
static const int8_t SC[N_SCALES][8] = {
  { 7, 0, 2, 4, 5, 7, 9, 11 },  /* MAJOR */      { 7, 0, 2, 4, 6, 7, 9, 11 },  /* LYDIAN */
  { 7, 0, 2, 4, 5, 7, 9, 10 },  /* MIXO */       { 7, 0, 2, 3, 5, 7, 9, 10 },  /* DORIAN */
  { 7, 0, 2, 3, 5, 7, 8, 10 },  /* MINOR */      { 7, 0, 1, 3, 5, 7, 8, 10 },  /* PHRYGIAN */
  { 5, 0, 2, 4, 7, 9 },         /* PENTA */      { 5, 0, 3, 5, 7, 10 },        /* PENTA MIN */
  { 5, 0, 2, 3, 7, 8 },         /* HIRAJOSHI */  { 6, 0, 2, 4, 6, 8, 10 } };   /* WHOLE */
static const char *const SC_N[N_SCALES] = { "MAJOR", "LYDIAN", "MIXO", "DORIAN", "MINOR", "PHRYG", "PENTA", "PENTA m", "HIRA", "WHOLE" };
const char *scale_name(int s) { return SC_N[s < 0 || s >= N_SCALES ? 0 : s]; }
int scale_count(int s) { return SC[s < 0 || s >= N_SCALES ? 0 : s][0]; }
int scale_has(int s, int pc) {
  if (s < 0 || s >= N_SCALES) s = 0;
  pc = ((pc % 12) + 12) % 12;
  for (int i = 1; i <= SC[s][0]; i++) if (SC[s][i] == pc) return 1;
  return 0;
}
static int ifloor(float x) { int i = (int)x; return (float)i > x ? i - 1 : i; }
static int key_, scale_, octave_, voicing_ = -1, chord_off;     /* chord_off: the chord root above the key, 0..11 */
static float snap(float x) {                              /* the scale note nearest x (MIDI) */
  int best = ifloor(x); float bd = 99;
  for (int m = ifloor(x) - 3; m <= ifloor(x) + 4; m++)
    if (scale_has(scale_, m - key_)) { float d = vd_absf((float)m - x); if (d < bd) { bd = d; best = m; } }
  return (float)best;
}
/* the voicings: intervals over the chord root, snapped into the scale (3.6 = "a third, major if the scale has it") */
static const float VOICE_IV[VC_COUNT][N_HARM] = {
  { -12, 0, 7, 15.6f, 19, 26 },        /* OPEN: bass, root, fifth, tenth, twelfth, ninth */
  { -12, 0, 7, 12, 19, 24 },           /* FIFTHS: open and hollow, a tanpura's */
  { -12, 0, 3.6f, 7, 10.6f, 14 },      /* CLOSE: a seventh chord with a ninth */
  { -12, 0, 2, 5, 7, 14 } };           /* CLUSTER: seconds, a cloud */
static float harm_t[N_HARM], harm_c[N_HARM];
static void chord_build(void) {
  float base = 36 + 12 * octave_ + key_;                  /* OCTAVE LOW..TOP: the root around C2 .. C5, the bass an octave under */
  float root = base + chord_off; if (chord_off >= 6) root -= 12;
  for (int i = 0; i < N_HARM; i++) harm_t[i] = snap(root + VOICE_IV[voicing_][i]);
  harm_t[1] = root; harm_t[0] = root - 12;               /* (the root itself is in the scale already) */
  V.chord_pc = (key_ + chord_off) % 12;
  int deg = 0; for (int i = 1; i <= SC[scale_][0]; i++) if (SC[scale_][i] == chord_off) deg = i - 1;
  V.chord_deg = deg;
}

/* ---------------------------------------------------------------- pitch (YIN at sr/4) ---------------------------- */
#define YW 256
#define YTMAX 200                      /* at 12 kHz: 60 Hz */
#define YTMIN 12                       /* 1 kHz */
static float yring[YW + YTMAX], yd[YTMAX + 1], yl1, yl2, yl3;
static int ypos, ydec;
static float cand = -1, stable_ms, howl_ms, howl_note; static int cand_used;
static float yin(float *conf) {
  float best = 1e9f, run = 0; int bt = 0;
  for (int t = YTMIN; t <= YTMAX; t++) {
    float d = 0; const float *p = yring, *q = yring + t;
    for (int j = 0; j < YW; j += 2) { float a = p[j] - q[j], b = p[j + 1] - q[j + 1]; d += a * a + b * b; }
    run += d; yd[t] = d * t / (run + 1e-12f);
  }
  for (int t = YTMIN + 1; t < YTMAX; t++)
    if (yd[t] < 0.15f && yd[t] <= yd[t - 1] && yd[t] <= yd[t + 1]) { bt = t; best = yd[t]; break; }
  if (!bt) { for (int t = YTMIN; t <= YTMAX; t++) if (yd[t] < best) { best = yd[t]; bt = t; } if (best > 0.3f) bt = 0; }
  *conf = 1 - best;
  if (!bt) return 0;
  float t = (float)bt;
  if (bt > YTMIN && bt < YTMAX) { float a = yd[bt - 1], b = yd[bt], c = yd[bt + 1], den = a - 2 * b + c; if (vd_absf(den) > 1e-9f) t += 0.5f * (a - c) / den; }
  return sr * 0.25f / t;
}

/* ---------------------------------------------------------------- the test voice (keys) -------------------------- */
static float tv_ph, tv_vib, tv_env, tv_hz; static vd_svf tv_f1, tv_f2; static vd_svf_c tv_c1, tv_c2;
static float test_voice(void) {
  int gate = V.key_gate;
  float tgt = gate ? 1.0f : 0.0f;
  tv_env += (tgt - tv_env) * (gate ? 0.0008f : 0.0002f);
  if (tv_env < 0.0001f && !gate) return 0;
  if (gate) { float hz = vd_mtof((float)V.key_note); tv_hz = tv_hz > 0 ? tv_hz + (hz - tv_hz) * 0.002f : hz; }
  tv_vib += 5.3f / sr; if (tv_vib >= 1) tv_vib -= 1;
  float hz = tv_hz * (1 + 0.012f * vd_sin(tv_vib)), dt = hz / sr;
  tv_ph += dt; if (tv_ph >= 1) tv_ph -= 1;
  float x = vd_saw(tv_ph, dt);
  float a = vd_svf_tick(&tv_f1, &tv_c1, x).bp, b = vd_svf_tick(&tv_f2, &tv_c2, x).bp;
  return (a * 1.0f + b * 0.6f) * tv_env * 0.35f;
}

/* ---------------------------------------------------------------- the block -------------------------------------- */
static float hp1, hp2, ef, em, eb, bhp, gate_s, agc_s = 1, presence_s, bright_s = 0.3f;
static float floor_db = -60, start_s;
static int gate_on;
static uint32_t since_onset = 1 << 30;
static vd_noise nz;
static double move_t;

void listen_init(float rate) {
  sr = rate;
  vd_svf_set(&tv_c1, 730, 6, sr); vd_svf_set(&tv_c2, 1090, 7, sr);
  for (int i = 0; i < N_HARM; i++) harm_t[i] = harm_c[i] = 36;
  V.floor_db = floor_db = -60; nz.s = 0x5EED1234u;
}

void listen_block(const float *raw, float *exc, float *mic, int n, const halo_sound *s) {
  /* the harmony's frame: key, scale, octave, voicing – rebuilt when one of them changed */
  int k = (int)(s->key + 0.5f) % 12, sc = (int)(s->scale + 0.5f), oc = (int)(s->octave + 0.5f), vc = (int)(s->voicing + 0.5f);
  if (sc < 0 || sc >= N_SCALES) sc = 0;
  if (vc < 0 || vc >= VC_COUNT) vc = 0;
  if (k != key_ || sc != scale_ || oc != octave_ || vc != voicing_) {
    int first = voicing_ < 0;
    key_ = k; scale_ = sc; octave_ = oc; voicing_ = vc;
    if (!scale_has(scale_, chord_off)) chord_off = ((int)snap((float)(key_ + chord_off)) - key_ + 24) % 12;
    chord_build();
    if (first) for (int i = 0; i < N_HARM; i++) harm_c[i] = harm_t[i];
  }
  const float a_hp = 1 - vd_coef(1.0f / (VD_TAU * 90), sr), a_b = 1 - vd_coef(1.0f / (VD_TAU * 2500), sr);
  const float fa = 1 - vd_coef(0.002f, sr), fr = 1 - vd_coef(0.06f, sr), ma = 1 - vd_coef(0.03f, sr), mr = 1 - vd_coef(0.3f, sr);
  const float ga = 1 - vd_coef(0.02f, sr), gr = 1 - vd_coef(0.4f, sr);
  float gain = db2lin(V.gain_db);
  float thr_db = 24 - vd_clamp(V.sense, 0, 1) * 18;      /* SENSE: 24 .. 6 dB over the floor */
  float thr_lin = db2lin(floor_db + thr_db);
  int guard = V.guard > 0.5f && V.now < V.guard_until;
  float gmax = V.guard > 0.5f ? 2.0f : 8.0f;
  float peak = 0;
  E.onsets = 0; E.onset_vel = 0;
  int ydiv = (int)(sr / 12000 + 0.5f); if (ydiv < 1) ydiv = 1;
  for (int i = 0; i < n; i++) {
    float x = (raw ? raw[i] : 0) * gain + test_voice() * V.keys_level;
    /* high-pass (two one-poles: rumble, handling, the wind) */
    hp1 += (x - hp1) * a_hp; float y = x - hp1;
    hp2 += (y - hp2) * a_hp; y = y - hp2;
    float ay = vd_absf(y); if (ay > peak) peak = ay;
    ef += (ay - ef) * (ay > ef ? fa : fr);
    em += (ay - em) * (ay > em ? ma : mr);
    bhp += (y - bhp) * a_b; float hb = vd_absf(y - bhp);
    eb += (hb - eb) * mr;
    /* onset */
    since_onset++;
    if (ef > em * 2.0f && ef > thr_lin && since_onset > (uint32_t)(0.12f * sr)) {
      since_onset = 0; E.onsets++; float v = vd_clamp(ef / (thr_lin * 6), 0.15f, 1); if (v > E.onset_vel) E.onset_vel = v;
    }
    /* the gate and the levelled excitation */
    float gt = gate_on ? 1.0f : 0.0f;
    gate_s += (gt - gate_s) * (gt > gate_s ? ga : gr);
    float o = y * agc_s * gate_s * (guard ? 0.125f : 1.0f);
    exc[i] = o; mic[i] = o;
    /* pitch: three one-poles (~1.5 kHz), every ydiv-th sample */
    yl1 += (y - yl1) * 0.18f; yl2 += (yl1 - yl2) * 0.18f; yl3 += (yl2 - yl3) * 0.18f;
    if (++ydec >= ydiv) {
      ydec = 0; yring[ypos++] = yl3;
      if (ypos >= YW + YTMAX) {
        float conf, hz = 0;
        if (gate_on && !V.freeze) hz = yin(&conf);
        for (int j = 0; j < YTMAX; j++) yring[j] = yring[YW + j];
        ypos = YTMAX;
        float dt_ms = YW * 1000.0f * ydiv / sr;
        if (hz > 0) {
          float note = 69 + 12 * vd_log2(hz / 440.0f);
          if (cand > 0 && vd_absf(note - cand) < 0.7f) { stable_ms += dt_ms; cand += (note - cand) * 0.2f; }
          else { cand = note; stable_ms = 0; cand_used = 0; }
          V.heard = cand; V.heard_until = V.now + i + sr * 1.5;
          /* the guard: a howl is a RIGID sine – no voice holds a pitch within 8 cents for 1.5 s (vibrato, jitter) */
          if (vd_absf(note - howl_note) < 0.08f && lin2db(em) > -36) howl_ms += dt_ms; else { howl_ms = 0; howl_note = note; }
          if (V.guard > 0.5f && howl_ms > 1500) { V.guard_until = V.now + i + sr * 8; V.guard_hits++; howl_ms = 0; }
          /* FOLLOW: a steady pitch moves the chord to its degree */
          float need = 250 + (1 - s->follow) * 3500;
          if (s->follow > 0.01f && !cand_used && stable_ms >= need) {
            cand_used = 1;
            int off = (((int)snap(cand) - key_) % 12 + 12) % 12;
            if (off != chord_off) { chord_off = off; chord_build(); }
          }
        } else { if (stable_ms > 0) stable_ms -= dt_ms * 0.5f; howl_ms = 0; }
      }
    }
  }
  /* once a block: the floor, the threshold, presence, brightness, AGC */
  float bsr = sr / (n > 0 ? n : 1);
  float em_db = lin2db(em);
  if (!V.freeze) {
    if (em_db > floor_db) floor_db += (em_db - floor_db) * (1 - vd_coef(vd_clamp(V.adapt, 5, 600), bsr));
    else floor_db += (em_db - floor_db) * (1 - vd_coef(2.0f, bsr));
  }
  floor_db = vd_clamp(floor_db, -84, -12);
  float over = em_db - floor_db - thr_db;
  if (gate_on ? over < -3 : over > 0) gate_on = !gate_on;
  float act = vd_clamp(over / 18 + (over > 0 ? 0.08f : 0), 0, 1);
  if (!V.freeze) {
    float t = act > presence_s ? s->rise : s->fall;
    presence_s += (act - presence_s) * (1 - vd_coef(vd_clamp(t, 0.05f, 120), bsr));
    if (gate_on) bright_s += (vd_clamp(eb / (em + 1e-9f) * 1.6f, 0, 1) - bright_s) * (1 - vd_coef(0.6f, bsr));
  }
  float tgt_agc = vd_clamp(0.12f / (em + 1e-6f), 0.25f, gmax);
  agc_s += (tgt_agc - agc_s) * (1 - vd_coef(0.25f, bsr));
  /* the level the room asks for */
  float idle = vd_clamp(s->idle, 0, 1), sw;
  switch ((int)(s->respond + 0.5f)) {
    case RS_CALM: sw = 1 - (1 - idle) * presence_s; break;            /* busy = soft, quiet = full */
    case RS_STEADY: sw = 1; break;
    default: sw = idle + (1 - idle) * presence_s; break;               /* SWELL: the room fills it */
  }
  start_s += (1 - start_s) * (1 - vd_coef(1.5f, bsr));               /* a soft start */
  /* MOVE: the chord wanders by itself */
  if (s->move > 0.01f && !V.freeze) {
    if (move_t <= 0) move_t = V.now + sr * (20 + (1 - s->move) * 100) * (0.7f + 0.6f * (0.5f + 0.5f * vd_noise_next(&nz)));
    if (V.now >= move_t) {
      move_t = 0;
      int cnt = SC[scale_][0], cur = 0; for (int j = 1; j <= cnt; j++) if (SC[scale_][j] == chord_off) cur = j - 1;
      int pick = cur;
      static const int8_t PREF7[] = { 0, 3, 4, 5, 3, 0, 4, 1 };          /* I IV V vi IV I V ii – the usual places */
      for (int tries = 0; tries < 6 && pick == cur; tries++) {
        uint32_t r = nz.s = nz.s * 1664525u + 1013904223u;
        pick = cnt == 7 ? PREF7[(r >> 16) & 7] : (int)((r >> 16) % (uint32_t)cnt);
      }
      chord_off = SC[scale_][pick + 1]; chord_build();
    }
  } else move_t = 0;
  /* glide toward the chord */
  float gc = 1 - vd_coef(vd_clamp(s->glide, 0.05f, 30), bsr);
  if (!V.freeze) for (int j = 0; j < N_HARM; j++) harm_c[j] += (harm_t[j] - harm_c[j]) * gc;
  /* hand over */
  E.swell = sw * start_s; E.act = act; E.presence = presence_s; E.bright = bright_s; E.gate = gate_s;
  E.heard = V.now < V.heard_until ? cand : 0; E.freeze = V.freeze;
  for (int j = 0; j < N_HARM; j++) { E.harm[j] = harm_c[j]; V.harm[j] = harm_t[j]; }
  V.in_db = lin2db(peak); V.floor_db = floor_db; V.thr_db = thr_db; V.act = act; V.presence = presence_s; V.swell = E.swell; V.bright = bright_s;
  V.onsets += E.onsets;
}

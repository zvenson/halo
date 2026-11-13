/* SPDX-License-Identifier: GPL-3.0-or-later – Copyright 2026 Sven Trogus (zvenson). HALO, see LICENSE. */
/* saver.c – the screensaver. 13.11.2026, Sven: "nach 1 min ohne Berührung … 3D-Räume, durch die man fährt, morpht, mit
   Realton aufs Mikro" – first acid, then (same day) "zu grell … eher Weltraum, langsamer, stylischer, nicht so wild".
   A slow flight through rooms that morph into each other – a round tunnel, a square corridor, a wide oval, a star tunnel,
   a diamond shaft – walls of soft nebula in a dim night gradient (black-blue, indigo, violet, rose, petrol, ice), stars
   that stream past. The room hears what HALO hears, gently:
     the mic's own waveform (SCOPE, written by the audio flow) bends the walls round the circle,
     PRESENCE a little more speed, BRIGHT the twist and the roll, every ONSET a soft glow, a HEARD pitch slides the colours.
   Control flow only: the picture is computed at 240 x 160 and doubled. The audio flow only fills the scope ring (single
   writer, VIBE_RELEASE), never waits. FPS (GLOBAL ▸ SAVER, default 24) = fewer pictures over the panel's SPI. */
#include "halo.h"

#define SW 240
#define SH 160
#define NR 5                                   /* rooms */
static uint16_t img[SW * SH];
static uint16_t pal[16][256], star[16];                  /* 16 light levels of a 256-colour palette, made new every frame */
static float costab[1024], wv[256];
static int ready;

halo_scope SCOPE;

/* rooms: 0 TUNNEL, 1 CORRIDOR, 2 OVAL, 3 STAR, 4 SHAFT */
static float t, travel, roll, flash, wamp = 1, hue, heard_hue, pres_s, bright_s;
static float room_t, morph = 1, fade = 1; static int room_a, room_b = 0, last_onsets = -1;
static double last_ms; static int last_preset = -1; static double name_until;
static uint32_t rng = 0x1234567u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static inline float cs(float turns) { return costab[(int)(turns * 1024.0f) & 1023]; }   /* cos of turns */
static inline float fabs_(float x) { return x < 0 ? -x : x; }
static float atan01(float y, float x) {        /* atan2 / 2π + 0.5, 0..1, ±0.002 */
  float ax = fabs_(x), ay = fabs_(y), mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
  if (mx < 1e-9f) return 0.5f;
  float a = mn / mx, s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;   /* atan(a), a in 0..1 */
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0) r = 3.14159274f - r;
  if (y < 0) r = -r;
  return r * 0.159154943f + 0.5f;
}
static inline uint16_t rgb565(float r, float g, float b) {
  int R = (int)(vd_clamp(r, 0, 1) * 31 + 0.5f), G = (int)(vd_clamp(g, 0, 1) * 63 + 0.5f), B = (int)(vd_clamp(b, 0, 1) * 31 + 0.5f);
  return (uint16_t)(R << 11 | G << 5 | B);
}

static void init(void) {
  for (int i = 0; i < 1024; i++) costab[i] = vd_sin(i / 1024.0f + 0.25f);
  ready = 1;
}


/* the mic's last ~23 ms (1024 samples of the ring, every 4th) → 256 wall offsets, normalised by a slow peak */
static void scope_read(void) {
  int h = SCOPE.head; VIBE_ACQUIRE();
  float pk = 1e-6f, tmp[256];
  for (int i = 0; i < 256; i++) { float x = SCOPE.w[(h - 256 + i) & (SCOPE_N - 1)]; tmp[i] = x; if (fabs_(x) > pk) pk = fabs_(x); }
  wamp += ((pk > wamp ? pk : wamp * 0.97f) - wamp) * 0.3f;          /* AGC: fast up, slow down */
  float g = 1.0f / (wamp < 0.004f ? 0.004f : wamp);                   /* (no more than ~48 dB: room noise stays small) */
  for (int i = 0; i < 256; i++) wv[i] += (tmp[i] * g - wv[i]) * 0.5f;
  /* the ends of the window meet at θ = 0: fade them so the wall has no seam */
  for (int i = 0; i < 16; i++) { float f = i / 16.0f; wv[i] *= f; wv[255 - i] *= f; }
}

/* space (13.11.2026, Sven: "Farben zu grell … eher Weltraum, langsamer, stylischer"): a cyclic gradient through the night
   – black-blue, indigo, violet, a dusty rose glow, petrol, pale ice – dim on the whole; a heard pitch slides along it */
static const float STOP[8][3] = {
  { 0.01f, 0.02f, 0.06f }, { 0.06f, 0.05f, 0.22f }, { 0.20f, 0.10f, 0.38f }, { 0.46f, 0.18f, 0.42f },
  { 0.12f, 0.08f, 0.30f }, { 0.04f, 0.22f, 0.34f }, { 0.30f, 0.62f, 0.70f }, { 0.05f, 0.10f, 0.20f } };
static void palette(void) {
  static float base[256][3];
  for (int i = 0; i < 256; i++) {
    float x = (i / 256.0f + hue) * 8; int a = (int)x; float f = x - a; a &= 7; int b = (a + 1) & 7;
    f = f * f * (3 - 2 * f);
    for (int c = 0; c < 3; c++) base[i][c] = STOP[a][c] + (STOP[b][c] - STOP[a][c]) * f;
  }
  for (int L = 0; L < 16; L++) {
    float f = L / 15.0f; f = f * (0.85f + 0.35f * flash) * fade;
    for (int i = 0; i < 256; i++) pal[L][i] = rgb565(base[i][0] * f, base[i][1] * f, base[i][2] * f);
    star[L] = rgb565(0.35f + 0.6f * f, 0.38f + 0.6f * f, 0.45f + 0.55f * f);
  }
}

static void render(void) {
  float w[NR] = { 0 };
  float m = morph * morph * (3 - 2 * morph);                          /* smoothstep */
  w[room_a] += 1 - m; w[room_b] += m;
  float cr = cs(roll), sr = cs(roll - 0.25f);
  float cx = 0.30f * cs(t * 0.013f) + 0.06f * cs(t * 0.031f), cy = 0.16f * cs(t * 0.019f + 0.3f);   /* the path curves */
  float twist = 0.25f * cs(t * 0.013f) + 0.4f * bright_s, wa = 0.04f + 0.10f * pres_s;
  float rep = 2, vs = 0.45f;
  float k = 2.0f / SH;
  int tp = (int)(t * 6);
  for (int py = 0; py < SH; py++) {
    float y0 = (py - SH * 0.5f) * k - cy;
    uint16_t *o = img + py * SW;
    for (int px = 0; px < SW; px++) {
      float x0 = (px - SW * 0.5f) * k - cx;
      float x = x0 * cr - y0 * sr, y = x0 * sr + y0 * cr;
      float ax = fabs_(x), ay = fabs_(y);
      float r2 = vd_sqrt(x * x + y * y), th = atan01(y, x);
      float rho = 0;
      if (w[0] > 0) rho += w[0] * r2;
      if (w[1] > 0) rho += w[1] * (ax > ay ? ax : ay);
      if (w[2] > 0) rho += w[2] * vd_sqrt(x * x * 0.45f + y * y * 1.7f);   /* a wide oval */
      if (w[3] > 0) rho += w[3] * r2 * (0.78f + 0.22f * cs(th * 5 + t * 0.1f));
      if (w[4] > 0) rho += w[4] * (ax + ay) * 0.72f;
      rho *= 1.0f + wv[(int)(th * 255.0f)] * wa;                     /* the real sound bends the wall */
      float depth = 1.0f / (rho + 0.015f);
      float u = th * rep + depth * twist * 0.12f;
      float v = depth * vs + travel;
      /* nebula: soft plasma, no hard edges */
      int pl = (int)(42 * (3 + cs(u + t * 0.01f) + cs(v * 0.5f + 0.3f * cs(u)) + cs(u - v * 0.25f + t * 0.02f)));   /* (periodic in u: no seam) */
      int idx = (pl + (int)(depth * 3) + tp) & 255;
      float fog = rho * 20.0f; if (fog > 15) fog = 15;                 /* the far middle fades into the dark */
      fog *= 0.78f + 0.22f * cs(v * 0.5f);                            /* slow, soft swells of light run towards you */
      int L = (int)fog;
      /* stars on the walls: rare cells, a small point in each, they stream past as you fly */
      int cu = (int)(u * 40), cv = (int)(v * 14);
      uint32_t h = (uint32_t)(cu * 73856093) ^ (uint32_t)(cv * 19349663); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
      if ((h & 63) == 0) { float fu = u * 40 - cu - 0.5f, fv = v * 14 - cv - 0.5f; if (fu * fu + fv * fv < 0.03f) { o[px] = star[L]; continue; } }
      o[px] = pal[L][idx];
    }
  }
}

/* ---------------------------------------------------------------- the demoscene (13.11.2026, Sven: "bissle Abwechslung,
   80er/90er Demoscene, geometrische Figuren die morphen, Lissajous-Figuren, Shepard Tones") – five scenes take turns,
   ~45 s each, a slow fade through black between them:
     TUNNEL    the nebula flight above
     LISSAJOUS a 3D Lissajous figure on a phosphor screen, its ratios gliding 1:2:3 → 3:2:1 → 3:4:5 …, the mic in its line
     MORPH     a dot-and-wire object turning in space: sphere → cube → torus → octahedron → spring → trefoil knot
     STARS     a starfield you fly through, copper bars slowly rolling behind it
     SHEPARD   the Shepard illusion for the eye: waves rise for ever, each doubles its frequency on the way up (an octave),
               fades out at the top while a new low one fades in below – the mic's waveform runs in every line
   The four glow scenes draw lines and dots into an intensity buffer that fades a little each picture (an afterglow). */
#define NS 5
static int scene, scene_first = 1; static float scene_t;
static uint16_t gl[SW * SH], gpal[256], crow[SH];
static void glow_decay(float rdt, float rate) {
  int keep = (int)(256 * (1 - vd_clamp(rdt * rate, 0, 0.95f)));
  for (int i = 0; i < SW * SH; i++) gl[i] = (uint16_t)((gl[i] * keep) >> 8);
}
static inline void dot(int x, int y, int a) {
  if ((unsigned)x >= SW || (unsigned)y >= SH) return;
  int i = y * SW + x, v = gl[i] + a; gl[i] = (uint16_t)(v > 65535 ? 65535 : v);
}
static void splat(float x, float y, int a) { int ix = (int)x, iy = (int)y; dot(ix, iy, a); dot(ix + 1, iy, a >> 1); dot(ix - 1, iy, a >> 1); dot(ix, iy + 1, a >> 1); dot(ix, iy - 1, a >> 1); }
static void line(float x0, float y0, float x1, float y1, int a) {
  float dx = x1 - x0, dy = y1 - y0, m = fabs_(dx) > fabs_(dy) ? fabs_(dx) : fabs_(dy);
  int n = (int)m + 1; if (n > 300) return;                            /* (a jump across the screen: no line) */
  float sx = dx / n, sy = dy / n;
  for (int i = 0; i <= n; i++) { int ix = (int)x0, iy = (int)y0; dot(ix, iy, a); dot(ix + 1, iy, a >> 2); dot(ix, iy + 1, a >> 2); x0 += sx; y0 += sy; }
}
/* each glow scene its own tint, from the night to a pale light – never neon */
static const float TINT[NS][4][3] = {
  { { 0 } },
  { { 0.01f, 0.02f, 0.05f }, { 0.04f, 0.22f, 0.30f }, { 0.25f, 0.65f, 0.70f }, { 0.75f, 0.92f, 0.95f } },   /* phosphor teal */
  { { 0.02f, 0.01f, 0.05f }, { 0.22f, 0.10f, 0.40f }, { 0.60f, 0.35f, 0.75f }, { 0.95f, 0.85f, 1.00f } },   /* violet */
  { { 0.01f, 0.01f, 0.04f }, { 0.10f, 0.14f, 0.35f }, { 0.45f, 0.55f, 0.85f }, { 0.92f, 0.95f, 1.00f } },   /* ice */
  { { 0.02f, 0.01f, 0.04f }, { 0.30f, 0.10f, 0.30f }, { 0.55f, 0.45f, 0.80f }, { 0.85f, 0.95f, 1.00f } } }; /* rose → ice */
static void glow_palette(void) {
  const float (*st)[3] = TINT[scene];
  for (int i = 0; i < 256; i++) {
    float x = i / 255.0f; x = vd_sqrt(x) * 3; int a = (int)x; if (a > 2) a = 2; float f = x - a;
    float c[3]; for (int k = 0; k < 3; k++) c[k] = (st[a][k] + (st[a + 1][k] - st[a][k]) * f) * (0.9f + 0.4f * flash) * fade;
    gpal[i] = rgb565(c[0], c[1], c[2]);
  }
}
static void glow_out(int copper) {
  for (int py = 0; py < SH; py++) {
    const uint16_t *g = gl + py * SW; uint16_t *o = img + py * SW, bg = copper ? crow[py] : gpal[0];
    for (int px = 0; px < SW; px++) { int i = g[px] >> 8; o[px] = copper && i < 10 ? bg : gpal[i]; }
  }
}
static inline float sn(float turns) { return cs(turns - 0.25f); }
static void rot_proj(float x, float y, float z, float ax, float ay, float sc, float *px, float *py, float *pz) {
  float ca = cs(ax), sa = sn(ax), cb = cs(ay), sb = sn(ay);
  float y1 = y * ca - z * sa, z1 = y * sa + z * ca;                  /* about x */
  float x2 = x * cb + z1 * sb, z2 = -x * sb + z1 * cb;               /* about y */
  float f = sc / (z2 + 3.2f);
  *px = SW * 0.5f + x2 * f * SH * 0.5f; *py = SH * 0.5f + y1 * f * SH * 0.5f; *pz = z2;
}
static float smooth(float x) { x = vd_clamp(x, 0, 1); return x * x * (3 - 2 * x); }

static void sc_lissajous(float rdt) {
  static const float R[6][3] = { { 1, 2, 3 }, { 3, 2, 1 }, { 3, 4, 5 }, { 5, 4, 3 }, { 2, 3, 4 }, { 1, 3, 5 } };
  glow_decay(rdt, 10);
  float p = scene_t / 14.0f; int a = (int)p % 6, b = (a + 1) % 6; float f = smooth((p - (int)p - 0.55f) / 0.45f);
  float fa = R[a][0] + (R[b][0] - R[a][0]) * f, fb = R[a][1] + (R[b][1] - R[a][1]) * f, fc = R[a][2] + (R[b][2] - R[a][2]) * f;
  float wa = 0.03f + 0.10f * pres_s, sc = 1.75f + 0.2f * flash;
  int N = 720, amt = 5200 + (int)(flash * 6000); float lx = 0, ly = 0, z;
  for (int i = 0; i <= N; i++) {
    float s = (float)i / N, r = 1 + wv[(i * 256 / N) & 255] * wa;
    float x = sn(fa * s + t * 0.011f) * r, y = sn(fb * s + 0.25f) * r, zz = sn(fc * s + t * 0.017f) * r;
    float px, py; rot_proj(x, y, zz, t * 0.013f, t * 0.021f, sc, &px, &py, &z);
    if (i) line(lx, ly, px, py, amt);
    lx = px; ly = py;
  }
  glow_palette(); glow_out(0);
}

#define NP 168
static void shape(int k, int i, float *o) {
  float s = (float)i / NP;
  switch (k) {
    case 0: { float y = 1 - 2 * (i + 0.5f) / NP, r = vd_sqrt(1 - y * y), ph = i * 0.381966f; o[0] = r * cs(ph); o[1] = y; o[2] = r * sn(ph); break; }   /* sphere */
    case 1: case 3: {                                                  /* cube / octahedron: points along the 12 edges */
      static const int8_t CE[12][6] = { {-1,-1,-1, 1,-1,-1}, {1,-1,-1, 1,1,-1}, {1,1,-1, -1,1,-1}, {-1,1,-1, -1,-1,-1},
        {-1,-1,1, 1,-1,1}, {1,-1,1, 1,1,1}, {1,1,1, -1,1,1}, {-1,1,1, -1,-1,1}, {-1,-1,-1, -1,-1,1}, {1,-1,-1, 1,-1,1}, {1,1,-1, 1,1,1}, {-1,1,-1, -1,1,1} };
      static const int8_t OE[12][6] = { {1,0,0, 0,1,0}, {0,1,0, -1,0,0}, {-1,0,0, 0,-1,0}, {0,-1,0, 1,0,0}, {1,0,0, 0,0,1}, {0,0,1, -1,0,0},
        {-1,0,0, 0,0,-1}, {0,0,-1, 1,0,0}, {0,1,0, 0,0,1}, {0,0,1, 0,-1,0}, {0,-1,0, 0,0,-1}, {0,0,-1, 0,1,0} };
      int e = i * 12 / NP; float f = (i - e * NP / 12.0f) / (NP / 12.0f);
      const int8_t *E = k == 1 ? CE[e] : OE[e]; float sc = k == 1 ? 0.62f : 1.05f;
      for (int c = 0; c < 3; c++) o[c] = (E[c] + (E[c + 3] - E[c]) * f) * sc;
      break; }
    case 2: { int a = i % 24, b = i / 24; float u = a / 24.0f, v = b / (NP / 24.0f), rr = 0.72f + 0.3f * cs(v);   /* torus */
      o[0] = rr * cs(u); o[1] = 0.3f * sn(v); o[2] = rr * sn(u); break; }
    case 4: o[0] = 0.7f * cs(s * 5); o[1] = -1 + 2 * s; o[2] = 0.7f * sn(s * 5); break;                            /* spring */
    default: { float u = s; o[0] = (sn(u) + 2 * sn(2 * u)) / 3; o[1] = (cs(u) - 2 * cs(2 * u)) / 3; o[2] = -sn(3 * u) / 3 * 1.4f; break; }   /* trefoil */
  }
}
static void sc_morph(float rdt) {
  glow_decay(rdt, 9);
  float p = scene_t / 7.5f; int a = (int)p % 6, b = (a + 1) % 6; float f = smooth((p - (int)p - 0.5f) / 0.5f);
  float wa = 0.05f + 0.12f * pres_s, sc = 2.2f + 0.3f * flash, P[3], Q[3], lx = 0, ly = 0, lz = 0, X[3], L[3] = { 0 };
  for (int i = 0; i < NP; i++) {
    shape(a, i, P); shape(b, i, Q);
    float r = 1 + wv[(i * 3) & 255] * wa;
    for (int c = 0; c < 3; c++) X[c] = (P[c] + (Q[c] - P[c]) * f) * r;
    float px, py, pz; rot_proj(X[0], X[1], X[2], t * 0.021f, t * 0.033f, sc, &px, &py, &pz);
    int br = (int)(15000 * (1.3f - pz * 0.35f));
    splat(px, py, br);
    if (i) { float d0 = X[0] - L[0], d1 = X[1] - L[1], d2 = X[2] - L[2]; if (d0 * d0 + d1 * d1 + d2 * d2 < 0.12f) line(lx, ly, px, py, br / 3); }
    lx = px; ly = py; lz = pz; L[0] = X[0]; L[1] = X[1]; L[2] = X[2];
  }
  (void)lz;
  glow_palette(); glow_out(0);
}

#define NST 220
static float stx[NST], sty[NST], stz[NST];
static void sc_stars(float rdt) {
  static int init_; if (!init_) { for (int i = 0; i < NST; i++) { stx[i] = (rnd() % 2000) / 1000.0f - 1; sty[i] = (rnd() % 2000) / 1000.0f - 1; stz[i] = 0.05f + (rnd() % 1000) / 1000.0f; } init_ = 1; }
  glow_decay(rdt, 14);
  float sp = 0.10f + 0.35f * pres_s + 0.15f * flash;
  for (int i = 0; i < NST; i++) {
    float z0 = stz[i]; stz[i] -= rdt * sp;
    if (stz[i] < 0.06f) { stx[i] = (rnd() % 2000) / 1000.0f - 1; sty[i] = (rnd() % 2000) / 1000.0f - 1; stz[i] = 1; continue; }
    float k0 = SH * 0.35f / z0, k1 = SH * 0.35f / stz[i];
    float x0 = SW * 0.5f + stx[i] * k0, y0 = SH * 0.5f + sty[i] * k0, x1 = SW * 0.5f + stx[i] * k1, y1 = SH * 0.5f + sty[i] * k1;
    int br = (int)(9000 / (stz[i] + 0.1f)); if (br > 60000) br = 60000;
    line(x0, y0, x1, y1, br);
  }
  /* copper bars: three soft bars rolling behind the stars */
  static const float CB[3][3] = { { 0.30f, 0.12f, 0.40f }, { 0.08f, 0.30f, 0.40f }, { 0.40f, 0.18f, 0.30f } };
  for (int py = 0; py < SH; py++) {
    float c[3] = { 0.01f, 0.01f, 0.035f };
    for (int k = 0; k < 3; k++) {
      float yk = SH * 0.5f + sn(t * 0.017f + k * 0.33f) * SH * 0.38f, d = (py - yk) / 11.0f;
      if (d > -1 && d < 1) { float g = (1 - d * d) * (0.35f + 0.4f * flash); for (int j = 0; j < 3; j++) c[j] += CB[k][j] * g; }
    }
    crow[py] = rgb565(c[0] * fade, c[1] * fade, c[2] * fade);
  }
  glow_palette(); glow_out(1);
}

static void sc_shepard(float rdt) {
  glow_decay(rdt, 7);
  const int NL = 9; float rise = scene_t * (0.035f + 0.04f * pres_s);   /* one octave per line spacing */
  float wa = 0.25f + 0.5f * pres_s;
  for (int l = 0; l < NL; l++) {
    float pos = (l + rise) / NL; pos -= (int)pos;                      /* 0 = bottom, 1 = top, endless */
    float bell = sn(pos * 0.5f); bell *= bell;                         /* fades in below, out above */
    float freq = 1.5f * (float)(1 << 0) * vd_exp2(pos * NL * 0.5f);   /* doubles every two lines on the way up */
    float y0 = SH * (0.92f - 0.84f * pos);
    int amt = (int)(9000 * bell * (1 + flash));
    float lx = 0, ly = 0;
    for (int px = 0; px <= SW; px += 2) {
      float s = (float)px / SW, w = wv[(int)(s * 255)] * wa;
      float y = y0 + (sn(s * freq + t * 0.03f) * 0.6f + w) * SH * 0.05f * (0.5f + bell);
      if (px) line(lx, ly, (float)px, y, amt);
      lx = (float)px; ly = y;
    }
  }
  glow_palette(); glow_out(0);
}

void saver_reset(void) { last_ms = 0; flash = 0.4f; scene_t = 0; scene_first = 1; last_onsets = -1; }   /* it just started */

void saver_draw(uint16_t *fb) {
  if (!ready) init();
  double now = v_ms();
  float dt = last_ms > 0 && now > last_ms ? (float)(now - last_ms) * 0.001f : 1 / 60.0f;
  if (dt > 0.2f) dt = 0.2f;
  /* FPS: fewer new pictures, the same speed – an unchanged picture is not sent again */
  int fps = (int)(V.saver_fps + 0.5f), redraw = 1;
  static float acc_t; acc_t += dt;
  if (fps > 0 && last_ms > 0) { float per = 1.0f / (fps == 1 ? 24 : 12); if (acc_t < per) redraw = 0; }
  float rdt = acc_t; if (redraw) acc_t = 0;
  last_ms = now;

  /* what the ears found (audio wrote it; floats with one writer) */
  float react = V.saver_react;
  pres_s += (V.presence * react - pres_s) * 0.08f; bright_s += (V.bright * react - bright_s) * 0.05f;
  int on = V.onsets;
  if (last_onsets < 0) last_onsets = on;
  if (on != last_onsets) {
    int n = on - last_onsets; last_onsets = on;
    if (react > 0.01f) { flash = vd_clamp(flash + 0.18f * react * (n > 3 ? 3 : n), 0, 0.6f); room_t += 0.5f; }   /* a soft glow */
  }
  if (V.now < V.heard_until && V.heard > 0 && react > 0.01f) heard_hue = (float)((int)(V.heard + 0.5f) % 12) / 12.0f;
  float dh = heard_hue - hue; dh -= (float)(int)(dh + (dh < 0 ? -0.5f : 0.5f));   /* the short way round */
  hue += dh * 0.004f + dt * 0.004f;

  t += dt;
  travel += dt * (0.12f + 0.35f * pres_s + 0.1f * flash);
  roll += dt * (0.004f + 0.012f * bright_s + 0.012f * cs(t * 0.007f));
  flash *= 1 - vd_clamp(dt * 1.2f, 0, 1);
  /* the tunnel's rooms: one holds ~20 s, then morphs into the next over 10 s */
  room_t += dt * (1 + 0.5f * pres_s);
  if (morph >= 1 && room_t > 20) { room_a = room_b; do room_b = (int)(rnd() % NR); while (room_b == room_a); morph = 0; room_t = 0; }
  if (morph < 1) { morph += dt / 10; if (morph > 1) { morph = 1; room_a = room_b; } }
  /* the scenes: ~45 s each, 2 s out to black and 2 s in */
  const float DUR = 45;
  scene_t += dt;
  if (scene_t > DUR) { scene_t = 0; scene = (scene + 1) % NS; for (int i = 0; i < SW * SH; i++) gl[i] = 0; }
  fade = smooth(scene_t / 2) * smooth((DUR - scene_t) / 2);
  if (scene_first) { fade = smooth(scene_t / 2); if (scene_t > 2) scene_first = 0; }

  if (redraw) {
    scope_read();
    switch (scene) {
      case 0: palette(); render(); break;
      case 1: sc_lissajous(rdt); break;
      case 2: sc_morph(rdt); break;
      case 3: sc_stars(rdt); break;
      default: sc_shepard(rdt); break;
    }
  }

  /* doubled to 480 x 320 */
  uint32_t *d = (uint32_t *)fb;
  for (int py = 0; py < SH; py++) {
    const uint16_t *s = img + py * SW;
    uint32_t *r0 = d + py * 2 * (VIBE_SCREEN_W / 2), *r1 = r0 + VIBE_SCREEN_W / 2;
    for (int px = 0; px < SW; px++) { uint32_t p = s[px]; p |= p << 16; r0[px] = p; r1[px] = p; }
  }
  /* a program change (WANDER, a pad) shows its name for a while */
  if (V.preset != last_preset) { if (last_preset >= 0) name_until = now + 4000; last_preset = V.preset; }
  if (now < name_until) {
    char b[24]; char n[8]; b[0] = 0;
    { char *p = b; const char *q = vg_itoa(V.preset + 1, n); while (*q) *p++ = *q++; *p++ = ' '; q = prog_name(V.preset); while (*q && p < b + 22) *p++ = *q++; *p = 0; }
    vg_text(b, 241, 291, VG_BOLD, 0x000000, VG_CENTER); vg_text(b, 240, 290, VG_BOLD, 0xFFFFFF, VG_CENTER);
  }
}

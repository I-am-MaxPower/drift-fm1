/* SPDX-License-Identifier: GPL-3.0-only */
/* DRIFT: an ambient drone and a Euclidean pulse under FoMni's chord harp. Included by omni.c (the same
 * compilation unit: it shares omni.c's state, its command queue and its mix buses). Runs in the audio
 * interrupt like the rest of the render: float only, no libm (fastmath.h), nothing allocated.
 *
 *   DRONE   four voices, each two detuned oscillators on one soft wave: the root (C2..B2), its fifth,
 *           the root an octave up, and a colour voice (the chord's third, C3..B3). FOLLOW takes the root
 *           from the chord button and glides to each new one; FIXED stays on KEY while the colour voice
 *           still follows the chords (a pedal tone). Two one-pole lowpasses a side, opened by TONE,
 *           moved by BREATHE (a slow free LFO) and by the SWELL lane. It sounds while the pulse plays or
 *           a chord is held, fading in and out over a few seconds.
 *   PULSE   four lanes, each a Euclidean rhythm (HITS spread as evenly as they go over STEPS, turned by
 *           ROTATE), stepping together at RATE. Each lane has its own length, so they drift against each
 *           other and meet again (the polymeter is the point).
 *             LOW     a soft thump tuned to the chord root
 *             MALLET  bell-like pings on the chord's tones, walking up through them
 *             SHAKER  short filtered noise
 *             SWELL   no sound: each hit opens the drone's filter and lifts its level, then lets go
 *           VARY humanises: softer, uneven hits, a few left out, the mallet straying to other tones. */

/* ------------------------------------------------------------ shared --- */
#define DW_N 256                                     /* wave table length (a power of two) */
static float dw_warm[DW_N + 1], dw_sine[DW_N + 1];   /* +1: the guard point for the interpolation */
static uint32_t rng = 0x9E3779B9u;

static inline float rndf(void)                       /* 0..1 */
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float)(rng >> 8) * (1.0f / 16777216.0f);
}

static inline float tbl(const float *w, float ph)    /* ph 0..DW_N */
{
    int i = (int)ph;
    float f = ph - (float)i;
    return w[i] + (w[i + 1] - w[i]) * f;
}
static inline float tadv(float ph, float inc)
{
    ph += inc;
    while (ph >= (float)DW_N)
        ph -= (float)DW_N;
    return ph;
}
static float hz_inc(float midi_note) { return 440.0f * tunefac * fm_exp2f((midi_note - 69.0f) * (1.0f / 12.0f)) * ((float)DW_N / OM_SR); }

/* a one-pole lowpass coefficient for a cutoff in Hz (exact: 1 - e^(-2 pi fc / sr)) */
static float lp_coef(float hz)
{
    hz = fm_clampf(hz, 20.0f, 16000.0f);
    return 1.0f - fm_expf(-2.0f * FM_PI * hz / OM_SR);
}

/* ------------------------------------------------------------- drone --- */
typedef struct {
    float ph_a, ph_b;                                /* the two oscillators */
    float inc, tgt;                                  /* the pitch now, and where it glides to */
    float gain;
} dv_t;
static dv_t dv[4];
static float d_fade;                                 /* 0..1: in while the drone should sound, out after */
static float d_l1, d_l2, d_r1, d_r2, d_coef;         /* the filters, and their coefficient */
static float d_lfo;                                  /* BREATHE, its phase 0..1 */
static float d_level, d_tone, d_glide_t, d_spread, d_breathe, d_color, d_send;

volatile float om_drone_level;
volatile uint8_t om_drone_note[4];

/* where the drone's voices go now: a root from the chord (FOLLOW) or KEY (FIXED), the colour from the chord */
static void drift_retune(void)
{
    const int8_t *t = OM_TYPE_TONES[(unsigned)type < CH_NTYPES ? type : 0];
    int r = par[P_DMODE] == DM_FIXED ? par[P_DKEY] : root + par[P_TRANSPOSE];
    int base = fold(r, 36), col = fold(root + par[P_TRANSPOSE] + t[1], 48), i;
    int note[4];
    note[0] = base;
    note[1] = base + 7;
    note[2] = base + 12;
    note[3] = col;
    for (i = 0; i < 4; i++) {
        om_drone_note[i] = (uint8_t)note[i];
        dv[i].tgt = hz_inc((float)note[i]);
        if (dv[i].inc <= 0.0f)                       /* the first time: start there, no glide from nothing */
            dv[i].inc = dv[i].tgt;
    }
}

static void drift_param(int p)
{
    float v = (float)par[p] * 0.01f;
    switch (p) {
    case P_DMODE: case P_DKEY: drift_retune(); break;     /* (TUNE and TRANSPOSE: retune() calls it) */
    case P_DLEVEL: d_level = v * v * 0.55f; break;
    case P_DTONE: d_tone = v; break;
    case P_DGLIDE: d_glide_t = 0.04f + v * v * 7.0f; break;          /* seconds: a slide to a slow drift */
    case P_DSPREAD: d_spread = v; break;
    case P_DBREATHE: d_breathe = v; break;
    case P_DCOLOR: d_color = v; break;
    case P_CREV: d_send = 0.12f + 0.8f * v * v; break;                /* the drone lives in the reverb */
    default: break;
    }
}

/* ------------------------------------------------------------- pulse --- */
static const uint8_t RATE_PULSES[4] = {6, 12, 24, 48};             /* MIDI clock pulses (24 a quarter) */
static int e_pulse, e_step;                                         /* the pulse's own count */
static uint8_t e_pos[OM_NLANE], e_first[OM_NLANE];
static uint8_t mal_walk;                                            /* the mallet's place in the chord */
volatile uint8_t om_lane_pos[OM_NLANE];
volatile uint8_t om_lane_hit;

int om_euclid(int hits, int steps, int rot, int i)
{
    if (steps < 1)
        return 0;
    if (hits >= steps)
        return hits > 0;
    if (hits <= 0)
        return 0;
    i = ((i + rot) % steps + steps) % steps;
    return (i * hits) % steps < hits;                /* Bresenham: the hits as evenly spread as they go */
}

typedef struct {
    float ph, inc, env, dec, penv, vel;              /* LOW: penv is the pitch drop */
} thump_t;
typedef struct {
    float ph1, ph2, inc, env1, env2, dec1, dec2, vel, pl, pr;
} mallet_t;
typedef struct {
    float env, att, dec, vel, lp, bp, up;
} shaker_t;
static thump_t th;
static mallet_t mal[4];
static uint8_t mal_next;
static shaker_t shk;
static float sw_env, sw_att, sw_dec, sw_up;          /* SWELL: an attack-decay envelope */
static float perc, vary;

static void hit_low(float vel)
{
    th.ph = 0.0f;
    th.inc = hz_inc((float)fold(root + par[P_TRANSPOSE], 31));     /* G1..F#2: felt more than heard */
    th.env = vel;
    th.vel = vel;
    th.penv = 1.0f;
    th.dec = 1.0f - 1.0f / (0.28f * OM_SR);
    midi(0x99, 36, (uint32_t)(40.0f + 80.0f * vel));
    midi(0x89, 36, 0);
}

static void hit_mallet(float vel)
{
    mallet_t *m = &mal[mal_next];
    int k = mal_walk % 3, oct = 1 + (mal_walk / 3) % 2, n;
    if (rndf() < vary * 0.8f) {                       /* VARY: stray to another tone or octave */
        k = (int)(rndf() * 2.99f);
        oct = rndf() < 0.5f ? 1 : 2;
    }
    mal_walk = (uint8_t)((mal_walk + 1) % 6);
    n = chord_n[k] + 12 * oct;
    m->inc = hz_inc((float)n);
    m->ph1 = m->ph2 = 0.0f;
    m->env1 = vel;
    m->env2 = vel * 0.5f;
    m->vel = vel;
    m->dec1 = 1.0f - 1.0f / (1.1f * OM_SR);
    m->dec2 = 1.0f - 1.0f / (0.12f * OM_SR);
    {   /* each voice its own place: alternating, a little wider with SPREAD */
        float p = (mal_next & 1 ? 0.35f : -0.35f) * (0.5f + d_spread), a = (p + 1.0f) * (FM_PI / 4.0f);
        m->pl = fm_cosf(a);
        m->pr = fm_sinf(a);
    }
    mal_next = (uint8_t)((mal_next + 1) & 3);
    midi(0x93, (uint32_t)n, (uint32_t)(40.0f + 80.0f * vel));
    midi(0x83, (uint32_t)n, 0);
}

static void hit_shaker(float vel)
{
    shk.env = 0.0f;
    shk.up = 1.0f;
    shk.vel = vel;
    shk.att = 1.0f / (0.006f * OM_SR);
    shk.dec = 1.0f - 1.0f / (0.06f * OM_SR);
    midi(0x99, 70, (uint32_t)(30.0f + 70.0f * vel));
    midi(0x89, 70, 0);
}

static void hit_swell(float vel)
{
    float step_s = (float)RATE_PULSES[par[P_PRATE] & 3] * step_len / (6.0f * OM_SR);   /* a pulse step, s */
    sw_up = fm_maxf(vel, sw_env);                    /* from where it is: never a drop */
    sw_att = 1.0f / (fm_clampf(2.0f * step_s, 0.15f, 3.0f) * OM_SR);
    sw_dec = 1.0f - 1.0f / (fm_clampf(3.0f * step_s, 0.4f, 6.0f) * OM_SR);
}

static void drift_step(void)
{
    int l;
    uint8_t hit = 0;
    for (l = 0; l < OM_NLANE; l++) {
        int hits = par[P_LANE(l, 0)], steps = par[P_LANE(l, 1)], rot = par[P_LANE(l, 2)];
        int i = e_pos[l] % steps;
        if (om_euclid(hits, steps, rot, i)) {
            int first = !e_first[l];                 /* the lane's first hit in its cycle: an accent */
            float vel = (first ? 1.0f : 0.78f) * (1.0f - vary * 0.55f * rndf());
            e_first[l] = 1;
            if (l < 3 && !first && rndf() < vary * 0.3f)
                vel = 0.0f;                          /* VARY: a hit left out */
            if (vel > 0.0f) {
                hit |= (uint8_t)(1u << l);
                if (l == 0)
                    hit_low(vel);
                else if (l == 1)
                    hit_mallet(vel);
                else if (l == 2)
                    hit_shaker(vel);
                else
                    hit_swell(vel);
            }
        }
        om_lane_pos[l] = (uint8_t)i;
        if (++i >= steps) {
            i = 0;
            e_first[l] = 0;
        }
        e_pos[l] = (uint8_t)i;
    }
    om_lane_hit |= hit;
    e_step++;
}

/* a MIDI clock pulse (6 a step of the OM rhythm, 24 a quarter): the pulse steps every RATE pulses */
static void drift_pulse(void)
{
    if (e_pulse % RATE_PULSES[par[P_PRATE] & 3] == 0)
        drift_step();
    e_pulse++;
}

static void drift_start(int from_top)
{
    int l;
    if (!from_top)
        return;
    e_pulse = e_step = 0;
    mal_walk = 0;
    for (l = 0; l < OM_NLANE; l++)
        e_pos[l] = e_first[l] = 0;
}

static void drift_quiet(void)
{
    int i;
    th.env = 0.0f;
    for (i = 0; i < 4; i++)
        mal[i].env1 = mal[i].env2 = 0.0f;
    shk.env = shk.up = 0.0f;
    sw_env = sw_up = 0.0f;
    d_fade = 0.0f;
}

static void drift_param_pulse(int p)
{
    float v = (float)par[p] * 0.01f;
    if (p == P_PPERC)
        perc = v * v;
    else if (p == P_PVARY)
        vary = v;
}

static void drift_init(void)
{
    int i, h;
    for (i = 0; i <= DW_N; i++) {                    /* the drone's wave: ten harmonics, falling softly */
        float x = (float)(i % DW_N) * (2.0f * FM_PI / (float)DW_N), s = 0.0f, norm = 0.0f;
        for (h = 1; h <= 10; h++) {
            float a = 1.0f / fm_powf((float)h, 1.6f);
            s += a * fm_sinf(x * (float)h);
            norm += a;
        }
        dw_warm[i] = s / norm * 1.6f;
        dw_sine[i] = fm_sinf(x);
    }
    for (i = 0; i < 4; i++) {
        dv[i].ph_a = (float)(i * 61 % DW_N);
        dv[i].ph_b = (float)(i * 113 % DW_N);
        dv[i].inc = 0.0f;
    }
    dv[0].gain = 1.0f;
    dv[1].gain = 0.62f;
    dv[2].gain = 0.5f;
    dv[3].gain = 0.0f;                               /* COLOR sets it */
    d_l1 = d_l2 = d_r1 = d_r2 = 0.0f;
    d_lfo = 0.0f;
    drift_quiet();
}

/* ------------------------------------------------------------ render --- */
static void render_drone(uint32_t n)
{
    const int on = par[P_DMODE] != DM_OFF && (om_playing || gate);
    const float fade_in = (float)n / (2.5f * OM_SR), fade_out = (float)n / (4.0f * OM_SR);
    float glide = 1.0f - fm_expf(-(float)n / (d_glide_t * OM_SR));
    float sw_amt, lfo, cut_oct, amp, gl, gr, det;
    int i;
    uint32_t j;
    /* the fade, the glide and the modulation move once a block (256 samples, 6 ms) */
    d_fade = on ? fm_minf(1.0f, d_fade + fade_in) : fm_maxf(0.0f, d_fade - fade_out);
    for (i = 0; i < 4; i++)
        dv[i].inc += (dv[i].tgt - dv[i].inc) * glide;
    dv[3].gain = d_color * 0.55f;
    d_lfo += (0.035f + 0.09f * d_breathe) * (float)n / OM_SR;
    if (d_lfo >= 1.0f)
        d_lfo -= 1.0f;
    lfo = fm_sinf(d_lfo * 2.0f * FM_PI);
    sw_amt = par[P_LANE(3, 0)] > 0 && om_playing ? (float)par[P_LANE(3, 3)] * 0.01f : 0.0f;
    cut_oct = d_tone * 6.5f + lfo * d_breathe * 1.3f + sw_env * sw_amt * 3.0f;
    d_coef = lp_coef(90.0f * fm_exp2f(cut_oct));
    {   /* the motion alone (fade, BREATHE, SWELL), 0..~1.2: the screen shows it whatever LEVEL is */
        float motion = d_fade * (1.0f + lfo * d_breathe * 0.22f) * (1.0f - sw_amt * 0.7f + sw_amt * 0.9f * sw_env);
        amp = d_level * motion;
        om_drone_level = fm_clampf(motion, 0.0f, 1.5f);
    }
    if (d_fade <= 0.0f && d_l1 * d_l1 + d_r1 * d_r1 < 1e-10f) {
        d_l1 = d_l2 = d_r1 = d_r2 = 0.0f;
        return;                                      /* silent: nothing to compute */
    }
    det = 1.0f + 0.0045f * d_spread;                 /* +-7.8 cents at full SPREAD */
    gl = 1.0f - 0.45f * d_spread;                    /* oscillator A leans left, B right, with SPREAD */
    gr = 1.0f;
    {
        const float c = d_coef, send_g = d_send;
        float l1 = d_l1, l2 = d_l2, r1 = d_r1, r2 = d_r2;
        for (j = 0; j < n; j++) {
            float a = 0.0f, b = 0.0f, ol, or_;
            for (i = 0; i < 4; i++) {
                dv_t *v = &dv[i];
                float g = v->gain;
                if (g <= 0.0f)
                    continue;
                a += tbl(dw_warm, v->ph_a) * g;
                b += tbl(dw_warm, v->ph_b) * g;
                v->ph_a = tadv(v->ph_a, v->inc * det);
                v->ph_b = tadv(v->ph_b, v->inc / det);
            }
            ol = (a * gr + b * (1.0f - gl)) * amp;   /* A and B, mixed per side: SPREAD 0 is mono */
            or_ = (b * gr + a * (1.0f - gl)) * amp;
            l1 += c * (ol - l1);
            l2 += c * (l1 - l2);
            r1 += c * (or_ - r1);
            r2 += c * (r1 - r2);
            mixl[j] += l2;
            mixr[j] += r2;
            send[j] += (l2 + r2) * send_g;
        }
        d_l1 = fm_flush(l1);
        d_l2 = fm_flush(l2);
        d_r1 = fm_flush(r1);
        d_r2 = fm_flush(r2);
    }
}

static void render_pulse(uint32_t n)
{
    uint32_t j;
    int i;
    const float g = perc * 0.9f;
    const float glow = g * (float)par[P_LANE(0, 3)] * 0.01f, gmal = g * (float)par[P_LANE(1, 3)] * 0.01f,
                gshk = g * (float)par[P_LANE(2, 3)] * 0.01f;
    const float msend = 0.35f + rsend;               /* the pings ring out in the plate */
    /* SWELL: up quickly, down slowly; the drone reads it once a block */
    for (j = 0; j < n; j++) {
        if (sw_up > 0.0f) {
            sw_env += sw_att;
            if (sw_env >= sw_up) {
                sw_env = sw_up;
                sw_up = 0.0f;
            }
        } else if (sw_env > 0.0f) {
            sw_env *= sw_dec;
            if (sw_env < 1e-4f)
                sw_env = 0.0f;
        } else {
            break;
        }
    }
    /* LOW: a sine that drops an octave and a half in its first moments */
    if (th.env > 1e-4f) {
        for (j = 0; j < n; j++) {
            float o = tbl(dw_sine, th.ph) * th.env * glow;
            th.ph = tadv(th.ph, th.inc * (1.0f + 1.8f * th.penv * th.penv));
            th.penv *= 0.9985f;
            th.env *= th.dec;
            mixl[j] += o;
            mixr[j] += o;
            send[j] += o * 0.08f;
        }
    }
    /* MALLET: a sine and its fourth partial (a bell's), the partial gone quickly */
    for (i = 0; i < 4; i++) {
        mallet_t *m = &mal[i];
        if (m->env1 < 1e-4f)
            continue;
        for (j = 0; j < n; j++) {
            float o = (tbl(dw_sine, m->ph1) * m->env1 + tbl(dw_sine, m->ph2) * m->env2 * 0.5f) * gmal * 0.6f;
            m->ph1 = tadv(m->ph1, m->inc);
            m->ph2 = tadv(m->ph2, m->inc * 4.0f);
            m->env1 *= m->dec1;
            m->env2 *= m->dec2;
            mixl[j] += o * m->pl;
            mixr[j] += o * m->pr;
            send[j] += o * msend;
        }
    }
    /* SHAKER: noise, a band around 6 kHz, a 6 ms rise and a 60 ms fall */
    if (shk.up > 0.0f || shk.env > 1e-4f) {
        const float c1 = 0.55f, c2 = 0.25f;
        for (j = 0; j < n; j++) {
            float x = rndf() * 2.0f - 1.0f, o;
            if (shk.up > 0.0f) {
                shk.env += shk.att;
                if (shk.env >= 1.0f) {
                    shk.env = 1.0f;
                    shk.up = 0.0f;
                }
            } else {
                shk.env *= shk.dec;
            }
            shk.lp += c1 * (x - shk.lp);             /* below ~8 kHz */
            shk.bp += c2 * (shk.lp - shk.bp);        /* minus below ~2 kHz: what is left is the band */
            o = (shk.lp - shk.bp) * shk.env * shk.vel * gshk * 0.7f;
            mixl[j] += o * (0.6f + 0.4f * (float)(rng & 1u));   /* a little movement across */
            mixr[j] += o * (1.0f - 0.4f * (float)(rng & 1u));
            send[j] += o * 0.15f;
        }
        shk.lp = fm_flush(shk.lp);
        shk.bp = fm_flush(shk.bp);
    }
}

static void render_drift(uint32_t n)
{
    render_pulse(n);                                 /* first: the swell the drone reads */
    render_drone(n);
}

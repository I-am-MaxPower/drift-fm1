/* SPDX-License-Identifier: GPL-3.0-only */
/* DRIFT (dsp/drift.c, inside dsp/omni.c) on its own: the Euclidean patterns, the drone's notes in both
 * modes and its glide, its fade with the transport and the chord buttons, the four lanes' hits and timing,
 * the swell, levels that stay inside full scale, and the cost of a block next to FoMni's own voices.
 *   drift_test [OUT.wav]   (with a file: a minute of everything playing, to listen to) */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "omni.h"

static int bad, midi_n, lane_notes[16][128];
void om_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_n++;
    if ((st & 0xF0u) == 0x90u && d2)
        lane_notes[st & 15u][d1 & 127u]++;
}

static void check(const char *what, int ok)
{
    printf("%-62s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t buf[2 * 256];
static float peak, sumsq;
static long nsamp;
static int finite_ok = 1;
static FILE *wav;
static void run(int blocks)
{
    int b, i;
    for (b = 0; b < blocks; b++) {
        om_render(buf, 256, 4096 / 2);               /* MASTER at half, as the simulator */
        for (i = 0; i < 512; i++) {
            float x = (float)buf[i] / 8388608.0f;
            if (!(x == x))
                finite_ok = 0;
            if (fabsf(x) > peak)
                peak = fabsf(x);
            sumsq += x * x;
            nsamp++;
            if (wav) {
                int16_t s = (int16_t)(x * 32767.0f);
                fwrite(&s, 2, 1, wav);
            }
        }
    }
}
static void meter_reset(void)
{
    peak = sumsq = 0.0f;
    nsamp = 0;
}
static float rms_db(void) { return nsamp ? 10.0f * log10f(sumsq / (float)nsamp + 1e-20f) : -200.0f; }
static float peak_db(void) { return 20.0f * log10f(peak + 1e-20f); }
static int blocks_for(float s) { return (int)(s * 44100.0f / 256.0f + 0.5f); }

static void settle(void) { run(2); }

int main(int argc, char **argv)
{
    /* ---- the patterns (the classic ones: Toussaint, "The Euclidean Algorithm Generates Traditional
     * Musical Rhythms", 2005) */
    {
        char p[17];
        int i;
        for (i = 0; i < 8; i++)
            p[i] = om_euclid(3, 8, 0, i) ? 'x' : '.';
        p[8] = 0;
        check("E(3,8) is the tresillo x..x..x.", !strcmp(p, "x..x..x."));
        for (i = 0; i < 8; i++)
            p[i] = om_euclid(5, 8, 0, i) ? 'x' : '.';
        check("E(5,8): five hits, never two rests together (x.x.xx.x)", !strcmp(p, "x.x.xx.x"));
        for (i = 0; i < 8; i++)
            p[i] = om_euclid(3, 8, 1, i) ? 'x' : '.';
        check("E(3,8) turned by 1 is the same pattern moved", !strcmp(p, "..x..x.x"));
        {
            int k, n, ok = 1;
            for (n = 1; n <= 16; n++)
                for (k = 0; k <= 16; k++) {
                    int c = 0, r;
                    for (r = 0; r < 16; r++) {
                        int cnt = 0;
                        for (i = 0; i < n; i++)
                            cnt += om_euclid(k, n, r, i);
                        if (r == 0)
                            c = cnt;
                        ok &= cnt == c;
                    }
                    ok &= c == (k < n ? k : n);
                }
            check("every pattern: exactly min(HITS, STEPS) hits, at any rotation", ok);
        }
        check("0 hits: silent; HITS > STEPS: every step", !om_euclid(0, 8, 0, 0) && om_euclid(9, 8, 0, 5));
    }

    om_init();
    /* ---- the drone: notes */
    om_chord(0, CH_MAJ);
    settle();
    check("FOLLOW, C major: drone C2 G2 C3, colour E3",
          om_drone_note[0] == 36 && om_drone_note[1] == 43 && om_drone_note[2] == 48 && om_drone_note[3] == 52);
    om_chord(9, CH_MIN);
    settle();
    check("FOLLOW, A minor: drone A2 E3 A3, colour C3",
          om_drone_note[0] == 45 && om_drone_note[1] == 52 && om_drone_note[2] == 57 && om_drone_note[3] == 48);
    om_set(P_DMODE, DM_FIXED);
    om_set(P_DKEY, 2);
    settle();
    check("FIXED on D, chord A minor: drone D2 A2 D3, colour still C3",
          om_drone_note[0] == 38 && om_drone_note[1] == 45 && om_drone_note[2] == 50 && om_drone_note[3] == 48);
    om_set(P_DMODE, DM_FOLLOW);

    /* ---- the drone: sounds while a chord is held or the pulse plays, fades out after */
    om_chord(0, CH_MAJ);
    meter_reset();
    run(blocks_for(1.0f));
    check("nothing held, stopped: silent", peak < 1e-4f);
    om_gate(1);
    run(blocks_for(0.3f));
    check("a chord held: the drone fades in (not at once)", om_drone_level > 0.01f && om_drone_level < 0.4f);
    run(blocks_for(3.5f));
    check("... and is all the way in after 3 s", om_drone_level > 0.5f);
    meter_reset();
    run(blocks_for(2.0f));
    printf("  (drone + chord, defaults: rms %.1f dBFS, peak %.1f dBFS)\n", (double)rms_db(), (double)peak_db());
    om_gate(0);
    run(blocks_for(1.0f));
    check("let go: still fading out after 1 s", om_drone_level > 0.05f);
    run(blocks_for(6.0f));
    check("... gone after 7 s", om_drone_level < 1e-3f);
    om_set(P_DMODE, DM_OFF);
    om_gate(1);
    run(blocks_for(2.0f));
    check("Mode Off: no drone", om_drone_level < 1e-3f);
    om_gate(0);
    om_set(P_DMODE, DM_FOLLOW);
    run(blocks_for(3.0f));

    {   /* FoMni alone, for comparison: a full strum of C with the chord held, its defaults, no drone */
        int i;
        om_set(P_DMODE, DM_OFF);
        om_gate(1);
        for (i = 0; i < 16; i++)
            om_strum(i);
        meter_reset();
        run(blocks_for(3.0f));
        printf("  (FoMni's own: a full strum and the chord: rms %.1f dBFS, peak %.1f dBFS)\n", (double)rms_db(), (double)peak_db());
        om_gate(0);
        om_set(P_DMODE, DM_FOLLOW);
        run(blocks_for(6.0f));
    }
    /* ---- the drone alone, for its level */
    om_set(P_CHORD, 0);
    om_gate(1);
    run(blocks_for(4.0f));
    meter_reset();
    run(blocks_for(3.0f));
    printf("  (drone alone, defaults: rms %.1f dBFS, peak %.1f dBFS)\n", (double)rms_db(), (double)peak_db());
    check("drone alone at defaults: comfortably loud, not clipping", rms_db() > -30.0f && peak_db() < -3.0f);
    om_set(P_DLEVEL, 100);
    om_set(P_DTONE, 100);
    om_set(P_DCOLOR, 100);
    om_set(P_DSPREAD, 100);
    om_set(P_DBREATHE, 100);
    run(blocks_for(2.0f));
    meter_reset();
    run(blocks_for(4.0f));
    printf("  (drone alone, everything up: rms %.1f dBFS, peak %.1f dBFS)\n", (double)rms_db(), (double)peak_db());
    check("drone everything up: finite, under full scale", finite_ok && peak < 1.0f);
    om_gate(0);
    om_set(P_DLEVEL, 55);
    om_set(P_DTONE, 35);
    om_set(P_DCOLOR, 30);
    om_set(P_DSPREAD, 40);
    om_set(P_DBREATHE, 40);
    om_set(P_CHORD, 35);
    om_chord(0, CH_MAJ);
    run(blocks_for(7.0f));

    /* ---- the pulse: timing and hits. Tempo 120, RATE 1/8: a step every 0.25 s; LOW E(3,8) */
    om_set(P_TEMPO, 120);
    om_set(P_PRATE, 1);
    om_set(P_PVARY, 0);
    om_set(P_LANE(0, 0), 3);
    om_set(P_LANE(0, 1), 8);
    om_set(P_LANE(1, 0), 5);
    om_set(P_LANE(1, 1), 16);
    om_set(P_LANE(2, 0), 7);
    om_set(P_LANE(2, 1), 16);
    om_set(P_LANE(3, 0), 1);
    om_set(P_LANE(3, 1), 8);
    memset(lane_notes, 0, sizeof lane_notes);
    om_play(1);
    {
        int low = 0, mal = 0, shk = 0, sw = 0, b, n;
        for (b = 0; b < blocks_for(4.0f) - 1; b++) {   /* 4 s: 16 steps, two cycles of LOW */
            om_lane_hit = 0;
            run(1);
            low += om_lane_hit & 1;
            mal += om_lane_hit >> 1 & 1;
            shk += om_lane_hit >> 2 & 1;
            sw += om_lane_hit >> 3 & 1;
        }
        check("4 s at 120, 1/8: LOW E(3,8) hit 6 times", low == 6);
        check("... MALLET E(5,16) 5 times, SHAKER E(7,16) 7, SWELL E(1,8) twice", mal == 5 && shk == 7 && sw == 2);
        for (n = 0, b = 0; b < 128; b++)
            n += lane_notes[3][b];
        check("MIDI out: the mallet's notes on channel 4", n == 5);
        check("MIDI out: the low hits on channel 10 (36)", lane_notes[9][36] == 6);
        for (n = 0, b = 0; b < 128; b++)
            if (lane_notes[3][b])
                n += (b % 12 == 0 || b % 12 == 4 || b % 12 == 7);
        check("the mallet plays only the chord's tones (C E G)", n > 0 && n == (int)(
              (lane_notes[3][72] != 0) + (lane_notes[3][76] != 0) + (lane_notes[3][67] != 0) + (lane_notes[3][79] != 0) +
              (lane_notes[3][84] != 0) + (lane_notes[3][88] != 0) + (lane_notes[3][91] != 0) + (lane_notes[3][60] != 0) +
              (lane_notes[3][64] != 0) + (lane_notes[3][55] != 0) + (lane_notes[3][96] != 0) + (lane_notes[3][100] != 0)));
    }
    check("playing: the drone is in, with nothing held", om_drone_level > 0.1f);
    {   /* the swell: the drone louder after a SWELL hit than before the next */
        float lo = 1.0f, hi = 0.0f;
        int b;
        run(blocks_for(3.0f));
        for (b = 0; b < blocks_for(4.0f); b++) {
            run(1);
            if (om_drone_level < lo)
                lo = om_drone_level;
            if (om_drone_level > hi)
                hi = om_drone_level;
        }
        printf("  (swell: the drone moves %.2f .. %.2f)\n", (double)lo, (double)hi);
        check("SWELL 55: the drone breathes with its hits (at least 1.6x)", hi > lo * 1.6f);
        om_set(P_LANE(3, 0), 0);
        lo = 1.0f;
        hi = 0.0f;
        run(blocks_for(3.0f));
        for (b = 0; b < blocks_for(4.0f); b++) {
            run(1);
            if (om_drone_level < lo)
                lo = om_drone_level;
            if (om_drone_level > hi)
                hi = om_drone_level;
        }
        check("SWELL with no hits: only BREATHE moves it (less than 1.6x)", hi < lo * 1.6f);
        om_set(P_LANE(3, 0), 1);
    }
    {   /* everything at once and up: inside full scale */
        int i;
        om_set(P_PVARY, 25);
        om_set(P_TEMPO, 80);
        om_gate(1);
        for (i = 0; i < 16; i++)
            om_strum(i);
        meter_reset();
        run(blocks_for(6.0f));
        printf("  (everything, defaults: rms %.1f dBFS, peak %.1f dBFS)\n", (double)rms_db(), (double)peak_db());
        om_set(P_PPERC, 100);
        for (i = 0; i < OM_NLANE; i++) {
            om_set(P_LANE(i, 0), 16);
            om_set(P_LANE(i, 1), 16);
            om_set(P_LANE(i, 3), 100);
        }
        om_set(P_DLEVEL, 100);
        om_set(P_TEMPO, 240);
        om_set(P_PRATE, 0);
        meter_reset();
        for (i = 0; i < 16; i++)
            om_strum(i);
        run(blocks_for(4.0f));
        printf("  (everything at full: rms %.1f dBFS, peak %.1f dBFS)\n", (double)rms_db(), (double)peak_db());
        check("everything at full: finite, under full scale", finite_ok && peak < 1.0f);
    }
    om_play(0);
    om_gate(0);
    om_panic();
    run(blocks_for(1.0f));
    check("panic: quiet", peak < 1.0f && om_drone_level < 1e-3f);

    /* ---- the cost of a block: DRIFT's voices next to the 16 strings ringing (host CPU, a ratio) */
    {
        clock_t t0, t1, t2;
        int i;
        om_init();
        om_set(P_DMODE, DM_OFF);
        om_chord(0, CH_MAJ);
        for (i = 0; i < 16; i++)
            om_strum(i);
        t0 = clock();
        run(blocks_for(3.0f));
        t1 = clock();
        om_init();
        om_set(P_DLEVEL, 100);
        om_set(P_PPERC, 100);
        for (i = 0; i < OM_NLANE; i++) {
            om_set(P_LANE(i, 0), 16);
            om_set(P_LANE(i, 1), 16);
        }
        om_set(P_TEMPO, 240);
        om_set(P_PRATE, 0);
        om_chord(0, CH_MAJ);
        om_play(1);
        om_gate(1);
        for (i = 0; i < 16; i++)
            om_strum(i);
        run(blocks_for(3.0f));
        t2 = clock();
        printf("  (cost: FoMni, 16 strings: %.0f ms; with DRIFT all on: %.0f ms, for 3 s of sound)\n",
               (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC, (double)(t2 - t1) * 1000.0 / CLOCKS_PER_SEC);
        check("DRIFT all on costs less than twice FoMni's busiest", (t2 - t1) < 2 * (t1 - t0) + CLOCKS_PER_SEC / 50);
        om_play(0);
        om_gate(0);
    }

    /* ---- a minute to listen to: the defaults, a chord walk, the pulse running */
    if (argc > 1 && (wav = fopen(argv[1], "wb")) != NULL) {
        static const uint8_t HDR[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                                        1, 0, 2, 0, 0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a'};
        static const int WALK[8][2] = {{0, CH_MAJ}, {9, CH_MIN}, {5, CH_MAJ}, {7, CH_SUS4},
                                       {0, CH_MAJ}, {4, CH_MIN}, {5, CH_MAJ7}, {7, CH_MAJ}};
        long bytes;
        int c, s;
        fwrite(HDR, 1, 44, wav);
        om_init();
        om_chord(0, CH_MAJ);
        om_play(1);
        for (c = 0; c < 8; c++) {
            om_chord(WALK[c][0], WALK[c][1]);
            om_gate(1);
            for (s = 0; s < 16; s += 1) {             /* a slow upward strum */
                om_strum(s);
                run(blocks_for(0.09f));
            }
            run(blocks_for(4.5f));
            om_gate(0);
            run(blocks_for(1.5f));
        }
        om_play(0);
        run(blocks_for(6.0f));
        bytes = ftell(wav) - 44;
        fseek(wav, 4, SEEK_SET);
        {
            uint32_t v = (uint32_t)(bytes + 36);
            fwrite(&v, 4, 1, wav);
            fseek(wav, 40, SEEK_SET);
            v = (uint32_t)bytes;
            fwrite(&v, 4, 1, wav);
        }
        fclose(wav);
        printf("  (wrote %s)\n", argv[1]);
    }
    printf(bad ? "drift test FAILED\n" : "drift test passed\n");
    return bad != 0;
}

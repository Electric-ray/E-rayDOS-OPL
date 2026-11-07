/* OPL core verification / performance test (shared source: host MinGW and DOS Watcom)
 * usage: gen_test <opl2|opl3> <rate> <seconds> <out.wav> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "opl_core.h"

static void w(u32 hi, u8 reg, u8 val) { oplc_write_port(hi ? 2 : 0, reg); oplc_write_port(hi ? 3 : 1, val); }
static void le32(unsigned char *p, u32 v){ p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24); }

int main(int argc, char **argv)
{
    int mode; u32 rate, secs, total, done, i, q, hash = 2166136261UL, nz = 0; s16 *buf; FILE *fp = 0;
    unsigned char hdr[44]; clock_t t0, t1; double el;
    static const unsigned char notes[4] = { 0x98, 0xB6, 0xD0, 0xFF };   /* A0 fnum low: simple arpeggio */
    if (argc < 5) { printf("usage: gen_test <opl2|opl3> <rate> <seconds> <out.wav>\n"); return 1; }
    mode = (argv[1][3] == '3') ? OPLC_OPL3 : OPLC_OPL2;
    rate = (u32)atol(argv[2]); secs = (u32)atol(argv[3]);
    printf("native rate = %lu Hz, out rate = %lu Hz, mode = OPL%d\n", oplc_init(mode, rate), rate, mode ? 3 : 2);

    w(0, 0x01, 0x20);                                    /* waveform select enable */
    if (mode == OPLC_OPL3) { w(1, 0x05, 0x01); w(0, 0xC0, 0x30); }   /* OPL3 enable, ch0 L+R */
    {   /* nch active channels (default 1, OPL2 max 9, OPL3 max 18): same patch, detuned per channel */
        static const unsigned char opm[9] = { 0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12 };
        int nch = (argc > 5) ? atoi(argv[5]) : 1, ch;
        if (nch < 1) nch = 1; if (nch > (mode ? 18 : 9)) nch = mode ? 18 : 9;
        for (ch = 0; ch < nch; ch++) {
            u32 hi = (ch >= 9); int c = ch % 9; unsigned char m = opm[c];
            w(hi, (u8)(0x20 + m), 0x01); w(hi, (u8)(0x40 + m), 0x10); w(hi, (u8)(0x60 + m), 0xF0); w(hi, (u8)(0x80 + m), 0x77);
            w(hi, (u8)(0x23 + m), 0x01); w(hi, (u8)(0x43 + m), 0x00); w(hi, (u8)(0x63 + m), 0xF0); w(hi, (u8)(0x83 + m), 0x77);
            if (mode) w(hi, (u8)(0xC0 + c), 0x30);
            w(hi, (u8)(0xA0 + c), (u8)(0x98 + ch * 7));
            w(hi, (u8)(0xB0 + c), 0x31);                 /* key on */
        }
        printf("active channels = %d\n", nch);
    }

    total = rate * secs; q = rate / 4; if (!q) q = 1;
    buf = (s16 *)malloc(1024 * sizeof(s16));
    if (!buf) return 2;
    fp = fopen(argv[4], "wb"); if (!fp) { printf("cannot open output\n"); return 3; }
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "RIFF", 4); le32(hdr + 4, 36 + total * 2); memcpy(hdr + 8, "WAVEfmt ", 8); le32(hdr + 16, 16);
    hdr[20] = 1; hdr[22] = 1; le32(hdr + 24, rate); le32(hdr + 28, rate * 2); hdr[32] = 2; hdr[34] = 16;
    memcpy(hdr + 36, "data", 4); le32(hdr + 40, total * 2);
    fwrite(hdr, 1, 44, fp);

    t0 = clock();
    for (done = 0; done < total; ) {
        u32 n = total - done; if (n > 1024) n = 1024;
        /* change note about every 0.25 s (register writes interleaved with PCM generation) */
        if (done && (done % q) < n) { w(0, 0xB0, 0x11); w(0, 0xA0, notes[(done / q) & 3]); w(0, 0xB0, 0x31); }
        oplc_generate_mono(buf, n);
        for (i = 0; i < n; i++) { hash = (hash * 16777619UL) ^ (u32)(unsigned short)buf[i]; if (buf[i]) nz++; }
        fwrite(buf, sizeof(s16), n, fp);
        done += n;
    }
    t1 = clock(); fclose(fp);
    el = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    printf("samples=%lu nonzero=%lu hash=%08lX arena=%lu bytes\n", total, nz, hash, oplc_arena_used());
    printf("elapsed=%.2f s  realtime x%.2f\n", el, el > 0 ? (double)secs / el : 0.0);
    return 0;
}

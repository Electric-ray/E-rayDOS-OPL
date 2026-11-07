// differential debug: dump DBOPL operator state after identical register writes (16-bit vs 32-bit builds)
#include <stdio.h>
#include "DBOPL.H"
static FILE *f;
static void dump(const char *tag, DBOPL::Operator &o)
{
    fprintf(f, "%s r20=%02X r40=%02X r60=%02X r80=%02X rE0=%02X tl=%ld sl=%ld wAdd=%lu wCur=%lu chanData=%lu fMul=%lu vib=%lu atk=%lu dec=%lu rel=%lu ridx=%lu rZero=%02X keyOn=%02X st=%u vol=%ld ksr=%u wMask=%lu wStart=%lu\n",
        tag, o.reg20,o.reg40,o.reg60,o.reg80,o.regE0,(long)o.totalLevel,(long)o.sustainLevel,(unsigned long)o.waveAdd,(unsigned long)o.waveCurrent,
        (unsigned long)o.chanData,(unsigned long)o.freqMul,(unsigned long)o.vibrato,(unsigned long)o.attackAdd,(unsigned long)o.decayAdd,(unsigned long)o.releaseAdd,
        (unsigned long)o.rateIndex,o.rateZero,o.keyOn,(unsigned)o.state,(long)o.volume,(unsigned)o.ksr,(unsigned long)o.waveMask,(unsigned long)o.waveStart);
}
static void w(DBOPL::Chip *c, unsigned r, unsigned v) { c->WriteReg(r, (uint8_t)v); }
int main(void)
{
    static int32_t buf[128];
    DBOPL::Chip *c = new DBOPL::Chip(false);
    f = fopen("ops.txt", "a");
    c->Setup(22050);
    w(c,0x01,0x20);
    w(c,0x20,0x01); w(c,0x40,0x10); w(c,0x60,0xF0); w(c,0x80,0x77); w(c,0xA0,0x98);
    w(c,0x23,0x01); w(c,0x43,0x00); w(c,0x63,0xF0); w(c,0x83,0x77);
    fprintf(f, "after config:\n");
    dump("op0", c->chan[0].op[0]); dump("op1", c->chan[0].op[1]);
    w(c,0xB0,0x31);
    fprintf(f, "after keyon: noiseAdd=%lu lfoAdd=%lu\n", (unsigned long)c->noiseAdd, (unsigned long)c->lfoAdd);
    dump("op0", c->chan[0].op[0]); dump("op1", c->chan[0].op[1]);
    c->Generate(buf, 64);
    fprintf(f, "after gen64: s[10]=%ld s[40]=%ld\n", (long)buf[10], (long)buf[40]);
    dump("op0", c->chan[0].op[0]); dump("op1", c->chan[0].op[1]);
    fprintf(f, "--------\n");
    fclose(f);
    return 0;
}

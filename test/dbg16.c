/* 16-bit hang locator: each log line is open/append/close (a killed DOSBox leaves unclosed files at size 0) */
#include <stdio.h>
#include "opl_core.h"
static void L(const char *s, long v)
{
    FILE *f = fopen("dbg.txt", "a");
    if (f) { fprintf(f, "%s %ld\n", s, v); fclose(f); }
}
int main(void)
{
    s16 buf[256]; u32 i;
    remove("dbg.txt");
    L("start", 0);
    oplc_init(OPLC_OPL2, 22050); L("init done", 0);
    oplc_write_port(0, 0x01); oplc_write_port(1, 0x20); L("write1", 0);
    oplc_write_port(0, 0xB0); oplc_write_port(1, 0x31); L("keyon", 0);
    for (i = 0; i < 20; i++) { oplc_generate_mono(buf, 256); L("gen s0=", buf[10]); }
    L("done", 0);
    return 0;
}

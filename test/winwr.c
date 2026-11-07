/* Writes OPL registers from a timer interrupt handler (INT 1Ch, 18.2 Hz), like a game music driver does. With the RTC clock these
 * writes arrive inside the ISR synthesis interrupt windows => exercises the second (write) stack. Run after: EOPL /NOTRAP */
#include <stdio.h>
#include <dos.h>
#include <conio.h>
#include <i86.h>
static unsigned char id, tick_val;
static volatile unsigned long ticks_seen;
static void (__interrupt __far *old1c)(void);
static void __interrupt __far h1c(void)
{
    unsigned char idl = id, v = (unsigned char)(0x40 + (tick_val++ & 0x3F));
    _asm {
        mov ah, idl
        mov al, 1
        mov bh, 0
        mov bl, 0xA0
        mov cl, v
        int 2Fh
    }
    ticks_seen++;
}
static void w(unsigned r, unsigned v)
{
    union REGS i, o; i.h.ah = id; i.h.al = 1; i.h.bh = 0; i.h.bl = (unsigned char)r; i.h.cl = (unsigned char)v; int86(0x2F, &i, &o);
}
int main(void)
{
    unsigned c, found = 0; union REGS i, o; struct SREGS s; unsigned long t0;
    volatile unsigned long far *t = (volatile unsigned long far *)MK_FP(0x40, 0x6C);
    static const unsigned op[3] = { 0, 1, 2 };
    for (c = 0xC0; c <= 0xFF; c++) { i.h.ah = (unsigned char)c; i.h.al = 0; i.x.bx = 0; i.x.cx = 0; i.x.dx = 0; int86(0x2F, &i, &o); if (o.h.al == 0xFF && o.x.cx == 0x4F45) { id = (unsigned char)c; found = 1; break; } }
    if (!found) { printf("EOPL not installed\n"); return 1; }
    w(0x01, 0x20);
    for (c = 0; c < 3; c++) {
        unsigned m = op[c];
        w(0x20 + m, 0x21); w(0x40 + m, 0x1C); w(0x60 + m, 0xF0); w(0x80 + m, 0x77);
        w(0x23 + m, 0x21); w(0x43 + m, 0x08); w(0x63 + m, 0xC2); w(0x83 + m, 0x15);
        w(0xC0 + c, 0); w(0xA0 + c, 0x57 + c * 0x20); w(0xB0 + c, 0x32);
    }
    old1c = _dos_getvect(0x1C); _dos_setvect(0x1C, h1c);
    t0 = *t; while (*t - t0 < 91) { }                      /* ~5 s */
    _dos_setvect(0x1C, old1c);
    segread(&s); i.h.ah = id; i.h.al = 2; int86x(0x2F, &i, &o, &s);
    printf("timer-handler OPL writes: %lu\n", ticks_seen);
    return 0;
}

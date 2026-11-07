#include <stdio.h>
#include "opl_core.h"
extern unsigned long dbopl_dbg_sum(int);
int main(void)
{
    static const char *n[8] = { "Exp","Sin","Wave","Mul","Ksl","Tremolo","ChanOfs","OpOfs" };
    int i; FILE *f;
    oplc_init(OPLC_OPL2, 22050);
    f = fopen("sums.txt", "a");
    for (i = 0; i < 8; i++) fprintf(f, "%-8s %08lX\n", n[i], dbopl_dbg_sum(i));
    fprintf(f, "--\n");
    fclose(f);
    return 0;
}

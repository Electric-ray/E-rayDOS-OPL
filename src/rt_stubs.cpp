/* SPDX-License-Identifier: GPL-3.0-or-later */
// Minimal freestanding runtime stubs (to link YMFM without libstdc++/libc)
#include <stddef.h>
namespace std { void __throw_length_error(const char *) { for (;;) {} } }

#ifdef OPLC_FREESTANDING
extern "C" {
void *memcpy(void *d, const void *s, size_t n)  { unsigned char *a=(unsigned char*)d; const unsigned char *b=(const unsigned char*)s; while(n--) *a++=*b++; return d; }
void *memmove(void *d, const void *s, size_t n) { unsigned char *a=(unsigned char*)d; const unsigned char *b=(const unsigned char*)s;
    if (a < b) { while(n--) *a++=*b++; } else { a+=n; b+=n; while(n--) *--a=*--b; } return d; }
void *memset(void *d, int c, size_t n)          { unsigned char *a=(unsigned char*)d; while(n--) *a++=(unsigned char)c; return d; }
}
#endif

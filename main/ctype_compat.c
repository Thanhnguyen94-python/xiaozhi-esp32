#include <stdint.h>

// Compatibility symbol for prebuilt libraries that still reference legacy newlib `_ctype_`.
// Layout: table is indexed by (unsigned char)c + 1.
__attribute__((weak)) unsigned char _ctype_[257] = {0};

#define _U 0x01
#define _L 0x02
#define _N 0x04
#define _S 0x08
#define _P 0x10
#define _C 0x20
#define _X 0x40
#define _B 0x80

static unsigned char classify_ascii(unsigned char c) {
    unsigned char f = 0;
    if (c < 0x20 || c == 0x7F) {
        f |= _C;
    }
    if (c == ' ' || c == '\t') {
        f |= _B;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
        f |= _S;
    }
    if (c >= '0' && c <= '9') {
        f |= _N | _X;
    }
    if (c >= 'A' && c <= 'Z') {
        f |= _U;
        if (c <= 'F') {
            f |= _X;
        }
    }
    if (c >= 'a' && c <= 'z') {
        f |= _L;
        if (c <= 'f') {
            f |= _X;
        }
    }
    if ((c >= 0x21 && c <= 0x2F) ||
        (c >= 0x3A && c <= 0x40) ||
        (c >= 0x5B && c <= 0x60) ||
        (c >= 0x7B && c <= 0x7E)) {
        f |= _P;
    }
    return f;
}

__attribute__((constructor)) static void init_ctype_compat(void) {
    _ctype_[0] = 0;
    for (int i = 0; i < 256; ++i) {
        _ctype_[i + 1] = classify_ascii((unsigned char)i);
    }
}

/*
 * Carbonella - QR codes on the e-ink screen (for "Ricevi libri": the phone scans the address).
 * The encoder is Project Nayuki's qrcodegen (MIT), in third_party/qrcodegen.
 */
#include "carbonella.h"
#include "../third_party/qrcodegen/qrcodegen.h"

#include <string.h>

int qr_draw(Canvas *c, int x, int y, int size, const char *text)
{
    static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    /* medium error correction: survives a dusty screen and a quick, slanted phone shot */
    if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true))
        return 0;
    int n = qrcodegen_getSize(qr);
    int quiet = 4;                      /* white border the scanners need, in modules */
    int m = size / (n + 2 * quiet);     /* whole pixels per module: sharp edges on e-ink */
    if (m < 2)
        return 0;
    int side = m * (n + 2 * quiet);
    int ox = x + (size - side) / 2, oy = y + (size - side) / 2;
    canvas_fill(c, ox, oy, side, side, INK_PAPER);
    for (int r = 0; r < n; r++)
        for (int k = 0; k < n; k++)
            if (qrcodegen_getModule(qr, k, r))
                canvas_fill(c, ox + (quiet + k) * m, oy + (quiet + r) * m, m, m, INK_BLACK);
    return side;
}

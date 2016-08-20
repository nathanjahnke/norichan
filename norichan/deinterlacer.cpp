#include "deinterlacer.h"

Deinterlacer::Deinterlacer(QObject *parent) : QObject(parent)
      ,width(0)
      ,height(0)
      ,linewidth(0)

      ,mfd_prevFrame(0)
      ,mfd_moving(0)
      ,mfd_fmoving(0)

      ,mfd_threshold(15)
      ,bDenoise(false)
      ,bMotionOnly(false)
{
}

Deinterlacer::~Deinterlacer() {
        clean_up();
}

void Deinterlacer::clean_up() {
        if (mfd_prevFrame) delete[] mfd_prevFrame; mfd_prevFrame = NULL;
        if (mfd_moving) delete[] mfd_moving; mfd_moving = NULL;
        if (mfd_fmoving) delete[] mfd_fmoving; mfd_fmoving = NULL;
}

void Deinterlacer::add_field(QImage in_image, bool high_field) {
        if (in_image.width() != width || in_image.height() != height) {
                clean_up();
                width = in_image.width(); height = in_image.height(); linewidth = in_image.width()*4;
                field_number = 0;

                mfd_prevFrame = new int[width*height];
                memset(mfd_prevFrame, 0, width*height*sizeof(int));
                mfd_moving = new unsigned char[width*height];
                memset(mfd_moving, 0, width*height*sizeof(unsigned char));
                mfd_fmoving = new unsigned char[width*height];
                memset(mfd_fmoving, 0, width*height*sizeof(unsigned char));
        }

        uchar *new_field = in_image.bits();
        bool expected_high_field = (field_number % 2 == 0);
        if (expected_high_field == high_field) { //we need to see both fields (20121124)
                return;
        }
        ++field_number;
        QImage out(width, height*2, QImage::Format_RGB32);
        unsigned char *fa_dst_data = out.bits();

        //if (fields_added % 2 != 0) return;





#define DENOISE_DIAMETER 5
#define DENOISE_THRESH 7

        typedef int32_t Pixel32;
        //int iOddEven = 0;//((MyFilterData *)(fa->filter_data))->bShiftEven ? 0 : 1;
        Pixel32 *src, *dst, *srcn, *srcnn, *srcp;
        unsigned char *moving, *fmoving;
        int x, y, *prev;
        long currValue, prevValue, nextValue, nextnextValue, luma, lumap, luman;
        int r, g, b, rp, gp, bp, rn, gn, bn, rnn, gnn, bnn, R, G, B, T = mfd_threshold * mfd_threshold;
        int h = height;

        int w = width;
        int hminus = height - 1;
        int hminus2 = height - 2;
        int wminus = width - 1;

        /* Calculate the motion map. */
        moving = mfd_moving;
        /* Threshold 0 means treat all areas as moving, i.e., dumb bob. */
        if (mfd_threshold == 0) {
                memset(moving, 1, height * width);
        } else {
                memset(moving, 0, height * width);
                src = (Pixel32 *)new_field;
                srcn = (Pixel32 *)((char *)src + linewidth);
                prev = mfd_prevFrame;
                if (high_field) //if (field_number % 2 == iOddEven)
                        prev += width;
                for (y = 0; y < hminus; y++) {
                        for (x = 0; x < w; x++) {
                                currValue = prev[x];
                                nextValue = srcn[x];
                                prevValue = src[x];
                                r = (currValue >> 16) & 0xff;
                                rp = (prevValue >> 16) & 0xff;
                                rn = (nextValue >> 16) & 0xff;
                                g = (currValue >> 8) & 0xff;
                                gp = (prevValue >> 8) & 0xff;
                                gn = (nextValue >> 8) & 0xff;
                                b = currValue & 0xff;
                                bp = prevValue & 0xff;
                                bn = nextValue & 0xff;
                                luma = (55 * r + 182 * g + 19 * b) >> 8;
                                lumap = (55 * rp + 182 * gp + 19 * bp) >> 8;
                                luman = (55 * rn + 182 * gn + 19 * bn) >> 8;
                                if ((lumap - luma) * (luman - luma) >= T)
                                        moving[x] = 1;
                        }
                        src = (Pixel32 *)((char *)src + linewidth);
                        srcn = (Pixel32 *)((char *)srcn + linewidth);
                        moving += w;
                        prev += w;
                }
                /* Can't diff the last line. */
                memset(moving, 0, width);

                /* Motion map denoising. */
                if (bDenoise) {
                        int xlo, xhi, ylo, yhi, xsize;
                        int u, v;
                        int N = DENOISE_DIAMETER;
                        int Nover2 = N/2;
                        int sum;
                        unsigned char *m;

                        // Erode.
                        moving = mfd_moving;
                        fmoving = mfd_fmoving;
                        for (y = 0; y < h; y++) {
                                for (x = 0; x < w; x++) {
                                        if (moving[x] == 0) {
                                                fmoving[x] = 0;
                                                continue;
                                        }
                                        xlo = x - Nover2; if (xlo < 0) xlo = 0;
                                        xhi = x + Nover2; if (xhi >= w) xhi = wminus;
                                        ylo = y - Nover2; if (ylo < 0) ylo = 0;
                                        yhi = y + Nover2; if (yhi >= h) yhi = hminus;
                                        for (u = ylo, sum = 0, m = mfd_moving + ylo * w; u <= yhi; u++) {
                                                for (v = xlo; v <= xhi; v++) {
                                                        sum += m[v];
                                                }
                                                m += w;
                                        }
                                        if (sum > DENOISE_THRESH)
                                                fmoving[x] = 1;
                                        else
                                                fmoving[x] = 0;
                                }
                                moving += w;
                                fmoving += w;
                        }

                        // Dilate.
                        moving = mfd_moving;
                        fmoving = mfd_fmoving;
                        for (y = 0; y < h; y++) {
                                for (x = 0; x < w; x++) {
                                        if (fmoving[x] == 0) {
                                                moving[x] = 0;
                                                continue;
                                        }
                                        xlo = x - Nover2;
                                        if (xlo < 0) xlo = 0;
                                        xhi = x + Nover2;
                                        /* Use w here instead of wminus so we don't have to add 1 in the
                                           the assignment of xsize. */
                                        if (xhi >= w) xhi = w;
                                        xsize = xhi - xlo;
                                        ylo = y - Nover2;
                                        if (ylo < 0) ylo = 0;
                                        yhi = y + Nover2;
                                        if (yhi >= h) yhi = hminus;
                                        m = mfd_moving + ylo * w;
                                        for (u = ylo; u <= yhi; u++) {
                                                memset(&m[xlo], 1, xsize);
                                                m += w;
                                        }
                                }
                                moving += w;
                                fmoving += w;
                        }
                }
        }


        /* Output the destination frame. */
        if (!bMotionOnly) {
                /* Output the destination frame. */
                src = (Pixel32 *)new_field;
                srcn = (Pixel32 *)((char *)new_field + linewidth);

                srcnn = (Pixel32 *)((char *)new_field + 2 * linewidth);

                srcp = (Pixel32 *)((char *)new_field - linewidth);

                dst = (Pixel32 *)fa_dst_data;

                if (high_field) { //if (field_number % 2 == iOddEven) {
                        /* Shift this frame's output up by one line. */
                        memcpy(dst, src, width * sizeof(Pixel32));
                        dst = (Pixel32 *)((char *)dst + linewidth);
                        prev = mfd_prevFrame + w;
                } else {
                        prev = mfd_prevFrame;
                }
                moving = mfd_moving;

                for (y = 0; y < hminus; y++) {
                        /* Even output line. Pass it through. */
                        memcpy(dst, src, width * sizeof(Pixel32));
                        dst = (Pixel32 *)((char *)dst + linewidth);
                        /* Odd output line. Synthesize it. */
                        for (x = 0; x < w; x++) {
                                if (moving[x] == 1) {
                                        /* Make up a new line. Use cubic interpolation where there
                                           are enough samples and linear where there are not enough. */
                                        nextValue = srcn[x];

                                        r = (src[x] >> 16) & 0xff;
                                        rn = (nextValue >> 16) & 0xff;
                                        g = (src[x] >> 8) & 0xff;
                                        gn = (nextValue >>8) & 0xff;
                                        b = src[x] & 0xff;
                                        bn = nextValue & 0xff;
                                        if (y == 0 || y == hminus2) {	/* Not enough samples; use linear. */
                                                R = (r + rn) >> 1;
                                                G = (g + gn) >> 1;
                                                B = (b + bn) >> 1;
                                        } else {
                                                /* Enough samples; use cubic. */
                                                prevValue = srcp[x];
                                                nextnextValue = srcnn[x];
                                                rp = (prevValue >> 16) & 0xff;
                                                rnn = (nextnextValue >>16) & 0xff;
                                                gp = (prevValue >> 8) & 0xff;
                                                gnn = (nextnextValue >> 8) & 0xff;
                                                bp = prevValue & 0xff;
                                                bnn = nextnextValue & 0xff;
                                                R = (5 * (r + rn) - (rp + rnn)) >> 3;
                                                if (R > 255) R = 255;

                                                else if (R < 0) R = 0;

                                                G = (5 * (g + gn) - (gp + gnn)) >> 3;
                                                if (G > 255) G = 255;

                                                else if (G < 0) G = 0;

                                                B = (5 * (b + bn) - (bp + bnn)) >> 3;
                                                if (B > 255) B = 255;

                                                else if (B < 0) B = 0;

                                        }
                                        dst[x] = (R << 16) | (G << 8) | B;
                                } else {
                                        /* Use line from previous field. */
                                        dst[x] = prev[x];
                                }
                        }
                        src = (Pixel32 *)((char *)src + linewidth);

                        srcn = (Pixel32 *)((char *)srcn + linewidth);

                        srcnn = (Pixel32 *)((char *)srcnn + linewidth);

                        srcp = (Pixel32 *)((char *)srcp + linewidth);

                        dst = (Pixel32 *)((char *)dst + linewidth);
                        moving += w;
                        prev += w;
                }
                /* Copy through the last source line. */


                memcpy(dst, src, width * sizeof(Pixel32));
                if (!high_field) { //if (field_number % 2 != iOddEven) {
                        dst = (Pixel32 *)((char *)dst + linewidth);
                        memcpy(dst, src, width * sizeof(Pixel32));
                }

        } else {
                /* Show motion only. */
                moving = mfd_moving;
                src = (Pixel32 *)new_field;
                dst = (Pixel32 *)fa_dst_data;
                for (y = 0; y < hminus; y++) {
                        for (x = 0; x < w; x++) {
                                if (moving[x]) {
                                        dst[x] = ((Pixel32 *)((char *)dst + linewidth))[x] = src[x];
                                } else {
                                        dst[x] = ((Pixel32 *)((char *)dst + linewidth))[x] = 0;
                                }
                        }
                        src = (Pixel32 *)((char *)src + linewidth);
                        dst = (Pixel32 *)((char *)dst + linewidth);
                        dst = (Pixel32 *)((char *)dst + linewidth);
                        moving += w;
                }
        }

        /* Buffer the input frame (aka field). */
        src = (Pixel32 *)new_field;
        prev = mfd_prevFrame;
        for (y = 0; y < h; y++) {
                memcpy(prev, src, w * sizeof(Pixel32));
                src = (Pixel32 *)((char *)src + linewidth);
                prev += w;
        }

        emit image_ready(out);
}

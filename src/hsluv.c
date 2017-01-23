/*
 * hsluv-c
 * Copyright (c) 2017 Martin Mitas
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "hsluv.h"

#include <float.h>
#include <math.h>


typedef struct Triplet_tag Triplet;
struct Triplet_tag {
    double a;
    double b;
    double c;
};

/* for RGB */
static const Triplet m[3] = {
    {  3.2409699419045214,   -1.5373831775700935, -0.49861076029300328  },
    { -0.96924363628087983,   1.8759675015077207,  0.041555057407175613 },
    {  0.055630079696993609, -0.20397695888897657, 1.0569715142428786   }
};

/* for XYZ */
static const Triplet m_inv[3] = {
    { 0.41239079926595948,  0.35758433938387796, 0.18048078840183429  },
    { 0.21263900587151036,  0.71516867876775593, 0.072192315360733715 },
    { 0.019330818715591851, 0.11919477979462599, 0.95053215224966058  }
};

static const double pi = 3.14159265358979323846;

static const double ref_u = 0.19783000664283681;
static const double ref_v = 0.468319994938791;

static const double kappa = 903.2962962962963;
static const double epsilon = 0.0088564516790356308;


typedef struct Bounds_tag Bounds;
struct Bounds_tag {
    double a;
    double b;
};


static void
get_bounds(double l, Bounds bounds[6])
{
    double sub1 = pow(l + 16.0, 3.0) / 1560896.0;
    double sub2 = (sub1 > epsilon ? sub1 : (l / kappa));
    int channel;
    int t;

    for(channel = 0; channel < 3; channel++) {
        double m1 = m[channel].a;
        double m2 = m[channel].b;
        double m3 = m[channel].c;

        for (t = 0; t < 2; t++) {
            double top1 = (284517.0 * m1 - 94839.0 * m3) * sub2;
            double top2 = (838422.0 * m3 + 769860.0 * m2 + 731718.0 * m1) * l * sub2 -  769860.0 * t * l;
            double bottom = (632260.0 * m3 - 126452.0 * m2) * sub2 + 126452.0 * t;

            bounds[channel * 2 + t].a = top1 / bottom;
            bounds[channel * 2 + t].b = top2 / bottom;
        }
    }
}

static double
intersect_line_line(const Bounds* line1, const Bounds* line2)
{
    return (line1->b - line2->b) / (line2->a - line1->a);
}

static double
dist_from_pole(double x, double y)
{
    return sqrt(x * x + y * y);
}

static double
ray_length_until_intersect(double theta, const Bounds* line)
{
    return line->b / (sin(theta) - line->a * cos(theta));
}

static double
max_safe_chroma_for_l(double l)
{
    double min_len = FLT_MAX;
    Bounds bounds[6];
    int i;

    get_bounds(l, bounds);
    for(i = 0; i < 6; i++) {
        double m1 = bounds[i].a;
        double b1 = bounds[i].b;
        /* x where line intersects with perpendicular running though (0, 0) */
        Bounds line2 = { -1.0 / m1, 0.0 };
        double x = intersect_line_line(&bounds[i], &line2);
        double distance = dist_from_pole(x, b1 + x * m1);

        if(distance >= 0) {
            if(distance < min_len) {
                min_len = distance;
            }
        }
    }

    return min_len;
}

static double
max_chroma_for_lh(double l, double h)
{
    double min_len = FLT_MAX;
    double hrad = h / 360.0 * pi * 2.0;
    Bounds bounds[6];
    int i;

    get_bounds(l, bounds);
    for(i = 0; i < 6; i++) {
        double l = ray_length_until_intersect(hrad, &bounds[i]);

        if(l >= 0)  {
            if(l < min_len) {
                min_len = l;
            }
        }
    }
    return min_len;
}

static double
dot_product(const Triplet* t1, const Triplet* t2)
{
    return (t1->a * t2->a + t1->b * t2->b + t1->c * t2->c);
}

/* Used for rgb conversions */
static double
from_linear(double c)
{
    if(c <= 0.0031308)
        return 12.92 * c;
    else
        return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

static double
to_linear(double c)
{
    if (c > 0.04045)
        return pow((c + 0.055) / (1 + 0.055), 2.4);
    else
        return c / 12.92;
}

static void
xyz2rgb(Triplet* in_out)
{
    double r = from_linear(dot_product(&m[0], in_out));
    double g = from_linear(dot_product(&m[1], in_out));
    double b = from_linear(dot_product(&m[2], in_out));
    in_out->a = r;
    in_out->b = g;
    in_out->c = b;
}

static void
rgb2xyz(Triplet* in_out)
{
    Triplet rgbl = { to_linear(in_out->a), to_linear(in_out->b), to_linear(in_out->c) };
    double x = dot_product(&m_inv[0], &rgbl);
    double y = dot_product(&m_inv[1], &rgbl);
    double z = dot_product(&m_inv[2], &rgbl);
    in_out->a = x;
    in_out->b = y;
    in_out->c = z;
}

/* http://en.wikipedia.org/wiki/CIELUV
 * In these formulas, Yn refers to the reference white point. We are using
 * illuminant D65, so Yn (see refY in Maxima file) equals 1. The formula is
 * simplified accordingly.
 */
static double
y2l(double y)
{
    if(y <= epsilon)
        return y * kappa;
    else
        return 116.0 * powl(y, 1.0/3.0) - 16.0;
}

static double
l2y(double l)
{
    if(l <= 8.0)
        return l / kappa;
    else
        return powl((l + 16.0) / 116.0, 3.0);
}

static void
xyz2luv(Triplet* in_out)
{
    double var_u = (4.0 * in_out->a) / (in_out->a + (15.0 * in_out->b) + (3.0 * in_out->c));
    double var_v = (9.0 * in_out->b) / (in_out->a + (15.0 * in_out->b) + (3.0 * in_out->c));
    double l = y2l(in_out->b);
    double u = 13.0 * l * (var_u - ref_u);
    double v = 13.0 * l * (var_v - ref_v);

    in_out->a = l;
    if(l < 0.00000001) {
        in_out->b = 0.0;
        in_out->c = 0.0;
    } else {
        in_out->b = u;
        in_out->c = v;
    }
}

static void
luv2xyz(Triplet* in_out)
{
    if(in_out->a <= 0.00000001) {
        /* Black will create a divide-by-zero error. */
        in_out->a = 0.0;
        in_out->b = 0.0;
        in_out->c = 0.0;
        return;
    }

    double var_u = in_out->b / (13.0 * in_out->a) + ref_u;
    double var_v = in_out->c / (13.0 * in_out->a) + ref_v;
    double y = l2y(in_out->a);
    double x = -(9.0 * y * var_u) / ((var_u - 4.0) * var_v - var_u * var_v);
    double z = (9.0 * y - (15.0 * var_v * y) - (var_v * x)) / (3 * var_v);
    in_out->a = x;
    in_out->b = y;
    in_out->c = z;
}

static void
luv2lch(Triplet* in_out)
{
    double l = in_out->a;
    double u = in_out->b;
    double v = in_out->c;
    double h;
    double c = sqrtf(powf(u, 2.0) + powf(v, 2.0));

    // Grays: disambiguate hue
    if(c < 0.00000001) {
        h = 0;
    } else {
        h = atan2(v, u) * (360.0 / 2.0 / pi);
        if(h < 0.0)
            h += 360.0;
    }

    in_out->a = l;
    in_out->b = c;
    in_out->c = h;
}

static void
lch2luv(Triplet* in_out)
{
    double hrad = in_out->c * (2.0 * pi / 360.0);
    double u = cosf(hrad) * in_out->b;
    double v = sinf(hrad) * in_out->b;

    in_out->b = u;
    in_out->c = v;
}

static void
hsluv2lch(Triplet* in_out)
{
    double h = in_out->a;
    double s = in_out->b;
    double l = in_out->c;
    double c;

    /* White and black: disambiguate chroma */
    if(l > 99.9999999 || l < 0.00000001)
        c = 0.0;
    else
        c = max_chroma_for_lh(l, h) / 100.0 * s;

    /* Grays: disambiguate hue */
    if (s < 0.00000001)
        h = 0;

    in_out->a = l;
    in_out->b = c;
    in_out->c = h;
}

static void
lch2hsluv(Triplet* in_out)
{
    double l = in_out->a;
    double c = in_out->b;
    double h = in_out->c;
    double s;

    /* White and black: disambiguate saturation */
    if(l > 99.9999999 || l < 0.00000001)
        s = 0;
    else
        s = c / max_chroma_for_lh(l, h) * 100.0;

    /* Grays: disambiguate hue */
    if (c < 0.00000001)
        h = 0;

    in_out->a = h;
    in_out->b = s;
    in_out->c = l;
}

static void
hpluv2lch(Triplet* in_out)
{
    double h = in_out->a;
    double s = in_out->b;
    double l = in_out->c;
    double c;

    /* White and black: disambiguate chroma */
    if(l > 99.9999999 || l < 0.00000001)
        c = 0.0;
    else
        c = max_safe_chroma_for_l(l) / 100.0 * s;

    /* Grays: disambiguate hue */
    if (s < 0.00000001)
        h = 0.0;

    in_out->a = l;
    in_out->b = c;
    in_out->c = h;
}

static void
lch2hpluv(Triplet* in_out)
{
    double l = in_out->a;
    double c = in_out->b;
    double h = in_out->c;
    double s;

    /* White and black: disambiguate saturation */
    if (l > 99.9999999 || l < 0.00000001)
        s = 0.0;
    else
        s = c / max_safe_chroma_for_l(l) * 100.0;

    /* Grays: disambiguate hue */
    if (c < 0.00000001)
        h = 0;

    in_out->a = h;
    in_out->b = s;
    in_out->c = l;
}



void
hsluv2rgb(double h, double s, double l, double* pr, double* pg, double* pb)
{
    Triplet tmp = { h, s, l };

    hsluv2lch(&tmp);
    lch2luv(&tmp);
    luv2xyz(&tmp);
    xyz2rgb(&tmp);

    *pr = tmp.a;
    *pg = tmp.b;
    *pb = tmp.c;
}

void
hpluv2rgb(double h, double s, double l, double* pr, double* pg, double* pb)
{
    Triplet tmp = { h, s, l };

    hpluv2lch(&tmp);
    lch2luv(&tmp);
    luv2xyz(&tmp);
    xyz2rgb(&tmp);

    *pr = tmp.a;
    *pg = tmp.b;
    *pb = tmp.c;
}

#include <stdio.h>

void
rgb2hsluv(double r, double g, double b, double* ph, double* ps, double* pl)
{
    Triplet tmp = { r, g, b };

    rgb2xyz(&tmp);
    xyz2luv(&tmp);
    luv2lch(&tmp);
    lch2hsluv(&tmp);

    *ph = tmp.a;
    *ps = tmp.b;
    *pl = tmp.c;
}

void
rgb2hpluv(double r, double g, double b, double* ph, double* ps, double* pl)
{
    Triplet tmp = { r, g, b };

    rgb2xyz(&tmp);
    xyz2luv(&tmp);
    luv2lch(&tmp);
    lch2hpluv(&tmp);

    *ph = tmp.a;
    *ps = tmp.b;
    *pl = tmp.c;
}
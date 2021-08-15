/* $Id: icongen.c,v 1.8 2009-07-22 07:23:02 ecd Exp $
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <cairo.h>

static char *progname;

typedef struct {
	double		x;
	double		y;
} point_t;

typedef struct {
	double		y1;	/* y coordinate of top edge */
	double		x11;	/* x coordinate of top left corner */
	double		x21;	/* x coordinate of top right corner */
	double		y2;	/* y coordinate of bottom edge */
	double		x12;	/* x coordinate of bottom left corner */
	double		x22;	/* x coordinate of bottom right corner */
} trapezoid_t;

static double
i2d(int x)
{
	return (double) x;
}

static int
draw_line(cairo_t *cr, uint32_t color, double w,
	  double x1, double y1, double x2, double y2)
{
	double r, g, b, a;

	r = ((double) ((color >> 24) & 0xff)) / 255.0;
	g = ((double) ((color >> 16) & 0xff)) / 255.0;
	b = ((double) ((color >>  8) & 0xff)) / 255.0;
	a = ((double) ((color >>  0) & 0xff)) / 255.0;

	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_set_line_width(cr, w);

	cairo_move_to(cr, x1, y1);
	cairo_line_to(cr, x2, y2);

	cairo_stroke(cr);
	return 0;
}

static int
draw_filled_circle(cairo_t *cr, uint32_t color,
		   double x, double y, double r)
{
	double R, G, B, A;
	double x2, y2;
	double a, inc;

	R = ((double) ((color >> 24) & 0xff)) / 255.0;
	G = ((double) ((color >> 16) & 0xff)) / 255.0;
	B = ((double) ((color >>  8) & 0xff)) / 255.0;
	A = ((double) ((color >>  0) & 0xff)) / 255.0;

	cairo_set_source_rgba(cr, R, G, B, A);

	if (r < 7.0)
		inc = M_PI / 8;
	else
		inc = asin(2.5 / r);

	x2 = x + r * cos(0.0);
	y2 = y - r * sin(0.0);
	cairo_move_to(cr, x2, y2);

	for (a = inc; a < 2.0 * M_PI; a += inc) {
		x2 = x + r * cos(a);
		y2 = y + r * sin(a);

		cairo_line_to(cr, x2, y2);
	}
	x2 = x + r * cos(2.0 * M_PI);
	y2 = y + r * sin(2.0 * M_PI);

	cairo_line_to(cr, x2, y2);
	cairo_fill(cr);

	return 0;
}

static int
draw_circle(cairo_t *cr, uint32_t color, double w,
	    double x, double y, double r)
{
	double R, G, B, A;
	double x2, y2;
	double a, inc;

	R = ((double) ((color >> 24) & 0xff)) / 255.0;
	G = ((double) ((color >> 16) & 0xff)) / 255.0;
	B = ((double) ((color >>  8) & 0xff)) / 255.0;
	A = ((double) ((color >>  0) & 0xff)) / 255.0;

	cairo_set_source_rgba(cr, R, G, B, A);
	cairo_set_line_width(cr, w);

	if (r < 7.0)
		inc = M_PI / 8;
	else
		inc = asin(2.5 / r);

	x2 = x + r * cos(0.0);
	y2 = y - r * sin(0.0);
	cairo_move_to(cr, x2, y2);
	for (a = inc; a < 2.0 * M_PI; a += inc) {
		x2 = x + r * cos(a);
		y2 = y - r * sin(a);

		cairo_line_to(cr, x2, y2);
	}
	x2 = x + r * cos(2.0 * M_PI);
	y2 = y + r * sin(2.0 * M_PI);

	cairo_line_to(cr, x2, y2);
	cairo_stroke(cr);

	return 0;
}

static int
generate_icon(int width, int height, const char *filename)
{
	cairo_surface_t *surface;
	cairo_t *cr;
	double cx, cy, r;
	double lw, a, a1;
	int err;
	int i;

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	cr = cairo_create(surface);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	cx = i2d(width) / 2.0;
	cy = i2d(height) / 2.0;
	if (width < height)
		r = i2d(width) / 2.0 - 1.0;
	else
		r = i2d(height) / 2.0 - 1.0;

	err = draw_filled_circle(cr, 0xffffffff, cx, cy, r);
	if (err)
		return err;

	lw = 1.0;
	if (r < 10.0)
		lw = 0.75;

	err = draw_circle(cr, 0x808080ff, lw, cx, cy, r + 0.25);
	if (err)
		return err;

	err = draw_circle(cr, 0x808080ff, lw, cx, cy, 2.0 * r / 3.0);
	if (err)
		return err;

	if (r >= 10.0) {
		err = draw_circle(cr, 0x808080ff, lw, cx, cy, 1.0 * r / 3.0);
		if (err)
			return err;
	}

	err = draw_line(cr, 0x808080ff, lw, cx, cy - r, cx, cy + r);
	if (err)
		return err;

	err = draw_line(cr, 0x808080ff, lw, cx - r, cy, cx + r, cy);
	if (err)
		return err;

	for (i = 1; i < 12; i++) {
		if ((i % 3) == 0)
			continue;

		a = i2d(i) * M_PI / 6.0;
		err = draw_line(cr, 0x808080ff, lw,
				cx + r / 6.0 * sin(a),
				cy - r / 6.0 * cos(a),
				cx + r * sin(a), cy - r * cos(a));
		if (err)
			return err;
	}

/*
 * Heading Line:
 */
	err = draw_line(cr, 0x008000ff, 2.0 * lw,
			cx, cy, cx, cy - r);
	if (err)
		return err;


	a = 50.0 * M_PI / 180.0;
	a1 = 90.0 * M_PI / 180.0;
	err = draw_line(cr, 0x008000ff, 2.0 * lw,
			cx + 5.5 * r / 6.0 * sin(a),
			cy - 5.5 * r / 6.0 * cos(a),
			cx + 4.2 * r / 6.0 * sin(a1),
			cy - 4.2 * r / 6.0 * cos(a1));

	a = 52.0 * M_PI / 180.0;
	a1 = 90.0 * M_PI / 180.0;
	err = draw_line(cr, 0x0000c0ff, 2.0 * lw,
			cx + 2.5 * r / 6.0 * sin(a),
			cy - 2.5 * r / 6.0 * cos(a),
			cx + 4.2 * r / 6.0 * sin(a1),
			cy - 4.2 * r / 6.0 * cos(a1));


	a = 50.0 * M_PI / 180.0;
	a1 = 52.0 * M_PI / 180.0;
	err = draw_line(cr, 0x800000ff, 2.0 * lw,
			cx + 5.5 * r / 6.0 * sin(a),
			cy - 5.5 * r / 6.0 * cos(a),
			cx + 2.5 * r / 6.0 * sin(a1),
			cy - 2.5 * r / 6.0 * cos(a1));

	a = 224.5 * M_PI / 180.0;
	err = draw_line(cr, 0x800000ff, 1.0 * lw,
			cx + 2.5 * r / 6.0 * sin(a1),
			cy - 2.5 * r / 6.0 * cos(a1),
			cx + 6.0 * r / 6.0 * sin(a),
			cy - 6.0 * r / 6.0 * cos(a));


	if (CAIRO_STATUS_SUCCESS != cairo_surface_write_to_png(surface, filename)) {
		fprintf(stderr, "%s: cairo_surface_write_to_png(%s) failed\n",
			progname, filename);
		return 1;
	}

	return 0;
}

int
main(int argc, char **argv)
{
	int width, height;
	char *end, *p;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	if (argc != 3) {
		fprintf(stderr, "usage: %s <width>x<height> <filename>\n", progname);
		exit(1);
	}

	width = strtoul(argv[1], &end, 10);
	if (argv[1] == end || *end != 'x') {
		fprintf(stderr, "usage: %s <width>x<height> <filename>\n", progname);
		exit(1);
	}
	p = end + 1;

	height = strtoul(p, &end, 10);
	if (p == end || *end != '\0') {
		fprintf(stderr, "usage: %s <width>x<height> <filename>\n", progname);
		exit(1);
	}

	return generate_icon(width, height, argv[2]);
}

/* $Id: print.c,v 1.33 2009-07-24 11:37:17 ecd Exp $
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <time.h>

#include <gtk/gtk.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "radar.h"
#include "afm.h"


#undef DEBUG_LABEL_ALIGN


typedef struct {
	const char	*name;
	unsigned int	pt_width;
	unsigned int	pt_height;
} paper_format_t;

typedef struct {
	double		llx;
	double		lly;
	double		urx;
	double		ury;
} pdf_rect_t;


static const paper_format_t paper_formats[] =
{
	{ N_("DIN A4"),			 841,	 595 },
	{ N_("DIN A3"),			1190,	 841 },
	{ N_("ANSI-A (Letter)"),	 792,	 612 },
	{ N_("ANSI-B (Ledger)"),	1224,	 792 }
};
#define NR_PAPER_FORMATS (sizeof(paper_formats) / sizeof(paper_formats[0]))

typedef struct {
	const char	*row;
	const char	*unit;
	unsigned int	columns;
	unsigned int	print_if_empty;
	unsigned int	alignment;
	int		(*get_text) (radar_t *radar, unsigned int column,
				     unsigned char *buffer, size_t buflen);
} table_descriptor_t;

#define TABLE_MAX_COLUMNS	(RADAR_NR_TARGETS)
#define TABLE_EMPTY_COLUMNS	3

#define TABLE_ALIGN_LEFT	0x00
#define TABLE_ALIGN_CENTER	0x01
#define TABLE_ALIGN_RIGHT	0x02
#define TABLE_ALIGN_MASK	0x03
#define TABLE_ALIGN_DECIMAL	0x80
#define TABLE_ALIGN_DEC_LEFT	(TABLE_ALIGN_DECIMAL | TABLE_ALIGN_LEFT)
#define TABLE_ALIGN_DEC_CENTER	(TABLE_ALIGN_DECIMAL | TABLE_ALIGN_CENTER)
#define TABLE_ALIGN_DEC_RIGHT	(TABLE_ALIGN_DECIMAL | TABLE_ALIGN_RIGHT)

static int table_get_orient(radar_t *radar, unsigned int column,
			    unsigned char *buffer, size_t buflen);
static int table_get_range(radar_t *radar, unsigned int column,
			   unsigned char *buffer, size_t buflen);
static int table_get_KA(radar_t *radar, unsigned int column,
			unsigned char *buffer, size_t buflen);
static int table_get_vA(radar_t *radar, unsigned int column,
			unsigned char *buffer, size_t buflen);

static int table_get_opponent(radar_t *radar, unsigned int column,
			      unsigned char *buffer, size_t buflen);

static int table_get_target0_time(radar_t *radar, unsigned int column,
				 unsigned char *buffer, size_t buflen);
static int table_get_target0_rasp(radar_t *radar, unsigned int column,
				 unsigned char *buffer, size_t buflen);
static int table_get_target0_rwk(radar_t *radar, unsigned int column,
				unsigned char *buffer, size_t buflen);
static int table_get_target0_rakrp(radar_t *radar, unsigned int column,
				  unsigned char *buffer, size_t buflen);
static int table_get_target0_distance(radar_t *radar, unsigned int column,
				     unsigned char *buffer, size_t buflen);

static int table_get_target1_time(radar_t *radar, unsigned int column,
				 unsigned char *buffer, size_t buflen);
static int table_get_target1_rasp(radar_t *radar, unsigned int column,
				 unsigned char *buffer, size_t buflen);
static int table_get_target1_rwk(radar_t *radar, unsigned int column,
				unsigned char *buffer, size_t buflen);
static int table_get_target1_rakrp(radar_t *radar, unsigned int column,
				  unsigned char *buffer, size_t buflen);
static int table_get_target1_distance(radar_t *radar, unsigned int column,
				     unsigned char *buffer, size_t buflen);

static int table_get_target_interval(radar_t *radar, unsigned int column,
				    unsigned char *buffer, size_t buflen);
static int table_get_target_KBr(radar_t *radar, unsigned int column,
			       unsigned char *buffer, size_t buflen);
static int table_get_target_vBr(radar_t *radar, unsigned int column,
			       unsigned char *buffer, size_t buflen);
static int table_get_target_KB(radar_t *radar, unsigned int column,
			      unsigned char *buffer, size_t buflen);
static int table_get_target_vB(radar_t *radar, unsigned int column,
			      unsigned char *buffer, size_t buflen);
static int table_get_target_aspect(radar_t *radar, unsigned int column,
				   unsigned char *buffer, size_t buflen);
static int table_get_target_CPA(radar_t *radar, unsigned int column,
			       unsigned char *buffer, size_t buflen);
static int table_get_target_PCPA(radar_t *radar, unsigned int column,
			        unsigned char *buffer, size_t buflen);
static int table_get_target_SPCPA(radar_t *radar, unsigned int column,
			         unsigned char *buffer, size_t buflen);
static int table_get_target_TCPA(radar_t *radar, unsigned int column,
			        unsigned char *buffer, size_t buflen);
static int table_get_target_tCPA(radar_t *radar, unsigned int column,
			        unsigned char *buffer, size_t buflen);
static int table_get_target_BCR(radar_t *radar, unsigned int column,
			       unsigned char *buffer, size_t buflen);
static int table_get_target_BCT(radar_t *radar, unsigned int column,
			        unsigned char *buffer, size_t buflen);
static int table_get_target_BCt(radar_t *radar, unsigned int column,
			        unsigned char *buffer, size_t buflen);

static int table_get_maneuver_time(radar_t *radar, unsigned int column,
				   unsigned char *buffer, size_t buflen);
static int table_get_maneuver_distance(radar_t *radar, unsigned int column,
				       unsigned char *buffer, size_t buflen);
static int table_get_maneuver_bearing(radar_t *radar, unsigned int column,
				      unsigned char *buffer, size_t buflen);

static int table_get_maneuver_type(radar_t *radar, unsigned int column,
				   unsigned char *buffer, size_t buflen);
static int table_get_maneuver_CPA(radar_t *radar, unsigned int column,
				  unsigned char *buffer, size_t buflen);
static int table_get_new_KA(radar_t *radar, unsigned int column,
			    unsigned char *buffer, size_t buflen);
static int table_get_new_vA(radar_t *radar, unsigned int column,
			    unsigned char *buffer, size_t buflen);

static int table_get_new_KBr(radar_t *radar, unsigned int column,
			     unsigned char *buffer, size_t buflen);
static int table_get_new_vBr(radar_t *radar, unsigned int column,
			     unsigned char *buffer, size_t buflen);
static int table_get_delta(radar_t *radar, unsigned int column,
			   unsigned char *buffer, size_t buflen);
static int table_get_new_RaSP(radar_t *radar, unsigned int column,
			      unsigned char *buffer, size_t buflen);
static int table_get_new_aspect(radar_t *radar, unsigned int column,
				unsigned char *buffer, size_t buflen);
static int table_get_new_CPA(radar_t *radar, unsigned int column,
			     unsigned char *buffer, size_t buflen);
static int table_get_new_PCPA(radar_t *radar, unsigned int column,
			      unsigned char *buffer, size_t buflen);
static int table_get_new_SPCPA(radar_t *radar, unsigned int column,
			       unsigned char *buffer, size_t buflen);
static int table_get_new_TCPA(radar_t *radar, unsigned int column,
			      unsigned char *buffer, size_t buflen);
static int table_get_new_tCPA(radar_t *radar, unsigned int column,
			      unsigned char *buffer, size_t buflen);
static int table_get_new_BCR(radar_t *radar, unsigned int column,
			     unsigned char *buffer, size_t buflen);
static int table_get_new_BCT(radar_t *radar, unsigned int column,
			      unsigned char *buffer, size_t buflen);
static int table_get_new_BCt(radar_t *radar, unsigned int column,
			      unsigned char *buffer, size_t buflen);

static const table_descriptor_t data_table[] =
{
	{ N_("Radar Settings"),		NULL,			0 },
	{ N_("Orientation (Course/North Up)"),	NULL,		1,
	  0, TABLE_ALIGN_CENTER, table_get_orient },
	{ N_("Range"),			N_("nm"),		1,
	  1, TABLE_ALIGN_DEC_CENTER, table_get_range },

	{ NULL,				NULL,			0 },
	{ N_("Own Ship"),		NULL,			0 },
	{ N_("T CRS"),			N_("\260"),		1,
	  0, TABLE_ALIGN_CENTER, table_get_KA },
	{ N_("SPD (STW)"),		N_("kn"),		1,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_vA },

	{ NULL,				NULL,			0 },
	{ N_("Observations"),		NULL,			0 },
	{ N_("Target"),			NULL,		TABLE_MAX_COLUMNS,
	  1, TABLE_ALIGN_CENTER, table_get_opponent },
	{ N_("Time"),			N_("Clock"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target0_time },
	{ N_("R BRG to Target"),	N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target0_rasp },
	{ N_("at T HDG of Own Ship"),	N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target0_rwk },
	{ N_("T BRG to Target"),	N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target0_rakrp },
	{ N_("Distance"),		N_("nm"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_target0_distance },
	{ N_("Time"),			N_("Clock"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target1_time },
	{ N_("R BRG to Target"),	N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target1_rasp },
	{ N_("at T HDG of Own Ship"),	N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target1_rwk },
	{ N_("T BRG to Target"),	N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target1_rakrp },
	{ N_("Distance"),		N_("nm"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_target1_distance },

	{ NULL,				NULL,			0 },
	{ N_("Completed Data"),		NULL,			0 },
	{ N_("Observation Interval"),	N_("min"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target_interval },
	{ N_("DRM of Target"),		N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target_KBr },
	{ N_("SRM of Target"),		N_("kn"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_target_vBr },
	{ N_("T CRS of Target"),	N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target_KB },
	{ N_("T SPD of Target"),	N_("kn"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_target_vB },
	{ N_("Aspect Angle"),		N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target_aspect },
	{ N_("Range at CPA"),		N_("nm"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_target_CPA },
	{ N_("T BRG at CPA"),		N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target_PCPA },
	{ N_("R BRG at CPA"),		N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target_SPCPA },
	{ N_("TCPA"),			N_("min"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_target_TCPA },
	{ "",				N_("Clock"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target_tCPA },
	{ N_("BCR (Bow Crossing Range)"), N_("nm"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_target_BCR },
	{ N_("BCT"),			N_("min"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_target_BCT },
	{ "",				N_("Clock"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_target_BCt },

	{ NULL,				NULL,			0 },
	{ N_("Maneuver"),		NULL,			0 },
	{ N_("Time"),			N_("Clock"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_maneuver_time },
	{ N_("Distance"),		N_("nm"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_maneuver_distance },
	{ N_("T BRG to Target"),	N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_maneuver_bearing },

	{ N_("Maneuver (Course/Speed Change)"),	NULL,	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_maneuver_type },
	{ N_("new CPA"),		N_("nm"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_maneuver_CPA },
	{ N_("new Course"),		N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_new_KA },
	{ N_("new Speed"),		N_("kn"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_new_vA },

	{ NULL,				NULL,			0 },
	{ N_("Completed Data after Maneuver"),	NULL,		0 },
	{ N_("DRM of Target"),		N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_new_KBr },
	{ N_("SRM of Target"),		N_("kn"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_new_vBr },
	{ N_("delta in DRM"),		N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_delta },
	{ N_("R BRG to Target"),	N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_new_RaSP },
	{ N_("Aspect Angle"),		N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_new_aspect },
	{ N_("Range at CPA"),		N_("nm"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_new_CPA },
	{ N_("T BRG at CPA"),		N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_new_PCPA },
	{ N_("R BRG at CPA"),		N_("\260"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_new_SPCPA },
	{ N_("TCPA"),			N_("min"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_new_TCPA },
	{ "",				N_("Clock"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_new_tCPA },
	{ N_("BCR (Bow Crossing Range)"), N_("nm"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_new_BCR },
	{ N_("BCT"),			N_("min"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_DEC_CENTER, table_get_new_BCT },
	{ "",				N_("Clock"),	TABLE_MAX_COLUMNS,
	  0, TABLE_ALIGN_CENTER, table_get_new_BCt }
};
#define NR_DATA_TABLE_ROWS	(sizeof(data_table) / sizeof(data_table[0]))

static char_t *pdf_char_bullet;


/*
 * Save plot as image:
 *
 * Plot into GdkPixmap, convert to GdkPixbuf, save as any Format Image.
 */
static void
translate_point(double x1, double y1, double cx1, double cy1, int r1,
		double cx2, double cy2, int r2, double *x2, double *y2)
{
	*x2 = cx2 + (x1 - cx1) * ((double) r2) / ((double) r1);
	*y2 = cy2 + (y1 - cy1) * ((double) r2) / ((double) r1);
}

static void
translate_length(double l1, int r1, int r2, double *l2)
{
	*l2 = l1 * ((double) r2) / ((double) r1);
}

int
radar_save_as_image(radar_t *radar, int width, int height,
		    const char *filename, const char *type,
		    char **keys, char **values)
{
	PangoFontDescription *font_description;
	PangoContext *context;
	PangoAttrList *attrs;
	PangoLayout *layout;
	char label_font[32];
	GdkPixbuf *pixbuf;
	GError *gerror;
	target_t *s;
	vector_t *v, tvec;
	arc_t *a, tarc;
	poly_t *p, tpoly;
	text_label_t *l, tlabel;
	char text[16];
	double cx, cy;
	int step, radius;
	int tw, th;
	int base, size;
	int error = 0;
	int i, j, k;


	if (width < height)
		base = width;
	else
		base = height;

	size = (base / 60);
	if (size < 8)
		size = 8;
	if (size > 24)
		size = 24;

	sprintf(label_font, "%s %u", LABEL_BASE, size);

	context = gtk_widget_get_pango_context(radar->canvas);
	layout = pango_layout_new(context);
	font_description = pango_font_description_from_string(label_font);

	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_font_description(layout, font_description);

	attrs = pango_attr_list_new();
	pango_attr_list_insert(attrs, pango_attr_foreground_new(0, 0, 0));
	pango_attr_list_insert(attrs,
			pango_attr_background_new(0xffff, 0xffff, 0xffff));
	pango_layout_set_attributes(layout, attrs);

	sprintf((char *) text, "%03u", 270);
	pango_layout_set_text(layout, text, strlen(text));
	pango_layout_get_pixel_size(layout, &tw, &th);
	tw += BG_TEXT_SIDE_SPACING;

	cx = ((double) width) / 2.0;
	cy = ((double) height) / 2.0;

	if (width < height)
		radius = ((width / 2 - tw) / 12) * 12;
	else
		radius = ((height / 2 - tw) / 12) * 12;
	step = radius / 6;

	pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
	if (NULL == pixbuf) {
		fprintf(stderr, "%s: gdk_pixbuf_new: out of memory\n",
			__FUNCTION__);
		return -ENOMEM;
	}

	gdk_window_set_cursor(radar->window->window, radar->busy_cursor);
	gdk_display_sync(gdk_drawable_get_display(radar->window->window));

	radar_draw_bg_pixmap(radar, radar->canvas->window, pixbuf, 0xff,
			     layout, TRUE, cx, cy, step, radius, width, height);

	for (i = 0; i < RADAR_NR_VECTORS; i++) {
		v = &radar->vectors[i];

		if (!v->is_visible)
			continue;

		tvec.gc = v->gc;
		translate_point(v->x1, v->y1, radar->cx, radar->cy, radar->r,
				cx, cy, radius, &tvec.x1, &tvec.y1);
		translate_point(v->x2, v->y2, radar->cx, radar->cy, radar->r,
				cx, cy, radius, &tvec.x2, &tvec.y2);

		radar_draw_vector(radar, radar->canvas->window,
				  pixbuf, 0xff, TRUE, &tvec);
	}

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		s = &radar->target[i];

		for (j = 0; j < TARGET_NR_VECTORS; j++) {
			v = &s->vectors[j];

			if (!v->is_visible)
				continue;

			tvec.gc = v->gc;
			translate_point(v->x1, v->y1,
					radar->cx, radar->cy, radar->r,
					cx, cy, radius, &tvec.x1, &tvec.y1);
			translate_point(v->x2, v->y2,
					radar->cx, radar->cy, radar->r,
					cx, cy, radius, &tvec.x2, &tvec.y2);

			radar_draw_vector(radar, radar->canvas->window,
					  pixbuf, 0xff, TRUE, &tvec);
		}

		for (j = 0; j < TARGET_NR_ARCS; j++) {
			a = &s->arcs[j];

			if (!a->is_visible)
				continue;

			tarc.gc = a->gc;
			tarc.angle1 = a->angle1;
			tarc.angle2 = a->angle2;
			translate_point(a->x, a->y,
					radar->cx, radar->cy, radar->r,
					cx, cy, radius, &tarc.x, &tarc.y);
			translate_length(a->radius, radar->r,
					 radius, &tarc.radius);

			radar_draw_arc(radar, radar->canvas->window,
				       pixbuf, 0xff, TRUE, &tarc);
		}

		for (j = 0; j < TARGET_NR_POLYS; j++) {
			p = &s->polys[j];

			if (!p->is_visible)
				continue;

			tpoly.gc = p->gc;
			tpoly.npoints = p->npoints;
			for (k = 0; k < tpoly.npoints; k++) {
				translate_point(p->points[k].x, p->points[k].y,
						radar->cx, radar->cy, radar->r,
						cx, cy, radius,
						&tpoly.points[k].x,
						&tpoly.points[k].y);
			}

			radar_draw_poly(radar, radar->canvas->window,
					pixbuf, 0xff, TRUE, &tpoly);
		}

		for (j = 0; j < TARGET_NR_LABELS; j++) {
			l = &s->labels[j];

			if (!l->is_visible)
				continue;

			tlabel.fg = l->fg;
			tlabel.bg = l->bg;
			tlabel.xalign = l->xalign;
			tlabel.yalign = l->yalign;

			tlabel.layout = pango_layout_copy(l->layout);
			pango_layout_set_font_description(tlabel.layout,
							  font_description);
			pango_layout_get_pixel_size(tlabel.layout,
						    (int *) &tlabel.tw,
						    (int *) &tlabel.th);

			translate_point(l->cx, l->cy,
					radar->cx, radar->cy, radar->r,
					cx, cy, radius, &tlabel.cx, &tlabel.cy);
			translate_length(l->xoff, radar->r, radius,
					 &tlabel.xoff);
			translate_length(l->yoff, radar->r, radius,
					 &tlabel.yoff);

			radar_draw_label(radar, radar->canvas->window,
					 pixbuf, 0xff, TRUE, &tlabel);

			g_object_unref(tlabel.layout);
		}
	}


	gerror = NULL;
	if (!gdk_pixbuf_savev(pixbuf, filename, type, keys, values, &gerror)) {
		fprintf(stderr, "%s: gdk_pixbuf_savev: %s\n",
			__FUNCTION__, gerror->message);
		error = -EIO;
		goto out;
	}

out:
	gdk_window_set_cursor(radar->window->window, NULL);

	g_object_unref(pixbuf);
	g_object_unref(layout);
	pango_font_description_free(font_description);
	pango_attr_list_unref(attrs);
	return error;
}

/*
 * Print Radarplot:
 *
 * Plot Background and Vectors as PDF,
 * Plot Calculated Values as PDF.
 */
static int
output_gc_rgb_color(FILE *file, GdkGC *gc)
{
	GdkGCValues values;
	GdkColormap *cmap;
	GdkColor color;
	double r, g, b;

	gdk_gc_get_values(gc, &values);
	color.pixel = values.foreground.pixel;

	cmap = gdk_gc_get_colormap(gc);
	if (cmap)
		gdk_colormap_query_color(cmap, color.pixel, &color);
	else
		g_warning("No colormap in output_gc_rgb_color");

	r = (double) (color.red >> 8) / 255.0;
	g = (double) (color.green >> 8) / 255.0;
	b = (double) (color.blue >> 8) / 255.0;

	return fprintf(file, "%.4f %.4f %.4f rg\n%.4f %.4f %.4f RG\n",
		       r, g, b, r, g, b);
}

static int
output_arc(FILE *file, double cx, double cy, double radius,
	   double angle1, double angle2)
{
	int offset = 0;
	double start, delta, sign, angle, inc;
	double x, y;

	start = M_PI * angle1 / 180.0;
	delta = M_PI * angle2 / 180.0;
	if (delta < 0) {
		sign = -1.0;
		delta = -delta;
	} else {
		sign = 1.0;
	}

	inc = M_PI / 180.0;

	x = cx + radius * cos(start);
	y = cy + radius * sin(start);

	offset += fprintf(file, "%.3f %.3f m\n", x, y);
	
	for (angle = inc; angle < delta; angle += inc) {
		x = cx + radius * cos(start + sign * angle);
		y = cy + radius * sin(start + sign * angle);

		offset += fprintf(file, "%.3f %.3f l\n", x, y);
	}

	x = cx + radius * cos(start + sign * delta);
	y = cy + radius * sin(start + sign * delta);

	offset += fprintf(file, "%.3f %.3f l\n", x, y);

	offset += fprintf(file, "S\n");
	return offset;
}

static int
output_linear_scale(FILE *file, afm_t *afm, double fs, double lw,
		    double width, double tick, double start, double end)
{
	unsigned char text[32];
	afm_extents_t extents;
	double factor, x, y, cursor, c2, step;
	int offset = 0;
	int i;

	factor = width / (end - start);
	step = 1.0;	/* FIXME */

	offset += fprintf(file, "0 g\n");
	offset += fprintf(file, "0 G\n");
	offset += fprintf(file, "%.3f w\n", lw);

	x = 0.0;
	y = 0.0;

	offset += fprintf(file, "%.3f %.3f m\n", x, y);

	x = factor * (end - start);
	y = 0.0;

	offset += fprintf(file, "%.3f %.3f l\n", x, y);
	offset += fprintf(file, "S\n");

	offset += fprintf(file, "%.3f w\n", lw / 2.0);

	sprintf((char *) text, _("Nautical Miles"));
	afm_text_extents(afm, 1.5 * fs, (char *) text,
			 strlen((char *) text), &extents);
	x = -extents.bbox.llx;
	y = -extents.ascent - tick / 4.0;
	offset += afm_print_text(file, afm, 1.5 * fs, x, y,
				 (char *) text, strlen((char *) text));


	cursor = start;
	while (cursor < end) {
		x = factor * (cursor - start);
		y = 0.0;

		offset += fprintf(file, "%.3f %.3f m\n", x, y);
		y = tick;
		offset += fprintf(file, "%.3f %.3f l\n", x, y);
		offset += fprintf(file, "S\n");


		if (cursor < 10)
			sprintf((char *) text, "%.1f", cursor);
		else
			sprintf((char *) text, "%.0f", cursor);


		afm_text_extents(afm, fs, (char *) text,
				 strlen((char *) text), &extents);

		x -= extents.width / 2.0;
		y = tick + tick / 4.0;

		offset += afm_print_text(file, afm, fs, x, y,
					 (char *) text, strlen((char *) text));

		for (i = 1; i < 10; i++) {
			c2 = cursor + (double)i * step / 10.0;
			x = factor * (c2 - start);
			y = 0.0;

			offset += fprintf(file, "%.3f %.3f m\n", x, y);
			if (i == 5)
				y += 2.0 * tick / 3.0;
			else
				y += tick / 2.0;
			offset += fprintf(file, "%.3f %.3f l\n", x, y);
			offset += fprintf(file, "S\n");
		}

		cursor += step;
	}

	cursor = end;

	x = factor * (cursor - start);
	y = 0.0;

	offset += fprintf(file, "%.3f %.3f m\n", x, y);
	y = tick;
	offset += fprintf(file, "%.3f %.3f l\n", x, y);
	offset += fprintf(file, "S\n");

	if (cursor < 10)
		sprintf((char *) text, "%.1f", cursor);
	else
		sprintf((char *) text, "%.0f", cursor);

	afm_text_extents(afm, fs, (char *) text,
			 strlen((char *) text), &extents);

	x -= extents.width / 2.0;
	y = tick + tick / 4.0;

	offset += afm_print_text(file, afm, fs, x, y,
				 (char *) text, strlen((char *) text));

	return offset;
}

static int
output_log_scale(FILE *file, afm_t *afm, double fs, double lw,
		 double width, double tick, double start, double end)
{
	unsigned char text[32];
	afm_extents_t extents;
	double factor, x, y, cursor, c2, step;
	int offset = 0;
	int i;

	factor = width / (log(end) - log(start));

	offset += fprintf(file, "0 g\n");
	offset += fprintf(file, "0 G\n");
	offset += fprintf(file, "%.3f w\n", lw);

	x = 0.0;
	y = 0.0;

	offset += fprintf(file, "%.3f %.3f m\n", x, y);

	x = factor * (log(end) - log(start));
	y = 0.0;

	offset += fprintf(file, "%.3f %.3f l\n", x, y);
	offset += fprintf(file, "S\n");

	offset += fprintf(file, "%.3f w\n", lw / 2.0);

	sprintf((char *) text, _("Nautical Miles"));
	afm_text_extents(afm, 1.5 * fs, (char *) text,
			 strlen((char *) text), &extents);
	x = -extents.bbox.llx;
	y = -extents.ascent - tick / 4.0;
	offset += afm_print_text(file, afm, 1.5 * fs, x, y,
				 (char *) text, strlen((char *) text));

	x = extents.width + extents.ascent / 2.0;
	y += extents.ascent / 2.0;

	offset += fprintf(file, "%.3f %.3f m\n",
			  x + extents.ascent / 4.0,
			  y + extents.ascent / 4.0);
	offset += fprintf(file, "%.3f %.3f l\n", x, y);
	offset += fprintf(file, "%.3f %.3f l\n",
			  x + extents.ascent / 4.0,
			  y - extents.ascent / 4.0);
	offset += fprintf(file, "h f\n");

	x += extents.ascent / 4.0;
	offset += fprintf(file, "%.3f %.3f m\n", x, y);
	x += extents.ascent;
	offset += fprintf(file, "%.3f %.3f l\n", x, y);
	offset += fprintf(file, "S\n");

	x += extents.ascent / 4.0;
	offset += fprintf(file, "%.3f %.3f m\n",
			  x - extents.ascent / 4.0,
			  y + extents.ascent / 4.0);
	offset += fprintf(file, "%.3f %.3f l\n", x, y);
	offset += fprintf(file, "%.3f %.3f l\n",
			  x - extents.ascent / 4.0,
			  y - extents.ascent / 4.0);
	offset += fprintf(file, "h f\n");

	x += extents.ascent / 2.0;
	y = -extents.ascent - tick / 4.0;

	sprintf((char *) text, _("Minutes"));
	offset += afm_print_text(file, afm, 1.5 * fs, x, y,
				 (char *) text, strlen((char *) text));


	cursor = start;
	while (cursor < end) {
		step = pow(10, floor(log10(cursor + EPSILON)));

		x = factor * (log(cursor) - log(start));
		y = 0.0;

		offset += fprintf(file, "%.3f %.3f m\n", x, y);
		y = tick;
		offset += fprintf(file, "%.3f %.3f l\n", x, y);
		offset += fprintf(file, "S\n");


		if (cursor < 10)
			sprintf((char *) text, "%.1f", cursor);
		else
			sprintf((char *) text, "%.0f", cursor);

		afm_text_extents(afm, fs, (char *) text,
				 strlen((char *) text), &extents);

		x -= extents.width / 2.0;
		y = tick + tick / 4.0;

		offset += afm_print_text(file, afm, fs, x, y,
					 (char *) text, strlen((char *) text));

		if (factor * (log(cursor + step) - log(cursor)) < 6.0) {
			for (i = 1; i < 5; i++) {
				c2 = cursor + (double)i * step / 5.0;
				x = factor * (log(c2) - log(start));
				y = 0.0;

				offset += fprintf(file, "%.3f %.3f m\n", x, y);
				y += tick / 2.0;
				offset += fprintf(file, "%.3f %.3f l\n", x, y);
				offset += fprintf(file, "S\n");
			}
		} else {
			for (i = 1; i < 10; i++) {
				c2 = cursor + (double)i * step / 10.0;
				x = factor * (log(c2) - log(start));
				y = 0.0;

				offset += fprintf(file, "%.3f %.3f m\n", x, y);
				if (i == 5)
					y += 2.0 * tick / 3.0;
				else
					y += tick / 2.0;
				offset += fprintf(file, "%.3f %.3f l\n", x, y);
				offset += fprintf(file, "S\n");
			}
		}

		cursor += step;
	}

	cursor = end;

	x = factor * (log(cursor) - log(start));
	y = 0.0;

	offset += fprintf(file, "%.3f %.3f m\n", x, y);
	y = tick;
	offset += fprintf(file, "%.3f %.3f l\n", x, y);
	offset += fprintf(file, "S\n");

	if (cursor < 10)
		sprintf((char *) text, "%.1f", cursor);
	else
		sprintf((char *) text, "%.0f", cursor);

	afm_text_extents(afm, fs, (char *) text,
			 strlen((char *) text), &extents);

	x -= extents.width / 2.0;
	y = tick + tick / 4.0;

	offset += afm_print_text(file, afm, fs, x, y,
				 (char *) text, strlen((char *) text));

	return offset;
}

static int
table_get_orient(radar_t *radar, unsigned int column,
		 unsigned char *buffer, size_t buflen)
{
	if ((radar->own_course == 0) && (radar->own_speed == 0.0))
		goto none;
	
	return sprintf((char *) buffer, "%s", radar->north_up ?
		       _("North Up") : _("Head Up"));

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_range(radar_t *radar, unsigned int column,
		unsigned char *buffer, size_t buflen)
{
	if (radar_ranges[radar->rindex].digits)
		return sprintf((char *) buffer, _("%.*f nm"),
			       radar_ranges[radar->rindex].digits,
			       radar->range);
	else
		return sprintf((char *) buffer, _("%.1f nm"), radar->range);
}

static int
table_get_KA(radar_t *radar, unsigned int column,
	     unsigned char *buffer, size_t buflen)
{
	if ((radar->own_course == 0) && (radar->own_speed == 0.0))
		goto none;
	
	return sprintf((char *) buffer, _("%03u\260"), radar->own_course);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_vA(radar_t *radar, unsigned int column,
	     unsigned char *buffer, size_t buflen)
{
	if ((radar->own_course == 0) && (radar->own_speed == 0.0))
		goto none;
	
	return sprintf((char *) buffer, _("%.1f kn"), radar->own_speed);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_opponent(radar_t *radar, unsigned int column,
		   unsigned char *buffer, size_t buflen)
{
	return sprintf((char *) buffer, "%c", 'B' + column);
}

static int
table_get_target0_time(radar_t *radar, unsigned int column,
		      unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->distance[0] == 0.0)
		goto none;

	return sprintf((char *) buffer, "%02u:%02u", s->time[0] / 60, s->time[0] % 60);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target0_rasp(radar_t *radar, unsigned int column,
		      unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->distance[0] == 0.0)
		goto none;

	return sprintf((char *) buffer, _("%03u\260"), s->rasp[0]);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target0_rwk(radar_t *radar, unsigned int column,
		     unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->distance[0] == 0.0)
		goto none;

	return sprintf((char *) buffer, _("%03u\260"),
		(360 + radar->own_course - s->rasp_course_offset[0]) % 360);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target0_rakrp(radar_t *radar, unsigned int column,
		       unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->distance[0] == 0.0)
		goto none;

	return sprintf((char *) buffer, _("%03u\260"), s->rakrp[0]);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target0_distance(radar_t *radar, unsigned int column,
			  unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->distance[0] == 0.0)
		goto none;

	return sprintf((char *) buffer, _("%.1f nm"), s->distance[0]);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target1_time(radar_t *radar, unsigned int column,
		      unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->distance[1] == 0.0)
		goto none;

	return sprintf((char *) buffer, "%02u:%02u", s->time[1] / 60, s->time[1] % 60);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target1_rasp(radar_t *radar, unsigned int column,
		      unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->distance[1] == 0.0)
		goto none;

	return sprintf((char *) buffer, _("%03u\260"), s->rasp[1]);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target1_rwk(radar_t *radar, unsigned int column,
		     unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->distance[1] == 0.0)
		goto none;

	return sprintf((char *) buffer, _("%03u\260"),
		(360 + radar->own_course - s->rasp_course_offset[1]) % 360);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target1_rakrp(radar_t *radar, unsigned int column,
		       unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->distance[1] == 0.0)
		goto none;

	return sprintf((char *) buffer, _("%03u\260"), s->rakrp[1]);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target1_distance(radar_t *radar, unsigned int column,
			  unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->distance[1] == 0.0)
		goto none;

	return sprintf((char *) buffer, _("%.1f nm"), s->distance[1]);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_interval(radar_t *radar, unsigned int column,
			 unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->delta_time == 0)
		goto none;

	return sprintf((char *) buffer, _("%u min"), s->delta_time);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_KBr(radar_t *radar, unsigned int column,
		    unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->delta_time == 0)
		goto none;

	if (fabs(s->vBr) < EPSILON)
		goto none;

	return sprintf((char *) buffer, _("%05.1f\260"), s->KBr);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_vBr(radar_t *radar, unsigned int column,
		    unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->delta_time == 0)
		goto none;

	return sprintf((char *) buffer, _("%.1f kn"), s->vBr);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_KB(radar_t *radar, unsigned int column,
		   unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->delta_time == 0)
		goto none;

	if (fabs(s->vB) < EPSILON)
		goto none;

	return sprintf((char *) buffer, _("%05.1f\260"), s->KB);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_vB(radar_t *radar, unsigned int column,
		    unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->delta_time == 0)
		goto none;

	return sprintf((char *) buffer, _("%.1f kn"), s->vB);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_aspect(radar_t *radar, unsigned int column,
			unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->delta_time == 0)
		goto none;

	return sprintf((char *) buffer, _("%05.1f\260"), s->aspect);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_CPA(radar_t *radar, unsigned int column,
		    unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_cpa == FALSE)
		goto none;

	return sprintf((char *) buffer, _("%.1f nm"), s->CPA);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_PCPA(radar_t *radar, unsigned int column,
		     unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_cpa == FALSE)
		goto none;

	if (s->PCPA < 0.0)
		return sprintf((char *) buffer, "-");

	return sprintf((char *) buffer, _("%05.1f\260"), s->PCPA);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_SPCPA(radar_t *radar, unsigned int column,
		      unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_cpa == FALSE)
		goto none;

	if (s->SPCPA < 0.0)
		return sprintf((char *) buffer, "-");

	return sprintf((char *) buffer, _("%05.1f\260"), s->SPCPA);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_TCPA(radar_t *radar, unsigned int column,
		     unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_cpa == FALSE)
		goto none;

	if (fabs(s->vBr) < EPSILON)
		goto none;

	return sprintf((char *) buffer, _("%.1f min"), s->TCPA);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_tCPA(radar_t *radar, unsigned int column,
		     unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_cpa == FALSE)
		goto none;

	if (fabs(s->vBr) < EPSILON)
		goto none;

	return sprintf((char *) buffer, "%02u:%02u", s->tCPA / 60, s->tCPA % 60);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_BCR(radar_t *radar, unsigned int column,
		    unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_crossing == FALSE)
		goto none;

	return sprintf((char *) buffer, _("%.1f nm"), s->BCR);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_BCT(radar_t *radar, unsigned int column,
		     unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_crossing == FALSE)
		goto none;

	return sprintf((char *) buffer, _("%.1f min"), s->BCT);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_target_BCt(radar_t *radar, unsigned int column,
		     unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_crossing == FALSE)
		goto none;

	return sprintf((char *) buffer, "%02u:%02u", s->BCt / 60, s->BCt % 60);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_maneuver_time(radar_t *radar, unsigned int column,
			unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->index != radar->mtarget)
		goto none;

	if (s->have_mpoint == FALSE)
		goto none;

	if (radar->mtime_selected) {
		return sprintf((char *) buffer, "%c %02u:%02u", pdf_char_bullet->index,
			       radar->mtime / 60, radar->mtime % 60);
	} else {
		return sprintf((char *) buffer, "%02u:%02u",
			       radar->mtime / 60, radar->mtime % 60);
	}

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_maneuver_distance(radar_t *radar, unsigned int column,
			    unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_mpoint == FALSE)
		goto none;

	if (s->index == radar->mtarget) {
		if (radar->mtime_selected) {
			return sprintf((char *) buffer, _("%.1f nm"),
				       radar->mdistance);
		} else {
			return sprintf((char *) buffer, _("%c %.1f nm"),
				       pdf_char_bullet->index,
				       radar->mdistance);
		}
	} else {
		return sprintf((char *) buffer, _("%.1f nm"), s->mdistance);
	}

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_maneuver_bearing(radar_t *radar, unsigned int column,
			   unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_mpoint == FALSE)
		goto none;

	return sprintf((char *) buffer, _("%05.1f\260"), s->mbearing);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_maneuver_type(radar_t *radar, unsigned int column,
			unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->index != radar->mtarget)
		goto none;

	if (s->have_mpoint == FALSE)
		goto none;

	return sprintf((char *) buffer, "%s",
		       radar->mcourse_change ?  _("Course") : _("Speed"));

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_maneuver_CPA(radar_t *radar, unsigned int column,
		       unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->index != radar->mtarget)
		goto none;

	switch (radar->maneuver) {
	case MANEUVER_NONE:
	default:
		goto none;

	case MANEUVER_COURSE_FROM_CPA:
	case MANEUVER_SPEED_FROM_CPA:
		return sprintf((char *) buffer, _("%c %.1f nm"),
			       pdf_char_bullet->index, radar->mcpa);

	case MANEUVER_CPA_FROM_SPEED:
	case MANEUVER_CPA_FROM_COURSE:
		if (s->have_problems)
			goto none;

		return sprintf((char *) buffer, _("%.1f nm"), radar->mcpa);
	}

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_KA(radar_t *radar, unsigned int column,
		 unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->index != radar->mtarget)
		goto none;

	switch (radar->maneuver) {
	case MANEUVER_NONE:
	default:
		goto none;

	case MANEUVER_COURSE_FROM_CPA:
		if (s->have_problems)
			return sprintf((char *) buffer, _("%05.1f\260 (!)"),
				       radar->ncourse);

		return sprintf((char *) buffer, _("%05.1f\260"), radar->ncourse);

	case MANEUVER_SPEED_FROM_CPA:
		goto none;

	case MANEUVER_CPA_FROM_SPEED:
		return sprintf((char *) buffer, _("%05.1f\260"), radar->ncourse);

	case MANEUVER_CPA_FROM_COURSE:
		return sprintf((char *) buffer, _("%c %05.1f\260"),
			       pdf_char_bullet->index, radar->ncourse);
	}

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_vA(radar_t *radar, unsigned int column,
		 unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->index != radar->mtarget)
		goto none;

	switch (radar->maneuver) {
	case MANEUVER_NONE:
	default:
		goto none;

	case MANEUVER_COURSE_FROM_CPA:
		goto none;

	case MANEUVER_SPEED_FROM_CPA:
		if (s->have_problems)
			return sprintf((char *) buffer, _("%.1f kn (!)"),
				       radar->nspeed);

		return sprintf((char *) buffer, _("%.1f kn"), radar->nspeed);

	case MANEUVER_CPA_FROM_SPEED:
		return sprintf((char *) buffer, _("%c %.1f kn"),
			       pdf_char_bullet->index, radar->nspeed);

	case MANEUVER_CPA_FROM_COURSE:
		return sprintf((char *) buffer, _("%.1f kn"), radar->nspeed);
	}

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_KBr(radar_t *radar, unsigned int column,
		  unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_new_cpa == FALSE)
		goto none;

	if (fabs(s->new_vBr) < EPSILON)
		goto none;

	return sprintf((char *) buffer, _("%05.1f\260"), s->new_KBr);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_vBr(radar_t *radar, unsigned int column,
		  unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_new_cpa == FALSE)
		goto none;

	return sprintf((char *) buffer, _("%.1f kn"), s->new_vBr);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_delta(radar_t *radar, unsigned int column,
		unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_new_cpa == FALSE)
		goto none;

	return sprintf((char *) buffer, _("%.1f\260"), s->delta);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_RaSP(radar_t *radar, unsigned int column,
		   unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_new_cpa == FALSE)
		goto none;

	return sprintf((char *) buffer, _("%.1f\260"), s->new_RaSP);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_aspect(radar_t *radar, unsigned int column,
		     unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_new_cpa == FALSE)
		goto none;

	return sprintf((char *) buffer, _("%.1f\260"), s->new_aspect);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_CPA(radar_t *radar, unsigned int column,
		  unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_new_cpa == FALSE)
		goto none;

	switch (radar->maneuver) {
	case MANEUVER_COURSE_FROM_CPA:
	case MANEUVER_SPEED_FROM_CPA:
		if (s->have_problems)
			return sprintf((char *) buffer, _("%.1f nm (!)"),
				       s->new_CPA);
		break;
	default:
		break;
	}

	return sprintf((char *) buffer, _("%.1f nm"), s->new_CPA);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_PCPA(radar_t *radar, unsigned int column,
		   unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_new_cpa == FALSE)
		goto none;

	if (s->new_PCPA < 0.0)
		return sprintf((char *) buffer, "-");

	return sprintf((char *) buffer, _("%05.1f\260"), s->new_PCPA);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_SPCPA(radar_t *radar, unsigned int column,
		    unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_new_cpa == FALSE)
		goto none;

	if (s->new_SPCPA < 0.0)
		return sprintf((char *) buffer, "-");

	return sprintf((char *) buffer, _("%05.1f\260"), s->new_SPCPA);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_TCPA(radar_t *radar, unsigned int column,
		   unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_new_cpa == FALSE)
		goto none;

	if (fabs(s->new_vBr) < EPSILON)
		goto none;

	return sprintf((char *) buffer, _("%.1f min"), s->new_TCPA);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_tCPA(radar_t *radar, unsigned int column,
		   unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->have_new_cpa == FALSE)
		goto none;

	if (fabs(s->new_vBr) < EPSILON)
		goto none;

	return sprintf((char *) buffer, "%02u:%02u", s->new_tCPA / 60, s->new_tCPA % 60);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_BCR(radar_t *radar, unsigned int column,
		  unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->new_have_crossing == FALSE)
		goto none;

	return sprintf((char *) buffer, _("%.1f nm"), s->new_BCR);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_BCT(radar_t *radar, unsigned int column,
		   unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->new_have_crossing == FALSE)
		goto none;

	return sprintf((char *) buffer, _("%.1f min"), s->new_BCT);

none:
	buffer[0] = '\0';
	return 0;
}

static int
table_get_new_BCt(radar_t *radar, unsigned int column,
		   unsigned char *buffer, size_t buflen)
{
	target_t *s;

	if (column >= RADAR_NR_TARGETS)
		goto none;

	s = &radar->target[column];

	if (s->new_have_crossing == FALSE)
		goto none;

	return sprintf((char *) buffer, "%02u:%02u", s->new_BCt / 60, s->new_BCt % 60);

none:
	buffer[0] = '\0';
	return 0;
}


static unsigned char *
first_non_digit(unsigned char *text)
{
	while (('\0' != *text) && isdigit(*text))
		text++;

	if ('\0' == *text)
		return NULL;

	return text;
}

static int
output_table(radar_t *radar, FILE *file, afm_t *afm, double fs, double lw,
	     double width, double height, pdf_rect_t *annot_rect)
{
	const table_descriptor_t *dp;
	unsigned char text[64], unit[32], *p;
	const char *row_text;
	afm_extents_t extents;
	double rh, x, y, w;
	double c1w, c2w, asc, desc, th;
	double predec[TABLE_MAX_COLUMNS];
	double postdec[TABLE_MAX_COLUMNS];
	double prewidth, aoffset;
	unsigned int sep, drow, row, col;
	unsigned int table_columns;
	target_t *target;
	int offset = 0;
	int len, prelen;
	int i;


	table_columns = 0;
	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		target = &radar->target[i];

		if ((target->distance[0] == 0.0) &&
		    (target->distance[1] == 0.0))
			continue;

		table_columns++;
	}
	if (0 == table_columns)
		table_columns = TABLE_EMPTY_COLUMNS;
	if (table_columns > TABLE_MAX_COLUMNS)
		table_columns = TABLE_MAX_COLUMNS;


	offset += fprintf(file, "0 g\n");
	offset += fprintf(file, "0 G\n");
	offset += fprintf(file, "%.3f w\n", lw);

	sprintf((char *) text, "Radarplot");
	afm_text_extents(afm, 3.0 * fs, (char *) text,
			 strlen((char *) text), &extents);
	offset += afm_print_text(file, afm, 3.0 * fs,
				 -extents.bbox.llx, height - extents.ascent,
				 (char *) text, strlen((char *) text));

	sprintf((char *) text, "Copyright © 2005 Christian Dost");
	afm_text_extents(afm, 0.75 * fs, (char *) text,
			 strlen((char *) text), &extents);
	offset += afm_print_text(file, afm, 0.75 * fs,
				 width - extents.bbox.urx,
				 height - extents.ascent,
				 (char *) text, strlen((char *) text));

	sprintf((char *) text, "ecd@brainaid.de");
	afm_text_extents(afm, 0.75 * fs, (char *) text,
			 strlen((char *) text), &extents);
	x = width - extents.bbox.urx;
	y = height - 1.25 * 0.75 * fs - extents.ascent;
	offset += afm_print_text(file, afm, 0.75 * fs,
				 x, y, (char *) text, strlen((char *) text));
	annot_rect[0].llx = x;
	annot_rect[0].lly = y + extents.descent;
	annot_rect[0].urx = x + extents.width;
	annot_rect[0].ury = y + extents.ascent;

	sprintf((char *) text, "http://brainaid.de/people/ecd/radarplot");
	afm_text_extents(afm, 0.75 * fs, (char *) text,
			 strlen((char *) text), &extents);
	x = width - extents.bbox.urx;
	y = height - 2.5 * 0.75 * fs - extents.ascent;
	offset += afm_print_text(file, afm, 0.75 * fs,
				 x, y, (char *) text, strlen((char *) text));
	annot_rect[1].llx = x;
	annot_rect[1].lly = y + extents.descent;
	annot_rect[1].urx = x + extents.width;
	annot_rect[1].ury = y + extents.ascent;

	height -= 1.25 * 3.0 * fs;

	sep = drow = 0;
	for (col = 0; col < table_columns; col++)
		predec[col] = postdec[col] = 0.0;

	c1w = c2w = asc = desc = 0.0;
	for (row = 0; row < NR_DATA_TABLE_ROWS; row++) {
		dp = &data_table[row];

		if ((NULL == dp->row) && (NULL == dp->unit))
			sep++;
		else
			drow++;

		if (dp->row) {
			row_text = (dp->row[0] == '\0') ? "" : _(dp->row);

			afm_text_extents(afm, fs, (char *) row_text,
					 strlen((char *) row_text), &extents);

			if (dp->columns > 0) {
				if (extents.width > c1w)
					c1w = extents.width;
			}
			if (extents.ascent > asc)
				asc = extents.ascent;
			if (extents.descent < desc)
				desc = extents.descent;
		}

		if (dp->unit) {
			snprintf((char *) unit, sizeof(unit), "[%s]",
				 (dp->unit[0] == '\0') ? "" : _(dp->unit));

			afm_text_extents(afm, fs, (char *) unit,
					 strlen((char *) unit), &extents);

			if (dp->columns > 0) {
				if (extents.width > c2w)
					c2w = extents.width;
			}
			if (extents.ascent > asc)
				asc = extents.ascent;
			if (extents.descent < desc)
				desc = extents.descent;
		}

		for (col = 0; col < dp->columns; col++) {

			if (col >= table_columns)
				continue;

			if (dp->get_text) {
				len = dp->get_text(radar, col,
						   text, sizeof(text));

				if (dp->alignment & TABLE_ALIGN_DECIMAL) {
					p = (unsigned char *) strchr((char *) text, '.');
					if (NULL == p)
						p = first_non_digit(text);
					if (NULL == p) {
						afm_text_extents(afm, fs, (char *) text, len, &extents);
						if (extents.width > predec[col])
							predec[col] = extents.width;
					} else {
						prelen = p - text;
						afm_text_extents(afm, fs, (char *) text, prelen, &extents);
						if (extents.width > predec[col])
							predec[col] = extents.width;
						afm_text_extents(afm, fs, (char *) p, len - prelen, &extents);
						if (extents.width > postdec[col])
							postdec[col] = extents.width;
					}
				}
			}
		}
	}

	rh = height / ((double) drow + ((double) sep / 2.0));

	c1w += fs;
	c2w += fs;

	th = (rh - asc) / 2.0;

	y = height;

	for (row = 0; row < NR_DATA_TABLE_ROWS; row++) {
		dp = &data_table[row];

		if ((NULL == dp->row) && (NULL == dp->unit)) {
			y -= rh / 2.0;
			continue;
		}

		row_text = (dp->row[0] == '\0') ? "" : _(dp->row);

		y -= rh;
		x = 0.0;

		if (dp->columns == 0) {
			afm_text_extents(afm, fs, (char *) row_text,
					 strlen((char *) row_text), &extents);

			offset += afm_print_text(file, afm, fs,
						 x - extents.bbox.llx, y + th,
						 (char *) row_text,
						 strlen((char *) row_text));
			continue;
		}

		offset += fprintf(file, "%.3f %.3f m\n", x, y);
		offset += fprintf(file, "%.3f %.3f l\n", x + width, y);
		offset += fprintf(file, "%.3f %.3f l\n", x + width, y + rh);
		offset += fprintf(file, "%.3f %.3f l\n", x, y + rh);
		offset += fprintf(file, "s\n");

		offset += afm_print_text(file, afm, fs,
					 x + fs / 2.0, y + th,
					 (char *) row_text,
					 strlen((char *) row_text));

		x = c1w;

		offset += fprintf(file, "%.3f %.3f m\n", x, y);
		offset += fprintf(file, "%.3f %.3f l\n", x, y + rh);
		offset += fprintf(file, "S\n");

		if (dp->unit) {
			snprintf((char *) unit, sizeof(unit), "[%s]",
				 (dp->unit[0] == '\0') ? "" : _(dp->unit));

			offset += afm_print_text(file, afm, fs,
						 x + fs / 2.0, y + th,
						 (char *) unit,
						 strlen((char *) unit));
		}

		x += c2w;
		w = (width - x) / (double) table_columns;

		for (col = 0; col < dp->columns; col++) {

			if (col >= table_columns)
				continue;

			offset += fprintf(file, "%.3f %.3f m\n", x, y);
			offset += fprintf(file, "%.3f %.3f l\n", x, y + rh);
			offset += fprintf(file, "S\n");

			if (dp->get_text) {
				len = dp->get_text(radar, col,
						   text, sizeof(text));

				aoffset = 0.0;

				if (dp->alignment & TABLE_ALIGN_DECIMAL) {
					p = (unsigned char *) strchr((char *) text, '.');
					if (NULL == p)
						p = first_non_digit(text);
					if (NULL == p)
						prelen = len;
					else
						prelen = p - text;

					afm_text_extents(afm, fs, (char *) text, prelen, &extents);
					prewidth = extents.width;

					switch (dp->alignment & TABLE_ALIGN_MASK) {
					case TABLE_ALIGN_LEFT:
						aoffset = predec[col] - prewidth;
						break;
					case TABLE_ALIGN_CENTER:
						aoffset = ((w - fs) - (predec[col] + postdec[col])) / 2.0 + predec[col] - prewidth;
						break;
					case TABLE_ALIGN_RIGHT:
						aoffset = (w - fs) - postdec[col] - prewidth;
						break;
					}
				} else {
					afm_text_extents(afm, fs, (char *) text, len, &extents);

					switch (dp->alignment & TABLE_ALIGN_MASK) {
					case TABLE_ALIGN_LEFT:
						aoffset = 0.0;
						break;
					case TABLE_ALIGN_CENTER:
						aoffset = ((w - fs) - extents.width) / 2.0;
						break;
					case TABLE_ALIGN_RIGHT:
						aoffset = (w - fs) - extents.width;
						break;
					}
				}

				offset += afm_print_text(file, afm, fs,
							 x + fs / 2.0 + aoffset,
							 y + th,
							 (char *) text, len);
			}

			x += w;
		}
	}

	return offset;
}

#ifdef DEBUG_LABEL_ALIGN

static int
pdf_output_align(FILE *file, double x, double y)
{
	int offset = 0;

	offset += fprintf(file, "%.3f w\n", 0.0);

	offset += fprintf(file, "%.3f %.3f m\n",
			  x - 20.0, y);
	offset += fprintf(file, "%.3f %.3f l\n",
			  x + 20.0, y);
	offset += fprintf(file, "S\n");

	offset += fprintf(file, "%.3f %.3f m\n",
			  x, y - 10.0);
	offset += fprintf(file, "%.3f %.3f l\n",
			  x, y + 10.0);
	offset += fprintf(file, "S\n");

	return offset;
}

static int
pdf_output_bbox(FILE *file, double x, double y, afm_extents_t *extents)
{
	int offset = 0;

	offset += fprintf(file, "%.3f w\n", 0.0);

	offset += fprintf(file, "%.3f %.3f m\n",
			  x + extents->bbox.llx,
			  y + extents->bbox.lly);
	offset += fprintf(file, "%.3f %.3f l\n",
			  x + extents->bbox.urx,
			  y + extents->bbox.lly);
	offset += fprintf(file, "%.3f %.3f l\n",
			  x + extents->bbox.urx,
			  y + extents->bbox.ury);
	offset += fprintf(file, "%.3f %.3f l\n",
			  x + extents->bbox.llx,
			  y + extents->bbox.ury);
	offset += fprintf(file, "%.3f %.3f l\n",
			  x + extents->bbox.llx,
			  y + extents->bbox.lly);
	offset += fprintf(file, "S\n");

	offset += fprintf(file, "%.3f %.3f m\n",
			  x,
			  y);
	offset += fprintf(file, "%.3f %.3f l\n",
			  x + extents->width,
			  y);
	offset += fprintf(file, "S\n");

	return offset;
}

#endif /* DEBUG_LABEL_ALIGN */

static void
pdf_label_align(double xalign, double yalign, afm_extents_t *extents,
		double *xoffp, double *yoffp)
{
	double xoff = *xoffp;
	double yoff = *yoffp;

	xoff -= ((0.5 - xalign) / 5.0 + (1.0 - xalign)) *
		(extents->bbox.urx - extents->bbox.llx);
	xoff -= extents->bbox.llx;
	yoff += ((yalign - 0.5) / 2.5 + yalign) *
		(extents->bbox.ury - extents->bbox.lly);
	yoff += extents->bbox.lly;

	*xoffp = xoff;
	*yoffp = yoff;
}

int
radar_print_as_PDF(radar_t *radar, const char *filename,
		   const char *paper, int color)
{
	unsigned int catalog, outlines, pages, page, content, stream, length;
	unsigned int procset, font, encoding, info, uri[2], annot[2], annots;
	unsigned int xref, offset;
	unsigned int paper_width = 0, paper_height = 0;
	static const char *source_date = "$Date: 2009-07-24 11:37:17 $";
	pdf_rect_t annot_rect[2];
	char *fontname, font_afm_file[1024];
	double step, fs, x, y, w, h, r, lw, xoff, yoff;
	double nw, nh, sw, xoffset, yoffset;
	unsigned char text[32];
	const char *p;
	afm_extents_t extents;
	GdkGCValues values;
	target_t *target;
	vector_t *vect;
	arc_t *arc;
	poly_t *poly;
	text_label_t *label;
	unsigned int c;
	struct tm *tm0, *tm1;
	time_t t, tz;
	FILE *file;
	afm_t *afm;
	int i, j, k, a;

	fontname = "Helvetica";

	for (i = 0; i < NR_PAPER_FORMATS; i++) {
		if (!strcmp(paper, paper_formats[i].name)) {
			paper_width = paper_formats[i].pt_width;
			paper_height = paper_formats[i].pt_height;
			break;
		}
	}
	if ((paper_width == 0) || (paper_height == 0)) {
		fprintf(stderr, "%s:%u: unknown paper format '%s'\n",
			__FUNCTION__, __LINE__, paper);
		return -1;
	}

	w = 25.4 * (double) paper_width / 72.0;
	h = 25.4 * (double) paper_height / 72.0;
	fs = h / 90.0;
	lw = 0.25;

	step = floor(((h - 20.0) - 8.0 * fs - 8.0) / 12.0 + 0.5);

#ifdef __WIN32__
{
	char *path = g_build_filename(progpath, fontname, NULL);
	sprintf(font_afm_file, "%s.afm", path);
	g_free(path);
}
#else /* __WIN32__ */
	sprintf(font_afm_file, "%s/share/radarplot/%s.afm", PREFIX, fontname);
#endif /* __WIN32__ */
	if (afm_read_file(font_afm_file, &afm) < 0) {
		fprintf(stderr, "%s:%u: can't read '%s': %s\n",
			__FUNCTION__, __LINE__,
			font_afm_file, strerror(errno));
		return -1;
	}
	afm->pdf_name = "Fn";

	pdf_char_bullet = afm_lookup_char_by_name(afm, "bullet", 6);
	if (NULL == pdf_char_bullet) {
		fprintf(stderr, "%s:%u: can't find char 'bullet'\n",
			__FUNCTION__, __LINE__);
		return -1;
	}

	c = 128;
	while (afm->char_table[c].ch)
		c++;

	pdf_char_bullet->index = c;
	afm->char_table[c].ch = pdf_char_bullet;

	afm_text_extents(afm, fs, "270", 3, &extents);
	nh = extents.bbox.ury - extents.bbox.lly;
	nw = extents.bbox.urx - extents.bbox.llx;


	file = fopen(filename, "wb");
	if (NULL == file) {
		fprintf(stderr, "%s:%u: open '%s': %s\n",
			__FUNCTION__, __LINE__, filename, strerror(errno));
		afm_free(afm);
		return -1;
	}

	offset = 0;
	offset += fprintf(file, "%%PDF-1.3\n");

	catalog = offset;
	offset += fprintf(file, "1 0 obj\n");
	offset += fprintf(file, "   << /Type /Catalog\n");
	offset += fprintf(file, "      /Outlines 2 0 R\n");
	offset += fprintf(file, "      /Pages 3 0 R\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	outlines = offset;
	offset += fprintf(file, "2 0 obj\n");
	offset += fprintf(file, "   << /Type /Outlines\n");
	offset += fprintf(file, "      /Count 0\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	pages = offset;
	offset += fprintf(file, "3 0 obj\n");
	offset += fprintf(file, "   << /Type /Pages\n");
	offset += fprintf(file, "      /Kids [4 0 R]\n");
	offset += fprintf(file, "      /Count 1\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	page = offset;
	offset += fprintf(file, "4 0 obj\n");
	offset += fprintf(file, "   << /Type /Page\n");
	offset += fprintf(file, "      /MediaBox [ 0 0 %u %u ]\n",
			  paper_width, paper_height);
	offset += fprintf(file, "      /CropBox [ 0 0 %u %u ]\n",
			  paper_width, paper_height);
	offset += fprintf(file, "      /Rotate   0\n");
	offset += fprintf(file, "      /Parent 3 0 R\n");
	offset += fprintf(file, "      /Contents 5 0 R\n");
	offset += fprintf(file, "      /Annots 15 0 R\n");
	offset += fprintf(file, "      /Resources\n");
	offset += fprintf(file, "         << /ProcSet 7 0 R\n");
	offset += fprintf(file, "            /Font\n");
	offset += fprintf(file, "               << /%s 8 0 R >>\n",
			  afm->pdf_name);
	offset += fprintf(file, "         >>\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	content = offset;
	offset += fprintf(file, "5 0 obj\n");
	offset += fprintf(file, "   << /Length 6 0 R >>\n");
	offset += fprintf(file, "stream\n");


	stream = 0;
	stream += fprintf(file, "q\n");
	stream += fprintf(file, "%.8f 0 0 %.8f 0 0 cm\n",
			  72.0 / 25.4, 72.0 / 25.4);

	xoffset = 6.0 * step + nw + nh / 2.0 + 10.0;
	yoffset = h - (6.0 * step + nh + nh / 2.0 + 10.0);

	stream += fprintf(file, "1 0 0 1 %.3f %.3f cm\n", xoffset, yoffset);

	stream += fprintf(file, "%.3f w\n", lw / 2.0);	/* lineWidth */
	stream += fprintf(file, "1 J\n");		/* lineCap: Round */
	stream += fprintf(file, "1 j\n");		/* lineJoin: Round */


	if (NULL == radar->license) {
		stream += fprintf(file, "q\n");

		sprintf((char *) text, _("Radarplot %u.%u.%u (unregistered)"),
			RADAR_MAJOR, RADAR_MINOR, RADAR_PATCHLEVEL);

		/* calculate extents, calculate scale from that. */
		afm_text_extents(afm, 7.0 * fs, (char *) text,
				 strlen((char *) text), &extents);

		/* rotate */
		stream += fprintf(file, "%.8f %.8f %.8f %.8f 0 0 cm\n",
				  cos(M_PI/4.0), sin(M_PI/4.0),
				  -sin(M_PI/4.0), cos(M_PI/4.0));

		/* translate */
		x = -extents.width / 2.0;
		y = -extents.ascent / 2.0;

		stream += fprintf(file, "1 0 0 1 %.3f %.3f cm\n", x, y);

		/* 25% grey */
		stream += fprintf(file, "%.3f g\n", 0.75);

		stream += afm_print_text(file, afm, 7.0 * fs, 0.0, 0.0,
					 (char *) text, strlen((char *) text));

		stream += fprintf(file, "Q\n");
	}


	if (color)
		stream += fprintf(file, "%.3f G\n", 0.75);	/* 25% Gray */

	for (a = 0; a < 360; a += 10) {
		if (0 == (a % 30))
			continue;

		x = 3.0 * step * cos(M_PI * (double) a / 180.0) / 4.0;
		y = 3.0 * step * sin(M_PI * (double) a / 180.0) / 4.0;
		stream += fprintf(file, "%.3f %.3f m\n", x, y);

		x = 6.0 * step * cos(M_PI * (double) a / 180.0);
		y = 6.0 * step * sin(M_PI * (double) a / 180.0);
		stream += fprintf(file, "%.3f %.3f l\n", x, y);

		stream += fprintf(file, "S\n");
	}

	if (color)
		stream += fprintf(file, "%.3f G\n", 0.5);	/* 50% Gray */

	for (a = 0; a < 360; a += 5) {
		x = (6.0 - 1.0 / 6.0) * step * cos(M_PI * (double) a / 180.0);
		y = (6.0 - 1.0 / 6.0) * step * sin(M_PI * (double) a / 180.0);
		stream += fprintf(file, "%.3f %.3f m\n", x, y);

		x = 6.0 * step * cos(M_PI * (double) a / 180.0);
		y = 6.0 * step * sin(M_PI * (double) a / 180.0);
		stream += fprintf(file, "%.3f %.3f l\n", x, y);

		stream += fprintf(file, "S\n");
	}

	for (a = 0; a < 360; a++) {
		if (0 == (a % 5))
			continue;

		x = (6.0 - 1.0 / 12.0) * step * cos(M_PI * (double) a / 180.0);
		y = (6.0 - 1.0 / 12.0) * step * sin(M_PI * (double) a / 180.0);
		stream += fprintf(file, "%.3f %.3f m\n", x, y);

		x = 6.0 * step * cos(M_PI * (double) a / 180.0);
		y = 6.0 * step * sin(M_PI * (double) a / 180.0);
		stream += fprintf(file, "%.3f %.3f l\n", x, y);

		stream += fprintf(file, "S\n");
	}

	if (color)
		stream += fprintf(file, "%.3f G\n", 0.75);	/* 25% Gray */

	for (i = 0; i < 6; i++) {
		r = step * ((double) i + 0.5);

		x = r * cos(M_PI * (double) 0 / 180.0);
		y = r * sin(M_PI * (double) 0 / 180.0);
		stream += fprintf(file, "%.3f %.3f m\n", x, y);

		for (a = 1; a < 360; a++) {
			x = r * cos(M_PI * (double) a / 180.0);
			y = r * sin(M_PI * (double) a / 180.0);

			stream += fprintf(file, "%.3f %.3f l\n", x, y);
		}

		stream += fprintf(file, "s\n");
	}

	stream += fprintf(file, "%.3f w\n", lw);	/* lineWidth */
	if (color)
		stream += fprintf(file, "%.3f G\n", 0.5);	/* 50% Gray */

	for (a = 0; a < 360; a += 90) {
		x = 0.0;
		y = 0.0;
		stream += fprintf(file, "%.3f %.3f m\n", x, y);

		x = 6.0 * step * cos(M_PI * (double) a / 180.0);
		y = 6.0 * step * sin(M_PI * (double) a / 180.0);
		stream += fprintf(file, "%.3f %.3f l\n", x, y);

		stream += fprintf(file, "S\n");
	}

	for (a = 0; a < 360; a += 30) {
		if (0 == (a % 90))
			continue;

		x = 1.0 * step * cos(M_PI * (double) a / 180.0) / 4.0;
		y = 1.0 * step * sin(M_PI * (double) a / 180.0) / 4.0;
		stream += fprintf(file, "%.3f %.3f m\n", x, y);

		x = 6.0 * step * cos(M_PI * (double) a / 180.0);
		y = 6.0 * step * sin(M_PI * (double) a / 180.0);
		stream += fprintf(file, "%.3f %.3f l\n", x, y);

		stream += fprintf(file, "S\n");
	}

	for (i = 0; i < 6; i++) {
		r = step * (double) (i + 1);

		x = r * cos(M_PI * (double) 0 / 180.0);
		y = r * sin(M_PI * (double) 0 / 180.0);
		stream += fprintf(file, "%.3f %.3f m\n", x, y);

		for (a = 1; a < 360; a++) {
			x = r * cos(M_PI * (double) a / 180.0);
			y = r * sin(M_PI * (double) a / 180.0);

			stream += fprintf(file, "%.3f %.3f l\n", x, y);
		}

		stream += fprintf(file, "s\n");
	}

	if (color)
		stream += fprintf(file, "%.3f G\n", 0.0);	/* Black */



	for (a = 0; a < 360; a += 10) {
		x = (6.0 * step + (nw + nh) / 2.0) * sin(M_PI * (double) a / 180.0);
		y = (6.0 * step + nh) * cos(M_PI * (double) a / 180.0);

		sprintf((char *) text, "%03u", a);

		x -= nw / 2.0;
		y -= nh / 2.0;

		stream += afm_print_text(file, afm, fs, x, y, (char *) text, 3);
	}


	stream += fprintf(file, "0.5 w\n");

	for (a = 0; a < 360; a += 90) {

		for (i = 1; i < 6; i++) {
			if (radar_ranges[radar->rindex].marks == 3) {
				if (i % 2)
					continue;
			}

			if (radar->range > 3.0)
				sprintf((char *) text, "%u", (unsigned int)
					((double) i * radar->range) / 6);
			else
				sprintf((char *) text, "%.*f",
					radar_ranges[radar->rindex].digits,
					(double) i * radar->range / 6.0);

			r = step * (double) i;

			x = r * sin(M_PI * (double) a / 180.0);
			y = r * cos(M_PI * (double) a / 180.0);

			afm_text_extents(afm, fs, (char *) text,
					 strlen((char *) text), &extents);

			x -= extents.bbox.llx +
				(extents.bbox.urx - extents.bbox.llx) / 2.0;
			y -= extents.ascent / 2.0;

			stream += fprintf(file, "1 g\n");
			stream += fprintf(file, "1 G\n");
			stream += fprintf(file, "%.3f %.3f m\n",
					  x + extents.bbox.llx,
					  y + extents.bbox.lly);
			stream += fprintf(file, "%.3f %.3f l\n",
					  x + extents.bbox.urx,
					  y + extents.bbox.lly);
			stream += fprintf(file, "%.3f %.3f l\n",
					  x + extents.bbox.urx,
					  y + extents.bbox.ury);
			stream += fprintf(file, "%.3f %.3f l\n",
					  x + extents.bbox.llx,
					  y + extents.bbox.ury);
			stream += fprintf(file, "b\n");
			stream += fprintf(file, "0 g\n");
			stream += fprintf(file, "0 G\n");
			stream += afm_print_text(file, afm, fs, x, y,
						 (char *) text,
						 strlen((char *) text));
		}
	}

	for (i = 0; i < RADAR_NR_VECTORS; i++) {
		vect = &radar->vectors[i];

		if (!vect->is_visible)
			continue;

		if (color)
			stream += output_gc_rgb_color(file, vect->gc);

		gdk_gc_get_values(vect->gc, &values);

		if (values.line_width == 0)
			stream += fprintf(file, "%.3f w\n", lw);
		else
			stream += fprintf(file, "%.3f w\n",
					  (double) values.line_width * lw);

		if (values.line_style == GDK_LINE_ON_OFF_DASH)
			stream += fprintf(file, "[1 1] 0 d\n");

		translate_point(vect->x1, vect->y1,
				radar->cx, radar->cy, radar->r,
				0.0, 0.0, 6.0 * step, &x, &y);
		stream += fprintf(file, "%.3f %.3f m\n", x, -y);

		translate_point(vect->x2, vect->y2,
				radar->cx, radar->cy, radar->r,
				0.0, 0.0, 6.0 * step, &x, &y);
		stream += fprintf(file, "%.3f %.3f l\n", x, -y);

		stream += fprintf(file, "S\n");

		if (values.line_style == GDK_LINE_ON_OFF_DASH)
			stream += fprintf(file, "[] 0 d\n");
	}

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		target = &radar->target[i];

		for (j = 0; j < TARGET_NR_VECTORS; j++) {
			vect = &target->vectors[j];

			if (!vect->is_visible)
				continue;

			if (color)
				stream += output_gc_rgb_color(file, vect->gc);

			gdk_gc_get_values(vect->gc, &values);

			if (values.line_width == 0)
				stream += fprintf(file, "%.3f w\n",
						  color ? lw : 2.0 * lw);
			else
				stream += fprintf(file, "%.3f w\n", (double)
						  values.line_width * lw);

			if (values.line_style == GDK_LINE_ON_OFF_DASH)
				stream += fprintf(file, "[1 1] 0 d\n");

			translate_point(vect->x1, vect->y1,
					radar->cx, radar->cy, radar->r,
					0.0, 0.0, 6.0 * step, &x, &y);
			stream += fprintf(file, "%.3f %.3f m\n", x, -y);

			translate_point(vect->x2, vect->y2,
					radar->cx, radar->cy, radar->r,
					0.0, 0.0, 6.0 * step, &x, &y);
			stream += fprintf(file, "%.3f %.3f l\n", x, -y);

			stream += fprintf(file, "S\n");

			if (values.line_style == GDK_LINE_ON_OFF_DASH)
				stream += fprintf(file, "[] 0 d\n");
		}

		for (j = 0; j < TARGET_NR_ARCS; j++) {
			arc = &target->arcs[j];

			if (!arc->is_visible)
				continue;

			if (color)
				stream += output_gc_rgb_color(file, arc->gc);

			gdk_gc_get_values(arc->gc, &values);

			if (values.line_width == 0)
				stream += fprintf(file, "%.3f w\n",
						  color ? lw : 2.0 * lw);
			else
				stream += fprintf(file, "%.3f w\n", (double)
						  values.line_width * lw);

			translate_point(arc->x, arc->y,
					radar->cx, radar->cy, radar->r,
					0.0, 0.0, 6.0 * step, &x, &y);
			translate_length(arc->radius, radar->r,
					 6.0 * step, &r);

			stream += output_arc(file, x, -y, r,
					     arc->angle1, arc->angle2);
		}

		for (j = 0; j < TARGET_NR_POLYS; j++) {
			poly = &target->polys[j];

			if (!poly->is_visible)
				continue;

			if (color)
				stream += output_gc_rgb_color(file, poly->gc);

			gdk_gc_get_values(poly->gc, &values);

			if (values.line_width == 0)
				stream += fprintf(file, "%.3f w\n",
						  color ? lw : 2.0 * lw);
			else
				stream += fprintf(file, "%.3f w\n", (double)
						  values.line_width * lw);

			translate_point(poly->points[0].x, poly->points[0].y,
					radar->cx, radar->cy, radar->r,
					0.0, 0.0, 6.0 * step, &x, &y);
			stream += fprintf(file, "%.3f %.3f m\n", x, -y);
			for (k = 1; k < poly->npoints; k++) {
				translate_point(poly->points[k].x,
						poly->points[k].y,
						radar->cx, radar->cy, radar->r,
						0.0, 0.0, 6.0 * step, &x, &y);
				stream += fprintf(file, "%.3f %.3f l\n", x, -y);
			}

			stream += fprintf(file, "h f\n");
		}

		for (j = 0; j < TARGET_NR_LABELS; j++) {
			label = &target->labels[j];

			if (!label->is_visible)
				continue;

			if (color)
				stream += output_gc_rgb_color(file, label->fg);

			translate_point(label->cx, label->cy,
					radar->cx, radar->cy, radar->r,
					0.0, 0.0, 6.0 * step, &x, &y);

			translate_length(label->xoff, radar->r, 6.0 * step,
					 &xoff);
			translate_length(label->yoff, radar->r, 6.0 * step,
					 &yoff);

			afm_text_extents(afm, 1.5 * fs, label->markup,
					 strlen(label->markup), &extents);

#ifdef DEBUG_LABEL_ALIGN
			stream += pdf_output_align(file, x + xoff, -y - yoff);
#endif

			pdf_label_align(label->xalign, label->yalign,
					&extents, &xoff, &yoff);

#ifdef DEBUG_LABEL_ALIGN
			stream += pdf_output_bbox(file, x + xoff, -y - yoff,
						  &extents);
#endif

			stream += afm_print_text(file, afm, 1.5 * fs,
						 x + xoff, -y - yoff,
						 label->markup,
						 strlen(label->markup));
		}
	}

	stream += fprintf(file, "1 0 0 1 %.3f %.3f cm\n", -xoffset, -yoffset);

	xoffset -= 6.0 * step;
	yoffset = 10.0 + fs + 2.0 * nh + step / 3.0;
	sw = 12.0 * step;

	stream += fprintf(file, "1 0 0 1 %.3f %.3f cm\n", xoffset, yoffset);

	stream += output_linear_scale(file, afm, 0.75 * fs, lw,
				      sw, step / 6.0, 0.0, 2.0 * radar->range);

	stream += fprintf(file, "1 0 0 1 %.3f %.3f cm\n", -xoffset, -yoffset);

	yoffset = 10.0 + nh + step / 24.0;
	sw = w - xoffset - 10.0;

	stream += fprintf(file, "1 0 0 1 %.3f %.3f cm\n", xoffset, yoffset);

	stream += output_log_scale(file, afm, 0.75 * fs, lw,
				   sw, step / 6.0, 0.1, 60.0);

	stream += fprintf(file, "1 0 0 1 %.3f %.3f cm\n", -xoffset, -yoffset);

	xoffset = 12.0 * step + 3.0 * nw + nh + 10.0;
	yoffset = 10.0 + fs + 2.0 * nh + step / 3.0;

	stream += fprintf(file, "1 0 0 1 %.3f %.3f cm\n", xoffset, yoffset);

	stream += output_table(radar, file, afm, fs, lw,
			       w - xoffset - 10.0, h - yoffset - 10.0,
			       annot_rect);

	annot_rect[0].llx += xoffset;
	annot_rect[0].urx += xoffset;
	annot_rect[0].lly += yoffset;
	annot_rect[0].ury += yoffset;
	annot_rect[1].llx += xoffset;
	annot_rect[1].urx += xoffset;
	annot_rect[1].lly += yoffset;
	annot_rect[1].ury += yoffset;

	annot_rect[0].llx *= 72.0 / 25.4;
	annot_rect[0].urx *= 72.0 / 25.4;
	annot_rect[0].lly *= 72.0 / 25.4;
	annot_rect[0].ury *= 72.0 / 25.4;
	annot_rect[1].llx *= 72.0 / 25.4;
	annot_rect[1].urx *= 72.0 / 25.4;
	annot_rect[1].lly *= 72.0 / 25.4;
	annot_rect[1].ury *= 72.0 / 25.4;

	stream += fprintf(file, "1 0 0 1 %.3f %.3f cm\n", -xoffset, -yoffset);

	if (radar_ranges[radar->rindex].digits)
		sprintf((char *) text, _("%.*f nm"),
			radar_ranges[radar->rindex].digits,
			radar->range);
	else
		sprintf((char *) text, _("%.1f nm"), radar->range);

	afm_text_extents(afm, 3.0 * fs, (char *) text,
			 strlen((char *) text), &extents);
	stream += afm_print_text(file, afm, 3.0 * fs,
				 10.0 - extents.bbox.llx,
				 h - 10.0 - extents.ascent,
				 (char *) text, strlen((char *) text));

	stream += fprintf(file, "Q\n");

	offset += stream;
	offset += fprintf(file, "endstream\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	length = offset;
	offset += fprintf(file, "6 0 obj\n");
	offset += fprintf(file, "   %u\n", stream);
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	procset = offset;
	offset += fprintf(file, "7 0 obj\n");
	offset += fprintf(file, "   [ /PDF /Text ]\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	font = offset;
	offset += fprintf(file, "8 0 obj\n");
	offset += fprintf(file, "   << /Type /Font\n");
	offset += fprintf(file, "      /Subtype /Type1\n");
	offset += fprintf(file, "      /Name /%s\n", afm->pdf_name);
	offset += fprintf(file, "      /BaseFont /%s\n", afm->font_name);
	offset += fprintf(file, "      /Encoding 9 0 R\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	encoding = offset;
	offset += fprintf(file, "9 0 obj\n");
	offset += fprintf(file, "   << /Type /Encoding\n");
	offset += fprintf(file, "      /Differences\n");
	offset += fprintf(file, "         [\n");
	for (i = 0; i < 256; i++) {
		if (NULL == afm->char_table[i].ch)
			continue;
		offset += fprintf(file, "            %3u /%s\n",
				  i, afm->char_table[i].ch->name);
	}
	offset += fprintf(file, "         ]\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	t = time(NULL);

	tm0 = localtime(&t);
	tm1 = gmtime(&t);

	tz = mktime(tm0) - mktime(tm1);

	if ((p = strrchr(filename, '/')))
		p++;
	else if ((p = strrchr(filename, '\\')))
		p++;
	else
		p = filename;

	info = offset;
	offset += fprintf(file, "10 0 obj\n");
	offset += fprintf(file, "   << /Type /Info\n");
	offset += fprintf(file, "      /Title (Radarplot - %s)\n", p);
	offset += fprintf(file, "      /Author (Christian Dost <ecd@brainaid.de>)\n");
	offset += fprintf(file, "      /Subject (Radarplot)\n");
	offset += fprintf(file, "      /Keywords (Radarplot)\n");
	offset += fprintf(file, "      /Creator (brainaid Radarplot %u.%u.%u)\n",
			  RADAR_MAJOR, RADAR_MINOR, RADAR_PATCHLEVEL);
	offset += fprintf(file, "      /Producer (brainaid Radarplot %u.%u.%u)\n",
			  RADAR_MAJOR, RADAR_MINOR, RADAR_PATCHLEVEL);
	offset += fprintf(file, "      /Company (brainaid GbR)\n");
	offset += fprintf(file, "      /SourceModified (%.4s%.2s%.2s%.2s%.2s%.2s)\n",
			  &source_date[7], &source_date[12],
			  &source_date[15], &source_date[18],
			  &source_date[21], &source_date[24]);
	offset += fprintf(file, "      /CreationDate (D:%04u%02u%02u%02u%02u%02u%c%02u'00')\n",
			  tm0->tm_year + 1900, tm0->tm_mon + 1, tm0->tm_mday,
			  tm0->tm_hour, tm0->tm_min, tm0->tm_sec,
			  tz < 0 ? '-' : '+', abs(tz) / 3600);
	offset += fprintf(file, "      /ModDate (D:%04u%02u%02u%02u%02u%02u%c%02u'00')\n",
			  tm0->tm_year + 1900, tm0->tm_mon + 1, tm0->tm_mday,
			  tm0->tm_hour, tm0->tm_min, tm0->tm_sec,
			  tz < 0 ? '-' : '+', abs(tz) / 3600);
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	uri[0] = offset;
	offset += fprintf(file, "11 0 obj\n");
	offset += fprintf(file, "   << /S /URI\n");
	offset += fprintf(file, "      /URI (mailto:ecd@brainaid.de)\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	uri[1] = offset;
	offset += fprintf(file, "12 0 obj\n");
	offset += fprintf(file, "   << /S /URI\n");
	offset += fprintf(file, "      /URI (http://brainaid.de/people/ecd/radarplot/)\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	annot[0] = offset;
	offset += fprintf(file, "13 0 obj\n");
	offset += fprintf(file, "   << /Type /Annot\n");
	offset += fprintf(file, "      /Subtype /Link\n");
	offset += fprintf(file, "      /Border [0 0 0]\n");
	offset += fprintf(file, "      /Rect [%.4f %.4f %.4f %.4f]\n",
			  annot_rect[0].llx, annot_rect[0].lly,
			  annot_rect[0].urx, annot_rect[0].ury);
	offset += fprintf(file, "      /BS << /Type /Border /S /S /W 0 >>\n");
	offset += fprintf(file, "      /A 11 0 R\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	annot[1] = offset;
	offset += fprintf(file, "14 0 obj\n");
	offset += fprintf(file, "   << /Type /Annot\n");
	offset += fprintf(file, "      /Subtype /Link\n");
	offset += fprintf(file, "      /Border [0 0 0]\n");
	offset += fprintf(file, "      /Rect [%.4f %.4f %.4f %.4f]\n",
			  annot_rect[1].llx, annot_rect[1].lly,
			  annot_rect[1].urx, annot_rect[1].ury);
	offset += fprintf(file, "      /BS << /Type /Border /S /S /W 0 >>\n");
	offset += fprintf(file, "      /A 12 0 R\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	annots = offset;
	offset += fprintf(file, "15 0 obj\n");
	offset += fprintf(file, "   [ 13 0 R 14 0 R ]\n");
	offset += fprintf(file, "endobj\n");
	offset += fprintf(file, "\n");

	xref = offset;
	offset += fprintf(file, "xref\n");
	offset += fprintf(file, "0 16\n");
	offset += fprintf(file, "%010u %05u f \n", 0, 65535);
	offset += fprintf(file, "%010u %05u n \n", catalog, 0);
	offset += fprintf(file, "%010u %05u n \n", outlines, 0);
	offset += fprintf(file, "%010u %05u n \n", pages, 0);
	offset += fprintf(file, "%010u %05u n \n", page, 0);
	offset += fprintf(file, "%010u %05u n \n", content, 0);
	offset += fprintf(file, "%010u %05u n \n", length, 0);
	offset += fprintf(file, "%010u %05u n \n", procset, 0);
	offset += fprintf(file, "%010u %05u n \n", font, 0);
	offset += fprintf(file, "%010u %05u n \n", encoding, 0);
	offset += fprintf(file, "%010u %05u n \n", info, 0);
	offset += fprintf(file, "%010u %05u n \n", uri[0], 0);
	offset += fprintf(file, "%010u %05u n \n", uri[1], 0);
	offset += fprintf(file, "%010u %05u n \n", annot[0], 0);
	offset += fprintf(file, "%010u %05u n \n", annot[1], 0);
	offset += fprintf(file, "%010u %05u n \n", annots, 0);
	offset += fprintf(file, "\n");

	offset += fprintf(file, "trailer\n");
	offset += fprintf(file, "   << /Size 16\n");
	offset += fprintf(file, "      /Root 1 0 R\n");
	offset += fprintf(file, "      /Info 10 0 R\n");
	offset += fprintf(file, "   >>\n");
	offset += fprintf(file, "\n");

	offset += fprintf(file, "startxref\n");
	offset += fprintf(file, "%u\n", xref);
	offset += fprintf(file, "%%%%EOF\n");

	fclose(file);

	afm_free(afm);
	return 0;
}


typedef struct {
	GtkWidget	*table;
	GtkTooltips	*tooltips;
	int		rows_used;
	int		cols_used;
	GtkWidget	*width_label;
	GtkSpinButton	*width_spin;
	GtkWidget	*height_label;
	GtkSpinButton	*height_spin;
	GtkToggleButton	*aspect_toggle;
	int		width;
	int		height;
	gboolean	keep_aspect;
	double		aspect;
	int		change_level;
} size_chooser_t;

static void
sizer_width_changed(GtkSpinButton *spin, gpointer user_data)
{
	size_chooser_t *sizer = user_data;

	sizer->change_level++;

	sizer->width = gtk_spin_button_get_value_as_int(spin);
	if (sizer->keep_aspect && (sizer->change_level == 1)) {
		sizer->height = ((double) sizer->width) / sizer->aspect;
		gtk_spin_button_set_value(sizer->height_spin, sizer->height);
	}

	sizer->change_level--;
}

static void
sizer_height_changed(GtkSpinButton *spin, gpointer user_data)
{
	size_chooser_t *sizer = user_data;

	sizer->change_level++;

	sizer->height = gtk_spin_button_get_value_as_int(spin);
	if (sizer->keep_aspect && (sizer->change_level == 1)) {
		sizer->width = ((double) sizer->height) * sizer->aspect;
		gtk_spin_button_set_value(sizer->width_spin, sizer->width);
	}

	sizer->change_level--;
}

static void
sizer_aspect_toggled(GtkToggleButton *toggle, gpointer user_data)
{
	size_chooser_t *sizer = user_data;

	if (gtk_toggle_button_get_active(toggle)) {
		sizer->keep_aspect = TRUE;
		sizer->aspect = ((double) sizer->width) /
				((double) sizer->height);
	} else {
		sizer->keep_aspect = FALSE;
	}
}

static void
radar_init_size_chooser(size_chooser_t *sizer, int width, int height,
			int extra_rows, int extra_cols)
{
	memset(sizer, 0, sizeof(size_chooser_t));

	sizer->table = gtk_table_new(2 + extra_rows, 4 + extra_cols, FALSE);
	sizer->tooltips = gtk_tooltips_new();

	gtk_container_set_border_width(GTK_CONTAINER(sizer->table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(sizer->table), TABLE_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(sizer->table), TABLE_COL_SPACING);

	sizer->width_label = gtk_label_new(_("Width:"));
	gtk_misc_set_alignment(GTK_MISC(sizer->width_label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(sizer->table), sizer->width_label,
			 0, 1, 0, 1,
			 0, GTK_EXPAND, 0, 0);
	sizer->width_spin = GTK_SPIN_BUTTON(
				radar_init_spin(320, 2048, 1, 10, width));
	gtk_tooltips_set_tip(sizer->tooltips, GTK_WIDGET(sizer->width_spin),
			     _("Width of Image in Pixels"), NULL);
	g_signal_connect(G_OBJECT(sizer->width_spin), "value-changed",
			 G_CALLBACK(sizer_width_changed), sizer);
	gtk_table_attach(GTK_TABLE(sizer->table),
			 GTK_WIDGET(sizer->width_spin),
			 1, 2, 0, 1,
			 0, GTK_EXPAND, 0, 0);

	sizer->height_label = gtk_label_new(_("Height:"));
	gtk_misc_set_alignment(GTK_MISC(sizer->height_label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(sizer->table), sizer->height_label,
			 2, 3, 0, 1,
			 0, GTK_EXPAND, 0, 0);
	sizer->height_spin = GTK_SPIN_BUTTON(
				radar_init_spin(320, 2048, 1, 10, height));
	gtk_tooltips_set_tip(sizer->tooltips, GTK_WIDGET(sizer->height_spin),
			     _("Height of Image in Pixels"), NULL);
	g_signal_connect(G_OBJECT(sizer->height_spin), "value-changed",
			 G_CALLBACK(sizer_height_changed), sizer);
	gtk_table_attach(GTK_TABLE(sizer->table),
			 GTK_WIDGET(sizer->height_spin),
			 3, 4, 0, 1,
			 0, GTK_EXPAND, 0, 0);

	sizer->aspect_toggle =
			GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label(
				_("Keep Aspect Ratio")));
	gtk_tooltips_set_tip(sizer->tooltips, GTK_WIDGET(sizer->aspect_toggle),
			     _("Keep fixed ratio of width and height of image"),
			     NULL);
	gtk_toggle_button_set_active(sizer->aspect_toggle, TRUE);
	g_signal_connect(G_OBJECT(sizer->aspect_toggle), "toggled",
			 G_CALLBACK(sizer_aspect_toggled), sizer);
	gtk_table_attach(GTK_TABLE(sizer->table),
			 GTK_WIDGET(sizer->aspect_toggle),
			 0, 4, 1, 2,
			 0, GTK_EXPAND, 0, 0);

	sizer->rows_used = 2;
	sizer->cols_used = 4;

	sizer->width = width;
	sizer->height = height;

	sizer->keep_aspect = TRUE;
	sizer->aspect = ((double) width) / ((double) height);
}

void
radar_export_jpeg(GtkAction *action, gpointer user_data)
{
	char quality_as_string[32];
	radar_t *radar = user_data;
	size_chooser_t sizer;
	GtkSpinButton *quality_spin;
	GtkWidget *quality_label;
	GtkFileChooser *chooser;
	GtkFileFilter *filter;
	GtkWidget *dialog;
	char *p, *filename;
	char *pathname;
	char *options[2];
	char *keys[2];

	dialog = gtk_file_chooser_dialog_new(_("Save as JPEG Image"),
					     GTK_WINDOW(radar->window),
					     GTK_FILE_CHOOSER_ACTION_SAVE,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_CANCEL,
					     GTK_STOCK_SAVE,
					     GTK_RESPONSE_ACCEPT,
					     NULL);
	chooser = GTK_FILE_CHOOSER(dialog);

	gtk_file_chooser_set_local_only(chooser, TRUE);
	gtk_file_chooser_set_select_multiple(chooser, FALSE);

	filter = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filter, "*.jpg");
	gtk_file_filter_add_pattern(filter, "*.jpeg");
	gtk_file_filter_set_name(filter, _("JPEG Files"));
	gtk_file_chooser_set_filter(chooser, filter);

	if (radar->image_pathname) {
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
						    radar->image_pathname);
	}

	radar_init_size_chooser(&sizer, radar->w, radar->h, 0, 2);

	quality_label = gtk_label_new(_("JPEG Quality:"));
	gtk_misc_set_alignment(GTK_MISC(quality_label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(sizer.table), quality_label,
			 sizer.cols_used, sizer.cols_used + 1, 0, 1,
			 0, GTK_EXPAND, 0, 0);
	quality_spin = GTK_SPIN_BUTTON(radar_init_spin(0, 100, 1, 10, 95));
	gtk_tooltips_set_tip(sizer.tooltips, GTK_WIDGET(quality_spin),
			     _("JPEG Quality in percent (0 - 100)"), NULL);
	gtk_table_attach(GTK_TABLE(sizer.table), GTK_WIDGET(quality_spin),
			 sizer.cols_used + 1, sizer.cols_used + 2, 0, 1,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show_all(sizer.table);

	gtk_file_chooser_set_extra_widget(chooser, sizer.table);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
		goto out;

	pathname = gtk_file_chooser_get_current_folder(
						GTK_FILE_CHOOSER(dialog));
	if (pathname) {
		if (radar->image_pathname) {
			g_free(radar->image_pathname);
		}
		radar->image_pathname = pathname;
	}

	filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

	p = strrchr(filename, '.');
	if ((NULL == p) || (strcasecmp(p, ".jpg") && strcasecmp(p, ".jpeg"))) {
		p = g_malloc(strlen(filename) + 5);
		sprintf(p, "%s.jpg", filename);
		g_free(filename);
		filename = p;
	}

	sprintf(quality_as_string, "%u",
		gtk_spin_button_get_value_as_int(quality_spin));

	keys[0] = "quality";
	keys[1] = NULL;
	options[0] = quality_as_string;
	options[1] = NULL;

	gdk_window_set_cursor(dialog->window, radar->busy_cursor);

	radar_save_as_image(radar, sizer.width, sizer.height,
			    filename, "jpeg", keys, options);

	g_free(filename);

out:
	gtk_widget_destroy(dialog);
}

void
radar_export_png(GtkAction *action, gpointer user_data)
{
	radar_t *radar = user_data;
	size_chooser_t sizer;
	GtkFileChooser *chooser;
	GtkFileFilter *filter;
	GtkWidget *dialog;
	char *p, *filename;
	char *pathname;
	char *options[1];
	char *keys[1];

	dialog = gtk_file_chooser_dialog_new(_("Save as PNG Image"),
					     GTK_WINDOW(radar->window),
					     GTK_FILE_CHOOSER_ACTION_SAVE,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_CANCEL,
					     GTK_STOCK_SAVE,
					     GTK_RESPONSE_ACCEPT,
					     NULL);
	chooser = GTK_FILE_CHOOSER(dialog);

	gtk_file_chooser_set_local_only(chooser, TRUE);
	gtk_file_chooser_set_select_multiple(chooser, FALSE);

	filter = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filter, "*.png");
	gtk_file_filter_set_name(filter, _("PNG Files"));
	gtk_file_chooser_set_filter(chooser, filter);

	if (radar->image_pathname) {
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
						    radar->image_pathname);
	}

	radar_init_size_chooser(&sizer, radar->w, radar->h, 0, 0);
	gtk_widget_show_all(sizer.table);

	gtk_file_chooser_set_extra_widget(chooser, sizer.table);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
		goto out;

	pathname = gtk_file_chooser_get_current_folder(
						GTK_FILE_CHOOSER(dialog));
	if (pathname) {
		if (radar->image_pathname) {
			g_free(radar->image_pathname);
		}
		radar->image_pathname = pathname;
	}

	filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

	p = strrchr(filename, '.');
	if ((NULL == p) || strcasecmp(p, ".png")) {
		p = g_malloc(strlen(filename) + 5);
		sprintf(p, "%s.png", filename);
		g_free(filename);
		filename = p;
	}

	keys[0] = NULL;
	options[0] = NULL;

	gdk_window_set_cursor(dialog->window, radar->busy_cursor);

	radar_save_as_image(radar, sizer.width, sizer.height,
			    filename, "png", keys, options);

	g_free(filename);

out:
	gtk_widget_destroy(dialog);
}

void
radar_export_pdf(GtkAction *action, gpointer user_data)
{
	radar_t *radar = user_data;
	GtkFileChooser *chooser;
	GtkFileFilter *filter;
	GtkWidget *dialog;
	GtkWidget *table;
	GtkWidget *label;
	GtkWidget *combo;
	GtkWidget *button;
	GSList *group;
	char *p, *filename;
	char *pathname;
	gboolean color;
	int i, index;

	dialog = gtk_file_chooser_dialog_new(_("Print to PDF File"),
					     GTK_WINDOW(radar->window),
					     GTK_FILE_CHOOSER_ACTION_SAVE,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_CANCEL,
					     GTK_STOCK_SAVE,
					     GTK_RESPONSE_ACCEPT,
					     NULL);
	chooser = GTK_FILE_CHOOSER(dialog);

	gtk_file_chooser_set_local_only(chooser, TRUE);
	gtk_file_chooser_set_select_multiple(chooser, FALSE);

	filter = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filter, "*.pdf");
	gtk_file_filter_set_name(filter, _("PDF Files"));
	gtk_file_chooser_set_filter(chooser, filter);

	if (radar->image_pathname) {
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
						    radar->image_pathname);
	}

	table = gtk_table_new(1, 5, FALSE);

	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), TABLE_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(table), TABLE_COL_SPACING);

	label = gtk_label_new(_("Papersize:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_table_attach(GTK_TABLE(table), label,
			 0, 1, 0, 1,
			 0, GTK_EXPAND, 0, 0);

	combo = gtk_combo_box_new_text();
	for (i = 0; i < NR_PAPER_FORMATS; i++)
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo),
					  _(paper_formats[i].name));
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 1);

	gtk_table_attach(GTK_TABLE(table), combo,
			 1, 2, 0, 1,
			 0, GTK_EXPAND, 0, 0);


	label = gtk_label_new(_("Color"));
	gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
	button = gtk_radio_button_new(NULL);
	gtk_container_add(GTK_CONTAINER(button), label);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);

	label = gtk_label_new("");
	gtk_table_attach(GTK_TABLE(table), label,
			 2, 3, 0, 1,
			 GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);

	gtk_table_attach(GTK_TABLE(table), button,
			 3, 4, 0, 1,
			 0, GTK_EXPAND, 0, 0);

	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));

	label = gtk_label_new(_("Black/White"));
	gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
	button = gtk_radio_button_new(group);
	gtk_container_add(GTK_CONTAINER(button), label);

	gtk_table_attach(GTK_TABLE(table), button,
			 4, 5, 0, 1,
			 0, GTK_EXPAND, 0, 0);

	gtk_widget_show_all(table);

	gtk_file_chooser_set_extra_widget(chooser, table);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
		goto out;

	pathname = gtk_file_chooser_get_current_folder(
						GTK_FILE_CHOOSER(dialog));
	if (pathname) {
		if (radar->image_pathname) {
			g_free(radar->image_pathname);
		}
		radar->image_pathname = pathname;
	}

	filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

	p = strrchr(filename, '.');
	if ((NULL == p) || strcasecmp(p, ".pdf")) {
		p = g_malloc(strlen(filename) + 5);
		sprintf(p, "%s.pdf", filename);
		g_free(filename);
		filename = p;
	}

	index = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	color = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)) ?
								FALSE : TRUE;

	gdk_window_set_cursor(dialog->window, radar->busy_cursor);

	bind_textdomain_codeset("radarplot", "ISO-8859-1");

	radar_print_as_PDF(radar, filename, paper_formats[index].name, color);

	bind_textdomain_codeset("radarplot", "UTF-8");

	g_free(filename);

out:
	gtk_widget_destroy(dialog);
}

/* $Id: radar.h,v 1.23 2009-07-22 06:18:50 ecd Exp $
 */

#ifndef RADAR_H
#define RADAR_H


#include "translation.h"


#define TABLE_ROW_SPACING	2
#define TABLE_COL_SPACING	2

#define PANEL_BORDER_WIDTH	2
#define PANEL_ROW_SPACING	2
#define PANEL_COL_SPACING	2

#define BG_TEXT_SIDE_SPACING	8

#define LABEL_BASE		"Sans"
#define LABEL_FONT		LABEL_BASE " 10"


#define ALIGN_LEFT		0.0
#define ALIGN_RIGHT		1.0
#define ALIGN_TOP		0.0
#define ALIGN_BOTTOM		1.0


#define COLOR_RED		0x80, 0, 0
#define COLOR_GREEN		0, 0x80, 0
#define COLOR_BLUE		0, 0, 0xc0


#ifndef GTK_STOCK_ABOUT
#define GTK_STOCK_ABOUT		NULL
#endif


#define RADAR_NR_TARGETS	5

#define EPSILON			1e-12

#define STARBOARD		(1.0)
#define PORT			(-1.0)


typedef struct {
	int		is_visible;
	double		x1, y1;
	double		x2, y2;
	GdkGC		*gc;
	GdkRectangle	bbox;
} vector_t;

typedef struct {
	int		is_visible;
	double		x, y;
	double		radius;
	double		angle1, angle2;
	GdkGC		*gc;
	GdkRectangle	bbox;
} arc_t;

typedef struct {
	int		is_visible;
	double		cx, cy;
	double		xoff, yoff;
	double		xalign, yalign;
	unsigned int	tw, th;
	GdkGC		*fg;
	GdkGC		*bg;
	char		*markup;
	PangoLayout	*layout;
	GdkRectangle	bbox;
} text_label_t;


typedef struct {
	double		x;
	double		y;
} point_t;

typedef struct {
	point_t		p1;
	point_t		p2;
} line_t;

typedef struct {
	double		y1;	/* y coordinate of top edge */
	double		x11;	/* x coordinate of top left corner */
	double		x21;	/* x coordinate of top right corner */
	double		y2;	/* y coordinate of bottom edge */
	double		x12;	/* x coordinate of bottom left corner */
	double		x22;	/* x coordinate of bottom right corner */
} trapezoid_t;

typedef struct {
	double		x1, y1;
	double		x2, y2;
} segment_t;

typedef struct {
	int		is_visible;
	point_t		points[3];
	int		npoints;
	GdkGC		*gc;
	GdkRectangle	bbox;
} poly_t;

enum radar_vector_number {
	VECTOR_HEADING = 0,
	VECTOR_NEW_HEADING,
	RADAR_NR_VECTORS
};

enum target_vector_number {
	VECTOR_REL_EXT = 0,
	VECTOR_NEW_REL0,
	VECTOR_NEW_REL1,
	VECTOR_OWN,
	VECTOR_CPA,
	VECTOR_NEW_CPA,
	VECTOR_TRUE,
	VECTOR_RELATIVE,
	VECTOR_NEW_OWN,
	VECTOR_POSX0,
	VECTOR_POSX1,
	VECTOR_POSY0,
	VECTOR_POSY1,
	VECTOR_MPOINTX,
	VECTOR_MPOINTY,
	TARGET_NR_VECTORS
};

enum target_arc_number {
	ARC_RELATIVE_ARROW = 0,
	ARC_COURSE,
	TARGET_NR_ARCS
};

enum target_poly_number {
	POLY_OWN_ARROW = 0,
	POLY_TRUE_ARROW0,
	POLY_TRUE_ARROW1,
	POLY_RELATIVE_ARROW,
	POLY_NEW_OWN_ARROW,
	TARGET_NR_POLYS
};

enum target_label_number {
	LABEL_SIGHT0 = 0,
	LABEL_SIGHT1,
	TARGET_NR_LABELS
};

typedef enum {
	MANEUVER_NONE = 0,
	MANEUVER_COURSE_FROM_CPA,
	MANEUVER_SPEED_FROM_CPA,
	MANEUVER_CPA_FROM_COURSE,
	MANEUVER_CPA_FROM_SPEED
} maneuver_t;


typedef struct {
	double		x;
	double		y;
} vector_xy_t;


typedef struct {
	double		range;
	int		marks;
	int		digits;
} range_t;

extern range_t radar_ranges[];


struct __radar_s__;
typedef struct __radar_s__ radar_t;


typedef struct {
	int		index;
	radar_t		*radar;

	int		time[2];
	int		rasp[2];
	int		rasp_course_offset[2];
	gboolean	rasp_selected[2];
	int		rakrp[2];
	double		distance[2];

	int		delta_time;

	double		KBr;
	double		vBr;
	double		KB;
	double		vB;
	double		aspect;

	gboolean	have_cpa;

	double		CPA;
	double		PCPA;
	double		SPCPA;
	double		TCPA;
	int		tCPA;

	gboolean	have_crossing;

	double		BCR;
	double		BCT;
	int		BCt;

	int		mtime_range;
	gboolean	have_mpoint;
	gboolean	have_new_cpa;
	gboolean	have_problems;

	double		new_KBr;
	double		new_vBr;
	double		delta;
	double		new_RaSP;
	double		new_aspect;

	double		new_CPA;
	double		new_PCPA;
	double		new_SPCPA;
	double		new_TCPA;
	int		new_tCPA;

	gboolean	new_have_crossing;

	double		new_BCR;
	double		new_BCT;
	int		new_BCt;

	double		mdistance;
	double		mbearing;

	vector_xy_t	sight[2];
	vector_xy_t	p0_sub_own;
	vector_xy_t	cpa;
	vector_xy_t	cross;
	vector_xy_t	mpoint;
	vector_xy_t	new_cpa;
	vector_xy_t	xpoint;
	vector_xy_t	new_cross;

	GtkSpinButton	*time_spin[2];
	GtkToggleButton	*rasp_radio[2];
	GtkToggleButton	*rakrp_radio[2];
	GtkSpinButton	*rasp_spin[2];
	GtkSpinButton	*rasp_course_spin[2];
	GtkSpinButton	*rakrp_spin[2];
	GtkSpinButton	*dist_spin[2];

	GdkGC		*pos_gc;
	GdkGC		*vec_gc;
	GdkGC		*vec_mark_gc;
	GdkGC		*cpa_gc;
	GdkGC		*ext_gc;
	GdkGC		*ext_dash_gc;
	GdkGC		*own_gc;
	GdkGC		*own_mark_gc;
	GdkGC		*true_gc;
	GdkGC		*true_mark_gc;
	GdkGC		*arc_gc;

	arc_t		arcs[TARGET_NR_ARCS];
	poly_t		polys[TARGET_NR_POLYS];
	vector_t	vectors[TARGET_NR_VECTORS];
	text_label_t	labels[TARGET_NR_LABELS];

	GtkEntry	*KBr_entry;
	GtkEntry	*vBr_entry;
	GtkEntry	*KB_entry;
	GtkEntry	*vB_entry;
	GtkEntry	*aspect_entry;
	GtkEntry	*CPA_entry;
	GtkEntry	*SPCPA_entry;
	GtkEntry	*PCPA_entry;
	GtkEntry	*TCPA_entry;
	GtkEntry	*tCPA_entry;
	GtkEntry	*BCR_entry;
	GtkEntry	*BCT_entry;
	GtkEntry	*BCt_entry;

	GtkEntry	*new_KBr_entry;
	GtkEntry	*new_vBr_entry;
	GtkEntry	*delta_entry;
	GtkEntry	*new_RaSP_entry;
	GtkEntry	*new_aspect_entry;
	GtkEntry	*new_CPA_entry;
	GtkEntry	*new_SPCPA_entry;
	GtkEntry	*new_PCPA_entry;
	GtkEntry	*new_TCPA_entry;
	GtkEntry	*new_tCPA_entry;
	GtkEntry	*new_BCR_entry;
	GtkEntry	*new_BCT_entry;
	GtkEntry	*new_BCt_entry;
} target_t;

struct __radar_s__ {
	gboolean	north_up;
	gboolean	show_heading;

	int		rindex;
	double		range;

	int		own_course;
	double		own_speed;

	target_t	target[RADAR_NR_TARGETS];

	gboolean	wait_expose;
	gboolean	redraw_pending;
	gboolean	mtime_set;
	gboolean	mdist_set;

	int		w, h;
	double		cx, cy;
	int		r;
	int		step;

	int		mtarget;
	int		mtime;
	double		mdistance;
	double		exact_mtime;

	gboolean	mtime_selected;
	gboolean	mcourse_change;
	gboolean	mcpa_selected;

	maneuver_t	maneuver;

	double		mcpa;
	double		direction;
	double		ncourse;
	double		nspeed;

	int		mapped;
	int		change_level;

	vector_t	vectors[RADAR_NR_VECTORS];

	GdkGC		*white_gc;
	GdkGC		*black_gc;
	GdkGC		*grey25_gc;
	GdkGC		*grey25_clip2_gc;
	GdkGC		*grey50_gc;
	GdkGC		*grey50_clip0_gc;
	GdkGC		*grey50_clip1_gc;
	GdkGC		*grey50_clip3_gc;
	GdkGC		*green_gc;
	GdkGC		*green_dash_gc;

	GtkWidget	*window;
	GtkWidget	*canvas;
	GtkWidget	*menubar;

	GtkComboBox	*orientation_combo;
	GtkSpinButton	*range_spin;
	GtkToggleButton	*heading_toggle;

	GtkSpinButton	*own_course_spin;
	GtkSpinButton	*own_speed_spin;

	GtkComboBox	*target_combo;
	GtkToggleButton	*mtime_radio;
	GtkToggleButton	*mdist_radio;
	GtkSpinButton	*mtime_spin;
	GtkSpinButton	*mdist_spin;

	GtkComboBox	*maneuver_combo;
	GtkToggleButton	*mcpa_radio;
	GtkSpinButton	*mcpa_spin;
	GtkToggleButton	*ncourse_radio;
	GtkSpinButton	*ncourse_spin;
	GtkToggleButton	*nspeed_radio;
	GtkSpinButton	*nspeed_spin;

	gboolean	do_render;
	gboolean	default_heading;
	gboolean	default_rakrp;

	PangoLayout	*layout;
	GdkPixmap	*pixmap;
	GdkBitmap	*clip[4];

	GdkPixbuf	*icon16;
	GdkPixbuf	*icon32;
	GdkPixbuf	*icon48;
	GdkPixbuf	*icon64;

	GdkPixbuf	*backbuf;
	GdkPixbuf	*forebuf;

	GdkCursor	*busy_cursor;

	char		*plot_pathname;
	char		*image_pathname;

	char		*plot_filename;
	gboolean	load_pending;

	char		*license_pathname;
	GKeyFile	*license;

	GKeyFile	*key_file;

	GList		*translations;

	char		*lang_name;

	GtkTooltips	*tooltips;
};


void
radar_draw_arc(radar_t *radar, GdkDrawable *drawable,
	       GdkPixbuf *pixbuf, guchar alpha,
	       gboolean render, arc_t *a);

void
radar_draw_vector(radar_t *radar, GdkDrawable *drawable,
		  GdkPixbuf *pixbuf, guchar alpha,
		  gboolean render, vector_t *v);

void
radar_draw_poly(radar_t *radar, GdkDrawable *drawable,
		GdkPixbuf *pixbuf, guchar alpha,
		gboolean render, poly_t *p);

void
radar_draw_label(radar_t *radar, GdkDrawable *drawable,
		 GdkPixbuf *pixbuf, guchar alpha,
		 gboolean render, text_label_t *l);

void
radar_draw_bg_pixmap(radar_t *radar, GdkDrawable *drawable,
		     GdkPixbuf *pixbuf, guchar alpha,
                     PangoLayout *layout, gboolean render,
                     double cx, double cy, int step, int radius,
                     int width, int height);

GtkWidget *radar_init_spin(double min, double max, double step, double page,
			   double init);

void	radar_save_config(radar_t *radar, const char *filename);

void	radar_export_jpeg(GtkAction *action, gpointer user_data);
void	radar_export_png(GtkAction *action, gpointer user_data);
void	radar_export_pdf(GtkAction *action, gpointer user_data);

void	radar_register(GtkAction *action, gpointer user_data);
void	radar_license(GtkAction *action, gpointer user_data);

extern char *progname;
extern char *progpath;

#endif /* !(RADAR_H) */

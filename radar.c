/* $Id: radar.c,v 1.89 2009-07-25 10:40:54 ecd Exp $
 *
 * This file is part of radarplot, a Nautical Radar Plotting Aid.
 * Copyright (C) 2005  Eddie C. Dost  <ecd@brainaid.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <locale.h>
#include <ctype.h>

#include <gtk/gtk.h>

#include <glib.h>
#include <glib/gstdio.h>
#define GETTEXT_PACKAGE	"radarplot"
#include <glib/gi18n.h>

#ifdef __WIN32__
#include <windows.h>
#else /* __WIN32__ */
#include <sys/types.h>
#include <sys/wait.h>
#endif /* __WIN32__ */

#ifdef OS_Darwin
#include <ige-mac-integration.h>
#endif

#include "radar.h"
#include "license.h"
#include "register.h"

#include "radar16x16.h"
#include "radar32x32.h"
#include "radar48x48.h"
#include "radar64x64.h"


#undef DEBUG
#undef DEBUG_BBOX
#undef DEBUG_TEXT


range_t radar_ranges[] =
{
	{ 0.75,	3, 2 },
	{  1.5,	3, 1 },
	{  3.0,	3, 0 },
	{  6.0,	6, 0 },
	{ 12.0,	6, 0 },
	{ 24.0,	6, 0 },
};

#define RADAR_NR_RANGES	(sizeof(radar_ranges) / sizeof(radar_ranges[0]))

static const vector_xy_t vector_xy_null = { 0.0, 0.0 };

static int
lines_crossing(radar_t *radar, vector_xy_t *v1_0, vector_xy_t *v1_1,
	       vector_xy_t *v2_0, vector_xy_t *v2_1, vector_xy_t *res);

static void radar_draw_foreground(radar_t *radar);
static void radar_draw_background(radar_t *radar);

static void radar_draw_segments(radar_t *radar, GdkDrawable *drawable,
				GdkGC *gc, GdkPixbuf *pixbuf, guchar alpha,
				gboolean render, segment_t *segs, int nsegs); 

static void radar_load_config(radar_t *radar, const char *filename);

#ifndef __WIN32__
typedef struct {
	const char	*cmd;
	int		sync;
} open_url_t;

static const open_url_t openURL[] =
{
	{ "mozilla -remote 'openURL(%s)'",	1 },
	{ "netscape -remote 'openURL(%s)'",	1 },
	{ "opera -remote 'openURL(%s)'",	1 },
	{ "mozilla '%s'",			0 },
	{ "netscape '%s'",			0 },
	{ "opera '%s'",				0 },
	{ NULL,					0 }
};
#endif /* !(__WIN32__) */

static void
radar_activate_link(GtkAboutDialog *about, const gchar *link, gpointer data)
{
#ifdef __WIN32__
	ShellExecuteA(NULL, "Open", link, NULL, NULL, SW_SHOWNORMAL);
#else /* __WIN32__ */
	char command[1024];
	GError *error;
	gint status;
	int i;

	for (i = 0; openURL[i].cmd; i++) {
		snprintf(command, sizeof(command), openURL[i].cmd, link);

		error = NULL;
		if (openURL[i].sync) {
			g_spawn_command_line_sync(command, NULL, NULL,
						  &status, &error);
		} else {
			status = 0;
			g_spawn_command_line_async(command, &error);
		}

		if (error) {
			g_message("command line '%s' failed: %s",
				  command, error->message);
			g_error_free(error);
		} else if (WIFEXITED(status) && WEXITSTATUS(status)) {
			g_message("command line '%s' failed: %d",
				  command, WEXITSTATUS(status));
		} else {
			break;
		}
	}
#endif /* __WIN32__ */
}

static void
radar_activate_mail(GtkAboutDialog *about, const gchar *mail, gpointer data)
{
	char link[1024];

	snprintf(link, sizeof(link),
		 "mailto:%s?subject=Radarplot%%20%u.%u.%u", mail,
		 RADAR_MAJOR, RADAR_MINOR, RADAR_PATCHLEVEL);
	radar_activate_link(about, link, data);
}

static void
radar_quit(GtkAction *action)
{
	exit(0);
}

static void
radar_about(GtkAction *action, gpointer user_data)
{
	char *date = "$Date: 2009-07-25 10:40:54 $", *q;
	radar_t *radar = user_data;
	gchar copyright[64];
	gchar comments[32];
	gchar version[16];
	const gchar *authors[] =
	{
		"Christian \"Eddie\" Dost <ecd@brainaid.de>",
		NULL
	};
	GtkWidget *dialog;

	snprintf(copyright, sizeof(copyright),
		 "Copyright \302\251 %04u %s", 2005, "brainaid GbR");
	snprintf(version, sizeof(version), "%u.%u.%u",
		 RADAR_MAJOR, RADAR_MINOR, RADAR_PATCHLEVEL);
	q = strchr(date, ':');
	if (q) {
		date = q + 1;
		snprintf(comments, sizeof(comments),
			 "Date: %.*s", (int) (strlen(date) - 2), date);
	}

	gtk_about_dialog_set_email_hook(radar_activate_mail, NULL, NULL);
	gtk_about_dialog_set_url_hook(radar_activate_link, NULL, NULL);

	dialog = gtk_about_dialog_new();
	gtk_window_set_transient_for(GTK_WINDOW(dialog),
				     GTK_WINDOW(radar->window));

	gtk_about_dialog_set_name(GTK_ABOUT_DIALOG(dialog),
				  "brainaid Radarplot");
	gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), version);
	gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog), copyright);
	gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), comments);
	gtk_about_dialog_set_license(GTK_ABOUT_DIALOG(dialog),
				     gnu_general_public_license);
	gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog),
				     "http://brainaid.de/people/ecd/radarplot");
	gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog), radar->icon64);
	gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);
	gtk_about_dialog_set_translator_credits(GTK_ABOUT_DIALOG(dialog),
						_("translator-credits"));

	g_signal_connect_swapped(dialog, "response",
				 G_CALLBACK(gtk_widget_destroy), dialog);

	gtk_widget_show_all(dialog);
}

static void
radar_new(GtkAction *action, gpointer user_data)
{
	radar_t *radar = user_data;
	target_t *s;
	int i;

	radar->change_level++;

	if (radar->plot_filename) {
		g_free(radar->plot_filename);
		radar->plot_filename = NULL;
	}

	gtk_combo_box_set_active(radar->orientation_combo, 0);
	gtk_spin_button_set_value(radar->range_spin, RADAR_NR_RANGES / 2);
	gtk_toggle_button_set_active(radar->heading_toggle,
				     radar->default_heading);

	gtk_spin_button_set_value(radar->own_course_spin, 0.0);
	gtk_spin_button_set_value(radar->own_speed_spin, 0.0);

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		s = &radar->target[i];

		gtk_spin_button_set_value(s->dist_spin[0], 0.0);
		gtk_spin_button_set_value(s->dist_spin[1], 0.0);
		gtk_spin_button_set_value(s->time_spin[0], 0.0);
		gtk_spin_button_set_value(s->time_spin[1], 0.0);
		if (radar->default_rakrp) {
			gtk_toggle_button_set_active(s->rakrp_radio[0], TRUE);
			gtk_toggle_button_set_active(s->rakrp_radio[1], TRUE);
		} else {
			gtk_toggle_button_set_active(s->rasp_radio[0], TRUE);
			gtk_toggle_button_set_active(s->rasp_radio[1], TRUE);
		}
		gtk_spin_button_set_value(s->rasp_spin[0], 0.0);
		gtk_spin_button_set_value(s->rasp_spin[1], 0.0);
		gtk_spin_button_set_value(s->rasp_course_spin[0], 0.0);
		gtk_spin_button_set_value(s->rasp_course_spin[1], 0.0);
	}

	gtk_toggle_button_set_active(radar->mtime_radio, TRUE);
	gtk_spin_button_set_value(radar->mtime_spin, 0.0);
	gtk_spin_button_set_value(radar->mdist_spin, 0.0);

	radar->mtime_set = 0;
	radar->mdist_set = 0;

	gtk_combo_box_set_active(radar->maneuver_combo, 0);
	gtk_toggle_button_set_active(radar->mcpa_radio, TRUE);
	gtk_spin_button_set_value(radar->mcpa_spin, 0.0);
	gtk_spin_button_set_value(radar->ncourse_spin, 0.0);
	gtk_spin_button_set_value(radar->nspeed_spin, 0.0);

	gtk_widget_grab_focus(GTK_WIDGET(radar->own_course_spin));

	radar_draw_foreground(radar);

	radar->change_level--;
}

static void
radar_configure_file_chooser(radar_t *radar, GtkFileChooser *chooser,
			     gboolean is_save)
{
	GtkFileFilter *filt1, *filt2;

	gtk_file_chooser_set_local_only(chooser, TRUE);
	gtk_file_chooser_set_select_multiple(chooser, FALSE);

	filt1 = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filt1, "*.rpt");
	gtk_file_filter_set_name(filt1, _("Radarplot Files"));
	gtk_file_chooser_add_filter(chooser, filt1);

	if (!is_save) {
		filt2 = gtk_file_filter_new();
		gtk_file_filter_add_pattern(filt2, "*");
		gtk_file_filter_set_name(filt2, _("All Files"));
		gtk_file_chooser_add_filter(chooser, filt2);
	}

	gtk_file_chooser_set_filter(chooser, filt1);

	if (radar->plot_pathname) {
		gtk_file_chooser_set_current_folder(chooser,
						    radar->plot_pathname);
	}

	if (is_save && radar->plot_filename) {
		gtk_file_chooser_set_current_name(chooser,
						  radar->plot_filename);
	}
}

static void
radar_g_key_file_set_double(GKeyFile *key_file, const gchar *group_name,
		            const gchar *key, gdouble value)
{
	char *p, buffer[32];

	snprintf(buffer, sizeof(buffer), "%.1f", value);

	p = buffer;
	while ((p = strchr(p, ',')))
		*p = '.';

	g_key_file_set_value(key_file, group_name, key, buffer);
}

static gdouble
radar_g_key_file_get_double(GKeyFile *key_file, const gchar *group_name,
		            const gchar *key, GError **error)
{
	gchar *value;
	char *p, *end;
	gdouble d;

	value = g_key_file_get_value(key_file, group_name, key, error);
	if (value == NULL)
		return 0.0;

	d = strtod(value, &end);
	if (value != end && *end == '\0')
		goto out;

	p = value;
	while ((p = strchr(p, ',')))
		*p = '.';

	d = strtod(value, &end);
	if (value != end && *end == '\0')
		goto out;

	p = value;
	while ((p = strchr(p, '.')))
		*p = ',';

	d = strtod(value, &end);
	if (value != end && *end == '\0')
		goto out;

	if (error)
		*error = (GError *)G_KEY_FILE_ERROR_INVALID_VALUE;
	g_free(value);
	return 0.0;

out:
	g_free(value);
	return d;
}

static int
radar_load(radar_t *radar, const char *filename)
{
	char group_name[32];
	GKeyFile *key_file;
	GError *error;
	target_t *s;
	gdouble d;
	gboolean b;
	gint i, j;

	key_file = g_key_file_new();
	if (NULL == key_file)
		return -1;

	radar->change_level++;

	error = NULL;
	if (!g_key_file_load_from_file(key_file, filename, 0, &error)) {
		fprintf(stderr, "%s:%u: g_key_file_load_from_file: %d %s\n",
			__FUNCTION__, __LINE__, error->code, error->message);
		goto out;
	}

	error = NULL;
	i = g_key_file_get_integer(key_file, "Radar", "Orientation", &error);
	if (NULL != error) {
		fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__, __LINE__,
			error->code, error->message);
		goto out;
	}
	gtk_combo_box_set_active(radar->orientation_combo, i);

	i = g_key_file_get_integer(key_file, "Radar", "Range", &error);
	if (NULL != error) {
		fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__, __LINE__,
			error->code, error->message);
		goto out;
	}
	gtk_spin_button_set_value(radar->range_spin, i);

	b = g_key_file_get_boolean(key_file, "Radar", "Heading", &error);
	if (NULL != error) {
		fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__, __LINE__,
			error->code, error->message);
		goto out;
	}
	gtk_toggle_button_set_active(radar->heading_toggle, b);

	i = g_key_file_get_integer(key_file, "Own Ship", "Course", &error);
	if (NULL != error) {
		fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__, __LINE__,
			error->code, error->message);
		goto out;
	}
	gtk_spin_button_set_value(radar->own_course_spin, i);

	d = radar_g_key_file_get_double(key_file, "Own Ship", "Speed", &error);
	if (NULL != error) {
		fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__, __LINE__,
			error->code, error->message);
		goto out;
	}
	gtk_spin_button_set_value(radar->own_speed_spin, d);

	for (j = 0; j < RADAR_NR_TARGETS; j++) {
		s = &radar->target[j];

		snprintf(group_name, sizeof(group_name),
			 "Opponent %c", ((char) (j + 'B')));

		if (!g_key_file_has_group(key_file, group_name)) {
			gtk_spin_button_set_value(s->time_spin[0], 0);
			gtk_spin_button_set_value(s->time_spin[1], 0);
			if (radar->default_rakrp) {
				gtk_toggle_button_set_active(s->rakrp_radio[0], TRUE);
				gtk_spin_button_set_value(s->rasp_course_spin[0], 0);
				gtk_spin_button_set_value(s->rakrp_spin[0], 0);
				gtk_toggle_button_set_active(s->rakrp_radio[1], TRUE);
				gtk_spin_button_set_value(s->rasp_course_spin[1], 0);
				gtk_spin_button_set_value(s->rakrp_spin[1], 0);
			} else {
				gtk_toggle_button_set_active(s->rasp_radio[0], TRUE);
				gtk_spin_button_set_value(s->rasp_spin[0], 0);
				gtk_spin_button_set_value(s->rasp_course_spin[0], 0);
				gtk_toggle_button_set_active(s->rasp_radio[1], TRUE);
				gtk_spin_button_set_value(s->rasp_spin[1], 0);
				gtk_spin_button_set_value(s->rasp_course_spin[1], 0);
			}
			gtk_spin_button_set_value(s->dist_spin[0], 0);
			gtk_spin_button_set_value(s->dist_spin[1], 0);

			continue;
		}

		i = g_key_file_get_integer(key_file, group_name,
					   "Time(0)", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		gtk_spin_button_set_value(s->time_spin[0], i);

		b = g_key_file_get_boolean(key_file, group_name,
					   "SideBearing(0)", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		if (b) {
			gtk_toggle_button_set_active(s->rasp_radio[0], TRUE);

			i = g_key_file_get_integer(key_file, group_name,
						   "RaSP(0)", &error);
			if (NULL != error) {
				fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
					__LINE__, error->code, error->message);
				goto out;
			}
			gtk_spin_button_set_value(s->rasp_spin[0], i);

			i = g_key_file_get_integer(key_file, group_name,
						   "CourseSP(0)", &error);
			if (NULL != error) {
				fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
					__LINE__, error->code, error->message);
				goto out;
			}
			gtk_spin_button_set_value(s->rasp_course_spin[0], i);
		} else {
			gtk_toggle_button_set_active(s->rakrp_radio[0], TRUE);

			i = g_key_file_get_integer(key_file, group_name,
						   "CourseSP(0)", &error);
			if (NULL != error) {
				fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
					__LINE__, error->code, error->message);
				goto out;
			}
			gtk_spin_button_set_value(s->rasp_course_spin[0], i);

			i = g_key_file_get_integer(key_file, group_name,
						   "RaKrP(0)", &error);
			if (NULL != error) {
				fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
					__LINE__, error->code, error->message);
				goto out;
			}
			gtk_spin_button_set_value(s->rakrp_spin[0], i);
		}

		d = radar_g_key_file_get_double(key_file, group_name,
					        "Distance(0)", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		gtk_spin_button_set_value(s->dist_spin[0], d);


		i = g_key_file_get_integer(key_file, group_name,
					   "Time(1)", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		gtk_spin_button_set_value(s->time_spin[1], i);

		b = g_key_file_get_boolean(key_file, group_name,
					   "SideBearing(1)", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		if (b) {
			gtk_toggle_button_set_active(s->rasp_radio[1], TRUE);

			i = g_key_file_get_integer(key_file, group_name,
						   "RaSP(1)", &error);
			if (NULL != error) {
				fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
					__LINE__, error->code, error->message);
				goto out;
			}
			gtk_spin_button_set_value(s->rasp_spin[1], i);

			i = g_key_file_get_integer(key_file, group_name,
						   "CourseSP(1)", &error);
			if (NULL != error) {
				fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
					__LINE__, error->code, error->message);
				goto out;
			}
			gtk_spin_button_set_value(s->rasp_course_spin[1], i);
		} else {
			gtk_toggle_button_set_active(s->rakrp_radio[1], TRUE);

			i = g_key_file_get_integer(key_file, group_name,
						   "CourseSP(1)", &error);
			if (NULL != error) {
				fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
					__LINE__, error->code, error->message);
				goto out;
			}
			gtk_spin_button_set_value(s->rasp_course_spin[1], i);

			i = g_key_file_get_integer(key_file, group_name,
						   "RaKrP(1)", &error);
			if (NULL != error) {
				fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
					__LINE__, error->code, error->message);
				goto out;
			}
			gtk_spin_button_set_value(s->rakrp_spin[1], i);
		}

		d = radar_g_key_file_get_double(key_file, group_name,
					        "Distance(1)", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		gtk_spin_button_set_value(s->dist_spin[1], d);
	}

	i = g_key_file_get_integer(key_file, "Maneuver", "Target", &error);
	if (NULL != error) {
		gtk_combo_box_set_active(radar->target_combo, 0);
		error = NULL;
	} else {
		gtk_combo_box_set_active(radar->target_combo, i);
	}

	b = g_key_file_get_boolean(key_file, "Maneuver", "ByTime", &error);
	if (NULL != error) {
		fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__, __LINE__,
			error->code, error->message);
		goto out;
	}
	if (b) {
		gtk_toggle_button_set_active(radar->mtime_radio, TRUE);

		i = g_key_file_get_integer(key_file, "Maneuver",
					   "Time", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		gtk_spin_button_set_value(radar->mtime_spin, i);
	} else {
		gtk_toggle_button_set_active(radar->mdist_radio, TRUE);

		d = radar_g_key_file_get_double(key_file, "Maneuver",
					        "Distance", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		gtk_spin_button_set_value(radar->mdist_spin, d);
	}

	i = g_key_file_get_integer(key_file, "Maneuver", "Type", &error);
	if (NULL != error) {
		fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__, __LINE__,
			error->code, error->message);
		goto out;
	}
	gtk_combo_box_set_active(radar->maneuver_combo, i);

	b = g_key_file_get_boolean(key_file, "Maneuver", "ByCPA", &error);
	if (NULL != error) {
		fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__, __LINE__,
			error->code, error->message);
		goto out;
	}
	if (b) {
		gtk_toggle_button_set_active(radar->mcpa_radio, TRUE);

		d = radar_g_key_file_get_double(key_file, "Maneuver",
					        "CPA", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		gtk_spin_button_set_value(radar->mcpa_spin, d);
	} else if (0 == i) {
		gtk_toggle_button_set_active(radar->ncourse_radio, TRUE);

		d = radar_g_key_file_get_double(key_file, "Maneuver",
					        "Course", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		gtk_spin_button_set_value(radar->ncourse_spin, d);
	} else {
		gtk_toggle_button_set_active(radar->nspeed_radio, TRUE);

		d = radar_g_key_file_get_double(key_file, "Maneuver",
					        "Speed", &error);
		if (NULL != error) {
			fprintf(stderr, "%s:%u: %d %s\n", __FUNCTION__,
				__LINE__, error->code, error->message);
			goto out;
		}
		gtk_spin_button_set_value(radar->nspeed_spin, d);
	}

	gtk_widget_grab_focus(GTK_WIDGET(radar->own_course_spin));

	radar_draw_foreground(radar);
	radar->change_level--;

	g_key_file_free(key_file);
	return 0;

out:
	radar_draw_foreground(radar);
	radar->change_level--;

	g_key_file_free(key_file);
	return -1;
}

static void
radar_file_open(GtkAction *action, gpointer user_data)
{
	radar_t *radar = user_data;
	GtkWidget *dialog;
	char *pathname;
	char *filename;

	dialog = gtk_file_chooser_dialog_new(_("Load radarplot from file"),
					     GTK_WINDOW(radar->window),
					     GTK_FILE_CHOOSER_ACTION_OPEN,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_CANCEL,
					     GTK_STOCK_OPEN,
					     GTK_RESPONSE_ACCEPT,
					     NULL);
	gtk_window_set_transient_for(GTK_WINDOW(dialog),
				     GTK_WINDOW(radar->window));

	radar_configure_file_chooser(radar, GTK_FILE_CHOOSER(dialog), FALSE);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
		goto out;

	if (radar->plot_filename) {
		g_free(radar->plot_filename);
		radar->plot_filename = NULL;
	}

	pathname = gtk_file_chooser_get_current_folder(
						GTK_FILE_CHOOSER(dialog));
	if (pathname) {
		if (radar->plot_pathname) {
			g_free(radar->plot_pathname);
		}
		radar->plot_pathname = pathname;
	}

	filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

	radar->plot_filename = filename;
	radar_load(radar, radar->plot_filename);

out:
	gtk_widget_destroy(dialog);
}

static int
radar_save(radar_t *radar, const char *filename)
{
	char group_name[32];
	GKeyFile *key_file;
	GError *error;
	gchar *data;
	gsize length;
	target_t *s;
	int fd;
	int i;

	key_file = g_key_file_new();
	if (NULL == key_file)
		return -1;


	g_key_file_set_integer(key_file, "Radar", "Orientation",
		gtk_combo_box_get_active(radar->orientation_combo));
	g_key_file_set_integer(key_file, "Radar", "Range",
		gtk_spin_button_get_value_as_int(radar->range_spin));
	g_key_file_set_boolean(key_file, "Radar", "Heading",
		gtk_toggle_button_get_active(radar->heading_toggle));

	g_key_file_set_integer(key_file, "Own Ship", "Course",
		gtk_spin_button_get_value_as_int(radar->own_course_spin));
	radar_g_key_file_set_double(key_file, "Own Ship", "Speed",
		gtk_spin_button_get_value(radar->own_speed_spin));

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		s = &radar->target[i];

		snprintf(group_name, sizeof(group_name),
			 "Opponent %c", ((char) (i + 'B')));

		g_key_file_set_integer(key_file, group_name,
			"Time(0)", s->time[0]);
		g_key_file_set_boolean(key_file, group_name,
			"SideBearing(0)", s->rasp_selected[0]);
		g_key_file_set_integer(key_file, group_name,
			"RaSP(0)", s->rasp[0]);
		g_key_file_set_integer(key_file, group_name, "CourseSP(0)",
			gtk_spin_button_get_value_as_int(s->rasp_course_spin[0]));
		g_key_file_set_integer(key_file, group_name,
			"RaKrP(0)", s->rakrp[0]);
		radar_g_key_file_set_double(key_file, group_name,
			"Distance(0)", s->distance[0]);

		g_key_file_set_integer(key_file, group_name,
			"Time(1)", s->time[1]);
		g_key_file_set_boolean(key_file, group_name,
			"SideBearing(1)", s->rasp_selected[1]);
		g_key_file_set_integer(key_file, group_name,
			"RaSP(1)", s->rasp[1]);
		g_key_file_set_integer(key_file, group_name, "CourseSP(1)",
			gtk_spin_button_get_value_as_int(s->rasp_course_spin[1]));
		g_key_file_set_integer(key_file, group_name,
			"RaKrP(1)", s->rakrp[1]);
		radar_g_key_file_set_double(key_file, group_name,
			"Distance(1)", s->distance[1]);
	}

	g_key_file_set_integer(key_file, "Maneuver", "Target",
		gtk_combo_box_get_active(radar->target_combo));

	g_key_file_set_boolean(key_file, "Maneuver", "ByTime",
		gtk_toggle_button_get_active(radar->mtime_radio));
	g_key_file_set_integer(key_file, "Maneuver", "Time",
		gtk_spin_button_get_value_as_int(radar->mtime_spin));
	radar_g_key_file_set_double(key_file, "Maneuver", "Distance",
		gtk_spin_button_get_value(radar->mdist_spin));

	g_key_file_set_integer(key_file, "Maneuver", "Type",
		gtk_combo_box_get_active(radar->maneuver_combo));

	g_key_file_set_boolean(key_file, "Maneuver", "ByCPA",
		gtk_toggle_button_get_active(radar->mcpa_radio));
	radar_g_key_file_set_double(key_file, "Maneuver", "CPA",
		gtk_spin_button_get_value(radar->mcpa_spin));
	radar_g_key_file_set_double(key_file, "Maneuver", "Course",
		gtk_spin_button_get_value(radar->ncourse_spin));
	radar_g_key_file_set_double(key_file, "Maneuver", "Speed",
		gtk_spin_button_get_value(radar->nspeed_spin));


	error = NULL;
	data = g_key_file_to_data(key_file, &length, &error);
	if (NULL != error)
		goto out1;

	fd = g_open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (fd < 0)
		goto out2;

	if (write(fd, data, length) != length) {
		close(fd);
		goto out2;
	}

	close(fd);
	g_free(data);
	g_key_file_free(key_file);

	return 0;

out2:
	g_free(data);

out1:
	g_key_file_free(key_file);
	return -1;
}

static void
radar_file_saveas(GtkAction *action, gpointer user_data)
{
	radar_t *radar = user_data;
	GtkWidget *dialog;
	char *pathname;
	char *p, *filename;

	dialog = gtk_file_chooser_dialog_new(_("Save radarplot to file"),
					     GTK_WINDOW(radar->window),
					     GTK_FILE_CHOOSER_ACTION_SAVE,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_CANCEL,
					     GTK_STOCK_SAVE,
					     GTK_RESPONSE_ACCEPT,
					     NULL);
	gtk_window_set_transient_for(GTK_WINDOW(dialog),
				     GTK_WINDOW(radar->window));

	radar_configure_file_chooser(radar, GTK_FILE_CHOOSER(dialog), TRUE);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
		goto out;

	if (radar->plot_filename) {
		g_free(radar->plot_filename);
		radar->plot_filename = NULL;
	}

	pathname = gtk_file_chooser_get_current_folder(
						GTK_FILE_CHOOSER(dialog));
	if (pathname) {
		if (radar->plot_pathname) {
			g_free(radar->plot_pathname);
		}
		radar->plot_pathname = pathname;
	}

	filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

	p = strrchr(filename, '.');
	if (p && !strcasecmp(p, ".rpt")) {
		radar->plot_filename = filename;
	} else {
		radar->plot_filename = malloc(strlen(filename) + 5);
		sprintf(radar->plot_filename, "%s.rpt", filename);
		g_free(filename);
	}

	radar_save(radar, radar->plot_filename);

out:
	gtk_widget_destroy(dialog);
}

static void
radar_file_save(GtkAction *action, gpointer user_data)
{
	radar_t *radar = user_data;

	if (NULL == radar->plot_filename) {
		radar_file_saveas(action, user_data);
		return;
	}

	radar_save(radar, radar->plot_filename);
}

static void
radar_heading(GtkToggleAction *action, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->default_heading = gtk_toggle_action_get_active(action);

	g_key_file_set_boolean(radar->key_file,
			       "Radarplot", "ShowHeading",
			       radar->default_heading);
	radar_save_config(radar, ".radarplot");
}

static void
radar_bearing(GtkToggleAction *action, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->default_rakrp = gtk_toggle_action_get_active(action);

	g_key_file_set_boolean(radar->key_file,
			       "Radarplot", "DefaultRaKrP",
			       radar->default_rakrp);
	radar_save_config(radar, ".radarplot");
}

static void
radar_render(GtkToggleAction *action, gpointer user_data)
{
	radar_t *radar = user_data;
	GdkEvent *event = gdk_event_new(GDK_CONFIGURE);

	radar->do_render = gtk_toggle_action_get_active(action);

	event->configure.window = g_object_ref(radar->canvas->window);
	event->configure.send_event = TRUE;
	event->configure.x = radar->canvas->allocation.x;
	event->configure.y = radar->canvas->allocation.y;
	event->configure.width = radar->canvas->allocation.width;
	event->configure.height = radar->canvas->allocation.height;

	g_key_file_set_boolean(radar->key_file,
			       "Radarplot", "Antialias",
			       radar->do_render);
	radar_save_config(radar, ".radarplot");

	gtk_widget_event(radar->canvas, event);
	gdk_event_free(event);
}

static GtkActionEntry ui_entries[] =
{
	{ "FileMenu",			NULL,
	  N_("_File")
	},

		{ "New",		GTK_STOCK_NEW,
		  N_("_New"),		"<control>N",
		  N_("Start new radarplot"),
		  G_CALLBACK(radar_new)
		},

		{ "Open",		GTK_STOCK_OPEN,
		  N_("_Open..."),	"<control>O",
		  N_("Load radarplot from file"),
		  G_CALLBACK(radar_file_open)
		},
		{ "Save",		GTK_STOCK_SAVE,
		  N_("_Save..."),	"<control>S",
		  N_("Save radarplot to file"),
		  G_CALLBACK(radar_file_save)
		},
		{ "SaveAs",		GTK_STOCK_SAVE_AS,
		  N_("Save _as..."),	NULL,
		  N_("Save radarplot to file"),
		  G_CALLBACK(radar_file_saveas)
		},

		{ "ExportMenu",		GTK_STOCK_CONVERT,
		  N_("E_xport as")
		},

			{ "JPEG",		NULL,
			  N_("_JPEG Image"),	NULL,
			  N_("Save radarplot as JPEG image"),
			  G_CALLBACK(radar_export_jpeg)
			},
			{ "PNG",		NULL,
			  N_("_PNG Image"),	NULL,
			  N_("Save radarplot as PNG image"),
			  G_CALLBACK(radar_export_png)
			},

		{ "PrintAs",		GTK_STOCK_PRINT,
		  N_("_Print as PDF"),	"<control>P",
		  N_("Print radarplot to PDF file"),
		  G_CALLBACK(radar_export_pdf)
		},

		{ "Exit",		GTK_STOCK_QUIT,
		  N_("_Exit"),		"<control>E",
		  N_("Exit from radarplot program"),
		  G_CALLBACK(radar_quit)
		},

	{ "PreferencesMenu",		NULL,
	  N_("_Preferences")
	},

	{ "ExtraMenu",			NULL,
	  N_("E_xtra")
	},

		{ "Register",			NULL,
		  N_("_Register"),		NULL,
		  N_("Register radarplot program"),
		  G_CALLBACK(radar_register)
		},
		{ "License",			NULL,
		  N_("Configure _License"),	NULL,
		  N_("Configure radarplot license file"),
		  G_CALLBACK(radar_license)
		},

	{ "HelpMenu",			NULL,
	  N_("_Help")
	},

		{ "About",			GTK_STOCK_ABOUT,
		  N_("_About Radarplot"),	NULL,
		  N_("Information about radarplot program"),
		  G_CALLBACK(radar_about)
		},
};
static guint n_entries = G_N_ELEMENTS(ui_entries);

static GtkToggleActionEntry ui_toggle_entries[] =
{
	{ "Heading",			NULL,
	  N_("Show Heading Line"),	NULL,
	  N_("Display Heading Line in radarplot"),
	  G_CALLBACK(radar_heading),
	  FALSE },

	{ "Bearing",			NULL,
	  N_("True Bearings"),		NULL,
	  N_("Default entries are True Bearings"),
	  G_CALLBACK(radar_bearing),
	  FALSE },

	{ "Render",			NULL,
	  N_("Antialias"),		NULL,
	  N_("Draw vectors using antialiasing filter"),
	  G_CALLBACK(radar_render),
	  TRUE },
};
static guint n_toggle_entries = G_N_ELEMENTS(ui_toggle_entries);


static const gchar *ui_info =
"<ui>"
"  <menubar name='MenuBar'>"
"    <menu action='FileMenu'>"
"      <menuitem action='New'/>"
"      <separator/>"
"      <menuitem action='Open'/>"
"      <menuitem action='Save'/>"
"      <menuitem action='SaveAs'/>"
"      <separator/>"
"      <menu action='ExportMenu'>"
"        <menuitem action='JPEG'/>"
"        <menuitem action='PNG'/>"
"      </menu>"
"      <separator/>"
"      <menuitem action='PrintAs'/>"
"      <separator/>"
"      <menuitem action='Exit'/>"
"    </menu>"
"    <menu action='PreferencesMenu'>"
"      <menuitem action='Heading'/>"
"      <menuitem action='Bearing'/>"
"      <menuitem action='Render'/>"
"    </menu>"
"    <menu action='ExtraMenu'>"
"      <menuitem action='Register'/>"
"      <menuitem action='License'/>"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <menuitem action='About'/>"
"    </menu>"
"  </menubar>"
"</ui>";


static void radar_init_private_data(radar_t *radar);

static void
radar_sincos(radar_t *radar, double a, double *sina, double *cosa)
{
	if (radar->north_up) {
		*sina = sin(M_PI * a / 180.0);
		*cosa = cos(M_PI * a / 180.0);
	} else {
		*sina = sin(M_PI * (a - ((double) radar->own_course)) / 180.0);
		*cosa = cos(M_PI * (a - ((double) radar->own_course)) / 180.0);
	}
}

static int
d2i(double x)
{
	return (int) floor(x + 0.5);
}

static double
i2d(int x)
{
	return (double) x;
}

static void
outer_sincos(radar_t *radar, double a, double *sina, double *cosa)
{
	*sina = sin(M_PI * a / 180.0);
	*cosa = cos(M_PI * a / 180.0);
}

static int
radar_compare_points(const void *ap, const void *bp)
{
	const point_t *a = ap, *b = bp;

	if (a->y < b->y)
		return -1;
	if (a->y > b->y)
		return 1;
	if (a->x < b->x)
		return -1;
	if (a->x > b->x)
		return 1;
	return 0;
}

static double
radar_compute_x(point_t *p1, point_t *p2, double y)
{
	double dy;

	dy = p2->y - p1->y;
	if (dy == 0.0)
		return p1->x;

	return p1->x + (y - p1->y) * (p2->x - p1->x) / dy;
}

static double
radar_compute_y(point_t *p1, point_t *p2, double x)
{
	double dx;

	dx = p2->x - p1->x;
	if (dx == 0.0)
		return p1->y;

	return p1->y + (x - p1->x) * (p2->y - p1->y) / dx;
}

static gboolean
radar_clip_vector(double x_min, double y_min, double x_max, double y_max,
		  double *x1, double *y1, double *x2, double *y2)
{
	point_t p[2];
	int i, clip = 0;

	p[0].x = *x1;
	p[0].y = *y1;
	p[1].x = *x2;
	p[1].y = *y2;

	if ((p[0].x < x_min && p[1].x < x_min) ||
	    (p[0].x > x_max && p[1].x > x_max) ||
	    (p[0].y < y_min && p[1].y < y_min) ||
	    (p[0].y > y_max && p[1].y > y_max))
		return FALSE;

	for (i = 0; i < 2; i++) {
		if (p[i].x < x_min) {
			p[i].y = radar_compute_y(&p[0], &p[1], x_min);
			p[i].x = x_min;
			clip++;
		} else if (p[i].x > x_max) {
			p[i].y = radar_compute_y(&p[0], &p[1], x_max);
			p[i].x = x_max;
			clip++;
		}

		if (p[i].y < y_min) {
			p[i].x = radar_compute_x(&p[0], &p[1], y_min);
			p[i].y = y_min;
			clip++;
		} else if (p[i].y > y_max) {
			p[i].x = radar_compute_x(&p[0], &p[1], y_max);
			p[i].y = y_max;
			clip++;
		}
	}

	if (clip) {
		*x1 = p[0].x;
		*y1 = p[0].y;
		*x2 = p[1].x;
		*y2 = p[1].y;
	}

	return TRUE;
}

static int
radar_add_trap_from_points(GList **traps, double top, double bottom,
			   double top_left_x, double bottom_left_x,
			   double top_right_x, double bottom_right_x)
{
	trapezoid_t *trap;

	if (top == bottom)
		return 0;

	trap = malloc(sizeof(trapezoid_t));
	if (NULL == trap) {
		printf("%s:%u: malloc(trapezoid_t) failed\n",
		       __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	trap->y1 = top;
	trap->y2 = bottom;

	if (top_left_x > top_right_x) {
		trap->x11 = top_right_x;
		trap->x21 = top_left_x;
	} else {
		trap->x11 = top_left_x;
		trap->x21 = top_right_x;
	}

	if (bottom_left_x > bottom_right_x) {
		trap->x12 = bottom_right_x;
		trap->x22 = bottom_left_x;
	} else {
		trap->x12 = bottom_left_x;
		trap->x22 = bottom_right_x;
	}

	*traps = g_list_append(*traps, trap);
	if (NULL == *traps) {
		printf("%s:%u: g_list_append() failed\n",
		       __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	return 0;
}

static int
radar_tessellate_triangle(point_t *points, GList **traps)
{
	point_t tsort[3];
	double intersect;
	int err;

	memcpy(tsort, points, 3 * sizeof(point_t));
	qsort(tsort, 3, sizeof(point_t), radar_compare_points);

	intersect = radar_compute_x(&tsort[0], &tsort[2], tsort[1].y);

	err = radar_add_trap_from_points(traps,
				         tsort[0].y, tsort[1].y,
				         tsort[0].x, intersect,
				         tsort[0].x, tsort[1].x);
	if (err < 0)
		return err;
	err = radar_add_trap_from_points(traps,
				         tsort[1].y, tsort[2].y,
				         intersect, tsort[2].x,
				         tsort[1].x, tsort[2].x);
	if (err < 0)
		return err;

	return 0;
}

static int
radar_tessellate_rectangle(point_t *points, GList **traps)
{
	point_t tsort[4];
	double isec02, isec13;
	int err;

	memcpy(tsort, points, 4 * sizeof(point_t));
	qsort(tsort, 4, sizeof(point_t), radar_compare_points);

	isec02 = radar_compute_x(&tsort[0], &tsort[2], tsort[1].y);
	isec13 = radar_compute_x(&tsort[1], &tsort[3], tsort[2].y);

	err = radar_add_trap_from_points(traps,
				         tsort[0].y, tsort[1].y,
				         tsort[0].x, isec02,
				         tsort[0].x, tsort[1].x);
	if (err < 0)
		return err;
	err = radar_add_trap_from_points(traps,
				         tsort[1].y, tsort[2].y,
				         isec02, tsort[2].x,
				         tsort[1].x, isec13);
	if (err < 0)
		return err;
	err = radar_add_trap_from_points(traps,
				         tsort[2].y, tsort[3].y,
				         tsort[2].x, tsort[3].x,
				         isec13, tsort[3].x);
	if (err < 0)
		return err;

	return 0;
}

static int
radar_tessellate_line(double x1, double y1, double x2, double y2,
		      double width, GList **traps)
{
	double alpha, sina, cosa;
	double halfwidth;
	point_t points[4];
	int err;

	halfwidth = width / 2.0;

	alpha = atan2(y2 - y1, x2 - x1);
	sina = sin(alpha);
	cosa = cos(alpha);

	points[0].x = x1 - halfwidth * sina;
	points[0].y = y1 + halfwidth * cosa;
	points[1].x = x1 + halfwidth * sina;
	points[1].y = y1 - halfwidth * cosa;
	points[2].x = x2 - halfwidth * sina;
	points[2].y = y2 + halfwidth * cosa;
	points[3].x = x2 + halfwidth * sina;
	points[3].y = y2 - halfwidth * cosa;

	err = radar_tessellate_rectangle(points, traps);
	if (err < 0) {
		printf("%s:%u: error %d: %s\n", __FUNCTION__, __LINE__,
		       err, strerror(-err));
		return err;
	}

	return 0;
}

static void
radar_gdk_draw_trapezoids(GdkDrawable *drawable, GdkGC *gc,
			  GdkTrapezoid *trapezoids, gint n_trapezoids)
{
#ifdef USE_GDK_DRAW_TRAPEZOIDS_FIXUP
	GdkGCValues values;
	GdkColormap *cmap;
	GdkColor color;
	cairo_t *cr;
	int i;

	gdk_gc_get_values(gc, &values);
	color.pixel = values.foreground.pixel;

	cmap = gdk_gc_get_colormap(gc);
	if (cmap)
		gdk_colormap_query_color(cmap, color.pixel, &color);
	else
		g_warning("No colormap in radar_gdk_draw_trapezoids");

	cr = gdk_cairo_create(drawable);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	gdk_cairo_set_source_color(cr, &color);
	cairo_set_line_width(cr, values.line_width);

	for (i = 0; i < n_trapezoids; i++) {
		cairo_move_to(cr, trapezoids[i].x11, trapezoids[i].y1);
		cairo_line_to(cr, trapezoids[i].x21, trapezoids[i].y1);
		cairo_line_to(cr, trapezoids[i].x22, trapezoids[i].y2);
		cairo_line_to(cr, trapezoids[i].x12, trapezoids[i].y2);
		cairo_close_path(cr);
	}

	cairo_fill(cr);
	cairo_destroy(cr);
#else
	gdk_draw_trapezoids(drawable, gc, trapezoids, n_trapezoids);
#endif /* USE_GDK_DRAW_TRAPEZOIDS_FIXUP */
}

static void
radar_draw_traps(radar_t *radar, GdkDrawable *drawable, GdkGC *gc,
		 GdkPixbuf *pixbuf, guchar alpha, GList **traps)
{
	int n = g_list_length(*traps);
	GdkTrapezoid trapezoids[n];
	trapezoid_t *trap;
	GList *p;
	int i;

	if (NULL == *traps)
		return;

	p = *traps;
	for (i = 0; i < n; i++) {
		trap = p->data;

		trapezoids[i].y1 = trap->y1;
		trapezoids[i].x11 = trap->x11;
		trapezoids[i].x21 = trap->x21;
		trapezoids[i].y2 = trap->y2;
		trapezoids[i].x12 = trap->x12;
		trapezoids[i].x22 = trap->x22;

		p = p->next;
	}

	radar_gdk_draw_trapezoids(drawable, gc, trapezoids, n);
}

static void
radar_draw_layout(radar_t *radar, GdkDrawable *drawable,
		  GdkGC *fg_gc, GdkGC *bg_gc,
		  GdkPixbuf *pixbuf, guchar alpha, gboolean render,
		  double x, double y, int width, int height,
		  PangoLayout *layout)
{
	GdkPixmap *testmap, *pixmap;
	GdkPixbuf *testbuf;
	GdkGCValues values;
	GdkColormap *cmap;
	GdkColor gdk_color;
	guchar *data, *p;
	guchar r, g, b;
	guint stride;
	int row, col;

	gdk_gc_get_values(bg_gc, &values);
	gdk_color.pixel = values.foreground.pixel;

	cmap = gdk_gc_get_colormap(bg_gc);
	if (cmap) {
		gdk_colormap_query_color(cmap, gdk_color.pixel,
					 &gdk_color);
	} else {
		printf("%s:%u: gdk_gc_get_colormap(%p) failed\n",
		       __FUNCTION__, __LINE__, bg_gc);
		return;
	}

	r = gdk_color.red >> 8;
	g = gdk_color.green >> 8;
	b = gdk_color.blue >> 8;


	testmap = gdk_pixmap_new(drawable, width + 4, height + 4, -1);
	if (NULL == testmap) {
		printf("%s:%u: gdk_pixmap_new(%u, %u) failed\n",
		       __FUNCTION__, __LINE__, width + 4, height + 4);
		return;
	}
	gdk_draw_rectangle(testmap, bg_gc, TRUE,
			   0, 0, width + 4, height + 4);
	gdk_draw_layout(testmap, fg_gc, 2, 2, layout);


	pixmap = gdk_pixmap_new(drawable, width + 4, height + 4, -1);
	if (NULL == pixmap) {
		printf("%s:%u: gdk_pixmap_new(%u, %u) failed\n",
		       __FUNCTION__, __LINE__, width + 4, height + 4);
		g_object_unref(testmap);
		return;
	}
	if (pixbuf && render) {
		gdk_draw_pixbuf(pixmap, fg_gc, pixbuf,
				d2i(x) - 2, d2i(y) - 2,
				0, 0, width + 4, height + 4,
				GDK_RGB_DITHER_NORMAL, 0, 0);
	} else {
		gdk_draw_drawable(pixmap, fg_gc, drawable,
				  d2i(x) - 2, d2i(y) - 2,
				  0, 0, width + 4, height + 4);
	}

	testbuf = gdk_pixbuf_get_from_drawable(NULL, testmap, cmap, 0, 0,
					       0, 0, width + 4, height + 4);
	if (NULL == testbuf) {
		printf("%s:%u: gdk_pixbuf_get_from_drawable(%u, %u) failed\n",
		       __FUNCTION__, __LINE__, width + 4, height + 4);
		g_object_unref(pixmap);
		g_object_unref(testmap);
		return;
	}

	data = gdk_pixbuf_get_pixels(testbuf);
	stride = gdk_pixbuf_get_rowstride(testbuf);
	for (row = 0; row < height + 4; row++) {
		p = data;

		for (col = 0; col < width + 4; col++, p += 3) {
			if (p[0] == r && p[1] == g && p[2] == b)
				continue;

			gdk_draw_rectangle(pixmap, bg_gc, TRUE,
					   col - 1, row - 1, 3, 3);
		}

		data += stride;
	}

	gdk_draw_layout(pixmap, fg_gc, 2, 2, layout);

	if (pixbuf && render) {
		gdk_pixbuf_get_from_drawable(pixbuf, pixmap, cmap,
					     0, 0, d2i(x) - 2, d2i(y) - 2,
					     width + 4, height + 4);
	} else {
		gdk_draw_drawable(drawable, fg_gc, pixmap,
				  0, 0, d2i(x) - 2, d2i(y) - 2,
				  width + 4, height + 4);
	}

	g_object_unref(testbuf);
	g_object_unref(pixmap);
	g_object_unref(testmap);
}

void
radar_draw_bg_pixmap(radar_t *radar, GdkDrawable *drawable,
		     GdkPixbuf *pixbuf, guchar alpha,
		     PangoLayout *layout, gboolean render,
		     double cx, double cy, int step, int radius,
		     int width, int height)
{
	int tw, th, a;
	double ox, oy, sina, cosa;
	double r, ri, x, y;
	segment_t segs[360];
//	text_label_t label;
	char text[16];
	GdkGC *gc;
	arc_t arc;
	int tick;
	int i, n;

	if (pixbuf && render) {
		gdk_pixbuf_fill(pixbuf, 0xffffff00 | alpha);
	} else {
		gdk_draw_rectangle(drawable, radar->white_gc,
				   TRUE, 0, 0, width, height);
	}

	tick = step / 12;
	if (tick < 4)
		tick = 4;

	for (a = 0; a < 6; a++) {
		r = i2d((a + 1) * step) - i2d(step) / 2.0;

		arc.gc = radar->grey25_gc;
		arc.x = cx;
		arc.y = cy;
		arc.radius = r;
		arc.angle1 = 0.0;
		arc.angle2 = 360.0;

		radar_draw_arc(radar, drawable, pixbuf, alpha, render, &arc);
	}

	n = 0;
	for (a = 10; a < 360; a += 10) {
		if ((a % 30) == 0)
			continue;

		outer_sincos(radar, a, &sina, &cosa);

		if (render) {
			segs[n].x1 = cx + 3.0 * i2d(step) / 4.0 * sina;
			segs[n].y1 = cy - 3.0 * i2d(step) / 4.0 * cosa;
		} else {
			segs[n].x1 = cx;
			segs[n].y1 = cy;
		}
		segs[n].x2 = cx + i2d(radius) * sina;
		segs[n].y2 = cy - i2d(radius) * cosa;
		n++;
	}
	gc = render ? radar->grey25_gc : radar->grey25_clip2_gc;
	radar_draw_segments(radar, drawable, gc, pixbuf, alpha,
			    render, segs, n);

	n = 0;
	for (a = 0; a < 360; a += 90) {
		outer_sincos(radar, a, &sina, &cosa);

		segs[n].x1 = cx;
		segs[n].y1 = cy;
		segs[n].x2 = cx + i2d(radius) * sina;
		segs[n].y2 = cy - i2d(radius) * cosa;
		n++;
	}
	radar_draw_segments(radar, drawable, radar->grey50_gc,
			    pixbuf, alpha, render, segs, n);

	n = 0;
	for (a = 30; a < 360; a += 30) {
		if ((a % 90) == 0)
			continue;

		outer_sincos(radar, a, &sina, &cosa);

		if (render) {
			segs[n].x1 = cx + i2d(step) / 4.0 * sina;
			segs[n].y1 = cy - i2d(step) / 4.0 * cosa;
		} else {
			segs[n].x1 = cx;
			segs[n].y1 = cy;
		}
		segs[n].x2 = cx + i2d(radius) * sina;
		segs[n].y2 = cy - i2d(radius) * cosa;
		n++;
	}
	gc = render ? radar->grey50_gc : radar->grey50_clip3_gc;
	radar_draw_segments(radar, drawable, gc, pixbuf, alpha,
			    render, segs, n);

	n = 0;
	for (a = 5; a < 360; a += 5) {
		outer_sincos(radar, a, &sina, &cosa);

		if (render) {
			segs[n].x1 = cx + i2d(radius - 2 * tick) * sina;
			segs[n].y1 = cy - i2d(radius - 2 * tick) * cosa;
		} else {
			segs[n].x1 = cx;
			segs[n].y1 = cy;
		}
		segs[n].x2 = cx + i2d(radius) * sina;
		segs[n].y2 = cy - i2d(radius) * cosa;
		n++;
	}
	gc = render ? radar->grey50_gc : radar->grey50_clip1_gc;
	radar_draw_segments(radar, drawable, gc, pixbuf, alpha,
			    render, segs, n);

	n = 0;
	for (a = 1; a < 360; a++) {
		if ((a % 5) == 0)
			continue;

		outer_sincos(radar, a, &sina, &cosa);

		if (render) {
			segs[n].x1 = cx + i2d(radius - tick) * sina;
			segs[n].y1 = cy - i2d(radius - tick) * cosa;
		} else {
			segs[n].x1 = cx;
			segs[n].y1 = cy;
		}
		segs[n].x2 = cx + i2d(radius) * sina;
		segs[n].y2 = cy - i2d(radius) * cosa;
		n++;
	}
	gc = render ? radar->grey50_gc : radar->grey50_clip0_gc;
	radar_draw_segments(radar, drawable, gc, pixbuf, alpha,
			    render, segs, n);

	for (a = 0; a < 6; a++) {
		r = i2d((a + 1) * step);

		arc.gc = radar->grey50_gc;
		arc.x = cx;
		arc.y = cy;
		arc.radius = r;
		arc.angle1 = 0;
		arc.angle2 = 360;

		radar_draw_arc(radar, drawable, pixbuf, alpha, render, &arc);
	}

	for (a = 0; a < 360; a += 10) {
		snprintf(text, sizeof(text), "%03u", a);

		pango_layout_set_text(layout, text, strlen(text));
		pango_layout_get_pixel_size(layout, &tw, &th);

		outer_sincos(radar, a, &sina, &cosa);

		ox = i2d(tw + BG_TEXT_SIDE_SPACING) * sina / 2.0;
		oy = i2d(th) * cosa;

		x = cx + (radius * sina - i2d(tw) / 2.0 + ox);
		y = cy - (radius * cosa + i2d(th) / 2.0 + oy);

		radar_draw_layout(radar, drawable, radar->black_gc,
				  radar->white_gc, pixbuf, alpha, render,
				  x, y, tw, th, layout);
	}

	for (a = 0; a < 360; a += 90) {
		outer_sincos(radar, a, &sina, &cosa);

		for (i = 1; i < 6; i++) {
			if (radar_ranges[radar->rindex].marks == 3) {
				if (i % 2)
					continue;
			}

			if (radar->range > 3.0)
				snprintf(text, sizeof(text), "%u",
					d2i(i2d(i) * radar->range) / 6);
			else
				snprintf(text, sizeof(text), "%.*f",
					radar_ranges[radar->rindex].digits,
					(i2d(i) * radar->range) / 6.0);

			pango_layout_set_text(layout, text, strlen(text));
			pango_layout_get_pixel_size(layout, &tw, &th);

			ri = i2d(i * step);

			x = cx + (ri * sina - i2d(tw) / 2.0);
			y = cy - (ri * cosa + i2d(th) / 2.0);

			radar_draw_layout(radar, drawable,
					  radar->grey50_gc, radar->white_gc,
					  pixbuf, alpha, render,
					  x, y, tw, th, layout);
		}
	}
}

static void
radar_draw_background(radar_t *radar)
{
	vector_t *v;
	int i;

	if (!radar->mapped || radar->wait_expose) {
		radar->redraw_pending = TRUE;
		return;
	}

	for (i = 0; i < RADAR_NR_VECTORS; i++) {
		v = &radar->vectors[i];
		v->is_visible = 0;
	}

	radar_draw_bg_pixmap(radar, radar->pixmap, radar->backbuf, 0xff,
			     radar->layout, radar->do_render,
			     radar->cx, radar->cy, radar->step, radar->r,
			     radar->w, radar->h);

	if (radar->backbuf && radar->do_render) {
		gdk_draw_rgb_32_image(radar->pixmap, radar->white_gc,
				      0, 0, radar->w, radar->h,
				      GDK_RGB_DITHER_NORMAL,
				      gdk_pixbuf_get_pixels(radar->backbuf),
				      gdk_pixbuf_get_rowstride(radar->backbuf));
	}

	radar->wait_expose = TRUE;
	gtk_widget_queue_draw_area(radar->canvas, 0, 0, radar->w, radar->h);
}

static void
radar_set_vector(radar_t *radar, vector_t *v, GdkGC *gc,
		 double x1, double y1, double x2, double y2)
{
	v->is_visible = 1;
	v->gc = gc;

	v->x1 = x1;
	v->y1 = y1;
	v->x2 = x2;
	v->y2 = y2;

	if (x1 < x2) {
		v->bbox.x = d2i(x1) - 2;
		v->bbox.width = d2i(x2 - x1) + 5;
	} else {
		v->bbox.x = d2i(x2) - 2;
		v->bbox.width = d2i(x1 - x2) + 5;
	}
	if (y1 < y2) {
		v->bbox.y = d2i(y1) - 2;
		v->bbox.height = d2i(y2 - y1) + 5;
	} else {
		v->bbox.y = d2i(y2) - 2;
		v->bbox.height = d2i(y1 - y2) + 5;
	}

	gdk_window_invalidate_rect(radar->canvas->window, &v->bbox, FALSE);
}

static void
radar_set_arc(radar_t *radar, arc_t *a, GdkGC *gc,
	      double x, double y, double radius,
	      double angle1, double angle2)
{
	a->is_visible = 1;
	a->gc = gc;

	a->x = x;
	a->y = y;
	a->radius = radius;
	a->angle1 = angle1;
	a->angle2 = angle2;

	a->bbox.x = d2i(x - radius) - 2;
	a->bbox.y = d2i(y - radius) - 2;
	a->bbox.width = d2i(2.0 * radius) + 4;
	a->bbox.height = d2i(2.0 * radius) + 4;

	gdk_window_invalidate_rect(radar->canvas->window, &a->bbox, FALSE);
}

void
radar_draw_arc(radar_t *radar, GdkDrawable *drawable,
	       GdkPixbuf *pixbuf, guchar alpha, gboolean render, arc_t *a)
{
	GdkGCValues values;
	GList *traps = NULL;
	double xc, yc, radius;
	double start, delta;
	double angle, sign, inc;
	double sina, cosa;
	point_t points[4];
	double halfwidth;
	int err;

	if (render) {
		gdk_gc_get_values(a->gc, &values);
		if (values.line_width == 0)
			halfwidth = 0.5;
		else
			halfwidth = i2d(values.line_width) / 2.0;

		xc = a->x;
		yc = a->y;
		radius = a->radius;

		start = M_PI * a->angle1 / 180.0;
		delta = M_PI * a->angle2 / 180.0;
		if (delta < 0) {
			sign = -1.0;
			delta = -delta;
		} else {
			sign = 1.0;
		}

		if (radius < 7.0) {
			inc = M_PI / 8.0;
		} else {
			inc = asin(2.5 / radius);
		}

		cosa = cos(start);
		sina = sin(start);
		points[2].x = xc + (radius + halfwidth) * cosa;
		points[2].y = yc - (radius + halfwidth) * sina;
		points[3].x = xc + (radius - halfwidth) * cosa;
		points[3].y = yc - (radius - halfwidth) * sina;

		for (angle = inc; angle < delta; angle += inc) {
			cosa = cos(start + sign * angle);
			sina = sin(start + sign * angle);

			points[0] = points[2];
			points[1] = points[3];
			points[2].x = xc + (radius + halfwidth) * cosa;
			points[2].y = yc - (radius + halfwidth) * sina;
			points[3].x = xc + (radius - halfwidth) * cosa;
			points[3].y = yc - (radius - halfwidth) * sina;

			err = radar_tessellate_rectangle(points, &traps);
			if (err < 0) {
				printf("%s:%u: error %d: %s\n",
				       __FUNCTION__, __LINE__,
				       err, strerror(-err));
				return;
			}
		}
		cosa = cos(start + sign * delta);
		sina = sin(start + sign * delta);

		points[0] = points[2];
		points[1] = points[3];
		points[2].x = xc + (radius + halfwidth) * cosa;
		points[2].y = yc - (radius + halfwidth) * sina;
		points[3].x = xc + (radius - halfwidth) * cosa;
		points[3].y = yc - (radius - halfwidth) * sina;

		err = radar_tessellate_rectangle(points, &traps);
		if (err < 0) {
			printf("%s:%u: error %d: %s\n", __FUNCTION__, __LINE__,
			       err, strerror(-err));
			return;
		}

		radar_draw_traps(radar, drawable, a->gc, pixbuf, alpha, &traps);
		g_list_foreach(traps, (GFunc) free, NULL);
		g_list_free(traps);
	} else {
		gdk_draw_arc(drawable, a->gc, FALSE,
			     d2i(a->x - a->radius), d2i(a->y - a->radius),
			     d2i(2.0 * a->radius), d2i(2.0 * a->radius),
			     d2i(a->angle1 * 64.0),
			     d2i(a->angle2 * 64.0));
	}

#ifdef DEBUG_BBOX
	gdk_draw_rectangle(radar->canvas->window, radar->black_gc,
			   FALSE, a->bbox.x, a->bbox.y,
			   a->bbox.width - 1, a->bbox.height - 1);
#endif
}

void
radar_draw_poly(radar_t *radar, GdkDrawable *drawable,
		GdkPixbuf *pixbuf, guchar alpha, gboolean render, poly_t *p)
{
	GdkPoint points[p->npoints];
	int i;
	GList *traps = NULL;
	int err;

	if (render) {
		err = radar_tessellate_triangle(p->points, &traps);
		if (err < 0) {
			printf("%s:%u: error %d: %s\n", __FUNCTION__, __LINE__,
			       err, strerror(-err));
			return;
		}

		radar_draw_traps(radar, drawable, p->gc, pixbuf, alpha, &traps);
		g_list_foreach(traps, (GFunc) free, NULL);
		g_list_free(traps);
	} else {
		for (i = 0; i < p->npoints; i++) {
			points[i].x = d2i(p->points[i].x);
			points[i].y = d2i(p->points[i].y);
		}

		gdk_draw_polygon(drawable, p->gc, TRUE,
				 points, p->npoints);
		gdk_draw_polygon(drawable, p->gc, FALSE,
				 points, p->npoints);
	}

#ifdef DEBUG_BBOX
	gdk_draw_rectangle(radar->canvas->window, radar->black_gc,
			   FALSE, p->bbox.x, p->bbox.y,
			   p->bbox.width - 1, p->bbox.height - 1);
#endif
}

void
radar_draw_vector(radar_t *radar, GdkDrawable *drawable, GdkPixbuf *pixbuf,
		  guchar alpha, gboolean render, vector_t *v)
{
	double dashes[2] = { 3.0, 3.0 };
	GdkGCValues values;
	GList *traps = NULL;
	double width, dx, dy, a, sina, cosa;
	double l, r1, r2, x1, y1, x2, y2;
	double vx1, vy1, vx2, vy2;
	int w, h;
	int err;

	if (render) {
		gdk_gc_get_values(v->gc, &values);
		if (values.line_width == 0)
			width = 1.0;
		else
			width = i2d(values.line_width);

		vx1 = v->x1;
		vy1 = v->y1;
		vx2 = v->x2;
		vy2 = v->y2;

/*
 * Pre-clip vector here...
 */
		if (pixbuf) {
			w = gdk_pixbuf_get_width(pixbuf);
			h = gdk_pixbuf_get_height(pixbuf);
		} else {
			gdk_drawable_get_size(drawable, &w, &h);
		}

		if (!radar_clip_vector(-1.0, -1.0, i2d(w + 1), i2d(h + 1),
				       &vx1, &vy1, &vx2, &vy2)) {
			return;
		}

		if (values.line_style == GDK_LINE_ON_OFF_DASH) {
			dx = vx2 - vx1;
			dy = vy2 - vy1;
			a = atan2(dy, dx);
			sina = sin(a);
			cosa = cos(a);
			l = sqrt(dx * dx + dy * dy);

			for (r1 = 0.0; r1 <= l; r1 += dashes[1]) {
				r2 = r1 + dashes[0];
				if (r2 > l)
					r2 = l;

				x1 = vx1 + r1 * cosa;
				y1 = vy1 + r1 * sina;
				x2 = vx1 + r2 * cosa;
				y2 = vy1 + r2 * sina;
				err = radar_tessellate_line(x1, y1, x2, y2,
							    width, &traps);

				r1 += dashes[0];
			}
		} else {
			err = radar_tessellate_line(vx1, vy1, vx2, vy2,
						    width, &traps);
			if (err < 0) {
				printf("%s:%u: error %d: %s\n",
				       __FUNCTION__, __LINE__,
				       err, strerror(-err));
				return;
			}
		}

		radar_draw_traps(radar, drawable, v->gc, pixbuf, alpha, &traps);
		g_list_foreach(traps, (GFunc) free, NULL);
		g_list_free(traps);
	} else {
		gdk_draw_line(radar->canvas->window, v->gc,
			      d2i(v->x1), d2i(v->y1),
			      d2i(v->x2), d2i(v->y2));
	}

#ifdef DEBUG_BBOX
	gdk_draw_rectangle(radar->canvas->window, radar->black_gc,
			   FALSE, v->bbox.x, v->bbox.y,
			   v->bbox.width - 1, v->bbox.height - 1);
#endif
}

static void
radar_draw_segments(radar_t *radar, GdkDrawable *drawable, GdkGC *gc,
		    GdkPixbuf *pixbuf, guchar alpha, gboolean render,
		    segment_t *segs, int nsegs)
{
	GdkGCValues values;
	GList *traps = NULL;
	double width;
	int i;

	if (render) {
		gdk_gc_get_values(gc, &values);
		if (values.line_width == 0)
			width = 1.0;
		else
			width = i2d(values.line_width);

		for (i = 0; i < nsegs; i++) {
			radar_tessellate_line(segs[i].x1, segs[i].y1,
					      segs[i].x2, segs[i].y2,
					      width, &traps);
		}

		radar_draw_traps(radar, drawable, gc, pixbuf, alpha, &traps);
		g_list_foreach(traps, (GFunc) free, NULL);
		g_list_free(traps);
	} else {
		for (i = 0; i < nsegs; i++) {
			gdk_draw_line(drawable, gc, 
				      d2i(segs[i].x1), d2i(segs[i].y1),
				      d2i(segs[i].x2), d2i(segs[i].y2));
		}
	}
}

static void
check_vector_horiz(radar_t *radar, vector_t *v, double low, double high,
		   double *left_bound, double *right_bound)
{
	vector_xy_t vl[2][2], vc[2];
	double x1, y1, x2, y2;
	double bx1, bx2;
	int n;

#ifdef DEBUG_TEXT
	printf("\t%s: vector: %.2f, %.2f - %.2f, %.2f\n",
	       __FUNCTION__, v->x1, v->y1, v->x2, v->y2);
#endif

	if (v->y1 > high && v->y2 > high) {
#ifdef DEBUG_TEXT
		printf("\t%s: above\n", __FUNCTION__);
#endif
		return;
	}

	if (v->y1 < low && v->y2 < low) {
#ifdef DEBUG_TEXT
		printf("\t%s: below\n", __FUNCTION__);
#endif
		return;
	}

	if (v->y1 < v->y2) {
		x1 = v->x1;
		y1 = v->y1;
		x2 = v->x2;
		y2 = v->y2;
	} else {
		x1 = v->x2;
		y1 = v->y2;
		x2 = v->x1;
		y2 = v->y1;
	}

	if (y1 >= low && y2 <= high) {
		if (x1 < x2) {
			bx1 = x1;
			bx2 = x2;
		} else {
			bx1 = x2;
			bx2 = x1;
		}
#ifdef DEBUG_TEXT
		printf("\t%s: inside\n", __FUNCTION__);
#endif
	} else {
		vl[0][0].x = x1;
		vl[0][0].y = y1;
		vl[0][1].x = x2;
		vl[0][1].y = y2;
		vl[1][0].x = 0.0;
		vl[1][1].x = i2d(radar->w);

		if (y1 < low) {
			vl[1][0].y = low;
			vl[1][1].y = low;
			n = lines_crossing(radar, &vl[0][0], &vl[0][1],
					   &vl[1][0], &vl[1][1], &vc[0]);
			if (0 == n)
				vc[0].x = 0;
#ifdef DEBUG_TEXT
			else
				printf("\t%s: low intersection %.2f, %.2f\n",
				       __FUNCTION__, vc[0].x, vc[0].y);
#endif
		} else {
			vc[0].x = x1;
		}

		if (y2 > high) {
			vl[1][0].y = high;
			vl[1][1].y = high;
			n = lines_crossing(radar, &vl[0][0], &vl[0][1],
					   &vl[1][0], &vl[1][1], &vc[1]);
			if (0 == n)
				vc[1].x = i2d(radar->w);
#ifdef DEBUG_TEXT
			else
				printf("\t%s: high intersection %.2f, %.2f\n",
				       __FUNCTION__, vc[1].x, vc[1].y);
#endif
		} else {
			vc[1].x = x2;
		}

		if (vc[0].x < vc[1].x) {
			bx1 = vc[0].x;
			bx2 = vc[1].x;
		} else {
			bx1 = vc[1].x;
			bx2 = vc[0].x;
		}
	}

#ifdef DEBUG_TEXT
	printf("\t%s: bounds %.2f - %.2f\n", __FUNCTION__, bx1, bx2);
#endif

	if (bx1 < *left_bound)
		*left_bound = bx1;
	if (bx2 > *right_bound)
		*right_bound = bx2;
}

static void
check_vector_vert(radar_t *radar, vector_t *v, double left, double right,
		   double *low_bound, double *high_bound)
{
	vector_xy_t vl[2][2], vc[2];
	double x1, y1, x2, y2;
	double by1, by2;
	int n;

#ifdef DEBUG_TEXT
	printf("\t%s: vector: %.2f, %.2f - %.2f, %.2f\n",
	       __FUNCTION__, v->x1, v->y1, v->x2, v->y2);
#endif

	if (v->x1 > right && v->x2 > right) {
#ifdef DEBUG_TEXT
		printf("\t%s: right of\n", __FUNCTION__);
#endif
		return;
	}

	if (v->x1 < left && v->x2 < left) {
#ifdef DEBUG_TEXT
		printf("\t%s: left of\n", __FUNCTION__);
#endif
		return;
	}

	if (v->x1 < v->x2) {
		x1 = v->x1;
		y1 = v->y1;
		x2 = v->x2;
		y2 = v->y2;
	} else {
		x1 = v->x2;
		y1 = v->y2;
		x2 = v->x1;
		y2 = v->y1;
	}

	if (x1 >= left && x2 <= right) {
		if (y1 < y2) {
			by1 = y1;
			by2 = y2;
		} else {
			by1 = y2;
			by2 = y1;
		}
#ifdef DEBUG_TEXT
		printf("\t%s: inside\n", __FUNCTION__);
#endif
	} else {
		vl[0][0].x = x1;
		vl[0][0].y = y1;
		vl[0][1].x = x2;
		vl[0][1].y = y2;
		vl[1][0].y = 0.0;
		vl[1][1].y = i2d(radar->h);

		if (x1 < left) {
			vl[1][0].x = left;
			vl[1][1].x = left;
			n = lines_crossing(radar, &vl[0][0], &vl[0][1],
					   &vl[1][0], &vl[1][1], &vc[0]);
			if (0 == n)
				vc[0].y = 0;
#ifdef DEBUG_TEXT
			else
				printf("\t%s: low intersection %.2f, %.2f\n",
				       __FUNCTION__, vc[0].x, vc[0].y);
#endif
		} else {
			vc[0].y = y1;
		}

		if (x2 > right) {
			vl[1][0].x = right;
			vl[1][1].x = right;
			n = lines_crossing(radar, &vl[0][0], &vl[0][1],
					   &vl[1][0], &vl[1][1], &vc[1]);
			if (0 == n)
				vc[1].y = i2d(radar->h);
#ifdef DEBUG_TEXT
			else
				printf("\t%s: high intersection %.2f, %.2f\n",
				       __FUNCTION__, vc[1].x, vc[1].y);
#endif
		} else {
			vc[1].y = y2;
		}

		if (vc[0].y < vc[1].y) {
			by1 = vc[0].y;
			by2 = vc[1].y;
		} else {
			by1 = vc[1].y;
			by2 = vc[0].y;
		}
	}

#ifdef DEBUG_TEXT
	printf("\t%s: bounds %.2f - %.2f\n", __FUNCTION__, by1, by2);
#endif

	if (by1 < *low_bound)
		*low_bound = by1;
	if (by2 > *high_bound)
		*high_bound = by2;
}

static void
check_arc(radar_t *radar, arc_t *a, double l1, double l2,
	  double *bound1, double *bound2,
	  void (*check_vector) (radar_t *, vector_t *, double, double,
				double *, double *))
{
	double start, delta, sign, inc;
	double angle, cosa, sina;
	vector_t v;

	start = M_PI * a->angle1 / 180.0;
	delta = M_PI * a->angle2 / 180.0;
	if (delta < 0) {
		sign = -1.0;
		delta = -delta;
	} else {
		sign = 1.0;
	}

	inc = M_PI / 8.0;

	cosa = cos(start);
	sina = sin(start);
	v.x2 = a->x + a->radius * cosa;
	v.y2 = a->y - a->radius * sina;

	for (angle = inc; angle < delta; angle += inc) {
		cosa = cos(start + sign * angle);
		sina = sin(start + sign * angle);

		v.x1 = v.x2;
		v.y1 = v.y2;
		v.x2 = a->x + a->radius * cosa;
		v.y2 = a->y - a->radius * sina;

		check_vector(radar, &v, l1, l2, bound1, bound2);
	}
	cosa = cos(start + sign * delta);
	sina = sin(start + sign * delta);

	v.x1 = v.x2;
	v.y1 = v.y2;
	v.x2 = a->x + a->radius * cosa;
	v.y2 = a->y - a->radius * sina;

	check_vector(radar, &v, l1, l2, bound1, bound2);
}

static double
label_align(double align, double extent)
{
	return -((0.5 - align) / 5.0 + (1.0 - align)) * extent;
}

static void
estimate_label_position(radar_t *radar, target_t *t, text_label_t *l)
{
	double low, high, left, right;
	double left_bound, right_bound;
	double low_bound, high_bound;
	vector_t *v;
	arc_t *a;
	int i;


	high = l->cy + i2d(l->th) / 2.0 + 1.0;
	low = l->cy - i2d(l->th) / 2.0 - 1.0;
	left = l->cx - i2d(l->tw) / 2.0 - 1.0;
	right = l->cx + i2d(l->tw) / 2.0 + 1.0;

	left_bound = l->cx;
	right_bound = l->cx;
	low_bound = l->cy;
	high_bound = l->cy;

#ifdef DEBUG_TEXT
	printf("\t%s: low %.6f, high %.6f\n", __FUNCTION__, low, high);
#endif

	for (i = 0; i < TARGET_NR_VECTORS; i++) {
		v = &t->vectors[i];

		if (!v->is_visible)
			continue;

		check_vector_horiz(radar, v, low, high,
				   &left_bound, &right_bound);
		check_vector_vert(radar, v, left, right,
				  &low_bound, &high_bound);

#ifdef DEBUG_TEXT
		printf("\t%s: total bounds (%.2f): %.2f - %.2f\n",
		       __FUNCTION__, l->cx, left_bound, right_bound);
		printf("\t%s: total bounds (%.2f): %.2f - %.2f\n",
		       __FUNCTION__, l->cy, low_bound, high_bound);
#endif
	}

	for (i = 0; i < TARGET_NR_ARCS; i++) {
		a = &t->arcs[i];

		if (!a->is_visible)
			continue;

		check_arc(radar, a, low, high, &left_bound, &right_bound,
			  check_vector_horiz);
		check_arc(radar, a, left, right, &low_bound, &high_bound,
			  check_vector_vert);

#ifdef DEBUG_TEXT
		printf("\t%s: total bounds (%.2f): %.2f - %.2f\n",
		       __FUNCTION__, l->cx, left_bound, right_bound);
		printf("\t%s: total bounds (%.2f): %.2f - %.2f\n",
		       __FUNCTION__, l->cy, low_bound, high_bound);
#endif
	}

	left = l->cx - left_bound;
	right = right_bound - l->cx;
	low = l->cy - low_bound;
	high = high_bound - l->cy;

	if (l->cx - left + label_align(0.0, i2d(l->tw)) <= 1.0)
		left = i2d(radar->w);
	if (l->cx + right + label_align(1.0, i2d(l->tw)) >= (i2d(radar->w - l->tw) - 1.0))
		right = i2d(radar->w);
	if (l->cy - low + label_align(0.0, i2d(l->th)) <= 1.0)
		low = i2d(radar->h);
	if (l->cy + high + label_align(1.0, i2d(l->th)) >= (i2d(radar->h - l->th) - 1.0))
		low = i2d(radar->h);

	l->xoff = 0.0;
	l->yoff = 0.0;
	l->xalign = 0.5;
	l->yalign = 0.5;

	if (low < high) {
		if (low < left) {
			if (low < right) {
				l->yoff = -low;
				l->yalign = 0.0;
			} else /* right <= low */ {
				l->xoff = right;
				l->xalign = 1.0;
			}
		} else /* left <= low */ {
			if (left < right) {
				l->xoff = -left;
				l->xalign = 0.0;
			} else /* right <= left */ {
				l->xoff = right;
				l->xalign = 1.0;
			}
		}
	} else /* high <= low */ {
		if (high < left) {
			if (high < right) {
				l->yoff = high;
				l->yalign = 1.0;
			} else /* right <= high */ {
				l->xoff = right;
				l->xalign = 1.0;
			}
		} else /* left <= high */ {
			if (left < right) {
				l->xoff = -left;
				l->xalign = 0.0;
			} else /* right <= left */ {
				l->xoff = right;
				l->xalign = 1.0;
			}
		}
	}

#ifdef DEBUG_TEXT
	printf("\t%s: xoff %.4f, yoff %.4f\n", __FUNCTION__, l->xoff, l->yoff);
#endif
}

static void
radar_set_label(radar_t *radar, target_t *t, text_label_t *l, GdkGC *fg, GdkGC *bg,
		double x, double y, char *markup, size_t len)
{
	pango_layout_set_markup(l->layout, markup, len);
	pango_layout_get_pixel_size(l->layout, (int *) &l->tw, (int *) &l->th);

	l->cx = x;
	l->cy = y;

	/*
	 * XXX: Find optimum position...
	 */
#ifdef DEBUG_TEXT
	printf("%s: label \"%s\":\n", __FUNCTION__, markup);
#endif
	estimate_label_position(radar, t, l);

#ifdef DEBUG_TEXT
	printf("Label '%s': x %.2f, y %.2f\n", markup, l->cx + l->xoff + label_align(l->xalign, i2d(l->tw)), l->cy + l->yoff + label_align(l->yalign, i2d(l->th)));
#endif

	l->is_visible = 1;
	l->fg = fg;
	l->bg = bg;

	l->bbox.x = d2i(l->cx + l->xoff + label_align(l->xalign, i2d(l->tw))) - 1;
	l->bbox.y = d2i(l->cy + l->yoff + label_align(l->yalign, i2d(l->th))) - 1;
	l->bbox.width = l->tw + 2;
	l->bbox.height = l->th + 2;

	if (l->markup)
		free(l->markup);
	l->markup = strdup(markup);

	gdk_window_invalidate_rect(radar->canvas->window, &l->bbox, FALSE);
}

void
radar_draw_label(radar_t *radar, GdkDrawable *drawable, GdkPixbuf *pixbuf,
		 guchar alpha, gboolean render, text_label_t *l)
{
	gdk_draw_layout(drawable, l->bg,
			l->cx + l->xoff + label_align(l->xalign, i2d(l->tw)) + 1,
			l->cy + l->yoff + label_align(l->yalign, i2d(l->th)) + 0,
			l->layout);
	gdk_draw_layout(drawable, l->bg,
			l->cx + l->xoff + label_align(l->xalign, i2d(l->tw)) - 1,
			l->cy + l->yoff + label_align(l->yalign, i2d(l->th)) + 0,
			l->layout);
	gdk_draw_layout(drawable, l->bg,
			l->cx + l->xoff + label_align(l->xalign, i2d(l->tw)) + 0,
			l->cy + l->yoff + label_align(l->yalign, i2d(l->th)) + 1,
			l->layout);
	gdk_draw_layout(drawable, l->bg,
			l->cx + l->xoff + label_align(l->xalign, i2d(l->tw)) + 0,
			l->cy + l->yoff + label_align(l->yalign, i2d(l->th)) - 1,
			l->layout);

	gdk_draw_layout(drawable, l->fg,
			l->cx + l->xoff + label_align(l->xalign, i2d(l->tw)),
			l->cy + l->yoff + label_align(l->yalign, i2d(l->th)),
			l->layout);
}

static void
radar_draw_vectors(radar_t *radar)
{
	vector_t *v;
	poly_t *p;
	arc_t *a;
	text_label_t *l;
	target_t *s;
	int i, j;

	if (radar->forebuf && radar->do_render) {
		guchar *dst, *src;

		dst = gdk_pixbuf_get_pixels(radar->forebuf);
		src = gdk_pixbuf_get_pixels(radar->backbuf);
		for (i = 0; i < radar->h; i++) {
			memcpy(dst, src, 4 * radar->w);
			dst += gdk_pixbuf_get_rowstride(radar->forebuf);
			src += gdk_pixbuf_get_rowstride(radar->backbuf);
		}
	}

	for (i = 0; i < RADAR_NR_VECTORS; i++) {
		v = &radar->vectors[i];

		if (!v->is_visible)
			continue;

		radar_draw_vector(radar, radar->canvas->window,
				  radar->forebuf, 0xff, radar->do_render, v);
	}

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		s = &radar->target[i];

		for (j = 0; j < TARGET_NR_VECTORS; j++) {
			v = &s->vectors[j];

			if (!v->is_visible)
				continue;

			radar_draw_vector(radar, radar->canvas->window,
					  radar->forebuf, 0xff,
					  radar->do_render, v);
		}

		for (j = 0; j < TARGET_NR_ARCS; j++) {
			a = &s->arcs[j];

			if (!a->is_visible)
				continue;

			radar_draw_arc(radar, radar->canvas->window,
				       radar->forebuf, 0xff,
				       radar->do_render, a);
		}

		for (j = 0; j < TARGET_NR_POLYS; j++) {
			p = &s->polys[j];

			if (!p->is_visible)
				continue;

			radar_draw_poly(radar, radar->canvas->window,
					radar->forebuf, 0xff,
					radar->do_render, p);
		}

		for (j = 0; j < TARGET_NR_LABELS; j++) {
			l = &s->labels[j];

			if (!l->is_visible)
				continue;

			radar_draw_label(radar, radar->canvas->window,
					 radar->forebuf, 0xff,
					 radar->do_render, l);
		}
	}

	if (radar->forebuf && radar->do_render) {
		gdk_draw_pixbuf(radar->canvas->window, radar->white_gc,
				radar->forebuf, 0, 0, 0, 0, radar->w, radar->h,
				GDK_RGB_DITHER_NORMAL, 0, 0);
	}
}

static double
course(radar_t *radar, const vector_xy_t *v0, const vector_xy_t *v1)
{
	double dx, dy;
	double course;

	dx = v1->x - v0->x;
	dy = v1->y - v0->y;

	if (fabs(dx) < EPSILON) {
		if (dy < 0)
			course = 180.0;
		else
			course = 0.0;
	} else {
		course = 450.0 - 180.0 * atan2(dy, dx) / M_PI;
	}

	if (radar->north_up == FALSE)
		course += radar->own_course;

	return fmod(course, 360.0);
}

static double
distance(radar_t *radar, const vector_xy_t *v0, const vector_xy_t *v1)
{
	double dx, dy;

	dx = v1->x - v0->x;
	dy = v1->y - v0->y;

	return sqrt(dx * dx + dy * dy);
}

static double
speed(radar_t *radar, const vector_xy_t *v0, const vector_xy_t *v1, int dt)
{
	double t;

	if (dt == 0)
		return 0.0;

	t = ((double) dt) / 60.0;

	return distance(radar, v0, v1) / t;
}

static double
dtime(radar_t *radar, const vector_xy_t *v0, const vector_xy_t *v1,
      double crs, double speed)
{
	double d, t;

	d = distance(radar, v0, v1);

	if (fabs(speed) < EPSILON)
		return 0.0;

	t = d / speed;

	if (fabs(crs - course(radar, v0, v1)) > 90.0)
		t = -t;

	return t * 60.0;
}

static int
add_time(radar_t *radar, int time, double dtime)
{
	int t2;

	if (dtime < 0.0) {
		dtime = -fmod(-dtime, 1440.0);
	} else {
		dtime = fmod(dtime, 1440.0);
	}

	t2 = d2i(1440.0 + ((double) time) + dtime);

	return t2 % 1440;
}

static void
advance(radar_t *radar, vector_xy_t *from, vector_xy_t *to,
	vector_xy_t *res, double f)
{
	res->x = from->x + (to->x - from->x) * f;
	res->y = from->y + (to->y - from->y) * f;
}

static int
lines_crossing(radar_t *radar, vector_xy_t *v1_0, vector_xy_t *v1_1,
	       vector_xy_t *v2_0, vector_xy_t *v2_1, vector_xy_t *res)
{
	double m1, m2, b1, b2;

	if ((fabs(v1_1->x - v1_0->x) < EPSILON) &&
	    (fabs(v2_1->x - v2_0->x) < EPSILON)) {
		return 0;
	} else if (fabs(v1_1->x - v1_0->x) < EPSILON) {
		m2 = (v2_1->y - v2_0->y) / (v2_1->x - v2_0->x);
		b2 = v2_1->y - m2 * v2_1->x;

		res->x = v1_1->x;
		res->y = m2 * v1_1->x + b2;
	} else if (fabs(v2_1->x - v2_0->x) < EPSILON) {
		m1 = (v1_1->y - v1_0->y) / (v1_1->x - v1_0->x);
		b1 = v1_1->y - m1 * v1_1->x;

		res->x = v2_1->x;
		res->y = m1 * v2_1->x + b1;
	} else {
		m1 = (v1_1->y - v1_0->y) / (v1_1->x - v1_0->x);
		b1 = v1_1->y - m1 * v1_1->x;

		m2 = (v2_1->y - v2_0->y) / (v2_1->x - v2_0->x);
		b2 = v2_1->y - m2 * v2_1->x;

		if (fabs(m2 - m1) < EPSILON)
			return 0;

		res->x = (b2 - b1) / (m1 - m2);
		res->y = m1 * res->x + b1;
	}

	return 1;
}

static gboolean
point_on_line(radar_t *radar, vector_xy_t *l0, vector_xy_t *l1, vector_xy_t *p)
{
	double x0, x1, y0, y1;

	if (l0->x > l1->x) {
		x0 = l1->x;
		x1 = l0->x;
	} else {
		x0 = l0->x;
		x1 = l1->x;
	}
	if (l0->y > l1->y) {
		y0 = l1->y;
		y1 = l0->y;
	} else {
		y0 = l0->y;
		y1 = l1->y;
	}

	if ((x0 <= p->x) && (p->x <= x1) && (y0 <= p->y) && (p->y <= y1))
		return TRUE;

	return FALSE;
}

static int
circle_crossing(radar_t *radar, vector_xy_t *v0, vector_xy_t *v1,
		double r, vector_xy_t *res[2])
{
	double m, a, b, c, p, q;

	if (fabs(v1->x - v0->x) < EPSILON) {
		if (fabs(r * r - v1->x * v1->x) < EPSILON) {
			res[0]->y = 0.0;
			res[0]->x = v1->x;

			return 1;
		} else if ((r * r - v1->x * v1->x) > 0.0) {
			res[0]->y = sqrt(r * r - v1->x * v1->x);
			res[0]->x = v1->x;

			res[1]->y = -sqrt(r * r - v1->x * v1->x);
			res[1]->x = v1->x;

			return 2;
		} else {
			return 0;
		}
	} else {
		m = (v1->y - v0->y) / (v1->x - v0->x);

		a = m * m + 1.0;
		b = 2.0 * m * (v1->y - m * v1->x);
		c = (m * v1->x - v1->y) * (m * v1->x - v1->y) - r * r;

		p = b / a;
		q = c / a;

		if (fabs(p * p / 4.0 - q) < EPSILON) {
			res[0]->x = -p / 2.0;
			res[0]->y = m * (res[0]->x - v1->x) + v1->y;
			return 1;
		} else if ((p * p / 4.0 - q) > 0.0) {
			res[0]->x = -p / 2.0 + sqrt(p * p / 4.0 - q);
			res[0]->y = m * (res[0]->x - v1->x) + v1->y;

			res[1]->x = -p / 2.0 - sqrt(p * p / 4.0 - q);
			res[1]->y = m * (res[1]->x - v1->x) + v1->y;

			return 2;
		} else {
			return 0;
		}
	}
}

static gboolean
extend(radar_t *radar, vector_xy_t *from, vector_xy_t *to,
       double course, double speed, vector_xy_t *res)
{
	vector_xy_t mp0, mp1;
	vector_xy_t *mp[2] = { &mp0, &mp1 };
	gboolean have_ext = FALSE;
	double t0, t1;
	int n;

	n = circle_crossing(radar, from, to, radar->range, mp);
	if (n == 2) {
		t0 = dtime(radar, to, mp[0], course, speed);
		t1 = dtime(radar, to, mp[1], course, speed);
		if (t0 < t1) {
			if (t1 > 0) {
				*res = *mp[1];
				have_ext = TRUE;
			}
		} else {
			if (t0 > 0) {
				*res = *mp[0];
				have_ext = TRUE;
			}
		}
	} else if (n == 1) {
		t0 = dtime(radar, to, mp[0], course, speed);
		if (t0 > 0) {
			*res = *mp[0];
			have_ext = TRUE;
		}
	}

	return have_ext;
}

static void
radar_set_course_entry(GtkEntry *entry, double value)
{
	char text[16];

	value = fmod(floor(value * 10.0 + 0.5) / 10.0, 360.0);

	snprintf(text, sizeof(text), "%05.1f", value);
	gtk_entry_set_text(entry, text);
}

static void
radar_set_scalar_entry(GtkEntry *entry, double value)
{
	char text[16];

	if (fabs(value) >= 10000.0)
		snprintf(text, sizeof(text), "-");
	else if (fabs(value) >= 1000.0)
		snprintf(text, sizeof(text), "%.0f", value);
	else
		snprintf(text, sizeof(text), "%.1f", value);
	gtk_entry_set_text(entry, text);
}

static void
radar_show_problems(GtkWidget *widget, gboolean have_problems)
{
	static const GdkColor red = { red: 0xc0c0, green: 0, blue: 0 };

	if (have_problems) {
		gtk_widget_modify_text(widget, GTK_STATE_NORMAL, &red);
		gtk_widget_modify_text(widget, GTK_STATE_ACTIVE, &red);
		gtk_widget_modify_text(widget, GTK_STATE_PRELIGHT, &red);
		gtk_widget_modify_text(widget, GTK_STATE_SELECTED, &red);
		gtk_widget_modify_text(widget, GTK_STATE_INSENSITIVE, &red);
	} else {
		gtk_widget_modify_text(widget, GTK_STATE_NORMAL, NULL);
		gtk_widget_modify_text(widget, GTK_STATE_ACTIVE, NULL);
		gtk_widget_modify_text(widget, GTK_STATE_PRELIGHT, NULL);
		gtk_widget_modify_text(widget, GTK_STATE_SELECTED, NULL);
		gtk_widget_modify_text(widget, GTK_STATE_INSENSITIVE, NULL);
	}
}

static gboolean
radar_calculate_new_course(target_t *s)
{
	radar_t *radar = s->radar;
	gboolean solution_found = FALSE;
	gboolean have_new_cpa[2];
	vector_xy_t new_cpa[2];
	vector_xy_t mp0, mp1;
	vector_xy_t *mp[2] = { &mp0, &mp1 };
	vector_xy_t v0, v1;
	vector_xy_t t[2];
	double alpha, c0, c1, d0, d1;
	double sina, cosa, l, m;
	double bearing;
	double KBr[2];
	int i, n;

	bearing = fmod(360.0 + course(radar, &vector_xy_null, &s->mpoint) - radar->own_course, 360.0);

	radar->direction = STARBOARD;
	if ((bearing < 180.0) && (090.0 <= bearing))
		radar->direction = PORT;

	alpha = 180.0 * asin(radar->mcpa / radar->mdistance) / M_PI;
	KBr[0] = fmod(course(radar, &s->mpoint, &vector_xy_null) + 360.0 + alpha, 360.0);
	KBr[1] = fmod(course(radar, &s->mpoint, &vector_xy_null) + 360.0 - alpha, 360.0);

	m = sqrt(radar->mdistance * radar->mdistance - radar->mcpa * radar->mcpa);
	l = radar->own_speed * ((double) s->delta_time) / 60.0;

	for (i = 0; i < 2; i++) {
		radar_sincos(radar, KBr[i], &sina, &cosa);

		new_cpa[i].x = s->mpoint.x + m * sina;
		new_cpa[i].y = s->mpoint.y + m * cosa;

		v0.x = s->sight[1].x - s->p0_sub_own.x;
		v0.y = s->sight[1].y - s->p0_sub_own.y;

		v1.x = s->sight[1].x - m * sina - s->p0_sub_own.x;
		v1.y = s->sight[1].y - m * cosa - s->p0_sub_own.y;
		have_new_cpa[i] = TRUE;
		n = circle_crossing(radar, &v0, &v1, l, mp);
		if (n == 2) {
			v0.x = mp[0]->x + s->p0_sub_own.x;
			v0.y = mp[0]->y + s->p0_sub_own.y;
			c0 = course(radar, &s->p0_sub_own, &v0);
			d0 = fmod(360.0 + radar->direction * (c0 - radar->own_course), 360.0);
			v1.x = mp[1]->x + s->p0_sub_own.x;
			v1.y = mp[1]->y + s->p0_sub_own.y;
			c1 = course(radar, &s->p0_sub_own, &v1);
			d1 = fmod(360.0 + radar->direction * (c1 - radar->own_course), 360.0);
			if (d0 < d1)
				t[i] = v0;
			else
				t[i] = v1;
		} else if (n == 1) {
			t[i] = v0;
		} else {
			have_new_cpa[i] = FALSE;
		}
	}

	if (have_new_cpa[0] && have_new_cpa[1]) {
		c0 = course(radar, &s->p0_sub_own, &t[0]);
		d0 = fmod(360.0 + radar->direction * (c0 - radar->own_course), 360.0);
		c1 = course(radar, &s->p0_sub_own, &t[1]);
		d1 = fmod(360.0 + radar->direction * (c1 - radar->own_course), 360.0);
		if (d0 < d1) {
			if (d0 <= 180.0) {
				s->new_KBr = KBr[0];
				s->new_cpa = new_cpa[0];
				s->xpoint = t[0];
				solution_found = TRUE;
			}
		} else {
			if (d1 <= 180.0) {
				s->new_KBr = KBr[1];
				s->new_cpa = new_cpa[1];
				s->xpoint = t[1];
				solution_found = TRUE;
			}
		}
	} else if (have_new_cpa[0]) {
		c0 = course(radar, &s->p0_sub_own, &t[0]);
		d0 = fmod(360.0 + radar->direction * (c0 - radar->own_course), 360.0);
		if (d0 <= 180.0) {
			s->new_KBr = KBr[0];
			s->new_cpa = new_cpa[0];
			s->xpoint = t[0];
			solution_found = TRUE;
		}
	} else if (have_new_cpa[1]) {
		c1 = course(radar, &s->p0_sub_own, &t[1]);
		d1 = fmod(360.0 + radar->direction * (c1 - radar->own_course), 360.0);
		if (d1 <= 180.0) {
			s->new_KBr = KBr[1];
			s->new_cpa = new_cpa[1];
			s->xpoint = t[1];
			solution_found = TRUE;
		}
	}

	return solution_found;
}

static gboolean
radar_calculate_new_speed(target_t *s)
{
	radar_t *radar = s->radar;
	gboolean solution_found = FALSE;
	gboolean have_new_cpa[2];
	vector_xy_t new_cpa[2];
	vector_xy_t t[2];
	vector_xy_t v0;
	double alpha, s0, s1;
	double sina, cosa, m;
	double KBr[2];
	int i;

	alpha = 180.0 * asin(radar->mcpa / radar->mdistance) / M_PI;
	KBr[0] = fmod(course(radar, &s->mpoint, &vector_xy_null) + 360.0 + alpha, 360.0);
	KBr[1] = fmod(course(radar, &s->mpoint, &vector_xy_null) + 360.0 - alpha, 360.0);

	m = sqrt(radar->mdistance * radar->mdistance - radar->mcpa * radar->mcpa);
	for (i = 0; i < 2; i++) {
		radar_sincos(radar, KBr[i], &sina, &cosa);

		new_cpa[i].x = s->mpoint.x + m * sina;
		new_cpa[i].y = s->mpoint.y + m * cosa;

		v0.x = s->sight[1].x - m * sina;
		v0.y = s->sight[1].y - m * cosa;

		/* Calculate crossing between lines:
		 * 1. mpoint .. new_cpa	(straight)
		 * 2. p0_sub_own .. sight[0] (segment)
		 */
		have_new_cpa[i] = FALSE;
		if (lines_crossing(radar, &s->p0_sub_own, &s->sight[0],
				   &s->sight[1], &v0, &t[i])) {
			if (point_on_line(radar, &s->p0_sub_own,
					  &s->sight[0], &t[i])) {
				have_new_cpa[i] = TRUE;
			}
		}
	}

	if (have_new_cpa[0] && have_new_cpa[1]) {
		s0 = speed(radar, &s->p0_sub_own, &t[0], s->delta_time);
		s1 = speed(radar, &s->p0_sub_own, &t[1], s->delta_time);

		if (s0 > s1) {
			s->xpoint = t[0];
			s->new_KBr = KBr[0];
			s->new_cpa = new_cpa[0];
			solution_found = TRUE;
		} else {
			s->xpoint = t[1];
			s->new_KBr = KBr[1];
			s->new_cpa = new_cpa[1];
			solution_found = TRUE;
		}
	} else if (have_new_cpa[0]) {
		s->xpoint = t[0];
		s->new_KBr = KBr[0];
		s->new_cpa = new_cpa[0];
		solution_found = TRUE;
	} else if (have_new_cpa[1]) {
		s->xpoint = t[1];
		s->new_KBr = KBr[1];
		s->new_cpa = new_cpa[1];
		solution_found = TRUE;
	}

	return solution_found;
}

static gboolean
radar_calculate_new_cpa(target_t *s)
{
	radar_t *radar = s->radar;
	double l, m, sina, cosa;
	vector_xy_t t;

	l = radar->nspeed * ((double) s->delta_time) / 60.0;
	radar_sincos(radar, radar->ncourse, &sina, &cosa);

	s->xpoint.x = s->p0_sub_own.x + l * sina;
	s->xpoint.y = s->p0_sub_own.y + l * cosa;

	s->new_KBr = course(radar, &s->xpoint, &s->sight[1]);

	radar_sincos(radar, s->new_KBr, &sina, &cosa);

	t.x = s->mpoint.x + sina;
	t.y = s->mpoint.y + cosa;

	if (fabs(s->mpoint.x - t.x) < EPSILON) {
		s->new_cpa.y = 0.0;
		s->new_cpa.x = s->mpoint.x;
	} else {
		m = (s->mpoint.y - t.y) / (s->mpoint.x - t.x);
		if (fabs(m) < EPSILON)
			s->new_cpa.x = 0.0;
		else
			s->new_cpa.x = (m * s->mpoint.x - s->mpoint.y) /
				       (m + 1.0 / m);
		s->new_cpa.y = m * (s->new_cpa.x - s->mpoint.x) + s->mpoint.y;
	}

	return TRUE;
}

static double
radar_calculate_max_course(target_t *s)
{
	radar_t *radar = s->radar;
	double bearing;
	double l, r;
	double alpha;
	double c, d;
	double CPA1, CPA2;

	bearing = fmod(360.0 + course(radar, &vector_xy_null, &s->mpoint) - radar->own_course, 360.0);

	radar->direction = STARBOARD;
	if ((bearing < 180.0) && (090.0 <= bearing))
		radar->direction = PORT;

	r = distance(radar, &s->p0_sub_own, &s->sight[1]);
	l = radar->own_speed * ((double) s->delta_time) / 60.0;

	alpha = 180.0 * acos(l / r) / M_PI;

	c = fmod(360.0 + course(radar, &s->p0_sub_own, &s->sight[1])
		       + radar->direction * alpha, 360.0);
	d = fmod(360.0 + radar->direction * (c - radar->own_course), 360.0);

	if (d <= 180.0) {
		radar->ncourse = c;
		radar_calculate_new_cpa(s);

		CPA1 = distance(radar, &vector_xy_null, &s->new_cpa);

		radar->ncourse = fmod(360.0 + radar->own_course
					    + radar->direction * 180.0, 360.0);
		radar_calculate_new_cpa(s);

		CPA2 = distance(radar, &vector_xy_null, &s->new_cpa);

		if (CPA2 > CPA1)
			c = radar->ncourse;
	} else {
		c = fmod(360.0 + radar->own_course
			       + radar->direction * 180.0, 360.0);
	}

	return c;
}

static void
radar_calculate_maneuver(target_t *s)
{
	radar_t *radar = s->radar;
	vector_xy_t mp0, mp1;
	vector_xy_t *mp[2] = { &mp0, &mp1 };
	gboolean select;
	double mt[2];
	int delta_t;
	int n;

	if ((s->distance[1] <= s->CPA) || (s->TCPA <= 0)) {
		radar->mtime = 0;
		radar->mdistance = 0.0;
		select = radar->mtime_selected;

		radar->mtime_selected = FALSE;
		gtk_spin_button_set_value(radar->mtime_spin, radar->mtime);

		radar->mtime_selected = TRUE;
		gtk_spin_button_set_value(radar->mdist_spin, radar->mdistance);

		radar->mtime_selected = select;
		radar->mtime_set = 0;
		radar->mdist_set = 0;

	} else if (radar->mtime_selected) {

		if (!radar->mtime_set) {
			radar->mtime = s->time[1];

			select = radar->mtime_selected;
			radar->mtime_selected = FALSE;
			gtk_spin_button_set_value(radar->mtime_spin,
						  radar->mtime);
			radar->mtime_selected = select;

			radar->mtime_set = TRUE;
		}

		delta_t = radar->mtime - s->time[1];
		if (delta_t < 0)
			delta_t += 1440;

		if (delta_t > s->mtime_range) {
			if (delta_t > (1440 - s->mtime_range) / 2) {
				radar->mtime = s->time[1];
			} else {
				radar->mtime = s->time[1] + s->mtime_range;
			}
			radar->mtime %= 1440;

			delta_t = radar->mtime - s->time[1];
			if (delta_t < 0)
				delta_t += 1440;

			select = radar->mtime_selected;
			radar->mtime_selected = FALSE;
			gtk_spin_button_set_value(radar->mtime_spin,
						  radar->mtime);
			radar->mtime_selected = select;

			radar->mtime_set = TRUE;
		}

		advance(radar, &s->sight[1], &s->cpa, &s->mpoint, 
			((double) delta_t) / s->TCPA);
		s->have_mpoint = TRUE;

		radar->mdistance = distance(radar, &vector_xy_null, &s->mpoint);

		select = radar->mtime_selected;
		radar->mtime_selected = TRUE;
		gtk_spin_button_set_value(radar->mdist_spin, radar->mdistance);
		radar->mtime_selected = select;
	} else {
		if (!radar->mdist_set) {
			radar->mdistance = s->distance[1];

			select = radar->mtime_selected;
			radar->mtime_selected = TRUE;
			gtk_spin_button_set_value(radar->mdist_spin,
						  radar->mdistance);
			radar->mtime_selected = select;
		}

		if (s->CPA < s->distance[1]) {
			if (radar->mdistance > s->distance[1]) {
				radar->mdistance = s->distance[1];
				select = radar->mtime_selected;
				radar->mtime_selected = TRUE;
				gtk_spin_button_set_value(radar->mdist_spin,
							  radar->mdistance);
				radar->mtime_selected = select;
			}
			if (radar->mdistance < s->CPA) {
				radar->mdistance = floor(10.0 * s->CPA + 1.0) / 10.0;
				select = radar->mtime_selected;
				radar->mtime_selected = TRUE;
				gtk_spin_button_set_value(radar->mdist_spin,
							  radar->mdistance);
				radar->mtime_selected = select;
			}
		} else {
			if (radar->mdistance < s->distance[1]) {
				radar->mdistance = s->distance[1];
				select = radar->mtime_selected;
				radar->mtime_selected = TRUE;
				gtk_spin_button_set_value(radar->mdist_spin,
							  radar->mdistance);
				radar->mtime_selected = select;
			}
			if (radar->mdistance > s->CPA) {
				radar->mdistance = floor(10.0 * s->CPA) / 10.0;
				select = radar->mtime_selected;
				radar->mtime_selected = TRUE;
				gtk_spin_button_set_value(radar->mdist_spin,
							  radar->mdistance);
				radar->mtime_selected = select;
			}
		}

		n = circle_crossing(radar, &s->sight[0], &s->sight[1],
				    radar->mdistance, mp);
		if (n == 2) {
			mt[0] = dtime(radar, &s->sight[1], mp[0],
				      s->KBr, s->vBr);
			mt[1] = dtime(radar, &s->sight[1], mp[1],
				      s->KBr, s->vBr);
			if (mt[0] < mt[1]) {
				s->mpoint = *mp[0];
				radar->mtime = add_time(radar, s->time[1],
							mt[0]);
			} else {
				s->mpoint = *mp[1];
				radar->mtime = add_time(radar, s->time[1],
							mt[1]);
			}
			s->have_mpoint = TRUE;
		} else if (n == 1) {
			mt[0] = dtime(radar, &s->sight[1], mp[0],
				      s->KBr, s->vBr);
			s->mpoint = *mp[0];
			s->have_mpoint = TRUE;
			radar->mtime = add_time(radar, s->time[1], mt[0]);
		}

		select = radar->mtime_selected;
		radar->mtime_selected = FALSE;
		gtk_spin_button_set_value(radar->mtime_spin, radar->mtime);
		radar->mtime_selected = select;

		radar->mtime_set = TRUE;
	}

	if (s->have_mpoint) {
		radar->exact_mtime = dtime(radar, &s->sight[1], &s->mpoint,
					   s->KBr, s->vBr) + (double)s->time[1];

		s->mdistance = radar->mdistance;
		s->mbearing = course(radar, &vector_xy_null, &s->mpoint);

		if (radar->mcpa_selected) {
			if (fabs(radar->mcpa) > EPSILON) {
				if (radar->mcourse_change) {
					radar->maneuver = MANEUVER_COURSE_FROM_CPA;
				} else {
					radar->maneuver = MANEUVER_SPEED_FROM_CPA;
				}
			}

			if (radar->maneuver != MANEUVER_NONE) {
				if (radar->mcpa > (radar->mdistance-EPSILON)) {
					radar->mcpa = radar->mdistance -
						      EPSILON;
					gtk_spin_button_set_value(
						radar->mcpa_spin, radar->mcpa);
				}

				if (radar->mcpa < s->CPA) {
					radar->mcpa =
						floor(10. * s->CPA + 1.) / 10.;
					gtk_spin_button_set_value(
						radar->mcpa_spin, radar->mcpa);
				}
			}
		} else if (radar->mcourse_change) {
			radar->maneuver = MANEUVER_CPA_FROM_COURSE;
		} else {
			radar->maneuver = MANEUVER_CPA_FROM_SPEED;
		}
	}

	switch (radar->maneuver) {
	case MANEUVER_NONE:
	default:
		break;

	case MANEUVER_COURSE_FROM_CPA:
		radar->nspeed = radar->own_speed;
		gtk_spin_button_set_value(radar->nspeed_spin, radar->nspeed);

		s->have_new_cpa = radar_calculate_new_course(s);

		if (s->have_new_cpa) {
			radar->ncourse = course(radar, &s->p0_sub_own,
						&s->xpoint);
			gtk_spin_button_set_value(radar->ncourse_spin,
						  radar->ncourse);
		} else {
			s->have_problems = TRUE;

			radar->ncourse = radar_calculate_max_course(s);
			gtk_spin_button_set_value(radar->ncourse_spin,
						  radar->ncourse);

			s->have_new_cpa = radar_calculate_new_cpa(s);

			if (!s->have_new_cpa) {
				radar->ncourse = radar->own_course;
				gtk_spin_button_set_value(radar->ncourse_spin,
							  radar->ncourse);
			}
		}
		break;

	case MANEUVER_SPEED_FROM_CPA:
		radar->ncourse = radar->own_course;
		gtk_spin_button_set_value(radar->ncourse_spin, radar->ncourse);

		s->have_new_cpa = radar_calculate_new_speed(s);

		if (s->have_new_cpa) {
			radar->nspeed = speed(radar, &s->p0_sub_own,
					      &s->xpoint, s->delta_time);
			gtk_spin_button_set_value(radar->nspeed_spin,
						  radar->nspeed);
		} else {
			s->have_problems = TRUE;

			radar->nspeed = 0.0;
			gtk_spin_button_set_value(radar->nspeed_spin,
						  radar->nspeed);

			s->have_new_cpa = radar_calculate_new_cpa(s);

			if (!s->have_new_cpa) {
				radar->nspeed = radar->own_speed;
				gtk_spin_button_set_value(radar->nspeed_spin,
							  radar->nspeed);
			}
		}
		break;

	case MANEUVER_CPA_FROM_COURSE:
		radar->nspeed = radar->own_speed;
		gtk_spin_button_set_value(radar->nspeed_spin, radar->nspeed);

		s->have_new_cpa = radar_calculate_new_cpa(s);

		if (s->have_new_cpa) {
			radar->mcpa = distance(radar, &vector_xy_null,
					       &s->new_cpa);
			gtk_spin_button_set_value(radar->mcpa_spin,
						  radar->mcpa);
		} else {
			s->have_problems = TRUE;

			radar->mcpa = 0.0;
			gtk_spin_button_set_value(radar->mcpa_spin,
						  radar->mcpa);
		}
		break;

	case MANEUVER_CPA_FROM_SPEED:
		radar->ncourse = radar->own_course;
		gtk_spin_button_set_value(radar->ncourse_spin, radar->ncourse);

		s->have_new_cpa = radar_calculate_new_cpa(s);

		if (s->have_new_cpa) {
			radar->mcpa = distance(radar, &vector_xy_null,
					       &s->new_cpa);
			gtk_spin_button_set_value(radar->mcpa_spin,
						  radar->mcpa);
		} else {
			s->have_problems = TRUE;

			radar->mcpa = 0.0;
			gtk_spin_button_set_value(radar->mcpa_spin,
						  radar->mcpa);
		}
		break;
	}
}

static void
radar_calculate_secondary(target_t *s)
{
	radar_t *radar = s->radar;
	target_t *t = &radar->target[radar->mtarget];
	double delta_t;

	if (!t->have_mpoint)
		return;

	delta_t = radar->exact_mtime - (double) s->time[1];

	advance(radar, &s->sight[1], &s->cpa, &s->mpoint, delta_t / s->TCPA);
	s->have_mpoint = TRUE;

	s->mdistance = distance(radar, &vector_xy_null, &s->mpoint);
	s->mbearing = course(radar, &vector_xy_null, &s->mpoint);

	if (radar->maneuver == MANEUVER_NONE)
		return;

	s->have_new_cpa = radar_calculate_new_cpa(s);
}

static void
radar_calculate_target(target_t *s)
{
	radar_t *radar = s->radar;
	double sina, cosa, l, m, delta_m;
	double exact_time;
	double bearing;
	char text[16];

	if (s->distance[0] != 0.0) {
		radar_sincos(radar, s->rakrp[0], &sina, &cosa);

		s->sight[0].x = s->distance[0] * sina;
		s->sight[0].y = s->distance[0] * cosa;
	}

	if (s->distance[1] != 0.0) {
		radar_sincos(radar, s->rakrp[1], &sina, &cosa);

		s->sight[1].x = s->distance[1] * sina;
		s->sight[1].y = s->distance[1] * cosa;
	}

	s->delta_time = 0;
	s->mtime_range = 1440;
	s->have_cpa = FALSE;
	s->have_mpoint = FALSE;
	s->have_new_cpa = FALSE;
	s->have_problems = FALSE;
	s->have_crossing = FALSE;
	s->new_have_crossing = FALSE;

	if ((s->distance[0] == 0.0) || (s->distance[1] == 0.0))
		goto out_clear_all;

	s->delta_time = s->time[1] - s->time[0];
	if (s->delta_time < 0)
		s->delta_time += 1440;

	if (s->delta_time == 0) {
		s->mtime_range = 1440;
		goto out_clear_all;
	}

	s->KBr = course(radar, &s->sight[0], &s->sight[1]);
	s->vBr = speed(radar, &s->sight[0], &s->sight[1], s->delta_time);

	l = radar->own_speed * ((double) s->delta_time) / 60.0;
	radar_sincos(radar, (180 + radar->own_course) % 360, &sina, &cosa);

	s->p0_sub_own.x = s->sight[0].x + l * sina;
	s->p0_sub_own.y = s->sight[0].y + l * cosa;

	s->have_crossing = TRUE;
	if ((fabs(s->sight[1].x - s->sight[0].x) < EPSILON) &&
	    (fabs(s->sight[1].y - s->sight[0].y) < EPSILON)) {
		s->cpa.x = s->sight[1].x;
		s->cpa.y = s->sight[1].y;

		s->have_crossing = FALSE;
		s->cross.y = 0.0;
		s->cross.x = 0.0;
	} else if (fabs(s->sight[1].x - s->sight[0].x) < EPSILON) {
		s->cpa.y = 0.0;
		s->cpa.x = s->sight[1].x;

		radar_sincos(radar, radar->own_course, &sina, &cosa);
		if (fabs(cosa) < EPSILON) {
			s->cross.y = 0.0;
			s->cross.x = s->sight[1].x;
		} else {
			delta_m = sina / cosa;
			if (fabs(delta_m) < EPSILON) {
				s->have_crossing = FALSE;
				s->cross.y = 0.0;
				s->cross.x = 0.0;
			} else {
				s->cross.y = s->sight[1].x / delta_m;
				s->cross.x = s->sight[1].x;
			}
		}
	} else {
		m = (s->sight[1].y - s->sight[0].y) /
		    (s->sight[1].x - s->sight[0].x);
		if (fabs(m) < EPSILON)
			s->cpa.x = 0.0;
		else
			s->cpa.x = (m * s->sight[1].x - s->sight[1].y) /
				   (m + 1.0 / m);
		s->cpa.y = m * (s->cpa.x - s->sight[1].x) + s->sight[1].y;

		radar_sincos(radar, radar->own_course, &sina, &cosa);
		if (fabs(sina) < EPSILON) {
			s->cross.x = 0.0;
			s->cross.y = m * (s->cross.x - s->sight[1].x) +
					s->sight[1].y;
		} else {
			delta_m = cosa / sina - m;
			if (fabs(delta_m) < EPSILON) {
				s->have_crossing = FALSE;
				s->cross.x = 0.0;
				s->cross.y = 0.0;
			} else {
				s->cross.x = (s->sight[1].y - m * s->sight[1].x)
						/ delta_m;
				s->cross.y = m * (s->cross.x - s->sight[1].x)
						+ s->sight[1].y;
			}
		}
	}

	s->KB = course(radar, &s->p0_sub_own, &s->sight[1]);
	s->vB = speed(radar, &s->p0_sub_own, &s->sight[1], s->delta_time);
	s->aspect = fmod(360.0 + course(radar, &s->sight[1], &vector_xy_null) -
			 s->KB, 360.0);

	s->CPA = distance(radar, &vector_xy_null, &s->cpa);
	s->TCPA = dtime(radar, &s->sight[1], &s->cpa, s->KBr, s->vBr);
	s->tCPA = add_time(radar, s->time[1], s->TCPA);

	if (s->TCPA >= 0.0)
		s->have_cpa = TRUE;
	s->mtime_range = (int) floor(s->TCPA);

	if (fabs(s->CPA) >= EPSILON) {
		s->PCPA = course(radar, &vector_xy_null, &s->cpa);
		s->SPCPA = fmod(360.0 + s->PCPA - radar->own_course, 360.0);
	} else {
		s->PCPA = -1.0;
		s->SPCPA = -1.0;
	}

	if (s->have_crossing) {
		s->BCR = distance(radar, &vector_xy_null, &s->cross);
		bearing = fmod(360.0 + course(radar, &vector_xy_null, &s->cross) - radar->own_course, 360.0);
		if (fabs(bearing - 180.0) < 1.0)
			s->BCR *= -1.0;
		s->BCT = dtime(radar, &s->sight[1], &s->cross, s->KBr, s->vBr);
		s->BCt = add_time(radar, s->time[1], s->BCT);
	} else {
		s->BCR = -1.0;
		s->BCT = 0.0;
		s->BCt = 0;
	}

	if (s->index == radar->mtarget) {
		radar_calculate_maneuver(s);
	} else {
		radar_calculate_secondary(s);
	}

	if (s->have_new_cpa) {
		s->new_have_crossing = TRUE;
		if (fabs(s->new_cpa.x - s->mpoint.x) < EPSILON) {
			radar_sincos(radar, radar->ncourse, &sina, &cosa);
			if (fabs(cosa) < EPSILON) {
				s->new_cross.y = 0.0;
				s->new_cross.x = s->new_cpa.x;
			} else {
				delta_m = sina / cosa;
				if (fabs(delta_m) < EPSILON) {
					s->new_have_crossing = FALSE;
					s->new_cross.y = 0.0;
					s->new_cross.x = 0.0;
				} else {
					s->new_cross.y = s->new_cpa.x / delta_m;
					s->new_cross.x = s->new_cpa.x;
				}
			}
		} else {
			m = (s->new_cpa.y - s->mpoint.y) /
			    (s->new_cpa.x - s->mpoint.x);

			radar_sincos(radar, radar->ncourse, &sina, &cosa);
			if (fabs(sina) < EPSILON) {
				s->new_cross.x = 0.0;
				s->new_cross.y =
					m * (s->new_cross.x - s->new_cpa.x) +
					s->new_cpa.y;
			} else {
				delta_m = cosa / sina - m;
				if (fabs(delta_m) < EPSILON) {
					s->new_have_crossing = FALSE;
					s->new_cross.x = 0.0;
					s->new_cross.y = 0.0;
				} else {
					s->new_cross.x = (s->new_cpa.y -
							  m * s->new_cpa.x) /
							 delta_m;
					s->new_cross.y = m * (s->new_cross.x -
							      s->new_cpa.x) +
							 s->new_cpa.y;
				}
			}
		}

		s->new_vBr = speed(radar, &s->xpoint, &s->sight[1],
				   s->delta_time);
		s->new_CPA = distance(radar, &vector_xy_null, &s->new_cpa);
		s->new_TCPA = dtime(radar, &s->mpoint, &s->new_cpa,
				    s->new_KBr, s->new_vBr);
		exact_time = dtime(radar, &s->sight[1], &s->mpoint,
				   s->KBr, s->vBr);
		exact_time += s->new_TCPA;
		s->new_tCPA = add_time(radar, s->time[1], exact_time);
		if (fabs(s->new_CPA) >= EPSILON) {
			s->new_PCPA = course(radar, &vector_xy_null,
					     &s->new_cpa);
			s->new_SPCPA = fmod(360.0 + s->new_PCPA -
					    radar->ncourse, 360.0);
		} else {
			s->new_PCPA = -1.0;
			s->new_SPCPA = -1.0;
		}

		s->delta = fabs(s->new_KBr - s->KBr);
		if (s->delta > 180.0)
			s->delta = 360.0 - s->delta;

		s->new_RaSP = fmod(360.0 + course(radar, &vector_xy_null,
						  &s->mpoint) -
				   radar->ncourse, 360.0);
		s->new_aspect = fmod(360.0 + course(radar, &s->mpoint,
						    &vector_xy_null) -
				     s->KB, 360.0);

		if (s->new_have_crossing) {
			s->new_BCR = distance(radar, &vector_xy_null,
					      &s->new_cross);
			bearing = fmod(360.0 + course(radar, &vector_xy_null, &s->new_cross) - radar->ncourse, 360.0);
			if (fabs(bearing - 180.0) < 1.0)
				s->new_BCR *= -1.0;
			s->new_BCT = dtime(radar, &s->mpoint, &s->new_cross,
					    s->new_KBr, s->new_vBr);
			exact_time = dtime(radar, &s->sight[1], &s->mpoint,
					   s->KBr, s->vBr);
			exact_time += s->new_BCT;
			s->new_BCt = add_time(radar, s->time[1], exact_time);
		} else {
			s->new_BCR = -1.0;
			s->new_BCT = 0.0;
			s->new_BCt = 0;
		}
	}

	if (fabs(s->vBr) < EPSILON) {
		gtk_entry_set_text(s->KBr_entry, "-");
	} else {
		radar_set_course_entry(s->KBr_entry, s->KBr);
	}
	radar_set_scalar_entry(s->vBr_entry, s->vBr);
	if (fabs(s->vB) < EPSILON) {
		gtk_entry_set_text(s->KB_entry, "-");
	} else {
		radar_set_course_entry(s->KB_entry, s->KB);
	}
	radar_set_scalar_entry(s->vB_entry, s->vB);
	radar_set_course_entry(s->aspect_entry, s->aspect);
	radar_set_scalar_entry(s->CPA_entry, s->CPA);
	if (fabs(s->CPA) < EPSILON) {
		gtk_entry_set_text(s->PCPA_entry, "-");
		gtk_entry_set_text(s->SPCPA_entry, "-");
	} else {
		radar_set_course_entry(s->PCPA_entry, s->PCPA);
		radar_set_course_entry(s->SPCPA_entry, s->SPCPA);
	}
	if (fabs(s->vBr) < EPSILON) {
		gtk_entry_set_text(s->TCPA_entry, "-");
		gtk_entry_set_text(s->tCPA_entry, "-");
	} else {
		radar_set_scalar_entry(s->TCPA_entry, s->TCPA);
		snprintf(text, sizeof(text), "%02u%02u", s->tCPA / 60, s->tCPA % 60);
		gtk_entry_set_text(s->tCPA_entry, text);
	}

	if (s->have_crossing) {
		radar_set_scalar_entry(s->BCR_entry, s->BCR);
		radar_set_scalar_entry(s->BCT_entry, s->BCT);
		snprintf(text, sizeof(text), "%02u%02u", s->BCt / 60, s->BCt % 60);
		gtk_entry_set_text(s->BCt_entry, text);
	} else {
		gtk_entry_set_text(s->BCR_entry, "-");
		gtk_entry_set_text(s->BCT_entry, "-");
		gtk_entry_set_text(s->BCt_entry, "-");
	}

	radar_show_problems(GTK_WIDGET(radar->mcpa_spin), s->have_problems);
	radar_show_problems(GTK_WIDGET(radar->ncourse_spin), s->have_problems);
	radar_show_problems(GTK_WIDGET(radar->nspeed_spin), s->have_problems);

	if (!s->have_new_cpa)
		goto out_clear_new;

	if (fabs(s->new_vBr) < EPSILON) {
		gtk_entry_set_text(s->new_KBr_entry, "-");
	} else {
		radar_set_course_entry(s->new_KBr_entry, s->new_KBr);
	}
	radar_set_scalar_entry(s->new_vBr_entry, s->new_vBr);

	radar_set_scalar_entry(s->delta_entry, s->delta);
	radar_set_scalar_entry(s->new_RaSP_entry, s->new_RaSP);
	radar_set_course_entry(s->new_aspect_entry, s->new_aspect);

	radar_show_problems(GTK_WIDGET(s->new_CPA_entry), s->have_problems);
	radar_set_scalar_entry(s->new_CPA_entry, s->new_CPA);

	if (fabs(s->new_CPA) < EPSILON) {
		gtk_entry_set_text(s->new_PCPA_entry, "-");
		gtk_entry_set_text(s->new_SPCPA_entry, "-");
	} else {
		radar_set_course_entry(s->new_PCPA_entry, s->new_PCPA);
		radar_set_course_entry(s->new_SPCPA_entry, s->new_SPCPA);
	}

	if (fabs(s->new_vBr) < EPSILON) {
		gtk_entry_set_text(s->new_TCPA_entry, "-");
		gtk_entry_set_text(s->new_tCPA_entry, "-");
	} else {
		radar_set_scalar_entry(s->new_TCPA_entry, s->new_TCPA);
		snprintf(text, sizeof(text),
			 "%02u%02u", s->new_tCPA / 60, s->new_tCPA % 60);
		gtk_entry_set_text(s->new_tCPA_entry, text);
	}

	if (s->new_have_crossing) {
		radar_set_scalar_entry(s->new_BCR_entry, s->new_BCR);
		radar_set_scalar_entry(s->new_BCT_entry, s->new_BCT);
		snprintf(text, sizeof(text),
			 "%02u%02u", s->new_BCt / 60, s->new_BCt % 60);
		gtk_entry_set_text(s->new_BCt_entry, text);
	} else {
		gtk_entry_set_text(s->new_BCR_entry, "-");
		gtk_entry_set_text(s->new_BCT_entry, "-");
		gtk_entry_set_text(s->new_BCt_entry, "-");
	}

	return;

out_clear_all:
	gtk_entry_set_text(s->KBr_entry, "");
	gtk_entry_set_text(s->vBr_entry, "");
	gtk_entry_set_text(s->KB_entry, "");
	gtk_entry_set_text(s->vB_entry, "");
	gtk_entry_set_text(s->aspect_entry, "");
	gtk_entry_set_text(s->CPA_entry, "");
	gtk_entry_set_text(s->PCPA_entry, "");
	gtk_entry_set_text(s->SPCPA_entry, "");
	gtk_entry_set_text(s->TCPA_entry, "");
	gtk_entry_set_text(s->tCPA_entry, "");
	gtk_entry_set_text(s->BCR_entry, "");
	gtk_entry_set_text(s->BCT_entry, "");
	gtk_entry_set_text(s->BCt_entry, "");

out_clear_new:
	gtk_entry_set_text(s->new_KBr_entry, "");
	gtk_entry_set_text(s->new_vBr_entry, "");
	gtk_entry_set_text(s->delta_entry, "");
	gtk_entry_set_text(s->new_RaSP_entry, "");
	gtk_entry_set_text(s->new_aspect_entry, "");
	gtk_entry_set_text(s->new_CPA_entry, "");
	gtk_entry_set_text(s->new_PCPA_entry, "");
	gtk_entry_set_text(s->new_SPCPA_entry, "");
	gtk_entry_set_text(s->new_TCPA_entry, "");
	gtk_entry_set_text(s->new_tCPA_entry, "");
	gtk_entry_set_text(s->new_BCR_entry, "");
	gtk_entry_set_text(s->new_BCT_entry, "");
	gtk_entry_set_text(s->new_BCt_entry, "");
}

static void
radar_mark_vector(radar_t *radar, target_t *s, enum target_vector_number type,
		  GdkGC *gc, double x1, double y1, double x2, double y2)
{
	double dx, dy, l, alpha;
	double cx, cy, ex, ey;
	double sina, cosa;
	double sx, sy;
	poly_t *p;

	dx = x2 - x1;
	dy = y2 - y1;

	l = sqrt(dx * dx + dy * dy);

	alpha = atan2(dy, dx);

	sina = sin(alpha);
	cosa = cos(alpha);

	switch (type) {
	case VECTOR_OWN:
		if (l < 10)
			return;
		p = &s->polys[POLY_OWN_ARROW];
		break;
	case VECTOR_TRUE:
		if (l < 18)
			return;
		p = &s->polys[POLY_TRUE_ARROW0];
		break;
	case VECTOR_NEW_OWN:
		if (l < 10)
			return;
		p = &s->polys[POLY_NEW_OWN_ARROW];
		break;
	case VECTOR_RELATIVE:
		if (l < 14)
			return;
		p = &s->polys[POLY_RELATIVE_ARROW];
		break;
	default:
		return;
	}

	switch (type) {
	case VECTOR_OWN:
	case VECTOR_NEW_OWN:
	case VECTOR_RELATIVE:
		cx = x1 + l / 2.0 * cosa;
		cy = y1 + l / 2.0 * sina;

		sx = cx + 5.0 * cosa;
		sy = cy + 5.0 * sina;

		ex = cx - 3.66 * cosa;
		ey = cy - 3.66 * sina;

		p->is_visible = 1;
		p->gc = gc;

		p->points[0].x = ex - 4.0 * sina;
		p->points[0].y = ey + 4.0 * cosa;
		p->points[1].x = sx;
		p->points[1].y = sy;
		p->points[2].x = ex + 4.0 * sina;
		p->points[2].y = ey - 4.0 * cosa;
		p->npoints = 3;

		p->bbox.x = d2i(cx) - 7;
		p->bbox.y = d2i(cy) - 7;
		p->bbox.width = 14;
		p->bbox.height = 14;

		gdk_window_invalidate_rect(radar->canvas->window,
					   &p->bbox, FALSE);

		if (type != VECTOR_RELATIVE)
			break;

		radar_set_arc(radar, &s->arcs[ARC_RELATIVE_ARROW], gc,
			      cx - cosa, cy - sina, 7.0, 0.0, 360.0);
		break;

	case VECTOR_TRUE:
		cx = x1 + l / 2.0 * cosa;
		cy = y1 + l / 2.0 * sina;

		sx = cx + 8.0 * cosa;
		sy = cy + 8.0 * sina;

		ex = cx - 0.66 * cosa;
		ey = cy - 0.66 * sina;

		p->is_visible = 1;
		p->gc = gc;

		p->points[0].x = ex - 4.0 * sina;
		p->points[0].y = ey + 4.0 * cosa;
		p->points[1].x = sx;
		p->points[1].y = sy;
		p->points[2].x = ex + 4.0 * sina;
		p->points[2].y = ey - 4.0 * cosa;
		p->npoints = 3;

		p->bbox.x = d2i(cx) - 9;
		p->bbox.y = d2i(cy) - 9;
		p->bbox.width = 20;
		p->bbox.height = 20;

		gdk_window_invalidate_rect(radar->canvas->window,
					   &p->bbox, FALSE);

		p = p + 1;

		sx = cx + 0.0 * cosa;
		sy = cy + 0.0 * sina;

		ex = cx - 8.66 * cosa;
		ey = cy - 8.66 * sina;

		p->is_visible = 1;
		p->gc = gc;

		p->points[0].x = ex - 4.0 * sina;
		p->points[0].y = ey + 4.0 * cosa;
		p->points[1].x = sx;
		p->points[1].y = sy;
		p->points[2].x = ex + 4.0 * sina;
		p->points[2].y = ey - 4.0 * cosa;
		p->npoints = 3;

		p->bbox.x = d2i(cx) - 9;
		p->bbox.y = d2i(cy) - 9;
		p->bbox.width = 20;
		p->bbox.height = 20;

		gdk_window_invalidate_rect(radar->canvas->window,
					   &p->bbox, FALSE);

		break;
	default:
		break;
	}
}

static void
radar_draw_foreground(radar_t *radar)
{
	vector_xy_t re, xp;
	gboolean have_rel_ext;
	double sina, cosa;
	double sins[2], coss[2];
	double dx[2], dy[2];
	double r, d, crs;
	double xs[2], ys[2];
	double x, y, x2, y2;
	char text[32];
	int delta_time;
	vector_t *v;
	poly_t *p;
	arc_t *a;
	text_label_t *l;
	target_t *s;
	int i, j;

	if (radar->change_level > 1)
		return;

	if (!radar->mapped)
		return;

	for (i = 0; i < RADAR_NR_VECTORS; i++) {
		v = &radar->vectors[i];

		if (!v->is_visible)
			continue;

		gdk_window_invalidate_rect(radar->canvas->window,
					   &v->bbox, TRUE);
		v->is_visible = 0;
	}

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		s = &radar->target[i];

		for (j = 0; j < TARGET_NR_ARCS; j++) {
			a = &s->arcs[j];

			if (!a->is_visible)
				continue;

			gdk_window_invalidate_rect(radar->canvas->window,
						   &a->bbox, TRUE);
			a->is_visible = 0;
		}

		for (j = 0; j < TARGET_NR_POLYS; j++) {
			p = &s->polys[j];

			if (!p->is_visible)
				continue;

			gdk_window_invalidate_rect(radar->canvas->window,
						   &p->bbox, TRUE);
			p->is_visible = 0;
		}

		for (j = 0; j < TARGET_NR_VECTORS; j++) {
			v = &s->vectors[j];

			if (!v->is_visible)
				continue;

			gdk_window_invalidate_rect(radar->canvas->window,
						   &v->bbox, TRUE);
			v->is_visible = 0;
		}

		for (j = 0; j < TARGET_NR_LABELS; j++) {
			l = &s->labels[j];

			if (!l->is_visible)
				continue;

			gdk_window_invalidate_rect(radar->canvas->window,
						   &l->bbox, TRUE);
			l->is_visible = 0;
		}
	}

	if (radar->show_heading) {
		radar_sincos(radar, radar->own_course, &sina, &cosa);

		x = radar->cx + i2d(radar->r) * sina;
		y = radar->cy - i2d(radar->r) * cosa;

		radar_set_vector(radar, &radar->vectors[VECTOR_HEADING],
				 radar->green_gc, radar->cx, radar->cy, x, y);
	}


	radar->maneuver = MANEUVER_NONE;

	s = &radar->target[radar->mtarget];
	radar_calculate_target(s);

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		if (i == radar->mtarget)
			continue;

		s = &radar->target[i];
		radar_calculate_target(s);
	}


	s = &radar->target[radar->mtarget];
	if (s->have_new_cpa && radar->show_heading) {
		radar_sincos(radar, radar->ncourse, &sina, &cosa);

		x = radar->cx + i2d(radar->r) * sina;
		y = radar->cy - i2d(radar->r) * cosa;

		radar_set_vector(radar, &radar->vectors[VECTOR_NEW_HEADING],
				 radar->green_dash_gc,
				 radar->cx, radar->cy, x, y);
	}

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		s = &radar->target[i];

		for (j = 0; j < 2; j++) {
			if (s->distance[j]) {
				radar_sincos(radar, s->rakrp[j],
					     &sins[j], &coss[j]);

				r = i2d(radar->r) * s->distance[j] / radar->range;

				dx[j] = r * sins[j];
				dy[j] = r * coss[j];

				xs[j] = radar->cx + dx[j];
				ys[j] = radar->cy - dy[j];

				radar_set_vector(radar,
						 &s->vectors[VECTOR_POSX0 + j],
						 s->pos_gc, xs[j] - 4, ys[j],
							    xs[j] + 4, ys[j]);
				radar_set_vector(radar,
						 &s->vectors[VECTOR_POSY0 + j],
						 s->pos_gc, xs[j], ys[j] - 4,
							    xs[j], ys[j] + 4);
			}
		}

		if ((s->distance[0] != 0.0) && (s->distance[1] != 0.0)) {
			radar_set_vector(radar, &s->vectors[VECTOR_RELATIVE],
					 s->vec_gc, xs[0], ys[0], xs[1], ys[1]);
			radar_mark_vector(radar, s, VECTOR_RELATIVE,
					  s->vec_mark_gc,
					  xs[0], ys[0], xs[1], ys[1]);

			have_rel_ext = extend(radar, &s->sight[0], &s->sight[1],
					      s->KBr, s->vBr, &re);

			if (!have_rel_ext && s->have_cpa) {
				re.x = s->cpa.x;
				re.y = s->cpa.y;
				have_rel_ext = TRUE;
			}

			if (have_rel_ext && fabs(s->vBr) >= EPSILON) {
				x = radar->cx + i2d(radar->r) * re.x / radar->range;
				y = radar->cy - i2d(radar->r) * re.y / radar->range;
				radar_set_vector(radar,
						 &s->vectors[VECTOR_REL_EXT],
						 s->ext_gc, xs[1], ys[1],
						 x, y);
			}

			if (s->have_cpa && fabs(s->vBr) >= EPSILON) {
				x = radar->cx + i2d(radar->r) * s->cpa.x / radar->range;
				y = radar->cy - i2d(radar->r) * s->cpa.y / radar->range;
				radar_set_vector(radar,
						 &s->vectors[VECTOR_CPA],
						 s->cpa_gc,
						 radar->cx, radar->cy, x, y);
			}

			delta_time = s->time[1] - s->time[0];
			if (delta_time < 0)
				delta_time = 1440 + s->time[1] - s->time[0];

			if (delta_time > 0) {
				d = radar->own_speed *
						((double) delta_time) / 60.0;

				if (d != 0.0) {
					radar_sincos(radar,
						(180 + radar->own_course) % 360,
						&sina, &cosa);

					r = i2d(radar->r) * d / radar->range;

					x = radar->cx + dx[0] + r * sina;
					y = radar->cy - (dy[0] + r * cosa);

					radar_set_vector(radar,
						&s->vectors[VECTOR_OWN],
						s->own_gc, x, y, xs[0], ys[0]);
					radar_mark_vector(radar, s,
						VECTOR_OWN, s->own_mark_gc,
						x, y, xs[0], ys[0]);

					radar_set_vector(radar,
						&s->vectors[VECTOR_TRUE],
						s->true_gc, x, y, xs[1], ys[1]);

					radar_mark_vector(radar, s, VECTOR_TRUE,
						s->true_mark_gc,
						x, y, xs[1], ys[1]);
				}
			}

			if (s->have_mpoint) {
				x = radar->cx + i2d(radar->r) * s->mpoint.x / radar->range;
				y = radar->cy - i2d(radar->r) * s->mpoint.y / radar->range;

				radar_set_vector(radar,
						 &s->vectors[VECTOR_MPOINTX],
						 s->pos_gc, x - 4, y,
							    x + 4, y);
				radar_set_vector(radar,
						 &s->vectors[VECTOR_MPOINTY],
						 s->pos_gc, x, y - 4,
							    x, y + 4);

				if (s->have_new_cpa) {
					if ((fabs(s->mpoint.x - s->sight[1].x) > EPSILON) ||
					    (fabs(s->mpoint.y - s->sight[1].y) > EPSILON)) {
						xp.x = s->mpoint.x;
						xp.y = s->mpoint.y;
					} else {
						xp.x = s->xpoint.x;
						xp.y = s->xpoint.y;
					}

					have_rel_ext = extend(radar, &s->new_cpa, &xp,
							      fmod(180 + s->new_KBr, 360.0), s->new_vBr, &re);
					if (!have_rel_ext) {
						re.x = xp.x;
						re.y = xp.y;
					}

					x = radar->cx + i2d(radar->r) * re.x / radar->range;
					y = radar->cy - i2d(radar->r) * re.y / radar->range;

					have_rel_ext = extend(radar, &s->mpoint, &s->new_cpa,
							      s->new_KBr, s->new_vBr, &re);
					if (!have_rel_ext) {
						re.x = s->new_cpa.x;
						re.y = s->new_cpa.y;
					}

					x2 = radar->cx + i2d(radar->r) * re.x / radar->range;
					y2 = radar->cy - i2d(radar->r) * re.y / radar->range;

					radar_set_vector(radar,
							 &s->vectors[VECTOR_NEW_REL0],
							 s->ext_gc,
							 x, y, x2, y2);

					x = radar->cx + i2d(radar->r) * s->new_cpa.x / radar->range;
					y = radar->cy - i2d(radar->r) * s->new_cpa.y / radar->range;
					radar_set_vector(radar,
							 &s->vectors[VECTOR_NEW_CPA],
							 s->cpa_gc,
							 radar->cx, radar->cy, x, y);


					if ((fabs(s->mpoint.x - s->sight[1].x) > EPSILON) ||
					    (fabs(s->mpoint.y - s->sight[1].y) > EPSILON)) {
						have_rel_ext = extend(radar, &s->sight[1], &s->xpoint,
								      fmod(180.0 + s->new_KBr, 360.0), s->new_vBr, &re);
						if (!have_rel_ext) {
							re.x = s->xpoint.x;
							re.y = s->xpoint.y;
						}

						x = radar->cx + i2d(radar->r) * s->sight[1].x / radar->range;
						y = radar->cy - i2d(radar->r) * s->sight[1].y / radar->range;
						x2 = radar->cx + i2d(radar->r) * re.x / radar->range;
						y2 = radar->cy - i2d(radar->r) * re.y / radar->range;
						radar_set_vector(radar,
								 &s->vectors[VECTOR_NEW_REL1],
								 s->ext_dash_gc,
								 x, y, x2, y2);
					}

					x = radar->cx + i2d(radar->r) * s->p0_sub_own.x / radar->range;
					y = radar->cy - i2d(radar->r) * s->p0_sub_own.y / radar->range;
					x2 = radar->cx + i2d(radar->r) * s->xpoint.x / radar->range;
					y2 = radar->cy - i2d(radar->r) * s->xpoint.y / radar->range;
					radar_set_vector(radar,
							 &s->vectors[VECTOR_NEW_OWN],
							 s->own_gc,
							 x, y, x2, y2);

					switch (radar->maneuver) {
					case MANEUVER_COURSE_FROM_CPA:
					case MANEUVER_CPA_FROM_COURSE:
						radar_mark_vector(radar, s,
								  VECTOR_NEW_OWN,
								  s->own_mark_gc,
								  x, y, x2, y2);
						break;
					default:
						break;
					}

					switch (radar->maneuver) {
					case MANEUVER_COURSE_FROM_CPA:
						if (s->index != radar->mtarget)
							break;

						d = radar->own_speed *
							((double) delta_time) / 60.0;

						x = radar->cx + i2d(radar->r) * s->p0_sub_own.x / radar->range;
						y = radar->cy - i2d(radar->r) * s->p0_sub_own.y / radar->range;
						x2 = radar->r * d / radar->range;

						if (radar->north_up == FALSE)
							crs = 0.0;
						else
							crs = radar->own_course;

						radar_set_arc(radar,
							      &s->arcs[ARC_COURSE],
							      s->arc_gc,
							      x, y, x2,
							      -fmod(crs + 270.0, 360.0),
							      -radar->direction * fmod(360.0 + radar->direction * (radar->ncourse - radar->own_course), 360.0));
						break;

					default:
						break;
					}
				}

			}
		}

		for (j = 0; j < 2; j++) {
			if (0.0 == s->distance[j])
				continue;

			snprintf(text, sizeof(text), "%c<sub>%02u%02u</sub>",
				'B' + s->index,
				s->time[j] / 60, s->time[j] % 60);

			radar_set_label(radar, s, &s->labels[LABEL_SIGHT0 + j],
					s->pos_gc, radar->white_gc,
					xs[j], ys[j], text, strlen(text));
		}
	}
}

static GdkBitmap *
radar_create_clip_mask(double cx, double cy, int w, int h, int ri, int ro)
{
	GdkBitmap *bitmap;
	GdkColor color;
	GdkGC *gc;

	bitmap = gdk_pixmap_new(NULL, w, h, 1);
	gc = gdk_gc_new(bitmap);
	gdk_gc_set_line_attributes(gc, 0, GDK_LINE_SOLID,
				   GDK_CAP_ROUND, GDK_JOIN_ROUND);

	color.pixel = 0;
	gdk_gc_set_foreground(gc, &color);
	gdk_draw_rectangle(bitmap, gc, TRUE, 0, 0, w, h);

	color.pixel = 1;
	gdk_gc_set_foreground(gc, &color);
	gdk_draw_arc(bitmap, gc, TRUE,
		     d2i(cx - i2d(ro)), d2i(cy - i2d(ro)),
		     d2i(2.0 * i2d(ro)), d2i(2.0 * i2d(ro)), 0, 360 * 64);

	color.pixel = 0;
	gdk_gc_set_foreground(gc, &color);
	gdk_draw_arc(bitmap, gc, TRUE,
		     d2i(cx - i2d(ri)), d2i(cy - i2d(ri)),
		     d2i(2.0 * i2d(ri)), d2i(2.0 * i2d(ri)), 0, 360 * 64);

	g_object_unref(gc);
	return bitmap;
}

/* Create a new backing pixmap of the appropriate size */
static int
configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer user_data)
{
	radar_t *radar = user_data;
	int tw, th, tick;
	char text[16];

	if (!radar->white_gc)
		radar_init_private_data(radar);

	if (radar->pixmap) {
		g_object_unref(radar->pixmap);
		radar->pixmap = NULL;
	}
	if (radar->clip[0]) {
		g_object_unref(radar->clip[0]);
		radar->clip[0] = NULL;
	}
	if (radar->clip[1]) {
		g_object_unref(radar->clip[1]);
		radar->clip[1] = NULL;
	}
	if (radar->clip[2]) {
		g_object_unref(radar->clip[2]);
		radar->clip[2] = NULL;
	}
	if (radar->clip[3]) {
		g_object_unref(radar->clip[3]);
		radar->clip[3] = NULL;
	}

	if (NULL == radar->busy_cursor)
		radar->busy_cursor = gdk_cursor_new(GDK_WATCH);

	radar->w = widget->allocation.width;
	radar->h = widget->allocation.height;

	radar->pixmap = gdk_pixmap_new(radar->canvas->window,
				       radar->w, radar->h, -1);

#ifndef HAVE_RENDER
	if (radar->backbuf) {
		g_object_unref(radar->backbuf);
		radar->backbuf = NULL;
	}
	if (radar->forebuf) {
		g_object_unref(radar->forebuf);
		radar->forebuf = NULL;
	}

	if (radar->do_render) {
		radar->forebuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
					        radar->w, radar->h);
		radar->backbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
					        radar->w, radar->h);
	}
#endif

	radar->cx = ((double) radar->w) / 2;
	radar->cy = ((double) radar->h) / 2;

	snprintf(text, sizeof(text), "%03u", 270);
	pango_layout_set_text(radar->layout, text, strlen(text));

	pango_layout_get_pixel_size(radar->layout, &tw, &th);
	tw += BG_TEXT_SIDE_SPACING;
	if (radar->w < radar->h)
		radar->r = ((radar->w / 2 - tw) / 12) * 12;
	else
		radar->r = ((radar->h / 2 - tw) / 12) * 12;
	radar->step = radar->r / 6;

	tick = radar->step / 12;
	if (tick < 4)
		tick = 4;

	if (!radar->do_render) {
		radar->clip[0] = radar_create_clip_mask(radar->cx, radar->cy,
							radar->w, radar->h,
							radar->r - tick,
							radar->r);
		gdk_gc_set_clip_mask(radar->grey50_clip0_gc, radar->clip[0]);

		radar->clip[1] = radar_create_clip_mask(radar->cx, radar->cy,
							radar->w, radar->h, 
							radar->r - 2 * tick,
							radar->r);
		gdk_gc_set_clip_mask(radar->grey50_clip1_gc, radar->clip[1]);

		radar->clip[2] = radar_create_clip_mask(radar->cx, radar->cy,
							radar->w, radar->h,
							3 * radar->step / 4,
							radar->r);
		gdk_gc_set_clip_mask(radar->grey25_clip2_gc, radar->clip[2]);

		radar->clip[3] = radar_create_clip_mask(radar->cx, radar->cy,
							radar->w, radar->h,
							1 * radar->step / 4,
							radar->r);
		gdk_gc_set_clip_mask(radar->grey50_clip3_gc, radar->clip[3]);
	}

	gdk_window_set_cursor(radar->window->window, radar->busy_cursor);

	radar->mapped = 1;

	radar_draw_background(radar);
	radar_draw_foreground(radar);

	gdk_window_set_cursor(radar->window->window, NULL);

	if (radar->load_pending) {
		radar->load_pending = FALSE;
		radar_load(radar, radar->plot_filename);
	}

	return TRUE;
}

/* Redraw the screen from the backing pixmap */
static int
expose_event(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
	radar_t *radar = user_data;
	GdkRectangle *rects;
	int i, n;

	gdk_region_get_rectangles(event->region, &rects, &n);
	for (i = 0; i < n; i++) {
		gdk_draw_drawable(widget->window,
				  widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
				  radar->pixmap,
				  rects[i].x, rects[i].y,
				  rects[i].x, rects[i].y,
				  rects[i].width, rects[i].height);
	}
	g_free(rects);

	radar_draw_vectors(radar);

	radar->wait_expose = FALSE;

	if (radar->redraw_pending) {
		radar->redraw_pending = FALSE;

		gdk_window_set_cursor(radar->window->window,
				      radar->busy_cursor);

		radar_draw_background(radar);
		radar_draw_foreground(radar);

		gdk_window_set_cursor(radar->window->window, NULL);
	}

	return FALSE;
}

static void
orientation_changed(GtkComboBox *combo, gpointer user_data)
{
	radar_t *radar = user_data;
	gboolean north_up;

	radar->change_level++;

	if (gtk_combo_box_get_active(combo) == 0)
		north_up = TRUE;
	else
		north_up = FALSE;

	if (north_up == radar->north_up) {
		radar->change_level--;
		return;
	}
	radar->north_up = north_up;

#ifdef DEBUG
	printf("%s: north up: %u\n", __FUNCTION__, radar->north_up);
#endif

	if (radar->own_course != 0)
		radar_draw_foreground(radar);

	radar->change_level--;
}

static void
heading_toggled(GtkToggleButton *toggle, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->change_level++;

	if (gtk_toggle_button_get_active(toggle))
		radar->show_heading = TRUE;
	else
		radar->show_heading = FALSE;

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: heading line: %u\n", __FUNCTION__, radar->show_heading);
		radar_draw_foreground(radar);
#endif
}

static void
range_value_changed(GtkSpinButton *button, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->change_level++;

	radar->rindex = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(button));
	radar->range = radar_ranges[radar->rindex].range;

	gdk_window_set_cursor(radar->window->window, radar->busy_cursor);

	radar_draw_background(radar);
	radar_draw_foreground(radar);

	gdk_window_set_cursor(radar->window->window, NULL);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: range %.1f\n", __FUNCTION__, radar->range);
#endif
}

static void
own_course_value_changed(GtkSpinButton *button, gpointer user_data)
{
	radar_t *radar = user_data;
	double old, ncourse, delta;
	gboolean select;
	target_t *s;
	int i, j;

	radar->change_level++;

	old = (double) radar->own_course;

	radar->own_course = gtk_spin_button_get_value_as_int(button);

	ncourse = gtk_spin_button_get_value(radar->ncourse_spin);
	delta = fmod(360.0 + ((double) radar->own_course) - old, 360.0);
	gtk_spin_button_set_value(radar->ncourse_spin,
				  fmod(ncourse + delta, 360.0));

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		s = &radar->target[i];

		for (j = 0; j < 2; j++) {
			select = s->rasp_selected[j];
			if (fabs(s->distance[j]) > EPSILON)
				s->rasp_selected[j] = FALSE;

			gtk_spin_button_set_value(s->rasp_course_spin[j],
						  (360 + radar->own_course
						   - s->rasp_course_offset[j])
						   % 360);

			s->rasp_selected[j] = select;
		}
	}

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: own_course %u\n", __FUNCTION__, radar->own_course);
#endif
}

static void
own_speed_value_changed(GtkSpinButton *button, gpointer user_data)
{
	radar_t *radar = user_data;
	double old, nspeed, delta;

	radar->change_level++;

	old = (double) radar->own_speed;

	radar->own_speed = gtk_spin_button_get_value(button);

	nspeed = gtk_spin_button_get_value(radar->nspeed_spin);
	delta = old - nspeed;
	nspeed = radar->own_speed - delta;
	if (nspeed < 0.0)
		nspeed = 0.0;
	gtk_spin_button_set_value(radar->nspeed_spin, nspeed);

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: own_speed %.1f\n", __FUNCTION__, radar->own_speed);
#endif
}

static void
time0_value_changed(GtkSpinButton *button, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	s->time[0] = gtk_spin_button_get_value_as_int(button);

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: Time 0: %02u:%02u (%u)\n", __FUNCTION__,
		s->time[0] / 60, s->time[0] % 60, s->time[0]);
#endif
}

static void
time1_value_changed(GtkSpinButton *button, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	s->time[1] = gtk_spin_button_get_value_as_int(button);

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: Time 1: %02u:%02u (%u)\n", __FUNCTION__,
		s->time[1] / 60, s->time[1] % 60, s->time[1]);
#endif
}

static void
rasp0_toggled(GtkToggleButton *toggle, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	if (gtk_toggle_button_get_active(toggle)) {
		s->rasp_selected[0] = TRUE;
		gtk_widget_set_sensitive(GTK_WIDGET(s->rakrp_spin[0]), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(s->rasp_spin[0]), TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(s->rasp_course_spin[0]),
					 TRUE);
	} else {
		s->rasp_selected[0] = FALSE;
		gtk_widget_set_sensitive(GTK_WIDGET(s->rasp_spin[0]), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(s->rasp_course_spin[0]),
					 FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(s->rakrp_spin[0]), TRUE);
	}

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: RaSP 0: %s\n", __FUNCTION__,
		s->rasp_selected[0] ? "selected" : "deselected");
#endif
}

static void
rasp0_course_changed(GtkSpinButton *button, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	s->rasp_course_offset[0] = radar->own_course -
				   gtk_spin_button_get_value_as_int(button);
	if (s->rasp_selected[0]) {
		s->rakrp[0] = (360 + radar->own_course
			       - s->rasp_course_offset[0]
			       + s->rasp[0]) % 360;
		gtk_spin_button_set_value(s->rakrp_spin[0], s->rakrp[0]);
	} else {
		s->rasp[0] = (360 + s->rakrp[0]
			      + s->rasp_course_offset[0]
			      - radar->own_course) % 360;
		gtk_spin_button_set_value(s->rasp_spin[0], s->rasp[0]);
	}

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: RaSP rwK 0%s: %u\n", __FUNCTION__,
		s->rasp_selected[0] ? " (selected)" : "", s->rasp[0]);
#endif
}

static void
rasp0_value_changed(GtkSpinButton *button, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	s->rasp[0] = gtk_spin_button_get_value_as_int(button);
	s->rakrp[0] = (360 + s->rasp[0] + radar->own_course - s->rasp_course_offset[0]) % 360;
	gtk_spin_button_set_value(s->rakrp_spin[0], s->rakrp[0]);

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: RaSP 0%s: %u\n", __FUNCTION__,
		s->rasp_selected[0] ? " (selected)" : "", s->rasp[0]);
#endif
}

static void
rakrp0_value_changed(GtkSpinButton *button, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	s->rakrp[0] = gtk_spin_button_get_value_as_int(button);
	s->rasp[0] = (720 + s->rakrp[0] - (radar->own_course - s->rasp_course_offset[0])) % 360;
	gtk_spin_button_set_value(s->rasp_spin[0], s->rasp[0]);

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: RaKrP 0%s: %u\n", __FUNCTION__,
		s->rasp_selected[0] ? "" : " (selected)", s->rakrp[0]);
#endif
}

static void
rasp1_toggled(GtkToggleButton *toggle, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	if (gtk_toggle_button_get_active(toggle)) {
		s->rasp_selected[1] = TRUE;
		gtk_widget_set_sensitive(GTK_WIDGET(s->rakrp_spin[1]), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(s->rasp_spin[1]), TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(s->rasp_course_spin[1]),
					 TRUE);
	} else {
		s->rasp_selected[1] = FALSE;
		gtk_widget_set_sensitive(GTK_WIDGET(s->rasp_spin[1]), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(s->rasp_course_spin[1]),
					 FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(s->rakrp_spin[1]), TRUE);
	}

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: RaSP 1: %s\n", __FUNCTION__,
		s->rasp_selected[1] ? "selected" : "deselected");
#endif
}

static void
rasp1_course_changed(GtkSpinButton *button, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	s->rasp_course_offset[1] = radar->own_course -
				   gtk_spin_button_get_value_as_int(button);
	if (s->rasp_selected[1]) {
		s->rakrp[1] = (360 + radar->own_course
			       - s->rasp_course_offset[1]
			       + s->rasp[1]) % 360;
		gtk_spin_button_set_value(s->rakrp_spin[1], s->rakrp[1]);
	} else {
		s->rasp[1] = (360 + s->rakrp[1]
			      + s->rasp_course_offset[1]
			      - radar->own_course) % 360;
		gtk_spin_button_set_value(s->rasp_spin[1], s->rasp[1]);
	}

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: RaSP rwK 1%s: %u\n", __FUNCTION__,
		s->rasp_selected[1] ? " (selected)" : "", s->rasp[1]);
#endif
}

static void
rasp1_value_changed(GtkSpinButton *button, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	s->rasp[1] = gtk_spin_button_get_value_as_int(button);
	s->rakrp[1] = (360 + s->rasp[1] + radar->own_course - s->rasp_course_offset[1]) % 360;
	gtk_spin_button_set_value(s->rakrp_spin[1], s->rakrp[1]);

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: RaSP 1%s: %u\n", __FUNCTION__,
		s->rasp_selected[1] ? " (selected)" : "", s->rasp[1]);
#endif
}

static void
rakrp1_value_changed(GtkSpinButton *button, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	s->rakrp[1] = gtk_spin_button_get_value_as_int(button);
	s->rasp[1] = (720 + s->rakrp[1] - (radar->own_course - s->rasp_course_offset[1])) % 360;
	gtk_spin_button_set_value(s->rasp_spin[1], s->rasp[1]);

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: RaKrP 1%s: %u\n", __FUNCTION__,
		s->rasp_selected[1] ? "" : " (selected)", s->rakrp[1]);
#endif
}

static void
dist0_value_changed(GtkSpinButton *button, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	s->distance[0] = gtk_spin_button_get_value(button);
	if (s->distance[0] < EPSILON)
		s->distance[0] = 0.0;

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: Distance 0: %.1f\n", __FUNCTION__, s->distance[0]);
#endif
}

static void
dist1_value_changed(GtkSpinButton *button, gpointer user_data)
{
	target_t *s = user_data;
	radar_t *radar = s->radar;

	radar->change_level++;

	s->distance[1] = gtk_spin_button_get_value(button);
	if (s->distance[1] < EPSILON)
		s->distance[1] = 0.0;

#ifdef DEBUG
	printf("%s: Distance 1: %.1f\n", __FUNCTION__, s->distance[1]);
#endif

	radar_draw_foreground(radar);

	radar->change_level--;
}

static void
mtime_toggled(GtkToggleButton *toggle, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->change_level++;

	if (gtk_toggle_button_get_active(toggle)) {
		radar->mtime_selected = TRUE;
		gtk_widget_set_sensitive(GTK_WIDGET(radar->mdist_spin), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(radar->mtime_spin), TRUE);
	} else {
		radar->mtime_selected = FALSE;
		gtk_widget_set_sensitive(GTK_WIDGET(radar->mtime_spin), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(radar->mdist_spin), TRUE);
	}

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: Maneuver Time: %s\n", __FUNCTION__,
		radar->mtime_selected ? "selected" : "deselected");
#endif
}

static void
mtime_value_changed(GtkSpinButton *button, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->change_level++;

	radar->mtime = gtk_spin_button_get_value_as_int(button);
	radar->mtime_set = TRUE;

#ifdef DEBUG
	printf("%s: Maneuver Time: %02u:%02u (%u)\n", __FUNCTION__,
		radar->mtime / 60, radar->mtime % 60, radar->mtime);
#endif

	if (radar->mtime_selected)
		radar_draw_foreground(radar);

	radar->change_level--;
}

static void
mdist_value_changed(GtkSpinButton *button, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->change_level++;

	radar->mdistance = gtk_spin_button_get_value(button);
	radar->mdist_set = TRUE;

#ifdef DEBUG
	printf("%s: Maneuver Distance: %.1f\n", __FUNCTION__, radar->mdistance);
#endif

	if (!radar->mtime_selected)
		radar_draw_foreground(radar);

	radar->change_level--;
}

static void
target_changed(GtkComboBox *combo, gpointer user_data)
{
	radar_t *radar = user_data;
	gint8 dashes[2] = { 3, 3 };
	target_t *t;
	int mtarget;
	int i;

	if (! radar->mapped)
		return;

	radar->change_level++;

	mtarget = gtk_combo_box_get_active(combo);
	if (mtarget == radar->mtarget) {
		radar->change_level--;
		return;
	}
	radar->mtarget = mtarget;

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		t = &radar->target[i];

		if (t->index == radar->mtarget) {
			gdk_gc_set_line_attributes(t->cpa_gc, 2,
						   GDK_LINE_SOLID,
						   GDK_CAP_ROUND,
						   GDK_JOIN_ROUND);

			gdk_gc_set_line_attributes(t->ext_gc, 0,
						   GDK_LINE_SOLID,
						   GDK_CAP_ROUND,
						   GDK_JOIN_ROUND);
		} else {
			gdk_gc_set_line_attributes(t->cpa_gc, 2,
						   GDK_LINE_ON_OFF_DASH,
						   GDK_CAP_ROUND,
						   GDK_JOIN_ROUND);
			gdk_gc_set_dashes(t->cpa_gc, 0, dashes, 2);

			gdk_gc_set_line_attributes(t->ext_gc, 0,
						   GDK_LINE_ON_OFF_DASH,
						   GDK_CAP_ROUND,
						   GDK_JOIN_ROUND);
			gdk_gc_set_dashes(t->ext_gc, 0, dashes, 2);
		}
	}

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: Target: %c\n", __FUNCTION__, 'B' + radar->target);
#endif
}

static void
maneuver_changed(GtkComboBox *combo, gpointer user_data)
{
	radar_t *radar = user_data;
	gboolean mcourse_change;

	radar->change_level++;

	if (gtk_combo_box_get_active(combo) == 0)
		mcourse_change = TRUE;
	else
		mcourse_change = FALSE;

	if (mcourse_change == radar->mcourse_change) {
		radar->change_level--;
		return;
	}
	radar->mcourse_change = mcourse_change;

	if (radar->mcourse_change) {
		if (gtk_toggle_button_get_active(radar->nspeed_radio))
			gtk_toggle_button_set_active(radar->ncourse_radio, TRUE);
		gtk_widget_show(GTK_WIDGET(radar->ncourse_radio));
		gtk_widget_hide(GTK_WIDGET(radar->nspeed_radio));
		gtk_widget_show(GTK_WIDGET(radar->ncourse_spin));
		gtk_widget_hide(GTK_WIDGET(radar->nspeed_spin));
	} else {
		if (gtk_toggle_button_get_active(radar->ncourse_radio))
			gtk_toggle_button_set_active(radar->nspeed_radio, TRUE);
		gtk_widget_show(GTK_WIDGET(radar->nspeed_radio));
		gtk_widget_hide(GTK_WIDGET(radar->ncourse_radio));
		gtk_widget_show(GTK_WIDGET(radar->nspeed_spin));
		gtk_widget_hide(GTK_WIDGET(radar->ncourse_spin));
	}

	radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: Maneuver: %s change\n", __FUNCTION__,
		radar->mcourse_change ? "course" : "speed");
#endif
}

static void
mcpa_toggled(GtkToggleButton *toggle, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->change_level++;

	if (gtk_toggle_button_get_active(toggle)) {
		radar->mcpa_selected = TRUE;
		gtk_widget_set_sensitive(GTK_WIDGET(radar->nspeed_spin), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(radar->ncourse_spin), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(radar->mcpa_spin), TRUE);
	} else {
		radar->mcpa_selected = FALSE;
		gtk_widget_set_sensitive(GTK_WIDGET(radar->mcpa_spin), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(radar->nspeed_spin), TRUE);
		gtk_widget_set_sensitive(GTK_WIDGET(radar->ncourse_spin), TRUE);
	}

	radar_draw_foreground(radar);

	radar->change_level--;
}

static void
mcpa_value_changed(GtkSpinButton *button, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->change_level++;

	radar->mcpa = gtk_spin_button_get_value(button);

	if (radar->mcpa_selected)
		radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: New CPA: %.1f\n", __FUNCTION__, radar->mcpa);
#endif
}

static void
ncourse_value_changed(GtkSpinButton *button, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->change_level++;

	radar->ncourse = gtk_spin_button_get_value(button);

	if (!radar->mcpa_selected)
		radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: New Course: %.1f\n", __FUNCTION__, radar->ncourse);
#endif
}

static void
nspeed_value_changed(GtkSpinButton *button, gpointer user_data)
{
	radar_t *radar = user_data;

	radar->change_level++;

	radar->nspeed = gtk_spin_button_get_value(button);

	if (!radar->mcpa_selected)
		radar_draw_foreground(radar);

	radar->change_level--;

#ifdef DEBUG
	printf("%s: New Speed: %.1f\n", __FUNCTION__, radar->nspeed);
#endif
}


static void
radar_set_orientation_tooltip(GtkWidget *widget, gpointer data)
{
	radar_t *radar = data;

	if (GTK_IS_BUTTON(widget)) {
		gtk_tooltips_set_tip(radar->tooltips, widget,
				     _("North Stabilized or Head Up Display"),
				     NULL);
	}
}

static void
radar_realize_orientation_combo(GtkWidget *combo, gpointer data)
{
	gtk_container_forall(GTK_CONTAINER(combo),
			     radar_set_orientation_tooltip, data);
}

static void
radar_set_target_tooltip(GtkWidget *widget, gpointer data)
{
	radar_t *radar = data;

	if (GTK_IS_BUTTON(widget)) {
		gtk_tooltips_set_tip(radar->tooltips, widget,
				     _("Target for Maneuver Calculations"), NULL);
	}
}

static void
radar_realize_target_combo(GtkWidget *combo, gpointer data)
{
	gtk_container_forall(GTK_CONTAINER(combo),
			     radar_set_target_tooltip, data);
}

static void
radar_set_maneuver_tooltip(GtkWidget *widget, gpointer data)
{
	radar_t *radar = data;

	if (GTK_IS_BUTTON(widget)) {
		gtk_tooltips_set_tip(radar->tooltips, widget,
				     _("Course or Speed Change"), NULL);
	}
}

static void
radar_realize_maneuver_combo(GtkWidget *combo, gpointer data)
{
	gtk_container_forall(GTK_CONTAINER(combo),
			     radar_set_maneuver_tooltip, data);
}

static void
radar_set_tooltip(radar_t *radar, GtkWidget *widget, const char *tip, int arg)
{
	gchar *p;

	p = g_strdup_printf(tip, arg);
	gtk_tooltips_set_tip(radar->tooltips, widget, p, NULL);
	g_free(p);
}

GtkWidget *
radar_init_spin(double min, double max, double step, double page, double init)
{
	GtkWidget *button;

	button = gtk_spin_button_new_with_range(min, max, step);
	gtk_spin_button_set_increments(GTK_SPIN_BUTTON(button), step, page);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(button), init);
	gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(button), FALSE);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(button), TRUE);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(button),
					  GTK_UPDATE_IF_VALID);
	gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(button), TRUE);
	gtk_entry_set_alignment(GTK_ENTRY(button), 1.0);

	return button;
}

static gboolean
course_spin_output(GtkSpinButton *button, gpointer user_data)
{
	gchar buffer[16];

	if (button->digits != 0)
		snprintf(buffer, sizeof(buffer),
			 "%05.1f", button->adjustment->value);
	else
		snprintf(buffer, sizeof(buffer),
			 "%03.0f", button->adjustment->value);
	if (strcmp(buffer, gtk_entry_get_text(GTK_ENTRY(button))))
		gtk_entry_set_text(GTK_ENTRY(button), buffer);
	return TRUE;
}

static GtkWidget *
radar_init_course_spin(void)
{
	GtkWidget *button;

	button = gtk_spin_button_new_with_range(0, 360, 1);
	gtk_spin_button_set_increments(GTK_SPIN_BUTTON(button), 1, 10);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(button), 0);
	gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(button), TRUE);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(button), TRUE);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(button),
					  GTK_UPDATE_IF_VALID);
	gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(button), TRUE);
	gtk_entry_set_alignment(GTK_ENTRY(button), 1.0);

	g_signal_connect(G_OBJECT(button), "output",
			 G_CALLBACK(course_spin_output), NULL);
	return button;
}

static gint
time_spin_input(GtkSpinButton *button, gdouble *value, gpointer user_data)
{
	const gchar *text;
	gchar *err = NULL;
	gint hhmm;

	text = gtk_entry_get_text(GTK_ENTRY(button));

	hhmm = strtoul(text, &err, 10);
	if (err == text || *err != '\0')
		return GTK_INPUT_ERROR;

	if ((hhmm % 100) > 59)
		return GTK_INPUT_ERROR;

	if (hhmm > 2359)
		return GTK_INPUT_ERROR;

	*value = (double) ((hhmm / 100) * 60 + (hhmm % 100));
	return TRUE;
}

static gboolean
time_spin_output(GtkSpinButton *button, gpointer user_data)
{
	gchar buffer[16];
	gint value;

	value = d2i(button->adjustment->value);
	snprintf(buffer, sizeof(buffer), "%02u%02u", value / 60, value % 60);
	if (strcmp(buffer, gtk_entry_get_text(GTK_ENTRY(button))))
		gtk_entry_set_text(GTK_ENTRY(button), buffer);
	return TRUE;
}

static GtkWidget *
radar_init_time_spin(void)
{
	GtkWidget *button;

	button = gtk_spin_button_new_with_range(0, 1439, 1);
	gtk_spin_button_set_increments(GTK_SPIN_BUTTON(button), 1, 60);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(button), 0);
	gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(button), TRUE);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(button), TRUE);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(button),
					  GTK_UPDATE_IF_VALID);
	gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(button), TRUE);
	gtk_entry_set_alignment(GTK_ENTRY(button), 1.0);

	g_signal_connect(G_OBJECT(button), "input",
			 G_CALLBACK(time_spin_input), NULL);
	g_signal_connect(G_OBJECT(button), "output",
			 G_CALLBACK(time_spin_output), NULL);
	return button;
}

static gint
range_spin_input(GtkSpinButton *button, gdouble *value, gpointer user_data)
{
	const gchar *text;
	gchar *err = NULL;
	double range;
	gint index;

	text = gtk_entry_get_text(GTK_ENTRY(button));

	range = strtod(text, &err);
	if (err == text || *err != '\0')
		return GTK_INPUT_ERROR;

	for (index = 0; index < RADAR_NR_RANGES; index++) {
		if (fabs(range - radar_ranges[index].range) < EPSILON) {
			*value = index;
			return TRUE;
		}
	}

	return GTK_INPUT_ERROR;
}

static gint
range_spin_output(GtkSpinButton *button, gdouble *value, gpointer user_data)
{
	gchar buffer[16];
	gint index;

	index = d2i(button->adjustment->value);
	snprintf(buffer, sizeof(buffer), "%.*f", radar_ranges[index].digits,
		radar_ranges[index].range);
	if (strcmp(buffer, gtk_entry_get_text(GTK_ENTRY(button)))) {
		if (button->timer)
			g_source_remove(button->timer);
		gtk_entry_set_text(GTK_ENTRY(button), buffer);
	}

	return TRUE;
}

static GtkWidget *
radar_init_range_spin(void)
{
	GtkWidget *button;

	button = gtk_spin_button_new_with_range(0, RADAR_NR_RANGES - 1, 1);
	gtk_spin_button_set_increments(GTK_SPIN_BUTTON(button), 1, 1);
	gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(button), FALSE);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(button), TRUE);
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(button), 2);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(button),
					  GTK_UPDATE_IF_VALID);
	gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(button), TRUE);
	gtk_entry_set_alignment(GTK_ENTRY(button), 1.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(button), RADAR_NR_RANGES / 2);

	g_signal_connect(G_OBJECT(button), "input",
			 G_CALLBACK(range_spin_input), NULL);
	g_signal_connect(G_OBJECT(button), "output",
			 G_CALLBACK(range_spin_output), NULL);
	return button;
}

static GtkEntry *
radar_init_display_entry(radar_t *radar,
			 const char *text, GtkWidget *table, int row, int col,
			 GtkSizeGroup *label_group, GtkSizeGroup *entry_group,
			 const char *tip, int arg)
{
	GtkWidget *hbox, *label, *entry;
	gchar *p;

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);

	if (text) {
		p = g_strdup_printf(text, arg);
		label = gtk_label_new(p);
		g_free(p);
	} else {
		label = gtk_label_new("");
	}
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(label_group, label);
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

	entry = gtk_entry_new();
	GTK_WIDGET_UNSET_FLAGS(entry, GTK_CAN_FOCUS);
	gtk_entry_set_max_length(GTK_ENTRY(entry), 6);
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 6);
	gtk_entry_set_alignment(GTK_ENTRY(entry), 1.0);
	gtk_entry_set_editable(GTK_ENTRY(entry), FALSE);
	gtk_entry_set_text(GTK_ENTRY(entry), "");
	gtk_size_group_add_widget(entry_group, entry);
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
	gtk_widget_show(entry);

	radar_set_tooltip(radar, entry, tip, arg);

	gtk_table_attach(GTK_TABLE(table), hbox, col, col+1, row, row+1,
			 GTK_FILL | GTK_EXPAND, GTK_EXPAND, 0, 0);

	return GTK_ENTRY(entry);
}

static int
radar_create_target(radar_t *radar, int n, GtkWidget *notebook)
{
	GtkSizeGroup *display_group;
	GtkSizeGroup *label_group;
	GtkSizeGroup *left;
	GtkWidget *vbox2;
	GtkWidget *opp_hbox;
	GtkWidget *separator;
	GtkWidget *frame;
	GtkWidget *label;
	GtkWidget *table;
	GtkWidget *button;
	GtkWidget *align;
	GtkWidget *ebox;
	GSList *group;
	target_t *s;
	gchar text[32];

	s = &radar->target[n];

	left = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	label_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	opp_hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(opp_hbox), 0);
	gtk_widget_show(opp_hbox);

	snprintf(text, sizeof(text), _("Target %c"), n + 'B');
	label = gtk_label_new(text);
	gtk_widget_show(label);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), opp_hbox, label);
	gtk_widget_show(notebook);

	frame = gtk_frame_new(_("Observations"));
	gtk_box_pack_start(GTK_BOX(opp_hbox), frame, FALSE, FALSE, 0);
	gtk_widget_show(frame);

	vbox2 = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox2);
	gtk_widget_show(vbox2);

	table = gtk_table_new(5, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), TABLE_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(table), TABLE_COL_SPACING);
	gtk_box_pack_start(GTK_BOX(vbox2), table, TRUE, TRUE, 0);
	gtk_widget_show(table);

	label = gtk_label_new(_("Time:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(label_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 0, 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	button = radar_init_time_spin();
	gtk_table_attach(GTK_TABLE(table), button, 1, 2, 0, 1,
			 0, GTK_EXPAND, 0, 0);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("1. Time of Observation"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(time0_value_changed), s);
	s->time_spin[0] = GTK_SPIN_BUTTON(button);
	s->time[0] = 0;

	label = gtk_label_new(_("R BRG:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(label_group, label);
	gtk_widget_show(label);

	button = gtk_radio_button_new(NULL);
	gtk_container_add(GTK_CONTAINER(button), label);
	gtk_table_attach(GTK_TABLE(table), button, 0, 1, 1, 2,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 0, 0);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	s->rasp_selected[0] = TRUE;

	gtk_widget_show(button);
	g_signal_connect(G_OBJECT(button), "toggled",
                         G_CALLBACK(rasp0_toggled), s);
	s->rasp_radio[0] = GTK_TOGGLE_BUTTON(button);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));


	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 1, 2,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox, _("1. Relative Bearing to Target"), 0);

	button = radar_init_course_spin();
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("1. Relative Bearing to Target"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(rasp0_value_changed), s);
	s->rasp_spin[0] = GTK_SPIN_BUTTON(button);
	s->rasp[0] = 0.0;

	label = gtk_label_new(_("at T HDG:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(label_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 2, 3,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 2, 3,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox,
			  _("Own Ships Heading at 1. Relative Bearing"), 0);

	button = radar_init_course_spin();
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button,
			  _("Own Ships Heading at 1. Relative Bearing"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(rasp0_course_changed), s);
	s->rasp_course_spin[0] = GTK_SPIN_BUTTON(button);
	s->rasp_course_offset[0] = 0;


	label = gtk_label_new(_("T BRG:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_widget_show(label);
	gtk_size_group_add_widget(label_group, label);

	button = gtk_radio_button_new(group);
	gtk_container_add(GTK_CONTAINER(button), label);
	s->rakrp_radio[0] = GTK_TOGGLE_BUTTON(button);

	gtk_table_attach(GTK_TABLE(table), button, 0, 1, 3, 4,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 0, 0);
	gtk_widget_show(button);

	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 3, 4,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox, _("1. True Bearing to Target"), 0);

	button = radar_init_course_spin();
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(left, button);
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("1. True Bearing to Target"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(rakrp0_value_changed), s);
	s->rakrp_spin[0] = GTK_SPIN_BUTTON(button);
	s->rakrp[0] = 0.0;

	label = gtk_label_new(_("Distance:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(label_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 4, 5,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	button = radar_init_spin(0.0, 24.0, 0.1, 1.0, 0.0);
	gtk_table_attach(GTK_TABLE(table), button, 1, 2, 4, 5,
			 0, GTK_EXPAND, 0, 0);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button,
			  _("1. Distance to Target in Nautical Miles"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(dist0_value_changed), s);
	s->dist_spin[0] = GTK_SPIN_BUTTON(button);
	s->distance[0] = 0.0;

	separator = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(vbox2), separator, FALSE, FALSE, 0);
	gtk_widget_show(separator);


	table = gtk_table_new(5, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), TABLE_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(table), TABLE_COL_SPACING);
	gtk_box_pack_start(GTK_BOX(vbox2), table, TRUE, TRUE, 0);
	gtk_widget_show(table);

	label = gtk_label_new(_("Time:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(label_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 0, 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	button = radar_init_time_spin();
	gtk_table_attach(GTK_TABLE(table), button, 1, 2, 0, 1,
			 0, GTK_EXPAND, 0, 0);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("2. Time of Observation"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(time1_value_changed), s);
	s->time_spin[1] = GTK_SPIN_BUTTON(button);
	s->time[1] = 0;

	label = gtk_label_new(_("R BRG:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(label_group, label);
	gtk_widget_show(label);

	button = gtk_radio_button_new(NULL);
	gtk_container_add(GTK_CONTAINER(button), label);
	gtk_table_attach(GTK_TABLE(table), button, 0, 1, 1, 2,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 0, 0);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	s->rasp_selected[1] = TRUE;

	gtk_widget_show(button);
	g_signal_connect(G_OBJECT(button), "toggled",
                         G_CALLBACK(rasp1_toggled), s);
	s->rasp_radio[1] = GTK_TOGGLE_BUTTON(button);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));


	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 1, 2,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox, _("2. Relative Bearing to Target"), 0);

	button = radar_init_course_spin();
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("2. Relative Bearing to Target"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(rasp1_value_changed), s);
	s->rasp_spin[1] = GTK_SPIN_BUTTON(button);
	s->rasp[1] = 0.0;

	label = gtk_label_new(_("at T HDG:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(label_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 2, 3,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 2, 3,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox,
			  _("Own Ships Heading at 2. Relative Bearing"), 0);

	button = radar_init_course_spin();
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button,
			  _("Own Ships Heading at 2. Relative Bearing"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(rasp1_course_changed), s);
	s->rasp_course_spin[1] = GTK_SPIN_BUTTON(button);
	s->rasp_course_offset[1] = 0;


	label = gtk_label_new(_("T BRG:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_widget_show(label);
	gtk_size_group_add_widget(label_group, label);

	button = gtk_radio_button_new(group);
	gtk_container_add(GTK_CONTAINER(button), label);
	s->rakrp_radio[1] = GTK_TOGGLE_BUTTON(button);

	gtk_table_attach(GTK_TABLE(table), button, 0, 1, 3, 4,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 0, 0);
	gtk_widget_show(button);

	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 3, 4,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox, _("2. True Bearing to Target"), 0);

	button = radar_init_course_spin();
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(left, button);
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("2. True Bearing to Target"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(rakrp1_value_changed), s);
	s->rakrp_spin[1] = GTK_SPIN_BUTTON(button);
	s->rakrp[1] = 0.0;

	label = gtk_label_new(_("Distance:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(label_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 4, 5,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	button = radar_init_spin(0.0, 24.0, 0.1, 1.0, 0.0);
	gtk_table_attach(GTK_TABLE(table), button, 1, 2, 4, 5,
			 0, GTK_EXPAND, 0, 0);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button,
			  _("2. Distance to Target in Nautical Miles"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(dist1_value_changed), s);
	s->dist_spin[1] = GTK_SPIN_BUTTON(button);
	s->distance[1] = 0.0;


	frame = gtk_frame_new(_("Completed Data"));
	gtk_box_pack_start(GTK_BOX(opp_hbox), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	table = gtk_table_new(13, 1, TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), TABLE_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(table), TABLE_COL_SPACING);
	gtk_container_add(GTK_CONTAINER(frame), table);
	gtk_widget_show(table);

	label_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	display_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	s->KBr_entry = radar_init_display_entry(radar, _("DRM %c:"),
				table, 0, 0, label_group, display_group,
				_("Direction of Relative Motion of %c"), 'B' + n);

	s->vBr_entry = radar_init_display_entry(radar, _("SRM %c:"),
				table, 1, 0, label_group, display_group,
				_("Speed of Relative Motion of %c"), 'B' + n);

	s->KB_entry = radar_init_display_entry(radar, _("T CRS %c:"),
				table, 2, 0, label_group, display_group,
				_("True Course of %c"), 'B' + n);

	s->vB_entry = radar_init_display_entry(radar, _("T SPD %c:"),
				table, 3, 0, label_group, display_group,
				_("True Speed of %c"), 'B' + n);

	s->aspect_entry = radar_init_display_entry(radar, _("Aspect %c:"),
				table, 4, 0, label_group, display_group,
				_("Aspect Angle (Relative Bearing to Own Ship from Target %c)"), 'B' + n);

	s->CPA_entry = radar_init_display_entry(radar, _("CPA:"),
				table, 5, 0, label_group, display_group,
				_("Range at Closest Point of Approach"), 0);

	s->PCPA_entry = radar_init_display_entry(radar, _("T BRG CPA:"),
				table, 6, 0, label_group, display_group,
				_("True Bearing at CPA"), 0);

	s->SPCPA_entry = radar_init_display_entry(radar, _("R BRG CPA:"),
				table, 7, 0, label_group, display_group,
				_("Relative Bearing at CPA"), 0);

	s->TCPA_entry = radar_init_display_entry(radar, _("TCPA:"),
				table, 8, 0, label_group, display_group,
				_("Time to CPA in Minutes"), 0);

	s->tCPA_entry = radar_init_display_entry(radar, NULL,
				table, 9, 0, label_group, display_group,
				_("Time at CPA"), 0);

	s->BCR_entry = radar_init_display_entry(radar, _("BCR:"),
				table, 10, 0, label_group, display_group,
				_("Bow Crossing Range"), 0);

	s->BCT_entry = radar_init_display_entry(radar, _("BCT:"),
				table, 11, 0, label_group, display_group,
				_("Time to Bow Crossing in Minutes"), 0);

	s->BCt_entry = radar_init_display_entry(radar, NULL,
				table, 12, 0, label_group, display_group,
				_("Time at Bow Crossing"), 0);


	frame = gtk_frame_new(_("after Maneuver"));
	gtk_box_pack_start(GTK_BOX(opp_hbox), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	table = gtk_table_new(13, 1, TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), TABLE_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(table), TABLE_COL_SPACING);
	gtk_container_add(GTK_CONTAINER(frame), table);
	gtk_widget_show(table);

	s->new_KBr_entry = radar_init_display_entry(radar, _("DRM %c:"),
				table, 0, 0, label_group, display_group,
				_("Direction of Relative Motion of %c"), 'B' + n);

	s->new_vBr_entry = radar_init_display_entry(radar, _("SRM %c:"),
				table, 1, 0, label_group, display_group,
				_("Speed of Relative Motion of %c"), 'B' + n);

	s->delta_entry = radar_init_display_entry(radar, _("delta:"),
				table, 2, 0, label_group, display_group,
				_("Change in Direction of Relative Motion"), 0);

	s->new_RaSP_entry = radar_init_display_entry(radar, _("R BRG:"),
				table, 3, 0, label_group, display_group,
				_("Relative Bearing to Target %c"), 'B' + n);

	s->new_aspect_entry = radar_init_display_entry(radar, _("Aspect %c:"),
				table, 4, 0, label_group, display_group,
				_("Aspect Angle (Relative Bearing to Own Ship from Target %c)"), 'B' + n);

	s->new_CPA_entry = radar_init_display_entry(radar, _("CPA:"),
				table, 5, 0, label_group, display_group,
				_("Range at Closest Point of Approach"), 0);

	s->new_PCPA_entry = radar_init_display_entry(radar, _("T BRG CPA:"),
				table, 6, 0, label_group, display_group,
				_("True Bearing at CPA"), 0);

	s->new_SPCPA_entry = radar_init_display_entry(radar, _("R BRG CPA:"),
				table, 7, 0, label_group, display_group,
				_("Relative Bearing at CPA"), 0);

	s->new_TCPA_entry = radar_init_display_entry(radar, _("TCPA:"),
				table, 8, 0, label_group, display_group,
				_("Time to CPA in Minutes"), 0);

	s->new_tCPA_entry = radar_init_display_entry(radar, NULL,
				table, 9, 0, label_group, display_group,
				_("Time at CPA"), 0);

	s->new_BCR_entry = radar_init_display_entry(radar, _("BCR:"),
				table, 10, 0, label_group, display_group,
				_("Bow Crossing Range"), 0);

	s->new_BCT_entry = radar_init_display_entry(radar, _("BCT:"),
				table, 11, 0, label_group, display_group,
				_("Time to Bow Crossing in Minutes"), 0);

	s->new_BCt_entry = radar_init_display_entry(radar, NULL,
				table, 12, 0, label_group, display_group,
				_("Time at Bow Crossing"), 0);


	if (radar->default_rakrp) {
		gtk_toggle_button_set_active(s->rakrp_radio[0], TRUE);
		gtk_toggle_button_set_active(s->rakrp_radio[1], TRUE);
	}

	return 0;
}

static int
radar_create_window(radar_t *radar)
{
	static char title[128];
	gchar text[32];
	GtkWidget *vbox, *hbox, *vbox2;
	GtkWidget *radar_vbox;
	GtkWidget *maneuver_vbox;
	GtkWidget *opp_vbox;
	GtkWidget *panel_table;
	GtkWidget *notebook;
	GtkWidget *label;
	GtkSizeGroup *left, *right;
	GtkSizeGroup *left_group;
	GtkSizeGroup *right_group;
	GtkRequisition requisition;
	GtkActionGroup *actions;
	GtkUIManager *ui;
	GtkWidget *frame;
	GtkWidget *table;
	GtkWidget *button;
	GtkWidget *combo;
	GtkWidget *align;
	GtkWidget *ebox;
	GdkScreen *screen;
	gint width, height;
	GError *error = NULL;
	GSList *group;
	GList *focus = NULL;
	GList *icons = NULL;
	GdkGeometry hints;
	int i;

	radar->icon16 = gdk_pixbuf_new_from_inline(-1, radar16x16, FALSE, NULL);
	icons = g_list_append(icons, radar->icon16);
	radar->icon32 = gdk_pixbuf_new_from_inline(-1, radar32x32, FALSE, NULL);
	icons = g_list_append(icons, radar->icon32);
	radar->icon48 = gdk_pixbuf_new_from_inline(-1, radar48x48, FALSE, NULL);
	icons = g_list_append(icons, radar->icon48);
	radar->icon64 = gdk_pixbuf_new_from_inline(-1, radar64x64, FALSE, NULL);
	icons = g_list_append(icons, radar->icon64);
	gtk_window_set_default_icon_list(icons);
	g_list_free(icons);

	radar->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_name(radar->window, "Radarplot");

	radar->tooltips = gtk_tooltips_new();

	if (radar->license) {
		snprintf(title, sizeof(title), "brainaid Radarplot (%.64s)",
			g_key_file_get_string(radar->license,
					      "Radarplot License",
					      "name", NULL));
	} else {
		snprintf(title, sizeof(title),
			 _("brainaid Radarplot (unregistered)"));
	}

	gtk_window_set_title(GTK_WINDOW(radar->window), title);

        hints.width_inc = 1;
        hints.height_inc = 1;
	gtk_window_set_geometry_hints(GTK_WINDOW(radar->window),
				      radar->window,
				      &hints, GDK_HINT_RESIZE_INC);

	actions = gtk_action_group_new("Actions");
	gtk_action_group_set_translation_domain(actions, GETTEXT_PACKAGE);

	gtk_action_group_add_actions(actions, ui_entries, n_entries, radar);
	for (i = 0; i < n_toggle_entries; i++) {
		if (!strcmp(ui_toggle_entries[i].name, "Heading"))
			ui_toggle_entries[i].is_active = radar->default_heading;
		if (!strcmp(ui_toggle_entries[i].name, "Bearing"))
			ui_toggle_entries[i].is_active = radar->default_rakrp;
		if (!strcmp(ui_toggle_entries[i].name, "Render"))
			ui_toggle_entries[i].is_active = radar->do_render;
	}
	gtk_action_group_add_toggle_actions(actions, ui_toggle_entries,
					    n_toggle_entries, radar);

	ui = gtk_ui_manager_new();
	gtk_ui_manager_insert_action_group(ui, actions, 0);
	gtk_window_add_accel_group(GTK_WINDOW(radar->window),
				   gtk_ui_manager_get_accel_group(ui));
	if (!gtk_ui_manager_add_ui_from_string(ui, ui_info, -1, &error)) {
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
	}

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(radar->window), vbox);
	gtk_widget_show(vbox);

	g_signal_connect(G_OBJECT(radar->window), "destroy",
			 G_CALLBACK(radar_quit), radar);

	radar->menubar = gtk_ui_manager_get_widget(ui, "/MenuBar");
#ifndef OS_Darwin
	gtk_box_pack_start(GTK_BOX(vbox), radar->menubar, FALSE, FALSE, 0);
	gtk_widget_show_all(radar->menubar);
#endif


	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);

	radar->canvas = gtk_drawing_area_new();

	gtk_widget_set_size_request(GTK_WIDGET(radar->canvas), 400, 400);

	gtk_box_pack_start(GTK_BOX(hbox), radar->canvas, TRUE, TRUE, 0);
	gtk_widget_show(radar->canvas);

	g_signal_connect(G_OBJECT(radar->canvas), "expose-event",
			 G_CALLBACK(expose_event), radar);
	g_signal_connect(G_OBJECT(radar->canvas), "configure-event",
			 G_CALLBACK(configure_event), radar);

	gtk_widget_set_events(radar->canvas, GDK_EXPOSURE_MASK);


	panel_table = gtk_table_new(2, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(panel_table),
				       PANEL_BORDER_WIDTH);
	gtk_table_set_row_spacings(GTK_TABLE(panel_table), PANEL_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(panel_table), PANEL_COL_SPACING);
	gtk_box_pack_start(GTK_BOX(hbox), panel_table, FALSE, FALSE, 0);
	gtk_widget_show(panel_table);


	left = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	right = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	left_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	right_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

/*
 * Preselections (Radar):
 */
	radar_vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(radar_vbox), 0);
	gtk_table_attach(GTK_TABLE(panel_table), radar_vbox, 0, 1, 0, 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(radar_vbox);
	focus = g_list_append(focus, radar_vbox);

	frame = gtk_frame_new(_("Radar Settings"));
	gtk_box_pack_start(GTK_BOX(radar_vbox), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	table = gtk_table_new(3, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), TABLE_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(table), TABLE_COL_SPACING);
	gtk_container_add(GTK_CONTAINER(frame), table);
	gtk_widget_show(table);

	label = gtk_label_new(_("Orientation:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(left_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 0, 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	combo = gtk_combo_box_new_text();
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), _("North Up"));
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), _("Head Up"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
	gtk_table_attach(GTK_TABLE(table), combo, 1, 2, 0, 1,
			 0, GTK_EXPAND, 0, 0);
	gtk_size_group_add_widget(left, combo);
	gtk_widget_show(combo);
	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(orientation_changed), radar);
	g_signal_connect(G_OBJECT(combo), "realize",
			 G_CALLBACK(radar_realize_orientation_combo), radar);
	radar->orientation_combo = GTK_COMBO_BOX(combo);
	radar->north_up = TRUE;

	label = gtk_label_new(_("Range:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(left_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 1, 2,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	button = radar_init_range_spin();
	gtk_table_attach(GTK_TABLE(table), button, 1, 2, 1, 2,
			 0, GTK_EXPAND, 0, 0);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button,
			  _("Range in Nautical Miles"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(range_value_changed), radar);
	radar->range_spin = GTK_SPIN_BUTTON(button);
	radar->rindex = gtk_spin_button_get_value_as_int(radar->range_spin);
	radar->range = radar_ranges[radar->rindex].range;

	label = gtk_label_new(_("Heading Line:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(left_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 2, 3,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	button = gtk_check_button_new_with_mnemonic(_("_HL"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),
				     radar->default_heading);
	radar->show_heading = radar->default_heading;
	gtk_table_attach(GTK_TABLE(table), button, 1, 2, 2, 3,
			 0, GTK_EXPAND, 1, 0);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("Show Heading Line"), 0);
	g_signal_connect(G_OBJECT(button), "toggled",
			 G_CALLBACK(heading_toggled), radar);
	radar->heading_toggle = GTK_TOGGLE_BUTTON(button);

/*
 * Preselections (Own Ship):
 */
	frame = gtk_frame_new(_("Own Ship"));
	gtk_box_pack_start(GTK_BOX(radar_vbox), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	table = gtk_table_new(2, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), TABLE_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(table), TABLE_COL_SPACING);
	gtk_container_add(GTK_CONTAINER(frame), table);
	gtk_widget_show(table);

	label = gtk_label_new(_("T CRS:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(left_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 0, 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	button = radar_init_course_spin();
	gtk_table_attach(GTK_TABLE(table), button, 1, 2, 0, 1,
			 0, GTK_EXPAND, 0, 0);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("True Course of Own Ship"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(own_course_value_changed), radar);
	radar->own_course_spin = GTK_SPIN_BUTTON(button);
	radar->own_course = 0.0;

	label = gtk_label_new(_("SPD (STW):"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(left_group, label);
	gtk_widget_show(label);

	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 1, 2,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);


	button = radar_init_spin(0.0, 50.0, 0.1, 1.0, 0.0);
	gtk_table_attach(GTK_TABLE(table), button, 1, 2, 1, 2,
			 0, GTK_EXPAND, 0, 0);
	gtk_size_group_add_widget(left, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("Speed Through Water of Own Ship"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(own_speed_value_changed), radar);
	radar->own_speed_spin = GTK_SPIN_BUTTON(button);
	radar->own_speed = 0.0;

/*
 * Targets
 */
	opp_vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(opp_vbox), 0);
	gtk_table_attach(GTK_TABLE(panel_table), opp_vbox, 0, 2, 1, 2,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(opp_vbox);
	focus = g_list_append(focus, opp_vbox);

	notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(opp_vbox), notebook, TRUE, TRUE, 0);

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		radar_create_target(radar, i, notebook);
	}


/*
 * Maneuver:
 */
	maneuver_vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(maneuver_vbox), 0);
	gtk_table_attach(GTK_TABLE(panel_table), maneuver_vbox, 1, 2, 0, 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(maneuver_vbox);
	focus = g_list_append(focus, maneuver_vbox);

	frame = gtk_frame_new(_("Maneuver"));
	gtk_box_pack_start(GTK_BOX(maneuver_vbox), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	vbox2 = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox2);
	gtk_widget_show(vbox2);

	table = gtk_table_new(2, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), TABLE_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(table), TABLE_COL_SPACING);
	gtk_box_pack_start(GTK_BOX(vbox2), table, TRUE, TRUE, 0);
	gtk_widget_show(table);

	label = gtk_label_new(_("Target:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(right_group, label);
	gtk_widget_show(label);

	gtk_widget_show(align);
	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 0, 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	combo = gtk_combo_box_new_text();
	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		snprintf(text, sizeof(text), _("Target %c"), 'B' + i);
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo), text);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
	gtk_table_attach(GTK_TABLE(table), combo, 1, 2, 0, 1,
			 0, GTK_EXPAND, 0, 0);
	gtk_size_group_add_widget(right, combo);
	gtk_widget_show(combo);
	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(target_changed), radar);
	g_signal_connect(G_OBJECT(combo), "realize",
			 G_CALLBACK(radar_realize_target_combo), radar);
	radar->target_combo = GTK_COMBO_BOX(combo);

	label = gtk_label_new(_("Time:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(right_group, label);
	gtk_widget_show(label);

	button = gtk_radio_button_new(NULL);
	gtk_container_add(GTK_CONTAINER(button), label);
	gtk_table_attach(GTK_TABLE(table), button, 0, 1, 1, 2,
			 0, GTK_EXPAND, 0, 0);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	gtk_widget_show(button);
	g_signal_connect(G_OBJECT(button), "toggled",
                         G_CALLBACK(mtime_toggled), radar);
	radar->mtime_selected = TRUE;
	radar->mtime_radio = GTK_TOGGLE_BUTTON(button);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));

	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 1, 2,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox, _("Time of Maneuver"), 0);

	button = radar_init_time_spin();
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(right, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("Projected Time of Maneuver"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(mtime_value_changed), radar);
	radar->mtime_spin = GTK_SPIN_BUTTON(button);
	radar->mtime = 0;

	label = gtk_label_new(_("Distance:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(right_group, label);
	gtk_widget_show(label);

	button = gtk_radio_button_new(group);
	gtk_container_add(GTK_CONTAINER(button), label);
	radar->mdist_radio = GTK_TOGGLE_BUTTON(button);

	gtk_table_attach(GTK_TABLE(table), button, 0, 1, 2, 3,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(button);

	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 2, 3,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox, _("Distance to Target at Maneuver"), 0);

	button = radar_init_spin(0.0, 24.0, 0.1, 1.0, 0.0);
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(right, button);
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button,
			  _("Projected Distance to Target at Maneuver"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(mdist_value_changed), radar);
	radar->mdist_spin = GTK_SPIN_BUTTON(button);
	radar->mdistance = 0.0;


	table = gtk_table_new(3, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), TABLE_ROW_SPACING);
	gtk_table_set_col_spacings(GTK_TABLE(table), TABLE_COL_SPACING);
	gtk_box_pack_start(GTK_BOX(vbox2), table, TRUE, TRUE, 0);
	gtk_widget_show(table);

	label = gtk_label_new(_("Maneuver:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(right_group, label);
	gtk_widget_show(label);

	gtk_widget_show(align);
	align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_table_attach(GTK_TABLE(table), align, 0, 1, 0, 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND, 1, 0);
	gtk_widget_show(align);

	combo = gtk_combo_box_new_text();
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), _("Course Change"));
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), _("Speed Change"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
	gtk_table_attach(GTK_TABLE(table), combo, 1, 2, 0, 1,
			 0, GTK_EXPAND, 0, 0);
	gtk_size_group_add_widget(right, combo);
	gtk_widget_show(combo);
	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(maneuver_changed), radar);
	g_signal_connect(G_OBJECT(combo), "realize",
			 G_CALLBACK(radar_realize_maneuver_combo), radar);
	radar->maneuver_combo = GTK_COMBO_BOX(combo);
	radar->mcourse_change = TRUE;

	label = gtk_label_new(_("CPA:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(right_group, label);
	gtk_widget_show(label);

	button = gtk_radio_button_new(NULL);
	gtk_container_add(GTK_CONTAINER(button), label);
	gtk_table_attach(GTK_TABLE(table), button, 0, 1, 1, 2,
			 0, GTK_EXPAND, 0, 0);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	gtk_widget_show(button);
	g_signal_connect(G_OBJECT(button), "toggled",
                         G_CALLBACK(mcpa_toggled), radar);
	radar->mcpa_selected = TRUE;
	radar->mcpa_radio = GTK_TOGGLE_BUTTON(button);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));

	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 1, 2,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox, _("CPA after Maneuver"), 0);

	button = radar_init_spin(0.0, 24.0, 0.1, 1.0, 0.0);
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(right, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button, _("Projected CPA to Target"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(mcpa_value_changed), radar);
	radar->mcpa_spin = GTK_SPIN_BUTTON(button);
	radar->mcpa = 0.0;


	label = gtk_label_new(_("T CRS:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(right_group, label);
	gtk_widget_show(label);

	button = gtk_radio_button_new(group);
	gtk_container_add(GTK_CONTAINER(button), label);
	radar->ncourse_radio = GTK_TOGGLE_BUTTON(button);

	gtk_table_attach(GTK_TABLE(table), button, 0, 1, 2, 3,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(button);

	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 2, 3,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox,
			  _("True Course of Own Ship after Maneuver"), 0);

	button = radar_init_course_spin();
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(button), 1);
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(right, button);
	gtk_widget_show(button);
	radar_set_tooltip(radar, button,
		_("Projected True Course of Own Ship after Maneuver"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(ncourse_value_changed), radar);
	gtk_widget_set_sensitive(button, FALSE);
	radar->ncourse_spin = GTK_SPIN_BUTTON(button);
	radar->ncourse = 0.0;

	label = gtk_label_new(_("SPD (STW):"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_size_group_add_widget(right_group, label);
	gtk_widget_show(label);

	button = gtk_radio_button_new_from_widget(
				GTK_RADIO_BUTTON(radar->ncourse_radio));
	gtk_container_add(GTK_CONTAINER(button), label);
	radar->nspeed_radio = GTK_TOGGLE_BUTTON(button);

	gtk_table_attach(GTK_TABLE(table), button, 0, 1, 2, 3,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_hide(button);

	ebox = gtk_event_box_new();
	gtk_event_box_set_above_child(GTK_EVENT_BOX(ebox), FALSE);
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), TRUE);
	gtk_table_attach(GTK_TABLE(table), ebox, 1, 2, 2, 3,
			 0, GTK_EXPAND, 0, 0);
	gtk_widget_show(ebox);
	radar_set_tooltip(radar, ebox,
			  _("Speed through Water of Own Ship after Maneuver"), 0);

	button = radar_init_spin(0.0, 50.0, 0.1, 1.0, 0.0);
	gtk_container_add(GTK_CONTAINER(ebox), button);
	gtk_size_group_add_widget(right, button);
	radar_set_tooltip(radar, button,
		_("Projected Speed through Water of Own Ship after Maneuver"), 0);
	g_signal_connect(G_OBJECT(button), "value-changed",
			 G_CALLBACK(nspeed_value_changed), radar);
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_hide(button);
	radar->nspeed_spin = GTK_SPIN_BUTTON(button);
	radar->nspeed = 0.0;

	gtk_container_set_focus_chain(GTK_CONTAINER(panel_table), focus);

	screen = gtk_widget_get_screen(radar->window);
	if (screen) {
		gtk_widget_size_request(GTK_WIDGET(panel_table), &requisition);

		width = requisition.width + requisition.height;
		height = requisition.height;

		if (width > gdk_screen_get_width(screen) - 16)
			width = gdk_screen_get_width(screen) - 16;

		if (height > gdk_screen_get_height(screen) - 40)
			height = gdk_screen_get_height(screen) - 40;

		gtk_window_set_default_size(GTK_WINDOW(radar->window),
					    width, height);
	}

	gtk_widget_show(radar->window);
	return 0;
}

static GdkGC *
radar_init_gc_copy(radar_t *radar, GdkGC *src)
{
	GdkGC *gc;

	gc = gdk_gc_new(radar->canvas->window);
	gdk_gc_copy(gc, src);
	return gc;
}

static GdkGC *
radar_init_gc_data(radar_t *radar, int red, int green, int blue,
		   int line_width, int line_cap)
{
	GdkColor color;
	GdkGC *gc;

	color.red = (red << 8) | red;
	color.green = (green << 8) | green;
	color.blue = (blue << 8) | blue;

	gc = gdk_gc_new(radar->canvas->window);
	gdk_gc_copy(gc, radar->canvas->style->white_gc);
	gdk_gc_set_rgb_fg_color(gc, &color);

	gdk_gc_set_line_attributes(gc, line_width, GDK_LINE_SOLID,
				   line_cap, GDK_JOIN_ROUND);
	return gc;
}

static void
radar_init_private_data(radar_t *radar)
{
	PangoFontDescription *font_description;
	PangoAttrList *attrs;
	PangoContext *context;
	gint8 dashes[2] = { 3, 3 };
	text_label_t *l;
	target_t *s;
	int i, j;

	radar->white_gc = radar_init_gc_data(radar, 0xff, 0xff, 0xff, 0,
					     GDK_CAP_ROUND);
	radar->black_gc = radar_init_gc_data(radar, 0, 0, 0, 0,
					     GDK_CAP_ROUND);
	radar->grey25_gc = radar_init_gc_data(radar, 0xc0, 0xc0, 0xc0, 0,
					     GDK_CAP_ROUND);
	radar->grey50_gc = radar_init_gc_data(radar, 0x80, 0x80, 0x80, 0,
					     GDK_CAP_ROUND);
	radar->green_gc = radar_init_gc_data(radar, 0, 0x80, 0, 2,
					     GDK_CAP_BUTT);
	radar->green_dash_gc = radar_init_gc_data(radar, 0, 0x80, 0, 2,
						  GDK_CAP_BUTT);
	gdk_gc_set_line_attributes(radar->green_dash_gc, 2,
				   GDK_LINE_ON_OFF_DASH,
				   GDK_CAP_ROUND, GDK_JOIN_ROUND);
	gdk_gc_set_dashes(radar->green_dash_gc, 0, dashes, 2);

	radar->grey25_clip2_gc = radar_init_gc_copy(radar, radar->grey25_gc);
	radar->grey50_clip0_gc = radar_init_gc_copy(radar, radar->grey50_gc);
	radar->grey50_clip1_gc = radar_init_gc_copy(radar, radar->grey50_gc);
	radar->grey50_clip3_gc = radar_init_gc_copy(radar, radar->grey50_gc);

	context = gtk_widget_get_pango_context(radar->canvas);
	radar->layout = pango_layout_new(context);
	font_description = pango_font_description_from_string(LABEL_FONT);

	pango_layout_set_alignment(radar->layout, PANGO_ALIGN_CENTER);
	pango_layout_set_font_description(radar->layout, font_description);

	attrs = pango_attr_list_new();
	pango_attr_list_insert(attrs, pango_attr_foreground_new(0, 0, 0));
	pango_attr_list_insert(attrs,
			pango_attr_background_new(0xffff, 0xffff, 0xffff));

	pango_layout_set_attributes(radar->layout, attrs);

	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		s = &radar->target[i];

		s->pos_gc = radar_init_gc_data(radar, COLOR_RED, 2,
					       GDK_CAP_BUTT);
		s->vec_gc = radar_init_gc_data(radar, COLOR_RED, 2,
					       GDK_CAP_ROUND);
		s->vec_mark_gc = radar_init_gc_data(radar, COLOR_RED, 0,
					            GDK_CAP_ROUND);
		s->cpa_gc = radar_init_gc_data(radar, COLOR_RED, 2,
					       GDK_CAP_ROUND);
		s->ext_gc = radar_init_gc_data(radar, COLOR_RED, 0,
					       GDK_CAP_ROUND);
		if (i != radar->mtarget) {
			gdk_gc_set_line_attributes(s->cpa_gc, 2,
						   GDK_LINE_ON_OFF_DASH,
						   GDK_CAP_ROUND,
						   GDK_JOIN_ROUND);
			gdk_gc_set_dashes(s->cpa_gc, 0, dashes, 2);

			gdk_gc_set_line_attributes(s->ext_gc, 0,
						   GDK_LINE_ON_OFF_DASH,
						   GDK_CAP_ROUND,
						   GDK_JOIN_ROUND);
			gdk_gc_set_dashes(s->ext_gc, 0, dashes, 2);
		}

		s->own_gc = radar_init_gc_data(radar, COLOR_GREEN, 2,
					       GDK_CAP_ROUND);
		s->own_mark_gc = radar_init_gc_data(radar, COLOR_GREEN, 0,
					       GDK_CAP_ROUND);

		s->true_gc = radar_init_gc_data(radar, COLOR_BLUE, 2,
						GDK_CAP_ROUND);
		s->true_mark_gc = radar_init_gc_data(radar, COLOR_BLUE, 0,
						     GDK_CAP_ROUND);

		s->arc_gc = radar_init_gc_data(radar, COLOR_GREEN, 0,
					       GDK_CAP_ROUND);
		s->ext_dash_gc = radar_init_gc_data(radar, COLOR_RED, 0,
						    GDK_CAP_ROUND);
		gdk_gc_set_line_attributes(s->ext_dash_gc, 0,
					   GDK_LINE_ON_OFF_DASH,
					   GDK_CAP_ROUND, GDK_JOIN_ROUND);
		gdk_gc_set_dashes(s->ext_dash_gc, 0, dashes, 2);

		for (j = 0; j < TARGET_NR_LABELS; j++) {
			l = &s->labels[j];

			l->layout = pango_layout_copy(radar->layout);
		}
	}
}

void
radar_save_config(radar_t *radar, const char *filename)
{
	GError *error;
	gchar *backup, *backpath;
	gchar *path;
	gchar *data;
	gsize length;
	int fd;

	backup = g_malloc(strlen(filename) + 2);
	if (NULL == backup)
		return;

	sprintf(backup, "%s~", filename);

	backpath = g_build_filename(g_get_home_dir(), backup, NULL);
	if (NULL == backpath)
		goto out1;

	path = g_build_filename(g_get_home_dir(), filename, NULL);
	if (NULL == path)
		goto out2;

	g_rename(path, backpath);

	error = NULL;
	data = g_key_file_to_data(radar->key_file, &length, &error);
	if (NULL != error)
		goto out4;

	fd = g_open(path, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (fd < 0)
		goto out5;

	if (write(fd, data, length) != length) {
		close(fd);
		goto out5;
	}

	close(fd);
	g_free(data);
	goto out3;

out5:
	g_free(data);

out4:
	g_rename(backpath, path);

out3:
	g_free(path);
out2:
	g_free(backpath);
out1:
	g_free(backup);
}

static void
radar_load_config(radar_t *radar, const char *filename)
{
	gchar *path, *lpath;
	gboolean bvalue;
	GError *error;

	radar->do_render = TRUE;
	radar->default_heading = TRUE;
	radar->default_rakrp = FALSE;
	radar->license_pathname = NULL;

	path = g_build_filename(g_get_home_dir(), filename, NULL);
	if (NULL == path)
		return;

	radar->key_file = g_key_file_new();
	if (NULL == radar->key_file)
		goto out;

	error = NULL;
	if (!g_key_file_load_from_file(radar->key_file, path,
				       G_KEY_FILE_KEEP_COMMENTS |
				       G_KEY_FILE_KEEP_TRANSLATIONS,
				       &error))
		goto out;

	error = NULL;
	bvalue = g_key_file_get_boolean(radar->key_file,
					"Radarplot", "Antialias",
					&error);
	if (NULL == error)
		radar->do_render = bvalue;

	error = NULL;
	bvalue = g_key_file_get_boolean(radar->key_file,
					"Radarplot", "ShowHeading",
					&error);
	if (NULL == error)
		radar->default_heading = bvalue;

	error = NULL;
	bvalue = g_key_file_get_boolean(radar->key_file,
					"Radarplot", "DefaultRaKrP",
					&error);
	if (NULL == error)
		radar->default_rakrp = bvalue;

	error = NULL;
	lpath = g_key_file_get_string(radar->key_file,
				      "Radarplot", "License",
				      &error);
	if (NULL == error)
		radar->license_pathname = lpath;

	if (radar->license) {
		g_key_file_free(radar->license);
		radar->license = NULL;
	}

	if (radar->license_pathname)
		verify_registration_file(radar->license_pathname,
					 &radar->license);

out:
	g_free(path);
}

char *progname;
char *progpath;

static void
radar_setup_locale(void)
{
	const char *dir = NULL;

#ifdef __WIN32__
	dir = g_win32_get_package_installation_subdirectory(NULL, NULL, "lib\\locale");
#endif
#ifdef OS_Darwin
	dir = ige_mac_bundle_get_localedir(ige_mac_bundle_get_default());
#endif

	textdomain(GETTEXT_PACKAGE);
	bindtextdomain(GETTEXT_PACKAGE, dir);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");

	setlocale(LC_NUMERIC, "C");

	if (NULL == dir) {
		return;
	}

	bindtextdomain("gtk20", dir);
	bind_textdomain_codeset("gtk20", "UTF-8");
}

int
main(int argc, char **argv)
{
	radar_t radar;
	int i;

	progname = g_path_get_basename(argv[0]);
	progpath = g_path_get_dirname(argv[0]);

	gtk_init(&argc, &argv);

	radar_setup_locale();

	memset(&radar, 0, sizeof(radar_t));
	for (i = 0; i < RADAR_NR_TARGETS; i++) {
		radar.target[i].index = i;
		radar.target[i].radar = &radar;
	}

	radar_load_config(&radar, ".radarplot");

	if ((argc > 1) && (0 == access(argv[1], R_OK))) {
		radar.plot_filename = strdup(argv[1]);
		radar.load_pending = TRUE;
	}

	radar_create_window(&radar);

#ifdef OS_Darwin
	gtk_widget_hide(radar.menubar);
	ige_mac_menu_set_menu_bar(GTK_MENU_SHELL(radar.menubar));
	{
		GtkWidget *item = gtk_menu_item_new();
		g_signal_connect(G_OBJECT(item), "activate",
				 G_CALLBACK(radar_quit), &radar);
		ige_mac_menu_set_quit_menu_item(GTK_MENU_ITEM(item));
	}
#endif

	gtk_main();
	return 0;
}

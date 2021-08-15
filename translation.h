/* $Id
 */

#ifndef _RADAR_TRANSLATION_H
#define _RADAR_TRANSLATION_H 1

#include <gtk/gtk.h>
#include <glib.h>


#ifdef __WIN32__

char *gettext(const char *msgid);

char *textdomain(const char *domainname);
char *bindtextdomain(const char *domainname, const char *dirname);
char *bind_textdomain_codeset(const char *domainname, const char *codeset);

#endif /* __WIN32__ */


struct __translation_s__;
typedef struct __translation_s__ translation_t;

struct __translation_s__ {
	GObject			*object;
	union {
		const char	*property;
		GtkWidget	*widget;
		unsigned int	position;
	} u;
	const char		*msgid;
	int			arg;
	void			(*function) (translation_t *, const char *);
};

typedef struct {
	const char	*menu_name;
	const char	*locale_name;
} language_t;

extern const language_t *radar_languages;

#endif /* !(_RADAR_TRANSLATION_H) */

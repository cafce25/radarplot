/* $Id: register.c,v 1.15 2009-07-24 11:37:17 ecd Exp $
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef __WIN32__
#include <winsock2.h>
#else /* __WIN32__ */
#include <sys/socket.h>
#include <netdb.h>
#endif /* __WIN32__ */

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>

#include <openssl/crypto.h>
#include <openssl/objects.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>

#include "radar.h"
#include "public.h"

#define SERVER		"brainaid.de"
#define MAILFORM	"/people/ecd/cgi-bin/mailform"
#define MAILADDR	"license@brainaid.de"
#define SUBJECT		"Radarplot License"
#define RET_PAGE	"http://brainaid.de/people/ecd/radarplot/"
#define NOT_EMPTY	"name,email"

/*
 * Registration File Format:
 *
 * [Radarplot License]
 * company=
 * name=Eddie C. Dost
 * address1=Rue de la Chapelle 51
 * address2=
 * city=Moresnet
 * state=
 * zip=4850
 * country=Belgium
 * email=ecd@brainaid.de
 * lat=50N44
 * lon=006E00
 * id=XXXXXXXX
 * signature=...
 */

typedef struct {
	const char	*key;
	int		required;
	char		*value;
} property_t;

static property_t properties[] =
{
	{ "company",	0,	NULL },
	{ "name",	1,	NULL },
	{ "address1",	0,	NULL },
	{ "address2",	0,	NULL },
	{ "city",	0,	NULL },
	{ "state",	0,	NULL },
	{ "zip",	0,	NULL },
	{ "country",	0,	NULL },
	{ "email",	1,	NULL },
	{ "lat",	0,	NULL },
	{ "lon",	0,	NULL },
	{ "id",		1,	NULL },
	{ NULL, 0 }
};

typedef struct {
	GtkWidget	*company;
	GtkWidget	*name;
	GtkWidget	*address1;
	GtkWidget	*address2;
	GtkWidget	*city;
	GtkWidget	*state;
	GtkWidget	*zip;
	GtkWidget	*country;
	GtkWidget	*email;
	GtkWidget	*lat;
	GtkWidget	*lon;
	char		*id;
	unsigned int	flags;
	GtkDialog	*dialog;
	radar_t		*radar;
} radar_register_t;

#define REGISTER_NAME_SET	(1 << 0)
#define REGISTER_EMAIL_SET	(1 << 1)

int
verify_registration_file(const char *pathname, GKeyFile **license)
{
	unsigned char md_value[EVP_MAX_MD_SIZE];
	size_t text_len, signature_len;
	unsigned int md_len;
	unsigned int nid = NID_md5;
	const EVP_MD *md;
	EVP_MD_CTX mdctx;
	GError *error;
	char *value = NULL;
	BIGNUM *sign_bn = NULL;
	unsigned char *signature = NULL;
	GKeyFile *key_file = NULL;
	char *text = NULL;
	RSA *public = NULL;
	int i, n;


	key_file = g_key_file_new();
	if (NULL == key_file) {
		fprintf(stderr, "%s:%u: g_key_file_new failed\n",
			__FUNCTION__, __LINE__);
		return -1;
	}

	error = NULL;
	if (!g_key_file_load_from_file(key_file, pathname, 0, &error)) {
		fprintf(stderr, "%s:%u: g_key_file_load_from_file: %s\n",
			__FUNCTION__, __LINE__, error->message);
		goto out;
	}

	text = NULL;
	text_len = 0;

	for (i = 0; properties[i].key; i++) {
		error = NULL;
		value = g_key_file_get_string(key_file, "Radarplot License",
					      properties[i].key, &error);
		if (NULL == value) {
			fprintf(stderr, "%s:%u: g_key_file_get_string '%s': %s\n",
				__FUNCTION__, __LINE__,
				properties[i].key, error->message);
			goto out;
		}

		if (0 == strlen(value)) {
			if (properties[i].required) {
				fprintf(stderr, "%s:%u: property missing: %s\n",
					__FUNCTION__, __LINE__,
					properties[i].key);
				goto out;
			}
		}

		properties[i].value = value;

		text = realloc(text, text_len + strlen(value) + 2);
		if (NULL == text) {
			fprintf(stderr, "%s:%u: realloc text failed\n",
				__FUNCTION__, __LINE__);
			goto out;
		}

		n = sprintf(&text[text_len], "%s\n", value);
		text_len += n;
	}

	error = NULL;
	value = g_key_file_get_string(key_file, "Radarplot License",
				      "signature", &error);
	if (NULL == value) {
		fprintf(stderr, "%s:%u: g_key_file_get_string '%s': %s\n",
			__FUNCTION__, __LINE__, "signature", error->message);
		goto out;
	}

	BN_hex2bn(&sign_bn, value);
	signature_len = BN_num_bytes(sign_bn);

	signature = malloc(signature_len);
	if (NULL == signature) {
		fprintf(stderr, "%s:%u: out of memory\n",
			__FUNCTION__, __LINE__);
		goto out;
	}

	BN_bn2bin(sign_bn, signature);


	OpenSSL_add_all_digests();

	md = EVP_get_digestbynid(nid);
	if (NULL == md) {
		fprintf(stderr, "%s:%u: unknown digest %u\n",
			__FUNCTION__, __LINE__, nid);
		goto out;
	}

	public = RSA_new();
	if (NULL == public) {
		fprintf(stderr, "%s:%u: RSA_new failed\n",
			__FUNCTION__, __LINE__);
		goto out;
	}

	BN_dec2bn(&public->n, rsa_n);
	BN_dec2bn(&public->e, rsa_e);


	EVP_DigestInit(&mdctx, md);
	EVP_DigestUpdate(&mdctx, text, text_len);
	EVP_DigestFinal(&mdctx, md_value, &md_len);


	if (1 != RSA_verify(nid, md_value, md_len,
			    signature, signature_len, public)) {
		fprintf(stderr, "%s:%u: RSA_verify failed\n",
			__FUNCTION__, __LINE__);
		goto out;
	}

	for (i = 0; properties[i].key; i++)
		g_free(properties[i].value);
	g_free(value);
	free(text);
	BN_free(sign_bn);
	free(signature);
	RSA_free(public);

	*license = key_file;
	return 0;

out:
	g_key_file_free(key_file);

	for (i = 0; properties[i].key; i++) {
		if (properties[i].value)
			g_free(properties[i].value);
	}
	if (value)
		g_free(value);
	if (text)
		free(text);
	if (sign_bn)
		BN_free(sign_bn);
	if (signature)
		free(signature);
	if (public)
		RSA_free(public);

	return -1;
}

static int
escape(unsigned char *out, const char *fmt, const char *s)
{
	unsigned char buffer[1024], *p = buffer;
	unsigned char c;

	memset(buffer, 0, sizeof(buffer));

	if (s) {
		while ((c = *s++) != '\0') {
			if (('0' <= c && c <= '9') ||
			    ('A' <= c && c <= 'Z') ||
			    ('a' <= c && c <= 'z'))
				p += sprintf((char *) p, "%c", c);
			else
				p += sprintf((char *) p, "%%%02X", c);
		}
	}

	return sprintf((char *) out, fmt, buffer);
}

#ifdef __WIN32__
#define SOCKERROR	g_win32_error_message(WSAGetLastError())
#else /* __WIN32__ */
#define SOCKERROR	strerror(errno)
#endif /* __WIN32__ */

static void
radar_network_message(radar_t *radar, GtkMessageType type, const char *message)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new(GTK_WINDOW(radar->window),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					type, GTK_BUTTONS_CLOSE,
					message);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static void
radar_register_response(GtkDialog *dialog, gint response, gpointer user_data)
{
	radar_register_t *reg = user_data;
	unsigned char data[1024], *p;
	unsigned char packet[2048];
	char fromaddr[1024];
	unsigned int length;
	struct sockaddr_in sin;
	struct hostent *he;
	struct servent *se;
	const char *company, *name, *address1, *address2, *city, *state;
	const char *zip, *country, *email, *lat, *lon;
	int s, n;
	int res;
#ifdef __WIN32__
	WSADATA W;
#endif /* __WIN32__ */

	if (response != GTK_RESPONSE_OK)
		goto out;

	company = gtk_entry_get_text(GTK_ENTRY(reg->company));
	name = gtk_entry_get_text(GTK_ENTRY(reg->name));
	address1 = gtk_entry_get_text(GTK_ENTRY(reg->address1));
	address2 = gtk_entry_get_text(GTK_ENTRY(reg->address2));
	city = gtk_entry_get_text(GTK_ENTRY(reg->city));
	state = "";
	zip = gtk_entry_get_text(GTK_ENTRY(reg->zip));
	country = gtk_entry_get_text(GTK_ENTRY(reg->country));
	email = gtk_entry_get_text(GTK_ENTRY(reg->email));
	lat = gtk_entry_get_text(GTK_ENTRY(reg->lat));
	lon = gtk_entry_get_text(GTK_ENTRY(reg->lon));

#ifdef __WIN32__
	if (WSAStartup(0x0202, &W)) {
		snprintf((char *) packet, sizeof(packet),
			 "%s:%u: WSAStartup failed",
			 __FUNCTION__, __LINE__);
		radar_network_message(reg->radar, GTK_MESSAGE_ERROR, (char *) packet);
		goto out;
	}
#endif /* __WIN32__ */

	he = gethostbyname(SERVER);
	if (NULL == he) {
		snprintf((char *) packet, sizeof(packet),
			 "%s:%u: gethostbyname(%s) failed",
			 __FUNCTION__, __LINE__, SERVER);
		radar_network_message(reg->radar, GTK_MESSAGE_ERROR, (char *) packet);
		goto out;
	}

	sin.sin_family = AF_INET;
	memcpy(&sin.sin_addr.s_addr, he->h_addr_list[0], he->h_length);

	se = getservbyname("http", "tcp");
	if (NULL == se) {
		sin.sin_port = htons(80);
	} else {
		sin.sin_port = se->s_port;
	}

	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		snprintf((char *) packet, sizeof(packet),
			 "%s:%u: socket() failed: %s",
			 __FUNCTION__, __LINE__, SOCKERROR);
		radar_network_message(reg->radar, GTK_MESSAGE_ERROR, (char *) packet);
		goto out;
	}

	if (connect(s, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
		snprintf((char *) packet, sizeof(packet),
			 "%s:%u: connect() failed: %s",
			 __FUNCTION__, __LINE__, SOCKERROR);
		radar_network_message(reg->radar, GTK_MESSAGE_ERROR, (char *) packet);
		close(s);
		goto out;
	}

	p = data;

	sprintf(fromaddr, "\"%s\" <%s>", name, email);
	p += escape(p, "mailaddr=%s", MAILADDR);
	p += escape(p, "&hdr_from=%s", fromaddr);
	p += escape(p, "&hdr_subj=%s", SUBJECT);
	p += escape(p, "&not_empty=%s", NOT_EMPTY);
	p += escape(p, "&ret_page=%s", RET_PAGE);
	p += escape(p, "&company=%s", company);
	p += escape(p, "&name=%s", name);
	p += escape(p, "&address1=%s", address1);
	p += escape(p, "&address2=%s", address2);
	p += escape(p, "&city=%s", city);
	p += escape(p, "&state=%s", state);
	p += escape(p, "&zip=%s", zip);
	p += escape(p, "&country=%s", country);
	p += escape(p, "&email=%s", email);
	p += escape(p, "&lat=%s", lat);
	p += escape(p, "&lon=%s", lon);
	if (NULL != reg->id)
		p += escape(p, "&id=%s", reg->id);

	length = p - data;

	p = packet;

	p += sprintf((char *) p, "POST %s HTTP/1.1\r\n", MAILFORM);
	p += sprintf((char *) p, "Host: %s\r\n", SERVER);
	p += sprintf((char *) p, "Connection: close\r\n");
	p += sprintf((char *) p, "Content-Type: text/plain\r\n");
	p += sprintf((char *) p, "Content-Length: %u\r\n", length);
	p += sprintf((char *) p, "\r\n");
	p += sprintf((char *) p, "%s\r\n", data);

	n = p - packet;

	if (send(s, packet, n, 0) != n) {
		snprintf((char *) packet, sizeof(packet),
			 "%s:%u: send failed: %s",
			 __FUNCTION__, __LINE__, SOCKERROR);
		radar_network_message(reg->radar, GTK_MESSAGE_ERROR, (char *) packet);
		close(s);
		goto out;
	}

	memset(packet, 0, sizeof(packet));
	p = packet;

	while ((n = recv(s, p, sizeof(packet) - (p - packet), 0)) > 0) {
		p += n;
	}
	if (n < 0) {
		snprintf((char *) packet, sizeof(packet),
			 "%s:%u: recv() failed: %s",
			 __FUNCTION__, __LINE__, SOCKERROR);
		radar_network_message(reg->radar, GTK_MESSAGE_ERROR, (char *) packet);
		close(s);
		goto out;
	}

	close(s);

	p = (unsigned char *) strchr((char *) packet, '\n');
	if (p)
		*p = '\0';
	p = (unsigned char *) strchr((char *) packet, '\r');
	if (p)
		*p = '\0';

	if ((1 == sscanf((char *) packet, "HTTP/%*d.%*d %d", &res)) && (res == 200)) {
		snprintf((char *) packet, sizeof(packet),
			 _("Your data has been successfully sent to the "
			   "brainaid License Server. The license will be send "
			   "to you by E-Mail to <%s>."),
			 email);

		radar_network_message(reg->radar, GTK_MESSAGE_INFO, (char *) packet);
	} else {
		snprintf((char *) packet, sizeof(packet),
			_("Error talking to the brainaid License Server: %u"),
			res);

		radar_network_message(reg->radar, GTK_MESSAGE_ERROR, (char *) packet);
	}

out:
#ifdef __WIN32__
	WSACleanup();
#endif /* __WIN32__ */

	gtk_widget_destroy(GTK_WIDGET(dialog));

	if (NULL != reg->id)
		g_free(reg->id);

	free(reg);
}

static void
radar_register_name(GtkEntry *entry, gpointer user_data)
{
	radar_register_t *reg = user_data;
	const char *name;

	name = gtk_entry_get_text(entry);
	if (strlen(name)) {
		reg->flags |= REGISTER_NAME_SET;

		if (reg->flags & REGISTER_EMAIL_SET)
			gtk_dialog_set_response_sensitive(reg->dialog,
							  GTK_RESPONSE_OK,
							  TRUE);
	} else {
		gtk_dialog_set_response_sensitive(reg->dialog,
						  GTK_RESPONSE_OK, FALSE);
	}
}

static void
radar_register_email(GtkEntry *entry, gpointer user_data)
{
	radar_register_t *reg = user_data;
	const char *email;

	email = gtk_entry_get_text(entry);
	if (strlen(email) && strchr(email, '@')) {
		reg->flags |= REGISTER_EMAIL_SET;

		if (reg->flags & REGISTER_NAME_SET)
			gtk_dialog_set_response_sensitive(reg->dialog,
							  GTK_RESPONSE_OK,
							  TRUE);
	} else {
		gtk_dialog_set_response_sensitive(reg->dialog,
						  GTK_RESPONSE_OK, FALSE);
	}
}

static GtkWidget *
radar_register_entry(GtkWidget *box, const char *text, int max,
		     void (*action) (GtkEntry *, gpointer),
		     gpointer arg, GKeyFile *license, const gchar *token)
{
	char markup[128];
	GtkWidget *label;
	GtkWidget *entry;
	GtkWidget *vbox;
	gchar *p;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), PANEL_BORDER_WIDTH);
	if (max > 0)
		gtk_box_pack_start(GTK_BOX(box), vbox, FALSE, TRUE, 0);
	else
		gtk_box_pack_start(GTK_BOX(box), vbox, TRUE, TRUE, 0);

	if (NULL != text) {
		label = gtk_label_new(NULL);

		snprintf(markup, sizeof(markup), _(text),
			 "<span foreground=\"red\">*</span>");

		gtk_label_set_markup(GTK_LABEL(label), markup);
		gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
		gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);
	}

	entry = gtk_entry_new();
	if (license) {
		p = g_key_file_get_string(license, "Radarplot License",
					  token, NULL);
		if (p) {
			gtk_entry_set_text(GTK_ENTRY(entry), p);
			g_free(p);
		} else {
			gtk_entry_set_text(GTK_ENTRY(entry), "");
		}
	} else {
		gtk_entry_set_text(GTK_ENTRY(entry), "");
	}

	if (max > 0) {
		gtk_entry_set_max_length(GTK_ENTRY(entry), max);
		gtk_entry_set_width_chars(GTK_ENTRY(entry), max);
	} else if (max < 0) {
		gtk_entry_set_width_chars(GTK_ENTRY(entry), -max);
	} else {
		gtk_entry_set_width_chars(GTK_ENTRY(entry), 32);
	}
	gtk_box_pack_start(GTK_BOX(vbox), entry, TRUE, TRUE, 0);

	if (action)
		g_signal_connect(entry, "changed", G_CALLBACK(action), arg);

	return entry;
}

void
radar_register(GtkAction *action, gpointer user_data)
{
	radar_t *radar = user_data;
	radar_register_t *reg;
	char markup[1024];
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *hbox;

	reg = malloc(sizeof(radar_register_t));
	if (NULL == reg) {
		fprintf(stderr, "%s:%u: out of memory\n",
			__FUNCTION__, __LINE__);
		return;
	}
	memset(reg, 0, sizeof(radar_register_t));

	reg->radar = radar;

	dialog = gtk_dialog_new_with_buttons(_("Register Radarplot"),
					     GTK_WINDOW(radar->window),
					     GTK_DIALOG_DESTROY_WITH_PARENT |
					     GTK_DIALOG_NO_SEPARATOR,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_CANCEL,
					     GTK_STOCK_OK,
					     GTK_RESPONSE_OK,
					     NULL);

	reg->dialog = GTK_DIALOG(dialog);

	g_signal_connect(dialog, "response",
			 G_CALLBACK(radar_register_response), reg);

	gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),
					  GTK_RESPONSE_OK, FALSE);

	gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
				       PANEL_BORDER_WIDTH);

	label = gtk_label_new(NULL);

	snprintf(markup, sizeof(markup),
		 _("Please enter your data in the fields below and send\n"
	 	   "it to the brainaid License Server by clicking on \"OK\".\n"
		   "Fields with %s must be filled in.\n"),
		 "<span foreground=\"red\">*</span>");

	gtk_label_set_markup(GTK_LABEL(label), markup);

	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
			   label, TRUE, TRUE, 0);


	reg->company = radar_register_entry(GTK_DIALOG(dialog)->vbox,
			N_("Company:"), 0, NULL, NULL, radar->license, "company");

	reg->name = radar_register_entry(GTK_DIALOG(dialog)->vbox,
			N_("Name %s:"), 0,
			radar_register_name, reg, radar->license, "name");
	reg->address1 = radar_register_entry(GTK_DIALOG(dialog)->vbox,
			N_("Address:"), 0, NULL, NULL, radar->license, "address1");
	reg->address2 = radar_register_entry(GTK_DIALOG(dialog)->vbox,
			NULL, 0, NULL, NULL, radar->license, "address2");

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), PANEL_BORDER_WIDTH);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
			   hbox, TRUE, TRUE, 0);

	reg->zip = radar_register_entry(hbox, N_("ZIP:"), 8, NULL, NULL,
			radar->license, "zip");
	reg->city = radar_register_entry(hbox, N_("City:"), 0, NULL, NULL,
			radar->license, "city");

	reg->country = radar_register_entry(GTK_DIALOG(dialog)->vbox,
			N_("Country:"), 0, NULL, NULL, radar->license, "country");
	reg->email = radar_register_entry(GTK_DIALOG(dialog)->vbox,
			N_("E-Mail %s:"), 0,
			radar_register_email, reg, radar->license, "email");

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), PANEL_BORDER_WIDTH);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
			   hbox, TRUE, TRUE, 0);

	reg->lat = radar_register_entry(hbox, N_("Latitude:"), -16, NULL, NULL,
					radar->license, "lat");
	reg->lon = radar_register_entry(hbox, N_("Longitude:"), -16, NULL, NULL,
					radar->license, "lon");

	if (radar->license) {
		reg->id = g_key_file_get_string(radar->license,
						"Radarplot License",
						"id", NULL);
	}

	radar_register_name(GTK_ENTRY(reg->name), reg);
	radar_register_email(GTK_ENTRY(reg->email), reg);

	gtk_widget_show_all(dialog);
}

void
radar_license(GtkAction *action, gpointer user_data)
{
	radar_t *radar = user_data;
	GtkFileFilter *filt1, *filt2;
	static char title[128];
	GtkWidget *dialog;
	char *filename;

	dialog = gtk_file_chooser_dialog_new(_("Configure License"),
					     GTK_WINDOW(radar->window),
					     GTK_FILE_CHOOSER_ACTION_OPEN,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_CANCEL,
					     GTK_STOCK_OPEN,
					     GTK_RESPONSE_ACCEPT,
					     NULL);

	gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog), TRUE);
	gtk_file_chooser_set_show_hidden(GTK_FILE_CHOOSER(dialog), TRUE);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), FALSE);

	filt1 = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filt1, "*.lic");
	gtk_file_filter_set_name(filt1, _("License Files"));
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filt1);

	filt2 = gtk_file_filter_new();
	gtk_file_filter_add_pattern(filt2, "*");
	gtk_file_filter_set_name(filt2, _("All Files"));
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filt2);

	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), filt1);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
		goto out;

	filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
	if (filename) {
		if (radar->license_pathname)
			g_free(radar->license_pathname);
		radar->license_pathname = filename;

		if (radar->license) {
			g_key_file_free(radar->license);
			radar->license = NULL;
		}

		g_key_file_set_string(radar->key_file,
				      "Radarplot", "License",
				      radar->license_pathname);
		radar_save_config(radar, ".radarplot");

		verify_registration_file(radar->license_pathname,
					 &radar->license);

		if (radar->license) {
			sprintf(title, _("brainaid Radarplot (%.64s)"),
				g_key_file_get_string(radar->license,
						      "Radarplot License",
						      "name", NULL));
		} else {
			sprintf(title, _("brainaid Radarplot (unregistered)"));
		}

		gtk_window_set_title(GTK_WINDOW(radar->window), title);
	}

out:
	gtk_widget_destroy(dialog);
}

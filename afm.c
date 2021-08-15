/* $Id: afm.c,v 1.7 2009-07-22 06:18:49 ecd Exp $
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#include <gtk/gtk.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "afm.h"
#include "encoding.h"

static __inline__ unsigned int
afm_name_hash(const char *name, unsigned int len)
{
	unsigned int hash = 0;
	char c;

	while (len--) {
		c = *(name++);
		hash = (hash + (c << 4) + (c >> 4)) * 11;
	}
	return hash;
}

static void
afm_free_char_data(char_t *ch)
{
	if (ch->name)
		free(ch->name);
	free(ch);
}

static void
afm_insert_char_data(afm_t *afm, char_t *ch)
{
	unsigned int key, hash;

	if (NULL == ch->name)
		return;

	ch->name_len = strlen(ch->name);

	key = afm_name_hash(ch->name, ch->name_len);
	ch->name_hash = key;

	hash = (key ^ (key >> CHAR_HASH_BITS)) % CHAR_HASH_SIZE;

	ch->next_by_name = afm->char_hash[hash];
	afm->char_hash[hash] = ch;

//printf("char '%s': %d %u [%d %d %d %d]\n",
//	ch->name, ch->index, ch->width,
//	ch->bbox.llx, ch->bbox.lly, ch->bbox.urx, ch->bbox.ury);
}

char_t *
afm_lookup_char_by_name(afm_t *afm, const char *name, unsigned int name_len)
{
	unsigned int key = afm_name_hash(name, name_len);
	unsigned int hash = (key ^ (key >> CHAR_HASH_BITS)) % CHAR_HASH_SIZE;
	char_t *list = afm->char_hash[hash];

	while (list) {
		if ((list->name_len == name_len) && !strcmp(list->name, name))
			return list;
		list = list->next_by_name;
	}

	return NULL;
}

static void
afm_free_ligature_data(ligature_t *lig)
{
	if (lig->name[0])
		free(lig->name[0]);
	if (lig->name[1])
		free(lig->name[1]);
	if (lig->ligature)
		free(lig->ligature);
	free(lig);
}

static void
afm_insert_ligature_data(afm_t *afm, ligature_t *lig)
{
	unsigned long key;
	unsigned int hash;
	char_t *ch0, *ch1, *ch;

	ch0 = afm_lookup_char_by_name(afm, lig->name[0], strlen(lig->name[0]));
	ch1 = afm_lookup_char_by_name(afm, lig->name[1], strlen(lig->name[1]));
	ch = afm_lookup_char_by_name(afm, lig->ligature, strlen(lig->ligature));

	if (NULL == ch0)
		fprintf(stderr, "%s:%u: char '%s' not found\n", __FUNCTION__, __LINE__, lig->name[0]);
	if (NULL == ch1)
		fprintf(stderr, "%s:%u: char '%s' not found\n", __FUNCTION__, __LINE__, lig->name[1]);
	if (NULL == ch)
		fprintf(stderr, "%s:%u: char '%s' not found\n", __FUNCTION__, __LINE__, lig->ligature);

	if ((NULL == ch0) || (NULL == ch1) || (NULL == ch)) {
		afm_free_ligature_data(lig);
		return;
	}

	lig->chars[0] = ch0;
	lig->chars[1] = ch1;
	lig->ch = ch;

	key = (unsigned long) ch0 + (unsigned long) ch1;
	hash = (key ^ (key >> LIGATURE_HASH_BITS)) % LIGATURE_HASH_SIZE;

	lig->next_by_chars = afm->ligature_hash[hash];
	afm->ligature_hash[hash] = lig;

//printf("ligature '%s' - '%s': '%s'\n", lig->name[0], lig->name[1], lig->ligature);
}

ligature_t *
afm_lookup_ligature(afm_t *afm, char_t *ch0, char_t *ch1)
{
	unsigned long key = (unsigned long) ch0 + (unsigned long) ch1;
	unsigned int hash = (key ^ (key >> LIGATURE_HASH_BITS)) % LIGATURE_HASH_SIZE;
	ligature_t *list = afm->ligature_hash[hash];

	while (list) {
		if ((list->chars[0] == ch0) && (list->chars[1] == ch1))
			return list;
		list = list->next_by_chars;
	}

	return NULL;
}

static void
afm_free_kern_data(kern_t *kern)
{
	if (kern->name[0])
		free(kern->name[0]);
	if (kern->name[1])
		free(kern->name[1]);
	free(kern);
}

static void
afm_insert_kern_data(afm_t *afm, kern_t *kern)
{
	unsigned long key;
	unsigned int hash;
	char_t *ch0, *ch1;

	ch0 = afm_lookup_char_by_name(afm, kern->name[0],
				      strlen(kern->name[0]));
	ch1 = afm_lookup_char_by_name(afm, kern->name[1],
				      strlen(kern->name[1]));

	if (NULL == ch0)
		fprintf(stderr, "%s:%u: char '%s' not found\n", __FUNCTION__, __LINE__, kern->name[0]);
	if (NULL == ch1)
		fprintf(stderr, "%s:%u: char '%s' not found\n", __FUNCTION__, __LINE__, kern->name[1]);

	if ((NULL == ch0) || (NULL == ch1)) {
		afm_free_kern_data(kern);
		return;
	}

	kern->chars[0] = ch0;
	kern->chars[1] = ch1;

	key = (unsigned long) ch0 + (unsigned long) ch1;
	hash = (key ^ (key >> KERN_HASH_BITS)) % KERN_HASH_SIZE;

	kern->next_by_chars = afm->kern_hash[hash];
	afm->kern_hash[hash] = kern;

//printf("kern '%s' - '%s': %d\n", kern->name[0], kern->name[1], kern->value);
}

kern_t *
afm_lookup_kern(afm_t *afm, char_t *ch0, char_t *ch1)
{
	unsigned long key = (unsigned long) ch0 + (unsigned long) ch1;
	unsigned int hash = (key ^ (key >> KERN_HASH_BITS)) % KERN_HASH_SIZE;
	kern_t *list = afm->kern_hash[hash];

	while (list) {
		if ((list->chars[0] == ch0) && (list->chars[1] == ch1))
			return list;
		list = list->next_by_chars;
	}

	return NULL;
}

int
afm_read_file(const char *filename, afm_t **afmp)
{
	ligature_t *lig, *tmp_lig = NULL, **lig_last = &tmp_lig;
	char_t *ch;
	kern_t *kern;
	afm_t *afm;
	char *buffer, *p, *q, *r;
	char *line, *token, *end;
	gsize size;
	GError *error;
	unsigned int i, c;

	error = NULL;
	if (!g_file_get_contents(filename, &buffer, &size, &error)) {
		fprintf(stderr, "%s:%u: read '%s': %s\n",
			__FUNCTION__, __LINE__, filename, error->message);
		return -1;
	}

	afm = malloc(sizeof(afm_t));
	if (NULL == afm) {
		fprintf(stderr, "%s:%u: afm: %s\n",
			__FUNCTION__, __LINE__, strerror(ENOMEM));
		free(buffer);
		return -1;
	}
	memset(afm, 0, sizeof(afm_t));

	line = buffer;
	while (NULL != (p = strchr(line, '\n'))) {
		*p = '\0';
		if (*(p - 1) == '\r')
			*(p - 1) = '\0';

		token = line;

		q = strchr(line, ' ');
		if (q) {
			*q = '\0';
			line = q + 1;
		}

		if (!strcmp(token, "Comment"))
			goto next;
		if (!strcmp(token, "Notice"))
			goto next;
		if (!strcmp(token, "StartFontMetrics"))
			goto next;
		if (!strcmp(token, "EndFontMetrics"))
			goto next;
		if (!strcmp(token, "StartCharMetrics"))
			goto next;
		if (!strcmp(token, "StartKernData"))
			goto next;
		if (!strcmp(token, "EndKernData"))
			goto next;
		if (!strcmp(token, "StartKernPairs"))
			goto next;
		if (!strcmp(token, "EndKernPairs"))
			goto next;
		if (!strcmp(token, "Version"))
			goto next;
		if (!strcmp(token, "CapHeight"))
			goto next;
		if (!strcmp(token, "XHeight"))
			goto next;
		if (!strcmp(token, "StdHW"))
			goto next;
		if (!strcmp(token, "StdVW"))
			goto next;

		if (!strcmp(token, "ItalicAngle"))
			goto next;
		if (!strcmp(token, "IsFixedPitch"))
			goto next;
		if (!strcmp(token, "CharacterSet"))
			goto next;
		if (!strcmp(token, "FontBBox"))
			goto next;
		if (!strcmp(token, "Ascender")) {
			afm->font_ascent = strtol(line, &end, 10);
			if (end == line || *end != '\0')
				goto error;
			goto next;
		}
		if (!strcmp(token, "Descender")) {
			afm->font_descent = strtol(line, &end, 10);
			if (end == line || *end != '\0')
				goto error;
			goto next;
		}
		if (!strcmp(token, "UnderlinePosition"))
			goto next;
		if (!strcmp(token, "UnderlineThickness"))
			goto next;
		if (!strcmp(token, "EncodingScheme"))
			goto next;

		if (!strcmp(token, "FontName")) {
			afm->font_name = strdup(line);
			if (NULL == afm->font_name) {
				fprintf(stderr, "%s:%u: %s: out of memory",
					__FUNCTION__, __LINE__, token);
				goto next;
			}

			goto next;
		}

		if (!strcmp(token, "FullName")) {
			afm->full_name = strdup(line);
			if (NULL == afm->full_name) {
				fprintf(stderr, "%s:%u: %s: out of memory",
					__FUNCTION__, __LINE__, token);
				goto next;
			}

			goto next;
		}

		if (!strcmp(token, "FamilyName")) {
			afm->family_name = strdup(line);
			if (NULL == afm->family_name) {
				fprintf(stderr, "%s:%u: %s: out of memory",
					__FUNCTION__, __LINE__, token);
				goto next;
			}

			goto next;
		}

		if (!strcmp(token, "Weight")) {
			afm->weight = strdup(line);
			if (NULL == afm->weight) {
				fprintf(stderr, "%s:%u: %s: out of memory",
					__FUNCTION__, __LINE__, token);
				goto next;
			}

			goto next;
		}

		if (!strcmp(token, "C")) {
/*
 * Data: 'C <number> ; WX <width> ; N <name> ; B <llx> <lly> <urx> <ury> ;'
 */
			q = strchr(line, ';');
			if (NULL == q)
				goto error;
			*q = '\0';
			if (*(q - 1) == ' ')
				*(q - 1) = '\0';

			ch = malloc(sizeof(char_t));
			if (NULL == ch) {
				fprintf(stderr, "%s:%u: char: out of memory",
					__FUNCTION__, __LINE__);
				goto next;
			}
			memset(ch, 0, sizeof(char_t));

			while (1) {
				line = q + 1;
				while (*line == ' ')
					line++;

				q = strchr(line, ';');
				if (NULL == q)
					break;
				*q = '\0';
				if (*(q - 1) == ' ')
					*(q - 1) = '\0';

				token = line;
				r = strchr(line, ' ');
				if (NULL == r) {
					afm_free_char_data(ch);
					goto error;
				}
				*r = '\0';

				line = r + 1;

				if (!strcmp(token, "N")) {
					ch->name = strdup(line);
					if (NULL == ch->name) {
						fprintf(stderr, "%s:%u: char name: out of memory",
							__FUNCTION__, __LINE__);
						afm_free_char_data(ch);
						goto next;
					}
				} else if (!strcmp(token, "WX")) {
					ch->width = strtol(line, &end, 10);
					if ((line == end) || (*end != '\0')) {
						afm_free_char_data(ch);
						goto error;
					}
				} else if (!strcmp(token, "B")) {
					if (4 != sscanf(line, "%d %d %d %d",
							&ch->bbox.llx,
							&ch->bbox.lly,
							&ch->bbox.urx,
							&ch->bbox.ury)) {
						afm_free_char_data(ch);
						goto error;
					}
				} else if (!strcmp(token, "L")) {
/*
 * Ligature: 'L <name> <ligature-name>'
 */
					lig = malloc(sizeof(ligature_t));
					if (NULL == lig) {
						fprintf(stderr, "%s:%u: ligature: out of memory",
							__FUNCTION__, __LINE__);
						goto next;
					}
					memset(lig, 0, sizeof(ligature_t));

					r = strchr(line, ' ');
					if (NULL == r) {
						afm_free_ligature_data(lig);
						goto error;
					}
					*r = '\0';

					lig->name[0] = strdup(ch->name);
					if (NULL == lig->name[0]) {
						fprintf(stderr, "%s:%u: lig name 0: out of memory",
							__FUNCTION__, __LINE__);
						afm_free_ligature_data(lig);
						goto next;
					}
					lig->name[1] = strdup(line);
					if (NULL == lig->name[1]) {
						fprintf(stderr, "%s:%u: lig name 1: out of memory",
							__FUNCTION__, __LINE__);
						afm_free_ligature_data(lig);
						goto next;
					}
					lig->ligature = strdup(r + 1);
					if (NULL == lig->ligature) {
						fprintf(stderr, "%s:%u: lig ligature: out of memory",
							__FUNCTION__, __LINE__);
						afm_free_ligature_data(lig);
						goto next;
					}

					*lig_last = lig;
					lig_last = &lig->next_by_chars;
				}
			}

			afm_insert_char_data(afm, ch);
			goto next;
		}

		if (!strcmp(token, "EndCharMetrics")) {
			while (tmp_lig) {
				lig = tmp_lig->next_by_chars;

				tmp_lig->next_by_chars = NULL;
				afm_insert_ligature_data(afm, tmp_lig);

				tmp_lig = lig;
			}

			goto next;
		}

		if (!strcmp(token, "KPX")) {
/*
 * Kerning Data: 'KPX <name1> <name2> <kern_value>'.
 */
			kern = malloc(sizeof(kern_t));
			if (NULL == kern) {
				fprintf(stderr, "%s:%u: kern: out of memory",
					__FUNCTION__, __LINE__);
				goto next;
			}

			q = strchr(line, ' ');
			if (NULL == q) {
				afm_free_kern_data(kern);
				goto error;
			}
			*q = '\0';
			kern->name[0] = strdup(line);
			if (NULL == kern->name[0]) {
				fprintf(stderr, "%s:%u: kern name 0: out of memory",
					__FUNCTION__, __LINE__);
				afm_free_kern_data(kern);
				goto next;
			}

			line = q + 1;

			q = strchr(line, ' ');
			if (NULL == q) {
				afm_free_kern_data(kern);
				goto error;
			}
			*q = '\0';
			kern->name[1] = strdup(line);
			if (NULL == kern->name[1]) {
				fprintf(stderr, "%s:%u: kern name 1: out of memory",
					__FUNCTION__, __LINE__);
				afm_free_kern_data(kern);
				goto next;
			}

			line = q + 1;
			kern->value = strtol(line, &end, 10);
			if ((end == line) || (*end != '\0')) {
				afm_free_kern_data(kern);
				goto error;
			}

			afm_insert_kern_data(afm, kern);
			goto next;
		}

	error:
		printf("%s: parse error in '%s': '%s'\n", __FUNCTION__, token, line);

	next:
		line = p + 1;
	}

	free(buffer);

	for (i = 0; i < nr_iso8859_chars; i++) {
		ch = afm_lookup_char_by_name(afm, iso8859[i].name,
					     strlen(iso8859[i].name));
		if (NULL == ch) {
			printf("%s:%u: character '%s' not found\n",
				__FUNCTION__, __LINE__, iso8859[i].name);
			continue;
		}

		ch->index = iso8859[i].index;
		afm->char_table[ch->index].ch = ch;
	}

	c = 32;
	for (i = 0; i < LIGATURE_HASH_SIZE; i++) {
		lig = afm->ligature_hash[i];
		while (lig) {
			while (afm->char_table[c].ch)
				c++;

			lig->ch->index = c;
			afm->char_table[c].ch = lig->ch;

			lig = lig->next_by_chars;
		}
	}

	*afmp = afm;
	return 0;
}

void
afm_free(afm_t *afm)
{
	unsigned int hash;
	kern_t *kern;
	ligature_t *lig;
	char_t *ch;

	for (hash = 0; hash < KERN_HASH_SIZE; hash++) {
		while (afm->kern_hash[hash]) {
			kern = afm->kern_hash[hash]->next_by_chars;
			afm_free_kern_data(afm->kern_hash[hash]);
			afm->kern_hash[hash] = kern;
		}
	}

	for (hash = 0; hash < LIGATURE_HASH_SIZE; hash++) {
		while (afm->ligature_hash[hash]) {
			lig = afm->ligature_hash[hash]->next_by_chars;
			afm_free_ligature_data(afm->ligature_hash[hash]);
			afm->ligature_hash[hash] = lig;
		}
	}

	for (hash = 0; hash < CHAR_HASH_SIZE; hash++) {
		while (afm->char_hash[hash]) {
			ch = afm->char_hash[hash]->next_by_name;
			afm_free_char_data(afm->char_hash[hash]);
			afm->char_hash[hash] = ch;
		}
	}

	if (afm->font_name)
		free(afm->font_name);
	if (afm->full_name)
		free(afm->full_name);
	if (afm->family_name)
		free(afm->family_name);
	if (afm->weight)
		free(afm->weight);
}

static void
afm_add_char_extents(afm_extents_t *extents, int *first,
		     char_t *ch, kern_t *kern, double scale, double rise)
{
	double llx = ((double) ch->bbox.llx) * scale;
	double lly = ((double) ch->bbox.lly) * scale;
	double urx = ((double) ch->bbox.urx) * scale;
	double ury = ((double) ch->bbox.ury) * scale;
	double width = ((double) ch->width) * scale;

	lly += rise * scale;
	ury += rise * scale;

	if (*first) {
		extents->bbox.llx = llx;
		extents->bbox.lly = lly;
		extents->bbox.urx = urx;
		extents->bbox.ury = ury;

		extents->ascent = ury;
		extents->descent = lly;

		extents->width = width;
		*first = 0;

		return;
	}

	if (kern)
		extents->width += ((double) kern->value) * scale;

	if (extents->width + llx < extents->bbox.llx)
		extents->bbox.llx = extents->width + llx;

	if (extents->width + urx > extents->bbox.urx)
		extents->bbox.urx = extents->width + urx;

	extents->width += width;

	if (ury > extents->bbox.ury) {
		extents->bbox.ury = ury;
		extents->ascent = ury;
	}
	if (lly < extents->bbox.lly) {
		extents->bbox.lly = lly;
		extents->descent = lly;
	}
}

static char_t *
afm_add_string_extents(afm_extents_t *extents, afm_t *afm,
		       const unsigned char *text, int *first,
		       unsigned int start, unsigned int end,
		       double scale, double rise)
{
	char_t *this, *next;
	kern_t *kern = NULL;
	ligature_t *lig;
	unsigned int i;

	this = afm->char_table[((unsigned int)text[start])].ch;

	for (i = start + 1; i < end; i++) {
		next = afm->char_table[((unsigned int)text[i])].ch;

		lig = afm_lookup_ligature(afm, this, next);
		if (NULL != lig) {
			this = lig->ch;
			if (++i == end)
				break;
			next = afm->char_table[((unsigned int)text[i])].ch;
		}

		afm_add_char_extents(extents, first, this, kern, scale, rise);

		kern = afm_lookup_kern(afm, this, next);

		this = next;
	}

	afm_add_char_extents(extents, first, this, kern, scale, rise);

	return this;
}

int
afm_text_extents(afm_t *afm, double scale,
		 const char *markup, size_t len, afm_extents_t *extents)
{
	PangoAttrList *attr_list;
	PangoAttrIterator *iter;
	GSList *alist, *anext;
	PangoAttribute *attr;
	double rise, fscale;
	gint start, end;
	char *utf8_markup;
	char *utf8_text;
	unsigned char *text;
	int first = 1;

	utf8_markup = g_convert(markup, len, "UTF-8", "ISO-8859-1",
				NULL, &len, NULL);
	if (NULL == utf8_markup) {
		fprintf(stderr, "%s: g_convert to UTF-8 failed for '%s'\n",
			__FUNCTION__, markup);
		g_free(utf8_markup);
		return 0;
	}

	if (!pango_parse_markup(utf8_markup, -1, 0,
				&attr_list, &utf8_text, NULL, NULL)) {
		fprintf(stderr, "%s: pango_parse_markup failed for '%s'\n",
			__FUNCTION__, markup);
		g_free(utf8_markup);
		return 0;
	}
	g_free(utf8_markup);

	text = (unsigned char *) g_convert(utf8_text, -1, "ISO-8859-1", "UTF-8",
					   NULL, &len, NULL);
	if (NULL == text) {
		fprintf(stderr, "%s: g_convert to ISO_8859_1 failed for '%s'\n",
			__FUNCTION__, utf8_text);
		g_free(utf8_text);
		return 0;
	}
	g_free(utf8_text);

	memset(extents, 0, sizeof(afm_extents_t));

	if (0 == len)
		goto out;

	iter = pango_attr_list_get_iterator(attr_list);
	do {
		pango_attr_iterator_range(iter, &start, &end);

		if (end > len)
			end = len;

		fscale = 1.0;
		rise = 0.0;

		alist = pango_attr_iterator_get_attrs(iter);

		while (alist) {
			anext = alist->next;
			alist->next = NULL;

			attr = alist->data;

			switch (attr->klass->type) {
			case PANGO_ATTR_RISE:
			{
				PangoAttrInt *arise = (void *) attr;
				rise = ((double) arise->value) / 10000.0;
				break;
			}
			case PANGO_ATTR_SCALE:
			{
				PangoAttrFloat *ascale = (void *) attr;
				fscale = ascale->value;
				break;
			}
			default:
				fprintf(stderr, "%s: unhandled attr %u\n",
					__FUNCTION__, attr->klass->type);
				break;
			}

			pango_attribute_destroy(attr);
			g_slist_free(alist);

			alist = anext;
		}

		afm_add_string_extents(extents, afm, text, &first,
				       start, end, fscale,
				       afm->font_ascent * rise);

		if (end == len)
			break;

	} while (TRUE == pango_attr_iterator_next(iter));

	pango_attr_iterator_destroy(iter);


	extents->bbox.ury *= scale / 1000.0;
	extents->bbox.lly *= scale / 1000.0;

	extents->width *= scale / 1000.0;
	extents->ascent *= scale / 1000.0;
	extents->descent *= scale / 1000.0;

	extents->bbox.llx *= scale / 1000.0;
	extents->bbox.urx *= scale / 1000.0;

out:
	pango_attr_list_unref(attr_list);
	g_free(text);

	return extents->width;
}

static int
afm_output_char(FILE *file, kern_t *kern, unsigned int c)
{
	int output = 0;

	if (kern && (0 != kern->value))
		output += fprintf(file, ") %d (", -kern->value);

	if ((c < 128) && isprint(c)) {
		switch (c) {
		case '(':
		case ')':
		case '\\':
			output += fprintf(file, "\\%c", c);
			break;
		default:
			output += fprintf(file, "%c", c);
			break;
		}

		return output;
	}

	output += fprintf(file, "\\%03o", c);
	return output;
}

static int
afm_output_string(FILE *file, afm_t *afm, const unsigned char *text,
		  unsigned int start, unsigned int end,
		  double scale, double rise)
{
	char_t *this, *next;
	kern_t *kern = NULL;
	ligature_t *lig;
	unsigned int i;
	int offset = 0;

	offset += fprintf(file, "   /%s %.3f Tf\n", afm->pdf_name, scale);
	offset += fprintf(file, "   %.3f Ts\n", rise);
	offset += fprintf(file, "   [ (");

	this = afm->char_table[((unsigned int)text[start])].ch;

	for (i = start + 1; i < end; i++) {

		next = afm->char_table[((unsigned int)text[i])].ch;

		lig = afm_lookup_ligature(afm, this, next);
		if (NULL != lig) {
			this = lig->ch;
			if (++i == end)
				break;
			next = afm->char_table[((unsigned int)text[i])].ch;
		}

		offset += afm_output_char(file, kern, this->index);

		kern = afm_lookup_kern(afm, this, next);

		this = next;
	}

	offset += afm_output_char(file, kern, this->index);

	offset += fprintf(file, ") ] TJ\n");
	return offset;
}

int
afm_print_text(FILE *file, afm_t *afm, double scale, double x, double y,
	       const char *markup, size_t len)
{
	PangoAttrList *attr_list;
	PangoAttrIterator *iter;
	GSList *alist, *anext;
	PangoAttribute *attr;
	double rise, fscale;
	gint start, end;
	char *utf8_markup;
	char *utf8_text;
	unsigned char *text;
	int offset = 0;

	utf8_markup = g_convert(markup, len, "UTF-8", "ISO-8859-1",
				NULL, &len, NULL);
	if (NULL == utf8_markup) {
		fprintf(stderr, "%s: g_convert to UTF-8 failed for '%s'\n",
			__FUNCTION__, markup);
		g_free(utf8_markup);
		return 0;
	}

	if (!pango_parse_markup(utf8_markup, -1, 0,
				&attr_list, &utf8_text, NULL, NULL)) {
		fprintf(stderr, "%s: pango_parse_markup failed for '%s'\n",
			__FUNCTION__, markup);
		g_free(utf8_markup);
		return 0;
	}
	g_free(utf8_markup);

	text = (unsigned char *) g_convert(utf8_text, -1, "ISO-8859-1", "UTF-8",
					   NULL, &len, NULL);
	if (NULL == text) {
		fprintf(stderr, "%s: g_convert to ISO_8859_1 failed for '%s'\n",
			__FUNCTION__, utf8_text);
		g_free(utf8_text);
		return 0;
	}
	g_free(utf8_text);

	if (0 == len)
		goto out;

	offset += fprintf(file, "BT\n");
	offset += fprintf(file, "   %.3f %.3f Td\n", x, y);

	iter = pango_attr_list_get_iterator(attr_list);
	do {
		pango_attr_iterator_range(iter, &start, &end);

		if (end > len)
			end = len;

		fscale = 1.0;
		rise = 0.0;

		alist = pango_attr_iterator_get_attrs(iter);

		while (alist) {
			anext = alist->next;
			alist->next = NULL;

			attr = alist->data;

			switch (attr->klass->type) {
			case PANGO_ATTR_RISE:
			{
				PangoAttrInt *arise = (void *) attr;
				rise = ((double) arise->value) / 10000.0;
				break;
			}
			case PANGO_ATTR_SCALE:
			{
				PangoAttrFloat *ascale = (void *) attr;
				fscale = ascale->value;
				break;
			}
			default:
				fprintf(stderr, "%s: unhandled attr %u\n",
					__FUNCTION__, attr->klass->type);
				break;
			}

			pango_attribute_destroy(attr);
			g_slist_free(alist);

			alist = anext;
		}

		offset += afm_output_string(file, afm, text, start, end,
					    scale * fscale,
					    scale * fscale * afm->font_ascent
							   * rise / 1000.0);

		if (end == len)
			break;
	} while (TRUE == pango_attr_iterator_next(iter));

	pango_attr_iterator_destroy(iter);

	offset += fprintf(file, "ET\n");

out:
	pango_attr_list_unref(attr_list);
	g_free(text);

	return offset;
}

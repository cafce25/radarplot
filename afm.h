/* $Id: afm.h,v 1.4 2005/10/15 06:47:56 ecd Exp $
 */

#ifndef _AFM_H
#define _AFM_H 1

struct __char_s__;
typedef struct __char_s__ char_t;

typedef struct {
	int	llx;
	int	lly;
	int	urx;
	int	ury;
} bbox_t;


struct __char_s__ {
	char		*name;
	unsigned int	name_len;
	char_t		*next_by_name;
	unsigned int	name_hash;

	int		index;
	char_t		*next_by_index;

	int		width;
	bbox_t		bbox;
};


struct __ligature_s__;
typedef struct __ligature_s__ ligature_t;

struct __ligature_s__ {
	char_t		*chars[2];
	ligature_t	*next_by_chars;

	char_t		*ch;

	char		*ligature;
	char		*name[2];
};


struct __kern_s__;
typedef struct __kern_s__ kern_t;

struct __kern_s__ {
	char_t		*chars[2];
	kern_t		*next_by_chars;

	int		value;

	char		*name[2];
};

#define CHAR_HASH_BITS		7
#define CHAR_HASH_SIZE		(1 << CHAR_HASH_BITS)

#define LIGATURE_HASH_BITS	3
#define LIGATURE_HASH_SIZE	(1 << CHAR_HASH_BITS)

#define KERN_HASH_BITS		9
#define KERN_HASH_SIZE		(1 << KERN_HASH_BITS)

typedef struct {
	unsigned char	index;
	char_t		*ch;
} char_table_t;


typedef struct {
	char		*pdf_name;

	char		*font_name;
	char		*full_name;
	char		*family_name;
	char		*weight;

	int		font_ascent;
	int		font_descent;

	char_t		*char_hash[CHAR_HASH_SIZE];
	ligature_t	*ligature_hash[LIGATURE_HASH_SIZE];
	kern_t		*kern_hash[KERN_HASH_SIZE];

	char_table_t	char_table[256];
} afm_t;

typedef struct {
	double		ascent;
	double		descent;
	double		width;

	struct {
		double	llx;
		double	lly;
		double	urx;
		double	ury;
	} bbox;
} afm_extents_t;

int afm_read_file(const char *filename, afm_t **afmp);
void afm_free(afm_t *afm);

char_t *afm_lookup_char_by_name(afm_t *afm, const char *name,
				unsigned int name_len);

int afm_text_extents(afm_t *afm, double scale,
		     const char *markup, size_t len,
		     afm_extents_t *extents);

int afm_print_text(FILE *file, afm_t *afm, double scale, double x, double y,
		     const char *markup, size_t len);

#endif /* !(_AFM_H) */

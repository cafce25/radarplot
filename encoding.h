/* $Id: encoding.h,v 1.1 2005/04/22 13:39:06 ecd Exp $
 */

#ifndef _ENCODING_H
#define _ENCODING_H 1

typedef struct {
	unsigned char	index;
	const char	*name;
} encoding_t;
	
extern const encoding_t iso8859[];
extern unsigned int nr_iso8859_chars;

#endif /* !(_ENCODING_H) */

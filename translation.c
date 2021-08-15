/* $Id: translation.c,v 1.9 2009-07-22 06:18:50 ecd Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "translation.h"

static const language_t languages[] =
{
	{
		"German",
		"de_DE"
	},
	{
		"English",
		"en_US"
	},
	{
		NULL,
	}
};

const language_t *radar_languages = languages;

/*! \file cpeuri.c
 *  \brief Interface to Common Platform Enumeration (CPE) URI
 *  
 *   See more details at http://nvd.nist.gov/cpe.cfm
 *  
 */

/*
 * Copyright 2009 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      Brandon Dixon  <brandon.dixon@g2-inc.com>
 *      Lukas Kuklinek <lkuklinek@redhat.com>
 */

#define _BSD_SOURCE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include <pcre.h>
#include <ctype.h>

#include "cpeuri.h"
#include "common/util.h"

#define CPE_URI_SUPPORTED "2.2"

// enumeration of CPE URI fields (useful for indexing arrays)
enum cpe_field_t {
	CPE_FIELD_TYPE = 0,
	CPE_FIELD_VENDOR,
	CPE_FIELD_PRODUCT,
	CPE_FIELD_VERSION,
	CPE_FIELD_UPDATE,
	CPE_FIELD_EDITION,
	CPE_FIELD_LANGUAGE,
	CPE_BASIC_FIELDNUM,
	CPE_FIELD_SW_EDITION = CPE_BASIC_FIELDNUM,
	CPE_FIELD_TARGET_SW,
	CPE_FIELD_TARGET_HW,
	CPE_FIELD_OTHER,
	CPE_TOTAL_FIELDNUM
};

struct cpe_name {
//      char *uri;       // complete URI cache
	cpe_format_t format;
	cpe_part_t part;	// part
	char *vendor;		// vendor
	char *product;		// product
	char *version;		// version
	char *update;		// update
	/* also used to pack the 4 extended attributes to CPE 2.2 URIs
	   we always load these into the model so the packed format should
	   never appear in the edition field itself
	*/
	char *edition;
	char *language;		// language

	/* extended attributes */
	char *sw_edition;
	char *target_sw;
	char *target_hw;
	char *other;
	/* end of extended attributes */
};

/* h - hardware
 * o - OS
 * a - application
 */
//static const char *CPE_PART_CHAR[] = { NULL, "h", "o", "a" };

static const char CPE_SEP_CHAR = ':';

static const struct oscap_string_map CPE_PART_MAP[] = {
	{ CPE_PART_HW,   "h"  },
	{ CPE_PART_OS,   "o"  },
	{ CPE_PART_APP,  "a"  },
	{ CPE_PART_NONE, NULL }
};

char **cpe_uri_split(char *str, const char *delim);
static bool cpe_urldecode(char *str);
bool cpe_name_check(const char *str);
static const char *cpe_get_field(const struct cpe_name *cpe, int idx);
static const char *as_str(const char *str);
static bool cpe_set_field(struct cpe_name *cpe, int idx, const char *newval);
/*
 * Fill @a cpe structure with parsed @a fields.
 *
 * Fields can be obtained via cpe_split().
 * Pointers in target sructure will point to same strings as pointers in @a fields do.
 * No string duplication is performed.
 *
 * @see cpe_split
 * @param cpe structure to be filled
 * @param fields NULL-terminated array of strings representing individual fields
 * @return true on success
 */
static bool cpe_assign_values(struct cpe_name *cpe, char **fields);

static int cpe_fields_num(const struct cpe_name *cpe)
{
	__attribute__nonnull__(cpe);

	if (cpe == NULL)
		return 0;
	int maxnum = 0;
	int i;
	for (i = 0; i < CPE_TOTAL_FIELDNUM; ++i)
		if (cpe_get_field(cpe, i))
			maxnum = i + 1;
	return maxnum;
}

static const char *cpe_get_field(const struct cpe_name *cpe, int idx)
{
	__attribute__nonnull__(cpe);

	if (cpe == NULL)
		return NULL;

	switch (idx) {
	case 0:
		return oscap_enum_to_string(CPE_PART_MAP, cpe->part);
	case 1:
		return cpe->vendor;
	case 2:
		return cpe->product;
	case 3:
		return cpe->version;
	case 4:
		return cpe->update;
	case 5:
		return cpe->edition;
	case 6:
		return cpe->language;
	case 7:
		return cpe->sw_edition;
	case 8:
		return cpe->target_sw;
	case 9:
		return cpe->target_hw;
	case 10:
		return cpe->other;
	default:
		assert(false);
		return NULL;
	}
}

bool cpe_set_field(struct cpe_name * cpe, int idx, const char *newval)
{

	__attribute__nonnull__(cpe);
	/*__attribute__nonnull__(newval); <-- can't be here, we want to set NULL */

	if (cpe == NULL)
		return false;

	char **fieldptr = NULL;
	switch (idx) {
	case 0:
		cpe->part = oscap_string_to_enum(CPE_PART_MAP, newval);
		return cpe->part != CPE_PART_NONE;
	case 1:
		fieldptr = &cpe->vendor;
		break;
	case 2:
		fieldptr = &cpe->product;
		break;
	case 3:
		fieldptr = &cpe->version;
		break;
	case 4:
		fieldptr = &cpe->update;
		break;
	case 5:
		fieldptr = &cpe->edition;
		break;
	case 6:
		fieldptr = &cpe->language;
		break;
	case 7:
		fieldptr = &cpe->sw_edition;
		break;
	case 8:
		fieldptr = &cpe->target_sw;
		break;
	case 9:
		fieldptr = &cpe->target_hw;
		break;
	case 10:
		fieldptr = &cpe->other;
		break;

	default:
		assert(false);
		return false;
	}

	oscap_free(*fieldptr);
	if (newval && strcmp(newval, "") == 0)
		newval = NULL;
	if (newval != NULL)
		*fieldptr = strdup(newval);
	else
		*fieldptr = NULL;

	return true;
}

struct cpe_name *cpe_name_new(const char *cpestr)
{

	int i;
	struct cpe_name *cpe;

	cpe_format_t format = cpe_name_get_format_of_str(cpestr);
	if (cpestr && format == CPE_FORMAT_UNKNOWN)
		return NULL;

	cpe = oscap_alloc(sizeof(struct cpe_name));
	if (cpe == NULL)
		return NULL;
	memset(cpe, 0, sizeof(struct cpe_name));	// zero memory

	if (cpestr) {
		if (format == CPE_FORMAT_URI) {
			char *data_ = strdup(cpestr + 5);	// without 'cpe:/'
			char **fields_ = oscap_split(data_, ":");
			for (i = 0; fields_[i]; ++i)
			{
				if (i == CPE_FIELD_EDITION)
				{
					// extended properties may be packed in "edition" field
					if (strlen(fields_[i]) >= 2 && fields_[i][0] == '~') {
						// first character is ~, that means that extended
						// attributes are embedded into "edition" field

						char **extended_attribs = oscap_split(fields_[i] + 1 * sizeof(char*), "~");
						// the first extended attribute is actually the edition
						oscap_free(fields_[i]);
						fields_[i] = extended_attribs[0];
						// the rest are ~-encoded extended attributes
						for (int j = 0; j < 4; ++j) {
							if (!cpe_urldecode(extended_attribs[1 + j])) {
								oscap_free(data_);
								oscap_free(fields_);
								return NULL;
							}

							cpe_set_field(cpe, CPE_BASIC_FIELDNUM + j, extended_attribs[1 + 0]);
						}

						oscap_free(extended_attribs); // we have used all the pointed to data
					}
				}

				if (!cpe_urldecode(fields_[i])) {
					oscap_free(data_);
					oscap_free(fields_);
				}
			}
			cpe_assign_values(cpe, fields_);
			oscap_free(data_);
			oscap_free(fields_);
		}
		else if (format == CPE_FORMAT_STRING) {
		}
		else if (format == CPE_FORMAT_WFN)
		{
		}
	}
	return cpe;
}

struct cpe_name * cpe_name_clone(struct cpe_name * old_name)
{
        struct cpe_name * new_name = oscap_alloc(sizeof(struct cpe_name));
        if (new_name == NULL) 
            return NULL;

	new_name->format = old_name->format;
	new_name->part = old_name->part;
	new_name->vendor = oscap_strdup(old_name->vendor);
	new_name->product = oscap_strdup(old_name->product);
	new_name->version = oscap_strdup(old_name->version);
	new_name->update = oscap_strdup(old_name->update);
	new_name->edition = oscap_strdup(old_name->edition);
	new_name->language = oscap_strdup(old_name->language);

        return new_name;
}

/**
 * Takes given urlencoded string and replaces all occurences of % followed by
 * 2 hex digits with a character.
 *
 * The operation is done in-place and is destructive to the data, even in
 * case of a failure!
 *
 * @see cpe_urlencode
 */
static bool cpe_urldecode(char *str)
{
	__attribute__nonnull__(str);

	char *inptr, *outptr;

	for (inptr = outptr = str; *inptr; ++inptr) {
		if (*inptr == '%') {
			if (isxdigit(inptr[1]) && isxdigit(inptr[2])) {
				char hex[3] = { inptr[1], inptr[2], '\0' };
				unsigned out;
				sscanf(hex, "%x", &out);
				if (out == 0) {
					// %00 encoded in the URI is definitely invalid
					*outptr++ = '\0';
					return false;
				}
				*outptr++ = out;
				inptr += 2;
			} else {
				// % followed by characters that are not hex digits is invalid
				*outptr = '\0';
				return false;
			}
		} else
			*outptr++ = *inptr;
	}
	*outptr = '\0';
	return true;
}

/**
 * Takes given string and replaces occurences of non-alphanumber characters
 * except -._~ with % followed by hex digits representing that particular
 * character.
 *
 * cpe_urlencode(" abc") would return "%20abc",
 * cpe_urlencode("%") would return "%25".
 */
static char *cpe_urlencode(const char *str)
{
	if (str == NULL)
		return NULL;

	// allocate enough space
	// in the worst case (all characters need to be replaced), the memory
	// we will need is 3 times the size of input, + 1 for the terminating \0
	char *result = oscap_alloc(strlen(str) * 3 * sizeof(char) + 1);
	char *out = result;

	for (const char *in = str; *in != '\0'; ++in, ++out) {
		if (isalnum(*in) || strchr("-._~", *in))
			*out = *in;
		else {
			// this char shall be %-encoded
			snprintf(out, 4, "%%%02X", *in); // we write 3 chars and \0
			out += 2; // for loop does another ++, giving us += 3 effectively
		}
	}

	// if the last character was alphanumeric we need to terminate
	// if the last character was non-alphanum we will have 2 consecutive
	// \0s at the end of the string which doesn't hurt anything
	*out = '\0';

	return result;
}

static bool cpe_has_extended_attributes(const struct cpe_name *cpe)
{
	return cpe && (
			cpe->sw_edition ||
			cpe->target_sw ||
			cpe->target_hw ||
			cpe->other
	);
}

static char *cpe_pack_extended_attributes(const struct cpe_name *cpe)
{
	return oscap_sprintf("~%s~%s~%s~%s",
			cpe_urlencode(cpe->sw_edition),
			cpe_urlencode(cpe->target_sw),
			cpe_urlencode(cpe->target_hw),
			cpe_urlencode(cpe->other)
	);
}

bool cpe_name_match_one(const struct cpe_name * cpe, const struct cpe_name * against)
{

	int i;
	if (cpe == NULL || against == NULL)
		return false;

	int cpefn = cpe_fields_num(cpe);
	if (cpe_fields_num(against) < cpefn)
		return false;

	for (i = 0; i < cpefn; ++i) {
		const char *cpefield = cpe_get_field(cpe, i);
		if (cpefield && strcasecmp(cpefield, as_str(cpe_get_field(against, i))) != 0)
			return false;
	}

	return true;
}

bool cpe_name_match_cpes(const struct cpe_name * name, size_t n, struct cpe_name ** namelist)
{

	__attribute__nonnull__(name);
	__attribute__nonnull__(namelist);

	if (name == NULL || namelist == NULL)
		return false;

	for (size_t i = 0; i < n; ++i)
		if (cpe_name_match_one(name, namelist[i]))
			return true;
	return false;
}

int cpe_name_match_strs(const char *candidate, size_t n, char **targets)
{
	__attribute__nonnull__(candidate);
	__attribute__nonnull__(targets);

	int i;
	struct cpe_name *ccpe, *tcpe;

	ccpe = cpe_name_new(candidate);	// candidate cpe
	if (ccpe == NULL)
		return -2;

	for (i = 0; i < (int)n; ++i) {
		tcpe = cpe_name_new(targets[i]);	// target cpe

		if (cpe_name_match_one(ccpe, tcpe)) {
			// CPE matched
			cpe_name_free(ccpe);
			cpe_name_free(tcpe);
			return i;
		}

		cpe_name_free(tcpe);
	}

	cpe_name_free(ccpe);
	return -1;
}

cpe_format_t cpe_name_get_format_of_str(const char *str)
{
	if (str == NULL)
		return CPE_FORMAT_UNKNOWN;

	pcre *re;
	const char *error;
	int erroffset;
	int rc;
	int ovector[30];

	re = pcre_compile("^cpe:/[aho]?(:[a-z0-9._~%-]*){0,6}$", PCRE_CASELESS, &error, &erroffset, NULL);
	rc = pcre_exec(re, NULL, str, strlen(str), 0, 0, ovector, 30);
	pcre_free(re);

	if (rc >= 0)
		return CPE_FORMAT_URI;

	re = pcre_compile("^cpe:2.3:[aho]?(:[a-z0-9._~%-]*){0,6}$", PCRE_CASELESS, &error, &erroffset, NULL);
	rc = pcre_exec(re, NULL, str, strlen(str), 0, 0, ovector, 30);
	pcre_free(re);

	if (rc >= 0)
		return CPE_FORMAT_STRING;

	// FIXME: This should be way more strict
	re = pcre_compile("^wfn:.+$", PCRE_CASELESS, &error, &erroffset, NULL);
	rc = pcre_exec(re, NULL, str, strlen(str), 0, 0, ovector, 30);
	pcre_free(re);

	if (rc >= 0)
		return CPE_FORMAT_WFN;

	return CPE_FORMAT_UNKNOWN;
}

bool cpe_name_check(const char *str)
{
	return cpe_name_get_format_of_str(str) != CPE_FORMAT_UNKNOWN;
}

static const char *as_str(const char *str)
{
	if (str == NULL)
		return "";
	return str;
}

char *cpe_name_get_uri(const struct cpe_name *cpe)
{
	__attribute__nonnull__(cpe);

	int len = 16;
	int i;
	char *result;
	char* part[CPE_BASIC_FIELDNUM] = { NULL }; // CPE URI parts

	if (cpe == NULL)
		return NULL;

	// get individual parts (%-encded)
	for (i = 0; i < CPE_BASIC_FIELDNUM; ++i) {
		if (i == CPE_FIELD_EDITION && cpe_has_extended_attributes(cpe))
			part[i] = cpe_pack_extended_attributes(cpe);
		else
			part[i] = cpe_urlencode(as_str(cpe_get_field(cpe, i)));

		len += strlen(part[i]);
	}

	result = oscap_alloc(len * sizeof(char));
	if (result == NULL)
		return NULL;

	// create the URI
	i = snprintf(result, len, "cpe:/%s:%s:%s:%s:%s:%s:%s",
		part[0], part[1], part[2], part[3], part[4], part[5], part[6]
	);

	// free individual parts
	for (int j = 0; j < CPE_BASIC_FIELDNUM; ++j)
		oscap_free(part[j]);

	// trim trailing colons
	while (result[--i] == ':')
		result[i] = '\0';

	return result;
}

int cpe_name_write(const struct cpe_name *cpe, FILE * f)
{
	__attribute__nonnull__(cpe);
	__attribute__nonnull__(f);

	int ret;
	char *uri;

	uri = cpe_name_get_uri(cpe);
	if (uri == NULL)
		return EOF;

	ret = fprintf(f, "%s", uri);

	oscap_free(uri);
	return ret;
}

static bool cpe_assign_values(struct cpe_name *cpe, char **fields)
{
	__attribute__nonnull__(cpe);
	__attribute__nonnull__(fields);

	int i;

	if (cpe == NULL || fields == NULL)
		return false;

	for (i = 0; fields[i]; ++i)
		cpe_set_field(cpe, i, fields[i]);

	return true;
}

void cpe_name_free(struct cpe_name *cpe)
{
	if (cpe == NULL)
		return;

	int i;
	for (i = 0; i < CPE_TOTAL_FIELDNUM; ++i)
		cpe_set_field(cpe, i, NULL);
	oscap_free(cpe);
}

const char * cpe_name_supported(void)
{
        return CPE_URI_SUPPORTED;
}

OSCAP_ACCESSOR_SIMPLE(cpe_format_t, cpe_name, format)
OSCAP_ACCESSOR_SIMPLE(cpe_part_t, cpe_name, part)
OSCAP_ACCESSOR_STRING(cpe_name, vendor)
OSCAP_ACCESSOR_STRING(cpe_name, product)
OSCAP_ACCESSOR_STRING(cpe_name, version)
OSCAP_ACCESSOR_STRING(cpe_name, update)
OSCAP_ACCESSOR_STRING(cpe_name, edition)
OSCAP_ACCESSOR_STRING(cpe_name, language)

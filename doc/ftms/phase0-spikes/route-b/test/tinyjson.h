/* tinyjson.h - minimal JSON DOM reader for test tools only (not for the game).
 * Numbers -> double, strings (basic escapes; \uXXXX -> '?'), objects, arrays,
 * true/false/null.  Objects are linear lists.  Leaks by design (test tools).
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TINYJSON_H
#define TINYJSON_H
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;
typedef struct jval jval;
struct jval
{
	jtype type;
	double num;          /* J_NUM, J_BOOL (0/1) */
	char *str;           /* J_STR value */
	char *key;           /* member name when child of an object */
	jval *kids;          /* J_ARR / J_OBJ children */
	size_t n;
};

static void j_ws(const char **p) { while (isspace((unsigned char)**p)) ++*p; }

static int j_string(const char **p, char **out)
{
	size_t cap = 16, n = 0;
	char *s;
	if (**p != '"') return -1;
	++*p;
	s = malloc(cap);
	while (**p && **p != '"')
	{
		char c = *(*p)++;
		if (c == '\\')
		{
			c = *(*p)++;
			switch (c)
			{
				case 'n': c = '\n'; break;
				case 't': c = '\t'; break;
				case 'r': c = '\r'; break;
				case 'b': c = '\b'; break;
				case 'f': c = '\f'; break;
				case 'u': if (strlen(*p) >= 4) *p += 4; c = '?'; break;
				default: break;
			}
		}
		if (n + 2 > cap) s = realloc(s, cap *= 2);
		s[n++] = c;
	}
	if (**p != '"') { free(s); return -1; }
	++*p;
	s[n] = 0;
	*out = s;
	return 0;
}

static int j_parse(const char **p, jval *v)
{
	memset(v, 0, sizeof *v);
	j_ws(p);
	if (**p == '"') { v->type = J_STR; return j_string(p, &v->str); }
	if (**p == '{' || **p == '[')
	{
		char close = **p == '{' ? '}' : ']';
		size_t cap = 4;
		v->type = close == '}' ? J_OBJ : J_ARR;
		v->kids = malloc(cap * sizeof *v->kids);
		++*p; j_ws(p);
		if (**p == close) { ++*p; return 0; }
		for (;;)
		{
			jval kid;
			char *key = NULL;
			j_ws(p);
			if (v->type == J_OBJ)
			{
				if (j_string(p, &key)) return -1;
				j_ws(p);
				if (*(*p)++ != ':') return -1;
			}
			if (j_parse(p, &kid)) return -1;
			kid.key = key;
			if (v->n == cap) v->kids = realloc(v->kids, (cap *= 2) * sizeof *v->kids);
			v->kids[v->n++] = kid;
			j_ws(p);
			if (**p == ',') { ++*p; continue; }
			if (**p == close) { ++*p; return 0; }
			return -1;
		}
	}
	if (strncmp(*p, "true", 4) == 0)  { v->type = J_BOOL; v->num = 1; *p += 4; return 0; }
	if (strncmp(*p, "false", 5) == 0) { v->type = J_BOOL; v->num = 0; *p += 5; return 0; }
	if (strncmp(*p, "null", 4) == 0)  { v->type = J_NULL; *p += 4; return 0; }
	{
		char *end;
		v->num = strtod(*p, &end);
		if (end == *p) return -1;
		v->type = J_NUM;
		*p = end;
		return 0;
	}
}

static const jval *j_get(const jval *obj, const char *key)
{
	size_t i;
	if (!obj || obj->type != J_OBJ) return NULL;
	for (i = 0; i < obj->n; ++i)
		if (obj->kids[i].key && strcmp(obj->kids[i].key, key) == 0) return &obj->kids[i];
	return NULL;
}
static const char *j_str(const jval *obj, const char *key)
{
	const jval *v = j_get(obj, key);
	return v && v->type == J_STR ? v->str : NULL;
}

/* hex string -> bytes; returns length or -1 */
static int hex_decode(const char *hex, unsigned char *out, size_t cap)
{
	size_t n = strlen(hex), i;
	if (n % 2 || n / 2 > cap) return -1;
	for (i = 0; i < n / 2; ++i)
	{
		unsigned v; char t[3] = { hex[2 * i], hex[2 * i + 1], 0 };
		char *e;
		v = (unsigned)strtoul(t, &e, 16);
		if (*e) return -1;
		out[i] = (unsigned char)v;
	}
	return (int)(n / 2);
}
#endif

/*
 * Copyright (c) 2023 Moritz Buhl <mbuhl@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <string.h>

#include "dict.h"
#include "index.h"

int
index_open(char *path, struct dc_index *idx)
{
	struct stat sb;
	int fd;

	if ((fd = open(path, O_RDONLY)) == -1)
		return -1;
	if (fstat(fd, &sb) == -1)
		return -1;
	idx->size = sb.st_size;

	idx->data = mmap(NULL, idx->size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (idx->data == MAP_FAILED)
		return -1;

	return 0;
}

int
index_validate(struct dc_index *idx, off_t db_size)
{
	off_t i;
	int b64off_max = 0, b64len_max = 0, tabs = 0, b64chars = -1;
	char c;

	for (; db_size; db_size >>= 6)
		b64off_max++;

	for (i = LOOKUP_MAX; i; i >>= 6)
		b64len_max++;

	for(i = 0; i < idx->size; i++) {
		c = idx->data[i];
		if (c == '\t') {
			if (tabs == 1 && b64chars > b64off_max)
				return -1;
			else if (tabs == 2 && b64chars > b64len_max)
				return -1;
			else if (b64chars == 0)
				return -1;
			tabs++;
			b64chars = 0;
			if (tabs > 2)
				return -1;
		} else if (c == '\n') {
			if (tabs != 2)
				return -1;
			tabs = 0;
		} else if (tabs) {
			if ((c != '+' && c < '/') || (c > '9' && c < 'A')
			    || (c > 'Z' && c < 'a') || (c > 'z')) {
				return -1;
			}
			b64chars++;
		}
	}
	if (idx->data[idx->size - 1] != '\n')
		return -1;
	return 0;
}

static size_t
index_parse_b64(const char *data, size_t *res)
{
	char c;
	int i, l = 0;
	*res = 0;

	while (data[l + 1] != '\t' && data[l + 1] != '\n') l++;

	for (i = 0; i < l; i++) {
		c = data[l - i];
		if (c == '+') {
			c = 62;
		} else if (c == '/') {
			c = 63;
		} else if (c >= '0' && c <= '9') {
			c += 4;
		} else if (c >= 'A' && c <= 'Z') {
			c -= 65;
		} else if (c >= 'a' && c <= 'z') {
			c -= 71;
		} else
			errx(1, "not base 64");

		*res += c << (i * 6);
	}

	return l + 1;
}

static struct dc_index_entry *
index_parse_line(const char *line, struct dc_index_entry *e)
{
	const char *data;
	size_t l = 0;

	e->match = line;

	while (line[l] != '\t') l++;
	if (l > WORD_MAX)
		l = WORD_MAX;
	e->match_len = l;

	data = line + l;
	data += index_parse_b64(data, &e->def_off);
	index_parse_b64(data, &e->def_len);

	if (e->def_len > LOOKUP_MAX)
		e->def_len =  LOOKUP_MAX;

	return e;
}

const char *
index_prev(const char *cur, const struct dc_index *idx)
{
	const char *base = idx->data;
	const char *p = cur;

	if (p == base)
		return NULL;

	p--;
	while (p > base && p[-1] != '\n') p--;

	return p;
}

const char *
index_next(const char *cur, const struct dc_index *idx)
{
	const char *end = idx->data + idx->size;
	const char *p = cur;

	while (p < end && p[0] != '\n') p++;
	p++;

	if (p >= end)
		return NULL;

	return p;
}

/*
 * key is NUL terminated
 * entry is HT terminated
 */
static int
index_exact_cmp(const char *key, const char *entry)
{
	size_t klen = strlen(key), elen = 0;
	int r;

	while (entry[elen] != '\t') elen++;
	r = strncmp(key, entry, MIN(klen, elen));

	if (r == 0 && klen < elen)
		return -1;
	else if (r == 0 && elen < klen)
		return 1;
	return r;
}

/*
 * key is NUL terminated
 * entry is HT terminated
 */
static int
index_prefix_cmp(const char *key, const char *entry)
{
	size_t klen = strlen(key), elen = 0;
	int r;

	while (entry[elen] != '\t') elen++;
	r = strncmp(key, entry, MIN(klen, elen));

	if (r == 0 && elen < klen)
		return 1;
	return r;
}

static void *
index_bsearch(const char *key, const struct dc_index *idx,
    int (*compar)(const char *, const char *))
{
	const char *base = idx->data;
	const char *end = idx->data + idx->size;
	const char *p, *op = NULL;
	size_t lim = idx->size / 2;
	int cmp;

	while (lim != 0) {
		p = base + lim;
		if (p > end)
			p = end;

		p = index_prev(p, idx);
		if (p == op)
			break;
		op = p;

		cmp = (*compar)(key, p);
		if (cmp == 0)
			return ((void *)p);
		if (cmp > 0) {	/* key > p: move right */
			base = index_next(p, idx);
			if (base == NULL)
				base = p;
			lim = (end - p) / 2;
		} else {	/* else move left */
			end = p;
			lim = (p - base) / 2;
		}
	}
	return (NULL);
}

static int
index_find(const char *req, const struct dc_index *idx,
    struct dc_index_list *list, int (*compar)(const char *, const char *))
{

	const char *p;
	struct dc_index_entry *e = SLIST_FIRST(list);
	int r = 0;

	if ((p = index_bsearch(req, idx, compar)) == NULL)
		return r;
	while (p && compar(req, p) == 0) {
		p = index_prev(p, idx);
	}

	if (p == NULL)
		p = idx->data;
	else
		p = index_next(p, idx);

	while (p && compar(req, p) == 0) {
		e = SLIST_NEXT(index_parse_line(p, e), entries);
		r++;
		if (e == NULL)
			return r;
		p = index_next(p, idx);
	}

	return r;
}

int
index_prefix_find(const char *req, const struct dc_index *idx,
    struct dc_index_list *list)
{
	return index_find(req, idx, list, index_prefix_cmp);
}

int
index_exact_find(const char *req, const struct dc_index *idx,
    struct dc_index_list *list)
{
	return index_find(req, idx, list, index_exact_cmp);
}

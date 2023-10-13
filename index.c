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
#include <limits.h>
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
index_validate(struct dc_index *idx, off_t size)
{
	off_t i;
	int maxb64len = 0, tabs = 0, b64len = -1;
	char c;

	for (; size; size >>= 6)
		maxb64len++;

	for(i = 0; i < idx->size; i++) {
		c = idx->data[i];
		if (c == '\t') {
			if (b64len > maxb64len)
				return -1;
			else if (b64len == 0)
				return -1;
			tabs++;
			b64len = 0;
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
			b64len++;
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
	if (l > INT_MAX)
		l = INT_MAX;
	e->match_len = l;

	data = line + l;
	data += index_parse_b64(data, &e->def_off);
	index_parse_b64(data, &e->def_len);

	return e;
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
	r = strncmp(key, entry, MINIMUM(klen, elen));

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
	r = strncmp(key, entry, MINIMUM(klen, elen));

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
	const char *p;
	size_t lim;
	int cmp;

	for (lim = idx->size; lim != 0; lim >>= 1) {
		p = base + (lim >> 1);
		while (p < end && p[0] != '\n') p++;
		p++;
		cmp = (*compar)(key, p);
		if (cmp == 0)
			return ((void *)p);
		if (cmp > 0) {  /* key > p: move right */
			base = (char *)p;
			lim--;
		} /* else move left */
	}
	return (NULL);
}

static int
index_find(const char *req, const struct dc_index *idx,
    struct dc_index_list *lst, int (*compar)(const char *, const char *))
{

	const char *base = idx->data;
	const char *end = idx->data + idx->size;
	struct dc_index_entry *e = SLIST_FIRST(lst);
	const char *p;
	int r = 0;

	if ((p = index_bsearch(req, idx, compar)) == NULL)
		return -1;
	do {
		p--;
		while (p > base && p[-1] != '\n') p--;
	} while (compar(req, p) == 0 && p > base);

	while (p < end && p[0] != '\n') p++;
	p++;
	while (compar(req, p) == 0) {
		e = SLIST_NEXT(index_parse_line(p, e), entries);
		r++;
		if (e == NULL)
			return r;
		while (p < end && p[0] != '\n') p++;
		p++;
		if (p == end)
			break;
	}

	return r;
}

int
index_prefix_find(const char *req, const struct dc_index *idx,
    struct dc_index_list *lst)
{
	return index_find(req, idx, lst, index_prefix_cmp);
}

int
index_exact_find(const char *req, const struct dc_index *idx,
    struct dc_index_list *lst)
{
	return index_find(req, idx, lst, index_exact_cmp);
}

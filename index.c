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

#include "dictd.h"
#include "index.h"

int
index_open(char *path, struct dc_index *idx)
{
	struct stat sb;
	off_t i;
	int fd, tabs = 0, b64len = 0;
	char c;

	if ((fd = open(path, O_RDONLY)) == -1)
		err(1, "open");
	if (fstat(fd, &sb) == -1)
		err(1, "fstat");
	idx->size = sb.st_size;

	idx->data = mmap(NULL, idx->size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (idx->data == MAP_FAILED)
		err(1, "mmap");

	for(i = 0; i < idx->size; i++) {
		c = idx->data[i];
		if (c == '\t') {
			if (b64len > 8)
				errx(1, "index offsets are too big");
			tabs++;
			b64len = 0;
			if (tabs > 2)
				errx(1, "index has too many tabs");
		} else if (c == '\n') {
			if (tabs != 2)
				errx(1, "index is missing tabs");
			tabs = 0;
		} else if (tabs) {
			if ((c != '+' && c < '/') || (c > '9' && c < 'A')
			    || (c > 'Z' && c < 'a') || (c > 'z')) {
				errx(1, "index offsets are not base64");
			}
			b64len++;
		}
	}
	if (idx->data[idx->size - 1] != '\n')
		errx(1, "index does not end with newline");

	return 0;
}

static const u_int8_t Base64Code[] =
"+/ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

static const u_int8_t index_64[128] = {
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 62, 255, 255, 255, 63, 52, 53,
        54, 55, 56, 57, 58, 59, 60, 61, 255, 255,
        255, 255, 255, 255, 255, 0, 1, 2, 3, 4,
        5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
	25, 255, 255, 255, 255, 255, 255, 26, 27, 28,
        29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
	39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
	49, 50, 51, 255, 255, 255, 255, 255
};

#define CHAR64(c)  ( (c) > 127 ? 255 : index_64[(c)])

/*
 * read buflen (after decoding) bytes of data from b64data
 */
static int
decode_base64(u_int8_t *buffer, size_t len, const char *b64data)
{
	u_int8_t *bp = buffer;
	const u_int8_t *p = b64data;
	u_int8_t c1, c2, c3, c4;

	while (bp < buffer + len) {
		c1 = CHAR64(*p);
		/* Invalid data */
		if (c1 == 255)
			return -1;

		c2 = CHAR64(*(p + 1));
		if (c2 == 255)
			return -1;

		*bp++ = (c1 << 2) | ((c2 & 0x30) >> 4);
		if (bp >= buffer + len)
			break;

		c3 = CHAR64(*(p + 2));
		if (c3 == 255)
			return -1;

		*bp++ = ((c2 & 0x0f) << 4) | ((c3 & 0x3c) >> 2);
		if (bp >= buffer + len)
			break;

		c4 = CHAR64(*(p + 3));
		if (c4 == 255)
			return -1;
		*bp++ = ((c3 & 0x03) << 6) | c4;

		p += 4;
	}
	return 0;
}


static size_t
index_parse_b64(const char *data, size_t *dest)
{
	char inbuf[9];
	uint64_t outbuf = 0;
	size_t l = 0;
	int i, pad;

	while (data[l] != '\t' && data[l] != '\n') l++;
	assert(l <= 8);

	pad = 8 - l;
	for (i = 0; i < pad; i++)
		inbuf[i] = 'A';
	inbuf[8] = '\0';
	bcopy(data, inbuf + pad, l);

	decode_base64(((unsigned char *)&outbuf) + 2, 6, inbuf);
	*dest = be64toh(outbuf);

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

	data = line + l + 1;
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
	size_t lim = idx->size;
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
	const char *p, *n;
	int r = 0;

	if ((p = index_bsearch(req, idx, compar)) == NULL)
		return -1;
	do {
		p--;
		while (p > base && p[-1] != '\n') p--;
	} while (compar(req, p) == 0);

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

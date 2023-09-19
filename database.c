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

#include <stdio.h>

#include "dictd.h"
#include "database.h"
#include "compress.h"

int
database_open(char *path, struct dc_database *db)
{
	if ((db->data = gz_ropen(path)) == NULL)
		return 1;

	return 0;
}

int
database_lookup(struct dc_index_entry *req, struct dc_database *db, char *out)
{
	int r;

	if ((r = gz_read(db->data, req->def_off, out, req->def_len)) == -1)
		return -1;

	if ((size_t)r != req->def_len) {
		printf("SHORT READ!");
		return -1;
	}

	return r;
}

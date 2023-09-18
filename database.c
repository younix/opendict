#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <zlib.h>

#include "dictd.h"
#include "compress.h"
#include "gzopen.h"
static int
database_open(char *path, struct dc_database *db)
{
	if ((db->data = gz_ropen(path)) == NULL)
		return 1;

	return 0;
}

static int
database_lookup(struct dc_index_entry *req, struct dc_database *db, char *out)
{
	char buf[65535];
	size_t chunk, off;

	gz_stream *s = db->data;
	chunk = req->def_off / s->ra_clen;
	off = req->def_off % s->ra_clen;

	s->z_stream.next_in = 0;
	s->z_stream.avail_in = 0;
	s->z_stream.next_out = Z_NULL;
	s->z_stream.avail_out = 0;

	s->z_stream.next_in = s->z_buf + s->z_hlen + s->ra_offset[chunk];
	s->z_stream.avail_in = s->ra_chunks[chunk];

	gz_read(s, buf, s->ra_chunks[chunk]); /* set Z_PARTIAL_FLUSH? */

	// shouldnt we just read req->def_len and multiple chunks?
	return 0;
}

#include "index.h"

int
main(void)
{
	struct dc_database mydb;
	struct dc_index_list list;
	struct dc_index myidx;
	struct dc_index_entry myr[100], *my;
	char *str;
	char ans[256] = {0};
	int i, r;

	SLIST_INIT(&list);
	memset(myr, 0, sizeof(struct dc_index_entry) * 100);
	for (i = 0; i < 100; i++)
		SLIST_INSERT_HEAD(&list, &myr[i], entries);

	r = database_open("/home/mbuhl/Downloads/eng-deu/eng-deu.dict.dz", &mydb);
	printf("open: %d\n", r);

	r = index_open("/home/mbuhl/Downloads/eng-deu/eng-deu.index", &myidx);
	printf("open: %d\n", r);
	r = index_prefix_find("shit", &myidx, &list);
	SLIST_FOREACH(my, &list, entries) {
		if (my->match == NULL)
			break;
		strncpy(ans, my->match, my->match_len);
		ans[my->match_len] = '\0';
		printf("index_prefix_find: %s, def_off=%lu, def_len=%lu\n", ans, my->def_off, my->def_len);

		database_lookup(my, &mydb, ans);
	}
}

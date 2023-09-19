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
	char buf[65535] = { 0 };

	int r = gz_read(db->data, req->def_off, buf, req->def_len); /* set Z_PARTIAL_FLUSH? */
	for (size_t i = 0; i < req->def_len; i++)
		printf("%c", buf[i]);
	printf("\nr = %d\n", r);
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
	r = index_prefix_find("00database", &myidx, &list);
	SLIST_FOREACH(my, &list, entries) {
		if (my->match == NULL)
			break;
		strncpy(ans, my->match, my->match_len);
		ans[my->match_len] = '\0';
		printf("index_prefix_find: %s, def_off=%lu, def_len=%lu\n", ans, my->def_off, my->def_len);

		database_lookup(my, &mydb, ans);
	}
}

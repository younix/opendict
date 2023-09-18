NOMAN= yes

#DEBUG=	-g -DDEBUG=3 -O0
CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith
CFLAGS+= -Wsign-compare

PROG = dict
SRCS = database.c gzopen.c index.c
LDADD+=	-lz
DPADD+= ${LIBZ}

.include <bsd.prog.mk>

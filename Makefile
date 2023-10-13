NOMAN= yes

#DEBUG=	-g -DDEBUG=3 -O0
CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith
CFLAGS+= -Wsign-compare

PROG = dict
SRCS = main.c index.c database.c
LDADD+=	-lz
DPADD+= ${LIBZ}

.include <bsd.prog.mk>

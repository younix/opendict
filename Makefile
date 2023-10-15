NOMAN= yes

#DEBUG=	-g -DDEBUG=3 -O0
CFLAGS+= -Wall -I${.CURDIR}

PROG = dict
SRCS = main.c index.c database.c
LDADD+=	-lz
DPADD+= ${LIBZ}

.include <bsd.prog.mk>

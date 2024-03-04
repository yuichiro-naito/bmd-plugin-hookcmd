MK_DEBUG_FILES=	no
SHLIB=		bmdplugin_hookcmd
SHLIB_MAJOR=	1
CFLAGS+=	-I${LOCALBASE}/include -DLOCALBASE=\"${LOCALBASE}\"
LIBDIR=		$(LOCALBASE)/libexec/bmd
SRCS=		hookcmd.c
MAN=		bmd-plugin-hookcmd.8
MANDIR=		$(LOCALBASE)/man/man

WARNS?=		6

.include "Makefile.inc"
.include <bsd.lib.mk>

LOCALBASE?= 	/usr/local
MK_DEBUG_FILES=	no
LIB=		bmdplugin_hookcmd
SHLIB_MAJOR=	1
CFLAGS+=	-I${LOCALBASE}/include -DLOCALBASE=\"${LOCALBASE}\"
LIBDIR=		$(LOCALBASE)/libexec/bmd
SRCS=		hookcmd.c
MAN=		bmd-plugin-hookcmd.8
MANDIR=		$(LOCALBASE)/man/man

.include <bsd.lib.mk>

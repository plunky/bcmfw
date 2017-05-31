#

BCMFW_DIR?=		/libdata/bcmfw

PROGS=			bcmfw bcmfw-install

SRCS.bcmfw=		bcmfw.c btdev.c ugen.c ihex.c
MAN.bcmfw=		bcmfw.8

SRCS.bcmfw-install=	bcmfw-install.c
MAN.bcmfw-install=


DPADD.bcmfw+=		${LIBBLUETOOTH}
LDADD.bcmfw+=		-lbluetooth

DPADD+=			${LIBUTIL}
LDADD+=			-lutil

CPPFLAGS+=		-DBCMFW_DIR=\"${BCMFW_DIR}\"

.include <bsd.prog.mk>

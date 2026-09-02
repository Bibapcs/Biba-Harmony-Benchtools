
/* OHOS changes: new stub file. */

/*
 * In the original icclib these icmSn_* helpers are static functions in
 * icc.c, and icc_util.c (#included into icc.c) calls them as if they
 * were in the same translation unit. This port compiles icc_util.c
 * standalone, so these helpers are provided here as no-ops: they are
 * only reachable from the ICC tag serialization code paths, which the
 * i1Pro3 instrument subset never exercises.
 */

/*
 * International Color Consortium Format Library (icclib)
 * Copyright 1997 - 2022 Graeme W. Gill
 * MIT license - see License4.txt in the icc directory.
 */

#include "icc.h"

/* Relative seek */
void icmSn_rseek(icmFBuf *b, INR32 size) {
	(void)b;	/* Stub: not used by the i1Pro3 subset */
	(void)size;
}

/* Unsigned char <-> unsigned int 8 bit */
void icmSn_uc_UInt8(icmFBuf *b, unsigned char *p) {
	(void)b;	/* Stub: not used by the i1Pro3 subset */
	(void)p;
}

/* Unsigned short <-> unsigned int 8 bit */
void icmSn_us_UInt8(icmFBuf *b, unsigned short *p) {
	(void)b;	/* Stub: not used by the i1Pro3 subset */
	(void)p;
}

/* Unsigned int <-> unsigned int 8 bit */
void icmSn_ui_UInt8(icmFBuf *b, unsigned int *p) {
	(void)b;	/* Stub: not used by the i1Pro3 subset */
	(void)p;
}

/* Unsigned short <-> unsigned int 16 bit */
void icmSn_us_UInt16(icmFBuf *b, unsigned short *p) {
	(void)b;	/* Stub: not used by the i1Pro3 subset */
	(void)p;
}

/* Unsigned int <-> unsigned int 16 bit */
void icmSn_ui_UInt16(icmFBuf *b, unsigned int *p) {
	(void)b;	/* Stub: not used by the i1Pro3 subset */
	(void)p;
}


/* Argyll i1Pro3 enumeration helper for HarmonyOS (OHOS). */

/* 
 * Argyll Color Management System
 *
 * Copyright 2026 Argyll port
 *
 * This material is licenced under the GNU GENERAL PUBLIC LICENSE Version 2 or later :-
 * see the License2.txt file for licencing details.
 */

/* OHOS changes: new file - a small exported entry point that exercises */
/* the Argyll instrument enumeration code (icoms + usbio_ohos). It only */
/* has effect at runtime on a device with the USB DDK permission; for the */
/* static library build only compiling and linking matter. */

#include <stdio.h>
#include "aconfig.h"
#include "numsup.h"
#include "cgats.h"
#include "xspect.h"
#include "conv.h"
#include "insttypes.h"
#include "icoms.h"
#include "inst.h"

/* Enumerate the USB instruments visible to the OHOS USB DDK and return */
/* the number of X-Rite i1Pro3 devices found (negative on fatal error). */
int argyll_i1_enumerate(void) {
	icompaths *pp;
	int i, found = 0;

	if ((pp = new_icompaths(NULL)) == NULL)
		return -1;

	for (i = 0; i < pp->ndpaths[dtix_inst]; i++) {
		icompath *path = pp->paths[i];
		if (path->dtype == instI1Pro3) {
			found++;
			printf("argyll_i1_enumerate: found i1Pro3 at '%s'\n", path->name);
		} else {
			printf("argyll_i1_enumerate: ignoring device '%s' (type %d)\n",
				path->name != NULL ? path->name : "?", (int)path->dtype);
		}
	}
	printf("argyll_i1_enumerate: %d i1Pro3 device(s) found\n", found);

	pp->del(pp);
	return found;
}

// Progress PNG export for the OpenHarmony (OHOS) pbrt port.
//
// If the environment variable PBRT_PROGRESS_PNG is set to a file path,
// StartProgressPNGExport() spawns a background thread that writes the
// current contents of the film to that path (as PNG if the path ends in
// .png) roughly every 2 seconds while rendering is in progress.
// StopProgressPNGExport() stops the thread; it must be called after
// rendering finishes, before the Film is destroyed.

#ifndef PBRT_OHOS_PROGRESS_PNG_H
#define PBRT_OHOS_PROGRESS_PNG_H

#include <pbrt/base/film.h>

namespace pbrt {

void StartProgressPNGExport(Film film);
void StopProgressPNGExport();

}  // namespace pbrt

#endif  // PBRT_OHOS_PROGRESS_PNG_H

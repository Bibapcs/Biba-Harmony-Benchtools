// Progress PNG export for the OpenHarmony (OHOS) pbrt port.
// See progress_png.h for details.

#include <progress_png.h>

#include <pbrt/util/image.h>
#include <pbrt/util/log.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace pbrt {

namespace {

std::atomic<bool> progressPNGStop{false};
std::thread progressPNGThread;

}  // namespace

void StartProgressPNGExport(Film film) {
    const char *path = std::getenv("PBRT_PROGRESS_PNG");
    if (!path || !*path || !film)
        return;

    std::string filename(path);
    progressPNGStop.store(false);

    progressPNGThread = std::thread([film, filename]() mutable {
        LOG_VERBOSE("PBRT_PROGRESS_PNG: writing progress images to %s", filename);
        while (!progressPNGStop.load(std::memory_order_relaxed)) {
            // Sleep ~2 seconds, in small increments so that we stop promptly
            // once rendering is done.
            for (int i = 0; i < 20; ++i) {
                if (progressPNGStop.load(std::memory_order_relaxed))
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Note: the film is being updated concurrently by the rendering
            // threads; Film::GetImage() does not synchronize with them, so a
            // progress image may be slightly torn, which is acceptable for a
            // progress preview. GetImage() normalizes pixel values by the
            // accumulated filter weight sum, so the image is correctly
            // exposed even partway through rendering.
            ImageMetadata metadata;
            Image image = film.GetImage(&metadata);
            if (!image.Write(filename, metadata))
                LOG_VERBOSE("PBRT_PROGRESS_PNG: unable to write %s", filename);
        }
    });
}

void StopProgressPNGExport() {
    progressPNGStop.store(true);
    if (progressPNGThread.joinable())
        progressPNGThread.join();
}

}  // namespace pbrt

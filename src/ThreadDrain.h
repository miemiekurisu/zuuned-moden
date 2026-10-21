#pragma once

#include <QThread>
#include <cstdio>

namespace Zuuned {

// A worker thread runs one job at a time, and the call it is inside - libzune,
// FFprobe, SQLite - cannot be interrupted part way through. Qt makes
// destroying a running QThread fatal, so a shutdown that runs out of patience
// waits in the open and then stops the thread itself instead of taking the
// whole process down with qFatal on its way out.
inline void drainThread(QThread &thread, const char *label,
                        int slices = 5, int sliceMs = 5000)
{
    thread.quit();
    for (int slice = 1; slice <= slices; ++slice) {
        if (thread.wait(sliceMs))
            return;
        std::fprintf(stderr, "[%s] worker still busy at shutdown, waited %ds\n",
                     label, slice * sliceMs / 1000);
    }
    std::fprintf(stderr, "[%s] worker would not stop, terminating\n", label);
    thread.terminate();
    thread.wait(2000);
}

} // namespace Zuuned

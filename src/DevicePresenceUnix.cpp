// Linux/macOS: libusb sees a Zune as soon as the kernel enumerates it, and udev
// access is a permissions question rather than a driver question - both already
// answered by UsbWatcher and UdevSetup.  Declining here keeps the Linux line
// exactly as it was rather than adding a second, weaker presence signal.

#include "DevicePresence.h"

namespace Zuuned {

ZuneSighting scanForZune() {
    return ZuneSighting{};
}

} // namespace Zuuned

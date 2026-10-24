// Platform-neutral half of the presence layer: the USB id set and the wording.
// The scan itself is per-platform (DevicePresenceWin / DevicePresenceUnix).

#include "DevicePresence.h"
#include "UiText.h"

namespace Zuuned {

bool isZuneUsbId(quint16 vid, quint16 pid) {
    return vid == 0x045e
           && (pid == 0x0710 || pid == 0x063e || (pid >= 0x0711 && pid <= 0x0714));
}

QString describeSighting(const ZuneSighting &s, bool libusbSighted) {
    if (!s.supported || !s.present)
        return QString();
    const QString ids = QStringLiteral("pid=0x%1")
                            .arg(s.pid, 4, 16, QLatin1Char('0'));
    if (libusbSighted)
        return UiText::tr("Zune %1 is on the bus and libusb can see it (%2)")
            .arg(ids, s.instanceId);
    if (!s.usable)
        return UiText::tr("Zune %1 is on the bus, invisible to libusb, and Windows has no driver for it (problem %2: %3) - %4")
            .arg(ids)
            .arg(s.problem)
            .arg(s.problemText, s.instanceId);
    return UiText::tr("Zune %1 is on the bus, invisible to libusb, and Windows says a driver owns it - so what owns it is not one zuuned can use (%2)")
            .arg(ids, s.instanceId);
}

} // namespace Zuuned

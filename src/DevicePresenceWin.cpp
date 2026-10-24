// Windows answers "is a Zune plugged in" through the devnode, because libusb
// cannot: with no driver bound there is no device interface for libusb to find,
// so the device is invisible to libusb_get_device_list() while the PnP manager
// still holds it with a problem code.  Verified on the machine this port runs on
// with a Zune HD attached: CM problem 28 ("no driver installed"),
// DN_HAS_PROBLEM set, and no libusb entry for it at all.
//
// Two traps this walks around.  CM_Get_Device_ID_List() also returns records for
// hardware that is not attached any more - a Zune 30 that was plugged in weeks
// ago answers CM_Locate_DevNode(CM_LOCATE_DEVNODE_NORMAL) with
// CR_NO_SUCH_DEVNODE - so a record is not a presence and gets located before it
// counts.  And the ids come back in whatever case the enumerator wrote them, so
// the vid/pid match is case-insensitive.

#include "DevicePresence.h"

#include <windows.h>
#include <cfgmgr32.h>

#include <cwchar>
#include <vector>
#include "UiText.h"

namespace {

QString problemTextFor(ULONG problem) {
    switch (problem) {
    case 0:  return UiText::tr("no problem");
    case 1:  return UiText::tr("the device is disabled");
    case 10: return UiText::tr("Windows cannot start the hardware");
    case 14: return UiText::tr("a reboot is required");
    case 18: return UiText::tr("the drivers need to be reinstalled");
    case 21: return UiText::tr("Windows is reinstalling the device");
    case 22: return UiText::tr("the device is disabled or gone");
    case 28: return UiText::tr("no driver is installed");
    case 31: return UiText::tr("the device is not working properly");
    case 37: return UiText::tr("a filter driver failed to load");
    case 38: return UiText::tr("the driver failed to load");
    case 39: return UiText::tr("the driver is corrupt or missing");
    case 45: return UiText::tr("it is not currently connected");
    case 48: return UiText::tr("the software for it cannot load");
    default: return QStringLiteral("problem code %1").arg(problem);
    }
}

// A devnode that Windows is keeping a record for, but which is not on the bus
// now, must not read as "plugged in".
bool countsAsAttached(ULONG problem) {
    return problem != 22 /* CM_PROB_PHANTOM */
           && problem != 45 /* CM_PROB_NOT_CONNECTED */;
}

bool parseIds(const QString &instanceId, quint16 &vid, quint16 &pid) {
    const QString upper = instanceId.toUpper();
    const int v = upper.indexOf(QStringLiteral("VID_"));
    const int p = upper.indexOf(QStringLiteral("PID_"));
    if (v < 0 || p < 0)
        return false;
    bool vidOk = false;
    bool pidOk = false;
    const quint64 vFound = upper.mid(v + 4, 4).toULongLong(&vidOk, 16);
    const quint64 pFound = upper.mid(p + 4, 4).toULongLong(&pidOk, 16);
    if (!vidOk || !pidOk)
        return false;
    vid = static_cast<quint16>(vFound);
    pid = static_cast<quint16>(pFound);
    return true;
}

} // namespace

namespace Zuuned {

ZuneSighting scanForZune() {
    ZuneSighting best;
    best.supported = true;

    ULONG needed = 0;
    if (CM_Get_Device_ID_List_SizeW(&needed, L"USB", 0) != CR_SUCCESS || needed < 2)
        return best;

    std::vector<WCHAR> buffer(needed);
    if (CM_Get_Device_ID_ListW(L"USB", buffer.data(), needed, 0) != CR_SUCCESS)
        return best;

    for (const WCHAR *cursor = buffer.data(); *cursor;) {
        const QString instanceId = QString::fromWCharArray(cursor);
        cursor += std::wcslen(cursor) + 1;

        quint16 vid = 0;
        quint16 pid = 0;
        if (!parseIds(instanceId, vid, pid) || !isZuneUsbId(vid, pid))
            continue;

        DEVINST devNode = 0;
        if (CM_Locate_DevNodeW(&devNode,
                               const_cast<PWSTR>(
                                   reinterpret_cast<const WCHAR *>(instanceId.utf16())),
                               CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            continue; // a record, not a device on the bus

        ULONG status = 0;
        ULONG problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, devNode, 0) != CR_SUCCESS)
            continue;
        if (!countsAsAttached(problem))
            continue;

        ZuneSighting sighting;
        sighting.supported = true;
        sighting.present = true;
        sighting.usable = problem == 0;
        sighting.vid = vid;
        sighting.pid = pid;
        sighting.instanceId = instanceId;
        sighting.problem = problem;
        sighting.problemText = problemTextFor(problem);
        (void)status;

        // Prefer a device Windows can actually use, then the most informative
        // blocked one, so a machine with an old recovery-mode node still reports
        // the Zune that matters.
        if (!best.present || (!best.usable && sighting.usable))
            best = sighting;
    }

    if (!best.present)
        best.problemText.clear();
    return best;
}

} // namespace Zuuned

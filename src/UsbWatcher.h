#pragma once

#include <QString>
#include <QThread>
#include <atomic>

// Hotplug detection — the USBWatcher.swift of this app.
//
// Runs a dedicated libusb event loop (own context, independent of the one
// libzune opens per-connection) and emits when a Zune arrives or leaves.
// LIBUSB_HOTPLUG_ENUMERATE means an already-plugged device fires
// zuneArrived() immediately at startup, so launch-while-connected and
// plug-in-after-launch follow the same path.
// Where the libusb build has no OS hotplug notifications -- Windows, whose
// backend enumerates on demand -- the same two signals come from polling the
// device list for presence instead, after a confirmed absence twice over.
//
// Polling a device list answers exactly one question, and on Windows it is the
// wrong one: a Zune with no driver bound has no device interface for libusb to
// enumerate, so it is missing from the list altogether rather than listed and
// refused.  zuneBlocked()/zuneUnblocked() carry the operating system's answer for
// that case (see DevicePresence.h), so the app can say "plugged in and blocked,
// here is the fix" instead of waiting in silence for a device it will never see.
//
// Signals are emitted from this thread; connect with the default queued
// connection and handle on the UI thread.
class UsbWatcher : public QThread {
    Q_OBJECT

public:
    explicit UsbWatcher(QObject *parent = nullptr);
    ~UsbWatcher() override;

    void stop();

signals:
    void zuneArrived();
    void zuneLeft();
    // In practice Windows only: Windows can see a Zune that libusb cannot, and
    // states why.  `detail` is that explanation, ready to show as-is.
    void zuneBlocked(const QString &detail);
    void zuneUnblocked();

protected:
    void run() override;

private:
    friend struct UsbWatcherThunk;
    std::atomic<bool> m_stop{false};
};

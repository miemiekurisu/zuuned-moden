# Windows Zune quirks - what WinUSB answers and what it does not

> Measured 2026-10-21 on the Zuuned Windows development machine against a Zune HD
> (Pavo, `USB\VID_045E&PID_063E`) bound to WinUSB by `packaging/windows/`.  Every
> return value below is observed, not inferred from the Win32 documentation, and the
> device was the only variable: nothing in libzune changed between the lines.

## The state: present, healthy, deaf

The player enumerated, `devicetool` answered `VERDICT=agree`, the devnode reported
`CM_PROB_NONE`, and every bulk transfer timed out, so `zune_breach()` never opened a
PTP session.  This is the same symptom Linux documents for the Keel OpenSession wedge
in `AGENTS.md`, and the same recovery does not exist here.

Two probes, one per question.  Probe one asked whether the pipes could be coaxed back;
probe two asked whether the control plane was alive and whether a configuration cycle
or a reset could be made to do the job.

| Call | Return | Measured effect |
| --- | --- | --- |
| `libusb_get_device_descriptor` | 0 | 18 bytes, correct VID/PID |
| `libusb_get_configuration` | 0 | library-side read of the cached descriptor |
| control `GET_CONFIGURATION` | -9 `LIBUSB_ERROR_PIPE` | request unanswered |
| `libusb_get_kernel_driver_active` | -12 `LIBUSB_ERROR_NOT_SUPPORTED` | no kernel driver here |
| `libusb_claim_interface(0)` | 0 | granted, before and after every recovery below |
| `libusb_clear_halt(IN)`, `(OUT)` | 0, 0 | bulk OUT still timed out |
| `libusb_set_interface_alt_setting(0, 0)` | 0 | bulk OUT still timed out |
| `libusb_set_configuration(1)` | 0, six times | bulk OUT still timed out |
| `libusb_set_configuration(0)` | -12 `LIBUSB_ERROR_NOT_SUPPORTED` | WinUSB owns the configuration |
| `libusb_reset_device()` | 0 | same bus/address after it, bulk OUT still timed out |
| `libusb_bulk_transfer(OUT)` | -7 `LIBUSB_ERROR_TIMEOUT` | no byte consumed, every try |

Three further rounds of `clear_halt()` plus a reset, then OpenSession: `-7`.  Both
probes ended `VERDICT=still-dead`.  The bus address was 2/40 before the reset and 2/40
after it, so nothing in the stack observed a reset either, which is the tell: a real
bus reset re-enumerates.

## What this means for the wedge ladder

- `libusb_reset_device()` on WinUSB **returns success and is not a recovery**.  The
  earlier note in `packaging/windows/DRIVER_INSTALL.md` said it always fails.  It does
  not fail, which is worse: a success code from a recovery that did nothing points the
  next reader at libzune instead of at the power button.
- `zune_unjam()` is not a rung.  `clear_halt()` answers success here and revives
  nothing, and a rung in this ladder costs a second PTP command timeout - 10 s of
  `PTP_USB_TIMEOUT_MS` on a doomed `OpenSession` - so a measured-useless rung is not
  worth the latency it would add to every failed connect.
- Retries are not a rung either.  Every combination above was tried repeatedly in one
  session and the answer never changed.

So `zune_breach()` keeps exactly what it had - reset, reopen, retry once, because on
Linux that retry *is* the fix and the retry is also the only honest liveness probe -
and adds the two things this measurement says are missing: it prints the reset's own
return value, and its failure text names the power cycle instead of implying that a
reset happened.

```
[libzune] OpenSession dead - resetting device and retrying
[libzune] device reset returned 0 (reported success)
Failed to open PTP session: the device still answers USB enumeration but not the bulk
pipes, and a device reset reported success and changed nothing.  Power-cycle the Zune -
unplug and replug it, or hold its power button - then connect again.
```

## What is still open

This is one wedged Pavo on one Windows machine.  It does not say WinUSB can never
reset, it does not say what wedged the player, and it is no evidence about any other
model: on Linux the same wedge is a `usbreset 045e:0710` away and the retry after it
has always worked.  Nothing here was written to or read back from the device - a
power cycle is required before the round trip can be re-run at all.

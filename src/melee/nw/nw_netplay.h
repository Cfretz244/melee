#ifndef GALE01_NW_NETPLAY_H
#define GALE01_NW_NETPLAY_H

#include <platform.h>

#include <baselib/controller.h>

/// Engine-level lockstep netplay over a paravirtual EXI device (slot B).
///
/// Both peers run this DOL in lockstep from boot: nw_Init() handshakes with
/// the host-side device (EXI_DeviceMeleeNetplay in the Dolphin fork) to obtain
/// the shared RNG seed, input delay, and port assignment; thereafter
/// nw_ExchangeMaster() -- called from the frame loop right after
/// lb_800198E0()'s master-status renew -- replaces HSD_PadMasterStatus[] with
/// the agreed frame-indexed entries for the current tick. Exchanging the full
/// post-transform HSD_PadStatus keeps each port's derived state (trigger/
/// release/repeat, normalized floats) consistent: the port owner computes it
/// over its own real input stream, which IS the agreed stream for that port.
///
/// If no device is present (retail hardware, or netplay not configured), the
/// module stays dormant and the DOL plays normally offline.
///
/// NOTE: hooks live in gm/ (gmmain.c, gm_1A45.c) ONLY. Do not hook sysdolphin
/// TUs: growing controller.o splits HSD's text layout and wedges the HSD
/// synth/DevCom ARAM machinery at boot (bisected 2026-07-09).
///
/// Protocol spec: aot-dolphin-helper/research/melee-netplay-design.md.

/// Probe the EXI device and perform the session handshake. Call once during
/// boot, after PADInit()/CARDInit(). Blocks (device-side) until the peer
/// connects or the session times out.
void nw_Init(void);

/// True once nw_Init() established a session. Never becomes true afterwards.
bool nw_IsActive(void);

/// The host-chosen shared RNG seed. Valid only while nw_IsActive().
u32 nw_GetSeed(void);

/// Exchange this tick's inputs: schedule the local HSD_PadMasterStatus
/// snapshot for tick+delay, block until the agreed entries for the current
/// tick arrive (lockstep stall), and overwrite HSD_PadMasterStatus[] with
/// them. No-op when inactive; drops to inactive on device failure.
///
/// Rollback (protocol v3): the device may answer the readiness poll with a
/// REPLAY directive after it restored memory K ticks back. This function
/// then re-runs K logic ticks itself -- injecting the recorded agreed inputs
/// and invoking the tick runner for each -- before completing the current
/// tick normally. The caller notices nothing.
void nw_ExchangeMaster(void);

/// The gm scene loop registers its factored tick body here so replays can
/// re-run engine ticks without layering nw into gm's internals.
typedef void (*nw_TickRunner)(void);
void nw_SetTickRunner(nw_TickRunner runner);

/// True while nw_ExchangeMaster() is re-running restored ticks. The frame
/// loop uses this to gate side effects that must not re-fire during replay
/// (audio submission).
bool nw_IsReplaying(void);

#endif

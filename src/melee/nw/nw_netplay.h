#ifndef GALE01_NW_NETPLAY_H
#define GALE01_NW_NETPLAY_H

#include <platform.h>

#include <dolphin/pad.h>

/// Engine-level lockstep netplay over a paravirtual EXI device (slot B).
///
/// Both peers run this DOL in lockstep from boot: nw_Init() handshakes with
/// the host-side device (EXI_DeviceMeleeNetplay in the Dolphin fork) to obtain
/// the shared RNG seed, input delay, and port assignment; thereafter
/// nw_ExchangePads() replaces every dequeued raw PADStatus[4] with the agreed
/// frame-indexed inputs for the current tick. If no device is present (retail
/// hardware, or netplay not configured), the module stays dormant and the DOL
/// plays normally offline.
///
/// Protocol spec: aot-dolphin-helper/research/melee-netplay-design.md.

/// Probe the EXI device and perform the session handshake. Call once during
/// boot, after PADInit()/CARDInit() and before the first pad renew. Blocks
/// (device-side) until the peer connects or the session times out.
void nw_Init(void);

/// True once nw_Init() established a session. Never becomes true afterwards.
bool nw_IsActive(void);

/// The host-chosen shared RNG seed. Valid only while nw_IsActive().
u32 nw_GetSeed(void);

/// Replace @p pads (the 4-port raw PADStatus block just dequeued by
/// HSD_PadRenewMasterStatus) with the agreed inputs for the current netplay
/// tick, scheduling the local sample for tick+delay. Blocks until the peer's
/// inputs arrive (lockstep stall). Must be called with interrupts ENABLED.
/// No-op when the session is inactive; drops to inactive on device failure.
void nw_ExchangePads(PADStatus* pads);

#endif

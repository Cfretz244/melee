#ifndef GALE01_NW_NETPLAY_H
#define GALE01_NW_NETPLAY_H

#include <platform.h>

#include <baselib/controller.h>

/// Engine-level lockstep netplay over a paravirtual EXI device (slot B).
///
/// Both peers run this DOL in lockstep from boot: nw_Init() handshakes with
/// the host-side device (EXI_DeviceMeleeNetplay in the Dolphin fork) to obtain
/// the shared RNG seed, input delay, and port assignment; thereafter
/// nw_ExchangeMaster() — called from the frame loop right after
/// lb_800198E0()'s master-status renew — replaces HSD_PadMasterStatus[] with
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
void nw_ExchangeMaster(void);

#endif

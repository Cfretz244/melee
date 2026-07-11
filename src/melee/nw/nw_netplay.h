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

/// Visual/sim RNG decoupling (v13): save/restore the live RNG state word
/// (sysdolphin random.c) around effect-spawn dispatch, so effect-pool-
/// conditioned VFX roll counts cannot shift sim-consequential rolls within
/// a tick. Callers guard on nw_IsActive().
u32 nw_SeedSave(void);
void nw_SeedRestore(u32 saved);

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

/// Scene-exit barrier. Stream-timed exit triggers (movie/THP end, load
/// completion) land on DIFFERENT ticks per peer once their cycle histories
/// differ (rollback replays execute real cycles), so honoring a scene exit
/// the tick it fires phase-shifts every later scene and splits the state
/// checksum at the next fight. Call every tick with the local exit decision:
/// the flag rides a struct-padding byte (0x42) of our own port's block in
/// the input exchange -- delayed, recorded and replayed exactly like inputs
/// -- and this returns true only on the first tick BOTH peers' flags are up,
/// which is the same tick on both sides by construction. On release the RNG
/// seed is re-synced from shared state (seed ^ tick), discarding the
/// roll-count divergence the wait window accumulated while one peer held a
/// finished scene. Passthrough (returns local_done) when inactive.
///
/// @p routing points at the minor-scene routing byte
/// (GameRouting.pending_scene). It is published alongside the ready flag in
/// padding byte 0x43, and on release BOTH peers overwrite it with the value
/// served from the HOST's port block. Exit timing alone is not enough: the
/// wait window (and stream drift) can leave the peers with DIFFERENT pending
/// routing at the same synchronized exit -- observed as one peer advancing
/// the attract loop (demo -> howto movie) while the other wrapped (demo ->
/// opening movie), a latent fork that only splits the checksum tens of
/// thousands of ticks later when the divergent scene flows finally roll
/// different demo casts into player_slots. Both peers adopting the host's
/// SERVED byte (the host too -- its live value may be newer than what the
/// delay line delivered) makes the destination identical by construction.
/// The sequential fallback (pending == 0) is deterministic given equal
/// curr_scene, and the attract Decide callbacks never write routing.
bool nw_SceneBarrier(bool local_done, u8* routing);

#endif

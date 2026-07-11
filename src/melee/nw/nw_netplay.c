#include "nw_netplay.h"

#include <string.h>

#include <dolphin/exi.h>
#include <dolphin/os.h>
#include <dolphin/pad.h>

#include "melee/pl/forward.h"
#include "melee/pl/player.h"

/// EXI wiring: the paravirtual device sits in memory card slot B.
#define NW_CHAN 1
#define NW_DEV 0
#define NW_FREQ 4 /* 16 MHz nominal; the HLE device ignores it */

/// SDK EXI transfer directions (no named constants in the decomp headers).
#define NW_EXI_READ 0
#define NW_EXI_WRITE 1

/// Device command words (high byte of the 4-byte imm write). Must match
/// EXI_DeviceMeleeNetplay.h in the Dolphin fork.
#define NW_CMD_HANDSHAKE 0x01
#define NW_CMD_SEND 0x02
#define NW_CMD_POLL 0x03
#define NW_CMD_RECV 0x04
#define NW_CMD_CHECKSUM 0x05

#define NW_MAGIC 0x4D4E4554 /* 'MNET' */
#define NW_PROTO_VERSION 3
#define NW_CHECKSUM_INTERVAL 60

/// v3 POLL status word: status<<24 | arg (must match the device enum).
#define NW_POLL_WAIT 0
#define NW_POLL_READY 1
#define NW_POLL_REPLAY 3

/// Exchange unit: one full post-transform HSD_PadStatus per port.
#define NW_PAD_BYTES sizeof(HSD_PadStatus) /* 0x44 */
/// DMA payload: u32 tick + 4 pads, padded to a 32-byte multiple.
#define NW_XFER_BYTES 288

/// Match state checksum source; no extern in player.h (game code goes through
/// accessors), so declare it ourselves.
extern StaticPlayer player_slots[PL_SLOT_MAX];

static struct {
    bool active;
    bool replaying;
    u8 local_mask;
    u8 combined_mask;
    u8 delay;
    u32 seed;
    u32 tick;
    nw_TickRunner runner;
    /// Scene barrier (nw_SceneBarrier): our outgoing ready flag, and both
    /// peers' flags as served for the current tick (see NW_PAD_FLAG_OFF).
    u8 scene_ready_out;
    u8 scene_flag_p0;
    u8 scene_flag_p1;
    /// Scene routing (see NW_PAD_ROUTE_OFF): our outgoing pending-scene
    /// byte, and the HOST's as served for the current tick.
    u8 scene_route_out;
    u8 scene_route_host;
    /// Post-release drain fence: barrier evaluation is suppressed for ticks
    /// < this value. Flag bytes ride the exchange delay+prediction pipeline,
    /// so for up to delay ticks after a release BOTH peers' served flags
    /// still read "ready" from pre-release stamps; without the fence the
    /// slower peer re-releases on the stale flags (v16q1: releases at
    /// 13067/13068/13069 -- re-adopting a stale routing byte and re-rolling
    /// the seed re-sync on ticks where the exited peer does neither).
    u32 barrier_drain_until;
} nw;

/// The RNG state word (sysdolphin random.c), for the barrier's seed re-sync.
/// Same extern gmmain.c uses for the boot-time seed override.
extern s32* seed_ptr;

/// Live match global (lbl_8046B6A0_t); byte 0 is the match-end state
/// (0 = playing, nonzero = GAME! frozen/finished). Declared opaquely to
/// keep nw free of the gm type graph (see the quiesce stamp).
struct lbl_8046B6A0_t;
extern struct lbl_8046B6A0_t* gm_8016AE38(void);
/// Remaining match timer in seconds; false when the match is untimed.
extern int GetMatchTimer(int*);

/// Match-end quiesce predicate. The GAME!/TIMEOUT transition issues
/// asset loads (banner, announcer, results-screen preloads) starting a
/// few ticks BEFORE the match global flips (v18q1: last rollback
/// 16918->16915, quiesce armed 16920, orphaned lbFile load in between).
/// The end is sim-deterministic, so lead the stamp: timed matches stamp
/// from 5 seconds remaining; untimed (stock) matches stamp when any
/// in-game player hits 0 stocks (in timed matches stocks sit at 0 from
/// the start, hence the gate). Cheap: runs once per SEND.
static int nw_MatchEnding(void)
{
    int t;
    int slot;

    if (*(volatile u8*) gm_8016AE38() != 0) {
        return 1;
    }
    if (GetMatchTimer(&t)) {
        if (t <= 5) {
            return 1;
        }
    } else {
        for (slot = 0; slot < PL_SLOT_MAX; slot++) {
            StaticPlayer* sp = &player_slots[slot];
            if (sp->player_state == 2 && sp->stocks <= 0) {
                return 1;
            }
        }
    }
    return 0;
}

/// Struct-padding bytes inside HSD_PadStatus (0x42..0x43 are alignment tail)
/// carrying the scene-ready flag and the minor-scene routing byte. They ride
/// the existing exchange end to end: delayed with inputs, recorded in the
/// device's replay history, opaque to the game (never read as pad data) and
/// outside the state checksum.
#define NW_PAD_FLAG_OFF 0x42
#define NW_PAD_ROUTE_OFF 0x43

/// One shared bounce buffer for all EXI DMA (transactions are sequential).
/// GC EXI DMA requires 32-byte alignment and 32-byte-multiple lengths.
static u8 nw_dma_buf[NW_XFER_BYTES] ATTRIBUTE_ALIGN(32);

bool nw_IsActive(void)
{
    return nw.active;
}

u32 nw_GetSeed(void)
{
    return nw.seed;
}

u32 nw_SeedSave(void)
{
    return (u32) *seed_ptr;
}

void nw_SeedRestore(u32 saved)
{
    *seed_ptr = (s32) saved;
}

/// Run one device transaction: imm-write the command word, then optionally a
/// DMA payload (@p dma_len must be a multiple of 32), then optionally a 4-byte
/// imm read into @p imm_out. Returns false on any EXI-layer failure.
static bool nw_Transact(u8 cmd, void* dma_buf, s32 dma_len, u32 dma_dir,
                        u32* imm_out)
{
    u32 cmd_word = (u32) cmd << 24;
    bool ok;

    if (!EXILock(NW_CHAN, NW_DEV, NULL)) {
        return false;
    }
    if (!EXISelect(NW_CHAN, NW_DEV, NW_FREQ)) {
        EXIUnlock(NW_CHAN);
        return false;
    }

    ok = EXIImm(NW_CHAN, &cmd_word, sizeof(cmd_word), NW_EXI_WRITE, NULL) &&
         EXISync(NW_CHAN);
    if (ok && dma_buf != NULL) {
        ok = EXIDma(NW_CHAN, dma_buf, dma_len, dma_dir, NULL) &&
             EXISync(NW_CHAN);
    }
    if (ok && imm_out != NULL) {
        ok = EXIImm(NW_CHAN, imm_out, sizeof(*imm_out), NW_EXI_READ, NULL) &&
             EXISync(NW_CHAN);
    }

    EXIDeselect(NW_CHAN);
    EXIUnlock(NW_CHAN);
    return ok;
}

void nw_Init(void)
{
    u32 id = 0;

    nw.active = false;

    if (EXIGetID(NW_CHAN, NW_DEV, &id) == 0 || id != NW_MAGIC) {
        return;
    }

    /// Handshake blob (device blocks until the session resolves):
    /// { u32 magic; u8 active; u8 local_port_mask; u8 delay; u8 proto_ver;
    ///   u32 rng_seed; u32 flags; }
    memset(nw_dma_buf, 0, sizeof(nw_dma_buf));
    if (!nw_Transact(NW_CMD_HANDSHAKE, nw_dma_buf, 32, NW_EXI_READ, NULL)) {
        return;
    }

    {
        u32 magic = *(u32*) &nw_dma_buf[0];
        u8 session_active = nw_dma_buf[4];
        u8 proto_ver = nw_dma_buf[7];

        if (magic != NW_MAGIC || !session_active ||
            proto_ver != NW_PROTO_VERSION)
        {
            OSReport("nw: no session (magic %08x active %d ver %d)\n", magic,
                     session_active, proto_ver);
            return;
        }

        nw.local_mask = nw_dma_buf[5];
        nw.delay = nw_dma_buf[6];
        nw.seed = *(u32*) &nw_dma_buf[8];
        /// The device guarantees frames for every participating port; ports
        /// outside this mask are forced to "no controller" after each RECV.
        /// MVP is 1v1: the peers' masks are disjoint and cover both players.
        nw.combined_mask = 0x3;
        nw.tick = 0;
        nw.active = true;

        OSReport("nw: session up (ports %02x, delay %d)\n", nw.local_mask,
                 nw.delay);
    }
}

/// Simple FNV-1a over the persistent per-player match state. Both peers run
/// identical code, so only peer-vs-peer equality matters.
///
/// RENDER-PHASE-WRITTEN spans of each slot are skipped: bytes the render
/// phase rewrites once per VIDEO frame diverge whenever the peers' video-
/// frame-to-tick alignment differs -- benign under symmetric lockstep, but
/// R1 rollback parks one peer at POLL for stretches while its render loop
/// keeps drawing, so these bytes desync the hash at fully identical sim
/// state + RNG (proven: seeds byte-equal through the desync tick, dump diff
/// hit only these spans + downstream).
/// - 0x10..0x1C: player_poses.nametag_pos (floating name tag, interpolated
///   per video frame; pure UI).
/// - 0xD30..0xEB0: the bookkeeping tail past StaleMoveTable -- plbonuslib
///   accumulators reading HUD state (magnify bubble counter etc.) plus its
///   unnamed float/word neighbors (+0xD59, +0xEA1 observed diverging on
///   both cached-core and jit runs). Results-screen bookkeeping only;
///   nothing in the fight sim reads it back. Known artifact: peers can
///   disagree on special-award tallies after rollbacks (and Bonus Mode
///   scoring would genuinely diverge -- out of scope).
#define NW_SLOT_NAMETAG_START 0x10
#define NW_SLOT_NAMETAG_END 0x1C
#define NW_SLOT_STATS_START 0xD30
#define NW_SLOT_STATS_END 0xEB0

static u32 nw_StateChecksum(void)
{
    u32 hash = 0x811C9DC5;
    u32 slot;
    u32 off;

    for (slot = 0; slot < PL_SLOT_MAX; slot++) {
        const u8* p = (const u8*) &player_slots[slot];
        for (off = 0; off < sizeof(StaticPlayer); off++) {
            if (off == NW_SLOT_NAMETAG_START) {
                off = NW_SLOT_NAMETAG_END - 1;
                continue;
            }
            if (off == NW_SLOT_STATS_START) {
                off = NW_SLOT_STATS_END - 1;
                continue;
            }
            hash = (hash ^ p[off]) * 0x01000193;
        }
    }
    return hash;
}

void nw_SetTickRunner(nw_TickRunner runner)
{
    nw.runner = runner;
}

bool nw_IsReplaying(void)
{
    return nw.replaying;
}

bool nw_SceneBarrier(bool local_done, u8* routing)
{
    bool both;

    if (!nw.active) {
        return local_done;
    }
    /// Drain fence: served flag bytes within delay ticks of the previous
    /// release are stale pre-release stamps (see barrier_drain_until).
    if (nw.tick < nw.barrier_drain_until) {
        nw.scene_ready_out = local_done ? 1 : 0;
        nw.scene_route_out = *routing;
        return false;
    }
    nw.scene_ready_out = local_done ? 1 : 0;
    nw.scene_route_out = *routing;
    /// Bit 0 only: bit 7 of the flag byte carries the match-end quiesce
    /// stamp (see nw_ExchangeMaster), which must never release the barrier.
    both = (nw.scene_flag_p0 & 1) != 0 && (nw.scene_flag_p1 & 1) != 0;
    if (both) {
        /// Release: both flags landed on the same tick on both peers. The
        /// wait window ran divergent code (one peer held a finished scene
        /// while the other caught up), so roll counts may differ -- re-seed
        /// deterministically from shared state before the next scene rolls
        /// anything (the attract-demo character/stage roll is first).
        *seed_ptr = (s32) (nw.seed ^ nw.tick);
        /// Destination sync: BOTH peers (host included -- its live value may
        /// be newer than what the delay line delivered) adopt the routing
        /// byte served from the host's block, so the exit lands both peers
        /// in the SAME next scene, not just at the same tick (see header).
        *routing = nw.scene_route_host;
        nw.scene_ready_out = 0;
        /// Served flag bytes for the next delay ticks were stamped before
        /// this release; suppress re-evaluation until they drain (both
        /// peers compute the same fence from the same release tick).
        nw.barrier_drain_until = nw.tick + nw.delay + 1;
        OSReport("nw: scene barrier released at tick %d route %d\n", nw.tick,
                 nw.scene_route_host);
    }
    return both;
}

/// RECV the device's serve-tick entries and swap them into master. Shared by
/// the normal per-tick path and replay re-runs (the device serves recorded
/// history while replaying).
static bool nw_RecvInject(void)
{
    int i;

    if (!nw_Transact(NW_CMD_RECV, nw_dma_buf, NW_XFER_BYTES, NW_EXI_READ,
                     NULL))
    {
        return false;
    }
    memcpy(HSD_PadMasterStatus, nw_dma_buf, 4 * NW_PAD_BYTES);

    /// Scene-barrier flags for the served tick (host stamps port 0's block,
    /// client port 1's; see nw_SceneBarrier). Replay re-injection overwrites
    /// these with historical values, but the post-replay inject of the
    /// current tick runs last and restores them before anyone looks.
    nw.scene_flag_p0 = nw_dma_buf[0 * NW_PAD_BYTES + NW_PAD_FLAG_OFF];
    nw.scene_flag_p1 = nw_dma_buf[1 * NW_PAD_BYTES + NW_PAD_FLAG_OFF];
    /// Routing byte from the HOST's block (port 0) -- the destination both
    /// peers adopt at barrier release.
    nw.scene_route_host = nw_dma_buf[0 * NW_PAD_BYTES + NW_PAD_ROUTE_OFF];

    /// Ports with no participant must read as unplugged, not neutral.
    for (i = 0; i < 4; i++) {
        if (!(nw.combined_mask & (1 << i))) {
            HSD_PadMasterStatus[i].err = PAD_ERR_NO_CONTROLLER;
        }
    }
    return true;
}

/// Per-tick deterministic RNG base: seed = f(session seed, tick), applied at
/// every tick's exchange point (and per replayed tick) on BOTH peers.
///
/// Why: some RNG roll BUNDLES are conditioned on state that render-phase
/// code writes from real-time counters (e.g. particle spawns skip their
/// whole rolling setup when HSD_ObjAlloc fails, and pool pressure tracks
/// display-frame aging). A rollback replay runs tick bodies with NO renders
/// interleaved, so those bundles can fire differently than the original
/// pass -- shifting the shared LCG walk even though every affected roll is
/// VFX-only (torture/R1 forks: 4-8 rolls off inside one replayed tick).
/// Masking can't fix a walk offset; reseeding per tick makes the walk
/// tick-deterministic, so any non-sim roll-count divergence heals at the
/// next tick boundary instead of compounding forever.
///
/// Residual exposure: a sim-consequential roll (item spawn, turnip...) in
/// the SAME tick AFTER a diverged VFX bundle still reads a shifted value.
/// Netplay-only (nw.active); retail walk continuity is not preserved.
static void nw_ReseedTick(u32 tick)
{
    *seed_ptr = (s32) (nw.seed ^ (tick * 0x9E3779B9u));
}

void nw_ExchangeMaster(void)
{
    u32 poll;
    u32 status;

    if (!nw.active) {
        return;
    }

    /// Schedule the local post-transform snapshot for tick + delay and ship
    /// it to the peer (the device loops our own ports back at that tick).
    memset(nw_dma_buf, 0, sizeof(nw_dma_buf));
    *(u32*) &nw_dma_buf[0] = nw.tick + nw.delay;
    memcpy(&nw_dma_buf[4], HSD_PadMasterStatus, 4 * NW_PAD_BYTES);
    {
        /// Stamp the scene-barrier flag and routing byte into our own first
        /// owned port's block; the device forwards only owned ports, so each
        /// peer's bytes arrive in its conventional slot (host: port 0,
        /// client: port 1).
        u32 p = 0;
        while (p < 3 && !(nw.local_mask & (1 << p))) {
            p++;
        }
        /// Bit 7: match-end quiesce stamp. From GAME! (match info unk_0
        /// nonzero: frozen final frame through the finished state) to the
        /// scene exit, the game issues results-screen loads (HSD synth
        /// bank ARQ chains, preload reads) whose completion flags live in
        /// restored heap; a rollback across one orphans it (v17q2: both
        /// peers wedged post-release, host in HSD_Synth spin). The device
        /// treats the stamp like an armed barrier fence: prediction
        /// quiesced, speculation drained -- the whole GAME!-to-exit window
        /// runs confirmed lockstep. unk_0 flips at the same tick on both
        /// peers (deterministic sim); it stays 0 for LRA+Start exits
        /// (residual exposure there, accepted). Layering: read via the
        /// documented first byte of the live match global rather than
        /// pulling in the whole gm type graph.
        nw_dma_buf[4 + p * NW_PAD_BYTES + NW_PAD_FLAG_OFF] =
            nw.scene_ready_out | (nw_MatchEnding() ? 0x80 : 0);
        nw_dma_buf[4 + p * NW_PAD_BYTES + NW_PAD_ROUTE_OFF] =
            nw.scene_route_out;
    }
    if (!nw_Transact(NW_CMD_SEND, nw_dma_buf, NW_XFER_BYTES, NW_EXI_WRITE,
                     NULL))
    {
        goto fail;
    }

    /// Block until the agreed entries for the current tick arrive (the
    /// device pre-seeds ticks 0..delay-1 with neutral pads, so this never
    /// deadlocks at boot). This spin is the lockstep stall.
    ///
    /// v3: the device may instead direct a REPLAY -- it has already restored
    /// memory K ticks back (rollback, or the R0 torture injector), and we
    /// must re-run those K engine ticks with the recorded agreed inputs
    /// before finishing the current tick. The replayed ticks are
    /// deterministic re-execution: no SENDs, no checksums, no tick advance.
    for (;;) {
        poll = 0;
        if (!nw_Transact(NW_CMD_POLL, NULL, 0, 0, &poll)) {
            goto fail;
        }
        status = poll >> 24;

        if (status == NW_POLL_READY) {
            break;
        }
        if (status == NW_POLL_REPLAY) {
            u32 k = poll & 0xFFFFFF;
            /// Replayed serves cover ticks [nw.tick - k, nw.tick): the
            /// device rewound its serve cursor k back; nw.tick itself is
            /// served by the READY path after the loop.
            u32 replay_tick = nw.tick - k;

            /// The device has already rewound its serve tick; dropping the
            /// directive would skew tick numbering forever. No runner means
            /// this build cannot replay -- fail the session loudly instead.
            if (nw.runner == NULL) {
                OSReport("nw: REPLAY(%d) but no tick runner\n", k);
                goto fail;
            }

            nw.replaying = true;
            while (k-- > 0) {
                if (!nw_RecvInject()) {
                    nw.replaying = false;
                    goto fail;
                }
                nw_ReseedTick(replay_tick);
                replay_tick += 1;
                nw.runner();
            }
            nw.replaying = false;
        }
    }

    if (!nw_RecvInject()) {
        goto fail;
    }
    nw_ReseedTick(nw.tick);

    nw.tick += 1;

    /// Periodic divergence check; the device cross-compares with the peer.
    if (nw.tick % NW_CHECKSUM_INTERVAL == 0) {
        memset(nw_dma_buf, 0, 32);
        *(u32*) &nw_dma_buf[0] = nw.tick;
        *(u32*) &nw_dma_buf[4] = nw_StateChecksum();
        if (!nw_Transact(NW_CMD_CHECKSUM, nw_dma_buf, 32, NW_EXI_WRITE, NULL))
        {
            goto fail;
        }
    }

    return;

fail:
    /// Device gone mid-session. A netplay session must never silently fork
    /// into two live offline games that each look like the real match --
    /// freeze this peer instead. The other peer stalls at its next exchange
    /// when our inputs stop arriving, so both sides end visibly frozen.
    /// (A user-facing error path can replace the spin later.)
    OSReport("nw: device failure at tick %d; halting session\n", nw.tick);
    nw.active = false;
    for (;;) {
    }
}

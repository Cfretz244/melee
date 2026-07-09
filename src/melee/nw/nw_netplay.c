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
#define NW_PROTO_VERSION 2
#define NW_CHECKSUM_INTERVAL 60

/// Exchange unit: one full post-transform HSD_PadStatus per port.
#define NW_PAD_BYTES sizeof(HSD_PadStatus) /* 0x44 */
/// DMA payload: u32 tick + 4 pads, padded to a 32-byte multiple.
#define NW_XFER_BYTES 288

/// Match state checksum source; no extern in player.h (game code goes through
/// accessors), so declare it ourselves.
extern StaticPlayer player_slots[PL_SLOT_MAX];

static struct {
    bool active;
    u8 local_mask;
    u8 combined_mask;
    u8 delay;
    u32 seed;
    u32 tick;
} nw;

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
static u32 nw_StateChecksum(void)
{
    const u8* p = (const u8*) player_slots;
    u32 hash = 0x811C9DC5;
    u32 i;

    for (i = 0; i < sizeof(player_slots); i++) {
        hash = (hash ^ p[i]) * 0x01000193;
    }
    return hash;
}

void nw_ExchangeMaster(void)
{
    u32 ready;
    int i;

    if (!nw.active) {
        return;
    }

    /// Schedule the local post-transform snapshot for tick + delay and ship
    /// it to the peer (the device loops our own ports back at that tick).
    memset(nw_dma_buf, 0, sizeof(nw_dma_buf));
    *(u32*) &nw_dma_buf[0] = nw.tick + nw.delay;
    memcpy(&nw_dma_buf[4], HSD_PadMasterStatus, 4 * NW_PAD_BYTES);
    if (!nw_Transact(NW_CMD_SEND, nw_dma_buf, NW_XFER_BYTES, NW_EXI_WRITE,
                     NULL))
    {
        goto fail;
    }

    /// Block until the agreed entries for the current tick arrive (the
    /// device pre-seeds ticks 0..delay-1 with neutral pads, so this never
    /// deadlocks at boot). This spin is the lockstep stall.
    do {
        ready = 0;
        if (!nw_Transact(NW_CMD_POLL, NULL, 0, 0, &ready)) {
            goto fail;
        }
    } while (ready == 0);

    if (!nw_Transact(NW_CMD_RECV, nw_dma_buf, NW_XFER_BYTES, NW_EXI_READ,
                     NULL))
    {
        goto fail;
    }
    memcpy(HSD_PadMasterStatus, nw_dma_buf, 4 * NW_PAD_BYTES);

    /// Ports with no participant must read as unplugged, not neutral.
    for (i = 0; i < 4; i++) {
        if (!(nw.combined_mask & (1 << i))) {
            HSD_PadMasterStatus[i].err = PAD_ERR_NO_CONTROLLER;
        }
    }

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
    /// Device gone mid-session: drop to offline play with local pads rather
    /// than hanging the game.
    OSReport("nw: device failure at tick %d; netplay disabled\n", nw.tick);
    nw.active = false;
}

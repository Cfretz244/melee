#include "gm_1A45.h"

#include "gm_1A36.h"

#include "gm_1A45.static.h"

#include "gm_unsplit.h"

#include "db/db.h"
#include "gm/gmscdata.h"
#include "if/ifcoget.h"
#include "lb/lb_0195.h"
#include "lb/lbaudio_ax.h"

#if defined(NETPLAY) && !defined(NETPLAY_NO_HOOKS)
#include "nw/nw_netplay.h"
#endif
#include "lb/lbcardgame.h"
#include "lb/lbheap.h"
#include "lb/lbspdisplay.h"

#include <dolphin/os/OSThread.h>
#include <baselib/controller.h>
#include <baselib/gobjproc.h>
#include <baselib/initialize.h>
#include <baselib/leak.h>
#include <baselib/particle.h>
#include <baselib/perf.h>
#include <baselib/sobjlib.h>

static u64 gm_803DA888[8] = {
    0, 0x82FFFA, 0, 0x8EFFFA, 0x800FFA, 0x808FFA, 0x800FFA, 0,
};

u64 gm_803DA8C8[2] = { -1, -1 };

bool gm_801A45E8(int bit)
{
    return gm_80479D58.unk_10.x0 & (1ULL << bit);
}

int gm_801A4624(void)
{
    return gm_80479D58.unk_10.x0;
}

void gm_801A4634(int bit)
{
    gm_80479D58.unk_10.x0 |= 1ULL << bit;
}

void gm_801A4674(int bit)
{
    gm_80479D58.unk_10.x0 &= ~(1ULL << bit);
}

bool gm_801A46B8(int bit)
{
    return gm_80479D58.unk_10.x2 & (1ULL << bit);
}

bool fn_801A46F4(void)
{
    int i;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        HSD_PadStatus* pad = &HSD_PadMasterStatus[(u8) i];
        if (pad->err == 0 && (pad->trigger & 8) && (pad->button & HSD_PAD_X)) {
            return true;
        }
    }
    return false;
}

bool fn_801A47E4(void)
{
    int i;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        HSD_PadStatus* pad = &HSD_PadMasterStatus[(u8) i];
        if (pad->err == 0 && (pad->trigger & 0x10)) {
            return true;
        }
    }
    return false;
}

u64 gm_801A48A4(u8 arg0)
{
    int i;
    u64 result = 0;

    for (i = 0; i < ARRAY_SIZE(gm_803DA888); i++) {
        if (arg0 & 1) {
            result |= gm_803DA888[i];
        }
        arg0 >>= 1;
    }

    return result;
}

void gm_801A4970(int (**arg0)(void))
{
    HSD_PadStatus* temp_r3;
    s8 var_r26;
    s8* temp_r4;
    u64 temp_ret;
    int i;
    PAD_STACK(8);

    var_r26 = 0;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        temp_r3 = &HSD_PadMasterStatus[(u8) i];
        if ((temp_r3->trigger & 2) && (temp_r3->button & 0x400)) {
            lbHeap_80015DF8();
            OSReport("[hsdDumpClassStat] -- Report --\n");
            hsdDumpClassStat(NULL, 0, 1);
            OSReport("\n");
            OSReport("[HSD_ObjDumpStat] -- Report --\n");
            HSD_ObjDumpStat();
            OSReport("\n");
            db_PrintEntityCounts();
            db_PrintThreadInfo();
            HSD_Leak_80387DF8(0);
            if (gm_804D6728 != NULL) {
                gm_801653C8(gm_804D6728);
                gm_804D6728 = NULL;
            } else {
                gm_804D6728 = gm_80165388(0x19, 0x3F, 0, 0xFE);
                if (gm_804D6724 != NULL) {
                    gm_804D6724();
                }
            }
        }
    }

    if (arg0[0] != NULL && arg0[0]() != 0) {
        if (gm_801A45E8(0)) {
            gm_80479D58.unk_10.x0 &= ~1;
        } else {
            gm_80479D58.unk_10.x0 |= 1;
        }
    }
    if (gm_801A45E8(0)) {
        if (arg0[1] != NULL && arg0[1]() != 0) {
            gm_80479D58.unk_10.x2 |= 1;
        }
    }
}

void gm_801A4B08(bool (*arg0)(void), bool (*arg1)(void))
{
    gm_80479D58.unk_10.x4[0] = arg0;
    gm_80479D58.unk_10.x4[1] = arg1;
}

void gm_801A4B1C(void)
{
    gm_801A4B08(fn_801A46F4, fn_801A47E4);
}

void gm_801A4B40(UNK_T arg0)
{
    gm_80479D58.unk_10.unk_30 = arg0;
}

void gm_801A4B50(int arg0)
{
    gm_80479D58.unk_10.unk_34 = arg0;
}

void gm_801A4B60(void)
{
    gm_80479D58.unk_C = 1;
}

void gm_801A4B74(void)
{
    gm_80479D58.unk_C = 2;
}

void gm_801A4B88(struct GameSceneInfo* info)
{
    gm_804D6720 = info;
}

/// @brief returns a pointer to the current scenes enter data
void* gm_801A4B90(void)
{
    return gm_804D6720->load_data;
}

/// @brief returns a pointer to the current scenes exit data
void* gm_801A4B9C(void)
{
    return gm_804D6720->leave_data;
}

u32 gm_801A4BA8(void)
{
    return gm_80479D58.unk_0;
}

u32 gm_801A4BB8(void)
{
    return gm_80479D58.unk_8;
}

HSD_GObj* gm_801A4BC8(void)
{
    return gm_804D672C;
}

void fn_801A4BD0(HSD_GObj* gobj) {}

void gm_801A4BD4(void)
{
    PAD_STACK(0x18);

    gm_801A4B08(fn_801A46F4, fn_801A47E4);
    gm_801A4B40(0);
    gm_801A4B50(0);

    lb_80019880(1.0F / 60 * OS_TIMER_CLOCK);
    HSD_GObj_803912E0(&gm_80479D48.initdata);
    gm_80479D48.initdata.gproc_pri_max = 0x18;
    HSD_SObjLib_804D7960 =
        HSD_GObj_803912A8(&gm_80479D48.initdata, &HSD_SObjLib_8040C3A4);
    HSD_SObjLib_803A44A4();
    gm_80479D48.initdata.unk_2 = &gm_80479D58.unk_10.unk_28;
    HSD_GObj_80391304(&gm_80479D48.initdata);
    hsd_80392474();
    un_802FF78C();
    gm_804D672C = GObj_Create(14, 0, 0);
    if (gm_804D672C != NULL) {
        HSD_GObj_SetupProc(gm_804D672C, fn_801A4BD0, 0);
    }
    gm_804D6728 = NULL;
    gm_804D6724 = NULL;
    gm_801A3E88();
    lbAudioAx_8002835C();
    lb_80014534();
}

GameSceneHandler* gm_801A4CE0(u8 id)
{
    GameSceneHandler* cur;
    for (cur = gm_801A50A0(); cur->class_id != 0x2D; cur++) {
        if (cur->class_id == id) {
            return cur;
        }
    }
    return NULL;
}

inline u64 maybe_gm_801A48A4(u8 i)
{
    u64 temp_ret = gm_801A48A4(i);
    if (gm_80479D58.unk_10.unk_38_0) {
        return temp_ret;
    } else {
        return -1ULL;
    }
}

#if defined(NETPLAY) && !defined(NETPLAY_NO_HOOKS)
/// NETPLAY build only: the scene loop's logic tick is factored into
/// gm_SceneTickBody so rollback replay (nw_netplay.h tick runner) can
/// deterministically re-run restored ticks. The matching build keeps the
/// original monolithic gm_801A4D34 in the #else branch below, byte-identical
/// to retail -- do not let the two drift except for the factoring itself.
///
/// One engine logic tick: scene think + GObj procs, everything between the
/// master-status exchange and the render phase. Rendering is NOT here -- the
/// scene loop draws once per video frame regardless of how many logic ticks
/// ran (pad-queue catch-up, replays).
static void gm_SceneTickBody(void (*arg0)(void));

/// Replay trampoline: nw_ExchangeMaster() re-runs engine ticks through this
/// after the device restores memory. The scene callback is a loop parameter,
/// so the loop parks it here each entry.
static void (*nw_scene_cb)(void);
static void nw_SceneTickRunner(void)
{
    gm_SceneTickBody(nw_scene_cb);
}


void gm_801A4D34(void (*arg0)(void), GameSceneInfo* arg1)
{
    int pad_queue_count;
    int i;
    int nw_exit_ok;
    struct gm_80479D58_t* temp_r25;

    PAD_STACK(28);

    temp_r25 = &gm_80479D58;
    gm_801677C0(&temp_r25->unk_10);
    gm_80479D58.unk_0 = 0;
    gm_80479D58.unk_4 = 0;
    gm_80479D58.unk_8 = 0;
    gm_80479D58.unk_C = 0;
    HSD_PadFlushQueue(HSD_PAD_FLUSH_QUEUE_LEAVE1);
    lb_8001CF18();

    nw_exit_ok = 0;
#if defined(NETPLAY) && !defined(NETPLAY_NO_HOOKS)
    nw_scene_cb = arg0;
    nw_SetTickRunner(nw_SceneTickRunner);
#endif

    /// NETPLAY: a locally-requested scene exit (unk_C) is only honored once
    /// nw_SceneBarrier releases -- stream-timed exit triggers land on
    /// different ticks per peer, and exiting unilaterally phase-shifts every
    /// later scene (see nw_netplay.h). Until release, keep ticking the
    /// finished scene.
    while (temp_r25->unk_C == 0 || !nw_exit_ok) {
        hsd_80392E80();
        gmMainLib_8046B0F0.xC = false;

        while ((pad_queue_count = lb_80019894()) == 0) {
            lb_800195D0();
        }
        lb_800195D0();

        if (HSD_PadGetResetSwitch()) {
            gmMainLib_8046B0F0.resetting = true;
            break;
        }

        for (i = 0; i < pad_queue_count; i++) {
            HSD_PerfSetStartTime();
            lb_800198E0();
#if defined(NETPLAY) && !defined(NETPLAY_NO_HOOKS)
            /// Lockstep netplay: substitute the agreed frame-indexed master
            /// entries before copy/game fanout and the logic tick consume
            /// them. May internally re-run restored ticks (replay) through
            /// gm_SceneTickBody. Hook lives here (gm/) and NOT in sysdolphin
            /// -- see nw_netplay.h.
            nw_ExchangeMaster();
#endif
            gm_SceneTickBody(arg0);
            gmMainLib_8046B0F0.xC = false;
            if (temp_r25->unk_C != 0) {
                if (temp_r25->unk_C == 2 ||
                    nw_SceneBarrier(true, gm_NwPendingScenePtr()))
                {
                    nw_exit_ok = 1;
                    break;
                }
                /// Exit swallowed: peer not ready yet; keep ticking.
            } else {
                nw_SceneBarrier(false, gm_NwPendingScenePtr());
            }
        }
        if (temp_r25->unk_C == 2) {
            break;
        }

        lb_800195D0();
        GXInvalidateVtxCache();
        GXInvalidateTexAll();
        HSD_StartRender(HSD_RP_SCREEN);
        HSD_GObj_80390FC0();
        HSD_Init_803755A8();
        HSD_PerfSetDrawTime();
        HSD_VICopyXFBAsync(HSD_RP_SCREEN);
        if (temp_r25->unk_4 != -2U) {
            temp_r25->unk_4++;
        }
        db_TakeScreenshotIfPending();
        HSD_PerfSetTotalTime();
        HSD_PerfInitStat();
    }
    HSD_VIWaitXFBFlush();
}

static void gm_SceneTickBody(void (*arg0)(void))
{
    struct gm_80479D58_t* temp_r25 = &gm_80479D58;

    {
            if (DbLevel >= 3) {
                gm_801A4970(temp_r25->unk_10.x4);
            }
            if (gm_801A46B8(0) || !gm_801A45E8(0)) {
                temp_r25->unk_10.unk_38_0 = true;
            } else {
                temp_r25->unk_10.unk_38_0 = false;
            }
            if (gm_80479D58.unk_10.unk_38_0) {
                lb_80019900();
                if (lb_80019A30(0)) {
                    gm_801A3A74();
                }
                if (lb_80019A30(0) && (arg0 != NULL)) {
                    arg0();
                }
            }
            if (gm_80479D58.unk_10.x0 != gm_80479D58.unk_10.x1 ||
                temp_r25->unk_10.x2 != temp_r25->unk_10.x3)
            {
                temp_r25->unk_10.unk_20 =
                    maybe_gm_801A48A4(temp_r25->unk_10.x0);
                temp_r25->unk_10.x1 = temp_r25->unk_10.x0;
                temp_r25->unk_10.x3 = temp_r25->unk_10.x2;
                temp_r25->unk_10.x2 = 0;
            }
            temp_r25->unk_10.unk_28 = temp_r25->unk_10.unk_20;
            if (lb_80019A30(0) == 0) {
                temp_r25->unk_10.unk_28 |=
                    gm_803DA8C8[temp_r25->unk_10.unk_34];
            }
            if (lb_80019A30(1) == 0) {
                temp_r25->unk_10.unk_28 |=
                    ~gm_803DA8C8[temp_r25->unk_10.unk_34];
            }
            if (DbLevel >= 3) {
                db_CheckScreenshot();
            }
            /// Audio runs during replays too (NETPLAY): the game-side audio
            /// pool IS in the restored region set, so a replay that skips
            /// this frame update misses the voice FREES the original pass
            /// performed -- post-replay pool occupancy then differs from a
            /// straight-through execution, and lb audio code rolls HSD_Rand
            /// conditioned on pool state, forking the peers' RNG walks
            /// (observed: every R1 rollback desynced within seconds; torture
            /// mostly dodged it because identical-input replays re-submit
            /// identical SFX). Running it ungated makes pool evolution
            /// byte-equal to a straight-through run with the corrected
            /// inputs. Cost: a rolled-back tick's SFX can start twice on the
            /// DSP side (device AX state is not rewound) -- an audible
            /// rollback artifact, not sim state; every rollback
            /// implementation has an equivalent.
            lbAudioAx_80027DF8();
            if (temp_r25->unk_10.unk_30 != NULL) {
                temp_r25->unk_10.unk_30();
            }
            HSD_GObj_80390CFC();
            if (temp_r25->unk_0 != -2) {
                temp_r25->unk_0++;
            }
            if (gm_80479D58.unk_10.unk_38_0 && (lb_80019A30(0) != 0)) {
                if (temp_r25->unk_8 != -2) {
                    temp_r25->unk_8++;
                }
            }
            HSD_PerfSetCPUTime();
            if (DbLevel >= 3) {
                OSCheckActiveThreads();
            }
    }
}

#else /* !NETPLAY: the retail scene loop, byte-identical when compiled */

void gm_801A4D34(void (*arg0)(void), GameSceneInfo* arg1)
{
    int pad_queue_count;
    int i;
    struct gm_80479D58_t* temp_r25;

    PAD_STACK(28);

    temp_r25 = &gm_80479D58;
    gm_801677C0(&temp_r25->unk_10);
    gm_80479D58.unk_0 = 0;
    gm_80479D58.unk_4 = 0;
    gm_80479D58.unk_8 = 0;
    gm_80479D58.unk_C = 0;
    HSD_PadFlushQueue(HSD_PAD_FLUSH_QUEUE_LEAVE1);
    lb_8001CF18();

    while (temp_r25->unk_C == 0) {
        hsd_80392E80();
        gmMainLib_8046B0F0.xC = false;

        while ((pad_queue_count = lb_80019894()) == 0) {
            lb_800195D0();
        }
        lb_800195D0();

        if (HSD_PadGetResetSwitch()) {
            gmMainLib_8046B0F0.resetting = true;
            break;
        }

        for (i = 0; i < pad_queue_count; i++) {
            HSD_PerfSetStartTime();
            lb_800198E0();
            if (DbLevel >= 3) {
                gm_801A4970(temp_r25->unk_10.x4);
            }
            if (gm_801A46B8(0) || !gm_801A45E8(0)) {
                temp_r25->unk_10.unk_38_0 = true;
            } else {
                temp_r25->unk_10.unk_38_0 = false;
            }
            if (gm_80479D58.unk_10.unk_38_0) {
                lb_80019900();
                if (lb_80019A30(0)) {
                    gm_801A3A74();
                }
                if (lb_80019A30(0) && (arg0 != NULL)) {
                    arg0();
                }
            }
            if (gm_80479D58.unk_10.x0 != gm_80479D58.unk_10.x1 ||
                temp_r25->unk_10.x2 != temp_r25->unk_10.x3)
            {
                temp_r25->unk_10.unk_20 =
                    maybe_gm_801A48A4(temp_r25->unk_10.x0);
                temp_r25->unk_10.x1 = temp_r25->unk_10.x0;
                temp_r25->unk_10.x3 = temp_r25->unk_10.x2;
                temp_r25->unk_10.x2 = 0;
            }
            temp_r25->unk_10.unk_28 = temp_r25->unk_10.unk_20;
            if (lb_80019A30(0) == 0) {
                temp_r25->unk_10.unk_28 |=
                    gm_803DA8C8[temp_r25->unk_10.unk_34];
            }
            if (lb_80019A30(1) == 0) {
                temp_r25->unk_10.unk_28 |=
                    ~gm_803DA8C8[temp_r25->unk_10.unk_34];
            }
            if (DbLevel >= 3) {
                db_CheckScreenshot();
            }
            lbAudioAx_80027DF8();
            if (temp_r25->unk_10.unk_30 != NULL) {
                temp_r25->unk_10.unk_30();
            }
            HSD_GObj_80390CFC();
            if (temp_r25->unk_0 != -2) {
                temp_r25->unk_0++;
            }
            if (gm_80479D58.unk_10.unk_38_0 && (lb_80019A30(0) != 0)) {
                if (temp_r25->unk_8 != -2) {
                    temp_r25->unk_8++;
                }
            }
            HSD_PerfSetCPUTime();
            if (DbLevel >= 3) {
                OSCheckActiveThreads();
            }
            gmMainLib_8046B0F0.xC = false;
            if (temp_r25->unk_C != 0) {
                break;
            }
        }
        if (temp_r25->unk_C == 2) {
            break;
        }

        lb_800195D0();
        GXInvalidateVtxCache();
        GXInvalidateTexAll();
        HSD_StartRender(HSD_RP_SCREEN);
        HSD_GObj_80390FC0();
        HSD_Init_803755A8();
        HSD_PerfSetDrawTime();
        HSD_VICopyXFBAsync(HSD_RP_SCREEN);
        if (temp_r25->unk_4 != -2U) {
            temp_r25->unk_4++;
        }
        db_TakeScreenshotIfPending();
        HSD_PerfSetTotalTime();
        HSD_PerfInitStat();
    }
    HSD_VIWaitXFBFlush();
}

#endif /* NETPLAY */

#ifndef MELEE_GM_1A3F_H
#define MELEE_GM_1A3F_H

#include <placeholder.h>

#include <melee/gm/forward.h>

/* 1A3EF4 */ void gm_801A3EF4(void);
/* 1A3F48 */ void gm_801A3F48(GameScene*);
/* 1A4014 */ void gm_801A4014(GameMode*);
/* 1A427C */ void* gm_801A427C(GameScene*);
/* 1A4284 */ void* gm_801A4284(GameScene*);
/* 1A428C */ void gm_SetScene(u8 arg0);
/* 1A42A0 */ void gm_SetPendingScene(u8 pending_scene);
#if defined(NETPLAY) && !defined(NETPLAY_NO_HOOKS)
/// Raw pending-scene routing byte for the netplay scene barrier (NETPLAY
/// builds only, where gm_1A3F.c links from source; see gm_1A3F.c).
u8* gm_NwPendingScenePtr(void);
#endif
/* 1A42B4 */ u8 gm_801A42B4(void); ///< get previous scene
/* 1A42C4 */ u8 gm_801A42C4(void); ///< get current scene
/* 1A42D4 */ UNK_RET gm_801A42D4(UNK_PARAMS);
/* 1A42E8 */ void gm_801A42E8(s8 pending_mode);
/* 1A42F8 */ void gm_801A42F8(int pending_mode);
/* 1A4310 */ u8 gm_801A4310(void); ///< get current mode
/* 1A4320 */ u8 gm_801A4320(void); ///< get previous mode
/* 1A4330 */ void gm_801A4330(u8 (*)(void));
/* 1A4340 */ bool gm_801A4340(u8 mode);
/* 1A43A0 */ u8 gm_801A43A0(u8 arg0);
/* 1A4510 */ void gm_801A4510(void);

#endif

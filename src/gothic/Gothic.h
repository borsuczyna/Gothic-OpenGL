#pragma once
// Gothic.h — Gothic II: Night of the Raven (2.6 fix) engine access layer
//
// Reads Gothic's live game state from known memory addresses and offsets.
// All addresses are for the retail Gothic 2 NotR v2.6 fix executable.
//
// This is a read-only helper — it does NOT hook or patch anything.
// Safe to call from the render thread at any point after Gothic has started.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cmath>

// ---------------------------------------------------------------------------
// Offset helper — casts 'this' to a raw address and adds an offset
// ---------------------------------------------------------------------------
#define G_OFFSET(ptr, off) (reinterpret_cast<DWORD>(ptr) + (off))

namespace Gothic {

// ===================================================================
// Forward declarations (opaque pointers into Gothic's address space)
// ===================================================================
struct zCVob;
struct zCWorld;
struct zCCamera;
struct zCTimer;
struct zCSkyController_Outdoor;
struct oCGame;
struct oCNPC;
struct zCBspTree;

// ===================================================================
// Math types matching Gothic's internal layout (column-major 4×4)
// ===================================================================
struct Float3 {
    float x, y, z;
};

struct Float4x4 {
    float m[4][4]; // [row][col], but stored column-major in Gothic memory
};

struct BBox3D {
    Float3 min;
    Float3 max;
};

// ===================================================================
// Memory addresses — Gothic 2 NotR v2.6 fix globals
// ===================================================================
namespace Addr {
    // Global object pointers (dereference once to get the object*)
    constexpr DWORD pCamera          = 0x008D7F94; // zCCamera**
    constexpr DWORD pGame            = 0x00AB0884; // oCGame**
    constexpr DWORD pTimer           = 0x0099B3D4; // zCTimer*  (direct, not double-ptr)
    constexpr DWORD pRenderer        = 0x00982F08; // zCRenderer*
    constexpr DWORD pInput           = 0x008D1650; // zCInput*
    constexpr DWORD pOption          = 0x008CD988; // zCOption*
    constexpr DWORD pScreen          = 0x00AB6468; // zCView*
    constexpr DWORD pActiveSkyCtrl   = 0x0099AC8C; // zCSkyController*

    // oCGame
    constexpr DWORD Player           = 0x00AB2684; // oCNPC*  (oCGame::s_player)

    // oCGame offsets (from oCGame*)
    constexpr DWORD oGame_World      = 0x08;       // zCWorld*  (_zCSession::world)
    constexpr DWORD oGame_Camera     = 0x0C;       // zCCamera*
    constexpr DWORD oGame_CamVob     = 0x14;       // zCVob*
    constexpr DWORD oGame_GameView   = 0x30;       // zCView*
    constexpr DWORD oGame_SingleStep = 0x11C;       // int

    // zCCamera offsets
    constexpr DWORD oCam_NearPlane   = 0x900;
    constexpr DWORD oCam_FarPlane    = 0x8FC;
    constexpr DWORD oCam_ScreenFade  = 0x8C0;      // int (bool)
    constexpr DWORD oCam_ScreenFadeColor = 0x8C4;  // DWORD (zColor)
    constexpr DWORD oCam_CinemaScope = 0x8E4;      // int (bool)
    constexpr DWORD fCam_GetFov      = 0x0054A8F0; // zCCamera::GetFOV(float&, float&)

    // zCVob offsets
    constexpr DWORD oVob_WorldMatrix = 0x3C;       // Float4x4 (inline 4×4)
    constexpr DWORD oVob_PosX        = 0x48;       // float (matrix[3][0])
    constexpr DWORD oVob_PosY        = 0x58;       // float (matrix[3][1])
    constexpr DWORD oVob_PosZ        = 0x68;       // float (matrix[3][2])
    constexpr DWORD oVob_BBox        = 0x7C;       // BBox3D
    constexpr DWORD oVob_Type        = 0xB0;       // int (EVobType)
    constexpr DWORD oVob_HomeWorld   = 0xB8;       // zCWorld*
    constexpr DWORD oVob_GroundPoly  = 0xBC;       // zCPolygon*
    constexpr DWORD oVob_Alpha       = 0xCC;       // float
    constexpr DWORD oVob_Flags       = 0x104;      // DWORD
    constexpr DWORD oVob_SleepMode   = 0x10C;      // DWORD (mask 0x3)
    constexpr DWORD oVob_VobTree     = 0x24;       // zCTree<zCVob>*

    // zCWorld offsets
    constexpr DWORD oWorld_VobTree   = 0x24;       // zCTree<zCVob> (inline)
    constexpr DWORD oWorld_SkyCtrl   = 0x0E4;      // zCSkyController_Outdoor*
    constexpr DWORD oWorld_BspTree   = 0x1AC;      // zCBspTree (inline)

    // zCSkyController_Outdoor offsets
    constexpr DWORD oSky_WeatherType = 0x30;        // int (0=snow, 1=rain)
    constexpr DWORD oSky_MasterTime  = 0x80;        // float [0..1]
    constexpr DWORD oSky_LastMasterTime = 0x84;
    constexpr DWORD oSky_MasterState = 0x88;        // zCSkyState*
    constexpr DWORD oSky_OverrideColor = 0x558;     // Float3
    constexpr DWORD oSky_OverrideFlag  = 0x564;     // int (bool)
    constexpr DWORD oSky_RainWeight  = 0x69C;       // float
    constexpr DWORD oSky_TimeStartRain = 0x6A8;     // float
    constexpr DWORD oSky_TimeStopRain  = 0x6AC;     // float
    constexpr DWORD oSky_RenderLightning = 0x6B0;   // int
    constexpr DWORD oSky_RainingCounter  = 0x6B8;   // int

    // zCSkyState offsets (from zCSkyState*)
    constexpr DWORD oSkyState_Time       = 0x00;    // float
    constexpr DWORD oSkyState_PolyColor  = 0x04;    // Float3
    constexpr DWORD oSkyState_FogColor   = 0x10;    // Float3
    constexpr DWORD oSkyState_DomeColor1 = 0x1C;    // Float3
    constexpr DWORD oSkyState_DomeColor0 = 0x28;    // Float3
    constexpr DWORD oSkyState_FogDist    = 0x34;    // float

    // zCTimer is a direct pointer (not double-indirect)
    // zCTimer member offsets (from zCTimer*)
    constexpr DWORD oTimer_FactorMotion     = 0x00;  // float
    constexpr DWORD oTimer_FrameTimeMs      = 0x04;  // float (milliseconds)
    constexpr DWORD oTimer_TotalTimeMs      = 0x08;  // float (milliseconds)
    constexpr DWORD oTimer_FrameTimeSec     = 0x0C;  // float (seconds) [G2 only]
    constexpr DWORD oTimer_TotalTimeSec     = 0x10;  // float (seconds) [G2 only]
}

// ===================================================================
// Vob type enum (matches Gothic's EVobType)
// ===================================================================
enum EVobType {
    VOB_TYPE_NORMAL = 0,
    VOB_TYPE_LIGHT  = 1,
    VOB_TYPE_SOUND  = 2,
    VOB_TYPE_LEVEL_COMPONENT = 3,
    VOB_TYPE_SPOT     = 4,
    VOB_TYPE_CAMERA   = 5,
    VOB_TYPE_STARTPOINT = 6,
    VOB_TYPE_WAYPOINT = 7,
    VOB_TYPE_MARKER   = 8,
    VOB_TYPE_SEPARATOR = 127,
    VOB_TYPE_MOB  = 128,
    VOB_TYPE_ITEM = 129,
    VOB_TYPE_NPC  = 130,
};

// ===================================================================
// Weather type enum
// ===================================================================
enum EWeather {
    WEATHER_SNOW = 0,
    WEATHER_RAIN = 1,
};

// ===================================================================
// Sky state — interpolated sky parameters at current time
// ===================================================================
struct SkyState {
    float time;
    Float3 polyColor;
    Float3 fogColor;
    Float3 domeColor0;
    Float3 domeColor1;
    float  fogDist;
};

// ===================================================================
// Sky information snapshot (safe copy from Gothic memory)
// ===================================================================
struct SkyInfo {
    bool   valid;
    float  masterTime;       // 0..1 day cycle
    float  lastMasterTime;
    EWeather weather;
    float  rainWeight;       // 0..1
    float  timeStartRain;
    float  timeStopRain;
    int    renderLightning;
    int    rainingCounter;
    Float3 overrideColor;
    bool   overrideFlag;
    SkyState masterState;    // current interpolated sky state
};

// ===================================================================
// Timer snapshot
// ===================================================================
struct TimerInfo {
    float factorMotion;
    float frameTimeMs;
    float totalTimeMs;
    float frameTimeSec;
    float totalTimeSec;
};

// ===================================================================
// Camera snapshot
// ===================================================================
struct CameraInfo {
    bool  valid;
    float nearPlane;
    float farPlane;
    float fovH;
    float fovV;
    bool  screenFadeEnabled;
    DWORD screenFadeColor;
    bool  cinemaScopeEnabled;
};

// ===================================================================
// Player / vob position snapshot
// ===================================================================
struct VobPosition {
    bool  valid;
    Float3 pos;
    BBox3D bbox;
    int    type;   // EVobType
    float  alpha;  // vob transparency
};

// ===================================================================
// Main Gothic game helper — singleton, call Init() once
// ===================================================================
class Game {
public:
    // --- Singleton ---
    static Game& Get() {
        static Game instance;
        return instance;
    }

    // --- Validity ---
    // Returns true if Gothic's oCGame pointer is non-null (game is running)
    bool IsInGame() const {
        return GetGamePtr() != nullptr;
    }

    // ---------------------------------------------------------------
    // Raw pointer access (use with care — these point into Gothic memory)
    // ---------------------------------------------------------------
    static oCGame* GetGamePtr() {
        auto** pp = reinterpret_cast<oCGame**>(Addr::pGame);
        return pp ? *pp : nullptr;
    }

    static zCCamera* GetCameraPtr() {
        auto** pp = reinterpret_cast<zCCamera**>(Addr::pCamera);
        return pp ? *pp : nullptr;
    }

    static zCTimer* GetTimerPtr() {
        return reinterpret_cast<zCTimer*>(Addr::pTimer);
    }

    // Get the world from the current oCGame
    static zCWorld* GetWorldPtr() {
        auto* game = GetGamePtr();
        if (!game) return nullptr;
        return *reinterpret_cast<zCWorld**>(G_OFFSET(game, Addr::oGame_World));
    }

    // Active sky controller
    static zCSkyController_Outdoor* GetSkyControllerPtr() {
        auto* world = GetWorldPtr();
        if (!world) return nullptr;
        return *reinterpret_cast<zCSkyController_Outdoor**>(G_OFFSET(world, Addr::oWorld_SkyCtrl));
    }

    // Player NPC vob
    static oCNPC* GetPlayerPtr() {
        return *reinterpret_cast<oCNPC**>(Addr::Player);
    }

    // Camera vob (the vob the camera is attached to)
    static zCVob* GetCameraVobPtr() {
        auto* game = GetGamePtr();
        if (!game) return nullptr;
        return *reinterpret_cast<zCVob**>(G_OFFSET(game, Addr::oGame_CamVob));
    }

    // ---------------------------------------------------------------
    // Safe snapshot reads — copy data out of Gothic memory
    // ---------------------------------------------------------------

    // Read vob position from any zCVob*
    static VobPosition ReadVobPosition(const void* vob) {
        VobPosition vp = {};
        if (!vob) return vp;
        vp.valid = true;
        vp.pos.x = *reinterpret_cast<const float*>(G_OFFSET(vob, Addr::oVob_PosX));
        vp.pos.y = *reinterpret_cast<const float*>(G_OFFSET(vob, Addr::oVob_PosY));
        vp.pos.z = *reinterpret_cast<const float*>(G_OFFSET(vob, Addr::oVob_PosZ));
        vp.bbox  = *reinterpret_cast<const BBox3D*>(G_OFFSET(vob, Addr::oVob_BBox));
        vp.type  = *reinterpret_cast<const int*>(G_OFFSET(vob, Addr::oVob_Type));
        vp.alpha = *reinterpret_cast<const float*>(G_OFFSET(vob, Addr::oVob_Alpha));
        return vp;
    }

    // Read the vob's 4×4 world matrix
    static Float4x4 ReadVobWorldMatrix(const void* vob) {
        Float4x4 m = {};
        if (!vob) return m;
        m = *reinterpret_cast<const Float4x4*>(G_OFFSET(vob, Addr::oVob_WorldMatrix));
        return m;
    }

    // Player position (convenience)
    static VobPosition GetPlayerPosition() {
        return ReadVobPosition(GetPlayerPtr());
    }

    // Camera vob position
    static VobPosition GetCameraVobPosition() {
        return ReadVobPosition(GetCameraVobPtr());
    }

    // Camera info
    static CameraInfo GetCameraInfo() {
        CameraInfo ci = {};
        auto* cam = GetCameraPtr();
        if (!cam) return ci;
        ci.valid = true;
        ci.nearPlane = *reinterpret_cast<float*>(G_OFFSET(cam, Addr::oCam_NearPlane));
        ci.farPlane  = *reinterpret_cast<float*>(G_OFFSET(cam, Addr::oCam_FarPlane));
        using GetFovFn = void(__thiscall*)(zCCamera*, float&, float&);
        auto getFov = reinterpret_cast<GetFovFn>(Addr::fCam_GetFov);
        if (getFov) getFov(cam, ci.fovH, ci.fovV);
        ci.screenFadeEnabled = *reinterpret_cast<int*>(G_OFFSET(cam, Addr::oCam_ScreenFade)) != 0;
        ci.screenFadeColor   = *reinterpret_cast<DWORD*>(G_OFFSET(cam, Addr::oCam_ScreenFadeColor));
        ci.cinemaScopeEnabled = *reinterpret_cast<int*>(G_OFFSET(cam, Addr::oCam_CinemaScope)) != 0;
        return ci;
    }

    // Timer info
    static TimerInfo GetTimerInfo() {
        TimerInfo ti = {};
        auto* timer = GetTimerPtr();
        if (!timer) return ti;
        ti.factorMotion = *reinterpret_cast<float*>(G_OFFSET(timer, Addr::oTimer_FactorMotion));
        ti.frameTimeMs  = *reinterpret_cast<float*>(G_OFFSET(timer, Addr::oTimer_FrameTimeMs));
        ti.totalTimeMs  = *reinterpret_cast<float*>(G_OFFSET(timer, Addr::oTimer_TotalTimeMs));
        ti.frameTimeSec = *reinterpret_cast<float*>(G_OFFSET(timer, Addr::oTimer_FrameTimeSec));
        ti.totalTimeSec = *reinterpret_cast<float*>(G_OFFSET(timer, Addr::oTimer_TotalTimeSec));
        return ti;
    }

    // Delta time in seconds (convenience)
    static float GetDeltaTime() {
        auto* timer = GetTimerPtr();
        if (!timer) return 0.016f;
        return *reinterpret_cast<float*>(G_OFFSET(timer, Addr::oTimer_FrameTimeSec));
    }

    // Total time in seconds
    static float GetTotalTime() {
        auto* timer = GetTimerPtr();
        if (!timer) return 0.0f;
        return *reinterpret_cast<float*>(G_OFFSET(timer, Addr::oTimer_TotalTimeSec));
    }

    // FPS (from delta time)
    static float GetFPS() {
        float dt = GetDeltaTime();
        return (dt > 0.0001f) ? (1.0f / dt) : 0.0f;
    }

    // Is game paused / single-stepping?
    static bool IsPaused() {
        auto* game = GetGamePtr();
        if (!game) return false;
        return *reinterpret_cast<int*>(G_OFFSET(game, Addr::oGame_SingleStep)) != 0;
    }

    // ---------------------------------------------------------------
    // Sky / weather
    // ---------------------------------------------------------------
    static SkyInfo GetSkyInfo() {
        SkyInfo info = {};
        auto* sky = GetSkyControllerPtr();
        if (!sky) return info;

        info.valid = true;
        info.masterTime     = *reinterpret_cast<float*>(G_OFFSET(sky, Addr::oSky_MasterTime));
        info.lastMasterTime = *reinterpret_cast<float*>(G_OFFSET(sky, Addr::oSky_LastMasterTime));
        info.weather        = static_cast<EWeather>(*reinterpret_cast<int*>(G_OFFSET(sky, Addr::oSky_WeatherType)));
        info.rainWeight     = *reinterpret_cast<float*>(G_OFFSET(sky, Addr::oSky_RainWeight));
        info.timeStartRain  = *reinterpret_cast<float*>(G_OFFSET(sky, Addr::oSky_TimeStartRain));
        info.timeStopRain   = *reinterpret_cast<float*>(G_OFFSET(sky, Addr::oSky_TimeStopRain));
        info.renderLightning = *reinterpret_cast<int*>(G_OFFSET(sky, Addr::oSky_RenderLightning));
        info.rainingCounter  = *reinterpret_cast<int*>(G_OFFSET(sky, Addr::oSky_RainingCounter));
        info.overrideColor   = *reinterpret_cast<Float3*>(G_OFFSET(sky, Addr::oSky_OverrideColor));
        info.overrideFlag    = *reinterpret_cast<int*>(G_OFFSET(sky, Addr::oSky_OverrideFlag)) != 0;

        // Read master sky state
        auto* state = *reinterpret_cast<void**>(G_OFFSET(sky, Addr::oSky_MasterState));
        if (state) {
            info.masterState.time      = *reinterpret_cast<float*>(G_OFFSET(state, Addr::oSkyState_Time));
            info.masterState.polyColor = *reinterpret_cast<Float3*>(G_OFFSET(state, Addr::oSkyState_PolyColor));
            info.masterState.fogColor  = *reinterpret_cast<Float3*>(G_OFFSET(state, Addr::oSkyState_FogColor));
            info.masterState.domeColor0 = *reinterpret_cast<Float3*>(G_OFFSET(state, Addr::oSkyState_DomeColor0));
            info.masterState.domeColor1 = *reinterpret_cast<Float3*>(G_OFFSET(state, Addr::oSkyState_DomeColor1));
            info.masterState.fogDist   = *reinterpret_cast<float*>(G_OFFSET(state, Addr::oSkyState_FogDist));
        }

        return info;
    }

    // Day time as hours (0.0 .. 24.0)
    static float GetTimeOfDay() {
        auto* sky = GetSkyControllerPtr();
        if (!sky) return 12.0f;
        float t = *reinterpret_cast<float*>(G_OFFSET(sky, Addr::oSky_MasterTime));
        return t * 24.0f;
    }

    // Is it night?  (before 6:00 or after 20:00)
    static bool IsNight() {
        float h = GetTimeOfDay();
        return (h < 6.0f || h > 20.0f);
    }

    // Is it raining?
    static bool IsRaining() {
        auto* sky = GetSkyControllerPtr();
        if (!sky) return false;
        return *reinterpret_cast<float*>(G_OFFSET(sky, Addr::oSky_RainWeight)) > 0.01f;
    }

    // ---------------------------------------------------------------
    // Sun position calculation (matches GD3D11's algorithm)
    // ---------------------------------------------------------------
    static Float3 GetSunDirection() {
        auto* sky = GetSkyControllerPtr();
        if (!sky) return { 0.0f, 1.0f, 0.0f }; // straight up fallback

        float skyTime = *reinterpret_cast<float*>(G_OFFSET(sky, Addr::oSky_MasterTime));

        // Remap sky time to linear angle progression
        float remapped;
        if (skyTime >= 0.708f) {
            remapped = lerp(0.75f, 1.0f, (skyTime - 0.708f) / 0.292f);
        } else if (skyTime <= 0.292f) {
            remapped = lerp(0.0f, 0.25f, skyTime / 0.292f);
        } else if (skyTime >= 0.5f) {
            remapped = lerp(0.5f, 0.75f, (skyTime - 0.5f) / 0.208f);
        } else {
            remapped = lerp(0.25f, 0.5f, (skyTime - 0.292f) / 0.208f);
        }

        constexpr float PI2 = 6.28318530718f;
        constexpr float PI_2 = 1.57079632679f;
        float angle = remapped * PI2 + PI_2;

        // Rotate (-60, 0, 100) normalized around X-axis by -angle
        // Base direction
        float bx = -60.0f, by = 0.0f, bz = 100.0f;
        float len = sqrtf(bx*bx + by*by + bz*bz);
        bx /= len; by /= len; bz /= len;

        // Rotation around X-axis by -angle
        float cosA = cosf(-angle);
        float sinA = sinf(-angle);

        Float3 dir;
        dir.x = bx;
        dir.y = by * cosA - bz * sinA;
        dir.z = by * sinA + bz * cosA;
        return dir;
    }

    // ---------------------------------------------------------------
    // Debug log — print current game state to console
    // ---------------------------------------------------------------
    static void PrintState() {
        if (!Get().IsInGame()) {
            printf("[Gothic] Not in game\n");
            return;
        }
        auto player = GetPlayerPosition();
        auto cam = GetCameraInfo();
        auto sky = GetSkyInfo();
        auto timer = GetTimerInfo();

        printf("[Gothic] === Game State ===\n");
        printf("  Player: pos=(%.1f, %.1f, %.1f) type=%d\n",
               player.pos.x, player.pos.y, player.pos.z, player.type);
        printf("  Camera: near=%.1f far=%.1f fade=%d cinema=%d\n",
               cam.nearPlane, cam.farPlane, cam.screenFadeEnabled, cam.cinemaScopeEnabled);
        printf("  Timer:  dt=%.3fs total=%.1fs fps=%.1f paused=%d\n",
               timer.frameTimeSec, timer.totalTimeSec, GetFPS(), IsPaused());
        if (sky.valid) {
            printf("  Sky:    time=%.3f (%.1fh) weather=%d rain=%.2f lightning=%d\n",
                   sky.masterTime, sky.masterTime * 24.0f, sky.weather, sky.rainWeight, sky.renderLightning);
            printf("  Fog:    color=(%.0f,%.0f,%.0f) dist=%.0f\n",
                   sky.masterState.fogColor.x, sky.masterState.fogColor.y,
                   sky.masterState.fogColor.z, sky.masterState.fogDist);
        }
        Float3 sun = GetSunDirection();
        printf("  Sun:    dir=(%.2f, %.2f, %.2f)\n", sun.x, sun.y, sun.z);
        printf("  Night:  %s  Raining: %s\n", IsNight() ? "yes" : "no", IsRaining() ? "yes" : "no");
    }

private:
    Game() = default;

    static float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }
};

} // namespace Gothic


#pragma once
#include "game/SdkCompat.hpp"
#include "math/VMath.hpp"
#include <Windows.h>
#include <cstdint>

namespace Simulator {
class cGameData;
class cVehicle;
class cSpatialObject;
class cCity;
}

namespace App {
class cViewer;
}

namespace game {
extern uintptr_t gBase;

template <class T = void*> inline T Va(uint32_t va) {
    return reinterpret_cast<T>(gBase + va - 0x400000);
}

namespace raw {
constexpr uint32_t GameCivVtable = 0x1478978;
constexpr uint32_t CivUiListenerVtable = 0x14786fc;
constexpr uint32_t VehicleSpatialVtable = 0x1474be8;
constexpr uint32_t VehicleCombatantVtable = 0x1474ce8;
constexpr uint32_t CameraManagerVtable = 0x1410718;
constexpr uint32_t TerrainCursorVtable = 0x145ff70;
constexpr uint32_t GameViewManagerVtable = 0x1460970;

constexpr uint32_t ActOnTarget = 0xcf3d50;
constexpr uint32_t TradeOnSpice = 0xce8c30;
constexpr uint32_t ClearOrders = 0xcaaaa0;
constexpr uint32_t GameCivGet = 0xcf7620;
constexpr uint32_t PlanetCameraGet = 0xb3d380;
constexpr uint32_t PlanetCameraFlyTo = 0xb12d10;
constexpr uint32_t SetVelocity = 0xc41ec0;
constexpr uint32_t AudioSystemVtable = 0x1452ce8;
constexpr uint32_t SetListenerPosition = 0xa209b0;
constexpr uint32_t MinimapProject = 0xe0bc90;
constexpr uint32_t MinimapCameraImage = 0x250;
constexpr uint32_t UiImageVtable = 0x1440814;
constexpr uint32_t NounGetData = 0xb212d0;
constexpr uint32_t ClaimBandOuter = 0xdce54c;
constexpr uint32_t StartClaim = 0xbfe590;
constexpr uint32_t AddOrder = 0xcac1a0;
constexpr uint32_t AddOrderAt = 0xcac280;
constexpr uint32_t GameCivPosse = 0xec;
constexpr uint32_t PosseMinimap = 0xf0;
constexpr uint32_t CursorAttachmentVtable = 0x1481b84;
constexpr uint32_t UiControllerAttachment = 0xe4;
constexpr uint32_t RolloverRootWindow = 0x8281b0;
constexpr uint32_t NewAudioTrack = 0x436350;
constexpr uint32_t GameCivUiController = 0xe8;
}

enum : uint32_t {
    kSlotKeyDown = 0x24,
    kSlotKeyUp = 0x28,
    kSlotMouseDown = 0x2c,
    kSlotMouseUp = 0x30,
    kSlotMouseMove = 0x34,
    kSlotMouseWheel = 0x38,
    kSlotUpdate = 0x3c,
};

constexpr uint32_t kMsgPosseDoubleClick = 0x346048a;
constexpr uint32_t kMsgActOnObject = 0x51dab6f;
constexpr uint32_t kMsgSelectionChanged = 0x52f1544;

constexpr uint32_t kAppUpdate = 0x1EE100A;

bool Verify();
bool CursorHookUsable();
bool ListenerHookUsable();
bool OrderHookUsable();
bool StanceUsable();
uintptr_t ForeignHookTarget(uint32_t va);
bool ChainInFront(uint32_t va, void* hook);
uint32_t ImageVa(uintptr_t address);
uint32_t GameCallerVa(const uintptr_t* returnSlot);

bool InCiv();
uint32_t PlayerPoliticalID();
HWND GameWindow();
bool GameHasFocus();

Simulator::cVehicle* AsVehicle(Simulator::cGameData* obj);
Simulator::cVehicle* PlayerVehicle(Simulator::cGameData* obj);
Simulator::cSpatialObject* Spatial(Simulator::cVehicle* v);
bool VehicleAlive(Simulator::cVehicle* v);
void* Cast(void* object, uint32_t type);

vm::V3 Position(Simulator::cSpatialObject* s);
vm::Q Orientation(Simulator::cSpatialObject* s);
vm::V3 Direction(Simulator::cSpatialObject* s);
float BoundingRadius(Simulator::cSpatialObject* s);
bool IsSelected(Simulator::cSpatialObject* s);
void SetSelected(Simulator::cSpatialObject* s, bool on);
void StopMovement(Simulator::cSpatialObject* s);

void RawSetPosition(Simulator::cSpatialObject* s, const vm::V3& p);
void RawSetOrientation(Simulator::cSpatialObject* s, const vm::Q& q);

bool PlanetReady();
bool IsInWater(const vm::V3& p);
float HeightAt(const vm::V3& p);
vm::V3 ToSurface(const vm::V3& p);
vm::Q PlanetOrientation(const vm::V3& pos, const vm::V3& dir);

Simulator::cGameData* PickHovered();
void SelectOnly(Simulator::cSpatialObject* s);
int SelectedCount();
bool Act(Simulator::cVehicle* v, Simulator::cGameData* target, int key);
void ClearOrders(Simulator::cVehicle* v);
Simulator::cGameData* CurrentOrderTarget(Simulator::cVehicle* v);
int InWeaponRange(Simulator::cVehicle* v, Simulator::cGameData* target);
void PlanetCameraFlyTo(const vm::V3& pos, const vm::V3& dir, bool snap);
bool NearestCityDisc(const vm::V3& p, vm::V3& center, float& radius);
int CityObstacles(const vm::V3& p, vm::V3* centers, float* radii, int max);
bool WeaponRange(Simulator::cVehicle* v, float& minRange, float& maxRange);
void SetListener(int index, const vm::V3& pos, const vm::M3& rot);
bool MinimapPoint(const vm::V3& pos, float& sx, float& sy);

struct MinimapIcon {
    float u0, v0, u1, v1;
    float w, h;
};

bool MinimapCameraIcon(MinimapIcon& out);
void SetShadowFocus(const vm::V3& centre, const vm::V3& viewer);
void BeforeHud();
Simulator::cGameData* NearestTribeHut(const vm::V3& p, vm::V3& center, float& radius);
bool ReadGameFile(uint32_t instance, uint32_t type, uint32_t group, unsigned char*& data, unsigned& size);
float ClaimRadius();
bool StartClaim(Simulator::cGameData* source, Simulator::cVehicle* v);
void SuppressAutoCombat(Simulator::cVehicle* v, bool on);
Simulator::cVehicle* SelectedPlayerVehicle();
bool CityCapturable(Simulator::cGameData* hovered);
void PlayRefusal();
void HideCityRollover();
void WantCityRolloverHidden(bool on);
void HideCityRolloverIfWanted();
int ConvertDeltaMs(int realMs);

struct CamXf {
    vm::V3 pos;
    vm::M3 rot;
};

bool ReadViewer(App::cViewer* viewer, CamXf& out);
void WriteViewer(App::cViewer* viewer, const CamXf& xf);

bool SetNativeCursor(uint32_t id);
uint32_t ActiveCursorId();
bool MouseOverUI();

void NotifyMessage(uint32_t id);
Simulator::cGameData* MessageObject(void* msg);
int MessageKey(void* msg);

extern void* origSetPosition;
extern void* origSetOrientation;
extern void* origSetActiveCursor;
extern void* origSetVelocity;
extern void* origSetListener;
extern void* origAddOrder;
extern void* origAddOrderAt;
extern void* origVehSetTarget;
}


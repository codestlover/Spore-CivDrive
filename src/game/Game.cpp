
#include "game/Game.hpp"
#include "game/Callers.hpp"
#include <Spore/ModAPI.h>
#include <Spore/App/IMessageManager.h>
#include <Spore/App/cViewer.h>
#include <Spore/App/cCameraManager.h>
#include <Spore/App/Canvas.h>
#include <Spore/Simulator/cVehicle.h>
#include <Spore/Simulator/cCity.h>
#include <Spore/Simulator/cCityWalls.h>
#include <Spore/Simulator/cCivilization.h>
#include <Spore/Simulator/cCommodityNode.h>
#include <Spore/Simulator/cGameTerrainCursor.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameViewManager.h>
#include <Spore/Simulator/SubSystem/GameTimeManager.h>
#include <Spore/Simulator/SubSystem/PlanetModel.h>
#include <Spore/Simulator/SubSystem/CinematicManager.h>
#include <Spore/UTFWin/IWindowManager.h>
#include <Spore/UTFWin/IWindow.h>
#include <Spore/UTFWin/CursorManager.h>
#include <Spore/Audio/AudioSystem.h>
#include <Spore/UTFWin/Image.h>
#include <Spore/Graphics/IShadowWorld.h>
#include <Spore/Resource/IResourceManager.h>
#include <Spore/Resource/Database.h>
#include <Spore/Resource/IRecord.h>
#include <cmath>
#include <cstddef>
#include <cstring>

using namespace Simulator;

static_assert(sizeof(cVehicle) == 0xd98, "cVehicle ABI");
static_assert(offsetof(cVehicle, mLocomotion) == 0xb1c, "cVehicle::mLocomotion");
static_assert(offsetof(cVehicle, mPurpose) == 0xb20, "cVehicle::mPurpose");
static_assert(offsetof(cVehicle, mOrders) == 0xb68, "cVehicle::mOrders");
static_assert(offsetof(cVehicle, mIdlePosition) == 0xb80, "cVehicle::mIdlePosition");
static_assert(offsetof(cVehicle, mbDead) == 0xbcd, "cVehicle::mbDead");
static_assert(offsetof(cGameData, mbIsDestroyed) == 0x21, "cGameData::mbIsDestroyed");
static_assert(offsetof(cGameData, mPoliticalID) == 0x30, "cGameData::mPoliticalID");
static_assert(offsetof(cSpatialObject, mPosition) == 0x4, "cSpatialObject::mPosition");
static_assert(offsetof(cSpatialObject, mOrientation) == 0x10, "cSpatialObject::mOrientation");
static_assert(offsetof(cSpatialObject, mBoundingRadius) == 0x5c, "cSpatialObject::mBoundingRadius");
static_assert(offsetof(cSpatialObject, mbIsSelected) == 0x6c, "cSpatialObject::mbIsSelected");
static_assert(offsetof(cCity, mpCityWalls) == 0x324, "cCity::mpCityWalls");
static_assert(offsetof(cCityWalls, mpDock) == 0x10c, "cCityWalls::mpDock");
static_assert(offsetof(cCityWalls, mGatesTransformed) == 0x198, "cCityWalls::mGatesTransformed");
static_assert(offsetof(cCityWalls, mOuterRadius) == 0x25c, "cCityWalls::mOuterRadius");
static_assert(offsetof(cCivilization, mPoliticalID) == 0x30, "cCivilization political ID");
static_assert(offsetof(App::cCameraManager, mpViewer) == 0xb0, "cCameraManager::mpViewer");
static_assert(offsetof(App::cCameraManager, mnActiveIndex) == 0xa8, "cCameraManager::mnActiveIndex");
static_assert(offsetof(cGameModeManager, mActiveModeID) == 0x20, "cGameModeManager::mActiveModeID");

namespace game {
uintptr_t gBase = 0;
void* origSetPosition = nullptr;
void* origSetOrientation = nullptr;
void* origSetActiveCursor = nullptr;
void* origSetVelocity = nullptr;
void* origSetListener = nullptr;
void* origAddOrder = nullptr;
void* origAddOrderAt = nullptr;
void* origVehSetTarget = nullptr;

namespace {
template <class T> T Sdk(uintptr_t address) {
    return reinterpret_cast<T>(address);
}

inline void** Vtbl(const void* obj) {
    return *reinterpret_cast<void** const*>(obj);
}

bool HasSlot(const void* obj, uint32_t offset, uint32_t expectedVa) {
    if (!obj)
        return false;
    void** vt = *reinterpret_cast<void** const*>(obj);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!vt || !VirtualQuery(vt + offset / 4, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;
    return reinterpret_cast<uintptr_t>(vt[offset / 4]) == gBase + expectedVa - 0x400000;
}

template <class R, class... A> R VCall(const void* obj, uint32_t offset, A... args) {
    auto fn = reinterpret_cast<R(__thiscall*)(const void*, A...)>(Vtbl(obj)[offset / 4]);
    return fn(obj, args...);
}

bool Bytes(uint32_t va, const char* hex) {
    auto* p = Va<const unsigned char*>(va);
    size_t n = strlen(hex) / 2;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned v = 0;
        sscanf(hex + 2 * i, "%2x", &v);
        if (p[i] != v)
            return false;
    }
    return true;
}

bool Slot(uint32_t vtable, uint32_t offset, uint32_t expectedVa) {
    uintptr_t have = *Va<uintptr_t*>(vtable + offset);
    uintptr_t want = expectedVa ? uintptr_t(gBase + expectedVa - 0x400000) : 0;
    if (have != want)
        return false;
    return true;
}

bool ForeignOrBytes(uint32_t va, const char* hex) {
    return ForeignHookTarget(va) || Bytes(va, hex);
}

cGameNounManager* NounManager() {
    return Sdk<cGameNounManager* (*)()>(GetAddress(Simulator::cGameNounManager, Get))();
}

cCivilization* PlayerCiv() {
    auto* m = NounManager();
    if (!m)
        return nullptr;
    return Sdk<cCivilization*(__thiscall*)(cGameNounManager*)>(
        GetAddress(Simulator::cGameNounManager, GetPlayerCivilization))(m);
}

cPlanetModel* Planet() {
    return Sdk<cPlanetModel* (*)()>(GetAddress(Simulator::cPlanetModel, Get))();
}

cGameTerrainCursor* TerrainCursor() {
    return Sdk<cGameTerrainCursor* (*)()>(GetAddress(Simulator::cGameTerrainCursor, GetTerrainCursor))();
}

cGameViewManager* ViewManager() {
    return Sdk<cGameViewManager* (*)()>(GetAddress(Simulator::cGameViewManager, Get))();
}

App::IMessageManager* Messages() {
    return Sdk<App::IMessageManager* (*)()>(GetAddress(App::IMessageManager, Get))();
}

App::Canvas* Canvas() {
    return Sdk<App::Canvas* (*)()>(GetAddress(App::Canvas, Get))();
}

UTFWin::IWindowManager* Windows() {
    return Sdk<UTFWin::IWindowManager* (*)()>(GetAddress(UTFWin::IWindowManager, Get))();
}

void* CursorMgr() {
    return Sdk<void* (*)()>(GetAddress(UTFWin::cCursorManager, Get))();
}

Math::Vector3 ToSdk(const vm::V3& v) {
    Math::Vector3 r;
    r.x = v.x;
    r.y = v.y;
    r.z = v.z;
    return r;
}

vm::V3 FromSdk(const Math::Vector3& v) {
    return {v.x, v.y, v.z};
}

bool terrainCursorOk = false, viewManagerOk = false, cursorOk = false, rangeOk = false, audioOk = false,
     minimapOk = false, rolloverOk = false, soundOk = false, listenerHookOk = false, minimapCameraOk = false,
     tribeOk = false, shadowOk = false, orderHookOk = false, claimOk = false, stanceOk = false, vehiclesOk = false,
     cinematicOk = false, cityAttackOk = false, cityEditorOk = false;
}

bool CursorHookUsable() {
    return cursorOk;
}

bool ListenerHookUsable() {
    return listenerHookOk;
}

bool OrderHookUsable() {
    return orderHookOk;
}

bool StanceUsable() {
    return stanceOk;
}

size_t ImageSize() {
    static size_t size = 0;
    if (!size) {
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(gBase);
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(gBase + dos->e_lfanew);
        size = nt->OptionalHeader.SizeOfImage;
    }
    return size;
}

bool InImage(uintptr_t address) {
    return address >= gBase && address - gBase < ImageSize();
}

uintptr_t ForeignHookTarget(uint32_t va) {
    auto* p = Va<const unsigned char*>(va);
    if (p[0] != 0xE9)
        return 0;
    uintptr_t target = uintptr_t(p) + 5 + *reinterpret_cast<const int32_t*>(p + 1);
    if (InImage(target))
        return 0;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(target), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return 0;
    return target;
}

bool ChainInFront(uint32_t va, void* hook) {
    auto* p = Va<unsigned char*>(va);
    DWORD old = 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    int32_t rel = int32_t(reinterpret_cast<uintptr_t>(hook) - (uintptr_t(p) + 5));
    std::memcpy(p + 1, &rel, 4);
    p[0] = 0xE9;
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

uint32_t ImageVa(uintptr_t address) {
    return uint32_t(address - gBase + 0x400000);
}

uint32_t GameCallerVa(const uintptr_t* returnSlot) {
    uintptr_t caller = callers::GameCaller(returnSlot, gBase, ImageSize());
    return caller ? ImageVa(caller) : 0;
}

bool Verify() {
    gBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    bool ok = true;
    ok &= Slot(raw::GameCivVtable, kSlotKeyDown, 0xcfacf0);
    ok &= Slot(raw::GameCivVtable, kSlotKeyUp, 0xd1c090);
    ok &= Slot(raw::GameCivVtable, kSlotMouseDown, 0xcf74e0);
    ok &= Slot(raw::GameCivVtable, kSlotMouseUp, 0xcf7580);
    ok &= Slot(raw::GameCivVtable, kSlotMouseMove, 0xcf7530);
    ok &= Slot(raw::GameCivVtable, kSlotMouseWheel, 0xcfac10);
    ok &= Slot(raw::GameCivVtable, kSlotUpdate, 0xcfe2c0);
    ok &= Slot(raw::CivUiListenerVtable, 0x04, 0xcf56a0);
    ok &= Bytes(0xcf56a0, "81ecd4000000535556578bbc24e80000");
    ok &= Slot(raw::VehicleSpatialVtable, 0x38, 0xc9fdf0);
    ok &= Slot(raw::VehicleSpatialVtable, 0x3c, 0xc9fed0);
    ok &= Slot(raw::VehicleSpatialVtable, 0x50, 0xad26e0);
    ok &= Slot(raw::VehicleSpatialVtable, 0x54, 0xc9f8e0);
    ok &= Slot(raw::VehicleSpatialVtable, 0x5c, 0xc891b0);
    ok &= Slot(raw::VehicleSpatialVtable, 0xec, 0xc43800);
    rangeOk = Slot(raw::VehicleCombatantVtable, 0x40, 0xbfc8e0) && Slot(raw::VehicleCombatantVtable, 0x14, 0xc9f370);
    ok &= ForeignOrBytes(raw::SetVelocity, "8b4424048b108991c80100008b500489");
    ok &= Bytes(0xbec240, "568bf18b46348b502c8d4e34ffd28b4c");
    audioOk = Slot(raw::AudioSystemVtable, 0x0c, 0xa209b0) && Slot(raw::AudioSystemVtable, 0x10, 0xa20c90);
    listenerHookOk = audioOk && ForeignOrBytes(raw::SetListenerPosition, "538b5c2408568bf1578b7c24148b0f8d045b");
    minimapOk = Bytes(0xcf7660, "8b81ec000000c3") && Bytes(0xceb722, "8b8bf0000000") &&
                Bytes(raw::MinimapProject, "83ec0856578bf9e8b417d3ff85c07405");
    minimapCameraOk = minimapOk && Bytes(0xe0d7fe, "8bb650020000") && Bytes(0xe0d84c, "db461c") &&
                      Slot(raw::UiImageVtable, 0x0c, 0x9574d0);
    tribeOk = Bytes(0xbf8876, "68aedae4016840e4b10068406fbf0068d0d3d30068907ecd00") &&
              Bytes(0xbf88c9, "8b138b42588bcbffd080b857050000") && Bytes(raw::NounGetData, "83ec10535657");
    shadowOk = uintptr_t(GetAddress(Graphics::IShadowWorld, Get)) == Va<uintptr_t>(0x67dd80) &&
               Bytes(0xcfe57f, "8b178b424c8bcfffd03d921be505") &&
               Bytes(0xcfe631, "8b078b403883c4088d4c2420518d542418528bcfffd0") &&
               Bytes(0xcfe679, "8b178b523c8d44243c508bcfffd2") && Bytes(0xcfe6b0, "8b078b500c8bcfffd2");
    claimOk = Bytes(raw::ClaimBandOuter, "d905") && Bytes(0xdce552, "8b47348b80e8000000") &&
              ForeignOrBytes(raw::StartClaim, "8b442404538b5c240c568bf157c78610") && Bytes(0xbfe5c3, "899ee0010000") &&
              Bytes(0xbfe604, "c20800");
    stanceOk = Bytes(0xc9fc00, "8b4424048981240b00008b81f00a000081a0fc050000fffcffff") &&
               Slot(raw::VehicleCombatantVtable, 0x50, 0xc9e8f0);
    vehiclesOk = Bytes(0xae7330, "68e86d8c016840e4b100685072ae0068d0d3d30068907ecd00");
    orderHookOk = ForeignOrBytes(raw::AddOrder, "83ec1855578bf9e8b4b1ffff") &&
                  ForeignOrBytes(raw::AddOrderAt, "83ec1856578bf1e814e8ffff");
    rolloverOk = Bytes(0xcf4f35, "8986e4000000") && Bytes(0xe35796, "8b4b1089b1a8000000") &&
                 Bytes(raw::RolloverRootWindow, "8b41486a015083c10ce89284feffc3cc") &&
                 Slot(raw::CursorAttachmentVtable, 0x10, 0xe35320);
    cityAttackOk = orderHookOk && Bytes(0xbd9d00, "53568bb140030000578bb94403000033") &&
                   Bytes(0xbd9cc0, "53568bb154030000578bb95803000033") &&
                   Bytes(0xc9efc0, "8b4424048981280d00008981240d0000") && Bytes(0xdcd3ef, "8b471033f683e802") &&
                   Bytes(0xdce930, "6832229bee");
    cityEditorOk = Bytes(0xcfe55a, "8b8ee80000008b898000000085c9740653") && Bytes(0xd124e5, "8bd9837b400056570f84") &&
                   Bytes(0xcf4bbc, "89b780000000");
    cinematicOk = uintptr_t(GetAddress(Simulator::cCinematicManager, Get)) == Va<uintptr_t>(0xb3d5d0) &&
                  Bytes(0xe35341, "8b402c83f801740983f802");
    soundOk = Bytes(raw::NewAudioTrack, "558bec83ec08e815a35e008945fc837d") &&
              uintptr_t(GetAddress(Audio, PlayAudio)) == Va<uintptr_t>(0x436390);

    ok &= Slot(raw::CameraManagerVtable, 0x2c, 0x7c6440);
    if (uintptr_t(GetAddress(App::cCameraManager, Update)) != Va<uintptr_t>(0x7c6440))
        ok = false;
    ok &= Bytes(raw::ActOnTarget, "83ec3c53558b6c2448565733ff894c24");
    ok &= Bytes(raw::TradeOnSpice, "83ec1c53578bd933ff80bb3501000000");
    ok &= Bytes(raw::ClearOrders, "56578bf98b8ff00a00006a0068f64f00");
    ok &= Bytes(raw::GameCivGet, "a1c8d26901c3");
    ok &= Bytes(raw::PlanetCameraGet, "a1ccea6701c3");
    ok &= Bytes(raw::PlanetCameraFlyTo, "83ec2055568bf1e8445d43008be885ed");
    terrainCursorOk = Slot(raw::TerrainCursorVtable, 0x64, 0xb2ffd0) &&
                      Slot(raw::TerrainCursorVtable, 0x70, 0xb2fd00) && Slot(raw::TerrainCursorVtable, 0x74, 0xb300f0);
    viewManagerOk = Slot(raw::GameViewManagerVtable, 0x34, 0xb38f50);
    ok &= terrainCursorOk && viewManagerOk;
    cursorOk = uintptr_t(GetAddress(UTFWin::cCursorManager, SetActiveCursor)) == Va<uintptr_t>(0x801cf0) &&
               ForeignOrBytes(0x801cf0, "518b44240856578bf98d4c2410518d54");
    return ok;
}

bool InCiv() {
    auto* m = Sdk<cGameModeManager* (*)()>(GetAddress(Simulator::cGameModeManager, Get))();
    return m && m->mActiveModeID == kGameCiv;
}

uint32_t PlayerPoliticalID() {
    auto* c = PlayerCiv();
    return c ? c->mPoliticalID : uint32_t(-1);
}

HWND GameWindow() {
    auto* c = Canvas();
    return c ? VCall<HWND>(c, 0x94) : nullptr;
}

bool GameHasFocus() {
    HWND w = GameWindow();
    HWND f = GetForegroundWindow();
    if (!f)
        return false;
    if (w && (f == w || IsChild(f, w) || GetAncestor(w, GA_ROOT) == f))
        return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(f, &pid);
    return pid == GetCurrentProcessId();
}

void* Cast(void* object, uint32_t type) {
    return object ? VCall<void*>(object, 0x0c, type) : nullptr;
}

cVehicle* AsVehicle(cGameData* obj) {
    return static_cast<cVehicle*>(Cast(obj, cVehicle::TYPE));
}

cSpatialObject* Spatial(cVehicle* v) {
    return v ? static_cast<cSpatialObject*>(static_cast<cLocomotiveObject*>(v)) : nullptr;
}

bool VehicleAlive(cVehicle* v) {
    return v && !v->mbIsDestroyed && !v->mbDead;
}

cVehicle* PlayerVehicle(cGameData* obj) {
    auto* v = AsVehicle(obj);
    uint32_t me = PlayerPoliticalID();
    if (!v || me == uint32_t(-1) || v->cGameData::mPoliticalID != me || !VehicleAlive(v))
        return nullptr;
    if (*reinterpret_cast<uintptr_t*>(Spatial(v)) != Va<uintptr_t>(raw::VehicleSpatialVtable))
        return nullptr;
    return v;
}

vm::V3 Position(cSpatialObject* s) {
    return FromSdk(s->mPosition);
}

vm::Q Orientation(cSpatialObject* s) {
    const auto& q = s->mOrientation;
    return {q.x, q.y, q.z, q.w};
}

vm::V3 Direction(cSpatialObject* s) {
    Math::Vector3 out;
    VCall<Math::Vector3*>(s, 0x5c, &out);
    return FromSdk(out);
}

float BoundingRadius(cSpatialObject* s) {
    return s->mBoundingRadius;
}

bool IsSelected(cSpatialObject* s) {
    return s->mbIsSelected;
}

void SetSelected(cSpatialObject* s, bool on) {
    VCall<void>(s, 0x54, on);
}

void StopMovement(cSpatialObject* s) {
    VCall<void>(s, 0xec);
}

void RawSetPosition(cSpatialObject* s, const vm::V3& p) {
    Math::Vector3 v = ToSdk(p);
    reinterpret_cast<void(__thiscall*)(cSpatialObject*, const Math::Vector3*)>(origSetPosition)(s, &v);
}

void RawSetOrientation(cSpatialObject* s, const vm::Q& q) {
    Math::Quaternion v;
    v.x = q.x;
    v.y = q.y;
    v.z = q.z;
    v.w = q.w;
    reinterpret_cast<void(__thiscall*)(cSpatialObject*, const Math::Quaternion*)>(origSetOrientation)(s, &v);
}

bool PlanetReady() {
    return Planet() != nullptr;
}

bool IsInWater(const vm::V3& p) {
    auto* pm = Planet();
    if (!pm)
        return false;
    Math::Vector3 v = ToSdk(p);
    return Sdk<bool(__thiscall*)(cPlanetModel*, const Math::Vector3*)>(GetAddress(Simulator::cPlanetModel, IsInWater))(
        pm, &v);
}

float HeightAt(const vm::V3& p) {
    auto* pm = Planet();
    if (!pm)
        return vm::Len(p);
    Math::Vector3 v = ToSdk(p);
    return Sdk<float(__thiscall*)(cPlanetModel*, const Math::Vector3*)>(
        GetAddress(Simulator::cPlanetModel, GetHeightAt))(pm, &v);
}

vm::V3 ToSurface(const vm::V3& p) {
    auto* pm = Planet();
    if (!pm)
        return p;
    Math::Vector3 in = ToSdk(p), out;
    Sdk<Math::Vector3*(__thiscall*)(cPlanetModel*, Math::Vector3*, const Math::Vector3*)>(
        GetAddress(Simulator::cPlanetModel, ToSurface))(pm, &out, &in);
    vm::V3 r = FromSdk(out);
    return vm::Finite(r) ? r : p;
}

vm::Q PlanetOrientation(const vm::V3& pos, const vm::V3& dir) {
    auto* pm = Planet();
    vm::Q r;
    if (!pm)
        return r;
    Math::Vector3 p = ToSdk(pos), d = ToSdk(dir);
    Math::Quaternion q;
    Sdk<Math::Quaternion*(__thiscall*)(cPlanetModel*, Math::Quaternion*, const Math::Vector3*, const Math::Vector3*)>(
        GetAddress(Simulator::cPlanetModel, GetOrientation))(pm, &q, &p, &d);
    return {q.x, q.y, q.z, q.w};
}

cGameData* PickHovered() {
    auto* vmgr = ViewManager();
    if (!vmgr || !viewManagerOk || !HasSlot(vmgr, 0x34, 0xb38f50))
        return nullptr;
    return VCall<cGameData*>(vmgr, 0x34);
}

int SelectedCount() {
    auto* tc = TerrainCursor();
    if (!tc || !terrainCursorOk || !HasSlot(tc, 0x70, 0xb2fd00))
        return -1;
    return VCall<int>(tc, 0x70);
}

void SelectOnly(cSpatialObject* s) {
    auto* tc = TerrainCursor();
    if (!tc || !terrainCursorOk || !HasSlot(tc, 0x64, 0xb2ffd0) || !HasSlot(tc, 0x74, 0xb300f0))
        return;
    VCall<void>(tc, 0x64);
    SetSelected(s, true);
    VCall<int>(tc, 0x74);
    NotifyMessage(kMsgSelectionChanged);
}

bool Act(cVehicle* v, cGameData* target, int key) {
    if (!v || !target)
        return false;
    auto* civ = Va<void* (*)()>(raw::GameCivGet)();
    if (!civ)
        return false;
    void* controller = *reinterpret_cast<void**>(static_cast<char*>(civ) + raw::GameCivUiController);
    if (!controller)
        return false;
    auto* node = static_cast<cGameData*>(Cast(target, cCommodityNode::TYPE));
    if (node && node->mPoliticalID != uint32_t(-1) && v->mPurpose == kVehicleEconomic) {
        Va<int(__thiscall*)(void*, cGameData*, int)>(raw::TradeOnSpice)(controller, target, 0xb);
        return true;
    }
    Va<int(__thiscall*)(void*, cGameData*, int)>(raw::ActOnTarget)(controller, target, key);
    return true;
}

void ClearOrders(cVehicle* v) {
    if (v)
        Va<void(__thiscall*)(cVehicle*)>(raw::ClearOrders)(v);
}

cGameData* CurrentOrderTarget(cVehicle* v) {
    auto* raw = reinterpret_cast<char*>(v);
    auto* begin = *reinterpret_cast<char**>(raw + 0xb68);
    auto* end = *reinterpret_cast<char**>(raw + 0xb6c);
    if (!begin || begin >= end)
        return nullptr;
    return *reinterpret_cast<cGameData**>(begin);
}

int InWeaponRange(cVehicle* v, cGameData* target) {
    if (!rangeOk || !v || !target || !v->mpWeapon)
        return -1;
    auto* other = static_cast<cCombatant*>(Cast(target, cCombatant::TYPE));
    if (!other)
        return -1;
    auto* self = static_cast<cCombatant*>(v);
    if (*reinterpret_cast<uintptr_t*>(self) != Va<uintptr_t>(raw::VehicleCombatantVtable))
        return -1;
    return VCall<bool>(self, 0x40, other) ? 1 : 0;
}

void PlanetCameraFlyTo(const vm::V3& pos, const vm::V3& dir, bool snap) {
    void* cam = Va<void* (*)()>(raw::PlanetCameraGet)();
    if (!cam || !PlanetReady())
        return;
    vm::Q q = PlanetOrientation(pos, dir);
    Math::Vector3 p = ToSdk(pos);
    Math::Quaternion mq;
    mq.x = q.x;
    mq.y = q.y;
    mq.z = q.z;
    mq.w = q.w;
    Va<void(__thiscall*)(void*, const Math::Vector3*, const Math::Quaternion*, int)>(raw::PlanetCameraFlyTo)(
        cam, &p, &mq, snap ? 1 : 0);
}

int CityObstacles(const vm::V3& p, vm::V3* centers, float* radii, int max) {
    vm::V3 c;
    float r;
    if (max < 1 || !NearestCityDisc(p, c, r))
        return 0;
    int n = 0;
    centers[n] = c;
    radii[n++] = r;
    auto* pm = Planet();
    Math::Vector3 v = ToSdk(p);
    auto* city = Sdk<cCity*(__thiscall*)(cPlanetModel*, const Math::Vector3*)>(
        GetAddress(Simulator::cPlanetModel, GetNearestCity))(pm, &v);
    auto* walls = city ? city->mpCityWalls.get() : nullptr;
    if (!walls)
        return n;
    const auto& g = walls->mGatesTransformed;
    if (g.size() <= 8)
        for (const auto& gp : g) {
            vm::V3 q = FromSdk(gp);
            if (!vm::Finite(q) || vm::Len(q - c) > r * 2.5f || n >= max)
                continue;
            centers[n] = q;
            radii[n++] = 10.0f;
        }
    if (auto* dock = walls->mpDock.get())
        if (auto* ds = static_cast<cSpatialObject*>(Cast(dock, 0x1186577))) {
            vm::V3 q = FromSdk(ds->mPosition);
            float br = ds->mBoundingRadius;
            if (vm::Finite(q) && vm::Len(q - c) < r * 3.0f && br > 1.0f && n < max) {
                centers[n] = q;
                radii[n++] = br < 60.0f ? br : 60.0f;
            }
        }
    return n;
}

bool NearestCityDisc(const vm::V3& p, vm::V3& center, float& radius) {
    auto* pm = Planet();
    if (!pm)
        return false;
    Math::Vector3 v = ToSdk(p);
    auto* city = Sdk<cCity*(__thiscall*)(cPlanetModel*, const Math::Vector3*)>(
        GetAddress(Simulator::cPlanetModel, GetNearestCity))(pm, &v);
    if (!city)
        return false;
    if (auto* walls = reinterpret_cast<char*>(city->mpCityWalls.get())) {
        auto* ws = reinterpret_cast<cSpatialObject*>(walls + 0x34);
        center = FromSdk(ws->mPosition);
        radius = *reinterpret_cast<float*>(walls + 0x25c);
        if (!(radius > 5.0f && radius < 400.0f))
            radius = 37.0f;
    } else {
        auto* cs = static_cast<cSpatialObject*>(city);
        center = FromSdk(cs->mPosition);
        radius = cs->mBoundingRadius > 5.0f && cs->mBoundingRadius < 400.0f ? cs->mBoundingRadius * 0.8f : 40.0f;
    }
    return vm::Finite(center);
}

bool WeaponRange(cVehicle* v, float& minRange, float& maxRange) {
    if (!rangeOk || !v)
        return false;
    auto* self = static_cast<cCombatant*>(v);
    if (*reinterpret_cast<uintptr_t*>(self) != Va<uintptr_t>(raw::VehicleCombatantVtable))
        return false;
    auto* weapon = VCall<char*>(self, 0x14);
    if (!weapon)
        return false;
    float mul = *reinterpret_cast<float*>(weapon + 0x148);
    minRange = *reinterpret_cast<float*>(weapon + 0x1c8) * mul;
    maxRange = *reinterpret_cast<float*>(weapon + 0x1cc) * mul;
    return std::isfinite(maxRange) && maxRange > 0.5f && maxRange < 5000.0f;
}

void SetListener(int index, const vm::V3& pos, const vm::M3& rot) {
    if (!audioOk || !vm::Finite(pos))
        return;
    void* audio = Sdk<void* (*)()>(GetAddress(Audio::AudioSystem, Get))();
    if (!audio || !HasSlot(audio, 0x0c, 0xa209b0))
        return;
    Math::Vector3 v = ToSdk(pos);
    float m[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[i * 3 + j] = rot.m[i][j];
    VCall<void>(audio, 0x0c, index, static_cast<const Math::Vector3*>(&v), static_cast<const float*>(m));
}

namespace {
char* CivMinimap(char** posseOut = nullptr) {
    auto* civ = static_cast<char*>(Va<void* (*)()>(raw::GameCivGet)());
    auto* posse = civ ? *reinterpret_cast<char**>(civ + raw::GameCivPosse) : nullptr;
    if (posseOut)
        *posseOut = posse;
    return posse ? *reinterpret_cast<char**>(posse + raw::PosseMinimap) : nullptr;
}
}

bool MinimapCameraIcon(MinimapIcon& out) {
    if (!minimapCameraOk || !PlanetReady())
        return false;
    char* minimap = CivMinimap();
    auto* image = minimap ? *reinterpret_cast<char**>(minimap + raw::MinimapCameraImage) : nullptr;
    if (!image)
        return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(image, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;
    uintptr_t vt = *reinterpret_cast<uintptr_t*>(image);
    const float* tc = reinterpret_cast<const float*>(image + 0x0c);
    const int* dim = reinterpret_cast<const int*>(image + 0x1c);
    out = {tc[0], tc[1], tc[2], tc[3], float(dim[0]) * 0.8f, float(dim[1]) * 0.8f};
    bool ok = (vt == Va<uintptr_t>(raw::UiImageVtable) || HasSlot(image, 0x0c, 0x9574d0)) && out.u0 >= 0 &&
              out.v0 >= 0 && out.u1 <= 1.001f && out.v1 <= 1.001f && out.u1 > out.u0 && out.v1 > out.v0 && out.w >= 2 &&
              out.w <= 128 && out.h >= 2 && out.h <= 128;
    return ok;
}

void SetShadowFocus(const vm::V3& p, const vm::V3& viewer) {
    if (!shadowOk || !vm::Finite(p) || vm::Len(p) < 1.0f || !vm::Finite(viewer))
        return;
    void* shadow = Sdk<void* (*)()>(GetAddress(Graphics::IShadowWorld, Get))();
    if (!shadow || VCall<uint32_t>(shadow, 0x4c) != 0x5E51B92)
        return;
    Math::Vector3 eye = ToSdk(viewer);
    Math::Vector3 centre = ToSdk(p);
    Math::Vector3 up = ToSdk(vm::Norm(p));
    VCall<void>(shadow, 0x3c, static_cast<const Math::Vector3*>(&eye));
    VCall<void>(shadow, 0x38, static_cast<const Math::Vector3*>(&centre), static_cast<const Math::Vector3*>(&up));
    VCall<void>(shadow, 0x0c);
}

void BeforeHud() {
    HideCityRolloverIfWanted();
}

cGameData* NearestTribeHut(const vm::V3& p, vm::V3& center, float& radius) {
    if (!tribeOk || !PlanetReady())
        return nullptr;
    auto* nouns = NounManager();
    if (!nouns)
        return nullptr;
    auto* list = Va<char*(__thiscall*)(void*, void*, void*, void*, void*, uint32_t)>(raw::NounGetData)(
        nouns, Va<void*>(0xcd7e90), Va<void*>(0xd3d3d0), Va<void*>(0xbf6f40), Va<void*>(0xb1e440), 0x1E4DAAE);
    if (!list)
        return nullptr;
    auto** begin = *reinterpret_cast<cGameData***>(list + 4);
    auto** end = *reinterpret_cast<cGameData***>(list + 8);
    cGameData* best = nullptr;
    float bestDist = 1e30f;
    for (auto** it = begin; it && it < end; ++it) {
        cGameData* hut = *it;
        if (!hut || hut->mbIsDestroyed)
            continue;
        auto* tribe = VCall<char*>(hut, 0x58);
        if (tribe && tribe[0x557])
            continue;
        auto* sp = static_cast<cSpatialObject*>(Cast(hut, 0x1186577));
        if (!sp)
            continue;
        vm::V3 c = FromSdk(sp->mPosition);
        float d = vm::Len(c - p);
        if (d < bestDist) {
            bestDist = d;
            best = hut;
            center = c;
            radius = sp->mBoundingRadius > 0.5f && sp->mBoundingRadius < 100.0f ? sp->mBoundingRadius : 6.0f;
        }
    }
    return best;
}

bool ReadGameFile(uint32_t instance, uint32_t type, uint32_t group, unsigned char*& data, unsigned& size) {
    data = nullptr;
    size = 0;
    auto* rm = Sdk<Resource::IResourceManager* (*)()>(GetAddress(Resource::IResourceManager, Get))();
    if (!rm)
        return false;
    const ResourceKey key(instance, type, group);
    Resource::Database* db = rm->FindRecord(key, nullptr, nullptr);
    Resource::IRecord* rec = nullptr;
    if (!db || !db->OpenRecord(key, &rec, IO::AccessFlags::Read) || !rec)
        return false;
    bool ok = false;
    if (IO::IStream* stream = rec->GetStream()) {
        unsigned n = unsigned(stream->GetSize());
        if (n > 0 && n < (8u << 20)) {
            data = new unsigned char[n];
            stream->SetPosition(0);
            ok = stream->Read(data, n) == int(n);
            if (ok) {
                size = n;
            } else {
                delete[] data;
                data = nullptr;
            }
        }
    }
    rec->RecordClose();
    rec->Release();
    return ok;
}

bool MinimapPoint(const vm::V3& pos, float& sx, float& sy) {
    if (!minimapOk || !PlanetReady())
        return false;
    char* minimap = CivMinimap();
    if (!minimap)
        return false;
    auto* window = reinterpret_cast<UTFWin::IWindow*>(minimap + 4);
    for (auto* w = window; w; w = w->GetParent())
        if (!w->IsVisible())
            return false;
    Math::Vector3 dir = ToSdk(vm::Norm(pos));
    float local[2] = {0, 0};
    if (!Va<bool(__thiscall*)(void*, const Math::Vector3*, float*, void*)>(raw::MinimapProject)(minimap, &dir, local,
                                                                                                nullptr)) {
        return false;
    }
    const auto& area = window->GetRealArea();
    if (!(local[0] >= -2 && local[1] >= -2 && local[0] <= area.GetWidth() + 2 && local[1] <= area.GetHeight() + 2))
        return false;
    Math::Point global = window->ToGlobalCoordinates(Math::Point(local[0], local[1]));
    sx = global.x;
    sy = global.y;
    if (auto* wm = Windows()) {
        auto* hit = wm->GetWindowAtPosition(Math::Point(sx, sy));
        auto* main = wm->GetMainWindow();
        bool related = !hit || hit == main;
        for (auto* w = hit; w && !related; w = w->GetParent())
            related = w == window;
        for (auto* w = window; w && !related; w = w->GetParent())
            related = w == hit;
        if (!related && main) {
            const auto& a = hit->GetRealArea();
            const auto& m = main->GetRealArea();
            if (a.GetWidth() * a.GetHeight() > 0.4f * m.GetWidth() * m.GetHeight())
                return false;
        }
    }
    return true;
}

float ClaimRadius() {
    if (!claimOk)
        return 24.0f;
    auto* at = *Va<const float**>(raw::ClaimBandOuter + 2);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!at || !VirtualQuery(at, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return 24.0f;
    float r = *at;
    return std::isfinite(r) && r >= 2.0f && r <= 500.0f ? r : 24.0f;
}

bool StartClaim(cGameData* source, cVehicle* v) {
    if (!claimOk || !source || !v || !VehicleAlive(v))
        return false;
    auto* node = static_cast<cCommodityNode*>(Cast(source, cCommodityNode::TYPE));
    if (!node || node->mbIsDestroyed || node->cGameData::mPoliticalID != uint32_t(-1) || node->mMineState != 0 ||
        node->mConstructingVehicle.get())
        return false;
    Va<void(__thiscall*)(void*, uint32_t, cVehicle*)>(raw::StartClaim)(node, v->cGameData::mPoliticalID, v);
    return node->mConstructingVehicle.get() == v;
}

void SuppressAutoCombat(cVehicle* v, bool on) {
    if (!stanceOk || !v)
        return;
    auto* raw = reinterpret_cast<char*>(v);
    if (!on) {
        Va<void(__thiscall*)(cVehicle*, uint32_t, int)>(0xc9fc00)(v, v->mStance, 1);
        return;
    }
    if (auto* bt = *reinterpret_cast<char**>(raw + 0xaf0))
        *reinterpret_cast<uint32_t*>(bt + 0x5fc) &= ~0x300u;
    if (CurrentOrderTarget(v))
        return;
    auto* self = static_cast<cCombatant*>(v);
    if (self->mpTarget && origVehSetTarget)
        reinterpret_cast<void(__thiscall*)(cCombatant*, cCombatant*)>(origVehSetTarget)(self, nullptr);
}

cVehicle* SelectedPlayerVehicle() {
    if (!vehiclesOk || !PlanetReady())
        return nullptr;
    auto* nouns = NounManager();
    if (!nouns)
        return nullptr;
    auto* list = Va<char*(__thiscall*)(void*, void*, void*, void*, void*, uint32_t)>(raw::NounGetData)(
        nouns, Va<void*>(0xcd7e90), Va<void*>(0xd3d3d0), Va<void*>(0xae7250), Va<void*>(0xb1e440), 0x18C6DE8);
    if (!list)
        return nullptr;
    auto** begin = *reinterpret_cast<cVehicle***>(list + 4);
    auto** end = *reinterpret_cast<cVehicle***>(list + 8);
    for (auto** it = begin; it && it < end; ++it) {
        cVehicle* v = PlayerVehicle(*it);
        if (v && IsSelected(Spatial(v)))
            return v;
    }
    return nullptr;
}

bool IsCityHall(cGameData* object) {
    return object && Cast(object, 0x1007AE63);
}

cCity* CityOf(cGameData* object) {
    auto* sp = object ? static_cast<cSpatialObject*>(Cast(object, 0x1186577)) : nullptr;
    auto* pm = Planet();
    if (!sp || !pm)
        return nullptr;
    Math::Vector3 v = sp->mPosition;
    return Sdk<cCity*(__thiscall*)(cPlanetModel*, const Math::Vector3*)>(
        GetAddress(Simulator::cPlanetModel, GetNearestCity))(pm, &v);
}

bool CityDefenseless(cCity* city) {
    if (!cityAttackOk || !city || city->cGameData::mbIsDestroyed)
        return false;
    int buildings = Va<int(__thiscall*)(cCity*)>(0xbd9d00)(city);
    int turrets = Va<int(__thiscall*)(cCity*)>(0xbd9cc0)(city);
    return buildings < 2 && turrets < 1;
}

bool AttackCity(cVehicle* v, cCity* city) {
    if (!cityAttackOk || !v || !city || !VehicleAlive(v))
        return false;
    Va<void(__thiscall*)(cVehicle*, cGameData*, int, int)>(raw::AddOrder)(v, city, 2, 1);
    Va<void(__thiscall*)(cVehicle*, int)>(0xc9efc0)(v, 0);
    return CurrentOrderTarget(v) == city;
}

bool CityEditorOpen() {
    if (!cityEditorOk)
        return false;
    auto* civ = static_cast<char*>(Va<void* (*)()>(raw::GameCivGet)());
    auto* controller = civ ? *reinterpret_cast<char**>(civ + raw::GameCivUiController) : nullptr;
    auto* editor = controller ? *reinterpret_cast<char**>(controller + 0x80) : nullptr;
    return editor && *reinterpret_cast<void**>(editor + 0x40);
}

bool CinematicPlaying() {
    if (!cinematicOk)
        return false;
    auto* manager = Sdk<char* (*)()>(GetAddress(Simulator::cCinematicManager, Get))();
    if (!manager)
        return false;
    int state = *reinterpret_cast<int*>(manager + 0x2c);
    return state == 1 || state == 2;
}

void PlayRefusal() {
    if (!soundOk)
        return;
    int track = Va<int (*)()>(raw::NewAudioTrack)();
    Sdk<void (*)(uint32_t, int)>(GetAddress(Audio, PlayAudio))(0x594303b7, track);
}

namespace {
bool hideRolloverWanted = false;
}

void WantCityRolloverHidden(bool on) {
    hideRolloverWanted = on;
}

void HideCityRolloverIfWanted() {
    if (hideRolloverWanted)
        HideCityRollover();
}

UTFWin::IWindow* CityRolloverRoot() {
    if (!rolloverOk)
        return nullptr;
    auto* civ = static_cast<char*>(Va<void* (*)()>(raw::GameCivGet)());
    auto* controller = civ ? *reinterpret_cast<char**>(civ + raw::GameCivUiController) : nullptr;
    auto* attachment = controller ? *reinterpret_cast<char**>(controller + raw::UiControllerAttachment) : nullptr;
    if (!attachment || !HasSlot(attachment, 0x10, 0xe35320))
        return nullptr;
    auto* rollover = *reinterpret_cast<char**>(attachment + 0x10);
    if (!rollover)
        return nullptr;
    return Va<UTFWin::IWindow*(__thiscall*)(void*)>(raw::RolloverRootWindow)(rollover);
}

void HideCityRollover() {
    auto* w = CityRolloverRoot();
    if (w && w->IsVisible())
        w->SetFlag(UTFWin::kWinFlagVisible, false);
}

bool OverCityRollover() {
    auto* root = CityRolloverRoot();
    auto* wm = Windows();
    HWND hwnd = GameWindow();
    if (!root || !root->IsVisible() || !wm || !hwnd)
        return false;
    POINT pt;
    if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt))
        return false;
    for (auto* w = wm->GetWindowAtPosition(Math::Point(float(pt.x), float(pt.y))); w; w = w->GetParent())
        if (w == root)
            return true;
    return false;
}

int ConvertDeltaMs(int realMs) {
    auto* tm = Sdk<cGameTimeManager* (*)()>(GetAddress(Simulator::cGameTimeManager, Get))();
    if (!tm)
        return realMs;
    return Sdk<int(__thiscall*)(cGameTimeManager*, int)>(GetAddress(Simulator::cGameTimeManager, ConvertDeltaTime))(
        tm, realMs);
}

namespace {
struct RawTransform {
    int16_t flags;
    int16_t count;
    float offset[3];
    float scale;
    float rot[3][3];
};

static_assert(sizeof(RawTransform) == 0x38, "Transform ABI");
}

bool ReadViewer(App::cViewer* viewer, CamXf& out) {
    if (!viewer)
        return false;
    const auto& m = viewer->viewTransform.m;
    for (int i = 0; i < 3; ++i) {
        vm::V3 r{m[i][0], m[i][1], m[i][2]};
        out.rot.SetRow(i, vm::Norm(r, i == 0 ? vm::V3{1, 0, 0} : (i == 1 ? vm::V3{0, 1, 0} : vm::V3{0, 0, 1})));
    }
    out.pos = {m[3][0], m[3][1], m[3][2]};
    return vm::Finite(out.pos);
}

void WriteViewer(App::cViewer* viewer, const CamXf& xf) {
    if (!viewer || !vm::Finite(xf.pos))
        return;
    RawTransform t{};
    t.flags = 7;
    t.count = 1;
    t.offset[0] = xf.pos.x;
    t.offset[1] = xf.pos.y;
    t.offset[2] = xf.pos.z;
    t.scale = 1.0f;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            t.rot[i][j] = xf.rot.m[i][j];
    Sdk<void(__thiscall*)(App::cViewer*, const RawTransform*)>(GetAddress(App::cViewer, SetViewTransform))(viewer, &t);
}

bool SetNativeCursor(uint32_t id) {
    void* cm = CursorMgr();
    if (!cm || !cursorOk)
        return false;
    auto fn = origSetActiveCursor
                  ? reinterpret_cast<bool(__thiscall*)(void*, uint32_t)>(origSetActiveCursor)
                  : Sdk<bool(__thiscall*)(void*, uint32_t)>(GetAddress(UTFWin::cCursorManager, SetActiveCursor));
    return fn(cm, id);
}

uint32_t ActiveCursorId() {
    void* cm = CursorMgr();
    if (!cm || !cursorOk)
        return 0;
    return Sdk<uint32_t(__thiscall*)(void*)>(GetAddress(UTFWin::cCursorManager, GetActiveCursor))(cm);
}

bool MouseOverUI() {
    auto* wm = Windows();
    HWND hwnd = GameWindow();
    if (!wm || !hwnd)
        return false;
    POINT pt;
    if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt))
        return false;
    auto* main = wm->GetMainWindow();
    auto* w = wm->GetWindowAtPosition(Math::Point(float(pt.x), float(pt.y)));
    if (!w || w == main || !main)
        return false;
    const auto& a = w->GetRealArea();
    const auto& m = main->GetRealArea();
    if (a.GetWidth() >= m.GetWidth() * 0.95f && a.GetHeight() >= m.GetHeight() * 0.95f)
        return false;
    return true;
}

void NotifyMessage(uint32_t id) {
    if (auto* m = Messages())
        m->MessageSend(id, nullptr);
}

Simulator::cGameData* MessageObject(void* msg) {
    if (!msg)
        return nullptr;
    __try {
        void* params = VCall<void*>(msg, 0x10);
        if (!params)
            return nullptr;
        auto* prop = VCall<unsigned char*>(params, 0x1c, 0);
        if (!prop || !(prop[0x10] & 0x30))
            return nullptr;
        void* obj = *reinterpret_cast<void**>(prop);
        return static_cast<cGameData*>(Cast(obj, cGameData::TYPE));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

int MessageKey(void* msg) {
    if (!msg)
        return 0;
    __try {
        void* params = VCall<void*>(msg, 0x10);
        if (!params)
            return 0;
        auto* prop = VCall<int*>(params, 0x1c, 1);
        return prop ? *prop : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
}


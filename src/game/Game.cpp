
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
#include <Spore/Simulator/cCulturalTarget.h>
#include <Spore/Simulator/cGameTerrainCursor.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameViewManager.h>
#include <Spore/Simulator/SubSystem/GameInputManager.h>
#include <Spore/Simulator/SubSystem/GameTimeManager.h>
#include <Spore/Simulator/SubSystem/PlanetModel.h>
#include <Spore/Simulator/SubSystem/CinematicManager.h>
#include <Spore/Simulator/SubSystem/cRelationshipManager.h>
#include <Spore/Swarm/IEffectsManager.h>
#include <Spore/UTFWin/IWindowManager.h>
#include <Spore/UTFWin/IWindow.h>
#include <Spore/UTFWin/CursorManager.h>
#include <Spore/Audio/AudioSystem.h>
#include <Spore/UTFWin/Image.h>
#include <Spore/Graphics/IShadowWorld.h>
#include <Spore/Graphics/IModelWorld.h>
#include <Spore/App/IPropManager.h>
#include <Spore/App/Property.h>
#include <Spore/Resource/IResourceManager.h>
#include <Spore/Resource/Database.h>
#include <Spore/Resource/IRecord.h>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace Simulator;

static_assert(sizeof(cVehicle) == 0xd98, "cVehicle ABI");
static_assert(offsetof(cVehicle, mLocomotion) == 0xb1c, "cVehicle::mLocomotion");
static_assert(offsetof(cVehicle, mPurpose) == 0xb20, "cVehicle::mPurpose");
static_assert(offsetof(cVehicle, mOrders) == 0xb68, "cVehicle::mOrders");
static_assert(offsetof(cVehicle, mIdlePosition) == 0xb80, "cVehicle::mIdlePosition");
static_assert(offsetof(cVehicle, mbDead) == 0xbcd, "cVehicle::mbDead");
static_assert(offsetof(cGameData, mbIsDestroyed) == 0x21, "cGameData::mbIsDestroyed");
static_assert(offsetof(cGameData, mPoliticalID) == 0x30, "cGameData::mPoliticalID");
static_assert(sizeof(Graphics::FilterSettings) == 0x18, "model query filter ABI");
static_assert(offsetof(Graphics::FilterSettings, flags) == 0x15, "model query flags ABI");
static_assert(sizeof(Math::BoundingBox) == 0x18, "model query bounds ABI");
static_assert(offsetof(cSpatialObject, mPosition) == 0x4, "cSpatialObject::mPosition");
static_assert(offsetof(cSpatialObject, mOrientation) == 0x10, "cSpatialObject::mOrientation");
static_assert(offsetof(cSpatialObject, mBoundingRadius) == 0x5c, "cSpatialObject::mBoundingRadius");
static_assert(offsetof(cSpatialObject, mbIsSelected) == 0x6c, "cSpatialObject::mbIsSelected");
static_assert(offsetof(cCity, mpCityWalls) == 0x324, "cCity::mpCityWalls");
static_assert(offsetof(cCity, mCulturalTargets) == 0x62c, "cCity::mCulturalTargets");
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

cGameInputManager* Input() {
    return Sdk<cGameInputManager* (*)()>(GetAddress(Simulator::cGameInputManager, Get))();
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
     cinematicOk = false, cityAttackOk = false, cityEditorOk = false, economicMenuOk = false,
     tradeContinuationOk = false, cityConvertOk = false, faithAttackOk = false, sceneryOk = false, bodiesOk = false,
     collisionOk = false, takeFireOk = false, retreatOk = false, relationsOk = false, effectsOk = false,
     moveAnimOk = false, vehicleListOk = false, rocksOk = false, scrapeOk = false, waterOk = false;

const void* economicCityMenu = nullptr;
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

bool IsTradeContinuationCaller(uint32_t callerVa) {
    return tradeContinuationOk &&
           (callerVa == raw::TradeAcceptedOrder + 5 || callerVa == raw::TradeExistingOrder + 5);
}

bool EconomicMenuUsable() {
    return economicMenuOk;
}

bool AllowPieMenuItem(const void* menu, uint32_t command) {
    return !economicCityMenu || menu != economicCityMenu || command == 0x37e95bb || command == 0x37e95bc ||
           command == 0x37e95bd;
}

bool PieMenuOpen() {
    return economicMenuOk && *Va<void**>(raw::ActivePieMenu);
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
    bodiesOk = Slot(raw::VehicleSpatialVtable, 0x68, 0xad2740) && Bytes(0xad2740, "8d4138c3") &&
               Slot(raw::VehicleSpatialVtable, 0xb8, 0xca7f90);
    sceneryOk = bodiesOk && Slot(raw::TerrainSphereVtable, 0xb0, 0xf97630) &&
                Slot(raw::ModelWorldVtable, 0x4c, 0x74a0e0) &&
                uintptr_t(GetAddress(Math::BoundingBox, ApplyTransform)) == Va<uintptr_t>(0x409dd0) &&
                Bytes(0xb846dd, "8b4e248b118b92b0000000") &&
                Bytes(0xf97630, "8b81d80700003b81dc070000") &&
                Bytes(0x74a299, "8b869c00000085c0744483b8cc00000000");
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
    tradeContinuationOk = orderHookOk && Bytes(0xdca213, "6a006a05568bcbe8811feeff") &&
                          Bytes(0xdca43f, "8b4c243c6a006a0556e8531deeff") &&
                          Bytes(0xcac1b4, "8b4c2428394810") && Bytes(0xcac242, "8946108b9708050000");
    rolloverOk = Bytes(0xcf4f35, "8986e4000000") && Bytes(0xe35796, "8b4b1089b1a8000000") &&
                 Bytes(raw::RolloverRootWindow, "8b41486a015083c10ce89284feffc3cc") &&
                 Slot(raw::CursorAttachmentVtable, 0x10, 0xe35320);
    cityAttackOk = orderHookOk && Bytes(0xbd9d00, "53568bb140030000578bb94403000033") &&
                   Bytes(0xbd9cc0, "53568bb154030000578bb95803000033") &&
                   Bytes(0xc9efc0, "8b4424048981280d00008981240d0000") && Bytes(0xdcd3ef, "8b471033f683e802") &&
                   Bytes(0xdce930, "6832229bee");
    cityConvertOk = orderHookOk && Bytes(0xcf36db, "83bf200b000001") && Bytes(0xcf3758, "6a03ffd0") &&
                    Bytes(0xcf3781, "508bcee887faffff") &&
                    Bytes(0xc9efc0, "8b4424048981280d00008981240d0000");
    faithAttackOk = stanceOk && orderHookOk && rangeOk &&
                  Bytes(raw::StopVehicleWeapons, "53578bf98b9f680c00002b9f640c0000") &&
                  Bytes(raw::ChooseCultureTarget, "83ec20578bf98b078b502c897c240cffd284") &&
                  Bytes(raw::ValidCombatTarget, "568b74240c85f6750432c05ec3") &&
                  Bytes(0xc9efc0, "8b4424048981280d00008981240d0000");
    cityEditorOk = Bytes(0xcfe55a, "8b8ee80000008b898000000085c9740653") && Bytes(0xd124e5, "8bd9837b400056570f84") &&
                   Bytes(0xcf4bbc, "89b780000000");
    cinematicOk = uintptr_t(GetAddress(Simulator::cCinematicManager, Get)) == Va<uintptr_t>(0xb3d5d0) &&
                  Bytes(0xe35341, "8b402c83f801740983f802");
    soundOk = Bytes(raw::NewAudioTrack, "558bec83ec08e815a35e008945fc837d") &&
              uintptr_t(GetAddress(Audio, PlayAudio)) == Va<uintptr_t>(0x436390);
    collisionOk = bodiesOk && vehiclesOk && Slot(raw::VehicleCombatantVtable, 0x18, raw::VehicleTakeDamage) &&
                  Bytes(raw::VehicleTakeDamage, "81ec8400000053558be98b85f8faffff");
    takeFireOk = soundOk && Bytes(raw::TakeFireVoices, "bc7a5ce583c3c45988935b91") &&
                 Bytes(0xca7334, "8b8e200b00008b148d7c4a4701");
    retreatOk = orderHookOk && Bytes(raw::StartOrders, "8b4424048981280d00008981240d0000");
    waterOk = Bytes(raw::WaterLevel, "8379240074148b49248b018b500cffd2d9403cd84838d84034c3") &&
              Bytes(0xb817a2, "f30f10403cf30f594038f30f584034") &&
              uintptr_t(GetAddress(Simulator::cPlanetModel, GetHeightAt)) == Va<uintptr_t>(0xb7f0c0);
    scrapeOk = soundOk && Bytes(raw::PlayAudioAt, "56e8eae64a008bf085f60f8480000000") &&
               Bytes(raw::StopAudio, "56e84ae64a008bf085f6743e");
    effectsOk = uintptr_t(GetAddress(Swarm::IEffectsManager, Get)) == Va<uintptr_t>(0x67dd90);
    relationsOk = uintptr_t(GetAddress(Simulator::cRelationshipManager, Get)) == Va<uintptr_t>(0xb3d3c0) &&
                  Bytes(raw::IsAtWar, "8b442408568bf18b4c2408505150518b") &&
                  Bytes(raw::RelationshipMap, "56578d4424108bf1508d4c24") &&
                  Bytes(raw::RelationshipData, "83ec0856578d44241c508d4c");
    moveAnimOk = ForeignOrBytes(raw::MoveAnimation, "56578b7c241c8bf185ff0f849c0000008b47048b") &&
                 Bytes(0xe96a90, "8db5b0000000") && Bytes(0xe96abb, "e830dfffff") && Bytes(0xe956da, "8b4d0c") &&
                 Bytes(raw::ContinuousMoveAnimation, "8b811c0b000083e800741083e8017408") &&
                 Bytes(raw::MoveAnimationThreshold, "cdcccc3d");
    vehicleListOk = minimapOk && Bytes(0xceb5d4, "83fd04750233ed397cab6c") && Bytes(0xceb660, "686e513fee") &&
                    Bytes(0xe255cf, "894e50") && Bytes(0xe25947, "c70620094801") &&
                    Bytes(0xe23235, "8bbba4000000") && Bytes(0xe23250, "8b7704") &&
                    Bytes(0xe27404, "6a0168e856f604") && Bytes(raw::LayoutFindWindow, "8b491485c97405e9");
    rocksOk = sceneryOk && Bytes(0xb2efda, "8b450c8b48088b5010") && Bytes(0xb2eff3, "8b088b5018") &&
              Bytes(0xb2b47a, "6bff4c") && Bytes(0xb2b482, "8d6f04") &&
              uintptr_t(GetAddress(App::IPropManager, Get)) == Va<uintptr_t>(0x67ddf0) &&
              uintptr_t(GetAddress(App::Property, GetFloat)) == Va<uintptr_t>(0x40cf10) &&
              uintptr_t(GetAddress(App::Property, GetBool)) == Va<uintptr_t>(0x407190);

    ok &= Slot(raw::CameraManagerVtable, 0x2c, 0x7c6440);
    if (uintptr_t(GetAddress(App::cCameraManager, Update)) != Va<uintptr_t>(0x7c6440))
        ok = false;
    ok &= Bytes(raw::ActOnTarget, "83ec3c53558b6c2448565733ff894c24");
    ok &= Bytes(raw::ShowEconomicMenu, "83ec1c53578bd933ff80bb3501000000");
    economicMenuOk = ForeignOrBytes(raw::AddPieMenuItem, "83ec1856b8ac7b66018bf18b4c2420") &&
                     uintptr_t(GetAddress(Simulator::cGameInputManager, Get)) == Va<uintptr_t>(0xb3d350) &&
                     Slot(raw::GameInputManagerVtable, 0x30, 0xb1c480) &&
                     Slot(raw::GameInputManagerVtable, 0x2c, 0xb1ba80) &&
                     Bytes(0xb1c480, "568bf18d4660508d4e28e801fcffff") &&
                     Bytes(0xce8c69, "8b44243483f8060f848400000083f80b") &&
                     Bytes(0xce8d04, "6863ae07108bcdffd08bf8") && Bytes(0xce8c89, "685fdf03048bcdffd28bf0") &&
                     Bytes(0xce8ddf, "8b4b5ce8f946e9ff") && Bytes(0xce8df1, "89bb0c010000") &&
                     Bytes(0xb7c8e0, "8b0d7c816801568bf1") && Bytes(0xb7c932, "c7057c81680100000000");
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

float WaterLevel() {
    auto* pm = Planet();
    return pm && waterOk ? Va<float(__thiscall*)(cPlanetModel*)>(raw::WaterLevel)(pm) : 0.0f;
}

bool IsInWater(const vm::V3& p) {
    auto* pm = Planet();
    if (!pm)
        return false;
    Math::Vector3 v = ToSdk(p);
    return Sdk<bool(__thiscall*)(cPlanetModel*, const Math::Vector3*)>(GetAddress(Simulator::cPlanetModel, IsInWater))(
        pm, &v);
}

Terrain::ITerrain* PlanetTerrain() {
    auto* pm = Planet();
    return pm ? *reinterpret_cast<Terrain::ITerrain**>(reinterpret_cast<char*>(pm) + raw::PlanetTerrain) : nullptr;
}

bool VehicleBody(cVehicle* v, bool transposed, vm::OBox& box) {
    if (!bodiesOk || !VehicleAlive(v))
        return false;
    auto* sp = Spatial(v);
    if (!sp || !sp->mpModel || !HasSlot(sp, 0x68, 0xad2740))
        return false;
    const auto* local = VCall<const Math::BoundingBox*>(sp, 0x68);
    vm::Box extents{FromSdk(local->lower), FromSdk(local->upper)};
    if (!vm::ValidBox(extents))
        return false;
    vm::M3 rot = vm::ToMatrix(Orientation(sp));
    box = vm::OrientedBox(extents, transposed ? vm::Transposed(rot) : rot, Position(sp));
    return vm::ValidOBox(box);
}

bool VehicleHeightSpan(cVehicle* v, bool transposed, float& bottom, float& top) {
    vm::OBox body;
    if (!VehicleBody(v, transposed, body))
        return false;
    vm::V3 up = vm::Norm(body.center);
    float half = 0;
    for (int i = 0; i < 3; ++i)
        half += std::fabs(vm::Dot(body.axis[i], up)) * body.half[i];
    float centre = vm::Dot(body.center, up);
    bottom = centre - half;
    top = centre + half;
    return half > 0.01f;
}

bool ModelBodyBounds(Graphics::Model* model, vm::Box& box) {
    if (!sceneryOk || !model || !(model->mFlags & 1))
        return false;
    Math::BoundingBox native = model->mDefaultBBox;
    Transform transform = model->mTransform;
    if (model->mFlags & 0x80)
        transform.SetScale(1.0f);
    Sdk<void(__thiscall*)(Math::BoundingBox*, const Transform*)>(GetAddress(Math::BoundingBox, ApplyTransform))(
        &native, &transform);
    box = {FromSdk(native.lower), FromSdk(native.upper)};
    return vm::ValidBox(box);
}

bool ModelHasCollisionMesh(Graphics::Model* model) {
    auto* mesh = *reinterpret_cast<char**>(reinterpret_cast<char*>(model) + 0x94);
    return mesh && *reinterpret_cast<void**>(mesh + 0xcc);
}

bool ModelIntersectsBody(Graphics::Model* model, const vm::Box& body) {
    auto* world = model->mpWorld;
    if (!world || !HasSlot(world, 0x4c, 0x74a0e0))
        return false;
    Graphics::FilterSettings filter;
    filter.flags = 0;
    filter.collisionMode = Graphics::CollisionMode::MeshCluster;
    Math::BoundingBox native(ToSdk(body.lower), ToSdk(body.upper));
    return world->BoxIntersectsModel(native, model, filter);
}

bool CellEntersModel(Graphics::Model* model, const vm::Box& cell, const vm::V3& step, const vm::Box& obstacle) {
    if (!ModelHasCollisionMesh(model))
        return vm::MovementEntersBox(cell, step, obstacle);
    if (!ModelIntersectsBody(model, vm::SweptBox(cell, step)))
        return false;
    if (!ModelIntersectsBody(model, cell))
        return true;
    vm::V3 centre = (obstacle.lower + obstacle.upper) * 0.5f, cellCentre = (cell.lower + cell.upper) * 0.5f;
    return ModelIntersectsBody(model, vm::ShiftBox(cell, step)) && vm::Dot(step, cellCentre - centre) < 0;
}

namespace {
constexpr uint32_t kPlanetAssetGroup = 0x40842000;
constexpr uint32_t kObstacleBaseCollisionRadius = 0x0254CF89, kObstacleCollisionHeight = 0x0254CF97,
                   kRockObstacleAllowed = 0x037575E5;
constexpr size_t kMaxRockBatches = 4096;

struct RockInstance {
    unsigned char raw[0x14];

    vm::V3 Position() const {
        const float* p = reinterpret_cast<const float*>(raw + 4);
        return {p[0], p[1], p[2]};
    }

    float Scale() const {
        float s = *reinterpret_cast<const float*>(raw + 0x10);
        return std::isfinite(s) && s > 0.01f && s < 100.0f ? s : 1.0f;
    }
};

struct RockBatch {
    uint32_t id = 0, model = 0;
    std::vector<RockInstance> rocks;
};

struct RockShape {
    uint32_t id = 0;
    float radius = 0, height = 0;
};

std::vector<RockBatch> rockBatches;
std::vector<RockShape> rockShapes;
const void* rockPlanet = nullptr;

struct RockMessage {
    uint32_t count, set, model;
    const char* instances;
};

bool ReadRockMessage(const void* msg, RockMessage& out) {
    __try {
        auto* m = static_cast<const char*>(msg);
        out.count = *reinterpret_cast<const uint32_t*>(m);
        out.instances = *reinterpret_cast<const char* const*>(m + 0x08);
        out.set = *reinterpret_cast<const uint32_t*>(m + 0x10);
        out.model = *reinterpret_cast<const uint32_t*>(m + 0x18);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CopyRocks(const char* instances, uint32_t count, RockInstance* out) {
    __try {
        for (uint32_t i = 0; i < count; ++i)
            std::memcpy(out[i].raw, instances + i * 0x4c, sizeof(out[i].raw));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

RockShape RockShapeFor(uint32_t id) {
    for (const auto& shape : rockShapes)
        if (shape.id == id)
            return shape;
    RockShape shape;
    shape.id = id;
    auto* props = Sdk<App::IPropManager* (*)()>(GetAddress(App::IPropManager, Get))();
    PropertyListPtr list;
    bool obstacle = true;
    if (props && props->GetPropertyList(id, kPlanetAssetGroup, list) && list) {
        auto getBool = Sdk<bool (*)(const App::PropertyList*, uint32_t, bool&)>(GetAddress(App::Property, GetBool));
        auto getFloat = Sdk<bool (*)(const App::PropertyList*, uint32_t, float&)>(GetAddress(App::Property, GetFloat));
        getBool(list.get(), kRockObstacleAllowed, obstacle);
        if (obstacle && getFloat(list.get(), kObstacleBaseCollisionRadius, shape.radius) &&
            !getFloat(list.get(), kObstacleCollisionHeight, shape.height))
            shape.height = shape.radius * 2;
    }
    if (!(shape.radius > 0 && shape.radius < 200 && shape.height > 0 && shape.height < 500))
        shape.radius = shape.height = 0;
    rockShapes.push_back(shape);
    return shape;
}

bool PlanetRocksBlock(const vm::OBox& body, const vm::V3& step) {
    if (rockBatches.empty() || rockPlanet != static_cast<const void*>(Planet()))
        return false;
    float reach = vm::OBoxRadius(body) + vm::Len(step);
    for (const auto& batch : rockBatches) {
        RockShape shape;
        bool known = false;
        for (const RockInstance& rock : batch.rocks) {
            vm::V3 at = rock.Position();
            if (vm::Len(at - body.center) > reach + 40.0f)
                continue;
            if (!known) {
                shape = RockShapeFor(batch.model);
                known = true;
            }
            if (!(shape.radius > 0))
                break;
            float radius = shape.radius * rock.Scale(), height = shape.height * rock.Scale();
            vm::V3 up = vm::Norm(at);
            if (vm::Len(vm::Flatten(at - body.center, up)) > reach + radius ||
                !vm::MovementEntersCylinder(body, step, at, up, radius, height))
                continue;
            return true;
        }
    }
    return false;
}
}

void OnPlanetRocks(void* msg) {
    RockMessage m{};
    if (!rocksOk || !ReadRockMessage(msg, m) || m.count > 4096)
        return;
    const void* planet = Planet();
    if (planet != rockPlanet) {
        rockBatches.clear();
        rockPlanet = planet;
    }
    auto found = rockBatches.end();
    for (auto it = rockBatches.begin(); it != rockBatches.end(); ++it)
        if (it->id == m.set)
            found = it;
    if (!m.instances || !m.count) {
        if (found != rockBatches.end())
            rockBatches.erase(found);
        return;
    }
    RockBatch batch;
    batch.id = m.set;
    batch.model = m.model;
    batch.rocks.resize(m.count);
    if (!CopyRocks(m.instances, m.count, batch.rocks.data()))
        return;
    if (found != rockBatches.end()) {
        *found = std::move(batch);
        return;
    }
    if (rockBatches.size() >= kMaxRockBatches)
        rockBatches.erase(rockBatches.begin());
    rockBatches.push_back(std::move(batch));
}

bool SceneryStepBlocked(cVehicle* controlled, const vm::V3& from, const vm::V3& to, bool transposed) {
    vm::OBox body;
    if (!sceneryOk || !vm::Finite(from) || !vm::Finite(to) || !VehicleBody(controlled, transposed, body))
        return false;
    vm::V3 step = to - from;
    if (vm::Dot(step, step) < 1e-12f)
        return false;
    body = vm::ShiftOBox(body, from - Position(Spatial(controlled)));
    vm::OBox cells[27];
    int cellCount = vm::SplitOBox(body, cells, 27);
    vm::Box whole = vm::OBoxBounds(body), sweep = vm::SweptBox(whole, step);
    auto* terrain = PlanetTerrain();
    ModelPtr* models = nullptr;
    size_t count = terrain && HasSlot(terrain, 0xb0, 0xf97630) ? terrain->GetModels(models) : 0;
    bool blocked = false;
    for (size_t i = 0; models && i < count && !blocked; ++i) {
        auto* model = models[i].get();
        vm::Box obstacle;
        if (!ModelBodyBounds(model, obstacle) || !vm::BoxesOverlap(sweep, obstacle))
            continue;
        if (ModelHasCollisionMesh(model) && !ModelIntersectsBody(model, sweep))
            continue;
        for (int c = 0; c < cellCount && !blocked; ++c)
            blocked = CellEntersModel(model, vm::OBoxBounds(cells[c]), step, obstacle);
    }
    return blocked || PlanetRocksBlock(body, step);
}

bool GetVehicles(cVehicle**& begin, cVehicle**& end) {
    begin = end = nullptr;
    if (!vehiclesOk)
        return false;
    auto* nouns = NounManager();
    if (!nouns)
        return false;
    auto* list = Va<char*(__thiscall*)(void*, void*, void*, void*, void*, uint32_t)>(raw::NounGetData)(
        nouns, Va<void*>(0xcd7e90), Va<void*>(0xd3d3d0), Va<void*>(0xae7250), Va<void*>(0xb1e440), 0x18C6DE8);
    if (!list)
        return false;
    begin = *reinterpret_cast<cVehicle***>(list + 4);
    end = *reinterpret_cast<cVehicle***>(list + 8);
    return begin && end && begin < end;
}

bool VehicleStepBlocked(cVehicle* controlled, const vm::V3& from, const vm::V3& to, bool transposed,
                        cVehicle** hit) {
    if (hit)
        *hit = nullptr;
    vm::OBox body;
    if (!vehiclesOk || !vm::Finite(from) || !vm::Finite(to) || !VehicleBody(controlled, transposed, body))
        return false;
    cVehicle** begin = nullptr;
    cVehicle** end = nullptr;
    if (!GetVehicles(begin, end))
        return false;
    vm::V3 step = to - from;
    body = vm::ShiftOBox(body, from - Position(Spatial(controlled)));
    float reach = vm::OBoxRadius(body) + vm::Len(step);
    for (auto** it = begin; it && it < end; ++it) {
        vm::OBox other;
        if (*it == controlled || !VehicleBody(*it, transposed, other) ||
            vm::Len(other.center - body.center) > reach + vm::OBoxRadius(other))
            continue;
        if (vm::MovementEntersOBox(body, step, other)) {
            if (hit)
                *hit = *it;
            return true;
        }
    }
    return false;
}

cVehicle* ForeignVehicleStepBlocked(cVehicle* controlled, const void* object, const vm::V3& to, bool transposed,
                                    vm::V3& allowed) {
    if (!bodiesOk || !object || !vm::Finite(to) || !VehicleAlive(controlled))
        return nullptr;
    auto* spatial = static_cast<cSpatialObject*>(const_cast<void*>(object));
    if (spatial == Spatial(controlled) || !HasSlot(spatial, 0xb8, 0xca7f90))
        return nullptr;
    auto* other = static_cast<cVehicle*>(spatial->Cast(cVehicle::TYPE));
    vm::OBox obstacle, moving;
    if (!VehicleBody(controlled, transposed, obstacle) || !VehicleBody(other, transposed, moving))
        return nullptr;
    vm::V3 from = Position(spatial);
    if (!vm::Finite(from) || vm::Len(from) < vm::Len(Position(Spatial(controlled))) * 0.5f)
        return nullptr;
    if (!vm::MovementEntersOBox(moving, to - from, obstacle))
        return nullptr;
    allowed = from + vm::AllowedStep(moving, to - from, obstacle, vm::Norm(from));
    return other;
}

vm::V3 ContactPoint(cVehicle* a, cVehicle* b, bool transposed) {
    vm::OBox ba, bb;
    if (!VehicleBody(a, transposed, ba) || !VehicleBody(b, transposed, bb))
        return (Position(Spatial(a)) + Position(Spatial(b))) * 0.5f;
    vm::V3 onB = vm::ClosestPoint(bb, ba.center), onA = vm::ClosestPoint(ba, onB);
    return (onA + onB) * 0.5f;
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

bool EconomicTarget(cVehicle* v, cGameData* target) {
    return v && target && !target->mbIsDestroyed && v->mPurpose == kVehicleEconomic &&
           target->mPoliticalID != uint32_t(-1) && target->mPoliticalID != PlayerPoliticalID() &&
           (IsCityHall(target) || Cast(target, cCommodityNode::TYPE));
}

namespace {
bool OpenEconomicMenu(cGameData* target, const void* cityMenu) {
    auto* input = Input();
    auto* sp = static_cast<cSpatialObject*>(Cast(target, 0x1186577));
    if (!economicMenuOk || !input || !sp || !HasSlot(input, 0x30, 0xb1c480))
        return false;
    const int flags = int(UIStateMachineObjectFlags::NPCOwned) | int(UIStateMachineObjectFlags::IsNotSelected) |
                      int(UIStateMachineObjectFlags::SomeObjectsAreSelected) |
                      int(UIStateMachineObjectFlags::ObjectIsNotShowingMenu);
    const void* previousMenu = economicCityMenu;
    economicCityMenu = cityMenu;
    bool result = input->ProcessTransition(UIStateMachineEvent::MouseDown, UIStateMachineEventKey::LEFT,
                                           static_cast<ISimulatorSerializable*>(target), sp->mPosition, flags,
                                           int(UIStateMachineKeyMod::NONE));
    economicCityMenu = previousMenu;
    return result && PieMenuOpen();
}
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
    if (EconomicTarget(v, target)) {
        bool city = IsCityHall(target);
        if (!economicMenuOk || (city && !CityOf(target)))
            return false;
        auto* menu = city ? *reinterpret_cast<void**>(static_cast<char*>(controller) + 0x5c) : nullptr;
        if (city && !menu)
            return false;
        return OpenEconomicMenu(target, menu);
    }
    Va<int(__thiscall*)(void*, cGameData*, int)>(raw::ActOnTarget)(controller, target, key);
    return true;
}

void ClearOrders(cVehicle* v) {
    if (v)
        Va<void(__thiscall*)(cVehicle*)>(raw::ClearOrders)(v);
}

bool HasOrders(cVehicle* v) {
    auto* raw = reinterpret_cast<char*>(v);
    return v && *reinterpret_cast<char**>(raw + 0xb68) < *reinterpret_cast<char**>(raw + 0xb6c);
}

cGameData* CurrentOrderTarget(cVehicle* v) {
    auto* raw = reinterpret_cast<char*>(v);
    auto* begin = *reinterpret_cast<char**>(raw + 0xb68);
    auto* end = *reinterpret_cast<char**>(raw + 0xb6c);
    if (!begin || begin >= end)
        return nullptr;
    return *reinterpret_cast<cGameData**>(begin);
}

int CurrentOrderKind(cVehicle* v) {
    if (!v || !tradeContinuationOk)
        return -1;
    auto* raw = reinterpret_cast<char*>(v);
    auto* begin = *reinterpret_cast<char**>(raw + 0xb68);
    auto* end = *reinterpret_cast<char**>(raw + 0xb6c);
    if (!begin || begin >= end)
        return -1;
    return *reinterpret_cast<int*>(begin + 0x10);
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

bool CityConvertible(cCity* city) {
    return cityConvertOk && faithAttackOk && city && !city->cGameData::mbIsDestroyed && city->mPoliticalID != uint32_t(-1) &&
           city->mPoliticalID != PlayerPoliticalID();
}

cGameData* FaithTarget(cVehicle* v, cGameData* target) {
    if (!faithAttackOk || !v || v->mPurpose != kVehicleCultural || !target || target->mbIsDestroyed)
        return nullptr;
    auto* city = static_cast<cCity*>(Cast(target, cCity::TYPE));
    if (!city && IsCityHall(target))
        city = CityOf(target);
    if (!city)
        return target;
    if (!CityConvertible(city))
        return nullptr;
    size_t first = v->mLocomotion == kVehicleWater ? 3 : 0;
    size_t end = v->mLocomotion == kVehicleWater ? 4 : 3;
    if (city->mCulturalTargets.size() < end)
        return nullptr;
    for (size_t i = first; i < end; ++i)
        if (!city->mCulturalTargets[i])
            return nullptr;
    auto* chosen = Va<cCulturalTarget*(__thiscall*)(cCity*, cVehicle*)>(raw::ChooseCultureTarget)(city, v);
    return chosen && !chosen->cGameData::mbIsDestroyed ? static_cast<cGameData*>(chosen) : nullptr;
}

bool FaithWeaponReady(cVehicle* v) {
    if (!faithAttackOk || !v || v->mPurpose != kVehicleCultural)
        return false;
    auto* combatant = static_cast<cCombatant*>(v);
    return HasSlot(combatant, 0x44, 0xc9e920) && VCall<bool>(combatant, 0x44, 0);
}

cCombatant* FaithCombatTarget(cVehicle* v, cGameData* target) {
    if (!faithAttackOk || !v || !VehicleAlive(v) || !origVehSetTarget)
        return nullptr;
    target = FaithTarget(v, target);
    auto* other = target ? static_cast<cCombatant*>(Cast(target, cCombatant::TYPE)) : nullptr;
    auto* self = static_cast<cCombatant*>(v);
    if (!other || !Va<bool(__cdecl*)(cCombatant*, cCombatant*)>(raw::ValidCombatTarget)(self, other) ||
        InWeaponRange(v, target) != 1)
        return nullptr;
    return other;
}

bool AimFaithWeapon(cVehicle* v, cGameData* target) {
    auto* other = FaithCombatTarget(v, target);
    if (!other)
        return false;
    auto* self = static_cast<cCombatant*>(v);
    if (self->mpTarget.get() != other)
        reinterpret_cast<void(__thiscall*)(cCombatant*, cCombatant*)>(origVehSetTarget)(self, other);
    return true;
}

bool AttackFaith(cVehicle* v, cGameData* target) {
    if (!target || IsCityHall(target) || Cast(target, cCity::TYPE) ||
        !FaithCombatTarget(v, target))
        return false;
    Va<void(__thiscall*)(cVehicle*, cGameData*, int, int)>(raw::AddOrder)(v, target, 1, 1);
    Va<void(__thiscall*)(cVehicle*, int)>(0xc9efc0)(v, 0);
    if (CurrentOrderTarget(v) == target && AimFaithWeapon(v, target))
        return true;
    if (CurrentOrderTarget(v) == target)
        ClearOrders(v);
    StopFaithWeapon(v);
    return false;
}

void StopFaithWeapon(cVehicle* v) {
    if (!faithAttackOk || !v || v->mPurpose != kVehicleCultural)
        return;
    if (origVehSetTarget)
        reinterpret_cast<void(__thiscall*)(cCombatant*, cCombatant*)>(origVehSetTarget)(static_cast<cCombatant*>(v), nullptr);
    Va<int(__thiscall*)(cVehicle*)>(raw::StopVehicleWeapons)(v);
}

bool AttackCity(cVehicle* v, cCity* city) {
    if (!v || !city || !VehicleAlive(v))
        return false;
    int kind = 2;
    cGameData* target = city;
    if (v->mPurpose == kVehicleCultural) {
        target = FaithTarget(v, city);
        if (!CityConvertible(city) || !target)
            return false;
        kind = kOrderConvertCity;
    } else if (v->mPurpose != kVehicleMilitary || !CityDefenseless(city)) {
        return false;
    }
    Va<void(__thiscall*)(cVehicle*, cGameData*, int, int)>(raw::AddOrder)(v, target, kind, 1);
    Va<void(__thiscall*)(cVehicle*, int)>(0xc9efc0)(v, 0);
    return CurrentOrderTarget(v) == target;
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

namespace {
UTFWin::IWindow* VehicleListFrame(cVehicle* v) {
    int purpose = v ? int(v->mPurpose) : -1;
    if (purpose == 4)
        purpose = 0;
    if (!vehicleListOk || purpose < 0 || purpose > 3)
        return nullptr;
    char* posse = nullptr;
    CivMinimap(&posse);
    auto* panel = posse ? *reinterpret_cast<char**>(posse + 0x6c + purpose * 4) : nullptr;
    if (!panel)
        return nullptr;
    void* key = Cast(v, 0xee3f516e);
    auto* begin = *reinterpret_cast<char**>(panel + 0xa4);
    auto* end = *reinterpret_cast<char**>(panel + 0xa8);
    for (char* entry = begin; key && entry && entry < end; entry += 8) {
        char* item = *reinterpret_cast<char**>(entry + 4);
        if (!item || *reinterpret_cast<uintptr_t*>(item) != Va<uintptr_t>(raw::PosseItemCivVtable) ||
            *reinterpret_cast<void**>(item + 0x50) != key)
            continue;
        return Va<UTFWin::IWindow*(__thiscall*)(void*, uint32_t, bool)>(raw::LayoutFindWindow)(item + 0x7c,
                                                                                               0x4f656e8, true);
    }
    return nullptr;
}
}

void TintVehicleListFrame(cVehicle* v, uint32_t color) {
    auto* frame = VehicleListFrame(v);
    Math::Color want(color ? color : 0xffffffff);
    if (frame && uint32_t(frame->GetShadeColor()) != want.value)
        frame->SetShadeColor(want);
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

bool VehicleDamage(cVehicle* v, float fraction, uint32_t attacker, const vm::V3& at, bool lethal, int type) {
    if (!collisionOk || !VehicleAlive(v) || !vm::Finite(at))
        return false;
    auto* self = static_cast<cCombatant*>(v);
    if (!HasSlot(self, 0x18, raw::VehicleTakeDamage) || self->mbInvincible)
        return false;
    float amount = self->mMaxHealthPoints * fraction;
    if (!lethal)
        amount = std::fmin(amount, self->mHealthPoints - 1.0f);
    if (!(amount > 0) || !std::isfinite(amount))
        return false;
    Math::Vector3 point = ToSdk(at);
    VCall<int>(self, 0x18, amount, attacker, type, static_cast<const Math::Vector3*>(&point),
               static_cast<cCombatant*>(nullptr));
    return true;
}

void PlayTakeFire(cVehicle* v) {
    int purpose = v ? int(v->mPurpose) : -1;
    if (!takeFireOk || purpose < kVehicleMilitary || purpose > kVehicleEconomic)
        return;
    int track = Va<int (*)()>(raw::NewAudioTrack)();
    Sdk<void (*)(uint32_t, int)>(GetAddress(Audio, PlayAudio))(Va<const uint32_t*>(raw::TakeFireVoices)[purpose],
                                                                track);
}

namespace {
float Standing(void* relations, uint32_t a, uint32_t b) {
    void* map = Va<void*(__thiscall*)(void*, uint32_t, uint32_t)>(raw::RelationshipMap)(relations, a, b);
    auto* data =
        Va<const float*(__thiscall*)(void*, void*, uint32_t, uint32_t)>(raw::RelationshipData)(relations, map, a, b);
    return data && std::isfinite(*data) ? *data : 0.0f;
}
}

bool HostileToPlayer(cVehicle* v, cVehicle* player) {
    uint32_t me = PlayerPoliticalID(), them = v ? uint32_t(v->cGameData::mPoliticalID) : uint32_t(-1);
    if (me == uint32_t(-1) || them == uint32_t(-1) || them == me)
        return false;
    if (player && static_cast<cCombatant*>(v)->mpTarget.get() == static_cast<cCombatant*>(player))
        return true;
    void* relations = relationsOk ? Sdk<void* (*)()>(GetAddress(Simulator::cRelationshipManager, Get))() : nullptr;
    if (!relations)
        return false;
    if (Va<bool(__thiscall*)(void*, uint32_t, uint32_t)>(raw::IsAtWar)(relations, me, them))
        return true;
    return std::fmin(Standing(relations, them, me), Standing(relations, me, them)) < 0;
}

bool Retreat(cVehicle* v, const vm::V3& threat, float distance) {
    if (!retreatOk || !VehicleAlive(v) || !vm::Finite(threat))
        return false;
    auto* sp = Spatial(v);
    vm::V3 pos = Position(sp), up = vm::Norm(pos);
    vm::V3 away = vm::Norm(vm::Flatten(pos - threat, up), vm::Norm(vm::Flatten(Direction(sp), up), {1, 0, 0}));
    auto addOrderAt = reinterpret_cast<void(__thiscall*)(cVehicle*, float, float, float, int)>(
        origAddOrderAt ? origAddOrderAt : Va<void*>(raw::AddOrderAt));
    const float turns[] = {0.0f, 0.6f, -0.6f, 1.2f, -1.2f, 1.8f, -1.8f};
    for (float turn : turns) {
        vm::V3 target = ToSurface(pos + vm::Rotate(away, up, turn) * distance);
        bool water = IsInWater(target);
        if (!vm::Finite(target) || (v->mLocomotion == kVehicleLand && water) ||
            (v->mLocomotion == kVehicleWater && !water))
            continue;
        StopMovement(sp);
        addOrderAt(v, target.x, target.y, target.z, 0);
        Va<void(__thiscall*)(cVehicle*, int)>(raw::StartOrders)(v, 0);
        return true;
    }
    return false;
}

namespace {
struct Spark {
    void* effect = nullptr;
    DWORD stop = 0;
};

Spark sparks[8];

void StopSpark(Spark& s) {
    VCall<int>(s.effect, 0x0c, 0);
    VCall<int>(s.effect, 0x04);
    s = Spark{};
}
}

void CollisionSparks(const vm::V3& at, const vm::V3& away, float scale) {
    void* manager = effectsOk ? Sdk<void* (*)()>(GetAddress(Swarm::IEffectsManager, Get))() : nullptr;
    if (!manager || !vm::Finite(at) || !VCall<bool>(manager, 0x28, kCollisionEffect, 0u))
        return;
    void* effect = nullptr;
    if (!VCall<bool>(manager, 0x2c, kCollisionEffect, 0u, &effect) || !effect)
        return;
    vm::V3 up = vm::Norm(at), dir = vm::Norm(vm::Flatten(away, up), {0, 1, 0});
    vm::V3 origin = at + dir * (6.0f * scale);
    RawTransform t{};
    t.flags = 7;
    t.count = 1;
    t.offset[0] = origin.x;
    t.offset[1] = origin.y;
    t.offset[2] = origin.z;
    t.scale = scale;
    vm::M3 rot = vm::M3::FromRows(vm::Norm(vm::Cross(dir, up)), dir, up);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            t.rot[i][j] = rot.m[i][j];
    VCall<void>(effect, 0x18, static_cast<const RawTransform*>(&t));
    const float silent = 0.0f;
    VCall<bool>(effect, 0x44, int(Swarm::kParamVolume), &silent, 1);
    VCall<void>(effect, 0x08, 0);
    Spark* slot = nullptr;
    for (auto& s : sparks)
        if (!s.effect && !slot)
            slot = &s;
    if (!slot) {
        slot = &sparks[0];
        for (auto& s : sparks)
            if (int(s.stop - slot->stop) < 0)
                slot = &s;
        StopSpark(*slot);
    }
    slot->effect = effect;
    slot->stop = GetTickCount() + 300;
}

namespace {
struct Scrape {
    const void* key = nullptr;
    int track = 0;
    DWORD seen = 0;
};

Scrape scrapes[4];
}

void CollisionScrape(const void* key, const vm::V3& at) {
    if (!scrapeOk || !key || !vm::Finite(at))
        return;
    DWORD now = GetTickCount();
    Scrape* slot = nullptr;
    for (auto& s : scrapes)
        if (s.key == key)
            slot = &s;
    if (slot) {
        slot->seen = now;
        return;
    }
    for (auto& s : scrapes)
        if (!s.key && !slot)
            slot = &s;
    if (!slot)
        return;
    int track = Va<int (*)()>(raw::NewAudioTrack)();
    if (!track)
        return;
    Va<void (*)(uint32_t, int, float, float, float)>(raw::PlayAudioAt)(kScrapeSound, track, at.x, at.y, at.z);
    *slot = {key, track, now};
}

void UpdateCollisionSparks() {
    DWORD now = GetTickCount();
    for (auto& s : sparks)
        if (s.effect && int(now - s.stop) >= 0)
            StopSpark(s);
    for (auto& s : scrapes)
        if (s.key && now - s.seen > 350) {
            Va<void (*)(int, int)>(raw::StopAudio)(s.track, 0);
            s = Scrape{};
        }
}

bool MoveAnimationUsable() {
    return moveAnimOk;
}

bool ContinuousMoveAnimation(cVehicle* v) {
    return moveAnimOk && v && Va<bool(__thiscall*)(cVehicle*)>(raw::ContinuousMoveAnimation)(v);
}

float MoveAnimationThreshold() {
    return *Va<const float*>(raw::MoveAnimationThreshold);
}

const void* MoveAnimationOwner(const void* state) {
    return *reinterpret_cast<const void* const*>(static_cast<const char*>(state) - 0xb0 + 0x0c);
}

void PauseMoveAnimation(void* world, void* model) {
    auto* m = static_cast<Graphics::Model*>(model);
    if (!world || !m || !(m->mFlags & (1 << 14)) || (m->mFlags & (1 << 18)))
        return;
    VCall<void>(world, 0x7c, m, 0.0f);
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


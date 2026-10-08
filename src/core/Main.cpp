
#include "core/Config.hpp"
#include "game/Game.hpp"
#include "pilot/Pilot.hpp"
#include <Windows.h>
#include <intrin.h>
#include <detours.h>
#include <Spore/ModAPI.h>
#include <Spore/App/IMessageManager.h>
#include <Spore/App/cCameraManager.h>
#include <Spore/UTFWin/CursorManager.h>

extern "C" LONG WINAPI DetourTransactionAbort();

namespace {
using KeyFn = bool(__thiscall*)(void*, int, uint32_t);
using MouseBtnFn = bool(__thiscall*)(void*, int, float, float, uint32_t);
using MouseMoveFn = bool(__thiscall*)(void*, float, float, uint32_t);
using WheelFn = bool(__thiscall*)(void*, int, float, float, uint32_t);
using UpdateFn = int(__thiscall*)(void*, float, float);
using MsgFn = bool(__thiscall*)(void*, uint32_t, void*);
using SetVecFn = void(__thiscall*)(void*, const void*);
using CamUpdateFn = void(__thiscall*)(void*, int);
using SetCursorFn = bool(__thiscall*)(void*, uint32_t);
using SetVelocityFn = void(__thiscall*)(void*, const float*);
using SetListenerFn = void(__thiscall*)(void*, int, const float*, const float*);
using AddOrderFn = void(__thiscall*)(void*, void*, int, int);
using AddOrderAtFn = void(__thiscall*)(void*, int, int, int, int);
using SetTargetFn = void(__thiscall*)(void*, void*);

KeyFn origKeyDown, origKeyUp;
MouseBtnFn origMouseDown, origMouseUp;
MouseMoveFn origMouseMove;
WheelFn origWheel;
UpdateFn origUpdate;
MsgFn origCivMsg;
SetVecFn origSetPos, origSetOri;
CamUpdateFn origCamUpdate;
SetCursorFn origSetCursor;
SetVelocityFn origSetVelocity;
SetListenerFn origSetListener;
AddOrderFn origAddOrder;
AddOrderAtFn origAddOrderAt;
SetTargetFn origVehSetTarget;

bool __fastcall HkKeyDown(void* self, void*, int vk, uint32_t mods) {
    if (pilot::OnKeyDown(vk))
        return true;
    return origKeyDown(self, vk, mods);
}

bool __fastcall HkKeyUp(void* self, void*, int vk, uint32_t mods) {
    if (pilot::OnKeyUp(vk))
        return true;
    return origKeyUp(self, vk, mods);
}

bool __fastcall HkMouseDown(void* self, void*, int button, float x, float y, uint32_t state) {
    if (pilot::OnMouseDown(button, x, y))
        return true;
    return origMouseDown(self, button, x, y, state);
}

bool __fastcall HkMouseUp(void* self, void*, int button, float x, float y, uint32_t state) {
    if (pilot::OnMouseUp(button))
        return true;
    return origMouseUp(self, button, x, y, state);
}

bool __fastcall HkMouseMove(void* self, void*, float x, float y, uint32_t state) {
    if (pilot::OnMouseMove(state))
        return true;
    return origMouseMove(self, x, y, state);
}

bool __fastcall HkWheel(void* self, void*, int delta, float x, float y, uint32_t state) {
    if (pilot::OnMouseWheel(delta))
        return true;
    return origWheel(self, delta, x, y, state);
}

int __fastcall HkUpdate(void* self, void*, float d1, float d2) {
    int r = origUpdate(self, d1, d2);
    pilot::AfterCivUpdate(d2 > 0 ? d2 : d1);
    return r;
}

bool __fastcall HkCivMsg(void* self, void*, uint32_t id, void* msg) {
    pilot::OnCivMessage(id, msg);
    return origCivMsg(self, id, msg);
}

void __fastcall HkSetPos(void* self, void*, const void* v) {
    if (pilot::BlockVehicleWrite(self))
        return;
    origSetPos(self, v);
}

void __fastcall HkSetOri(void* self, void*, const void* q) {
    if (pilot::BlockVehicleWrite(self))
        return;
    origSetOri(self, q);
}

void __fastcall HkCamUpdate(void* self, void*, int dt) {
    auto* viewer = static_cast<App::cCameraManager*>(self)->mpViewer;
    if (pilot::FreezeNativeCamera()) {
        pilot::CameraFrame(viewer, dt, false);
        return;
    }
    origCamUpdate(self, dt);
    pilot::CameraFrame(viewer, dt, true);
}

bool __fastcall HkSetCursor(void* self, void*, uint32_t id) {
    uint32_t shown = pilot::FilterCursor(id);
    if (shown != id && origSetCursor(self, shown))
        return true;
    return origSetCursor(self, id);
}

void __fastcall HkSetVelocity(void* self, void*, const float* v) {
    float mine[3];
    if (pilot::VelocityFor(self, mine))
        v = mine;
    origSetVelocity(self, v);
}

void __fastcall HkSetListener(void* self, void*, int index, const float* pos, const float* rot) {
    float p[3], r[9];
    bool replaceRot = false;
    if (pilot::ListenerOverride(index, pos, rot, p, r, replaceRot)) {
        pos = p;
        if (replaceRot)
            rot = r;
    }
    origSetListener(self, index, pos, rot);
}

void __fastcall HkAddOrder(void* self, void*, void* target, int kind, int extra) {
    if (pilot::RefuseOrder(self, target, game::GameCallerVa(static_cast<uintptr_t*>(_AddressOfReturnAddress()))))
        return;
    origAddOrder(self, target, kind, extra);
}

void __fastcall HkAddOrderAt(void* self, void*, int a, int b, int c, int d) {
    if (pilot::RefuseOrder(self, nullptr, game::GameCallerVa(static_cast<uintptr_t*>(_AddressOfReturnAddress()))))
        return;
    origAddOrderAt(self, a, b, c, d);
}

void __fastcall HkVehSetTarget(void* self, void*, void* target) {
    if (pilot::RefuseCombatTarget(self, target))
        return;
    origVehSetTarget(self, target);
}

class AppListener final : public App::IUnmanagedMessageListener {
    bool HandleMessage(uint32_t, void*) override {
        pilot::OnAppUpdate();
        return false;
    }
};

AppListener appListener;

struct SlotPatch {
    uint32_t vtable, offset;
    void* hook;
    void** orig;
};

bool PatchSlots(const SlotPatch* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        auto* slot = game::Va<void**>(p[i].vtable + p[i].offset);
        DWORD old = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old))
            return false;
        *p[i].orig = *slot;
        *slot = p[i].hook;
        VirtualProtect(slot, sizeof(void*), old, &old);
    }
    return true;
}

struct FnHook {
    uint32_t va;
    void* hook;
    void** orig;
    bool chain;
};

bool AttachAll(FnHook* hooks, size_t n) {
    LONG err = DetourTransactionBegin();
    if (err == NO_ERROR)
        err = DetourUpdateThread(GetCurrentThread());
    for (size_t i = 0; i < n && err == NO_ERROR; ++i) {
        FnHook& h = hooks[i];
        if (uintptr_t t = game::ForeignHookTarget(h.va)) {
            *h.orig = reinterpret_cast<void*>(t);
            h.chain = true;
        } else {
            *h.orig = game::Va<void*>(h.va);
            err = DetourAttach(h.orig, h.hook);
        }
    }
    if (err != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    if ((err = DetourTransactionCommit()) != NO_ERROR)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (hooks[i].chain)
            game::ChainInFront(hooks[i].va, hooks[i].hook);
    return true;
}

bool Install() {
    if (!game::Verify())
        return false;
    FnHook hooks[6];
    size_t n = 0;
    hooks[n++] = {game::raw::SetVelocity, reinterpret_cast<void*>(&HkSetVelocity),
                  reinterpret_cast<void**>(&origSetVelocity), false};
    if (game::CursorHookUsable())
        hooks[n++] = {0x801cf0, reinterpret_cast<void*>(&HkSetCursor), reinterpret_cast<void**>(&origSetCursor), false};
    if (game::OrderHookUsable()) {
        hooks[n++] = {game::raw::AddOrder, reinterpret_cast<void*>(&HkAddOrder),
                      reinterpret_cast<void**>(&origAddOrder), false};
        hooks[n++] = {game::raw::AddOrderAt, reinterpret_cast<void*>(&HkAddOrderAt),
                      reinterpret_cast<void**>(&origAddOrderAt), false};
    }
    if (game::ListenerHookUsable())
        hooks[n++] = {game::raw::SetListenerPosition, reinterpret_cast<void*>(&HkSetListener),
                      reinterpret_cast<void**>(&origSetListener), false};
    if (!AttachAll(hooks, n))
        return false;
    game::origSetVelocity = reinterpret_cast<void*>(origSetVelocity);
    if (game::CursorHookUsable())
        game::origSetActiveCursor = reinterpret_cast<void*>(origSetCursor);
    if (game::ListenerHookUsable())
        game::origSetListener = reinterpret_cast<void*>(origSetListener);
    if (game::OrderHookUsable()) {
        game::origAddOrder = reinterpret_cast<void*>(origAddOrder);
        game::origAddOrderAt = reinterpret_cast<void*>(origAddOrderAt);
    }

    using namespace game;
    const SlotPatch patches[] = {
        {raw::GameCivVtable, kSlotKeyDown, reinterpret_cast<void*>(&HkKeyDown), reinterpret_cast<void**>(&origKeyDown)},
        {raw::GameCivVtable, kSlotKeyUp, reinterpret_cast<void*>(&HkKeyUp), reinterpret_cast<void**>(&origKeyUp)},
        {raw::GameCivVtable, kSlotMouseDown, reinterpret_cast<void*>(&HkMouseDown),
         reinterpret_cast<void**>(&origMouseDown)},
        {raw::GameCivVtable, kSlotMouseUp, reinterpret_cast<void*>(&HkMouseUp), reinterpret_cast<void**>(&origMouseUp)},
        {raw::GameCivVtable, kSlotMouseMove, reinterpret_cast<void*>(&HkMouseMove),
         reinterpret_cast<void**>(&origMouseMove)},
        {raw::GameCivVtable, kSlotMouseWheel, reinterpret_cast<void*>(&HkWheel), reinterpret_cast<void**>(&origWheel)},
        {raw::GameCivVtable, kSlotUpdate, reinterpret_cast<void*>(&HkUpdate), reinterpret_cast<void**>(&origUpdate)},
        {raw::CivUiListenerVtable, 0x04, reinterpret_cast<void*>(&HkCivMsg), reinterpret_cast<void**>(&origCivMsg)},
        {raw::VehicleSpatialVtable, 0x38, reinterpret_cast<void*>(&HkSetPos), reinterpret_cast<void**>(&origSetPos)},
        {raw::VehicleSpatialVtable, 0x3c, reinterpret_cast<void*>(&HkSetOri), reinterpret_cast<void**>(&origSetOri)},
        {raw::CameraManagerVtable, 0x2c, reinterpret_cast<void*>(&HkCamUpdate),
         reinterpret_cast<void**>(&origCamUpdate)},
    };
    if (!PatchSlots(patches, sizeof(patches) / sizeof(patches[0])))
        return false;
    if (game::StanceUsable()) {
        const SlotPatch combat[] = {{raw::VehicleCombatantVtable, 0x50, reinterpret_cast<void*>(&HkVehSetTarget),
                                     reinterpret_cast<void**>(&origVehSetTarget)}};
        PatchSlots(combat, 1);
        game::origVehSetTarget = reinterpret_cast<void*>(origVehSetTarget);
    }
    game::origSetPosition = reinterpret_cast<void*>(origSetPos);
    game::origSetOrientation = reinterpret_cast<void*>(origSetOri);
    return true;
}

void Init() {
    if (!Install())
        return;
    auto* messages = reinterpret_cast<App::IMessageManager* (*)()>(GetAddress(App::IMessageManager, Get))();
    if (messages)
        messages->AddUnmanagedListener(&appListener, game::kAppUpdate);
}
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        ModAPI::AddPostInitFunction(Init);
    }
    return TRUE;
}


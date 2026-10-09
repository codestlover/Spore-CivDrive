
#pragma once
#include <cstdint>

namespace App {
class cViewer;
}

namespace pilot {
bool OnKeyDown(int vk);
bool OnKeyUp(int vk);
bool OnMouseDown(int button, float x, float y);
bool OnMouseUp(int button);
bool OnMouseMove(uint32_t& mouseState);
bool OnMouseWheel(int delta);

void AfterCivUpdate(float realSeconds);
bool FreezeNativeCamera(int activeCamera);
void CameraFrame(App::cViewer* viewer, int deltaMs, bool nativeRan);
void OnAppUpdate();

void OnCivMessage(uint32_t id, void* msg);
bool BlockVehicleWrite(const void* spatial);
bool VelocityFor(const void* locomotive, float out[3]);
uint32_t FilterCursor(uint32_t id);
bool RefuseOrder(const void* vehicle, const void* target, int kind, uint32_t callerVa);
bool RefuseCombatTarget(const void* combatant, const void* target);
bool ListenerOverride(int index, const float* gamePos, const float* gameRot, float pos[3], float rot[9],
                      bool& replaceRot);
}


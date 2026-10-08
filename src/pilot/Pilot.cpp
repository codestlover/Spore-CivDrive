
#include "pilot/Pilot.hpp"
#include "core/Config.hpp"
#include "render/Effect.hpp"
#include "game/Game.hpp"
#include "render/Overlay.hpp"
#include <Spore/Simulator/cVehicle.h>
#include <Spore/Simulator/cCommodityNode.h>
#include <cstddef>
#include <cmath>
#include <cstring>

using namespace Simulator;
using vm::V3;

namespace pilot {
namespace {
enum class Mode { Idle, Entering, Active, Exiting };

constexpr int kMouseLeft = 1000, kMouseMiddle = 1001, kMouseRight = 1002;

constexpr uint32_t kCursorAttack = 0x3570021;
constexpr uint32_t kCursorDefault = 0x1002;
constexpr uint32_t kCursorNone = 0x1001;

constexpr uint32_t kCursorGoodieHut = 0x56e3a22;
constexpr float kLookUpMax = 1.25f;
constexpr float kClickSlop = 4.0f;
constexpr float kMarkerScale = 0.7f;

bool IsActionCursor(uint32_t id) {
    switch (id) {
    case 0x3570021:
    case 0x3febe41:
    case 0x5d53800:
        return true;
    }
    return false;
}

struct State {
    Mode mode = Mode::Idle;
    cVehicle* veh = nullptr;
    cSpatialObject* sp = nullptr;
    const void* lock = nullptr;
    int loco = 0, purpose = 0;

    V3 pos, vel;
    V3 fwd{0, 1, 0};
    float speed = 0, maxSpeed = 12, turn = 1.7f, ground = 0, airAlt = 20, radius = 0, bank = 0, bound = 3;
    V3 lf{0, 1, 0}, lu{0, 0, 1};
    bool transposed = false;

    float camYaw = 0, camPitch = 0.36f, zoom = 1, baseDist = 20, lookH = 2;
    V3 camPos, camTarget;
    bool camInit = false;
    game::CamXf from, last;
    bool haveLast = false;
    float t = 0, dur = 1;
    App::cViewer* viewer = nullptr;

    bool swallowBtn[8] = {};
    bool swallowKey[256] = {};
    bool orbit = false;
    int orbitVk = VK_MBUTTON;
    float orbitMoved = 0;
    DWORD orbitTick = 0;
    POINT anchor{};
    int wheel = 0;
    const void* lastClickVeh = nullptr;
    DWORD lastClickTick = 0;
    float lastClickX = 0, lastClickY = 0;
    bool shiftWas = false, escWas = false;
    DWORD lastShiftTick = 0;

    bool firing = false;
    const void* fireTarget = nullptr;
    const void* orderTarget = nullptr;
    DWORD lastFireTick = 0, lastSelectCheck = 0;
    cGameData* hover = nullptr;
    bool hoverValid = false;
    int actKey = 0;

    uint32_t nativeCursor = kCursorDefault;
    DWORD nativeCursorTick = 0;
    uint32_t forcedCursor = 0;

    float arrowAngle = -vm::kPi / 2;

    V3 tiltUp{0, 0, 1};
    float smoothR = 0, pitchDyn = 0, rollDyn = 0, lastSpeed = 0, time = 0;
    bool tiltInit = false;

    cGameData* dock = nullptr;
    DWORD dockTick = 0;
    cGameData* raid = nullptr;
    V3 raidAt;
    float raidReach = 0;
    bool raidDocking = false;
    DWORD raidScan = 0;
    const void* triedHut = nullptr;
    V3 triedHutAt;
    float triedHutReach = 0;
    const void* refusedTarget = nullptr;

    bool ownAct = false;
    bool uiOrderSet = false;
    const void* uiOrder = nullptr;
    DWORD rolloverTick = 0;
    DWORD rolloverClickTick = 0;
    bool cinematic = false;

    cVehicle* pending = nullptr;
};

State S;

DWORD Now() {
    return GetTickCount();
}

bool Down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool Piloting() {
    return S.mode == Mode::Entering || S.mode == Mode::Active;
}

float Wrap(float a) {
    while (a > vm::kPi)
        a -= 2 * vm::kPi;
    while (a < -vm::kPi)
        a += 2 * vm::kPi;
    return a;
}

game::CamXf Blend(const game::CamXf& a, const game::CamXf& b, float e) {
    game::CamXf r;
    r.pos = vm::Lerp(a.pos, b.pos, e);
    r.rot = vm::ToMatrix(vm::Slerp(vm::ToQuat(a.rot), vm::ToQuat(b.rot), e));
    float s = vm::Len(game::ToSurface(r.pos)) + 1.5f;
    if (vm::Len(r.pos) < s)
        r.pos = vm::Norm(r.pos) * s;
    return r;
}

void RestoreCursor() {
    if (S.forcedCursor) {
        game::SetNativeCursor(S.nativeCursor ? S.nativeCursor : kCursorDefault);
        S.forcedCursor = 0;
    }
}

uint32_t WantedCursor() {
    if (S.orbit && (S.orbitVk == VK_MBUTTON || S.orbitMoved > kClickSlop))
        return kCursorNone;
    if (S.hoverValid && game::IsCityHall(S.hover))
        return kCursorAttack;
    if (S.hoverValid)
        return IsActionCursor(S.nativeCursor) && Now() - S.nativeCursorTick < 300 ? S.nativeCursor : kCursorAttack;
    return 0;
}

void UpdateCursor() {
    uint32_t want = WantedCursor();
    if (want) {
        if (game::ActiveCursorId() != want && game::SetNativeCursor(want))
            S.forcedCursor = want;
    } else {
        RestoreCursor();
    }
}

void InitAxes() {
    V3 pos = game::Position(S.sp);
    V3 up = vm::Norm(pos);
    vm::M3 m0 = vm::ToMatrix(game::Orientation(S.sp));
    V3 dir = game::Direction(S.sp);
    bool dirOk = vm::Finite(dir) && vm::Len(dir) > 0.5f;
    const V3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float bestScore = -1;
    int bestConv = 0, bestAxis = 1;
    float bestSign = 1;
    if (dirOk) {
        for (int conv = 0; conv < 2; ++conv) {
            vm::M3 mc = conv ? vm::Transposed(m0) : m0;
            V3 l = vm::ToLocal(mc, vm::Norm(dir));
            float c[3] = {l.x, l.y, l.z};
            for (int k = 0; k < 3; ++k)
                if (std::fabs(c[k]) > bestScore) {
                    bestScore = std::fabs(c[k]);
                    bestConv = conv;
                    bestAxis = k;
                    bestSign = c[k] < 0 ? -1.0f : 1.0f;
                }
        }
    }
    S.transposed = bestConv == 1;
    S.lf = axes[bestAxis] * bestSign;
    vm::M3 mc = S.transposed ? vm::Transposed(m0) : m0;
    V3 lu = vm::ToLocal(mc, up);
    float c[3] = {lu.x, lu.y, lu.z};
    int upAxis = bestAxis == 2 ? 1 : 2;
    float upBest = -1, upSign = 1;
    for (int k = 0; k < 3; ++k)
        if (k != bestAxis && std::fabs(c[k]) > upBest) {
            upBest = std::fabs(c[k]);
            upAxis = k;
            upSign = c[k] < 0 ? -1.0f : 1.0f;
        }
    S.lu = axes[upAxis] * upSign;
    V3 f = dirOk ? dir : vm::ToWorld(mc, S.lf);
    S.fwd = vm::Norm(vm::Flatten(f, up), vm::Norm(vm::Flatten({0, 0, 1}, up), {1, 0, 0}));
}

vm::Q BuildOrientation(const V3& up, const V3& fwd, float bank) {
    V3 u = bank != 0 ? vm::Rotate(up, fwd, bank) : up;
    vm::M3 a = vm::M3::FromRows(vm::Cross(S.lf, S.lu), S.lf, S.lu);
    vm::M3 b = vm::M3::FromRows(vm::Norm(vm::Cross(fwd, u)), fwd, u);
    vm::M3 m = vm::Mul(vm::Transposed(a), b);
    if (S.transposed)
        m = vm::Transposed(m);
    return vm::ToQuat(m);
}

void InitMotion() {
    S.pos = game::Position(S.sp);
    S.vel = {0, 0, 0};
    float r = vm::Len(S.pos);
    float sr = vm::Len(game::ToSurface(S.pos));
    S.radius = r;
    S.ground = 0;
    if (S.loco == kVehicleLand)
        S.ground = vm::Clamp(r - sr, -1.0f, 4.0f);
    else if (S.loco == kVehicleWater)
        S.ground = vm::Clamp(r - sr, -2.0f, 2.0f);
    else
        S.airAlt = vm::Clamp(r - sr, 8.0f, 150.0f);
    float std = S.veh->mStandardSpeed, des = S.veh->mDesiredSpeed;
    float base = std > des ? std : des;
    if (!(base >= 3.0f && base < 500.0f))
        base = S.loco == kVehicleAir ? 22.0f : (S.loco == kVehicleWater ? 12.0f : 14.0f);
    S.maxSpeed = base * cfg::Get().speedMul;
    S.turn = cfg::Get().turnRate * (S.loco == kVehicleWater ? 0.8f : (S.loco == kVehicleAir ? 0.85f : 1.0f));
    S.speed = 0;
    S.bank = 0;
    S.bound = vm::Clamp(game::BoundingRadius(S.sp), 1.0f, 20.0f);
    S.tiltInit = false;
    S.smoothR = r;
    S.pitchDyn = S.rollDyn = 0;
    S.lastSpeed = 0;
    S.dock = nullptr;
}

void InitCamera() {
    const auto& c = cfg::Get();
    S.baseDist = vm::Clamp(S.bound * 7.0f, 14.0f, 90.0f) * c.camDistance * (S.loco == kVehicleAir ? 1.25f : 1.0f);
    S.lookH = (S.bound * 0.9f + 1.0f) * c.camHeight;
    S.camYaw = 0;
    S.camPitch = S.loco == kVehicleAir ? 0.30f : 0.36f;
    S.zoom = 1;
    S.camInit = false;
    S.wheel = 0;
}

bool CityBlocks(const V3& from, const V3& to, V3* center = nullptr) {
    V3 cs[8];
    float rs[8];
    int n = game::CityObstacles(to, cs, rs, 8);
    for (int i = 0; i < n; ++i) {
        float R = rs[i] + S.bound * 0.6f + 1.0f;
        float d1 = vm::Len(to - cs[i]), d0 = vm::Len(from - cs[i]);
        if (d1 < R && d1 < d0 - 1e-4f) {
            if (center)
                *center = cs[i];
            return true;
        }
    }
    return false;
}

bool LandStepBlocked(const V3& from, const V3& to) {
    V3 up = vm::Norm(to);
    V3 dir = vm::Norm(vm::Flatten(to - from, up), S.fwd);
    V3 nose = to + dir * (S.bound * 0.5f);
    if (!game::IsInWater(from)) {
        if (game::IsInWater(to) || game::IsInWater(nose))
            return true;
    } else if (game::IsInWater(to) && game::HeightAt(to) <= game::HeightAt(from)) {
        return true;
    }
    return false;
}

bool ShipStepBlocked(const V3& from, const V3& to) {
    V3 up = vm::Norm(to);
    V3 dir = vm::Norm(vm::Flatten(to - from, up), S.fwd);
    V3 nose = to + dir * (S.bound * 0.5f);
    if (game::IsInWater(from))
        return !game::IsInWater(to) || !game::IsInWater(nose);
    return game::IsInWater(to) ? false : game::HeightAt(to) >= game::HeightAt(from);
}

bool Blocked(const V3& from, const V3& to) {
    if (S.loco == kVehicleLand && CityBlocks(from, to))
        return true;
    if (S.loco != kVehicleLand && S.loco != kVehicleWater)
        return false;
    float len = vm::Len(to - from);
    int n = len > 1.0f ? int(std::ceil(len / 1.0f)) : 1;
    if (n > 12)
        n = 12;
    V3 a = from;
    for (int i = 1; i <= n; ++i) {
        V3 b = vm::Lerp(from, to, float(i) / float(n));
        if (S.loco == kVehicleLand ? LandStepBlocked(a, b) : ShipStepBlocked(a, b))
            return true;
        a = b;
    }
    return false;
}

void Drive(float dt) {
    V3 pos = S.pos;
    V3 up = vm::Norm(pos);
    S.fwd = vm::Norm(vm::Flatten(S.fwd, up), vm::Norm(vm::Flatten({0, 0, 1}, up), {1, 0, 0}));
    V3 next = pos;

    if (dt > 0) {
        bool focus = game::GameHasFocus();
        float throttle = 0, steer = 0;
        if (focus) {
            throttle = float((Down('W') || Down(VK_UP)) ? 1 : 0) - float((Down('S') || Down(VK_DOWN)) ? 1 : 0);
            steer = float((Down('D') || Down(VK_RIGHT)) ? 1 : 0) - float((Down('A') || Down(VK_LEFT)) ? 1 : 0);
        }
        float target = throttle > 0 ? S.maxSpeed : (throttle < 0 ? -S.maxSpeed * 0.5f : 0.0f);
        float rate =
            (std::fabs(target) > std::fabs(S.speed) && target * S.speed >= 0) ? S.maxSpeed * 1.4f : S.maxSpeed * 2.6f;
        if (S.speed < target)
            S.speed = S.speed + rate * dt > target ? target : S.speed + rate * dt;
        else if (S.speed > target)
            S.speed = S.speed - rate * dt < target ? target : S.speed - rate * dt;

        float moving = vm::Clamp(std::fabs(S.speed) / (S.maxSpeed * 0.35f), 0.0f, 1.0f);
        float yaw = -steer * S.turn * (0.55f + 0.45f * moving) * dt;
        if (S.speed < -0.1f)
            yaw = -yaw;
        S.fwd = vm::Norm(vm::Rotate(S.fwd, up, yaw), S.fwd);

        V3 step = S.fwd * (S.speed * dt);
        if (vm::Len(step) > 1e-5f) {
            V3 cand = pos + step;
            if (!Blocked(pos, cand)) {
                next = cand;
            } else {
                bool moved = false;
                V3 c;
                if (S.loco == kVehicleLand && CityBlocks(pos, cand, &c)) {
                    V3 n = vm::Norm(vm::Flatten(pos - c, up));
                    V3 slide = step - n * std::fmin(0.0f, vm::Dot(step, n));
                    V3 c2 = pos + slide;
                    if (vm::Len(slide) > 1e-5f && !Blocked(pos, c2)) {
                        next = c2;
                        moved = true;
                    }
                }
                const float tries[4] = {0.6f, -0.6f, 1.2f, -1.2f};
                for (int i = 0; i < 4 && !moved; ++i) {
                    V3 dir = vm::Rotate(S.fwd, up, tries[i]);
                    V3 c2 = pos + dir * (S.speed * dt * std::cos(tries[i]));
                    if (!Blocked(pos, c2)) {
                        next = c2;
                        moved = true;
                    }
                }
                if (!moved)
                    S.speed = 0;
            }
        }
    }

    V3 nUp = vm::Norm(next);
    V3 right = vm::Norm(vm::Cross(S.fwd, nUp), {1, 0, 0});
    float sr = vm::Len(game::ToSurface(next));
    float r;
    V3 bodyUp = nUp;
    S.time += dt;
    if (S.loco == kVehicleAir) {
        if (dt > 0) {
            float want = sr + S.airAlt;
            S.radius += (want - S.radius) * vm::Approach(dt, 0.4f);
            float steer = (game::GameHasFocus() ? float((Down('D') || Down(VK_RIGHT)) ? 1 : 0) -
                                                      float((Down('A') || Down(VK_LEFT)) ? 1 : 0)
                                                : 0.0f);
            float bankWant = steer * 0.35f * vm::Clamp(std::fabs(S.speed) / S.maxSpeed, 0.2f, 1.0f);
            S.bank += (bankWant - S.bank) * vm::Approach(dt, 0.25f);
        }
        r = S.radius < sr + 3.0f ? sr + 3.0f : S.radius;
        S.radius = r;
        bodyUp = vm::Rotate(nUp, S.fwd, S.bank);
    } else {
        float d = vm::Clamp(S.bound * 0.75f, 1.0f, 8.0f);
        V3 pF = game::ToSurface(next + S.fwd * d), pB = game::ToSurface(next - S.fwd * d);
        V3 pR = game::ToSurface(next + right * d), pL = game::ToSurface(next - right * d);
        float avg = (sr * 2 + vm::Len(pF) + vm::Len(pB) + vm::Len(pR) + vm::Len(pL)) / 6.0f;
        V3 n = vm::Norm(vm::Cross(pR - pL, pF - pB), nUp);
        if (S.loco == kVehicleWater || vm::Dot(n, nUp) < 0.45f)
            n = nUp;
        if (!S.tiltInit) {
            S.tiltUp = n;
            S.smoothR = avg + S.ground;
            S.tiltInit = true;
        }
        float k = dt > 0 ? vm::Approach(dt, 0.12f) : 0.0f;
        S.tiltUp = vm::Norm(vm::Lerp(S.tiltUp, n, k), nUp);
        S.smoothR += (avg + S.ground - S.smoothR) * (dt > 0 ? vm::Approach(dt, 0.07f) : 0.0f);
        r = S.smoothR < sr + S.ground - 0.25f ? sr + S.ground - 0.25f : S.smoothR;
        if (dt > 0) {
            float accel = (S.speed - S.lastSpeed) / dt;
            float pitchWant = vm::Clamp(-accel * 0.0035f, -0.06f, 0.06f);
            float steer = (game::GameHasFocus() ? float((Down('D') || Down(VK_RIGHT)) ? 1 : 0) -
                                                      float((Down('A') || Down(VK_LEFT)) ? 1 : 0)
                                                : 0.0f);
            float rollWant = vm::Clamp(steer * std::fabs(S.speed) / S.maxSpeed * 0.06f, -0.06f, 0.06f);
            S.pitchDyn += (pitchWant - S.pitchDyn) * vm::Approach(dt, 0.18f);
            S.rollDyn += (rollWant - S.rollDyn) * vm::Approach(dt, 0.18f);
        }
        bodyUp = S.tiltUp;
        if (S.loco == kVehicleWater) {
            bodyUp = vm::Rotate(bodyUp, S.fwd, 0.025f * std::sin(S.time * 1.3f));
            bodyUp = vm::Rotate(bodyUp, right, 0.018f * std::sin(S.time * 0.9f + 1.0f));
            r += 0.12f * std::sin(S.time * 1.7f);
        }
        bodyUp = vm::Rotate(bodyUp, right, S.pitchDyn);
        bodyUp = vm::Rotate(bodyUp, S.fwd, S.rollDyn);
    }
    S.lastSpeed = S.speed;
    next = nUp * r;
    if (!vm::Finite(next))
        next = pos;
    S.fwd = vm::Norm(vm::Flatten(S.fwd, nUp), S.fwd);
    S.vel = dt > 0 ? (next - pos) * (1.0f / dt) : V3{0, 0, 0};
    S.pos = next;

    V3 bodyFwd = vm::Norm(vm::Flatten(S.fwd, bodyUp), S.fwd);
    game::RawSetPosition(S.sp, next);
    game::RawSetOrientation(S.sp, BuildOrientation(bodyUp, bodyFwd, 0.0f));
    S.veh->mVelocity.x = S.vel.x;
    S.veh->mVelocity.y = S.vel.y;
    S.veh->mVelocity.z = S.vel.z;
    S.veh->mIdlePosition.x = next.x;
    S.veh->mIdlePosition.y = next.y;
    S.veh->mIdlePosition.z = next.z;
}

void UpdateClaim() {
    if (S.dock && game::CurrentOrderTarget(S.veh) != S.dock)
        S.dock = nullptr;
}

bool TypeValid(cGameData* obj) {
    uint32_t me = game::PlayerPoliticalID();
    uint32_t pid = obj->mPoliticalID;
    if (auto* v = game::AsVehicle(obj))
        return pid != me && game::VehicleAlive(v);
    if (game::Cast(obj, 0xE9CB8BA) || game::Cast(obj, 0x436F315))
        return pid != me && pid != uint32_t(-1);
    if (game::Cast(obj, 0x403DF5F))
        return pid != me;
    return false;
}

void EnsureSelected(bool force) {
    DWORD now = Now();
    if (!force && now - S.lastSelectCheck < 400)
        return;
    S.lastSelectCheck = now;
    if (!game::IsSelected(S.sp) || game::SelectedCount() != 1)
        game::SelectOnly(S.sp);
}

bool IsClaimTarget(cGameData* t) {
    if (!t)
        return false;
    return game::Cast(t, cCommodityNode::TYPE) && t->mPoliticalID == uint32_t(-1);
}

int InActionRange(cGameData* t) {
    if (!t)
        return -1;
    auto* ts = static_cast<cSpatialObject*>(game::Cast(t, 0x1186577));
    if (IsClaimTarget(t)) {
        if (!ts)
            return -1;
        return vm::Len(game::Position(ts) - S.pos) <= game::ClaimRadius() ? 1 : 0;
    }
    float minR = 0, maxR = 0;
    if (!game::WeaponRange(S.veh, minR, maxR))
        return -1;
    int in = game::InWeaponRange(S.veh, t);
    if (in >= 0)
        return in;
    if (!ts)
        return -1;
    return vm::Len(game::Position(ts) - S.pos) <= maxR ? 1 : 0;
}

bool CityHallAttackable(cGameData* hall) {
    uint32_t pid = hall ? uint32_t(hall->mPoliticalID) : uint32_t(-1);
    return game::IsCityHall(hall) && S.purpose == kVehicleMilitary && pid != uint32_t(-1) &&
           pid != game::PlayerPoliticalID() && game::CityDefenseless(game::CityOf(hall));
}

void Fire(cGameData* target) {
    if (!target || !S.veh)
        return;
    if (game::IsCityHall(target)) {
        EnsureSelected(true);
        S.ownAct = true;
        game::AttackCity(S.veh, game::CityOf(target));
        S.ownAct = false;
        S.orderTarget = game::CurrentOrderTarget(S.veh);
        S.fireTarget = target;
        S.lastFireTick = Now();
        S.dock = nullptr;
        return;
    }
    if (IsClaimTarget(target)) {
        if (game::StartClaim(target, S.veh)) {
            S.fireTarget = target;
            S.lastFireTick = Now();
            return;
        }
    }
    EnsureSelected(true);
    S.ownAct = true;
    game::Act(S.veh, target, S.actKey);
    S.ownAct = false;
    S.orderTarget = game::CurrentOrderTarget(S.veh);
    S.fireTarget = target;
    S.lastFireTick = Now();
    S.dock = IsClaimTarget(target) && S.orderTarget ? target : nullptr;
    S.dockTick = Now();
}

bool TryFire(cGameData* target) {
    if (InActionRange(target) == 0) {
        if (S.refusedTarget != target) {
            game::PlayRefusal();
            S.refusedTarget = target;
        }
        return false;
    }
    S.refusedTarget = nullptr;
    Fire(target);
    return true;
}

void AttackPress() {
    cGameData* target = S.hoverValid ? S.hover : nullptr;
    S.refusedTarget = nullptr;
    if (S.loco == kVehicleAir) {
        S.firing = true;
        if (target)
            TryFire(target);
    } else if (target) {
        TryFire(target);
    }
}

void AttackRelease() {
    if (S.loco == kVehicleAir && S.firing) {
        S.firing = false;
        S.fireTarget = nullptr;
        if (S.veh)
            game::ClearOrders(S.veh);
    }
}

void UpdateAim() {
    bool overUI = game::MouseOverUI() && !game::OverCityRollover();
    S.hover = overUI ? nullptr : game::PickHovered();
    if (S.hover && game::AsVehicle(S.hover) == S.veh)
        S.hover = nullptr;
    bool nativeFresh = Now() - S.nativeCursorTick < 300;
    S.hoverValid = S.hover && (nativeFresh ? IsActionCursor(S.nativeCursor) : TypeValid(S.hover));
    if (S.hover && game::IsCityHall(S.hover))
        S.hoverValid = CityHallAttackable(S.hover);

    if (S.loco == kVehicleAir && S.firing) {
        if (!Down(VK_LBUTTON) || !game::GameHasFocus()) {
            AttackRelease();
        } else if (S.hoverValid && S.hover != S.fireTarget && Now() - S.lastFireTick > 250) {
            TryFire(S.hover);
        } else if (S.fireTarget && !game::CurrentOrderTarget(S.veh) && S.hoverValid && Now() - S.lastFireTick > 400) {
            TryFire(S.hover);
        } else if (S.fireTarget && InActionRange(static_cast<cGameData*>(const_cast<void*>(S.fireTarget))) == 0 &&
                   game::CurrentOrderTarget(S.veh) == S.fireTarget) {
            game::ClearOrders(S.veh);
            S.fireTarget = nullptr;
        }
    }

    if (!cfg::Get().autonomousFire) {
        cGameData* t = game::CurrentOrderTarget(S.veh);
        bool ui = S.uiOrderSet && t && (!S.uiOrder || t == S.uiOrder);
        bool mine =
            t && (ui || t == S.raid || t == S.dock || (t == S.orderTarget && (S.loco != kVehicleAir || S.firing)));
        if (t && !mine)
            game::ClearOrders(S.veh);
    }
    EnsureSelected(false);
    UpdateCursor();
}

void UpdateRaid() {
    if (S.raid) {
        cGameData* t = game::CurrentOrderTarget(S.veh);
        bool over = t != S.raid || S.raid->mbIsDestroyed;
        if (!over && vm::Len(S.raidAt - S.pos) <= S.raidReach + 6.0f)
            return;
        if (!over)
            game::ClearOrders(S.veh);
        S.raid->Release();
        S.raid = nullptr;
        return;
    }
    if (S.triedHut && vm::Len(S.triedHutAt - S.pos) > S.triedHutReach + 6.0f)
        S.triedHut = nullptr;
    DWORD now = Now();
    if (int(now - S.raidScan) < 150)
        return;
    S.raidScan = now;
    V3 c;
    float r = 0;
    cGameData* hut = game::NearestTribeHut(S.pos, c, r);
    float reach = S.bound + r + 1.5f;
    if (!hut || hut == S.triedHut || vm::Len(c - S.pos) > reach)
        return;
    S.triedHut = hut;
    S.triedHutAt = c;
    S.triedHutReach = reach;
    EnsureSelected(true);
    S.ownAct = true;
    game::Act(S.veh, hut, S.actKey);
    S.ownAct = false;
    if (cGameData* t = game::CurrentOrderTarget(S.veh)) {
        t->AddRef();
        S.raid = t;
        S.raidAt = c;
        S.raidReach = reach;
    }
}

bool RaidDocking() {
    bool want = false;
    if (S.mode == Mode::Active) {
        bool hands = game::GameHasFocus() && (Down('W') || Down('A') || Down('S') || Down('D') || Down(VK_UP) ||
                                              Down(VK_DOWN) || Down(VK_LEFT) || Down(VK_RIGHT));
        cGameData* live = game::CurrentOrderTarget(S.veh);
        if (S.uiOrderSet && (!live || (S.uiOrder && live != S.uiOrder))) {
            S.uiOrderSet = false;
        }
        cGameData* t = hands ? nullptr : live;
        if (t && S.uiOrderSet && (!S.uiOrder || t == S.uiOrder)) {
            want = true;
        } else if (t && t == S.raid) {
            want = vm::Len(S.raidAt - S.pos) <= S.raidReach;
        } else if (t && t == S.dock && Now() - S.dockTick > 1500) {
            auto* node = static_cast<cCommodityNode*>(game::Cast(S.dock, cCommodityNode::TYPE));
            want = node && !node->mConstructingVehicle.get() &&
                   vm::Len(game::Position(static_cast<cSpatialObject*>(node)) - S.pos) <= game::ClaimRadius() + 2.0f;
        }
    }
    if (want != S.raidDocking) {
        S.raidDocking = want;
        S.lock = want ? nullptr : S.sp;
        if (!want) {
            S.pos = game::Position(S.sp);
            S.speed = 0;
        }
    }
    if (S.raidDocking) {
        S.pos = game::Position(S.sp);
        S.vel = {S.veh->mVelocity.x, S.veh->mVelocity.y, S.veh->mVelocity.z};
        S.speed = 0;
    }
    return S.raidDocking;
}

void GroundRing(const V3& center, float radius, bool onGround, V3* out) {
    V3 up = vm::Norm(center);
    V3 a = vm::Norm(vm::Cross(S.fwd, up), {1, 0, 0});
    V3 b = vm::Norm(vm::Cross(up, a));
    float cr = vm::Len(center);
    for (int i = 0; i <= overlay::kRingPoints; ++i) {
        float th = 2 * vm::kPi * float(i) / float(overlay::kRingPoints);
        V3 q = center + (a * std::cos(th) + b * std::sin(th)) * radius;
        if (onGround) {
            float s = vm::Len(game::ToSurface(q));
            out[i] = vm::Norm(q) * (s + 0.6f);
        } else {
            out[i] = vm::Norm(q) * cr;
        }
    }
}

void UpdateOverlay() {
    static V3 pts[overlay::kRingPoints + 1];
    float minR = 0, maxR = 0;
    bool weapon = game::WeaponRange(S.veh, minR, maxR);
    bool claim = S.hoverValid && IsClaimTarget(S.hover);
    auto* ts = S.hoverValid ? static_cast<cSpatialObject*>(game::Cast(S.hover, 0x1186577)) : nullptr;

    const uint32_t neutral = 0x90D8ECFF, inside = 0xD860FF7A, outside = 0xE0FF9A3C;
    int in = S.hoverValid ? InActionRange(S.hover) : -1;
    uint32_t status = in < 0 ? neutral : (in ? inside : outside);

    if (weapon) {
        V3 groundPt = game::ToSurface(S.pos);
        float h = vm::Len(S.pos) - vm::Len(groundPt);
        float rh = maxR > h ? std::sqrt(maxR * maxR - h * h) : 0.0f;
        if (rh >= 1.0f)
            GroundRing(groundPt, rh, true, pts);
        else
            GroundRing(S.pos, maxR, false, pts);
        overlay::SetRing(overlay::kRingRange, pts, overlay::kRingPoints + 1, claim ? 0x50D8ECFF : status, 2.6f);
        if (minR > 2.0f && minR < maxR && rh >= 1.0f) {
            float rmin = minR > h ? std::sqrt(minR * minR - h * h) : 0.0f;
            if (rmin >= 1.0f) {
                GroundRing(groundPt, rmin, true, pts);
                overlay::SetRing(overlay::kRingMinRange, pts, overlay::kRingPoints + 1, 0x50FFFFFF, 1.5f);
            } else {
                overlay::SetRing(overlay::kRingMinRange, nullptr, 0, 0, 0);
            }
        } else {
            overlay::SetRing(overlay::kRingMinRange, nullptr, 0, 0, 0);
        }
    } else {
        overlay::SetRing(overlay::kRingRange, nullptr, 0, 0, 0);
        overlay::SetRing(overlay::kRingMinRange, nullptr, 0, 0, 0);
    }

    if (ts) {
        float r = claim ? game::ClaimRadius() : vm::Clamp(game::BoundingRadius(ts) * 1.15f, 2.5f, 45.0f);
        V3 tp = game::Position(ts), ground = game::ToSurface(tp);
        bool airborne = !claim && vm::Len(tp) - vm::Len(ground) > game::BoundingRadius(ts) + 3.0f;
        if (airborne)
            GroundRing(tp, r, false, pts);
        else
            GroundRing(ground, r, true, pts);
        overlay::SetRing(overlay::kRingTarget, pts, overlay::kRingPoints + 1, status | 0xFF000000, claim ? 2.6f : 2.0f);
    } else if (S.dock) {
        auto* ds = static_cast<cSpatialObject*>(game::Cast(S.dock, 0x1186577));
        if (ds) {
            GroundRing(game::ToSurface(game::Position(ds)), game::ClaimRadius(), true, pts);
            overlay::SetRing(overlay::kRingTarget, pts, overlay::kRingPoints + 1, 0xD860FF7A, 2.6f);
        }
    } else {
        overlay::SetRing(overlay::kRingTarget, nullptr, 0, 0, 0);
    }

    game::MinimapIcon icon{};
    bool haveIcon = !overlay::ArrowSpriteFailed() && game::MinimapCameraIcon(icon);
    if (!haveIcon && !overlay::ArrowSpriteFailed()) {
        icon = {234.0f / 256, 25.0f / 256, 253.0f / 256, 48.0f / 256, 19 * 0.8f, 23 * 0.8f};
        haveIcon = true;
    }
    icon.w *= kMarkerScale;
    icon.h *= kMarkerScale;
    float x0, y0;
    if (haveIcon && game::MinimapPoint(S.pos, x0, y0)) {
        float k = 0.03f * vm::Len(S.pos);
        float x1, y1;
        if (game::MinimapPoint(S.pos + S.fwd * k, x1, y1) && std::fabs(x1 - x0) + std::fabs(y1 - y0) > 0.05f &&
            std::fabs(x1 - x0) + std::fabs(y1 - y0) < 80.0f)
            S.arrowAngle = std::atan2(y1 - y0, x1 - x0);
        else if (game::MinimapPoint(S.pos - S.fwd * k, x1, y1) && std::fabs(x1 - x0) + std::fabs(y1 - y0) > 0.05f &&
                 std::fabs(x1 - x0) + std::fabs(y1 - y0) < 80.0f)
            S.arrowAngle = std::atan2(y0 - y1, x0 - x1);
        overlay::SetArrow(true, x0, y0, S.arrowAngle, icon.u0, icon.v0, icon.u1, icon.v1, icon.w, icon.h);
    } else {
        overlay::SetArrow(false, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
}

void UpdateOrbit(float dt) {
    const auto& c = cfg::Get();
    if (S.orbit) {
        if (!Down(S.orbitVk) || !game::GameHasFocus()) {
            S.orbit = false;
        } else {
            POINT p;
            if (GetCursorPos(&p)) {
                float dx = float(p.x - S.anchor.x), dy = float(p.y - S.anchor.y);
                S.orbitMoved += std::fabs(dx) + std::fabs(dy);
                if (c.lockCursorOnOrbit)
                    SetCursorPos(S.anchor.x, S.anchor.y);
                else
                    S.anchor = p;
                float k = c.mouseSens * vm::kPi / 180.0f;
                S.camYaw = Wrap(S.camYaw - dx * k);
                S.camPitch = vm::Clamp(S.camPitch + dy * k * (c.invertY ? -1.0f : 1.0f), -kLookUpMax, 1.35f);
            }
        }
    } else if (c.autoCenter && S.mode == Mode::Active && S.speed > S.maxSpeed * 0.25f) {
        S.camYaw *= std::exp(-dt / 1.4f);
    }
    if (S.wheel) {
        S.zoom = vm::Clamp(S.zoom * std::pow(0.88f, float(S.wheel) / 120.0f), 0.4f, 2.8f);
        S.wheel = 0;
    }
}

game::CamXf PilotCamera(float dt) {
    V3 up = vm::Norm(S.pos);
    V3 dirFlat = vm::Rotate(S.fwd, up, S.camYaw);
    float p = S.camPitch;
    float dist = S.baseDist * S.zoom;
    V3 target = S.pos + up * S.lookH;
    V3 lookDir = vm::Norm(dirFlat * std::cos(p) - up * std::sin(p));
    V3 want, aim;
    if (p >= 0) {
        want = target - lookDir * dist;
        aim = target;
    } else {
        float t = vm::Clamp(-p / kLookUpMax, 0.0f, 1.0f);
        float orbitP = p * 0.3f;
        V3 orbitDir = vm::Norm(dirFlat * std::cos(orbitP) - up * std::sin(orbitP));
        float d = dist * (1.0f - 0.35f * t);
        want = target - orbitDir * d;
        aim = want + lookDir * d;
    }
    float s = vm::Len(game::ToSurface(want)) + 2.0f;
    if (vm::Len(want) < s)
        want = vm::Norm(want) * s;
    if (!S.camInit) {
        S.camPos = want;
        S.camTarget = aim;
        S.camInit = true;
    } else {
        S.camPos = vm::Lerp(S.camPos, want, vm::Approach(dt, 0.08f));
        S.camTarget = vm::Lerp(S.camTarget, aim, vm::Approach(dt, 0.035f));
    }
    game::CamXf xf;
    xf.pos = S.camPos;
    xf.rot = vm::LookFrame(S.camTarget - S.camPos, vm::Norm(S.camPos));
    return xf;
}

struct Listeners {
    float gameRot[2][9];
    bool haveRot[2] = {};
    bool own = false;
} L;

V3 ListenerWanted(int index) {
    if (index == 0)
        return S.pos;
    return S.haveLast ? S.last.pos : S.pos;
}

void FeedListeners() {
    vm::M3 rot = S.haveLast ? S.last.rot : vm::LookFrame(S.fwd, vm::Norm(S.pos));
    L.own = true;
    game::SetListener(0, ListenerWanted(0), rot);
    game::SetListener(1, ListenerWanted(1), rot);
    L.own = false;
}

void Unlock() {
    S.lock = nullptr;
    S.cinematic = false;
    if (S.veh) {
        if (S.firing)
            game::ClearOrders(S.veh);
        if (game::VehicleAlive(S.veh)) {
            game::SuppressAutoCombat(S.veh, false);
            S.veh->mIdlePosition.x = S.pos.x;
            S.veh->mIdlePosition.y = S.pos.y;
            S.veh->mIdlePosition.z = S.pos.z;
            S.veh->mVelocity.x = S.veh->mVelocity.y = S.veh->mVelocity.z = 0;
            game::StopMovement(S.sp);
        }
        S.veh->Release();
    }
    S.veh = nullptr;
    S.sp = nullptr;
    S.firing = false;
    S.fireTarget = nullptr;
    S.orderTarget = nullptr;
    S.orbit = false;
    S.hover = nullptr;
    S.hoverValid = false;
    S.vel = {0, 0, 0};
    S.dock = nullptr;
    if (S.raid)
        S.raid->Release();
    S.raid = nullptr;
    S.raidDocking = false;
    S.triedHut = nullptr;
    S.uiOrderSet = false;
    S.uiOrder = nullptr;
    S.ownAct = false;
    game::WantCityRolloverHidden(false);
    overlay::Clear();
}

void ForceIdle() {
    if (S.mode == Mode::Idle)
        return;
    Unlock();
    RestoreCursor();
    fx::Off();
    S.mode = Mode::Idle;
}

void Enter(cVehicle* v) {
    if (Piloting() && v == S.veh)
        return;
    game::CamXf start;
    bool haveStart = false;
    if (S.mode == Mode::Idle) {
        haveStart = S.viewer && game::ReadViewer(S.viewer, start);
    } else {
        start = S.last;
        haveStart = S.haveLast;
        Unlock();
    }
    S.veh = v;
    v->AddRef();
    S.sp = game::Spatial(v);
    S.loco = v->mLocomotion;
    S.purpose = v->mPurpose;
    game::ClearOrders(v);
    game::StopMovement(S.sp);
    InitAxes();
    InitMotion();
    InitCamera();
    S.lock = S.sp;
    game::SelectOnly(S.sp);
    S.from = start;
    S.t = 0;
    S.dur = cfg::Get().enterSeconds;
    S.mode = haveStart ? Mode::Entering : Mode::Active;
    S.shiftWas = Down(VK_SHIFT);
    S.escWas = Down(VK_ESCAPE);
    S.lastShiftTick = 0;
    S.forcedCursor = 0;
    fx::Ensure();
}

void BeginExit(bool flight) {
    if (!Piloting())
        return;
    S.lastShiftTick = 0;
    if (!flight && S.veh && game::VehicleAlive(S.veh)) {
        V3 up = vm::Norm(S.pos);
        game::PlanetCameraFlyTo(S.pos, vm::Rotate(S.fwd, up, S.camYaw), true);
    }
    Unlock();
    RestoreCursor();
    if (flight && S.haveLast && S.viewer) {
        S.from = S.last;
        S.t = 0;
        S.dur = cfg::Get().exitSeconds;
        S.mode = Mode::Exiting;
        fx::Ensure();
    } else {
        fx::Off();
        S.mode = Mode::Idle;
    }
}

void RequestEnter(cVehicle* v) {
    if (S.pending)
        S.pending->Release();
    v->AddRef();
    S.pending = v;
}

void IdleShift() {
    bool sh = S.mode == Mode::Idle && game::GameHasFocus() && Down(VK_SHIFT);
    if (sh && !S.shiftWas) {
        DWORD now = Now();
        if (S.lastShiftTick && now - S.lastShiftTick <= DWORD(cfg::Get().doubleShiftMs)) {
            S.lastShiftTick = 0;
            if (cVehicle* v = game::SelectedPlayerVehicle())
                RequestEnter(v);
        } else {
            S.lastShiftTick = now;
        }
    }
    S.shiftWas = sh;
}

void StartCinematic() {
    if (S.cinematic)
        return;
    S.cinematic = true;
    S.orbit = false;
    S.hover = nullptr;
    S.hoverValid = false;
    S.speed = 0;
    if (S.firing) {
        S.firing = false;
        S.fireTarget = nullptr;
    }
    RestoreCursor();
    overlay::Clear();
    fx::Set(0, 1, 0);
    game::WantCityRolloverHidden(false);
    for (auto& k : S.swallowKey)
        k = false;
    for (auto& b : S.swallowBtn)
        b = false;
}

bool UpdateCinematic() {
    bool playing = game::CinematicPlaying();
    if (playing) {
        StartCinematic();
    } else if (S.cinematic) {
        S.cinematic = false;
        game::CamXf native;
        if (S.viewer && game::ReadViewer(S.viewer, native)) {
            S.from = native;
            S.t = 0;
            S.dur = cfg::Get().enterSeconds;
            S.mode = Mode::Entering;
            fx::Ensure();
        }
        S.escWas = Down(VK_ESCAPE);
        S.shiftWas = Down(VK_SHIFT);
        S.lastShiftTick = 0;
    }
    if (S.cinematic)
        Drive(0);
    return S.cinematic;
}

bool Validate() {
    return game::InCiv() && game::PlanetReady() && S.veh && game::VehicleAlive(S.veh) &&
           S.veh->cGameData::mPoliticalID == game::PlayerPoliticalID();
}

bool ControlledKey(int vk) {
    switch (vk) {
    case 'W':
    case 'A':
    case 'S':
    case 'D':
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_SPACE:
        return true;
    }
    return false;
}
}

bool OnKeyDown(int vk) {
    if (S.cinematic)
        return false;
    if (vk == VK_ESCAPE) {
        if (Piloting())
            BeginExit(false);
        return false;
    }
    if (Piloting() && ControlledKey(vk)) {
        if (vk >= 0 && vk < 256)
            S.swallowKey[vk] = true;
        return true;
    }
    return false;
}

bool OnKeyUp(int vk) {
    if (vk >= 0 && vk < 256 && S.swallowKey[vk]) {
        S.swallowKey[vk] = false;
        return true;
    }
    return false;
}

namespace {
bool SwitchClick(float x, float y) {
    cVehicle* v = game::MouseOverUI() ? nullptr : game::PlayerVehicle(game::PickHovered());
    if (v == S.veh)
        v = nullptr;
    DWORD now = Now();
    if (v && v == S.lastClickVeh && now - S.lastClickTick <= DWORD(cfg::Get().doubleClickMs) &&
        std::fabs(x - S.lastClickX) < 30 && std::fabs(y - S.lastClickY) < 30) {
        S.lastClickVeh = nullptr;
        RequestEnter(v);
        return true;
    }
    S.lastClickVeh = v;
    S.lastClickTick = now;
    S.lastClickX = x;
    S.lastClickY = y;
    return v != nullptr;
}
}

bool OnMouseDown(int button, float x, float y) {
    int idx = button - 1000;
    if (S.cinematic)
        return false;
    if (Piloting()) {
        if (idx >= 0 && idx < 8)
            S.swallowBtn[idx] = true;
        if (button == kMouseMiddle || button == kMouseRight) {
            S.orbit = true;
            S.orbitVk = button == kMouseMiddle ? VK_MBUTTON : VK_RBUTTON;
            S.orbitMoved = 0;
            S.orbitTick = Now();
            GetCursorPos(&S.anchor);
        } else if (S.mode == Mode::Active && button == kMouseLeft) {
            if (!SwitchClick(x, y))
                AttackPress();
        }
        return true;
    }
    if (button != kMouseLeft || !game::InCiv())
        return false;
    cVehicle* v = game::PlayerVehicle(game::PickHovered());
    DWORD now = Now();
    if (v && v == S.lastClickVeh && now - S.lastClickTick <= DWORD(cfg::Get().doubleClickMs) &&
        std::fabs(x - S.lastClickX) < 12 && std::fabs(y - S.lastClickY) < 12) {
        S.lastClickVeh = nullptr;
        S.swallowBtn[0] = true;
        RequestEnter(v);
        return true;
    }
    S.lastClickVeh = v;
    S.lastClickTick = now;
    S.lastClickX = x;
    S.lastClickY = y;
    return false;
}

bool OnMouseUp(int button) {
    int idx = button - 1000;
    if (idx < 0 || idx >= 8 || !S.swallowBtn[idx])
        return false;
    S.swallowBtn[idx] = false;
    if (button == kMouseLeft) {
        AttackRelease();
    } else if (button == kMouseMiddle || button == kMouseRight) {
        bool click =
            button == kMouseRight && S.orbitVk == VK_RBUTTON && S.orbitMoved <= kClickSlop && Now() - S.orbitTick < 400;
        if ((button == kMouseMiddle) == (S.orbitVk == VK_MBUTTON))
            S.orbit = false;
        if (click && S.mode == Mode::Active && S.veh) {
            game::ClearOrders(S.veh);
            S.firing = false;
            S.fireTarget = nullptr;
            S.orderTarget = nullptr;
        }
    }
    return true;
}

bool OnMouseMove(uint32_t& state) {
    if (Piloting() && !S.cinematic)
        state &= ~uint32_t(8 | 16 | 32);
    return false;
}

bool OnMouseWheel(int delta) {
    if (!Piloting() || S.cinematic)
        return false;
    S.wheel += delta;
    return true;
}

void AfterCivUpdate(float realSeconds) {
    float realDt = vm::Clamp(std::isfinite(realSeconds) ? realSeconds : 0.0f, 0.0f, 0.1f);

    if (S.pending) {
        cVehicle* v = S.pending;
        S.pending = nullptr;
        if (game::InCiv() && game::PlayerVehicle(v) == v)
            Enter(v);
        v->Release();
    }
    if (!Piloting()) {
        IdleShift();
        return;
    }
    if (!Validate()) {
        BeginExit(game::InCiv());
        return;
    }
    if (UpdateCinematic())
        return;

    bool focus = game::GameHasFocus();
    bool esc = focus && Down(VK_ESCAPE);
    if (esc && !S.escWas) {
        S.escWas = esc;
        BeginExit(false);
        return;
    }
    S.escWas = esc;
    bool sh = focus && Down(VK_SHIFT);
    if (sh && !S.shiftWas) {
        DWORD now = Now();
        if (S.lastShiftTick && now - S.lastShiftTick <= DWORD(cfg::Get().doubleShiftMs)) {
            S.shiftWas = sh;
            BeginExit(true);
            return;
        }
        S.lastShiftTick = now;
    }
    S.shiftWas = sh;

    if (!cfg::Get().autonomousFire)
        game::SuppressAutoCombat(S.veh, true);
    int simMs = game::ConvertDeltaMs(int(realDt * 1000.0f + 0.5f));
    UpdateClaim();
    if (!RaidDocking())
        Drive(float(simMs) / 1000.0f);
    if (S.hover && game::IsCityHall(S.hover))
        S.rolloverTick = Now();
    if (focus && Down(VK_LBUTTON) && game::OverCityRollover())
        S.rolloverClickTick = Now();
    bool keepRollover =
        S.rolloverTick && (Now() - S.rolloverTick < 400 || (game::OverCityRollover() && Now() - S.rolloverTick < 8000));
    game::WantCityRolloverHidden(S.mode == Mode::Active && !keepRollover);
    if (S.mode == Mode::Active && !keepRollover)
        game::HideCityRollover();
    UpdateOrbit(realDt);
    if (S.mode == Mode::Active) {
        UpdateAim();
        UpdateRaid();
        UpdateOverlay();
    } else {
        overlay::Clear();
    }

    game::CamXf xf = PilotCamera(realDt);
    const auto& c = cfg::Get();
    if (S.mode == Mode::Entering) {
        S.t += realDt;
        float u = vm::Clamp(S.t / S.dur, 0, 1);
        float e = vm::EaseInOutCubic(u);
        xf = Blend(S.from, xf, e);
        fx::Set(std::sin(vm::kPi * u), 1.0f, c.pilotVignette * e);
        if (u >= 1) {
            S.mode = Mode::Active;
            fx::DropCapture();
            fx::Set(0, 1, c.pilotVignette);
        }
    } else {
        fx::Set(0, 1, c.pilotVignette);
    }
    if (S.viewer) {
        game::WriteViewer(S.viewer, xf);
        S.last = xf;
        S.haveLast = true;
    }
    game::SetShadowFocus(S.pos, xf.pos);
    FeedListeners();
}

bool FreezeNativeCamera() {
    if (!Piloting())
        return false;
    if (game::CinematicPlaying())
        StartCinematic();
    return !S.cinematic;
}

void CameraFrame(App::cViewer* viewer, int deltaMs, bool nativeRan) {
    if (viewer) {
        S.viewer = viewer;
        overlay::SetViewer(viewer);
    }
    if (Piloting()) {
        if (!S.cinematic)
            FeedListeners();
        return;
    }
    if (!nativeRan || S.mode != Mode::Exiting || !S.viewer)
        return;
    game::CamXf native;
    if (!game::ReadViewer(S.viewer, native)) {
        ForceIdle();
        return;
    }
    float dt = vm::Clamp(float(deltaMs) / 1000.0f, 0.0f, 0.1f);
    S.t += dt;
    float u = vm::Clamp(S.t / S.dur, 0, 1);
    float e = vm::EaseInOutCubic(u);
    game::CamXf out = Blend(S.from, native, e);
    game::WriteViewer(S.viewer, out);
    S.last = out;
    fx::Set(std::sin(vm::kPi * u), -1.0f, cfg::Get().pilotVignette * (1 - e));
    if (u >= 1) {
        S.mode = Mode::Idle;
        fx::Off();
    }
}

void OnAppUpdate() {
    if (S.mode == Mode::Idle) {
        if (S.pending && !game::InCiv()) {
            S.pending->Release();
            S.pending = nullptr;
        }
        return;
    }
    if (!game::InCiv())
        ForceIdle();
}

void OnCivMessage(uint32_t id, void* msg) {
    if (id == game::kMsgPosseDoubleClick) {
        cVehicle* v = game::PlayerVehicle(game::MessageObject(msg));
        if (v)
            RequestEnter(v);
    } else if (id == game::kMsgActOnObject) {
        int key = game::MessageKey(msg);
        if (!Piloting())
            S.actKey = key;
    }
}

bool BlockVehicleWrite(const void* spatial) {
    return S.lock && spatial == S.lock;
}

bool VelocityFor(const void* locomotive, float out[3]) {
    if (!S.lock || locomotive != S.lock)
        return false;
    out[0] = S.vel.x;
    out[1] = S.vel.y;
    out[2] = S.vel.z;
    return true;
}

uint32_t FilterCursor(uint32_t id) {
    S.nativeCursor = id;
    S.nativeCursorTick = Now();
    if (S.mode != Mode::Active || S.cinematic)
        return id;
    if (id == kCursorGoodieHut)
        return kCursorDefault;
    uint32_t want = WantedCursor();
    if (!want)
        return id;
    S.forcedCursor = want;
    return want;
}

bool RefuseCombatTarget(const void* combatant, const void* target) {
    if (!target || !Piloting() || !S.veh || cfg::Get().autonomousFire)
        return false;
    if (combatant != static_cast<const void*>(static_cast<cCombatant*>(S.veh)))
        return false;
    if (game::CurrentOrderTarget(S.veh))
        return false;
    return true;
}

bool RefuseOrder(const void* vehicle, const void* target, uint32_t va) {
    if (!Piloting() || !S.veh || vehicle != S.veh || cfg::Get().autonomousFire)
        return false;
    if (S.ownAct)
        return false;
    if ((va >= 0xcf2000 && va < 0xcf8000) || (S.rolloverClickTick && Now() - S.rolloverClickTick < 600)) {
        S.uiOrderSet = true;
        S.uiOrder = target;
        return false;
    }
    return true;
}

bool ListenerOverride(int index, const float* gamePos, const float* gameRot, float pos[3], float rot[9],
                      bool& replaceRot) {
    replaceRot = false;
    if (index < 0 || index > 1 || !gamePos)
        return false;
    if (!L.own && gameRot) {
        std::memcpy(L.gameRot[index], gameRot, sizeof(L.gameRot[index]));
        L.haveRot[index] = true;
    }
    if (!Piloting() || !S.veh || S.cinematic)
        return false;
    V3 p = ListenerWanted(index);
    if (!vm::Finite(p))
        return false;
    pos[0] = p.x;
    pos[1] = p.y;
    pos[2] = p.z;
    if (L.haveRot[1]) {
        std::memcpy(rot, L.gameRot[1], sizeof(L.gameRot[1]));
        replaceRot = true;
    }
    return true;
}
}


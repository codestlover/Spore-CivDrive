
#include "render/Overlay.hpp"
#include "game/Game.hpp"
#include "game/SdkCompat.hpp"
#include <Spore/App/cViewer.h>
#include <Windows.h>
#include <d3d9.h>
#include <cmath>

namespace overlay {
namespace {
struct RingData {
    vm::V3 pts[kRingPoints + 1];
    int count = 0;
    uint32_t color = 0;
    float width = 2;
};

RingData rings[kRingCount];
App::cViewer* viewer = nullptr;
bool arrowOn = false;
float arrowX = 0, arrowY = 0, arrowAngle = 0;
float arrowU0 = 0, arrowV0 = 0, arrowU1 = 0, arrowV1 = 0, arrowW = 0, arrowH = 0;

constexpr uint32_t kIconAtlas = 0x588D3D52, kIconAtlasType = 0x2F7D0004, kIconAtlasGroup = 0x31A44893;
IDirect3DTexture9* sprite = nullptr;
float spriteU = 1, spriteV = 1;
float spriteKey[4] = {-1, -1, -1, -1};
bool spriteFailed = false;

using CreateTexFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const void*, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
                                     DWORD, DWORD, D3DCOLOR, void*, PALETTEENTRY*, IDirect3DTexture9**);

CreateTexFn D3dxCreateTexture() {
    const char* dlls[] = {"d3dx9_27.dll", "d3dx9_43.dll", "d3dx9_42.dll", "d3dx9_41.dll", "d3dx9_40.dll",
                          "d3dx9_39.dll", "d3dx9_38.dll", "d3dx9_37.dll", "d3dx9_36.dll", "d3dx9_35.dll"};
    for (const char* name : dlls) {
        HMODULE m = GetModuleHandleA(name);
        if (!m)
            m = LoadLibraryA(name);
        if (auto fn =
                m ? reinterpret_cast<CreateTexFn>(GetProcAddress(m, "D3DXCreateTextureFromFileInMemoryEx")) : nullptr)
            return fn;
    }
    return nullptr;
}

void Recolor(unsigned char* bgra) {
    float b = bgra[0] / 255.0f, g = bgra[1] / 255.0f, r = bgra[2] / 255.0f;
    float mx = std::fmax(r, std::fmax(g, b)), mn = std::fmin(r, std::fmin(g, b)), d = mx - mn;
    if (mx <= 0 || d <= 0)
        return;
    float s = d / mx, v = mx, h;
    if (mx == r)
        h = std::fmod((g - b) / d, 6.0f);
    else if (mx == g)
        h = (b - r) / d + 2;
    else
        h = (r - g) / d + 4;
    h *= 60;
    if (h < 0)
        h += 360;
    if (!(h > 61 && h < 151 && s > 0.2f))
        return;
    h -= 48;
    float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60, 2.0f) - 1)), m = v - c;
    float rr, gg, bb;
    if (h < 60) {
        rr = c, gg = x, bb = 0;
    } else if (h < 120) {
        rr = x, gg = c, bb = 0;
    } else {
        rr = 0, gg = c, bb = x;
    }
    bgra[0] = (unsigned char)(vm::Clamp(bb + m, 0, 1) * 255 + 0.5f);
    bgra[1] = (unsigned char)(vm::Clamp(gg + m, 0, 1) * 255 + 0.5f);
    bgra[2] = (unsigned char)(vm::Clamp(rr + m, 0, 1) * 255 + 0.5f);
}

void DropSprite() {
    if (sprite)
        sprite->Release();
    sprite = nullptr;
}

bool BuildSprite(IDirect3DDevice9* dev) {
    DropSprite();
    CreateTexFn create = D3dxCreateTexture();
    if (!create)
        return false;
    unsigned char* png = nullptr;
    unsigned size = 0;
    if (!game::ReadGameFile(kIconAtlas, kIconAtlasType, kIconAtlasGroup, png, size))
        return false;
    IDirect3DTexture9* atlas = nullptr;
    HRESULT hr =
        create(dev, png, size, 0, 0, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, 1, 1, 0, nullptr, nullptr, &atlas);
    delete[] png;
    if (FAILED(hr) || !atlas)
        return false;
    D3DSURFACE_DESC desc{};
    atlas->GetLevelDesc(0, &desc);
    int x0 = int(arrowU0 * desc.Width + 0.5f), y0 = int(arrowV0 * desc.Height + 0.5f);
    int x1 = int(arrowU1 * desc.Width + 0.5f), y1 = int(arrowV1 * desc.Height + 0.5f);
    int w = x1 - x0, h = y1 - y0;
    bool ok = false;
    if (w > 0 && h > 0 && w <= 64 && h <= 64 && x1 <= int(desc.Width) && y1 <= int(desc.Height)) {
        UINT side = 1;
        while (side < UINT(w > h ? w : h))
            side <<= 1;
        IDirect3DTexture9* tex = nullptr;
        D3DLOCKED_RECT src{}, dst{};
        if (SUCCEEDED(dev->CreateTexture(side, side, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) && tex) {
            if (SUCCEEDED(atlas->LockRect(0, &src, nullptr, D3DLOCK_READONLY))) {
                if (SUCCEEDED(tex->LockRect(0, &dst, nullptr, 0))) {
                    for (UINT y = 0; y < side; ++y) {
                        auto* out = static_cast<unsigned char*>(dst.pBits) + y * dst.Pitch;
                        std::memset(out, 0, side * 4);
                        if (int(y) >= h)
                            continue;
                        auto* in = static_cast<const unsigned char*>(src.pBits) + (y0 + int(y)) * src.Pitch + x0 * 4;
                        std::memcpy(out, in, size_t(w) * 4);
                        for (int x = 0; x < w; ++x)
                            Recolor(out + x * 4);
                    }
                    tex->UnlockRect(0);
                    ok = true;
                }
                atlas->UnlockRect(0);
            }
            if (ok) {
                sprite = tex;
                spriteU = float(w) / float(side);
                spriteV = float(h) / float(side);
            } else {
                tex->Release();
            }
        }
    }
    atlas->Release();
    return ok;
}

struct Vtx {
    float x, y, z, rhw;
    DWORD color;
};

struct VBuf {
    static constexpr unsigned kMax = kRingCount * (kRingPoints + 1) * 18 + 64;
    Vtx v[kMax];
    unsigned n = 0;

    void push_back(const Vtx& x) {
        if (n < kMax)
            v[n++] = x;
    }

    bool empty() const {
        return n == 0;
    }

    unsigned size() const {
        return n;
    }

    const Vtx* data() const {
        return v;
    }

    void clear() {
        n = 0;
    }
};

VBuf buf;

DWORD WithAlpha(uint32_t argb, float a) {
    float base = float(argb >> 24) / 255.0f;
    DWORD A = DWORD(vm::Clamp(base * a, 0, 1) * 255.0f + 0.5f);
    return (A << 24) | (argb & 0x00FFFFFF);
}

void States(IDirect3DDevice9* dev) {
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    for (DWORD s = 0; s < 4; ++s)
        dev->SetTexture(s, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
}

void Segment(VBuf& out, float ax, float ay, float bx, float by, float halfW, DWORD core, DWORD edge) {
    float dx = bx - ax, dy = by - ay;
    float l = std::sqrt(dx * dx + dy * dy);
    if (l < 0.01f)
        return;
    float nx = -dy / l, ny = dx / l;
    const float o[4] = {-(halfW + 1.5f), -halfW, halfW, halfW + 1.5f};
    const DWORD c[4] = {edge, core, core, edge};
    for (int k = 0; k < 3; ++k) {
        Vtx a0{ax + nx * o[k], ay + ny * o[k], 0, 1, c[k]}, a1{ax + nx * o[k + 1], ay + ny * o[k + 1], 0, 1, c[k + 1]};
        Vtx b0{bx + nx * o[k], by + ny * o[k], 0, 1, c[k]}, b1{bx + nx * o[k + 1], by + ny * o[k + 1], 0, 1, c[k + 1]};
        out.push_back(a0);
        out.push_back(a1);
        out.push_back(b0);
        out.push_back(a1);
        out.push_back(b1);
        out.push_back(b0);
    }
}

struct Projector {
    vm::V3 pos, right, fwd, up;
    float vwx = 1, vwy = 1, offX = 0, offY = 0, nearZ = 0.1f;
    float vpX = 0, vpY = 0, vpW = 1, vpH = 1;
    bool ok = false;

    explicit Projector(App::cViewer* v) {
        if (!v || !v->pCamera)
            return;
        const auto& m = v->viewTransform.m;
        right = vm::Norm({m[0][0], m[0][1], m[0][2]});
        fwd = vm::Norm({m[1][0], m[1][1], m[1][2]});
        up = vm::Norm({m[2][0], m[2][1], m[2][2]});
        pos = {m[3][0], m[3][1], m[3][2]};
        const auto* cam = v->pCamera;
        if (cam->projectionType == App::cViewer::Projection::Parallel)
            return;
        vwx = cam->viewWindow.x;
        vwy = cam->viewWindow.y;
        offX = cam->viewOffset.x;
        offY = cam->viewOffset.y;
        nearZ = cam->nearPlane > 0 ? cam->nearPlane : 0.1f;
        vpX = float(cam->viewport.X);
        vpY = float(cam->viewport.Y);
        vpW = float(cam->viewport.Width);
        vpH = float(cam->viewport.Height);
        ok = vwx > 1e-5f && vwy > 1e-5f && vpW > 1 && vpH > 1 && vm::Finite(pos);
    }

    vm::V3 ToCam(const vm::V3& p) const {
        vm::V3 d = p - pos;
        return {vm::Dot(d, right), vm::Dot(d, fwd), vm::Dot(d, up)};
    }

    void ToScreen(const vm::V3& c, float& sx, float& sy) const {
        float nx = (c.x / c.y - offX) / vwx;
        float ny = (c.z / c.y - offY) / vwy;
        sx = vpX + (nx * 0.5f + 0.5f) * vpW;
        sy = vpY + (0.5f - ny * 0.5f) * vpH;
    }
};

void DrawRing(VBuf& out, const Projector& pr, const RingData& r, float pulse) {
    float nearY = pr.nearZ * 1.05f;
    for (int i = 0; i + 1 < r.count; ++i) {
        vm::V3 a = pr.ToCam(r.pts[i]), b = pr.ToCam(r.pts[i + 1]);
        if (a.y < nearY && b.y < nearY)
            continue;
        if (a.y < nearY)
            a = vm::Lerp(a, b, (nearY - a.y) / (b.y - a.y));
        else if (b.y < nearY)
            b = vm::Lerp(b, a, (nearY - b.y) / (a.y - b.y));
        float ax, ay, bx, by;
        pr.ToScreen(a, ax, ay);
        pr.ToScreen(b, bx, by);
        if (!std::isfinite(ax) || !std::isfinite(bx))
            continue;
        float ang = 2 * vm::kPi * float(i) / float(r.count - 1);
        float sweep = std::pow(std::fmax(0.0f, std::cos(ang - pulse)), 12.0f);
        DWORD core = WithAlpha(r.color, 0.80f + 0.20f * sweep);
        Segment(out, ax, ay, bx, by, r.width * 0.5f, core, WithAlpha(r.color, 0.0f));
    }
}
}

void SetViewer(App::cViewer* v) {
    viewer = v;
}

void SetRing(int ring, const vm::V3* points, int count, uint32_t argb, float width) {
    if (ring < 0 || ring >= kRingCount)
        return;
    auto& r = rings[ring];
    r.count = count > kRingPoints + 1 ? kRingPoints + 1 : (count < 0 ? 0 : count);
    for (int i = 0; i < r.count; ++i)
        r.pts[i] = points[i];
    r.color = argb;
    r.width = width;
}

void SetArrow(bool on, float x, float y, float angle, float u0, float v0, float u1, float v1, float w, float h) {
    arrowOn = on && !spriteFailed;
    arrowX = x;
    arrowY = y;
    arrowAngle = angle;
    arrowU0 = u0, arrowV0 = v0, arrowU1 = u1, arrowV1 = v1, arrowW = w, arrowH = h;
}

bool ArrowSpriteFailed() {
    return spriteFailed;
}

void Clear() {
    for (auto& r : rings)
        r.count = 0;
    arrowOn = false;
}

bool HasWorld() {
    for (auto& r : rings)
        if (r.count > 1)
            return true;
    return false;
}

bool HasScreen() {
    return arrowOn;
}

void DrawWorld(IDirect3DDevice9* dev) {
    if (!HasWorld() || !viewer)
        return;
    Projector pr(viewer);
    if (!pr.ok)
        return;
    VBuf& v = buf;
    v.clear();
    float pulse = float(GetTickCount() % 4000) / 4000.0f * 2 * vm::kPi;
    for (auto& r : rings)
        if (r.count > 1)
            DrawRing(v, pr, r, pulse);
    if (v.empty())
        return;
    IDirect3DStateBlock9* saved = nullptr;
    dev->CreateStateBlock(D3DSBT_ALL, &saved);
    States(dev);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(v.size() / 3), v.data(), sizeof(Vtx));
    if (saved) {
        saved->Apply();
        saved->Release();
    }
}

void DrawScreen(IDirect3DDevice9* dev) {
    if (!arrowOn || spriteFailed)
        return;
    const float key[4] = {arrowU0, arrowV0, arrowU1, arrowV1};
    if (!sprite || std::memcmp(key, spriteKey, sizeof(key)) != 0) {
        std::memcpy(spriteKey, key, sizeof(key));
        if (!BuildSprite(dev)) {
            spriteFailed = true;
            return;
        }
    }
    float fx = std::cos(arrowAngle), fy = std::sin(arrowAngle);
    float rx = -fy, ry = fx;
    float hw = arrowW * 0.5f, hh = arrowH * 0.5f;
    float cx = arrowX - 0.5f, cy = arrowY - 0.5f;

    struct TVtx {
        float x, y, z, rhw;
        DWORD color;
        float u, v;
    } q[4] = {
        {cx + fx * hh - rx * hw, cy + fy * hh - ry * hw, 0, 1, 0xFFFFFFFF, 0, 0},
        {cx + fx * hh + rx * hw, cy + fy * hh + ry * hw, 0, 1, 0xFFFFFFFF, spriteU, 0},
        {cx - fx * hh - rx * hw, cy - fy * hh - ry * hw, 0, 1, 0xFFFFFFFF, 0, spriteV},
        {cx - fx * hh + rx * hw, cy - fy * hh + ry * hw, 0, 1, 0xFFFFFFFF, spriteU, spriteV},
    };

    IDirect3DStateBlock9* saved = nullptr;
    dev->CreateStateBlock(D3DSBT_ALL, &saved);
    States(dev);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    dev->SetTexture(0, sprite);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(TVtx));
    if (saved) {
        saved->Apply();
        saved->Release();
    }
}
}


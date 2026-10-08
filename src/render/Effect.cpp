
#include "render/Effect.hpp"
#include "core/Config.hpp"
#include "render/Overlay.hpp"
#include "game/Game.hpp"
#include "game/SdkCompat.hpp"
#include <Spore/Graphics/IRenderer.h>
#include <Spore/Graphics/ILayer.h>
#include <Spore/Graphics/RenderUtils.h>
#include <Windows.h>
#include <d3d9.h>
#include <cstring>

#include "transition_ps_hlsl.h"

namespace fx {
namespace {
struct ID3DXBufferMin {
    virtual HRESULT __stdcall QueryInterface(const IID&, void**) = 0;
    virtual ULONG __stdcall AddRef() = 0;
    virtual ULONG __stdcall Release() = 0;
    virtual void* __stdcall GetBufferPointer() = 0;
    virtual DWORD __stdcall GetBufferSize() = 0;
};

struct Macro {
    const char* name;
    const char* definition;
};

using CompileFn = HRESULT(WINAPI*)(LPCSTR, UINT, const Macro*, void*, LPCSTR, LPCSTR, DWORD, ID3DXBufferMin**,
                                   ID3DXBufferMin**, void**);

float gStrength = 0, gDirection = 1, gVignette = 0;
DWORD gStart = 0;

IDirect3DPixelShader9* Compile(IDirect3DDevice9* dev, const char* entry) {
    const char* dlls[] = {"d3dx9_27.dll", "d3dx9_43.dll", "d3dx9_42.dll", "d3dx9_41.dll", "d3dx9_40.dll",
                          "d3dx9_39.dll", "d3dx9_38.dll", "d3dx9_37.dll", "d3dx9_36.dll", "d3dx9_35.dll"};
    CompileFn compile = nullptr;
    for (const char* name : dlls) {
        HMODULE m = GetModuleHandleA(name);
        if (!m)
            m = LoadLibraryA(name);
        if (m && (compile = reinterpret_cast<CompileFn>(GetProcAddress(m, "D3DXCompileShader"))))
            break;
    }
    if (!compile)
        return nullptr;

    struct Try {
        const char* profile;
        const char* taps;
    } tries[] = {{"ps_2_b", "10"}, {"ps_2_a", "10"}, {"ps_3_0", "10"}, {"ps_2_0", "4"}};

    for (auto& t : tries) {
        Macro defs[] = {{"TAPS", t.taps}, {nullptr, nullptr}};
        ID3DXBufferMin *code = nullptr, *errors = nullptr;
        HRESULT hr = compile(kTransitionPsHlsl, UINT(strlen(kTransitionPsHlsl)), defs, nullptr, entry, t.profile, 0,
                             &code, &errors, nullptr);
        if (FAILED(hr) || !code) {
            if (errors)
                errors->Release();
            if (code)
                code->Release();
            continue;
        }
        IDirect3DPixelShader9* ps = nullptr;
        HRESULT hr2 = dev->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &ps);
        if (errors)
            errors->Release();
        code->Release();
        if (SUCCEEDED(hr2) && ps)
            return ps;
    }
    return nullptr;
}

struct Vtx {
    float x, y, z, rhw, u, v;
};

class Layer : public Graphics::ILayer {
public:
    int AddRef() override {
        return ++refs;
    }

    int Release() override {
        return refs > 1 ? --refs : 1;
    }

    void DrawLayer(int, int, App::cViewer**, Graphics::RenderStatistics&) override;

    void Drop() {
        if (capture) {
            capture->Release();
            capture = nullptr;
        }
        w = h = 0;
    }

private:
    int refs = 1;
    IDirect3DTexture9* capture = nullptr;
    IDirect3DPixelShader9* shader = nullptr;
    IDirect3DPixelShader9* vignette = nullptr;
    bool tried = false;
    void States(IDirect3DDevice9* dev);
    void Quad(IDirect3DDevice9* dev, float fw, float fh);
    unsigned w = 0, h = 0;
    int failures = 0;
};

Layer layer;
int layerIndex = -1;

class TopLayer : public Graphics::ILayer {
public:
    int AddRef() override {
        return 2;
    }

    int Release() override {
        return 1;
    }

    void DrawLayer(int, int, App::cViewer**, Graphics::RenderStatistics&) override {
        if (!overlay::HasScreen())
            return;
        IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9**>(GetAddress(Graphics::RenderUtils, Device_ptr));
        if (dev && dev->TestCooperativeLevel() == D3D_OK)
            overlay::DrawScreen(dev);
    }
};

TopLayer topLayer;
int topIndex = -1;

void Layer::States(IDirect3DDevice9* dev) {
    dev->SetVertexShader(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    for (DWORD s = 1; s < 4; ++s)
        dev->SetTexture(s, nullptr);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);
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
}

void Layer::Quad(IDirect3DDevice9* dev, float fw, float fh) {
    const Vtx quad[4] = {
        {-0.5f, -0.5f, 0, 1, 0, 0},
        {fw - 0.5f, -0.5f, 0, 1, 1, 0},
        {-0.5f, fh - 0.5f, 0, 1, 0, 1},
        {fw - 0.5f, fh - 0.5f, 0, 1, 1, 1},
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vtx));
}

void Layer::DrawLayer(int, int, App::cViewer**, Graphics::RenderStatistics&) {
    game::BeforeHud();
    if (gStrength <= 0.001f && gVignette <= 0.001f && !overlay::HasWorld())
        return;
    IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9**>(GetAddress(Graphics::RenderUtils, Device_ptr));
    if (!dev || dev->TestCooperativeLevel() != D3D_OK)
        return;
    overlay::DrawWorld(dev);
    if (failures > 5 || (gStrength <= 0.001f && gVignette <= 0.001f))
        return;
    if (!tried) {
        tried = true;
        shader = Compile(dev, "main");
        vignette = Compile(dev, "vignette");
    }
    IDirect3DSurface9* rt = nullptr;
    if (FAILED(dev->GetRenderTarget(0, &rt)) || !rt)
        return;
    D3DSURFACE_DESC desc{};
    rt->GetDesc(&desc);
    const float fw = float(desc.Width), fh = float(desc.Height);
    float t = float(GetTickCount() - gStart) / 1000.0f;
    const float c0[4] = {gStrength, t, gDirection, fw / (fh > 0 ? fh : 1.0f)};
    const float c1[4] = {1.0f / fw, 1.0f / fh, 0.5f, 0.5f};
    const float c2[4] = {gVignette, 0, 0, 0};

    if (gStrength > 0.001f && shader) {
        if (capture && (w != desc.Width || h != desc.Height))
            Drop();
        if (!capture) {
            HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format,
                                            D3DPOOL_DEFAULT, &capture, nullptr);
            if (FAILED(hr))
                hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                        D3DPOOL_DEFAULT, &capture, nullptr);
            if (FAILED(hr) || !capture) {
                ++failures;
                capture = nullptr;
                rt->Release();
                return;
            }
            w = desc.Width;
            h = desc.Height;
        }
        IDirect3DSurface9* dst = nullptr;
        if (FAILED(capture->GetSurfaceLevel(0, &dst)) || !dst) {
            rt->Release();
            return;
        }
        HRESULT hrc = dev->StretchRect(rt, nullptr, dst, nullptr, D3DTEXF_NONE);
        dst->Release();
        if (FAILED(hrc)) {
            ++failures;
            rt->Release();
            return;
        }
        IDirect3DStateBlock9* saved = nullptr;
        dev->CreateStateBlock(D3DSBT_ALL, &saved);
        States(dev);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetPixelShader(shader);
        dev->SetTexture(0, capture);
        dev->SetPixelShaderConstantF(0, c0, 1);
        dev->SetPixelShaderConstantF(1, c1, 1);
        dev->SetPixelShaderConstantF(2, c2, 1);
        Quad(dev, fw, fh);
        if (saved) {
            saved->Apply();
            saved->Release();
        }
    } else if (gVignette > 0.001f && vignette) {
        IDirect3DStateBlock9* saved = nullptr;
        dev->CreateStateBlock(D3DSBT_ALL, &saved);
        States(dev);
        dev->SetTexture(0, nullptr);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        dev->SetPixelShader(vignette);
        dev->SetPixelShaderConstantF(0, c0, 1);
        dev->SetPixelShaderConstantF(1, c1, 1);
        dev->SetPixelShaderConstantF(2, c2, 1);
        Quad(dev, fw, fh);
        if (saved) {
            saved->Apply();
            saved->Release();
        }
    }
    rt->Release();
}

Graphics::IRenderer* RendererGet() {
    return reinterpret_cast<Graphics::IRenderer* (*)()>(GetAddress(Graphics::IRenderer, Get))();
}
}

void Ensure() {
    auto* r = RendererGet();
    if (!r)
        return;
    if (!(layerIndex >= 0 && r->Layer(layerIndex) == &layer)) {
        const int candidates[] = {29, 28, 27, 26, 25, 24, 23};
        layerIndex = -1;
        for (int idx : candidates) {
            Graphics::ILayer* existing = r->Layer(idx);
            if (existing == nullptr || existing == &layer) {
                if (existing != &layer)
                    r->RegisterLayer(&layer, idx, 0);
                layerIndex = idx;
                break;
            }
        }
    }
    if (!(topIndex >= 0 && r->Layer(topIndex) == &topLayer)) {
        int highest = -1;
        for (int idx = 0; idx < 64; ++idx) {
            Graphics::ILayer* existing = r->Layer(idx);
            if (existing && existing != &topLayer && existing != &layer)
                highest = idx;
        }
        topIndex = -1;
        for (int idx = highest + 1; idx < 64; ++idx) {
            Graphics::ILayer* existing = r->Layer(idx);
            if (existing == nullptr || existing == &topLayer) {
                if (existing != &topLayer)
                    r->RegisterLayer(&topLayer, idx, 0);
                topIndex = idx;
                break;
            }
        }
    }
}

void Set(float strength, float direction, float vignette) {
    const auto& c = cfg::Get();
    if (!c.shader) {
        gStrength = gVignette = 0;
        return;
    }
    if (gStrength <= 0.001f && strength > 0.001f)
        gStart = GetTickCount();
    gStrength = strength * c.shaderStrength;
    gDirection = direction;
    gVignette = vignette;
}

void DropCapture() {
    layer.Drop();
}

void Off() {
    gStrength = 0;
    gVignette = 0;
    layer.Drop();
}
}


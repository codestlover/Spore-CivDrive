
#pragma once
#include "math/VMath.hpp"
#include <cstdint>

struct IDirect3DDevice9;

namespace App {
class cViewer;
}

namespace overlay {
constexpr int kRingPoints = 96;

enum Ring { kRingRange, kRingMinRange, kRingTarget, kRingCount };

void SetViewer(App::cViewer* viewer);
void SetRing(int ring, const vm::V3* points, int count, uint32_t argb, float width);
void SetArrow(bool on, float x, float y, float angle, float u0, float v0, float u1, float v1, float w, float h);
bool ArrowSpriteFailed();
void Clear();

void DrawWorld(IDirect3DDevice9* dev);
void DrawScreen(IDirect3DDevice9* dev);
bool HasWorld();
bool HasScreen();
}


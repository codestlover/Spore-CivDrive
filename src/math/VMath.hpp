
#pragma once
#include <cmath>

namespace vm {
constexpr float kPi = 3.14159265358979f;

struct V3 {
    float x = 0, y = 0, z = 0;
    V3() = default;

    V3(float a, float b, float c) : x(a), y(b), z(c) {}

    V3 operator+(const V3& o) const {
        return {x + o.x, y + o.y, z + o.z};
    }

    V3 operator-(const V3& o) const {
        return {x - o.x, y - o.y, z - o.z};
    }

    V3 operator*(float s) const {
        return {x * s, y * s, z * s};
    }

    V3 operator-() const {
        return {-x, -y, -z};
    }

    V3& operator+=(const V3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
};

inline float Dot(const V3& a, const V3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline V3 Cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float Len(const V3& a) {
    return std::sqrt(Dot(a, a));
}

inline V3 Norm(const V3& a, const V3& fallback = {0, 0, 1}) {
    float l = Len(a);
    return l > 1e-6f ? a * (1.0f / l) : fallback;
}

inline V3 Lerp(const V3& a, const V3& b, float t) {
    return a + (b - a) * t;
}

inline bool Finite(const V3& a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

inline V3 Flatten(const V3& v, const V3& n) {
    return v - n * Dot(v, n);
}

inline bool InsideGroundRing(const V3& point, const V3& center, float radius) {
    if (!Finite(point) || !Finite(center) || !std::isfinite(radius) || radius < 0)
        return false;
    float length = Len(center);
    if (length <= 1e-6f)
        return false;
    V3 up = center * (1.0f / length);
    float along = Dot(point, up);
    // GroundRing projects center + tangent * radius onto the planet. Compare
    // the same angular footprint; model height must not turn an inside point red.
    return along > 0 && Len(Flatten(point, up)) * length <= radius * along;
}

inline V3 Rotate(const V3& v, const V3& axis, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return v * c + Cross(axis, v) * s + axis * (Dot(axis, v) * (1 - c));
}

struct M3 {
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    V3 Row(int i) const {
        return {m[i][0], m[i][1], m[i][2]};
    }

    void SetRow(int i, const V3& v) {
        m[i][0] = v.x;
        m[i][1] = v.y;
        m[i][2] = v.z;
    }

    static M3 FromRows(const V3& r0, const V3& r1, const V3& r2) {
        M3 r;
        r.SetRow(0, r0);
        r.SetRow(1, r1);
        r.SetRow(2, r2);
        return r;
    }
};

inline V3 ToWorld(const M3& r, const V3& local) {
    return r.Row(0) * local.x + r.Row(1) * local.y + r.Row(2) * local.z;
}

inline V3 ToLocal(const M3& r, const V3& world) {
    return {Dot(world, r.Row(0)), Dot(world, r.Row(1)), Dot(world, r.Row(2))};
}

inline M3 Mul(const M3& a, const M3& b) {
    M3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}

inline M3 Transposed(const M3& a) {
    M3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = a.m[j][i];
    return r;
}

struct Q {
    float x = 0, y = 0, z = 0, w = 1;
};

inline M3 ToMatrix(const Q& q) {
    M3 d;
    float sqw = q.w * q.w, sqx = q.x * q.x, sqy = q.y * q.y, sqz = q.z * q.z;
    float invs = 1.0f / (sqx + sqy + sqz + sqw);
    d.m[0][0] = (sqx - sqy - sqz + sqw) * invs;
    d.m[1][1] = (-sqx + sqy - sqz + sqw) * invs;
    d.m[2][2] = (-sqx - sqy + sqz + sqw) * invs;
    float t1 = q.x * q.y, t2 = q.z * q.w;
    d.m[0][1] = 2.0f * (t1 + t2) * invs;
    d.m[1][0] = 2.0f * (t1 - t2) * invs;
    t1 = q.x * q.z;
    t2 = q.y * q.w;
    d.m[0][2] = 2.0f * (t1 - t2) * invs;
    d.m[2][0] = 2.0f * (t1 + t2) * invs;
    t1 = q.y * q.z;
    t2 = q.x * q.w;
    d.m[1][2] = 2.0f * (t1 + t2) * invs;
    d.m[2][1] = 2.0f * (t1 - t2) * invs;
    return d;
}

inline Q ToQuat(const M3& a) {
    const auto& m = a.m;
    Q q;
    float trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0) {
        float s = 0.5f / std::sqrt(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (m[1][2] - m[2][1]) * s;
        q.y = (m[2][0] - m[0][2]) * s;
        q.z = (m[0][1] - m[1][0]) * s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = 2.0f * std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]);
        q.w = (m[1][2] - m[2][1]) / s;
        q.x = 0.25f * s;
        q.y = (m[0][1] + m[1][0]) / s;
        q.z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        float s = 2.0f * std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]);
        q.w = (m[2][0] - m[0][2]) / s;
        q.x = (m[1][0] + m[0][1]) / s;
        q.y = 0.25f * s;
        q.z = (m[2][1] + m[1][2]) / s;
    } else {
        float s = 2.0f * std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]);
        q.w = (m[0][1] - m[1][0]) / s;
        q.x = (m[2][0] + m[0][2]) / s;
        q.y = (m[2][1] + m[1][2]) / s;
        q.z = 0.25f * s;
    }
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l > 1e-8f) {
        q.x /= l;
        q.y /= l;
        q.z /= l;
        q.w /= l;
    }
    return q;
}

inline Q Slerp(Q a, Q b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    float ka, kb;
    if (d > 0.9995f) {
        ka = 1 - t;
        kb = t;
    } else {
        float th = std::acos(d), s = std::sin(th);
        ka = std::sin((1 - t) * th) / s;
        kb = std::sin(t * th) / s;
    }
    Q r{a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb};
    float l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (l > 1e-8f) {
        r.x /= l;
        r.y /= l;
        r.z /= l;
        r.w /= l;
    }
    return r;
}

inline M3 LookFrame(const V3& fwd, const V3& up) {
    V3 f = Norm(fwd, {0, 1, 0});
    V3 r = Cross(f, up);
    if (Len(r) < 1e-4f)
        r = Cross(f, std::fabs(f.z) < 0.9f ? V3{0, 0, 1} : V3{1, 0, 0});
    r = Norm(r);
    V3 u = Norm(Cross(r, f));
    return M3::FromRows(r, f, u);
}

inline float Clamp(float v, float a, float b) {
    return v < a ? a : (v > b ? b : v);
}

inline float Smooth(float t) {
    t = Clamp(t, 0, 1);
    return t * t * (3 - 2 * t);
}

inline float EaseInOutCubic(float t) {
    t = Clamp(t, 0, 1);
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

inline float Approach(float dt, float tau) {
    return tau <= 0 ? 1.0f : 1.0f - std::exp(-dt / tau);
}
}



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

struct Box { V3 lower, upper; };

inline bool ValidBox(const Box& box) {
    return Finite(box.lower) && Finite(box.upper) && box.upper.x > box.lower.x + 1e-4f &&
           box.upper.y > box.lower.y + 1e-4f && box.upper.z > box.lower.z + 1e-4f;
}

inline Box ShiftBox(const Box& box, const V3& shift) {
    return {box.lower + shift, box.upper + shift};
}

inline Box SweptBox(const Box& box, const V3& step) {
    return {box.lower + V3{std::fmin(0.0f,step.x), std::fmin(0.0f,step.y), std::fmin(0.0f,step.z)},
            box.upper + V3{std::fmax(0.0f,step.x), std::fmax(0.0f,step.y), std::fmax(0.0f,step.z)}};
}

inline bool BoxesOverlap(const Box& a, const Box& b) {
    return a.lower.x < b.upper.x && a.upper.x > b.lower.x && a.lower.y < b.upper.y &&
           a.upper.y > b.lower.y && a.lower.z < b.upper.z && a.upper.z > b.lower.z;
}

inline bool MovementEntersBox(const Box& moving, const V3& step, const Box& obstacle) {
    if (!ValidBox(moving) || !ValidBox(obstacle) || !Finite(step) || Dot(step,step) < 1e-12f)
        return false;
    V3 center = (moving.lower + moving.upper) * 0.5f;
    V3 half = (moving.upper - moving.lower) * 0.5f;
    V3 low = obstacle.lower - half, high = obstacle.upper + half;
    float p[3]={center.x,center.y,center.z}, d[3]={step.x,step.y,step.z};
    float lo[3]={low.x,low.y,low.z}, hi[3]={high.x,high.y,high.z};
    if (BoxesOverlap(moving,obstacle)) {
        float forward=1e30f, backward=1e30f;
        for (int i=0;i<3;++i) {
            if (std::fabs(d[i]) < 1e-7f)
                continue;
            float a=(lo[i]-p[i])/d[i], b=(hi[i]-p[i])/d[i];
            forward=std::fmin(forward,std::fmax(a,b));
            backward=std::fmin(backward,-std::fmin(a,b));
        }
        return forward > backward + 1e-5f;
    }
    float enter=0, leave=1;
    for (int i=0;i<3;++i) {
        if (std::fabs(d[i]) < 1e-7f) {
            if (p[i] <= lo[i] || p[i] >= hi[i])
                return false;
            continue;
        }
        float a=(lo[i]-p[i])/d[i], b=(hi[i]-p[i])/d[i];
        enter=std::fmax(enter,std::fmin(a,b));
        leave=std::fmin(leave,std::fmax(a,b));
        if (enter >= leave)
            return false;
    }
    return enter < 1 && leave > 0;
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

struct OBox {
    V3 center;
    V3 axis[3];
    float half[3] = {0, 0, 0};
};

inline OBox OrientedBox(const Box& local, const M3& rot, const V3& position) {
    OBox b;
    V3 c = (local.lower + local.upper) * 0.5f, h = (local.upper - local.lower) * 0.5f;
    b.center = position + ToWorld(rot, c);
    for (int i = 0; i < 3; ++i)
        b.axis[i] = Norm(rot.Row(i));
    b.half[0] = h.x;
    b.half[1] = h.y;
    b.half[2] = h.z;
    return b;
}

inline bool ValidOBox(const OBox& b) {
    if (!Finite(b.center))
        return false;
    for (int i = 0; i < 3; ++i)
        if (!Finite(b.axis[i]) || !std::isfinite(b.half[i]) || b.half[i] <= 1e-4f)
            return false;
    return true;
}

inline OBox ShiftOBox(OBox b, const V3& shift) {
    b.center = b.center + shift;
    return b;
}

inline float OBoxRadius(const OBox& b) {
    return std::sqrt(b.half[0] * b.half[0] + b.half[1] * b.half[1] + b.half[2] * b.half[2]);
}

inline Box OBoxBounds(const OBox& b) {
    V3 e;
    for (int i = 0; i < 3; ++i) {
        const V3& a = b.axis[i];
        e += V3{std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)} * b.half[i];
    }
    return {b.center - e, b.center + e};
}

inline float OBoxPenetration(const OBox& a, const OBox& b) {
    V3 axes[15];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        axes[n++] = a.axis[i];
        axes[n++] = b.axis[i];
    }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            V3 c = Cross(a.axis[i], b.axis[j]);
            float l = Len(c);
            if (l > 1e-4f)
                axes[n++] = c * (1.0f / l);
        }
    V3 d = b.center - a.center;
    float depth = 1e30f;
    for (int k = 0; k < n; ++k) {
        float ra = 0, rb = 0;
        for (int i = 0; i < 3; ++i) {
            ra += std::fabs(Dot(a.axis[i], axes[k])) * a.half[i];
            rb += std::fabs(Dot(b.axis[i], axes[k])) * b.half[i];
        }
        float overlap = ra + rb - std::fabs(Dot(d, axes[k]));
        if (overlap <= 0)
            return overlap;
        depth = std::fmin(depth, overlap);
    }
    return depth;
}

inline bool MovementEntersOBox(const OBox& moving, const V3& step, const OBox& obstacle) {
    if (!ValidOBox(moving) || !ValidOBox(obstacle) || !Finite(step) || Dot(step, step) < 1e-12f)
        return false;
    float start = OBoxPenetration(moving, obstacle);
    if (start > 0)
        return OBoxPenetration(ShiftOBox(moving, step), obstacle) > start + 1e-4f;
    float thin = 1e30f;
    for (int i = 0; i < 3; ++i)
        thin = std::fmin(thin, std::fmin(moving.half[i], obstacle.half[i]));
    int n = int(std::ceil(Len(step) / std::fmax(thin, 0.05f)));
    n = n < 1 ? 1 : (n > 64 ? 64 : n);
    for (int i = 1; i <= n; ++i)
        if (OBoxPenetration(ShiftOBox(moving, step * (float(i) / float(n))), obstacle) > 0)
            return true;
    return false;
}

inline V3 AllowedStep(const OBox& moving, const V3& step, const OBox& obstacle, const V3& up) {
    if (!MovementEntersOBox(moving, step, obstacle))
        return step;
    float lo = 0, hi = 1;
    for (int i = 0; i < 12; ++i) {
        float mid = (lo + hi) * 0.5f;
        if (MovementEntersOBox(moving, step * mid, obstacle))
            hi = mid;
        else
            lo = mid;
    }
    V3 done = step * lo, rest = Flatten(step - done, up);
    OBox at = ShiftOBox(moving, done);
    V3 best;
    for (int i = 0; i < 6; ++i) {
        V3 n = Norm(Flatten(i < 3 ? obstacle.axis[i] : moving.axis[i - 3], up), V3{});
        if (Dot(n, n) < 0.5f)
            continue;
        V3 slide = rest - n * Dot(rest, n);
        for (int k = 0; k < 2 && Dot(slide, slide) > Dot(best, best); ++k, slide = slide * 0.5f)
            if (!MovementEntersOBox(at, slide, obstacle)) {
                best = slide;
                break;
            }
    }
    return done + best;
}

inline V3 ClosestPoint(const OBox& b, const V3& p) {
    V3 d = p - b.center, q = b.center;
    for (int i = 0; i < 3; ++i)
        q += b.axis[i] * Clamp(Dot(d, b.axis[i]), -b.half[i], b.half[i]);
    return q;
}

inline float CylinderPenetration(const OBox& b, const V3& base, const V3& up, float radius, float height) {
    float centre = Dot(b.center - base, up), half = 0;
    for (int i = 0; i < 3; ++i)
        half += std::fabs(Dot(b.axis[i], up)) * b.half[i];
    if (centre + half <= 0 || centre - half >= height)
        return -1;
    V3 axis = base + up * Clamp(centre, 0, height);
    V3 nearest = ClosestPoint(b, axis);
    return radius - Len(Flatten(nearest - axis, up));
}

inline bool MovementEntersCylinder(const OBox& moving, const V3& step, const V3& base, const V3& up, float radius,
                                   float height) {
    if (!ValidOBox(moving) || !Finite(step) || !Finite(base) || !(radius > 0) || !(height > 0) ||
        Dot(step, step) < 1e-12f)
        return false;
    float before = CylinderPenetration(moving, base, up, radius, height);
    float after = CylinderPenetration(ShiftOBox(moving, step), base, up, radius, height);
    return after > 0 && after > before + 1e-4f;
}

inline int SplitOBox(const OBox& b, OBox* out, int max) {
    float thin = std::fmin(b.half[0], std::fmin(b.half[1], b.half[2]));
    int n[3];
    for (int i = 0; i < 3; ++i)
        n[i] = thin > 1e-4f ? int(Clamp(std::ceil(b.half[i] / thin - 0.25f), 1, 3)) : 1;
    int count = 0;
    for (int x = 0; x < n[0]; ++x)
        for (int y = 0; y < n[1]; ++y)
            for (int z = 0; z < n[2]; ++z) {
                if (count >= max)
                    return count;
                OBox c = b;
                const int at[3] = {x, y, z};
                c.center = b.center;
                for (int i = 0; i < 3; ++i) {
                    c.half[i] = b.half[i] / float(n[i]);
                    c.center += b.axis[i] * (-b.half[i] + c.half[i] * float(2 * at[i] + 1));
                }
                out[count++] = c;
            }
    return count;
}
}


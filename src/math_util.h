#pragma once

#include <cmath>
#include <cstdint>

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    Vector3 operator-(const Vector3& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }
};

struct Matrix4x4 {
    float m[4][4]{};

    float* operator[](size_t i) { return m[i]; }
    const float* operator[](size_t i) const { return m[i]; }
};

struct ScreenPosition {
    float x = 0.0f;
    float y = 0.0f;
    bool  visible = false;
};

class MathUtil {
public:
    [[nodiscard]] static ScreenPosition worldToScreen(
        const Vector3& worldPos,
        const Vector3& cameraPos,
        const Matrix4x4& viewProjection,
        int screenWidth,
        int screenHeight);

    [[nodiscard]] static float distance(const Vector3& a, const Vector3& b);

    [[nodiscard]] static Vector3 lerp(const Vector3& a, const Vector3& b, float t);
};

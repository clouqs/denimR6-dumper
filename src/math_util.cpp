#include "math_util.h"

ScreenPosition MathUtil::worldToScreen(
    const Vector3& worldPos,
    const Vector3& cameraPos,
    const Matrix4x4& viewProjection,
    int screenWidth,
    int screenHeight) {

    const Vector3 relative = worldPos - cameraPos;

    const float clipX = relative.x * viewProjection[0][0] +
                        relative.y * viewProjection[1][0] +
                        relative.z * viewProjection[2][0] +
                        viewProjection[3][0];

    const float clipY = relative.x * viewProjection[0][1] +
                        relative.y * viewProjection[1][1] +
                        relative.z * viewProjection[2][1] +
                        viewProjection[3][1];

    const float clipW = relative.x * viewProjection[0][3] +
                        relative.y * viewProjection[1][3] +
                        relative.z * viewProjection[2][3] +
                        viewProjection[3][3];

    ScreenPosition result;

    if (clipW < 0.0001f) {
        result.visible = false;
        return result;
    }

    const float ndcX = clipX / clipW;
    const float ndcY = clipY / clipW;

    result.x = static_cast<float>(screenWidth) * 0.5f + ndcX * static_cast<float>(screenWidth) * 0.5f;
    result.y = static_cast<float>(screenHeight) * 0.5f - ndcY * static_cast<float>(screenHeight) * 0.5f;
    result.visible = ndcX >= -1.5f && ndcX <= 1.5f && ndcY >= -1.5f && ndcY <= 1.5f;

    return result;
}

float MathUtil::distance(const Vector3& a, const Vector3& b) {
    const Vector3 diff = a - b;
    return diff.length();
}

Vector3 MathUtil::lerp(const Vector3& a, const Vector3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

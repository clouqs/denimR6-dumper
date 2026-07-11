#pragma once

#include "math_util.h"

#include <Windows.h>
#include <d3d11.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct EspEntity {
    uint64_t address = 0;
    Vector3  headPos;
    Vector3  feetPos;
    float    health = 0.0f;
    int32_t  teamId = 0;
    bool     isLocal = false;
    bool     isDead = false;
    std::string name;
};

class Overlay {
public:
    Overlay() = default;
    ~Overlay();

    bool init(HWND gameWindow);
    void shutdown();

    void render(const std::vector<EspEntity>& entities, const Vector3& cameraPos, const Matrix4x4& vpMatrix);

private:
    HWND gameWindow_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTarget_ = nullptr;
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* pixelShader_ = nullptr;
    ID3D11InputLayout* inputLayout_ = nullptr;
    ID3D11BlendState* blendState_ = nullptr;
    ID3D11RasterizerState* rasterizerState_ = nullptr;
    ID3D11SamplerState* samplerState_ = nullptr;
    ID3D11Buffer* vertexBuffer_ = nullptr;

    void createDevice();
    void createShaders();
    void createStates();
    void drawLine(float x0, float y0, float x1, float y1, float r, float g, float b, float a);
    void drawBoundingBox(float centerX, float centerY, float width, float height, float r, float g, float b);
};

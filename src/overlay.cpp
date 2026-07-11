#include "overlay.h"

#include <d3dcompiler.h>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {

struct Vertex {
    float x, y, z;
    float color;
};

const char* kVertexShaderSource = R"(
    struct VS_INPUT {
        float3 pos : POSITION;
        float  rgba : COLOR;
    };

    struct PS_INPUT {
        float4 pos : SV_POSITION;
        float  rgba : COLOR;
    };

    PS_INPUT VS(VS_INPUT input) {
        PS_INPUT output;
        output.pos = float4(input.pos, 1.0f);
        output.rgba = input.rgba;
        return output;
    }
)";

const char* kPixelShaderSource = R"(
    struct PS_INPUT {
        float4 pos : SV_POSITION;
        float  rgba : COLOR;
    };

    float4 PS(PS_INPUT input) : SV_TARGET {
        return float4(input.rgba, input.rgba, input.rgba, input.rgba);
    }
)";

} // namespace

Overlay::~Overlay() {
    shutdown();
}

void Overlay::createDevice() {
    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferCount = 1;
    swapDesc.BufferDesc.Width = 0;
    swapDesc.BufferDesc.Height = 0;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = gameWindow_;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swapDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &swapDesc,
        &swapChain_,
        &device_,
        nullptr,
        &context_);

    if (FAILED(hr)) {
        std::cerr << "[Overlay] Failed to create D3D11 device: 0x"
                  << std::hex << hr << std::dec << "\n";
        return;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        device_->CreateRenderTargetView(backBuffer, nullptr, &renderTarget_);
        backBuffer->Release();
    }

    if (context_ && renderTarget_) {
        context_->OMSetRenderTargets(1, &renderTarget_, nullptr);

        D3D11_VIEWPORT viewport{};
        viewport.Width = 1920;
        viewport.Height = 1080;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &viewport);
    }
}

void Overlay::createShaders() {
    if (!device_) {
        return;
    }

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;

    HRESULT vsResult = D3DCompile(
        kVertexShaderSource,
        strlen(kVertexShaderSource),
        nullptr,
        nullptr,
        nullptr,
        "VS",
        "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &vsBlob,
        nullptr);

    HRESULT psResult = D3DCompile(
        kPixelShaderSource,
        strlen(kPixelShaderSource),
        nullptr,
        nullptr,
        nullptr,
        "PS",
        "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &psBlob,
        nullptr);

    if (SUCCEEDED(vsResult) && vsBlob) {
        device_->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr,
            &vertexShader_);
    }

    if (SUCCEEDED(psResult) && psBlob) {
        device_->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            &pixelShader_);
        psBlob->Release();
    }

    if (vertexShader_ && vsBlob) {
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R32_FLOAT,        0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        device_->CreateInputLayout(
            layout,
            2,
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            &inputLayout_);

        vsBlob->Release();
    }
}

void Overlay::createStates() {
    if (!device_) {
        return;
    }

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&blendDesc, &blendState_);

    D3D11_RASTERIZER_DESC rastDesc{};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable = TRUE;
    device_->CreateRasterizerState(&rastDesc, &rasterizerState_);

    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&sampDesc, &samplerState_);
}

void Overlay::drawLine(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    (void)r;
    (void)g;
    (void)b;

    if (!context_ || !inputLayout_) {
        return;
    }

    const Vertex vertices[] = {
        {x0, y0, 0.5f, a},
        {x1, y1, 0.5f, a},
    };

    D3D11_BUFFER_DESC bufDesc{};
    bufDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufDesc.ByteWidth = sizeof(vertices);
    bufDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = vertices;

    ID3D11Buffer* buf = nullptr;
    HRESULT hr = device_->CreateBuffer(&bufDesc, &initData, &buf);
    if (FAILED(hr)) {
        return;
    }

    context_->IASetInputLayout(inputLayout_);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(pixelShader_, nullptr, 0);

    const float blendFactor = 0.0f;
    context_->OMSetBlendState(blendState_, &blendFactor, 0xFFFFFFFF);
    context_->RSSetState(rasterizerState_);
    context_->PSSetSamplers(0, 1, &samplerState_);

    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    context_->IASetVertexBuffers(0, 1, &buf, &stride, &offset);
    context_->Draw(2, 0);

    buf->Release();
}

void Overlay::drawBoundingBox(float centerX, float centerY, float width, float height, float r, float g, float b) {
    const float halfW = width * 0.5f;
    const float halfH = height * 0.5f;

    drawLine(centerX - halfW, centerY - halfH, centerX + halfW, centerY - halfH, r, g, b, 1.0f);
    drawLine(centerX + halfW, centerY - halfH, centerX + halfW, centerY + halfH, r, g, b, 1.0f);
    drawLine(centerX + halfW, centerY + halfH, centerX - halfW, centerY + halfH, r, g, b, 1.0f);
    drawLine(centerX - halfW, centerY + halfH, centerX - halfW, centerY - halfH, r, g, b, 1.0f);
}

bool Overlay::init(HWND gameWindow) {
    gameWindow_ = gameWindow;
    createDevice();
    createShaders();
    createStates();
    return device_ != nullptr && context_ != nullptr;
}

void Overlay::shutdown() {
    if (vertexBuffer_) vertexBuffer_->Release();
    if (samplerState_) samplerState_->Release();
    if (rasterizerState_) rasterizerState_->Release();
    if (blendState_) blendState_->Release();
    if (inputLayout_) inputLayout_->Release();
    if (pixelShader_) pixelShader_->Release();
    if (vertexShader_) vertexShader_->Release();
    if (renderTarget_) renderTarget_->Release();
    if (swapChain_) swapChain_->Release();
    if (context_) context_->Release();
    if (device_) device_->Release();

    vertexBuffer_ = nullptr;
    samplerState_ = nullptr;
    rasterizerState_ = nullptr;
    blendState_ = nullptr;
    inputLayout_ = nullptr;
    pixelShader_ = nullptr;
    vertexShader_ = nullptr;
    renderTarget_ = nullptr;
    swapChain_ = nullptr;
    context_ = nullptr;
    device_ = nullptr;
}

void Overlay::render(const std::vector<EspEntity>& entities, const Vector3& cameraPos, const Matrix4x4& vpMatrix) {
    if (!swapChain_ || !context_) {
        return;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (!backBuffer) {
        return;
    }

    ID3D11RenderTargetView* currentTarget = nullptr;
    context_->OMGetRenderTargets(1, &currentTarget, nullptr);
    if (currentTarget != renderTarget_) {
        device_->CreateRenderTargetView(backBuffer, nullptr, &renderTarget_);
    }
    backBuffer->Release();

    if (!renderTarget_) {
        return;
    }

    context_->OMSetRenderTargets(1, &renderTarget_, nullptr);

    D3D11_VIEWPORT viewport{};
    UINT numViewports = 1;
    context_->RSGetViewports(&numViewports, &viewport);

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    context_->ClearRenderTargetView(renderTarget_, clearColor);

    for (const auto& entity : entities) {
        if (entity.isLocal || entity.isDead) {
            continue;
        }

        const auto head = MathUtil::worldToScreen(
            entity.headPos, cameraPos, vpMatrix,
            static_cast<int>(viewport.Width),
            static_cast<int>(viewport.Height));

        const auto feet = MathUtil::worldToScreen(
            entity.feetPos, cameraPos, vpMatrix,
            static_cast<int>(viewport.Width),
            static_cast<int>(viewport.Height));

        if (!head.visible || !feet.visible) {
            continue;
        }

        const float height = std::abs(head.y - feet.y);
        const float width = height * 0.6f;
        const float centerX = (head.x + feet.x) * 0.5f;
        const float centerY = (head.y + feet.y) * 0.5f;

        const float r = entity.teamId == 0 ? 1.0f : 0.0f;
        const float g = entity.teamId == 0 ? 0.0f : 1.0f;
        const float b = 0.0f;

        drawBoundingBox(centerX, centerY, width, height, r, g, b);

        if (entity.health > 0) {
            drawLine(centerX - width * 0.5f, feet.y + 5,
                     centerX - width * 0.5f + (width * entity.health * 0.01f), feet.y + 5,
                     0.0f, 1.0f, 0.0f, 1.0f);
        }
    }

    swapChain_->Present(1, 0);
}

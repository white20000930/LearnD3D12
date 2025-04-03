#pragma once

#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/d3dUtil.h"

struct ObjectConstants {
    // 局部空间到世界空间的矩阵
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    // 纹理变换矩阵 ?
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
};

struct PassConstants {
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();

    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f; //?
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f }; // 环境光

    DirectX::XMFLOAT4 FogColor = { 0.7f, 0.7f, 0.7f, 1.0f }; // 雾的颜色
    float gFogStart = 5.0f; // 雾的起始位置
    float gFogRange = 150.0f; // 雾的范围
    DirectX::XMFLOAT2 cbPerObjectPad2; //?

    Light Lights[MaxLights];
};

struct Vertex {
    Vertex() = default;
    Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v)
        : Pos(x, y, z)
        , Normal(nx, ny, nz)
        , TexC(u, v)
    {
    }

    DirectX::XMFLOAT3 Pos; // 顶点位置
    DirectX::XMFLOAT3 Normal; // 顶点法线
    DirectX::XMFLOAT2 TexC; // 纹理坐标
};

struct FrameResource {
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
    {
        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

        PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
        MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
        ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
    };
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource() {

    };

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    // 在GPU处理完对应cmd之前，CPU不应修改CB中的数据，所以每个FrameResource都有自己的CB
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr; //
    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr; // 材质
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr; // 变换矩阵

    UINT64 Fence = 0;
};
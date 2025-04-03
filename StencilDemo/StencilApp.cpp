
#include "../Common/GeometryGenerator.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/d3dApp.h"

#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

struct RenderItem {
    RenderItem() = default;

    XMFLOAT4X4 World = MathHelper::Identity4x4();

    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    int NumFramesDirty = gNumFrameResources;

    UINT ObjCBIndex = -1;

    Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;

    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int {
    Opaque = 0, // 不透明
    Mirrors, // 镜子
    Reflected, // 反射
    Transparent, // 透明
    Shadow, // 阴影
    Count // 计数用的
};

class StencilApp : public D3DApp {
public:
    StencilApp(HINSTANCE hInstance);
    StencilApp(const StencilApp& rhs) = delete;
    StencilApp& operator=(const StencilApp& rhs) = delete;
    ~StencilApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    virtual void OnKeyboardInput(WPARAM wParam) override;

    void UpdateSkull(const GameTimer& gt);
    void UpdateCamera(const GameTimer& gt);
    void AnimateMaterials(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateReflectedPassCB(const GameTimer& gt);

    void LoadTextures();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildRoomGeometry();
    void BuildSkullGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    // Cache render items of interest.
    RenderItem* mSkullRitem = nullptr; // 指向骷髅渲染项的指针
    RenderItem* mReflectedSkullRitem = nullptr; // 指向反射的骷髅渲染项的指针
    RenderItem* mShadowedSkullRitem = nullptr; // 指向有阴影的骷髅渲染项的指针

    // List of all the render items.
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;

    // Render items divided by PSO.
    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    PassConstants mMainPassCB; // 主Pass常量缓冲区
    PassConstants mReflectedPassCB; // 反射Pass常量缓冲区

    XMFLOAT3 mSkullTranslation = { 0.0f, 1.0f, -5.0f }; // 骷髅的位置

    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.24f * XM_PI;
    float mPhi = 0.42f * XM_PI;
    float mRadius = 12.0f;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try {
        StencilApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        return theApp.Run();
    } catch (DxException& e) {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

StencilApp::StencilApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

StencilApp::~StencilApp()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();
}

bool StencilApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // 获取描述符堆中描述符的增量大小，这个大小是固定的，与设备有关
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    LoadTextures(); // 从dds文件中加载纹理
    BuildRootSignature(); // 创建根签名
    BuildDescriptorHeaps(); // 创建纹理的SRV堆，并初始化SRV
    BuildShadersAndInputLayout();
    BuildRoomGeometry();
    BuildSkullGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}

void StencilApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void StencilApp::Update(const GameTimer& gt)
{
    UpdateCamera(gt);
    UpdateSkull(gt);
    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence) {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
    UpdateReflectedPassCB(gt);
}

void StencilApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport); // 设置视口
    mCommandList->RSSetScissorRects(1, &mScissorRect); // 设置裁剪矩形

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)); // 资源状态转换

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr); // 重置后台缓冲区的颜色为某一颜色
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr); // 重置深度缓冲区的值为1.0

    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView()); // 设置渲染目标

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps); // 设置描述符堆
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get()); // 设置根签名
    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

    // 绘制不透明物体
    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress()); // 设置根描述符表
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

    // 用1，在模板缓冲区标记镜子所在像素。这一步不需要绘制东西，只标记
    mCommandList->OMSetStencilRef(1);
    mCommandList->SetPipelineState(mPSOs["markStencilMirrors"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Mirrors]);

    // 绘制镜子所在像素 区域的 反射
    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
    mCommandList->SetPipelineState(mPSOs["drawStencilReflections"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Reflected]);

    // 重置 CB和模板缓冲索引
    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
    mCommandList->OMSetStencilRef(0);

    // 绘制透明镜子
    mCommandList->SetPipelineState(mPSOs["transparent"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

    // 绘制阴影
    mCommandList->SetPipelineState(mPSOs["shadow"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Shadow]);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void StencilApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void StencilApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void StencilApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0) {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    } else if ((btnState & MK_RBUTTON) != 0) {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void StencilApp::OnKeyboardInput(WPARAM wParam) // 不对劲，待优化
{
    float offset = 0.06f;
    switch (wParam) {
    case 'A':
        mSkullTranslation.x -= offset; // A，左移
        break;
    case 'D':
        mSkullTranslation.x += offset; // D，右移
        break;
    case 'W':
        mSkullTranslation.z += offset; // W，前移
        break;
    case 'S':
        mSkullTranslation.z -= offset; // S，后移
        break;
    }
}

void StencilApp::UpdateSkull(const GameTimer& gt)
{
    // 更新 骷髅 世界矩阵
    XMMATRIX skullRotate = XMMatrixRotationY(0.5f * MathHelper::Pi);
    XMMATRIX skullScale = XMMatrixScaling(0.45f, 0.45f, 0.45f);
    XMMATRIX skullOffset = XMMatrixTranslation(mSkullTranslation.x, mSkullTranslation.y, mSkullTranslation.z);
    XMMATRIX skullWorld = skullRotate * skullScale * skullOffset;
    XMStoreFloat4x4(&mSkullRitem->World, skullWorld);

    // 更新 头骨镜像的 世界矩阵
    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // ？
    XMMATRIX R = XMMatrixReflect(mirrorPlane); // 反射矩阵
    XMStoreFloat4x4(&mReflectedSkullRitem->World, skullWorld * R);

    // 更新 有阴影的头骨 世界矩阵
    XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR toMainLight = -XMLoadFloat3(&mMainPassCB.Lights[0].Direction);
    XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight); // 阴影矩阵
    XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.01f, 0.0f); // 阴影偏移
    XMStoreFloat4x4(&mShadowedSkullRitem->World, skullWorld * S * shadowOffsetY);

    // 标记接下来的帧中需要更新FrameResources的资源  （CPU记录的数据，并没有更新GPU缓冲区的）
    mSkullRitem->NumFramesDirty = gNumFrameResources;
    mReflectedSkullRitem->NumFramesDirty = gNumFrameResources;
    mShadowedSkullRitem->NumFramesDirty = gNumFrameResources;
}

void StencilApp::UpdateCamera(const GameTimer& gt)
{

    mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
    mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
    mEyePos.y = mRadius * cosf(mPhi);

    XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view); // 更新视图矩阵
}

void StencilApp::AnimateMaterials(const GameTimer& gt)
{
}

void StencilApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for (auto& e : mAllRitems) {
        if (e->NumFramesDirty > 0) {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            e->NumFramesDirty--;
        }
    }
}

void StencilApp::UpdateMaterialCBs(const GameTimer& gt) // 暂时没什么用
{
    auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
    for (auto& e : mMaterials) {
        // Only update the cbuffer data if the constants have changed.  If the cbuffer
        // data changes, it needs to be updated for each FrameResource.
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0) {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialConstants matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

            currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

            mat->NumFramesDirty--;
        }
    }
}

void StencilApp::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX view = XMLoadFloat4x4(&mView);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

    mMainPassCB.EyePosW = mEyePos;
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 1.0f;
    mMainPassCB.FarZ = 1000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
    mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
    mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
    mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void StencilApp::UpdateReflectedPassCB(const GameTimer& gt)
{
    mReflectedPassCB = mMainPassCB;

    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
    XMMATRIX R = XMMatrixReflect(mirrorPlane);

    // Reflect the lighting.
    for (int i = 0; i < 3; ++i) {
        XMVECTOR lightDir = XMLoadFloat3(&mMainPassCB.Lights[i].Direction);
        XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
        XMStoreFloat3(&mReflectedPassCB.Lights[i].Direction, reflectedLightDir);
    }

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(1, mReflectedPassCB);
}

void StencilApp::LoadTextures()
{
    // 砖块纹理
    auto bricksTex = std::make_unique<Texture>();
    bricksTex->Name = "bricksTex"; // 名
    bricksTex->Filename = L"../Textures/bricks3.dds"; // 路径
    // 创建纹理
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), bricksTex->Filename.c_str(), bricksTex->Resource, bricksTex->UploadHeap));

    // 棋盘纹理
    auto checkboardTex = std::make_unique<Texture>();
    checkboardTex->Name = "checkboardTex"; // 名
    checkboardTex->Filename = L"../Textures/checkboard.dds"; // 路径
    // 创建纹理
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), checkboardTex->Filename.c_str(), checkboardTex->Resource, checkboardTex->UploadHeap));

    // 冰纹理
    auto iceTex = std::make_unique<Texture>();
    iceTex->Name = "iceTex"; // 名
    iceTex->Filename = L"../Textures/ice.dds"; // 路径
    // 创建纹理
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), iceTex->Filename.c_str(), iceTex->Resource, iceTex->UploadHeap));

    // 白色纹理
    auto white1x1Tex = std::make_unique<Texture>();
    white1x1Tex->Name = "white1x1Tex"; // 名
    white1x1Tex->Filename = L"../Textures/white1x1.dds"; // 路径
    // 创建纹理
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), white1x1Tex->Filename.c_str(), white1x1Tex->Resource, white1x1Tex->UploadHeap));

    // 将智能指针移动到容器中
    mTextures[bricksTex->Name] = std::move(bricksTex);
    mTextures[checkboardTex->Name] = std::move(checkboardTex);
    mTextures[iceTex->Name] = std::move(iceTex);
    mTextures[white1x1Tex->Name] = std::move(white1x1Tex);
}

void StencilApp::BuildRootSignature()
{
    // 根参数数组 不包含采样器
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    // 根参数1 对应hlsl中的t0
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); /// 参数1：类型；2：描述符数量；3：起始寄存器索引

    slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    // 根参数2 对应hlsl中的b0
    slotRootParameter[1].InitAsConstantBufferView(0);
    // 根参数3 对应hlsl中的b1
    slotRootParameter[2].InitAsConstantBufferView(1);
    // 根参数4 对应hlsl中的b2
    slotRootParameter[3].InitAsConstantBufferView(2);

    // 根参数与hlsl中寄存器一一对应，所以在draw时，才可以通过根参数，将常量缓冲区绑定到对应的寄存器上 ？

    // 获取采样器列表
    auto staticSamplers = GetStaticSamplers();

    // 初始化根签名描述
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // 序列化根签名描述符
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr) {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    // 创建根签名
    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void StencilApp::BuildDescriptorHeaps()
{
    // 创建SRV堆------------------------------------------------------------

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 4; // 堆中的view数量
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; // 类型
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // 着色器可见
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    // 用SRV填充堆------------------------------------------------------------

    // hDescriptor为SRV堆的第一个句柄
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; // ？
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = -1; // Mip=-1表示使用所有级别

    // 创建四种纹理的SRV
    auto bricksTex = mTextures["bricksTex"]->Resource; // 获取纹理资源
    srvDesc.Format = bricksTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor); // SRV记录了纹理资源的句柄、格式等信息

    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    auto checkboardTex = mTextures["checkboardTex"]->Resource;
    srvDesc.Format = checkboardTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(checkboardTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    auto iceTex = mTextures["iceTex"]->Resource;
    srvDesc.Format = iceTex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    auto white1x1Tex = mTextures["white1x1Tex"]->Resource;
    srvDesc.Format = white1x1Tex->GetDesc().Format;
    md3dDevice->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, hDescriptor);
}

void StencilApp::BuildShadersAndInputLayout()
{
    const D3D_SHADER_MACRO defines[] = {
        "FOG", "1", // Name, Value
        NULL, NULL
    };

    const D3D_SHADER_MACRO alphaTestDefines[] = {
        "FOG", "1",
        "ALPHA_TEST", "1",
        NULL, NULL
    };

    // 顶点着色器
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Default.hlsl", nullptr, "VS", "vs_5_0");
    // 像素着色器，不透明，只传入了FOG宏
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Default.hlsl", defines, "PS", "ps_5_0");
    // 像素着色器，有透明效果，传入了FOG和ALPHA_TEST宏
    mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Default.hlsl", alphaTestDefines, "PS", "ps_5_0");

    // 定义顶点输入布局  与hlsl中的结构体对应
    mInputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void StencilApp::BuildRoomGeometry()
{
    // 创建并指定几何数据。在这个示例中，我们绘制一个地板和一面带有镜子的墙。
    // 我们将地板、墙和镜子的几何数据放在一个顶点缓冲区中。
    //
    //   |--------------|
    //   |              |
    //   |----|----|----|
    //   |墙  |镜子|墙  |
    //   |    |    |    |
    //  /--------------/
    // /   地板       /
    ///--------------/

    // 定义包含20个顶点的数组
    std::array<Vertex, 20> vertices = {
        //(x, y, z, nx, ny, nz, u, v)
        // 地板：注意我们对纹理坐标进行了平铺处理
        Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0
        Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(7.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
        Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

        // 墙：注意我们对纹理坐标进行了平铺处理，并且在中间留出了镜子的空间
        Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
        Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
        Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

        Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8
        Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f),
        Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f),

        Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
        Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f),
        Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f),

        // 镜子
        Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 16
        Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
        Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
    };

    std::array<std::int16_t, 30> indices = {
        // Floor
        0, 1, 2,
        0, 2, 3,

        // Walls
        4, 5, 6,
        4, 6, 7,

        8, 9, 10,
        8, 10, 11,

        12, 13, 14,
        12, 14, 15,

        // Mirror
        16, 17, 18,
        16, 18, 19
    };

    SubmeshGeometry floorSubmesh(6, 0, 0);
    SubmeshGeometry wallSubmesh(18, 6, 0);
    SubmeshGeometry mirrorSubmesh(6, 24, 0);

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int16_t);

    auto roomGeo = std::make_unique<MeshGeometry>();
    roomGeo->Name = "roomGeo";

    // 在内存中保存一份顶点数据（暂时它还不用于绘制）
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &roomGeo->VertexBufferCPU));
    CopyMemory(roomGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &roomGeo->IndexBufferCPU));
    CopyMemory(roomGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    // 创建顶点缓冲区
    roomGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, roomGeo->VertexBufferUploader);

    // 创建索引缓冲区
    roomGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, roomGeo->IndexBufferUploader);

    roomGeo->VertexByteStride = sizeof(Vertex); // 顶点结构体大小 stride步幅
    roomGeo->VertexBufferByteSize = vbByteSize; // 顶点缓冲区大小
    roomGeo->IndexFormat = DXGI_FORMAT_R16_UINT; // 索引格式
    roomGeo->IndexBufferByteSize = ibByteSize; // 索引缓冲区大小

    // 为几何体设置子网格
    roomGeo->DrawArgs["floor"] = floorSubmesh;
    roomGeo->DrawArgs["wall"] = wallSubmesh;
    roomGeo->DrawArgs["mirror"] = mirrorSubmesh;

    // 将几何体添加到容器
    mGeometries[roomGeo->Name] = std::move(roomGeo);
}

void StencilApp::BuildSkullGeometry()
{
    std::ifstream fin("Models/skull.txt");

    if (!fin) {
        MessageBox(0, L"Models/skull.txt not found.", 0, 0);
        return;
    }

    UINT vcount = 0;
    UINT tcount = 0;
    std::string ignore;

    fin >> ignore >> vcount;
    fin >> ignore >> tcount;
    fin >> ignore >> ignore >> ignore >> ignore;

    std::vector<Vertex> vertices(vcount);
    for (UINT i = 0; i < vcount; ++i) {
        fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
        fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

        // Model does not have texture coordinates, so just zero them out.
        vertices[i].TexC = { 0.0f, 0.0f };
    }

    fin >> ignore;
    fin >> ignore;
    fin >> ignore;

    std::vector<std::int32_t> indices(3 * tcount);
    for (UINT i = 0; i < tcount; ++i) {
        fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
    }

    fin.close();

    //
    // Pack the indices of all the meshes into one index buffer.
    //

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "skullGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh((UINT)indices.size(), 0, 0);
    geo->DrawArgs["skull"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void StencilApp::BuildPSOs()
{
    // 渲染顺序？
    //  不透明PSO--------------------------
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    opaquePsoDesc.pRootSignature = mRootSignature.Get();
    opaquePsoDesc.VS = {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS = {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); // 光栅化器状态  顺时针为正面
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT); // 混合状态 默认为不混合
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // 深度模板状态
    opaquePsoDesc.SampleMask = UINT_MAX; // 采样掩码
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // 图元拓扑类型
    opaquePsoDesc.NumRenderTargets = 1; // 渲染目标数量
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat; // 渲染目标格式
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1; // 采样
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0; // 采样质量
    opaquePsoDesc.DSVFormat = mDepthStencilFormat; // 深度模板视图 DXGI_FORMAT_D24_UNORM_S8_UINT 为24位深度，8位模板

    // 创建不透明PSO
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    // 透明PSO--------------------------

    // 混合状态
    D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
    transparencyBlendDesc.BlendEnable = true; // 开启常规混合
    transparencyBlendDesc.LogicOpEnable = false; // 逻辑运算符混合 二者只能开一个
    transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA; // 源混合因子 为 源颜色的alpha值
    transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; // 目标混合因子 为 1-源颜色的alpha值
    transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD; // 混合操作 为 源颜色*源因子+目标颜色*目标因子
    transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE; // 透明度 源混合因子 为 1
    transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO; // 透明度 目标混合因子 为 0
    transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD; // 透明度 混合操作 为 源颜色*源因子+目标颜色*目标因子
    transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP; // 逻辑运算符操作为无
    transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; // 渲染目标写入掩码 为 全部颜色通道可写

    // 基于不透明物体的PSO配置初始化透明物体的PSO配置
    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;
    transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc; // 更改混合状态 渲染目标 ？
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

    // 镜子PSO--------------------------
    // 主要作用是在模板缓冲区中标记出镜子所在的区域，而不直接绘制颜色

    CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT); // 基于默认混合状态初始化镜子的混合状态
    mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0; // 禁用颜色写入

    D3D12_DEPTH_STENCIL_DESC mirrorDSS;
    mirrorDSS.DepthEnable = true; // 开启深度测试
    mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 禁止深度写入，确保镜子位置正确，但不会影响深度缓冲区
    mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS; // 深度比较函数
    mirrorDSS.StencilEnable = true; // 开启模板测试
    mirrorDSS.StencilReadMask = 0xff; // 模板读掩码
    mirrorDSS.StencilWriteMask = 0xff; // 模板写掩码

    // 正面测试设置
    mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP; // 模板测试失败时的操作:保持
    mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP; // 深度测试失败时的操作:保持
    mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE; // 模板测试通过时的操作:替换
    mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS; // 模板测试函数

    // 背面设置，无关紧要，因为不渲染背面
    mirrorDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    mirrorDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    mirrorDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    mirrorDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    // 镜子的pso
    // 基于不透明物体的PSO配置初始化标记镜子的PSO配置
    D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsPsoDesc = opaquePsoDesc;
    markMirrorsPsoDesc.BlendState = mirrorBlendState; // 混合状态
    markMirrorsPsoDesc.DepthStencilState = mirrorDSS; // 深度模板状态
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&markMirrorsPsoDesc, IID_PPV_ARGS(&mPSOs["markStencilMirrors"])));

    // 模板反射PSO--------------------------
    D3D12_DEPTH_STENCIL_DESC reflectionsDSS;
    reflectionsDSS.DepthEnable = true; // 开启深度测试
    reflectionsDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; // 允许写入深度
    reflectionsDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS; // 深度比较函数：小于
    reflectionsDSS.StencilEnable = true; // 开启模板测试
    reflectionsDSS.StencilReadMask = 0xff; // 模板读掩码
    reflectionsDSS.StencilWriteMask = 0xff; // 模板写掩码

    // 正面设置
    reflectionsDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP; // 模板测试失败时的操作：保持
    reflectionsDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP; // 深度测试失败时的操作：保持
    reflectionsDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP; // 模板测试通过时的操作：保持，因为模板缓冲区中的信息是标记信息，这一步主要是利用，而不是修改
    reflectionsDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL; // 模板测试函数：等于

    // 背面设置，无关紧要，因为不渲染背面
    reflectionsDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

    // 模板反射的pso
    // 基于不透明物体的PSO配置初始化标记模板反射的PSO配置
    D3D12_GRAPHICS_PIPELINE_STATE_DESC drawReflectionsPsoDesc = opaquePsoDesc;
    drawReflectionsPsoDesc.DepthStencilState = reflectionsDSS; // 深度模板状态
    drawReflectionsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; // 剔除背面
    drawReflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true; // 逆时针为正面
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawReflectionsPsoDesc, IID_PPV_ARGS(&mPSOs["drawStencilReflections"])));

    // 阴影PSO--------------------------

    D3D12_DEPTH_STENCIL_DESC shadowDSS;
    shadowDSS.DepthEnable = true; // 开启深度测试
    shadowDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; // 允许写入深度
    shadowDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS; // 深度比较函数：小于
    shadowDSS.StencilEnable = true; // 开启模板测试
    shadowDSS.StencilReadMask = 0xff; // 模板读掩码
    shadowDSS.StencilWriteMask = 0xff; // 模板写掩码

    // 正面设置
    shadowDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP; // 模板测试失败时的操作：保持
    shadowDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP; // 深度测试失败时的操作：保持
    shadowDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR; // 模板测试通过时的操作：递增
    shadowDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL; // 模板测试函数：等于

    // 背面设置
    shadowDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    shadowDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    shadowDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
    shadowDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

    // 基于透明物体的PSO配置初始化标记阴影的PSO配置
    D3D12_GRAPHICS_PIPELINE_STATE_DESC drawShadowsPsoDesc = transparentPsoDesc;
    drawShadowsPsoDesc.DepthStencilState = shadowDSS; // 深度模板状态
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawShadowsPsoDesc, IID_PPV_ARGS(&mPSOs["shadow"])));
}

void StencilApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i) {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            2, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void StencilApp::BuildMaterials()
{
    auto bricks = std::make_unique<Material>();
    bricks->Name = "bricks";
    bricks->MatCBIndex = 0;
    bricks->DiffuseSrvHeapIndex = 0;
    bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    bricks->Roughness = 0.25f;

    auto checkertile = std::make_unique<Material>();
    checkertile->Name = "checkertile";
    checkertile->MatCBIndex = 1;
    checkertile->DiffuseSrvHeapIndex = 1;
    checkertile->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    checkertile->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
    checkertile->Roughness = 0.3f;

    auto icemirror = std::make_unique<Material>();
    icemirror->Name = "icemirror";
    icemirror->MatCBIndex = 2;
    icemirror->DiffuseSrvHeapIndex = 2;
    icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
    icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    icemirror->Roughness = 0.5f;

    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 3;
    skullMat->DiffuseSrvHeapIndex = 3;
    skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    skullMat->Roughness = 0.3f;

    auto shadowMat = std::make_unique<Material>();
    shadowMat->Name = "shadowMat";
    shadowMat->MatCBIndex = 4;
    shadowMat->DiffuseSrvHeapIndex = 3;
    shadowMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
    shadowMat->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
    shadowMat->Roughness = 0.0f;

    mMaterials["bricks"] = std::move(bricks);
    mMaterials["checkertile"] = std::move(checkertile);
    mMaterials["icemirror"] = std::move(icemirror);
    mMaterials["skullMat"] = std::move(skullMat);
    mMaterials["shadowMat"] = std::move(shadowMat);
}

void StencilApp::BuildRenderItems()
{
    auto floorRitem = std::make_unique<RenderItem>();
    floorRitem->World = MathHelper::Identity4x4();
    floorRitem->TexTransform = MathHelper::Identity4x4();
    floorRitem->ObjCBIndex = 0;
    floorRitem->Mat = mMaterials["checkertile"].get();
    floorRitem->Geo = mGeometries["roomGeo"].get();
    floorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    floorRitem->IndexCount = floorRitem->Geo->DrawArgs["floor"].IndexCount;
    floorRitem->StartIndexLocation = floorRitem->Geo->DrawArgs["floor"].StartIndexLocation;
    floorRitem->BaseVertexLocation = floorRitem->Geo->DrawArgs["floor"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(floorRitem.get());

    auto wallsRitem = std::make_unique<RenderItem>();
    wallsRitem->World = MathHelper::Identity4x4();
    wallsRitem->TexTransform = MathHelper::Identity4x4();
    wallsRitem->ObjCBIndex = 1;
    wallsRitem->Mat = mMaterials["bricks"].get();
    wallsRitem->Geo = mGeometries["roomGeo"].get();
    wallsRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wallsRitem->IndexCount = wallsRitem->Geo->DrawArgs["wall"].IndexCount;
    wallsRitem->StartIndexLocation = wallsRitem->Geo->DrawArgs["wall"].StartIndexLocation;
    wallsRitem->BaseVertexLocation = wallsRitem->Geo->DrawArgs["wall"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(wallsRitem.get());

    auto skullRitem = std::make_unique<RenderItem>();
    skullRitem->World = MathHelper::Identity4x4();
    skullRitem->TexTransform = MathHelper::Identity4x4();
    skullRitem->ObjCBIndex = 2;
    skullRitem->Mat = mMaterials["skullMat"].get();
    skullRitem->Geo = mGeometries["skullGeo"].get();
    skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
    skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
    skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
    mSkullRitem = skullRitem.get();
    mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());

    // Reflected skull will have different world matrix, so it needs to be its own render item.
    auto reflectedSkullRitem = std::make_unique<RenderItem>();
    *reflectedSkullRitem = *skullRitem;
    reflectedSkullRitem->ObjCBIndex = 3;
    mReflectedSkullRitem = reflectedSkullRitem.get();
    mRitemLayer[(int)RenderLayer::Reflected].push_back(reflectedSkullRitem.get());

    // Shadowed skull will have different world matrix, so it needs to be its own render item.
    auto shadowedSkullRitem = std::make_unique<RenderItem>();
    *shadowedSkullRitem = *skullRitem;
    shadowedSkullRitem->ObjCBIndex = 4;
    shadowedSkullRitem->Mat = mMaterials["shadowMat"].get();
    mShadowedSkullRitem = shadowedSkullRitem.get();
    mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowedSkullRitem.get());

    auto mirrorRitem = std::make_unique<RenderItem>();
    mirrorRitem->World = MathHelper::Identity4x4();
    mirrorRitem->TexTransform = MathHelper::Identity4x4();
    mirrorRitem->ObjCBIndex = 5;
    mirrorRitem->Mat = mMaterials["icemirror"].get();
    mirrorRitem->Geo = mGeometries["roomGeo"].get();
    mirrorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    mirrorRitem->IndexCount = mirrorRitem->Geo->DrawArgs["mirror"].IndexCount;
    mirrorRitem->StartIndexLocation = mirrorRitem->Geo->DrawArgs["mirror"].StartIndexLocation;
    mirrorRitem->BaseVertexLocation = mirrorRitem->Geo->DrawArgs["mirror"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Mirrors].push_back(mirrorRitem.get());
    mRitemLayer[(int)RenderLayer::Transparent].push_back(mirrorRitem.get());

    mAllRitems.push_back(std::move(floorRitem));
    mAllRitems.push_back(std::move(wallsRitem));
    mAllRitems.push_back(std::move(skullRitem));
    mAllRitems.push_back(std::move(reflectedSkullRitem));
    mAllRitems.push_back(std::move(shadowedSkullRitem));
    mAllRitems.push_back(std::move(mirrorRitem));
}

void StencilApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();
    auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for (size_t i = 0; i < ritems.size(); ++i) {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

        cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> StencilApp::GetStaticSamplers()
{
    // Applications usually only need a handful of samplers.  So just define them all up front
    // and keep them available as part of the root signature.

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressW
        0.0f, // mipLODBias
        8); // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressW
        0.0f, // mipLODBias
        8); // maxAnisotropy

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp
    };
}

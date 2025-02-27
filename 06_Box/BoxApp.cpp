#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/d3dApp.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

struct Vertex {
    XMFLOAT3 Pos;
    XMFLOAT4 Color;
};

struct ObjectConstants { // 16个float 16*4=64字节
    XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
};

class BoxApp : public D3DApp {
public:
    BoxApp(HINSTANCE hInstance);
    BoxApp(const BoxApp& rhs) = delete;
    BoxApp& operator=(const BoxApp& rhs) = delete;
    ~BoxApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void BuildDescriptorHeaps();
    void BuildConstantBuffers();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildBoxGeometry();
    void BuildPSO();

private:
    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;

    std::unique_ptr<MeshGeometry> mBoxGeo = nullptr;

    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsByteCode = nullptr;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    ComPtr<ID3D12PipelineState> mPSO = nullptr;

    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    // 基于球坐标系的相机位置
    float mTheta = 1.5f * XM_PI; // 在xOz平面上，绕y轴的旋转角度，弧度
    float mPhi = XM_PIDIV4; // pi/4 与y轴正方向夹角
    float mRadius = 5.0f; // 到圆心距离

    POINT mLastMousePos;
};

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try {
        BoxApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        return theApp.Run();
    } catch (DxException& e) {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

BoxApp::BoxApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

BoxApp::~BoxApp() { }

bool BoxApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    BuildDescriptorHeaps();
    BuildConstantBuffers();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildBoxGeometry();
    BuildPSO();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}

void BoxApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection
    // matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(
        0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void BoxApp::Update(const GameTimer& gt)
{
    // 球坐标到笛卡尔坐标
    float x = mRadius * sinf(mPhi) * cosf(mTheta); // 初始值为r*sin pi/4 * cos 1.5 pi= 0
    float z = mRadius * sinf(mPhi) * sinf(mTheta); // 初始值为r*cos pi/4 * sin 1.5 pi= r * (0.707) * (-1)
    float y = mRadius * cosf(mPhi); // 初始值为r*cos pi/4 = r * 0.707

    // 更新观察矩阵  目标放在原点
    XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);

    XMMATRIX world = XMLoadFloat4x4(&mWorld);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX worldViewProj = world * view * proj;

    // Update the constant buffer with the latest worldViewProj matrix.
    ObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.WorldViewProj,
        XMMatrixTranspose(worldViewProj));
    mObjectCB->CopyData(0, objConstants);
}

void BoxApp::Draw(const GameTimer& gt)
{
    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution
    // on the GPU.
    ThrowIfFailed(mDirectCmdListAlloc->Reset());

    // 重置命令列表，重新关联命令分配器和管线状态对象
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport); // 设置视口
    mCommandList->RSSetScissorRects(1, &mScissorRect); // 设置裁剪矩形

    // Indicate a state transition on the resource usage.
    // 将后台缓冲区从呈现状态转换为渲染目标状态
    mCommandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET));

    // 设置后台缓冲区的颜色为蓝色
    mCommandList->ClearRenderTargetView(
        CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    // 设置深度缓冲区的值为1.0，模板缓冲区的值为0
    mCommandList->ClearDepthStencilView(DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f,
        0,
        0,
        nullptr);

    // 指定要渲染到的缓冲区
    mCommandList->OMSetRenderTargets(
        1, &CurrentBackBufferView(), true, &DepthStencilView());

    // 定义描述符堆数组
    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    // 设置命令列表使用的描述符堆
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // 设置根签名
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // 设置输入装配阶段的顶点缓冲区
    mCommandList->IASetVertexBuffers(0, 1, &mBoxGeo->VertexBufferView());
    // 设置输入装配阶段的索引缓冲区
    mCommandList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());
    // 设置图元拓扑类型
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 设置根描述符表
    mCommandList->SetGraphicsRootDescriptorTable(
        0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    // 执行绘制
    mCommandList->DrawIndexedInstanced(
        mBoxGeo->DrawArgs["box"].IndexCount, 1, 0, 0, 0);

    // 将后台缓冲区从渲染目标状态转换为呈现状态
    mCommandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT));

    // 完成命令列表的记录
    ThrowIfFailed(mCommandList->Close());

    // 将命令列表添加到命令队列中
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // 交换前后台缓冲区
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // 等待当前帧命令执行完成
    FlushCommandQueue();
}

void BoxApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    // 将鼠标输入捕获到指定的窗口。
    // 确保在鼠标按钮按下的过程中，即使鼠标指针移出了主窗口的客户区域，主窗口仍然能够接收到鼠标的移动和释放消息
    SetCapture(mhMainWnd);
}

void BoxApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0) { // 旋转  一个像素~0.25度
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        mTheta += dx;
        mPhi += dy;

        // 限制垂直方向，mPhi 在 0.1 弧度到 (π - 0.1) 弧度之间，避免相机视角出现异常
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    } else if ((btnState & MK_RBUTTON) != 0) {
        // Make each pixel correspond to 0.005 unit in the scene.
        float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

// 构建描述符堆
void BoxApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = 1; // 描述符数量
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; // 描述符堆的类型，包含 CBV, SRV ,UAV
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // 表示：其中的描述符可以被GPU访问，如不设置，则只能用于CPU操作
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(
        md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap))); // （用于常量缓冲区）
}

// 构建常量缓冲区
void BoxApp::BuildConstantBuffers()
{
    // 创建UploadBuffer
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);

    // 计算常量元素大小（256倍数）
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    // 获取常量缓冲区的GPU虚拟地址
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
    // 偏移到第一个对象常量缓冲区
    int boxCBufIndex = 0;
    cbAddress += boxCBufIndex * objCBByteSize;

    // 定义常量缓冲区视图 属性
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    // 创建常量缓冲区视图
    md3dDevice->CreateConstantBufferView(
        &cbvDesc, mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

// 创建根签名
// 着色器运行时需要访问资源，如常量缓冲区，纹理，采样器等，每种资源都需要一个对应的根参数来进行描述
// 1. 创建根参数数组（此处数组大小为1，因为只有一个常量缓冲区）
// 2. 创建常量缓冲区视图（CBV）的描述符范围，并将根参数初始化为指向该描述符范围的描述符表
// 3. 定义根签名属性结构体（由跟参数初始化）
// 4. 将上一步结构体描述的根签名信息序列化为二进制数据
// 5. 使用序列化后的根签名数据创建根签名对象
void BoxApp::BuildRootSignature()
{
    //  If we think of the shader programs as a function, and
    // the input resources as function parameters, then the root signature can be
    // thought of as defining the function signature.

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[1];

    // Create a single descriptor table of CBVs.
    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

    // A root signature is an array of root parameters.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        1, // 根参数的数量
        slotRootParameter, // 根参数数组指针
        0, // 静态采样器数量
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT); // 允许输入装配器使用输入布局

    // create a root signature with a single slot which points to a descriptor
    // range consisting of a single constant buffer
    // 用单个寄存器槽创建根签名，该槽指向一个仅含有单个常量缓冲区的视图区域
    ComPtr<ID3DBlob> serializedRootSig = nullptr; // 存储序列化后的根签名数据
    ComPtr<ID3DBlob> errorBlob = nullptr; // 存储错误信息

    // 将CD3DX12_ROOT_SIGNATURE_DESC结构体描述的根签名信息序列化为二进制数据
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf());

    if (errorBlob != nullptr) {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    // 使用序列化后的根签名数据创建根签名对象。
    ThrowIfFailed(
        md3dDevice->CreateRootSignature(0,
            serializedRootSig->GetBufferPointer(), // 序列化后的根签名数据指针
            serializedRootSig->GetBufferSize(),
            IID_PPV_ARGS(&mRootSignature))); // 将创建的根签名对象存储在这里
}

void BoxApp::BuildShadersAndInputLayout()
{
    HRESULT hr = S_OK;

    mvsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
    mpsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

    // 定义输入布局
    mInputLayout = { { "POSITION", // 顶点位置
                         0,
                         DXGI_FORMAT_R32G32B32_FLOAT,
                         0,
                         0,
                         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                         0 },
        { "COLOR",
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            0,
            12,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0 } };
}

void BoxApp::BuildBoxGeometry()
{

    std::array<Vertex, 8> vertices = {
        Vertex({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White) }),
        Vertex({ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black) }),
        Vertex({ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red) }),
        Vertex({ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green) }),
        Vertex({ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue) }),
        Vertex({ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow) }),
        Vertex({ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan) }),
        Vertex({ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta) })
    };

    std::array<std::uint16_t, 36> indices = { // front face
        0,
        1,
        2,
        0,
        2,
        3,

        // back face
        4,
        6,
        5,
        4,
        7,
        6,

        // left face
        4,
        5,
        1,
        4,
        1,
        0,

        // right face
        3,
        2,
        6,
        3,
        6,
        7,

        // top face
        1,
        5,
        6,
        1,
        6,
        2,

        // bottom face
        4,
        0,
        3,
        4,
        3,
        7
    };

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    // 创建MeshGeometry
    mBoxGeo = std::make_unique<MeshGeometry>();
    mBoxGeo->Name = "boxGeo";

    // 创建一个内存块用于存储顶点数据的 CPU 副本
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
    // 将顶点数据从 vertices 数组复制到 VertexBufferCPU 内存块中
    CopyMemory(
        mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU));
    CopyMemory(
        mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    // 在 GPU 上创建顶点缓冲区，并将顶点数据从 CPU 上传到 GPU
    mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), // 设备
        mCommandList.Get(), // 命令列表
        vertices.data(), // 顶点数据
        vbByteSize, // 数据大小
        mBoxGeo->VertexBufferUploader); // 上传缓冲区

    mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(),
        indices.data(),
        ibByteSize,
        mBoxGeo->IndexBufferUploader);

    mBoxGeo->VertexByteStride = sizeof(Vertex);
    mBoxGeo->VertexBufferByteSize = vbByteSize;
    mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
    mBoxGeo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    mBoxGeo->DrawArgs["box"] = submesh;
}

// 构建图形渲染管线状态对象
void BoxApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC)); // 将结构体的值初始化为0
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() }; // 输入布局，指定顶点数据的格式和布局
    psoDesc.pRootSignature = mRootSignature.Get(); // 根签名，指定着色器所需的资源
    psoDesc.VS = { reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize() }; // 顶点着色器
    psoDesc.PS = { reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
        mpsByteCode->GetBufferSize() }; // 像素着色器
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); // 光栅化状态，默认
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT); // 混合状态，默认
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // 深度模板状态，默认
    psoDesc.SampleMask = UINT_MAX; // 采样掩码，允许所有采样
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // 图元拓扑类型，三角形
    psoDesc.NumRenderTargets = 1; // 渲染目标数量
    psoDesc.RTVFormats[0] = mBackBufferFormat; // 渲染目标视图的格式
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1; // MSAA的数量、质量
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat; // 深度模板视图的格式
    ThrowIfFailed(
        md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO))); // 创建图形渲染管线状态对象
}
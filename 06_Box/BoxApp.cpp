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

struct ObjectConstants { // 16��float 16*4=64�ֽ�
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

    // ����������ϵ�����λ��
    float mTheta = 1.5f * XM_PI; // ��xOzƽ���ϣ���y�����ת�Ƕȣ�����
    float mPhi = XM_PIDIV4; // pi/4 ��y��������н�
    float mRadius = 5.0f; // ��Բ�ľ���

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
    // �����굽�ѿ�������
    float x = mRadius * sinf(mPhi) * cosf(mTheta); // ��ʼֵΪr*sin pi/4 * cos 1.5 pi= 0
    float z = mRadius * sinf(mPhi) * sinf(mTheta); // ��ʼֵΪr*cos pi/4 * sin 1.5 pi= r * (0.707) * (-1)
    float y = mRadius * cosf(mPhi); // ��ʼֵΪr*cos pi/4 = r * 0.707

    // ���¹۲����  Ŀ�����ԭ��
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

    // ���������б����¹�������������͹���״̬����
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport); // �����ӿ�
    mCommandList->RSSetScissorRects(1, &mScissorRect); // ���òü�����

    // Indicate a state transition on the resource usage.
    // ����̨�������ӳ���״̬ת��Ϊ��ȾĿ��״̬
    mCommandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET));

    // ���ú�̨����������ɫΪ��ɫ
    mCommandList->ClearRenderTargetView(
        CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    // ������Ȼ�������ֵΪ1.0��ģ�建������ֵΪ0
    mCommandList->ClearDepthStencilView(DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f,
        0,
        0,
        nullptr);

    // ָ��Ҫ��Ⱦ���Ļ�����
    mCommandList->OMSetRenderTargets(
        1, &CurrentBackBufferView(), true, &DepthStencilView());

    // ����������������
    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    // ���������б�ʹ�õ���������
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // ���ø�ǩ��
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // ��������װ��׶εĶ��㻺����
    mCommandList->IASetVertexBuffers(0, 1, &mBoxGeo->VertexBufferView());
    // ��������װ��׶ε�����������
    mCommandList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());
    // ����ͼԪ��������
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // ���ø���������
    mCommandList->SetGraphicsRootDescriptorTable(
        0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    // ִ�л���
    mCommandList->DrawIndexedInstanced(
        mBoxGeo->DrawArgs["box"].IndexCount, 1, 0, 0, 0);

    // ����̨����������ȾĿ��״̬ת��Ϊ����״̬
    mCommandList->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT));

    // ��������б�ļ�¼
    ThrowIfFailed(mCommandList->Close());

    // �������б���ӵ����������
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // ����ǰ��̨������
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // �ȴ���ǰ֡����ִ�����
    FlushCommandQueue();
}

void BoxApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    // ��������벶��ָ���Ĵ��ڡ�
    // ȷ������갴ť���µĹ����У���ʹ���ָ���Ƴ��������ڵĿͻ�������������Ȼ�ܹ����յ������ƶ����ͷ���Ϣ
    SetCapture(mhMainWnd);
}

void BoxApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0) { // ��ת  һ������~0.25��
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        mTheta += dx;
        mPhi += dy;

        // ���ƴ�ֱ����mPhi �� 0.1 ���ȵ� (�� - 0.1) ����֮�䣬��������ӽǳ����쳣
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

// ������������
void BoxApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = 1; // ����������
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; // �������ѵ����ͣ����� CBV, SRV ,UAV
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // ��ʾ�����е����������Ա�GPU���ʣ��粻���ã���ֻ������CPU����
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(
        md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap))); // �����ڳ�����������
}

// ��������������
void BoxApp::BuildConstantBuffers()
{
    // ����UploadBuffer
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);

    // ���㳣��Ԫ�ش�С��256������
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    // ��ȡ������������GPU�����ַ
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
    // ƫ�Ƶ���һ��������������
    int boxCBufIndex = 0;
    cbAddress += boxCBufIndex * objCBByteSize;

    // ���峣����������ͼ ����
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    // ����������������ͼ
    md3dDevice->CreateConstantBufferView(
        &cbvDesc, mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

// ������ǩ��
// ��ɫ������ʱ��Ҫ������Դ���糣���������������������ȣ�ÿ����Դ����Ҫһ����Ӧ�ĸ���������������
// 1. �������������飨�˴������СΪ1����Ϊֻ��һ��������������
// 2. ����������������ͼ��CBV������������Χ��������������ʼ��Ϊָ�����������Χ����������
// 3. �����ǩ�����Խṹ�壨�ɸ�������ʼ����
// 4. ����һ���ṹ�������ĸ�ǩ����Ϣ���л�Ϊ����������
// 5. ʹ�����л���ĸ�ǩ�����ݴ�����ǩ������
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
        1, // ������������
        slotRootParameter, // ����������ָ��
        0, // ��̬����������
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT); // ��������װ����ʹ�����벼��

    // create a root signature with a single slot which points to a descriptor
    // range consisting of a single constant buffer
    // �õ����Ĵ����۴�����ǩ�����ò�ָ��һ�������е�����������������ͼ����
    ComPtr<ID3DBlob> serializedRootSig = nullptr; // �洢���л���ĸ�ǩ������
    ComPtr<ID3DBlob> errorBlob = nullptr; // �洢������Ϣ

    // ��CD3DX12_ROOT_SIGNATURE_DESC�ṹ�������ĸ�ǩ����Ϣ���л�Ϊ����������
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf());

    if (errorBlob != nullptr) {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    // ʹ�����л���ĸ�ǩ�����ݴ�����ǩ������
    ThrowIfFailed(
        md3dDevice->CreateRootSignature(0,
            serializedRootSig->GetBufferPointer(), // ���л���ĸ�ǩ������ָ��
            serializedRootSig->GetBufferSize(),
            IID_PPV_ARGS(&mRootSignature))); // �������ĸ�ǩ������洢������
}

void BoxApp::BuildShadersAndInputLayout()
{
    HRESULT hr = S_OK;

    mvsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
    mpsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

    // �������벼��
    mInputLayout = { { "POSITION", // ����λ��
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

    // ����MeshGeometry
    mBoxGeo = std::make_unique<MeshGeometry>();
    mBoxGeo->Name = "boxGeo";

    // ����һ���ڴ�����ڴ洢�������ݵ� CPU ����
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
    // ���������ݴ� vertices ���鸴�Ƶ� VertexBufferCPU �ڴ����
    CopyMemory(
        mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU));
    CopyMemory(
        mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    // �� GPU �ϴ������㻺�����������������ݴ� CPU �ϴ��� GPU
    mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), // �豸
        mCommandList.Get(), // �����б�
        vertices.data(), // ��������
        vbByteSize, // ���ݴ�С
        mBoxGeo->VertexBufferUploader); // �ϴ�������

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

// ����ͼ����Ⱦ����״̬����
void BoxApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC)); // ���ṹ���ֵ��ʼ��Ϊ0
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() }; // ���벼�֣�ָ���������ݵĸ�ʽ�Ͳ���
    psoDesc.pRootSignature = mRootSignature.Get(); // ��ǩ����ָ����ɫ���������Դ
    psoDesc.VS = { reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize() }; // ������ɫ��
    psoDesc.PS = { reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
        mpsByteCode->GetBufferSize() }; // ������ɫ��
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); // ��դ��״̬��Ĭ��
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT); // ���״̬��Ĭ��
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // ���ģ��״̬��Ĭ��
    psoDesc.SampleMask = UINT_MAX; // �������룬�������в���
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // ͼԪ�������ͣ�������
    psoDesc.NumRenderTargets = 1; // ��ȾĿ������
    psoDesc.RTVFormats[0] = mBackBufferFormat; // ��ȾĿ����ͼ�ĸ�ʽ
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1; // MSAA������������
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat; // ���ģ����ͼ�ĸ�ʽ
    ThrowIfFailed(
        md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO))); // ����ͼ����Ⱦ����״̬����
}
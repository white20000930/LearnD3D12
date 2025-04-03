//***************************************************************************************
// d3dApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "d3dApp.h"
#include <WindowsX.h>

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Forward hwnd on because we can get messages (e.g., WM_CREATE)
    // before CreateWindow returns, and thus before mhMainWnd is valid.
    return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

D3DApp* D3DApp::mApp = nullptr;
D3DApp* D3DApp::GetApp()
{
    return mApp;
}

D3DApp::D3DApp(HINSTANCE hInstance)
    : mhAppInst(hInstance)
{
    // Only one D3DApp can be constructed.
    assert(mApp == nullptr);
    mApp = this;
}

D3DApp::~D3DApp()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();
}

HINSTANCE D3DApp::AppInst() const
{
    return mhAppInst;
}

HWND D3DApp::MainWnd() const
{
    return mhMainWnd;
}

float D3DApp::AspectRatio() const
{
    return static_cast<float>(mClientWidth) / mClientHeight;
}

bool D3DApp::Get4xMsaaState() const
{
    return m4xMsaaState;
}

void D3DApp::Set4xMsaaState(bool value)
{
    if (m4xMsaaState != value) {
        m4xMsaaState = value;

        // Recreate the swapchain and buffers with new multisample settings.
        CreateSwapChain();
        OnResize();
    }
}

int D3DApp::Run()
{
    MSG msg = { 0 };

    mTimer.Reset();

    while (msg.message != WM_QUIT) {
        // If there are Window messages then process them.
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        // Otherwise, do animation/game stuff.
        else {
            mTimer.Tick();

            if (!mAppPaused) {
                CalculateFrameStats();
                Update(mTimer); // 纯虚函数，由派生类实现
                Draw(mTimer); // 纯虚函数，由派生类实现
            } else {
                Sleep(100);
            }
        }
    }

    return (int)msg.wParam;
}

bool D3DApp::Initialize()
{
    if (!InitMainWindow())
        return false;

    if (!InitDirect3D())
        return false;

    // Do the initial resize code.
    OnResize();

    return true;
}

void D3DApp::CreateRtvAndDsvDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount; // view数量=交换链缓冲区数量
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // 视图类型：渲染目标视图
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // 无特殊标志
    rtvHeapDesc.NodeMask = 0; // 节点掩码
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1; // 深度/模板视图数量
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; // 视图类型：深度/模板视图
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void D3DApp::OnResize()
{
    assert(md3dDevice);
    assert(mSwapChain);
    assert(mDirectCmdListAlloc);

    // 在更改任何资源之前，刷新命令队列，确保 GPU 完成之前的操作
    FlushCommandQueue();

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // 释放之前的缓冲区数据
    for (int i = 0; i < SwapChainBufferCount; ++i)
        mSwapChainBuffer[i].Reset();
    mDepthStencilBuffer.Reset();

    // 调整交换链缓冲区大小
    ThrowIfFailed(mSwapChain->ResizeBuffers(
        SwapChainBufferCount,
        mClientWidth, mClientHeight,
        mBackBufferFormat,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    mCurrBackBuffer = 0;

    // 获取RTV堆的起始CPU句柄
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    // 为每个交换链缓冲区创建RTV
    // 在 DirectX 12 中，GPU 不能直接访问资源，而是通过视图来访问和操作资源。渲染目标视图允许 GPU 将渲染结果（如颜色、深度等信息）写入到交换链缓冲区或其他渲染目标资源中。
    for (UINT i = 0; i < SwapChainBufferCount; i++) {
        ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
        md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(1, mRtvDescriptorSize);
    }

    // 创建深度/模板缓冲区和视图
    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = mClientWidth;
    depthStencilDesc.Height = mClientHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;

    // Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from
    // the depth buffer.  Therefore, because we need to create two views to the same resource:
    //   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
    //   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
    // we need to create the depth buffer resource with a typeless format.
    depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

    depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = mDepthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;
    // 创建深度/模板缓冲区资源
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

    // Create descriptor to mip level 0 of entire resource using the format of the resource.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = mDepthStencilFormat;
    dsvDesc.Texture2D.MipSlice = 0;
    // 创建深度模板视图
    md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

    // 将资源从初始状态转换为用作深度缓冲区的状态
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

    // Execute the resize commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until resize is complete.
    FlushCommandQueue();

    // 更新视口变换，覆盖整个客户区
    mScreenViewport.TopLeftX = 0;
    mScreenViewport.TopLeftY = 0;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f;
    mScreenViewport.MaxDepth = 1.0f;

    // 更新裁剪矩形，覆盖整个客户区
    mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}

LRESULT D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        // WM_ACTIVATE is sent when the window is activated or deactivated.
        // We pause the game when the window is deactivated and unpause it
        // when it becomes active.
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            mAppPaused = true;
            mTimer.Stop();
        } else {
            mAppPaused = false;
            mTimer.Start();
        }
        return 0;

        // WM_SIZE is sent when the user resizes the window.
    case WM_SIZE:
        // Save the new client area dimensions.
        mClientWidth = LOWORD(lParam);
        mClientHeight = HIWORD(lParam);
        if (md3dDevice) {
            if (wParam == SIZE_MINIMIZED) {
                mAppPaused = true;
                mMinimized = true;
                mMaximized = false;
            } else if (wParam == SIZE_MAXIMIZED) {
                mAppPaused = false;
                mMinimized = false;
                mMaximized = true;
                OnResize();
            } else if (wParam == SIZE_RESTORED) {

                // Restoring from minimized state?
                if (mMinimized) {
                    mAppPaused = false;
                    mMinimized = false;
                    OnResize();
                }

                // Restoring from maximized state?
                else if (mMaximized) {
                    mAppPaused = false;
                    mMaximized = false;
                    OnResize();
                } else if (mResizing) {
                    // If user is dragging the resize bars, we do not resize
                    // the buffers here because as the user continuously
                    // drags the resize bars, a stream of WM_SIZE messages are
                    // sent to the window, and it would be pointless (and slow)
                    // to resize for each WM_SIZE message received from dragging
                    // the resize bars.  So instead, we reset after the user is
                    // done resizing the window and releases the resize bars, which
                    // sends a WM_EXITSIZEMOVE message.
                } else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
                {
                    OnResize();
                }
            }
        }
        return 0;

        // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
    case WM_ENTERSIZEMOVE:
        mAppPaused = true;
        mResizing = true;
        mTimer.Stop();
        return 0;

        // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
        // Here we reset everything based on the new window dimensions.
    case WM_EXITSIZEMOVE:
        mAppPaused = false;
        mResizing = false;
        mTimer.Start();
        OnResize();
        return 0;

        // WM_DESTROY is sent when the window is being destroyed.
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

        // The WM_MENUCHAR message is sent when a menu is active and the user presses
        // a key that does not correspond to any mnemonic or accelerator key.
    case WM_MENUCHAR:
        // Don't beep when we alt-enter.
        return MAKELRESULT(0, MNC_CLOSE);

        // Catch this message so to prevent the window from becoming too small.
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
        return 0;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_KEYDOWN:
        OnKeyboardInput(wParam);
        return 0;
    case WM_KEYUP:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
        } else if ((int)wParam == VK_F2)
            Set4xMsaaState(!m4xMsaaState);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool D3DApp::InitMainWindow()
{
    WNDCLASS wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = mhAppInst;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = L"MainWnd";

    if (!RegisterClass(&wc)) {
        MessageBox(0, L"RegisterClass Failed.", 0, 0);
        return false;
    }

    // Compute window rectangle dimensions based on requested client area dimensions.
    RECT R = { 0, 0, mClientWidth, mClientHeight };
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
    int width = R.right - R.left;
    int height = R.bottom - R.top;

    mhMainWnd = CreateWindow(L"MainWnd", mMainWndCaption.c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, mhAppInst, 0);
    if (!mhMainWnd) {
        MessageBox(0, L"CreateWindow Failed.", 0, 0);
        return false;
    }

    ShowWindow(mhMainWnd, SW_SHOW);
    UpdateWindow(mhMainWnd);

    return true;
}

bool D3DApp::InitDirect3D()
{
#if defined(DEBUG) || defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
        debugController->EnableDebugLayer(); // 启用调试层
    }
#endif

    // 创建dxgi工厂
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));

    // 创建硬件设备
    HRESULT hardwareResult = D3D12CreateDevice(
        nullptr, // default adapter
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&md3dDevice));

    // 如果创建失败，尝试使用 WARP 适配器
    if (FAILED(hardwareResult)) {
        ComPtr<IDXGIAdapter> pWarpAdapter;
        ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            pWarpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&md3dDevice)));
    }

    // 创建围栏对象
    ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&mFence)));

    // 获取RTV句柄增量大小
    mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    // 获取DSV句柄增量大小
    mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    // 获取CBV/SRV/UAV句柄增量大小
    mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Check 4X MSAA quality support for our back buffer format.
    // All Direct3D 11 capable devices support 4X MSAA for all render
    // target formats, so we only need to check quality support.

    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
    msQualityLevels.Format = mBackBufferFormat;
    msQualityLevels.SampleCount = 4;
    msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    msQualityLevels.NumQualityLevels = 0;
    ThrowIfFailed(md3dDevice->CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
        &msQualityLevels,
        sizeof(msQualityLevels)));
    // 设置MSAA质量级别
    m4xMsaaQuality = msQualityLevels.NumQualityLevels;
    assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

#ifdef _DEBUG
    LogAdapters();
#endif

    CreateCommandObjects(); // 创建命令相关对象
    CreateSwapChain(); // 创建交换链
    CreateRtvAndDsvDescriptorHeaps(); // 创建RTV和DSV堆

    return true;
}

void D3DApp::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

    // 调用 CreateCommandAllocator 时，DirectX 12 运行时会在系统内存或显存中预留一块内存区域，用于后续命令列表记录渲染命令时使用。
    ThrowIfFailed(md3dDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));

    ThrowIfFailed(md3dDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        mDirectCmdListAlloc.Get(), // 关联的命令分配器
        nullptr, // 管线状态对象
        IID_PPV_ARGS(mCommandList.GetAddressOf())));

    // 初始时将命令列表关闭，因为第一次使用命令列表时需要先重置它，而重置前需要确保它是关闭状态
    mCommandList->Close();
}

void D3DApp::CreateSwapChain()
{
    // 释放之前的交换链
    mSwapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd;
    sd.BufferDesc.Width = mClientWidth; // 缓冲区宽高
    sd.BufferDesc.Height = mClientHeight;
    sd.BufferDesc.RefreshRate.Numerator = 60; // 刷新率分子
    sd.BufferDesc.RefreshRate.Denominator = 1; // 刷新率分母
    sd.BufferDesc.Format = mBackBufferFormat; // 缓冲区格式
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED; // 扫描线顺序，未指定
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED; // 缩放模式，未指定
    sd.SampleDesc.Count = m4xMsaaState ? 4 : 1; // 采样时，根据是否启动MSAA指定
    sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0; // 采样质量
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 缓冲区用途，渲染目标输出
    sd.BufferCount = SwapChainBufferCount; // 缓冲区数量
    sd.OutputWindow = mhMainWnd; // 输出窗口
    sd.Windowed = true; // 窗口模式
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // 交换效果为翻转丢弃
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; // 允许模式切换

    // 交换链使用命令队列来执行刷新
    ThrowIfFailed(mdxgiFactory->CreateSwapChain(
        mCommandQueue.Get(),
        &sd,
        mSwapChain.GetAddressOf()));
}

// 用于同步 CPU 和 GPU 操作，通过增加围栏值并向命令队列设置新围栏点，检查 GPU 是否完成到该围栏点的命令
void D3DApp::FlushCommandQueue()
{
    mCurrentFence++; // 增加围栏值，标记到此围栏点的命令

    // 向命令队列添加一条指令，设置一个新的围栏点。
    // 由于我们处于 GPU 时间线，新的围栏点要等 GPU 处理完 Signal() 之前的所有命令才会设置
    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

    // 等待 GPU 完成到这个围栏点的命令
    if (mFence->GetCompletedValue() < mCurrentFence) {
        // 创建一个事件句柄，用于等待 GPU 完成指定围栏点的命令
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

        // 当 GPU 到达当前围栏点时触发事件
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

        // 等待直到 GPU 到达当前围栏点并触发事件
        WaitForSingleObject(eventHandle, INFINITE);

        // 关闭事件句柄，释放资源
        CloseHandle(eventHandle);
    }
}

ID3D12Resource* D3DApp::CurrentBackBuffer() const
{
    return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::CurrentBackBufferView() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE( // 当前后台缓冲区的 CPU 描述符句柄
        mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        mCurrBackBuffer,
        mRtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::DepthStencilView() const
{
    return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void D3DApp::CalculateFrameStats()
{
    // Code computes the average frames per second, and also the
    // average time it takes to render one frame.  These stats
    // are appended to the window caption bar.

    static int frameCnt = 0;
    static float timeElapsed = 0.0f;

    frameCnt++;

    // Compute averages over one second period.
    if ((mTimer.TotalTime() - timeElapsed) >= 1.0f) {
        float fps = (float)frameCnt; // fps = frameCnt / 1
        float mspf = 1000.0f / fps;

        wstring fpsStr = to_wstring(fps);
        wstring mspfStr = to_wstring(mspf);

        wstring windowText = mMainWndCaption + L"    fps: " + fpsStr + L"   mspf: " + mspfStr;

        SetWindowText(mhMainWnd, windowText.c_str());

        // Reset for next average.
        frameCnt = 0;
        timeElapsed += 1.0f;
    }
}

void D3DApp::LogAdapters()
{
    UINT i = 0;
    IDXGIAdapter* adapter = nullptr;
    std::vector<IDXGIAdapter*> adapterList;
    while (mdxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC desc;
        adapter->GetDesc(&desc);

        std::wstring text = L"***Adapter: ";
        text += desc.Description;
        text += L"\n";

        OutputDebugString(text.c_str());

        adapterList.push_back(adapter);

        ++i;
    }

    for (size_t i = 0; i < adapterList.size(); ++i) {
        LogAdapterOutputs(adapterList[i]);
        ReleaseCom(adapterList[i]);
    }
}

void D3DApp::LogAdapterOutputs(IDXGIAdapter* adapter)
{
    UINT i = 0;
    IDXGIOutput* output = nullptr;
    while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND) {
        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);

        std::wstring text = L"***Output: ";
        text += desc.DeviceName;
        text += L"\n";
        OutputDebugString(text.c_str());

        LogOutputDisplayModes(output, mBackBufferFormat);

        ReleaseCom(output);

        ++i;
    }
}

void D3DApp::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
{
    UINT count = 0;
    UINT flags = 0;

    // Call with nullptr to get list count.
    output->GetDisplayModeList(format, flags, &count, nullptr);

    std::vector<DXGI_MODE_DESC> modeList(count);
    output->GetDisplayModeList(format, flags, &count, &modeList[0]);

    for (auto& x : modeList) {
        UINT n = x.RefreshRate.Numerator;
        UINT d = x.RefreshRate.Denominator;
        std::wstring text = L"Width = " + std::to_wstring(x.Width) + L" " + L"Height = " + std::to_wstring(x.Height) + L" " + L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) + L"\n";

        ::OutputDebugString(text.c_str());
    }
}
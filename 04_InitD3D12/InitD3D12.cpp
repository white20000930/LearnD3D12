#include "../Common/d3dApp.h"
#include<DirectXColors.h> // 提供常用颜色

using namespace DirectX;

class InitD3D12App : public D3DApp
{
public:
    InitD3D12App(HINSTANCE hInstance);
    ~InitD3D12App();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;
};


int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd) {

#if defined(DEBUG)||defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try {
        InitD3D12App app(hInstance);
        if (!app.Initialize()) {
            return 0;
        }
        return app.Run();
    }
    catch (DxException& e) {
        MessageBox(nullptr, e.ToString().c_str(), L"HRFailed", MB_OK);
        return 0;
    }
}

InitD3D12App::InitD3D12App(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

InitD3D12App::~InitD3D12App()
{
}

bool InitD3D12App::Initialize()
{
    if (!D3DApp::Initialize())
        return false;
    return true;
}

void InitD3D12App::OnResize()
{
    D3DApp::OnResize();
}

void InitD3D12App::Update(const GameTimer& gt)
{
}

void InitD3D12App::Draw(const GameTimer& gt)
{
    ThrowIfFailed(mDirectCmdListAlloc->Reset());

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    ////将后台缓冲区从呈现状态转换为渲染目标状态
    //ResourceBarrier(执行状态转换的数量，状态转换数组指针)
    //CD3DX12_RESOURCE_BARRIER::Transition(当前后台缓冲区的指针，资源当前状态，资源目标状态)
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    //设置视口和裁剪矩形
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    //清除后台缓冲区和深度缓冲区，实际不是清楚，只使用一个值覆盖，类似于擦黑板
    //参数 1：后台缓冲区视图句柄 2：颜色 3：无作用 4：nullptr 表示清除整个渲染目标视图
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    //参数  1：DSV句柄 2：同时清除深度和模板缓冲区 3：深度值 
    //      4：模板值 5：0无作用 6：nullptr 表示清除整个深度缓冲区
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    //指定将要渲染的缓冲区
    // 1：pRenderTargetDescriptors 数组中的条目数
    // 2: 后台缓冲区视图句柄
    // 3：true:所有RTV对象在描述符堆中都是连续存放的
    // 4：DSV句柄
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    //将后台缓冲区从渲染目标状态转换为呈现状态
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    //结束命令记录
    ThrowIfFailed(mCommandList->Close());

    //将命令从命令列表传递到命令队列
    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    //交换前后台缓冲区
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}

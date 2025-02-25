#include "../Common/d3dApp.h"
#include<DirectXColors.h> // �ṩ������ɫ

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

    ////����̨�������ӳ���״̬ת��Ϊ��ȾĿ��״̬
    //ResourceBarrier(ִ��״̬ת����������״̬ת������ָ��)
    //CD3DX12_RESOURCE_BARRIER::Transition(��ǰ��̨��������ָ�룬��Դ��ǰ״̬����ԴĿ��״̬)
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    //�����ӿںͲü�����
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    //�����̨����������Ȼ�������ʵ�ʲ��������ֻʹ��һ��ֵ���ǣ������ڲ��ڰ�
    //���� 1����̨��������ͼ��� 2����ɫ 3�������� 4��nullptr ��ʾ���������ȾĿ����ͼ
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    //����  1��DSV��� 2��ͬʱ�����Ⱥ�ģ�建���� 3�����ֵ 
    //      4��ģ��ֵ 5��0������ 6��nullptr ��ʾ���������Ȼ�����
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    //ָ����Ҫ��Ⱦ�Ļ�����
    // 1��pRenderTargetDescriptors �����е���Ŀ��
    // 2: ��̨��������ͼ���
    // 3��true:����RTV���������������ж���������ŵ�
    // 4��DSV���
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    //����̨����������ȾĿ��״̬ת��Ϊ����״̬
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    //���������¼
    ThrowIfFailed(mCommandList->Close());

    //������������б��ݵ��������
    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    //����ǰ��̨������
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}

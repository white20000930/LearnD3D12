#pragma once

#include "d3dUtil.h"

// 用于将数据从CPU上传到GPU
template <typename T>
class UploadBuffer {
public:
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer)
        : mIsConstantBuffer(isConstantBuffer)
    {
        mElementByteSize = sizeof(T); // 只基于数据类型计算大小

        // 常量缓冲区元素大小需为 256 字节的倍数  （除了常量缓冲区外，其他缓冲区 如顶点缓冲区 不需要）
        if (isConstantBuffer)
            mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T)); // 将数据类型大小调整为256字节的倍数

        // 创建一个资源和一个隐式堆，以便堆足够大，足以包含整个资源，并将资源映射到堆。
        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // 表示是：上传堆
            D3D12_HEAP_FLAG_NONE, // 无特殊标志
            &CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount), // 缓冲区大小
            D3D12_RESOURCE_STATE_GENERIC_READ, // 表示资源可被GPU读取
            nullptr,
            IID_PPV_ARGS(&mUploadBuffer))); // 将创建的资源存储在mUploadBuffer中

        // 将资源映射到 CPU 可访问的内存
        ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));

        // 需注意CPU和GPU的同步（GPU在使用该资源时，CPU不能写入）
    }

    // 禁用拷贝构造函数、复制运算符
    UploadBuffer(const UploadBuffer& rhs) = delete;
    UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
    ~UploadBuffer()
    {
        if (mUploadBuffer != nullptr)
            mUploadBuffer->Unmap(0, nullptr); // 取消映射

        mMappedData = nullptr;
    }

    // 获取上传缓冲区资源
    ID3D12Resource* Resource() const
    {
        return mUploadBuffer.Get();
    }

    // 将CPU数据复制到缓冲区
    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer; // 要上传到的缓冲区资源
    BYTE* mMappedData = nullptr; // 映射后的CPU内存指针

    UINT mElementByteSize = 0;
    bool mIsConstantBuffer = false;
};
#pragma once

#include "d3dUtil.h"

// ���ڽ����ݴ�CPU�ϴ���GPU
template <typename T>
class UploadBuffer {
public:
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer)
        : mIsConstantBuffer(isConstantBuffer)
    {
        mElementByteSize = sizeof(T); // ֻ�����������ͼ����С

        // ����������Ԫ�ش�С��Ϊ 256 �ֽڵı���  �����˳����������⣬���������� �綥�㻺���� ����Ҫ��
        if (isConstantBuffer)
            mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T)); // ���������ʹ�С����Ϊ256�ֽڵı���

        // ����һ����Դ��һ����ʽ�ѣ��Ա���㹻�����԰���������Դ��������Դӳ�䵽�ѡ�
        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), // ��ʾ�ǣ��ϴ���
            D3D12_HEAP_FLAG_NONE, // �������־
            &CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount), // ��������С
            D3D12_RESOURCE_STATE_GENERIC_READ, // ��ʾ��Դ�ɱ�GPU��ȡ
            nullptr,
            IID_PPV_ARGS(&mUploadBuffer))); // ����������Դ�洢��mUploadBuffer��

        // ����Դӳ�䵽 CPU �ɷ��ʵ��ڴ�
        ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));

        // ��ע��CPU��GPU��ͬ����GPU��ʹ�ø���Դʱ��CPU����д�룩
    }

    // ���ÿ������캯�������������
    UploadBuffer(const UploadBuffer& rhs) = delete;
    UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
    ~UploadBuffer()
    {
        if (mUploadBuffer != nullptr)
            mUploadBuffer->Unmap(0, nullptr); // ȡ��ӳ��

        mMappedData = nullptr;
    }

    // ��ȡ�ϴ���������Դ
    ID3D12Resource* Resource() const
    {
        return mUploadBuffer.Get();
    }

    // ��CPU���ݸ��Ƶ�������
    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer; // Ҫ�ϴ����Ļ�������Դ
    BYTE* mMappedData = nullptr; // ӳ����CPU�ڴ�ָ��

    UINT mElementByteSize = 0;
    bool mIsConstantBuffer = false;
};
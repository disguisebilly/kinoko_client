#pragma once
#include "zlock.h"
#include <windows.h>
#include <cstdint>
#include <memory>

#define ZALLOC_GLOBAL \
    void* operator new(size_t uSize) { return ZAllocEx<ZAllocAnonSelector>::s_Alloc(uSize); } \
    void* operator new[](size_t uSize) { return ZAllocEx<ZAllocAnonSelector>::s_Alloc(uSize); } \
    void operator delete(void* p) { return ZAllocEx<ZAllocAnonSelector>::s_Free(p); } \
    void operator delete[](void* p) { return ZAllocEx<ZAllocAnonSelector>::s_Free(p); }

#define ZALLOCEX(T, ADDRESS) \
    ZAllocEx<T>* ZAllocEx<T>::_s_pAlloc = reinterpret_cast<ZAllocEx<T>*>(ADDRESS);

#define ZRECYCLABLE(T, ADDRESS) \
    ZRecyclableAvBuffer<ZRefCountedDummy<T>, 0x10, T>*& ZRecyclableAvBuffer<ZRefCountedDummy<T>, 0x10, T>::s_pInstance = *reinterpret_cast<ZRecyclableAvBuffer<ZRefCountedDummy<T>, 0x10, T>**>(ADDRESS);


template <int32_t S0, int32_t S1, int32_t S2, int32_t S3>
class ZAllocAbstractSelector {
protected:
    static int32_t SelectBufferIndex(uint32_t uSize) {
        if (uSize > S1) {
            if (uSize <= S2) {
                return 2;
            } else {
                return uSize > S3 ? -1 : 3;
            }
        } else {
            return uSize > S0 ? 1 : 0;
        }
    }
    static uint32_t GetBlockSize(int32_t nIndex, int32_t& nAllocBlocks) {
        switch (nIndex) {
        case 0:
            nAllocBlocks = 64;
            return S0;
        case 1:
            nAllocBlocks = 32;
            return S1;
        case 2:
            nAllocBlocks = 16;
            return S2;
        case 3:
            nAllocBlocks = 8;
            return S3;
        default:
            nAllocBlocks = 0;
            return 0;
        }
    }
};

class ZAllocAnonSelector : protected ZAllocAbstractSelector<0x10, 0x20, 0x40, 0x80> {
};

template <typename T>
class ZAllocStrSelector;

template <>
class ZAllocStrSelector<char> : protected ZAllocAbstractSelector<0x1D, 0x2D, 0x4D, 0x8D> {
};

template <>
class ZAllocStrSelector<wchar_t> : protected ZAllocAbstractSelector<0x2E, 0x4E, 0x8E, 0x10E> {
};


class ZAllocHelper {
public:
    explicit ZAllocHelper(int32_t nDummy) {
        ;
    }
};


class ZAllocBase {
public:
    static void* _AllocRaw(uint32_t uSize) {
        HANDLE hHeap = GetProcessHeap();
        auto pAlloc = static_cast<uint32_t*>(HeapAlloc(hHeap, 0, uSize + sizeof(uint32_t)));
        *pAlloc = uSize;
        return pAlloc + 1;
    }
    static void _FreeRaw(void* p) {
        HANDLE hHeap = GetProcessHeap();
        HeapFree(hHeap, 0, static_cast<uint32_t*>(p) - 1);
    }
    static void* _AllocRawBlocks(uint32_t uBlockSize, uint32_t u) {
        uint32_t uAllocSize = uBlockSize + sizeof(uint32_t);
        auto pAlloc = static_cast<uint32_t*>(_AllocRaw(u * uAllocSize + sizeof(uint32_t)));
        *pAlloc++ = 0;
        *pAlloc = uBlockSize;
        uint32_t* pBlock = pAlloc + 1;
        for (uint32_t nRemain = u - 1; nRemain > 0; --nRemain) {
            *pBlock = reinterpret_cast<uintptr_t>(pBlock) + uAllocSize;
            pBlock = reinterpret_cast<uint32_t*>(*pBlock);
            *(pBlock - 1) = uBlockSize;
        }
        *pBlock = 0;
        return pAlloc + 1;
    }
    static void _FreeRawBlocks(void* p) {
        if (!p) {
            return;
        }
        _FreeRaw(static_cast<uint32_t*>(p) - 2);
    }
    static uint32_t _MemSize(void* p) {
        uint32_t uSize = *(static_cast<uint32_t*>(p) - 1);
        if (uSize & 0x80000000) {
            uSize = ~uSize;
        }
        return uSize;
    }
    static void*& _NextHeadBlock(void* p) {
        return *reinterpret_cast<void**>(static_cast<uint32_t*>(p) - 2);
    }
};

template <typename T>
class ZAllocEx : private ZAllocBase, private T {
private:
    ZFatalSection m_lock;
    void* m_apBuff[4] = {};
    void* m_apBlockHead[4] = {};
    static ZAllocEx<T>* _s_pAlloc; // changed from _s_alloc to allow using the ZAllocEx<T> instance in client

public:
    ~ZAllocEx() {
        for (auto i = 0; i < 4; ++i) {
            void* pNext = m_apBlockHead[i];
            while (pNext) {
                void* pFree = pNext;
                pNext = _NextHeadBlock(pNext);
                _FreeRawBlocks(pFree);
            }
        }
    }
    void* Alloc(uint32_t uSize) {
        int32_t nIndex = T::SelectBufferIndex(uSize);
        if (nIndex < 0) {
            return _AllocRaw(uSize);
        }
        int32_t nAllocBlocks;
        uint32_t uBlockSize = T::GetBlockSize(nIndex, nAllocBlocks);
        ZSynchronizedHelper<ZFatalSection> _sync(m_lock);
        if (!m_apBuff[nIndex]) {
            void* pBlock = _AllocRawBlocks(uBlockSize, nAllocBlocks);
            *reinterpret_cast<void**>(static_cast<uint32_t*>(pBlock) - 2) = m_apBlockHead[nIndex];
            m_apBlockHead[nIndex] = pBlock;
            m_apBuff[nIndex] = pBlock;
        }
        void* pBuffer = m_apBuff[nIndex];
        m_apBuff[nIndex] = *static_cast<void**>(pBuffer);
        return pBuffer;
    }
    void Free(void* p) {
        if (!p) {
            return;
        }
        int32_t nIndex = T::SelectBufferIndex(_MemSize(p));
        if (nIndex < 0) {
            _FreeRaw(p);
            return;
        }
        ZSynchronizedHelper<ZFatalSection> _sync(m_lock);
        *reinterpret_cast<void**>(p) = m_apBuff[nIndex];
        m_apBuff[nIndex] = p;
    }
    static void* s_Alloc(uint32_t uSize) {
        return _s_pAlloc->Alloc(uSize);
    }
    static void s_Free(void* p) {
        _s_pAlloc->Free(p);
    }
};
static_assert(sizeof(ZAllocEx<ZAllocAnonSelector>) == 0x2C);


class ZRefCounted {
public: // changed from private to simplify implementation
    union {
        volatile long _m_nRef;
        ZRefCounted* _m_pNext;
    };
    ZRefCounted* _m_pPrev;

    virtual ~ZRefCounted() = default;
};
static_assert(sizeof(ZRefCounted) == 0xC);


class ZRecyclableStatic {
public:
    class CallBack {
    public:
        CallBack* m_pNext;

        CallBack() {
            m_pNext = ZRecyclableStatic::_s_pStatic->m_pHead;
        }
        virtual ~CallBack() = 0;
    };
    static_assert(sizeof(CallBack) == 0x8);

private:
    CallBack* m_pHead;
    static ZRecyclableStatic* _s_pStatic; // changed from _s_sStatic

public:
    ~ZRecyclableStatic() {
        CallBack* pHead = m_pHead;
        while (pHead) {
            auto p = pHead;
            pHead = p->m_pNext;
            delete p;
        }
    }
};
static_assert(sizeof(ZRecyclableStatic) == 0x4);


class ZFakeStatAvBuff {
};


template <typename T, size_t SIZE, typename U>
class ZRecyclableAvBuffer : public ZRecyclableStatic::CallBack {
public:
    ZFakeStatAvBuff* m_pStat;
    ZFatalSection m_l;
    void* m_pAv;
    static ZRecyclableAvBuffer<T, SIZE, U>*& s_pInstance;

    void* raw_new() {
        ZSynchronizedHelper<ZFatalSection> _sync(&m_l);
        if (m_pAv == nullptr) {
            m_pAv = ZAllocBase::_AllocRawBlocks(sizeof(T), SIZE);
        }
        auto result = static_cast<void**>(m_pAv);
        m_pAv = *result;
        return result;
    }
    void raw_delete(void* p) {
        ZSynchronizedHelper<ZFatalSection> _sync(&m_l);
        *static_cast<void**>(p) = m_pAv;
        m_pAv = p;
    }
};
static_assert(sizeof(ZRecyclableAvBuffer<void, 16, void>) == 0x18);


template <typename T, size_t SIZE, typename U>
class ZRecyclable : public ZAllocBase {
protected:
    virtual ~ZRecyclable() = default;

public:
    void* operator new(uint32_t uSize) {
        return ZRecyclableAvBuffer<T, SIZE, U>::s_pInstance->raw_new();
    }
    void* operator new[](uint32_t uSize) {
        return ZRecyclableAvBuffer<T, SIZE, U>::s_pInstance->raw_new();
    }
    void operator delete(void* p) noexcept {
        ZRecyclableAvBuffer<T, SIZE, U>::s_pInstance->raw_delete(static_cast<void**>(p));
    }
    void operator delete[](void* p) noexcept {
        ZRecyclableAvBuffer<T, SIZE, U>::s_pInstance->raw_delete(static_cast<void**>(p));
    }
};


template <typename T>
class ZRefCountedDummy : public ZRefCounted, public ZRecyclable<ZRefCountedDummy<T>, 0x10, T> {
public:
    T t; // changed from private to simplify implemnentation

    virtual ~ZRefCountedDummy() override = default;

    static ZRefCountedDummy<T>* From(void* p) {
        return reinterpret_cast<ZRefCountedDummy<T>*>(reinterpret_cast<uintptr_t>(p) - sizeof(ZRefCounted) - sizeof(ZRecyclable<ZRefCountedDummy<T>, 0x10, T>));
    }
};
static_assert(sizeof(ZRecyclable<ZRefCountedDummy<int>, 0x10, int>) == 0x4);
static_assert(sizeof(ZRefCountedDummy<int>) == 0x10 + sizeof(int));


class ZRefCountedAccessorBase {
};

template <typename T>
class ZRefCountedAccessor : public ZRefCountedAccessorBase {
};

template <typename T>
class ZRef : private ZRefCountedAccessor<T>, private ZRefCountedAccessor<ZRefCountedDummy<T>> {
private:
    T* p;

public:
    ~ZRef() {
        _Release();
    }
    explicit ZRef(const ZAllocHelper& _ALLOC) : p(nullptr) {
        _Alloc();
    }
    ZRef(T* pT, int32_t bAddRef = true) : p(pT) {
        if (bAddRef) {
            _AddRef();
        }
    }
    ZRef(const ZRef<T>& r, int32_t bAddRef = true) : p(r.p) {
        if (bAddRef) {
            _AddRef();
        }
    }
    ZRef() : p(nullptr) {
    }
    operator bool() const {
        return p != nullptr;
    }
    int32_t operator!() const {
        return p == nullptr;
    }
    operator T*() const {
        return p;
    }
    T* operator->() const {
        return p;
    }

    ZRef<T>& operator=(const ZRef<T>& r) {
        if (p == r.p) {
            return *this;
        }
        _Release();
        if (r.p) {
            p = r.p;
            _AddRef();
        }
        return *this;
    }
    ZRef<T>& operator=(T* pT) {
        _Release();
        p = pT;
        _AddRef();
        return *this;
    }

private:
    void _Alloc() {
        _Release();
        p = _AllocRaw(p);
    }
    void _AddRef() {
        if (p){
            _AddRefRaw(p);
        }
    }
    void _AddRefRaw(void* __formal) {
        auto pDummy = ZRefCountedDummy<T>::From(p);
        InterlockedIncrement(&pDummy->_m_nRef);
    }
    void _AddRefRaw(ZRefCounted* __formal) {
        InterlockedIncrement(&p->_m_nRef);
    }
    void _Release() {
        if (p) {
            _ReleaseRaw(p);
            p = nullptr;
        }
    }
    void _ReleaseRaw(void* __formal) {
        auto pDummy = ZRefCountedDummy<T>::From(p);
        if (!InterlockedDecrement(&pDummy->_m_nRef)) {
            InterlockedIncrement(&pDummy->_m_nRef);
            delete pDummy;
        }
    }
    void _ReleaseRaw(ZRefCounted* __formal) {
        if (!InterlockedDecrement(&p->_m_nRef)) {
            InterlockedIncrement(&p->_m_nRef);
            delete p;
        }
    }

    static T* _AllocRaw(void* __formal) {
        auto pDummy = new ZRefCountedDummy<T>();
        pDummy->_m_nRef = 1;
        return std::addressof(pDummy->t);
    }
    static T* _AllocRaw(ZRefCounted* __formal) {
        auto p = new T();
        p->_m_nRef = 1;
        return p;
    }
};

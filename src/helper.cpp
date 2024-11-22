#include "pch.h"
#include "client.h"
#include "hook.h"
#include "debug.h"


static uintptr_t CDropPool__TryPickUpDropByPet_jmp = 0x005133AD;
static uintptr_t CDropPool__TryPickUpDropByPet_ret = 0x005133BC;

void __declspec(naked) CDropPool__TryPickUpDropByPet_hook() {
    __asm {
        lea     edx, [ eax - 200 ]      ; x1
        mov     [ ebp - 0x34 ], edx
        lea     edx, [ ecx - 200 ]      ; y1
        add     eax, 200                ; x2
        add     ecx, 200                ; y2
        jmp     [ CDropPool__TryPickUpDropByPet_ret ]
    };
}


typedef void (__thiscall* CCurseProcess__ProcessString_t)(void*, char*, int*, int);
static auto CCurseProcess__ProcessString = reinterpret_cast<CCurseProcess__ProcessString_t>(0x00753C50);

int __fastcall CCurseProcess__ProcessString_hook(void* pThis, void* _EDX, char* sString, int* pbFiltered, int bIgnoreNewLine) {
    return 1;
}


typedef void (__thiscall* CInputSystem__DetectJoystick_t)(CInputSystem*);
static auto CInputSystem__DetectJoystick = reinterpret_cast<CInputSystem__DetectJoystick_t>(0x00573A70);

void __fastcall CInputSystem__DetectJoystick_hook(void* pThis, void* _EDX) {
    // noop
}


class CVecCtrl;

typedef void (__thiscall* CVecCtrl__SetImpactNext_t)(CVecCtrl*, int, long double, long double);
static auto CVecCtrl__SetImpactNext = reinterpret_cast<CVecCtrl__SetImpactNext_t>(0x00924D20);

void __fastcall CVecCtrl__SetImpactNext_hook(CVecCtrl* pThis, void* _EDX, int nAttr, long double vx, long double vy) {
    if (nAttr == 0x14 && CInputSystem::GetInstance()->IsKeyPressed(38)) {
        // MPA_FLASHJUMP + ArrowUp
        vx = 0.0;
        vy = -1000.0;
    }
    CVecCtrl__SetImpactNext(pThis, nAttr, vx, vy);
}


typedef void (__thiscall* CUserLocal__Jump_t)(CUserLocal*, int);
static auto CUserLocal__Jump = reinterpret_cast<CUserLocal__Jump_t>(0x009293B0);

class CVecCtrl {
public:
    MEMBER_AT(void*, 0x1A0, m_pfh)
};

void __fastcall CUserLocal__Jump_hook(CUserLocal* pThis, void* _EDX, int bEnforced) {
    CUserLocal__Jump(pThis, bEnforced);
    // Check if on the ground (has foothold), or on ladder/rope (CVecCtrl::GetLadderOrRope)
    CVecCtrl* pVecCtrl = reinterpret_cast<CVecCtrl*>(&static_cast<IWzVector2D*>(pThis->m_pvc())[-3]);
    if (pVecCtrl->m_pfh() || reinterpret_cast<void* (__thiscall*)(CVecCtrl*)>(0x004BCC30)(pVecCtrl)) {
        return;
    }
    // Trigger flash jump skill
    if (!bEnforced && pThis->m_bJumpKeyUp()) {
        int nJob = pThis->GetJobCode();
        int nSkillID = 0;
        if (nJob / 10 == 41) {
            nSkillID = 4111006; // Night Lord
        } else if (nJob / 10 == 42) {
            nSkillID = 4211009; // Shadower
        } else if (nJob / 10 == 43) {
            nSkillID = 4321003; // Dual Blade
        } else if (nJob / 100 == 14) {
            nSkillID = 14101004; // Night Walker
        }
        if (nSkillID) {
            pThis->m_bJumpKeyUp() = false;
            pThis->DoActiveSkill(nSkillID, 0, nullptr);
        }
    }
}


typedef ZXString<char>* (__thiscall* CItemInfo__GetMapString_t)(CItemInfo*, ZXString<char>*, unsigned int, const char*);
static auto CItemInfo__GetMapString = reinterpret_cast<CItemInfo__GetMapString_t>(0x005A9CA0);

ZXString<char>* __fastcall CItemInfo__GetMapString_hook(CItemInfo* pThis, void* _EDX,  ZXString<char>* result, unsigned int dwFieldID, const char* sKey) {
    CItemInfo__GetMapString(pThis, result, dwFieldID, sKey);
    if (!strcmp(sKey, "mapName")) {
        char sFieldID[20];
        snprintf(sFieldID, 20, " (%d)", dwFieldID);
        // ZXString<char>::_Cat(result, sFieldID, strlen(sFieldID));
        reinterpret_cast<ZXString<char>* (__thiscall*)(ZXString<char>*, char*, int)>(0x0042D020)(result, sFieldID, strlen(sFieldID));
    }
    return result;
}


typedef ZXString<char>* (__thiscall* CItemInfo__GetItemDesc_t)(CItemInfo*, ZXString<char>*, int);
static auto CItemInfo__GetItemDesc = reinterpret_cast<CItemInfo__GetItemDesc_t>(0x005B16E0);

ZXString<char>* __fastcall CItemInfo__GetItemDesc_hook(CItemInfo* pThis, void* _EDX, ZXString<char>* result, int nItemID) {
    CItemInfo__GetItemDesc(pThis, result, nItemID);
    if (result->GetLength() > 0) {
        char* sNewLine = "\r\n";
        // ZXString<char>::_Cat(result, sNewLine, strlen(sNewLine));
        reinterpret_cast<ZXString<char>* (__thiscall*)(ZXString<char>*, char*, int)>(0x0042D020)(result, sNewLine, strlen(sNewLine));
    }
    char sQuestID[30];
    snprintf(sQuestID, 30, "#cItem ID : %d#", nItemID);
    // ZXString<char>::_Cat(result, sQuestID, strlen(sQuestID));
    reinterpret_cast<ZXString<char>* (__thiscall*)(ZXString<char>*, char*, int)>(0x0042D020)(result, sQuestID, strlen(sQuestID));
    return result;
}


class CSkillInfo;

struct SKILLENTRY {
    unsigned char padding[0x264];

    MEMBER_AT(int, 0x0, nSkillID)
    MEMBER_AT(ZXString<char>, 0x4, sName)
    MEMBER_AT(ZXString<char>, 0x8, sDescription)
};

typedef ZRef<SKILLENTRY>* (__thiscall* CSkillInfo__LoadSkill_t)(CSkillInfo*, ZRef<SKILLENTRY>*, int, void*, void*);
static auto CSkillInfo__LoadSkill = reinterpret_cast<CSkillInfo__LoadSkill_t>(0x0070C190);

ZRef<SKILLENTRY>* __fastcall CSkillInfo__LoadSkill_hook(CSkillInfo* pThis, void* _EDX, ZRef<SKILLENTRY>* result, int nSkillID, void* pSkill, void* pStrSR) {
    CSkillInfo__LoadSkill(pThis, result, nSkillID, pSkill, pStrSR);
    if (result->p->sDescription().GetLength() > 0) {
        char* sNewLine = "\r\n";
        // ZXString<char>::_Cat(&result->p->sDescription, sNewLine, strlen(sNewLine));
        reinterpret_cast<ZXString<char>* (__thiscall*)(ZXString<char>*, char*, int)>(0x0042D020)(&result->p->sDescription(), sNewLine, strlen(sNewLine));
    }
    char sSkillID[30];
    snprintf(sSkillID, 30, "#cSkill ID : %d#", nSkillID);
    // ZXString<char>::_Cat(&result->p->sDescription, sSkillID, strlen(sSkillID));
    reinterpret_cast<ZXString<char>* (__thiscall*)(ZXString<char>*, char*, int)>(0x0042D020)(&result->p->sDescription(), sSkillID, strlen(sSkillID));
    return result;
}


static uintptr_t CUIQuestInfoDetail__Draw_jmp = 0x00824A93;
static uintptr_t CUIQuestInfoDetail__Draw_ret = 0x00824C04;

void __stdcall CUIQuestInfoDetail__Draw_helper(IWzCanvas* pCanvas, IWzFont* pFont, unsigned short usQuestID) {
    wchar_t sQuestID[30];
    swprintf(sQuestID, 30, L"Quest ID : %d", usQuestID);
    Ztl_variant_t vEmpty;
    unsigned int result[4];
    CHECK_HRESULT(pCanvas->raw_DrawText(35, 56, sQuestID, pFont, vEmpty, vEmpty, result));
}

void __declspec(naked) CUIQuestInfoDetail__Draw_hook() {
    __asm {
        mov     eax, [ ebp + 0x8C ]
        movzx   edx, word ptr [ eax ]
        push    edx ; this->m_pQI.p->usQuestID
        mov     eax, [ ebp + 0xF0 ]
        push    eax ; IWzFont*
        push    esi ; IWzCanvas*
        call    CUIQuestInfoDetail__Draw_helper
        jmp     [ CUIQuestInfoDetail__Draw_ret ]
    };
}


void AttachClientHelper() {
    // CChatHelper::TryChat
    Patch1(0x004AB5A7, 0xEB); // bypass chat cooldown
    Patch1(0x004AB502, 0xEB); // bypass chat repeat

    // CVecCtrl::IsFalling
    PatchRetZero(0x0095A6C0); // double jump while falling

    // CDropPool::TryPickUpDropByPet
    PatchJmp(CDropPool__TryPickUpDropByPet_jmp, reinterpret_cast<uintptr_t>(&CDropPool__TryPickUpDropByPet_hook)); // increase pet pickup range

    ATTACH_HOOK(CCurseProcess__ProcessString, CCurseProcess__ProcessString_hook); // disable profanity filter
    ATTACH_HOOK(CInputSystem__DetectJoystick, CInputSystem__DetectJoystick_hook); // fix stutter
    ATTACH_HOOK(CVecCtrl__SetImpactNext, CVecCtrl__SetImpactNext_hook); // vertical double jump
    ATTACH_HOOK(CUserLocal__Jump, CUserLocal__Jump_hook); // double jump with jump key

#ifdef _DEBUG
    // // Add ID to map name, item description, skill description, quest info
    // ATTACH_HOOK(CItemInfo__GetMapString, CItemInfo__GetMapString_hook);
    // ATTACH_HOOK(CItemInfo__GetItemDesc, CItemInfo__GetItemDesc_hook);
    // ATTACH_HOOK(CSkillInfo__LoadSkill, CSkillInfo__LoadSkill_hook);
    // PatchJmp(CUIQuestInfoDetail__Draw_jmp, reinterpret_cast<uintptr_t>(&CUIQuestInfoDetail__Draw_hook)); // replace "Low Level Quest"
#endif
}
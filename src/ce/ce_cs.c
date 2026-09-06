/* Run-time binding to pcmcia.dll (Card Services) and thin call wrappers.
 *
 * Windows CE 2.11 ships no import library for Card Services; every client binds with
 * LoadLibraryW + GetProcAddressW, exactly as Microsoft's own PC Card drivers do. Because the
 * targets are Microsoft-compiled, each call goes through an xt_callN thunk that establishes
 * Microsoft's SH-3 stack layout (see tools/gen_thunks.py).
 */
#include "ce/ce_api.h"
#include "ce/ce_cardserv.h"

static PVOID bind_one(HMODULE m, LPCWSTR name, BOOL *ok)
{
    PVOID p = GetProcAddressW(m, name);
    if (p == NULL) {
        *ok = FALSE;
    }
    return p;
}

BOOL cs_bind(cs_table *t)
{
    BOOL ok = TRUE;
    HMODULE m = LoadLibraryW(L"pcmcia.dll");
    if (m == NULL) {
        return FALSE;
    }
    t->module = m;
    t->RegisterClient = bind_one(m, L"CardRegisterClient", &ok);
    t->DeregisterClient = bind_one(m, L"CardDeregisterClient", &ok);
    t->GetFirstTuple = bind_one(m, L"CardGetFirstTuple", &ok);
    t->GetNextTuple = bind_one(m, L"CardGetNextTuple", &ok);
    t->GetTupleData = bind_one(m, L"CardGetTupleData", &ok);
    t->GetParsedTuple = bind_one(m, L"CardGetParsedTuple", &ok);
    t->RequestConfiguration = bind_one(m, L"CardRequestConfiguration", &ok);
    t->ReleaseConfiguration = bind_one(m, L"CardReleaseConfiguration", &ok);
    t->GetStatus = bind_one(m, L"CardGetStatus", &ok);
    t->ResetFunction = bind_one(m, L"CardResetFunction", &ok);
    t->RequestExclusive = bind_one(m, L"CardRequestExclusive", &ok);
    t->ReleaseExclusive = bind_one(m, L"CardReleaseExclusive", &ok);
    t->RequestWindow = bind_one(m, L"CardRequestWindow", &ok);
    t->ReleaseWindow = bind_one(m, L"CardReleaseWindow", &ok);
    t->ModifyWindow = bind_one(m, L"CardModifyWindow", &ok);
    t->MapWindow = bind_one(m, L"CardMapWindow", &ok);
    t->RequestIRQ = bind_one(m, L"CardRequestIRQ", &ok);
    t->ReleaseIRQ = bind_one(m, L"CardReleaseIRQ", &ok);
    t->AccessConfigurationRegister = bind_one(m, L"CardAccessConfigurationRegister", &ok);
    if (!ok) {
        cs_unbind(t);
    }
    return ok;
}

VOID cs_unbind(cs_table *t)
{
    if (t->module != NULL) {
        FreeLibrary(t->module);
        t->module = NULL;
    }
}

#define U(x) ((UINT32)(x))

CARD_CLIENT_HANDLE cs_RegisterClient(const cs_table *t, CLIENT_CALLBACK cb, PCARD_REGISTER_PARMS p)
{
    return (CARD_CLIENT_HANDLE)xt_call2(t->RegisterClient, U(cb), U(p));
}

STATUS cs_DeregisterClient(const cs_table *t, CARD_CLIENT_HANDLE h)
{
    return xt_call1(t->DeregisterClient, U(h));
}

STATUS cs_GetFirstTuple(const cs_table *t, PCARD_TUPLE_PARMS p)
{
    return xt_call1(t->GetFirstTuple, U(p));
}

STATUS cs_GetNextTuple(const cs_table *t, PCARD_TUPLE_PARMS p)
{
    return xt_call1(t->GetNextTuple, U(p));
}

STATUS cs_GetTupleData(const cs_table *t, PCARD_DATA_PARMS p)
{
    return xt_call1(t->GetTupleData, U(p));
}

STATUS cs_GetParsedTuple(const cs_table *t, CS_SOCKET s, UINT8 tuple, PVOID buf, PUINT32 items)
{
    return xt_call4(t->GetParsedTuple, s, tuple, U(buf), U(items));
}

STATUS cs_RequestConfiguration(const cs_table *t, CARD_CLIENT_HANDLE h, PCARD_CONFIG_INFO p)
{
    return xt_call2(t->RequestConfiguration, U(h), U(p));
}

STATUS cs_ReleaseConfiguration(const cs_table *t, CARD_CLIENT_HANDLE h, CS_SOCKET s)
{
    return xt_call2(t->ReleaseConfiguration, U(h), s);
}

STATUS cs_GetStatus(const cs_table *t, PCARD_STATUS p)
{
    return xt_call1(t->GetStatus, U(p));
}

STATUS cs_ResetFunction(const cs_table *t, CARD_CLIENT_HANDLE h, CS_SOCKET s)
{
    return xt_call2(t->ResetFunction, U(h), s);
}

STATUS cs_RequestExclusive(const cs_table *t, CARD_CLIENT_HANDLE h, CS_SOCKET s)
{
    return xt_call2(t->RequestExclusive, U(h), s);
}

STATUS cs_ReleaseExclusive(const cs_table *t, CARD_CLIENT_HANDLE h, CS_SOCKET s)
{
    return xt_call2(t->ReleaseExclusive, U(h), s);
}

CARD_WINDOW_HANDLE cs_RequestWindow(const cs_table *t, CARD_CLIENT_HANDLE h, PCARD_WINDOW_PARMS p)
{
    return (CARD_WINDOW_HANDLE)xt_call2(t->RequestWindow, U(h), U(p));
}

STATUS cs_ReleaseWindow(const cs_table *t, CARD_WINDOW_HANDLE w)
{
    return xt_call1(t->ReleaseWindow, U(w));
}

PVOID cs_MapWindow(const cs_table *t, CARD_WINDOW_HANDLE w, UINT32 addr, UINT32 size, PUINT32 gran)
{
    return (PVOID)xt_call4(t->MapWindow, U(w), addr, size, U(gran));
}

STATUS cs_RequestIRQ(const cs_table *t, CARD_CLIENT_HANDLE h, CS_SOCKET s, CARD_ISR isr, UINT32 ctx)
{
    return xt_call4(t->RequestIRQ, U(h), s, U(isr), ctx);
}

STATUS cs_ReleaseIRQ(const cs_table *t, CARD_CLIENT_HANDLE h, CS_SOCKET s)
{
    return xt_call2(t->ReleaseIRQ, U(h), s);
}

STATUS cs_AccessConfigurationRegister(const cs_table *t, CARD_CLIENT_HANDLE h, CS_SOCKET s, UINT8 rw, UINT8 off, PUINT8 val)
{
    return xt_call5(t->AccessConfigurationRegister, U(h), s, rw, off, U(val));
}

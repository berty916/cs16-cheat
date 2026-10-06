#include <Windows.h>
#include <math.h>
#include <stdio.h>

typedef float vec3_t[3];

struct cl_entity_t {
    int      index;
    int      player;
    vec3_t   origin;
    vec3_t   angles;
    void*    model;
    int      curstate;
    void*    playerinfo;
};

struct clientinfo_t {
    char name[32];
    char model_name[64];
    int  health;
    int  team;
};

HMODULE g_hClient = nullptr;
HMODULE g_hEngine = nullptr;
bool g_ESP = true;
bool g_Aimbot = false;
float g_ViewMatrix[16];

#define OFFSET_ENTITYLIST    0x0A1B2C3
#define OFFSET_LOCALPLAYER   0x0D4E5F6
#define OFFSET_VIEWMATRIX    0x0E7F8A9
#define OFFSET_HUD_REDRAW    0x1A2B3C4

cl_entity_t* GetEntity(int index) {
    if (!g_hClient) return nullptr;
    uintptr_t base = (uintptr_t)g_hClient;
    cl_entity_t** list = *(cl_entity_t***)(base + OFFSET_ENTITYLIST);
    if (!list) return nullptr;
    return list[index];
}

cl_entity_t* GetLocalPlayer() {
    if (!g_hClient) return nullptr;
    uintptr_t base = (uintptr_t)g_hClient;
    return *(cl_entity_t**)(base + OFFSET_LOCALPLAYER);
}

bool WorldToScreen(vec3_t world, float* screen, int w, int h) {
    float clipW = g_ViewMatrix[12]*world[0] + g_ViewMatrix[13]*world[1]
                + g_ViewMatrix[14]*world[2] + g_ViewMatrix[15];
    if (clipW < 0.01f) return false;
    float x = g_ViewMatrix[0]*world[0] + g_ViewMatrix[1]*world[1]
            + g_ViewMatrix[2]*world[2] + g_ViewMatrix[3];
    float y = g_ViewMatrix[4]*world[0] + g_ViewMatrix[5]*world[1]
            + g_ViewMatrix[6]*world[2] + g_ViewMatrix[7];
    screen[0] = (w * 0.5f) + (x / clipW) * (w * 0.5f);
    screen[1] = (h * 0.5f) - (y / clipW) * (h * 0.5f);
    return true;
}

void DrawBoxGDI(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, x, y, x + w, y + h);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
}

void DrawTextGDI(HDC hdc, int x, int y, const char* txt, COLORREF c) {
    SetTextColor(hdc, c);
    SetBkMode(hdc, TRANSPARENT);
    TextOutA(hdc, x, y, txt, (int)strlen(txt));
}

void RenderESP() {
    if (!g_ESP) return;
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return;
    HDC hdc = GetDC(hwnd);
    if (!hdc) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    cl_entity_t* local = GetLocalPlayer();
    if (!local) { ReleaseDC(hwnd, hdc); return; }

    if (g_hEngine)
        memcpy(g_ViewMatrix, (void*)((uintptr_t)g_hEngine + OFFSET_VIEWMATRIX), sizeof(g_ViewMatrix));

    for (int i = 1; i <= 32; i++) {
        cl_entity_t* ent = GetEntity(i);
        if (!ent || ent == local) continue;
        if (!ent->player) continue;
        clientinfo_t* info = (clientinfo_t*)ent->playerinfo;
        if (!info || info->health <= 0) continue;

        vec3_t top    = { ent->origin[0], ent->origin[1], ent->origin[2] + 72.0f };
        vec3_t bottom = { ent->origin[0], ent->origin[1], ent->origin[2] };

        float sTop[2], sBot[2];
        if (!WorldToScreen(top, sTop, W, H)) continue;
        if (!WorldToScreen(bottom, sBot, W, H)) continue;

        int boxH = (int)(sBot[1] - sTop[1]);
        int boxW = boxH / 2;
        int boxX = (int)sTop[0] - boxW / 2;
        int boxY = (int)sTop[1];

        COLORREF col = (info->team == 1) ? RGB(255, 80, 80) : RGB(80, 150, 255);
        DrawBoxGDI(hdc, boxX, boxY, boxW, boxH, col);
        DrawTextGDI(hdc, boxX, boxY - 16, info->name, RGB(255, 255, 255));

        char hp[32];
        sprintf(hp, "HP:%d", info->health);
        DrawTextGDI(hdc, boxX, boxY + boxH + 2, hp, RGB(100, 255, 100));
    }
    ReleaseDC(hwnd, hdc);
}

void RunAimbot() {
    if (!g_Aimbot) return;
    cl_entity_t* local = GetLocalPlayer();
    if (!local) return;
    cl_entity_t* best = nullptr;
    float bestDist = 999999.0f;
    for (int i = 1; i <= 32; i++) {
        cl_entity_t* ent = GetEntity(i);
        if (!ent || ent == local || !ent->player) continue;
        clientinfo_t* info = (clientinfo_t*)ent->playerinfo;
        if (!info || info->health <= 0) continue;
        float dx = ent->origin[0] - local->origin[0];
        float dy = ent->origin[1] - local->origin[1];
        float dz = ent->origin[2] - local->origin[2];
        float d = sqrt(dx*dx + dy*dy + dz*dz);
        if (d < bestDist) { bestDist = d; best = ent; }
    }
    if (!best) return;
    float dx = best->origin[0] - local->origin[0];
    float dy = best->origin[1] - local->origin[1];
    float dz = (best->origin[2] + 36.0f) - local->origin[2];
    float dist = sqrt(dx*dx + dy*dy);
    local->angles[0] = (float)(-atan2(dz, dist) * 180.0 / 3.14159265);
    local->angles[1] = (float)(atan2(dy, dx) * 180.0 / 3.14159265);
    local->angles[2] = 0.0f;
}

typedef int(__stdcall* tHUD_Redraw)(float, int);
tHUD_Redraw oHUD_Redraw = nullptr;

int __stdcall hkHUD_Redraw(float time, int intermission) {
    int ret = oHUD_Redraw ? oHUD_Redraw(time, intermission) : 0;
    RenderESP();
    RunAimbot();
    return ret;
}

void InstallHook(void* target, void* hook, void** original) {
    DWORD oldProtect;
    VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    BYTE* tramp = (BYTE*)VirtualAlloc(nullptr, 10, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(tramp, target, 5);
    tramp[5] = 0xE9;
    *(DWORD*)(tramp + 6) = (DWORD)((BYTE*)target + 5 - (tramp + 5) - 5);
    *original = tramp;
    *(BYTE*)target = 0xE9;
    *(DWORD*)((BYTE*)target + 1) = (DWORD)((BYTE*)hook - (BYTE*)target - 5);
    VirtualProtect(target, 5, oldProtect, &oldProtect);
}

DWORD WINAPI MainThread(LPVOID) {
    while (!(g_hClient = GetModuleHandleA("client.dll"))) Sleep(100);
    while (!(g_hEngine = GetModuleHandleA("engine.dll"))) Sleep(100);
    Sleep(1500);
    void* pHUD = (void*)((uintptr_t)g_hClient + OFFSET_HUD_REDRAW);
    InstallHook(pHUD, (void*)hkHUD_Redraw, (void**)&oHUD_Redraw);
    while (true) {
        Sleep(50);
        if (GetAsyncKeyState(VK_F1) & 1) g_ESP = !g_ESP;
        if (GetAsyncKeyState(VK_F2) & 1) g_Aimbot = !g_Aimbot;
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}

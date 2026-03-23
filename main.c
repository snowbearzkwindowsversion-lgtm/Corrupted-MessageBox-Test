#include <windows.h>
#include <stdint.h>

HWND hwndMsgBox;
HHOOK hMsgHook;

typedef struct tagRGBQUAD RGBQUAD, *PRGBQUAD;

// Xorshift32 RNG
DWORD Xorshift32(void)
{
    static uint32_t x = 123456789;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

// Fill a WCHAR array with random Unicode chars
VOID GetRandomPath(PWSTR szRandom, INT nLength)
{
    for (INT i = 0; i < nLength; i++)
        szRandom[i] = (WCHAR)(Xorshift32() % (0x9FFF - 0x4E00 + 1) + 0x4E00);
}

// Redraw child windows (used for messagebox corruption)
BOOL CALLBACK MsgBoxRefreshWndProc(HWND hwnd, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE);
    return TRUE;
}

// Disable child controls of MessageBox and set text
BOOL CALLBACK MsgBoxWndProc(HWND hwnd, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    EnableWindow(hwnd, FALSE);
    SetWindowTextW(hwnd, L"Terrible decision.");
    return TRUE;
}

// Thread that corrupts the MessageBox pixels
DWORD WINAPI MsgBoxCorruptionThread(LPVOID lpParam)
{
    HWND hwndMsgBox = (HWND)lpParam;

    BITMAPINFO bmi = { 0 };
    HDC hdcMsgBox, hdcTemp;
    HBITMAP hbm;
    HANDLE hHeap;
    PRGBQUAD prgbPixels;
    RECT rc;
    INT w, h;

    GetWindowRect(hwndMsgBox, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = h;

    hHeap = GetProcessHeap();
    prgbPixels = (PRGBQUAD)HeapAlloc(hHeap, 0, w * h * sizeof(RGBQUAD));

    hdcMsgBox = GetDC(hwndMsgBox);
    hdcTemp = CreateCompatibleDC(hdcMsgBox);
    hbm = CreateDIBSection(hdcMsgBox, &bmi, DIB_RGB_COLORS, (VOID**)&prgbPixels, NULL, 0);
    SelectObject(hdcTemp, hbm);

    for (;;)
    {
        for (INT i = 0; i < w * h; i++)
        {
            prgbPixels[i].rgbRed = prgbPixels[i].rgbGreen = prgbPixels[i].rgbBlue = Xorshift32() % 256;
            prgbPixels[i].rgbReserved = 0;
        }

        BitBlt(hdcMsgBox, 0, 0, w, h, hdcTemp, 0, 0, SRCCOPY);
        EnumChildWindows(hwndMsgBox, MsgBoxRefreshWndProc, 0);
        Sleep(10);
    }

    // Cleanup (never reached)
    DeleteDC(hdcTemp);
    ReleaseDC(hwndMsgBox, hdcMsgBox);
    HeapFree(hHeap, 0, prgbPixels);

    return 0;
}

// Hook procedure to intercept MessageBox creation
LRESULT CALLBACK MsgBoxHookProc(INT nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HCBT_ACTIVATE)
    {
        hwndMsgBox = (HWND)wParam;
        ShowWindow(hwndMsgBox, SW_SHOW);
        EnumChildWindows(hwndMsgBox, MsgBoxWndProc, 0);
        CreateThread(NULL, 0, MsgBoxCorruptionThread, hwndMsgBox, 0, NULL);
        return 0;
    }

    return CallNextHookEx(hMsgHook, nCode, wParam, lParam);
}

// Thread to show the hooked MessageBox
DWORD WINAPI MessageBoxThread(LPVOID lpParam)
{
    UNREFERENCED_PARAMETER(lpParam);
    hMsgHook = SetWindowsHookExW(WH_CBT, MsgBoxHookProc, NULL, GetCurrentThreadId());
    MessageBoxW(NULL, L"Terrible decision.", L"Terrible decision.", MB_ABORTRETRYIGNORE | MB_ICONERROR);
    UnhookWindowsHookEx(hMsgHook);
    return 0;
}

// Entry point
int main()
{
    // Start the hooked MessageBox
    CreateThread(NULL, 0, MessageBoxThread, NULL, 0, NULL);

    // Keep main alive
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
        DispatchMessage(&msg);

    return 0;
}

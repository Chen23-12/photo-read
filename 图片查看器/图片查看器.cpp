#include <Windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <shellapi.h>
#include <gdiplus.h>

// 新增头文件
#include <shlobj.h>
#include <stdio.h>

using namespace Gdiplus;

// 链接选项
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

// 宏定义
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

// 全局变量
HWND g_hMainWnd;
HWND g_hStatusBar;
Image* g_pImage = nullptr;
std::wstring g_currentFile;

UINT g_imageWidth = 0;
UINT g_imageHeight = 0;

// 动画相关
bool g_isAnimated = false;
UINT g_totalFrames = 1;
UINT g_currentFrame = 0;
std::vector<UINT> g_frameDelays;
UINT_PTR g_animationTimer = 0;

// 缩放与拖拽
float g_zoomFactor = 1.0f;
const float ZOOM_MIN = 0.1f;
const float ZOOM_MAX = 5.0f;
const float ZOOM_STEP = 0.1f;
bool g_imageDragging = false;
POINT g_dragStartPoint = { 0, 0 };
POINT g_imageOffset = { 0, 0 };
POINT g_zoomCenter = { 0, 0 };

// GDI+初始化
ULONG_PTR g_gdiplusToken = 0;

// 函数声明
void LoadImageFile(const wchar_t* filePath);
bool SaveImageFile(const wchar_t* filePath);
void UpdateStatusBar();
void Cleanup();
std::wstring GetFileName(const std::wstring& path);
std::wstring GetFileExtension(const std::wstring& path);
void ConvertToFormat(const wchar_t* format);
void ResetView();
void ZoomImage(float factor, POINT mousePos);
void DrawImage(HDC hdc, int windowWidth, int windowHeight);
void LoadNextFrame();
void StopAnimation();
void StartAnimation();
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
bool RegisterFileAssociation(const wchar_t* ext, const wchar_t* progId, const wchar_t* description, const wchar_t* iconPath, const wchar_t* exePath);
bool UnregisterFileAssociation(const wchar_t* ext, const wchar_t* progId);

// 获取文件名
std::wstring GetFileName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

// 获取扩展名
std::wstring GetFileExtension(const std::wstring& path) {
    size_t pos = path.find_last_of(L'.');
    return (pos == std::wstring::npos) ? L"" : path.substr(pos + 1);
}

// 注册文件关联
bool RegisterFileAssociation(const wchar_t* ext, const wchar_t* progId, const wchar_t* description, const wchar_t* iconPath, const wchar_t* exePath) {
    wchar_t classKey[256];
    wchar_t commandKey[256];
    wchar_t exeCommand[512];

    // 注册文件扩展名
    swprintf_s(classKey, L"%s\\ShellNew", ext);
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, ext, 0, NULL, REG_OPTION_NON_VOLATILE,
        KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return false;
    }
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)progId, (wcslen(progId) + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);

    // 创建ProgID键
    swprintf_s(classKey, L"%s", progId);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, classKey, 0, NULL, REG_OPTION_NON_VOLATILE,
        KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return false;
    }
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)description, (wcslen(description) + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);

    // 设置图标
    swprintf_s(classKey, L"%s\\DefaultIcon", progId);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, classKey, 0, NULL, REG_OPTION_NON_VOLATILE,
        KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return false;
    }
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)iconPath, (wcslen(iconPath) + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);

    // 设置Open命令
    swprintf_s(commandKey, L"%s\\shell\\open\\command", progId);
    swprintf_s(exeCommand, L"\"%s\" \"%%1\"", exePath);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, commandKey, 0, NULL, REG_OPTION_NON_VOLATILE,
        KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return false;
    }
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)exeCommand, (wcslen(exeCommand) + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);

    // 通知系统文件关联已更改
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return true;
}

// 取消注册文件关联
bool UnregisterFileAssociation(const wchar_t* ext, const wchar_t* progId) {
    wchar_t classKey[256];

    // 删除文件扩展名关联
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, ext, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, NULL);
        RegCloseKey(hKey);
    }

    // 删除ProgID键及其子键
    swprintf_s(classKey, L"%s\\shell\\open\\command", progId);
    RegDeleteTreeW(HKEY_CURRENT_USER, classKey);

    swprintf_s(classKey, L"%s\\shell\\open", progId);
    RegDeleteTreeW(HKEY_CURRENT_USER, classKey);

    swprintf_s(classKey, L"%s\\shell", progId);
    RegDeleteTreeW(HKEY_CURRENT_USER, classKey);

    swprintf_s(classKey, L"%s\\DefaultIcon", progId);
    RegDeleteTreeW(HKEY_CURRENT_USER, classKey);

    RegDeleteKeyW(HKEY_CURRENT_USER, progId);

    // 通知系统文件关联已更改
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return true;
}

// 加载图片
void LoadImageFile(const wchar_t* filePath) {
    Cleanup();

    g_pImage = new Image(filePath);
    if (g_pImage->GetLastStatus() != Ok) {
        MessageBoxW(g_hMainWnd, L"无法加载图片文件", L"错误", MB_ICONERROR);
        delete g_pImage;
        g_pImage = nullptr;
        return;
    }

    g_imageWidth = g_pImage->GetWidth();
    g_imageHeight = g_pImage->GetHeight();
    g_currentFile = filePath;

    // 检查动画
    GUID dimension = FrameDimensionTime;
    g_totalFrames = g_pImage->GetFrameCount(&dimension);
    g_isAnimated = (g_totalFrames > 1);

    // 处理帧延迟
    if (g_isAnimated) {
        g_frameDelays.clear();
        PROPID propId = PropertyTagFrameDelay;

        UINT propSize = g_pImage->GetPropertyItemSize(propId);
        if (propSize > 0) {
            PropertyItem* pProp = (PropertyItem*)malloc(propSize);
            if (pProp && g_pImage->GetPropertyItem(propId, propSize, pProp) == Ok) {
                UINT* pDelays = (UINT*)pProp->value;
                for (UINT i = 0; i < g_totalFrames; i++) {
                    g_frameDelays.push_back(pDelays[i] * 10);
                }
                free(pProp);
            }
        }

        if (g_frameDelays.empty()) {
            g_frameDelays.assign(g_totalFrames, 100);
        }

        g_currentFrame = 0;
        g_pImage->SelectActiveFrame(&dimension, g_currentFrame);
        StartAnimation();
    }

    ResetView();
    UpdateStatusBar();
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

// 保存图片
bool SaveImageFile(const wchar_t* filePath) {
    if (!g_pImage) {
        MessageBoxW(g_hMainWnd, L"没有要保存的图片", L"错误", MB_ICONWARNING);
        return false;
    }

    std::wstring ext = GetFileExtension(filePath);
    if (ext.empty()) {
        MessageBoxW(g_hMainWnd, L"无效的文件扩展名", L"错误", MB_ICONERROR);
        return false;
    }

    const WCHAR* format = L"image/bmp";
    if (ext == L"jpg" || ext == L"jpeg") format = L"image/jpeg";
    else if (ext == L"png") format = L"image/png";
    else if (ext == L"gif") format = L"image/gif";
    else if (ext == L"tiff") format = L"image/tiff";

    CLSID encoderClsid;
    if (GetEncoderClsid(format, &encoderClsid) == -1) {
        MessageBoxW(g_hMainWnd, L"不支持的保存格式", L"错误", MB_ICONERROR);
        return false;
    }

    if (g_pImage->Save(filePath, &encoderClsid, nullptr) == Ok) {
        wchar_t msg[256];
        swprintf_s(msg, L"图片已成功保存为: %s", filePath);
        MessageBoxW(g_hMainWnd, msg, L"成功", MB_ICONINFORMATION);
        return true;
    }

    MessageBoxW(g_hMainWnd, L"保存图片失败", L"错误", MB_ICONERROR);
    return false;
}

// 更新状态栏
void UpdateStatusBar() {
    wchar_t statusText[256] = L"";
    wchar_t windowTitle[256] = L"增强型图片查看器";

    if (g_pImage) {
        std::wstring fileName = GetFileName(g_currentFile);
        if (g_isAnimated) {
            swprintf_s(statusText, L"%s | %dx%d | 动画帧: %d/%d | 缩放: %.0f%%",
                fileName.c_str(), g_imageWidth, g_imageHeight,
                g_currentFrame + 1, g_totalFrames, g_zoomFactor * 100);
            swprintf_s(windowTitle, L"%s - 增强型图片查看器", fileName.c_str());
        }
        else {
            swprintf_s(statusText, L"%s | %dx%d | 缩放: %.0f%%",
                fileName.c_str(), g_imageWidth, g_imageHeight, g_zoomFactor * 100);
            swprintf_s(windowTitle, L"%s - 增强型图片查看器", fileName.c_str());
        }
    }
    else {
        wcscpy_s(statusText, L"准备就绪");
    }

    SendMessageW(g_hStatusBar, SB_SETTEXT, 0, (LPARAM)statusText);
    SetWindowTextW(g_hMainWnd, windowTitle);
}

// 清理资源
void Cleanup() {
    StopAnimation();

    if (g_pImage) {
        delete g_pImage;
        g_pImage = nullptr;
    }

    g_imageWidth = 0;
    g_imageHeight = 0;
    g_currentFile.clear();
    g_isAnimated = false;
    g_totalFrames = 1;
    g_currentFrame = 0;
    g_frameDelays.clear();
}

// 重置视图
void ResetView() {
    g_zoomFactor = 1.0f;
    g_imageOffset.x = 0;
    g_imageOffset.y = 0;
    UpdateStatusBar();
}

// 缩放图片
void ZoomImage(float factor, POINT mousePos) {
    if (!g_pImage) return;

    RECT clientRect;
    GetClientRect(g_hMainWnd, &clientRect);
    int centerX = (clientRect.right - clientRect.left) / 2;
    int centerY = (clientRect.bottom - clientRect.top) / 2;

    float imgMouseX = (mousePos.x - centerX - g_imageOffset.x) / g_zoomFactor;
    float imgMouseY = (mousePos.y - centerY - g_imageOffset.y) / g_zoomFactor;

    float oldZoom = g_zoomFactor;
    g_zoomFactor *= factor;
    g_zoomFactor = max(ZOOM_MIN, min(ZOOM_MAX, g_zoomFactor));

    if (oldZoom != g_zoomFactor) {
        g_imageOffset.x = (LONG)(mousePos.x - centerX - imgMouseX * g_zoomFactor);
        g_imageOffset.y = (LONG)(mousePos.y - centerY - imgMouseY * g_zoomFactor);
    }

    UpdateStatusBar();
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

// 转换格式
void ConvertToFormat(const wchar_t* format) {
    if (!g_pImage) {
        MessageBoxW(g_hMainWnd, L"没有要转换的图片", L"错误", MB_ICONWARNING);
        return;
    }

    wchar_t fileName[MAX_PATH];
    wcscpy_s(fileName, g_currentFile.c_str());
    PathRemoveExtensionW(fileName);
    PathAddExtensionW(fileName, format);

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = g_hMainWnd;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"位图文件 (*.bmp)\0*.bmp\0JPEG文件 (*.jpg)\0*.jpg\0PNG文件 (*.png)\0*.png\0"
        L"GIF文件 (*.gif)\0*.gif\0TIFF文件 (*.tiff)\0*.tiff\0所有文件 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = format + 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) {
        SaveImageFile(fileName);
    }
}

// 绘制图像
void DrawImage(HDC hdc, int windowWidth, int windowHeight) {
    if (!g_pImage) return;

    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);

    int scaledWidth = (int)(g_imageWidth * g_zoomFactor);
    int scaledHeight = (int)(g_imageHeight * g_zoomFactor);
    int x = (windowWidth - scaledWidth) / 2 + g_imageOffset.x;
    int y = (windowHeight - scaledHeight) / 2 + g_imageOffset.y;

    graphics.DrawImage(g_pImage, x, y, scaledWidth, scaledHeight);

    // 绘制边框
    Pen pen(Color(0, 0, 0));
    graphics.DrawRectangle(&pen, x, y, scaledWidth, scaledHeight);

    // 显示缩放比例
    if (g_zoomFactor != 1.0f) {
        wchar_t zoomText[32];
        swprintf_s(zoomText, L"缩放: %.0f%%", g_zoomFactor * 100);
        SolidBrush brush(Color(255, 0, 0));
        Font font(L"Arial", 12);
        graphics.DrawString(zoomText, -1, &font, PointF(10, 10), &brush);
    }

    // 显示动画帧信息
    if (g_isAnimated) {
        wchar_t frameInfo[32];
        swprintf_s(frameInfo, L"帧: %d/%d", g_currentFrame + 1, g_totalFrames);
        SolidBrush brush(Color(0, 0, 255));
        Font font(L"Arial", 12);
        graphics.DrawString(frameInfo, -1, &font, PointF(10, 30), &brush);
    }
}

// 加载下一帧
void LoadNextFrame() {
    if (!g_pImage || !g_isAnimated) return;

    GUID dimension = FrameDimensionTime;
    g_currentFrame = (g_currentFrame + 1) % g_totalFrames;
    g_pImage->SelectActiveFrame(&dimension, g_currentFrame);

    StopAnimation();
    if (g_totalFrames > 1) {
        g_animationTimer = SetTimer(g_hMainWnd, 1, g_frameDelays[g_currentFrame], NULL);
    }

    UpdateStatusBar();
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

// 停止动画
void StopAnimation() {
    if (g_animationTimer) {
        KillTimer(g_hMainWnd, g_animationTimer);
        g_animationTimer = 0;
    }
}

// 开始动画
void StartAnimation() {
    if (g_isAnimated && g_totalFrames > 1 && !g_animationTimer) {
        g_animationTimer = SetTimer(g_hMainWnd, 1, g_frameDelays[g_currentFrame], NULL);
    }
}

// 获取编码器CLSID
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    ImageCodecInfo* pImageCodecInfo = NULL;

    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;

    GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT i = 0; i < num; i++) {
        if (wcscmp(pImageCodecInfo[i].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[i].Clsid;
            free(pImageCodecInfo);
            return i;
        }
    }

    free(pImageCodecInfo);
    return -1;
}

// 窗口过程
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // 创建状态栏
        g_hStatusBar = CreateWindowExW(0, STATUSCLASSNAMEW, NULL,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hWnd, NULL, GetModuleHandleW(NULL), NULL);

        int parts[] = { 200, 400, -1 };
        SendMessageW(g_hStatusBar, SB_SETPARTS, 3, (LPARAM)parts);

        // 接受拖放
        DragAcceptFiles(hWnd, TRUE);
        break;
    }

    case WM_SIZE: {
        SendMessageW(g_hStatusBar, WM_SIZE, 0, 0);
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    }

    case WM_ERASEBKGND: {
        return 1; // 减少闪烁
    }

    case WM_TIMER: {
        if (wParam == 1) {
            LoadNextFrame();
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int windowWidth = rc.right - rc.left;
        int windowHeight = rc.bottom - rc.top;

        // 双缓冲
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, windowWidth, windowHeight);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hbmMem);

        // 背景
        HBRUSH hBrush = CreateSolidBrush(RGB(240, 240, 240));
        FillRect(hdcMem, &rc, hBrush);
        DeleteObject(hBrush);

        if (g_pImage) {
            DrawImage(hdcMem, windowWidth, windowHeight);
        }
        else {
            // 无图片提示
            Graphics graphics(hdcMem);
            SolidBrush brush(Color(150, 150, 150));
            Font font(L"Arial", 24);
            graphics.DrawString(L"拖放图片文件到此处", -1, &font,
                PointF((windowWidth - 300) / 2, (windowHeight - 30) / 2), &brush);

            // 添加关联提示
            Font smallFont(L"Arial", 12);
            graphics.DrawString(L"右键菜单可设置图片关联", -1, &smallFont,
                PointF((windowWidth - 200) / 2, (windowHeight + 20) / 2), &brush);
        }

        // 复制到屏幕
        BitBlt(hdc, 0, 0, windowWidth, windowHeight, hdcMem, 0, 0, SRCCOPY);

        // 清理
        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
        break;
    }

    case WM_MOUSEWHEEL: {
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            POINT mousePos;
            mousePos.x = GET_X_LPARAM(lParam);
            mousePos.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hWnd, &mousePos);

            float zoomFactor = (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ?
                (1.0f + ZOOM_STEP) : (1.0f - ZOOM_STEP);
            ZoomImage(zoomFactor, mousePos);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (g_pImage) {
            SetCapture(hWnd);
            g_imageDragging = true;
            g_dragStartPoint.x = GET_X_LPARAM(lParam);
            g_dragStartPoint.y = GET_Y_LPARAM(lParam);
        }
        break;
    }

    case WM_MOUSEMOVE: {
        if (g_imageDragging) {
            int currentX = GET_X_LPARAM(lParam);
            int currentY = GET_Y_LPARAM(lParam);

            g_imageOffset.x += currentX - g_dragStartPoint.x;
            g_imageOffset.y += currentY - g_dragStartPoint.y;

            g_dragStartPoint.x = currentX;
            g_dragStartPoint.y = currentY;

            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }

    case WM_LBUTTONUP: {
        if (g_imageDragging) {
            ReleaseCapture();
            g_imageDragging = false;
        }
        break;
    }

    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_ADD:
        case VK_OEM_PLUS:
            ZoomImage(1.0f + ZOOM_STEP, POINT{ 0, 0 });
            break;
        case VK_SUBTRACT:
        case VK_OEM_MINUS:
            ZoomImage(1.0f - ZOOM_STEP, POINT{ 0, 0 });
            break;
        case '0':
        case VK_ESCAPE:
            ResetView();
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        case VK_SPACE:
            if (g_isAnimated) {
                g_animationTimer ? StopAnimation() : StartAnimation();
            }
            break;
        case VK_LEFT:
            if (g_isAnimated && g_totalFrames > 1) {
                StopAnimation();
                g_currentFrame = (g_currentFrame - 1 + g_totalFrames) % g_totalFrames;
                GUID dim = FrameDimensionTime;
                g_pImage->SelectActiveFrame(&dim, g_currentFrame);
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;
        case VK_RIGHT:
            if (g_isAnimated && g_totalFrames > 1) {
                StopAnimation();
                LoadNextFrame();
            }
            break;
        }
        break;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        wchar_t filePath[MAX_PATH];
        DragQueryFileW(hDrop, 0, filePath, MAX_PATH);
        DragFinish(hDrop);
        LoadImageFile(filePath);
        break;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case 1: { // 打开
            OPENFILENAMEW ofn = { 0 };
            wchar_t szFile[MAX_PATH] = L"";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"所有图片格式\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff\0"
                L"GIF图片 (*.gif)\0*.gif\0所有文件 (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

            if (GetOpenFileNameW(&ofn)) {
                LoadImageFile(szFile);
            }
            break;
        }
        case 2: ConvertToFormat(L".bmp"); break;
        case 3: ConvertToFormat(L".jpg"); break;
        case 4: ConvertToFormat(L".png"); break;
        case 5: ConvertToFormat(L".gif"); break;
        case 6: { // 保存为
            if (g_pImage) {
                wchar_t saveFile[MAX_PATH];
                wcscpy_s(saveFile, g_currentFile.c_str());
                OPENFILENAMEW sfn = { 0 };
                sfn.lStructSize = sizeof(sfn);
                sfn.hwndOwner = hWnd;
                sfn.lpstrFile = saveFile;
                sfn.nMaxFile = MAX_PATH;
                sfn.lpstrFilter = L"所有支持的格式\0*.bmp;*.jpg;*.png;*.gif;*.tiff\0"
                    L"位图文件 (*.bmp)\0*.bmp\0JPEG文件 (*.jpg)\0*.jpg\0";
                sfn.nFilterIndex = 1;
                sfn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

                if (GetSaveFileNameW(&sfn)) {
                    SaveImageFile(saveFile);
                }
            }
            else {
                MessageBoxW(g_hMainWnd, L"没有打开的图片", L"错误", MB_ICONWARNING);
            }
            break;
        }
        case 7: ResetView(); InvalidateRect(hWnd, NULL, TRUE); break;
        case 8: ZoomImage(1.0f + ZOOM_STEP, POINT{ 0, 0 }); break;
        case 9: ZoomImage(1.0f - ZOOM_STEP, POINT{ 0, 0 }); break;
        case 10:
            if (g_isAnimated) {
                g_animationTimer ? StopAnimation() : StartAnimation();
            }
            break;
        case 11:
            if (g_isAnimated && g_totalFrames > 1) {
                StopAnimation();
                g_currentFrame = (g_currentFrame - 1 + g_totalFrames) % g_totalFrames;
                GUID dim = FrameDimensionTime;
                g_pImage->SelectActiveFrame(&dim, g_currentFrame);
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;
        case 12:
            if (g_isAnimated && g_totalFrames > 1) {
                StopAnimation();
                LoadNextFrame();
            }
            break;
        case 13: PostQuitMessage(0); break;
        case 14: { // 注册文件关联
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            wchar_t iconPath[MAX_PATH];
            swprintf_s(iconPath, L"%s,0", exePath);

            if (MessageBoxW(g_hMainWnd, L"此操作将关联以下图片格式:\n.jpg .jpeg .png .bmp .gif .tiff\n是否继续?",
                L"设置文件关联", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                const wchar_t* extensions[] = { L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".tiff" };
                const wchar_t* progId = L"EnhancedImageViewer.File";
                const wchar_t* description = L"增强型图片查看器";

                bool success = true;
                for (int i = 0; i < 6; i++) {
                    if (!RegisterFileAssociation(extensions[i], progId, description, iconPath, exePath)) {
                        success = false;
                    }
                }

                if (success) {
                    MessageBoxW(g_hMainWnd, L"文件关联设置成功!", L"成功", MB_ICONINFORMATION);
                }
                else {
                    MessageBoxW(g_hMainWnd, L"文件关联设置失败，请以管理员身份运行程序重试。", L"错误", MB_ICONERROR);
                }
            }
            break;
        }
        case 15: { // 取消文件关联
            if (MessageBoxW(g_hMainWnd, L"此操作将取消所有图片格式关联，是否继续?",
                L"取消文件关联", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                const wchar_t* extensions[] = { L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".tiff" };
                const wchar_t* progId = L"EnhancedImageViewer.File";

                bool success = true;
                for (int i = 0; i < 6; i++) {
                    if (!UnregisterFileAssociation(extensions[i], progId)) {
                        success = false;
                    }
                }

                if (success) {
                    MessageBoxW(g_hMainWnd, L"文件关联已取消!", L"成功", MB_ICONINFORMATION);
                }
                else {
                    MessageBoxW(g_hMainWnd, L"文件关联取消失败，请以管理员身份运行程序重试。", L"错误", MB_ICONERROR);
                }
            }
            break;
        }
        }
        break;
    }

    case WM_RBUTTONUP: {
        // 创建右键菜单
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 14, L"关联图片文件");
        AppendMenuW(hMenu, MF_STRING, 15, L"取消图片关联");

        POINT pt;
        GetCursorPos(&pt);
        TrackPopupMenu(hMenu, TPM_LEFTBUTTON, pt.x, pt.y, 0, g_hMainWnd, NULL);
        DestroyMenu(hMenu);
        break;
    }

    case WM_DESTROY:
        Cleanup();
        GdiplusShutdown(g_gdiplusToken);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// 入口函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 初始化GDI+
    GdiplusStartupInput gdiplusInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusInput, NULL);
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusInput, NULL) != Ok) {
        MessageBoxW(NULL, L"GDI+初始化失败", L"错误", MB_ICONERROR);
        return 1;
    }
    // 初始化通用控件
    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    // 注册窗口类
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"EnhancedImageViewer";

    if (!RegisterClassExW(&wcex)) {
        MessageBoxW(NULL, L"窗口注册失败", L"错误", MB_ICONERROR);
        return 1;
    }

    // 创建窗口
    g_hMainWnd = CreateWindowExW(
        0, L"EnhancedImageViewer", L"增强型图片查看器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);

    if (!g_hMainWnd) {
        MessageBoxW(NULL, L"窗口创建失败", L"错误", MB_ICONERROR);
        return 1;
    }

    // 创建菜单
    HMENU hMenu = CreateMenu();
    HMENU hFileMenu = CreateMenu();
    HMENU hViewMenu = CreateMenu();
    HMENU hAnimateMenu = CreateMenu();

    AppendMenuW(hFileMenu, MF_STRING, 1, L"打开...");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFileMenu, MF_STRING, 2, L"另存为BMP...");
    AppendMenuW(hFileMenu, MF_STRING, 3, L"另存为JPG...");
    AppendMenuW(hFileMenu, MF_STRING, 4, L"另存为PNG...");
    AppendMenuW(hFileMenu, MF_STRING, 5, L"另存为GIF...");
    AppendMenuW(hFileMenu, MF_STRING, 6, L"另存为...");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFileMenu, MF_STRING, 13, L"退出");

    AppendMenuW(hViewMenu, MF_STRING, 7, L"重置视图");
    AppendMenuW(hViewMenu, MF_STRING, 8, L"放大");
    AppendMenuW(hViewMenu, MF_STRING, 9, L"缩小");

    AppendMenuW(hAnimateMenu, MF_STRING, 10, L"播放/暂停");
    AppendMenuW(hAnimateMenu, MF_STRING, 11, L"上一帧");
    AppendMenuW(hAnimateMenu, MF_STRING, 12, L"下一帧");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"文件");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, L"视图");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hAnimateMenu, L"动画");

    SetMenu(g_hMainWnd, hMenu);

    // 显示窗口
    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    // 处理命令行参数
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != NULL) {
        // 若有至少一个参数（argv[0]是程序自身路径，argv[1]及以后是附加参数）
        if (argc > 1) {
            // 取第一个附加参数（图片路径）
            LoadImageFile(argv[1]);
        }
        LocalFree(argv); // 释放解析出的参数数组
    }
    else {
        // 解析失败时提示
        MessageBoxW(g_hMainWnd, L"命令行参数解析失败", L"警告", MB_ICONWARNING);
    }

    // 消息循环
    MSG msg = { 0 };
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
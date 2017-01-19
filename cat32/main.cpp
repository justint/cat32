// cat32
// Alexey Pavlov, 2008
// http://apavlov.wordpres.com

#define _WIN32_WINNT 0x501

#include <windows.h>
#include <tchar.h>
#include <time.h>
#include <stdio.h>
#include <crtdbg.h>
#include <math.h>
#include <shlobj.h>
#include <fstream>

#include "resource.h"


#include <gdiplus.h>
using namespace Gdiplus;

#pragma comment(lib, "gdiplus.lib")
#ifndef _DEBUG
#pragma comment(lib, "delayimp.lib")
#pragma comment(linker, "/delayload:gdiplus.dll")
#endif

// Some constants
#define MAINWINDOWCLASSNAME   _T("CAT32_MAIN")
#define CARPETWINDOWCLASSNAME   _T("CAT32_CARPET")

// Global variables
HINSTANCE ghInst = 0;
HWND ghWndMain = 0;
HWND ghWndCarpet = 0;

// Functions forward definitions
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK CarpetWndProc(HWND, UINT, WPARAM, LPARAM);
bool RegisterClasses();
bool CreateWindows();
void LoadSettings();
void SaveSettings();

struct Settings
{
	int left, top, cat;	
	Settings() : left(0), top(0), cat(0) { }
} settings;

//
// Application entry point
//
int APIENTRY WinMain(HINSTANCE hInstance,
					 HINSTANCE hPrevInstance,
					 LPSTR     lpCmdLine,
					 int       nCmdShow)
{
	DEVMODE dmSettings;  
	memset(&dmSettings, 0, sizeof(dmSettings));
	dmSettings.dmSize = sizeof(dmSettings);
	EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dmSettings);
	if (dmSettings.dmBitsPerPel != 32)
	{
		MessageBox(NULL, _T("Sorry, current color depth is not supported.\nPlease change display color depth to 32 and restart the program."), _T("Cat32"), MB_OK | MB_ICONERROR);
		return 1;
	}

	// Init global hInstance
	ghInst = hInstance;	

	LoadSettings();

	if (!RegisterClasses() ||  !CreateWindows())
		return 1;
	
	// Main message loop
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) 
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);		
	}

	SaveSettings();

	return 0;
}

//
// Registering window classes
//
bool RegisterClasses()
{
	// Create Main window class	
	WNDCLASSEX wcMain;
	memset(&wcMain, 0, sizeof(wcMain));
	wcMain.cbSize = sizeof(WNDCLASSEX);
	wcMain.style			= CS_HREDRAW | CS_VREDRAW;
	wcMain.lpfnWndProc		= MainWndProc;	
	wcMain.hInstance		= ghInst;	
	wcMain.hCursor			= LoadCursor(NULL, IDC_ARROW);	
	wcMain.lpszClassName	= MAINWINDOWCLASSNAME;
	wcMain.hIconSm			= NULL;
	if (!RegisterClassEx(&wcMain))
		return false;

	// Create Carpet window class	
	WNDCLASSEX wcCarpet;
	memset(&wcCarpet, 0, sizeof(wcCarpet));
	wcCarpet.cbSize = sizeof(WNDCLASSEX);
	wcCarpet.style			= CS_HREDRAW | CS_VREDRAW;
	wcCarpet.lpfnWndProc	= CarpetWndProc;
	wcCarpet.hInstance		= ghInst;
	wcCarpet.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcCarpet.lpszClassName	= CARPETWINDOWCLASSNAME;
	wcCarpet.hIconSm		= NULL;
	if (!RegisterClassEx(&wcCarpet))
		return false;
	
	return true;
}

static const int tileWidth = 37;
static const int tileHeight = 37;
static const int runSpeed = 12;

bool gbCatIsSleepingOnACarpet = false;
POINT gptCatOffset;

bool CreateWindows()
{
	// Create windows	
	ghWndMain = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, 
		MAINWINDOWCLASSNAME, NULL, WS_POPUP | WS_VISIBLE,
		settings.left, settings.top, tileWidth, tileHeight, 
		NULL, NULL, ghInst, NULL);

	ghWndCarpet = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 
		CARPETWINDOWCLASSNAME, NULL, WS_POPUP | WS_VISIBLE,
		settings.left, settings.top, 100, 100, 
		NULL, NULL, ghInst, NULL);

	return ghWndMain && ghWndCarpet;
}

#pragma region GDI+ Helpers

class CGdiPlusInit  
{
public:
	CGdiPlusInit()
	{
		present=true;
		Gdiplus::GdiplusStartupInput input;
		__try
		{
			Gdiplus::GdiplusStartup(&token, &input, 0);
		}
		__except(1)
		{
			present=false;
		}
	}
	virtual ~CGdiPlusInit()
	{
		if(present) 
			Gdiplus::GdiplusShutdown(token);
	}
	bool Good(){ return present; }
private:
	bool present;
	ULONG_PTR token;
} gdi;


Bitmap* BitmapFromResource(HINSTANCE hInstance, 
                           LPCTSTR szResName, LPCTSTR szResType)
{
    HRSRC hrsrc=FindResource(hInstance, szResName, szResType);
    if(!hrsrc) return 0;
    HGLOBAL hg1=LoadResource(hInstance, hrsrc);
    DWORD sz=SizeofResource(hInstance, hrsrc);
    void* ptr1=LockResource(hg1);
    HGLOBAL hg2=GlobalAlloc(GMEM_FIXED, sz);
    CopyMemory(LPVOID(hg2), ptr1, sz);
    IStream *pStream;
    HRESULT hr=CreateStreamOnHGlobal(hg2, TRUE, &pStream);
    if(FAILED(hr)) return 0;
    Bitmap *image=Bitmap::FromStream(pStream);
    pStream->Release();
    return image;
}

#pragma endregion

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static HBITMAP backBitmap = NULL;
	static HDC backDC = NULL;
	static Bitmap* catBitmap = NULL;
	static Bitmap* shadowBitmap = NULL;		
	static int oddCounter = 0;
	static int sleepCounter = 0;
	static int currentFreq = 0;
	static bool timerIsOn = true;
	static int cleaningCount = 0;

	static const int movingFreq = 100;
	static const int waitingFreq = 300;

	switch(message)
	{		
	case WM_CREATE:
		{
			SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);		

			HDC hdc = GetDC(hWnd);
			backBitmap = CreateCompatibleBitmap(hdc, tileWidth, tileHeight);
			backDC = CreateCompatibleDC(hdc);
			SelectObject(backDC, backBitmap);
			ReleaseDC(hWnd, hdc);

			SendMessage(hWnd, WM_USER, settings.cat, 0);
			shadowBitmap = BitmapFromResource(ghInst, MAKEINTRESOURCE(IDR_PNG2), _T("PNG"));			

			currentFreq = movingFreq;
			SetTimer(hWnd, 1, currentFreq, 0);
		}
		break;

	case WM_DESTROY:
		delete catBitmap;
		catBitmap = NULL;

		delete shadowBitmap;		
		shadowBitmap = NULL;
		break;

	case WM_USER:
		{
			Bitmap* newCatBitmap = 0;
			switch(wParam)
			{
			case 0: newCatBitmap = BitmapFromResource(ghInst, MAKEINTRESOURCE(IDR_PNG1), _T("PNG")); break;
			case 1: newCatBitmap = BitmapFromResource(ghInst, MAKEINTRESOURCE(IDR_PNG4), _T("PNG")); break;
			case 2: newCatBitmap = BitmapFromResource(ghInst, MAKEINTRESOURCE(IDR_PNG5), _T("PNG")); break;
			}
			if (newCatBitmap)
			{
				delete catBitmap;
				catBitmap = newCatBitmap;
				settings.cat = static_cast<int>(wParam);
			}
		}
		break;

	case WM_TIMER:
		{
			RECT rect, carpetRect;
			GetWindowRect(hWnd, &rect);
			GetWindowRect(ghWndCarpet, &carpetRect);
			
			bool bTargetIsCarpet = false;
			int deltaX = 0, deltaY = 0;

			if (!gbCatIsSleepingOnACarpet)
			{			
				// Calculate changes for cat coordinates using vector (cat, mouse cursor)
				POINT cur;
				if (!GetCursorPos(&cur))
					break;

				bTargetIsCarpet = carpetRect.left < cur.x && cur.x < carpetRect.right &&
					carpetRect.top < cur.y && cur.y < carpetRect.bottom;

				if (bTargetIsCarpet)
				{
					// User points on a carpet - justify coordinates
					cur.x = carpetRect.left + (carpetRect.right - carpetRect.left - tileWidth) / 2;
					cur.y = carpetRect.top + (carpetRect.bottom - carpetRect.top - static_cast<int>(tileHeight * 1.5)) / 2;
				}

				deltaX = (cur.x - rect.left);
				deltaY = (cur.y - rect.top);
												
				if (deltaX || deltaY)
				{
					float dx = static_cast<float>(cur.x - rect.left);
					float dy = static_cast<float>(cur.y - rect.top);
					float length = sqrt(dx*dx + dy*dy);
					float invLength = min(length, static_cast<float>(runSpeed)) / length;
					deltaX = static_cast<int>(dx * invLength);
					deltaY = static_cast<int>(dy * invLength);
				}
			}

			if (deltaX || deltaY)
			{				
				// Change cat window position
				SetWindowPos(hWnd, HWND_TOPMOST, 
					rect.left + deltaX, rect.top + deltaY, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOREDRAW);
			}
			else if (GetTopWindow(NULL) != hWnd)
			{
				// Just bring in on top (just in case)
				SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOMOVE);
			}

			oddCounter = (oddCounter + 1) % 2;

			int scaleX = 1;
			int scaleY = 1;

			int row = 0;

			if (deltaX || deltaY)
			{
				// Need to move - update timer and invalidate sleep cntr
				if (currentFreq != movingFreq)
				{
					KillTimer(hWnd, 1);
					SetTimer(hWnd, 1, currentFreq = movingFreq, 0);
				}
				sleepCounter = 0;
			}
			else if (currentFreq != waitingFreq)
			{
				// Update timer				
				KillTimer(hWnd, 1);
				SetTimer(hWnd, 1, currentFreq = waitingFreq, 0);				
			}

			if (!deltaX && !deltaY)
			{
				// No need to move
				if (!(sleepCounter++))
				{
					cleaningCount = bTargetIsCarpet ? 1 : 1 + (rand() % 5);
				}

				if (sleepCounter < 20 * cleaningCount)
				{
					// Cat is waiting and cleaning
					if ((sleepCounter % 20) < 10 || 16 < (sleepCounter % 20))					
						oddCounter = 0;

					row = 4;
				}
				else
				{
					// Cat is sleeping
					row = 5;
					if (sleepCounter == 20 * cleaningCount)
					{											
						if (bTargetIsCarpet)
						{
							gbCatIsSleepingOnACarpet = true;
							gptCatOffset.x = rect.left - carpetRect.left;
							gptCatOffset.y = rect.top - carpetRect.top;
							SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT);
						}
					}
				}				
			}
			else if (abs(deltaX) < 4)
			{
				// Move up/down
				row = 3;
				if (0 < deltaY)
					scaleY = -1;
			}
			else
			{
				// Move left/right
				if (abs(deltaY) < 4)
					row = 0;
				else if (deltaY < 0)
					row = 2; 
				else if (0 < deltaY)
					row = 1;				

				if (deltaX < 0)
					scaleX = -1;
			}

			// Repaint window
			{
				Graphics g(backDC);

				SolidBrush brush(Color(255, 0, 0, 0));
				g.FillRectangle(&brush, RectF(0, 0, static_cast<REAL>(tileWidth), static_cast<REAL>(tileHeight)));							
				
				if (scaleX < 0)
					g.TranslateTransform(static_cast<REAL>(tileWidth), 0);				

				if (scaleY < 0)
					g.TranslateTransform(0, static_cast<REAL>(tileHeight));

				g.ScaleTransform(static_cast<REAL>(scaleX), static_cast<REAL>(scaleY));

				g.DrawImage(shadowBitmap, 2 * scaleX, 2 * scaleY, 
					oddCounter * tileWidth, row * tileHeight, 
					tileWidth, tileHeight, UnitPixel);
				g.DrawImage(catBitmap, 
					0, 0, 
					oddCounter * tileWidth, row * tileHeight, 
					tileWidth, tileHeight, UnitPixel);
				g.Flush();
			
				BLENDFUNCTION blend;
				blend.BlendOp = AC_SRC_OVER;
				blend.BlendFlags = 0;
				blend.AlphaFormat = AC_SRC_ALPHA;
				blend.SourceConstantAlpha = 255;

				SIZE size = {tileWidth, tileHeight};
				POINT ptSrc = {0, 0};

				UpdateLayeredWindow(hWnd, NULL, NULL, &size, backDC, &ptSrc, 0, &blend, ULW_ALPHA);
			}
		}
		break;

	case WM_LBUTTONDOWN:
		if (gbCatIsSleepingOnACarpet)			
		{
			gbCatIsSleepingOnACarpet = false;
			sleepCounter = 0;
			SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);			
		}
		break;		

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}


LRESULT CALLBACK CarpetWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static Bitmap* carpetBitmap = NULL;	
	static HBITMAP backBitmap = NULL;
	static HDC backDC = NULL;
	static POINT offset;
	
	switch(message)
	{		
	case WM_CREATE:
		{
			SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);		
			
			carpetBitmap = BitmapFromResource(ghInst, MAKEINTRESOURCE(IDR_PNG3), _T("PNG"));

			int width = carpetBitmap->GetWidth();
			int height = carpetBitmap->GetHeight();

			settings.left = min(settings.left, GetSystemMetrics(SM_CXSCREEN) - width);
			settings.top = min(settings.top, GetSystemMetrics(SM_CYSCREEN) - height);

			MoveWindow(hWnd, settings.left, settings.top, width, height, 0);

			HDC hdc = GetDC(hWnd);
			backBitmap = CreateCompatibleBitmap(hdc, width, height);
			backDC = CreateCompatibleDC(hdc);
			SelectObject(backDC, backBitmap);
			ReleaseDC(hWnd, hdc);

			SendMessage(hWnd, WM_USER, 180, 0);
		}
		break;

	case WM_DESTROY:
		delete carpetBitmap;
		carpetBitmap = NULL;
		break;

	case WM_USER:
		{
			Graphics g(backDC);

			SolidBrush brush(Color(255, 0, 0, 0));
			g.FillRectangle(&brush, 
				RectF(0, 0, static_cast<REAL>(carpetBitmap->GetWidth()), static_cast<REAL>(carpetBitmap->GetHeight())));	

			g.DrawImage(carpetBitmap, 0, 0);

			g.Flush();
		
			BLENDFUNCTION blend;
			blend.BlendOp = AC_SRC_OVER;
			blend.BlendFlags = 0;
			blend.AlphaFormat = AC_SRC_ALPHA;
			blend.SourceConstantAlpha = static_cast<BYTE>(wParam);

			SIZE size = {carpetBitmap->GetWidth(), carpetBitmap->GetHeight()};
			POINT ptSrc = {0, 0};

			UpdateLayeredWindow(hWnd, NULL, NULL, &size, backDC, &ptSrc, 0, &blend, ULW_ALPHA);
		}
		break;

	case WM_RBUTTONDOWN:		
		{
			POINT cur;
			GetCursorPos(&cur);
			HMENU hMenu = LoadMenu(ghInst, MAKEINTRESOURCE(IDR_MENU1));	
			
			HMENU hSub = GetSubMenu(hMenu, 0);
			
			CheckMenuItem(hSub, settings.cat, MF_BYPOSITION | MF_CHECKED);

			TrackPopupMenu(hSub, TPM_RIGHTALIGN | TPM_TOPALIGN, cur.x, cur.y, 0, hWnd, NULL);

			DestroyMenu(hMenu);
		}
		break;	

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case ID_1_ORANGE:
			SendMessage(ghWndMain, WM_USER, 0, 0);
			break;

		case ID_1_BLACK:
			SendMessage(ghWndMain, WM_USER, 1, 0);
			break;

		case ID_1_WHITE:
			SendMessage(ghWndMain, WM_USER, 2, 0);
			break;

		case ID_1_EXIT:
			CloseWindow(ghWndMain);
			DestroyWindow(ghWndMain);
			CloseWindow(hWnd);
			DestroyWindow(hWnd);
			PostQuitMessage(0);
			break;

		case ID_1_ABOUT:
			MessageBox(hWnd, 
				_T("Cat32\n")				
				_T("Alexey Pavlov, 2008\n")
				_T("http://apavlov.wordpress.com\n")				
				_T("----------------\n")
				_T("- Use left mouse to move the carpet;\n")
				_T("- The cat likes to sleep on the carpet;\n")
				_T("----------------\n")
				_T("Original idea and character: TopCAT! (c) 1991 by Robert Dannbauer"), _T("About"), MB_OK | MB_ICONASTERISK);

			break;
		}
		break;

	case WM_LBUTTONDOWN:			
		offset.x = LOWORD(lParam);
		offset.y = HIWORD(lParam);
		SetCapture(hWnd);		
		break;

	case WM_LBUTTONUP:
		ReleaseCapture();
		break;

	case WM_MOUSEMOVE:
		if (GetCapture() == hWnd)
		{
			POINT cursor;
			if (GetCursorPos(&cursor))
			{			
				SetWindowPos(hWnd, ghWndMain, 
					cursor.x - offset.x, cursor.y - offset.y,
					0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOREDRAW);

				settings.left = cursor.x - offset.x;
				settings.top = cursor.y - offset.y;

				if (gbCatIsSleepingOnACarpet)
				{
					SetWindowPos(ghWndMain, HWND_TOPMOST, 
						cursor.x - offset.x  + gptCatOffset.x, cursor.y - offset.y + gptCatOffset.y,					
						0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOREDRAW);
				}
			}
		}
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

bool GetSettingsFileName(wchar_t* szPath)
{
	if (SUCCEEDED(SHGetFolderPathW(NULL, 
		CSIDL_PERSONAL|CSIDL_FLAG_CREATE, 
		NULL, 
		0, 
		szPath))) 
	{
		wcscat_s(szPath, MAX_PATH, L"\\cat32.dta");
		return true;
	}
	return false;
}

void LoadSettings()
{
	wchar_t szPath[MAX_PATH];
	if (GetSettingsFileName(szPath)) 
	{		
		std::ifstream settingsStream(szPath);
		if (settingsStream.is_open())
		{			
			settingsStream >> settings.left >> settings.top >> settings.cat;
		}
	}
}

void SaveSettings()
{
	wchar_t szPath[MAX_PATH];
	if (GetSettingsFileName(szPath)) 
	{
		std::ofstream settingsStream(szPath);
		if (!settingsStream.fail())
		{			
			settingsStream << settings.left << " " << settings.top << " " << settings.cat;
		}
	}
}
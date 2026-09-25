#include <windows.h>
#include <commctrl.h>
#include <richedit.h>

#include "mdi_unit.h"

#define ID_STATUSBAR       4997
#define ID_TOOLBAR         4998

#define ID_MDI_CLIENT      4999
#define ID_MDI_FIRSTCHILD  50000

#define IDC_CHILD_EDIT      2000

LRESULT CALLBACK WndProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MDIChildWndProc(HWND hwnd, UINT Message, WPARAM wParam,
   LPARAM lParam);

char g_szAppName[] = "WriteWord";
char g_szChild[] = "MyMDIChild";
HINSTANCE g_hInst;
HWND g_hMDIClient, g_hStatusBar, g_hToolBar;
HWND g_hMainWindow;

/* Keep the frame menu, toolbar and status bar in sync with the active MDI
 * document. During the first child's WM_MDIACTIVATE, g_hMainWindow has
 * not been assigned yet because the frame is still inside CreateWindowEx.
 * Accept the actual frame HWND instead of relying on the global.
 */
static void UpdateDocumentControls(HWND hFrame, HWND hActive)
{
   HMENU hMenu = GetMenu(hFrame);
   HMENU hFileMenu;
   BOOL enabled = (hActive != NULL && IsWindow(hActive));
   char fileName[MAX_PATH];

   if(hMenu)
   {
      EnableMenuItem(hMenu, 1,
         MF_BYPOSITION | (enabled ? MF_ENABLED : MF_GRAYED));
      EnableMenuItem(hMenu, 2,
         MF_BYPOSITION | (enabled ? MF_ENABLED : MF_GRAYED));

      hFileMenu = GetSubMenu(hMenu, 0);
      if(hFileMenu)
      {
         EnableMenuItem(hFileMenu, CM_FILE_SAVE,
            MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
         EnableMenuItem(hFileMenu, CM_FILE_SAVEAS,
            MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
      }
      DrawMenuBar(hFrame);
   }

   if(g_hToolBar)
   {
      SendMessage(g_hToolBar, TB_ENABLEBUTTON, CM_FILE_SAVE, MAKELONG(enabled, 0));
      SendMessage(g_hToolBar, TB_ENABLEBUTTON, CM_EDIT_UNDO, MAKELONG(enabled, 0));
      SendMessage(g_hToolBar, TB_ENABLEBUTTON, CM_EDIT_CUT, MAKELONG(enabled, 0));
      SendMessage(g_hToolBar, TB_ENABLEBUTTON, CM_EDIT_COPY, MAKELONG(enabled, 0));
      SendMessage(g_hToolBar, TB_ENABLEBUTTON, CM_EDIT_PASTE, MAKELONG(enabled, 0));
   }

   if(g_hStatusBar)
   {
      fileName[0] = 0;
      if(enabled)
         GetWindowText(hActive, fileName, MAX_PATH);
      SendMessage(g_hStatusBar, SB_SETTEXT, 0, (LPARAM)fileName);
   }
}

/*
 * Rich Edit streams documents in small chunks. This avoids the old EDIT
 * control's text limit and avoids a second document-sized allocation.
 * File contents remain plain text in the system ANSI code page, as before.
 */
static DWORD CALLBACK ReadTextStream(DWORD_PTR cookie, LPBYTE buffer,
   LONG cb, LONG *readCount)
{
   DWORD count = 0;
   *readCount = 0;
   if(!ReadFile((HANDLE)cookie, buffer, (DWORD)cb, &count, NULL))
   {
      DWORD error = GetLastError();
      return error ? error : ERROR_READ_FAULT;
   }
   *readCount = (LONG)count;
   return 0;
}

static DWORD CALLBACK WriteTextStream(DWORD_PTR cookie, LPBYTE buffer,
   LONG cb, LONG *writtenCount)
{
   DWORD total = 0;
   *writtenCount = 0;
   while(total < (DWORD)cb)
   {
      DWORD count = 0;
      if(!WriteFile((HANDLE)cookie, buffer + total,
         (DWORD)cb - total, &count, NULL))
      {
         DWORD error = GetLastError();
         return error ? error : ERROR_WRITE_FAULT;
      }
      if(count == 0)
         return ERROR_WRITE_FAULT;
      total += count;
      *writtenCount = (LONG)total;
   }
   return 0;
}

BOOL LoadFile(HWND hEdit, LPSTR pszFileName)
{
   HANDLE hFile = CreateFile(pszFileName, GENERIC_READ, FILE_SHARE_READ,
      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   BOOL bSuccess = FALSE;
   if(hFile != INVALID_HANDLE_VALUE)
   {
      DWORD fileSizeHigh = 0;
      DWORD fileSizeLow = GetFileSize(hFile, &fileSizeHigh);
      /*
       * GetFileSize is declared by the older Win32 headers shipped with
       * Dev-C++ 4.9.9.2. Reject files outside Rich Edit's signed 32-bit
       * character range instead of silently loading a truncated document.
       * A failed GetFileSize returns INVALID_FILE_SIZE (0xFFFFFFFF), which
       * also fails the size check below.
       */
      if(fileSizeHigh == 0 && fileSizeLow <= 0x7FFFFFFEUL)
      {
         EDITSTREAM stream;
         ZeroMemory(&stream, sizeof(stream));
         stream.dwCookie = (DWORD_PTR)hFile;
         stream.pfnCallback = ReadTextStream;
         SendMessage(hEdit, EM_STREAMIN, SF_TEXT, (LPARAM)&stream);
         bSuccess = (stream.dwError == 0);
      }
      CloseHandle(hFile);
   }
   return bSuccess;
}

BOOL SaveFile(HWND hEdit, LPSTR pszFileName)
{
   HANDLE hFile = CreateFile(pszFileName, GENERIC_WRITE, 0, NULL,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
   BOOL bSuccess = FALSE;
   if(hFile != INVALID_HANDLE_VALUE)
   {
      EDITSTREAM stream;
      ZeroMemory(&stream, sizeof(stream));
      stream.dwCookie = (DWORD_PTR)hFile;
      stream.pfnCallback = WriteTextStream;
      SendMessage(hEdit, EM_STREAMOUT, SF_TEXT, (LPARAM)&stream);
      bSuccess = (stream.dwError == 0);
      /* An empty document must also be saved as a valid, empty file. */
      if(!CloseHandle(hFile))
         bSuccess = FALSE;
   }
   return bSuccess;
}

BOOL GetFileName(HWND hwnd, LPSTR pszFileName, BOOL bSave)
{
   OPENFILENAME ofn;

   ZeroMemory(&ofn, sizeof(ofn));
   pszFileName[0] = 0;

   ofn.lStructSize = sizeof(ofn);
   ofn.hwndOwner = hwnd;
   ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0\0";
   ofn.lpstrFile = pszFileName;
   ofn.nMaxFile = MAX_PATH;
   ofn.lpstrDefExt = "txt";

   if(bSave)
   {
      ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
         OFN_OVERWRITEPROMPT;
      if(!GetSaveFileName(&ofn))
         return FALSE;
   }
   else
   {
      ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
      if(!GetOpenFileName(&ofn))
         return FALSE;
   }
   return TRUE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
   LPSTR lpszCmdParam, int nCmdShow)
{
   MSG  Msg;
   WNDCLASSEX WndClassEx;
   HACCEL hAccel;
   HMODULE hRichEdit;

   InitCommonControls();
   hRichEdit = LoadLibrary("RICHED20.DLL");
   if(!hRichEdit)
   {
      MessageBox(NULL, "Windows Rich Edit 2.0 is required.",
         "WriteWord", MB_OK | MB_ICONEXCLAMATION);
      return -1;
   }
   hAccel = LoadAccelerators(hInstance, "MAINACCEL");

   g_hInst = hInstance;

   WndClassEx.cbSize          = sizeof(WNDCLASSEX);
   WndClassEx.style           = CS_HREDRAW | CS_VREDRAW;
   WndClassEx.lpfnWndProc     = WndProc;
   WndClassEx.cbClsExtra      = 0;
   WndClassEx.cbWndExtra      = 0;
   WndClassEx.hInstance       = hInstance;
   WndClassEx.hIcon           = LoadIcon(NULL, IDI_APPLICATION);
   WndClassEx.hCursor         = LoadCursor(NULL, IDC_ARROW);
   WndClassEx.hbrBackground   = (HBRUSH)(COLOR_3DSHADOW+1);
   WndClassEx.lpszMenuName       = "MAIN";
   WndClassEx.lpszClassName   = g_szAppName;
   WndClassEx.hIconSm           = LoadIcon(NULL, IDI_APPLICATION);

   if(!RegisterClassEx(&WndClassEx))
   {
      MessageBox(0, "Could Not Register Window", "Error...",
         MB_ICONEXCLAMATION | MB_OK);
      return -1;
   }

   WndClassEx.lpfnWndProc     = MDIChildWndProc;
   WndClassEx.lpszMenuName       = NULL;
   WndClassEx.lpszClassName   = g_szChild;
   WndClassEx.hbrBackground   = (HBRUSH)(COLOR_3DFACE+1);

   if(!RegisterClassEx(&WndClassEx))
   {
      MessageBox(0, "Could Not Register Child Window", "Error...",
         MB_ICONEXCLAMATION | MB_OK);
      return -1;
   }

    g_hMainWindow = CreateWindowEx(WS_EX_APPWINDOW, g_szAppName,
      "WriteWord", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
      0, 0, hInstance, NULL);

   if (g_hMainWindow == NULL){
      MessageBox(0, "No Window", "Error...", MB_ICONEXCLAMATION | MB_OK);
      return -1;
   }

   ShowWindow(g_hMainWindow, nCmdShow);
   UpdateWindow(g_hMainWindow);

   /*
    * The initial child was created during the frame's WM_CREATE, before
    * g_hMainWindow was assigned. Initialize its controls now that the
    * frame exists, and focus its editor so typing works immediately.
    */
   {
      HWND hActive = (HWND)SendMessage(g_hMDIClient,
         WM_MDIGETACTIVE, 0, 0);
      UpdateDocumentControls(g_hMainWindow, hActive);
      if(hActive && !IsIconic(g_hMainWindow))
      {
         HWND hEdit = GetDlgItem(hActive, IDC_CHILD_EDIT);
         if(hEdit)
            SetFocus(hEdit);
      }
   }

   while(GetMessage(&Msg, NULL, 0, 0))
   {
      if (!TranslateAccelerator(g_hMainWindow, hAccel, &Msg) &&
          !TranslateMDISysAccel(g_hMDIClient, &Msg))
      {
         TranslateMessage(&Msg);
         DispatchMessage(&Msg);
      }
   }
 //  WndProc(g_hMainWindow, WM_COMMAND, CM_FILE_NEW, 0);
//   SendMessage(g_hMainWindow, WM_COMMAND, CM_FILE_NEW, 0);
   FreeLibrary(hRichEdit);
   return Msg.wParam;
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
        int width = 0;
         int height = 0;         
         RECT rect;
        if(GetWindowRect(hwnd, &rect))
        {
          width = rect.right - rect.left;
          height = rect.bottom - rect.top;
          width = width/100*90;
          height = height/100*63;
        }
   switch(Message)
   {
      case WM_CREATE:
      {
         CLIENTCREATESTRUCT ccs;
         int iStatusWidths[] = {200, 300, -1};
         TBADDBITMAP tbab;
         TBBUTTON tbb[9];

         // Find window menu where children will be listed
         ccs.hWindowMenu  = GetSubMenu(GetMenu(hwnd), 2);
         ccs.idFirstChild = ID_MDI_FIRSTCHILD;
         g_hMDIClient = CreateWindowEx(WS_EX_CLIENTEDGE, "mdiclient", NULL,
            WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, (HMENU)ID_MDI_CLIENT, g_hInst, (LPVOID)&ccs);
         ShowWindow(g_hMDIClient, SW_SHOW);

         g_hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
            hwnd, (HMENU)ID_STATUSBAR, g_hInst, NULL);
         SendMessage(g_hStatusBar, SB_SETPARTS, 3, (LPARAM)iStatusWidths);
         SendMessage(g_hStatusBar, SB_SETTEXT, 2, (LPARAM)"Plain Text");

         g_hToolBar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
            hwnd, (HMENU)ID_TOOLBAR, g_hInst, NULL);

         // Send the TB_BUTTONSTRUCTSIZE message, which is required for
         // backward compatibility.
         SendMessage(g_hToolBar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

         tbab.hInst = HINST_COMMCTRL;
         tbab.nID = IDB_STD_LARGE_COLOR;
         SendMessage(g_hToolBar, TB_ADDBITMAP, 0, (LPARAM)&tbab);

         ZeroMemory(tbb, sizeof(tbb));

         tbb[0].iBitmap = STD_FILENEW;
         tbb[0].fsState = TBSTATE_ENABLED;
         tbb[0].fsStyle = TBSTYLE_BUTTON;
         tbb[0].idCommand = CM_FILE_NEW;

         tbb[1].iBitmap = STD_FILEOPEN;
         tbb[1].fsState = TBSTATE_ENABLED;
         tbb[1].fsStyle = TBSTYLE_BUTTON;
         tbb[1].idCommand = CM_FILE_OPEN;

         tbb[2].iBitmap = STD_FILESAVE;
         tbb[2].fsStyle = TBSTYLE_BUTTON;
         tbb[2].idCommand = CM_FILE_SAVE;

         tbb[3].fsStyle = TBSTYLE_SEP;

         tbb[4].iBitmap = STD_CUT;
         tbb[4].fsStyle = TBSTYLE_BUTTON;
         tbb[4].idCommand = CM_EDIT_CUT;

         tbb[5].iBitmap = STD_COPY;
         tbb[5].fsStyle = TBSTYLE_BUTTON;
         tbb[5].idCommand = CM_EDIT_COPY;

         tbb[6].iBitmap = STD_PASTE;
         tbb[6].fsStyle = TBSTYLE_BUTTON;
         tbb[6].idCommand = CM_EDIT_PASTE;

         tbb[7].fsStyle = TBSTYLE_SEP;

         tbb[8].iBitmap = STD_UNDO;
         tbb[8].fsStyle = TBSTYLE_BUTTON;
         tbb[8].idCommand = CM_EDIT_UNDO;

         SendMessage(g_hToolBar, TB_ADDBUTTONS, 9, (LPARAM)&tbb);
         
         MDICREATESTRUCT mcs;
               HWND hChild;

               mcs.szTitle = "[Untitled]";
               mcs.szClass = g_szChild;
               mcs.hOwner  = g_hInst;
               mcs.x = CW_USEDEFAULT;
               mcs.y = CW_USEDEFAULT;
               mcs.cx = width;
               mcs.cy = height;
               mcs.style = MDIS_ALLCHILDSTYLES;

               hChild = (HWND)SendMessage(g_hMDIClient, WM_MDICREATE,
                  0, (LONG)&mcs);
               if(!hChild)
               {
                  MessageBox(hwnd, "MDI Child creation failed.", "Error...",
                     MB_ICONEXCLAMATION | MB_OK);
               }

         return 0;
      }
      case WM_COMMAND:
      {
         switch(LOWORD(wParam))
         {
            case CM_FILE_EXIT:
               PostMessage(hwnd, WM_CLOSE, 0, 0);
            break;
            case CM_FILE_NEW:
            {
               MDICREATESTRUCT mcs;
               HWND hChild;

               mcs.szTitle = "[Untitled]";
               mcs.szClass = g_szChild;
               mcs.hOwner  = g_hInst;
               mcs.x = CW_USEDEFAULT;
               mcs.y = CW_USEDEFAULT;
               mcs.cx = width;
               mcs.cy = height;
               mcs.style = MDIS_ALLCHILDSTYLES;

               hChild = (HWND)SendMessage(g_hMDIClient, WM_MDICREATE,
                  0, (LONG)&mcs);
               if(!hChild)
               {
                  MessageBox(hwnd, "MDI Child creation failed.", "Error...",
                     MB_ICONEXCLAMATION | MB_OK);
               }
            }
            break;
            case CM_FILE_OPEN:
            {
               MDICREATESTRUCT mcs;
               HWND hChild;
               char szFileName[MAX_PATH];

               if(!GetFileName(hwnd, szFileName, FALSE))
                  break;

               mcs.szTitle = szFileName;
               mcs.szClass = g_szChild;
               mcs.hOwner  = g_hInst;
               mcs.x = CW_USEDEFAULT;
               mcs.cx = width;
               mcs.y = CW_USEDEFAULT;
               mcs.cy = height;
               mcs.style = MDIS_ALLCHILDSTYLES;

               hChild = (HWND)SendMessage(g_hMDIClient, WM_MDICREATE,
                  0, (LONG)&mcs);

               if(!hChild)
               {
                  MessageBox(hwnd, "MDI Child creation failed.", "Error...",
                     MB_ICONEXCLAMATION | MB_OK);
               }
            }
            break;
            case CM_WINDOW_TILEHORZ:
               PostMessage(g_hMDIClient, WM_MDITILE, MDITILE_HORIZONTAL, 0);
            break;
            case CM_WINDOW_TILEVERT:
               PostMessage(g_hMDIClient, WM_MDITILE, MDITILE_VERTICAL, 0);
            break;
            case CM_WINDOW_CASCADE:
               PostMessage(g_hMDIClient, WM_MDICASCADE, 0, 0);
            break;
            case CM_WINDOW_ARRANGE:
               PostMessage(g_hMDIClient, WM_MDIICONARRANGE, 0, 0);
            break;
            default:
            {
               if(LOWORD(wParam) >= ID_MDI_FIRSTCHILD){
                  DefFrameProc(hwnd, g_hMDIClient, Message, wParam, lParam);
               }
               else {
                  HWND hChild;
                  hChild = (HWND)SendMessage(g_hMDIClient, WM_MDIGETACTIVE,0,0);
                  if(hChild){
                     SendMessage(hChild, WM_COMMAND, wParam, lParam);
                  }
               }
            }
         }
      }
      break;
      case WM_SIZE:
      {
         RECT rectClient, rectStatus, rectTool;
         UINT uToolHeight, uStatusHeight, uClientAlreaHeight;

         SendMessage(g_hToolBar, TB_AUTOSIZE, 0, 0);
         SendMessage(g_hStatusBar, WM_SIZE, 0, 0);

         GetClientRect(hwnd, &rectClient);
         GetWindowRect(g_hStatusBar, &rectStatus);
         GetWindowRect(g_hToolBar, &rectTool);

         uToolHeight = rectTool.bottom - rectTool.top;
         uStatusHeight = rectStatus.bottom - rectStatus.top;
         uClientAlreaHeight = rectClient.bottom;

         MoveWindow(g_hMDIClient, 0, uToolHeight, rectClient.right, uClientAlreaHeight - uStatusHeight - uToolHeight, TRUE);
      }
      break;
      case WM_CLOSE:
         DestroyWindow(hwnd);
      break;
      case WM_DESTROY:
         PostQuitMessage(0);
      break;
      default:
         return DefFrameProc(hwnd, g_hMDIClient, Message, wParam, lParam);
   }
   return 0;
}

LRESULT CALLBACK MDIChildWndProc(HWND hwnd, UINT Message, WPARAM wParam,
   LPARAM lParam)
{
   switch(Message)
   {
      case WM_CREATE:
      {
         char szFileName[MAX_PATH];
         HWND hEdit;

         hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "RichEdit20A", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
               ES_WANTRETURN | ES_AUTOVSCROLL | ES_NOHIDESEL |
               ES_DISABLENOSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, (HMENU)IDC_CHILD_EDIT, g_hInst, NULL);
         if(!hEdit)
            return -1;
         /* Use the largest supported signed 32-bit character range. */
         SendMessage(hEdit, EM_EXLIMITTEXT, 0, (LPARAM)0x7FFFFFFEUL);
 
        const INT ITEM_POINT_SIZE = 14;
        HDC hdc = GetDC(hwnd);
        INT nFontHeight = MulDiv(ITEM_POINT_SIZE, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        HFONT hFont = CreateFont(nFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("MS Shell Dlg"));
        SendMessage(hEdit, WM_SETFONT,
            (WPARAM)hFont, MAKELPARAM(TRUE, 0));
        ReleaseDC(hwnd, hdc); 

         GetWindowText(hwnd, szFileName, MAX_PATH);
         if(*szFileName != '[')
         {
            if(!LoadFile(hEdit, szFileName))
            {
               MessageBox(hwnd, "Couldn't Load File.", "Error...",
                  MB_OK | MB_ICONEXCLAMATION);
               return -1; //cancel window creation
            }
         }
      }
      break;
      case WM_SIZE:
         if(wParam != SIZE_MINIMIZED)
            MoveWindow(GetDlgItem(hwnd, IDC_CHILD_EDIT), 0, 0, LOWORD(lParam),
               HIWORD(lParam), TRUE);
      break;
      case WM_MDIACTIVATE:
      {
         /*
          * Only the newly active child updates the shared controls.
          * The old child's deactivation must not gray the frame menu
          * after the new child has already enabled it.
          */
         if(hwnd == (HWND)lParam)
         {
            HWND hFrame = GetParent(GetParent(hwnd));
            if(hFrame)
               UpdateDocumentControls(hFrame, hwnd);
         }
      }
      break;
      case WM_SETFOCUS:
         SetFocus(GetDlgItem(hwnd, IDC_CHILD_EDIT));
      break;
      case WM_COMMAND:
         switch(LOWORD(wParam))
         {
            case CM_FILE_SAVE:
            {
               char szFileName[MAX_PATH];

               GetWindowText(hwnd, szFileName, MAX_PATH);
               if(*szFileName != '[')
               {
                  if(!SaveFile(GetDlgItem(hwnd, IDC_CHILD_EDIT), szFileName))
                  {
                     MessageBox(hwnd, "Couldn't Save File.", "Error...",
                        MB_OK | MB_ICONEXCLAMATION);
                     return 0;
                  }
               }
               else
               {
                  PostMessage(hwnd, WM_COMMAND,
                     MAKEWPARAM(CM_FILE_SAVEAS, 0), 0);
               }
            }
            return 0;
            case CM_FILE_SAVEAS:
            {
               char szFileName[MAX_PATH];

               if(GetFileName(hwnd, szFileName, TRUE))
               {
                  if(!SaveFile(GetDlgItem(hwnd, IDC_CHILD_EDIT), szFileName))
                  {
                     MessageBox(hwnd, "Couldn't Save File.", "Error...",
                        MB_OK | MB_ICONEXCLAMATION);
                     return 0;
                  }
                  else
                  {
                     SetWindowText(hwnd, szFileName);
                  }
               }
            }
            return 0;
            case CM_EDIT_UNDO:
               SendDlgItemMessage(hwnd, IDC_CHILD_EDIT, EM_UNDO, 0, 0);
            break;
            case CM_EDIT_SELECTALL:
            {
               CHARRANGE range;
               range.cpMin = 0;
               range.cpMax = -1;
               SendDlgItemMessage(hwnd, IDC_CHILD_EDIT,
                  EM_EXSETSEL, 0, (LPARAM)&range);
            }
            break;
            case CM_EDIT_CUT:
               SendDlgItemMessage(hwnd, IDC_CHILD_EDIT, WM_CUT, 0, 0);
            break;
            case CM_EDIT_COPY:
               SendDlgItemMessage(hwnd, IDC_CHILD_EDIT, WM_COPY, 0, 0);
            break;
            case CM_EDIT_PASTE:
               SendDlgItemMessage(hwnd, IDC_CHILD_EDIT, WM_PASTE, 0, 0);
            break;
         }
      return 0;
   }
   return DefMDIChildProc(hwnd, Message, wParam, lParam);
}


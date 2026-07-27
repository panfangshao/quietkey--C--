// 资源 ID。数值本身无意义，但**不要改动已有项的值**——
// .rc 编译产物和代码是靠这些数字对上的。
#pragma once

#define IDI_APP 101

// 托盘右键菜单项。
#define IDM_SETTINGS 40001
#define IDM_TOGGLE   40002
#define IDM_QUIT     40003

// ---- 设置对话框 ----
#define IDD_SETTINGS          200
#define IDC_STATUS            1001
#define IDC_HOTKEY            1002
#define IDC_HOTKEY_HINT       1003
#define IDC_TARGET_TEXT       1004
#define IDC_TARGET_PICK       1005
#define IDC_TARGET_CLEAR      1006
#define IDC_S_GSMTC           1007
#define IDC_S_FOLLOW          1008
#define IDC_S_APPCOMMAND      1009
#define IDC_S_POSTMESSAGE     1010
#define IDC_S_FALLBACK        1011
#define IDC_SETTLE            1012
#define IDC_SETTLE_LABEL      1013
#define IDC_LAST_SUMMARY      1014
#define IDC_TRACE             1015
#define IDC_TEST_ONCE         1016
#define IDC_REFRESH_SESSIONS  1017
#define IDC_SESSIONS          1018
#define IDC_HIDE              1019
#define IDC_AUTOSTART         1020

// ---- 窗口拾取器对话框 ----
#define IDD_PICKER            201
#define IDC_PICKER_FILTER     1101
#define IDC_PICKER_REFRESH    1102
#define IDC_PICKER_MATCH_TITLE 1103
#define IDC_PICKER_LIST       1104
#define IDC_PICKER_HINT       1105

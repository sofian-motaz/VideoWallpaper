// resource.h
// Central definition of resource and control IDs used by resource.rc and
// App.cpp. Kept as plain #defines (Visual Studio resource editor convention)
// rather than an enum so resource.rc's RC compiler can include it directly.

#pragma once

// Icons
#define IDI_APPICON                 101
#define IDI_TRAYICON                102

// Version info (see resource.rc)
#define VER_FILEVERSION              1,0,0,0
#define VER_FILEVERSION_STR          "1.0.0.0\0"
#define VER_PRODUCTVERSION           1,0,0,0
#define VER_PRODUCTVERSION_STR       "1.0.0.0\0"

// Main window controls (created programmatically in App.cpp, IDs referenced
// in WM_COMMAND handling)
#define IDC_BTN_OPEN                 1001
#define IDC_BTN_PLAY                 1002
#define IDC_BTN_PAUSE                1003
#define IDC_BTN_STOP                 1004
#define IDC_CHK_LOOP                 1005
#define IDC_CHK_AUTOSTART            1006
#define IDC_RADIO_FIT                1007
#define IDC_RADIO_FILL                1008
#define IDC_RADIO_STRETCH             1009
#define IDC_STATIC_FILEPATH          1010
#define IDC_CHK_MUTE                 1011
#define IDC_STATIC_STATUS            1012
#define IDC_GROUP_SCALE              1013

// Timer IDs
#define TIMER_ID_PLAYER_TICK          1
#define TIMER_ID_WORKERW_WATCHDOG     2

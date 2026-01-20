/*
 * Workspace
 *
 * Copyright (c) 2025 amigazen project
 * Licensed under BSD 2-Clause License
 *
 * Workspace - Amiga Public Screen Manager
 * Creates one or more public screens (virtual desktops) that are clones
 * of the Workbench screen mode.
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <intuition/classusr.h>
#include <intuition/classes.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <libraries/commodities.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <dos/datetime.h>
#include <libraries/locale.h>
#include <datatypes/datatypes.h>
#include <datatypes/pictureclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <graphics/modeid.h>
#include <graphics/gfx.h> 
#include <proto/icon.h>
#include <proto/wb.h>
#include <proto/utility.h>
#include <proto/commodities.h>
#include <proto/locale.h>
#include <proto/datatypes.h>
#include <proto/input.h>
#include <proto/timer.h>
#include <clib/alib_protos.h>
#include <string.h>

/* Private datatypes functions not in proto/datatypes.h - need pragma declarations */
/* These are marked as ==private in datatypes.library SFD */
#pragma libcall DataTypesBase ObtainDTDrawInfoA 78 9802
#pragma libcall DataTypesBase DrawDTObjectA 7e A5432109809
#pragma libcall DataTypesBase ReleaseDTDrawInfo 84 9802

/* Function prototypes for private datatypes functions */
APTR ObtainDTDrawInfoA(Object *o, struct TagItem *attrs);
LONG DrawDTObjectA(struct RastPort *rp, Object *o, LONG x, LONG y, LONG w, LONG h, LONG th, LONG tv, struct TagItem *attrs);
VOID ReleaseDTDrawInfo(Object *o, APTR handle);

/* Tag definitions for SystemTagList() - from dos/dostags.h */
/* These are defined here in case dos/dostags.h is not available in the include path */
#ifndef SYS_Asynch
#define SYS_Dummy     (TAG_USER + 32)
#define SYS_Input     (SYS_Dummy + 1)   /* specifies the input filehandle */
#define SYS_Output    (SYS_Dummy + 2)   /* specifies the output filehandle */
#define SYS_Asynch    (SYS_Dummy + 3)   /* run asynch, close input/output on exit */
#define SYS_UserShell (SYS_Dummy + 4)   /* send to user shell instead of boot shell */
#define SYS_CmdStream (SYS_Dummy + 8)   /* command input stream, closed on exit (V47) */
#define SYS_InName    (SYS_Dummy + 9)   /* file name opened for input instead of file handle (V47) */
#define SYS_OutName   (SYS_Dummy + 10)  /* file name opened for output instead of file handle (V47) */
#define SYS_CmdName   (SYS_Dummy + 11)  /* file name for command input (V47) */
#endif

#ifndef NP_StackSize
#define NP_Dummy      (TAG_USER + 1000)
#define NP_StackSize  (NP_Dummy + 11)   /* stacksize for process - default 4000 */
#endif

#include <stdlib.h>
#include <stdarg.h>

/* Library base pointers */
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Library *IconBase;
extern struct Library *WorkbenchBase;
extern struct Library *UtilityBase;
extern struct LocaleBase *LocaleBase;
extern struct Library *DataTypesBase;
struct Library *CommoditiesBase = NULL;
struct GfxBase *GfxBase = NULL;
struct MsgPort *InputPort = NULL;
struct IOStdReq *InputIO = NULL;
struct Library *InputBase = NULL;
struct MsgPort *TimerPort = NULL;
struct timerequest *TimerIO = NULL;
struct Device *TimerBase = NULL;

/* Forward declarations */
VOID Cleanup(VOID);
BOOL InitializeLibraries(VOID);
BOOL InitializeCommodity(VOID);
VOID CleanupCommodity(VOID);
BOOL CreateWorkspaceScreen(VOID);
BOOL CloseWorkspaceScreen(VOID);
BOOL CreateBackdropWindow(VOID);
VOID CloseBackdropWindow(VOID);
BOOL CreateMenuStrip(VOID);
VOID FreeMenuStrip(VOID);
BOOL CreateShellConsole(VOID);
VOID CloseShellConsole(VOID);
BOOL LoadBackdropImage(STRPTR imagePath);
VOID FreeBackdropImage(VOID);
VOID UpdateScreenTitle(VOID);
VOID ProcessCommodityMessages(VOID);
STRPTR GetWorkspaceName(VOID);
VOID ParseToolTypes(VOID);
BOOL ParseCommandLine(VOID);
VOID HandleAboutMenu(VOID);
BOOL HandleCloseMenu(VOID);  /* Returns TRUE if quit should proceed, FALSE if blocked by visitors */
VOID HandleShellConsoleMenu(VOID);
VOID HandleWindowsMenu(ULONG itemNumber);  /* Handle Windows menu items */
WORD CheckWorkspaceVisitors(VOID);
VOID HandleSetAsDefaultMenu(struct MenuItem *menuItem);
VOID HandleDefaultPubScreenSubMenu(STRPTR screenName);
struct NewMenu *BuildDefaultPubScreenMenu(ULONG *menuCount);
BOOL GetToolType(STRPTR toolType, STRPTR defaultValue, STRPTR buffer, ULONG bufferSize);
BOOL InitializeTimer(VOID);
VOID CleanupTimer(VOID);
STRPTR FormatTimeDate(STRPTR buffer, ULONG bufferSize);
VOID HandleThemeMenu(ULONG itemNumber);  /* Handle Theme menu items */
BOOL ApplyTheme(ULONG themeIndex);  /* Apply color theme to screen */

/* Version string */
static const char *verstag = "$VER: Workspace 47.1 (1.1.2026)\n";
static const char *stack_cookie = "$STACK: 8192\n";
const long oslibversion = 47L;

/* Application state */
struct WorkspaceState {
    struct Screen *workspaceScreen;
    struct Window *backdropWindow;  /* Standard Intuition window */
    struct Window *shellWindow;     /* Separate backdrop window for shell console */
    struct Menu *menuStrip;
    CxObj *commodityBroker;
    CxObj *commoditySender;
    CxObj *commodityReceiver;
    struct MsgPort *commodityPort;
    struct Process *commodityProcess;
    STRPTR workspaceName;
    STRPTR pubName;  /* Command line pubname (default "Workspace.1") */
    STRPTR cxName;   /* Command line cxname (default "Workspace") */
    STRPTR cxPopKey; /* Command line CX_POPKEY hotkey string */
    CxObj *commodityFilter;  /* Filter object for hotkey */
    BOOL shellEnabled;
    STRPTR shellPath;
    STRPTR backdropImagePath;
    Object *backdropImageObj;
    APTR backdropDrawHandle;  /* Draw handle from ObtainDTDrawInfoA */
    struct BitMap *backdropBitmap;
    struct RastPort *backdropRastPort;
    struct Task *mainTask;
    BOOL quitFlag;
    ULONG instanceNumber;
    struct DrawInfo *drawInfo;
    ULONG lastMinute;
    BOOL commodityActive;  /* Track broker activation state */
    BOOL isDefaultScreen;  /* Track if this screen is set as default */
    struct RDArgs *rda;  /* ReadArgs result for cleanup */
    ULONG currentTheme;  /* Current color theme index (0 = Like Workbench) */
    STRPTR themeName;  /* Command line theme name */
    ULONG originalRGB[256 * 3]; /* Original palette captured when screen opened (GetRGB32 format) */
    ULONG numColors; /* Number of colors captured in originalRGB (<=256) */
    BOOL haveOriginalPalette; /* TRUE if originalRGB/numColors is valid */
};

static struct WorkspaceState wsState;

/* Tooltype defaults */
/* Note: Default uses WINDOW parameter - user can override with custom path */
/* For custom path, use %p for window pointer in hex format */
static STRPTR defaultShellPath = NULL;  /* Will use WINDOW parameter by default */
static BOOL defaultShellEnabled = FALSE;
static STRPTR defaultBackdropImage = NULL;

/* Color theme definitions */
/* Theme indices: 0=Like Workbench, 1=Dark Mode, 2=Sepia, 3=Blue, 4=Green */
#define THEME_LIKE_WORKBENCH 0
#define THEME_DARK_MODE 1
#define THEME_SEPIA 2
#define THEME_BLUE 3
#define THEME_GREEN 4
#define THEME_COUNT 5

/* Theme names for menu */
static const STRPTR themeNames[] = {
    "Like Workbench",
    "Dark Mode",
    "Sepia",
    "Blue",
    "Green",
    NULL
};

/* Main entry point */
int main(int argc, char *argv[])
{
    struct WBStartup *wbs = NULL;
    struct DiskObject *icon = NULL;
    BOOL fromWorkbench = FALSE;
    BOOL done = FALSE;  /* Declare at top of function for C89 compliance */
    
    Printf("Workspace: Starting application\n");
    
    /* Initialize state */
    memset(&wsState, 0, sizeof(struct WorkspaceState));
    wsState.instanceNumber = 1;
    wsState.quitFlag = FALSE;
    wsState.commodityActive = FALSE;
    wsState.isDefaultScreen = FALSE;
    wsState.mainTask = (struct Task *)FindTask(NULL);
    wsState.currentTheme = THEME_LIKE_WORKBENCH;  /* Default to Like Workbench */
    
    Printf("Workspace: State initialized\n");
    
    /* Check if running from Workbench */
    fromWorkbench = (argc == 0);
    {
        STRPTR workbenchStatus;
        if (fromWorkbench) {
            workbenchStatus = "YES";
        } else {
            workbenchStatus = "NO";
        }
        Printf("Workspace: Running from Workbench: %s\n", workbenchStatus);
    }
    
    if (fromWorkbench) {
        wbs = (struct WBStartup *)argv;
        if (wbs && wbs->sm_ArgList && wbs->sm_ArgList[0].wa_Name) {
            /* Get icon to read tooltypes */
            icon = GetDiskObject(wbs->sm_ArgList[0].wa_Name);
        }
    }
    
    /* Initialize libraries */
    Printf("Workspace: Initializing libraries...\n");
    if (!InitializeLibraries()) {
        Printf("Workspace: ERROR - Failed to initialize libraries\n");
        return RETURN_FAIL;
    }
    Printf("Workspace: Libraries initialized successfully\n");
    
    /* Initialize timer for time/date updates */
    Printf("Workspace: Initializing timer...\n");
    if (!InitializeTimer()) {
        Printf("Workspace: ERROR - Failed to initialize timer\n");
        Cleanup();
        return RETURN_FAIL;
    }
    Printf("Workspace: Timer initialized successfully\n");
    
    /* Parse command line arguments */
    Printf("Workspace: Parsing command line arguments...\n");
    if (!ParseCommandLine()) {
        Printf("Workspace: ERROR - Failed to parse command line arguments\n");
        if (icon) {
            FreeDiskObject(icon);
        }
        Cleanup();
        return RETURN_FAIL;
    }
    Printf("Workspace: Command line arguments parsed successfully\n");
    
    /* Parse tooltypes if icon available */
    if (icon) {
        Printf("Workspace: Parsing tooltypes...\n");
        /* Store tooltypes for parsing */
        ParseToolTypes();
        FreeDiskObject(icon);
    }
    
    /* Determine workspace instance number and name */
    wsState.workspaceName = GetWorkspaceName();
    Printf("Workspace: Workspace name: %s (instance %ld)\n", wsState.workspaceName, wsState.instanceNumber);
    
    /* Initialize commodity */
    Printf("Workspace: Initializing commodity...\n");
    if (!InitializeCommodity()) {
        Printf("Workspace: ERROR - Failed to initialize commodity\n");
        CleanupTimer();
        Cleanup();
        return RETURN_FAIL;
    }
    Printf("Workspace: Commodity initialized successfully\n");
    
    /* Create workspace screen */
    Printf("Workspace: Creating workspace screen...\n");
    if (!CreateWorkspaceScreen()) {
        Printf("Workspace: ERROR - Failed to create workspace screen\n");
        CleanupCommodity();
        Cleanup();
        return RETURN_FAIL;
    }
    Printf("Workspace: Workspace screen created successfully\n");
    
    /* Apply theme if specified (and not Like Workbench) */
    if (wsState.currentTheme != THEME_LIKE_WORKBENCH) {
        Printf("Workspace: Applying theme %lu: %s\n", wsState.currentTheme, themeNames[wsState.currentTheme]);
        if (!ApplyTheme(wsState.currentTheme)) {
            Printf("Workspace: WARNING - Failed to apply theme, continuing with default\n");
        }
    }
    
    /* Create backdrop window first - menu will be created after window is open */
    Printf("Workspace: Creating backdrop window...\n");
    if (!CreateBackdropWindow()) {
        Printf("Workspace: ERROR - Failed to create backdrop window\n");
        CloseWorkspaceScreen();
        CleanupCommodity();
        Cleanup();
        return RETURN_FAIL;
    }
    Printf("Workspace: Backdrop window created successfully\n");
    
    /* Create and attach menu strip AFTER window is open */
    Printf("Workspace: Creating menu strip...\n");
    if (!CreateMenuStrip()) {
        Printf("Workspace: ERROR - Failed to create menu strip\n");
        CloseBackdropWindow();
        CloseWorkspaceScreen();
        CleanupCommodity();
        Cleanup();
        return RETURN_FAIL;
    }
    Printf("Workspace: Menu strip created and attached successfully\n");
    
    /* Load backdrop image if specified and shell not enabled */
    if (!wsState.shellEnabled && wsState.backdropImagePath) {
        LoadBackdropImage(wsState.backdropImagePath);
    }
    
    /* Create shell console if enabled */
    if (wsState.shellEnabled) {
        CreateShellConsole();
    }
    
    /* Update screen title bar */
    UpdateScreenTitle();
    
    /* Initialize last minute for time update tracking */
    {
        struct timeval tv;
        struct ClockData cd;
        GetSysTime(&tv);
        Amiga2Date(tv.tv_secs, &cd);
        wsState.lastMinute = cd.min;
    }
    
    /* Main event loop */
    Printf("Workspace: Entering main event loop...\n");
    {
        LONG windowSigBit;
        if (wsState.backdropWindow && wsState.backdropWindow->UserPort) {
            windowSigBit = wsState.backdropWindow->UserPort->mp_SigBit;
        } else {
            windowSigBit = -1;
        }
        Printf("Workspace: Window UserPort signal bit: %ld\n", windowSigBit);
    }
    {
        LONG commoditySigBit;
        if (wsState.commodityPort) {
            commoditySigBit = wsState.commodityPort->mp_SigBit;
        } else {
            commoditySigBit = -1;
        }
        Printf("Workspace: Commodity port signal bit: %ld\n", commoditySigBit);
    }
    {
        LONG timerSigBit;
        if (TimerPort) {
            timerSigBit = TimerPort->mp_SigBit;
        } else {
            timerSigBit = -1;
        }
        Printf("Workspace: Timer port signal bit: %ld\n", timerSigBit);
    }
    
    event_loop_start:
    
    while (!wsState.quitFlag) {
        ULONG signals;
        ULONG windowSignal = 0;
        ULONG expectedSignals;
        ULONG timerSignal = 0;
        
        /* Reset done at start of each iteration */
        done = FALSE;
        
        /* Wait for messages - check if backdrop window exists */
        if (wsState.backdropWindow == NULL) {
            Printf("Workspace: ERROR - Backdrop window is NULL, exiting\n");
            wsState.quitFlag = TRUE;
            break;
        }
        
        /* Check if shell window was closed by console (when shell ends) */
        /* Only check if shell is actually enabled - avoid false positives during startup */
        if (wsState.shellEnabled && wsState.shellWindow != NULL) {
            if (wsState.shellWindow->UserPort == NULL) {
                /* Shell window was closed by console - shell has ended */
                Printf("Workspace: Shell console ended - shell window was closed by console\n");
                wsState.shellWindow = NULL;  /* Clear pointer */
                wsState.shellEnabled = FALSE;  /* Reset shell enabled flag */
                
                /* Re-enable "Open AmigaShell" menu item since shell is now closed */
                /* Menu 0, Item 3, Sub 0 - format: (menu << 16) | (item << 8) | sub */
                if (wsState.backdropWindow) {
                    OnMenu(wsState.backdropWindow, (0UL << 16) | (3UL << 8) | 0UL);
                    Printf("Workspace: Re-enabled 'Open AmigaShell' menu item\n");
                }
            }
        }
        
        /* Get window signal from UserPort */
        if (wsState.backdropWindow && wsState.backdropWindow->UserPort) {
            windowSignal = (1L << wsState.backdropWindow->UserPort->mp_SigBit);
        } else {
            Printf("Workspace: ERROR - Window or UserPort is NULL, exiting\n");
            wsState.quitFlag = TRUE;
            break;
        }
        
        /* Set up timer signal if timer is available */
        if (TimerPort) {
            timerSignal = (1L << TimerPort->mp_SigBit);
        }
        
        /* Calculate expected signal mask */
        {
            ULONG commoditySignal = 0;
            if (wsState.commodityPort) {
                commoditySignal = (1L << wsState.commodityPort->mp_SigBit);
            }
            expectedSignals = windowSignal | commoditySignal | timerSignal | SIGBREAKF_CTRL_C;
        }
        
        /* If no valid signals, we can't wait - exit */
        if (expectedSignals == SIGBREAKF_CTRL_C) {
            Printf("Workspace: ERROR - No valid signals to wait for, exiting\n");
            wsState.quitFlag = TRUE;
            break;
        }
        
        /* Wait for messages */
        signals = Wait(expectedSignals);
        
        /* Check for break signal */
        if (signals & SIGBREAKF_CTRL_C) {
            Printf("Workspace: Received CTRL-C break signal\n");
            wsState.quitFlag = TRUE;
            break;
        }
        
        /* Process timer messages (minute updates) */
        if (timerSignal && (signals & timerSignal)) {
            struct Message *timerMsg = GetMsg(TimerPort);
            if (timerMsg) {
                ReplyMsg(timerMsg);
                
                /* Check if minute has changed */
                {
                    struct timeval tv;
                    struct ClockData cd;
                    GetSysTime(&tv);
                    Amiga2Date(tv.tv_secs, &cd);
                    if (cd.min != wsState.lastMinute) {
                        wsState.lastMinute = cd.min;
                        UpdateScreenTitle();
                    }
                }
                
                /* Re-send timer request for next minute */
                if (TimerIO) {
                    TimerIO->tr_node.io_Command = TR_ADDREQUEST;
                    TimerIO->tr_node.io_Flags = 0;
                    TimerIO->tr_time.tv_secs = 60;
                    TimerIO->tr_time.tv_micro = 0;
                    SendIO((struct IORequest *)TimerIO);
                }
            }
        }
        
        /* Process commodity messages */
        if (wsState.commodityPort && (signals & (1L << wsState.commodityPort->mp_SigBit))) {
            ProcessCommodityMessages();
        }
        
        /* Process window messages using standard Intuition message handling */
        if (windowSignal && (signals & windowSignal) && wsState.backdropWindow != NULL) {
            struct IntuiMessage *imsg;
            struct Message *msg;
            struct MsgPort *userPort;
            
            /* Check if window is still valid - if UserPort is NULL, window was closed */
            if (wsState.backdropWindow) {
                userPort = wsState.backdropWindow->UserPort;
            } else {
                userPort = NULL;
            }
            if (userPort == NULL) {
                /* Window was closed (likely by shell console) - recreate if shell was enabled */
                Printf("Workspace: Window UserPort is NULL - window was closed\n");
                if (wsState.shellEnabled) {
                    Printf("Workspace: Shell console ended - window was closed by console, recreating backdrop window\n");
                    wsState.shellEnabled = FALSE;  /* Reset shell enabled flag */
                    wsState.backdropWindow = NULL;  /* Clear invalid pointer */
                    if (!CreateBackdropWindow()) {
                        Printf("Workspace: ERROR - Failed to recreate backdrop window after shell ended\n");
                        wsState.quitFlag = TRUE;
                        break;
                    }
                    /* Recreate menu on new window */
                    if (!CreateMenuStrip()) {
                        Printf("Workspace: ERROR - Failed to recreate menu after shell ended\n");
                        wsState.quitFlag = TRUE;
                        break;
                    }
                    /* Activate the new window */
                    if (wsState.backdropWindow) {
                        ActivateWindow(wsState.backdropWindow);
                    }
                    Printf("Workspace: Backdrop window recreated successfully after shell ended\n");
                    continue;  /* Restart loop with new window */
                } else {
                    Printf("Workspace: ERROR - Window closed but shell not enabled, exiting\n");
                    wsState.quitFlag = TRUE;
                    break;
                }
            }
            
            /* Get all messages from window's UserPort */
            while (wsState.backdropWindow != NULL && (msg = GetMsg(userPort)) != NULL) {
                imsg = (struct IntuiMessage *)msg;
                
                switch (imsg->Class) {
                    case IDCMP_CLOSEWINDOW:
                        wsState.quitFlag = TRUE;
                        done = TRUE;
                        ReplyMsg(msg);
                        break;
                    
                    case IDCMP_MENUPICK:
                        {
                            struct MenuItem *item;
                            UWORD menuCode = imsg->Code;
                            
                            Printf("Workspace: IDCMP_MENUPICK received, menuCode=0x%x\n", menuCode);
                            
                            while (menuCode != MENUNULL) {
                                item = ItemAddress(wsState.menuStrip, menuCode);
                                if (item) {
                                    Printf("Workspace: Found menu item at 0x%lx\n", (ULONG)item);
                                    /* Get menu/item/sub numbers from UserData (GadTools stores it there) */
                                    if (GTMENUITEM_USERDATA(item)) {
                                        ULONG userData = (ULONG)GTMENUITEM_USERDATA(item);
                                        ULONG menuNumber = (userData >> 16) & 0xFF;
                                        ULONG itemNumber = (userData >> 8) & 0xFF;
                                        ULONG subNumber = userData & 0xFF;
                                        
                                        Printf("Workspace: Menu item - menuNumber=%lu, itemNumber=%lu, subNumber=%lu, Flags=0x%x\n",
                                               menuNumber, itemNumber, subNumber, (UWORD)item->Flags);
                                        
                                        if (menuNumber == 0) {
                                            if (itemNumber == 0) {
                                                /* Submenu items under "Default PubScreen" */
                                                if (subNumber == 0) {
                                                    /* "Workbench" sub-item */
                                                    Printf("Workspace: Handling 'Workbench' submenu item\n");
                                                    HandleDefaultPubScreenSubMenu(NULL);
                                                } else {
                                                    /* Workspace.n sub-item - get screen name from menu item text */
                                                    STRPTR screenName = NULL;
                                                    if (item->ItemFill && (item->Flags & ITEMTEXT)) {
                                                        screenName = ((struct IntuiText *)item->ItemFill)->IText;
                                                        if (screenName) {
                                                            Printf("Workspace: Handling Workspace screen submenu item: %s\n", screenName);
                                                            HandleDefaultPubScreenSubMenu(screenName);
                                                        }
                                                    }
                                                }
                                            } else {
                                                /* Regular menu items (not sub-items) */
                                                switch (itemNumber) {
                                                    case 1:  /* About */
                                                        HandleAboutMenu();
                                                        break;
                                                    
                                                    case 2:  /* Quit */
                                                        /* Only set done if HandleCloseMenu allows quit */
                                                        Printf("Workspace: Quit menu item selected - calling HandleCloseMenu()\n");
                                                        {
                                                            STRPTR doneStr;
                                                            STRPTR quitFlagStr;
                                                            if (done) {
                                                                doneStr = "TRUE";
                                                            } else {
                                                                doneStr = "FALSE";
                                                            }
                                                            if (wsState.quitFlag) {
                                                                quitFlagStr = "TRUE";
                                                            } else {
                                                                quitFlagStr = "FALSE";
                                                            }
                                                            Printf("Workspace: BEFORE HandleCloseMenu - done=%s, quitFlag=%s\n", 
                                                                   doneStr, quitFlagStr);
                                                        }
                                                        {
                                                            BOOL allowQuit = HandleCloseMenu();
                                                            {
                                                                STRPTR returnStr;
                                                                if (allowQuit) {
                                                                    returnStr = "TRUE";
                                                                } else {
                                                                    returnStr = "FALSE";
                                                                }
                                                                Printf("Workspace: HandleCloseMenu returned %s\n", returnStr);
                                                            }
                                                            if (allowQuit) {
                                                                Printf("Workspace: Setting done=TRUE because HandleCloseMenu returned TRUE\n");
                                                                done = TRUE;
                                                            } else {
                                                                Printf("Workspace: NOT setting done - HandleCloseMenu returned FALSE\n");
                                                                {
                                                                    STRPTR doneStr;
                                                                    STRPTR quitFlagStr;
                                                                    if (done) {
                                                                        doneStr = "TRUE";
                                                                    } else {
                                                                        doneStr = "FALSE";
                                                                    }
                                                                    if (wsState.quitFlag) {
                                                                        quitFlagStr = "TRUE";
                                                                    } else {
                                                                        quitFlagStr = "FALSE";
                                                                    }
                                                                    Printf("Workspace: done remains %s, quitFlag is %s\n", 
                                                                           doneStr, quitFlagStr);
                                                                }
                                                            }
                                                        }
                                                        {
                                                            STRPTR doneStr;
                                                            STRPTR quitFlagStr;
                                                            if (done) {
                                                                doneStr = "TRUE";
                                                            } else {
                                                                doneStr = "FALSE";
                                                            }
                                                            if (wsState.quitFlag) {
                                                                quitFlagStr = "TRUE";
                                                            } else {
                                                                quitFlagStr = "FALSE";
                                                            }
                                                            Printf("Workspace: AFTER HandleCloseMenu - done=%s, quitFlag=%s\n", 
                                                                   doneStr, quitFlagStr);
                                                        }
                                                        break;
                                                    
                                                    case 3:  /* Shell Console */
                                                        HandleShellConsoleMenu();
                                                        break;
                                                    
                                                    default:
                                                        Printf("Workspace: Unknown menu item number: %lu\n", itemNumber);
                                                        break;
                                                }
                                            }
                                        } else if (menuNumber == 1) {
                                            /* Windows menu */
                                            HandleWindowsMenu(itemNumber);
                                        } else if (menuNumber == 2) {
                                            /* Prefs menu */
                                            HandleThemeMenu(subNumber);
                                        } else {
                                            Printf("Workspace: Unknown menu number: %lu\n", menuNumber);
                                        }
                                    } else {
                                        Printf("Workspace: WARNING - Menu item has no UserData\n");
                                    }
                                    menuCode = item->NextSelect;
                                } else {
                                    Printf("Workspace: WARNING - ItemAddress returned NULL for menuCode=0x%x\n", menuCode);
                                    break;
                                }
                            }
                        }
                        ReplyMsg(msg);
                        {
                            STRPTR doneStr;
                            STRPTR quitFlagStr;
                            if (done) {
                                doneStr = "TRUE";
                            } else {
                                doneStr = "FALSE";
                            }
                            if (wsState.quitFlag) {
                                quitFlagStr = "TRUE";
                            } else {
                                quitFlagStr = "FALSE";
                            }
                            Printf("Workspace: After ReplyMsg for IDCMP_MENUPICK - done=%s, quitFlag=%s\n", 
                                   doneStr, quitFlagStr);
                        }
                        if (done) {
                            Printf("Workspace: Breaking from message processing loop because done=TRUE\n");
                            break;
                        }
                        Printf("Workspace: Continuing message processing loop (done=FALSE)\n");
                        break;
                    
                    default:
                        ReplyMsg(msg);
                        break;
                }
                
                if (done) {
                    break;
                }
            }
        }
        
        {
            STRPTR doneStr;
            STRPTR quitFlagStr;
            if (done) {
                doneStr = "TRUE";
            } else {
                doneStr = "FALSE";
            }
            if (wsState.quitFlag) {
                quitFlagStr = "TRUE";
            } else {
                quitFlagStr = "FALSE";
            }
            Printf("Workspace: End of event loop iteration - done=%s, quitFlag=%s\n", 
                   doneStr, quitFlagStr);
        }
        if (done) {
            Printf("Workspace: Breaking from main event loop because done=TRUE\n");
            break;
        }
        {
            STRPTR quitFlagStr;
            if (wsState.quitFlag) {
                quitFlagStr = "TRUE";
            } else {
                quitFlagStr = "FALSE";
            }
            Printf("Workspace: Continuing main event loop (done=FALSE, quitFlag=%s)\n", 
                   quitFlagStr);
        }
    }
    
    {
        STRPTR doneStr;
        STRPTR quitFlagStr;
        if (done) {
            doneStr = "TRUE";
        } else {
            doneStr = "FALSE";
        }
        if (wsState.quitFlag) {
            quitFlagStr = "TRUE";
        } else {
            quitFlagStr = "FALSE";
        }
        Printf("Workspace: Event loop exited - done=%s, quitFlag=%s\n", 
               doneStr, quitFlagStr);
    }
    
    /* Only run cleanup if quitFlag was set (user actually wants to quit) */
    /* If HandleCloseMenu returned FALSE due to visitors, quitFlag should be FALSE */
    if (!wsState.quitFlag) {
        Printf("Workspace: Event loop exited but quitFlag is FALSE - skipping cleanup\n");
        Printf("Workspace: App will continue running\n");
        return RETURN_OK;  /* Exit main() but don't cleanup */
    }
    
    Printf("Workspace: quitFlag is TRUE - checking visitor count BEFORE closing anything\n");
    
    /* CRITICAL: Check visitor count FIRST, before closing the backdrop window */
    /* Note: Backdrop window IS counted as a visitor window */
    /* Expected: visitor count should be 1 (only backdrop window is open) */
    {
        WORD visitorCount = CheckWorkspaceVisitors();
        Printf("Workspace: Visitor count: %ld\n", (LONG)visitorCount);
        
        if (visitorCount == 0) {
            /* ERROR: Should have at least the backdrop window (count = 1) */
            Printf("Workspace: ERROR - Visitor count is 0, expected at least 1 (backdrop window)\n");
            Printf("Workspace: Something is wrong - aborting cleanup\n");
            wsState.quitFlag = FALSE;
            done = FALSE;
            goto event_loop_start;
        } else if (visitorCount == 1) {
            /* Perfect: Only the backdrop window is open (backdrop counts as 1 visitor) */
            Printf("Workspace: Only backdrop window is open (count=1) - proceeding with cleanup\n");
            
            /* Cleanup - order is important */
            if (wsState.shellEnabled) {
                CloseShellConsole();
            }
            
            FreeBackdropImage();
            /* CloseBackdropWindow will call ClearMenuStrip, so do it before FreeMenuStrip */
            CloseBackdropWindow();
            /* Now free the menu structure */
            FreeMenuStrip();
            /* Close screen - should succeed since only backdrop window was open */
            if (!CloseWorkspaceScreen()) {
                Printf("Workspace: ERROR - CloseWorkspaceScreen failed unexpectedly\n");
                Printf("Workspace: Cannot exit - aborting cleanup, app will continue running\n");
                wsState.quitFlag = FALSE;
                done = FALSE;
                goto event_loop_start;
            }
            /* Cleanup successful - continue with remaining cleanup */
        } else {
            /* visitorCount > 1: Other windows are open (backdrop + others) - show requester and abort */
            /* Subtract 1 for the backdrop window when showing message */
            {
                WORD otherWindows = visitorCount - 1;
                Printf("Workspace: %ld visitor window(s) total (1 backdrop + %ld others)\n", (LONG)visitorCount, (LONG)otherWindows);
            }
            Printf("Workspace: Showing requester and aborting cleanup - app will continue running\n");
            
            /* Show requester using backdrop window (which is still open) */
            {
                struct EasyStruct es;
                STRPTR titleStr = "Cannot Exit Workspace";
                char textBuffer[256];
                STRPTR textStr;
                STRPTR okStr = "OK";
                struct Window *reqWindow;
                WORD otherWindows;
                
                /* Format message with visitor count (subtract 1 for backdrop window) */
                otherWindows = visitorCount - 1;
                if (otherWindows == 1) {
                    SNPrintf(textBuffer, sizeof(textBuffer), 
                             "Cannot exit Workspace.\n\nThere is 1 window open on a Workspace screen.\n\nPlease close all windows and try again.");
                } else {
                    SNPrintf(textBuffer, sizeof(textBuffer), 
                             "Cannot exit Workspace.\n\nThere are %d windows open on Workspace screens.\n\nPlease close all windows and try again.", 
                             (int)otherWindows);
                }
                textStr = textBuffer;
                
                es.es_StructSize = sizeof(struct EasyStruct);
                es.es_Flags = 0;
                es.es_Title = titleStr;
                es.es_TextFormat = textStr;
                es.es_GadgetFormat = okStr;
                
                /* Ensure our screen is in front */
                if (wsState.workspaceScreen) {
                    ScreenToFront(wsState.workspaceScreen);
                }
                reqWindow = wsState.backdropWindow;
                if (reqWindow == NULL || reqWindow->WScreen != wsState.workspaceScreen) {
                    if (wsState.workspaceScreen && wsState.workspaceScreen->FirstWindow) {
                        reqWindow = wsState.workspaceScreen->FirstWindow;
                    }
                }
                EasyRequestArgs(reqWindow, &es, NULL, NULL);
            }
            
            /* Reset quitFlag and restart event loop - do NOT close anything */
            Printf("Workspace: Resetting quitFlag - app will continue running\n");
            wsState.quitFlag = FALSE;
            done = FALSE;
            /* Restart the event loop */
            Printf("Workspace: Restarting event loop\n");
            goto event_loop_start;
        }
    }
    
    /* If we get here, cleanup was successful (visitorCount == 1 case) */
    /* Continue with remaining cleanup */
    CleanupCommodity();
    CleanupTimer();
    
    /* Free command line arguments */
    if (wsState.rda) {
        FreeArgs(wsState.rda);
        wsState.rda = NULL;
    }
    
    Cleanup();
    
    return RETURN_OK;
}

/* Initialize required libraries */
BOOL InitializeLibraries(VOID)
{
    Printf("Workspace: Opening intuition.library...\n");
    /* Open intuition.library */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 40L);
    if (IntuitionBase == NULL) {
        Printf("Workspace: ERROR - Failed to open intuition.library\n");
        return FALSE;
    }
    Printf("Workspace: intuition.library opened successfully\n");
    
    /* Open utility.library */
    UtilityBase = OpenLibrary("utility.library", 40L);
    if (UtilityBase == NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    /* Open graphics.library */
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 40L);
    if (GfxBase == NULL) {
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    /* Open icon.library */
    if (!(IconBase = OpenLibrary("icon.library", 40L))) {
        CloseLibrary(GfxBase);
        GfxBase = NULL;
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    /* Open workbench.library */
    if (!(WorkbenchBase = OpenLibrary("workbench.library", 40L))) {
        CloseLibrary(IconBase);
        IconBase = NULL;
        CloseLibrary(GfxBase);
        GfxBase = NULL;
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    /* Open locale.library (optional) */
    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 40L);
    
    /* Open datatypes.library (optional, for backdrop images) */
    DataTypesBase = OpenLibrary("datatypes.library", 40L);
        
    /* Open commodities.library */
    CommoditiesBase = OpenLibrary("commodities.library", 40L);
    if (CommoditiesBase == NULL) {
        /* Commodities not available, but continue anyway */
    }
    
    /* Open input.device for qualifier checking (optional) */
    InputPort = CreateMsgPort();
    if (InputPort != NULL) {
        InputIO = (struct IOStdReq *)CreateIORequest(InputPort, sizeof(struct IOStdReq));
        if (InputIO != NULL) {
            if (OpenDevice("input.device", 0, (struct IORequest *)InputIO, 0) == 0) {
                InputBase = (struct Library *)InputIO->io_Device;
            } else {
                DeleteIORequest((struct IORequest *)InputIO);
                InputIO = NULL;
                DeleteMsgPort(InputPort);
                InputPort = NULL;
            }
        } else {
            DeleteMsgPort(InputPort);
            InputPort = NULL;
        }
    }
    
    return TRUE;
}

/* Initialize commodity */
BOOL InitializeCommodity(VOID)
{
    struct NewBroker nb;
    CxObj *broker;
    LONG brokerError;
    UBYTE commodityName[64];
    
    /* Initialize to NULL in case of early return */
    wsState.commodityBroker = NULL;
    wsState.commodityPort = NULL;
    wsState.commoditySender = NULL;
    wsState.commodityReceiver = NULL;
    
    /* Open commodities.library - requires OS 3.0+ */
    /* Not critical - can run without commodity support */
    if (CommoditiesBase == NULL) {
        Printf("Workspace: WARNING - Commodities library not available, continuing without commodity support\n");
        return TRUE; /* Non-fatal - continue without commodity support */
    }
    
    Printf("Workspace: Creating commodity message port...\n");
    /* Create message port for broker */
    wsState.commodityPort = CreateMsgPort();
    if (wsState.commodityPort == NULL) {
        Printf("Workspace: WARNING - Failed to create commodity message port, continuing without commodity support\n");
        return TRUE; /* Non-fatal */
    }
    Printf("Workspace: Commodity message port created (signal bit: %ld)\n", wsState.commodityPort->mp_SigBit);
    
    /* Commodity name from command line or default "Workspace" */
    if (wsState.cxName && wsState.cxName[0] != '\0') {
        SNPrintf(commodityName, sizeof(commodityName), "%s", wsState.cxName);
    } else {
        SNPrintf(commodityName, sizeof(commodityName), "Workspace");
    }
    
    /* Initialize NewBroker structure */
    nb.nb_Version = NB_VERSION;
    nb.nb_Name = commodityName;
    nb.nb_Title = commodityName;
    nb.nb_Descr = "Workspace - Public Screen Manager";
    nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY; /* Unique name, notify on duplicate */
    nb.nb_Flags = COF_SHOW_HIDE; /* Support show/hide commands */
    nb.nb_Pri = 0; /* Normal priority */
    nb.nb_Port = wsState.commodityPort;
    nb.nb_ReservedChannel = 0;
    
    Printf("Workspace: Creating commodity broker (name: %s)...\n", nb.nb_Name);
    /* Create broker */
    broker = CxBroker(&nb, &brokerError);
    if (broker == NULL) {
        /* Check specific error code */
        switch (brokerError) {
            case CBERR_DUP:
                Printf("Workspace: WARNING - Broker name '%s' already exists, continuing without commodity support\n", nb.nb_Name);
                break;
            case CBERR_SYSERR:
                Printf("Workspace: WARNING - System error creating broker (low memory), continuing without commodity support\n");
                break;
            case CBERR_VERSION:
                Printf("Workspace: WARNING - Unknown broker version, continuing without commodity support\n");
                break;
            default:
                Printf("Workspace: WARNING - Failed to create broker (error: %ld), continuing without commodity support\n", brokerError);
                break;
        }
        /* Failed to create broker - cleanup and continue */
        DeleteMsgPort(wsState.commodityPort);
        wsState.commodityPort = NULL;
        return TRUE; /* Non-fatal - continue without commodity support */
    }
    
    /* Check for object errors */
    {
        LONG objError = CxObjError(broker);
        if (objError) {
            Printf("Workspace: WARNING - Broker created but has errors (0x%lx), continuing without commodity support\n", (ULONG)objError);
            /* Broker created but has errors - cleanup */
            DeleteCxObjAll(broker);
            broker = NULL;
            DeleteMsgPort(wsState.commodityPort);
            wsState.commodityPort = NULL;
            return TRUE; /* Non-fatal */
        }
    }
    
    wsState.commodityBroker = broker;
    wsState.commoditySender = NULL;
    wsState.commodityReceiver = NULL;
    wsState.commodityFilter = NULL;
    
    /* Create filter for hotkey if CX_POPKEY is specified */
    if (wsState.cxPopKey && wsState.cxPopKey[0] != '\0') {
        Printf("Workspace: Creating filter for hotkey: %s\n", wsState.cxPopKey);
        wsState.commodityFilter = CxFilter(wsState.cxPopKey);
        if (wsState.commodityFilter) {
            LONG filterError = CxObjError(wsState.commodityFilter);
            if (filterError) {
                Printf("Workspace: WARNING - Filter has errors (0x%lx)\n", (ULONG)filterError);
                DeleteCxObj(wsState.commodityFilter);
                wsState.commodityFilter = NULL;
            } else {
                /* Attach filter to broker */
                AttachCxObj(broker, wsState.commodityFilter);
                /* Create sender to receive filtered events */
                wsState.commoditySender = CxSender(wsState.commodityPort, 1);
                if (wsState.commoditySender) {
                    AttachCxObj(wsState.commodityFilter, wsState.commoditySender);
                    Printf("Workspace: Hotkey filter and sender created successfully\n");
                } else {
                    Printf("Workspace: WARNING - Failed to create sender for hotkey filter\n");
                }
            }
        } else {
            Printf("Workspace: WARNING - Failed to create filter for hotkey\n");
        }
    }
    
    Printf("Workspace: Activating commodity broker...\n");
    /* Activate the broker (brokers are created inactive) */
    /* ActivateCxObj returns previous activation state: 0 = was inactive, non-zero = was active */
    {
        LONG prevState = ActivateCxObj(broker, TRUE);
        if (prevState == 0) {
            /* Was inactive, now activated - this is expected */
            wsState.commodityActive = TRUE;
            Printf("Workspace: Commodity broker activated successfully\n");
        } else {
            /* Was already active - unexpected but not an error */
            wsState.commodityActive = TRUE;
            Printf("Workspace: WARNING - Broker was already active (unexpected)\n");
        }
    }
    return TRUE;
}

/* Cleanup commodity */
VOID CleanupCommodity(VOID)
{
    if (CommoditiesBase == NULL) {
        return;
    }
    
    if (wsState.commodityBroker) {
        /* Deactivate the broker if active */
        if (wsState.commodityActive) {
            ActivateCxObj(wsState.commodityBroker, FALSE);
            wsState.commodityActive = FALSE;
        }
        /* Delete the commodity object (this will also delete filter and sender) */
        DeleteCxObjAll(wsState.commodityBroker);
        wsState.commodityBroker = NULL;
        wsState.commoditySender = NULL;
        wsState.commodityReceiver = NULL;
        wsState.commodityFilter = NULL;
    }
    
    if (wsState.commodityPort) {
        DeleteMsgPort(wsState.commodityPort);
        wsState.commodityPort = NULL;
    }
}

/* Get workspace name (from command line or default "Workspace.1") */
STRPTR GetWorkspaceName(VOID)
{
    static UBYTE nameBuffer[64];
    
    /* Use command line pubname if provided, otherwise default to "Workspace.1" */
    if (wsState.pubName && wsState.pubName[0] != '\0') {
        SNPrintf(nameBuffer, sizeof(nameBuffer), "%s", wsState.pubName);
    } else {
        SNPrintf(nameBuffer, sizeof(nameBuffer), "Workspace.1");
    }
    wsState.instanceNumber = 1;
    return nameBuffer;
}

/* Create workspace screen (clone of Workbench) */
BOOL CreateWorkspaceScreen(VOID)
{
    struct Screen *newScreen;
    LONG screenError = 0;
    ULONG numColors;
    
    /* Create screen using SA_LikeWorkbench (like example.c) */
    Printf("Workspace: Opening screen with SA_LikeWorkbench...\n");
    newScreen = OpenScreenTags(NULL,
        SA_Type, PUBLICSCREEN,
        SA_PubName, wsState.workspaceName,
        SA_Title, wsState.workspaceName,
        SA_LikeWorkbench, TRUE,
        SA_ErrorCode, &screenError,
        TAG_DONE);
    
    if (newScreen == NULL) {
        /* Check specific error code */
        switch (screenError) {
            case OSERR_PUBNOTUNIQUE:
                Printf("Workspace: ERROR - Public screen name '%s' already in use\n", wsState.workspaceName);
                break;
            case OSERR_NOMEM:
                Printf("Workspace: ERROR - Out of memory (normal memory)\n");
                break;
            case OSERR_NOCHIPMEM:
                Printf("Workspace: ERROR - Out of memory (chip memory)\n");
                break;
            case OSERR_NOMONITOR:
                Printf("Workspace: ERROR - Monitor not available\n");
                break;
            case OSERR_NOCHIPS:
                Printf("Workspace: ERROR - Newer custom chips required\n");
                break;
            case OSERR_UNKNOWNMODE:
                Printf("Workspace: ERROR - Unknown display mode\n");
                break;
            case OSERR_TOODEEP:
                Printf("Workspace: ERROR - Screen too deep for hardware\n");
                break;
            case OSERR_ATTACHFAIL:
                Printf("Workspace: ERROR - Failed to attach screens\n");
                break;
            case OSERR_NOTAVAILABLE:
                Printf("Workspace: ERROR - Mode not available\n");
                break;
            case OSERR_NORTGBITMAP:
                Printf("Workspace: ERROR - Could not allocate RTG bitmap\n");
                break;
            default:
                Printf("Workspace: ERROR - Failed to open screen (error code: %ld)\n", screenError);
                break;
        }
        return FALSE;
    }
    
    Printf("Workspace: Screen opened successfully\n");
    /* Use Screen->RastPort.BitMap instead of Screen->BitMap (recommended in docs) */
    Printf("Workspace: Screen->Width=%ld, Screen->Height=%ld\n",
           (LONG)newScreen->Width, (LONG)newScreen->Height);
    Printf("Workspace: ViewPort.DWidth=%ld, ViewPort.DHeight=%ld\n",
           (LONG)newScreen->ViewPort.DWidth, (LONG)newScreen->ViewPort.DHeight);
    
    /* Get draw info for our screen */
    wsState.drawInfo = GetScreenDrawInfo(newScreen);
    
    /* Make screen public - PubScreenStatus(screen, 0) makes it public */
    /* According to docs: Returns 0 in bit 0 if screen wasn't public (success when making public) */
    /* So bit 0 = 0 means SUCCESS when making public, bit 0 = 1 means was already public or error */
    {
        UWORD statusResult = PubScreenStatus(newScreen, 0);
        if ((statusResult & 0x0001) == 0) {
            /* Bit 0 = 0 means screen wasn't public before, so we successfully made it public */
            Printf("Workspace: Screen is now public\n");
        } else {
            /* Bit 0 = 1 means screen was already public or error occurred */
            Printf("Workspace: WARNING - Screen was already public or error (status: 0x%x)\n", statusResult);
            /* Continue anyway */
        }
    }
    
    /* Check dimensions after making public */
        Printf("Workspace: After making public - Width: %ld, Height: %ld\n",
               (LONG)newScreen->Width, (LONG)newScreen->Height);
    
    wsState.workspaceScreen = newScreen;

    /* Capture original palette immediately after opening the screen */
    wsState.haveOriginalPalette = FALSE;
    wsState.numColors = 0;
    numColors = 1UL << newScreen->BitMap.Depth;
    if (numColors > 256) {
        numColors = 256;
    }
    if (newScreen->ViewPort.ColorMap != NULL && numColors > 0) {
        GetRGB32(newScreen->ViewPort.ColorMap, 0, numColors, wsState.originalRGB);
        wsState.numColors = numColors;
        wsState.haveOriginalPalette = TRUE;
    }
    
    return TRUE;
}

/* Close workspace screen - shows warning if visitors exist, then returns FALSE */
/* Returns TRUE if screen was closed successfully, FALSE if visitors prevent closing */
BOOL CloseWorkspaceScreen(VOID)
{
    struct List *pubScreenList = NULL;
    struct PubScreenNode *psn = NULL;
    WORD visitorCount = 0;
    BOOL closeSucceeded = FALSE;
    struct EasyStruct es;
    STRPTR titleStr;
    STRPTR textStr;
    STRPTR okStr;
    
    if (!wsState.workspaceScreen) {
        return TRUE;  /* No screen to close - consider it successful */
    }
    
    /* Check for visitor windows once - don't loop */
    {
        visitorCount = 0;
        
        /* Lock public screen list to check visitor count for all Workspace screens */
        pubScreenList = LockPubScreenList();
        if (pubScreenList) {
            /* Iterate through all public screens - correct Exec list iteration */
            psn = (struct PubScreenNode *)pubScreenList->lh_Head;
            while (psn && psn->psn_Node.ln_Succ != (struct Node *)&pubScreenList->lh_Tail) {
                /* Check if this is a Workspace screen (starts with "Workspace.") */
                if (psn->psn_Node.ln_Name && 
                    strncmp(psn->psn_Node.ln_Name, "Workspace.", 10) == 0) {
                    visitorCount += (WORD)psn->psn_VisitorCount;
                    Printf("Workspace: Screen '%s' has %ld visitor windows\n", 
                           psn->psn_Node.ln_Name, (LONG)psn->psn_VisitorCount);
                }
                psn = (struct PubScreenNode *)psn->psn_Node.ln_Succ;
            }
            UnlockPubScreenList();
        }
        
        Printf("Workspace: Total visitor windows on all Workspace screens: %d\n", visitorCount);
        
        /* Check if we can close - no visitors allowed */
        if (visitorCount > 0) {
            /* Show EasyRequest dialog warning user */
            titleStr = "Cannot Exit Workspace";
            textStr = "Cannot exit Workspace.\n\nAll windows on Workspace screens must be closed before exiting.\n\nPlease close all windows and try again.";
            okStr = "OK";
            
            es.es_StructSize = sizeof(struct EasyStruct);
            es.es_Flags = 0;
            es.es_Title = titleStr;
            es.es_TextFormat = textStr;
            es.es_GadgetFormat = okStr;
            
            /* Ensure our screen is in front and use a valid window on our screen */
            if (wsState.workspaceScreen) {
                ScreenToFront(wsState.workspaceScreen);
            }
            /* Use backdrop window if available and valid, otherwise try to use screen's first window */
            {
                struct Window *reqWindow = wsState.backdropWindow;
                /* If backdrop window is NULL or not on our screen, try to find another window */
                if (reqWindow == NULL || reqWindow->WScreen != wsState.workspaceScreen) {
                    /* Try to use the screen's first window if available */
                    if (wsState.workspaceScreen && wsState.workspaceScreen->FirstWindow) {
                        reqWindow = wsState.workspaceScreen->FirstWindow;
                    }
                }
                EasyRequestArgs(reqWindow, &es, NULL, NULL);
            }
        Printf("Workspace: Cannot close - %ld visitor windows still open, user must close them\n", (LONG)visitorCount);
        /* Return FALSE - screen was not closed */
        return FALSE;
        }
        
        /* No visitors - try to close screen */
        /* Take screen private before closing */
        {
            UWORD statusResult = PubScreenStatus(wsState.workspaceScreen, PSNF_PRIVATE);
            if ((statusResult & 0x0001) == 0) {
                /* Bit 0 = 0 means can't make private (visitors are open) */
                Printf("Workspace: WARNING - Could not make screen private (status: 0x%x), may have visitors\n", statusResult);
                /* Show warning and return */
                titleStr = "Cannot Close Screen";
                textStr = "Cannot close Workspace screen.\n\nAll windows on this screen must be closed before exiting.\n\nPlease close all windows and try again.";
                okStr = "OK";
                
                es.es_StructSize = sizeof(struct EasyStruct);
                es.es_Flags = 0;
                es.es_Title = titleStr;
                es.es_TextFormat = textStr;
                es.es_GadgetFormat = okStr;
                
                if (wsState.workspaceScreen) {
                    ScreenToFront(wsState.workspaceScreen);
                }
                {
                    struct Window *reqWindow = wsState.backdropWindow;
                    if (reqWindow == NULL || reqWindow->WScreen != wsState.workspaceScreen) {
                        if (wsState.workspaceScreen && wsState.workspaceScreen->FirstWindow) {
                            reqWindow = wsState.workspaceScreen->FirstWindow;
                        }
                    }
                    EasyRequestArgs(reqWindow, &es, NULL, NULL);
                }
            Printf("Workspace: Cannot make screen private, user must close windows\n");
            return FALSE;
            } else {
                /* Bit 0 = 1 means successfully made private */
                Printf("Workspace: Screen made private\n");
            }
        }
        
        /* Close screen - returns TRUE if closed, FALSE if windows still open */
        closeSucceeded = CloseScreen(wsState.workspaceScreen);
        if (!closeSucceeded) {
            Printf("Workspace: CloseScreen failed - windows may still be open\n");
            /* Show requester and retry */
            titleStr = "Cannot Close Screen";
            textStr = "Cannot close Workspace screen.\n\nAll windows on this screen must be closed before exiting.\n\nPlease close all windows and try again.";
            okStr = "OK";
            
            es.es_StructSize = sizeof(struct EasyStruct);
            es.es_Flags = 0;
            es.es_Title = titleStr;
            es.es_TextFormat = textStr;
            es.es_GadgetFormat = okStr;
            
            /* Ensure our screen is in front and use a valid window on our screen */
            if (wsState.workspaceScreen) {
                ScreenToFront(wsState.workspaceScreen);
            }
            /* Use backdrop window if available and valid, otherwise try to use screen's first window */
            {
                struct Window *reqWindow = wsState.backdropWindow;
                /* If backdrop window is NULL or not on our screen, try to find another window */
                if (reqWindow == NULL || reqWindow->WScreen != wsState.workspaceScreen) {
                    /* Try to use the screen's first window if available */
                    if (wsState.workspaceScreen && wsState.workspaceScreen->FirstWindow) {
                        reqWindow = wsState.workspaceScreen->FirstWindow;
                    }
                }
                EasyRequestArgs(reqWindow, &es, NULL, NULL);
            }
            Printf("Workspace: CloseScreen failed, user must close windows\n");
            /* Return FALSE - screen was not closed */
            return FALSE;
        }
    }
    
    /* Only free draw info after successful close */
    if (wsState.drawInfo) {
        FreeScreenDrawInfo(wsState.workspaceScreen, wsState.drawInfo);
        wsState.drawInfo = NULL;
    }
    
    wsState.workspaceScreen = NULL;
    Printf("Workspace: Screen closed successfully\n");
    return TRUE;
}

/* Create backdrop window using standard Intuition OpenWindowTags() */
BOOL CreateBackdropWindow(VOID)
{
    WORD screenWidth, screenHeight;
    WORD titleBarHeight, windowTop, windowHeight;
    
    if (wsState.workspaceScreen == NULL) {
        return FALSE;
    }
    
    Printf("Workspace: Creating backdrop window on workspace screen...\n");
    
    /* Get screen dimensions - Width and Height are the RastPort dimensions */
    /* According to docs: "Width = the width for this screen's RastPort" */
    /* For SA_LikeWorkbench screens, use ViewPort dimensions if Screen->Width is 0 */
    if (wsState.workspaceScreen->Width == 0 && wsState.workspaceScreen->ViewPort.DWidth > 0) {
        screenWidth = 640; /*wsState.workspaceScreen->ViewPort.DWidth;*/
        screenHeight = 480; /*wsState.workspaceScreen->ViewPort.DHeight;*/
        Printf("Workspace: Using ViewPort dimensions: Width=%ld, Height=%ld\n",
               (LONG)screenWidth, (LONG)screenHeight);
    } else {
        screenWidth = wsState.workspaceScreen->Width;
        screenHeight = wsState.workspaceScreen->Height;
        Printf("Workspace: Using Screen dimensions: Width=%ld, Height=%ld\n",
               (LONG)screenWidth, (LONG)screenHeight);
    }
    
    /* Calculate title bar height - BarHeight is one less than actual height */
    titleBarHeight = wsState.workspaceScreen->BarHeight + 1;
    windowTop = titleBarHeight;
    windowHeight = screenHeight - titleBarHeight;
    
    Printf("Workspace: Screen BarHeight=%ld, TitleBarHeight=%ld\n", 
           (LONG)wsState.workspaceScreen->BarHeight, (LONG)titleBarHeight);
    Printf("Workspace: Creating window: Left=0, Top=%ld, Width=%ld, Height=%ld\n", 
           (LONG)windowTop, (LONG)screenWidth, (LONG)windowHeight);
    
    /* Validate dimensions - OpenWindowTags will fail or create invalid window if dimensions are 0 */
    if (screenWidth <= 0 || windowHeight <= 0) {
        Printf("Workspace: ERROR - Invalid window dimensions: Width=%ld, Height=%ld\n",
               (LONG)screenWidth, (LONG)windowHeight);
        return FALSE;
    }
    
    /* Open backdrop window - positioned below title bar */
    /* Don't activate initially - will activate after menu is set */
    wsState.backdropWindow = OpenWindowTags(NULL,
        WA_Left, 0,
        WA_Top, windowTop,
        WA_Width, screenWidth,
        WA_Height, windowHeight,
        WA_CustomScreen, wsState.workspaceScreen,
        WA_Backdrop, TRUE,
        WA_Borderless, TRUE,
        WA_DragBar, FALSE,
        WA_IDCMP, IDCMP_MENUPICK | IDCMP_CLOSEWINDOW,
        WA_DetailPen, -1,
        WA_BlockPen, -1,
        WA_Activate, FALSE,  /* Don't activate yet - will activate after menu is set */
        WA_NewLookMenus, TRUE,  /* Required for GadTools NewLook menus */
        TAG_DONE);
    
    if (wsState.backdropWindow == NULL) {
        Printf("Workspace: ERROR - Failed to open window (OpenWindowTags returned NULL)\n");
        return FALSE;
    }
    
    Printf("Workspace: Window opened successfully: 0x%lx\n", (ULONG)wsState.backdropWindow);
    Printf("Workspace: Window actual dimensions: LeftEdge=%ld, TopEdge=%ld, Width=%ld, Height=%ld\n",
           (LONG)wsState.backdropWindow->LeftEdge, (LONG)wsState.backdropWindow->TopEdge,
           (LONG)wsState.backdropWindow->Width, (LONG)wsState.backdropWindow->Height);
    Printf("Workspace: Window Flags: 0x%lx\n", (ULONG)wsState.backdropWindow->Flags);
    
    /* Check if window was created with valid dimensions */
    if (wsState.backdropWindow->Width == 0 || wsState.backdropWindow->Height == 0) {
        Printf("Workspace: ERROR - Window created with invalid dimensions (Width=%ld, Height=%ld)\n",
               (LONG)wsState.backdropWindow->Width, (LONG)wsState.backdropWindow->Height);
        CloseWindow(wsState.backdropWindow);
        wsState.backdropWindow = NULL;
        return FALSE;
    }
    {
        LONG signalBit;
        if (wsState.backdropWindow && wsState.backdropWindow->UserPort) {
            signalBit = wsState.backdropWindow->UserPort->mp_SigBit;
        } else {
            signalBit = -1;
        }
        Printf("Workspace: Window UserPort: 0x%lx, Signal bit: %ld\n", 
               (ULONG)wsState.backdropWindow->UserPort, signalBit);
    }
    
    /* Window is now open - menu will be created separately after this function returns */
    
    return TRUE;
}

/* Close backdrop window */
VOID CloseBackdropWindow(VOID)
{
    if (wsState.backdropWindow) {
        /* Clear menu strip before closing window (required by Intuition API) */
        if (wsState.menuStrip) {
            ClearMenuStrip(wsState.backdropWindow);
        }
        /* Close the window using standard Intuition CloseWindow() */
        CloseWindow(wsState.backdropWindow);
        wsState.backdropWindow = NULL;
    }
}

/* Menu item handlers */
VOID HandleAboutMenu(VOID)
{
    struct EasyStruct es;
    STRPTR titleStr = "About Workspace";
    STRPTR textStr = "Workspace\n\nA public screen manager for AmigaOS\n\nVersion 1.0";
    STRPTR okStr = "OK";
    struct Window *reqWindow;
    
    es.es_StructSize = sizeof(struct EasyStruct);
    es.es_Flags = 0;
    es.es_Title = titleStr;
    es.es_TextFormat = textStr;
    es.es_GadgetFormat = okStr;
    
    /* Ensure our screen is in front */
    if (wsState.workspaceScreen) {
        ScreenToFront(wsState.workspaceScreen);
    }
    
    /* Use backdrop window if available and valid, otherwise try to use screen's first window */
    reqWindow = wsState.backdropWindow;
    if (reqWindow == NULL || reqWindow->WScreen != wsState.workspaceScreen) {
        if (wsState.workspaceScreen && wsState.workspaceScreen->FirstWindow) {
            reqWindow = wsState.workspaceScreen->FirstWindow;
        }
    }
    
    EasyRequestArgs(reqWindow, &es, NULL, NULL);
}

/* Structure to hold window information for tiling */
struct WindowInfo {
    struct Window *window;
    WORD minWidth;
    WORD minHeight;
    WORD maxWidth;
    WORD maxHeight;
    BOOL isResizable;
    BOOL isShellWindow;
};

/* Get all visitor windows on the workspace screen */
/* Returns number of windows found, stores pointers in windows array */
/* Excludes backdrop window and shell window if specified */
WORD GetVisitorWindows(struct WindowInfo *windows, WORD maxWindows, BOOL excludeShell)
{
    struct Window *win;
    WORD count = 0;
    WORD titleBarHeight;
    
    if (!wsState.workspaceScreen || !windows || maxWindows == 0) {
        return 0;
    }
    
    /* Get title bar height for screen */
    titleBarHeight = wsState.workspaceScreen->BarHeight + 1;
    
    /* Iterate through all windows on the screen */
    win = wsState.workspaceScreen->FirstWindow;
    Printf("Workspace: GetVisitorWindows - backdropWindow=0x%lx, shellWindow=0x%lx\n",
           (ULONG)wsState.backdropWindow, (ULONG)wsState.shellWindow);
    while (win != NULL && count < maxWindows) {
        Printf("Workspace: GetVisitorWindows - checking window 0x%lx\n", (ULONG)win);
        
        /* ALWAYS skip backdrop window - it's our own window, never tile it */
        if (win == wsState.backdropWindow) {
            Printf("Workspace: GetVisitorWindows - skipping backdrop window\n");
            win = win->NextWindow;
            continue;
        }
        
        /* ALWAYS skip shell window - it's our own window, never tile it */
        /* Check both by pointer and by checking if it's a backdrop window at the bottom */
        if (win == wsState.shellWindow) {
            Printf("Workspace: GetVisitorWindows - skipping shell window (by pointer match)\n");
            win = win->NextWindow;
            continue;
        }
        
        /* Also check if this window matches shell window characteristics:
         * - Backdrop window
         * - Borderless
         * - At bottom of screen (TopEdge near screen height - 200)
         * - Height is approximately 200
         * This works even if wsState.shellWindow is NULL (after shell ends)
         */
        if ((win->Flags & WFLG_BACKDROP) != 0 &&
            (win->Flags & WFLG_BORDERLESS) != 0 &&
            win->WScreen == wsState.workspaceScreen) {
            WORD expectedTop = wsState.workspaceScreen->Height - 200;
            WORD expectedHeight = 200;
            /* Check if window is at bottom of screen with ~200px height */
            /* Use a wider tolerance for TopEdge since it might vary slightly */
            if (win->TopEdge >= expectedTop - 20 && win->TopEdge <= expectedTop + 20 &&
                win->Height >= expectedHeight - 20 && win->Height <= expectedHeight + 20) {
                Printf("Workspace: GetVisitorWindows - skipping shell window (by characteristics: TopEdge=%ld, Height=%ld, expected Top=%ld, Height=%ld)\n",
                       (LONG)win->TopEdge, (LONG)win->Height, (LONG)expectedTop, (LONG)expectedHeight);
                win = win->NextWindow;
                continue;
            }
        }
        
        /* Store window info */
        windows[count].window = win;
        windows[count].isShellWindow = FALSE;  /* We've already excluded shell window above */
        Printf("Workspace: GetVisitorWindows - including window 0x%lx in tiling list\n", (ULONG)win);
        
        /* Check if window is resizable */
        /* Windows with WFLG_SIZEGADGET or WFLG_DRAGBAR are typically resizable */
        windows[count].isResizable = ((win->Flags & WFLG_SIZEGADGET) != 0);
        
        /* Get window size limits if available */
        /* For now, use reasonable defaults */
        windows[count].minWidth = 64;
        windows[count].minHeight = 32;
        windows[count].maxWidth = win->WScreen->Width;
        windows[count].maxHeight = win->WScreen->Height - titleBarHeight;
        
        count++;
        /* Split Printf to avoid potential stack corruption */
        Printf("Workspace: GetVisitorWindows - count is now %ld\n", (LONG)count);
        Printf("Workspace: GetVisitorWindows - included window 0x%lx\n", (ULONG)win);
        win = win->NextWindow;
    }
    
    Printf("Workspace: GetVisitorWindows - final count before return: %ld\n", (LONG)count);
    return count;
}

/* Tile windows horizontally */
VOID TileWindowsHorizontally(VOID)
{
    struct WindowInfo windows[32];
    WORD windowCount;
    WORD i;
    WORD screenWidth;
    WORD screenHeight;
    WORD titleBarHeight;
    WORD windowWidth;
    WORD windowHeight;
    WORD windowTop;
    WORD windowLeft;
    WORD shellHeight = 0;
    WORD usableHeight;
    
    if (!wsState.workspaceScreen) {
        return;
    }
    
    /* Get screen dimensions */
    screenWidth = wsState.workspaceScreen->Width;
    screenHeight = wsState.workspaceScreen->Height;
    titleBarHeight = wsState.workspaceScreen->BarHeight + 1;
    
    /* Account for shell window at bottom if open */
    if (wsState.shellWindow && wsState.shellEnabled) {
        shellHeight = 200;  /* Shell window height */
    }
    
    usableHeight = screenHeight - titleBarHeight - shellHeight;
    
    /* Get all visitor windows (excluding backdrop, excluding shell) */
    Printf("Workspace: Getting visitor windows...\n");
    windowCount = GetVisitorWindows(windows, 32, TRUE);
    Printf("Workspace: GetVisitorWindows returned %ld windows\n", (LONG)windowCount);
    
    /* Early return if no windows - check immediately after function call */
    if (windowCount == 0) {
        Printf("Workspace: No windows to tile - returning early\n");
        return;
    }
    
    /* Double-check windowCount before proceeding */
    if (windowCount == 0 || windowCount > 32) {
        Printf("Workspace: ERROR - invalid windowCount=%ld, aborting\n", (LONG)windowCount);
        return;
    }
    
    Printf("Workspace: Tiling %ld windows horizontally\n", (LONG)windowCount);
    
    /* Calculate window dimensions - prevent division by zero */
    if (windowCount == 0) {
        Printf("Workspace: ERROR - windowCount is 0, returning early\n");
        return;
    }
    
    windowWidth = screenWidth / windowCount;
    if (windowWidth == 0) {
        Printf("Workspace: ERROR - calculated windowWidth is 0, returning early\n");
        return;
    }
    windowHeight = usableHeight;
    windowTop = titleBarHeight;
    
    Printf("Workspace: Calculated windowWidth=%ld, windowHeight=%ld, windowTop=%ld\n", 
           (LONG)windowWidth, (LONG)windowHeight, (LONG)windowTop);
    
    /* Tile windows */
    Printf("Workspace: Starting tile loop for %ld windows\n", (LONG)windowCount);
    
    /* Safety check - should never happen if early return worked */
    if (windowCount == 0 || windowCount > 32) {
        Printf("Workspace: ERROR - invalid windowCount=%ld, aborting tile operation\n", (LONG)windowCount);
        return;
    }
    
    for (i = 0; i < windowCount; i++) {
        /* Safety check - ensure window pointer is valid */
        if (windows[i].window == NULL) {
            Printf("Workspace: ERROR - window[%ld] is NULL, skipping\n", (LONG)i);
            continue;
        }
        
        {
            char *resizableStr;
            if (windows[i].isResizable) {
                resizableStr = "YES";
            } else {
                resizableStr = "NO";
            }
            Printf("Workspace: Tiling window %ld of %ld (window=0x%lx, resizable=%s)\n", 
                   (LONG)(i + 1), (LONG)windowCount, (ULONG)windows[i].window, resizableStr);
        }
        windowLeft = i * windowWidth;
        
        /* For resizable windows, resize them */
        if (windows[i].isResizable) {
            Printf("Workspace: Calling ChangeWindowBox for window %ld: left=%ld, top=%ld, width=%ld, height=%ld\n",
                   (LONG)i, (LONG)windowLeft, (LONG)windowTop, (LONG)windowWidth, (LONG)windowHeight);
            ChangeWindowBox(windows[i].window, windowLeft, windowTop, windowWidth, windowHeight);
        } else {
            /* For fixed-size windows, just move them */
            Printf("Workspace: Calling MoveWindow for window %ld: deltaX=%ld, deltaY=%ld\n",
                   (LONG)i, 
                   (LONG)(windowLeft - windows[i].window->LeftEdge),
                   (LONG)(windowTop - windows[i].window->TopEdge));
            MoveWindow(windows[i].window, windowLeft - windows[i].window->LeftEdge, 
                      windowTop - windows[i].window->TopEdge);
        }
        Printf("Workspace: Finished tiling window %ld\n", (LONG)i);
    }
    Printf("Workspace: Finished tiling all %ld windows\n", (LONG)windowCount);
}

/* Tile windows vertically */
VOID TileWindowsVertically(VOID)
{
    struct WindowInfo windows[32];
    WORD windowCount;
    WORD i;
    WORD screenWidth;
    WORD screenHeight;
    WORD titleBarHeight;
    WORD windowWidth;
    WORD windowHeight;
    WORD windowTop;
    WORD windowLeft;
    WORD shellHeight = 0;
    WORD usableHeight;
    
    if (!wsState.workspaceScreen) {
        return;
    }
    
    /* Get screen dimensions */
    screenWidth = wsState.workspaceScreen->Width;
    screenHeight = wsState.workspaceScreen->Height;
    titleBarHeight = wsState.workspaceScreen->BarHeight + 1;
    
    /* Account for shell window at bottom if open */
    if (wsState.shellWindow && wsState.shellEnabled) {
        shellHeight = 200;  /* Shell window height */
    }
    
    usableHeight = screenHeight - titleBarHeight - shellHeight;
    
    /* Get all visitor windows (excluding backdrop, excluding shell) */
    windowCount = GetVisitorWindows(windows, 32, TRUE);
    
    if (windowCount == 0) {
        Printf("Workspace: No windows to tile\n");
        return;
    }
    
    Printf("Workspace: Tiling %ld windows vertically\n", (LONG)windowCount);
    
    /* Calculate window dimensions - prevent division by zero */
    if (windowCount == 0) {
        Printf("Workspace: ERROR - windowCount is 0, returning early\n");
        return;
    }
    
    windowWidth = screenWidth;
    windowHeight = usableHeight / windowCount;
    if (windowHeight == 0) {
        Printf("Workspace: ERROR - calculated windowHeight is 0, returning early\n");
        return;
    }
    windowLeft = 0;
    
    /* Tile windows */
    for (i = 0; i < windowCount; i++) {
        windowTop = titleBarHeight + (i * windowHeight);
        
        /* For resizable windows, resize them */
        if (windows[i].isResizable) {
            ChangeWindowBox(windows[i].window, windowLeft, windowTop, windowWidth, windowHeight);
        } else {
            /* For fixed-size windows, just move them */
            MoveWindow(windows[i].window, windowLeft - windows[i].window->LeftEdge, 
                      windowTop - windows[i].window->TopEdge);
        }
    }
}

/* Tile windows in a grid layout */
VOID TileWindowsGrid(VOID)
{
    struct WindowInfo windows[32];
    WORD windowCount;
    WORD i;
    WORD screenWidth;
    WORD screenHeight;
    WORD titleBarHeight;
    WORD windowWidth;
    WORD windowHeight;
    WORD windowTop;
    WORD windowLeft;
    WORD shellHeight = 0;
    WORD usableHeight;
    WORD cols;
    WORD rows;
    WORD col;
    WORD row;
    
    if (!wsState.workspaceScreen) {
        return;
    }
    
    /* Get screen dimensions */
    screenWidth = wsState.workspaceScreen->Width;
    screenHeight = wsState.workspaceScreen->Height;
    titleBarHeight = wsState.workspaceScreen->BarHeight + 1;
    
    /* Account for shell window at bottom if open */
    if (wsState.shellWindow && wsState.shellEnabled) {
        shellHeight = 200;  /* Shell window height */
    }
    
    usableHeight = screenHeight - titleBarHeight - shellHeight;
    
    /* Get all visitor windows (excluding backdrop, excluding shell) */
    windowCount = GetVisitorWindows(windows, 32, TRUE);
    
    if (windowCount == 0) {
        Printf("Workspace: No windows to tile\n");
        return;
    }
    
    Printf("Workspace: Tiling %ld windows in grid\n", (LONG)windowCount);
    
    /* Calculate grid dimensions (aim for roughly square grid) */
    cols = (WORD)((windowCount + 1) / 2);  /* Roughly square */
    if (cols == 0) cols = 1;
    rows = (windowCount + cols - 1) / cols;  /* Ceiling division */
    if (rows == 0) rows = 1;
    
    windowWidth = screenWidth / cols;
    windowHeight = usableHeight / rows;
    
    /* Validate calculated dimensions */
    if (windowWidth == 0 || windowHeight == 0) {
        Printf("Workspace: ERROR - Invalid grid dimensions (windowWidth=%ld, windowHeight=%ld, cols=%ld, rows=%ld)\n",
               (LONG)windowWidth, (LONG)windowHeight, (LONG)cols, (LONG)rows);
        return;
    }
    
    Printf("Workspace: Grid layout - cols=%ld, rows=%ld, windowWidth=%ld, windowHeight=%ld\n",
           (LONG)cols, (LONG)rows, (LONG)windowWidth, (LONG)windowHeight);
    
    /* Tile windows in grid */
    for (i = 0; i < windowCount; i++) {
        /* Safety check */
        if (windows[i].window == NULL) {
            Printf("Workspace: ERROR - window[%ld] is NULL, skipping\n", (LONG)i);
            continue;
        }
        
        row = i / cols;
        col = i % cols;
        
        windowLeft = col * windowWidth;
        windowTop = titleBarHeight + (row * windowHeight);
        
        Printf("Workspace: Tiling window %ld of %ld (row=%ld, col=%ld, left=%ld, top=%ld, width=%ld, height=%ld)\n",
               (LONG)(i + 1), (LONG)windowCount, (LONG)row, (LONG)col, 
               (LONG)windowLeft, (LONG)windowTop, (LONG)windowWidth, (LONG)windowHeight);
        
        /* Validate window pointer and dimensions before operations */
        if (windows[i].window == NULL) {
            Printf("Workspace: ERROR - window[%ld] is NULL, skipping\n", (LONG)i);
            continue;
        }
        
        /* Validate dimensions are positive */
        if (windowWidth <= 0 || windowHeight <= 0) {
            Printf("Workspace: ERROR - Invalid dimensions for window[%ld] (width=%ld, height=%ld), skipping\n",
                   (LONG)i, (LONG)windowWidth, (LONG)windowHeight);
            continue;
        }
        
        /* Validate window is on our screen */
        if (windows[i].window->WScreen != wsState.workspaceScreen) {
            Printf("Workspace: ERROR - window[%ld] is not on workspace screen, skipping\n", (LONG)i);
            continue;
        }
        
        /* For resizable windows, resize them */
        if (windows[i].isResizable) {
            Printf("Workspace: Calling ChangeWindowBox for window %ld\n", (LONG)i);
            ChangeWindowBox(windows[i].window, windowLeft, windowTop, windowWidth, windowHeight);
            Printf("Workspace: ChangeWindowBox completed for window %ld\n", (LONG)i);
        } else {
            /* For fixed-size windows, just move them */
            /* Calculate delta values */
            {
                WORD deltaX = windowLeft - windows[i].window->LeftEdge;
                WORD deltaY = windowTop - windows[i].window->TopEdge;
                Printf("Workspace: Calling MoveWindow for window %ld (deltaX=%ld, deltaY=%ld)\n", 
                       (LONG)i, (LONG)deltaX, (LONG)deltaY);
                MoveWindow(windows[i].window, deltaX, deltaY);
                Printf("Workspace: MoveWindow completed for window %ld\n", (LONG)i);
            }
        }
    }
    
    Printf("Workspace: Finished tiling all %ld windows in grid\n", (LONG)windowCount);
}

/* Cascade windows */
VOID CascadeWindows(VOID)
{
    struct WindowInfo windows[32];
    WORD windowCount;
    WORD i;
    WORD screenWidth;
    WORD screenHeight;
    WORD titleBarHeight;
    WORD windowTop;
    WORD windowLeft;
    WORD shellHeight = 0;
    WORD usableHeight;
    WORD cascadeOffset = 30;  /* Offset for cascade effect */
    
    if (!wsState.workspaceScreen) {
        return;
    }
    
    /* Get screen dimensions */
    screenWidth = wsState.workspaceScreen->Width;
    screenHeight = wsState.workspaceScreen->Height;
    titleBarHeight = wsState.workspaceScreen->BarHeight + 1;
    
    /* Account for shell window at bottom if open */
    if (wsState.shellWindow && wsState.shellEnabled) {
        shellHeight = 200;  /* Shell window height */
    }
    
    usableHeight = screenHeight - titleBarHeight - shellHeight;
    
    /* Get all visitor windows (excluding backdrop, excluding shell) */
    windowCount = GetVisitorWindows(windows, 32, TRUE);
    
    if (windowCount == 0) {
        Printf("Workspace: No windows to cascade\n");
        return;
    }
    
    Printf("Workspace: Cascading %ld windows\n", (LONG)windowCount);
    
    /* Cascade windows with offset */
    for (i = 0; i < windowCount; i++) {
        windowLeft = i * cascadeOffset;
        windowTop = titleBarHeight + (i * cascadeOffset);
        
        /* Make sure windows don't go off screen */
        if (windowLeft + windows[i].window->Width > screenWidth) {
            windowLeft = screenWidth - windows[i].window->Width;
        }
        if (windowTop + windows[i].window->Height > screenHeight - shellHeight) {
            windowTop = screenHeight - shellHeight - windows[i].window->Height;
        }
        
        /* Move window to cascade position */
        MoveWindow(windows[i].window, windowLeft - windows[i].window->LeftEdge, 
                  windowTop - windows[i].window->TopEdge);
    }
}

/* Maximize all windows */
VOID MaximizeAllWindows(VOID)
{
    struct WindowInfo windows[32];
    WORD windowCount;
    WORD i;
    WORD screenWidth;
    WORD screenHeight;
    WORD titleBarHeight;
    WORD windowTop;
    WORD windowLeft;
    WORD windowWidth;
    WORD windowHeight;
    WORD shellHeight = 0;
    WORD usableHeight;
    
    if (!wsState.workspaceScreen) {
        return;
    }
    
    /* Get screen dimensions */
    screenWidth = wsState.workspaceScreen->Width;
    screenHeight = wsState.workspaceScreen->Height;
    titleBarHeight = wsState.workspaceScreen->BarHeight + 1;
    
    /* Account for shell window at bottom if open */
    if (wsState.shellWindow && wsState.shellEnabled) {
        shellHeight = 200;  /* Shell window height */
    }
    
    usableHeight = screenHeight - titleBarHeight - shellHeight;
    
    /* Get all visitor windows (excluding backdrop, excluding shell) */
    windowCount = GetVisitorWindows(windows, 32, TRUE);
    
    if (windowCount == 0) {
        Printf("Workspace: No windows to maximize\n");
        return;
    }
    
    Printf("Workspace: Maximizing %ld windows\n", (LONG)windowCount);
    
    windowLeft = 0;
    windowTop = titleBarHeight;
    windowWidth = screenWidth;
    windowHeight = usableHeight;
    
    /* Maximize all resizable windows */
    for (i = 0; i < windowCount; i++) {
        if (windows[i].isResizable) {
            ChangeWindowBox(windows[i].window, windowLeft, windowTop, windowWidth, windowHeight);
        }
    }
}

/* Handle Windows menu items */
VOID HandleWindowsMenu(ULONG itemNumber)
{
    Printf("Workspace: HandleWindowsMenu called with itemNumber=%lu\n", itemNumber);
    
    switch (itemNumber) {
        case 0:  /* Tile Horizontally */
            Printf("Workspace: Calling TileWindowsHorizontally\n");
            TileWindowsHorizontally();
            Printf("Workspace: TileWindowsHorizontally returned\n");
            break;
        
        case 1:  /* Tile Vertically */
            Printf("Workspace: Calling TileWindowsVertically\n");
            TileWindowsVertically();
            Printf("Workspace: TileWindowsVertically returned\n");
            break;
        
        case 2:  /* Grid Layout */
            Printf("Workspace: Calling TileWindowsGrid\n");
            TileWindowsGrid();
            Printf("Workspace: TileWindowsGrid returned\n");
            break;
        
        default:
            Printf("Workspace: Unknown Windows menu item: %lu\n", itemNumber);
            break;
    }
    
    Printf("Workspace: HandleWindowsMenu returning\n");
}

/* Handle Theme menu items */
VOID HandleThemeMenu(ULONG itemNumber)
{
    Printf("Workspace: HandleThemeMenu called with itemNumber=%lu\n", itemNumber);
    
    /* itemNumber is the theme index (0-4) */
    if (itemNumber < THEME_COUNT) {
        if (itemNumber == wsState.currentTheme) {
            Printf("Workspace: Theme already active, ignoring\n");
            return;
        }
        Printf("Workspace: Applying theme %lu: %s\n", itemNumber, themeNames[itemNumber]);
        if (ApplyTheme(itemNumber)) {
            wsState.currentTheme = itemNumber;
            Printf("Workspace: Theme applied successfully\n");
        } else {
            Printf("Workspace: ERROR - Failed to apply theme\n");
        }
    } else {
        Printf("Workspace: Unknown theme index: %lu\n", itemNumber);
    }
}

/* Apply color theme to screen */
BOOL ApplyTheme(ULONG themeIndex)
{
    struct ColorMap *colorMap;
    ULONG numColors;
    ULONG i;
    UBYTE r, g, b;
    ULONG srcR, srcG, srcB;
    ULONG brightness;
    ULONG invertedBrightness;
    ULONG gray;
    
    if (!wsState.workspaceScreen) {
        Printf("Workspace: ERROR - No screen available for theme\n");
        return FALSE;
    }
    
    colorMap = wsState.workspaceScreen->ViewPort.ColorMap;
    if (!colorMap) {
        Printf("Workspace: ERROR - No ColorMap available\n");
        return FALSE;
    }
    
    /* Always base themes on the original palette captured at screen open */
    if (!wsState.haveOriginalPalette) {
        Printf("Workspace: ERROR - No original palette captured\n");
        return FALSE;
    }
    numColors = wsState.numColors;
    if (numColors == 0 || numColors > 256) {
        Printf("Workspace: ERROR - Invalid numColors in original palette: %lu\n", numColors);
        return FALSE;
    }
    
    Printf("Workspace: Applying theme %lu to screen with %lu colors\n", themeIndex, numColors);
    
    /* Like Workbench restores the original palette captured at open */
    if (themeIndex == THEME_LIKE_WORKBENCH) {
        for (i = 0; i < numColors; i++) {
            srcR = wsState.originalRGB[i * 3] >> 24;
            srcG = wsState.originalRGB[i * 3 + 1] >> 24;
            srcB = wsState.originalRGB[i * 3 + 2] >> 24;
            SetRGB32(&wsState.workspaceScreen->ViewPort, i,
                     (ULONG)srcR << 24, (ULONG)srcG << 24, (ULONG)srcB << 24);
        }
        Printf("Workspace: Restored original palette\n");
        return TRUE;
    }
    
    /* Apply theme colors based on theme index */
    for (i = 0; i < numColors; i++) {
        /* Source color always from original palette (stable baseline) */
        srcR = wsState.originalRGB[i * 3] >> 24;
        srcG = wsState.originalRGB[i * 3 + 1] >> 24;
        srcB = wsState.originalRGB[i * 3 + 2] >> 24;
        r = (UBYTE)srcR;
        g = (UBYTE)srcG;
        b = (UBYTE)srcB;
        
        switch (themeIndex) {
            case THEME_DARK_MODE:
                /* Dark mode: invert colors and reduce brightness */
                brightness = ((ULONG)r + (ULONG)g + (ULONG)b) / 3;
                invertedBrightness = 255 - brightness;
                /* Scale to dark range (0-128) */
                r = (UBYTE)((invertedBrightness * 128) / 255);
                g = (UBYTE)((invertedBrightness * 128) / 255);
                b = (UBYTE)((invertedBrightness * 128) / 255);
                break;
            
            case THEME_SEPIA:
                /* Sepia: warm brown tones */
                gray = ((ULONG)r + (ULONG)g + (ULONG)b) / 3;
                r = (UBYTE)((gray * 240) / 255);  /* Warm red */
                g = (UBYTE)((gray * 220) / 255);  /* Warm green */
                b = (UBYTE)((gray * 180) / 255); /* Warm blue (reduced) */
                break;
            
            case THEME_BLUE:
                /* Blue theme: cool blue tones */
                gray = ((ULONG)r + (ULONG)g + (ULONG)b) / 3;
                r = (UBYTE)((gray * 180) / 255);  /* Reduced red */
                g = (UBYTE)((gray * 200) / 255);  /* Slightly reduced green */
                b = (UBYTE)((gray * 240) / 255);  /* Enhanced blue */
                break;
            
            case THEME_GREEN:
                /* Green theme: natural green tones */
                gray = ((ULONG)r + (ULONG)g + (ULONG)b) / 3;
                r = (UBYTE)((gray * 200) / 255);  /* Slightly reduced red */
                g = (UBYTE)((gray * 240) / 255);  /* Enhanced green */
                b = (UBYTE)((gray * 200) / 255);  /* Slightly reduced blue */
                break;
            
            default:
                /* Keep original color */
                break;
        }
        
        /* Set the color using SetRGB32 */
        SetRGB32(&wsState.workspaceScreen->ViewPort, i, (ULONG)r << 24, (ULONG)g << 24, (ULONG)b << 24);
    }
    
    Printf("Workspace: Theme applied to %lu colors\n", numColors);
    return TRUE;
}

VOID HandleShellConsoleMenu(VOID)
{
    /* Toggle shell console - create it if not already enabled */
    if (!wsState.shellEnabled) {
        /* Enable shell console */
        wsState.shellEnabled = TRUE;
        
        /* Free backdrop image if loaded (shell and backdrop are mutually exclusive) */
        if (wsState.backdropImageObj) {
            FreeBackdropImage();
        }
        
        /* Create shell console */
        if (!CreateShellConsole()) {
            Printf("Workspace: ERROR - Failed to create shell console\n");
            wsState.shellEnabled = FALSE;
        } else {
            Printf("Workspace: Shell console enabled\n");
        }
    } else {
        /* Shell is already enabled - just inform user */
        Printf("Workspace: Shell console is already running\n");
    }
}

/* Check visitor count for all Workspace screens (not Workbench) */
/* Returns the number of visitor windows (0 if none, or if error) */
/* Note: Backdrop window IS counted as a visitor window */
/* psn_VisitorCount only counts windows from other processes, so we add 1 for backdrop */
WORD CheckWorkspaceVisitors(VOID)
{
    struct List *pubScreenList = NULL;
    struct PubScreenNode *psn = NULL;
    WORD totalVisitors = 0;
    
    /* Lock public screen list */
    pubScreenList = LockPubScreenList();
    if (!pubScreenList) {
        Printf("Workspace: WARNING - Could not lock public screen list\n");
        return 0; /* Return 0 if we can't check */
    }
    
    /* Iterate through all public screens - correct Exec list iteration */
    psn = (struct PubScreenNode *)pubScreenList->lh_Head;
    while (psn && psn->psn_Node.ln_Succ != (struct Node *)&pubScreenList->lh_Tail) {
        /* Check if this is a Workspace screen (starts with "Workspace.") */
        if (psn->psn_Node.ln_Name && 
            strncmp(psn->psn_Node.ln_Name, "Workspace.", 10) == 0) {
            totalVisitors += (WORD)psn->psn_VisitorCount;
        }
        psn = (struct PubScreenNode *)psn->psn_Node.ln_Succ;
    }
    
    UnlockPubScreenList();
    
    /* Add 1 for the backdrop window (owner's window, not counted in psn_VisitorCount) */
    if (wsState.backdropWindow != NULL) {
        totalVisitors += 1;
    }
    
    /* Don't log here - let caller log with context */
    return totalVisitors;
}

BOOL HandleCloseMenu(VOID)
{
    struct EasyStruct es;
    STRPTR titleStr;
    STRPTR textStr;
    STRPTR okStr;
    struct Window *reqWindow;
    WORD visitorCount;
    char textBuffer[256];
    
    /* Check for visitor windows before allowing quit */
    Printf("Workspace: HandleCloseMenu called - checking for visitors...\n");
    visitorCount = CheckWorkspaceVisitors();
    Printf("Workspace: Visitor count: %ld\n", (LONG)visitorCount);
    if (visitorCount > 0) {
        /* Show EasyRequest dialog warning user */
        Printf("Workspace: Visitors detected (%ld windows) - showing warning dialog\n", (LONG)visitorCount);
        titleStr = "Cannot Exit Workspace";
        
        /* Format message with visitor count */
        if (visitorCount == 1) {
            SNPrintf(textBuffer, sizeof(textBuffer), 
                     "Cannot exit Workspace.\n\nThere is 1 window open on a Workspace screen.\n\nPlease close all windows and try again.");
        } else {
            SNPrintf(textBuffer, sizeof(textBuffer), 
                     "Cannot exit Workspace.\n\nThere are %d windows open on Workspace screens.\n\nPlease close all windows and try again.", 
                     (int)visitorCount);
        }
        textStr = textBuffer;
        okStr = "OK";
        
        es.es_StructSize = sizeof(struct EasyStruct);
        es.es_Flags = 0;
        es.es_Title = titleStr;
        es.es_TextFormat = textStr;
        es.es_GadgetFormat = okStr;
        
        /* Ensure our screen is in front and use a valid window on our screen */
        if (wsState.workspaceScreen) {
            ScreenToFront(wsState.workspaceScreen);
        }
        reqWindow = wsState.backdropWindow;
        if (reqWindow == NULL || reqWindow->WScreen != wsState.workspaceScreen) {
            if (wsState.workspaceScreen && wsState.workspaceScreen->FirstWindow) {
                reqWindow = wsState.workspaceScreen->FirstWindow;
            }
        }
        EasyRequestArgs(reqWindow, &es, NULL, NULL);
        Printf("Workspace: User dismissed dialog - NOT setting quitFlag, NOT exiting\n");
        {
            STRPTR quitFlagStr;
            if (wsState.quitFlag) {
                quitFlagStr = "TRUE";
            } else {
                quitFlagStr = "FALSE";
            }
            Printf("Workspace: quitFlag is currently: %s\n", quitFlagStr);
        }
        /* CRITICAL: Do NOT set quitFlag - this prevents the event loop from exiting */
        /* Do NOT proceed with cleanup - just return FALSE and let event loop continue */
        return FALSE;
    }
    
    /* No visitors - safe to quit */
    Printf("Workspace: No visitors detected - setting quitFlag to exit\n");
    wsState.quitFlag = TRUE;
    return TRUE;
}

VOID HandleDefaultPubScreenSubMenu(STRPTR screenName)
{
    if (screenName == NULL) {
        /* Workbench (NULL) */
        SetDefaultPubScreen(NULL);
        Printf("Workspace: Set Workbench as default pubscreen\n");
    } else {
        /* Workspace.n screen */
        SetDefaultPubScreen(screenName);
        Printf("Workspace: Set as default pubscreen: %s\n", screenName);
    }
}

/* Find all Workspace.n screens and build menu structure */
struct NewMenu *BuildDefaultPubScreenMenu(ULONG *menuCount)
{
    struct List *pubScreenList = NULL;
    struct PubScreenNode *psn = NULL;
    struct NewMenu *newMenu = NULL;
    struct NewMenu *newMenu2 = NULL;
    ULONG count = 0;
    ULONG maxCount = 32; /* Start with space for 32 screens */
    ULONG idx = 0;
    STRPTR screenName = NULL;
    STRPTR nameCopy = NULL;
    ULONG nameLen = 0;
    ULONG subItemCount = 1; /* Start with Workbench */
    ULONG subIdx;
    ULONG workbenchIdx = 1; /* Workbench is at index 1 (after title) */
    ULONG totalSubItems;
    
    /* Allocate menu array - start with base menu items + space for screens */
    newMenu = AllocMem(sizeof(struct NewMenu) * (maxCount + 10), MEMF_CLEAR);
    if (!newMenu) {
        Printf("Workspace: ERROR - Failed to allocate menu array\n");
        return NULL;
    }
    
    /* Start with menu title */
    newMenu[idx].nm_Type = NM_TITLE;
    newMenu[idx].nm_Label = "Workspace";
    idx++;
    
    /* Add "Default PubScreen" menu item */
    newMenu[idx].nm_Type = NM_ITEM;
    newMenu[idx].nm_Label = "Default PubScreen";
    newMenu[idx].nm_CommKey = NULL;
    newMenu[idx].nm_Flags = 0;
    newMenu[idx].nm_MutualExclude = 0;
    newMenu[idx].nm_UserData = NULL;
    idx++;
    
    /* Add "Workbench" as first sub-item (corresponds to NULL) */
    newMenu[idx].nm_Type = NM_SUB;
    newMenu[idx].nm_Label = "Workbench";
    newMenu[idx].nm_Flags = CHECKIT | CHECKED; /* Checkmark item, initially checked */
    newMenu[idx].nm_MutualExclude = 0; /* Will be set after we know total count */
    newMenu[idx].nm_UserData = (APTR)((0UL << 16) | (0UL << 8) | 0UL); /* Menu 0, Item 0, Sub 0 */
    idx++;
    subItemCount++;
    
    /* First, add our own screen if it exists */
    if (wsState.workspaceScreen && wsState.workspaceName) {
        nameLen = 0;
        if (wsState.workspaceName) {
            while (wsState.workspaceName[nameLen] != '\0') nameLen++;
        }
        if (nameLen > 0) {
            nameCopy = AllocMem(nameLen + 1, MEMF_CLEAR);
            if (nameCopy) {
                strcpy(nameCopy, wsState.workspaceName);
                
                newMenu[idx].nm_Type = NM_SUB;
                newMenu[idx].nm_Label = nameCopy;
                newMenu[idx].nm_Flags = CHECKIT; /* Checkmark item */
                newMenu[idx].nm_MutualExclude = 0; /* Will be set after we know total count */
                newMenu[idx].nm_UserData = (APTR)((0UL << 16) | (0UL << 8) | (subItemCount - 1));
                idx++;
                count++;
                subItemCount++;
            }
        }
    }
    
    /* Lock public screen list to enumerate other screens */
    pubScreenList = LockPubScreenList();
    if (pubScreenList) {
        /* Iterate through all public screens */
        for (psn = (struct PubScreenNode *)pubScreenList->lh_Head;
             psn->psn_Node.ln_Succ != (struct Node *)&pubScreenList->lh_Tail;
             psn = (struct PubScreenNode *)psn->psn_Node.ln_Succ) {
            
            screenName = psn->psn_Node.ln_Name;
            /* Check if screen name starts with "Workspace." and is not our own screen */
            nameLen = 0;
            if (screenName) {
                while (screenName[nameLen] != '\0') nameLen++;
            }
            if (screenName && nameLen >= 10 && 
                strncmp(screenName, "Workspace.", 10) == 0) {
                /* Skip our own screen if we already added it */
                if (wsState.workspaceName && strcmp(screenName, wsState.workspaceName) == 0) {
                    continue;
                }
                /* This is a Workspace screen - add to menu */
                if (idx >= maxCount + 5) {
                    /* Need more space - reallocate by allocating new and copying */
                    maxCount *= 2;
                    newMenu2 = AllocMem(sizeof(struct NewMenu) * (maxCount + 10), MEMF_CLEAR);
                    if (!newMenu2) {
                        Printf("Workspace: ERROR - Failed to reallocate menu array\n");
                        UnlockPubScreenList();
                        FreeMem(newMenu, sizeof(struct NewMenu) * (maxCount / 2 + 10));
                        return NULL;
                    }
                    CopyMem(newMenu, newMenu2, sizeof(struct NewMenu) * idx);
                    FreeMem(newMenu, sizeof(struct NewMenu) * (maxCount / 2 + 10));
                    newMenu = newMenu2;
                }
                
                /* Allocate persistent copy of name */
                nameCopy = AllocMem(nameLen + 1, MEMF_CLEAR);
                if (nameCopy) {
                    strcpy(nameCopy, screenName);
                    
                    newMenu[idx].nm_Type = NM_SUB;
                    newMenu[idx].nm_Label = nameCopy;
                    newMenu[idx].nm_Flags = CHECKIT; /* Checkmark item */
                    newMenu[idx].nm_MutualExclude = 0; /* Will be set after we know total count */
                    newMenu[idx].nm_UserData = (APTR)((0UL << 16) | (0UL << 8) | (subItemCount - 1)); /* Menu 0, Item 0, Sub (subItemCount-1) */
                    idx++;
                    count++;
                    subItemCount++;
                }
            }
        }
        UnlockPubScreenList();
    }
    
    /* Now set mutual exclusion for all sub-items */
    /* Each item excludes all other items */
    /* Bit 0 = first item (Workbench), bit 1 = second item, etc. */
    /* For item i, set all bits except bit i (exclude all other items) */
    totalSubItems = subItemCount - 1; /* Total number of sub-items */
    for (subIdx = 0; subIdx < totalSubItems; subIdx++) {
        ULONG menuIdx = workbenchIdx + subIdx;
        /* Set all bits for items 0..(totalSubItems-1), except bit subIdx */
        /* Example: 3 items -> item 0 excludes 1,2 = 0x6, item 1 excludes 0,2 = 0x5, item 2 excludes 0,1 = 0x3 */
        ULONG excludeMask = ((1UL << totalSubItems) - 1) & ~(1UL << subIdx);
        newMenu[menuIdx].nm_MutualExclude = excludeMask;
    }
    
    /* Add separator */
    newMenu[idx].nm_Type = NM_ITEM;
    newMenu[idx].nm_Label = NM_BARLABEL;
    idx++;
    
    /* Add "About" - this is a regular menu item, not a sub-item */
    newMenu[idx].nm_Type = NM_ITEM;
    newMenu[idx].nm_Label = "About";
    newMenu[idx].nm_CommKey = "?";
    newMenu[idx].nm_UserData = (APTR)((0UL << 16) | (1UL << 8) | 0UL); /* Menu 0, Item 1, Sub 0 */
    idx++;
    
    /* Add "Shell Console" - this is a regular menu item, not a sub-item */
    newMenu[idx].nm_Type = NM_ITEM;
    newMenu[idx].nm_Label = "Open AmigaShell";
    newMenu[idx].nm_CommKey = "S";
    newMenu[idx].nm_UserData = (APTR)((0UL << 16) | (3UL << 8) | 0UL); /* Menu 0, Item 3, Sub 0 */
    idx++;
    
    /* Add "Quit" - this is a regular menu item, not a sub-item */
    newMenu[idx].nm_Type = NM_ITEM;
    newMenu[idx].nm_Label = "Close Workspace";
    newMenu[idx].nm_CommKey = "Q";
    newMenu[idx].nm_UserData = (APTR)((0UL << 16) | (2UL << 8) | 0UL); /* Menu 0, Item 2, Sub 0 */
    idx++;
    
    /* Ensure we have enough space for second menu (need ~10 more entries) */
    if (idx >= maxCount + 5) {
        /* Need more space - reallocate */
        maxCount *= 2;
        newMenu2 = AllocMem(sizeof(struct NewMenu) * (maxCount + 10), MEMF_CLEAR);
        if (!newMenu2) {
            Printf("Workspace: ERROR - Failed to reallocate menu array for second menu\n");
            FreeMem(newMenu, sizeof(struct NewMenu) * (maxCount / 2 + 10));
            return NULL;
        }
        CopyMem(newMenu, newMenu2, sizeof(struct NewMenu) * idx);
        FreeMem(newMenu, sizeof(struct NewMenu) * (maxCount / 2 + 10));
        newMenu = newMenu2;
    }
    
    /* Start second menu - Windows (NO NM_END between menus!) */
    newMenu[idx].nm_Type = NM_TITLE;
    newMenu[idx].nm_Label = "Windows";
    newMenu[idx].nm_CommKey = NULL;
    newMenu[idx].nm_Flags = 0;
    newMenu[idx].nm_MutualExclude = 0;
    newMenu[idx].nm_UserData = NULL;
    idx++;
    
    /* Add "Tile Horizontally" */
    newMenu[idx].nm_Type = NM_ITEM;
    newMenu[idx].nm_Label = "Tile Horizontally";
    newMenu[idx].nm_CommKey = "H";
    newMenu[idx].nm_UserData = (APTR)((1UL << 16) | (0UL << 8) | 0UL); /* Menu 1, Item 0, Sub 0 */
    idx++;
    
    /* Add "Tile Vertically" */
    newMenu[idx].nm_Type = NM_ITEM;
    newMenu[idx].nm_Label = "Tile Vertically";
    newMenu[idx].nm_CommKey = "V";
    newMenu[idx].nm_UserData = (APTR)((1UL << 16) | (1UL << 8) | 0UL); /* Menu 1, Item 1, Sub 0 */
    idx++;
    
    /* Add "Grid Layout" */
    newMenu[idx].nm_Type = NM_ITEM;
    newMenu[idx].nm_Label = "Grid Layout";
    newMenu[idx].nm_CommKey = "G";
    newMenu[idx].nm_UserData = (APTR)((1UL << 16) | (2UL << 8) | 0UL); /* Menu 1, Item 2, Sub 0 */
    idx++;
    
    /* Ensure we have enough space for third menu (Prefs) */
    if (idx >= maxCount + 5) {
        /* Need more space - reallocate */
        maxCount *= 2;
        newMenu2 = AllocMem(sizeof(struct NewMenu) * (maxCount + 10), MEMF_CLEAR);
        if (!newMenu2) {
            Printf("Workspace: ERROR - Failed to reallocate menu array for third menu\n");
            FreeMem(newMenu, sizeof(struct NewMenu) * (maxCount / 2 + 10));
            return NULL;
        }
        CopyMem(newMenu, newMenu2, sizeof(struct NewMenu) * idx);
        FreeMem(newMenu, sizeof(struct NewMenu) * (maxCount / 2 + 10));
        newMenu = newMenu2;
    }
    
    /* Start third menu - Prefs (NO NM_END between menus!) */
    newMenu[idx].nm_Type = NM_TITLE;
    newMenu[idx].nm_Label = "Prefs";
    newMenu[idx].nm_CommKey = NULL;
    newMenu[idx].nm_Flags = 0;
    newMenu[idx].nm_MutualExclude = 0;
    newMenu[idx].nm_UserData = NULL;
    idx++;
    
    /* Add "Theme" menu item */
    newMenu[idx].nm_Type = NM_ITEM;
    newMenu[idx].nm_Label = "Theme";
    newMenu[idx].nm_CommKey = NULL;
    newMenu[idx].nm_Flags = 0;
    newMenu[idx].nm_MutualExclude = 0;
    newMenu[idx].nm_UserData = NULL;
    idx++;
    
    /* Add theme sub-items with mutex */
    {
        ULONG themeSubIdx;
        ULONG themeSubItemCount = THEME_COUNT;
        ULONG themeStartIdx = idx;
        
        for (themeSubIdx = 0; themeSubIdx < themeSubItemCount; themeSubIdx++) {
            STRPTR themeLabel = (STRPTR)themeNames[themeSubIdx];
            ULONG checkFlags = CHECKIT;
            
            /* Mark current theme as checked */
            if (themeSubIdx == wsState.currentTheme) {
                checkFlags |= CHECKED;
            }
            
            newMenu[idx].nm_Type = NM_SUB;
            newMenu[idx].nm_Label = themeLabel;
            newMenu[idx].nm_Flags = checkFlags;
            newMenu[idx].nm_MutualExclude = 0; /* Will be set after loop */
            newMenu[idx].nm_UserData = (APTR)((2UL << 16) | (0UL << 8) | themeSubIdx); /* Menu 2, Item 0, Sub themeSubIdx */
            idx++;
        }
        
        /* Set mutual exclusion for theme sub-items */
        for (themeSubIdx = 0; themeSubIdx < themeSubItemCount; themeSubIdx++) {
            ULONG excludeMask = ((1UL << themeSubItemCount) - 1) & ~(1UL << themeSubIdx);
            newMenu[themeStartIdx + themeSubIdx].nm_MutualExclude = excludeMask;
        }
    }
    
    /* Terminate third menu (this also terminates the entire menu strip) */
    newMenu[idx].nm_Type = NM_END;
    newMenu[idx].nm_Label = NULL;
    newMenu[idx].nm_CommKey = NULL;
    newMenu[idx].nm_Flags = 0;
    newMenu[idx].nm_MutualExclude = 0;
    newMenu[idx].nm_UserData = NULL;
    idx++;
    
    *menuCount = idx;  /* Count includes final NM_END entry */
    Printf("Workspace: Built menu with %lu items (%lu Workspace screens, 3 menus)\n", *menuCount, count);
    return newMenu;
}

/* Create menu strip using GadTools - MUST be called AFTER window is open */
BOOL CreateMenuStrip(VOID)
{
    struct NewMenu *newMenu = NULL;
    ULONG menuCount = 0;
    struct Menu *menuStrip = NULL;
    struct VisualInfo *visInfo = NULL;
    
    /* Window must exist */
    if (!wsState.backdropWindow) {
        Printf("Workspace: ERROR - Window must exist before creating menu strip\n");
        return FALSE;
    }
    
    Printf("Workspace: Creating menu strip using GadTools...\n");
    
    /* Build menu structure dynamically */
    newMenu = BuildDefaultPubScreenMenu(&menuCount);
    if (!newMenu) {
        Printf("Workspace: ERROR - Failed to build menu structure\n");
        return FALSE;
    }
    
    /* Create menu strip from NewMenu array */
    menuStrip = CreateMenus(newMenu, TAG_DONE);
    if (!menuStrip) {
        Printf("Workspace: ERROR - CreateMenus failed\n");
        return FALSE;
    }
    
    /* Free the dynamically allocated menu structure (CreateMenus copies it) */
    FreeMem(newMenu, sizeof(struct NewMenu) * menuCount);
    
    /* Get visual info for layout (required for GadTools menus) */
    visInfo = GetVisualInfo(wsState.backdropWindow->WScreen, TAG_END);
    if (!visInfo) {
        Printf("Workspace: ERROR - GetVisualInfo failed\n");
        FreeMenus(menuStrip);
        return FALSE;
    }
    
    /* Layout menus with visual info */
    if (!LayoutMenus(menuStrip, visInfo, 
                     GTMN_NewLookMenus, TRUE,
                     TAG_END)) {
        Printf("Workspace: ERROR - LayoutMenus failed\n");
        FreeVisualInfo(visInfo);
        FreeMenus(menuStrip);
        return FALSE;
    }
    
    /* Set menu strip on window */
    if (!SetMenuStrip(wsState.backdropWindow, menuStrip)) {
        Printf("Workspace: ERROR - SetMenuStrip failed\n");
        FreeVisualInfo(visInfo);
        FreeMenus(menuStrip);
        return FALSE;
    }
    
    /* Set Workbench as initially checked (default pubscreen) after SetMenuStrip */
    if (menuStrip && menuStrip->FirstItem && menuStrip->FirstItem->SubItem) {
        struct MenuItem *workbenchItem = menuStrip->FirstItem->SubItem;
        if (workbenchItem) {
            workbenchItem->Flags |= CHECKED;
            Printf("Workspace: Set Workbench as initially checked\n");
            /* Refresh menu to show checkmark */
            ClearMenuStrip(wsState.backdropWindow);
            ResetMenuStrip(wsState.backdropWindow, menuStrip);
        }
    }
    
    /* Store menu strip */
    wsState.menuStrip = menuStrip;
    
    /* Verify menu is actually attached to window */
    if (wsState.backdropWindow->MenuStrip != menuStrip) {
        Printf("Workspace: ERROR - Menu strip not found in window structure!\n");
        FreeVisualInfo(visInfo);
        FreeMenus(menuStrip);
        wsState.menuStrip = NULL;
        return FALSE;
    }
    
    Printf("Workspace: Menu strip verified in window (MenuStrip=0x%lx)\n", (ULONG)wsState.backdropWindow->MenuStrip);
    
    /* Free visual info (no longer needed after LayoutMenus and SetMenuStrip) */
    FreeVisualInfo(visInfo);
    
    /* Activate window and refresh frame to make menu visible */
    ActivateWindow(wsState.backdropWindow);
    WindowToFront(wsState.backdropWindow);
    RefreshWindowFrame(wsState.backdropWindow);
    ScreenToFront(wsState.workspaceScreen);
    
    Printf("Workspace: Menu strip created and attached successfully\n");
    return TRUE;
}

/* Free menu strip */
VOID FreeMenuStrip(VOID)
{
    if (wsState.menuStrip) {
        /* Use FreeMenus() to properly free GadTools menu structure */
        FreeMenus(wsState.menuStrip);
        wsState.menuStrip = NULL;
    }
}

/* Create shell backdrop window - separate from main backdrop */
BOOL CreateShellWindow(VOID)
{
    ULONG screenWidth, screenHeight;
    WORD windowTop;
    WORD windowHeight = 200;  /* Fixed height: 200px at bottom */
    
    /* Prerequisites check */
    if (!wsState.workspaceScreen) {
        Printf("Workspace: Cannot create shell window - screen not available\n");
        return FALSE;
    }
    
    /* Get screen dimensions */
    screenWidth = wsState.workspaceScreen->Width;
    screenHeight = wsState.workspaceScreen->Height;
    
    /* Handle case where screen dimensions might be 0 (use ViewPort dimensions) */
    if (screenWidth == 0 && wsState.workspaceScreen->ViewPort.DWidth > 0) {
        screenWidth = wsState.workspaceScreen->ViewPort.DWidth;
        screenHeight = wsState.workspaceScreen->ViewPort.DHeight;
    }
    
    /* Calculate window position - at bottom of screen */
    windowTop = screenHeight - windowHeight;
    
    /* Validate dimensions */
    if (screenWidth <= 0 || windowHeight <= 0 || windowTop < 0) {
        Printf("Workspace: ERROR - Invalid dimensions for shell window (Width=%lu, Height=%ld, Top=%ld)\n",
               screenWidth, (LONG)windowHeight, (LONG)windowTop);
        return FALSE;
    }
    
    Printf("Workspace: Creating shell window: Left=0, Top=%ld, Width=%lu, Height=%ld\n",
           (LONG)windowTop, screenWidth, (LONG)windowHeight);
    
    /* Open shell backdrop window - positioned at bottom of screen */
    wsState.shellWindow = OpenWindowTags(NULL,
        WA_Left, 0,
        WA_Top, windowTop,
        WA_Width, screenWidth,
        WA_Height, windowHeight,
        WA_CustomScreen, wsState.workspaceScreen,
        WA_Backdrop, TRUE,
        WA_Borderless, TRUE,
        WA_DragBar, FALSE,
        WA_IDCMP, 0,  /* No IDCMP needed - shell will handle it */
        WA_DetailPen, -1,
        WA_BlockPen, -1,
        WA_Activate, FALSE,
        TAG_DONE);
    
    if (wsState.shellWindow == NULL) {
        Printf("Workspace: ERROR - Failed to open shell window (OpenWindowTags returned NULL)\n");
        return FALSE;
    }
    
    Printf("Workspace: Shell window opened successfully: 0x%lx\n", (ULONG)wsState.shellWindow);
    Printf("Workspace: Shell window dimensions: LeftEdge=%ld, TopEdge=%ld, Width=%ld, Height=%ld\n",
           (LONG)wsState.shellWindow->LeftEdge, (LONG)wsState.shellWindow->TopEdge,
           (LONG)wsState.shellWindow->Width, (LONG)wsState.shellWindow->Height);
    
    return TRUE;
}

/* Create shell console */
BOOL CreateShellConsole(VOID)
{
    STRPTR conspec = NULL;
    UBYTE conspecBuffer[256];
    WORD windowWidth, windowHeight;
    LONG result;
    
    /* Prerequisites check */
    if (!wsState.workspaceScreen || !wsState.shellEnabled) {
        Printf("Workspace: Cannot create shell console - prerequisites not met\n");
        return FALSE;
    }
    
    /* Create shell backdrop window if it doesn't exist */
    if (!wsState.shellWindow) {
        if (!CreateShellWindow()) {
            Printf("Workspace: ERROR - Failed to create shell window\n");
            return FALSE;
        }
    }
    
    /* Get dimensions from the shell window */
    windowWidth = wsState.shellWindow->Width;
    windowHeight = wsState.shellWindow->Height;
    
    /* Validate dimensions */
    if (windowWidth <= 0 || windowHeight <= 0) {
        Printf("Workspace: ERROR - Invalid dimensions for shell console (Width=%ld, Height=%ld)\n",
               (LONG)windowWidth, (LONG)windowHeight);
        return FALSE;
    }
    
    Printf("Workspace: Shell console dimensions - width=%ld, height=%ld\n",
           (LONG)windowWidth, (LONG)windowHeight);
    
    /* Build CON: device specifier using WINDOW parameter */
    /* According to RKM: WINDOW instructs the console to hijack an already open window */
    /* Format: CON:x/y/width/height/title/WINDOW 0x<hex_address> */
    if (wsState.shellPath && wsState.shellPath[0] != '\0') {
        /* Use custom shell path */
        SNPrintf(conspecBuffer, sizeof(conspecBuffer), wsState.shellPath, wsState.workspaceName);
        conspec = conspecBuffer;
    } else {
        /* Use default: CON:0/0/width/height//WINDOW 0x<hex_address> */
        {
            struct Window *win = wsState.shellWindow;
            ULONG windowAddr;
            
            /* Get window address */
            windowAddr = (ULONG)win;
            
            /* Build CON: specifier */
            SNPrintf(conspecBuffer, sizeof(conspecBuffer),
                     "CON:0/0/%ld/%ld//WINDOW 0x%08lX",
                     (LONG)windowWidth, (LONG)windowHeight, windowAddr);
            conspec = conspecBuffer;
            
            Printf("Workspace: CON: specifier: '%s'\n", conspec);
            Printf("Workspace: Shell window pointer: 0x%lx\n", windowAddr);
        }
    }
    
    Printf("Workspace: Creating shell console with CON: spec: %s\n", conspec);
    
    /* Ensure window is on the workspace screen and active before hijacking */
    if (wsState.shellWindow && wsState.shellWindow->WScreen != wsState.workspaceScreen) {
        Printf("Workspace: WARNING - Shell window is not on workspace screen!\n");
    }
    
    /* Activate window and bring to front */
    if (wsState.shellWindow) {
        ActivateWindow(wsState.shellWindow);
        WindowToFront(wsState.shellWindow);
        ScreenToFront(wsState.workspaceScreen);
    }
    
    /* Use System() directly instead of NewShell command */
    /* According to RKM: NewShell uses System() with SYS_InName pointing to CON: specifier */
    /* Format: SystemTags(NULL, SYS_InName, window, SYS_CmdStream, ..., SYS_Output, 0, ...) */
    {
        BPTR cmdStream = 0;
        BPTR startupFile = 0;
        struct TagItem tags[8];
        
        /* Open startup file (default is S:Shell-Startup) */
        startupFile = Open("S:Shell-Startup", MODE_OLDFILE);
        if (startupFile != 0) {
            cmdStream = startupFile;
        }
        
        /* Build TagItem array - can't use variables in initializers in C89 */
        /* According to RKM: With SYS_Asynch, must provide SYS_Input/InName AND SYS_Output/OutName */
        /* Setting SYS_Output to ZERO makes it clone from input (the CON: device) */
        tags[0].ti_Tag = SYS_InName;
        tags[0].ti_Data = (ULONG)conspec;  /* CON: specifier with WINDOW parameter */
        tags[1].ti_Tag = SYS_CmdStream;
        tags[1].ti_Data = (ULONG)cmdStream;  /* Startup file stream */
        tags[2].ti_Tag = SYS_Output;
        tags[2].ti_Data = 0;  /* ZERO - clone from input (CON: device) to preserve our stdout */
        tags[3].ti_Tag = SYS_Asynch;
        tags[3].ti_Data = TRUE;  /* Must be TRUE to avoid blocking */
        tags[4].ti_Tag = SYS_UserShell;
        tags[4].ti_Data = TRUE;  /* Use user shell */
        tags[5].ti_Tag = NP_StackSize;
        tags[5].ti_Data = 4096;
        tags[6].ti_Tag = NP_Name;
        tags[6].ti_Data = (ULONG)"Workspace Shell";  /* Process name */
        tags[7].ti_Tag = TAG_DONE;
        tags[7].ti_Data = 0;
        
        /* Call System() directly with CON: specifier using WINDOW parameter */
        /* Pass NULL as command since we're using SYS_CmdStream for startup file */
        /* With NULL command and SYS_Asynch, shell reads from SYS_CmdStream then SYS_InName */
        result = SystemTagList(NULL, tags);
        
        /* Note: cmdStream will be closed by System() when shell terminates */
    }
    
    /* SystemTagList returns process ID on success (non-zero) or 0 on failure */
    /* However, with SYS_Asynch, it may return immediately before process starts */
    if (result == 0) {
        Printf("Workspace: WARNING - SystemTagList returned 0 (may be normal with async)\n");
        /* Don't fail immediately - check if window was actually donated */
        /* If window UserPort becomes NULL, it was successfully donated */
        if (wsState.shellWindow && wsState.shellWindow->UserPort == NULL) {
            Printf("Workspace: Window was donated to console despite return value 0\n");
            /* Success - window was donated */
        } else {
            Printf("Workspace: ERROR - Failed to create shell console (System returned 0 and window not donated)\n");
            return FALSE;
        }
    } else {
        Printf("Workspace: SystemTagList returned process ID: %ld\n", result);
    }
    
    /* Verify window was actually donated (UserPort should be NULL) */
    /* Small delay to let console take ownership */
    Delay(1);
    if (wsState.shellWindow && wsState.shellWindow->UserPort != NULL) {
        Printf("Workspace: WARNING - Window was not donated to console\n");
        return FALSE;
    }
    
    /* IMPORTANT: When using WINDOW parameter, the console takes ownership of the window */
    /* The console will close the window when it exits */
    /* Don't set shellWindow to NULL - we'll detect when it's closed by checking UserPort */
    Printf("Workspace: Shell console launched successfully - shell window ownership transferred to console\n");
    Printf("Workspace: Note - when shell ends, console will close the shell window\n");
    
    /* Disable "Open AmigaShell" menu item since shell is now open */
    /* Menu 0, Item 3, Sub 0 - format: (menu << 16) | (item << 8) | sub */
    if (wsState.backdropWindow) {
        OffMenu(wsState.backdropWindow, (0UL << 16) | (3UL << 8) | 0UL);
        Printf("Workspace: Disabled 'Open AmigaShell' menu item\n");
    }
    
    Printf("Workspace: Shell console created successfully\n");
    return TRUE;
}

/* Close shell console */
VOID CloseShellConsole(VOID)
{
    /* If shell window exists and hasn't been donated to the console, close it */
    /* Check if window is still valid (UserPort not NULL means we still own it) */
    if (wsState.shellWindow != NULL) {
        if (wsState.shellWindow->UserPort != NULL) {
            /* We still own the window - close it */
            Printf("Workspace: Closing shell window (not donated to console)\n");
            CloseWindow(wsState.shellWindow);
            wsState.shellWindow = NULL;
        } else {
            /* Window was donated to console - don't close it, console will close it */
            Printf("Workspace: Shell window was donated to console - console will close it\n");
            wsState.shellWindow = NULL;  /* Clear pointer, but don't close */
        }
    }
    
    wsState.shellEnabled = FALSE;
    
    /* Re-enable "Open AmigaShell" menu item since shell is now closed */
    /* Menu 0, Item 3, Sub 0 - format: (menu << 16) | (item << 8) | sub */
    if (wsState.backdropWindow) {
        OnMenu(wsState.backdropWindow, (0UL << 16) | (3UL << 8) | 0UL);
        Printf("Workspace: Re-enabled 'Open AmigaShell' menu item\n");
    }
    
    Printf("Workspace: Shell console cleanup complete\n");
}

/* Load backdrop image */
/* Parse command line arguments */
BOOL ParseCommandLine(VOID)
{
    LONG argArray[5];
    STRPTR pubNameArg = NULL;
    STRPTR cxNameArg = NULL;
    STRPTR backdropArg = NULL;
    STRPTR cxPopKeyArg = NULL;
    STRPTR themeArg = NULL;
    static UBYTE pubNameBuffer[64];
    static UBYTE cxNameBuffer[64];
    static UBYTE backdropBuffer[256];
    static UBYTE cxPopKeyBuffer[64];
    static UBYTE themeBuffer[64];
    
    /* Initialize arg array */
    argArray[0] = 0;
    argArray[1] = 0;
    argArray[2] = 0;
    argArray[3] = 0;
    argArray[4] = 0;
    
    /* Clear IoErr before ReadArgs */
    SetIoErr(0);
    
    /* Parse arguments: PUBNAME/K, CX_NAME/K, BACKDROP/K, CX_POPKEY/K, THEME/K */
    wsState.rda = ReadArgs("PUBNAME/K,CX_NAME/K,BACKDROP/K,CX_POPKEY/K,THEME/K", argArray, NULL);
    if (!wsState.rda) {
        LONG errorCode = IoErr();
        if (errorCode != 0) {
            Printf("Workspace: ReadArgs failed with error: %ld\n", errorCode);
        }
        /* ReadArgs can return NULL even on success if no args provided */
        /* Set defaults */
        wsState.pubName = NULL;
        wsState.cxName = NULL;
        wsState.backdropImagePath = NULL;
        wsState.cxPopKey = NULL;
        wsState.themeName = NULL;
        return TRUE; /* Not a fatal error */
    }
    
    /* Extract arguments */
    pubNameArg = (STRPTR)argArray[0];
    cxNameArg = (STRPTR)argArray[1];
    backdropArg = (STRPTR)argArray[2];
    cxPopKeyArg = (STRPTR)argArray[3];
    themeArg = (STRPTR)argArray[4];
    
    /* Store pubname if provided */
    if (pubNameArg && pubNameArg[0] != '\0') {
        SNPrintf(pubNameBuffer, sizeof(pubNameBuffer), "%s", pubNameArg);
        wsState.pubName = pubNameBuffer;
        Printf("Workspace: PUBNAME set to: %s\n", wsState.pubName);
    } else {
        wsState.pubName = NULL;
    }
    
    /* Store cxname if provided */
    if (cxNameArg && cxNameArg[0] != '\0') {
        SNPrintf(cxNameBuffer, sizeof(cxNameBuffer), "%s", cxNameArg);
        wsState.cxName = cxNameBuffer;
        Printf("Workspace: CXNAME set to: %s\n", wsState.cxName);
    } else {
        wsState.cxName = NULL;
    }
    
    /* Store backdrop path if provided */
    if (backdropArg && backdropArg[0] != '\0') {
        SNPrintf(backdropBuffer, sizeof(backdropBuffer), "%s", backdropArg);
        wsState.backdropImagePath = backdropBuffer;
        Printf("Workspace: BACKDROP set to: %s\n", wsState.backdropImagePath);
    } else {
        wsState.backdropImagePath = NULL;
    }
    
    /* Store CX_POPKEY if provided */
    if (cxPopKeyArg && cxPopKeyArg[0] != '\0') {
        SNPrintf(cxPopKeyBuffer, sizeof(cxPopKeyBuffer), "%s", cxPopKeyArg);
        wsState.cxPopKey = cxPopKeyBuffer;
        Printf("Workspace: CX_POPKEY set to: %s\n", wsState.cxPopKey);
    } else {
        wsState.cxPopKey = NULL;
    }
    
    /* Store THEME if provided */
    if (themeArg && themeArg[0] != '\0') {
        SNPrintf(themeBuffer, sizeof(themeBuffer), "%s", themeArg);
        wsState.themeName = themeBuffer;
        Printf("Workspace: THEME set to: %s\n", wsState.themeName);
        
        /* Map theme name to index */
        if (strcmp(themeArg, "dark") == 0 || strcmp(themeArg, "Dark Mode") == 0) {
            wsState.currentTheme = THEME_DARK_MODE;
        } else if (strcmp(themeArg, "sepia") == 0 || strcmp(themeArg, "Sepia") == 0) {
            wsState.currentTheme = THEME_SEPIA;
        } else if (strcmp(themeArg, "blue") == 0 || strcmp(themeArg, "Blue") == 0) {
            wsState.currentTheme = THEME_BLUE;
        } else if (strcmp(themeArg, "green") == 0 || strcmp(themeArg, "Green") == 0) {
            wsState.currentTheme = THEME_GREEN;
        } else {
            /* Default to Like Workbench */
            wsState.currentTheme = THEME_LIKE_WORKBENCH;
        }
    } else {
        wsState.themeName = NULL;
    }
    
    return TRUE;
}

BOOL LoadBackdropImage(STRPTR imagePath)
{
    Object *dtObject = NULL;
    APTR drawHandle = NULL;
    struct RastPort *rp = NULL;
    LONG drawResult;
    WORD screenWidth;
    WORD screenHeight;
    
    if (!imagePath || imagePath[0] == '\0') {
        Printf("Workspace: No backdrop image path provided\n");
        return FALSE;
    }
    
    if (!wsState.backdropWindow || !wsState.workspaceScreen) {
        Printf("Workspace: Window or screen not available for backdrop image\n");
        return FALSE;
    }
    
    if (!DataTypesBase) {
        Printf("Workspace: datatypes.library not available\n");
        return FALSE;
    }
    
    Printf("Workspace: Loading backdrop image: %s\n", imagePath);
    
    /* Create datatype object for the image */
    dtObject = NewDTObject((APTR)imagePath,
                           DTA_GroupID, GID_PICTURE,
                           PDTA_Screen, (ULONG)wsState.workspaceScreen,
                           PDTA_Remap, TRUE,
                           PDTA_DestMode, PMODE_V43,
                           TAG_DONE);
    
    if (!dtObject) {
        LONG errorCode = IoErr();
        Printf("Workspace: Failed to create datatype object (error: %ld)\n", errorCode);
        return FALSE;
    }
    
    Printf("Workspace: Datatype object created successfully\n");
    
    /* Obtain draw info (required before DrawDTObjectA) */
    {
        struct TagItem drawTags[2];
        drawTags[0].ti_Tag = PDTA_Screen;
        drawTags[0].ti_Data = (ULONG)wsState.workspaceScreen;
        drawTags[1].ti_Tag = TAG_DONE;
        drawHandle = ObtainDTDrawInfoA(dtObject, drawTags);
    }
    
    if (!drawHandle) {
        Printf("Workspace: Failed to obtain draw info for backdrop image\n");
        DisposeDTObject(dtObject);
        return FALSE;
    }
    
    Printf("Workspace: Draw info obtained successfully\n");
    
    /* Get window dimensions */
    rp = wsState.backdropWindow->RPort;
    screenWidth = wsState.backdropWindow->Width;
    screenHeight = wsState.backdropWindow->Height;
    
    /* Draw the image to fill the backdrop window */
    drawResult = DrawDTObjectA(rp, dtObject,
                               0, 0,  /* x, y */
                               screenWidth, screenHeight,  /* width, height */
                               0, 0,  /* th, tv */
                               TAG_DONE);
    
    if (!drawResult) {
        Printf("Workspace: Failed to draw backdrop image\n");
        ReleaseDTDrawInfo(dtObject, drawHandle);
        DisposeDTObject(dtObject);
        return FALSE;
    }
    
    Printf("Workspace: Backdrop image drawn successfully\n");
    
    /* Store object and draw handle for cleanup */
    wsState.backdropImageObj = dtObject;
    wsState.backdropDrawHandle = drawHandle;
    
    return TRUE;
}

/* Free backdrop image */
VOID FreeBackdropImage(VOID)
{
    if (wsState.backdropImageObj) {
        /* Release draw info if obtained */
        if (wsState.backdropDrawHandle) {
            ReleaseDTDrawInfo(wsState.backdropImageObj, wsState.backdropDrawHandle);
            wsState.backdropDrawHandle = NULL;
        }
        
        /* Dispose of datatype object */
        DisposeDTObject(wsState.backdropImageObj);
        wsState.backdropImageObj = NULL;
        Printf("Workspace: Backdrop image freed\n");
    }
}

/* Hook function for FormatDate */
ULONG FormatDateHook(struct Hook *hook, ULONG obj, ULONG msg)
{
    UBYTE **buffer = (UBYTE **)hook->h_Data;
    if (buffer && *buffer) {
        **buffer = (UBYTE)msg;
        (*buffer)++;
    }
    return 0;
}

/* Format time and date string using locale */
STRPTR FormatTimeDate(STRPTR buffer, ULONG bufferSize)
{
    struct timeval tv;
    struct ClockData cd;
    struct DateStamp ds;
    STRPTR timeStr = NULL;
    STRPTR dateStr = NULL;
    UBYTE tempBuffer[128];
    UBYTE dateBuffer[64];
    struct Hook dateHook;
    UBYTE *hookBuffer;
    
    if (buffer == NULL || bufferSize == 0) {
        return NULL;
    }
    
    /* Get current time */
    GetSysTime(&tv);
    Amiga2Date(tv.tv_secs, &cd);
    
    /* Convert to DateStamp for locale formatting */
    ds.ds_Days = tv.tv_secs / 86400;
    ds.ds_Minute = (tv.tv_secs % 86400) / 60;
    ds.ds_Tick = ((tv.tv_secs % 86400) % 60) * TICKS_PER_SECOND;
    
    if (LocaleBase) {
        struct Locale *locale = NULL;
        
        /* Get locale if available */
        locale = OpenLocale(NULL);
        
        if (locale) {
            /* Format time using locale with hook */
            hookBuffer = tempBuffer;
            dateHook.h_Entry = (ULONG (*)())FormatDateHook;
            dateHook.h_SubEntry = (ULONG (*)())FormatDateHook;
            dateHook.h_Data = (APTR)&hookBuffer;
            FormatDate(locale, "%H:%M", &ds, &dateHook);
            *hookBuffer = '\0';
            timeStr = tempBuffer;
            
            /* Format date using locale (short format) */
            hookBuffer = dateBuffer;
            dateHook.h_Entry = (ULONG (*)())FormatDateHook;
            dateHook.h_SubEntry = (ULONG (*)())FormatDateHook;
            dateHook.h_Data = (APTR)&hookBuffer;
            FormatDate(locale, "%d-%b", &ds, &dateHook);
            *hookBuffer = '\0';
            dateStr = dateBuffer;
            
            CloseLocale(locale);
        }
    }
    
    /* Fallback if locale not available */
    if (!timeStr) {
        SNPrintf(tempBuffer, sizeof(tempBuffer), "%02ld:%02ld", cd.hour, cd.min);
        timeStr = tempBuffer;
    }
    
    if (!dateStr) {
        static STRPTR monthNames[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        if (cd.month >= 1 && cd.month <= 12) {
            SNPrintf(dateBuffer, sizeof(dateBuffer), "%02ld-%s", cd.mday, monthNames[cd.month - 1]);
        } else {
            SNPrintf(dateBuffer, sizeof(dateBuffer), "%02ld-%02ld", cd.mday, cd.month);
        }
        dateStr = dateBuffer;
    }
    
    /* Combine workspace name, time and date */
    SNPrintf(buffer, bufferSize, "%s  %s %s", wsState.workspaceName, timeStr, dateStr);
    
    return buffer;
}

/* Update screen title bar */
VOID UpdateScreenTitle(VOID)
{
    UBYTE titleBuffer[128];
    STRPTR titleText;
    
    if (wsState.workspaceScreen == NULL) {
        return;
    }
    
    /* Format title with workspace name, time and date */
    titleText = FormatTimeDate(titleBuffer, sizeof(titleBuffer));
    
    if (titleText) {
        /* Set screen title */
        /* Note: Screen title is set via SA_Title tag when opening screen */
        /* For runtime updates, we may need to use a different approach */
        /* For now, we'll store it for potential future use */
    }
}

/* Process commodity messages from Exchange program */
VOID ProcessCommodityMessages(VOID)
{
    CxMsg *cxmsg;
    
    if (CommoditiesBase == NULL || wsState.commodityPort == NULL) {
        return;
    }
    
    while ((cxmsg = (CxMsg *)GetMsg(wsState.commodityPort)) != NULL) {
        if (CxMsgType(cxmsg) & CXM_COMMAND) {
            switch (CxMsgID(cxmsg)) {
                case CXCMD_DISABLE:
                    /* Disable commodity - deactivate broker */
                    if (wsState.commodityActive && wsState.commodityBroker) {
                        /* ActivateCxObj returns previous state */
                        ActivateCxObj(wsState.commodityBroker, FALSE);
                        wsState.commodityActive = FALSE;
                        Printf("Workspace: Commodity disabled\n");
                    }
                    break;
                
                case CXCMD_ENABLE:
                    /* Enable commodity - reactivate broker */
                    if (!wsState.commodityActive && wsState.commodityBroker) {
                        /* ActivateCxObj returns previous state */
                        LONG prevState = ActivateCxObj(wsState.commodityBroker, TRUE);
                        if (prevState == 0) {
                            /* Was inactive, now active - expected */
                            wsState.commodityActive = TRUE;
                            Printf("Workspace: Commodity enabled\n");
                        } else {
                            /* Was already active - unexpected */
                            wsState.commodityActive = TRUE;
                            Printf("Workspace: WARNING - Broker was already active when enabling\n");
                        }
                    }
                    break;
                
                case CXCMD_APPEAR:
                    /* Show/bring workspace screen to front */
                    Printf("Workspace: Received CXCMD_APPEAR\n");
                    if (wsState.workspaceScreen) {
                        ScreenToFront(wsState.workspaceScreen);
                        /* Note: Backdrop windows cannot be depth-arranged, so WindowToFront() is invalid */
                    }
                    break;
                
                case CXCMD_DISAPPEAR:
                    /* Hide workspace - safely ignore for now to prevent lockups */
                    /* Just acknowledge and reply - do not perform any operations */
                    Printf("Workspace: Received CXCMD_DISAPPEAR (ignored)\n");
                    /* Immediately reply to prevent lockup */
                    ReplyMsg((struct Message *)cxmsg);
                    continue; /* Skip the ReplyMsg at end of loop */
                
                case CXCMD_KILL:
                    /* Quit application - check visitors first */
                    Printf("Workspace: Received CXCMD_KILL\n");
                    /* HandleCloseMenu will check visitors and only set quitFlag if allowed */
                    HandleCloseMenu();
                    break;
                
                case CXCMD_UNIQUE:
                    /* Another instance tried to start - show ourselves */
                    Printf("Workspace: Received CXCMD_UNIQUE (another instance tried to start)\n");
                    if (wsState.workspaceScreen) {
                        ScreenToFront(wsState.workspaceScreen);
                        /* Note: Backdrop windows cannot be depth-arranged, so WindowToFront() is invalid */
                    }
                    break;
                
                default:
                    Printf("Workspace: Received unknown commodity command: %ld\n", CxMsgID(cxmsg));
                    break;
            }
        } else if (CxMsgType(cxmsg) & CXM_IEVENT) {
            /* Input event message - check if it's from our hotkey filter */
            if (CxMsgID(cxmsg) == 1) {
                /* This is from our sender (ID 1) - hotkey was pressed */
                Printf("Workspace: Hotkey pressed - bringing screen to front\n");
                if (wsState.workspaceScreen) {
                    ScreenToFront(wsState.workspaceScreen);
                }
            }
        }
        
        /* Always reply to the message */
        ReplyMsg((struct Message *)cxmsg);
    }
}

/* Parse tooltypes */
VOID ParseToolTypes(VOID)
{
    /* TODO: Implement tooltype parsing */
    /* Tooltypes to support:
     * - SHELL/S or SHELLENABLED/S: Enable shell console
     * - SHELLPATH/K: Path to shell (default: CON:...)
     * - BACKDROP/K: Path to backdrop image
     */
}

/* Get tooltype value */
BOOL GetToolType(STRPTR toolType, STRPTR defaultValue, STRPTR buffer, ULONG bufferSize)
{
    /* TODO: Implement tooltype retrieval */
    if (defaultValue) {
        Strncpy(buffer, defaultValue, bufferSize);
        return TRUE;
    }
    return FALSE;
}

/* Initialize timer device for minute updates */
BOOL InitializeTimer(VOID)
{
    TimerPort = CreateMsgPort();
    if (TimerPort == NULL) {
        return FALSE;
    }
    
    TimerIO = (struct timerequest *)CreateIORequest(TimerPort, sizeof(struct timerequest));
    if (TimerIO == NULL) {
        DeleteMsgPort(TimerPort);
        TimerPort = NULL;
        return FALSE;
    }
    
    if (OpenDevice("timer.device", UNIT_VBLANK, (struct IORequest *)TimerIO, 0) != 0) {
        DeleteIORequest((struct IORequest *)TimerIO);
        TimerIO = NULL;
        DeleteMsgPort(TimerPort);
        TimerPort = NULL;
        return FALSE;
    }
    
    TimerBase = TimerIO->tr_node.io_Device;
    
    /* Send initial timer request for 60 seconds */
    TimerIO->tr_node.io_Command = TR_ADDREQUEST;
    TimerIO->tr_node.io_Flags = 0;
    TimerIO->tr_time.tv_secs = 60;
    TimerIO->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)TimerIO);
    
    return TRUE;
}

/* Cleanup timer device */
VOID CleanupTimer(VOID)
{
    if (TimerIO != NULL) {
        AbortIO((struct IORequest *)TimerIO);
        WaitIO((struct IORequest *)TimerIO);
        CloseDevice((struct IORequest *)TimerIO);
        DeleteIORequest((struct IORequest *)TimerIO);
        TimerIO = NULL;
        TimerBase = NULL;
    }
    
    if (TimerPort != NULL) {
        DeleteMsgPort(TimerPort);
        TimerPort = NULL;
    }
}

/* Cleanup libraries */
VOID Cleanup(VOID)
{
    if (InputIO != NULL) {
        CloseDevice((struct IORequest *)InputIO);
        DeleteIORequest((struct IORequest *)InputIO);
        InputIO = NULL;
        InputBase = NULL;
    }
    if (InputPort != NULL) {
        DeleteMsgPort(InputPort);
        InputPort = NULL;
    }
        
    if (CommoditiesBase) {
        CloseLibrary(CommoditiesBase);
        CommoditiesBase = NULL;
    }
    
    if (DataTypesBase) {
        CloseLibrary(DataTypesBase);
        DataTypesBase = NULL;
    }
    
    if (LocaleBase) {
        CloseLibrary(LocaleBase);
        LocaleBase = NULL;
    }
    
    if (WorkbenchBase) {
        CloseLibrary(WorkbenchBase);
        WorkbenchBase = NULL;
    }
    
    if (IconBase) {
        CloseLibrary(IconBase);
        IconBase = NULL;
    }
    
    if (GfxBase) {
        CloseLibrary(GfxBase);
        GfxBase = NULL;
    }
    
    if (UtilityBase) {
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
    }
    
    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}


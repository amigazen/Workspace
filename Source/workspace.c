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
VOID CloseWorkspaceScreen(VOID);
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
VOID HandleCloseMenu(VOID);
VOID HandleShellConsoleMenu(VOID);
BOOL CheckWorkspaceVisitors(VOID);
VOID HandleSetAsDefaultMenu(struct MenuItem *menuItem);
VOID HandleDefaultPubScreenSubMenu(STRPTR screenName);
struct NewMenu *BuildDefaultPubScreenMenu(ULONG *menuCount);
BOOL GetToolType(STRPTR toolType, STRPTR defaultValue, STRPTR buffer, ULONG bufferSize);
BOOL InitializeTimer(VOID);
VOID CleanupTimer(VOID);
STRPTR FormatTimeDate(STRPTR buffer, ULONG bufferSize);

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
};

static struct WorkspaceState wsState;

/* Tooltype defaults */
/* Note: Default uses WINDOW parameter - user can override with custom path */
/* For custom path, use %p for window pointer in hex format */
static STRPTR defaultShellPath = NULL;  /* Will use WINDOW parameter by default */
static BOOL defaultShellEnabled = FALSE;
static STRPTR defaultBackdropImage = NULL;

/* Main entry point */
int main(int argc, char *argv[])
{
    struct WBStartup *wbs = NULL;
    struct DiskObject *icon = NULL;
    BOOL fromWorkbench = FALSE;
    
    Printf("Workspace: Starting application\n");
    
    /* Initialize state */
    memset(&wsState, 0, sizeof(struct WorkspaceState));
    wsState.instanceNumber = 1;
    wsState.quitFlag = FALSE;
    wsState.commodityActive = FALSE;
    wsState.isDefaultScreen = FALSE;
    wsState.mainTask = (struct Task *)FindTask(NULL);
    
    Printf("Workspace: State initialized\n");
    
    /* Check if running from Workbench */
    fromWorkbench = (argc == 0);
    Printf("Workspace: Running from Workbench: %s\n", fromWorkbench ? "YES" : "NO");
    
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
    Printf("Workspace: Window UserPort signal bit: %ld\n", 
           wsState.backdropWindow ? wsState.backdropWindow->UserPort->mp_SigBit : -1);
    Printf("Workspace: Commodity port signal bit: %ld\n",
           wsState.commodityPort ? wsState.commodityPort->mp_SigBit : -1);
    Printf("Workspace: Timer port signal bit: %ld\n",
           TimerPort ? TimerPort->mp_SigBit : -1);
    
    while (!wsState.quitFlag) {
        ULONG signals;
        ULONG windowSignal = 0;
        ULONG expectedSignals;
        BOOL done = FALSE;
        ULONG timerSignal = 0;
        
        /* Wait for messages - check if backdrop window exists */
        if (wsState.backdropWindow == NULL) {
            Printf("Workspace: ERROR - Backdrop window is NULL, exiting\n");
            wsState.quitFlag = TRUE;
            break;
        }
        
        /* Check if shell window was closed by console (when shell ends) */
        if (wsState.shellWindow != NULL && wsState.shellWindow->UserPort == NULL) {
            /* Shell window was closed by console - shell has ended */
            Printf("Workspace: Shell console ended - shell window was closed by console\n");
            wsState.shellWindow = NULL;  /* Clear pointer */
            wsState.shellEnabled = FALSE;  /* Reset shell enabled flag */
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
        expectedSignals = windowSignal |
                         (wsState.commodityPort ? (1L << wsState.commodityPort->mp_SigBit) : 0) |
                         timerSignal |
                         SIGBREAKF_CTRL_C;
        
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
            userPort = wsState.backdropWindow ? wsState.backdropWindow->UserPort : NULL;
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
                            
                            Printf("Workspace: IDCMP_MENUPICK received, menuCode=0x%04X\n", menuCode);
                            
                            while (menuCode != MENUNULL) {
                                item = ItemAddress(wsState.menuStrip, menuCode);
                                if (item) {
                                    Printf("Workspace: Found menu item at 0x%08lX\n", (ULONG)item);
                                    /* Get menu/item/sub numbers from UserData (GadTools stores it there) */
                                    if (GTMENUITEM_USERDATA(item)) {
                                        ULONG userData = (ULONG)GTMENUITEM_USERDATA(item);
                                        ULONG menuNumber = (userData >> 16) & 0xFF;
                                        ULONG itemNumber = (userData >> 8) & 0xFF;
                                        ULONG subNumber = userData & 0xFF;
                                        
                                        Printf("Workspace: Menu item - menuNumber=%lu, itemNumber=%lu, subNumber=%lu, Flags=0x%04X\n",
                                               menuNumber, itemNumber, subNumber, item->Flags);
                                        
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
                                                        HandleCloseMenu();
                                                        done = TRUE;
                                                        break;
                                                    
                                                    case 3:  /* Shell Console */
                                                        HandleShellConsoleMenu();
                                                        break;
                                                    
                                                    default:
                                                        Printf("Workspace: Unknown menu item number: %lu\n", itemNumber);
                                                        break;
                                                }
                                            }
                                        } else {
                                            Printf("Workspace: Menu number not 0: %lu\n", menuNumber);
                                        }
                                    } else {
                                        Printf("Workspace: WARNING - Menu item has no UserData\n");
                                    }
                                    menuCode = item->NextSelect;
                                } else {
                                    Printf("Workspace: WARNING - ItemAddress returned NULL for menuCode=0x%04X\n", menuCode);
                                    break;
                                }
                            }
                        }
                        ReplyMsg(msg);
                        if (done) {
                            break;
                        }
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
        
        if (done) {
            break;
        }
    }
    
    /* Cleanup - order is important */
    if (wsState.shellEnabled) {
        CloseShellConsole();
    }
    
    FreeBackdropImage();
    /* CloseBackdropWindow will call ClearMenuStrip, so do it before FreeMenuStrip */
    CloseBackdropWindow();
    /* Now free the menu structure */
    FreeMenuStrip();
    CloseWorkspaceScreen();
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
            Printf("Workspace: WARNING - Broker created but has errors (0x%08lX), continuing without commodity support\n", objError);
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
                Printf("Workspace: WARNING - Filter has errors (0x%08lX)\n", filterError);
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
            Printf("Workspace: WARNING - Screen was already public or error (status: 0x%04X)\n", statusResult);
            /* Continue anyway */
        }
    }
    
    /* Check dimensions after making public */
        Printf("Workspace: After making public - Width: %ld, Height: %ld\n",
               (LONG)newScreen->Width, (LONG)newScreen->Height);
    
    wsState.workspaceScreen = newScreen;
    
    return TRUE;
}

/* Close workspace screen - retries until CloseScreen succeeds */
VOID CloseWorkspaceScreen(VOID)
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
        return;
    }
    
    /* Retry loop until CloseScreen succeeds */
    while (!closeSucceeded) {
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
                    Printf("Workspace: Screen '%s' has %d visitor windows\n", 
                           psn->psn_Node.ln_Name, (int)psn->psn_VisitorCount);
                }
                psn = (struct PubScreenNode *)psn->psn_Node.ln_Succ;
            }
            UnlockPubScreenList();
        }
        
        Printf("Workspace: Total visitor windows on all Workspace screens: %d\n", (int)visitorCount);
        
        /* Check if we can close - no visitors allowed */
        if (visitorCount > 0) {
            /* Show EasyRequest dialog warning user */
            titleStr = "Cannot Exit Workspace";
            textStr = "Cannot exit Workspace.\n\nAll windows on Workspace screens must be closed before exiting.";
            okStr = "OK";
            
            es.es_StructSize = sizeof(struct EasyStruct);
            es.es_Flags = 0;
            es.es_Title = titleStr;
            es.es_TextFormat = textStr;
            es.es_GadgetFormat = okStr;
            
            EasyRequestArgs(wsState.backdropWindow, &es, NULL, NULL);
            Printf("Workspace: Cannot close - %d visitor windows still open, retrying...\n", (int)visitorCount);
            /* Loop will continue and check again */
            continue;
        }
        
        /* No visitors - try to close screen */
        /* Take screen private before closing */
        {
            UWORD statusResult = PubScreenStatus(wsState.workspaceScreen, PSNF_PRIVATE);
            if ((statusResult & 0x0001) == 0) {
                /* Bit 0 = 0 means can't make private (visitors are open) */
                Printf("Workspace: WARNING - Could not make screen private (status: 0x%04X), may have visitors\n", statusResult);
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
            textStr = "Cannot close Workspace screen.\n\nAll windows on this screen must be closed before exiting.";
            okStr = "OK";
            
            es.es_StructSize = sizeof(struct EasyStruct);
            es.es_Flags = 0;
            es.es_Title = titleStr;
            es.es_TextFormat = textStr;
            es.es_GadgetFormat = okStr;
            
            EasyRequestArgs(wsState.backdropWindow, &es, NULL, NULL);
            Printf("Workspace: CloseScreen failed, retrying...\n");
            /* Loop will continue and retry */
        }
    }
    
    /* Only free draw info after successful close */
    if (wsState.drawInfo) {
        FreeScreenDrawInfo(wsState.workspaceScreen, wsState.drawInfo);
        wsState.drawInfo = NULL;
    }
    
    wsState.workspaceScreen = NULL;
    Printf("Workspace: Screen closed successfully\n");
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
        Printf("Workspace: Using ViewPort dimensions: Width=%lu, Height=%lu\n",
               screenWidth, screenHeight);
    } else {
        screenWidth = wsState.workspaceScreen->Width;
        screenHeight = wsState.workspaceScreen->Height;
        Printf("Workspace: Using Screen dimensions: Width=%lu, Height=%lu\n",
               screenWidth, screenHeight);
    }
    
    /* Calculate title bar height - BarHeight is one less than actual height */
    titleBarHeight = wsState.workspaceScreen->BarHeight + 1;
    windowTop = titleBarHeight;
    windowHeight = screenHeight - titleBarHeight;
    
    Printf("Workspace: Screen BarHeight=%d, TitleBarHeight=%d\n", 
           wsState.workspaceScreen->BarHeight, titleBarHeight);
    Printf("Workspace: Creating window: Left=0, Top=%d, Width=%lu, Height=%d\n", 
           windowTop, screenWidth, windowHeight);
    
    /* Validate dimensions - OpenWindowTags will fail or create invalid window if dimensions are 0 */
    if (screenWidth <= 0 || windowHeight <= 0) {
        Printf("Workspace: ERROR - Invalid window dimensions: Width=%lu, Height=%d\n",
               screenWidth, windowHeight);
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
    
    Printf("Workspace: Window opened successfully: 0x%08lX\n", (ULONG)wsState.backdropWindow);
    Printf("Workspace: Window actual dimensions: LeftEdge=%ld, TopEdge=%ld, Width=%ld, Height=%ld\n",
           (LONG)wsState.backdropWindow->LeftEdge, (LONG)wsState.backdropWindow->TopEdge,
           (LONG)wsState.backdropWindow->Width, (LONG)wsState.backdropWindow->Height);
    Printf("Workspace: Window Flags: 0x%08lX\n", wsState.backdropWindow->Flags);
    
    /* Check if window was created with valid dimensions */
    if (wsState.backdropWindow->Width == 0 || wsState.backdropWindow->Height == 0) {
        Printf("Workspace: ERROR - Window created with invalid dimensions (Width=%ld, Height=%ld)\n",
               (LONG)wsState.backdropWindow->Width, (LONG)wsState.backdropWindow->Height);
        CloseWindow(wsState.backdropWindow);
        wsState.backdropWindow = NULL;
        return FALSE;
    }
    Printf("Workspace: Window UserPort: 0x%08lX, Signal bit: %ld\n", 
           (ULONG)wsState.backdropWindow->UserPort,
           wsState.backdropWindow->UserPort ? wsState.backdropWindow->UserPort->mp_SigBit : -1);
    
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
    /* TODO: Show about requester */
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
BOOL CheckWorkspaceVisitors(VOID)
{
    struct List *pubScreenList = NULL;
    struct PubScreenNode *psn = NULL;
    WORD totalVisitors = 0;
    
    /* Lock public screen list */
    pubScreenList = LockPubScreenList();
    if (!pubScreenList) {
        Printf("Workspace: WARNING - Could not lock public screen list\n");
        return FALSE; /* Assume no visitors if we can't check */
    }
    
    /* Iterate through all public screens - correct Exec list iteration */
    psn = (struct PubScreenNode *)pubScreenList->lh_Head;
    while (psn && psn->psn_Node.ln_Succ != (struct Node *)&pubScreenList->lh_Tail) {
        /* Check if this is a Workspace screen (starts with "Workspace.") */
        if (psn->psn_Node.ln_Name && 
            strncmp(psn->psn_Node.ln_Name, "Workspace.", 10) == 0) {
            totalVisitors += (WORD)psn->psn_VisitorCount;
            Printf("Workspace: Screen '%s' has %d visitor windows\n", 
                   psn->psn_Node.ln_Name, (int)psn->psn_VisitorCount);
        }
        psn = (struct PubScreenNode *)psn->psn_Node.ln_Succ;
    }
    
    UnlockPubScreenList();
    
    Printf("Workspace: Total visitor windows on all Workspace screens: %d\n", (int)totalVisitors);
    if (totalVisitors > 0) {
        return TRUE;
    } else {
        return FALSE;
    }
}

VOID HandleCloseMenu(VOID)
{
    /* Check for visitor windows before allowing quit */
    if (CheckWorkspaceVisitors()) {
        /* Show EasyRequest dialog warning user */
        struct EasyStruct es;
        STRPTR titleStr = "Cannot Exit Workspace";
        STRPTR textStr = "Cannot exit Workspace.\n\nAll windows on Workspace screens must be closed before exiting.";
        STRPTR okStr = "OK";
        
        es.es_StructSize = sizeof(struct EasyStruct);
        es.es_Flags = 0;
        es.es_Title = titleStr;
        es.es_TextFormat = textStr;
        es.es_GadgetFormat = okStr;
        
        EasyRequestArgs(wsState.backdropWindow, &es, NULL, NULL);
        Printf("Workspace: Quit prevented - visitor windows still open\n");
        return; /* Don't set quitFlag - prevent cleanup */
    }
    
    /* No visitors - safe to quit */
    wsState.quitFlag = TRUE;
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
    newMenu[idx].nm_Label = "Quit";
    newMenu[idx].nm_CommKey = "Q";
    newMenu[idx].nm_UserData = (APTR)((0UL << 16) | (2UL << 8) | 0UL); /* Menu 0, Item 2, Sub 0 */
    idx++;
    
    /* Terminate menu */
    newMenu[idx].nm_Type = NM_END;
    
    *menuCount = idx;
    Printf("Workspace: Built menu with %lu items (%lu Workspace screens)\n", idx, count);
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
    
    Printf("Workspace: Menu strip verified in window (MenuStrip=0x%08lX)\n", (ULONG)wsState.backdropWindow->MenuStrip);
    
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
        Printf("Workspace: ERROR - Invalid dimensions for shell window (Width=%lu, Height=%d, Top=%d)\n",
               screenWidth, windowHeight, windowTop);
        return FALSE;
    }
    
    Printf("Workspace: Creating shell window: Left=0, Top=%d, Width=%lu, Height=%d\n",
           windowTop, screenWidth, windowHeight);
    
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
    
    Printf("Workspace: Shell window opened successfully: 0x%08lX\n", (ULONG)wsState.shellWindow);
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
        Printf("Workspace: ERROR - Invalid dimensions for shell console (Width=%d, Height=%d)\n",
               windowWidth, windowHeight);
        return FALSE;
    }
    
    Printf("Workspace: Shell console dimensions - width=%d, height=%d\n",
           windowWidth, windowHeight);
    
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
            Printf("Workspace: Shell window pointer: 0x%08lX\n", windowAddr);
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
    
    if (result == 0) {
        Printf("Workspace: ERROR - Failed to create shell console (System returned 0)\n");
        return FALSE;
    }
    
    /* IMPORTANT: When using WINDOW parameter, the console takes ownership of the window */
    /* The console will close the window when it exits */
    /* Don't set shellWindow to NULL - we'll detect when it's closed by checking UserPort */
    Printf("Workspace: Shell console launched successfully - shell window ownership transferred to console\n");
    Printf("Workspace: Note - when shell ends, console will close the shell window\n");
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
    Printf("Workspace: Shell console cleanup complete\n");
}

/* Load backdrop image */
/* Parse command line arguments */
BOOL ParseCommandLine(VOID)
{
    LONG argArray[4];
    STRPTR pubNameArg = NULL;
    STRPTR cxNameArg = NULL;
    STRPTR backdropArg = NULL;
    STRPTR cxPopKeyArg = NULL;
    static UBYTE pubNameBuffer[64];
    static UBYTE cxNameBuffer[64];
    static UBYTE backdropBuffer[256];
    static UBYTE cxPopKeyBuffer[64];
    
    /* Initialize arg array */
    argArray[0] = 0;
    argArray[1] = 0;
    argArray[2] = 0;
    argArray[3] = 0;
    
    /* Clear IoErr before ReadArgs */
    SetIoErr(0);
    
    /* Parse arguments: PUBNAME/K, CX_NAME/K, BACKDROP/K, CX_POPKEY/K */
    wsState.rda = ReadArgs("PUBNAME/K,CX_NAME/K,BACKDROP/K,CX_POPKEY/K", argArray, NULL);
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
        return TRUE; /* Not a fatal error */
    }
    
    /* Extract arguments */
    pubNameArg = (STRPTR)argArray[0];
    cxNameArg = (STRPTR)argArray[1];
    backdropArg = (STRPTR)argArray[2];
    cxPopKeyArg = (STRPTR)argArray[3];
    
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
                    HandleCloseMenu(); /* This will check visitors and show dialog if needed */
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


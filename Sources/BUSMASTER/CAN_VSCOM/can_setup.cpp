/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file      can_setup.cpp
 * \brief     Source file for Can_setup definitions
 * \author
 * \copyright Copyright (c) 2012, ETAS GmbH. All rights reserved.
 *
 * Source file for Can_setup definitions
 */

#include "CAN_VSCOM_stdafx.h"
#include <windows.h>
#include <basetyps.h>
#include <commctrl.h>
#include <windowsx.h>
#include <setupapi.h>
#include "CAN_VSCOM.h"
#include "resource.h"
#include "EXTERNAL/vs_can_api.h"

#pragma comment(lib, "setupapi.lib")


#define MAX_DEVICES         256

#define VSCAN_DEVICE_TYPE_UNKNOWN   0
#define VSCAN_DEVICE_TYPE_SERIAL    1
#define VSCAN_DEVICE_TYPE_USB       2
#define VSCAN_DEVICE_TYPE_NET_VCOM  3
#define VSCAN_DEVICE_TYPE_NET_IP    4
#define VSCAN_DEVICE_TYPE_BUS       5   // Ocera LinCAN, PCI, AM3517


typedef struct
{
    DWORD InitDone;
    VSCAN_HANDLE Handle;            // ListElement + 1
    char Location[MAX_PATH + 1];    // ComPort or NetworkAddress
    UINT32 SerialNr;
    UINT8 HwVersion;
    UINT8 SwVersion;
    int Type;                       // Type of the Device

    HANDLE hComm;                   // Handle for the ComPort

    SOCKET s;                       // Descriptor of the socket
    char Hostname[256];
    USHORT Port;

    BOOL bOverlapped;
    OVERLAPPED ovWrite;
    OVERLAPPED ovRead;

    char* RecvBuf;                  // Receive Buffer
    //pthread_t ptReadThread;

    DWORD dwChannelOpen;
    DWORD dwChannelOpenMode;
    DWORD dwSuspendReadThread;
    DWORD dwSuspendedReadThread;
    DWORD dwDontRecv;               // Thread shouldn't receive data
    VSCAN_MSG* RecvMsgs;            // Received Messages
    DWORD dwRecvMsgsSize;           // Number of Received Messages
    VSCAN_MSG* RecvMsgsHead;        // the last Received Message
    VSCAN_MSG* RecvMsgsTail;        // the first Received Message
    //pthread_mutex_t ptRecvMsgsMutex;// Mutex for the Receiving

    DWORD Status;                   // Status for which Information is set
    DWORD Flags;                    // the last Flags from the Device
} VSCAN_DEVICE;

typedef VSCAN_DEVICE* VSCAN_DEVICE_LIST;
static struct _VSCanCfg* CanCfg = nullptr;
static HWND g_hDlg = nullptr;
static VSCAN_DEVICE_LIST g_list[MAX_DEVICES];
static DWORD g_dwListCnt = 0;

/* Progress timer for the device search */
#define IDT_SEARCH_PROGRESS    1

/* Label of the search button, as laid out in the dialog resource */
#define SEARCH_BUTTON_TEXT     "Search for Devices\non COM-Ports"

/**
 * One device search. The vendor API's blocking EnumDevices2List scan used
 * to run on the thread that pumps this dialog, freezing the whole
 * application for minutes (a single Bluetooth serial link can block the
 * port open for around a minute, and the vendor's network phase blocks
 * as well). The search now enumerates the COM ports itself and probes
 * them on a worker thread while the dialog stays responsive.
 *
 * The dialog creates the job, fills the port list and starts the worker.
 * From then on only the worker writes the progress and result fields,
 * until it publishes the result (or notices that the dialog gave up).
 * Only the dialog thread ever writes g_list/g_dwListCnt: it copies the
 * published result out of the job. That keeps the search off the UI
 * thread without any lock, and lets a stuck probe be abandoned without
 * corrupting anything.
 */
typedef struct tagSEARCH_JOB
{
    /* input, filled by the dialog before the worker starts */
    char acPorts[MAX_DEVICES][16];
    DWORD dwPortCount;
    HANDLE hThread;                // worker thread handle (dialog closes it)

    /* progress, written by the worker while it runs */
    DWORD dwPortIndex;
    char acPortName[16];

    /* results, written by the worker until it publishes them */
    VSCAN_DEVICE* list[MAX_DEVICES];
    DWORD dwFound;

    /* ownership handoff between worker and dialog, see AbandonSearchJob */
    volatile LONG lClaimed;
} SEARCH_JOB;

static SEARCH_JOB* g_pActiveJob = nullptr;   /* dialog thread only */

/**
 * \brief    Frees the entries of the device list.
 *
 * The entries are allocated with malloc by this module's search worker,
 * so they must not be passed to the vendor API's FreeDeviceList. Only
 * the dialog thread touches g_list/g_dwListCnt, so this is safe to call
 * whenever the dialog wants to let go of the current result.
 */
static void FreeDeviceListContents(void)
{
    DWORD i;

    for (i = 0; i < g_dwListCnt; i++)
    {
        free(g_list[i]);
        g_list[i] = nullptr;
    }
    g_dwListCnt = 0;
}

/**
 * Releases the job's thread handle and the job itself, keeping the found
 * device entries (their ownership moves elsewhere).
 */
static void FinishSearchJob(SEARCH_JOB* pJob)
{
    if (pJob->hThread != nullptr)
    {
        CloseHandle(pJob->hThread);
    }
    free(pJob);
}

/**
 * Releases the job and the device entries it found.
 */
static void FreeSearchJob(SEARCH_JOB* pJob)
{
    DWORD i;

    for (i = 0; i < pJob->dwFound; i++)
    {
        free(pJob->list[i]);
    }
    FinishSearchJob(pJob);
}

/**
 * \brief    The dialog gives up waiting for the running search.
 *
 * Exactly one side ends up freeing the job, decided atomically via
 * lClaimed: if the worker already published its result (the exchange
 * below yields 2), the dialog owns everything and frees it here;
 * otherwise the worker is told to clean up itself when it stops (a
 * probe that blocks forever inside the port driver only leaks the job
 * until that call returns, and never blocks the dialog).
 */
static void AbandonSearchJob(void)
{
    SEARCH_JOB* pJob = g_pActiveJob;

    if (pJob == nullptr)
    {
        return;
    }
    if (InterlockedExchange(&pJob->lClaimed, 1) != 0)
    {
        /* the worker finished and published the result */
        FreeSearchJob(pJob);
    }
    /* else: the worker will free the job when it stops */
    g_pActiveJob = nullptr;
}

/**
 * \brief    Collects the COM ports present on the system.
 *
 * \return   TRUE when the enumeration succeeded (also when it found no
 *           port), FALSE when the setup API could not be opened. The
 *           enumeration itself never blocks.
 *
 * Bluetooth serial links are skipped deliberately: opening such a port
 * blocks for around a minute inside the Bluetooth stack when the paired
 * device is not reachable, and it cannot carry a VScom CAN device.
 */
static BOOL CollectComPorts(SEARCH_JOB* pJob)
{
    /* Ports device setup class {4D36E978-E325-11CE-BFC1-08002BE10318} */
    static const GUID sGuidPorts =
    {
        0x4D36E978, 0xE325, 0x11CE,
        { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 }
    };
    SP_DEVINFO_DATA sDevInfo;
    DWORD i;

    HDEVINFO hDevInfo = SetupDiGetClassDevs(&sGuidPorts, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    sDevInfo.cbSize = sizeof(sDevInfo);
    for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &sDevInfo); i++)
    {
        char acInstanceId[MAX_PATH] = {'\0'};
        char acFriendly[MAX_PATH] = {'\0'};
        char acPort[16] = {'\0'};
        DWORD dwSize = 0;
        DWORD dwType = 0;
        HKEY hKey;

        if (SetupDiGetDeviceInstanceId(hDevInfo, &sDevInfo, acInstanceId,
                                       sizeof(acInstanceId), nullptr)
                && (_strnicmp(acInstanceId, "BTHENUM\\", 8) == 0))
        {
            continue;   // Bluetooth serial link - see function comment
        }
        if (SetupDiGetDeviceRegistryProperty(hDevInfo, &sDevInfo, SPDRP_FRIENDLYNAME,
                                             nullptr, (PBYTE) acFriendly,
                                             sizeof(acFriendly), &dwSize)
                && (strstr(acFriendly, "Bluetooth") != nullptr))
        {
            continue;   // Bluetooth serial link under a vendor driver
        }

        hKey = SetupDiOpenDevRegKey(hDevInfo, &sDevInfo, DICS_FLAG_GLOBAL, 0,
                                    DIREG_DEV, KEY_QUERY_VALUE);
        if (hKey == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        dwSize = sizeof(acPort);
        if ((RegQueryValueEx(hKey, "PortName", nullptr, &dwType, (LPBYTE) acPort,
                            &dwSize) == ERROR_SUCCESS)
                && (dwType == REG_SZ)
                && (_strnicmp(acPort, "COM", 3) == 0)
                && (acPort[3] != '\0'))
        {
            if (pJob->dwPortCount < MAX_DEVICES)
            {
                strcpy_s(pJob->acPorts[pJob->dwPortCount],
                         sizeof(pJob->acPorts[0]), acPort);
                pJob->dwPortCount++;
            }
        }
        RegCloseKey(hKey);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return TRUE;
}

/**
 * \brief    Probes the collected COM ports, on a worker thread.
 *
 * Probing a port opens it through the VSCAN API, which can block for a
 * long time inside the port driver (a Bluetooth serial link without its
 * peer can block forever), so it must never run on the thread that
 * pumps the dialog. The worker fills only its own job: when it is done
 * it atomically hands the job back to the dialog; if the dialog gave up
 * on it in the meantime, this thread cleans up the job itself.
 */
static DWORD WINAPI DeviceSearchThread(LPVOID pVoid)
{
    SEARCH_JOB* pJob = (SEARCH_JOB*) pVoid;
    DWORD i;

    for (i = 0; i < pJob->dwPortCount; i++)
    {
        char acLocation[MAX_PATH + 1];

        if (pJob->lClaimed != 0)
        {
            break;   // the dialog gave up on this search
        }

        sprintf_s(acLocation, sizeof(acLocation), "\\\\.\\%s", pJob->acPorts[i]);
        strcpy_s(pJob->acPortName, sizeof(pJob->acPortName), pJob->acPorts[i]);
        pJob->dwPortIndex = i + 1;

        VSCAN_HANDLE hDevice = VSCAN_Open(acLocation, VSCAN_MODE_NORMAL);
        if (hDevice > 0)
        {
            VSCAN_HWPARAM sHwParam;

            if (VSCAN_Ioctl(hDevice, VSCAN_IOCTL_GET_HWPARAM, &sHwParam) == VSCAN_ERR_OK)
            {
                VSCAN_DEVICE* pDevice = (VSCAN_DEVICE*) calloc(1, sizeof(VSCAN_DEVICE));
                if (pDevice != nullptr)
                {
                    pDevice->InitDone = 1;
                    strcpy_s(pDevice->Location, sizeof(pDevice->Location), acLocation);
                    pDevice->SerialNr = sHwParam.SerialNr;
                    pDevice->HwVersion = sHwParam.HwVersion;
                    pDevice->SwVersion = sHwParam.SwVersion;
                    switch (sHwParam.HwType)
                    {
                        case VSCAN_HWTYPE_SERIAL:
                            pDevice->Type = VSCAN_DEVICE_TYPE_SERIAL;
                            break;
                        case VSCAN_HWTYPE_USB:
                            pDevice->Type = VSCAN_DEVICE_TYPE_USB;
                            break;
                        case VSCAN_HWTYPE_NET:
                            pDevice->Type = VSCAN_DEVICE_TYPE_NET_VCOM;
                            break;
                        case VSCAN_HWTYPE_BUS:
                            pDevice->Type = VSCAN_DEVICE_TYPE_BUS;
                            break;
                        default:
                            pDevice->Type = VSCAN_DEVICE_TYPE_UNKNOWN;
                            break;
                    }
                    pJob->list[pJob->dwFound] = pDevice;
                    pJob->dwFound++;
                }
            }
            VSCAN_Close(hDevice);

            if (pJob->dwFound >= MAX_DEVICES)
            {
                break;
            }
        }
    }

    /* Hand the result back to the dialog, or clean up when the dialog
       stopped waiting (see AbandonSearchJob) */
    if (InterlockedExchange(&pJob->lClaimed, 2) != 0)
    {
        FreeSearchJob(pJob);
    }
    return 0;
}

/**
 * Ends the setup dialog, stopping any search that is still running.
 */
static void CloseSetupDialog(HWND hDlg, INT_PTR nResult)
{
    AbandonSearchJob();
    FreeDeviceListContents();
    KillTimer(hDlg, IDT_SEARCH_PROGRESS);
    EndDialog(hDlg, nResult);
}


static void PrintDeviceInfo(int nDeviceNr)
{
    char temp[32];

    if (nDeviceNr == -1)
    {
        SetDlgItemText(g_hDlg, IDC_DEVICE_TYPE, "Unknown");
        SetDlgItemText(g_hDlg, IDC_DEVICE_SERIAL, "Unknown");
        SetDlgItemText(g_hDlg, IDC_DEVICE_SW_VERSION, "Unknown");
        SetDlgItemText(g_hDlg, IDC_DEVICE_HW_VERSION, "Unknown");
        return;
    }

    if (!g_dwListCnt || (int)g_dwListCnt <= nDeviceNr)
    {
        return;
    }

    switch (g_list[nDeviceNr]->Type)
    {
        case VSCAN_DEVICE_TYPE_SERIAL:
            strcpy(temp, "SER-CAN");
            break;
        case VSCAN_DEVICE_TYPE_USB:
            strcpy(temp, "USB-CAN");
            break;
        case VSCAN_DEVICE_TYPE_BUS:
            strcpy(temp, "PCI-CAN");
            break;
        case VSCAN_DEVICE_TYPE_NET_VCOM:
            strcpy(temp, "NET-CAN");
            break;
        default:
            strcpy(temp, "Unknown");
            break;
    }
    SetDlgItemText(g_hDlg, IDC_DEVICE_TYPE, temp);

    sprintf(temp, "%d", g_list[nDeviceNr]->SerialNr);
    SetDlgItemText(g_hDlg, IDC_DEVICE_SERIAL, temp);

    sprintf(temp, "%d.%d", g_list[nDeviceNr]->SwVersion >> 4, g_list[nDeviceNr]->SwVersion & 0xf);
    SetDlgItemText(g_hDlg, IDC_DEVICE_SW_VERSION, temp);

    sprintf(temp, "%d.%d", g_list[nDeviceNr]->HwVersion >> 4, g_list[nDeviceNr]->HwVersion & 0xf);
    SetDlgItemText(g_hDlg, IDC_DEVICE_HW_VERSION, temp);
}

/**
 * Fills the device combo box from the search result in g_list. When the
 * search found no device, what the user typed into the combo box (e.g. a
 * network address) is kept.
 */
static void RefreshDeviceList(void)
{
    char acPrevious[MAX_PATH];
    int i;

    GetDlgItemText(g_hDlg, IDC_DEVICE_LIST, acPrevious, sizeof(acPrevious));

    SendDlgItemMessage(g_hDlg, IDC_DEVICE_LIST, CB_RESETCONTENT, 0, 0);

    for (i = 0; i < (int)g_dwListCnt; i++)
    {
        const char* pcName = strrchr(g_list[i]->Location, '\\');
        if (pcName != nullptr)
        {
            SendDlgItemMessage(g_hDlg, IDC_DEVICE_LIST, CB_ADDSTRING, 0, (LPARAM)(pcName + 1));
        }
    }

    if (g_dwListCnt > 0)
    {
        SendDlgItemMessage(g_hDlg, IDC_DEVICE_LIST, CB_SETCURSEL, 0, 0);
        PrintDeviceInfo(0);
    }
    else
    {
        SetDlgItemText(g_hDlg, IDC_DEVICE_LIST, acPrevious);
        PrintDeviceInfo(-1);
    }
}

static void InitBaudrateList(void)
{
    int i, cnt;

    cnt = SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_GETCOUNT, 0, 0);
    for (i = 0; i < cnt; i++)
    {
        SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_DELETESTRING, i, 0);
    }

    SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_ADDSTRING, 0, (LPARAM)"Use BTR Registers");
    SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_ADDSTRING, 0, (LPARAM)"20 kbit");
    SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_ADDSTRING, 0, (LPARAM)"50 kbit");
    SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_ADDSTRING, 0, (LPARAM)"100 kbit");
    SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_ADDSTRING, 0, (LPARAM)"125 kbit");
    SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_ADDSTRING, 0, (LPARAM)"250 kbit");
    SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_ADDSTRING, 0, (LPARAM)"500 kbit");
    SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_ADDSTRING, 0, (LPARAM)"800 kbit");
    SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_ADDSTRING, 0, (LPARAM)"1000 kbit");

    SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_SETCURSEL, (int)CanCfg->vSpeed, 0);
}

static void SaveDeviceData(void)
{
    char temp[MAX_PATH];

    // Location
    GetDlgItemText(g_hDlg, IDC_DEVICE_LIST, temp, sizeof(temp));
    if (strchr(temp, ':'))  // network
    {
        strcpy(CanCfg->szLocation, temp);
    }
    else
    {
        if (strncmp(temp, "\\\\.\\", 4))
        {
            strcpy(CanCfg->szLocation, "\\\\.\\");
            strcat_s(CanCfg->szLocation, (sizeof(CanCfg->szLocation)-sizeof("\\\\.\\")), temp);
        }
        // convert COM number to upper case (not WinCE OS)
        int len = strlen(CanCfg->szLocation) - 1;
        do
        {
            if (CanCfg->szLocation[len] == '\\')
            {
                break;
            }
            if (CanCfg->szLocation[len] >= 97 && CanCfg->szLocation[len] <= 122)
            {
                CanCfg->szLocation[len] -= 32;
            }
        }
        while (len--);
    }

    // Baudrate
    CanCfg->vSpeed = (void*)(SendDlgItemMessage(g_hDlg, IDC_BAUDRATE, CB_GETCURSEL, 0, 0));

    // Operation Mode
    if (IsDlgButtonChecked(g_hDlg, IDC_MODE_SELF_RECEPTION) == BST_CHECKED)
    {
        CanCfg->dwMode = VSCAN_MODE_SELF_RECEPTION;
    }
    else
    {
        if (IsDlgButtonChecked(g_hDlg, IDC_MODE_LISTEN_ONLY) == BST_CHECKED)
        {
            CanCfg->dwMode = VSCAN_MODE_LISTEN_ONLY;
        }
        else
        {
            CanCfg->dwMode = VSCAN_MODE_NORMAL;
        }
    }

    // Timestamps
    if (IsDlgButtonChecked(g_hDlg, IDC_CHECK_TIMESTAMPS) == BST_CHECKED)
    {
        CanCfg->bTimestamps = TRUE;
    }
    else
    {
        CanCfg->bTimestamps = FALSE;
    }

    // Debug Mode
    if (IsDlgButtonChecked(g_hDlg, IDC_CHECK_DEBUG) == BST_CHECKED)
    {
        CanCfg->bDebug = TRUE;
    }
    else
    {
        CanCfg->bDebug = FALSE;
    }

    // BTR Register
    if (GetDlgItemText(g_hDlg, IDC_BTR0, temp, sizeof(temp)) <= 2)
    {
        CanCfg->btr.Btr0 = (UCHAR)strtoul(temp, nullptr, 16);
    }
    if (GetDlgItemText(g_hDlg, IDC_BTR1, temp, sizeof(temp)) <= 2)
    {
        CanCfg->btr.Btr1 = (UCHAR)strtoul(temp, nullptr, 16);
    }

    // Acceptance Code and Mask
    if (GetDlgItemText(g_hDlg, IDC_ACC_CODE, temp, sizeof(temp)))
    {
        CanCfg->codeMask.Code = (UINT32)strtoul(temp, nullptr, 16);
    }
    if (GetDlgItemText(g_hDlg, IDC_ACC_MASK, temp, sizeof(temp)))
    {
        CanCfg->codeMask.Mask = (UINT32)strtoul(temp, nullptr, 16);
    }
    if (IsDlgButtonChecked(g_hDlg, IDC_FILTER_DUAL) == BST_CHECKED)
    {
        CanCfg->bDualFilter = TRUE;
    }
    else
    {
        CanCfg->bDualFilter = FALSE;
    }
}

static BOOL CALLBACK CanSetupDlgProc(HWND hDlg, UINT uMessage, WPARAM wParam, LPARAM /* lParam */)
{
    int wmId, wmEvent;

    switch(uMessage)
    {
        case WM_INITDIALOG:
        {
            VSCAN_API_VERSION version;
            char temp[MAX_PATH];

            g_hDlg = hDlg;

            /* A previous dialog instance cannot leave a search behind
               (it abandoned it when it closed), but be defensive anyway */
            AbandonSearchJob();
            FreeDeviceListContents();

            VSCAN_Ioctl(CanCfg->hCan, VSCAN_IOCTL_GET_API_VERSION, &version);
            sprintf(temp, "%d.%d.%d", version.Major, version.Minor, version.SubMinor);
            SetDlgItemText(g_hDlg, IDC_API_VERSION, temp);

            InitBaudrateList();

            if (!strncmp(CanCfg->szLocation, "\\\\.\\", 4))
            {
                SetDlgItemText(g_hDlg, IDC_DEVICE_LIST, CanCfg->szLocation + 4);
            }
            else
            {
                SetDlgItemText(g_hDlg, IDC_DEVICE_LIST, CanCfg->szLocation);
            }

            switch(CanCfg->dwMode)
            {
                case VSCAN_MODE_LISTEN_ONLY:
                    CheckDlgButton(g_hDlg, IDC_MODE_LISTEN_ONLY, BST_CHECKED);
                    break;
                case VSCAN_MODE_SELF_RECEPTION:
                    CheckDlgButton(g_hDlg, IDC_MODE_SELF_RECEPTION, BST_CHECKED);
                    break;
                default:
                    CheckDlgButton(g_hDlg, IDC_MODE_NORMAL, BST_CHECKED);
                    break;
            }
            if (CanCfg->bTimestamps)
            {
                CheckDlgButton(g_hDlg, IDC_CHECK_TIMESTAMPS, BST_CHECKED);
            }
            if (CanCfg->bDebug)
            {
                CheckDlgButton(g_hDlg, IDC_CHECK_DEBUG, BST_CHECKED);
            }

            SendDlgItemMessage(g_hDlg, IDC_BTR0, EM_LIMITTEXT, 2, 0);
            SendDlgItemMessage(g_hDlg, IDC_BTR1, EM_LIMITTEXT, 2, 0);
            sprintf(temp, "%02X", CanCfg->btr.Btr0);
            SetDlgItemText(g_hDlg, IDC_BTR0, temp);
            sprintf(temp, "%02X", CanCfg->btr.Btr1);
            SetDlgItemText(g_hDlg, IDC_BTR1, temp);

            SendDlgItemMessage(g_hDlg, IDC_ACC_CODE, EM_LIMITTEXT, 8, 0);
            SendDlgItemMessage(g_hDlg, IDC_ACC_MASK, EM_LIMITTEXT, 8, 0);
            sprintf(temp, "%08X", CanCfg->codeMask.Code);
            SetDlgItemText(g_hDlg, IDC_ACC_CODE, temp);
            sprintf(temp, "%08X", CanCfg->codeMask.Mask);
            SetDlgItemText(g_hDlg, IDC_ACC_MASK, temp);
            if (CanCfg->bDualFilter)
            {
                SendDlgItemMessage(g_hDlg, IDC_FILTER_DUAL, BM_SETCHECK, BST_CHECKED, 0);
            }
        }
        return TRUE;
        case WM_TIMER:
            if (wParam == IDT_SEARCH_PROGRESS)
            {
                SEARCH_JOB* pJob = g_pActiveJob;

                if (pJob == nullptr)
                {
                    break;
                }
                if ((pJob->hThread != nullptr)
                        && (WaitForSingleObject(pJob->hThread, 0) == WAIT_TIMEOUT))
                {
                    /* the worker is still probing; show where it is */
                    char temp[64];
                    if ((pJob->dwPortIndex > 0) && (pJob->dwPortCount > 0))
                    {
                        sprintf_s(temp, sizeof(temp), "Searching %s\n(%lu of %lu)",
                                  pJob->acPortName,
                                  (unsigned long) pJob->dwPortIndex,
                                  (unsigned long) pJob->dwPortCount);
                    }
                    else
                    {
                        strcpy_s(temp, sizeof(temp), "Searching...");
                    }
                    SetDlgItemText(hDlg, IDC_SEARCH, temp);
                }
                else
                {
                    /* the worker finished and published the result; take
                       it over into the dialog's device list */
                    DWORD i;

                    KillTimer(hDlg, IDT_SEARCH_PROGRESS);
                    FreeDeviceListContents();
                    for (i = 0; (i < pJob->dwFound) && (i < MAX_DEVICES); i++)
                    {
                        g_list[i] = pJob->list[i];
                    }
                    g_dwListCnt = (pJob->dwFound < MAX_DEVICES) ? pJob->dwFound : MAX_DEVICES;
                    FinishSearchJob(pJob);   // the entries now live in g_list
                    g_pActiveJob = nullptr;
                    SetDlgItemText(hDlg, IDC_SEARCH, SEARCH_BUTTON_TEXT);
                    RefreshDeviceList();
                }
            }
            break;
        case WM_CLOSE:
            CloseSetupDialog(hDlg, FALSE);
            return TRUE;
        case WM_COMMAND:
            wmId    = LOWORD(wParam);
            wmEvent = HIWORD(wParam);

            switch (wmId)
            {
                case IDCANCEL:
                case IDOK:
                    CloseSetupDialog(hDlg, FALSE);
                    return TRUE;
                case IDC_SAVE:
                    SaveDeviceData();
                    CloseSetupDialog(hDlg, TRUE);
                    return TRUE;
                case IDC_SEARCH:
                {
                    SEARCH_JOB* pJob;

                    if (g_pActiveJob != nullptr)
                    {
                        /* a search is running; the click stops it */
                        AbandonSearchJob();
                        KillTimer(hDlg, IDT_SEARCH_PROGRESS);
                        SetDlgItemText(hDlg, IDC_SEARCH, SEARCH_BUTTON_TEXT);
                        return TRUE;
                    }

                    pJob = (SEARCH_JOB*) calloc(1, sizeof(SEARCH_JOB));
                    if (pJob == nullptr)
                    {
                        return TRUE;
                    }
                    if (!CollectComPorts(pJob))
                    {
                        free(pJob);
                        return TRUE;
                    }

                    FreeDeviceListContents();

                    pJob->hThread = CreateThread(nullptr, 0, DeviceSearchThread, pJob, 0, nullptr);
                    if (pJob->hThread == nullptr)
                    {
                        free(pJob);
                        return TRUE;
                    }
                    g_pActiveJob = pJob;

                    SetDlgItemText(hDlg, IDC_SEARCH, "Searching...");
                    SetTimer(hDlg, IDT_SEARCH_PROGRESS, 250, nullptr);
                }
                return TRUE;
                case IDC_DEVICE_LIST:
                    switch (wmEvent)
                    {
                        case CBN_SELCHANGE:
                        {
                            int sel = SendDlgItemMessage(g_hDlg, IDC_DEVICE_LIST, CB_GETCURSEL, 0, 0);
                            PrintDeviceInfo(sel);
                        }
                        break;
                        case CBN_SETFOCUS:
                            SetDlgItemText(g_hDlg, IDC_DEVICE_LIST, "");
                            PrintDeviceInfo(-1);
                            break;
                        case CBN_EDITCHANGE:
                            PrintDeviceInfo(-1);
                            break;
                        default:
                            return FALSE;
                    }
                    return TRUE;
            }

    }
    return FALSE;
}


int WINAPI ShowCanVsComSetup(HWND hwnd, struct _VSCanCfg* cfg)
{
    MEMORY_BASIC_INFORMATION mbi;
    static int dummy;
    VirtualQuery(&dummy, &mbi, sizeof(mbi));
    DWORD hMod = (DWORD)mbi.AllocationBase;
    char szModule[MAX_PATH];
    GetModuleFileName((HMODULE)hMod, szModule, sizeof(szModule));
    CanCfg = cfg;
    return DialogBox(GetModuleHandle(szModule), MAKEINTRESOURCE(IDD_CONFIG), hwnd, CanSetupDlgProc);
}

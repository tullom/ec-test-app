/*
 ** Copyright (c) Microsoft Corporation
 *
 *  File: ectest.cpp
 *  Description: An application that allows us to call ACPI Methods to validate EC functionality
 *
 *  Author: Phil Weber
 *  Date: 10/24/2024
 */


#include <DriverSpecs.h>
_Analysis_mode_(_Analysis_code_type_user_code_)

#define INITGUID

#include <windows.h>
#include <strsafe.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <SetupAPI.h>
#include <Devpkey.h>
#include <Acpiioct.h>
#include <devioctl.h>
#include "..\inc\ectest.h"

//#define EC_TEST_NOTIFICATIONS
//#define EC_TEST_SHARED_BUFFER

#define MAX_ACPIPATH_LENGTH 64
#define MAX_DEVPATH_LENGTH  64

// GUID defined in the KMDF INX file for ectest.sys
// {5362ad97-ddfe-429d-9305-31c0ad27880a}
const GUID GUID_DEVCLASS_ECTEST = { 0x5362ad97, 0xddfe, 0x429d, { 0x93, 0x05, 0x31, 0xc0, 0xad, 0x27, 0x88, 0x0a } };

static WCHAR gDevicePath[MAX_DEVPATH_LENGTH];
static char gMethodName[MAX_ACPIPATH_LENGTH];

// Global event handle
static HANDLE gExitEvent = NULL;

/*
 * Function: BOOL GetGUIDPath
 *
 * Description:
 * The GetGUIDPath function retrieves the device path for a specified device class GUID and device name.
 * It searches for devices of the specified class, checks for a matching device instance ID, and retrieves the device path if a match is found.
 *
 * Parameters:
 * GUID GUID_DEVCLASS_SYSTEM: The GUID of the device class to search for.
 * wchar_t *name: The name of the device to match against the device instance ID.
 *
 * Return Value:
 * Returns TRUE if the device path is successfully retrieved, otherwise returns FALSE.
 */
BOOL GetGUIDPath(
    _In_ GUID GUID_DEVCLASS_SYSTEM,
    _In_ wchar_t *name
    )
{
    BOOL bRet = TRUE;
    static BOOL bDeviceFound = FALSE;

    // Don't spend time to look up the device again if we already have the handle
    if(bDeviceFound) {
        return bRet;
    }

    // Get devices of ACPI class there should only be one on the system
    HDEVINFO DeviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_SYSTEM, NULL, NULL, DIGCF_PRESENT);
    SP_DEVINFO_DATA DeviceInfoData;
    DWORD DeviceIndex = 0;

    ZeroMemory(&DeviceInfoData, sizeof(SP_DEVINFO_DATA));
    DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    while (SetupDiEnumDeviceInfo(
                                DeviceInfoSet,
                                DeviceIndex,
                                &DeviceInfoData)) {
        // Read Device instance path and check for ACPI_HAL\PNP0C08 as this is the ACPI driver
        DEVPROPTYPE PropertyType;
        DWORD RequiredSize = 0;
        BYTE PropertyBuffer[128];
        bRet = SetupDiGetDevicePropertyW(
                                DeviceInfoSet,
                                &DeviceInfoData,
                                &DEVPKEY_Device_InstanceId,
                                &PropertyType,
                                PropertyBuffer,
                                sizeof(PropertyBuffer),
                                &RequiredSize,
                                0);

        if(RequiredSize > 0) {
            printf("Found matching Class GUID: %ls\n", (wchar_t *)PropertyBuffer);
            // Check if string contains PNP0C08 then this is our main ACPI device
            if( wcsstr((wchar_t*)PropertyBuffer,name) ) {
                bRet = SetupDiGetDevicePropertyW(
                                        DeviceInfoSet,
                                        &DeviceInfoData,
                                        &DEVPKEY_Device_PDOName,
                                        &PropertyType,
                                        PropertyBuffer,
                                        sizeof(PropertyBuffer),
                                        &RequiredSize,
                                        0);

                if(RequiredSize > 0) {
                    StringCchPrintf(gDevicePath,sizeof(gDevicePath),L"\\\\.\\GLOBALROOT%ls",(wchar_t*)PropertyBuffer);
                    printf("%ls\n", gDevicePath);
                    break;
                }
            }
        }

        DeviceIndex++;

    }

    if (DeviceInfoSet) {
        SetupDiDestroyDeviceInfoList(DeviceInfoSet);
    }

    return TRUE;
}

/*
 * Function: int EvaluateAcpi
 *
 * Description:
 * The EvaluateAcpi function evaluates an ACPI method on a specified device and prints the results.
 * It sends an IOCTL request to the device to execute the ACPI method and processes the returned data.
 *
 * Parameters:
 * HANDLE hDevice: A handle to the device on which the ACPI method is to be evaluated.
 *
 * Return Value:
 * Returns ERROR_SUCCESS if the ACPI method is successfully evaluated, otherwise returns ERROR_INVALID_PARAMETER.
 */
int EvaluateAcpi(
    _In_ HANDLE hDevice
    )
{
    char OutputBuffer[sizeof(ACPI_EVAL_OUTPUT_BUFFER_V1)+256];
    ACPI_EVAL_INPUT_BUFFER_V1_EX InputBuffer;
    ACPI_EVAL_OUTPUT_BUFFER_V1 *AcpiOut = (ACPI_EVAL_OUTPUT_BUFFER_V1 *)OutputBuffer;
    BOOL bRc;
    ULONG bytesReturned;

    printf("\nCalling DeviceIoControl EVAL_ACPI_METHOD: %s\n",gMethodName);
    memset(OutputBuffer, 0, sizeof(OutputBuffer));
    memset(&InputBuffer, 0, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
    strncpy_s(InputBuffer.MethodName,gMethodName,sizeof(InputBuffer.MethodName));

    bRc = DeviceIoControl ( hDevice,
                            (DWORD) IOCTL_ACPI_EVAL_METHOD_EX,
                            &InputBuffer,
                            sizeof(InputBuffer),
                            OutputBuffer,
                            sizeof( OutputBuffer),
                            &bytesReturned,
                            NULL
                            );

    if ( !bRc )
    {
        printf ( "Error in DeviceIoControl : %d\n", GetLastError());
        return ERROR_INVALID_PARAMETER;
    }

    // Print the raw output data returned from ACPI function
    printf("ACPI Method: \n");
    printf("  Signature: 0x%x\n", AcpiOut->Signature);
    printf("  Length: 0x%x\n", AcpiOut->Length);
    printf("  Count: 0x%x\n", AcpiOut->Count);
    // Dump out the contents of each Argument separately
    ACPI_METHOD_ARGUMENT_V1 *Argument = AcpiOut->Argument;

    for(ULONG i=0; i < AcpiOut->Count; i++) {
        printf("    Argument[%i]:\n", i);
        switch(Argument->Type) {
            case ACPI_METHOD_ARGUMENT_INTEGER:
                printf("    Integer Value: 0x%x\n", Argument->Argument);
                break;
            case ACPI_METHOD_ARGUMENT_STRING:
                printf("    String Value: %s\n", Argument->Data);
                break;
            case ACPI_METHOD_ARGUMENT_BUFFER:
            case ACPI_METHOD_ARGUMENT_PACKAGE:
            default:
                printf("    Buffer Data:\n");
                for(int j=0; j < Argument->DataLength; j++) {
                    printf(" 0x%x,", Argument->Data[j]);
                }
                break;

        }
        // Argument is variable length so update to point to next entry
        Argument = (ACPI_METHOD_ARGUMENT_V1 *)(Argument->Data + Argument->DataLength);
    }

    printf("\n\nACPI Raw Output:\n");
    for(ULONG i=0; i < AcpiOut->Length; i++) {
        printf(" 0x%x",((BYTE *)AcpiOut)[i]);
    }
    printf("\n\n");

    return ERROR_SUCCESS;
}

/*
 * Function: int parse_cmdline
 *
 * Description:
 * The parse_cmdline function parses the command line arguments and sets the ACPI method name if provided.
 * It checks the number of arguments and prints usage instructions if the required arguments are not provided.
 *
 * Parameters:
 * int argc: The number of command line arguments.
 * char **argv: The array of command line arguments.
 *
 * Return Value:
 * Returns ERROR_SUCCESS if the ACPI method name is successfully set, otherwise returns ERROR_INVALID_PARAMETER.
 */
int ParseCmdline(
    _In_ int argc,
    _In_ char ** argv
    )
{
    if (argc > 2)  {
        strncpy_s(gMethodName,argv[2],MAX_ACPIPATH_LENGTH);
    } else {
        printf("Usage:\n");
        printf("    ec-test-app.exe   --- Print this help\n");
        printf("    ec-test-app.exe -acpi \\_SB.ECT0._STA  --- Evaluate given ACPI method\n");
        return ERROR_INVALID_PARAMETER;
    }

    return ERROR_SUCCESS;
}

/*
 * Function: int GetKMDFDriverHandle
 *
 * Description:
 * The GetKMDFDriverHandle function retrieves a handle to a Kernel-Mode Driver Framework (KMDF) driver.
 * It searches for the device path using a specified device class GUID and device name, and then opens the device.
 *
 * Parameters:
 * DWORD flags: Flags to open file handle with
 * HANDLE *hDevice: A pointer to a handle that will receive the device handle.
 *
 * Return Value:
 * Returns ERROR_SUCCESS if the device handle is successfully retrieved, otherwise returns ERROR_INVALID_HANDLE.
*/
int GetKMDFDriverHandle(
    _In_ DWORD flags,
    _Out_ HANDLE *hDevice
    )
{
    int status = ERROR_SUCCESS;

    if (!GetGUIDPath(GUID_DEVCLASS_ECTEST,L"ETST0001") )
    {
        status = ERROR_INVALID_HANDLE;
        goto CleanUp;
    }

    printf("DevicePath: %ws\n", gDevicePath);
    *hDevice = CreateFile(gDevicePath,
                         GENERIC_READ|GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         flags,
                         NULL );

    if (*hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open device. Error %d\n",GetLastError());
        status = ERROR_INVALID_HANDLE;
        goto CleanUp;
    }

    printf("Opened device successfully\n");
CleanUp:
    return status;
}

/*
 * Function: int ReadRxBuffer
 *
 * Description:
 * The ReadRxBuffer function reads the receive buffer from a Kernel-Mode Driver Framework (KMDF) driver.
 * It sends a request to the driver and receives the buffer data.
 *
 * Parameters:
 * HANDLE hDevice: A handle to the device from which the receive buffer is to be read.
 *
 * Return Value:
 * Returns ERROR_SUCCESS if the receive buffer is successfully read, otherwise returns ERROR_INVALID_PARAMETER.
 */
#ifdef EC_TEST_SHARED_BUFFER
int ReadRxBuffer(
    _In_ HANDLE hDevice
    )
{
    BOOL bRc;
    ULONG bytesReturned;
    ULONG inbuf;
    RxBufferRsp_t rxrsp;

    printf("\nCalling DeviceIoControl IOCTL_GET_RX_BUFFER\n");

    bRc = DeviceIoControl ( hDevice,
                            (DWORD) IOCTL_READ_RX_BUFFER,
                            &inbuf,
                            sizeof(inbuf),
                            &rxrsp,
                            sizeof(rxrsp),
                            &bytesReturned,
                            NULL
                            );

    if ( !bRc )
    {
        printf ( "***       Error in DeviceIoControl : %d \n", GetLastError());
        return ERROR_INVALID_PARAMETER;
    }

    // Print out notification details
    printf("***                 data: 0x%llx\n", rxrsp.data);

    return ERROR_SUCCESS;
}
#endif // EC_TEST_SHARED_BUFFER

#ifdef EC_TEST_NOTIFICATIONS
/*
 * Function: DDWORD NotificationThread
 *
 * Description:
 * The NotificationThread function retrieves a notification from a Kernel-Mode Driver Framework (KMDF) driver.
 * It sends a request to the driver and receives the notification details.
 *
 * Parameters:
 * LPVOID lpParam: A handle to ready event we notfiy after sending IOCTL to wait for events
 *
 * Return Value:
 * Returns ERROR_SUCCESS if the notification is successfully retrieved, otherwise returns ERROR_INVALID_PARAMETER.
 */
DWORD WINAPI NotificationThread(LPVOID lpParam) 
{
    HANDLE hDevice = NULL;
    int status;
    OVERLAPPED overlapped = {0};
    HANDLE hReadyEvent = (HANDLE)lpParam;

    // Open the device. Seperate open is required for the IO to go through.
    status = GetKMDFDriverHandle(
                         FILE_FLAG_OVERLAPPED,
                         &hDevice );

    if (hDevice != INVALID_HANDLE_VALUE) {

        BOOL bRc;
        ULONG bytesReturned;
        NotificationRsp_t notify_response;
        NotificationReq_t notify_request;

        overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (overlapped.hEvent == NULL) {
            printf ( "Error in CreateEvent : %d\n", GetLastError());
            goto CleanUp;
        }

        // Loop forever receiving events until we get an exit event
        for(;;) {
            printf("\n***       Calling DeviceIoControl IOCTL_GET_NOTIFICATION\n");
            notify_request.type = 0x1;
            bRc = DeviceIoControl ( hDevice,
                                    (DWORD) IOCTL_GET_NOTIFICATION,
                                    &notify_request,
                                    sizeof(notify_request),
                                    &notify_response,
                                    sizeof( notify_response),
                                    &bytesReturned,
                                    &overlapped
                                    );

            if (!bRc && GetLastError() == ERROR_IO_PENDING) {

                // If we haven't signalled hReadyEvent yet send it
                if(hReadyEvent) {
                    SetEvent(hReadyEvent);
                    hReadyEvent = NULL;
                }

                HANDLE events[2] = { overlapped.hEvent, gExitEvent };
                DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                switch (waitResult) {
                    case WAIT_OBJECT_0:
                        // DeviceIoControl completed
                        if (!GetOverlappedResult(hDevice, &overlapped, &bytesReturned, TRUE)) {
                            printf("Error in GetOverlappedResult: %d\n", GetLastError());
                            status = ERROR_INVALID_PARAMETER;
                            goto CleanUp;
                        }
                        break;
                    case WAIT_OBJECT_0 + 1:
                        // gExitEvent event is set
                        printf ( "Cancelling Io \n");
                        CancelIo(hDevice);
                        status = ERROR_OPERATION_ABORTED;
                        goto CleanUp;
                    default:
                        printf ( "Error Waiting for Completion : %d\n", waitResult);
                        status = ERROR_INVALID_PARAMETER;
                        goto CleanUp;
                }
            } else if ( !bRc ) {
                printf ( "Error in DeviceIoControl : %d\n", GetLastError());
                status = ERROR_INVALID_PARAMETER;
                goto CleanUp;
            }

            // Print out notification details
            printf("\n***       Received Notification \n");
            printf("***                count: 0x%llx\n", notify_response.count);
            printf("***            timestamp: 0x%llx\n", notify_response.timestamp);
            printf("***            lastevent: 0x%x\n", notify_response.lastevent);

            #ifdef EC_TEST_SHARED_BUFFER
            ReadRxBuffer(hDevice);
            #endif

            // Reset the event and wait for next notification
            ResetEvent(overlapped.hEvent);
        }
    }
CleanUp:
    // Cancel any pending IO
    if(hDevice) CancelIo(hDevice);
    if(hDevice) CloseHandle(hDevice);
    if(overlapped.hEvent) CloseHandle(overlapped.hEvent);

    printf("Exiting Notification Thread \n");
    return 0;
}


/*
 * Function: HANDLE StartNotificationListener
 *
 * Description:
 * The StartNotificationListener function creates a thread to listen for notifications and returns a handle to the thread.
 * It creates an event to signal when the thread is ready and waits for the thread to be ready or terminated.
 *
 * Parameters:
 * None
 *
 * Return Value:
 * Returns a handle to the notification listener thread if successful, otherwise returns NULL.
 */
HANDLE StartNotificationListener(void)
{
    HANDLE hThread;
    DWORD dwThreadId;

    HANDLE hReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (hReadyEvent == NULL) {
        return NULL;
    }


    // Create the thread
    hThread = CreateThread(
        NULL,                   // Default security attributes
        0,                      // Default stack size
        NotificationThread,     // Thread routine
        hReadyEvent,            // Parameter to thread routine
        0,                      // Default creation flags
        &dwThreadId);           // Receive thread identifier

    if (hThread != NULL) {
        // Wait for the thread ready or termination
        HANDLE events[2] = { hThread, hReadyEvent };
        DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        switch (waitResult) {
            case WAIT_OBJECT_0:
                printf("Notification Thread Terminated.\n");
                hThread = NULL;
                break;
            case WAIT_OBJECT_0 + 1:
                printf("hReadyEvent signalled.\n");
        }
    }

    CloseHandle(hReadyEvent);
    return hThread;
}
#endif // EC_TEST_NOTIFICATIONS


/*
 * Function: int main
 *
 * Description:
 * The main function serves as the entry point for the program. It parses the command line arguments,
 * retrieves a handle to the KMDF driver, and evaluates an ACPI method on the device.
 *
 * Parameters:
 * int argc: The number of command line arguments.
 * char* argv[]: The array of command line arguments.
 *
 * Return Value:
 * Returns the status of the operations. Returns ERROR_SUCCESS if all operations are successful,
 * otherwise returns an error code.
 */
int __cdecl
main(
    _In_ int argc,
    _In_reads_(argc) char* argv[]
    )
{

    HANDLE hDevice = NULL;
    HANDLE hThread = NULL;
    int status = ERROR_SUCCESS;

    UINT8 *ptr = (UINT8*)&GUID_DEVCLASS_ECTEST;
    printf("GUID:  {5362ad97-ddfe-429d-9305-31c0ad27880a}\n");
    for(int i=0 ; i < 16; i++) {
        printf("%02x",ptr[i]);
    }
    printf("\n\n");

    // Keep only one instance of the application running
    // This makes the App & Driver simple by not allowing multiple instances
    //
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"Global\\ECTestAppMutex");
    if (hMutex == NULL) {
        status = GetLastError();
        printf("CreateMutex failed, error: %d\n", status);
        goto CleanUp;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        printf("Another instance of the application is already running.\n");
        goto CleanUp;
    }

    status = ParseCmdline(argc,argv);
    if(status != ERROR_SUCCESS) {
        goto CleanUp;
    }

    status = GetKMDFDriverHandle(0,&hDevice);
    if(status != ERROR_SUCCESS) {
        goto CleanUp;
    }

#ifdef EC_TEST_NOTIFICATIONS
    // Create the exit event
    gExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (gExitEvent == NULL) {
        status = GetLastError();
        printf("CreateEvent failed, error: %d\n", status);
        goto CleanUp;
    }

    // This creates a new thread and blocks until listener is running
    hThread = StartNotificationListener();
    if(hThread == NULL) {
        goto CleanUp;
    }
#endif // EC_TEST_NOTIFICATIONS

    // Evaluate ACPI function
    status = EvaluateAcpi(hDevice);

    if(status == ERROR_SUCCESS ) {
        // Loop until we hit "q to quit"
        printf("Waiting for notification press 'q' to quit.\n");
        int key;
        for(;;) {
            key = getchar();
            if( key == 'q') {
                break;
            }
        }
    }

    printf("You pressed 'q'. Exiting...\n");
CleanUp:

    // Signal the exit event to stop the thread
    if(gExitEvent) SetEvent(gExitEvent);
    if(hThread) WaitForSingleObject(hThread, INFINITE);

    if(hThread) CloseHandle(hThread);
    if(gExitEvent) CloseHandle(gExitEvent);

    if(hDevice) CloseHandle(hDevice);
    if(hMutex) CloseHandle(hMutex);
    return status;

}

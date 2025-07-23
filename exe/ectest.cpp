/*
MIT License

Copyright (c) 2025 Open Device Partnership

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
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

extern "C" {
    #include "..\inc\eclib.h"
}

//#define EC_TEST_NOTIFICATIONS
//#define EC_TEST_SHARED_BUFFER

#define ACPI_OUTPUT_BUFFER_SIZE 1024

// Global event handle
static HANDLE gExitEvent = NULL;

/*
 * Function: void DumpAcpi
 *
 * Description:
 * The DumpAcpi function evaluates an ACPI method on a specified device and prints the results.
 * It sends an IOCTL request to the device to execute the ACPI method and processes the returned data.
 *
 * Parameters:
 * methodName: Method of ACPI to evaluate and dump
 *
 * Return Value:
 * None.
 */
void DumpAcpi(char *methodName )
{

    BYTE buffer[ACPI_OUTPUT_BUFFER_SIZE];
    ACPI_EVAL_OUTPUT_BUFFER_V1 *AcpiOut = (ACPI_EVAL_OUTPUT_BUFFER_V1 *)buffer;
    size_t buffer_size = sizeof(buffer);

    int status = EvaluateAcpi(methodName, strlen(methodName), buffer, &buffer_size );

    if(status != ERROR_SUCCESS) {
        printf("EvaluateAcpi failed, status: 0x%x", status);
        return;
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
}

/*
 * Function: int ParseCmdline
 *
 * Description:
 * The ParseCmdline function parses the command line arguments and sets the ACPI method name if provided.
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
    if (argc > 2) {
        DumpAcpi(argv[2]);
    } else {
        printf("Usage:\n");
        printf("    ectest.exe                        --- Print this help\n");
        printf("    ectest.exe -acpi \\_SB.ECT0.NEVT  --- Evaluate given ACPI method\n");
        return ERROR_INVALID_PARAMETER;
    }

    return ERROR_SUCCESS;
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

    HANDLE hThread = NULL;
    int status = ERROR_SUCCESS;

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

    status = ParseCmdline(argc,argv);
    if(status != ERROR_SUCCESS) {
        goto CleanUp;
    }

    // Loop until we hit "q to quit"
    printf("Waiting for notification press 'q' to quit.\n");
    int key;
    for(;;) {
        key = getchar();
        if( key == 'q') {
            break;
        }
    }

    printf("You pressed 'q'. Exiting...\n");
CleanUp:

    // Signal the exit event to stop the thread
    if(gExitEvent) SetEvent(gExitEvent);
    if(hThread) WaitForSingleObject(hThread, INFINITE);

    if(hThread) CloseHandle(hThread);
    if(gExitEvent) CloseHandle(gExitEvent);
    if(hMutex) CloseHandle(hMutex);

    return status;
}

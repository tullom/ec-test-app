/*
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

#define MAX_ACPIPATH_LENGTH 64
#define MAX_DEVPATH_LENGTH  64

// GUID defined in the KMDF INX file for ectest.sys
const GUID GUID_DEVCLASS_ECTEST = { 0xedc778aa, 0x35ee, 0x4c03, { 0xb1, 0xe4, 0xaf, 0x78, 0x82, 0x90, 0x35, 0x71 } };

static WCHAR gDevicePath[MAX_DEVPATH_LENGTH];
static char gMethodName[MAX_ACPIPATH_LENGTH];

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

   return true;
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
        printf ( "Error in DeviceIoControl : %d", GetLastError());
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

    printf("ACPI Raw Output:\n");
    for(ULONG i=0; i < AcpiOut->Length; i++) {
        printf(" 0x%x",((BYTE *)AcpiOut)[i]);
    }

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
 * HANDLE *hDevice: A pointer to a handle that will receive the device handle.
 *
 * Return Value:
 * Returns ERROR_SUCCESS if the device handle is successfully retrieved, otherwise returns ERROR_INVALID_HANDLE.
*/
int GetKMDFDriverHandle(
    _Out_ HANDLE *hDevice
    )
{
    int status = ERROR_SUCCESS;

    if ( !GetGUIDPath(GUID_DEVCLASS_ECTEST,L"ACPI1234") )
    {
        status = ERROR_INVALID_HANDLE;
        goto exit;
    }

    printf("DevicePath: %ws\n", gDevicePath);

    *hDevice = CreateFile(gDevicePath,
                         GENERIC_READ|GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL );

    if (*hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open device. Error %d\n",GetLastError());
        status = ERROR_INVALID_HANDLE;
        goto exit;
    }

    printf("Opened device successfully\n");
exit:
    return status;
}

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
    HANDLE hDevice;
    int status = ParseCmdline(argc,argv);
    if(status == ERROR_SUCCESS) {
        status = GetKMDFDriverHandle(&hDevice);
        status = EvaluateAcpi(hDevice);
    }
    return status;
}

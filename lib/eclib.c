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
#include "..\inc\eclib.h"

#define MAX_DEVPATH_LENGTH  64

// GUID defined in the KMDF INX file for ectest.sys
// {5362ad97-ddfe-429d-9305-31c0ad27880a}
const GUID GUID_DEVCLASS_ECTEST = { 0x5362ad97, 0xddfe, 0x429d, { 0x93, 0x05, 0x31, 0xc0, 0xad, 0x27, 0x88, 0x0a } };

/*
 * Function: GetGUIDPath
 * Description:
 *   Retrieves the device path for a specified device class GUID and device name.
 * Parameters:
 *   GUID GUID_DEVCLASS_SYSTEM - The GUID of the device class to search for.
 *   const wchar_t* name       - The name of the device to match.
 *   wchar_t* path             - Output buffer for the device path.
 *   size_t path_len           - Length of the output buffer.
 * Return Value:
 *   Returns path if successful, NULL otherwise.
 */

wchar_t *GetGUIDPath(
    _In_ GUID GUID_DEVCLASS_SYSTEM,
    _In_ const wchar_t* name,
    _Out_ wchar_t* path,
    _In_ size_t path_len
)
{
    // Get devices of ACPI class there should only be one on the system
    BYTE PropertyBuffer[128];

    HDEVINFO DeviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_SYSTEM, NULL, NULL, DIGCF_PRESENT);
    SP_DEVINFO_DATA DeviceInfoData = { .cbSize = sizeof(SP_DEVINFO_DATA) };
    DWORD DeviceIndex = 0;
    BOOL bRet = TRUE;
    BOOL bPathFound = FALSE;

    while (SetupDiEnumDeviceInfo(DeviceInfoSet, DeviceIndex, &DeviceInfoData)) 
    {
        // Read Device instance path and check for ACPI_HAL\PNP0C08 as this is the ACPI driver
        DEVPROPTYPE PropertyType;
        DWORD RequiredSize = 0;
        bRet = SetupDiGetDevicePropertyW(
            DeviceInfoSet,
            &DeviceInfoData,
            &DEVPKEY_Device_InstanceId,
            &PropertyType,
            PropertyBuffer,
            sizeof(PropertyBuffer),
            &RequiredSize,
            0);

        if (RequiredSize > 0 && wcsstr((wchar_t*)PropertyBuffer, name) ) {
            bRet = SetupDiGetDevicePropertyW(
                DeviceInfoSet,
                &DeviceInfoData,
                &DEVPKEY_Device_PDOName,
                &PropertyType,
                PropertyBuffer,
                sizeof(PropertyBuffer),
                &RequiredSize,
                0);

            StringCchPrintf(path, path_len, L"\\\\.\\GLOBALROOT%ls", (wchar_t*)PropertyBuffer);
            bPathFound = TRUE;
            break;
        }
        DeviceIndex++;
    }

    if (DeviceInfoSet) {
        SetupDiDestroyDeviceInfoList(DeviceInfoSet);
    }

    // If device path was not found return NULL
    return bPathFound ? path : NULL;

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
ECLIB_API
int GetKMDFDriverHandle(
    _In_ DWORD flags,
    _Out_ HANDLE *hDevice
    )
{
    WCHAR pathbuf[MAX_DEVPATH_LENGTH];
    int status = ERROR_SUCCESS;
    wchar_t *devicePath = GetGUIDPath(GUID_DEVCLASS_ECTEST,L"ETST0001",pathbuf,sizeof(pathbuf));

    if ( devicePath == NULL )
    {
        return ERROR_INVALID_HANDLE;
    }

    *hDevice = CreateFile(devicePath,
                         GENERIC_READ|GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         flags,
                         NULL );

    if (*hDevice == INVALID_HANDLE_VALUE) {
        status = ERROR_INVALID_HANDLE;
    }

    return status;
}

/*
 * Function: EvaluateAcpi
 * Description:
 *   Evaluates an ACPI method on a specified device and returns the result.
 * Parameters:
 *   void* acpi_input     - ACPI_EVAL_INPUT_xxxx structure pointer passed through
 *   size_t input_len     - Length of input structure
 *   BYTE* buffer         - Output buffer for the result.
 *   size_t* buf_len      - Input: size of buffer; Output: bytes returned.
 * Return Value:
 *   ERROR_SUCCESS on success, ERROR_INVALID_PARAMETER on failure.
 */
ECLIB_API
int EvaluateAcpi(
    _In_ void* acpi_input,
    _In_ size_t input_len,
    _Out_ BYTE* buffer,
    _In_ size_t* buf_len
)
{
    WCHAR pathbuf[MAX_DEVPATH_LENGTH];
    ULONG bytesReturned;

    // Look up handle to ACPI entry
    wchar_t* dpath = GetGUIDPath(GUID_DEVCLASS_ECTEST, L"ETST0001", pathbuf, sizeof(pathbuf));
    if (dpath == NULL) {
        return ERROR_INVALID_PARAMETER;
    }

    HANDLE hDevice = CreateFile(dpath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hDevice != INVALID_HANDLE_VALUE) {
        if( DeviceIoControl(hDevice,
            (DWORD)IOCTL_ACPI_EVAL_METHOD_EX,
            acpi_input,
            (DWORD)input_len,
            buffer,
            (DWORD)*buf_len,
            &bytesReturned,
            NULL) == TRUE ) 
        {
            *buf_len = bytesReturned;
            return ERROR_SUCCESS;
        }
    }

    return ERROR_INVALID_PARAMETER;
}


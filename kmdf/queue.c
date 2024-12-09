/*++

Copyright (c) 1990-2000  Microsoft Corporation

Module Name:

    queue.c

Abstract:

    This is a C version of a very simple sample driver that illustrates
    how to use the driver framework and demonstrates best practices.

--*/

#include "driver.h"
#include <stdio.h>
#include <acpiioct.h>
#include "wdm.h"
#include "wdmguid.h"
#include "..\inc\ectest.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, ECTestQueueInitialize)
#endif

// Globals
NotificationRsp_t m_NotifyStats = {0};

/**
 * Function: NTSTATUS NotificationCallback
 *
 * Description: 
 * Callback function for handling ACPI notifications.
 *
 * This function is called when an ACPI notification is received. It updates
 * the notification statistics with the current timestamp and the notification
 * value.
 *
 * Parameters:
 * Context - A pointer to the context information for the callback.
 * NotifyValue - The value associated with the ACPI notification.
 *
 * Return Value:
 * VOID
 *
 */
VOID NotificationCallback(
    PVOID Context,
    ULONG NotifyValue
    )
{
    UNREFERENCED_PARAMETER(Context);

    LARGE_INTEGER timestamp;
    KeQuerySystemTimePrecise(&timestamp);

    m_NotifyStats.count++;
    m_NotifyStats.timestamp = timestamp.QuadPart;
    m_NotifyStats.lastevent = NotifyValue;
}

/*
 * Function: NTSTATUS SetupNotification
 *
 * Description: 
 * Sets up ACPI notifications for the specified device.
 *
 * Parameters:
 * device - The WDFDEVICE object representing the device.
 *
 * Return Value:
 * NTSTATUS status code indicating the success or failure of the operation.
 *
 */
NTSTATUS SetupNotification(WDFDEVICE device)
{
    ACPI_INTERFACE_STANDARD2 acpiInterface;
    NTSTATUS status = STATUS_SUCCESS;

    status = WdfFdoQueryForInterface(device,
                                     &GUID_ACPI_INTERFACE_STANDARD2,
                                     (PINTERFACE) &acpiInterface,
                                     sizeof(ACPI_INTERFACE_STANDARD2),
                                     1,
                                     NULL);
    
    if (NT_SUCCESS(status)) {
        status = acpiInterface.RegisterForDeviceNotifications(acpiInterface.Context,
                        NotificationCallback, NULL);
    }
    return status;
}


/*
 * Function: NTSTATUS ECTestQueueInitialize
 *
 * Description:
 * The ECTestQueueInitialize function configures and creates a default I/O queue for a specified device.
 * It sets up the queue to handle device control requests and ensures that requests not forwarded to other queues are dispatched here.
 *
 * Parameters:
 * WDFDEVICE Device: A handle to the framework device object.
 *
 * Return Value:
 * Returns an NTSTATUS value indicating the success or failure of the queue creation.
 * If the queue is successfully created, it returns STATUS_SUCCESS. Otherwise, it returns an appropriate error code.
 */
NTSTATUS
ECTestQueueInitialize(
    WDFDEVICE Device
    )
{
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG    queueConfig;

    PAGED_CODE();

    //
    // Configure a default queue so that requests that are not
    // configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
    // other queues get dispatched here.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
         &queueConfig,
        WdfIoQueueDispatchSequential
        );

    queueConfig.EvtIoDeviceControl = ECTestEvtIoDeviceControl;

    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 WDF_NO_HANDLE
                 );

    if( !NT_SUCCESS(status) ) {
        KdPrint(("WdfIoQueueCreate failed 0x%x\n",status));
        return status;
    }


    status = SetupNotification(Device);
    return status;
}

/*
 * Function: VOID WorkItemCallback
 *
 * Description:
 * The WorkItemCallback function is a callback function that processes a work item in a KMDF driver.
 * It retrieves the output buffer, creates a preallocated memory object, and sends an internal IOCTL request to the device.
 * The function then completes the request with the appropriate status and information.
 *
 * Parameters:
 * WDFWORKITEM WorkItem: A handle to the work item being processed.
 *
 * Return Value:
 * This function does not return a value.
 */
VOID
WorkItemCallback(
    _In_ WDFWORKITEM WorkItem
    )
{
    PWORKITEM_CONTEXT context = WorkItemGetContext(WorkItem);

    WDF_MEMORY_DESCRIPTOR inputMemDesc;
    WDF_MEMORY_DESCRIPTOR outputMemDesc;
    WDFMEMORY outputMemory = WDF_NO_HANDLE;
    WDF_OBJECT_ATTRIBUTES attributes;
    ULONG BytesReturned = 0;
    NTSTATUS status = STATUS_SUCCESS;
    PCHAR outBuf = NULL;
    size_t outSize = 0;

    // Determine the size of output buffer and only give this much space to ACPI request
    status = WdfRequestRetrieveOutputBuffer(context->Request, 0, &outBuf, &outSize);
    if(!NT_SUCCESS(status)) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = context->Device;
    status = WdfMemoryCreatePreallocated(&attributes,
                                        outBuf,
                                        outSize,
                                        &outputMemory);

    if(!NT_SUCCESS(status)) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, context->Buffer, sizeof(ACPI_EVAL_INPUT_BUFFER_V1_EX));
    WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);
    
    status = WdfIoTargetSendInternalIoctlSynchronously(
                 WdfDeviceGetIoTarget(context->Device),
                 NULL,
                 IOCTL_ACPI_EVAL_METHOD_EX,
                 &inputMemDesc,
                 &outputMemDesc,
                 NULL,
                 (PULONG_PTR)&BytesReturned);

    if(!NT_SUCCESS(status)) {
        status = STATUS_INVALID_PARAMETER;
    }

Cleanup:
    WdfRequestSetInformation(context->Request,BytesReturned);
    WdfRequestComplete( context->Request, status);
}

/*
 * Function: NTSTATUS CreateAndEnqueueWorkItem
 *
 * Description:
 * The CreateAndEnqueueWorkItem function creates a work item and enqueues it for execution.
 * It initializes the work item configuration and context, and sets the callback function for the work item.
 *
 * Parameters:
 * WDFDEVICE Device: A handle to the framework device object.
 * WDFREQUEST Request: A handle to the framework request object.
 * ACPI_EVAL_INPUT_BUFFER_V1_EX *Buffer: A pointer to the input buffer for the ACPI evaluation.
 *
 * Return Value:
 * Returns an NTSTATUS value indicating the success or failure of the work item creation and enqueueing.
 * If the work item is successfully created and enqueued, it returns STATUS_SUCCESS. Otherwise, it returns an appropriate error code.
 */
NTSTATUS
CreateAndEnqueueWorkItem(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ ACPI_EVAL_INPUT_BUFFER_V1_EX *Buffer
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG workitemConfig;
    WDFWORKITEM workItem;
    PWORKITEM_CONTEXT context;

    WDF_WORKITEM_CONFIG_INIT(&workitemConfig, WorkItemCallback);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, WORKITEM_CONTEXT);
    attributes.ParentObject = Device;

    status = WdfWorkItemCreate(&workitemConfig, &attributes, &workItem);
    if (!NT_SUCCESS(status)) {
        KdPrint(("WdfWorkItemCreate failed: %!STATUS!\n", status));
        return status;
    }

    context = WorkItemGetContext(workItem);
    context->Device = Device;
    context->Request = Request;
    context->Buffer = Buffer;

    WdfWorkItemEnqueue(workItem);

    return status;
}

/*
 * Function: VOID ECTestEvtIoDeviceControl
 *
 * Description:
 * The ECTestEvtIoDeviceControl function handles device control requests for a KMDF driver.
 * It processes the specified I/O control code and enqueues a work item for ACPI method evaluation if applicable.
 *
 * Parameters:
 * WDFQUEUE Queue: A handle to the framework queue object.
 * WDFREQUEST Request: A handle to the framework request object.
 * size_t OutputBufferLength: The length of the output buffer.
 * size_t InputBufferLength: The length of the input buffer.
 * ULONG IoControlCode: The I/O control code specifying the operation to perform.
 *
 * Return Value:
 * This function does not return a value.
 */
VOID
ECTestEvtIoDeviceControl(
    IN WDFQUEUE         Queue,
    IN WDFREQUEST       Request,
    IN size_t           OutputBufferLength,
    IN size_t           InputBufferLength,
    IN ULONG            IoControlCode
    )
{
    NTSTATUS            status = STATUS_SUCCESS;// Assume success

    if(!OutputBufferLength || !InputBufferLength)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    //
    // Determine which I/O control code was specified.
    //

    switch (IoControlCode)
    {
    case IOCTL_ACPI_EVAL_METHOD_EX:
        // For bufffered ioctls WdfRequestRetrieveInputBuffer &
        // WdfRequestRetrieveOutputBuffer return the same buffer
        // pointer (Irp->AssociatedIrp.SystemBuffer), so read the
        // content of the buffer before writing to it.
        size_t bufSize = 0;
        ACPI_EVAL_INPUT_BUFFER_V1_EX *InputBuffer = NULL;

        status = WdfRequestRetrieveInputBuffer(Request, 0, &InputBuffer, &bufSize);
        if(!NT_SUCCESS(status)) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
    
        // Check to make sure this buffer was populated properly
        if(InputBuffer->Signature != ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX)
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        // Don't complete the request here it will be completed in the callback
        WDFDEVICE Device = WdfIoQueueGetDevice(Queue);
        status = CreateAndEnqueueWorkItem(Device, Request,InputBuffer);

        // If we enqueue it successfully it will be completed later, otherwise complete with status
        if(!NT_SUCCESS(status)) {
            break;
        }

        // Request will be completed later in work item callback
        return;
    case IOCTL_GET_NOTIFICATION:
        size_t reqSize = 0;
        size_t rspSize = 0;
        NotificationReq_t *req = NULL;
        NotificationRsp_t *rsp = NULL;

        status = WdfRequestRetrieveInputBuffer(Request, 0, &req, &reqSize);
        if(!NT_SUCCESS(status)) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
    
        // Determine the size of output buffer and only give this much space to ACPI request
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &rsp, &rspSize);
        if(!NT_SUCCESS(status)) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        rsp->count = m_NotifyStats.count;
        rsp->timestamp = m_NotifyStats.timestamp;
        rsp->lastevent = m_NotifyStats.lastevent;

        break;

    case IOCTL_READ_RX_BUFFER:
        size_t rxSize = 0;
        RxBufferRsp_t *rxrsp = NULL;

        // Determine the size of output buffer and only give this much space to ACPI request
        status = WdfRequestRetrieveOutputBuffer(Request, 0, &rxrsp, &rxSize);
        if(!NT_SUCCESS(status)) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        PHYSICAL_ADDRESS physicalAddress;
        PVOID virtualAddress;
        ULONG64 value;

        // Set the physical address
        physicalAddress.QuadPart = SBSAQEMU_SHARED_MEM_BASE;

        // Map the physical address to a virtual address
        virtualAddress = MmMapIoSpaceEx(physicalAddress, sizeof(ULONG64), PAGE_READONLY);

        if (virtualAddress == NULL) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        // Read the value from the virtual address
        value = *(volatile ULONG64*)virtualAddress;

        // Unmap the virtual address
        MmUnmapIoSpace(virtualAddress, sizeof(ULONG64));
        
        rxrsp->data = value;

        break;

    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }

    WdfRequestComplete( Request, status);

}

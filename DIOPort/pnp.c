
#include <ntddk.h>
#include <ntstrsafe.h>
#include "../Include/dioctl.h"
#include "dioport.h"

static
NTSTATUS
DiopPnpIoCompletionRoutine(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp, 
	IN PVOID Context)
{
	PKEVENT Event = (PKEVENT)Context;
	KeSetEvent(Event, IO_NO_INCREMENT, FALSE);

	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
DioPnpLowerLevelPassThru(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
{
	PIO_STACK_LOCATION IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
	ULONG MinorFunction = IoStackLocation->MinorFunction;
	PSZ MinorFunctionName = DioPnpRtlLookupMinorFunctionName(MinorFunction);

	DFTRACE("Skipping the PnP IRP, Minor %d (%s)\n", MinorFunction, MinorFunctionName);

	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(DeviceObject, Irp);
}

NTSTATUS
DioPnpCallNextDriverSynchronous(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
{
	KEVENT Event;
	DIO_DEVICE_EXTENSION *DeviceExtension;
	NTSTATUS Status;

	DeviceExtension = (DIO_DEVICE_EXTENSION *)DeviceObject->DeviceExtension;

	// Initialize notification event as non-signaled state.
	KeInitializeEvent(&Event, NotificationEvent, FALSE);

	// Call the lower level driver.
	IoCopyCurrentIrpStackLocationToNext(Irp);
	IoSetCompletionRoutine(Irp, DiopPnpIoCompletionRoutine, &Event, TRUE, TRUE, TRUE);
	Status = IoCallDriver(DeviceExtension->LowerLevelDeviceObject, Irp);

	if (Status == STATUS_PENDING)
	{
		// IRP is in pending. Wait for the IRP to be completed.
		KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

		// Get last status of IRP.
		Status = Irp->IoStatus.Status;
	}

	// NOTE : Caller must call IoCompleteRequest() to complete the current IRP.
	return Status;
}

NTSTATUS
DioPnpStartDevice(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
{
	DIO_DEVICE_EXTENSION *DeviceExtension;
	PIO_STACK_LOCATION IoStackLocation;
	CM_RESOURCE_LIST *ResourceList;
	CM_FULL_RESOURCE_DESCRIPTOR *FullDescriptor;
	CM_PARTIAL_RESOURCE_DESCRIPTOR *PartialDescriptor;
	ULONG i, j;

	DeviceExtension = (DIO_DEVICE_EXTENSION *)DeviceObject->DeviceExtension;
	IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
	ResourceList = IoStackLocation->Parameters.StartDevice.AllocatedResourcesTranslated;

	DFTRACE("Parsing the translated resource list...\n");

	if (ResourceList)
	{
		FullDescriptor = ResourceList->List;

		for (i = 0; i < ResourceList->Count; i++)
		{
			for (j = 0; j < FullDescriptor->PartialResourceList.Count; j++)
			{
				PartialDescriptor = FullDescriptor->PartialResourceList.PartialDescriptors + j;
			
				// Currently we only need ports
				if (PartialDescriptor->Type == CmResourceTypePort)
				{
					USHORT Base = (USHORT)(PartialDescriptor->u.Port.Start.QuadPart & 0xffff);
					USHORT Length = (USHORT)(PartialDescriptor->u.Port.Length & 0xffff);

					DFTRACE(">> I/O range Base 0x%04hx, Length 0x%04hx (Port.Start 0x%llx, Port.Length 0x%lx)\n", 
						Base, Length, PartialDescriptor->u.Port.Start.QuadPart, PartialDescriptor->u.Port.Length);

					if (Length)
					{
						DeviceExtension->PortResources[DeviceExtension->PortRangeCount].StartAddress = Base;
						DeviceExtension->PortResources[DeviceExtension->PortRangeCount].EndAddress = Base + Length - 1;
						DeviceExtension->PortRangeCount++;
					}
				}
			}

			FullDescriptor = (CM_FULL_RESOURCE_DESCRIPTOR *)(
				FullDescriptor->PartialResourceList.PartialDescriptors + 
				FullDescriptor->PartialResourceList.Count);
		}
	}
	else
	{
		DFTRACE("Null resource list\n");
	}

	DeviceExtension->DeviceState = 0;

	return STATUS_SUCCESS;
}


//	case IRP_MN_QUERY_REMOVE_DEVICE:	// Mandatory
//	case IRP_MN_REMOVE_DEVICE:			// Mandatory
//	case IRP_MN_CANCEL_REMOVE_DEVICE:	// Mandatory
//	case IRP_MN_STOP_DEVICE:			// Mandatory

NTSTATUS
DioPnpQueryRemoveDevice(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
{
	// Info In/Out : None, None
	return STATUS_SUCCESS;
}

NTSTATUS
DioPnpRemoveDevice(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
{
	// Info In/Out : None, None
	DIO_DEVICE_EXTENSION *DeviceExtension;
	DeviceExtension = (DIO_DEVICE_EXTENSION *)DeviceObject->DeviceExtension;

	//
	// FIXME : Additionally stop the device.
	//         Acquire-Release the remove lock while processing remove.
	//         
	//         For more information how to handle the PnP IRPs, see following:
	//         https://github.com/microsoft/Windows-driver-samples/blob/master/serial/serenum/pnp.c
	//

	RtlFreeUnicodeString(&DeviceExtension->PhysicalDeviceSymbolicLinkName);
	IoDeleteSymbolicLink(&DeviceExtension->FunctionDeviceSymbolicLinkName);

	IoDetachDevice(DeviceExtension->LowerLevelDeviceObject);
	IoDeleteDevice(DeviceObject);

	return STATUS_SUCCESS;
}


NTSTATUS
DioDispatchPnP(
	IN PDEVICE_OBJECT DeviceObject, 
	IN PIRP Irp)
/**
 *	@brief	Dispatch routine for IRP_MJ_PNP.
 *	
 *	@param	[in] DeviceObject			Device object.
 *	@param	[in] Irp					Irp object.
 *	@return								STATUS_SUCCESS if succeeds.
 *	
 */
{
	PIO_STACK_LOCATION IoStackLocation;
	PSZ MinorFunctionString;
	ULONG MinorFunction;
	DIO_DEVICE_EXTENSION *DeviceExtension;
	NTSTATUS Status = STATUS_SUCCESS;

	DeviceExtension = (DIO_DEVICE_EXTENSION *)DeviceObject->DeviceExtension;
	IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
	MinorFunction = IoStackLocation->MinorFunction;
	MinorFunctionString = DioPnpRtlLookupMinorFunctionName(MinorFunction);

	DFTRACE("IRP_MJ_PNP, Minor %d (%s)\n", MinorFunction, MinorFunctionString);

	DIO_IN_DEBUG_BREAKPOINT();

	// 1, 2, 3, 7, 9, 19, 20
	switch (MinorFunction)
	{
	case IRP_MN_START_DEVICE:			// Mandatory* (not called because PnP manager thinks the device is already started at AddDevice.)
		Status = DioPnpStartDevice(DeviceObject, Irp);
		break;

	case IRP_MN_QUERY_REMOVE_DEVICE:	// Mandatory
		break;

	case IRP_MN_REMOVE_DEVICE:			// Mandatory
		Status = DioPnpRemoveDevice(DeviceObject, Irp);
		break;

	case IRP_MN_CANCEL_REMOVE_DEVICE:	// Mandatory
	case IRP_MN_STOP_DEVICE:			// Mandatory
	case IRP_MN_QUERY_STOP_DEVICE:		// Mandatory
	case IRP_MN_CANCEL_STOP_DEVICE:		// Mandatory
		break;

//	case IRP_MN_QUERY_DEVICE_RELATIONS:	// Non-PnP device must not handle this

//	case IRP_MN_QUERY_INTERFACE: // Maybe we dont need to handle this
//	case IRP_MN_QUERY_CAPABILITIES:
//	case IRP_MN_QUERY_RESOURCES: // for PDO
//	case IRP_MN_QUERY_RESOURCE_REQUIREMENTS: // for PDO
//	case IRP_MN_QUERY_DEVICE_TEXT:
//	case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:

//	case IRP_MN_READ_CONFIG:
//	case IRP_MN_WRITE_CONFIG:
//	case IRP_MN_EJECT:
//	case IRP_MN_SET_LOCK:
//	case IRP_MN_QUERY_ID: // opt
	case IRP_MN_QUERY_PNP_DEVICE_STATE:
		Irp->IoStatus.Information = DeviceExtension->DeviceState;
		break;
//	case IRP_MN_QUERY_BUS_INFORMATION:
//	case IRP_MN_DEVICE_USAGE_NOTIFICATION:

	case IRP_MN_SURPRISE_REMOVAL:		// Mandatory
		break;

	default:
		DFTRACE("PnP minor function %d (%s) - Not supported\n", MinorFunction, MinorFunctionString);
		Status = STATUS_NOT_SUPPORTED;
		break;
	}

//		IoSkipCurrentIrpStackLocation(Irp);
//		return IoCallDriver(DiopStackTopDeviceObject, Irp);

	Irp->IoStatus.Status = Status;
	IofCompleteRequest(Irp, IO_NO_INCREMENT);

	return Status;
}


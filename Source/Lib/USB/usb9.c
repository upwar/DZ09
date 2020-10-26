// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* This file is part of the DZ09 project.
*
* Copyright (C) 2020, 2019 AJScorp
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; version 2 of the License.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/
#include "systemconfig.h"
#include "usb.h"
#include "usb9.h"

static pUSBDRIVERINTERFACE DevInterface;
static uint8_t U9Buffer[2] __attribute__ ((aligned (4)));

static boolean USB9_GetDescriptor(pUSBSETUP Setup)
{
    boolean Error = false;

    if (Setup->bmRequestType != USB_CMD_STDDEVIN)
        Error = true;
    else
    {
        switch(Setup->wValue & USB_CMD_DESCMASK)
        {
        case USB_CMD_DEVICE:
            DebugPrint("->DEVICE\r\n");
            USB_PrepareDataTransmit(USB_EP0, DevInterface->DeviceDescriptor,
                                    min(DEV_LENGTH, Setup->wLength));
            break;
        case USB_CMD_CONFIG:
            DebugPrint("->CONFIG\r\n");
            USB_PrepareDataTransmit(USB_EP0, DevInterface->ConfigDescriptor,
                                    min(DevInterface->ConfigDescriptor->wTotalLength, Setup->wLength));
            break;
        case USB_CMD_STRING:
        {
            uint8_t        Index = Setup->wValue & 0x00FF;
            pUSB_STR_DESCR String = DevInterface->GetStringDescriptor(Index);

            DebugPrint("->STRING\r\n");
            if (String == NULL) Error = true;
            else USB_PrepareDataTransmit(USB_EP0, String, min(String->bLength, Setup->wLength));
        }
        break;
        default:
            Error = true;
            break;
        }
    }
    return Error;
}

static boolean USB9_ClearFeature(pUSBSETUP Setup)
{
    boolean Error = false;

    switch (Setup->bmRequestType & USB_FS_MASK)
    {
    case USB_FS_ENDPOINT:
    {
        uint8_t EPIndex = Setup->wIndex & USB_EPNUM_MASK;

        DebugPrint("ENDPOINT %02X\r\n", Setup->wIndex);

        if (EPIndex > USB_EP_MAXNUM) Error = true;
        else
        {
            if (((Setup->wIndex & USB_DIR_MASK) == USB_DIR_OUT) && EPIndex)
                EPIndex += USB_EP_MAXNUM;
            if (EPIndex < USB_EPNUM) USB_ControlEPStall(EPIndex, false);
            else Error = true;
        }
    }
    break;
    case USB_FS_DEVICE:                                                                             // Remote Wakeup / Self Powered features
        DebugPrint("DEVICE\r\n");
        Error = true;
        break;
    case USB_FS_INTERFACE:
        DebugPrint("INTERFACE\r\n");
        Error = true;
        break;
    }
    return Error;
}

static boolean USB9_SetFeature(pUSBSETUP Setup)
{
    boolean Error = false;

    switch (Setup->bmRequestType & USB_FS_MASK)
    {
    case USB_FS_ENDPOINT:
    {
        uint8_t EPIndex = Setup->wIndex & USB_EPNUM_MASK;

        if (EPIndex > USB_EP_MAXNUM) Error = true;
        else
        {
            if (((Setup->wIndex & USB_DIR_MASK) == USB_DIR_OUT) && EPIndex)
                EPIndex += USB_EP_MAXNUM;
            if (EPIndex < USB_EPNUM) USB_ControlEPStall(EPIndex, true);
            else Error = true;
        }
    }
    break;
    case USB_FS_DEVICE:                                                                             // Remote Wakeup / Self Powered features
    case USB_FS_INTERFACE:
        Error = true;
        break;
    }
    return Error;
}

static boolean USB9_GetStatus(pUSBSETUP Setup)
{
    boolean Error = false;

    switch (Setup->bmRequestType & USB_FS_MASK)
    {
    case USB_FS_ENDPOINT:
    {
        uint8_t EPIndex = Setup->wIndex & USB_EPNUM_MASK;

        DebugPrint("ENDPOINT %02X\r\n", Setup->wIndex);
        if (EPIndex > USB_EP_MAXNUM) Error = true;
        else
        {
            if (((Setup->wIndex & USB_DIR_MASK) == USB_DIR_OUT) && EPIndex)
                EPIndex += USB_EP_MAXNUM;
            if (EPIndex < USB_EPNUM)
            {
                U9Buffer[0] = EPState[EPIndex].Stalled;
                U9Buffer[1] = 0;
                USB_PrepareDataTransmit(USB_EP0, (void *)U9Buffer, 2);
            }
            else Error = true;
        }
    }
    break;
    case USB_FS_DEVICE:
        DebugPrint("DEVICE\r\n");
        USB_PrepareDataTransmit(USB_EP0, (void *)DevInterface->DeviceStatus, 2);
        break;
    case USB_FS_INTERFACE:
        DebugPrint("INTERFACE\r\n");
        U9Buffer[0] = U9Buffer[1] = 0;
        USB_PrepareDataTransmit(USB_EP0, (void *)U9Buffer, 2);
        break;
    }
    return Error;
}

static void USB9_HandleStdRequest(pUSBSETUP Setup)
{
    boolean Error = false;

    switch(Setup->bRequest)
    {
    case USB_SET_ADDRESS:
        DebugPrint("SET_ADDRESS %02X\r\n", Setup->wValue);
        USB_SetDeviceAddress(Setup->wValue);
        break;
    case USB_GET_DESCRIPTOR:
        DebugPrint("GET_DESCRIPTOR\r\n");
        Error = USB9_GetDescriptor(Setup);
        break;
    case USB_GET_CONFIGURATION:
        DebugPrint("GET_CONFIGURATION\r\n");
        USB_PrepareDataTransmit(USB_EP0, (void *)DevInterface->ConfigIndex, 1);
        break;
    case USB_GET_STATUS:
        DebugPrint("GET_STATUS ");
        Error = USB9_GetStatus(Setup);
        break;
    case USB_CLEAR_FEATURE:
        DebugPrint("CLEAR_FEATURE ");
        Error = USB9_ClearFeature(Setup);
        break;
    case USB_SET_FEATURE:
        DebugPrint("SET_FEATURE\r\n");
        Error = USB9_SetFeature(Setup);
        break;
    case USB_SET_CONFIGURATION:
        DebugPrint("SET_CONFIGURATION\r\n");
        if (USBDeviceState < USB_DEVICE_ADDRESSED) Error = true;
        else
        {
            uint8_t ConfigIndex = Setup->wValue;

            if (ConfigIndex > DevInterface->DeviceDescriptor->bNumConfigurations) Error = true;
            else
            {
                DevInterface->SetConfiguration(ConfigIndex);
                USBDeviceState = (ConfigIndex) ? USB_DEVICE_CONFIGURED : USB_DEVICE_ADDRESSED;
            }
        }
        break;
    case USB_GET_INTERFACE:
        DebugPrint("GET_INTERFACE\r\n");
        if ((USBDeviceState < USB_DEVICE_CONFIGURED) ||
                (DevInterface->GetAltInterface == NULL)) Error = true;
        else
        {
            uint8_t *AltInerfIndex = DevInterface->GetAltInterface(Setup->wIndex);

            if (AltInerfIndex == NULL) Error = true;
            else USB_PrepareDataTransmit(USB_EP0, AltInerfIndex, 1);
        }
        break;
    case USB_SET_INTERFACE:
        DebugPrint("SET_INTERFACE\r\n");
        if ((USBDeviceState < USB_DEVICE_CONFIGURED) ||
                (DevInterface->SetAltInterface == NULL)) Error = true;
        else Error = !DevInterface->SetAltInterface(Setup->wIndex, Setup->wValue);
        break;
    case USB_SET_DESCRIPTOR:
    case USB_SYNCH_FRAME:
    default:
        Error = true;
        break;
    }

    if (EPState[USB_EP0].Stage == EPSTAGE_IDLE)                                                     // no data to transmit
        USB_UpdateEPState(USB_EP0, USB_DIR_OUT, Error, true);
    else USB_UpdateEPState(USB_EP0, USB_DIR_OUT, Error, false);
}

void USB9_HandleSetupRequest(pUSBSETUP Setup, uint32_t ExtraDataSize)
{
    switch(Setup->bmRequestType & USB_CMD_TYPEMASK)
    {
    case USB_CMD_STDREQ:
        DebugPrint("STDREQ: ");
        USB9_HandleStdRequest(Setup);
        break;
    case USB_CMD_CLASSREQ:
        if ((DevInterface->InterfaceReqHandler != NULL) &&
                ((Setup->bmRequestType == USB_CMD_CLASSIFIN) ||
                 (Setup->bmRequestType == USB_CMD_CLASSIFOUT)))
        {
            DebugPrint("CLASSREQ: ");
            DevInterface->InterfaceReqHandler(Setup);
        }
        else USB_UpdateEPState(USB_EP0, USB_DIR_OUT, true, false);
        break;
    case USB_CMD_VENDREQ:
        DebugPrint("VENDREQ: ");
        if (DevInterface->VendorReqHandler != NULL)
        {
            DevInterface->VendorReqHandler(Setup);
        }
        break;
    default:
        USB_UpdateEPState(USB_EP0, USB_DIR_OUT, true, false);
        break;
    }
}

boolean USB9_InterfaceInitialize(void)
{
    DevInterface = USB_ITF_Initialize();

    return (DevInterface != NULL);
}

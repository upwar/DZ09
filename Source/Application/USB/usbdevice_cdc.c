// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* This file is part of the DZ09 project.
*
* Copyright (C) 2020 AJScorp
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
#include "usb9.h"
#include "usbdevice_cdc.h"

#define USB_CDC_CONTROL_EP          (USB_EP3 | USB_DIR_IN)
#define USB_CDC_DATAIN_EP           (USB_EP1 | USB_DIR_IN)
#define USB_CDC_DATAOUT_EP          (USB_EP2 | USB_DIR_OUT)

#define USB_CDC_EPDEV_MAXP           USB_EP1_FIFOSIZE                                               // The same for EP2
#define USB_CDC_EPCTL_MAXP           USB_EP3_FIFOSIZE

#define CDC_CTL_INTERFACE_INDEX     0x00
#define CDC_DATA_INTERFACE_INDEX    0x01

typedef struct
{
    uint16_t Request;
    uint8_t  Data;
} CDC_VENDOR_REQ;

typedef struct
{
    uint32_t dwDTERate;
    uint8_t  bCharFormat;
    uint8_t  bParityType;
    uint8_t  bDataBits;
} CDC_LINE_CODING, *pCDC_LINE_CODING;

_USB_STRING_(sd00_cdc, 0x0409);
_USB_STRING_(sd01_cdc, u"Prolific Technology Inc. ");
_USB_STRING_(sd02_cdc, u"USB-Serial Controller D");

static const pUSB_STR_DESCR STR_DATA_CDC[] =
{
    (const pUSB_STR_DESCR)&sd00_cdc,
    (const pUSB_STR_DESCR)&sd01_cdc,
    (const pUSB_STR_DESCR)&sd02_cdc
};

static const uint8_t DEV_DESC_CDC[] =
{
    DEV_LENGTH,                                                                                     // Size of this descriptor in bytes
    USB_DEVICE,                                                                                     // DEVICE Descriptor
    0x10, 0x01,                                                                                     // USB version 1.1
    0x00,                                                                                           // Devise Class Code
    0x00,                                                                                           // Device Subclass Code
    0x00,                                                                                           // Protocol Code
    USB_EP0_FIFOSIZE,                                                                               // EP0 packet max size
    0x7B, 0x06,                                                                                     // Vendor ID
    0x03, 0x23,                                                                                     // Product ID
    0x00, 0x04,                                                                                     // Revision ID in BCD
    0x01,                                                                                           // Index of string descriptor describing manufacturer
    0x02,                                                                                           // Index of Product description string in String Descriptor
    0x00,                                                                                           // Index of string descriptor describing the device's serial number
    0x01                                                                                            // Number of possible configurations
};

static const uint8_t CFG_DESC_CDC[] =
{
    CFG_LENGTH,                                                                                     // Size of this descriptor in bytes
    USB_CONFIG,                                                                                     // CONFIGURATION Descriptor
    0x27, 0x00,                                                                                     // Total size of descriptor, including Interface and Endpoint descriptors
    0x01,                                                                                           // Number of interfaces supported by this configuration
    0x01,                                                                                           // Index of configuration
    0x00,                                                                                           // Index of string descriptor describing this configuration
    0x80,                                                                                           // Powered from USB
    0x64,                                                                                           // Max power consumption 200 mA

// CDC control interface
    /*Interface Descriptor */
    INT_LENGTH,                                                                                     // Size of this descriptor in bytes
    USB_INTERFACE,                                                                                  // INTERFACE Descriptor
    CDC_CTL_INTERFACE_INDEX,                                                                        // Index of interface in this Configuration
    0x00,                                                                                           // Alternative interface ('0' - None)
    0x03,                                                                                           // Number of endpoints, used by this interface
    0xFF,                                                                                           // Class Code: Vendor specific (?)
    0x00,                                                                                           // Subclass Code: Reserved (?)
    0x00,                                                                                           // Protocol Code: No class specific protocol required
    0x00,                                                                                           // Index of string descriptor describing this interface

    // Pipe 1 (endpoint 1)
    END_LENGTH,                                                                                     // Size of this descriptor in bytes
    USB_ENDPOINT,                                                                                   // ENDPOINT Descriptor
    USB_CDC_CONTROL_EP,                                                                             // IN Endpoint with address 0x03
    USB_EPTYPE_INTR,                                                                                // Data transfer type - Interrupt
    USB_CDC_EPCTL_MAXP, 0x00,                                                                       // Max packet size = 16
    0x01,                                                                                           // Endpoint polling interval - 1 ms

    // Pipe 2 (endpoint 2)
    END_LENGTH,                                                                                     // Size of this descriptor in bytes
    USB_ENDPOINT,                                                                                   // ENDPOINT Descriptor
    USB_CDC_DATAIN_EP,                                                                              // IN Endpoint with address 0x01
    0x02,                                                                                           // Data transfer Type - Bulk
    USB_CDC_EPDEV_MAXP, 0x00,                                                                       // Max packet size = 64
    0x00,                                                                                           // '0' - endpoint never NAKs

    // Pipe 2 (endpoint 3)
    END_LENGTH,                                                                                     // Size of this descriptor in bytes
    USB_ENDPOINT,                                                                                   // ENDPOINT Descriptor
    USB_CDC_DATAOUT_EP,                                                                             // OUT Endpoint with address 0x02
    0x02,                                                                                           // Data transfer Type - Bulk
    USB_CDC_EPDEV_MAXP, 0x00,                                                                       // Max packet size = 64
    0x00                                                                                            // '0' - endpoint never NAKs
};

static TUSBDRIVERINTERFACE USB_CDC_Interface;
static uint8_t  CDC_DeviceConfig;
static uint16_t CDC_DeviceStatus;

static CDC_LINE_CODING CDC_Params =
{
    115200,
    0,
    0,
    8
};

static CDC_VENDOR_REQ VReq[] =
{
    0x0080, 0x01,
    0x0081, 0x00,
    0x0082, 0x44,
    0x8383, 0xFF,
    0x8484, 0x02,
    0x8686, 0xAA,
    0x9494, 0x00
};

static uint8_t USB_CDC_GetStringDescriptorCount(void)
{
    return sizeof(STR_DATA_CDC) / sizeof(STR_DATA_CDC[0]);
}

static pUSB_STR_DESCR USB_CDC_GetStringDescriptor(uint8_t Index)
{
    return (Index < USB_CDC_GetStringDescriptorCount()) ? STR_DATA_CDC[Index] : NULL;
}

static void USB_CDC_SetConfiguration(uint8_t Index)
{
    /* Do something here when the configuration changes */
    CDC_DeviceConfig = Index;
}

static void USB_CDC_InterfaceReqHandler(pUSBSETUP Setup)
{
    switch (Setup->bRequest)
    {
    case SET_LINE_CODING:
        break;
    case GET_LINE_CODING:
        break;
    case SET_CONTROL_LINE_STATE:
        break;
    default:
        break;
    }
}

static void USB_CDC_VendorReqHandler(pUSBSETUP Setup)
{
//    if ((Setup->bmRequestType & USB_CMD_DATADIR) == USB_DIR_OUT)
//        USB_StartTransmitData(USB_EP0, NULL, 0);
//    else
//    {
//        if (Setup->wValue == 0x0606)
//            USB_StartTransmitData(USB_EP0, NULL, 0);
//        else
//        {
//            uint32_t i;
//
//            bError = true;
//
//            for(i = 0; i < sizeof(VReq) / sizeof(CDC_VENDOR_REQ); i++)
//                if (Setup->wValue == VReq[i].Request)
//                {
//                    USB_StartTransmitData(USB_EP0, &VReq[i].Data, sizeof(VReq[i].Data));
//                    bError = false;
//                    break;
//                }
//        }
//    }
}

void *USB_CDC_Initialize(void)
{
    CDC_DeviceConfig = 0;
    CDC_DeviceStatus = 0;
    memset(&USB_CDC_Interface, 0x00, sizeof(TUSBDRIVERINTERFACE));

    USB_CDC_Interface.DeviceDescriptor = (pUSB_DEV_DESCR)DEV_DESC_CDC;
    USB_CDC_Interface.ConfigDescriptor = (pUSB_CFG_DESCR)CFG_DESC_CDC;
    USB_CDC_Interface.GetStringDescriptor = USB_CDC_GetStringDescriptor;
    USB_CDC_Interface.ConfigIndex = &CDC_DeviceConfig;
    USB_CDC_Interface.SetConfiguration = USB_CDC_SetConfiguration;
    USB_CDC_Interface.DeviceStatus = &CDC_DeviceStatus;
    USB_CDC_Interface.InterfaceReqHandler = USB_CDC_InterfaceReqHandler;
    USB_CDC_Interface.VendorReqHandler = USB_CDC_VendorReqHandler;

    return &USB_CDC_Interface;
}

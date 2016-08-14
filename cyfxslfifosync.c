/*
 ## Cypress USB 3.0 Platform source file (cyfxbulksrcsink.c)
 ## ===========================
 ##
 ##  Copyright Cypress Semiconductor Corporation, 2010-2011,
 ##  All Rights Reserved
 ##  UNPUBLISHED, LICENSED SOFTWARE.
 ##
 ##  CONFIDENTIAL AND PROPRIETARY INFORMATION
 ##  WHICH IS THE PROPERTY OF CYPRESS.
 ##
 ##  Use of this file is governed
 ##  by the license agreement included in the file
 ##
 ##     LICENSE_CYPRESS.txt
 ##
 ## ===========================
 */

/* This file illustrates the bulk source sink application example using the DMA MANUAL_IN
 * and DMA MANUAL_OUT mode */

/*
   This example illustrates USB endpoint data source and data sink mechanism. The example
   comprises of vendor class USB enumeration descriptors with 2 bulk endpoints. A bulk OUT
   endpoint acts as the producer of data and acts as the sink to the host. A bulk IN endpoint
   acts as the consumer of data and acts as the source to the host.

   The data source and sink is achieved with the help of a DMA MANUAL IN channel and a DMA
   MANUAL OUT channel. A DMA MANUAL IN channel is created between the producer USB bulk
   endpoint and the CPU. A DMA MANUAL OUT channel is created between the CPU and the consumer
   USB bulk endpoint. Data is received in the IN channel DMA buffer from the host through the
   producer endpoint. CPU is signalled of the data reception using DMA callbacks. The CPU
   discards this buffer. This leads to the sink mechanism. A constant patern data is loaded
   onto the OUT Channel DMA buffer whenever the buffer is available. CPU issues commit of
   the DMA data transfer to the consumer endpoint which then gets transferred to the host.
   This leads to a constant source mechanism.

   The DMA buffer size is defined based on the USB speed. 64 for full speed, 512 for high speed
   and 1024 for super speed. CY_FX_BULKSRCSINK_DMA_BUF_COUNT in the header file defines the
   number of DMA buffers.

   For performance optimizations refer the readme.txt
 */

#include "its_fx3_project_config.h"

#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyfxslfifosync.h"
#include "cyu3usb.h"
#include "cyu3uart.h"
#include "cyu3gpif.h"
#include "cyu3pib.h"
#include "cyu3spi.h"
#include "pib_regs.h"
#include "cyfxspi_bb.h"
#include "gpif2_config.h"
//#include "spi_patch.h"


uint8_t glEp0Buffer[32];
uint16_t glRecvdLen;
CyU3PThread     bulkSrcSinkAppThread;	 /* Application thread structure */
CyU3PDmaChannel glChHandleBulkSink;      /* DMA MANUAL_IN channel handle.          */
CyU3PDmaMultiChannel glChHandleBulkSrc;       /* DMA MANUAL_OUT channel handle.         */

CyBool_t glIsApplnActive = CyFalse;      /* Whether the source sink application is active or not. */
CyBool_t glStartAd9269Gpif = CyFalse;
uint32_t glDMARxCount = 0;               /* Counter to track the number of buffers received. */
uint32_t glDMATxCount = 0;               /* Counter to track the number of buffers transmitted. */

CyU3PDmaChannel glSpiTxHandle;   /* SPI Tx channel handle */
CyU3PDmaChannel glSpiRxHandle;   /* SPI Rx channel handle */


/* Application Error Handler */
void
CyFxAppErrorHandler (
		CyU3PReturnStatus_t apiRetStatus    /* API return status */
)
{
	/* Application failed with the error code apiRetStatus */

	/* Add custom debug or recovery actions here */

	/* Loop Indefinitely */
	for (;;)
	{
		/* Thread sleep : 100 ms */
		CyU3PThreadSleep (100);
	}
}

/* This function initializes the debug module. The debug prints
 * are routed to the UART and can be seen using a UART console
 * running at 115200 baud rate. */
void
CyFxBulkSrcSinkApplnDebugInit (void)
{
	CyU3PUartConfig_t uartConfig;
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

	/* Initialize the UART for printing debug messages */
	apiRetStatus = CyU3PUartInit();

	//apiRetStatus = CyU3PUartInit();
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		/* Error handling */
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Set UART configuration */
	CyU3PMemSet ((uint8_t *)&uartConfig, 0, sizeof (uartConfig));
	uartConfig.baudRate = CY_U3P_UART_BAUDRATE_115200;
	uartConfig.stopBit = CY_U3P_UART_ONE_STOP_BIT;
	uartConfig.parity = CY_U3P_UART_NO_PARITY;
	uartConfig.txEnable = CyTrue;
	uartConfig.rxEnable = CyFalse;
	uartConfig.flowCtrl = CyFalse;
	uartConfig.isDma = CyTrue;

	//apiRetStatus = CyU3PUartSetConfig (&uartConfig, NULL);
	apiRetStatus = CyU3PUartSetConfig (&uartConfig, NULL);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Set the UART transfer to a really large value. */
	//apiRetStatus = CyU3PUartTxSetBlockXfer (0xFFFFFFFF);
	apiRetStatus = CyU3PUartTxSetBlockXfer (0xFFFFFFFF);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Initialize the debug module. */
	apiRetStatus = CyU3PDebugInit (CY_U3P_LPP_SOCKET_UART_CONS, 8);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyFxAppErrorHandler(apiRetStatus);
	}
/*
	CyU3PThreadSleep (3000);
	CyU3PDebugPrint (4, "UART CONFIGURED0\n");
	CyU3PThreadSleep (3000);
	CyU3PDebugPrint (4, "UART CONFIGURED1\n");
	CyU3PThreadSleep (3000);
	CyU3PDebugPrint (4, "UART CONFIGURED2\n");
	CyU3PThreadSleep (3000);
	CyU3PDebugPrint (4, "UART CONFIGURED3\n");*/
}

CyU3PDmaChannel glChHandleUtoCPU;   /* DMA Channel handle for U2CPU transfer. */
CyU3PDmaChannelConfig_t dmaCfg1;

/* This function starts the application. This is called
 * when a SET_CONF event is received from the USB host. The endpoints
 * are configured and the DMA pipe is setup in this function. */
void
CyFxBulkSrcSinkApplnStart (
		void)
{
	uint16_t size = 0;
	CyU3PEpConfig_t epCfg;


	CyU3PDmaMultiChannelConfig_t dmaCfg;
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	CyU3PUSBSpeed_t usbSpeed = CyU3PUsbGetSpeed();

	/* First identify the usb speed. Once that is identified,
	 * create a DMA channel and start the transfer on this. */

	/* Based on the Bus Speed configure the endpoint packet size */
	switch (usbSpeed)
	{
	case CY_U3P_FULL_SPEED:
		size = 64;
		break;

	case CY_U3P_HIGH_SPEED:
		size = 512;
		break;

	case  CY_U3P_SUPER_SPEED:
		size = 1024;
		break;

	default:
		CyU3PDebugPrint (4, "Error! Invalid USB speed.\n");
		CyFxAppErrorHandler (CY_U3P_ERROR_FAILURE);
		break;
	}
#if 1
	CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
	epCfg.enable = CyTrue;
	epCfg.epType = CY_U3P_USB_EP_BULK;
	epCfg.burstLen = (usbSpeed == CY_U3P_SUPER_SPEED) ?
			(CY_FX_EP_BURST_LENGTH) : 1;
	epCfg.streams = 0;
	epCfg.pcktSize = size;
#if 0
	/* Producer endpoint configuration */
	apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_PRODUCER, &epCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler (apiRetStatus);
	}
	/* Flush the endpoint memory */
	CyU3PUsbFlushEp(CY_FX_EP_PRODUCER);
#else
	/* Consumer endpoint configuration */
	apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler (apiRetStatus);
	}


#endif
	/* Create a DMA MANUAL_IN channel for the producer socket. */
	CyU3PMemSet ((uint8_t *)&dmaCfg, 0, sizeof (dmaCfg));
	/* The buffer size will be same as packet size for the
	 * full speed, high speed and super speed non-burst modes.
	 * For super speed burst mode of operation, the buffers will be
	 * 1024 * burst length so that a full burst can be completed.
	 * This will mean that a buffer will be available only after it
	 * has been filled or when a short packet is received. */
	dmaCfg.size  = (usbSpeed == CY_U3P_SUPER_SPEED) ?
			(size * CY_FX_EP_BURST_LENGTH ) : (size);
	dmaCfg.size  = (size * CY_FX_EP_BURST_LENGTH );
	dmaCfg.count = CY_FX_BULKSRCSINK_DMA_BUF_COUNT;
	dmaCfg.validSckCount = 2;
#if 0
	dmaCfg.prodSckId[0] = CY_FX_EP_PRODUCER_SOCKET;
	dmaCfg.consSckId[0] = CY_FX_CONSUMER_PPORT_SOCKET;
	dmaCfg.consSckId[1] = CY_FX_CONSUMER_PPORT_SOCKET;
#endif
	dmaCfg.dmaMode = CY_U3P_DMA_MODE_BYTE;
	dmaCfg.notification = 0;
	dmaCfg.cb = NULL;
	dmaCfg.prodHeader = 0;
	dmaCfg.prodFooter = 0;
	dmaCfg.consHeader = 0;
	dmaCfg.prodAvailCount = 0;
#if 0
	apiRetStatus = CyU3PDmaChannelCreate (&glChHandleBulkSink,
			CY_U3P_DMA_TYPE_AUTO_SIGNAL, &dmaCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PDmaChannelCreate failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Set DMA Channel transfer size */
	apiRetStatus = CyU3PDmaChannelSetXfer (&glChHandleBulkSink, CY_FX_BULKSRCSINK_DMA_TX_SIZE);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PDmaChannelSetXfer failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}
#else
	/* Create a DMA MANUAL_OUT channel for the consumer socket. */
	dmaCfg.prodSckId[0] = CY_U3P_PIB_SOCKET_0;
	dmaCfg.prodSckId[1] = CY_U3P_PIB_SOCKET_1;
	dmaCfg.consSckId[0] = CY_FX_EP_CONSUMER_SOCKET;
	apiRetStatus = CyU3PDmaMultiChannelCreate (&glChHandleBulkSrc, CY_U3P_DMA_TYPE_AUTO_MANY_TO_ONE, &dmaCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PDmaChannelCreate failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Set DMA Channel transfer size */

	apiRetStatus = CyU3PDmaMultiChannelSetXfer (&glChHandleBulkSrc, CY_FX_BULKSRCSINK_DMA_TX_SIZE, 0);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PDmaChannelSetXfer failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Flush the endpoint memory */
		CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);

#endif
#endif

#if 1
		{
	    CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
	    epCfg.enable = CyTrue;
	    epCfg.epType = CY_U3P_USB_EP_BULK;
	    epCfg.burstLen = 1;
	    epCfg.streams = 0;
	    epCfg.pcktSize = size;

	    /* Producer endpoint configuration */
	    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_PRODUCER, &epCfg);
	    if (apiRetStatus != CY_U3P_SUCCESS)
	    {
	        CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
	        CyFxAppErrorHandler (apiRetStatus);
	    }

	    CyU3PMemSet ((uint8_t *)&dmaCfg1, 0, sizeof (dmaCfg1));
	    /* Create a DMA MANUAL channel for U2CPU transfer.
	     * DMA size is set based on the USB speed. */
	    dmaCfg1.size  = size;
	    dmaCfg1.count = 16;
	    dmaCfg1.prodSckId = CY_U3P_UIB_SOCKET_PROD_1;
	    dmaCfg1.consSckId = CY_U3P_CPU_SOCKET_CONS;
	    dmaCfg1.dmaMode = CY_U3P_DMA_MODE_BYTE;
	    /* Enabling the callback for produce event. */
	    dmaCfg1.notification = 0;
	    dmaCfg1.cb = NULL;
	    dmaCfg1.prodHeader = 0;
	    dmaCfg1.prodFooter = 0;
	    dmaCfg1.consHeader = 0;
	    dmaCfg1.prodAvailCount = 0;

	    apiRetStatus = CyU3PDmaChannelCreate (&glChHandleUtoCPU,
	    		CY_U3P_DMA_TYPE_MANUAL_IN, &dmaCfg1);
	    if (apiRetStatus != CY_U3P_SUCCESS)
	    {
	        CyU3PDebugPrint (4, "CyU3PDmaChannelCreate failed, Error code = %d\n", apiRetStatus);
	        CyFxAppErrorHandler(apiRetStatus);
	    }

	    /* Flush the Endpoint memory */
	    CyU3PUsbFlushEp(CY_FX_EP_PRODUCER);

	    /* Set DMA channel transfer size. */
	    apiRetStatus = CyU3PDmaChannelSetXfer (&glChHandleUtoCPU, 16);
	    if (apiRetStatus != CY_U3P_SUCCESS)
	    {
	        CyU3PDebugPrint (4, "CyU3PDmaChannelSetXfer Failed, Error code = %d\n", apiRetStatus);
	        CyFxAppErrorHandler(apiRetStatus);
	    }
		}
#endif

#if 0
	apiRetStatus = CyU3PGpifSocketConfigure(0,CY_FX_PRODUCER_PPORT_SOCKET,0,CyTrue,0);						// True=more then (4)
	    if (apiRetStatus != CY_U3P_SUCCESS){
	        CyU3PDebugPrint (4, "CyU3PGpifSocketConfigure Failed, Error code = %d\n", apiRetStatus);
	        CyFxAppErrorHandler(apiRetStatus);
	    }
#endif

#if 0
	    /* Create the DMA channels for SPI write and read. */
	    CyU3PMemSet ((uint8_t *)&dmaCfg1, 0, sizeof(dmaCfg1));
	    dmaCfg1.size           = 16;//16=min
	    /* No buffers need to be allocated as this channel
	     * will be used only in override mode. */
	    dmaCfg1.count          = 0;
	    dmaCfg1.prodAvailCount = 0;
	    dmaCfg1.dmaMode        = CY_U3P_DMA_MODE_BYTE;
	    dmaCfg1.prodHeader     = 0;
	    dmaCfg1.prodFooter     = 0;
	    dmaCfg1.consHeader     = 0;
	    dmaCfg1.notification   = 0;
	    dmaCfg1.cb             = NULL;

	    /* Channel to write to SPI flash. */
	    dmaCfg1.prodSckId = CY_U3P_CPU_SOCKET_PROD;
	    dmaCfg1.consSckId = CY_U3P_LPP_SOCKET_SPI_CONS;
	    apiRetStatus = CyU3PDmaChannelCreate (&glSpiTxHandle,
	            CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaCfg1);
	    if (apiRetStatus != CY_U3P_SUCCESS)
	    {
	    	CyFxAppErrorHandler(apiRetStatus);
	    }

	    /* Channel to read from SPI flash. */
	    dmaCfg1.prodSckId = CY_U3P_LPP_SOCKET_SPI_PROD;
	    dmaCfg1.consSckId = CY_U3P_CPU_SOCKET_CONS;
	    apiRetStatus = CyU3PDmaChannelCreate (&glSpiRxHandle,
	            CY_U3P_DMA_TYPE_MANUAL_IN, &dmaCfg1);

#endif
	    	glIsApplnActive = CyTrue;
}

/* This function stops the application. This shall be called whenever a RESET
 * or DISCONNECT event is received from the USB host. The endpoints are
 * disabled and the DMA pipe is destroyed by this function. */
void
CyFxBulkSrcSinkApplnStop (
		void)
{
	CyU3PEpConfig_t epCfg;
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

	/* Update the flag so that the application thread is notified of this. */
	glIsApplnActive = CyFalse;

	/* Disable endpoints. */
	CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
	epCfg.enable = CyFalse;

#if 0
	/* Destroy the channels */
	CyU3PDmaChannelDestroy (&glChHandleBulkSink);
	/* Flush the endpoint memory */
	CyU3PUsbFlushEp(CY_FX_EP_PRODUCER);
	/* Producer endpoint configuration. */
	apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_PRODUCER, &epCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler (apiRetStatus);
	}

#else
	CyU3PDmaMultiChannelDestroy (&glChHandleBulkSrc);
	CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);
	/* Consumer endpoint configuration. */
	apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler (apiRetStatus);
	}
#endif
}

/* Callback to handle the USB setup requests. */
CyBool_t
CyFxBulkSrcSinkApplnUSBSetupCB (
		uint32_t setupdat0, /* SETUP Data 0 */
		uint32_t setupdat1  /* SETUP Data 1 */
)
{
	uint8_t  bRequest, bReqType;
	uint8_t  bType, bTarget;
	uint16_t wValue;
	uint16_t wLength;
	uint16_t wIndex;

	/* Decode the fields from the setup request. */
	bReqType = (setupdat0 & CY_U3P_USB_REQUEST_TYPE_MASK);
	bType    = (bReqType & CY_U3P_USB_TYPE_MASK);
	bTarget  = (bReqType & CY_U3P_USB_TARGET_MASK);
	bRequest = ((setupdat0 & CY_U3P_USB_REQUEST_MASK) >> CY_U3P_USB_REQUEST_POS);
	wValue   = ((setupdat0 & CY_U3P_USB_VALUE_MASK)   >> CY_U3P_USB_VALUE_POS);
	wLength   = ((setupdat1 & CY_U3P_USB_LENGTH_MASK)   >> CY_U3P_USB_LENGTH_POS);
	wIndex   = ((setupdat1 & CY_U3P_USB_INDEX_MASK)   >> CY_U3P_USB_INDEX_POS);

	if( bRequest == 0x05)
	{

		CyU3PDebugPrint (4, "\n\n\r Vendor Req = 0x%x Received", bRequest);
		CyU3PDebugPrint (4, "\n\n\r wValue = 0x%d, wLength = 0x%x, wIndex = 0x%x", wValue, wLength,  wIndex );
		if(wValue) /* Read */
		{
			//SpiReadWrite(wIndex, wValue, glEp0Buffer, wLength);
			/*
        			tmpbuf = glEp0Buffer;
        			CyU3PDebugPrint (4, "\n\n\r glRecvdLen = 0x%x status = 0x%x\n\n\r", glRecvdLen, apiRetStatus);
        			for(wLength=0; wLength<glRecvdLen; wLength++ )
        			{
        				CyU3PDebugPrint (4, "\t\t  0x%x ", *(tmpbuf));
        				tmpbuf++;
        			}
			 */
			//CyU3PUsbSendEP0Data(wLength, glEp0Buffer);
		}
		else
		{

			/*apiRetStatus = */
			//CyU3PUsbGetEP0Data (32, glEp0Buffer, &glRecvdLen);
			//SpiReadWrite(wIndex, wValue, glEp0Buffer, wLength);


		}

		return CyTrue;
	}
	else
		//if (bType == CY_U3P_USB_VENDOR_RQT)
		{
	   		if (bRequest == 0xB3)
	   		{
	   			//CyU3PDeviceReset(CyFalse);
	   			//if ((bReqType & 0x80) == 0)
	   			{
	   				CyU3PUsbGetEP0Data (wLength, glEp0Buffer, NULL);
	   				if (glEp0Buffer[2] != 0xFF)
	   				{
	   					//SPI send
	   					CyU3PSpiSetSsnLine (CyFalse);
	   					CyU3PSpiTransmitWords(glEp0Buffer, 2);
	   					CyU3PSpiSetSsnLine (CyTrue);
	   				}else
	   				{
	   					CyU3PDeviceReset(CyFalse);
	   				}
	   				return CyTrue;
	   			}
	   		}else if (bRequest == 0xB5)
	   		{
	   			//CyU3PDeviceReset(CyFalse);
	   			//if ((bReqType & 0x80) == 0)
	   			{
	   				CyU3PDmaBuffer_t buf_p;

	   				//CyU3PUsbGetEP0Data (wLength, glEp0Buffer, NULL);
	   				glEp0Buffer[0] = wValue; glEp0Buffer[1] = wIndex;

	   					//SPI send
	   				CyU3PSpiSetSsnLine (CyFalse);
	   				CyU3PSpiTransmitReceiveWords(glEp0Buffer, 2);

	   				//CyU3PSpiTransmitWords(glEp0Buffer, 2);
	   				CyU3PSpiSetSsnLine (CyTrue);

	   				CyU3PUsbSendEP0Data (wLength, glEp0Buffer);

	   				return CyTrue;
	   			}
	   		}
		}

	/* Fast enumeration is used. Only class, vendor and unknown requests
	 * are received by this function. These are not handled in this
	 * application. Hence return CyFalse. */
	return CyFalse;
}
/* This is a callback function to handle gpif events */
void
CyFxBulkSrcSinkApplnGPIFEventCB (
		CyU3PGpifEventType event,               /* Event type that is being notified. */
		uint8_t            currentState         /* Current state of the State Machine. */
)
{

	CyU3PDebugPrint (4, "\n\r !!!!GPIF INTERRUPT\n");

	switch (event)
	{
	case CYU3P_GPIF_EVT_SM_INTERRUPT:
	{
		CyU3PDebugPrint (4, "\n\r GPIF overflow INT received\n");

	}
	break;


	default:
		break;



	}
}

/* This is the callback function to handle the USB events. */
void
CyFxBulkSrcSinkApplnUSBEventCB (
		CyU3PUsbEventType_t evtype, /* Event type */
		uint16_t            evdata  /* Event data */
)
{
	CyU3PDebugPrint (4, "\n\r USB EVENT\n");
	switch (evtype)
	{
	case CY_U3P_USB_EVENT_SETCONF:
		/* If the application is already active
		 * stop it before re-enabling. */
		if (glIsApplnActive)
		{
			CyFxBulkSrcSinkApplnStop ();
		}
		/* Start the source sink function. */
		CyFxBulkSrcSinkApplnStart ();
		break;

	case CY_U3P_USB_EVENT_RESET:
	case CY_U3P_USB_EVENT_DISCONNECT:
		/* Stop the source sink function. */
		if (glIsApplnActive)
		{
			CyFxBulkSrcSinkApplnStop ();
		}
		break;

	default:
		break;
	}
}

void CyFxStartAd9269Gpif(void)
{
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	    /* Start the state machine. */
	    	apiRetStatus = CyU3PGpifSMStart (RESET, ALPHA_RESET);
			//apiRetStatus = CyU3PGpifSMStart (START, ALPHA_START);
	    	if (apiRetStatus != CY_U3P_SUCCESS)
	    	{
	    		CyU3PDebugPrint (4, "CyU3PGpifSMStart failed, Error Code = %d\n",apiRetStatus);

	    	}

    		CyU3PDebugPrint (4, "CyU3PGpifSMStart Done = %d\n",apiRetStatus);


}

void CyFxConfigureAd9269(uint8_t clockDiv)
{

}


/* This function initializes the USB Module, sets the enumeration descriptors.
 * This function does not start the bulk streaming and this is done only when
 * SET_CONF event is received. */
void
CyFxBulkSrcSinkApplnInit (void)
{
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	/**************************************GPIF****************************************************/
	CyU3PPibClock_t pibClock;

	/* Initialize the p-port block. */
	pibClock.clkDiv = 2;
	pibClock.clkSrc = CY_U3P_SYS_CLK;
	pibClock.isHalfDiv = CyFalse;
	/* Disable DLL for sync GPIF */
	pibClock.isDllEnable = CyFalse;
	apiRetStatus = CyU3PPibInit(CyTrue, &pibClock);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "P-port Initialization failed, Error Code = %d\n",apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Load the GPIF configuration for Slave FIFO sync mode. */
	apiRetStatus = CyU3PGpifLoad (&CyFxGpifConfig);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PGpifLoad failed, Error Code = %d\n",apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}


	CyU3PGpifRegisterCallback(CyFxBulkSrcSinkApplnGPIFEventCB);

	/**********************************************************************************************/
	/* Start the USB functionality. */
	apiRetStatus = CyU3PUsbStart();
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PUsbStart failed to Start, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* The fast enumeration is the easiest way to setup a USB connection,
	 * where all enumeration phase is handled by the library. Only the
	 * class / vendor requests need to be handled by the application. */
	CyU3PUsbRegisterSetupCallback(CyFxBulkSrcSinkApplnUSBSetupCB, CyTrue);

	/* Setup the callback to handle the USB events. */
	CyU3PUsbRegisterEventCallback(CyFxBulkSrcSinkApplnUSBEventCB);

	/* Set the USB Enumeration descriptors */

	/* Super speed device descriptor. */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_DEVICE_DESCR, NULL, (uint8_t *)CyFxUSB30DeviceDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set device descriptor failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* High speed device descriptor. */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_DEVICE_DESCR, NULL, (uint8_t *)CyFxUSB20DeviceDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set device descriptor failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* BOS descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_BOS_DESCR, NULL, (uint8_t *)CyFxUSBBOSDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set configuration descriptor failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Device qualifier descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_DEVQUAL_DESCR, NULL, (uint8_t *)CyFxUSBDeviceQualDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set device qualifier descriptor failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Super speed configuration descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_CONFIG_DESCR, NULL, (uint8_t *)CyFxUSBSSConfigDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set configuration descriptor failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* High speed configuration descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_CONFIG_DESCR, NULL, (uint8_t *)CyFxUSBHSConfigDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB Set Other Speed Descriptor failed, Error Code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Full speed configuration descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_FS_CONFIG_DESCR, NULL, (uint8_t *)CyFxUSBFSConfigDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB Set Configuration Descriptor failed, Error Code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* String descriptor 0 */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* String descriptor 1 */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* String descriptor 2 */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 2, (uint8_t *)CyFxUSBProductDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}

	/* Connect the USB Pins with super speed operation enabled. */
	apiRetStatus = CyU3PConnectState(CyTrue, CyTrue);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB Connect failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler(apiRetStatus);
	}


}

/* Entry function for the BulkSrcSinkAppThread. */
void
BulkSrcSinkAppThread_Entry (
		uint32_t input)
{

	/* Initialize the debug module */
	//CyFxBulkSrcSinkApplnDebugInit();

	/* Initialize GPIO module. */
	CyFxGpioInit();
	/* Initialize the application */
	CyFxBulkSrcSinkApplnInit();

	//CyFxConfigureAd9269(3);

	CyFxStartAd9269Gpif();
	CyU3PDebugPrint (6, "\n\rSTART DBM");
	for (;;)
	{
		CyU3PThreadSleep (3000);


		/*if (glIsApplnActive)
		{

			if(!glStartAd9269Gpif)
			{
				glStartAd9269Gpif = CyTrue;
				CyFxStartAd9269Gpif();
			}*/


			/* Print the number of buffers received / transmitted so far from the USB host. */
		//	CyU3PDebugPrint (4, "\n\rData tracker: buffers received: %d, buffers sent: %d\n", glDMARxCount, glDMATxCount);
		//}
	}
}

/* Application define function which creates the threads. */
void
CyFxApplicationDefine (
		void)
{
	void *ptr = NULL;
	uint32_t retThrdCreate = CY_U3P_SUCCESS;

	/* Allocate the memory for the threads */
	ptr = CyU3PMemAlloc (CY_FX_BULKSRCSINK_THREAD_STACK);

	/* Create the thread for the application */
	retThrdCreate = CyU3PThreadCreate (&bulkSrcSinkAppThread,      /* App thread structure */
			"21:Bulk_src_sink",                      /* Thread ID and thread name */
			BulkSrcSinkAppThread_Entry,              /* App thread entry function */
			0,                                       /* No input parameter to thread */
			ptr,                                     /* Pointer to the allocated thread stack */
			CY_FX_BULKSRCSINK_THREAD_STACK,          /* App thread stack size */
			CY_FX_BULKSRCSINK_THREAD_PRIORITY,       /* App thread priority */
			CY_FX_BULKSRCSINK_THREAD_PRIORITY,       /* App thread priority */
			CYU3P_NO_TIME_SLICE,                     /* No time slice for the application thread */
			CYU3P_AUTO_START                         /* Start the thread immediately */
	);

	/* Check the return code */
	if (retThrdCreate != 0)
	{
		/* Thread Creation failed with the error code retThrdCreate */

		/* Add custom recovery or debug actions here */

		/* Application cannot continue */
		/* Loop indefinitely */
		while(1);
	}
}

/*
 * Main function
 */
int
main (void)
{
	CyU3PIoMatrixConfig_t io_cfg;
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* Initialize the device */
	status = CyU3PDeviceInit (NULL);
	if (status != CY_U3P_SUCCESS)
	{
		goto handle_fatal_error;
	}

	/* Initialize the caches. Enable instruction cache and keep data cache disabled.
	 * The data cache is useful only when there is a large amount of CPU based memory
	 * accesses. When used in simple cases, it can decrease performance due to large
	 * number of cache flushes and cleans and also it adds to the complexity of the
	 * code. */
	status = CyU3PDeviceCacheControl (CyTrue, CyFalse, CyFalse);
	if (status != CY_U3P_SUCCESS)
	{
		goto handle_fatal_error;
	}


	/* Configure the IO matrix for the device.
	 * Use its_fx3_project_config.h file to set project defines. */

    io_cfg.isDQ32Bit        = CyFalse;

#if defined( ITS_HAVE_ONE_SDCARD )
    io_cfg.s0Mode  			= CY_U3P_SPORT_8BIT;
    io_cfg.s1Mode  			= CY_U3P_SPORT_INACTIVE;
    io_cfg.lppMode 			= CY_U3P_IO_MATRIX_LPP_DEFAULT;
    io_cfg.gpioComplexEn[0] = 0;
    io_cfg.gpioComplexEn[1] = 0;
    // TODO: Set IO bits correctly
    io_cfg.gpioSimpleEn[0]  = 0;//0x02102800; /* IOs 43, 45, 52 and 57 are chosen as GPIO. */
    io_cfg.gpioSimpleEn[1]  = 0;
#elif defined( ITS_HAVE_TWO_SDCARDS )
    io_cfg.s0Mode  			= CY_U3P_SPORT_8BIT;
    io_cfg.s1Mode  			= CY_U3P_SPORT_8BIT;
    io_cfg.lppMode 			= CY_U3P_IO_MATRIX_LPP_NONE;
    io_cfg.gpioComplexEn[0] = 0;
    io_cfg.gpioComplexEn[1] = 0;
    // TODO: Set IO bits correctly
    io_cfg.gpioSimpleEn[0]  = 0;//0x02102800; /* IOs 43, 45, 52 and 57 are chosen as GPIO. */
    io_cfg.gpioSimpleEn[1]  = 0;
#else
    io_cfg.s0Mode  			= CY_U3P_SPORT_INACTIVE;
    io_cfg.s1Mode  			= CY_U3P_SPORT_INACTIVE;
    io_cfg.lppMode 			= CY_U3P_IO_MATRIX_LPP_DEFAULT;
    io_cfg.gpioComplexEn[0] = 0;
    io_cfg.gpioComplexEn[1] = 0;
    io_cfg.gpioSimpleEn[0]  = 0;
    io_cfg.gpioSimpleEn[1]  = 0;
#endif

    io_cfg.useUart  = CyFalse;
    io_cfg.useI2C   = CyFalse;
    io_cfg.useI2S   = CyFalse;
#ifdef ITS_FX3_HAVE_SPI
    io_cfg.useSpi   = CyTrue;
#else
    io_cfg.useSpi   = CyFalse;
#endif

	status = CyU3PDeviceConfigureIOMatrix (&io_cfg);
	if (status != CY_U3P_SUCCESS)
	{
		goto handle_fatal_error;
	}

	if ( io_cfg.useSpi == CyTrue )
	{
	    CyU3PSpiConfig_t spiConfig;
	    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	    apiRetStatus = CyU3PSpiInit();
	        if (apiRetStatus != CY_U3P_SUCCESS)
	        {
	        	CyU3PDebugPrint (4, "SPI init failed, Error Code = %d\n",apiRetStatus);
	        }
        /* Start the SPI master block. Run the SPI clock at 25MHz
         * and configure the word length to 8 bits. Also configure
         * the slave select using FW. */
        CyU3PMemSet ((uint8_t *)&spiConfig, 0, sizeof(spiConfig));
        spiConfig.isLsbFirst = CyFalse;
        spiConfig.cpol       = CyFalse;//CyTrue;
        spiConfig.ssnPol     = CyFalse;
        spiConfig.cpha       = CyFalse;//CyTrue;
        spiConfig.leadTime   = CY_U3P_SPI_SSN_LAG_LEAD_HALF_CLK;
        spiConfig.lagTime    = CY_U3P_SPI_SSN_LAG_LEAD_HALF_CLK;
        spiConfig.ssnCtrl    = CY_U3P_SPI_SSN_CTRL_HW_EACH_WORD;
        //spiConfig.ssnCtrl    = CY_U3P_SPI_SSN_CTRL_FW;
        spiConfig.clock      = 10000000;
        spiConfig.wordLen    = 16;//8;

        apiRetStatus = CyU3PSpiSetConfig (&spiConfig, NULL);
        if (apiRetStatus != CY_U3P_SUCCESS)
        {
        	CyU3PDebugPrint (4, "SPI config failed, Error Code = %d\n",apiRetStatus);
        }

	}


	/* This is a non returnable call for initializing the RTOS kernel */
	CyU3PKernelEntry ();

	/* Dummy return to make the compiler happy */
	return 0;

	handle_fatal_error:


	/* Cannot recover from this error. */
	while (1);
}

/* [ ] */


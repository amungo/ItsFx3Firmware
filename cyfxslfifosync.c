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

#define PROJECT_VERSION ( 0x17072800 )

#include "its_fx3_project_config.h"

#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyfxslfifosync.h"
#include "cyu3usb.h"
#include "cyu3uart.h"
#include "cyu3gpif.h"
#include <cyu3gpio.h>
#include "cyu3pib.h"
#include "cyu3spi.h"
#include "pib_regs.h"
#include "cyfxspi_bb.h"
#include "gpif2_config.h"
#include "host_commands.h"


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

typedef enum SpiState_e {
	SPI_ST_NOT_INITED = 0,
	SPI_ST_CFG_PRERUN = 1,
	SPI_ST_CFG_WORK   = 2
} SpiState_e;

typedef struct SystemState_t {
//	CyBool_t loaded;
//	CyBool_t started;
//	CyBool_t streaming;
//	CyBool_t need_start;
	CyBool_t need_reset;
//	int32_t overflowCount;
	int32_t spi_state;
} SystemState_t;

SystemState_t state;

void StartGPIF();
void InitSPI( SpiState_e wantState );

void
CheckError (
		CyU3PReturnStatus_t apiRetStatus    /* API return status */
)
{
	if ( apiRetStatus != CY_U3P_SUCCESS ) {
		CyU3PThreadSleep(100);
		CyU3PDeviceReset(CyFalse);
	}
}

/* Application Error Handler */
void
CyFxAppErrorHandler (
		CyU3PReturnStatus_t apiRetStatus    /* API return status */
)
{
	CyU3PThreadSleep(100);
	CyU3PDeviceReset(CyFalse);
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

	CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
	epCfg.enable = CyTrue;
	epCfg.epType = CY_U3P_USB_EP_BULK;
	epCfg.burstLen = (usbSpeed == CY_U3P_SUPER_SPEED) ?
			(CY_FX_EP_BURST_LENGTH) : 1;
	epCfg.streams = 0;
	epCfg.pcktSize = size;

	/* Consumer endpoint configuration */
	apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler (apiRetStatus);
	}


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

	dmaCfg.dmaMode = CY_U3P_DMA_MODE_BYTE;
	dmaCfg.notification = 0;
	dmaCfg.cb = NULL;
	dmaCfg.prodHeader = 0;
	dmaCfg.prodFooter = 0;
	dmaCfg.consHeader = 0;
	dmaCfg.prodAvailCount = 0;

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

	CyU3PDmaMultiChannelDestroy (&glChHandleBulkSrc);
	CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);
	/* Consumer endpoint configuration. */
	apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
		CyFxAppErrorHandler (apiRetStatus);
	}
}

static uint32_t read_CPSR(void)
{
	register uint32_t cpsr;
    __asm__ volatile(
        "MRS %0, CPSR\n\t"
       : "=r"(cpsr)
    );
    return cpsr;
}
static void write_CPSR(register uint32_t cpsr)
{
    __asm__ volatile(
        "MSR CPSR, %0;"
		:
        : "r"(cpsr)
    );
}

/* Set I and F bits in Program Status Register, i.e. disable IRQ and FIQ interrupts */
static uint32_t disable_interrupts( void )
{
	uint32_t cpsr=read_CPSR();
	write_CPSR( cpsr | 0xC0 );
	return cpsr;
}

/* Restore I and F bits in Program Status Register  */
static void restore_interrupts( register uint32_t cpsr )
{
	write_CPSR( (cpsr & 0xC0) | (read_CPSR() & ~0xC0) );
}

/*
 * NB! FX3 firmware (at least versions 1.2.2 and 1.2.3 and 1.3.1) clears hardware
 * Error Counter Register periodically in timer interrupt, specifically in
 * CyU3PUibLnkErrClrTimerCb function.
 * Also CyU3PUsbGetErrorCounts implementation itself clears the same hardware
 * register, causing this way concurrency between hardware and software updating
 * the same register. As a result, if both HW and SW update the register
 * concurrently, one of the operations has no effect on content of register.
 *
 * Function below is free of CyU3PUsbGetErrorCounts flaw. Unfortunately its
 * not possible to avoid hardware register clearing in timer interrupt, but
 * assuming that MyU3PUsbGetErrorCounts function is called frequently enough,
 * hopefully still a better accuracy can be achieved.
 *
 * Note 1. MyU3PUsbGetErrorCounts function is not multi-thread safe (as also
 *       CyU3PUsbGetErrorCounts is not).
 */
static CyU3PReturnStatus_t MyU3PUsbGetErrorCounts(uint16_t *phy_err_cnt, uint16_t *lnk_err_cnt)
{
	// This function does not update PHY/LINK errors register.
	// Instead, it preserves the previous register value and
	// calculates the changes based on previous and current counter
	// values.
	static uint32_t current_value=0; //NB! static variable preserves its value between function calls
	register uint32_t previous_value;
	register uint16_t delta;

	if (!phy_err_cnt || !lnk_err_cnt)
	{
		return CY_U3P_ERROR_BAD_ARGUMENT;
	}

	// Take the previous register value
	previous_value=current_value;
	// Read current PHY/LINK errors register value (address 0xe0033000+20)
    // bits 16...31 - PHY  errors
    // bits  0...15 - LINK errors
	{
		// CPU and USB state machine are in separate clock domains
		// and therefore CPU can see any arbitrary value when
		// USB state machine updates the register at the same moment.
		// Let's assume that if three read operations return
        // equal values then it must be correct value - it's
        // very very unlikely that it's not.
		register uint32_t r[3];
		r[0]=*(volatile uint32_t *)(0xe0033000+20);
		r[1]=*(volatile uint32_t *)(0xe0033000+20);
		r[2]=*(volatile uint32_t *)(0xe0033000+20);
		if (r[0]==r[1] && r[0]==r[2])
		{
			current_value=r[0];
		}
		else
		{
			// Three values were not equal.
			// Let's try to read again but at maximal speed without any possible pauses
			// between read operations. The assumption is that USB automata does not
			// increment error counters very frequently and therefore we should see at
			// least two identical values.
			register uint32_t m=disable_interrupts();
			r[0]=*(volatile uint32_t *)(0xe0033000+20);
			r[1]=*(volatile uint32_t *)(0xe0033000+20);
			r[2]=*(volatile uint32_t *)(0xe0033000+20);
			restore_interrupts(m);
			// Update current value depending on what the two values were identical.
			// If there are no identical values then let's assume that
			// "current value" is equal to "previous value" this time - hopefully next
			// time we will have better luck.
			if (r[0]==r[1]) current_value=r[0];
			else if (r[1]==r[2]) current_value=r[1];
		}
	}
	if (CY_U3P_SUPER_SPEED!=CyU3PUsbGetSpeed())
	{
		return CY_U3P_ERROR_INVALID_SEQUENCE;
	}

	// NB! According Cypress technical support information, hardware error counters saturate
    // at value 0xFFFF (they never turn around to 0). Therefore, FX3 API needs to clear
	// counters itself periodically (it does this typically in timer interrupt).

	// NB! FX3 API version 1.3.1 clears LNK error counter but does not clear
	//     PHY error counter in timer interrupt. Therefore we need to clear counters
	//     ourselves if they have saturated. But as this is dangerous operation (USB
	//     state machine may also read any arbitrary value while CPU updates register,
	//     plus, we will lose errors that have appeared between our read and this write)
	//     then do this only when this is needed indeed.
	if (((current_value & 0xFFFF0000) == 0xFFFF0000) || ((current_value & 0x0000FFFF) == 0x0000FFFF))
	{
		*(volatile uint32_t *)(0xe0033000+20)=0;
	}

	// Calculate PHY errors increment
	delta=(uint16_t)(current_value>>16);
	if ((uint16_t)(previous_value>>16) <= delta)
	{
		delta -= (uint16_t)(previous_value>>16);
	}
	*phy_err_cnt=delta;

	// Calculate LINK errors increment
	delta=(uint16_t)(current_value);
	if ((uint16_t)previous_value <= delta)
	{
		delta -= (uint16_t)previous_value;
	}
	*lnk_err_cnt=delta;

	return CY_U3P_SUCCESS;
}

static unsigned int errff = 0;
static unsigned int ctrlCounter = 0;
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

	if( bRequest == 0x05) {
		return CyTrue;
	} else if ( bRequest == CMD_GET_VERSION ) {

		FirmwareDescription_t fw_desc;
		fw_desc.version = PROJECT_VERSION;
		CyU3PUsbSendEP0Data( (uint16_t)sizeof( FirmwareDescription_t ), (uint8_t*)&fw_desc);
		return CyTrue;

	} else if ( bRequest == CMD_START ) {

		CyU3PMemSet ((uint8_t *)&glEp0Buffer[0], 0, sizeof (glEp0Buffer));
		StartGPIF();
		CyU3PUsbSendEP0Data (wLength, glEp0Buffer);
		return CyTrue;

	} else if ( bRequest == CMD_READ_GPIO ) {

		CyU3PMemSet ((uint8_t *)&glEp0Buffer[0], 0, sizeof (glEp0Buffer));
		unsigned int* Ep0Buffer = (unsigned int*)&glEp0Buffer[0];
		CyBool_t val = CyFalse;
		CyU3PReturnStatus_t apiRetStatus = CyU3PGpioGetValue( wIndex, &val );

		Ep0Buffer[0] = apiRetStatus;
		if ( val == CyTrue ) {
			Ep0Buffer[1] = 1;
		} else {
			Ep0Buffer[1] = 0;
		}
		CyU3PUsbSendEP0Data (wLength, glEp0Buffer);
		return CyTrue;

	} else if ( bRequest == CMD_WRITE_GPIO ) {

		CyU3PMemSet ((uint8_t *)&glEp0Buffer[0], 0, sizeof (glEp0Buffer));
		unsigned int* Ep0Buffer = (unsigned int*)&glEp0Buffer[0];
		CyU3PReturnStatus_t apiRetStatus = CyU3PGpioSetValue( wIndex, wValue );

		Ep0Buffer[0] = apiRetStatus;

		CyU3PUsbSendEP0Data (wLength, glEp0Buffer);

		return CyTrue;

	} else if (bRequest == CMD_REG_WRITE) {
		InitSPI(SPI_ST_CFG_WORK);
		CyU3PUsbGetEP0Data (wLength, glEp0Buffer, NULL);
		//SPI send
		CyU3PSpiSetSsnLine (CyFalse);
		CyU3PSpiTransmitWords(glEp0Buffer, 2);
		CyU3PSpiSetSsnLine (CyTrue);
		return CyTrue;

	} else if (bRequest == CMD_CYPRESS_RESET) {

		CyU3PUsbGetEP0Data( wLength, glEp0Buffer, NULL );
		state.need_reset = CyTrue;
		return CyTrue;

	} else if (bRequest == CMD_READ_DEBUG_INFO) {

		CyU3PMemSet ((uint8_t *)&glEp0Buffer[0], 0, sizeof (glEp0Buffer));
		unsigned int* Ep0Buffer = (unsigned int*)&glEp0Buffer[0];
		static unsigned int glPhyErrs = 0;
		static unsigned int glLnkErrs = 0;
		Ep0Buffer[0] = ctrlCounter;
		ctrlCounter++;
		Ep0Buffer[1] = errff;
		uint16_t phyerrs;
		uint16_t lnkerrs;
		//CyU3PUsbGetErrorCounts(&phyerrs, &lnkerrs);
		MyU3PUsbGetErrorCounts(&phyerrs, &lnkerrs);
		Ep0Buffer[2] = phyerrs;
		Ep0Buffer[3] = lnkerrs;
		Ep0Buffer[4] = *(volatile uint32_t *)(0xe0033000+20);
		glPhyErrs += phyerrs;
		Ep0Buffer[5] = glPhyErrs;
		glLnkErrs += lnkerrs;
		Ep0Buffer[6] = glLnkErrs;
		Ep0Buffer[7] = 0xDEADBEEF;
		CyU3PUsbSendEP0Data (wLength, glEp0Buffer);
		return CyTrue;

	} else if (bRequest == CMD_REG_READ) {
		InitSPI(SPI_ST_CFG_WORK);

		//CyU3PDmaBuffer_t buf_p;

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
		errff += 1;
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
		if (glIsApplnActive)
		{
			CyFxBulkSrcSinkApplnStop ();
		}
		CyFxBulkSrcSinkApplnStart ();
		break;

	case CY_U3P_USB_EVENT_RESET:
	case CY_U3P_USB_EVENT_DISCONNECT:
		if (glIsApplnActive)
		{
			CyFxBulkSrcSinkApplnStop ();
		}
		break;

	default:
		break;
	}
}

void StartGPIF(void)
{
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	/* Start the state machine. */
	apiRetStatus = CyU3PGpifSMStart (RESET, ALPHA_RESET);
	CheckError(apiRetStatus);

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
	CheckError(apiRetStatus);


	/* Load the GPIF configuration for Slave FIFO sync mode. */
	apiRetStatus = CyU3PGpifLoad (&CyFxGpifConfig);
	CheckError(apiRetStatus);


	CyU3PGpifRegisterCallback(CyFxBulkSrcSinkApplnGPIFEventCB);

	/**********************************************************************************************/
	/* Start the USB functionality. */
	apiRetStatus = CyU3PUsbStart();
	CheckError(apiRetStatus);


	/* The fast enumeration is the easiest way to setup a USB connection,
	 * where all enumeration phase is handled by the library. Only the
	 * class / vendor requests need to be handled by the application. */
	CyU3PUsbRegisterSetupCallback(CyFxBulkSrcSinkApplnUSBSetupCB, CyTrue);

	/* Setup the callback to handle the USB events. */
	CyU3PUsbRegisterEventCallback(CyFxBulkSrcSinkApplnUSBEventCB);

	/* Set the USB Enumeration descriptors */

	/* Super speed device descriptor. */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_DEVICE_DESCR, NULL, (uint8_t *)CyFxUSB30DeviceDscr);
	CheckError(apiRetStatus);

	/* High speed device descriptor. */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_DEVICE_DESCR, NULL, (uint8_t *)CyFxUSB20DeviceDscr);
	CheckError(apiRetStatus);

	/* BOS descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_BOS_DESCR, NULL, (uint8_t *)CyFxUSBBOSDscr);
	CheckError(apiRetStatus);

	/* Device qualifier descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_DEVQUAL_DESCR, NULL, (uint8_t *)CyFxUSBDeviceQualDscr);
	CheckError(apiRetStatus);

	/* Super speed configuration descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_CONFIG_DESCR, NULL, (uint8_t *)CyFxUSBSSConfigDscr);
	CheckError(apiRetStatus);

	/* High speed configuration descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_CONFIG_DESCR, NULL, (uint8_t *)CyFxUSBHSConfigDscr);
	CheckError(apiRetStatus);

	/* Full speed configuration descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_FS_CONFIG_DESCR, NULL, (uint8_t *)CyFxUSBFSConfigDscr);
	CheckError(apiRetStatus);


	/* String descriptor 0 */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
	CheckError(apiRetStatus);

	/* String descriptor 1 */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
	CheckError(apiRetStatus);

	/* String descriptor 2 */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 2, (uint8_t *)CyFxUSBProductDscr);
	CheckError(apiRetStatus);

	/* Connect the USB Pins with super speed operation enabled. */
	apiRetStatus = CyU3PConnectState(CyTrue, CyTrue);
	CheckError(apiRetStatus);

}

void InitSPI( SpiState_e wantState ) {

	if ( state.spi_state == wantState ) {
		return;
	}

    CyU3PSpiConfig_t spiConfig;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    if ( state.spi_state == SPI_ST_NOT_INITED ) {
    	apiRetStatus = CyU3PSpiInit();
    	CheckError(apiRetStatus);
    }

    CyU3PMemSet ((uint8_t *)&spiConfig, 0, sizeof(spiConfig));
    spiConfig.isLsbFirst = CyFalse;
    spiConfig.cpol       = CyFalse;
    spiConfig.cpha       = CyFalse;
    spiConfig.leadTime   = CY_U3P_SPI_SSN_LAG_LEAD_HALF_CLK;
    spiConfig.lagTime    = CY_U3P_SPI_SSN_LAG_LEAD_HALF_CLK;
    spiConfig.ssnCtrl    = CY_U3P_SPI_SSN_CTRL_HW_EACH_WORD;
    spiConfig.clock      = 10000000;
    spiConfig.wordLen    = 16;

    if ( wantState == SPI_ST_CFG_PRERUN ) {
    	spiConfig.ssnPol = CyTrue;

    } else if ( wantState == SPI_ST_CFG_WORK ) {
    	spiConfig.ssnPol = CyFalse;
    }

	state.spi_state = wantState;

    apiRetStatus = CyU3PSpiSetConfig (&spiConfig, NULL);
    CheckError(apiRetStatus);
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

	for (;;)
	{
		CyU3PThreadSleep(100);
		if ( state.need_reset == CyTrue ) {
			CyU3PThreadSleep(2500);
			CyU3PDeviceReset(CyFalse);
		}
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

	state.need_reset = CyFalse;
	state.spi_state = SPI_ST_NOT_INITED;

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
		InitSPI(SPI_ST_CFG_PRERUN);
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


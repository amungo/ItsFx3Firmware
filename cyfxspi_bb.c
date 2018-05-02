/*
 ## Cypress USB 3.0 Platform source file (cyfxspi_bb.c)
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
#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyfxslfifosync.h"
#include "cyu3usb.h"
#include "cyu3uart.h"
#include <cyu3gpio.h>
#include "cyfxspi_bb.h"

CyU3PReturnStatus_t CyU3PSpiReadAd9269(uint16_t addr, uint8_t *value_p /* 8 bit read data */) {

	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	uint8_t tmp_addr = 0;

	tmp_addr = (uint8_t) ((addr >> 8) & 0x1F);

	tmp_addr |= 0x80;
	apiRetStatus = CyU3PGpioSetValue(SPI_SS0, CyFalse);

	CyU3PSpiSendData(tmp_addr);

	tmp_addr = (uint8_t) ((addr) & 0xFF);
	CyU3PSpiSendData(tmp_addr);

	CyU3PSpiGetData(value_p);

	apiRetStatus = CyU3PGpioSetValue(SPI_SS0, CyTrue);

	return apiRetStatus;
}

CyU3PReturnStatus_t CyU3PSpiWriteAd9269(uint16_t addr, uint8_t value_p /* 8 bit write data */) {

	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	uint8_t tmp_addr = 0;

	tmp_addr = (uint8_t) ((addr >> 8) & 0x1F);

	tmp_addr &= 0x7F;
	apiRetStatus = CyU3PGpioSetValue(SPI_SS0, CyFalse);

	CyU3PSpiSendData(tmp_addr);

	tmp_addr = (uint8_t) ((addr) & 0xFF);
	CyU3PSpiSendData(tmp_addr);

	CyU3PSpiSendData(value_p);

	apiRetStatus = CyU3PGpioSetValue(SPI_SS0, CyTrue);

	return apiRetStatus;
}

CyU3PReturnStatus_t CyU3PSpiGetData(uint8_t *value_p /* 8 bit read data */) {

	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	uint8_t tmp_cnt = 0, tmp_value = 0;
	CyBool_t gpioValue = CyFalse;

	apiRetStatus = CyU3PGpioSetValue(SPI_CLK, CyTrue);

	for (tmp_cnt = 0; tmp_cnt < 8; tmp_cnt++) {

		tmp_value <<= 1;
		apiRetStatus = CyU3PGpioSetValue(SPI_CLK, CyFalse);

		apiRetStatus = CyU3PGpioGetValue(SPI_MISO, &gpioValue);

		if (gpioValue)
			tmp_value |= 0x01;
		else
			tmp_value &= 0xFE;

		apiRetStatus = CyU3PGpioSetValue(SPI_CLK, CyTrue);

	}
	*value_p = tmp_value;
	return apiRetStatus;
}

CyU3PReturnStatus_t CyU3PSpiSendData(uint8_t value /* 8 bit write data */) {

	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	uint8_t tmp_cnt = 0;

	apiRetStatus = CyU3PGpioSetValue(SPI_CLK, CyTrue);

	for (tmp_cnt = 0; tmp_cnt < 8; tmp_cnt++) {

		if ((value) & (0x80))
			apiRetStatus = CyU3PGpioSetValue(SPI_MOSI, CyTrue);
		else
			apiRetStatus = CyU3PGpioSetValue(SPI_MOSI, CyFalse);

		apiRetStatus = CyU3PGpioSetValue(SPI_CLK, CyFalse);
		apiRetStatus = CyU3PGpioSetValue(SPI_CLK, CyTrue);
		value <<= 1;
	}

	return apiRetStatus;
}
void CyFxGpioInit(void) {
	CyU3PIoMatrixConfig_t io_cfg;
		//CyU3PReturnStatus_t status = CY_U3P_SUCCESS;
		CyU3PGpioClock_t gpioClock;
		CyU3PGpioSimpleConfig_t gpioConfig;
		CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

#if 1
		/* Configure the IO matrix for the device. On the FX3 DVK board, the COM port
		 * is connected to the IO(53:56). This means that either DQ32 mode should be
		 * selected or lppMode should be set to UART_ONLY. Here we are choosing
		 * UART_ONLY configuration. */
		io_cfg.isDQ32Bit = CyFalse;//CyTrue;
		io_cfg.useUart   = CyTrue;
		io_cfg.useI2C    = CyFalse;
		io_cfg.useI2S    = CyFalse;
		io_cfg.useSpi    = CyTrue;//CyFalse;
		io_cfg.lppMode   = CY_U3P_IO_MATRIX_LPP_DEFAULT;
		//io_cfg.lppMode   = CY_U3P_IO_MATRIX_LPP_UART_ONLY;
		//io_cfg.lppMode   = CY_U3P_IO_MATRIX_LPP_NONE;
	    /* GPIOs 50 and 52 are enabled. */
	    io_cfg.gpioSimpleEn[0]  = 0x0F800000;
	    //io_cfg.gpioSimpleEn[1]  = 0x01F40000;
	    //io_cfg.gpioSimpleEn[1]  = 0x001c0000;//140000
	    io_cfg.gpioSimpleEn[1]  = 0x0000ffff;//(1<<(FPGA_DONE-32)) | (1<<(FPGA_PROG_B-32)) | (1<<(FPGA_INIT_B-32)) | (1<<(TEST_LED-32));
		/* No GPIOs are enabled. */
		//io_cfg.gpioSimpleEn[0]  = 0;
		//io_cfg.gpioSimpleEn[1]  = 0;
		io_cfg.gpioComplexEn[0] = 0;
		io_cfg.gpioComplexEn[1] = 0;
		apiRetStatus = CyU3PDeviceConfigureIOMatrix (&io_cfg);
		if (apiRetStatus != CY_U3P_SUCCESS)
		{
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PDeviceConfigureIOMatrix failed, error code = %d\n",
					apiRetStatus);
		}
	#endif

		/* Init the GPIO module */
		gpioClock.fastClkDiv = 2;
		gpioClock.slowClkDiv = 0;
		gpioClock.simpleDiv = CY_U3P_GPIO_SIMPLE_DIV_BY_2;
		gpioClock.clkSrc = CY_U3P_SYS_CLK;
		gpioClock.halfDiv = 0;

		apiRetStatus = CyU3PGpioInit(&gpioClock, NULL);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4, "CyU3PGpioInit failed, error code = %d\n",
					apiRetStatus);

		}
	#if 0
		/* Override GPIOs as these pins is associated with GPIF Control signal.
		 * The IO cannot be selected as GPIO by CyU3PDeviceConfigureIOMatrix call
		 * as it is part of the GPIF IOs. Override API call must be made with
		 * caution as this will change the functionality of the pin. If the IO
		 * line is used as part of GPIF and is connected to some external device,
		 * then the line will no longer behave as a GPIF IO.. Here CTL4 line is
		 * not used and so it is safe to override.  */
		/* Configure SPI CLK as OUTPUT */
		apiRetStatus = CyU3PDeviceGpioOverride(SPI_CLK, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_CLK CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		apiRetStatus = CyU3PDeviceGpioOverride(SPI_SS0, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_SS0 CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		apiRetStatus = CyU3PDeviceGpioOverride(SPI_MOSI, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MOSI CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		apiRetStatus = CyU3PDeviceGpioOverride(SPI_MISO, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
	#endif
		apiRetStatus = CyU3PDeviceGpioOverride(ANTFEED_EN, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		//TCXO_EN
		apiRetStatus = CyU3PDeviceGpioOverride(TCXO_EN, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		//NT1065_CS
		apiRetStatus = CyU3PDeviceGpioOverride(NT1065_CS, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		//RCV_EN
		apiRetStatus = CyU3PDeviceGpioOverride(RCV_EN, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		//
		apiRetStatus = CyU3PDeviceGpioOverride(TEST_LED, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		apiRetStatus = CyU3PDeviceGpioOverride(TEST_LED2, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		apiRetStatus = CyU3PDeviceGpioOverride(FPGA_INIT_B, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		apiRetStatus = CyU3PDeviceGpioOverride(FPGA_DONE, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		apiRetStatus = CyU3PDeviceGpioOverride(FPGA_PROG_B, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		//FPGA_CS
		apiRetStatus = CyU3PDeviceGpioOverride(FPGA_CS, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		apiRetStatus = CyU3PDeviceGpioOverride(FPGA_HOLDN, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		//NT1065_AOK
		apiRetStatus = CyU3PDeviceGpioOverride(NT1065_AOK, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		apiRetStatus = CyU3PDeviceGpioOverride(FCTRL_LATCH, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}
		apiRetStatus = CyU3PDeviceGpioOverride(FCTRL_CS, CyTrue);
		if (apiRetStatus != 0) {
			/* Error Handling */
			CyU3PDebugPrint(4,
					"SPI_MISO CyU3PDeviceGpioOverride failed, error code = %d\n",
					apiRetStatus);

		}

	#if 0//ATT control
		/* Configure  output line : CLK, MOSI, SS */
		gpioConfig.outValue = CyTrue;
		gpioConfig.driveLowEn = CyTrue;
		gpioConfig.driveHighEn = CyTrue;
		gpioConfig.inputEn = CyFalse;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		apiRetStatus = CyU3PGpioSetSimpleConfig(SPI_CLK, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(SPI_MOSI, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(SPI_SS0, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
	#endif
		/* Configure  input-output line : 52, 50 */
		gpioConfig.outValue = CyFalse;//CyTrue;
		gpioConfig.driveLowEn = CyFalse;
		gpioConfig.driveHighEn = CyFalse;
		gpioConfig.inputEn = CyTrue;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;//CY_U3P_GPIO_INTR_BOTH_EDGE;
		apiRetStatus = CyU3PGpioSetSimpleConfig(FPGA_INIT_B, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		//gpioConfig.outValue = CyFalse;
		gpioConfig.driveLowEn = CyFalse;
		gpioConfig.driveHighEn = CyFalse;
		gpioConfig.inputEn = CyTrue;
		//gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		apiRetStatus = CyU3PGpioSetSimpleConfig(FPGA_DONE, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(NT1065_AOK, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}

		gpioConfig.outValue = CyTrue;
		gpioConfig.driveLowEn = CyTrue;
		gpioConfig.driveHighEn = CyTrue;
		gpioConfig.inputEn = CyFalse;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		apiRetStatus = CyU3PGpioSetSimpleConfig(TEST_LED, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(TEST_LED2, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(FPGA_PROG_B, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(FPGA_CS, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(FPGA_HOLDN, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(23, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(24, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(25, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(26, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(27, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		//RCV_EN
		apiRetStatus = CyU3PGpioSetSimpleConfig(RCV_EN, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		//NT1065_CS
		apiRetStatus = CyU3PGpioSetSimpleConfig(NT1065_CS, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		//TCXO_EN
		apiRetStatus = CyU3PGpioSetSimpleConfig(TCXO_EN, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(ANTFEED_EN, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(FCTRL_LATCH, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
		apiRetStatus = CyU3PGpioSetSimpleConfig(FCTRL_CS, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}

		//for(;;)
		{
			CyU3PGpioSimpleSetValue (TEST_LED, 0);
			//---CyU3PGpioSimpleSetValue (TEST_LED, 1);
			CyU3PGpioSimpleSetValue (TEST_LED2, 0);
			CyU3PGpioSimpleSetValue (RCV_EN, 0);
			CyU3PGpioSimpleSetValue (TCXO_EN, 1);
			CyU3PThreadSleep(100);
			CyU3PGpioSimpleSetValue (RCV_EN, 1); //RCV_EN
			CyU3PGpioSimpleSetValue (FPGA_HOLDN, 1);
			CyU3PGpioSimpleSetValue (ANTFEED_EN, 1);
			CyU3PGpioSimpleSetValue (FCTRL_CS, 1);
			CyU3PGpioSimpleSetValue (FCTRL_LATCH, 1);
		}

	#if 0
		/* Configure  input line : MISO */
		gpioConfig.outValue = CyTrue;
		gpioConfig.driveLowEn = CyFalse;
		gpioConfig.driveHighEn = CyFalse;
		gpioConfig.inputEn = CyTrue;
		gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
		apiRetStatus = CyU3PGpioSetSimpleConfig(SPI_MISO, &gpioConfig);
		if (apiRetStatus != CY_U3P_SUCCESS) {
			/* Error handling */
			CyU3PDebugPrint(4,
					"CyU3PGpioSetSimpleConfig failed, error code = %d\n",
					apiRetStatus);
		}
	#endif
}

/* [ ] */


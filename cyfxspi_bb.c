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

void CheckError ( CyU3PReturnStatus_t apiRetStatus );

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
	CyU3PGpioClock_t gpioClock;
	CyU3PGpioSimpleConfig_t gpioConfig;
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

	/* Init the GPIO module */
	gpioClock.fastClkDiv = 2;
	gpioClock.slowClkDiv = 0;
	gpioClock.simpleDiv = CY_U3P_GPIO_SIMPLE_DIV_BY_2;
	gpioClock.clkSrc = CY_U3P_SYS_CLK;
	gpioClock.halfDiv = 0;

	apiRetStatus = CyU3PGpioInit(&gpioClock, NULL);
	CheckError(apiRetStatus);


	apiRetStatus = CyU3PDeviceGpioOverride(NT1065EN, CyTrue);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PDeviceGpioOverride(NT1065AOK, CyTrue);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PDeviceGpioOverride(VCTCXOEN, CyTrue);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PDeviceGpioOverride(ANTFEEDEN, CyTrue);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PDeviceGpioOverride(ANTLNAEN, CyTrue);
	CheckError(apiRetStatus);

	/* Configure  output line : CLK, MOSI, SS */
	gpioConfig.outValue    = CyTrue;
	gpioConfig.driveLowEn  = CyTrue;
	gpioConfig.driveHighEn = CyTrue;
	gpioConfig.inputEn     = CyFalse;
	gpioConfig.intrMode    = CY_U3P_GPIO_NO_INTR;


	apiRetStatus = CyU3PGpioSetSimpleConfig(NT1065EN,  &gpioConfig);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PGpioSetSimpleConfig(NT1065AOK, &gpioConfig);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PGpioSetSimpleConfig(VCTCXOEN,  &gpioConfig);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PGpioSetSimpleConfig(ANTFEEDEN, &gpioConfig);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PGpioSetSimpleConfig(ANTLNAEN,  &gpioConfig);
	CheckError(apiRetStatus);



	apiRetStatus = CyU3PGpioSetValue(ANTLNAEN,  CyFalse);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PGpioSetValue(VCTCXOEN,  CyTrue);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PGpioSetValue(NT1065EN,  CyTrue);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PGpioSetValue(NT1065AOK, CyFalse);
	CheckError(apiRetStatus);

	apiRetStatus = CyU3PGpioSetValue(ANTFEEDEN, CyFalse);
	CheckError(apiRetStatus);



}

/* [ ] */


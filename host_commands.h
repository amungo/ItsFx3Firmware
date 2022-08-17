#ifndef HOST_COMMANDS_H_
#define HOST_COMMANDS_H_

#define CMD_GET_VERSION     ( 0xB0 )
#define CMD_INIT_PROJECT    ( 0xB1 )
#define CMD_READ_DEBUG_INFO ( 0xB4 )
#define CMD_CYPRESS_RESET   ( 0xB3 )

#define CMD_REG_WRITE8		( 0xD6 )
#define CMD_REG_READ8		( 0xD9 )
#define CMD_ECP5_RESET		( 0xD0 )
#define CMD_ECP5_SWITCHOFF	( 0xDA )
#define CMD_ECP5_WRITE		( 0xD1 )
#define CMD_ECP5_READ		( 0xD5 )
#define CMD_ECP5_CSON		( 0xD3 )
#define CMD_ECP5_CSOFF		( 0xD4 )
#define CMD_ECP5_CHECK		( 0xD2 )
#define CMD_ECP5_SET_DAC	( 0xD8 )
#define CMD_DEVICE_START	( 0xBA )
#define CMD_DEVICE_STOP		( 0xBB )
#define CMD_NT1065_RESET	( 0xD7 )

#define CMD_ADXL_WRITE		( 0xDB )
#define CMD_ADXL_READ		( 0xDE )

#define CMD_SET_SPI_CLOCK	( 0xB5 )

typedef struct FirmwareDescription_t {
	uint32_t version;
	uint8_t  reserved[ 28 ];
} FirmwareDescription_t;


#endif /* HOST_COMMANDS_H_ */

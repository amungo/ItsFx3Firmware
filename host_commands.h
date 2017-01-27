#ifndef HOST_COMMANDS_H_
#define HOST_COMMANDS_H_

#define CMD_GET_VERSION     ( 0xB0 )
#define CMD_INIT_PROJECT    ( 0xB1 )
#define CMD_REG_WRITE       ( 0xB3 )
#define CMD_READ_DEBUG_INFO ( 0xB4 )
#define CMD_REG_READ        ( 0xB5 )
#define CMD_CYPRESS_RESET   ( 0xBF )

typedef struct FirmwareDescription_t {
	uint32_t version;
	uint8_t  reserved[ 28 ];
} FirmwareDescription_t;


#endif /* HOST_COMMANDS_H_ */

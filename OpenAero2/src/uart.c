//***********************************************************
//* uart.c
//***********************************************************

//***********************************************************
//* Includes
//***********************************************************

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include <stdlib.h>
#include <util/delay.h>
#include "..\inc\io_cfg.h"

//************************************************************
// Prototypes
//************************************************************

void init_uart(void);

//************************************************************
// Code
// Baud = 20000000 / (16 * (UBRRn + 1))	Where U2X0 = 0
// Baud = 20000000 / (8 * (UBRRn + 1)) 	Where U2X0 = 1
//************************************************************

// Work out best divisor for baudrate generator
#define USART_BAUDRATE_XTREME 250000
#define BAUD_PRESCALE_XTREME ((F_CPU + USART_BAUDRATE_XTREME * 8L) / (USART_BAUDRATE_XTREME * 16L) - 1) // Default RX rate for Xtreme

#define USART_BAUDRATE_SBUS 100000
#define BAUD_PRESCALE_SBUS ((F_CPU + USART_BAUDRATE_SBUS * 4L) / (USART_BAUDRATE_SBUS * 8L) - 1) // Default RX rate for S-Bus

#define USART_BAUDRATE_SPEKTRUM 115200
#define BAUD_PRESCALE_SPEKTRUM ((F_CPU + USART_BAUDRATE_SPEKTRUM * 8L) / (USART_BAUDRATE_SPEKTRUM * 16L) - 1) // Default RX rate for Spektrum

// Initialise UART with adjusted bitrate
void init_uart(void)
{
	// Common init
	UCSR0B |= 	(1 << RXEN0);							// Enable receiver

	switch (Config.RxMode)
	{
		// Xtreme 8N1 (8 data bits / No parity / 1 stop bit / 250Kbps)
		case XTREME: 	
			UCSR0A &= ~(1 << U2X0);						// Clear the 2x flag
			UBRR0H  = (BAUD_PRESCALE_XTREME >> 8); 		// Actual = 250000, Error = 0%
			UBRR0L  =  BAUD_PRESCALE_XTREME & 0xff;		// 0x04 
			UCSR0C &= ~(1 << USBS0); 					// 1 stop bit
			UCSR0C &= ~(1 << UPM00) | 					// No parity 
					   (1 << UPM01); 
			break;

		// Futaba S-Bus 8E2 (8 data bits / Even parity / 2 stop bits / 100Kbps)
		case SBUS: 		
			UCSR0A |=  (1 << U2X0);						// Need to set the 2x flag
			UBRR0H  = (BAUD_PRESCALE_SBUS >> 8);  		// Actual = 100000 , Error = 0%	
			UBRR0L  =  BAUD_PRESCALE_SBUS & 0xff;		// 0x18 (24)
			UCSR0C |=  (1 << USBS0); 					// 2 stop bits
			UCSR0C &= ~(1 << UPM00); 					// Even parity 
			UCSR0C |=  (1 << UPM01); 
			break;

		// Spektrum 8N1 (8 data bits / No parity / 1 stop bit / 115.2Kbps)
		case SPEKTRUM: 	
			UCSR0A &=  ~(1 << U2X0);					// Clear the 2x flag
			UBRR0H  =  (BAUD_PRESCALE_SPEKTRUM >> 8); 	// Actual = 113636, Error = -1.36%
			UBRR0L  =   BAUD_PRESCALE_SPEKTRUM & 0xff;	// 0x0A (10.35)	
			UCSR0C &=  ~(1 << USBS0); 					// 1 stop bit
			UCSR0C &=  ~(1 << UPM00) | 					// No parity 
						(1 << UPM01); 
			break;

		case CPPM_MODE:
		case PWM1:
		case PWM2:
		case PWM3:
		default:
			UCSR0B &= 	~(1 << RXEN0);					// Disable receiver
			break;
	}

}

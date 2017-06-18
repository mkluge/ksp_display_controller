#include "Arduino.h"
#include "ArduinoJson.h"
#include "Wire.h"
#include "../../ksp_main_controller/src/ConsoleSetup.h"
#include "../../ksp_main_controller/src/ksp_display_defines.h"
#include <SPI.h>
#include "Ucglib.h"

Ucglib_ST7735_18x128x160_SWSPI lcd_right( 1, 2, 3, 0, 4);
Ucglib_ST7735_18x128x160_SWSPI lcd_left( 9, 10, 11, 8, 12);

#define READ_BUFFER_SIZE 200
char read_buffer[READ_BUFFER_SIZE];
volatile int read_buffer_offset = 0;
int empty_buffer_size = 0;
volatile bool have_handshake=false;
volatile bool command_complete=false;

void receiveEvent(int how_many);
void reset_input_buffer();

void setupLCD( Ucglib_ST7735_18x128x160_SWSPI &lcd) {
	  lcd.begin(UCG_FONT_MODE_SOLID);
	  lcd.setRotate180();
	  lcd.setColor(0, 255, 255, 255);
	  lcd.setColor(1, 0, 0, 0);
	  lcd.clearScreen();
}

void setup() {
	setupLCD( lcd_right );
	setupLCD( lcd_left );

	reset_input_buffer();
	Wire.onReceive(receiveEvent);
	Wire.begin(SLAVE_HW_ADDRESS);
	delay(100);
}

void print_lcd( Ucglib_ST7735_18x128x160_SWSPI &lcd, int line, const char *str) {
	  lcd.setFont(ucg_font_9x15_mf);
	  lcd.setPrintPos(1,20+line*18);
	  char buf[20]="                   ";
	  snprintf( buf, 15, "%13s", str);
	  buf[14]=0;
	  lcd.print(buf);
}

void print_lcd( Ucglib_ST7735_18x128x160_SWSPI &lcd, int line,
		        int col, const char c) {
	  lcd.setFont(ucg_font_helvR10_hr);
	  lcd.setPrintPos(1+10*col,20+line*15);
	  lcd.print(c);
}

void print_lcd( Ucglib_ST7735_18x128x160_SWSPI &lcd, int line, int number) {
	char buf[20];
	sprintf( buf, "%d", number);
	print_lcd( lcd, line, buf);
}

void reset_input_buffer() {
	memset(read_buffer, 0, READ_BUFFER_SIZE);
	read_buffer_offset = 0;
}

void dieError(int number) {
	print_lcd( lcd_left, 0, "Error:");
	print_lcd( lcd_left, 1, number);
}

// read the data into the buffer,
// if the current input buffer is not full
void receiveEvent(int how_many) {
	while( Wire.available()>0 )
	{
		char inByte = Wire.read();
		if ( inByte == '\n' )
		{
			command_complete = true;
			// dump if there is more one the wire
			return;
		}
		// otherwise store the current byte
		if (read_buffer_offset < READ_BUFFER_SIZE) {
			read_buffer[read_buffer_offset] = inByte;
			read_buffer_offset++;
		} else {
			read_buffer[READ_BUFFER_SIZE-1]=0;
			dieError(4);
			command_complete = true;
			return;
		}
	}
}

signed int check_for_key( JsonArray &data, short key)
{
	for( unsigned int index=0; index<data.size(); index+=2)
	{
		if( data[index]==key )
		{
			return index;
		}
	}
	return KEY_NOT_FOUND;
}

#define CHECK_DATA(KEY,LCD,LINE) \
		if ( (index=check_for_key( data, KEY))!=KEY_NOT_FOUND ) { \
			print_lcd( LCD, LINE, (const char *) data[index+1]); }

void work_on_command(JsonObject& rj) {
	if (!rj.success()) {
		print_lcd( lcd_left, 4, read_buffer);
		dieError(3);
	} else {
		if(!have_handshake)
		{
			if (rj["start"] == 2016) {
				have_handshake = true;
				print_lcd( lcd_left, 0, "Surf. Height");
				print_lcd( lcd_left, 2, "Surf. Time");
				print_lcd( lcd_right, 0, "Apoapsis");
				print_lcd( lcd_right, 2, "time to AP");
				print_lcd( lcd_right, 4, "Periapsis");
				print_lcd( lcd_right, 6, "time to PE");
				return;
			}
		}
		else
		{
			JsonArray& data=rj["data"];
			signed index;
			CHECK_DATA( INFO_APOAPSIS, lcd_right, 1);
			CHECK_DATA( INFO_APOAPSIS_TIME, lcd_right, 3);
			CHECK_DATA( INFO_PERIAPSIS, lcd_right, 5);
			CHECK_DATA( INFO_APOAPSIS_TIME, lcd_right, 7);
			CHECK_DATA( INFO_SURFACE_HEIGHT, lcd_left, 1);
			CHECK_DATA( INFO_SURFACE_TIME, lcd_left, 3);
		}
	}
}

void loop() {
	static bool dot_on=false;
	static int completed_commands=0;

	while( 1 )
	{
		if( command_complete )
		{
			char mybuf[READ_BUFFER_SIZE];
			memcpy( mybuf, read_buffer, READ_BUFFER_SIZE);
			reset_input_buffer();
			StaticJsonBuffer<READ_BUFFER_SIZE> sjb;
			JsonObject& rj = sjb.parseObject(mybuf);
			completed_commands++;
			work_on_command(rj);
			command_complete = false;
		}
		if( !have_handshake )
		{
			if (dot_on == true) {
				print_lcd( lcd_left, 2, "   .");
				print_lcd( lcd_right, 2, "    ");
			} else {
				print_lcd( lcd_right, 2, "   .");
				print_lcd( lcd_left, 2, "    ");
			}
			dot_on = !dot_on;
			delay(100);
		}
	}
}

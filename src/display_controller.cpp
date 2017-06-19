#include "Arduino.h"
#include "ArduinoJson.h"
#include "Wire.h"
#include "../../ksp_main_controller/src/ConsoleSetup.h"
#include "../../ksp_main_controller/src/ksp_display_defines.h"
#include <SPI.h>
#include "Ucglib.h"

#define DisplayType Ucglib_ST7735_18x128x160_SWSPI

#define CHECK_DATA(KEY,LCD,LINE) \
		if ( (index=check_for_key( data, KEY))!=KEY_NOT_FOUND ) { \
			print_lcd( LCD, LINE, (const char *) data[index+1]); }

#define CHECK_BAR(KEY,LCD,INDEX) \
					if ( (index=check_for_key( data, KEY))!=KEY_NOT_FOUND ) { \
						setBarPercentage( LCD, INDEX, atoi(data[index+1])); }

DisplayType lcd_right( 1, 2, 3, 0, 4);
DisplayType lcd_left( 9, 10, 11, 8, 12);

#define READ_BUFFER_SIZE 200
char read_buffer[READ_BUFFER_SIZE];
volatile int read_buffer_offset = 0;
int empty_buffer_size = 0;
volatile bool have_handshake=false;
volatile bool command_complete=false;

#define BAR_FIRST_VERTICAL_OFFSET 5
#define BAR_INNER_WIDTH 100
#define BAR_INNER_HEIGHT 10
#define BAR_BORDER_DISTANCE 4
#define BAR_BORDER_RADIUS 2
#define BAR_LEFT_OFFSET 5
#define BAR_VERTICAL_DISTANCE 15
#define BAR_TEXTV_ADD 10
#define BAR_BORDER_HEIGHT (BAR_INNER_HEIGHT+2*BAR_BORDER_DISTANCE)
#define BAR_TOTAL_HEIGHT (BAR_VERTICAL_DISTANCE+BAR_BORDER_HEIGHT)
#define BAR_TOTAL_WIDTH (BAR_INNER_WIDTH+2*BAR_BORDER_DISTANCE)

void receiveEvent(int how_many);
void reset_input_buffer();
signed int check_for_key( JsonArray &data, short key);

void print_lcd( DisplayType &lcd, int line, const char *str);
void print_lcd( DisplayType &lcd, int line, int col, const char c);
void print_lcd( DisplayType &lcd, int line, int number);

void prepareFuelBar( DisplayType &lcd);
void updateFuelBar(DisplayType &lcd, JsonArray& data);

void prepareLanding( DisplayType &lcd);
void updateLanding(DisplayType &lcd, JsonArray& data);

void setBarPercentage( DisplayType &lcd, int number, float percentage);

typedef struct LCDMode {
	void (*initFunc)(DisplayType &lcd);
	void (*updateFunc)(DisplayType &lcd, JsonArray& data);
	LCDMode *next;
} LCDModeS;

LCDModeS fuel = { prepareFuelBar, updateFuelBar, NULL };
LCDModeS landing = { prepareLanding, updateLanding, &fuel };
LCDModeS *current_lcd_mode = &fuel;

void setupLCD( DisplayType &lcd) {
	  lcd.begin(UCG_FONT_MODE_TRANSPARENT);
	  lcd.setRotate180();
	  lcd.setColor(0, 255, 255, 255);
	  lcd.setColor(1, 0, 0, 0);
		lcd.setFont(ucg_font_9x15_mf);
	  lcd.clearScreen();
}

void initBar( DisplayType &lcd, int number, const char *text)
{
	int border_t = BAR_FIRST_VERTICAL_OFFSET+number*BAR_TOTAL_HEIGHT;
  int border_l = BAR_LEFT_OFFSET;

	lcd.drawRFrame( border_l, border_t, BAR_TOTAL_WIDTH, BAR_TOTAL_HEIGHT, BAR_BORDER_RADIUS);
	lcd.setPrintPos( border_l+BAR_BORDER_DISTANCE, border_t+BAR_TOTAL_HEIGHT+BAR_TEXTV_ADD);
	lcd.print(text);
}

void prepareLanding( DisplayType &lcd)
{
	lcd.clearScreen();
	print_lcd( lcd, 0, "Surf. Height");
	print_lcd( lcd, 2, "Surf. Time");
}

void updateLanding(DisplayType &lcd, JsonArray& data)
{
	signed index = KEY_NOT_FOUND;
	CHECK_DATA( INFO_SURFACE_HEIGHT, lcd, 1);
	CHECK_DATA( INFO_SURFACE_TIME, lcd, 3);
}

void prepareFuelBar( DisplayType &lcd)
{
	lcd.clearScreen();
	initBar( lcd, 0, "Liquid Fuel");
	initBar( lcd, 1, "Oxygen");
	initBar( lcd, 2, "RCS Fuel");
	initBar( lcd, 3, "Battery");
}

void updateFuelBar(DisplayType &lcd, JsonArray& data)
{
	signed index=KEY_NOT_FOUND;
	CHECK_BAR( INFO_PERCENTAGE_FUEL, lcd, 0 );
	CHECK_BAR( INFO_PERCENTAGE_OXYGEN, lcd, 1 );
	CHECK_BAR( INFO_PERCENTAGE_RCS, lcd, 2 );
	CHECK_BAR( INFO_PERCENTAGE_BATTERY, lcd, 3 );
}

void setBarPercentage( DisplayType &lcd, int number, float percentage)
{
	int border_t = BAR_FIRST_VERTICAL_OFFSET+number*BAR_TOTAL_HEIGHT;
  int border_l = BAR_LEFT_OFFSET;
	int bar_width = (int)(BAR_INNER_WIDTH*percentage/100.0);

	// need to draw the color box as well as the black box
	float other_brightness = percentage*255.0/100.0;
	lcd.setColor( 255, other_brightness, (int) other_brightness);
	lcd.drawBox( border_l+BAR_BORDER_DISTANCE,
		           border_t+BAR_BORDER_DISTANCE,
		           bar_width,
							 BAR_INNER_HEIGHT);
	// need to draw the color box as well as the black box
	lcd.setColor( 0, 0, 0);
	lcd.drawBox( border_l+BAR_BORDER_DISTANCE+bar_width,
		 	         border_t+BAR_BORDER_DISTANCE,
						 	 BAR_INNER_WIDTH-bar_width,
							 BAR_INNER_HEIGHT);
}

void setup() {

	fuel.next = &landing;

	setupLCD( lcd_right );
	setupLCD( lcd_left );

	current_lcd_mode->initFunc( lcd_left );

	reset_input_buffer();
	Wire.onReceive(receiveEvent);
	Wire.begin(SLAVE_HW_ADDRESS);
	delay(100);
}

void print_lcd( DisplayType &lcd, int line, const char *str) {
//	  lcd.setFont(ucg_font_9x15_mf);
	  lcd.setPrintPos(1,20+line*18);
	  char buf[20]="                   ";
	  snprintf( buf, 15, "%13s", str);
	  buf[14]=0;
	  lcd.print(buf);
}

void print_lcd( DisplayType &lcd, int line,
		        int col, const char c) {
//	  lcd.setFont(ucg_font_helvR10_hr);
	  lcd.setPrintPos(1+10*col,20+line*15);
	  lcd.print(c);
}

void print_lcd( DisplayType &lcd, int line, int number) {
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

void work_on_command(JsonObject& rj) {
	if (!rj.success()) {
		print_lcd( lcd_left, 4, read_buffer);
		dieError(3);
	} else {
		if(!have_handshake)
		{
			if (rj["start"] == 2016) {
				have_handshake = true;
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
			if( (index=check_for_key( data, BUTTON_NEXT_LEFT_LCD_MODE))!=KEY_NOT_FOUND )
			{
				current_lcd_mode = current_lcd_mode->next;
				current_lcd_mode->initFunc( lcd_left );
			}
			current_lcd_mode->updateFunc( lcd_left, data);
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

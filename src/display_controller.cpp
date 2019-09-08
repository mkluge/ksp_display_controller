#include "Arduino.h"
#include <ArduinoJson.h>
#include "Wire.h"
#include "../../ksp_main_controller/src/ConsoleSetup.h"
#include "../../ksp_main_controller/src/ksp_display_defines.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>

// Screen dimensions
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 128 // Change this to 96 for 1.27" OLED.

// You can use any (4 or) 5 pins
#define SCLK_PINr 3
#define MOSI_PINr 2
#define DC_PINr   5
#define CS_PINr   4
#define RST_PINr  6

#define SCLK_PINl 9
#define MOSI_PINl 8
#define DC_PINl   11
#define CS_PINl   10
#define RST_PINl  12

// Color definitions
#define BLACK           0x0000
#define BLUE            0x001F
#define RED             0xF800
#define GREEN           0x07E0
#define CYAN            0x07FF
#define MAGENTA         0xF81F
#define YELLOW          0xFFE0
#define WHITE           0xFFFF

#define DisplayType Adafruit_SSD1351

#define CHECK_DATA(KEY,TFT,LINE) \
		if ( (index=check_for_key( data, KEY))!=KEY_NOT_FOUND ) { \
			print_tft( TFT, LINE, (const char *) data[index+1]); }

#define CHECK_BAR(KEY,TFT,INDEX) \
					if ( (index=check_for_key( data, KEY))!=KEY_NOT_FOUND ) { \
						setBarPercentage( TFT, INDEX, atoi(data[index+1])); }

//DisplayType tft_left( 9, 10, 11, 8, 12);
// Option 1: use any pins but a little slower
Adafruit_SSD1351 tft_right = Adafruit_SSD1351(SCREEN_WIDTH, SCREEN_HEIGHT, CS_PINr, DC_PINr, MOSI_PINr, SCLK_PINr, RST_PINr);
Adafruit_SSD1351 tft_left = Adafruit_SSD1351(SCREEN_WIDTH, SCREEN_HEIGHT, CS_PINl, DC_PINl, MOSI_PINl, SCLK_PINl, RST_PINl);


#define dprint(X)	print_tft( tft_right, 7, X)
#define cprint(X)	print_tft( tft_right, 5, X)

char read_buffer[DISPLAY_WIRE_BUFFER_SIZE];
volatile int read_buffer_offset = 0;
int empty_buffer_size = 0;
volatile bool command_complete=false;
byte ready_to_receive = 0;

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
signed int check_for_key( const JsonArray &data, short key);

void print_tft( DisplayType &tft, int line, const char *str);
void print_tft( DisplayType &tft, int line, int col, const char c);
void print_tft( DisplayType &tft, int line, int number);

void prepareFlightInfo( DisplayType &tft);
void updateFlightInfo( DisplayType &tft, const JsonArray& data);

void prepareFuelBar( DisplayType &tft);
void updateFuelBar( DisplayType &tft, const JsonArray& data);

void prepareLanding( DisplayType &tft);
void updateLanding( DisplayType &tft, const JsonArray& data);

void setBarPercentage( DisplayType &tft, int number, float percentage);

typedef struct TFTMode {
	void (*initFunc)(DisplayType &tft);
	void (*updateFunc)(DisplayType &tft, const JsonArray& data);
	TFTMode *next;
} TFTModeS;

TFTModeS flightInfo = { prepareFlightInfo, updateFlightInfo, NULL };
TFTModeS landing = { prepareLanding, updateLanding, &flightInfo };
TFTModeS *current_right_tft_mode = &flightInfo;

TFTModeS fuel = { prepareFuelBar, updateFuelBar, &fuel };
TFTModeS *current_left_tft_mode = &fuel;

void setupTFT( DisplayType &tft) {
	tft.begin();
	tft.setRotation(3);
	tft.fillScreen(BLACK);
	tft.setTextSize(2);

//	  tft.begin(UCG_FONT_MODE_SOLID);
//	  tft.setRotate180();
	tft.setTextColor(WHITE);

//	  tft.setColor(0, 255, 255, 255);
//	  tft.setColor(1, 0, 0, 0);
//		tft.setFont(ucg_font_9x15_mf);
//	  tft.clearScreen();
}

void initBar( DisplayType &tft, int number, const char *text)
{
	int border_t = BAR_FIRST_VERTICAL_OFFSET+number*BAR_TOTAL_HEIGHT;
  int border_l = BAR_LEFT_OFFSET;

	tft.drawRoundRect( border_l, border_t, BAR_TOTAL_WIDTH, BAR_TOTAL_HEIGHT, BAR_BORDER_RADIUS, WHITE);
	tft.setCursor( border_l+BAR_BORDER_DISTANCE, border_t+BAR_TOTAL_HEIGHT+BAR_TEXTV_ADD);
	tft.print(text);
}

void prepareFlightInfo( DisplayType &tft)
{
	tft.fillScreen(BLACK);
	print_tft( tft, 0, "Apoapsis");
  print_tft( tft, 2, "time to AP");
  print_tft( tft, 4, "Periapsis");
  print_tft( tft, 6, "time to PE");
}

void updateFlightInfo(DisplayType &tft, const JsonArray& data)
{
	signed index = KEY_NOT_FOUND;
  CHECK_DATA( INFO_APOAPSIS, tft, 1);
  CHECK_DATA( INFO_APOAPSIS_TIME, tft, 3);
  CHECK_DATA( INFO_PERIAPSIS, tft, 5);
  CHECK_DATA( INFO_APOAPSIS_TIME, tft, 7);
}

void prepareLanding( DisplayType &tft)
{
	tft.fillScreen(BLACK);
	print_tft( tft, 0, "Surf. Height");
	print_tft( tft, 2, "Surf. Time");
}

void updateLanding(DisplayType &tft, const JsonArray& data)
{
	signed index = KEY_NOT_FOUND;
	CHECK_DATA( INFO_SURFACE_HEIGHT, tft, 1);
	CHECK_DATA( INFO_SURFACE_TIME, tft, 3);
}

void prepareFuelBar( DisplayType &tft)
{
	tft.fillScreen(BLACK);
	initBar( tft, 0, "Liquid Fuel");
	initBar( tft, 1, "Oxygen");
	initBar( tft, 2, "RCS Fuel");
	initBar( tft, 3, "Battery");
}

void updateFuelBar(DisplayType &tft, const JsonArray& data)
{
	signed index=KEY_NOT_FOUND;
	CHECK_BAR( INFO_PERCENTAGE_FUEL, tft, 0 );
	CHECK_BAR( INFO_PERCENTAGE_OXYGEN, tft, 1 );
	CHECK_BAR( INFO_PERCENTAGE_RCS, tft, 2 );
	CHECK_BAR( INFO_PERCENTAGE_BATTERY, tft, 3 );
}

void setBarPercentage( DisplayType &tft, int number, float percentage)
{
	int border_t = BAR_FIRST_VERTICAL_OFFSET+number*BAR_TOTAL_HEIGHT;
  int border_l = BAR_LEFT_OFFSET;
	int bar_width = (int)(BAR_INNER_WIDTH*percentage/100.0);

	// need to draw the color box as well as the black box
//	float other_brightness = percentage*255.0/100.0;
//	tft.setColor( 255, other_brightness, (int) other_brightness);
	tft.fillRect( border_l+BAR_BORDER_DISTANCE,
		           border_t+BAR_BORDER_DISTANCE,
		           bar_width,
							 BAR_INNER_HEIGHT, WHITE);
	tft.fillRect( border_l+BAR_BORDER_DISTANCE+bar_width,
		 	         border_t+BAR_BORDER_DISTANCE,
						 	 BAR_INNER_WIDTH-bar_width,
							 BAR_INNER_HEIGHT, BLACK);
}

void requestEvent() {
  Wire.write( ready_to_receive );
}

void setup() {

	flightInfo.next = &landing;
	setupTFT( tft_right );
	setupTFT( tft_left );

	reset_input_buffer();
	Wire.begin(DISPLAY_I2C_ADDRESS);
	Wire.onReceive(receiveEvent);
	ready_to_receive = 1;
}

void print_tft( DisplayType &tft, int line, const char *str) {
//	  tft.setFont(ucg_font_9x15_mf);
	  tft.setCursor(1,20+line*18);
	  char buf[20]="                   ";
	  snprintf( buf, 15, "%12s", str);
	  buf[14]=0;
	  tft.print(buf);
}

void print_tft( DisplayType &tft, int line,
		        int col, const char c) {
//	  tft.setFont(ucg_font_helvR10_hr);
	  tft.setCursor(1+10*col,20+line*15);
	  tft.print(c);
}

void print_tft( DisplayType &tft, int line, int number) {
	char buf[20];
	sprintf( buf, "%d", number);
	print_tft( tft, line, buf);
}

void reset_input_buffer() {
	memset(read_buffer, 0, DISPLAY_WIRE_BUFFER_SIZE);
	read_buffer_offset = 0;
	command_complete = false;
//	Wire.onRequest(requestEvent);
}

void dieError(int number) {
	print_tft( tft_right, 0, "Error:");
	print_tft( tft_right, 1, number);
}

// read the data into the buffer,
// if the current input buffer is not full
void receiveEvent(int how_many) {
	// as soon as we receive, we tell the master that we will
	// not accept additional messages until this one is done
	ready_to_receive = 0;
	if( command_complete == true )
		return;
	while( Wire.available()>0 )
	{
		dprint(11);
		char inByte = Wire.read();
		if ( inByte == '\n' )
		{
			dprint(9);
			command_complete = true;
			return;
		}
		// otherwise store the current byte
		if (read_buffer_offset < DISPLAY_WIRE_BUFFER_SIZE) {
			read_buffer[read_buffer_offset] = inByte;
			read_buffer_offset++;
		} else {
			read_buffer[DISPLAY_WIRE_BUFFER_SIZE-1]=0;
			dieError(4);
			command_complete = true;
			return;
		}
	}
}

signed int check_for_key( const JsonArray& data, short key)
{
	for( size_t index=0; index<data.size(); index+=2)
	{
		if( data[index]==key )
		{
			return index;
		}
	}
	return KEY_NOT_FOUND;
}

void work_on_command( StaticJsonDocument<DISPLAY_WIRE_BUFFER_SIZE>& rj) {
	if(rj.containsKey("chk") )
	{
		current_left_tft_mode->initFunc( tft_left );
		current_right_tft_mode->initFunc( tft_right );
		return;
	}
	if( rj.containsKey("data") )
	{
		const JsonArray& data=rj["data"];
		if( check_for_key( data, BUTTON_NEXT_LEFT_TFT_MODE)!=KEY_NOT_FOUND )
		{
			current_right_tft_mode = current_right_tft_mode->next;
			current_right_tft_mode->initFunc( tft_right );
		}
		current_right_tft_mode->updateFunc( tft_right, data);
		current_left_tft_mode->updateFunc( tft_left, data);
	}
}

void loop() {
	static bool dot_on=false;
	static int completed_commands=0;
	static int loop=80;
	static int idle_loops=0;

	while( 1 )
	{
		dprint(idle_loops++);
		cprint(read_buffer_offset);
		if( command_complete )
		{
			idle_loops=0;
			StaticJsonDocument<DISPLAY_WIRE_BUFFER_SIZE> rj;
			DeserializationError error = deserializeJson( rj, read_buffer);
			if (error) {
				dieError(2);
			} else {
				completed_commands++;
				work_on_command(rj);
			}
			reset_input_buffer();
			ready_to_receive = 1;
		}
/*		if( !have_handshake )
		{
			if (dot_on == true) {
				print_tft( tft_right, 3, "    ");
				print_tft( tft_right, 2, "   .");
			} else {
				print_tft( tft_right, 2, "    ");
				print_tft( tft_right, 3, "   .");
			}
			dot_on = !dot_on;
			delay(100);
		} */
	}
}

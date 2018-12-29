#include "Arduino.h"
#include <ArduinoJson.h>
#include "Wire.h"
#include "../../ksp_main_controller/src/ConsoleSetup.h"
#include "../../ksp_main_controller/src/ksp_display_defines.h"
#include <SPI.h>
#include <Ucglib.h>

#define DisplayType Ucglib_ST7735_18x128x160_SWSPI

#define CHECK_DATA(KEY,LCD,LINE) \
		if ( (index=check_for_key( data, KEY))!=KEY_NOT_FOUND ) { \
			print_lcd( LCD, LINE, (const char *) data[index+1]); }

#define CHECK_BAR(KEY,LCD,INDEX) \
					if ( (index=check_for_key( data, KEY))!=KEY_NOT_FOUND ) { \
						setBarPercentage( LCD, INDEX, atoi(data[index+1])); }

//#define TEST

DisplayType lcd_right( 1, 2, 3, 0, 4);
//DisplayType lcd_left( 9, 10, 11, 8, 12);

#define dprint(X)	print_lcd( lcd_right, 7, X)
#define cprint(X)	print_lcd( lcd_right, 5, X)
#define ucg lcd_right

#ifndef TEST

char read_buffer[DISPLAY_WIRE_BUFFER_SIZE];
volatile int read_buffer_offset = 0;
int empty_buffer_size = 0;
volatile bool have_handshake=false;
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
signed int check_for_key( JsonArray &data, short key);

void print_lcd( DisplayType &lcd, int line, const char *str);
void print_lcd( DisplayType &lcd, int line, int col, const char c);
void print_lcd( DisplayType &lcd, int line, int number);

void prepareFlightInfo( DisplayType &lcd);
void updateFlightInfo( DisplayType &lcd, JsonArray& data);

void prepareFuelBar( DisplayType &lcd);
void updateFuelBar( DisplayType &lcd, JsonArray& data);

void prepareLanding( DisplayType &lcd);
void updateLanding( DisplayType &lcd, JsonArray& data);

void setBarPercentage( DisplayType &lcd, int number, float percentage);

typedef struct LCDMode {
	void (*initFunc)(DisplayType &lcd);
	void (*updateFunc)(DisplayType &lcd, JsonArray& data);
	LCDMode *next;
} LCDModeS;

LCDModeS flightInfo = { prepareFlightInfo, updateFlightInfo, NULL };
LCDModeS fuel = { prepareFuelBar, updateFuelBar, &flightInfo };
LCDModeS landing = { prepareLanding, updateLanding, &fuel };
LCDModeS *current_lcd_mode = &flightInfo;

void setupLCD( DisplayType &lcd) {
	  lcd.begin(UCG_FONT_MODE_SOLID);
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

void prepareFlightInfo( DisplayType &lcd)
{
	lcd.clearScreen();
	print_lcd( lcd, 0, "Apoapsis");
  print_lcd( lcd, 2, "time to AP");
  print_lcd( lcd, 4, "Periapsis");
  print_lcd( lcd, 6, "time to PE");
}

void updateFlightInfo(DisplayType &lcd, JsonArray& data)
{
	signed index = KEY_NOT_FOUND;
  CHECK_DATA( INFO_APOAPSIS, lcd, 1);
  CHECK_DATA( INFO_APOAPSIS_TIME, lcd, 3);
  CHECK_DATA( INFO_PERIAPSIS, lcd, 5);
  CHECK_DATA( INFO_APOAPSIS_TIME, lcd, 7);
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

#ifndef TEST

void requestEvent() {
  Wire.write( ready_to_receive );
}

void setup() {

	flightInfo.next = &landing;
	setupLCD( lcd_right );
//	setupLCD( lcd_left );

	reset_input_buffer();
	Wire.onReceive(receiveEvent);
	Wire.onRequest(requestEvent);
	Wire.begin(DISPLAY_I2C_ADDRESS);
	ready_to_receive = 1;
}
#endif

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
	memset(read_buffer, 0, DISPLAY_WIRE_BUFFER_SIZE);
	read_buffer_offset = 0;
	command_complete = false;
}

void dieError(int number) {
	print_lcd( lcd_right, 0, "Error:");
	print_lcd( lcd_right, 1, number);
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
			have_handshake = true;
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
	if(rj.containsKey("chk") )
	{
		current_lcd_mode->initFunc( lcd_right );
		return;
	}
	if( rj.containsKey("data") )
	{
		JsonArray& data=rj["data"];
		signed index;
		if( (index=check_for_key( data, BUTTON_NEXT_LEFT_LCD_MODE))!=KEY_NOT_FOUND )
		{
			current_lcd_mode = current_lcd_mode->next;
			current_lcd_mode->initFunc( lcd_right );
		}
		current_lcd_mode->updateFunc( lcd_right, data);
	}
}

void loop() {
	static bool dot_on=false;
	static int completed_commands=0;
	static int loop=80;
	static int idle_loops=0;

	dprint(0);
	while( 1 )
	{
		cprint(read_buffer_offset);
		if( loop==100 )
			loop=80;
		if( command_complete )
		{
			idle_loops=0;
			dprint(1);
			StaticJsonBuffer<DISPLAY_WIRE_BUFFER_SIZE> sjb;
			JsonObject& rj = sjb.parseObject(read_buffer);
			completed_commands++;
			if (!rj.success()) {
				dieError(3);
			} else {
				work_on_command(rj);
			}
			reset_input_buffer();
			ready_to_receive = 1;
		}
		if( !have_handshake )
		{
			if (dot_on == true) {
				print_lcd( lcd_right, 3, "    ");
				print_lcd( lcd_right, 2, "   .");
			} else {
				print_lcd( lcd_right, 2, "    ");
				print_lcd( lcd_right, 3, "   .");
			}
			dot_on = !dot_on;
			delay(100);
		}
	}
}
#endif

/********
 *
 * Test Code for library
 *
 */
#ifdef TEST

#define T 4000
#define DLY() delay(2000)

/*
  Linear Congruential Generator (LCG)
  z = (a*z + c) % m;
  m = 256 (8 Bit)

  for period:
  a-1: dividable by 2
  a-1: multiple of 4
  c: not dividable by 2

  c = 17
  a-1 = 64 --> a = 65
*/
uint8_t z = 127;	// start value
uint32_t lcg_rnd(void) {
  z = (uint8_t)((uint16_t)65*(uint16_t)z + (uint16_t)17);
  return (uint32_t)z;
}

void ucglib_graphics_test(void)
{
  //ucg.setMaxClipRange();
  ucg.setColor(0, 0, 40, 80);
  ucg.setColor(1, 80, 0, 40);
  ucg.setColor(2, 255, 0, 255);
  ucg.setColor(3, 0, 255, 255);

  ucg.drawGradientBox(0, 0, ucg.getWidth(), ucg.getHeight());

  ucg.setColor(255, 168, 0);
  ucg.setPrintDir(0);
  ucg.setPrintPos(2,18);
  ucg.print("Ucglib");
  ucg.setPrintPos(2,18+20);
  ucg.print("GraphicsTest");

  DLY();
}

void gradient(void)
{
  ucg.setColor(0, 0, 255, 0);
  ucg.setColor(1, 255, 0, 0);
  ucg.setColor(2, 255, 0, 255);
  ucg.setColor(3, 0, 255, 255);

  ucg.drawGradientBox(0, 0, ucg.getWidth(), ucg.getHeight());

  ucg.setColor(255, 255, 255);
  ucg.setPrintPos(2,18);
  ucg.setPrintDir(0);
  ucg.print("GradientBox");

  ucg.setColor(0, 0, 255, 0);
  ucg.drawBox(2, 25, 8, 8);

  ucg.setColor(0, 255, 0, 0);
  ucg.drawBox(2+10, 25, 8, 8);

  ucg.setColor(0, 255, 0, 255);
  ucg.drawBox(2, 25+10, 8, 8);

  ucg.setColor(0, 0, 255, 255);
  ucg.drawBox(2+10, 25+10, 8, 8);

  DLY();
}

void box(void)
{
  ucg_int_t x, y, w, h;
  unsigned long m;

  ucg.setColor(0, 0, 40, 80);
  ucg.setColor(1, 60, 0, 40);
  ucg.setColor(2, 128, 0, 140);
  ucg.setColor(3, 0, 128, 140);
  ucg.drawGradientBox(0, 0, ucg.getWidth(), ucg.getHeight());

  ucg.setColor(255, 255, 255);
  ucg.setPrintPos(2,18);
  ucg.setPrintDir(0);
  ucg.print("Box");

  m = millis() + T;

  while( millis() < m )
  {
    ucg.setColor((lcg_rnd()&127)+127, (lcg_rnd()&127)+64, lcg_rnd() & 31);
    w = lcg_rnd() & 31;
    h = lcg_rnd() & 31;
    w += 10;
    h += 10;
    x = (lcg_rnd()*(ucg.getWidth()-w))>>8;
    y = (lcg_rnd()*(ucg.getHeight()-h-20))>>8;

    ucg.drawBox(x, y+20, w, h);
  }

}

void pixel_and_lines(void)
{
  ucg_int_t mx;
  ucg_int_t x, xx;
  mx = ucg.getWidth() / 2;
  //my = ucg.getHeight() / 2;

  ucg.setColor(0, 0, 0, 150);
  ucg.setColor(1, 0, 60, 40);
  ucg.setColor(2, 60, 0, 40);
  ucg.setColor(3, 120, 120, 200);
  ucg.drawGradientBox(0, 0, ucg.getWidth(), ucg.getHeight());

  ucg.setColor(255, 255, 255);
  ucg.setPrintPos(2,18);
  ucg.setPrintDir(0);
  ucg.print("Pix&Line");

  ucg.drawPixel(0, 0);
  ucg.drawPixel(1, 0);
  //ucg.drawPixel(ucg.getWidth()-1, 0);
  //ucg.drawPixel(0, ucg.getHeight()-1);

  ucg.drawPixel(ucg.getWidth()-1, ucg.getHeight()-1);
  ucg.drawPixel(ucg.getWidth()-1-1, ucg.getHeight()-1);


  for( x = 0; x  < mx; x++ )
  {
    xx = (((uint16_t)x)*255)/mx;
    ucg.setColor(255, 255-xx/2, 255-xx);
    ucg.drawPixel(x, 24);
    ucg.drawVLine(x+7, 26, 13);
  }

  DLY();
}

void color_test(void)
{
  ucg_int_t mx;
  uint16_t c, x;
  mx = ucg.getWidth() / 2;
  //my = ucg.getHeight() / 2;

  ucg.setColor(0, 0, 0, 0);
  ucg.drawBox(0, 0, ucg.getWidth(), ucg.getHeight());

  ucg.setColor(255, 255, 255);
  ucg.setPrintPos(2,18);
  ucg.setPrintDir(0);
  ucg.print("Color Test");

  ucg.setColor(0, 127, 127, 127);
  ucg.drawBox(0, 20, 16*4+4, 5*8+4);

  for( c = 0, x = 2; c <= 255; c+=17, x+=4 )
  {
    ucg.setColor(0, c, c, c);
    ucg.drawBox(x, 22, 4, 8);
    ucg.setColor(0, c, 0, 0);
    ucg.drawBox(x, 22+8, 4, 8);
    ucg.setColor(0, 0, c, 0);
    ucg.drawBox(x, 22+2*8, 4, 8);
    ucg.setColor(0, 0, 0, c);
    ucg.drawBox(x, 22+3*8, 4, 8);
    ucg.setColor(0, c, 255-c, 0);
    ucg.drawBox(x, 22+4*8, 4, 8);

  }

  DLY();
}



void cross(void)
{
  ucg_int_t mx, my;
  ucg.setColor(0, 250, 0, 0);
  ucg.setColor(1, 255, 255, 30);
  ucg.setColor(2, 220, 235, 10);
  ucg.setColor(3, 205, 0, 30);
  ucg.drawGradientBox(0, 0, ucg.getWidth(), ucg.getHeight());
  mx = ucg.getWidth() / 2;
  my = ucg.getHeight() / 2;

  ucg.setColor(0, 255, 255, 255);
  ucg.setPrintPos(2,18);
  ucg.print("Cross");

  ucg.setColor(0, 0, 0x66, 0xcc);
  ucg.setPrintPos(mx+15,my-5);
  ucg.print("dir0");
  ucg.setPrintPos(mx+5,my+26);
  ucg.print("dir1");
  ucg.setPrintPos(mx-40,my+20);
  ucg.print("dir2");
  ucg.setPrintPos(mx+5,my-25);
  ucg.print("dir3");

  ucg.setColor(0, 0, 0x66, 0xff);
  ucg.setColor(1, 0, 0x66, 0xcc);
  ucg.setColor(2, 0, 0, 0x99);

  ucg_Draw90Line(ucg.getUcg(), mx+2, my-1, 20, 0, 0);
  ucg_Draw90Line(ucg.getUcg(), mx+2, my, 20, 0, 1);
  ucg_Draw90Line(ucg.getUcg(), mx+2, my+1, 20, 0, 2);

  ucg_Draw90Line(ucg.getUcg(), mx+1, my+2, 20, 1, 0);
  ucg_Draw90Line(ucg.getUcg(), mx, my+2, 20, 1, 1);
  ucg_Draw90Line(ucg.getUcg(), mx-1, my+2, 20, 1, 2);

  ucg_Draw90Line(ucg.getUcg(), mx-2, my+1, 20, 2, 0);
  ucg_Draw90Line(ucg.getUcg(), mx-2, my, 20, 2, 1);
  ucg_Draw90Line(ucg.getUcg(), mx-2, my-1, 20, 2, 2);

  ucg_Draw90Line(ucg.getUcg(), mx-1, my-2, 20, 3, 0);
  ucg_Draw90Line(ucg.getUcg(), mx, my-2, 20, 3, 1);
  ucg_Draw90Line(ucg.getUcg(), mx+1, my-2, 20, 3, 2);

  DLY();
}

void triangle(void)
{
  unsigned long m;

  ucg.setColor(0, 0, 80, 20);
  ucg.setColor(1, 60, 80, 20);
  ucg.setColor(2, 60, 120, 0);
  ucg.setColor(3, 0, 140, 30);
  ucg.drawGradientBox(0, 0, ucg.getWidth(), ucg.getHeight());

  ucg.setColor(255, 255, 255);
  ucg.setPrintPos(2,18);
  ucg.print("Triangle");

  m = millis() + T;

  while( millis() < m )
  {
    ucg.setColor((lcg_rnd()&127)+127, lcg_rnd() & 31, (lcg_rnd()&127)+64);

    ucg.drawTriangle(
      (lcg_rnd()*(ucg.getWidth()))>>8,
      ((lcg_rnd()*(ucg.getHeight()-20))>>8)+20,
      (lcg_rnd()*(ucg.getWidth()))>>8,
      ((lcg_rnd()*(ucg.getHeight()-20))>>8)+20,
      (lcg_rnd()*(ucg.getWidth()))>>8,
      ((lcg_rnd()*(ucg.getHeight()-20))>>8)+20
    );

  }

}

void text(void)
{
  ucg_int_t x, y, w, h, i;
  unsigned long m;

  ucg.setColor(0, 80, 40, 0);
  ucg.setColor(1, 60, 0, 40);
  ucg.setColor(2, 20, 0, 20);
  ucg.setColor(3, 60, 0, 0);
  ucg.drawGradientBox(0, 0, ucg.getWidth(), ucg.getHeight());

  ucg.setColor(255, 255, 255);
  ucg.setPrintPos(2,18);
  ucg.setPrintDir(0);
  ucg.print("Text");

  m = millis() + T;
  i = 0;
  while( millis() < m )
  {
    ucg.setColor(lcg_rnd() & 31, (lcg_rnd()&127)+127, (lcg_rnd()&127)+64);
    w = 40;
    h = 22;
    x = (lcg_rnd()*(ucg.getWidth()-w))>>8;
    y = (lcg_rnd()*(ucg.getHeight()-h))>>8;

    ucg.setPrintPos(x,y+h);
    ucg.setPrintDir((i>>2)&3);
    i++;
    ucg.print("Ucglib");
  }
  ucg.setPrintDir(0);

}

void fonts(void)
{
  ucg_int_t d = 5;
  ucg.setColor(0, 0, 40, 80);
  ucg.setColor(1, 150, 0, 200);
  ucg.setColor(2, 60, 0, 40);
  ucg.setColor(3, 0, 160, 160);

  ucg.drawGradientBox(0, 0, ucg.getWidth(), ucg.getHeight());

  ucg.setColor(255, 255, 255);
  ucg.setPrintDir(0);
  ucg.setPrintPos(2,18);
  ucg.print("Fonts");

  ucg.setFontMode(UCG_FONT_MODE_TRANSPARENT);

  ucg.setColor(255, 200, 170);
  ucg.setFont(ucg_font_helvB08_hr);
  ucg.setPrintPos(2,30+d);
  ucg.print("ABC abc 123");
  ucg.setFont(ucg_font_helvB10_hr);
  ucg.setPrintPos(2,45+d);
  ucg.print("ABC abc 123");
  ucg.setFont(ucg_font_helvB12_hr);
  //ucg.setPrintPos(2,62+d);
  //ucg.print("ABC abc 123");
  ucg.drawString(2,62+d, 0, "ABC abc 123"); // test drawString

  ucg.setFontMode(UCG_FONT_MODE_SOLID);

  ucg.setColor(255, 200, 170);
  ucg.setColor(1, 0, 100, 120);		// background color in solid mode
  ucg.setFont(ucg_font_helvB08_hr);
  ucg.setPrintPos(2,75+30+d);
  ucg.print("ABC abc 123");
  ucg.setFont(ucg_font_helvB10_hr);
  ucg.setPrintPos(2,75+45+d);
  ucg.print("ABC abc 123");
  ucg.setFont(ucg_font_helvB12_hr);
  ucg.setPrintPos(2,75+62+d);
  ucg.print("ABC abc 123");

  ucg.setFontMode(UCG_FONT_MODE_TRANSPARENT);

  /* big fonts removed, some trouble with the Arduino IDE */
  /*
  ucg.setFont(ucg_font_helvB14_hr);
  ucg.setPrintPos(2,79+d);
  ucg.print("ABC abc 123");
  ucg.setFont(ucg_font_helvB18_hr);
  ucg.setPrintPos(2,79+22+d);
  ucg.print("ABC abc 123");
  */

  ucg.setFont(ucg_font_ncenR14_hr);
  DLY();
}

void clip(void)
{
  ucg.setColor(0, 0x00, 0xd1, 0x5e);		// dark green
  ucg.setColor(1, 0xff, 0xf7, 0x61);		// yellow
  ucg.setColor(2, 0xd1, 0xc7, 0x00);			// dark yellow
  ucg.setColor(3, 0x61, 0xff, 0xa8);		// green

  ucg.drawGradientBox(0, 0, ucg.getWidth(), ucg.getHeight());

  ucg.setColor(255, 255, 255);
  ucg.setPrintPos(2,18);
  ucg.setPrintDir(0);
  ucg.print("ClipRange");

  ucg.setColor(0xd1, 0x00, 0x073);

  ucg.setFont(ucg_font_helvB18_hr);

  ucg.setPrintPos(25,45);
  ucg.setPrintDir(0);
  ucg.print("Ucg");
  ucg.setPrintDir(1);
  ucg.print("Ucg");
  ucg.setPrintDir(2);
  ucg.print("Ucg");
  ucg.setPrintDir(3);
  ucg.print("Ucg");


  ucg.setMaxClipRange();
  ucg.setColor(0xff, 0xff, 0xff);
  ucg.drawFrame(20-1,30-1,15+2,20+2);
  ucg.setClipRange(20, 30, 15, 20);
  ucg.setColor(0xff, 0x61, 0x0b8);
  ucg.setPrintPos(25,45);
  ucg.setPrintDir(0);
  ucg.print("Ucg");
  ucg.setPrintDir(1);
  ucg.print("Ucg");
  ucg.setPrintDir(2);
  ucg.print("Ucg");
  ucg.setPrintDir(3);
  ucg.print("Ucg");


  ucg.setMaxClipRange();
  ucg.setColor(0xff, 0xff, 0xff);
  ucg.drawFrame(60-1,35-1,25+2,18+2);
  ucg.setClipRange(60, 35, 25, 18);
  ucg.setColor(0xff, 0x61, 0x0b8);
  ucg.setPrintPos(25,45);
  ucg.setPrintDir(0);
  ucg.print("Ucg");
  ucg.setPrintDir(1);
  ucg.print("Ucg");
  ucg.setPrintDir(2);
  ucg.print("Ucg");
  ucg.setPrintDir(3);
  ucg.print("Ucg");

  ucg.setMaxClipRange();
  ucg.setColor(0xff, 0xff, 0xff);
  ucg.drawFrame(7-1,58-1,90+2,4+2);
  ucg.setClipRange(7, 58, 90, 4);
  ucg.setColor(0xff, 0x61, 0x0b8);
  ucg.setPrintPos(25,45);
  ucg.setPrintDir(0);
  ucg.print("Ucg");
  ucg.setPrintDir(1);
  ucg.print("Ucg");
  ucg.setPrintDir(2);
  ucg.print("Ucg");
  ucg.setPrintDir(3);
  ucg.print("Ucg");

  ucg.setFont(ucg_font_ncenR14_hr);
  ucg.setMaxClipRange();
  DLY();

}

void setup(void)
{
  delay(1000);
//	lcd_left.clearScreen();
	lcd_right.clearScreen();
  ucg.begin(UCG_FONT_MODE_TRANSPARENT);
  ucg.setFont(ucg_font_ncenR14_hr);
  ucg.clearScreen();
}

void set_clip_range(void)
{
  ucg_int_t x, y, w, h;
  w = lcg_rnd() & 31;
  h = lcg_rnd() & 31;
  w += 25;
  h += 25;
  x = (lcg_rnd()*(ucg.getWidth()-w))>>8;
  y = (lcg_rnd()*(ucg.getHeight()-h))>>8;

  ucg.setClipRange(x, y, w, h);
}


uint8_t r = 0;
void loop(void)
{
  switch(r&3)
  {
    case 0: ucg.undoRotate(); break;
    case 1: ucg.setRotate90(); break;
    case 2: ucg.setRotate180(); break;
    default: ucg.setRotate270(); break;
  }

  if ( r > 3 )
  {
    ucg.clearScreen();
    set_clip_range();
  }

  r++;
  ucglib_graphics_test();
  cross();
  pixel_and_lines();
  color_test();
  triangle();
  fonts();
  text();
  if ( r <= 3 )
    clip();
  box();
  gradient();
  //ucg.clearScreen();
  DLY();
  ucg.setMaxClipRange();
}
#endif

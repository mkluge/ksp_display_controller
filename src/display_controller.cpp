#undef WIRE_TEST

#ifdef WIRE_TEST
#define TEST
#endif

#include "Arduino.h"
#include <ArduinoJson.h>
#include "Wire.h"
#include "../../ksp_main_controller/src/ConsoleSetup.h"
#include "../../ksp_main_controller/src/ksp_display_defines.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
//#include <Fonts/FreeMono9pt7b.h>
//#include "UbuntuMono_Regular7pt7b.h"
//#include "mikefont.h"

// Screen dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128

// Display Pins
#define SCLK_PINr 3
#define MOSI_PINr 2
#define DC_PINr 5
#define CS_PINr 4
#define RST_PINr 6

#define SCLK_PINl 9
#define MOSI_PINl 8
#define DC_PINl 11
#define CS_PINl 10
#define RST_PINl 12

// Color definitions
#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define WHITE 0xFFFF

// definitions for text layout
#define LEFT_OFFSET 1
#define CHAR_WIDTH 10
#define FIRST_LINE_OFFSET 10
#define LINE_HEIGHT 15

// definitions for the fuel bars
#define BAR_LEFT_OFFSET 0			// offset for the bars from the left
#define BAR_FIRST_VERTICAL_OFFSET 0 // offset of the first bar from the top
#define BAR_INNER_WIDTH 100			// width of the inner bar
#define BAR_INNER_HEIGHT 10			// height of the inner bar
#define BAR_BORDER_DISTANCE 2		// the distance between the inner bar and the outer rect
#define BAR_BORDER_RADIUS 2			// the radius for the round rects
#define BAR_TEXTV_ADD 10			// the reserved height below the bar for the text
#define BAR_TEXTV_OFFSET 7			// where to set the cursor below the bar for printing
#define BAR_BORDER_HEIGHT (BAR_INNER_HEIGHT + 2 * BAR_BORDER_DISTANCE)
#define BAR_TOTAL_HEIGHT (BAR_TEXTV_ADD + BAR_BORDER_HEIGHT)
#define BAR_TOTAL_WIDTH (BAR_INNER_WIDTH + 2 * BAR_BORDER_DISTANCE)

// the type of display attached (the C++ class name)
#define DisplayType Adafruit_SSD1351

#define CHECK_DATA(KEY, TFT, LINE)                           \
	if ((index = check_for_key(data, KEY)) != KEY_NOT_FOUND) \
	{                                                        \
		print_tft(TFT, LINE, (const char *)data[index + 1]); \
	}

#define CHECK_BAR(KEY, TFT, INDEX)                           \
	if ((index = check_for_key(data, KEY)) != KEY_NOT_FOUND) \
	{                                                        \
		setBarPercentage(TFT, INDEX, atoi(data[index + 1])); \
	}

namespace {

#undef NO_RIGHT_DISPLAY
#undef NO_LEFT_DISPLAY
Adafruit_SSD1351 tft_right = Adafruit_SSD1351(SCREEN_WIDTH, SCREEN_HEIGHT, CS_PINr, DC_PINr, MOSI_PINr, SCLK_PINr, RST_PINr);
Adafruit_SSD1351 tft_left = Adafruit_SSD1351(SCREEN_WIDTH, SCREEN_HEIGHT, CS_PINl, DC_PINl, MOSI_PINl, SCLK_PINl, RST_PINl);

#define dprint(X) print_tft(tft_right, 7, X)
#define cprint(X) print_tft(tft_right, 5, X)

char read_buffer[DISPLAY_WIRE_BUFFER_SIZE];
bool command_complete = false;
byte slave_ready = 1;

void receiveEvent(int how_many);
signed int check_for_key(const JsonArray &data, short key);

void print_tft(DisplayType &tft, int line, const char *str);
void print_tft(DisplayType &tft, int line, int col, const char c);
void print_tft(DisplayType &tft, int line, int number);

void prepareFlightInfo(DisplayType &tft);
void updateFlightInfo(DisplayType &tft, const JsonArray &data);

void prepareFuelBar(DisplayType &tft);
void updateFuelBar(DisplayType &tft, const JsonArray &data);

void prepareLanding(DisplayType &tft);
void updateLanding(DisplayType &tft, const JsonArray &data);

void setBarPercentage(DisplayType &tft, int number, float percentage);

typedef struct TFTMode
{
	void (*initFunc)(DisplayType &tft);
	void (*updateFunc)(DisplayType &tft, const JsonArray &data);
	TFTMode *next;
} TFTModeS;

TFTModeS flightInfo = {prepareFlightInfo, updateFlightInfo, NULL};
TFTModeS landing = {prepareLanding, updateLanding, &flightInfo};
TFTModeS *current_right_tft_mode = &flightInfo;

TFTModeS fuel = {prepareFuelBar, updateFuelBar, &fuel};
TFTModeS *current_left_tft_mode = &fuel;

word ConvertRGB( byte R, byte G, byte B)
{
  return ( ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3) );
}

void setupTFT(DisplayType &tft)
{
	tft.begin();
	tft.setRotation(3);
	tft.fillScreen(BLACK);
	tft.setTextSize(1);
	// using fonts would be great, but one cannot have a custom font and
	// the background erased -> so just stay with the current one
	//  tft.setFont(&FreeMono9pt7b);
	//	tft.setFont(&UbuntuMono_Regular7pt7b);
	tft.setTextColor(WHITE, BLACK);
}

void initBar(DisplayType &tft, int number, const char *text)
{
	int border_t = BAR_FIRST_VERTICAL_OFFSET + number * BAR_TOTAL_HEIGHT;
	int border_l = BAR_LEFT_OFFSET;

	tft.drawRoundRect(border_l, border_t, BAR_TOTAL_WIDTH, BAR_TOTAL_HEIGHT, BAR_BORDER_RADIUS, WHITE);
	tft.setCursor(border_l + BAR_BORDER_DISTANCE, border_t + BAR_TOTAL_HEIGHT + BAR_TEXTV_OFFSET);
	tft.print(text);
}

void prepareFlightInfo(DisplayType &tft)
{
	tft.fillScreen(BLACK);
	print_tft(tft, 0, "Apoapsis");
	print_tft(tft, 2, "time to AP");
	print_tft(tft, 4, "Periapsis");
	print_tft(tft, 6, "time to PE");
}

void updateFlightInfo(DisplayType &tft, const JsonArray &data)
{
	signed index = KEY_NOT_FOUND;
	CHECK_DATA(INFO_APOAPSIS, tft, 1);
	CHECK_DATA(INFO_APOAPSIS_TIME, tft, 3);
	CHECK_DATA(INFO_PERIAPSIS, tft, 5);
	CHECK_DATA(INFO_APOAPSIS_TIME, tft, 7);
}

void prepareLanding(DisplayType &tft)
{
	tft.fillScreen(BLACK);
	print_tft(tft, 0, "Surf. Height");
	print_tft(tft, 2, "Surf. Time");
}

void updateLanding(DisplayType &tft, const JsonArray &data)
{
	signed index = KEY_NOT_FOUND;
	CHECK_DATA(INFO_SURFACE_HEIGHT, tft, 1);
	CHECK_DATA(INFO_SURFACE_TIME, tft, 3);
}

void prepareFuelBar(DisplayType &tft)
{
	tft.fillScreen(BLACK);
	initBar(tft, 0, "Liquid Fuel");
	initBar(tft, 1, "Oxygen");
	initBar(tft, 2, "RCS Fuel");
	initBar(tft, 3, "Battery");
}

void updateFuelBar(DisplayType &tft, const JsonArray &data)
{
	signed index = KEY_NOT_FOUND;
	CHECK_BAR(INFO_PERCENTAGE_FUEL, tft, 0);
	CHECK_BAR(INFO_PERCENTAGE_OXYGEN, tft, 1);
	CHECK_BAR(INFO_PERCENTAGE_RCS, tft, 2);
	CHECK_BAR(INFO_PERCENTAGE_BATTERY, tft, 3);
}

void setBarPercentage(DisplayType &tft, int number, float percentage)
{
	int border_t = BAR_FIRST_VERTICAL_OFFSET + number * BAR_TOTAL_HEIGHT;
	int border_l = BAR_LEFT_OFFSET;
	int bar_width = (int)(BAR_INNER_WIDTH * percentage / 100.0);
	int color_offset = 255 * percentage;
	int color = ConvertRGB( 255, 255-color_offset, 255-color_offset);

	// need to draw the color box as well as the black box
	//	float other_brightness = percentage*255.0/100.0;
	//	tft.setColor( 255, other_brightness, (int) other_brightness);
	tft.fillRect(border_l + BAR_BORDER_DISTANCE,
				 border_t + BAR_BORDER_DISTANCE,
				 bar_width,
				 BAR_INNER_HEIGHT, color);
	tft.fillRect(border_l + BAR_BORDER_DISTANCE + bar_width,
				 border_t + BAR_BORDER_DISTANCE,
				 BAR_INNER_WIDTH - bar_width,
				 BAR_INNER_HEIGHT, BLACK);
}

void requestStatus()
{
	Wire.write(slave_ready);
}

void print_tft(DisplayType &tft, int line, const char *str)
{
	tft.setCursor(LEFT_OFFSET, FIRST_LINE_OFFSET + line * LINE_HEIGHT);
	char buf[20] = "                   ";
	snprintf(buf, 15, "%14s", str);
	buf[14] = 0;
	tft.print(buf);
}

void print_tft(DisplayType &tft, int line,
			   int col, const char c)
{
	//	  tft.setFont(ucg_font_helvR10_hr);
	tft.setCursor(LEFT_OFFSET + CHAR_WIDTH * col, FIRST_LINE_OFFSET + line * LINE_HEIGHT);
	tft.print(c);
}

void print_tft(DisplayType &tft, int line, int number)
{
	char buf[20];
	snprintf(buf, 15, "%d", number);
	print_tft(tft, line, buf);
}

void dieError(int number)
{
	print_tft(tft_right, 0, "Error:");
	print_tft(tft_right, 1, number);
}

void wire_read_until(char delimiter)
{
	static int read_buffer_offset = 0;
	int bytes_read = 0;
	static int line=1;
	static int col=0;
	// at first call, clear the buffer
	if( read_buffer_offset == 0 )
	{
		memset(read_buffer, 0, DISPLAY_WIRE_BUFFER_SIZE);
	}
	while (Wire.available() > 0)
	{
		slave_ready = 0;
		char inByte = Wire.read();
//		char buf[2]={ inByte, 0};
		if( line!=7 ) 
		{
//			print_tft( tft_left, line, col, inByte);
//			print_tft( tft_right, 4, 3);
			col++;
			if( col==11)
			{
				col=0;
				line++;
			}
		}
		if (inByte == delimiter)
		{
			read_buffer[read_buffer_offset] = 0;
			command_complete = true;
//			tft_left.fillScreen(BLACK);
			col=0;
			line=1;
			read_buffer_offset=0;
			// this should be the last byte on the line
			if(Wire.available())
				dieError(12);
			break;
		}
		// otherwise store the current byte
		if (read_buffer_offset < DISPLAY_WIRE_BUFFER_SIZE)
		{
			read_buffer[read_buffer_offset] = inByte;
			read_buffer_offset++;
			bytes_read++;
		}
		else
		{
			dieError(4);
		}
	}
}

#ifndef WIRE_TEST
// read the data into the buffer,
// if the current input buffer is not full
void receiveEvent(int how_many)
{
	// as soon as we receive, we tell the master that we will
	// not accept additional messages until this one is done
//	dprint("event");
	wire_read_until('\n');
}
#endif

signed int check_for_key(const JsonArray &data, short key)
{
	for (size_t index = 0; index < data.size(); index += 2)
	{
		if (data[index] == key)
		{
			return index;
		}
	}
	return KEY_NOT_FOUND;
}

void work_on_command(StaticJsonDocument<DISPLAY_WIRE_BUFFER_SIZE> &rj)
{
	if (rj.containsKey("chk"))
	{
#ifndef NO_LEFT_DISPLAY
		current_left_tft_mode->initFunc(tft_left);
#endif
		current_right_tft_mode->initFunc(tft_right);
		return;
	}
	else if (rj.containsKey("data"))
	{
		const JsonArray &data = rj["data"];
		if (check_for_key(data, BUTTON_NEXT_LEFT_TFT_MODE) != KEY_NOT_FOUND)
		{
			current_right_tft_mode = current_right_tft_mode->next;
			current_right_tft_mode->initFunc(tft_right);
		}
		current_right_tft_mode->updateFunc(tft_right, data);
#ifndef NO_LEFT_DISPLAY
		current_left_tft_mode->updateFunc(tft_left, data);
#endif
	}
	else
	{
		cprint("wrong command");
	}
	
}

}

#ifndef TEST

void setup()
{
	Wire.begin(DISPLAY_I2C_ADDRESS);
	Wire.onReceive(receiveEvent);
	Wire.onRequest(requestStatus);

	flightInfo.next = &landing;
	setupTFT(tft_right);
	setupTFT(tft_left);
	current_right_tft_mode->initFunc(tft_right);
	current_left_tft_mode->initFunc(tft_left);
}

void loop()
{
	static int completed_commands = 0;
#//		cprint(read_buffer_offset);
	if (command_complete)
	{
		StaticJsonDocument<DISPLAY_WIRE_BUFFER_SIZE> rj;
		DeserializationError error = deserializeJson(rj, read_buffer);
		if (error)
		{
			dieError(2);
		}
		else
		{
			completed_commands++;
			work_on_command(rj);
			slave_ready=1;
		}
	}
}

#endif

#ifdef WIRE_TEST

void testReceiveEvent(int how_many)
{
	wire_read_until('+');
}

void setup()
{
	Wire.begin(DISPLAY_I2C_ADDRESS);
	Wire.setTimeout(30000);
	Wire.onReceive(testReceiveEvent);
	Wire.onRequest(requestStatus);

	flightInfo.next = &landing;
	setupTFT(tft_right);
#ifndef NO_LEFT_DISPLAY
	setupTFT(tft_left);
#endif
}

void loop()
{
	// wait for data
	// if complete, print it
	static int idle_loops=0;
	static int completed=0;
	static int ack_ed=0;
	char s[10];
	sprintf(s, "%d", idle_loops++);
	print_tft( tft_right, 3, s);

	if (command_complete)
	{
		char mb[20];
		completed++;
		sprintf( mb, "OK: %d", completed);
		dprint(read_buffer);
		print_tft( tft_right, 0, read_buffer);
		print_tft( tft_right, 2, mb);
		idle_loops=0;
		command_complete = false;
		slave_ready=1;
	}
}

#endif
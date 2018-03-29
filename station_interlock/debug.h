// debug.h  contributed by Matt VK5ZM

#ifndef _DEBUG_h
#define _DEBUG_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif



class DebugClass
{
 protected:


 public:
	void init();

	void print(const char *str);
	void print(const __FlashStringHelper *str);
	void print(char ch);
	void print(int i);
	void print(float f);
	void print(float f, byte places);
	void print(unsigned int i);
	void print(long unsigned int i);
	void print(long i);
	void print(double i);
	
	void println(double i);
	void println(const char *str);
	void println (const __FlashStringHelper *str);

	void println(String str);
	
	void write(const char *str);
	void write(int i);
};


extern uint8_t debug_mode;
extern HardwareSerial * debug_serial_port;

#endif


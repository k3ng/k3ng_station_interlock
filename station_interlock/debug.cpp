// 
// contributed by Matt VK5ZM
// 

#include "debug.h"

void DebugClass::print(const char *str)
{
  if (debug_mode){
	debug_serial_port->print(str);
	//ethernetclient0.print(str);
  }

}

void DebugClass::print(const __FlashStringHelper *str)
{
	char c;
	if(!str) return;
	
	/* since str is a const we can't increment it, so do this instead */
	char *p = (char *)str;
	
	/* keep going until we find the null */
	while((c = pgm_read_byte(p++)))
	{
		 if (debug_mode)
		 {
			debug_serial_port->write(c);
			// ethernetclient0.write(c);
		 }

	}
}

void DebugClass::println(const __FlashStringHelper *str)
{
   DebugClass::print(str);
   DebugClass::print("\n\r");


}

void DebugClass::print(char ch)
{

	if (debug_mode){
		debug_serial_port->print(ch);
		// ethernetclient0.print(ch);
	}

}

void DebugClass::print(int i)
{

	if (debug_mode){
		debug_serial_port->print(i);
		// ethernetclient0.print(i);
	}

}

void DebugClass::print(unsigned int i)
{

	if (debug_mode){
		debug_serial_port->print(i);
		// ethernetclient0.print(i);
	}

}

void DebugClass::print(long unsigned int i)
{

	if (debug_mode){
		debug_serial_port->print(i);
		// ethernetclient0.print(i);
	}

}

void DebugClass::print(long i)
{

	if (debug_mode){
		debug_serial_port->print(i);
		// ethernetclient0.print(i);
	}

}

void DebugClass::print(double i)
{

	if (debug_mode){
		debug_serial_port->print(i);
		// ethernetclient0.print(i);
	}

}

void DebugClass::println(double i)
{

	if (debug_mode){
		debug_serial_port->println(i);
		// ethernetclient0.println(i);
	}

}

void DebugClass::print(float f,byte places)
{
	char tempstring[16] = "";

	dtostrf( f,0,places,tempstring);


	if (debug_mode){
		debug_serial_port->print(tempstring);
		// ethernetclient0.print(tempstring);
	}

}

void DebugClass::print(float f)
{
	char tempstring[16] = "";

	dtostrf( f,0,2,tempstring);


	if (debug_mode){
		debug_serial_port->print(tempstring);
		// ethernetclient0.print(tempstring);
	}

}

void DebugClass::println(const char *str)
{
	if (debug_mode){
		debug_serial_port->println(str);
		// ethernetclient0.println(str);
	}


}

void DebugClass::write(const char *str)
{

	if (debug_mode){
		debug_serial_port->write(str);
		// ethernetclient0.write(str);
	}


}

void DebugClass::write(int i)
{

	if (debug_mode){
		debug_serial_port->write(i);
		// ethernetclient0.write(i);
	}


}

void DebugClass::println(String str){

	if (debug_mode){
		debug_serial_port->println(str);
		// ethernetclient0.write(i);
	}

}
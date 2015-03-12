#include "Arduino.h"
#include <EEPROM.h>
#include "SODA.h"
#include <SdFat.h>
#ifndef NO_SD
SdFat card;
SdFile file;
#endif
int timeArray[6] = {2010,1,1,12,0,0};
char buffer[30];
int bufferIndex = 0;
boolean end_on_connect = false;
boolean single_file = false;

//////////////////////////////////////////////////////////////////////
//@I SHARED FUNCTIONS //////////////////////////////////////////////////-
////////////////////////////////////////////////////////////////////
//should be initiated at the beginning of the sketch
void SODA::begin(){
	Wire.begin();
	setAddress(CLOCK_I2C_ADDR, CLOCK_CONTROL_ADDR, CLOCK_SETUP);
	setStandby(0x00);
//allow SD functions to be removed by defining NO_SD
#ifndef NO_SD
//this is legacy code to control SD card.	
#endif
}
//@I set an address of an I2C device
void SODA::setAddress(uint8_t device, uint8_t addr, uint8_t val)
{
    Wire.beginTransmission(device);
    Wire.write(addr);
    Wire.write(val);
    Wire.endTransmission();
}

//get an address of a device
unsigned char SODA::getAddress(uint8_t device, uint8_t addr)
{
    uint8_t rv;
    Wire.beginTransmission(device);
    Wire.write(addr);
    Wire.endTransmission();
    Wire.requestFrom(int(device), int(1));
    rv = Wire.read();
    return rv;
}

//decimal to hex and reverse
int SODA::dectobcd(int val)
{
    return ((val / 10 * 16) + (val % 10));
}

int SODA::bcdtodec(int val)
{
    return ((val / 16 * 10) + (val % 16));
}

//print the contents of the buffer
void SODA::printBuffer(){
	Serial.print(buffer);
}


/////////////////////////////////////////////////////////////////////////////
//@I CLOCK FUNCTIONS 
/////////////////////////////////////////////////////////////////////////////
//@I Set the time by passing values from the array to the clock register
void SODA::setTime(){
   int sYear;
   unsigned char century;
   int i = 0;
   unsigned char aTime;
    if (timeArray[0] > 2000) {
        century = 0x80;
        sYear = timeArray[0] - 2000;
    } else {
        century = 0x00;
        sYear = timeArray[0] - 1900;
    }

    Wire.beginTransmission(CLOCK_I2C_ADDR);
    Wire.write(CLOCK_TIME_CAL_ADDR);
    while(i<=6) {
		if(i==3){ //put in the addition byte for day of week
			aTime = 0x01;
			Wire.write(aTime);
		}
        aTime = dectobcd(timeArray[5-i]);
		if(i==5) aTime = dectobcd(sYear);
        if (i == 4) aTime += century;
        Wire.write(aTime);
		i++;
    }
    Wire.endTransmission();
}

//loads current time into the timeArray
void SODA::getTime(){
    unsigned char century = 0;
    int i = 0;
	unsigned char n;
    int year_full;
    Wire.beginTransmission(CLOCK_I2C_ADDR);
    Wire.write(CLOCK_TIME_CAL_ADDR);
    Wire.endTransmission();
    Wire.requestFrom(CLOCK_I2C_ADDR, 7);
    while(i<6){
		if(i==3){ //skip over day of week
			n = Wire.read();
		}
        n = Wire.read();
        if (i == 4) {
		    century = (n & 0x80) >> 7;
            n = n & 0x1F;
		}
		timeArray[5-i] = bcdtodec(n);
		if(i==5){
			if (century == 1) {
				timeArray[0] = 2000 + timeArray[0];
			} else {
				timeArray[0] = 1900 + timeArray[0];
			}
		}
	i++;
	}
	bufferTime(); //load into character buffer
}

//gets current time and loads it into a char array;
//with format YYYY-MM-DD HH:MM:SS;
void SODA::bufferTime(){
	int spot = 0;
	int needed = 4;
	char spacers[] = "0000-00-00 00:00:00";
	for(int k = 0; k < 6; k++){
		char buff2[4];
		itoa(timeArray[k],buff2,10);
		if(timeArray[k] < 10){
			needed = 1;
			spot++;
		} else{
			needed = 2;
		}
		if(k==0) needed = 4;
		for(int i = 0; i < needed; i++){
			spacers[spot] = buff2[i];
			spot++;
		}
		spot++; //space between each number
	} //end loop through array
	//now load into buffer
	for(int k = 0; k<19; k++){
		buffer[k] = spacers[k];
	}
} //end function

//Set clock using input from the serial monitor
void SODA::serialSetTime(){
	//expects 19=character format YYYY-MM-DD HH:MM:SS
	if(bufferIndex<19) return; //coarse filter for malformed data
	bufferIndex = 0;
	for(int k = 0; k < 6; k++){
		int need = 2;
		if(k==0) need = 4;
		char temp[need];
		for(int i = 0; i < need; i++){ //read value
			temp[i] = buffer[bufferIndex];
			bufferIndex++;
		}
		timeArray[k] = atoi(temp);
		if(k < 5) temp[0] = bufferIndex++; //read spacing characters
		} //end loop through time characters
		setTime(); //set the time using the current time array;
		getTime(); //reload the time
		printBuffer();
}


//set the values in the clock array
void SODA::updateTime(int val,int place){
	if(place > 5) return;  //only 6 places
	timeArray[place] = val;
}

//retrieve values from clock array
int SODA::checkTime(int place){
	if(place>5) return(0);
	return(timeArray[place]);
}

//get temperature value
float SODA::getClockTemp()
{
    float rv;
    uint8_t temp_msb, temp_lsb;
    int8_t nint;
	setAddress(CLOCK_I2C_ADDR,CLOCK_CONTROL_ADDR,CLOCK_SETUP | 0x20); //updates temp
    Wire.beginTransmission(CLOCK_I2C_ADDR);
    Wire.write(CLOCK_TEMPERATURE_ADDR);
    Wire.endTransmission();
    Wire.requestFrom(CLOCK_I2C_ADDR, 2);
    temp_msb = Wire.read();
    temp_lsb = Wire.read() >> 6;
    if ((temp_msb & 0x80) != 0)
        nint = temp_msb | ~((1 << 8) - 1);      // if negative get two's complement
    else
        nint = temp_msb;
    rv = 0.25 * temp_lsb + nint;
    return rv;
}

void SODA::setWake(int val, int valType){
	//valType: 1= secs, 2 = mins, 3=hours
	valType--;
	getTime(); //load current timese
	unsigned char current = getAddress(CLOCK_I2C_ADDR,CLOCK_CONTROL_ADDR); //load current control
	current = current | 0x05;  //make sure alarm1 bits are set
	setAddress(CLOCK_I2C_ADDR,CLOCK_CONTROL_ADDR,current);
	int times[3] = {timeArray[5],timeArray[4],timeArray[3]}; //load current time
	for(int i = 0; i < 3; i++){
		if(i<valType) times[i] = 0;
		if(i==valType){
			int next1 = times[i] % val;
			next1 = val - next1;
			times[i]+= next1;
		}
			if(i<2 && times[i] >= 60){ //push into next increment if needed
				times[i+1] = times[i+1] + 1;
				times[i]-=60;
			}
			if(i==2 & times[i] >= 24) times[i]-= 24;
		}
    //now load values	
    Wire.beginTransmission(CLOCK_I2C_ADDR);
    Wire.write(CLOCK_ALARM1_ADDR);
    for (int i = 0; i <= 3; i++) {
        if (i == 3) {
            Wire.write(0x80);
        } else
			if(i <= valType){ //set so that seconds must match for second increments, minutes + seconds match for minute increments, minute + second + hrs match for hour increments.
				Wire.write(dectobcd(times[i]) & 0x7f);
			}else{
				Wire.write(dectobcd(times[i]) & 0xff);
			}
    }
	delay(100); //give a little time to make sure things are solidified.
    Wire.endTransmission();
	if(getStandby() == 0x01){
		setStandby(0x00);
		turnOff();  //doesn't continue if it was on standby already
	}
}

//turn off the alarm1 flag, thereby going to sleep
void SODA::turnOff(){
	if(getStandby() != 0x00) return; //no need to go through sleep process since it should be turned off next sleep cycle
	unsigned char current = getAddress(CLOCK_I2C_ADDR,CLOCK_ALARM_STATUS);
	current = current & 0xFE;
	setAddress(CLOCK_I2C_ADDR,CLOCK_ALARM_STATUS,current);
	delay(1000);  //just stay here until process turned off
}


//set and get standby
void SODA::setStandby(unsigned char val){ //forces the clock to alarm every second or removes it
	standby = val;
	if(standby < 0x01) return;
#ifndef NO_SD
	if(file.isOpen()) file.close();
#endif
	unsigned char fill = 0x00;
	if(standby != 0x00) fill = 0x80;
	Wire.beginTransmission(CLOCK_I2C_ADDR);
    Wire.write(CLOCK_ALARM1_ADDR);
    for (int i = 0; i <= 3; i++) {
		Wire.write(fill);
    }
    Wire.endTransmission();
	unsigned char current = getAddress(CLOCK_I2C_ADDR,CLOCK_CONTROL_ADDR); //load current control
	current = current | 0x05;  //make sure alarm1 bits are set
	setAddress(CLOCK_I2C_ADDR,CLOCK_CONTROL_ADDR,current);
}

int SODA::getStandby(){
	return standby;
}

///////////////////////////////////////////////////////////////////////////////////
//@I END CLOCK FUNCTIONS
///////////////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////////////
// ADC FUNCTIONS
///////////////////////////////////////////////////////////////////////////////////
long SODA::adcRead(int ch, int bit, int gain){
	unsigned char set1 = ADC_BASE; 
	long reading = 0x00000000;
	long bitVal;
	long vref = 2048000000;
	unsigned char getBytes[4] = {0x00, 0x00, 0x00,0x00};
	if(ch > 4 || ch < 1 || bit < 1 || bit > 4 || gain < 1 || gain > 4) return (-999L);
	//build the control byte
	unsigned char chs[4] = {ADC_CH1,ADC_CH2,ADC_CH3,ADC_CH4};
	unsigned char bits[4] = {ADC_12BITS, ADC_14BITS, ADC_16BITS, ADC_18BITS};
	unsigned char gains[4] = {ADC_GAIN1, ADC_GAIN2, ADC_GAIN4, ADC_GAIN8};
	unsigned char sampleBits[4] = {0x0B,0x0D,0x0F,0x11};
	set1 = (ADC_BASE | chs[ch-1] | bits[bit-1] | gains[gain-1]);
	Wire.beginTransmission(ADC_I2C_ADDR); //set control parameters
    Wire.write(set1);
    Wire.endTransmission();
	long now = millis();
	int j = 2;
	if(bit==4) j = 1;
	while((now + 500L) > millis()){ //allow up to 500 ms to read
		Wire.requestFrom(ADC_I2C_ADDR,(5-j));
		for(int k = j; k<4; k++){
			getBytes[k] = Wire.read();
		}
		unsigned char configByte = Wire.read(); //look for the control flag
		if(!(configByte & 0x80)) break; // break loop if new conversion
	} //end wait for conversion or timeout
	//extend sign bit
	getBytes[0] = getBytes[j] & 0X80 ? 0XFF : 0;
	if(j==2) getBytes[1] =  getBytes[j] & 0X80 ? 0XFF : 0;
	//assemble value
	reading = long(getBytes[0])<<24;
	for(int p = 1; p<4; p++){
		int shift = 8*(3-p);
		reading = reading | (long(getBytes[p]) << shift);
	}
	//convert to volt x 10**9
	vref = vref >> sampleBits[bit-1];
	vref = vref >> (gain - 1);
	reading = reading * vref;
	return(reading);
}
//read thermocouple and return temperature in degrees C
int SODA::tcReadK(int ch){
	//break points for the uV to C and C to uV equations
	long breakuV[3] = {-5800,0,37000};
	long breakC[3] = {-125,0,670};
	//coefficients for the uV to C*10**4 equation
	long a0[4] = {729905,4341,17619,1572949};
	long a1[4] = { 344,202,186,125};
	long a2[4] = {-66,-3873,-155,366};
	//coefficients for the C to uV equations
	long b0[4] = {934459,10309,-88285,-11510582};
	long b1[4] = {65986,51452,53746,78962};
	long b2[4] = {875,406,21,-76};
	long refPV = long(getClockTemp());
	long currentuV = adcRead(1,4,4)/1000L;
  //figure out which segment to use for temp to uV
  int seg = 0;
  for(int k = 0; k < 3; k++){
    if(refPV > breakC[k]) seg++;
  }
  refPV = (b0[seg] + b1[seg]*refPV + b2[seg]*refPV*refPV)/1000;
  currentuV = currentuV + refPV;
   //figure out which segment to use for uv to temp
  seg = 0;
  for(int k = 0; k < 3; k++){
    if(currentuV > breakuV[k]) seg++;
  } 
  currentuV = (a0[seg] + a1[seg]*currentuV + a2[seg]*(currentuV/1000L)*(currentuV/1000L));
  currentuV = currentuV/10000L;
  return(int(currentuV));
}

//Average analogRead across 5 measurements to reduce noise
int SODA::smoothAnalogRead(int pin1){
	int allRead = 0;
	//dump first reading;
	allRead = analogRead(pin1);
	delay(10);
	allRead = 0;
	for(int k = 0; k <5; k++){
		allRead+= analogRead(pin1);
		delay(5);
	}
	allRead = allRead/5;
	return(allRead);
}

//////////////////////////////////////////////////////////////////////////
//@I SD Card Functions
//////////////////////////////////////////////////////////////////////////
//run the SD card
void SODA::dataLineBegin(boolean binary, boolean set_end_on_connect, boolean set_single_file, int sd_cs_pin){
	getTime();
	long loggerID = getID();
	end_on_connect = set_end_on_connect;
	single_file = set_single_file;
	if(usbConnected()==false || end_on_connect == false){ //only open file if not intending to stop
#ifndef NO_SD
		digitalWrite(LEDPIN,HIGH);
		card.begin(sd_cs_pin,SPI_HALF_SPEED);
		if(single_file){
			file.open(FILENAME, O_CREAT | O_APPEND | O_WRITE | O_READ);
		}else{
			char nextDir[] = "D000";
			if(!card.exists(nextDir)) card.mkdir(nextDir); //find the next file within the directory
		     card.chdir(nextDir);
			 char nextFile[] = "F001.DAT";
			 int k = 0;
			 while(card.exists(nextFile) && k < 501){
				 k++;
				incrementName(nextFile,k);
			 }
			 if(k==500){ //rename the current directory to the next available directory and write the final file there
				card.chdir();
				int m = 1;
				while(card.exists(nextDir)){
					incrementName(nextDir,m);
					m++;
				}
				card.rename("D000",nextDir);
				card.chdir(nextDir);
			 }
			file.open(nextFile, O_CREAT | O_APPEND | O_WRITE | O_READ);
			getTime(); //the file generation process can take a long time so it makes sense to update the time before writing.
		}
		if(file.isOpen()) {
			if(binary==false){
				file.print(loggerID);
				file.print(',');
				file.print(buffer);
			}else{ //Save ID as long but still set time as character since the format is known (YYYY-MM-DD HH:MM:SS = 19 characters) 
				dataLineAddBytes(&loggerID,4);     
				file.print(buffer);
			}
			return;
		}
#endif
	} //end file open and write block
	Serial.print(loggerID);
	Serial.print(',');
	Serial.print(buffer);
}
//function to update directory or filename
void SODA::incrementName(char* inChar, int newVal, int nameLength){
	int need = int(log10(newVal))+1;
	char tempChar[need + 1]; //remember the terminal character!!!!
	ltoa(newVal,tempChar,10);
	for(int j = 0;j<need;j++){
			inChar[nameLength-need+j] = tempChar[j];
		}
}

//Write and Int
void SODA::dataLineAdd(int value){
#ifndef NO_SD
	testForConnection();
	if(file.isOpen()) {
		file.print(',');
		file.print(value);
		return;
	}
#endif
	Serial.print(',');
	Serial.print(value);
}

//Write a long
void SODA::dataLineAdd(long value){
#ifndef NO_SD
	testForConnection();
	if(file.isOpen()) {
		file.print(',');
		file.print(value);
		return;
	}
#endif
	Serial.print(',');
	Serial.print(value);
}

//write a float
void SODA::dataLineAdd(float value){
#ifndef NO_SD
	testForConnection();
	if(file.isOpen()) {
		file.print(',');
		file.print(value);
		return;
	}
#endif
	Serial.print(',');
	Serial.print(value);
}

//write as raw bytes
void SODA::dataLineAddBytes(const void* buffer, int nbytes){
#ifndef NO_SD
	testForConnection();
	if(file.isOpen()) {
		file.write(buffer, nbytes);
		return;
	}
#endif
	return;
}


void SODA::dataLineEnd(){
#ifndef NO_SD
	if(file.isOpen()) {
		file.print("\r\n"); //end of a line
		file.sync();  //make sure the file is updated before closing
		file.close();
		digitalWrite(LEDPIN,LOW);
		delay(200); //give SD card some time to finish writing
		return;
	}
#endif
	Serial.print("\r\n"); //end of a line
	digitalWrite(LEDPIN,LOW);
}

void SODA::dataDownload(){
#ifndef NO_SD
	if(!file.isOpen()) file.open(FILENAME, O_CREAT | O_APPEND | O_WRITE | O_READ);
	char data;
	unsigned long nBytes = 0;
    while ((data = file.read()) >= 0) Serial.print(char(data));
	file.close();
	delay(100);
#endif
}

//handle interruptions while file is writting and USB is connected.
void SODA::testForConnection(){
	if(usbConnected() && end_on_connect){
		dataLineEnd();
		blinks(3);
		communicate();
	}
	return;
}

////////////////////////////////////////////////////////////////////////
// @I Communication Functions
//////////////////////////////////////////////////////////////////////
// command format = "[cXXXXXXXXX]" 
//where [,] indicate beginning and end of a command
//c = command
//XXXXXXXXX = auxillary information
void SODA::communicate(){  
	char aChar = ' ';
	char command = ' ';
	if(!usbConnected()) return;
	if(getStandby()==0x02) Serial.print(']'); //indicates that a reading was made. This character ends the reading.
	if(getStandby() != 0x01) setStandby(0x01);  ///set standby so the clock will be reset properly
	while(usbConnected() ){
		while(Serial.available() && aChar != ']'){
			aChar = Serial.read();
			if(aChar=='['){ //begin a new command
				command = Serial.read();
				bufferIndex = 0;
			}else{
				if(aChar==']') continue;
				buffer[bufferIndex] = aChar;
				bufferIndex++;
				if(bufferIndex>19) bufferIndex = 0; //don't overrun the buffer
			}
		delay(5); //wait for more chars
		}
		if(command!=' ') {
			if(command == 'R'){
				setStandby(0x02);
				Serial.print("[R");
				break; //allows the rest of the sketch to run;
			}
			parseCommand(command);
			command = ' ';
			aChar = ' ';
		}
	}
}

void SODA::parseCommand(char command){
	Serial.print('[');
	Serial.print(command);
	switch (command) {
    case 'T':
      serialSetTime();
      break;
    case 't':
      	getTime(); //reload the time
		printBuffer();
      break;
	case 'I':
		Serial.print(getID());
	break;
	case 'D':
		Serial.print("]"); //don't change this to println.  The CRLF will mess things up
		dataDownload();
	break;
      // default is optional
	}
	Serial.println(']');
}


//////////////////////////////////////////////////////////////////////////
//@I MISC FUNCTION  
/////////////////////////////////////////////////////////////////////////
//set an identifer for the board as a long in non-volatile memory
void SODA::setID(long ID){
	 uint8_t *p = (uint8_t *)&ID;
	 for(int k = 0; k<4; k++){
		 EEPROM.write(k,p[k]);
	 }
}

long SODA::getID(){
	long ID = 0L;
	for(int k = 3; k > - 1; k--){
		ID = ID | EEPROM.read(k)<<(k*8);
	}
	return ID;
}

//test to see if the usb is connected
bool SODA::usbConnected(){
	return(digitalRead(0));
}

//blink LED n times
void SODA::blinks(int n){
	pinMode(LEDPIN,OUTPUT);
	for(int i = 0; i < n; i++){
		digitalWrite(LEDPIN,HIGH);
		delay(100);
		digitalWrite(LEDPIN,LOW);
		delay(100);
	}
	pinMode(LEDPIN,INPUT);
}












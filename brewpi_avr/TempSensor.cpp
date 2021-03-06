/*
 * Copyright 2012 BrewPi/Elco Jacobs.
 *
 * This file is part of BrewPi.
 * 
 * BrewPi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * BrewPi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with BrewPi.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "TempSensor.h"
#include "OneWire.h"
#include "DallasTemperature.h"
#include "PiLink.h"
#include <limits.h>

void TempSensor::init(void){
	// give reset pulse to temp sensors
	oneWire->reset();

	// get sensor address
	if (!sensor->getAddress(sensorAddress, 0)){
		//error
		piLink.debugMessage(PSTR("Unable to find address for sensor on pin %d"), pin);
	}
	sensor->setResolution(sensorAddress, 12);
		
	sensor->requestTemperatures();
	lastRequestTime = millis();
	delay(750); // delay 750ms for conversion time
	fixed7_9 temperature = DEVICE_DISCONNECTED;
	while(temperature == DEVICE_DISCONNECTED){
		temperature = sensor->getTempRaw(sensorAddress);
		if(millis() - lastRequestTime > 2000){
			connected = false; // sensor disconnected
			return;
		}
	}
	sensor->requestTemperatures();
	delay(750);
	temperature = sensor->getTempRaw(sensorAddress); // read again. First read is not accurate
	connected = true;
	temperature = constrain(temperature, ((int) INT_MIN)>>5, ((int) INT_MAX)>>5)<<5; // sensor returns 12 bits with 4 fraction bits. Store with 9 fraction bits
	fastFilter.init(temperature);
	slowFilter.init(temperature);
	slopeFilter.init(0);
	prevOutputForSlope = slowFilter.readOutputDoublePrecision();
}

bool TempSensor::isConnected(void){
	return connected;
}

void TempSensor::update(void){
	if((millis()-lastRequestTime) > 5000){ // if last request is longer than 5 seconds ago, request again and delay
		sensor->requestTemperatures();
		lastRequestTime = millis();
		delay(750); // wait 750 ms (18B20 max conversion time)
	}
	fixed7_9 temperature = sensor->getTempRaw(sensorAddress);
	if(temperature == DEVICE_DISCONNECTED){
		// device disconnected. Don't update filters.  Log a debug message.
		if(connected == true){
			piLink.debugMessage(PSTR("Temperature sensor on pin %d disconnected"), pin);
		}			
		connected = false;
		return;
	}
	else{
		if(connected == false){
			delay(2000); // delay for two seconds to be sure sensor is correctly inserted
			init(); // was disconnected, initialize again
			piLink.debugMessage(PSTR("Temperature sensor on pin %d reconnected"), pin);
			temperature = sensor->getTempRaw(sensorAddress); // re-read temperature after proper initialization
		}
	}
	temperature = constrain(temperature, ((int) INT_MIN)>>5, ((int) INT_MAX)>>5)<<5; // sensor returns 12 bits with 4 fraction bits. Store with 9 fraction bits
	
		
	fastFilter.add(temperature);
	slowFilter.add(temperature);
	
	// update slope filter every 30 samples.
	// averaged differences will give the slope. Use the slow filter as input
	updateCounter--;
	// initialize first read for slope filter after (255-31) seconds. This prevents an influence for the startup inaccuracy.
	if(updateCounter == 31){
		// only happens once after startup.
		prevOutputForSlope = slowFilter.readOutputDoublePrecision();
	}
	if(updateCounter == 0){
		slopeFilter.addDoublePrecision(slowFilter.readOutputDoublePrecision() - prevOutputForSlope);
		prevOutputForSlope = slowFilter.readOutputDoublePrecision();
		updateCounter = 30;
	}
		
	// already send request for next read
	sensor->requestTemperatures();
	lastRequestTime = millis();
}

fixed7_9 TempSensor::read(void){
	return fastFilter.readInput(); //return most recent unfiltered value
}

fixed7_9 TempSensor::readFastFiltered(void){
	return fastFilter.readOutput(); //return most recent unfiltered value
}

fixed7_9 TempSensor::readSlowFiltered(void){
	return slowFilter.readOutput(); //return most recent unfiltered value
}

fixed7_9 TempSensor::readSlope(void){
	// return slope per hour. Multiply by 120 (1h/30s), shift to single precision
	fixed7_25 doublePrecision = slopeFilter.readOutputDoublePrecision();
	return (doublePrecision*120)>>16;
}

fixed7_9 TempSensor::detectPosPeak(void){
	return slowFilter.detectPosPeak();
}

fixed7_9 TempSensor::detectNegPeak(void){
	return slowFilter.detectNegPeak();
}

void TempSensor::setFastFilterCoefficients(uint16_t ab){
	fastFilter.setCoefficients(ab);
}

void TempSensor::setSlowFilterCoefficients(uint16_t ab){
	slowFilter.setCoefficients(ab);
}

void TempSensor::setSlopeFilterCoefficients(uint16_t ab){
	slopeFilter.setCoefficients(ab);
}
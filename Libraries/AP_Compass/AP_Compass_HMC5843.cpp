// -*- tab-width: 4; Mode: C++; c-basic-offset: 3; indent-tabs-mode: t -*-
/*
	AP_Compass_HMC5843.cpp - Arduino Library for HMC5843 I2C magnetometer
	Code by Jordi Muñoz and Jose Julio. DIYDrones.com

	This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

	Sensor is conected to I2C port
	Sensor is initialized in Continuos mode (10Hz)

*/

//#include <wirish.h>
//#include <math.h>
//
//#include <I2C.h>
#include "AP_Compass_HMC5843.h"

static FastSerial *serPort;

#ifdef AP_COMPASS_HMC5843_DEBUG_ENABLE
#pragma message "*** AP_COMPASS_HMC5843 Debug Enabled ***"
#define debug(fmt, args...) do { if (serPort != NULL) { serPort->printf("%s:%d: " fmt "\n", __FUNCTION__, __LINE__ , ##args); delay(100); } } while(0)
#else
#define debug(fmt, args...)
#endif
#define notify(fmt, args...) do { if (serPort != NULL) { serPort->printf(fmt, ##args); delay(100); } } while(0)

#define COMPASS_ADDRESS      0x1E // 0x3C
#define ConfigRegA           0x00
#define ConfigRegB           0x01
#define magGain              0x20
#define PositiveBiasConfig   0x11
#define NegativeBiasConfig   0x12
#define NormalOperation      0x10
#define ModeRegister         0x02
#define ContinuousConversion 0x00
#define SingleConversion     0x01

// ConfigRegA valid sample averaging for 5883L
#define SampleAveraging_1    0x00
#define SampleAveraging_2    0x01
#define SampleAveraging_4    0x02
#define SampleAveraging_8    0x03

// ConfigRegA valid data output rates for 5883L
#define DataOutputRate_0_75HZ 0x00
#define DataOutputRate_1_5HZ  0x01
#define DataOutputRate_3HZ    0x02
#define DataOutputRate_7_5HZ  0x03
#define DataOutputRate_15HZ   0x04
#define DataOutputRate_30HZ   0x05
#define DataOutputRate_75HZ   0x06

	// Constructor
AP_Compass_HMC5843::AP_Compass_HMC5843(HardwareI2C *i2c_d, FastSerial *ser_port) :
	Compass(),
	_I2Cx(i2c_d)
{
	serPort = ser_port;
}

// read_register - read a register value
bool AP_Compass_HMC5843::read_register(uint8_t address, uint8_t *value)
{
	serPort->printf("Read %02x ", address);
   if (_I2Cx->read((uint8_t)COMPASS_ADDRESS, address, 1, value) != 0) {

	   serPort->println("FAILED");
	  healthy = false;
	  return false;
   }
   serPort->println("OK");
   return true;
}

// write_register - update a register value
bool AP_Compass_HMC5843::write_register(uint8_t address, uint8_t value)
{
	serPort->printf("Write %02x %02x", address, value);
   if (_I2Cx->write((uint8_t)COMPASS_ADDRESS, address, value) != 0) {
	   serPort->println("FAILED");


	  healthy = false;
	  return false;
   }
   serPort->println("OK");
   return true;
}

// Read Sensor data
bool AP_Compass_HMC5843::read_raw()
{
   uint8_t buff[6];

   //serPort->println("Read RAW ");

  if (_I2Cx->read((uint8_t)COMPASS_ADDRESS, (uint8_t)0x03, (uint8_t)6, (uint8_t *)buff) != 0) {
	  healthy = false;
	  return false;
   }
/*
   for (int c = 0; c < 6; c++)
   {
	 //  serPort->printf("Read RAW %d\r\n", c);
	   if (_I2Cx->read((uint8_t)COMPASS_ADDRESS, (uint8_t)(0x03 + c), (uint8_t)1, (uint8_t *) (buff + c)) != 0) {
	   	  healthy = false;
	   	  return false;
	   }
   }*/

   int16_t rx, ry, rz;
   rx = (int16_t)(buff[0] << 8) | buff[1];
   if (product_id == AP_COMPASS_TYPE_HMC5883L) {
	  rz = (int16_t)(buff[2] << 8) | buff[3];
	  ry = (int16_t)(buff[4] << 8) | buff[5];
   } else {
	  ry = (int16_t)(buff[2] << 8) | buff[3];
	  rz = (int16_t)(buff[4] << 8) | buff[5];
   }
   if (rx == -4096 || ry == -4096 || rz == -4096) {
	  // no valid data available
	  return false;
   }

   _mag_x = -rx;
   _mag_y = ry;
   _mag_z = -rz;
   
   return true;
}


// accumulate a reading from the magnetometer
void AP_Compass_HMC5843::accumulate(void)
{
   uint32_t tnow = micros();
   if (healthy && _accum_count != 0 && (tnow - _last_accum_time) < 13333) {
	  // the compass gets new data at 75Hz
	  return;
   }
   if (read_raw()) {
	  // the _mag_N values are in the range -2048 to 2047, so we can
	  // accumulate up to 15 of them in an int16_t. Let's make it 14
	  // for ease of calculation. We expect to do reads at 10Hz, and
	  // we get new data at most 75Hz, so we don't expect to
	  // accumulate more than 8 before a read
	  _mag_x_accum += _mag_x;
	  _mag_y_accum += _mag_y;
	  _mag_z_accum += _mag_z;
	  _accum_count++;
	  if (_accum_count == 14) {
		 _mag_x_accum /= 2;
		 _mag_y_accum /= 2;
		 _mag_z_accum /= 2;
		 _accum_count = 7;
	  }
	  _last_accum_time = tnow;
   }
}


/*
  re-initialise after a IO error
 */
bool AP_Compass_HMC5843::re_initialise()
{
   if (! write_register(ConfigRegA, _base_config) ||
	   ! write_register(ConfigRegB, magGain) ||
	   ! write_register(ModeRegister, ContinuousConversion))
	  return false;
   return true;
}


// Public Methods //////////////////////////////////////////////////////////////
bool
AP_Compass_HMC5843::init(void)
{
  int numAttempts = 0, good_count = 0;
  bool success = false;
  byte calibration_gain = 0x20;
  uint16_t expected_x = 715;
  uint16_t expected_yz = 715;
  float gain_multiple = 1.0;

  delay(10);

  // determine if we are using 5843 or 5883L
  if (! write_register(ConfigRegA, SampleAveraging_8<<5 | DataOutputRate_75HZ<<2 | NormalOperation)) {
	  serPort->println("Write config error\r\n");
	 healthy = false;
	 return false;
  }

  if (! read_register(ConfigRegA, &_base_config)) {
	  serPort->println("Read config error\r\n");
	 healthy = false;
	 return false;
  }
  if ( _base_config == (SampleAveraging_8<<5 | DataOutputRate_75HZ<<2 | NormalOperation)) {
	 // a 5883L supports the sample averaging config
	 product_id = AP_COMPASS_TYPE_HMC5883L;
	 calibration_gain = 0x60;
	 expected_x = 766;
	 expected_yz  = 713;
	 gain_multiple = 660.0 / 1090; // adjustment for runtime vs calibration gain
	 serPort->println("AP_COMPASS_TYPE_HMC5883L");
  } else if (_base_config == (NormalOperation | DataOutputRate_75HZ<<2)) {
      product_id = AP_COMPASS_TYPE_HMC5843;
  	  serPort->println("MAG5843");
  } else {
	 // not behaving like either supported compass type
	 serPort->println("NO MAG");
	 return false;
  }

  calibration[0] = 0;
  calibration[1] = 0;
  calibration[2] = 0;
  
  while ( success == 0 && numAttempts < 20 && good_count < 5)
  {
      // record number of attempts at initialisation
	  numAttempts++;
	  serPort->printf("Attempts: %d \r", numAttempts);

	  // force positiveBias (compass should return 715 for all channels)
	  if (! write_register(ConfigRegA, PositiveBiasConfig))
		 continue; // compass not responding on the bus
	  delay(50);

	  // set gains
	  if (! write_register(ConfigRegB, calibration_gain) ||
		  ! write_register(ModeRegister, SingleConversion))
		 continue;

	  // read values from the compass
	  delay(50);
	  if (!read_raw())
		 continue; // we didn't read valid values

	  delay(10);

	  float cal[3];

	  cal[0] = fabs(expected_x / (float)_mag_x);
	  cal[1] = fabs(expected_yz / (float)_mag_y);
	  cal[2] = fabs(expected_yz / (float)_mag_z);

	  if (cal[0] > 0.7 && cal[0] < 1.3 && 
		  cal[1] > 0.7 && cal[1] < 1.3 && 
		  cal[2] > 0.7 && cal[2] < 1.3) 	  {
		 good_count++;
		 calibration[0] += cal[0];
		 calibration[1] += cal[1];
		 calibration[2] += cal[2];
	  }
  }

  if (good_count >= 5) {
	 calibration[0] = calibration[0] * gain_multiple / good_count;
	 calibration[1] = calibration[1] * gain_multiple / good_count;
	 calibration[2] = calibration[2] * gain_multiple / good_count;
	 success = true;
	 serPort->println("Calibrazione OK");
  } else {
	 /* best guess */
	 calibration[0] = 1.0;
	 calibration[1] = 1.0;
	 calibration[2] = 1.0;
	 serPort->println("best guess :\\");
  }

  // leave test mode
  if (!re_initialise()) {
	 return false;
  }

  _initialised = true;

	// perform an initial read
	healthy = true;
	read();

    return success;
}

// Read Sensor data
bool AP_Compass_HMC5843::read()
{
   if (!_initialised) {
	  // someone has tried to enable a compass for the first time
	  // mid-flight .... we can't do that yet (especially as we won't
	  // have the right orientation!)
	  return false;
   }
   if (!healthy) {
	  if (millis() < _retry_time) {
		 return false;
	  }
	  if (!re_initialise()) {
		 _retry_time = millis() + 1000;
		 _I2Cx->setSpeed(false);
		 return false;
	  }
   }

	if (_accum_count == 0) {
	   accumulate();
	   if (!healthy || _accum_count == 0) {
	  // try again in 1 second, and set I2c clock speed slower
	  _retry_time = millis() + 1000;
	  _I2Cx->setSpeed(false);
	  return false;
      }
	}

	mag_x = _mag_x_accum * calibration[0] / _accum_count;
	mag_y = _mag_y_accum * calibration[1] / _accum_count;
	mag_z = _mag_z_accum * calibration[2] / _accum_count;
	_accum_count = 0;
	_mag_x_accum = _mag_y_accum = _mag_z_accum = 0;

   last_update = micros();  // record time of update

   // rotate to the desired orientation
   Vector3f rot_mag = Vector3f(mag_x,mag_y,mag_z);
   rot_mag.rotate(_orientation);

   rot_mag += _offset.get();
   mag_x = rot_mag.x;
   mag_y = rot_mag.y;
   mag_z = rot_mag.z;
   healthy = true;


//   //TEO 20130827
//   //introduco una correzione di prova per gestire correttamente gli assi di BruGi
//   int16_t tmp = mag_x;
//   mag_x = mag_y;
//   mag_y = tmp;
//   mag_z = -mag_z;


   return true;
}

// set orientation
void
AP_Compass_HMC5843::set_orientation(enum Rotation rotation)
{
   _orientation = rotation;
}
